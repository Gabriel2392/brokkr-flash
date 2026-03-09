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

#include "app/md5_xxh3_cache.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

#include <fmt/format.h>

namespace brokkr::app {
namespace {

constexpr std::size_t kMd5HexChars = 32;
constexpr std::size_t kXxh3HexChars = 16;
constexpr std::string_view kCacheHeader = "brokkr-md5-xxh3-cache v1";
constexpr std::size_t kMaxEntriesDefault = 100;
constexpr std::uintmax_t kMaxCacheFileBytes = 1024 * 1024;

struct ParsedCacheFile {
  std::vector<Md5Xxh3CacheEntry> entries;
  bool has_header = false;
  bool saw_corruption = false;
};

int hex_nibble(unsigned char c) noexcept {
  if (c >= '0' && c <= '9') return (c - '0');
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  return -1;
}

bool parse_md5_hex(std::string_view hex32, std::array<unsigned char, 16>& out) noexcept {
  if (hex32.size() != kMd5HexChars) return false;
  for (std::size_t i = 0; i < out.size(); ++i) {
    const int hi = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 0]));
    const int lo = hex_nibble(static_cast<unsigned char>(hex32[2 * i + 1]));
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<unsigned char>((hi << 4) | lo);
  }
  return true;
}

bool parse_u64(std::string_view sv, std::uint64_t& out) noexcept {
  const char* begin = sv.data();
  const char* end = sv.data() + sv.size();
  auto [ptr, ec] = std::from_chars(begin, end, out, 10);
  return ec == std::errc{} && ptr == end;
}

bool parse_u64_hex(std::string_view sv, std::uint64_t& out) noexcept {
  const char* begin = sv.data();
  const char* end = sv.data() + sv.size();
  auto [ptr, ec] = std::from_chars(begin, end, out, 16);
  return ec == std::errc{} && ptr == end;
}

std::uint64_t next_touch(const std::vector<Md5Xxh3CacheEntry>& entries) noexcept {
  std::uint64_t best = 0;
  for (const auto& entry : entries) best = std::max(best, entry.touched);
  return (best == UINT64_MAX) ? 1 : (best + 1);
}

void normalize_entries(std::vector<Md5Xxh3CacheEntry>& entries, std::size_t max_entries) {
  std::sort(entries.begin(), entries.end(), [](const Md5Xxh3CacheEntry& lhs, const Md5Xxh3CacheEntry& rhs) {
    return lhs.touched > rhs.touched;
  });

  std::vector<Md5Xxh3CacheEntry> deduped;
  deduped.reserve(std::min(entries.size(), max_entries));
  for (const auto& entry : entries) {
    bool duplicate = false;
    for (const auto& kept : deduped) {
      if (kept.md5 == entry.md5 && kept.bytes_to_hash == entry.bytes_to_hash) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) deduped.push_back(entry);
    if (deduped.size() == max_entries) break;
  }

  entries = std::move(deduped);
  if (entries.size() > max_entries) entries.resize(max_entries);

  for (std::size_t i = 0; i < entries.size(); ++i) {
    entries[i].touched = static_cast<std::uint64_t>(entries.size() - i);
  }
}

std::optional<std::size_t> find_entry_index(const std::vector<Md5Xxh3CacheEntry>& entries,
                                            const std::array<unsigned char, 16>& md5,
                                            std::uint64_t bytes_to_hash) noexcept {
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].md5 == md5 && entries[i].bytes_to_hash == bytes_to_hash) return i;
  }
  return std::nullopt;
}

std::filesystem::path backup_cache_file(const std::filesystem::path& cache_file) noexcept {
  auto out = cache_file;
  out += ".bak";
  return out;
}

std::filesystem::path temporary_cache_file(const std::filesystem::path& cache_file) noexcept {
  auto out = cache_file;
  out += ".tmp";
  return out;
}

#if defined(_WIN32)
brokkr::core::Status ensure_replaceable_now(const std::filesystem::path& path) noexcept {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    if (ec) return brokkr::core::failf("Cannot inspect cache file {}: {}", path.string(), ec.message());
    return {};
  }
  if (ec) return brokkr::core::failf("Cannot inspect cache file {}: {}", path.string(), ec.message());

  HANDLE h = CreateFileW(path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
    return {};
  }

  const DWORD err = GetLastError();
  if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION || err == ERROR_ACCESS_DENIED) {
    return brokkr::core::failf("Cache file busy, skipping save: {}", path.string());
  }
  if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return {};
  return brokkr::core::failf("Cannot access cache file {} for replace: {}", path.string(), err);
}
#endif

