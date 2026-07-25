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

#include "platform/android/fd_package.hpp"

#include "core/str.hpp"
#include "io/lz4_frame.hpp"
#include "io/source.hpp"
#include "io/tar.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>

namespace brokkr::android_platform {

namespace {

constexpr std::size_t kTarBlock = 512;
constexpr std::size_t kTrailerMaxBytes = 16 * 1024;
constexpr std::size_t kMd5HexChars = 32;

brokkr::core::Result<int> dup_cloexec(int fd) noexcept {
#if defined(F_DUPFD_CLOEXEC)
  const int duped = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
#else
  const int duped = ::dup(fd);
#endif
  if (duped < 0) return brokkr::core::failf("dup failed: {}", std::strerror(errno));
  return duped;
}

std::int64_t stat_write_time(const struct stat& st) noexcept {
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

brokkr::core::Status pread_exact(int fd, std::uint64_t offset, std::span<std::byte> out) noexcept {
  std::size_t done = 0;
  while (done < out.size()) {
    const ssize_t rc = ::pread(fd, out.data() + done, out.size() - done, static_cast<off_t>(offset + done));
    if (rc < 0) {
      if (errno == EINTR) continue;
      return brokkr::core::failf("pread failed: {}", std::strerror(errno));
    }
    if (rc == 0) return brokkr::core::fail("Unexpected EOF");
    done += static_cast<std::size_t>(rc);
  }
  return {};
}

brokkr::core::Result<std::size_t> pread_some(int fd, std::uint64_t offset, unsigned char* out, std::size_t want) noexcept {
  std::size_t done = 0;
  while (done < want) {
    const ssize_t rc = ::pread(fd, out + done, want - done, static_cast<off_t>(offset + done));
    if (rc < 0) {
      if (errno == EINTR) continue;
      return brokkr::core::failf("pread failed: {}", std::strerror(errno));
    }
    if (rc == 0) break;
    done += static_cast<std::size_t>(rc);
  }
  return done;
}

inline std::uint64_t round_up_512(std::uint64_t n) noexcept {
  return (n + (kTarBlock - 1)) & ~(static_cast<std::uint64_t>(kTarBlock - 1));
}

inline std::string basename_of(std::string_view s) {
  const auto pos1 = s.find_last_of('/');
  const auto pos2 = s.find_last_of('\\');
  const auto pos = (pos1 == std::string_view::npos)   ? pos2
                   : (pos2 == std::string_view::npos) ? pos1
                                                      : std::max(pos1, pos2);
  return (pos == std::string_view::npos) ? std::string(s) : std::string(s.substr(pos + 1));
}

static brokkr::core::Result<std::uint64_t> parse_u64_dec(std::string_view s) noexcept {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
    s.remove_suffix(1);
  }

  std::uint64_t value = 0;
  const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value, 10);
  if (ec != std::errc{} || ptr != s.data() + s.size()) return brokkr::core::fail("PAX: invalid decimal number");
  return value;
}

bool header_all_zero(std::span<const std::byte, 512> header) noexcept {
  return std::all_of(header.begin(), header.end(), [](std::byte value) { return value == std::byte{0}; });
}

std::string trim_cstr_field(const char* p, std::size_t n) {
  const auto* nul = static_cast<const char*>(std::memchr(p, '\0', n));
  const std::size_t len = nul ? static_cast<std::size_t>(nul - p) : n;

  std::size_t end = len;
  while (end > 0 && (p[end - 1] == ' ' || p[end - 1] == '\t' || p[end - 1] == '\r' || p[end - 1] == '\n')) {
    --end;
  }
  return std::string(p, p + end);
}

std::uint64_t parse_octal(std::string_view s) noexcept {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\0')) s.remove_prefix(1);
  while (!s.empty() &&
         (s.back() == ' ' || s.back() == '\t' || s.back() == '\0' || s.back() == '\r' || s.back() == '\n')) {
    s.remove_suffix(1);
  }

  std::uint64_t value = 0;
  for (char ch : s) {
    if (ch < '0' || ch > '7') break;
    value = (value << 3) + static_cast<std::uint64_t>(ch - '0');
  }
  return value;
}

brokkr::core::Result<std::uint64_t> parse_tar_number(const char* p, std::size_t n) noexcept {
  if (n == 0) return std::uint64_t{0};

  const unsigned char b0 = static_cast<unsigned char>(p[0]);
  if ((b0 & 0x80U) != 0U) {
    if ((b0 & 0x40U) != 0U) return brokkr::core::fail("Tar: negative base-256 numeric field");

    std::uint64_t value = static_cast<std::uint64_t>(b0 & 0x3FU);
    for (std::size_t i = 1; i < n; ++i) {
      if (value > (std::numeric_limits<std::uint64_t>::max() >> 8)) {
        return brokkr::core::fail("Tar: base-256 numeric field too large for uint64");
      }
      value = (value << 8) | static_cast<unsigned char>(p[i]);
    }
    return value;
  }

  return parse_octal(std::string_view(p, n));
}

std::string join_ustar_name(std::string_view prefix, std::string_view name) {
  if (prefix.empty()) return std::string(name);

  std::string out(prefix);
  if (!out.empty() && out.back() != '/') out.push_back('/');
  out.append(name);
  return out;
}

bool validate_header_checksum(std::span<const std::byte, 512> header) noexcept {
  constexpr std::size_t chk_off = 148;
  constexpr std::size_t chk_len = 8;

  const char* chk_field = reinterpret_cast<const char*>(header.data() + chk_off);
  const auto expected = static_cast<unsigned long>(parse_octal(std::string_view(chk_field, chk_len)));

  auto compute = [&](bool signed_mode) -> unsigned long {
    long sum = 0;
    for (std::size_t i = 0; i < header.size(); ++i) {
      unsigned char current = static_cast<unsigned char>(header[i]);
      if (i >= chk_off && i < chk_off + chk_len) current = 0x20;
      sum += signed_mode ? static_cast<signed char>(current) : static_cast<unsigned char>(current);
    }
    return static_cast<unsigned long>(sum);
  };

  return expected == compute(false) || expected == compute(true);
}

struct PaxKV {
  std::optional<std::string> path;
  std::optional<std::uint64_t> size;

