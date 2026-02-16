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
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace brokkr::odin {

namespace {

static bool is_lz4_name(std::string_view base) {
  return brokkr::core::ends_with_ci(base, ".lz4");
}

static std::string strip_lz4_suffix(std::string s) {
  if (s.size() >= 4 && brokkr::core::ends_with_ci(s, ".lz4"))
    s.resize(s.size() - 4);
  return s;
}

static std::string_view trim_ws(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' ||
                        s.front() == '\r' || s.front() == '\n'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                        s.back() == '\r' || s.back() == '\n'))
    s.remove_suffix(1);
  return s;
}

static std::optional<std::string>
read_text(io::ByteSource &src, std::size_t max_bytes, std::string_view what) {
  const std::uint64_t n64 = src.size();
  if (n64 > max_bytes) {
    spdlog::error("{} is too large: {} bytes (limit {})", what, n64, max_bytes);
    return std::nullopt;
  }
  const std::size_t n = static_cast<std::size_t>(n64);

  std::string s(n, '\0');
  for (std::size_t off = 0; off < n;) {
    const std::size_t got = src.read(
        std::as_writable_bytes(std::span<char>(s.data() + off, n - off)));
    if (!got) {
      spdlog::error("Failed to read {}: read returned 0 bytes at offset {}",
                    what, off);
      return std::nullopt;
    }
    off += got;
  }
  return s;
}

static std::optional<std::vector<std::string>>
parse_download_list(std::string_view txt) {
  std::vector<std::string> names;
  std::unordered_set<std::string> seen;

  for (std::size_t pos = 0; pos <= txt.size();) {
    const std::size_t next = txt.find('\n', pos);
    const std::size_t end =
        (next == std::string_view::npos) ? txt.size() : next;
    auto line = trim_ws(txt.substr(pos, end - pos));
    pos = (next == std::string_view::npos) ? (txt.size() + 1) : (next + 1);

    if (line.empty())
      continue;
    std::string name(line);
    if (!seen.insert(name).second) {
      spdlog::error("Duplicate entry in download-list.txt: '{}'", name);
      return std::nullopt;
    }
    names.push_back(std::move(name));
  }

  if (names.empty()) {
    spdlog::error("download-list.txt is empty");
    return std::nullopt;
  }
  return names;
}

static bool is_download_list_name(std::string_view name) noexcept {
  return name == "meta-data/download-list.txt" ||
         name == "./meta-data/download-list.txt";
}

static std::optional<io::TarEntry>
find_download_list_entry(const io::TarArchive &tar) {
  for (const auto &e : tar.entries())
    if (is_download_list_name(e.name))
      return e;
  return std::nullopt;
}

static bool lists_equal(const std::vector<std::string> &a,
                        const std::vector<std::string> &b) noexcept {
  if (a.size() != b.size())
    return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i] != b[i])
      return false;
  return true;
}

static std::uint64_t lz4_content_size(ImageSpec &spec) {
  auto src = spec.open();
  return io::parse_lz4_frame_header_or_throw(*src).content_size;
}

static ImageSpec make_spec(ImageSpec::Kind kind, std::filesystem::path path,
                           io::TarEntry entry, std::string display,
                           std::string source_basename, std::uint64_t disk_size,
                           bool dl_mode) {
  ImageSpec spec;
  spec.kind = kind;
  spec.path = std::move(path);
  spec.entry = std::move(entry);

  spec.source_basename = std::move(source_basename);
  spec.disk_size = disk_size;
  spec.display = std::move(display);

  spec.lz4 = is_lz4_name(spec.source_basename);
  spec.basename =
      spec.lz4 ? strip_lz4_suffix(spec.source_basename) : spec.source_basename;
  spec.download_list_mode = dl_mode;

  spec.size = spec.lz4 ? lz4_content_size(spec) : spec.disk_size;
  return spec;
}

struct SourceCandidate {
  ImageSpec::Kind kind{};
  std::filesystem::path path;
  io::TarEntry entry{};
  std::string basename, source_basename, display;
  bool lz4 = false;
  std::uint64_t disk_size = 0;
};

static void merge(std::unordered_map<std::string, SourceCandidate> &out,
                  SourceCandidate c) {
  if (!c.basename.empty())
    out.insert_or_assign(c.basename, std::move(c));
}

static ImageSpec finalize(const SourceCandidate &c, bool dl_mode) {
  return make_spec(c.kind, c.path, c.entry, c.display, c.source_basename,
                   c.disk_size, dl_mode);
}

} // namespace

std::unique_ptr<io::ByteSource> ImageSpec::open() const {
  switch (kind) {
  case Kind::RawFile:
    return io::open_raw_file(path);
  case Kind::TarEntry:
    return io::open_tar_entry(path, entry);
  }
  spdlog::error("Invalid ImageSpec kind: {}", static_cast<int>(kind));
  return nullptr;
}

