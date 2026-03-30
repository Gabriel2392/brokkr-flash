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

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

using brokkr::app::Md5Xxh3CacheEntry;

static int g_pass = 0;
static int g_fail = 0;

static std::array<unsigned char, 16> make_md5(unsigned char seed) {
  std::array<unsigned char, 16> out{};
  for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<unsigned char>(seed + i);
  return out;
}

static std::filesystem::path unique_test_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() / ("brokkr-md5-xxh3-" + std::to_string(stamp));
}

static void fail_msg(const char* label, const std::string& msg) {
  std::fprintf(stderr, "FAIL %s: %s\n", label, msg.c_str());
  ++g_fail;
}

static void pass() { ++g_pass; }

static void write_text_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::trunc);
  out << content;
}

static void test_roundtrip_save_load() {
  const auto dir = unique_test_dir();
  const auto cache_file = brokkr::app::md5_xxh3_cache_file(dir);

  std::vector<Md5Xxh3CacheEntry> entries;
  brokkr::app::remember_md5_xxh3_cache(entries, make_md5(0x10), 1024, 0x1111222233334444ULL);
  brokkr::app::remember_md5_xxh3_cache(entries, make_md5(0x20), 2048, 0x5555666677778888ULL);

  auto save_st = brokkr::app::save_md5_xxh3_cache(cache_file, entries);
  if (!save_st) {
    fail_msg("roundtrip_save_load", save_st.error());
    std::filesystem::remove_all(dir);
    return;
  }

  auto loaded = brokkr::app::load_md5_xxh3_cache(cache_file);
  if (!loaded) {
    fail_msg("roundtrip_save_load", loaded.error());
    std::filesystem::remove_all(dir);
    return;
  }

  if (loaded->size() != 2) {
    fail_msg("roundtrip_save_load", "expected 2 entries after reload");
    std::filesystem::remove_all(dir);
    return;
  }

  auto hit = brokkr::app::lookup_md5_xxh3_cache(*loaded, make_md5(0x20), 2048);
  if (!hit || *hit != 0x5555666677778888ULL) {
    fail_msg("roundtrip_save_load", "lookup did not return persisted XXH3 value");
    std::filesystem::remove_all(dir);
    return;
  }

  pass();
  std::filesystem::remove_all(dir);
}

static void test_eviction_keeps_most_recent_65535() {
  std::vector<Md5Xxh3CacheEntry> entries;
  for (std::uint64_t i = 0; i < 65540; ++i) {
    brokkr::app::remember_md5_xxh3_cache(entries, make_md5(static_cast<unsigned char>(i)), i, 0xABC00000ULL + i);
  }

  if (entries.size() != 65535) {
    fail_msg("eviction_keeps_most_recent_65535", "cache did not clamp to 65535 entries");
    return;
  }

  if (brokkr::app::lookup_md5_xxh3_cache(entries, make_md5(0), 0).has_value()) {
    fail_msg("eviction_keeps_most_recent_65535", "oldest entry was not evicted");
    return;
  }

  auto newest =
      brokkr::app::lookup_md5_xxh3_cache(entries, make_md5(static_cast<unsigned char>(65539)), 65539);
  if (!newest || *newest != 0xABC00000ULL + 65539) {
    fail_msg("eviction_keeps_most_recent_65535", "newest entry missing after eviction");
    return;
  }

  pass();
}

static void test_remember_updates_existing_pair() {
  std::vector<Md5Xxh3CacheEntry> entries;
  const auto md5 = make_md5(0x33);

  brokkr::app::remember_md5_xxh3_cache(entries, md5, 4096, 0x1111ULL);
  brokkr::app::remember_md5_xxh3_cache(entries, md5, 4096, 0x2222ULL);

  if (entries.size() != 1) {
    fail_msg("remember_updates_existing_pair", "duplicate cache entries were created");
    return;
  }

  auto hit = brokkr::app::lookup_md5_xxh3_cache(entries, md5, 4096);
  if (!hit || *hit != 0x2222ULL) {
    fail_msg("remember_updates_existing_pair", "existing cache entry was not updated");
    return;
  }

  pass();
}

static void test_forget_removes_existing_pair() {
  std::vector<Md5Xxh3CacheEntry> entries;
  const auto md5 = make_md5(0x66);

  brokkr::app::remember_md5_xxh3_cache(entries, md5, 4096, 0x1111ULL);
  brokkr::app::remember_md5_xxh3_cache(entries, make_md5(0x67), 8192, 0x2222ULL);

  if (!brokkr::app::forget_md5_xxh3_cache(entries, md5, 4096)) {
    fail_msg("forget_removes_existing_pair", "existing cache entry was not removed");
    return;
  }

  if (brokkr::app::lookup_md5_xxh3_cache(entries, md5, 4096).has_value()) {
    fail_msg("forget_removes_existing_pair", "removed cache entry was still found");
    return;
  }

  if (entries.size() != 1) {
    fail_msg("forget_removes_existing_pair", "wrong cache entry count after removal");
    return;
  }

  pass();
}