  void clear() {
    path.reset();
    size.reset();
  }

  void merge_from(const PaxKV& other) {
    if (other.path) path = *other.path;
    if (other.size) size = *other.size;
  }
};

brokkr::core::Result<PaxKV> parse_pax_payload(std::string_view payload) noexcept {
  PaxKV kv;

  std::size_t pos = 0;
  while (pos < payload.size()) {
    const auto sp = payload.find(' ', pos);
    if (sp == std::string_view::npos) break;

    BRK_TRYV(record_len, parse_u64_dec(payload.substr(pos, sp - pos)));
    if (record_len == 0 || pos + record_len > payload.size()) break;

    const auto record = payload.substr(pos, static_cast<std::size_t>(record_len));
    pos += static_cast<std::size_t>(record_len);

    const auto sp2 = record.find(' ');
    if (sp2 == std::string_view::npos) continue;

    std::string_view kvs = record.substr(sp2 + 1);
    if (!kvs.empty() && kvs.back() == '\n') kvs.remove_suffix(1);

    const auto eq = kvs.find('=');
    if (eq == std::string_view::npos) continue;

    const auto key = kvs.substr(0, eq);
    const auto value = kvs.substr(eq + 1);

    if (key == "path") {
      kv.path = std::string(value);
    } else if (key == "size") {
      BRK_TRYV(parsed_size, parse_u64_dec(value));
      kv.size = parsed_size;
    }
  }

  return kv;
}

class FdByteSource final : public brokkr::io::ByteSource {
 public:
  FdByteSource(std::shared_ptr<PackageFile> package, std::uint64_t data_offset, std::uint64_t size, std::string display)
      : package_(std::move(package)), data_offset_(data_offset), size_(size), display_(std::move(display)) {}

  std::string display_name() const override { return display_; }
  std::uint64_t size() const override { return size_; }

