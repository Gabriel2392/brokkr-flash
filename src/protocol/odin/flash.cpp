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

#include "protocol/odin/flash.hpp"

#include "core/str.hpp"
#include "io/lz4_frame.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <spdlog/spdlog.h>

namespace brokkr::odin {

namespace {

static bool is_lz4_name(std::string_view base) { return brokkr::core::ends_with_ci(base, ".lz4"); }

static std::string strip_lz4_suffix(std::string s) {
  if (s.size() >= 4 && brokkr::core::ends_with_ci(s, ".lz4")) s.resize(s.size() - 4);
  return s;
}

static std::string_view trim_ws(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
    s.remove_suffix(1);
  return s;
}

static brokkr::core::Result<std::string> read_text(io::ByteSource& src, std::size_t max_bytes,
                                                   std::string_view what) noexcept {
  const std::uint64_t n64 = src.size();
  if (n64 > max_bytes) return brokkr::core::fail("read_text: too large: " + std::string(what));
  const std::size_t n = static_cast<std::size_t>(n64);

  std::string s(n, '\0');
  for (std::size_t off = 0; off < n;) {
    const std::size_t got = src.read(std::as_writable_bytes(std::span<char>(s.data() + off, n - off)));
    if (!got) return brokkr::core::fail("Short read: " + std::string(what));
    off += got;
  }
  return s;
}

static brokkr::core::Result<std::vector<std::string>> parse_download_list(std::string_view txt) noexcept {
  std::vector<std::string> names;
  std::unordered_set<std::string> seen;

  for (std::size_t pos = 0; pos <= txt.size();) {
    const std::size_t next = txt.find('\n', pos);
    const std::size_t end = (next == std::string_view::npos) ? txt.size() : next;
    auto line = trim_ws(txt.substr(pos, end - pos));
    pos = (next == std::string_view::npos) ? (txt.size() + 1) : (next + 1);

    if (line.empty()) continue;
    std::string name(line);
    if (!seen.insert(name).second) return brokkr::core::fail("download-list.txt contains duplicate entry: " + name);
    names.push_back(std::move(name));
  }

  if (names.empty()) return brokkr::core::fail("download-list.txt is empty");
  return names;
}

static bool is_download_list_name(std::string_view name) noexcept {
  return name == "meta-data/download-list.txt" || name == "./meta-data/download-list.txt";
}

static std::optional<io::TarEntry> find_download_list_entry(const io::TarArchive& tar) {
  for (const auto& e : tar.entries())
    if (is_download_list_name(e.name)) return e;
  return std::nullopt;
}

static bool lists_equal(const std::vector<std::string>& a, const std::vector<std::string>& b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i] != b[i]) return false;
  return true;
}

static brokkr::core::Result<std::uint64_t> lz4_content_size(const ImageSpec& spec) noexcept {
  BRK_TRYV(src, spec.open());
  BRK_TRYV(h, io::parse_lz4_frame_header(*src));
  return h.content_size;
}

static brokkr::core::Result<ImageSpec> make_spec(ImageSpec::Kind kind, std::filesystem::path path, io::TarEntry entry,
                                                 std::string display, std::string source_basename,
                                                 std::uint64_t disk_size) noexcept {
  ImageSpec spec;
  spec.kind = kind;
  spec.path = std::move(path);
  spec.entry = std::move(entry);

  spec.source_basename = std::move(source_basename);
  spec.disk_size = disk_size;
  spec.display = std::move(display);

  spec.lz4 = is_lz4_name(spec.source_basename);
  spec.basename = spec.lz4 ? strip_lz4_suffix(spec.source_basename) : spec.source_basename;

  if (spec.lz4) {
    BRK_TRYV(sz, lz4_content_size(spec));
    spec.size = sz;
  } else {
    spec.size = spec.disk_size;
  }

  return spec;
}

} // namespace

brokkr::core::Result<std::unique_ptr<io::ByteSource>> ImageSpec::open() const noexcept {
  switch (kind) {
    case Kind::RawFile: return io::open_raw_file(path);
    case Kind::TarEntry: return io::open_tar_entry(path, entry);
  }
  return brokkr::core::fail("ImageSpec::open: invalid kind");
}

