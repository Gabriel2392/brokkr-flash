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

#include "io/random_access.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <cerrno>
  #include <cstring>
  #include <fcntl.h>
  #include <sys/stat.h>
  #include <unistd.h>
#endif

namespace brokkr::io {

brokkr::core::Status RandomAccessSource::read_exact_at(std::uint64_t offset,
                                                       std::span<std::byte> out) const noexcept {
  BRK_TRYV(got, read_at(offset, out));
  if (got != out.size()) return brokkr::core::failf("Unexpected EOF: {}", label());
  return {};
}

namespace {

std::string identity_of(const std::filesystem::path& path) {
  std::error_code ec;

  auto canonical = std::filesystem::weakly_canonical(path, ec);
  if (!ec) return canonical.generic_string();

  auto absolute = std::filesystem::absolute(path, ec);
  if (!ec) return absolute.lexically_normal().generic_string();

  return path.lexically_normal().generic_string();
}

#if defined(_WIN32)

class WindowsSource final : public RandomAccessSource {
 public:
  WindowsSource(HANDLE handle, std::string label, std::string identity, std::uint64_t size,
                std::int64_t write_time) noexcept
      : handle_(handle),
        label_(std::move(label)),
        identity_(std::move(identity)),
        size_(size),
        write_time_(write_time) {}

  ~WindowsSource() override {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
  }

  const std::string& label() const noexcept override { return label_; }
  const std::string& identity() const noexcept override { return identity_; }
  std::int64_t write_time() const noexcept override { return write_time_; }
  std::uint64_t size() const noexcept override { return size_; }

  brokkr::core::Result<std::size_t> read_at(std::uint64_t offset, std::span<std::byte> out) const noexcept override {
    std::size_t done = 0;
    while (done < out.size()) {
      const DWORD want = static_cast<DWORD>(std::min<std::size_t>(out.size() - done, 1u << 30));
      const std::uint64_t at = offset + done;

      OVERLAPPED ov{};
      ov.Offset = static_cast<DWORD>(at & 0xFFFFFFFFull);
      ov.OffsetHigh = static_cast<DWORD>(at >> 32);

      DWORD got = 0;
      if (!ReadFile(handle_, out.data() + done, want, &got, &ov)) {
        if (GetLastError() == ERROR_HANDLE_EOF) break;
        return brokkr::core::failf("Read failed: {}", label_);
      }
      if (got == 0) break;
      done += static_cast<std::size_t>(got);
    }
    return done;
  }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  std::string label_;
  std::string identity_;
  std::uint64_t size_ = 0;
  std::int64_t write_time_ = 0;
};

#else

std::int64_t write_time_of(const struct stat& st) noexcept {
#if defined(__APPLE__)
  return static_cast<std::int64_t>(st.st_mtimespec.tv_sec) * 1000 +
         static_cast<std::int64_t>(st.st_mtimespec.tv_nsec / 1'000'000);
#elif defined(__linux__) || defined(__ANDROID__)
  return static_cast<std::int64_t>(st.st_mtim.tv_sec) * 1000 +
         static_cast<std::int64_t>(st.st_mtim.tv_nsec / 1'000'000);
#else
  return static_cast<std::int64_t>(st.st_mtime) * 1000;
#endif
}

class PosixSource final : public RandomAccessSource {
 public:
  PosixSource(int fd, std::string label, std::string identity, std::uint64_t size, std::int64_t write_time) noexcept
      : fd_(fd), label_(std::move(label)), identity_(std::move(identity)), size_(size), write_time_(write_time) {}

  ~PosixSource() override {
    if (fd_ >= 0) ::close(fd_);
  }

  const std::string& label() const noexcept override { return label_; }
  const std::string& identity() const noexcept override { return identity_; }
  std::int64_t write_time() const noexcept override { return write_time_; }
  std::uint64_t size() const noexcept override { return size_; }

  brokkr::core::Result<std::size_t> read_at(std::uint64_t offset, std::span<std::byte> out) const noexcept override {
    std::size_t done = 0;
    while (done < out.size()) {
      const ssize_t rc = ::pread(fd_, out.data() + done, out.size() - done, static_cast<off_t>(offset + done));
      if (rc < 0) {
        if (errno == EINTR) continue;
        return brokkr::core::failf("Read failed: {}: {}", label_, std::strerror(errno));
      }
      if (rc == 0) break;
      done += static_cast<std::size_t>(rc);
    }
    return done;
  }

  void advise_sequential() const noexcept override {
#if defined(POSIX_FADV_SEQUENTIAL)
    (void)::posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
#if defined(POSIX_FADV_WILLNEED)
    (void)::posix_fadvise(fd_, 0, 0, POSIX_FADV_WILLNEED);
#endif
  }

 private:
  int fd_ = -1;
  std::string label_;
  std::string identity_;
  std::uint64_t size_ = 0;
  std::int64_t write_time_ = 0;
};

brokkr::core::Result<RandomAccessSourcePtr> adopt_fd(int owned_fd, std::string label, std::string identity) noexcept {
  struct stat st {};
  if (::fstat(owned_fd, &st) != 0) {
    const int err = errno;
    ::close(owned_fd);
    return brokkr::core::failf("fstat failed: {}: {}", label, std::strerror(err));
  }

  return RandomAccessSourcePtr(std::make_shared<PosixSource>(owned_fd, std::move(label), std::move(identity),
                                                            static_cast<std::uint64_t>(st.st_size),
                                                            write_time_of(st)));
}

#endif

} // namespace

brokkr::core::Result<RandomAccessSourcePtr> open_file_source(const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return brokkr::core::failf("Cannot open: {}", path.string());

  BY_HANDLE_FILE_INFORMATION info{};
  if (!GetFileInformationByHandle(handle, &info)) {
    CloseHandle(handle);
    return brokkr::core::failf("Cannot stat: {}", path.string());
  }

  const std::uint64_t size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;

  ULARGE_INTEGER ticks{};
  ticks.LowPart = info.ftLastWriteTime.dwLowDateTime;
  ticks.HighPart = info.ftLastWriteTime.dwHighDateTime;

  return RandomAccessSourcePtr(std::make_shared<WindowsSource>(handle, path.string(), identity_of(path), size,
                                                              static_cast<std::int64_t>(ticks.QuadPart / 10000)));
#else
  const int fd = ::open(path.c_str(), O_RDONLY
  #if defined(O_CLOEXEC)
                                          | O_CLOEXEC
  #endif
  );
  if (fd < 0) return brokkr::core::failf("Cannot open: {}: {}", path.string(), std::strerror(errno));

  return adopt_fd(fd, path.string(), identity_of(path));
#endif
}

#if !defined(_WIN32)
brokkr::core::Result<RandomAccessSourcePtr> open_fd_source(int fd, std::string label) noexcept {
  #if defined(F_DUPFD_CLOEXEC)
  const int duped = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
  #else
  const int duped = ::dup(fd);
  #endif
  if (duped < 0) return brokkr::core::failf("dup failed: {}: {}", label, std::strerror(errno));

  std::string identity = label;
  return adopt_fd(duped, std::move(label), std::move(identity));
}
#endif

} // namespace brokkr::io