  std::size_t read(std::span<std::byte> out) override {
    if (cursor_ >= size_ || out.empty()) return 0;

    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(size_ - cursor_, out.size()));
    auto result = pread_some(package_->fd, data_offset_ + cursor_, reinterpret_cast<unsigned char*>(out.data()), want);
    if (!result) {
      status_ = brokkr::core::fail(std::move(result.error()));
      return 0;
    }

    cursor_ += *result;
    return *result;
  }

  brokkr::core::Status status() const noexcept override { return status_; }

 private:
  std::shared_ptr<PackageFile> package_;
  std::uint64_t data_offset_ = 0;
  std::uint64_t size_ = 0;
  std::uint64_t cursor_ = 0;
  std::string display_;
  brokkr::core::Status status_{};
};

class FdHashReader final : public brokkr::app::HashReader {
 public:
  explicit FdHashReader(std::shared_ptr<PackageFile> package) : package_(std::move(package)) {}

  brokkr::core::Status open() noexcept override { return {}; }

  brokkr::core::Result<std::size_t> read_some(unsigned char* data, std::size_t want) noexcept override {
    auto result = pread_some(package_->fd, cursor_, data, want);
    if (!result) return brokkr::core::fail(std::move(result.error()));
    cursor_ += *result;
    return *result;
  }

 private:
  std::shared_ptr<PackageFile> package_;
  std::uint64_t cursor_ = 0;
};

class FdTarArchive {
 public:
  static brokkr::core::Result<FdTarArchive> open(std::shared_ptr<PackageFile> package,
                                                 bool validate_headers = true) noexcept {
    FdTarArchive archive;
    archive.package_ = std::move(package);
    archive.validate_headers_ = validate_headers;

    auto st = archive.scan();
    if (!st) return brokkr::core::fail(std::move(st.error()));
    return archive;
  }

  static bool is_tar_file(const PackageFile& package) noexcept {
    if (package.size < kTarBlock) return false;

    std::array<std::byte, kTarBlock> header{};
    if (!pread_exact(package.fd, 0, header)) return false;
    if (header_all_zero(std::span<const std::byte, 512>(header))) return false;
    return validate_header_checksum(std::span<const std::byte, 512>(header));
  }

  const std::vector<brokkr::io::TarEntry>& entries() const noexcept { return entries_; }

  brokkr::core::Result<std::unique_ptr<brokkr::io::ByteSource>> open_entry(const brokkr::io::TarEntry& entry) const noexcept {
    return std::unique_ptr<brokkr::io::ByteSource>(
        std::make_unique<FdByteSource>(package_, entry.data_offset, entry.size, package_->label + ":" + entry.name));
  }

 private:
  brokkr::core::Status scan() noexcept {
    std::uint64_t offset = 0;
    PaxKV pax_global;
    PaxKV pax_next;
    payload_size_bytes_.reset();

    while (offset + kTarBlock <= package_->size) {
      std::array<std::byte, kTarBlock> header{};
      BRK_TRY(pread_exact(package_->fd, offset, header));
      if (header_all_zero(std::span<const std::byte, 512>(header))) break;

      if (validate_headers_ && !validate_header_checksum(std::span<const std::byte, 512>(header))) {
        return brokkr::core::failf("TarArchive: invalid header checksum in: {}", package_->label);
      }

      const char* raw = reinterpret_cast<const char*>(header.data());
      const std::string name = trim_cstr_field(raw + 0, 100);
      const std::string prefix = trim_cstr_field(raw + 345, 155);
      const char typeflag = raw[156];

      BRK_TRYV(parsed_size, parse_tar_number(raw + 124, 12));
      const std::uint64_t entry_size = pax_next.size.value_or(parsed_size);

      std::string full_name = pax_next.path.value_or(join_ustar_name(prefix, name));
      if (full_name.empty()) full_name = name;

      const std::uint64_t data_offset = offset + kTarBlock;

      if (typeflag == 'g' || typeflag == 'x') {
        if (data_offset + entry_size > package_->size) {
          return brokkr::core::failf("TarArchive: pax payload truncated: {}", package_->label);
        }
        if (entry_size > (8ull * 1024ull * 1024ull)) {
          return brokkr::core::failf("TarArchive: refusing huge PAX header in: {}", package_->label);
        }

        std::string payload(static_cast<std::size_t>(entry_size), '\0');
        BRK_TRY(pread_exact(package_->fd, data_offset,
                            std::as_writable_bytes(std::span<char>(payload.data(), payload.size()))));

        BRK_TRYV(parsed_pax, parse_pax_payload(payload));
        if (typeflag == 'g') {
          pax_global = parsed_pax;
          pax_next.clear();
        } else {
          pax_next = pax_global;
          pax_next.merge_from(parsed_pax);
        }

        offset = data_offset + round_up_512(entry_size);
        continue;
      }

      PaxKV effective = pax_global;
      effective.merge_from(pax_next);
      pax_next.clear();

      full_name = effective.path.value_or(full_name);
      const std::uint64_t effective_size = effective.size.value_or(entry_size);

      if (typeflag == '0' || typeflag == '\0') {
        if (data_offset + effective_size > package_->size) {
          return brokkr::core::failf("TarArchive: entry truncated: {}:{}", package_->label, full_name);
        }

        entries_.push_back({.name = full_name, .size = effective_size, .data_offset = data_offset});
      }

      offset = data_offset + round_up_512(entry_size);
      payload_size_bytes_ = offset;
    }

    spdlog::debug("FdTarArchive: scanned {} entries in {}", entries_.size(), package_->label);
    return {};
  }

