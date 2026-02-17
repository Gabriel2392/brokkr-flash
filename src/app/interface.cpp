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

#include "app/interface.hpp"
#include "app/version.hpp"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include <fmt/core.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace brokkr::app {

namespace {

static std::string join_ids(const std::vector<std::string> &ids) {
  std::ostringstream oss;
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (i) oss << ' ';
    oss << ids[i];
  }
  return oss.str();
}

static std::size_t u8_advance(std::string_view s, std::size_t i) {
  if (i >= s.size()) return s.size();
  const auto c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) return i + 1;
  if ((c & 0xE0) == 0xC0) return std::min(i + 2, s.size());
  if ((c & 0xF0) == 0xE0) return std::min(i + 3, s.size());
  if ((c & 0xF8) == 0xF0) return std::min(i + 4, s.size());
  return i + 1;
}

static std::string json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"':  out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) out += "\\u00" + fmt::format("{:02x}", c);
        else out.push_back(static_cast<char>(c));
        break;
    }
  }
  return out;
}

constexpr const char *kAltOn = "\x1b[?1049h";
constexpr const char *kAltOff = "\x1b[?1049l";
constexpr const char *kHideCursor = "\x1b[?25l";
constexpr const char *kShowCursor = "\x1b[?25h";

constexpr const char *kReset = "\x1b[0m";
constexpr const char *kDim = "\x1b[2m";
constexpr const char *kBold = "\x1b[1m";

constexpr const char *kRed = "\x1b[31m";
constexpr const char *kGreen = "\x1b[32m";
constexpr const char *kYellow = "\x1b[33m";
constexpr const char *kBlue = "\x1b[34m";
constexpr const char *kCyan = "\x1b[36m";
constexpr const char *kGray = "\x1b[90m";

} // namespace

#if defined(__linux__) || defined(__APPLE__)
namespace {

static bool env_has_utf8() {
  auto has = [](const char *v) {
    if (!v || !*v) return false;
    std::string_view s(v);
    auto ci = [&](std::string_view needle) {
      for (std::size_t i = 0; i + needle.size() <= s.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < needle.size(); ++j) {
          const auto a = static_cast<unsigned char>(s[i + j]);
          const auto b = static_cast<unsigned char>(needle[j]);
          if (static_cast<char>(std::tolower(a)) != static_cast<char>(std::tolower(b))) {
            ok = false;
            break;
          }
        }
        if (ok) return true;
      }
      return false;
    };
    return ci("utf-8") || ci("utf8");
  };
  return has(std::getenv("LC_ALL")) || has(std::getenv("LC_CTYPE")) || has(std::getenv("LANG"));
}

struct TermSignalGuard {
  using H = void (*)(int);

  static inline bool installed = false;
  static inline H old_int = SIG_DFL;
  static inline H old_term = SIG_DFL;
  static inline H old_quit = SIG_DFL;
  static inline H old_hup = SIG_DFL;

  static void restore_now_() noexcept {
    static constexpr char seq[] = "\x1b[?25h\x1b[?1049l";
    (void)::write(1, seq, sizeof(seq) - 1);
  }

  static void handler_(int signo) noexcept {
    restore_now_();
    std::_Exit(128 + signo);
  }

  static void install() noexcept {
    if (installed) return;
    installed = true;
    old_int = std::signal(SIGINT, &handler_);
    old_term = std::signal(SIGTERM, &handler_);
    old_quit = std::signal(SIGQUIT, &handler_);
    old_hup = std::signal(SIGHUP, &handler_);
  }

  static void uninstall() noexcept {
    if (!installed) return;
    installed = false;
    std::signal(SIGINT, old_int);
    std::signal(SIGTERM, old_term);
    std::signal(SIGQUIT, old_quit);
    std::signal(SIGHUP, old_hup);
  }
};

} // namespace
#endif

bool FlashInterface::is_tty_() {
#if defined(__linux__) || defined(__APPLE__)
  return ::isatty(1) == 1;
#else
  return false;
#endif
}

bool FlashInterface::colors_enabled_() {
#if defined(__linux__) || defined(__APPLE__)
  if (!is_tty_()) return false;
  const char *no = std::getenv("NO_COLOR");
  return !(no && *no);
#else
  return false;
#endif
}