brokkr::core::Result<std::vector<ImageSpec>> expand_inputs_tar_or_raw(
    const std::vector<std::filesystem::path>& inputs) noexcept {
  std::vector<ImageSpec> out;

  std::vector<std::optional<io::TarArchive>> tars(inputs.size());
  std::optional<std::vector<std::string>> dl;

  for (std::size_t i = 0; i < inputs.size(); ++i) {
    const auto& p = inputs[i];
    if (!io::TarArchive::is_tar_file(p.string())) continue;

    BRK_TRYV(tar, io::TarArchive::open(p.string(), true));

    if (auto e = find_download_list_entry(tar)) {
      BRK_TRYV(src, io::open_tar_entry(p, *e));
      BRK_TRYV(txt, read_text(*src, 128 * 1024, "download-list.txt"));
      BRK_TRYV(names, parse_download_list(txt));

      if (!dl)
        dl = std::move(names);
      else if (!lists_equal(*dl, names))
        return brokkr::core::fail("Multiple download-list.txt files found with different contents");
    }

    tars[i].emplace(std::move(tar));
  }

  std::unordered_set<std::string> allow;
  if (dl) {
    allow.reserve(dl->size());
    for (const auto& n : *dl) allow.insert(n);
  }

  auto compute_basename = [](std::string_view sb) -> std::string {
    if (sb.empty() || sb.back() == '/') return {};
    return is_lz4_name(sb) ? strip_lz4_suffix(std::string(sb)) : std::string(sb);
  };

  struct Coord { std::size_t inp; std::size_t ent; };
  static constexpr std::size_t kRaw = static_cast<std::size_t>(-1);
  std::unordered_map<std::string, Coord> last_of;

  auto record = [&](const std::string& base, std::size_t inp, std::size_t ent) {
    auto [it, ins] = last_of.try_emplace(base, Coord{inp, ent});
    if (!ins) {
      spdlog::debug("Duplicate image '{}' across inputs \u2014 later occurrence wins", base);
      it->second = Coord{inp, ent};
    }
  };

  for (std::size_t i = 0; i < inputs.size(); ++i) {
    if (tars[i]) {
      const auto& entries = tars[i]->entries();
      for (std::size_t j = 0; j < entries.size(); ++j) {
        if (is_download_list_name(entries[j].name)) continue;
        const std::string sb = io::basename(entries[j].name);
        const std::string base = compute_basename(sb);
        if (base.empty()) continue;
        record(base, i, j);
      }
    } else {
      const std::string sb = io::basename(inputs[i].string());
      const std::string base = compute_basename(sb);
      if (base.empty()) continue;
      record(base, i, kRaw);
    }
  }

  std::vector<ImageSpec> pit_specs;
  std::unordered_set<std::string> emitted;

  for (std::size_t i = 0; i < inputs.size(); ++i) {
    const auto& p = inputs[i];

    if (tars[i]) {
      const auto& entries = tars[i]->entries();
      for (std::size_t j = 0; j < entries.size(); ++j) {
        const auto& e = entries[j];
        if (is_download_list_name(e.name)) continue;
        const std::string sb = io::basename(e.name);
        const std::string base = compute_basename(sb);
        if (base.empty()) continue;

        const auto lit = last_of.find(base);
        if (lit == last_of.end() || lit->second.inp != i || lit->second.ent != j) continue;

        const bool is_pit = brokkr::core::ends_with_ci(base, ".pit");
        if (dl && !is_pit && !allow.contains(base)) {
          spdlog::debug("Skipping {}: not in download-list.txt", sb);
          continue;
        }

        BRK_TRYV(spec, make_spec(ImageSpec::Kind::TarEntry, p, e, p.string() + ":" + e.name, sb, e.size));
        if (is_pit) {
          pit_specs.push_back(std::move(spec));
        } else {
          emitted.insert(base);
          out.push_back(std::move(spec));
        }
      }
    } else {
      const std::string sb = io::basename(p.string());
      const std::string base = compute_basename(sb);
      if (base.empty()) continue;

      const auto lit = last_of.find(base);
      if (lit == last_of.end() || lit->second.inp != i || lit->second.ent != kRaw) continue;

      const bool is_pit = brokkr::core::ends_with_ci(base, ".pit");
      if (dl && !is_pit && !allow.contains(base)) {
        spdlog::debug("Skipping {}: not in download-list.txt", sb);
        continue;
      }

      BRK_TRYV(src, io::open_raw_file(p));
      BRK_TRYV(spec, make_spec(ImageSpec::Kind::RawFile, p, {}, p.string(), sb, src->size()));
      if (is_pit) {
        pit_specs.push_back(std::move(spec));
      } else {
        emitted.insert(base);
        out.push_back(std::move(spec));
      }
    }
  }

  if (dl) {
    for (const auto& name : *dl) {
      if (!emitted.contains(name))
        spdlog::debug("download-list.txt references missing file: {} (skipping)", name);
    }
  }

  for (auto& s : pit_specs) out.push_back(std::move(s));
  return out;
}

brokkr::core::Result<std::vector<FlashItem>> map_to_pit(const pit::PitTable& pit_table,
                                                        const std::vector<ImageSpec>& sources) noexcept {
  std::vector<FlashItem> items;
  items.reserve(sources.size());

  std::unordered_map<std::int32_t, std::size_t> by_part;

  for (const auto& s : sources) {
    if (s.basename.empty()) continue;
    const auto* part = pit_table.find_by_file_name(s.basename);
    if (!part) continue;

    const auto [it, inserted] = by_part.emplace(part->id, items.size());
    if (inserted)
      items.push_back(FlashItem{.part = *part, .spec = s});
    else
      items[it->second] = FlashItem{.part = *part, .spec = s};
  }

  if (items.empty())
    spdlog::warn("No flashable items after PIT mapping");
  else
    spdlog::debug("PIT mapping: {} items", items.size());
  return items;
}

} // namespace brokkr::odin
