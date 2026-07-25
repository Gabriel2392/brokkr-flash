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

#include "platform/android/libusb_transport.hpp"

#include <libusb.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <thread>
#include <utility>

namespace brokkr::android_platform {

namespace {

constexpr std::size_t kDefaultChunkBytes = 128 * 1024;
constexpr std::chrono::milliseconds kRetryBackoff{10};
constexpr int kZlpProbeTimeoutMs = 100;
constexpr std::size_t kMaxChunkBytes = 1 * 1024 * 1024;
constexpr int kZlpRecvTimeoutMs = 10;
constexpr int kCancelPollMs = 250;

std::string libusb_strerror_pair(int rc) {
  return std::format("{} ({})", libusb_error_name(rc), libusb_strerror(rc));
}

bool is_bulk_in_addr(std::uint8_t addr) noexcept { return (addr & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN; }
bool is_bulk_out_addr(std::uint8_t addr) noexcept { return (addr & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_OUT; }

} // namespace

std::expected<std::unique_ptr<LibusbContext>, std::string> LibusbContext::create() {
  libusb_context* ctx = nullptr;
  libusb_init_option options[1] = {};
  options[0].option = LIBUSB_OPTION_NO_DEVICE_DISCOVERY;
  const int rc = libusb_init_context(&ctx, options, 1);
  if (rc != 0 || ctx == nullptr) {
    return std::unexpected{
        std::format("libusb_init_context failed: {} ({})", libusb_error_name(rc), libusb_strerror(rc))};
  }
  return std::unique_ptr<LibusbContext>(new LibusbContext(ctx));
}

LibusbContext::~LibusbContext() {
  if (ctx_ != nullptr) {
    libusb_exit(ctx_);
    ctx_ = nullptr;
  }
}

void LibusbContext::interrupt_event_handler() noexcept {
  if (ctx_ != nullptr) libusb_interrupt_event_handler(ctx_);
}

LibusbUsbTransport::LibusbUsbTransport(libusb_device_handle* handle, OpenParams params) noexcept
    : handle_(handle),
      interface_number_(params.interface_number),
      in_ep_(params.bulk_in_addr),
      out_ep_(params.bulk_out_addr),
      fd_(params.fd),
      id_(std::move(params.id)),
      external_cancel_(params.external_cancel),
      chunk_bytes_(kDefaultChunkBytes) {}

LibusbUsbTransport::~LibusbUsbTransport() { close_now(); }

std::expected<std::unique_ptr<LibusbUsbTransport>, std::string> LibusbUsbTransport::open(OpenParams params) {
  if (params.context == nullptr || params.context->raw() == nullptr) {
    return std::unexpected{"libusb context not provided"};
  }
  if (params.fd < 0) return std::unexpected{"invalid USB file descriptor"};
  if (params.interface_number < 0) return std::unexpected{"invalid USB interface number"};
  if (!is_bulk_in_addr(params.bulk_in_addr)) {
    return std::unexpected{std::format("invalid bulk IN endpoint 0x{:02x}", params.bulk_in_addr)};
  }
  if (!is_bulk_out_addr(params.bulk_out_addr)) {
    return std::unexpected{std::format("invalid bulk OUT endpoint 0x{:02x}", params.bulk_out_addr)};
  }

  libusb_device_handle* handle = nullptr;
  const int wrap_rc =
      libusb_wrap_sys_device(params.context->raw(), static_cast<intptr_t>(params.fd), &handle);
  if (wrap_rc != 0 || handle == nullptr) {
    return std::unexpected{std::format("libusb_wrap_sys_device failed: {}", libusb_strerror_pair(wrap_rc))};
  }

  (void)libusb_set_auto_detach_kernel_driver(handle, 1);

  const int claim_rc = libusb_claim_interface(handle, params.interface_number);
  if (claim_rc != 0) {
    libusb_close(handle);
    return std::unexpected{
        std::format("libusb_claim_interface({}) failed: {}", params.interface_number, libusb_strerror_pair(claim_rc))};
  }

  auto transport = std::unique_ptr<LibusbUsbTransport>(new LibusbUsbTransport(handle, std::move(params)));
  return transport;
}

void LibusbUsbTransport::close_now() noexcept {
  if (!connected_.exchange(false, std::memory_order_acq_rel)) return;

  std::lock_guard<std::mutex> lock(io_mutex_);
  if (handle_ == nullptr) return;

  const int rel_rc = libusb_release_interface(handle_, interface_number_);
  if (rel_rc != 0 && rel_rc != LIBUSB_ERROR_NO_DEVICE && rel_rc != LIBUSB_ERROR_NOT_FOUND) {
    spdlog::debug("libusb_release_interface({}) returned {}", interface_number_, libusb_strerror_pair(rel_rc));
  }
  libusb_close(handle_);
  handle_ = nullptr;
}

void LibusbUsbTransport::set_packet_size_hint(std::size_t bytes) noexcept {
  if (bytes == 0) return;
  const auto capped = std::clamp<std::size_t>(bytes, 1, kMaxChunkBytes);
  chunk_bytes_.store(capped, std::memory_order_relaxed);
}

void LibusbUsbTransport::set_timeout_ms(int ms) noexcept {
  timeout_ms_.store(ms <= 0 ? 1 : ms, std::memory_order_relaxed);
}

int LibusbUsbTransport::send(std::span<const std::uint8_t> data, unsigned retries) {
  if (!connected_.load(std::memory_order_acquire)) return -1;

  std::lock_guard<std::mutex> lock(io_mutex_);
  if (handle_ == nullptr) return -1;

  const std::size_t total = data.size();
  const auto* base = data.data();
  const std::size_t chunk_cap = chunk_bytes_.load(std::memory_order_relaxed);
  const int chunk_timeout = timeout_ms_.load(std::memory_order_relaxed);

  std::size_t sent = 0;
  unsigned attempt = 0;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(chunk_timeout);

  while (sent < total) {
    if (!connected_.load(std::memory_order_acquire)) return -1;
    if (external_cancel_ != nullptr && external_cancel_->load(std::memory_order_acquire)) {
      connected_.store(false, std::memory_order_release);
      return -1;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      if (++attempt > retries) {
        spdlog::error("bulk OUT timed out after {} bytes, retries exhausted", sent);
        return -1;
      }
      spdlog::debug("bulk OUT timed out after {} bytes, retrying ({}/{})", sent, attempt, retries);
      std::this_thread::sleep_for(kRetryBackoff);
      deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(chunk_timeout);
      continue;
    }
    const int remaining_ms =
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    const int call_timeout = std::max(1, std::min(kCancelPollMs, remaining_ms));
    const int chunk = static_cast<int>(std::min<std::size_t>(chunk_cap, total - sent));
    int transferred = 0;
    const int rc = libusb_bulk_transfer(handle_, out_ep_, const_cast<unsigned char*>(base + sent), chunk, &transferred,
                                        static_cast<unsigned int>(call_timeout));

    if (transferred > 0) {
      sent += static_cast<std::size_t>(transferred);
      attempt = 0;
      deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(chunk_timeout);
      continue;
    }

    if (rc == LIBUSB_ERROR_TIMEOUT) {
      // A torn-down fd returns -ETIMEDOUT instantly; without this yield the
      // loop starves system_server and freezes the device.
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO || rc == LIBUSB_ERROR_NOT_FOUND ||
        rc == LIBUSB_ERROR_PIPE) {
      // PIPE is terminal: clearing a halt on a yanked device can hang the
      // ioctl path long enough to freeze the phone.
      spdlog::debug("Device disconnected during send ({})", libusb_error_name(rc));
      connected_.store(false, std::memory_order_release);
      return -1;
    }

    if (++attempt > retries) {
      spdlog::error("bulk OUT failed: {}, retries exhausted", libusb_strerror_pair(rc));
      return -1;
    }

    spdlog::debug("bulk OUT failed: {}, retrying ({}/{})", libusb_strerror_pair(rc), attempt, retries);
    std::this_thread::sleep_for(kRetryBackoff);
  }

  if (zlp_needed_.load(std::memory_order_relaxed)) {
    int zlp_transferred = 0;
    const int rc = libusb_bulk_transfer(handle_, out_ep_, nullptr, 0, &zlp_transferred,
                                        static_cast<unsigned int>(kZlpProbeTimeoutMs));
    if (rc != 0) zlp_needed_.store(false, std::memory_order_relaxed);
  }

  return static_cast<int>(sent);
}

int LibusbUsbTransport::recv(std::span<std::uint8_t> data, unsigned retries) {
  if (data.empty()) return recv_zlp(retries);
  if (!connected_.load(std::memory_order_acquire)) return -1;

  std::lock_guard<std::mutex> lock(io_mutex_);
  if (handle_ == nullptr) return -1;

  const std::size_t total = data.size();
  auto* base = data.data();
  const std::size_t chunk_cap = chunk_bytes_.load(std::memory_order_relaxed);
  const int chunk_timeout = timeout_ms_.load(std::memory_order_relaxed);

  std::size_t read_total = 0;
  unsigned attempt = 0;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(chunk_timeout);

  while (read_total < total) {
    if (!connected_.load(std::memory_order_acquire)) return -1;
    if (external_cancel_ != nullptr && external_cancel_->load(std::memory_order_acquire)) {
      connected_.store(false, std::memory_order_release);
      return -1;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      if (++attempt > retries) {
        spdlog::error("bulk IN timed out after {} bytes, retries exhausted", read_total);
        return -1;
      }
      spdlog::debug("bulk IN timed out after {} bytes, retrying ({}/{})", read_total, attempt, retries);
      std::this_thread::sleep_for(kRetryBackoff);
      deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(chunk_timeout);
      continue;
    }
    const int remaining_ms =
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    const int call_timeout = std::max(1, std::min(kCancelPollMs, remaining_ms));
    const int chunk = static_cast<int>(std::min<std::size_t>(chunk_cap, total - read_total));
    int transferred = 0;
    const int rc = libusb_bulk_transfer(handle_, in_ep_, base + read_total, chunk, &transferred,
                                        static_cast<unsigned int>(call_timeout));

    if (transferred > 0) {
      read_total += static_cast<std::size_t>(transferred);
      attempt = 0;
      deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(chunk_timeout);
      if (transferred < chunk) {
        spdlog::debug("libusb recv: short read ({} of {} bytes, total {} of {})", transferred, chunk, read_total,
                      total);
        break;
      }
      continue;
    }

    if (rc == LIBUSB_ERROR_TIMEOUT) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    if (rc == LIBUSB_ERROR_NO_DEVICE || rc == LIBUSB_ERROR_IO || rc == LIBUSB_ERROR_NOT_FOUND ||
        rc == LIBUSB_ERROR_PIPE) {
      spdlog::debug("Device disconnected during recv ({})", libusb_error_name(rc));
      connected_.store(false, std::memory_order_release);
      return -1;
    }

    if (++attempt > retries) {
      spdlog::error("bulk IN failed: {}, retries exhausted", libusb_strerror_pair(rc));
      return -1;
    }

    spdlog::debug("bulk IN failed: {}, retrying ({}/{})", libusb_strerror_pair(rc), attempt, retries);
    std::this_thread::sleep_for(kRetryBackoff);
  }

  return static_cast<int>(read_total);
}

int LibusbUsbTransport::recv_zlp(unsigned /*retries*/) {
  if (!connected_.load(std::memory_order_acquire)) return -1;

  std::lock_guard<std::mutex> lock(io_mutex_);
  if (handle_ == nullptr) return -1;

  int transferred = 0;
  const int rc = libusb_bulk_transfer(handle_, in_ep_, nullptr, 0, &transferred,
                                      static_cast<unsigned int>(kZlpRecvTimeoutMs));
  if (rc == LIBUSB_ERROR_NO_DEVICE) {
    spdlog::debug("Device disconnected during recv_zlp ({})", libusb_error_name(rc));
    connected_.store(false, std::memory_order_release);
    return -1;
  }
  return 0;
}

} // namespace brokkr::android_platform
