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

#include "protocol/odin/group_flasher.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brokkr::app {

class FlashInterface {
public:
  FlashInterface(bool is_tty_enabled);
  ~FlashInterface();

  FlashInterface(const FlashInterface &) = delete;
  FlashInterface &operator=(const FlashInterface &) = delete;

  void devices(std::size_t count, std::vector<std::string> ids);
  void cpu_bl_id(std::string model);
  void stage(std::string stage);

  void plan(std::vector<brokkr::odin::PlanItem> plan,
            std::uint64_t total_flash_bytes);
  void active(std::size_t index);
  void done_item(std::size_t index);

  void progress(std::uint64_t overall_done, std::uint64_t overall_total,
                std::uint64_t item_done, std::uint64_t item_total);

  void notice(std::string msg);
  void fail(std::string msg);
  void done(std::string msg);

private:
  struct TermSize {
    int rows = 0;
    int cols = 0;
  };
  struct Clip {
    std::string s;
    std::size_t w = 0;
  };

  void redraw_(bool force);
  TermSize term_size_() const;

  static bool is_tty_();
  static bool colors_enabled_();
  static bool utf8_enabled_();

  static std::string bytes_h_(std::uint64_t b);
  static std::string rate_h_(double bytes_per_sec);
  static std::string eta_h_(std::optional<std::chrono::seconds> eta);

  char spinner_() const;

  Clip clip_(std::string_view s, std::size_t max_cols) const;
  std::string pad_(std::string_view s, std::size_t cols, bool left_pad) const;

  std::string bar_(double frac, std::size_t width_cols) const;

  bool tty_ = false, color_ = false, utf8_ = false;

  mutable std::mutex mtx_;

  std::size_t dev_count_ = 0;
  std::vector<std::string> dev_ids_;
  std::string model_, stage_;

  std::vector<brokkr::odin::PlanItem> plan_;
  std::vector<bool> plan_done_;
  std::size_t active_item_ = static_cast<std::size_t>(-1);

  std::uint64_t overall_done_ = 0, overall_total_ = 0, item_done_ = 0,
                item_total_ = 0;

  std::string notice_line_, status_line_;
  bool fatal_ = false;

  std::chrono::steady_clock::time_point start_{}, last_rate_ts_{},
      last_redraw_{};
  std::uint64_t last_rate_bytes_ = 0;
  double ema_rate_bps_ = 0.0;
};

} // namespace brokkr::app