 private:
  std::shared_ptr<PackageFile> package_;
  bool validate_headers_ = true;
  std::vector<brokkr::io::TarEntry> entries_;
  std::optional<std::uint64_t> payload_size_bytes_;
};

bool is_hex(unsigned char ch) noexcept {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

int hex_nibble(unsigned char ch) noexcept {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + (ch - 'a');
  if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
  return -1;
}

bool parse_md5_hex(std::string_view hex32, std::array<unsigned char, 16>& out) noexcept {
  if (hex32.size() != kMd5HexChars) return false;
  for (std::size_t i = 0; i < out.size(); ++i) {
    const int hi = hex_nibble(static_cast<unsigned char>(hex32[2 * i]));
    const int lo = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 1]));
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<unsigned char>((hi << 4) | lo);
  }
  return true;
}

std::string_view trim_ws(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
    s.remove_prefix(1);
  }
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
    s.remove_suffix(1);
  }
  return s;
}

brokkr::core::Result<std::string> read_text(brokkr::io::ByteSource& src, std::size_t max_bytes,
                                            std::string_view what) noexcept {
  const std::uint64_t size64 = src.size();
  if (size64 > max_bytes) return brokkr::core::fail("read_text: too large: " + std::string(what));

  std::string text(static_cast<std::size_t>(size64), '\0');
  std::size_t offset = 0;
  while (offset < text.size()) {
    const auto got = src.read(std::as_writable_bytes(std::span<char>(text.data() + offset, text.size() - offset)));
    if (got == 0) return brokkr::core::fail("Short read: " + std::string(what));
    offset += got;
  }
  return text;
}

brokkr::core::Result<std::vector<std::string>> parse_download_list(std::string_view text) noexcept {
  std::vector<std::string> names;
  std::unordered_set<std::string> seen;

  for (std::size_t pos = 0; pos <= text.size();) {
    const std::size_t next = text.find('\n', pos);
    const std::size_t end = (next == std::string_view::npos) ? text.size() : next;
    auto line = trim_ws(text.substr(pos, end - pos));
    pos = (next == std::string_view::npos) ? (text.size() + 1) : (next + 1);

    if (line.empty()) continue;
    std::string name(line);
    if (!seen.insert(name).second) {
      return brokkr::core::fail("download-list.txt contains duplicate entry: " + name);
    }
    names.push_back(std::move(name));
  }

  if (names.empty()) return brokkr::core::fail("download-list.txt is empty");
  return names;
}

bool is_download_list_name(std::string_view name) noexcept {
  return name == "meta-data/download-list.txt" || name == "./meta-data/download-list.txt";
}

