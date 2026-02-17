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

#pragma once

#include <cerrno>
#include <cstring>

#include <spdlog/spdlog.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace brokkr {

struct FileHandle {
  int fd = -1;

  FileHandle() = default;
  explicit FileHandle(int fd_) : fd(fd_) {}

  FileHandle(const FileHandle&) = delete;
  FileHandle& operator=(const FileHandle&) = delete;

  FileHandle(FileHandle&& o) noexcept : fd(o.fd) { o.fd = -1; }
  FileHandle& operator=(FileHandle&& o) noexcept {
    if (this == &o) return *this;
    close();
    fd = o.fd;
    o.fd = -1;
    return *this;
  }

  ~FileHandle() { close(); }

  FileHandle& take(int new_fd, bool close_old = true) noexcept {
    if (close_old && valid()) close();
    fd = new_fd;
    return *this;
  }

  void close() noexcept {
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
  }

  bool valid() const noexcept { return fd >= 0; }

  int setsockopt(int level, int optname, const void* optval, socklen_t optlen,
                 const char* level_name, const char* opt_name) const noexcept
  {
    const int rc = ::setsockopt(fd, level, optname, optval, optlen);
    if (rc != 0) {
      const int e = errno;
      spdlog::error("setsockopt(fd={}, level={}, opt={}): {}", fd, level_name, opt_name, std::strerror(e));
      errno = e;
    }
    return rc;
  }

  ssize_t send(const void* buf, size_t len, int flags, const char* flags_desc) const noexcept {
    const ssize_t rc = ::send(fd, buf, len, flags);
    if (rc < 0) {
      const int e = errno;
      spdlog::error("send(fd={}, len={}, flags={}): {}", fd, len, flags_desc, std::strerror(e));
      errno = e;
    }
    return rc;
  }

  ssize_t recv(void* buf, size_t len, int flags, const char* flags_desc) const noexcept {
    const ssize_t rc = ::recv(fd, buf, len, flags);
    if (rc < 0) {
      const int e = errno;
      spdlog::error("recv(fd={}, len={}, flags={}): {}", fd, len, flags_desc, std::strerror(e));
      errno = e;
    }
    return rc;
  }

  static int socket(int domain, int type, int protocol,
                    const char* domain_name, const char* type_name, const char* proto_name) noexcept
  {
    const int rc = ::socket(domain, type, protocol);
    if (rc < 0) {
      const int e = errno;
      spdlog::error("socket(domain={}, type={}, proto={}): {}", domain_name, type_name, proto_name, std::strerror(e));
      errno = e;
    }
    return rc;
  }

  int bind(const struct sockaddr* addr, socklen_t addrlen) const noexcept {
    const int rc = ::bind(fd, addr, addrlen);
    if (rc != 0) {
      const int e = errno;
      spdlog::error("bind(fd={}): {}", fd, std::strerror(e));
      errno = e;
    }
    return rc;
  }

  int listen(int backlog) const noexcept {
    const int rc = ::listen(fd, backlog);
    if (rc != 0) {
      const int e = errno;
      spdlog::error("listen(fd={}, backlog={}): {}", fd, backlog, std::strerror(e));
      errno = e;
    }
    return rc;
  }

  int accept(struct sockaddr* addr, socklen_t* addrlen, int flags, const char* flags_desc) const noexcept {
#if defined(__linux__)
    const int rc = ::accept4(fd, addr, addrlen, flags);
#elif defined(__APPLE__)
    (void)flags;
    const int rc = ::accept(fd, addr, addrlen);
#else
#  error "accept with flags not implemented on this platform"
#endif
    if (rc < 0) {
      const int e = errno;
      spdlog::error("accept(fd={}, flags={}): {}", fd, flags_desc, std::strerror(e));
      errno = e;
    }
    return rc;
  }

  int ioctl(unsigned long request, void* arg, const char* req_name) const noexcept {
    const int rc = ::ioctl(fd, request, arg);
    if (rc < 0) { // FIX: ioctl signals error with -1; some ioctls return positive values
      const int e = errno;
      spdlog::error("ioctl(fd={}, req={}): {}", fd, req_name, std::strerror(e));
      errno = e;
    }
    return rc;
  }

  int read(void* buf, size_t count) const noexcept {
    const ssize_t rc = ::read(fd, buf, count);
    if (rc < 0) {
      const int e = errno;
      spdlog::error("read(fd={}, count={}): {}", fd, count, std::strerror(e));
      errno = e;
    }
    return static_cast<int>(rc);
  }
};

} // namespace brokkr

#define do_setsockopt(fd, level, optname, optval, optlen) \
  ((fd).valid() ? (fd).setsockopt(level, optname, optval, optlen, #level, #optname) : -1)

#define do_send(fd, buf, len, flags) ((fd).valid() ? (fd).send(buf, len, flags, #flags) : -1)
#define do_recv(fd, buf, len, flags) ((fd).valid() ? (fd).recv(buf, len, flags, #flags) : -1)

#define do_socket(domain, type, protocol) (FileHandle::socket(domain, type, protocol, #domain, #type, #protocol))
#define do_bind(fd, addr, addrlen) ((fd).valid() ? (fd).bind(addr, addrlen) : -1)
#define do_listen(fd, backlog) ((fd).valid() ? (fd).listen(backlog) : -1)
#define do_accept(fd, addr, addrlen, flags) ((fd).valid() ? (fd).accept(addr, addrlen, flags, #flags) : -1)
#define do_ioctl(fd, request, arg) ((fd).valid() ? (fd).ioctl(request, arg, #request) : -1)
#define do_read(fd, buf, count) ((fd).valid() ? (fd).read(buf, count) : -1)
