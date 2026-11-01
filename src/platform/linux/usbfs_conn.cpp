/*
 * Copyright (c) 2026 Gabriel2392
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "platform/linux/usbfs_conn.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>

#include <linux/usbdevice_fs.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <spdlog/spdlog.h>

namespace brokkr::linux {

static_assert(UsbFsConnection::MAX_BULK_IOCTL_BYTES % 1024 == 0);

namespace {

constexpr std::size_t kMaxQueuedBytes = 1024 * 1024;
constexpr std::size_t kMaxQueuedUrbs = kMaxQueuedBytes / UsbFsConnection::MAX_BULK_IOCTL_BYTES;
static_assert(kMaxQueuedBytes % UsbFsConnection::MAX_BULK_IOCTL_BYTES == 0);

struct BulkResult {
  std::size_t bytes = 0;
  int error = 0;
  bool short_read = false;
  bool disconnected = false;
  bool fatal = false;
};

bool is_disconnect_errno(int e) noexcept { return e == ENODEV || e == ESHUTDOWN || e == ENOENT; }

int usbfs_ioctl(int fd, unsigned long request, void* arg) noexcept {
  int rc;
  do {
    rc = ::ioctl(fd, request, arg);
  } while (rc < 0 && errno == EINTR);
  return rc;
}

int ms_until(std::chrono::steady_clock::time_point deadline) noexcept {
  const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
  if (left <= 0) return 0;
  return static_cast<int>(std::min<long long>(left, std::numeric_limits<int>::max()));
}

BulkResult bulk_transfer(int fd, std::uint32_t caps, std::uint8_t ep, std::uint8_t* data, std::size_t len, bool in,
                         int timeout_ms) noexcept {
  BulkResult res;
  if (len == 0) return res;

  const bool scatter_gather = (caps & USBDEVFS_CAP_BULK_SCATTER_GATHER) != 0;
  const bool continuation = (caps & USBDEVFS_CAP_BULK_CONTINUATION) != 0;
  const std::size_t piece = scatter_gather ? std::min(len, kMaxQueuedBytes) : UsbFsConnection::MAX_BULK_IOCTL_BYTES;
  const std::size_t span = (scatter_gather || continuation) ? std::min(len, kMaxQueuedBytes) : std::min(len, piece);
  const std::size_t count = (span + piece - 1) / piece;

  std::array<usbdevfs_urb, kMaxQueuedUrbs> urbs{};
  for (std::size_t i = 0; i < count; ++i) {
    usbdevfs_urb& u = urbs[i];
    const std::size_t off = i * piece;
    u.type = USBDEVFS_URB_TYPE_BULK;
    u.endpoint = ep;
    u.buffer = data + off;
    u.buffer_length = static_cast<int>(std::min(piece, span - off));
    if (i > 0) u.flags |= USBDEVFS_URB_BULK_CONTINUATION;
    if (in && i + 1 < count) u.flags |= USBDEVFS_URB_SHORT_NOT_OK;
  }

  int submit_error = 0;
  std::size_t submitted = 0;
  while (submitted < count) {
    if (usbfs_ioctl(fd, USBDEVFS_SUBMITURB, &urbs[submitted]) < 0) {
      submit_error = errno;
      break;
    }
    ++submitted;
  }
  if (scatter_gather && submitted == 0 && submit_error == ENOMEM)
    return bulk_transfer(fd, caps & ~USBDEVFS_CAP_BULK_SCATTER_GATHER, ep, data, len, in, timeout_ms);

  std::array<bool, kMaxQueuedUrbs> reaped{};
  std::size_t pending = submitted;
  int cancel_error = 0;
  bool blocking = false;
  const auto timeout = std::chrono::milliseconds(timeout_ms);
  auto deadline = std::chrono::steady_clock::now() + timeout;

  while (pending > 0) {
    void* completed = nullptr;
    if (usbfs_ioctl(fd, blocking ? USBDEVFS_REAPURB : USBDEVFS_REAPURBNDELAY, &completed) == 0) {
      std::size_t idx = 0;
      while (idx < submitted && completed != &urbs[idx]) ++idx;
      if (idx == submitted || reaped[idx]) {
        spdlog::error("usbfs: reaped a URB that was not pending");
        res.fatal = true;
        continue;
      }
      reaped[idx] = true;
      --pending;
      deadline = std::chrono::steady_clock::now() + timeout;
      continue;
    }

    const int reap_error = errno;
    if (reap_error != EAGAIN) {
      if (reap_error != ENODEV)
        spdlog::error("usbfs: reaping URBs failed: {} ({})", std::strerror(reap_error), reap_error);
      res.disconnected = true;
      res.error = reap_error;
      return res;
    }

    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLOUT;
    const int pr = ::poll(&pfd, 1, timeout_ms > 0 ? ms_until(deadline) : -1);
    if (pr <= 0) {
      if (pr < 0 && errno == EINTR) continue;
      cancel_error = pr == 0 ? ETIMEDOUT : errno;
      blocking = true;
      for (std::size_t i = submitted; i-- > 0;)
        if (!reaped[i]) (void)usbfs_ioctl(fd, USBDEVFS_DISCARDURB, &urbs[i]);
      continue;
    }
    if (pfd.revents & (POLLERR | POLLHUP)) blocking = true;
  }

  std::size_t first_incomplete = submitted;
  for (std::size_t i = 0; i < submitted; ++i) {
    const usbdevfs_urb& u = urbs[i];
    const std::size_t actual = u.actual_length > 0 ? static_cast<std::size_t>(u.actual_length) : 0;
    res.bytes += actual;
    if (u.status == 0 && actual == static_cast<std::size_t>(u.buffer_length)) continue;

    first_incomplete = i;
    if (in && (u.status == 0 || u.status == -EREMOTEIO)) {
      res.short_read = true;
    } else if (u.status == 0) {
      spdlog::error("bulk OUT transferred {} of {} bytes", actual, u.buffer_length);
      res.fatal = true;
    } else if (u.status == -ENOENT || u.status == -ECONNRESET) {
      res.error = cancel_error != 0 ? cancel_error : ENODEV;
      res.disconnected = cancel_error == 0;
    } else {
      res.error = -u.status;
      res.disconnected = u.status == -ESHUTDOWN || u.status == -ENODEV;
    }
    break;
  }

  for (std::size_t i = first_incomplete + 1; i < submitted; ++i) {
    if (urbs[i].actual_length > 0) {
      spdlog::error("usbfs: data moved after an incomplete URB");
      res.fatal = true;
      break;
    }
  }

  if (first_incomplete == submitted && submit_error != 0) {
    res.error = submit_error;
    res.disconnected = is_disconnect_errno(submit_error);
  }
  return res;
}

} // namespace

UsbFsConnection::UsbFsConnection(UsbFsDevice& dev) : dev_(dev) {}

void UsbFsConnection::set_packet_size_hint(std::size_t bytes) noexcept {
  (void)bytes;
}

brokkr::core::Status UsbFsConnection::open() noexcept {
  if (connected_) return {};
  if (!dev_.is_open()) return brokkr::core::fail("UsbFsConnection::open: device not open");

  connected_ = true;
  zlp_needed_ = true;
  return {};
}

void UsbFsConnection::close() noexcept { connected_ = false; }

int UsbFsConnection::send(std::span<const std::uint8_t> data, unsigned retries) {
  if (!connected_) return -1;

  const auto eps = dev_.endpoints();
  if (eps.bulk_out == 0) return -1;

  auto* const base = const_cast<std::uint8_t*>(data.data());
  std::size_t done = 0;
  unsigned attempt = 0;

  while (done < data.size()) {
    const BulkResult r = bulk_transfer(dev_.fd(), dev_.caps(), eps.bulk_out, base + done, data.size() - done, false,
                                       timeout_ms_);
    done += r.bytes;
    if (r.disconnected) {
      spdlog::info("Device disconnected");
      connected_ = false;
      return -1;
    }
    if (r.fatal) return -1;
    if (r.error == 0) continue;

    if (r.bytes > 0) attempt = 0;
    if (++attempt > retries) {
      spdlog::error("bulk OUT failed: {} ({}), retries exhausted", std::strerror(r.error), r.error);
      return -1;
    }
    spdlog::warn("bulk OUT failed: {} ({}), retrying (attempt {}/{})", std::strerror(r.error), r.error, attempt,
                 retries);
    ::usleep(10'000);
  }

  if (zlp_needed_) {
    usbdevfs_bulktransfer zlp{};
    zlp.ep = eps.bulk_out;
    zlp.timeout = 100;
    zlp.len = 0;
    zlp.data = nullptr;

    const int rc = ::ioctl(dev_.fd(), USBDEVFS_BULK, &zlp);
    if (rc != 0) zlp_needed_ = false;
  }

  return static_cast<int>(done);
}

int UsbFsConnection::recv_zlp(unsigned /*retries*/) {
  if (!connected_) return -1;

  const auto eps = dev_.endpoints();
  if (eps.bulk_in == 0) return -1;

  std::array<std::uint8_t, 1024> buf;
  usbdevfs_bulktransfer zlp{};
  zlp.ep = eps.bulk_in;
  zlp.timeout = 10;
  zlp.len = buf.size();
  zlp.data = buf.data();
  (void)::ioctl(dev_.fd(), USBDEVFS_BULK, &zlp);
  return 0;
}