brokkr::core::Result<ParsedCacheFile> parse_cache_file(const std::filesystem::path& path) noexcept {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return ParsedCacheFile{};
  if (ec) return brokkr::core::failf("Cannot access cache file {}: {}", path.string(), ec.message());

  const auto size = std::filesystem::file_size(path, ec);
  if (ec) return brokkr::core::failf("Cannot stat cache file {}: {}", path.string(), ec.message());
  if (size > kMaxCacheFileBytes) {
    ParsedCacheFile parsed;
    parsed.saw_corruption = true;
    return parsed;
  }

  std::ifstream in(path);
  if (!in.is_open()) return brokkr::core::failf("Cannot open cache file {}", path.string());

  ParsedCacheFile parsed;
  std::string line;
  std::size_t line_count = 0;
  while (std::getline(in, line)) {
    ++line_count;
    if (line_count > 4096) {
      parsed.saw_corruption = true;
      break;
    }

    if (!parsed.has_header) {
      if (line.empty() || line[0] == '#') continue;
      if (line != kCacheHeader) {
        parsed.saw_corruption = true;
        break;
      }
      parsed.has_header = true;
      continue;
    }

    if (line.empty() || line[0] == '#') continue;

    std::istringstream iss(line);
    std::string touched_str;
    std::string bytes_str;
    std::string md5_str;
    std::string xxh3_str;
    std::string extra;
    if (!(iss >> touched_str >> bytes_str >> md5_str >> xxh3_str) || (iss >> extra)) {
      parsed.saw_corruption = true;
      continue;
    }

    Md5Xxh3CacheEntry entry;
    if (!parse_u64(touched_str, entry.touched)) {
      parsed.saw_corruption = true;
      continue;
    }
    if (!parse_u64(bytes_str, entry.bytes_to_hash)) {
      parsed.saw_corruption = true;
      continue;
    }
    if (!parse_md5_hex(md5_str, entry.md5)) {
      parsed.saw_corruption = true;
      continue;
    }
    if (xxh3_str.size() != kXxh3HexChars || !parse_u64_hex(xxh3_str, entry.xxh3_64)) {
      parsed.saw_corruption = true;
      continue;
    }

    parsed.entries.push_back(std::move(entry));
  }

  if (!in.eof() && in.fail()) return brokkr::core::failf("Cannot read cache file {}", path.string());

  if (!parsed.has_header && !parsed.entries.empty()) parsed.saw_corruption = true;

  normalize_entries(parsed.entries, kMaxEntriesDefault);
  return parsed;
}

