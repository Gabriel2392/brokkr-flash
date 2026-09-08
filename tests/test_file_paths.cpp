#include "app/md5_verify.hpp"
#include "app/md5_xxh3_cache.hpp"
#include "app/pit_file.hpp"
#include "core/path_utf8.hpp"
#include "io/source.hpp"
#include "platform/platform_all.hpp"
#include "protocol/odin/flash.hpp"
#include "third_party/md5/md5.h"

#include <array>
#include <chrono>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using brokkr::core::path_from_utf8;
using brokkr::core::path_to_utf8;

namespace {
fs::path cache_dir;
int checks = 0;
constexpr std::string_view payload = "Unicode firmware path test\n";

void check(bool ok, std::string_view message) {
  ++checks;
  if (!ok) throw std::runtime_error(std::string(message));
}

template<class T>
auto require(T result) {
  check(static_cast<bool>(result), result ? std::string_view{} : std::string_view(result.error()));
  return std::move(*result);
}

struct TempDir {
  fs::path path = fs::temp_directory_path() /
      ("brokkr-paths-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TempDir() { fs::create_directory(path); }
  ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};

void write_file(const fs::path& path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  check(static_cast<bool>(out), "write fixture");
}

std::string make_tar(std::string_view entry_name) {
  std::string tar(2048, '\0');
  check(entry_name.size() < 100 && payload.size() <= 512, "fixture fits USTAR fields");
  std::memcpy(tar.data(), entry_name.data(), entry_name.size());
  std::snprintf(tar.data() + 100, 8, "%07o", 0644);
  std::snprintf(tar.data() + 124, 12, "%011llo", static_cast<unsigned long long>(payload.size()));
  tar[156] = '0';
  std::memcpy(tar.data() + 257, "ustar", 5);
  std::memcpy(tar.data() + 263, "00", 2);
  std::memset(tar.data() + 148, ' ', 8);
  unsigned checksum = 0;
  for (std::size_t i = 0; i < 512; ++i) checksum += static_cast<unsigned char>(tar[i]);
  std::snprintf(tar.data() + 148, 7, "%06o", checksum);
  tar[155] = ' ';
  std::memcpy(tar.data() + 512, payload.data(), payload.size());
  return tar;
}

std::string wrap_md5(const std::string& tar) {
  MD5_CTX ctx;
  std::array<unsigned char, 16> digest{};
  md5_init(&ctx);
  md5_update(&ctx, reinterpret_cast<const unsigned char*>(tar.data()), tar.size());
  md5_final(&ctx, digest.data());
  return tar + brokkr::app::md5_hex32(digest) + "  firmware.tar\n";
}

void check_image(const fs::path& path, std::string_view basename = "boot.img") {
  const auto specs = require(brokkr::odin::expand_inputs_tar_or_raw({path}));
  check(specs.size() == 1 && specs.front().basename == basename, "expanded image basename");
  auto reader = require(specs.front().open());
  std::array<std::byte, payload.size()> bytes{};
  check(reader->read(bytes) == bytes.size(), "image read size");
  check(std::memcmp(bytes.data(), payload.data(), bytes.size()) == 0, "image payload unchanged");
}

void test_unicode_files(const fs::path& root) {
  const std::array names = {u8"ASCII & [space]", u8"Flix Pozna\u0144", u8"\u0141\u00f3d\u017a",
      u8"\u65e5\u672c\u8a9e", u8"\ud55c\uae00", u8"\u0627\u0644\u0639\u0631\u0628\u064a\u0629",
      u8"\U0001f600 phone", u8"caf\u00e9", u8"cafe\u0301"};
  const std::string tar = make_tar("payload/boot.img");
  for (const auto* name : names) {
    const fs::path leaf(name);
    const fs::path dir = root / leaf;
    fs::create_directories(dir);
    const auto raw = dir / "boot.img";
    const auto archive = dir / (leaf.native() + fs::path(".tar").native());
    const auto wrapped = dir / (leaf.native() + fs::path(".tar.md5").native());
    const auto pit = dir / (leaf.native() + fs::path(".pit").native());
    write_file(raw, payload);
    write_file(archive, tar);
    write_file(wrapped, wrap_md5(tar));
    write_file(pit, payload);

    for (const auto& path : {raw, archive, wrapped, pit}) {
      check(path_from_utf8(path_to_utf8(path)) == path, "native/UTF-8 round trip");
      auto src = require(brokkr::io::open_file_source(path));
      auto alternate = require(brokkr::io::open_file_source(path.parent_path() / "." / path.filename()));
      check(src->label() == path_to_utf8(path), "UTF-8 display label");
      check(src->identity() == alternate->identity(), "canonical source identity");
      check(path_from_utf8(src->identity()) == fs::weakly_canonical(path), "UTF-8 identity path");
      check(src->size() == fs::file_size(path), "file size");
    }
    check_image(raw);
    check_image(archive);
    check_image(wrapped);
    const auto pit_bytes = require(brokkr::app::read_pit_file(pit));
    check(pit_bytes.size() == payload.size() && std::memcmp(pit_bytes.data(), payload.data(), payload.size()) == 0,
          "PIT contents");

    const auto jobs = require(brokkr::app::md5_jobs({wrapped}));
    check(jobs.size() == 1 && jobs.front().display_name == path_to_utf8(wrapped), "MD5 job label");
    brokkr::app::clear_session_verify_cache();
    check(static_cast<bool>(brokkr::app::md5_verify(jobs, {})), "MD5 verification");
    check(brokkr::app::md5_verify_name(jobs) == "XXH3", "persistent Unicode cache hit");
    brokkr::app::clear_session_verify_cache();
    check(static_cast<bool>(brokkr::app::md5_verify(jobs, {})), "cached XXH3 verification");

    auto entries = require(brokkr::app::load_md5_xxh3_cache(brokkr::app::md5_xxh3_cache_file(cache_dir)));
    const auto local_cache = brokkr::app::md5_xxh3_cache_file(dir / leaf);
    check(static_cast<bool>(brokkr::app::save_md5_xxh3_cache(local_cache, entries)), "Unicode cache save");
    check(static_cast<bool>(brokkr::app::save_md5_xxh3_cache(local_cache, entries)), "Unicode cache replacement");
    check(require(brokkr::app::load_md5_xxh3_cache(local_cache)).size() == entries.size(), "Unicode cache reload");

    const auto missing = dir / leaf;
    const auto error = brokkr::io::open_file_source(missing);
    check(!error && error.error().find(path_to_utf8(missing)) != std::string::npos, "Unicode non-file error");
  }

  auto corrupt = wrap_md5(tar);
  corrupt[512] ^= 1;
  const auto bad_path = root / "bad.tar.md5";
  write_file(bad_path, corrupt);
  const auto bad_jobs = require(brokkr::app::md5_jobs({bad_path}));
  brokkr::app::clear_session_verify_cache();
  check(!brokkr::app::md5_verify(bad_jobs, {}), "corrupt firmware rejected even after cache hit");
}

void test_legacy_names(const fs::path& root) {
  const std::string legacy = "caf\xe9";
  const auto archive = root / "legacy.tar";
  write_file(archive, make_tar(legacy + "/boot.img"));
  check_image(archive);
  write_file(archive, make_tar(legacy + ".img"));
  check_image(archive, legacy + ".img");
  check(brokkr::io::tar_basename("dir/" + legacy + ".img") == legacy + ".img", "legacy basename bytes");
  const std::string shift_jis = "\x83\x5c.img";
  write_file(archive, make_tar("dir/" + shift_jis));
  check_image(archive, shift_jis);
  check(brokkr::io::basename("/dir/").empty(), "trailing separator");
  check(brokkr::io::basename("").empty(), "empty basename");
  const auto long_name = path_to_utf8(fs::path(u8"123456789\u00e9\U0001f600.tar.md5"));
  check(brokkr::io::basename("dir/" + long_name) == long_name, "complete multibyte display name");
#if defined(_WIN32)
  check(brokkr::io::basename("C:boot.img") == "boot.img", "drive-relative basename");
  check(brokkr::io::basename("\\\\server\\share\\boot.img") == "boot.img", "UNC basename");
  const fs::path unc(L"\\\\server\\share\\Pozna\u0144\\boot.img");
  check(path_from_utf8(path_to_utf8(unc)) == unc, "UNC Unicode round trip");
#else
#if defined(__linux__)


  const auto raw = root / fs::path(legacy + ".img");
  write_file(raw, payload);
  check_image(raw, legacy + ".img");
#endif
  check(brokkr::io::basename("literal\\boot.img") == "literal\\boot.img", "POSIX backslash retained");
#endif
}
}



#if defined(_WIN32)
namespace brokkr::windows {
#else
namespace brokkr::posix_common {
#endif
brokkr::core::Result<fs::path> app_cache_dir() noexcept { return cache_dir; }
}

int main() {
  try {
    std::setlocale(LC_ALL, "C");
    TempDir temp;
    cache_dir = temp.path / fs::path(u8"\u7f13\u5b58 \U0001f600");
    check(path_from_utf8({}).empty(), "empty UTF-8 path");
    test_unicode_files(temp.path);
    test_legacy_names(temp.path);
    std::cout << "file_paths: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "file_paths: " << e.what() << '\n';
    return 1;
  }
}
