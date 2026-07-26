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
#include <utility>

namespace brokkr::io {

namespace {

class RangeSource final : public ByteSource {
 public:
  RangeSource(RandomAccessSourcePtr src, std::uint64_t offset, std::uint64_t size, std::string display)
      : src_(std::move(src)), offset_(offset), size_(size), display_(std::move(display)) {}

  std::string display_name() const override { return display_; }
  std::uint64_t size() const override { return size_; }

  std::size_t read(std::span<std::byte> out) override {
    if (cursor_ >= size_ || out.empty()) return 0;

    const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(size_ - cursor_, out.size()));
    auto got = src_->read_at(offset_ + cursor_, out.subspan(0, want));
    if (!got) {
      status_ = brokkr::core::fail(std::move(got.error()));
      return 0;
    }

    cursor_ += *got;
    return *got;
  }

  brokkr::core::Status status() const noexcept override { return status_; }

 private:
  RandomAccessSourcePtr src_;
  std::uint64_t offset_ = 0;
  std::uint64_t size_ = 0;
  std::uint64_t cursor_ = 0;
  std::string display_;
  brokkr::core::Status status_{};
};

} // namespace

std::unique_ptr<ByteSource> open_range(RandomAccessSourcePtr src, std::uint64_t offset, std::uint64_t size,
                                       std::string display) {
  return std::make_unique<RangeSource>(std::move(src), offset, size, std::move(display));
}

std::unique_ptr<ByteSource> open_tar_entry(RandomAccessSourcePtr src, const TarEntry& entry) {
  std::string display = src->label() + ":" + entry.name;
  return open_range(std::move(src), entry.data_offset, entry.size, std::move(display));
}

brokkr::core::Result<std::unique_ptr<ByteSource>> open_raw_file(const std::filesystem::path& path) noexcept {
  BRK_TRYV(src, open_file_source(path));
  const auto size = src->size();
  std::string display = src->label();
  return open_range(std::move(src), 0, size, std::move(display));
}

brokkr::core::Result<std::unique_ptr<ByteSource>> open_tar_entry(const std::filesystem::path& tar_path,
                                                                 const TarEntry& entry) noexcept {
  BRK_TRYV(src, open_file_source(tar_path));
  return open_tar_entry(std::move(src), entry);
}

} // namespace brokkr::io
