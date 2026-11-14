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
#include "core/path_utf8.hpp"
#include "core/utils.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <optional>
#include <set>
#include <utility>
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
constexpr std::size_t kMaxEntriesDefault = 65535;
constexpr std::uintmax_t kMaxCacheFileBytes = 1024 * 1024;

struct ParsedCacheFile {
  std::vector<Md5Xxh3CacheEntry> entries;
  bool has_header = false;
  bool saw_corruption = false;
};

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
  std::set<std::pair<std::array<unsigned char, 16>, std::uint64_t>> seen;
  for (const auto& entry : entries) {
    if (deduped.size() == max_entries) break;
    if (seen.emplace(entry.md5, entry.bytes_to_hash).second) deduped.push_back(entry);
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
    if (ec) return brokkr::core::failf("Cannot inspect cache file {}: {}", brokkr::core::path_to_utf8(path), ec.message());
    return {};
  }
  if (ec) return brokkr::core::failf("Cannot inspect cache file {}: {}", brokkr::core::path_to_utf8(path), ec.message());

  HANDLE h = CreateFileW(path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
    return {};
  }

  const DWORD err = GetLastError();
  if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION || err == ERROR_ACCESS_DENIED) {
    return brokkr::core::failf("Cache file busy, skipping save: {}", brokkr::core::path_to_utf8(path));
  }
  if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return {};
  return brokkr::core::failf("Cannot access cache file {} for replace: {}", brokkr::core::path_to_utf8(path), err);
}
#endif

brokkr::core::Result<ParsedCacheFile> parse_cache_file(const std::filesystem::path& path) noexcept {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return ParsedCacheFile{};
  if (ec) return brokkr::core::failf("Cannot access cache file {}: {}", brokkr::core::path_to_utf8(path), ec.message());

  const auto size = std::filesystem::file_size(path, ec);
  if (ec) return brokkr::core::failf("Cannot stat cache file {}: {}", brokkr::core::path_to_utf8(path), ec.message());
  if (size > kMaxCacheFileBytes) {
    ParsedCacheFile parsed;
    parsed.saw_corruption = true;
    return parsed;
  }

  std::ifstream in(path);
  if (!in.is_open()) return brokkr::core::failf("Cannot open cache file {}", brokkr::core::path_to_utf8(path));

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
    if (!brokkr::core::parse_int_exact(touched_str, entry.touched, 10)) {
      parsed.saw_corruption = true;
      continue;
    }
    if (!brokkr::core::parse_int_exact(bytes_str, entry.bytes_to_hash, 10)) {
      parsed.saw_corruption = true;
      continue;
    }
    if (!brokkr::core::parse_md5_hex(md5_str, entry.md5)) {
      parsed.saw_corruption = true;
      continue;
    }
    if (xxh3_str.size() != kXxh3HexChars || !brokkr::core::parse_int_exact(xxh3_str, entry.xxh3_64, 16)) {
      parsed.saw_corruption = true;
      continue;
    }

    parsed.entries.push_back(std::move(entry));
  }

  if (!in.eof() && in.fail()) return brokkr::core::failf("Cannot read cache file {}", brokkr::core::path_to_utf8(path));

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
      return brokkr::core::failf("Cannot move cache file {} into place: {}", brokkr::core::path_to_utf8(dst), GetLastError());
    }
    return brokkr::core::failf("Cannot replace cache file {}: {}", brokkr::core::path_to_utf8(dst), replace_error);
  }
  return {};
#else
  std::error_code ec;
  if (std::filesystem::exists(dst, ec) && !ec) {
    std::filesystem::copy_file(dst, backup, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) return brokkr::core::failf("Cannot back up cache file {}: {}", brokkr::core::path_to_utf8(backup), ec.message());
  } else if (ec) {
    return brokkr::core::failf("Cannot inspect cache file {}: {}", brokkr::core::path_to_utf8(dst), ec.message());
  }

  std::filesystem::rename(src, dst, ec);
  if (ec) return brokkr::core::failf("Cannot replace cache file {}: {}", brokkr::core::path_to_utf8(dst), ec.message());
  return {};
#endif
}

} // namespace

std::string md5_hex32(const std::array<unsigned char, 16>& digest) { return brokkr::core::format_hex_lower(digest); }

std::string xxh3_hex16(std::uint64_t digest) { return fmt::format("{:016x}", digest); }

std::filesystem::path md5_xxh3_cache_file(const std::filesystem::path& app_cache_dir) noexcept {
  return app_cache_dir / "md5_xxh3_cache.txt";
}

brokkr::core::Result<std::vector<Md5Xxh3CacheEntry>> load_md5_xxh3_cache(
    const std::filesystem::path& cache_file) noexcept {
  const std::array candidates = {cache_file, backup_cache_file(cache_file), temporary_cache_file(cache_file)};

  for (const auto& candidate : candidates) {
    auto parsed = parse_cache_file(candidate);
    if (!parsed) return brokkr::core::fail(std::move(parsed.error()));
    if (parsed->has_header) return parsed->entries;
  }

  return std::vector<Md5Xxh3CacheEntry>{};
}

brokkr::core::Status save_md5_xxh3_cache(const std::filesystem::path& cache_file,
                                         std::vector<Md5Xxh3CacheEntry> entries,
                                         std::size_t max_entries) noexcept {
  normalize_entries(entries, max_entries);

  std::error_code ec;
  const auto parent = cache_file.parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, ec);
  if (ec) return brokkr::core::failf("Cannot create cache directory {}: {}", brokkr::core::path_to_utf8(parent), ec.message());

  const auto tmp_file = temporary_cache_file(cache_file);
  const auto bak_file = backup_cache_file(cache_file);

  {
    std::ofstream out(tmp_file, std::ios::trunc);
    if (!out.is_open()) return brokkr::core::failf("Cannot write cache file {}", brokkr::core::path_to_utf8(tmp_file));

    out << kCacheHeader << '\n';
    for (const auto& entry : entries) {
      out << entry.touched << ' ' << entry.bytes_to_hash << ' ' << md5_hex32(entry.md5) << ' '
          << xxh3_hex16(entry.xxh3_64) << '\n';
    }

    out.flush();
    if (!out.good()) return brokkr::core::failf("Cannot flush cache file {}", brokkr::core::path_to_utf8(tmp_file));
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
    return brokkr::core::failf("Refusing to install malformed cache file {}", brokkr::core::path_to_utf8(tmp_file));
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