bool FlashInterface::utf8_enabled_() {
#if defined(__linux__) || defined(__APPLE__)
  return is_tty_() && env_has_utf8();
#else
  return false;
#endif
}

FlashInterface::FlashInterface(bool is_tty_enabled, bool output_in_json)
    : output_json_(output_in_json) {
  if (is_tty_enabled) {
    tty_ = is_tty_();
    color_ = colors_enabled_();
    utf8_ = utf8_enabled_();
  } else {
    tty_ = false;
    color_ = false;
    utf8_ = false;
  }
  start_ = last_rate_ts_ = last_redraw_ = std::chrono::steady_clock::now();

#if defined(__linux__) || defined(__APPLE__)
  if (tty_) {
    TermSignalGuard::install();
    std::cout << kAltOn << kHideCursor << std::flush;
  }
#endif
}

FlashInterface::~FlashInterface() {
  std::string final;
  bool fatal = false;
  {
    std::lock_guard lk(mtx_);
    final = status_line_;
    fatal = fatal_;
  }

#if defined(__linux__) || defined(__APPLE__)
  if (tty_) {
    std::cout << kShowCursor << kAltOff << std::flush;
    TermSignalGuard::uninstall();
  }
#endif

  if (!final.empty())
    (fatal ? std::cerr : std::cout) << final << "\n" << std::flush;
}

void FlashInterface::devices(std::size_t count, std::vector<std::string> ids) {
  std::lock_guard lk(mtx_);
  dev_count_ = count;
  dev_ids_ = std::move(ids);
  redraw_(true);
}

void FlashInterface::cpu_bl_id(std::string model) {
  std::lock_guard lk(mtx_);
  model_ = std::move(model);
  redraw_(true);
}

void FlashInterface::stage(std::string stage) {
  std::lock_guard lk(mtx_);
  stage_ = std::move(stage);
  redraw_(true);
}

void FlashInterface::plan(std::vector<brokkr::odin::PlanItem> plan,
                          std::uint64_t total_flash_bytes) {
  std::lock_guard lk(mtx_);
  plan_ = std::move(plan);
  plan_done_.assign(plan_.size(), false);
  active_item_ = static_cast<std::size_t>(-1);

  overall_total_ = total_flash_bytes;
  overall_done_ = item_done_ = item_total_ = 0;

  last_rate_ts_ = std::chrono::steady_clock::now();
  last_rate_bytes_ = 0;
  ema_rate_bps_ = 0.0;

  redraw_(true);
}

void FlashInterface::active(std::size_t index) {
  std::lock_guard lk(mtx_);
  active_item_ = index;
  item_done_ = 0;
  item_total_ = (index < plan_.size()) ? plan_[index].size : 0;
  redraw_(true);
}

void FlashInterface::done_item(std::size_t index) {
  std::lock_guard lk(mtx_);
  if (index < plan_done_.size())
    plan_done_[index] = true;
  redraw_(true);
}

void FlashInterface::progress(std::uint64_t overall_done, std::uint64_t overall_total,
                              std::uint64_t item_done, std::uint64_t item_total) {
  std::lock_guard lk(mtx_);
  overall_done_ = overall_done;
  overall_total_ = overall_total;
  item_done_ = item_done;
  item_total_ = item_total;

  const auto now = std::chrono::steady_clock::now();
  if (overall_done_ < last_rate_bytes_) {
    last_rate_ts_ = now;
    last_rate_bytes_ = overall_done_;
    ema_rate_bps_ = 0.0;
    redraw_(false);
    return;
  }

  const auto dt = std::chrono::duration_cast<std::chrono::duration<double>>(now - last_rate_ts_).count();
  const auto db = static_cast<double>(overall_done_ - last_rate_bytes_);

  if (dt >= 0.2) {
    const double inst = (dt > 0.0) ? (db / dt) : 0.0;
    ema_rate_bps_ = (ema_rate_bps_ <= 1e-9) ? inst : (ema_rate_bps_ * 0.90 + inst * 0.10);
    last_rate_ts_ = now;
    last_rate_bytes_ = overall_done_;
    redraw_(false);
  } else if (overall_done_ == overall_total_ && overall_total_ > 0) {
    redraw_(false);
  }
}

