#pragma once

#include <spdlog/spdlog.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace brokkr {
struct FileHandle {
  int fd = -1;

  FileHandle() = default;
  explicit FileHandle(int fd) : fd(fd) {}

  FileHandle(const FileHandle &) = delete;
  FileHandle &operator=(const FileHandle &) = delete;

  FileHandle(FileHandle &&other) noexcept : fd(other.fd) { other.fd = -1; }
  FileHandle &operator=(FileHandle &&other) noexcept {
    if (this != &other) {
      close();
      fd = other.fd;
      other.fd = -1;
    }
    return *this;
  }

  ~FileHandle() { close(); }

  FileHandle &take(int new_fd, bool close_old = true) noexcept {
    if (close_old && valid())
      close();
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

  void setsocketopt(int level, int optname, const void *optval,
                    socklen_t optlen, const char *level_name,
                    const char *optname_name) const noexcept {
    if (::setsockopt(fd, level, optname, optval, optlen) != 0) {
      // Log the error but don't throw, since this is a best-effort operation.
      spdlog::error("setsockopt({}, {}, {}, <ptr>, <len>): error: {}", fd,
                    std::strerror(errno), level_name, optname_name);
    };
  }

  ssize_t send(const void *buf, size_t len, int flags,
               const char *flags_desc) const noexcept {
    ssize_t rc = ::send(fd, buf, len, flags);
    if (rc < 0) {
      spdlog::error("send(<ptr>, {}, {}, {}): error: {}", fd, len, flags_desc,
                    std::strerror(errno));
    }
    return rc;
  }

  ssize_t recv(void *buf, size_t len, int flags,
               const char *flags_desc) const noexcept {
    ssize_t rc = ::recv(fd, buf, len, flags);
    if (rc < 0) {
      spdlog::error("recv(<ptr>, {}, {}, {}): error: {}", fd, len, flags_desc,
                    std::strerror(errno));
    }
    return rc;
  }

  static int socket(int domain, int type, int protocol, const char *domain_name,
                    const char *type_name, const char *protocol_name) noexcept {
    int rc = ::socket(domain, type, protocol);
    if (rc < 0) {
      spdlog::error("socket({}, {}, {}): error: {}", domain_name, type_name,
                    protocol_name, std::strerror(errno));
    }
    return rc;
  }

  int bind(const struct sockaddr *addr, socklen_t addrlen) const noexcept {
    int rc = ::bind(fd, addr, addrlen);
    if (rc != 0) {
      spdlog::error("bind({}, <ptr>, <len>): error: {}", fd,
                    std::strerror(errno));
    }
    return rc;
  }

  int listen(int backlog) const noexcept {
    int rc = ::listen(fd, backlog);
    if (rc != 0) {
      spdlog::error("listen({}, {}): error: {}", fd, backlog,
                    std::strerror(errno));
    }
    return rc;
  }

  int accept(struct sockaddr *addr, socklen_t *addrlen, int flags,
             const char *flags_desc) const noexcept {

#if defined(__linux__)
    int rc = ::accept4(fd, addr, addrlen, flags);
#elif defined(__APPLE__)
    int rc = ::accept(fd, addr, addrlen);
#else
#error "accept with flags not implemented on this platform"
#endif
    if (rc < 0) {
      spdlog::error("accept({}, <ptr>, <len>, {}): error: {}", fd, flags_desc,
                    std::strerror(errno));
    }
    return rc;
  }

  int ioctl(unsigned long request, void *arg,
            const char *request_name) const noexcept {
    int rc = ::ioctl(fd, request, arg);
    if (rc != 0) {
      spdlog::error("ioctl({}, {}, <ptr>): error: {}", fd, request_name,
                    std::strerror(errno));
    }
    return rc;
  }

  int read(void *buf, size_t count) const noexcept {
    ssize_t rc = ::read(fd, buf, count);
    if (rc < 0) {
      spdlog::error("read({}, <ptr>, {}): error: {}", fd, count,
                    std::strerror(errno));
    }
    return static_cast<int>(rc);
  }
};
} // namespace brokkr

#define do_setsockopt(fd, level, optname, optval, optlen)                      \
  do {                                                                         \
    if ((fd).valid()) {                                                        \
      (fd).setsocketopt(level, optname, optval, optlen, #level, #optname);     \
    }                                                                          \
  } while (0)

#define do_send(fd, buf, len, flags)                                           \
  ((fd).valid() ? (fd).send(buf, len, flags, #flags) : -1)

#define do_recv(fd, buf, len, flags)                                           \
  ((fd).valid() ? (fd).recv(buf, len, flags, #flags) : -1)

#define do_socket(domain, type, protocol)                                      \
  (FileHandle::socket(domain, type, protocol, #domain, #type, #protocol))

#define do_bind(fd, addr, addrlen)                                             \
  ((fd).valid() ? (fd).bind(addr, addrlen) : -1)

#define do_listen(fd, backlog) ((fd).valid() ? (fd).listen(backlog) : -1)

#define do_accept(fd, addr, addrlen, flags)                                    \
  ((fd).valid() ? (fd).accept(addr, addrlen, flags, #flags) : -1)

#define do_ioctl(fd, request, arg)                                             \
  ((fd).valid() ? (fd).ioctl(request, arg, #request) : -1)

#define do_read(fd, buf, count) ((fd).valid() ? (fd).read(buf, count) : -1)