brokkr::core::Status replace_cache_file(const std::filesystem::path& src,
                                        const std::filesystem::path& dst,
                                        const std::filesystem::path& backup) noexcept {
#if defined(_WIN32)
  auto dst_ready = ensure_replaceable_now(dst);
  if (!dst_ready) return dst_ready;

  auto bak_ready = ensure_replaceable_now(backup);
  if (!bak_ready) return bak_ready;

  if (!ReplaceFileW(dst.c_str(), src.c_str(), backup.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
    const DWORD replace_error = GetLastError();
    if (replace_error == ERROR_FILE_NOT_FOUND || replace_error == ERROR_PATH_NOT_FOUND) {
      if (MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return {};
      return brokkr::core::failf("Cannot move cache file {} into place: {}", dst.string(), GetLastError());
    }
    return brokkr::core::failf("Cannot replace cache file {}: {}", dst.string(), replace_error);
  }
  return {};
#else
  std::error_code ec;
  if (std::filesystem::exists(dst, ec) && !ec) {
    std::filesystem::copy_file(dst, backup, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return brokkr::core::failf("Cannot back up cache file {}: {}", backup.string(), ec.message());
  } else if (ec) {
    return brokkr::core::failf("Cannot inspect cache file {}: {}", dst.string(), ec.message());
  }

  std::filesystem::rename(src, dst, ec);
  if (ec) return brokkr::core::failf("Cannot replace cache file {}: {}", dst.string(), ec.message());
  return {};
#endif
}

} // namespace

std::string md5_hex32(const std::array<unsigned char, 16>& digest) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string out(kMd5HexChars, '0');
  for (std::size_t i = 0; i < digest.size(); ++i) {
    out[2 * i + 0] = hex[(digest[i] >> 4) & 0x0F];
    out[2 * i + 1] = hex[digest[i] & 0x0F];
  }
  return out;
}

std::string xxh3_hex16(std::uint64_t digest) { return fmt::format("{:016x}", digest); }

std::filesystem::path md5_xxh3_cache_file(const std::filesystem::path& app_cache_dir) noexcept {
  return app_cache_dir / "md5_xxh3_cache.txt";
}

brokkr::core::Result<std::vector<Md5Xxh3CacheEntry>> load_md5_xxh3_cache(
    const std::filesystem::path& cache_file) noexcept {
  const std::array candidates = {cache_file, backup_cache_file(cache_file), temporary_cache_file(cache_file)};

  bool saw_corruption = false;
  for (const auto& candidate : candidates) {
    auto parsed = parse_cache_file(candidate);
    if (!parsed) return brokkr::core::fail(std::move(parsed.error()));

    if (!parsed->has_header) {
      saw_corruption = saw_corruption || parsed->saw_corruption;
      continue;
    }

    if (parsed->saw_corruption) saw_corruption = true;
    return parsed->entries;
  }

  if (saw_corruption) return std::vector<Md5Xxh3CacheEntry>{};
  return std::vector<Md5Xxh3CacheEntry>{};
}

brokkr::core::Status save_md5_xxh3_cache(const std::filesystem::path& cache_file,
                                         std::vector<Md5Xxh3CacheEntry> entries,
                                         std::size_t max_entries) noexcept {
  normalize_entries(entries, max_entries);

  std::error_code ec;
  const auto parent = cache_file.parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, ec);
  if (ec) return brokkr::core::failf("Cannot create cache directory {}: {}", parent.string(), ec.message());

  const auto tmp_file = temporary_cache_file(cache_file);
  const auto bak_file = backup_cache_file(cache_file);

  {
    std::ofstream out(tmp_file, std::ios::trunc);
    if (!out.is_open()) return brokkr::core::failf("Cannot write cache file {}", tmp_file.string());

    out << kCacheHeader << '\n';
    for (const auto& entry : entries) {
      out << entry.touched << ' ' << entry.bytes_to_hash << ' ' << md5_hex32(entry.md5) << ' '
          << xxh3_hex16(entry.xxh3_64) << '\n';
    }

    out.flush();
    if (!out.good()) return brokkr::core::failf("Cannot flush cache file {}", tmp_file.string());
  }

  auto verify_tmp = parse_cache_file(tmp_file);
  if (!verify_tmp) {
    std::error_code rm_ec;
    std::filesystem::remove(tmp_file, rm_ec);
    return brokkr::core::fail(std::move(verify_tmp.error()));
  }
  if (!verify_tmp->has_header || verify_tmp->saw_corruption) {
    std::error_code rm_ec;
    std::filesystem::remove(tmp_file, rm_ec);
    return brokkr::core::failf("Refusing to install malformed cache file {}", tmp_file.string());
  }

  auto replace_st = replace_cache_file(tmp_file, cache_file, bak_file);
  if (!replace_st) {
    std::error_code rm_ec;
    std::filesystem::remove(tmp_file, rm_ec);
    return replace_st;
  }

  return {};
}

std::optional<std::uint64_t> lookup_md5_xxh3_cache(std::vector<Md5Xxh3CacheEntry>& entries,
                                                   const std::array<unsigned char, 16>& md5,
                                                   std::uint64_t bytes_to_hash) noexcept {
  const auto idx = find_entry_index(entries, md5, bytes_to_hash);
  if (!idx) return std::nullopt;

  entries[*idx].touched = next_touch(entries);
  return entries[*idx].xxh3_64;
}

bool forget_md5_xxh3_cache(std::vector<Md5Xxh3CacheEntry>& entries,
                           const std::array<unsigned char, 16>& md5,
                           std::uint64_t bytes_to_hash) noexcept {
  const auto idx = find_entry_index(entries, md5, bytes_to_hash);
  if (!idx) return false;

  entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(*idx));
  normalize_entries(entries, entries.size());
  return true;
}

void remember_md5_xxh3_cache(std::vector<Md5Xxh3CacheEntry>& entries,
                             const std::array<unsigned char, 16>& md5,
                             std::uint64_t bytes_to_hash,
                             std::uint64_t xxh3_64,
                             std::size_t max_entries) noexcept {
  const auto idx = find_entry_index(entries, md5, bytes_to_hash);
  const auto touch = next_touch(entries);
  if (idx) {
    auto& entry = entries[*idx];
    entry.xxh3_64 = xxh3_64;
    entry.touched = touch;
  } else {
    Md5Xxh3CacheEntry entry;
    entry.md5 = md5;
    entry.bytes_to_hash = bytes_to_hash;
    entry.xxh3_64 = xxh3_64;
    entry.touched = touch;
    entries.push_back(std::move(entry));
  }

  normalize_entries(entries, max_entries);
}

} // namespace brokkr::app