void FlashInterface::notice(std::string msg) {
  std::lock_guard lk(mtx_);
  notice_line_ = std::move(msg);
  redraw_(true);
}

void FlashInterface::fail(std::string msg) {
  std::lock_guard lk(mtx_);
  fatal_ = true;
  status_line_ = std::move(msg);
  redraw_(true);
}

void FlashInterface::done(std::string msg) {
  std::lock_guard lk(mtx_);
  fatal_ = false;
  status_line_ = std::move(msg);
  redraw_(true);
}

FlashInterface::TermSize FlashInterface::term_size_() const {
#if defined(__linux__) || defined(__APPLE__)
  winsize ws{};
  if (::ioctl(1, TIOCGWINSZ, &ws) == 0)
    return {static_cast<int>(ws.ws_row), static_cast<int>(ws.ws_col)};
#endif
  return {24, 80};
}

std::string FlashInterface::bytes_h_(std::uint64_t b) {
  const double d = static_cast<double>(b);
  const char *u[] = {"B", "KB", "MB", "GB", "TB"};
  int i = 0;
  double v = d;
  while (v >= 1024.0 && i < 4) {
    v /= 1024.0;
    ++i;
  }
  std::ostringstream oss;
  if (!i)
    oss << static_cast<std::uint64_t>(v) << u[i];
  else
    oss << std::fixed << std::setprecision(v >= 10 ? 1 : 2) << v << u[i];
  return oss.str();
}

std::string FlashInterface::rate_h_(double bps) {
  if (bps <= 1e-9) return "0B/s";
  return bytes_h_(static_cast<std::uint64_t>(bps)) + "/s";
}

std::string FlashInterface::eta_h_(std::optional<std::chrono::seconds> eta) {
  if (!eta) return "--:--";
  auto s = eta->count();
  const auto h = s / 3600; s %= 3600;
  const auto m = s / 60;   s %= 60;
  std::ostringstream oss;
  if (h) oss << h << "h";
  oss << std::setw(2) << std::setfill('0') << m << "m" << std::setw(2) << s << "s";
  return oss.str();
}

char FlashInterface::spinner_() const {
  static constexpr char sp[] = {'|', '/', '-', '\\'};
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start_).count();
  return sp[(ms / 120) % 4];
}

FlashInterface::Clip FlashInterface::clip_(std::string_view s, std::size_t max_cols) const {
  if (!max_cols) return {};

  if (!utf8_) {
    if (s.size() <= max_cols) return {std::string(s), s.size()};
    if (max_cols <= 3) return {std::string(s.substr(0, max_cols)), max_cols};
    return {std::string(s.substr(0, max_cols - 3)) + "...", max_cols};
  }

  auto take_cols = [&](std::size_t cols_want) -> std::pair<std::size_t, std::size_t> {
    std::size_t cols = 0, out_bytes = 0;
    for (std::size_t i = 0; i < s.size() && cols < cols_want;) {
      const std::size_t next = u8_advance(s, i);
      out_bytes = next;
      i = next;
      ++cols;
    }
    return {out_bytes, cols};
  };

  const auto [bytes_max, cols_max] = take_cols(max_cols);
  if (bytes_max >= s.size()) return {std::string(s), cols_max};

  if (max_cols == 1) return {"…", 1};

  const auto [bytes_keep, cols_keep] = take_cols(max_cols - 1);
  std::string out(s.substr(0, bytes_keep));
  out += "…";
  return {std::move(out), cols_keep + 1};
}

std::string FlashInterface::pad_(std::string_view s, std::size_t cols, bool left_pad) const {
  auto c = clip_(s, cols);
  if (c.w >= cols) return std::move(c.s);
  const std::string pad(cols - c.w, ' ');
  return left_pad ? (pad + c.s) : (c.s + pad);
}