static void test_corrupt_primary_falls_back_to_backup() {
  const auto dir = unique_test_dir();
  const auto cache_file = brokkr::app::md5_xxh3_cache_file(dir);
  auto backup_file = cache_file;
  backup_file += ".bak";

  std::vector<Md5Xxh3CacheEntry> entries;
  brokkr::app::remember_md5_xxh3_cache(entries, make_md5(0x44), 8192, 0xABCDEF1234567890ULL);

  auto save_st = brokkr::app::save_md5_xxh3_cache(cache_file, entries);
  if (!save_st) {
    fail_msg("corrupt_primary_falls_back_to_backup", save_st.error());
    std::filesystem::remove_all(dir);
    return;
  }

  brokkr::app::remember_md5_xxh3_cache(entries, make_md5(0x55), 16384, 0x0123456789ABCDEFULL);
  save_st = brokkr::app::save_md5_xxh3_cache(cache_file, entries);
  if (!save_st) {
    fail_msg("corrupt_primary_falls_back_to_backup", save_st.error());
    std::filesystem::remove_all(dir);
    return;
  }

  if (!std::filesystem::exists(backup_file)) {
    fail_msg("corrupt_primary_falls_back_to_backup", "backup cache file was not created");
    std::filesystem::remove_all(dir);
    return;
  }

  write_text_file(cache_file, "not a valid cache\n123\n");

  auto loaded = brokkr::app::load_md5_xxh3_cache(cache_file);
  if (!loaded) {
    fail_msg("corrupt_primary_falls_back_to_backup", loaded.error());
    std::filesystem::remove_all(dir);
    return;
  }

  auto hit = brokkr::app::lookup_md5_xxh3_cache(*loaded, make_md5(0x44), 8192);
  if (!hit || *hit != 0xABCDEF1234567890ULL) {
    fail_msg("corrupt_primary_falls_back_to_backup", "did not recover valid entry from backup cache");
    std::filesystem::remove_all(dir);
    return;
  }

  pass();
  std::filesystem::remove_all(dir);
}

static void test_headerless_cache_is_rejected() {
  const auto dir = unique_test_dir();
  const auto cache_file = brokkr::app::md5_xxh3_cache_file(dir);

  write_text_file(cache_file, "1 4096 00112233445566778899aabbccddeeff 0123456789abcdef\n");

  auto loaded = brokkr::app::load_md5_xxh3_cache(cache_file);
  if (!loaded) {
    fail_msg("headerless_cache_is_rejected", loaded.error());
    std::filesystem::remove_all(dir);
    return;
  }

  if (!loaded->empty()) {
    fail_msg("headerless_cache_is_rejected", "headerless cache content should be ignored");
    std::filesystem::remove_all(dir);
    return;
  }

  pass();
  std::filesystem::remove_all(dir);
}

#if defined(_WIN32)
static void test_locked_cache_file_fails_fast() {
  const auto dir = unique_test_dir();
  const auto cache_file = brokkr::app::md5_xxh3_cache_file(dir);

  std::vector<Md5Xxh3CacheEntry> entries;
  brokkr::app::remember_md5_xxh3_cache(entries, make_md5(0x77), 4096, 0xABCDEF1234567890ULL);

  auto save_st = brokkr::app::save_md5_xxh3_cache(cache_file, entries);
  if (!save_st) {
    fail_msg("locked_cache_file_fails_fast", save_st.error());
    std::filesystem::remove_all(dir);
    return;
  }

  HANDLE h = CreateFileW(cache_file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    fail_msg("locked_cache_file_fails_fast", "could not open cache file for lock simulation");
    std::filesystem::remove_all(dir);
    return;
  }

  brokkr::app::remember_md5_xxh3_cache(entries, make_md5(0x78), 8192, 0x0123456789ABCDEFULL);
  save_st = brokkr::app::save_md5_xxh3_cache(cache_file, entries);
  CloseHandle(h);

  if (save_st) {
    fail_msg("locked_cache_file_fails_fast", "save unexpectedly succeeded while cache file was locked");
    std::filesystem::remove_all(dir);
    return;
  }

  if (save_st.error().find("Cache file busy") == std::string::npos) {
    fail_msg("locked_cache_file_fails_fast", "locked cache did not report a busy-file failure");
    std::filesystem::remove_all(dir);
    return;
  }

  pass();
  std::filesystem::remove_all(dir);
}
#endif

int main() {
  test_roundtrip_save_load();
  test_eviction_keeps_most_recent_65535();
  test_remember_updates_existing_pair();
  test_forget_removes_existing_pair();
  test_corrupt_primary_falls_back_to_backup();
  test_headerless_cache_is_rejected();
#if defined(_WIN32)
  test_locked_cache_file_fails_fast();
#endif

  std::fprintf(stdout, "md5_xxh3_cache: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}