bool is_pit_name(std::string_view name) noexcept { return brokkr::core::ends_with_ci(basename_of(name), ".pit"); }

std::optional<brokkr::io::TarEntry> find_download_list_entry(const FdTarArchive& tar) {
  for (const auto& entry : tar.entries()) {
    if (is_download_list_name(entry.name)) return entry;
  }
  return std::nullopt;
}

bool lists_equal(const std::vector<std::string>& lhs, const std::vector<std::string>& rhs) noexcept {
  return lhs == rhs;
}

bool is_lz4_name(std::string_view base) { return brokkr::core::ends_with_ci(base, ".lz4"); }

std::string strip_lz4_suffix(std::string value) {
  if (value.size() >= 4 && brokkr::core::ends_with_ci(value, ".lz4")) value.resize(value.size() - 4);
  return value;
}

brokkr::core::Result<brokkr::io::Lz4FrameHeaderInfo> lz4_header(const brokkr::odin::ImageSpec& spec) noexcept {
  BRK_TRYV(source, spec.open());
  return brokkr::io::parse_lz4_frame_header(*source);
}

brokkr::core::Result<brokkr::odin::ImageSpec> make_tar_spec(std::shared_ptr<PackageFile> package,
                                                            brokkr::io::TarEntry entry,
                                                            std::string source_basename) noexcept {
  brokkr::odin::ImageSpec spec;
  spec.kind = brokkr::odin::ImageSpec::Kind::TarEntry;
  spec.entry = entry;
  spec.source_basename = std::move(source_basename);
  spec.display = package->label + ":" + entry.name;
  spec.disk_size = entry.size;
  spec.lz4 = is_lz4_name(spec.source_basename);
  spec.basename = spec.lz4 ? strip_lz4_suffix(spec.source_basename) : spec.source_basename;
  spec.custom_open = [package, entry, display = spec.display]() -> brokkr::core::Result<std::unique_ptr<brokkr::io::ByteSource>> {
    return std::unique_ptr<brokkr::io::ByteSource>(std::make_unique<FdByteSource>(package, entry.data_offset, entry.size, display));
  };

  if (spec.lz4) {
    BRK_TRYV(header, lz4_header(spec));
    spec.size = header.content_size;
    spec.lz4_block_size = header.max_block_size;
  } else {
    spec.size = spec.disk_size;
  }

  return spec;
}

brokkr::core::Result<std::optional<brokkr::app::Md5Job>> detect_md5_job(const std::shared_ptr<PackageFile>& package) noexcept {
  if (package->size < (kMd5HexChars + 2) || !FdTarArchive::is_tar_file(*package)) return std::nullopt;

  const std::uint64_t tail_offset = (package->size > kTrailerMaxBytes) ? (package->size - kTrailerMaxBytes) : 0;
  const std::size_t tail_size = static_cast<std::size_t>(package->size - tail_offset);

  std::string tail(tail_size, '\0');
  BRK_TRY(pread_exact(package->fd, tail_offset, std::as_writable_bytes(std::span<char>(tail.data(), tail.size()))));

  std::int64_t delim = -1;
  for (std::int64_t i = static_cast<std::int64_t>(tail.size()) - 2; i >= 0; --i) {
    if (tail[static_cast<std::size_t>(i)] != ' ' || tail[static_cast<std::size_t>(i) + 1] != ' ') continue;
    const std::int64_t start = i - static_cast<std::int64_t>(kMd5HexChars);
    if (start < 0) continue;

    bool valid = true;
    for (std::size_t j = 0; j < kMd5HexChars; ++j) {
      if (!is_hex(static_cast<unsigned char>(tail[static_cast<std::size_t>(start) + j]))) {
        valid = false;
        break;
      }
    }

    if (valid) {
      delim = i;
      break;
    }
  }

  if (delim < 0) return std::nullopt;

  std::array<unsigned char, 16> expected{};
  if (!parse_md5_hex({tail.data() + static_cast<std::size_t>(delim - static_cast<std::int64_t>(kMd5HexChars)), kMd5HexChars},
                     expected)) {
    return std::nullopt;
  }

  const std::uint64_t bytes_to_hash =
      tail_offset + static_cast<std::uint64_t>(delim - static_cast<std::int64_t>(kMd5HexChars));
  if (package->size - bytes_to_hash > kTrailerMaxBytes) {
    return brokkr::core::failf("MD5 trailer too large: {}", package->label);
  }

  brokkr::app::Md5Job job;
  job.display_name = package->label;
  job.identity_path = package->label;
  job.identity_size = package->size;
  job.identity_write_time = package->write_time;
  job.bytes_to_hash = bytes_to_hash;
  job.expected = expected;
  job.open_reader = [package]() -> brokkr::core::Result<std::unique_ptr<brokkr::app::HashReader>> {
    return std::unique_ptr<brokkr::app::HashReader>(std::make_unique<FdHashReader>(package));
  };
  return std::optional<brokkr::app::Md5Job>(std::move(job));
}

} // namespace