std::string FlashInterface::bar_(double frac, std::size_t w) const {
  frac = std::clamp(frac, 0.0, 1.0);
  const std::size_t filled = static_cast<std::size_t>(std::llround(frac * static_cast<double>(w)));

  std::string s;
  if (utf8_) {
    s.reserve(w * 3);
    for (std::size_t i = 0; i < w; ++i) s.append(i < filled ? "█" : "░");
    return s;
  }

  s.reserve(w);
  for (std::size_t i = 0; i < w; ++i) s.push_back(i < filled ? '=' : '-');
  return s;
}

void FlashInterface::redraw_(bool force) {
  const auto now = std::chrono::steady_clock::now();
  if (!force && (now - last_redraw_) < std::chrono::milliseconds(33)) return;
  last_redraw_ = now;

  if (!tty_) {
    if (output_json_) {
      fmt::print(
        R"(PROGRESSUPDATE{{"devices":{},"stage":"{}","overall_done":{},"overall_total":{},"cpu_bl_id":"{}","notice":"{}","status":"{}"}}
)",
        dev_count_,
        json_escape(stage_.empty() ? "-" : stage_),
        overall_done_, overall_total_,
        json_escape(model_.empty() ? "-" : model_),
        json_escape(notice_line_),
        json_escape(status_line_)
      );
    } else {
      spdlog::info("Devices={} Stage={} Overall={}/{} cpu_bl_id={}{}{}",
                   dev_count_, (stage_.empty() ? "-" : stage_),
                   bytes_h_(overall_done_), bytes_h_(overall_total_),
                   (model_.empty() ? "-" : model_),
                   (notice_line_.empty() ? "" : (" | " + notice_line_)),
                   (status_line_.empty() ? "" : (" | " + status_line_)));
    }
    return;
  }

  const auto ts = term_size_();
  const int rows = std::max(12, ts.rows), cols = std::max(60, ts.cols);

  auto pct = [&](std::uint64_t d, std::uint64_t t) {
    if (!t) return 0;
    if (d > t) d = t;
    return static_cast<int>((d * 100) / t);
  };

  const int overall_pct = pct(overall_done_, overall_total_);

  std::optional<std::chrono::seconds> eta;
  if (overall_total_ && ema_rate_bps_ > 1.0 && overall_done_ <= overall_total_) {
    const double rem = static_cast<double>(overall_total_ - overall_done_);
    eta = std::chrono::seconds(static_cast<long long>(rem / ema_rate_bps_));
  }

  std::ostringstream out;
  out << "\x1b[H\x1b[J";

  auto emit = [&](const char *c, std::string_view plain) {
    const auto clipped = clip_(plain, static_cast<std::size_t>(cols)).s;
    if (color_) out << c;
    out << clipped;
    if (color_) out << kReset;
    out << "\n";
  };

  {
    std::string title = "Brokkr Flash v" + brokkr::app::version_string() +
                        " --- Copyright (c) 2026 Gabriel2392.";
    if (color_) out << kBold;
    emit(kGray, title);
    if (color_) out << kReset;
  }

  emit(kBlue, fmt::format("Devices: {}  IDs: {}  cpu_bl_id: {}", dev_count_,
                          join_ids(dev_ids_), (model_.empty() ? "-" : model_)));

  {
    std::ostringstream l;
    l << "Stage: " << (stage_.empty() ? "-" : stage_);
    if (!overall_total_ && plan_.empty() && !fatal_) l << "  " << spinner_();
    emit(kBlue, l.str());
  }

  {
    const std::string prefix = fmt::format("Overall: {:3}% ", overall_pct);
    const std::string bytes = fmt::format("  {}/{}", bytes_h_(overall_done_), bytes_h_(overall_total_));
    const std::string rate  = fmt::format("  {}", rate_h_(ema_rate_bps_));
    const std::string etas  = fmt::format("  ETA {}", eta_h_(eta));

    auto suf = [&](bool r, bool e) {
      std::string s = bytes;
      if (r) s += rate;
      if (e) s += etas;
      return s;
    };

    std::string used = suf(true, true);
    std::size_t bar_w = 10;

    auto layout = [&](std::string_view s) {
      const auto budget = static_cast<std::size_t>(cols);
      const auto need = clip_(prefix, budget).w + clip_(s, budget).w + 10;
      if (need > budget) return false;
      bar_w = budget - clip_(prefix, budget).w - clip_(s, budget).w;
      bar_w = std::clamp<std::size_t>(bar_w, 10, budget);
      used = std::string(s);
      return true;
    };

    if (!layout(suf(true, true)) && !layout(suf(false, true)) && !layout(suf(false, false))) {
      used = bytes;
      bar_w = 10;
    }

    const double frac = overall_total_ ? (static_cast<double>(overall_done_) / static_cast<double>(overall_total_)) : 0.0;
    const auto line = prefix + bar_(frac, bar_w) + used;
    const char *col = fatal_ ? kRed : (!overall_total_ ? kGray : kGreen);

    if (color_) out << kBold;
    emit(col, line);
    if (color_) out << kReset;
  }

  if (!notice_line_.empty()) { if (color_) out << kDim; emit(kGray, notice_line_); if (color_) out << kReset; }
  if (!status_line_.empty()) emit(fatal_ ? kRed : kGreen, status_line_);

  emit(kCyan, fmt::format("Plan: {} items", plan_.size()));

  const int header =
      1 + 1 + 1 + 1 +
      (!notice_line_.empty() ? 1 : 0) +
      (!status_line_.empty() ? 1 : 0) +
      1;

  const int remaining = rows - header;
  if (remaining <= 1) { std::cout << out.str() << std::flush; return; }
  if (plan_.empty()) { emit(kGray, "Waiting for PIT + mapping..."); std::cout << out.str() << std::flush; return; }

  const std::size_t C = static_cast<std::size_t>(cols);

  struct Cols { bool pit=false, src=true; std::size_t st=4,id=6,nm=26,pitw=18,sz=10,srcw=18; } cw;
  if (C >= 110) { cw.pit=true; cw.nm=30; cw.srcw=22; }
  else if (C >= 92) { cw.pit=true; cw.srcw=16; }
  else if (C >= 72) { cw.nm=30; cw.srcw=20; }
  else { cw.src=false; cw.nm = (C > (cw.st + 1 + cw.id + 1 + cw.sz + 2 + 10)) ? (C - (cw.st + 1 + cw.id + 1 + cw.sz + 2)) : 18; }

  auto row = [&](std::string_view st, std::string_view id, std::string_view name,
                 std::string_view pit, std::string_view size, std::string_view src) {
    std::ostringstream l;
    l << pad_(st, cw.st, false) << " " << pad_(id, cw.id, true) << " " << pad_(name, cw.nm, false) << " ";
    if (cw.pit) l << pad_(pit, cw.pitw, false) << " ";
    l << pad_(size, cw.sz, true);
    if (cw.src) l << "  " << pad_(src, cw.srcw, false);
    return l.str();
  };

  emit(kGray, row("STAT", "ID", "PARTITION", "PIT-FILE", "SIZE", "SOURCE"));

  const std::size_t max_lines = static_cast<std::size_t>(std::max(1, remaining - 1));

  std::size_t first = 0;
  if (active_item_ < plan_.size() && plan_.size() > max_lines) {
    const auto half = max_lines / 2;
    first = (active_item_ > half) ? (active_item_ - half) : 0;
    if (first + max_lines > plan_.size()) first = plan_.size() - max_lines;
  }

  const std::size_t last = std::min(plan_.size(), first + max_lines);
  if (first) emit(kGray, fmt::format("↑ {} hidden", first));

  auto stat = [&](std::size_t i) -> std::pair<const char *, std::string_view> {
    if (i < plan_done_.size() && plan_done_[i]) return {kGreen, "DONE"};
    if (i == active_item_) return {kYellow, "LIVE"};
    return {kGray, "WAIT"};
  };

  for (std::size_t i = first; i < last; ++i) {
    const auto &it = plan_[i];
    auto [col, st] = stat(i);

    std::string id = "-", pit = "PIT";
    if (it.kind == brokkr::odin::PlanItem::Kind::Part) { id = fmt::to_string(it.part_id); pit = it.pit_file_name; }

    emit(col, row(st, id, it.part_name, pit, bytes_h_(it.size), it.source_base));
  }

  if (plan_.size() > last) emit(kGray, fmt::format("↓ {} hidden", plan_.size() - last));
  std::cout << out.str() << std::flush;
}

} // namespace brokkr::app
