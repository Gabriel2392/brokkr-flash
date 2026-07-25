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

#pragma once

#include "app/md5_verify.hpp"
#include "core/status.hpp"
#include "protocol/odin/flash.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace brokkr::android_platform {

struct PackageFile {
  ~PackageFile();

  PackageFile(const PackageFile&) = delete;
  PackageFile& operator=(const PackageFile&) = delete;

  static brokkr::core::Result<std::shared_ptr<PackageFile>> open(int fd, std::string label) noexcept;

  int fd = -1;
  std::string label;
  std::uint64_t size = 0;
  std::int64_t write_time = 0;

 private:
  PackageFile() = default;
};

using PackageFiles = std::vector<std::shared_ptr<PackageFile>>;

brokkr::core::Result<std::vector<brokkr::app::Md5Job>> md5_jobs_from_packages(const PackageFiles& packages) noexcept;
brokkr::core::Result<std::vector<brokkr::odin::ImageSpec>> expand_package_inputs(const PackageFiles& packages) noexcept;
brokkr::core::Result<std::shared_ptr<const std::vector<std::byte>>> load_pit_file(int fd, std::string label) noexcept;
brokkr::core::Result<std::shared_ptr<const std::vector<std::byte>>> load_pit_from_packages(
  const PackageFiles& packages) noexcept;

} // namespace brokkr::android_platform