int UsbFsConnection::recv(std::span<std::uint8_t> data, unsigned retries) {
  if (!connected_) return -1;

  const auto eps = dev_.endpoints();
  if (eps.bulk_in == 0) return -1;

  if (data.size() == 0) return recv_zlp();

  std::size_t done = 0;
  unsigned attempt = 0;

  while (done < data.size()) {
    const BulkResult r = bulk_transfer(dev_.fd(), dev_.caps(), eps.bulk_in, data.data() + done, data.size() - done,
                                       true, timeout_ms_);
    done += r.bytes;
    if (r.disconnected) {
      spdlog::info("Device disconnected");
      connected_ = false;
      return -1;
    }
    if (r.fatal) return -1;
    if (r.error == 0) {
      if (r.short_read) {
        spdlog::debug("UsbFs recv: short read ({} of {} bytes)", done, data.size());
        break;
      }
      continue;
    }

    if (r.bytes > 0) attempt = 0;
    if (++attempt > retries) {
      spdlog::error("bulk IN failed: {} ({}), retries exhausted", std::strerror(r.error), r.error);
      return -1;
    }
    spdlog::warn("bulk IN failed: {} ({}), retrying (attempt {}/{})", std::strerror(r.error), r.error, attempt, retries);
    ::usleep(10'000);
  }

  return static_cast<int>(done);
}

} // namespace brokkr::linux