PackageFile::~PackageFile() {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

brokkr::core::Result<std::shared_ptr<PackageFile>> PackageFile::open(int fd, std::string label) noexcept {
  BRK_TRYV(duped_fd, dup_cloexec(fd));

  struct stat st {};
  if (::fstat(duped_fd, &st) != 0) {
    ::close(duped_fd);
    return brokkr::core::failf("fstat failed: {}", std::strerror(errno));
  }

  auto package = std::shared_ptr<PackageFile>(new PackageFile());
  package->fd = duped_fd;
  package->label = std::move(label);
  package->size = static_cast<std::uint64_t>(st.st_size);
  package->write_time = stat_write_time(st);
  return package;
}

brokkr::core::Result<std::vector<brokkr::app::Md5Job>> md5_jobs_from_packages(const PackageFiles& packages) noexcept {
  std::vector<brokkr::app::Md5Job> jobs;
  jobs.reserve(packages.size());

  for (const auto& package : packages) {
    BRK_TRYV(job, detect_md5_job(package));
    if (job) jobs.push_back(std::move(*job));
  }

  return jobs;
}

brokkr::core::Result<std::shared_ptr<const std::vector<std::byte>>> load_pit_file(int fd, std::string label) noexcept {
  BRK_TRYV(package, PackageFile::open(fd, std::move(label)));
  if (package->size == 0) return brokkr::core::fail("Manual PIT file is empty");
  if (package->size > std::numeric_limits<std::size_t>::max()) {
    return brokkr::core::fail("Manual PIT file is too large");
  }

  auto pit = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(package->size));
  std::size_t offset = 0;
  while (offset < pit->size()) {
    BRK_TRYV(got,
             pread_some(package->fd, offset, reinterpret_cast<unsigned char*>(pit->data()) + offset, pit->size() - offset));
    if (got == 0) return brokkr::core::fail("Failed to read manual PIT file");
    offset += got;
  }

  spdlog::info("Using manual PIT: {}", package->label);
  return std::shared_ptr<const std::vector<std::byte>>(std::move(pit));
}

