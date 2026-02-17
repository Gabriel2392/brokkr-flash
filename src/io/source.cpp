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

#include "io/source.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

#include <spdlog/spdlog.h>

namespace brokkr::io {

class RawFileSource final : public ByteSource {
public:
  explicit RawFileSource(std::filesystem::path p, std::uint64_t size)
    : path_(std::move(p)), in_(path_, std::ios::binary), size_(size)
  {}

  bool opened() const noexcept { return in_.is_open(); }

  std::string display_name() const override { return path_.string(); }
  std::uint64_t size() const override { return size_; }

  std::size_t read(std::span<std::byte> out) override {
    if (out.empty()) return 0;
    in_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    const auto n = in_.gcount();
    return (n <= 0) ? 0u : static_cast<std::size_t>(n);
  }

private:
  std::filesystem::path path_;
  std::ifstream in_;
  std::uint64_t size_ = 0;
};

class TarEntrySource final : public ByteSource {
public:
  TarEntrySource(std::filesystem::path tar, TarEntry e)
    : tar_path_(std::move(tar))
    , entry_(std::move(e))
    , in_(tar_path_, std::ios::binary)
    , remaining_(entry_.size)
  {}

  bool opened() const noexcept { return in_.is_open(); }

  bool seek_to_data() noexcept {
    if (!in_.is_open()) return false;
    if (entry_.data_offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) return false;
    in_.seekg(static_cast<std::streamoff>(entry_.data_offset), std::ios::beg);
    return in_.good();
  }

  std::string display_name() const override { return tar_path_.string() + ":" + entry_.name; }
  std::uint64_t size() const override { return entry_.size; }

  std::size_t read(std::span<std::byte> out) override {
    if (remaining_ == 0 || out.empty()) return 0;
    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining_, out.size()));
    in_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(want));
    const auto n = in_.gcount();
    if (n <= 0) return 0;
    remaining_ -= static_cast<std::uint64_t>(n);
    return static_cast<std::size_t>(n);
  }

private:
  std::filesystem::path tar_path_;
  TarEntry entry_;
  std::ifstream in_;
  std::uint64_t remaining_ = 0;
};

brokkr::core::Result<std::unique_ptr<ByteSource>> open_raw_file(const std::filesystem::path& path) noexcept {
  std::error_code ec;
  const auto sz = std::filesystem::file_size(path, ec);
  if (ec) return brokkr::core::Result<std::unique_ptr<ByteSource>>::Failf("open_raw_file: stat failed: {}", path.string());

  auto ptr = std::make_unique<RawFileSource>(path, static_cast<std::uint64_t>(sz));
  if (!ptr->opened()) {
    return brokkr::core::Result<std::unique_ptr<ByteSource>>::Failf("open_raw_file: cannot open: {}", path.string());
  }

  return brokkr::core::Result<std::unique_ptr<ByteSource>>::Ok(std::move(ptr));
}

brokkr::core::Result<std::unique_ptr<ByteSource>> open_tar_entry(const std::filesystem::path& tar_path, const TarEntry& entry) noexcept {
  auto ptr = std::make_unique<TarEntrySource>(tar_path, entry);

  if (!ptr->opened()) {
    return brokkr::core::Result<std::unique_ptr<ByteSource>>::Failf("open_tar_entry: cannot open tar: {}", tar_path.string());
  }
  if (!ptr->seek_to_data()) {
    return brokkr::core::Result<std::unique_ptr<ByteSource>>::Failf("open_tar_entry: seek failed: {}", tar_path.string());
  }

  return brokkr::core::Result<std::unique_ptr<ByteSource>>::Ok(std::move(ptr));
}

} // namespace brokkr::io