std::vector<ImageSpec>
expand_inputs_tar_or_raw(const std::vector<std::filesystem::path> &inputs) {
  std::vector<ImageSpec> out;
  std::optional<std::vector<std::string>> dl;

  for (const auto &p : inputs) {
    if (!io::TarArchive::is_tar_file(p.string()))
      continue;

    io::TarArchive tar{p.string(), true};
    if (auto e = find_download_list_entry(tar)) {
      auto src = io::open_tar_entry(p, *e);
      auto text = read_text(*src, 128 * 1024, "download-list.txt");
      if (!text) {
        spdlog::error("Failed to read download-list.txt from '{}'", p.string());
        continue;
      }
      auto names = parse_download_list(*text);
      if (!names) {
        spdlog::error("Failed to parse download-list.txt from '{}'",
                      p.string());
        continue;
      }
      if (!dl)
        dl = std::move(names);
      else if (!lists_equal(*dl, *names)) {
        spdlog::error(
            "Conflicting download-list.txt in '{}': contents differ from "
            "previously read list",
            p.string());
        return {};
      }
    }
  }

  if (dl) {
    std::unordered_map<std::string, SourceCandidate> cands;

    for (const auto &p : inputs) {
      if (io::TarArchive::is_tar_file(p.string())) {
        io::TarArchive tar{p.string(), true};
        for (const auto &e : tar.entries()) {
          if (is_download_list_name(e.name))
            continue;

          const std::string sb = io::basename(e.name);
          if (sb.empty() || sb.back() == '/')
            continue;

          const bool lz4 = is_lz4_name(sb);
          const std::string base = lz4 ? strip_lz4_suffix(sb) : sb;

          merge(cands, SourceCandidate{.kind = ImageSpec::Kind::TarEntry,
                                       .path = p,
                                       .entry = e,
                                       .basename = base,
                                       .source_basename = sb,
                                       .display = p.string() + ":" + e.name,
                                       .lz4 = lz4,
                                       .disk_size = e.size});
        }
        continue;
      }

      auto src = io::open_raw_file(p);
      const std::string sb = io::basename(p.string());
      const bool lz4 = is_lz4_name(sb);

      merge(cands, SourceCandidate{.kind = ImageSpec::Kind::RawFile,
                                   .path = p,
                                   .basename = lz4 ? strip_lz4_suffix(sb) : sb,
                                   .source_basename = sb,
                                   .display = p.string(),
                                   .lz4 = lz4,
                                   .disk_size = src->size()});
    }

    out.reserve(dl->size());
    for (const auto &name : *dl) {
      auto it = cands.find(name);
      if (it == cands.end())
        throw std::runtime_error("download-list.txt references missing file: " +
                                 name);
      out.push_back(finalize(it->second, true));
    }
    return out;
  }

  for (const auto &p : inputs) {
    if (io::TarArchive::is_tar_file(p.string())) {
      io::TarArchive tar{p.string(), true};
      for (const auto &e : tar.entries()) {
        if (is_download_list_name(e.name))
          continue;
        const auto sb = io::basename(e.name);
        if (sb.empty())
          continue;

        out.push_back(make_spec(ImageSpec::Kind::TarEntry, p, e,
                                p.string() + ":" + e.name, io::basename(e.name),
                                e.size, false));
      }
      continue;
    }

    auto src = io::open_raw_file(p);
    out.push_back(make_spec(ImageSpec::Kind::RawFile, p, {}, p.string(),
                            io::basename(p.string()), src->size(), false));
  }

  return out;
}

std::vector<FlashItem> map_to_pit(const pit::PitTable &pit_table,
                                  const std::vector<ImageSpec> &sources) {
  std::vector<FlashItem> items;
  items.reserve(sources.size());

  std::unordered_map<std::int32_t, std::size_t> by_part;

  for (const auto &s : sources) {
    if (s.basename.empty())
      continue;
    const auto *part = pit_table.find_by_file_name(s.basename);
    if (!part)
      continue;

    const auto [it, inserted] = by_part.emplace(part->id, items.size());
    if (inserted)
      items.push_back(FlashItem{.part = *part, .spec = s});
    else
      items[it->second] = FlashItem{.part = *part, .spec = s};
  }

  if (items.empty()) {
    spdlog::error("None of the input files match any partition in the PIT");
	throw std::runtime_error("No matching partitions");
  }
  return items;
}

namespace detail {

PreparedLz4Window prepare_lz4_window(brokkr::io::Lz4BlockStreamReader &r,
                                     std::uint64_t decomp_sent,
                                     std::size_t max_blocks,
                                     std::size_t packet_size,
                                     std::vector<std::byte> &stream) {
  const std::uint64_t total = r.content_size();
  const std::uint64_t rem = total - decomp_sent;

  PreparedLz4Window w{};
  if (rem > static_cast<std::uint64_t>(max_blocks) * kOneMiB) {
    w.decomp_size = static_cast<std::uint64_t>(max_blocks) * kOneMiB;
    w.last = false;
  } else {
    w.decomp_size = rem;
    w.last = true;
  }

  const std::size_t blocks =
      !w.last ? static_cast<std::size_t>(w.decomp_size / kOneMiB)
              : r.blocks_remaining_1m();

  stream.clear();
  stream.reserve(blocks * (static_cast<std::size_t>(kOneMiB) + 4));

  w.comp_size = r.read_n_blocks(blocks, stream);
  w.rounded_size = round_up64(w.comp_size, packet_size);

  stream.resize(static_cast<std::size_t>(w.rounded_size), std::byte{0});
  return w;
}

} // namespace detail
} // namespace brokkr::odin