brokkr::core::Result<std::vector<brokkr::odin::ImageSpec>> expand_package_inputs(const PackageFiles& packages) noexcept {
  std::vector<std::pair<std::shared_ptr<PackageFile>, FdTarArchive>> archives;
  archives.reserve(packages.size());

  std::optional<std::vector<std::string>> download_list;

  for (const auto& package : packages) {
    if (!FdTarArchive::is_tar_file(*package)) {
      return brokkr::core::fail("Raw single-image flashing is not supported on Android");
    }

    BRK_TRYV(archive, FdTarArchive::open(package, true));
    if (auto entry = find_download_list_entry(archive)) {
      BRK_TRYV(source, archive.open_entry(*entry));
      BRK_TRYV(text, read_text(*source, 128 * 1024, "download-list.txt"));
      BRK_TRYV(parsed_list, parse_download_list(text));

      if (!download_list) {
        download_list = std::move(parsed_list);
      } else if (!lists_equal(*download_list, parsed_list)) {
        return brokkr::core::fail("Multiple download-list.txt files found with different contents");
      }
    }

    archives.emplace_back(package, std::move(archive));
  }

  std::vector<brokkr::odin::ImageSpec> out;

  std::unordered_set<std::string> allow_set;
  if (download_list) {
    allow_set.reserve(download_list->size());
    for (const auto& name : *download_list) allow_set.insert(name);
  }

  std::vector<brokkr::odin::ImageSpec> pit_specs;

  struct Coord { std::size_t pkg; std::size_t ent; };
  std::unordered_map<std::string, Coord> last_of;
  for (std::size_t pi = 0; pi < archives.size(); ++pi) {
    const auto& archive = archives[pi].second;
    const auto& entries = archive.entries();
    for (std::size_t ei = 0; ei < entries.size(); ++ei) {
      const auto& entry = entries[ei];
      if (is_download_list_name(entry.name)) continue;
      const std::string source_basename = brokkr::io::basename(entry.name);
      if (source_basename.empty() || source_basename.back() == '/') continue;
      const bool lz4 = is_lz4_name(source_basename);
      const std::string basename = lz4 ? strip_lz4_suffix(source_basename) : source_basename;
      if (basename.empty()) continue;
      auto [it, inserted] = last_of.try_emplace(basename, Coord{pi, ei});
      if (!inserted) {
        spdlog::debug("Duplicate image '{}' across packages — later occurrence wins", basename);
        it->second = Coord{pi, ei};
      }
    }
  }

  std::unordered_set<std::string> emitted;
  for (std::size_t pi = 0; pi < archives.size(); ++pi) {
    const auto& [package, archive] = archives[pi];
    const auto& entries = archive.entries();
    for (std::size_t ei = 0; ei < entries.size(); ++ei) {
      const auto& entry = entries[ei];
      if (is_download_list_name(entry.name)) continue;

      const std::string source_basename = brokkr::io::basename(entry.name);
      if (source_basename.empty() || source_basename.back() == '/') continue;

      const bool lz4 = is_lz4_name(source_basename);
      const std::string basename = lz4 ? strip_lz4_suffix(source_basename) : source_basename;
      if (basename.empty()) continue;

      const auto lit = last_of.find(basename);
      if (lit == last_of.end() || lit->second.pkg != pi || lit->second.ent != ei) continue;

      const bool is_pit = brokkr::core::ends_with_ci(basename, ".pit");

      if (download_list && !is_pit && !allow_set.contains(basename)) {
        spdlog::debug("Skipping {}: not in download-list.txt", source_basename);
        continue;
      }

      BRK_TRYV(spec, make_tar_spec(package, entry, source_basename));
      if (is_pit) {
        pit_specs.push_back(std::move(spec));
      } else {
        emitted.insert(basename);
        out.push_back(std::move(spec));
      }
    }
  }

  if (download_list) {
    for (const auto& name : *download_list) {
      if (!emitted.contains(name))
        spdlog::debug("download-list.txt references missing file: {} (skipping)", name);
    }
  }

  for (auto& s : pit_specs) out.push_back(std::move(s));

  return out;
}

brokkr::core::Result<std::shared_ptr<const std::vector<std::byte>>> load_pit_from_packages(
    const PackageFiles& packages) noexcept {
  for (const auto& package : packages) {
    if (!FdTarArchive::is_tar_file(*package)) continue;

    BRK_TRYV(archive, FdTarArchive::open(package, true));
    for (const auto& entry : archive.entries()) {
      if (!is_pit_name(entry.name)) continue;

      BRK_TRYV(source, archive.open_entry(entry));
      auto pit = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(entry.size));

      std::size_t offset = 0;
      while (offset < pit->size()) {
        const auto got = source->read(std::span<std::byte>(pit->data() + offset, pit->size() - offset));
        if (got == 0) return brokkr::core::fail("Failed to read PIT entry from package");
        offset += got;
      }

      spdlog::info("Using PIT from {}:{}", package->label, entry.name);
      return std::shared_ptr<const std::vector<std::byte>>(std::move(pit));
    }
  }

  return std::shared_ptr<const std::vector<std::byte>>{};
}

} // namespace brokkr::android_platform