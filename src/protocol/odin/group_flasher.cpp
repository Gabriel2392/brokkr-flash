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

#include "protocol/odin/group_flasher.hpp"

#include "core/compat.hpp"
#include "core/elapsed.hpp"
#include "core/prefetcher.hpp"
#include "core/str.hpp"
#include "core/thread_priority.hpp"
#include "io/lz4_frame.hpp"
#include "io/read_exact.hpp"
#include "protocol/odin/pit_transfer.hpp"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace brokkr::odin {

namespace {

using u64 = std::uint64_t;

struct FirstError {
  std::mutex m;
  std::optional<brokkr::core::Error> e;

  void set(brokkr::core::Status s) noexcept {
    if (s) return;
    std::lock_guard lk(m);
    if (e) return;
    e = std::move(s.error());
  }

  brokkr::core::Status status_or_ok() noexcept {
    std::lock_guard lk(m);
    return e ? brokkr::core::Status{std::unexpect, *e} : brokkr::core::Status{};
  }

  bool has_error() noexcept {
    std::lock_guard lk(m);
    return e.has_value();
  }
};

static std::string_view final_stage(OdinCommands::ShutdownMode m) {
  return m == OdinCommands::ShutdownMode::Reboot ? "Finalizing + reboot" : "Finalizing";
}

static OdinCommands::ShutdownMode shutdown_mode_final(const Cfg& cfg) {
  return cfg.reboot_after ? OdinCommands::ShutdownMode::Reboot : OdinCommands::ShutdownMode::NoReboot;
}

static void log_summary(std::size_t total, std::size_t failed) {
  const std::size_t ok = (failed <= total) ? (total - failed) : 0;
  const std::size_t bad = (failed <= total) ? failed : total;
  spdlog::info("{} threads succeeded, {} failed.", ok, bad);
}

static void log_shutdown_action(OdinCommands::ShutdownMode m) {
  if (m == OdinCommands::ShutdownMode::Reboot) {
    spdlog::info("Reset");
    return;
  }
  spdlog::info("No Reboot");
}

static void emit_devfail(const Ui& ui, std::size_t orig_idx, const std::string& msg) {
  if (ui.on_error)
    ui.on_error("DEVFAIL idx=" + std::to_string(orig_idx) + " " + msg);
  else
    spdlog::error("DEVFAIL idx={} {}", orig_idx, msg);
}

static brokkr::core::IByteTransport& link(Target& d) { return *d.link; }

static std::size_t choose_pkt(const std::vector<Target*>& devs, const Cfg& cfg) {
  return std::ranges::any_of(devs, [](Target* d) { return d->proto < ProtocolVersion::PROTOCOL_VER2; })
             ? cfg.pkt_any_old
             : cfg.pkt_all_v2plus;
}

static bool any_lz4(const std::vector<ImageSpec>& v) {
  return std::ranges::any_of(v, [](const ImageSpec& s) { return s.lz4; });
}

static brokkr::core::Result<std::vector<ImageSpec>> sources_common_mapping_or_empty(
    const std::vector<Target*>& devs, const std::vector<ImageSpec>& sources) noexcept {
  std::vector<ImageSpec> out;
  if (devs.empty()) return out;

  out.reserve(sources.size());

  for (const auto& s : sources) {
    const auto skipped = [&](std::string_view why) {
      if (s.kind == ImageSpec::Kind::RawFile)
        spdlog::warn("Skipping '{}': {}", s.basename, why);
      else
        spdlog::debug("Skipping '{}': {}", s.basename, why);
    };

    const auto* ref = devs.front()->pit_table.find_by_file_name(s.basename);
    if (!ref) {
      skipped("no matching PIT entry");
      continue;
    }

    bool missing = false;
    for (auto* d : devs) {
      const auto* p = d->pit_table.find_by_file_name(s.basename);
      if (!p) {
        skipped("missing on one or more devices");
        missing = true;
        break;
      }
      if (p->id != ref->id || p->dev_type != ref->dev_type)
        return brokkr::core::fail("PIT mapping differs across devices");
    }
    if (missing) continue;

    out.push_back(s);
  }

  return out;
}

struct Step {
  enum class Op : std::uint8_t { Quit, Begin, Data, End };
  Op op = Op::Quit;
  bool comp = false;

  u64 a = 0;
  u64 b = 0;
  const std::byte* base = nullptr;
  u64 off = 0;
  std::size_t n = 0;

  std::int32_t part_id = 0;
  std::int32_t bin_type = 0;
  std::int32_t dev_type = 0;
  bool last = false;
};

static Step st_begin(bool comp, u64 begin_sz, u64 decomp_sz) {
  return {.op = Step::Op::Begin, .comp = comp, .a = begin_sz, .b = decomp_sz};
}
static Step st_data(bool comp, const std::byte* base, u64 off, std::size_t n) {
  return {.op = Step::Op::Data, .comp = comp, .base = base, .off = off, .n = n};
}
static Step st_end(bool comp, u64 end_sz, std::int32_t part_id, std::int32_t bin_type, std::int32_t dev_type,
                   bool last) {
  return Step{.op = Step::Op::End,
              .comp = comp,
              .a = end_sz,
              .part_id = part_id,
              .bin_type = bin_type,
              .dev_type = dev_type,
              .last = last};
}

static bool streams_lz4(const FlashItem& item, bool use_lz4) noexcept {
  return item.spec.lz4 && use_lz4 &&
         (item.spec.lz4_block_size == static_cast<std::size_t>(detail::kOneMiB) ||
          item.spec.size <= item.spec.lz4_block_size);
}

struct Window {
  std::vector<std::byte> buf;
  u64 begin = 0, end = 0, rounded = 0;
  bool last = false;
  std::size_t lz4_block_size = 0;
  u64 lz4_content_size = 0;
  std::optional<brokkr::core::Error> error;
  bool setup_failed = false;
  const std::byte* data() const { return buf.data(); }
};

class WindowPrefetcher {
 public:
  using Lease = brokkr::core::TwoSlotPrefetcher<Window>::Lease;

  WindowPrefetcher(const std::vector<FlashItem>& items, bool use_lz4, std::size_t pkt, u64 buffer_bytes)
      : items_(items),
        use_lz4_(use_lz4),
        pkt_(pkt),
        buffer_bytes_(buffer_bytes),
        pf_([this](Window& w, std::stop_token stop) { return fill_(w, stop); }) {}

  bool use_lz4() const noexcept { return use_lz4_; }
  std::optional<Lease> next() noexcept { return pf_.next(); }

 private:
  brokkr::core::Result<bool> fill_(Window& w, std::stop_token stop) noexcept {
    if (stop.stop_requested() || failed_ || item_ >= items_.size()) return false;

    w.error.reset();
    w.setup_failed = false;

    try {
      if (!open_) {
        auto opened = open_item_(items_[item_]);
        if (!opened) {
          fail_(w, std::move(opened.error()), true);
          return true;
        }
      }
      w.lz4_block_size = block_;
      w.lz4_content_size = total_;

      auto r = stream_ ? read_lz4_(w) : read_raw_(w);
      if (!r) {
        fail_(w, std::move(r.error()), false);
        return true;
      }

      if (w.last) close_item_();
      return true;
    } catch (const std::exception& e) {
      fail_(w, e.what(), !open_);
      return true;
    }
  }

  brokkr::core::Status open_item_(const FlashItem& item) {
    window_ = detail::xmit_window_bytes(item.part, buffer_bytes_);
    stream_ = streams_lz4(item, use_lz4_);
    window_decomp_ = 0;
    total_ = 0;
    sent_ = 0;
    block_ = 0;

    if (stream_) {
      BRK_TRYV(src, item.spec.open());
      BRK_TRYV(reader, io::Lz4BlockStreamReader::open(std::move(src)));

      total_ = reader.content_size();
      if (!total_) return brokkr::core::fail("LZ4 content size is zero: " + item.spec.display);

      block_ = reader.block_size();
      if (block_ != static_cast<std::size_t>(detail::kOneMiB) && total_ > block_)
        return brokkr::core::fail("LZ4 stream needs 1MiB blocks unless the image fits in one block: " +
                                  item.spec.display);

      window_decomp_ = detail::lz4_window_decomp_bytes(window_, block_);
      if (!window_decomp_) return brokkr::core::fail("Transfer window smaller than one LZ4 block: " + item.spec.display);

      lz4_.emplace(std::move(reader));
    } else {
      BRK_TRYV(src, item.spec.open());
      if (item.spec.lz4) {
        BRK_TRYV(decomp, io::open_lz4_decompressed(std::move(src)));
        raw_ = std::move(decomp);
      } else {
        raw_ = std::move(src);
      }

      total_ = raw_->size();
      if (!total_) return brokkr::core::fail("Empty source: " + item.spec.display);
    }

    open_ = true;
    return {};
  }

  brokkr::core::Status read_raw_(Window& w) {
    const u64 actual = std::min<u64>(total_ - sent_, window_);
    const u64 rounded = detail::round_up64(actual, pkt_);
    const auto n = static_cast<std::size_t>(actual);
    const auto r = static_cast<std::size_t>(rounded);

    if (w.buf.capacity() < r) {
      w.buf.clear();
      w.buf.reserve(r);
    }
    w.buf.resize(r);

    BRK_TRY(io::read_exact(*raw_, std::span<std::byte>(w.buf.data(), n)));
    if (r > n) std::memset(w.buf.data() + n, 0, r - n);

    w.begin = detail::round_up64(actual, detail::kXmitStartAlign);
    w.end = actual;
    w.rounded = rounded;
    w.last = sent_ + actual >= total_;

    sent_ += actual;
    return {};
  }

  brokkr::core::Status read_lz4_(Window& w) {
    const u64 rem = total_ - sent_;
    const bool last = rem <= window_decomp_;
    const u64 decomp = last ? rem : window_decomp_;
    const std::size_t blocks = last ? lz4_->blocks_remaining() : static_cast<std::size_t>(decomp / block_);

    w.buf.clear();
    w.buf.reserve(blocks * (block_ + 4));

    BRK_TRY(lz4_->read_n_blocks(blocks, w.buf));

    const u64 comp = w.buf.size();
    const u64 rounded = detail::round_up64(comp, pkt_);
    w.buf.reserve(static_cast<std::size_t>(rounded));
    w.buf.resize(static_cast<std::size_t>(rounded), std::byte{0});

    w.begin = comp;
    w.end = decomp;
    w.rounded = rounded;
    w.last = last;

    sent_ += decomp;
    return {};
  }

  void close_item_() noexcept {
    raw_.reset();
    lz4_.reset();
    open_ = false;
    ++item_;
  }

  void fail_(Window& w, brokkr::core::Error e, bool setup) noexcept {
    w.error = std::move(e);
    w.setup_failed = setup;
    failed_ = true;
    raw_.reset();
    lz4_.reset();
  }

  const std::vector<FlashItem>& items_;
  const bool use_lz4_;
  const std::size_t pkt_;
  const u64 buffer_bytes_;

  std::size_t item_ = 0;
  bool open_ = false;
  bool failed_ = false;
  bool stream_ = false;
  u64 window_ = 0;
  u64 window_decomp_ = 0;
  u64 total_ = 0;
  u64 sent_ = 0;
  std::size_t block_ = 0;
  std::unique_ptr<io::ByteSource> raw_;
  std::optional<io::Lz4BlockStreamReader> lz4_;

  brokkr::core::TwoSlotPrefetcher<Window> pf_;
};

template <class MakeContrib>
static brokkr::core::Status send_prefetched(WindowPrefetcher& pf, std::optional<WindowPrefetcher::Lease> lease,
                                            std::barrier<>& sync, Step& cur, const std::size_t pkt, const bool comp,
                                            const std::int32_t part_id, const std::int32_t bin_type,
                                            const std::int32_t dev_type, const u64 total_bytes, const u64 item_total,
                                            u64& overall_done, u64& item_done, const Ui& ui,
                                            std::atomic_uint32_t& failed_count, const std::size_t ndevs,
                                            MakeContrib make_contrib) noexcept {
  const u64 pkt64 = static_cast<u64>(pkt);

  auto emit = [&](Step s) {
    cur = s;
    sync.arrive_and_wait();
    sync.arrive_and_wait();
  };

  for (;;) {
    if (failed_count.load(std::memory_order_relaxed) >= ndevs) break;

    if (!lease) lease = pf.next();
    if (!lease) break;

    auto& w = lease->get();
    if (w.error) return brokkr::core::fail(*w.error);

    const u64 rounded = static_cast<u64>(w.rounded);
    const u64 packets = rounded / pkt64;

    spdlog::debug("XMIT window: comp={} part_id={} begin_size={} rounded={} packets={} pkt={} end_size={} last={}",
                  comp, part_id, w.begin, rounded, packets, pkt, w.end, w.last);

    emit(st_begin(comp, w.begin, w.end));
    auto contrib = make_contrib(w, packets);

    for (u64 p = 0; p < packets; ++p) {
      if (failed_count.load(std::memory_order_relaxed) >= ndevs) break;

      emit(st_data(comp, w.data(), p * pkt64, pkt));
      const u64 add = contrib(p);

      item_done += add;
      overall_done += add;
      if (ui.on_progress) ui.on_progress(overall_done, total_bytes, item_done, item_total);
    }

    emit(st_end(comp, w.end, part_id, bin_type, dev_type, w.last));
    if (w.last || (failed_count.load(std::memory_order_relaxed) >= ndevs)) break;
    lease.reset();
  }

  return {};
}

} // namespace

brokkr::core::Status flash(std::vector<Target*>& devs, const std::vector<ImageSpec>& sources,
                           std::shared_ptr<const std::vector<std::byte>> pit_to_upload, const Cfg& cfg,
                           Ui ui) noexcept {
  if (devs.empty()) return brokkr::core::fail("flash: no devices");
  for (auto* d : devs)
    if (!d || !d->link || !d->link->connected()) return brokkr::core::fail("flash: transport not connected");

  const bool has_sources = !sources.empty();
  const bool has_pit = pit_to_upload && !pit_to_upload->empty();

  if (!has_sources && !has_pit) return brokkr::core::fail("flash: nothing to do (no sources, no PIT)");

  const std::size_t total_devices = devs.size();
  std::size_t failed_total = 0;

  FirstError first_err;
  const auto sm_final = shutdown_mode_final(cfg);

  std::optional<std::chrono::steady_clock::time_point> flash_started;
  std::optional<std::chrono::steady_clock::time_point> flash_finished;

  auto stage = [&](std::string_view s) {
    if (ui.on_stage) ui.on_stage(std::string(s));
  };

  std::vector<Target*> active = devs;
  std::vector<std::size_t> active_idx;
  active_idx.reserve(devs.size());
  for (std::size_t i = 0; i < devs.size(); ++i) active_idx.push_back(i);

  auto fanout_keep = [&](auto&& fn) -> brokkr::core::Status {
    if (active.empty()) return brokkr::core::fail("No active devices");

    std::vector<brokkr::core::Status> sts(active.size(), brokkr::core::Status{});
    std::vector<std::jthread> ts;
    ts.reserve(active.size());

    for (std::size_t i = 0; i < active.size(); ++i) ts.emplace_back([&, i] {
      brokkr::core::bump_thread_priority(-8);
      sts[i] = fn(*active[i]);
    });
    for (auto& t : ts)
      if (t.joinable()) t.join();

    std::vector<Target*> next;
    std::vector<std::size_t> next_idx;
    next.reserve(active.size());
    next_idx.reserve(active.size());

    for (std::size_t i = 0; i < active.size(); ++i) {
      if (sts[i]) {
        next.push_back(active[i]);
        next_idx.push_back(active_idx[i]);
        continue;
      }

      ++failed_total;
      const auto msg = sts[i].error();
      first_err.set(std::move(sts[i]));
      emit_devfail(ui, active_idx[i], msg);
    }

    active.swap(next);
    active_idx.swap(next_idx);

    if (active.empty()) {
      auto st = first_err.status_or_ok();
      if (st) st = brokkr::core::fail("All devices failed");
      return st;
    }

    return {};
  };

  auto finish = [&](brokkr::core::Status st) -> brokkr::core::Status {
    if (!st) {
      log_summary(total_devices, total_devices);
      return st;
    }
    log_summary(total_devices, failed_total);
    if (failed_total > 0 || first_err.has_error()) return first_err.status_or_ok();
    if (flash_started && flash_finished)
      spdlog::info("Done ({})", brokkr::core::format_elapsed(*flash_finished - *flash_started));
    if (ui.on_done) ui.on_done();
    return {};
  };

  auto set_flash_timeout_active = [&] {
    for (auto* d : active) link(*d).set_timeout_ms(cfg.flash_timeout_ms);
  };

  std::size_t pkt = 0;
  std::vector<PlanItem> plan;
  std::vector<FlashItem> items;
  std::vector<ImageSpec> effective_sources;
  u64 total = 0;
  std::optional<WindowPrefetcher> prefetcher;

  auto lz4_usable = [&] {
    return any_lz4(effective_sources) &&
           std::ranges::all_of(active, [](Target* d) { return d->init.supports_compressed_download(); });
  };

  auto shutdown_active = [&](OdinCommands::ShutdownMode m, std::string_view stg) -> brokkr::core::Status {
    log_shutdown_action(m);
    stage(stg);
    return fanout_keep([&](Target& d) { return OdinCommands(link(d)).shutdown(m); });
  };

  auto steps = std::vector<brokkr::core::MoveOnlyFunction<brokkr::core::Status()>>{};

  steps.emplace_back([&] {
    spdlog::info("> Odin");
    stage("ODIN handshake");

    auto st = fanout_keep([&](Target& d) -> brokkr::core::Status {
      auto& c = link(d);
      c.set_timeout_ms(cfg.preflash_timeout_ms);
      OdinCommands odin(c);

      BRK_TRY(odin.handshake(cfg.preflash_retries));
      BRK_TRYV(vr, odin.get_version(cfg.preflash_retries));

      d.init = vr;
      d.proto = d.init.protocol();
      return {};
    });
    if (st) spdlog::info("< Loke");
    return st;
  });

  steps.emplace_back([&] -> brokkr::core::Status {
    if (active.empty()) return brokkr::core::fail("No active devices");

    pkt = choose_pkt(active, cfg);
    stage("Negotiating transfer options");

    auto st = fanout_keep([&](Target& d) -> brokkr::core::Status {
      if (d.proto < ProtocolVersion::PROTOCOL_VER2) return {};
      auto& c = link(d);
      c.set_timeout_ms(cfg.preflash_timeout_ms);
      return OdinCommands(c).setup_transfer_options(static_cast<std::int32_t>(pkt), cfg.preflash_retries);
    });
    if (!st) return st;

    set_flash_timeout_active();
    return {};
  });

  if (has_pit) {
    steps.emplace_back([&] {
      spdlog::info("Uploading PIT");
      stage("Uploading PIT");
      return fanout_keep([&](Target& d) {
        return OdinCommands(link(d)).set_pit({pit_to_upload->data(), pit_to_upload->size()}, cfg.preflash_retries);
      });
    });
  }

  steps.emplace_back([&] {
    spdlog::info("Get PIT for mapping");
    stage("Downloading PIT(s)");
    set_flash_timeout_active();

    return fanout_keep([&](Target& d) -> brokkr::core::Status {
      OdinCommands odin(link(d));
      BRK_TRYV(bytes, download_pit_bytes(odin));
      d.pit_bytes = std::move(bytes);
      BRK_TRYV(t, pit::parse({d.pit_bytes.data(), d.pit_bytes.size()}));
      d.pit_table = std::move(t);
      return {};
    });
  });

  steps.emplace_back([&] -> brokkr::core::Status {
    if (active.empty()) return brokkr::core::fail("No active devices");

    stage("Checking if devices are equal");
    const std::string ref = active.front()->pit_table.cpu_bl_id;
    if (ref.empty()) return brokkr::core::fail("PIT cpu_bl_id missing");
    for (auto* d : active)
      if (d->pit_table.cpu_bl_id != ref) return brokkr::core::fail("cpu_bl_id mismatch across devices");
    if (ui.on_model) ui.on_model(ref);
    return {};
  });

  steps.emplace_back([&] -> brokkr::core::Status {
    spdlog::info("Verifying PIT mapping");
    stage("Verifying PIT mapping");

    BRK_TRYV(eff, sources_common_mapping_or_empty(active, sources));
    effective_sources = std::move(eff);

    if (effective_sources.empty() && !sources.empty())
      spdlog::debug("No sources matched any PIT partition — nothing to flash from sources");
    else if (effective_sources.size() < sources.size())
      spdlog::debug("{} of {} source(s) matched PIT entries", effective_sources.size(), sources.size());

    BRK_TRYV(items2, map_to_pit(active.front()->pit_table, effective_sources));
    items = std::move(items2);

    total = 0;
    for (const auto& it : items) BRK_TRY(detail::checked_add_u64(total, it.spec.size, "TOTALSIZE"));

    if (!items.empty()) prefetcher.emplace(items, lz4_usable(), pkt, cfg.buffer_bytes);

    plan.clear();
    plan.reserve(items.size() + (has_pit ? 1u : 0u));

    if (has_pit)
      plan.push_back(PlanItem{.kind = PlanItem::Kind::Pit,
                              .part_name = "PIT",
                              .pit_file_name = "PIT",
                              .source_base = "PIT",
                              .size = pit_to_upload->size()});
    for (const auto& it : items) {
      plan.push_back(
          PlanItem{.kind = PlanItem::Kind::Part,
                   .part_id = it.part.id,
                   .dev_type = it.part.dev_type,
                   .part_name = !it.part.name.empty() ? it.part.name : it.part.file_name,
                   .pit_file_name = it.part.file_name,
                   .source_base = it.spec.source_basename.empty() ? it.spec.basename : it.spec.source_basename,
                   .size = it.spec.size});
    }

    if (ui.on_plan) ui.on_plan(plan, total);
    return {};
  });

  steps.emplace_back([&] -> brokkr::core::Status {
    if (items.empty()) return {};
    stage("Sending total size");
    return fanout_keep(
        [&](Target& d) { return OdinCommands(link(d)).send_total_size(total, cfg.preflash_retries); });
  });

  steps.emplace_back([&] -> brokkr::core::Status {
    if (has_pit || items.empty()) return {};

    const bool has_super =
        std::ranges::any_of(items, [](const FlashItem& it) { return brokkr::core::equals_ci(it.part.name, "SUPER"); });
    if (!has_super) return {};

    if (!std::ranges::all_of(active, [](Target* d) { return d->pit_table.is_ab(); })) {
      spdlog::debug("Not declaring super size: device is not A/B");
      return {};
    }

    stage("Declaring super size");
    return fanout_keep(
        [&](Target& d) { return OdinCommands(link(d)).declare_super_used_blocks(0, cfg.preflash_retries); });
  });

  steps.emplace_back([&] -> brokkr::core::Status {
    const bool use_lz4 = lz4_usable();
    if (prefetcher && prefetcher->use_lz4() != use_lz4) prefetcher.emplace(items, use_lz4, pkt, cfg.buffer_bytes);

    stage(use_lz4 ? "Flashing (Speed: Enhanced)" : "Flashing (Speed: Normal)");
    spdlog::info("Flashing has begun!");
    flash_started = std::chrono::steady_clock::now();

    const std::size_t ndevs = active.size();
    if (!ndevs) return brokkr::core::fail("No active devices");

    Step cur{};
    std::barrier sync(static_cast<std::ptrdiff_t>(ndevs + 1));
    FirstError berr;
    std::atomic_uint32_t failed_count{0};
    std::vector<std::uint8_t> dead(ndevs, 0);

    auto exec = [&](OdinCommands& odin, const Step& s) -> brokkr::core::Status {
      if (s.op == Step::Op::Begin)
        return s.comp ? odin.begin_download_compressed(static_cast<std::int32_t>(s.a), static_cast<std::int32_t>(s.b))
                      : odin.begin_download(static_cast<std::int32_t>(s.a));
      if (s.op == Step::Op::Data) {
        BRK_TRY(odin.send_raw({s.base + static_cast<std::ptrdiff_t>(s.off), s.n}));
        return odin.recv_data_ack();
      }
      if (s.op == Step::Op::End)
        return s.comp ? odin.end_download_compressed(static_cast<std::int32_t>(s.a), s.part_id, s.dev_type, s.last,
                                                     s.bin_type)
                      : odin.end_download(static_cast<std::int32_t>(s.a), s.part_id, s.dev_type, s.last, s.bin_type);
      return {};
    };

    std::vector<std::jthread> workers;
    workers.reserve(ndevs);

    for (std::size_t i = 0; i < ndevs; ++i) {
      auto* d = active[i];
      const std::size_t orig = active_idx[i];

      workers.emplace_back([&, d, i, orig](std::stop_token stt) {
        brokkr::core::bump_thread_priority(-8);
        OdinCommands odin(link(*d));
        bool dead_local = false;

        for (;;) {
          sync.arrive_and_wait();
          const Step s = cur;

          const bool quit = (s.op == Step::Op::Quit) || stt.stop_requested();
          if (!quit && !dead_local) {
            auto rst = exec(odin, s);
            if (!rst) {
              spdlog::debug(
                  "Device {}: step failed: op={} comp={} size_arg={} data_off={} data_len={} part_id={} dev_type={} "
                  "last={}: {}",
                  orig, static_cast<int>(s.op), s.comp, s.a, s.off, s.n, s.part_id, s.dev_type, s.last, rst.error());
              dead[i] = 1;
              failed_count.fetch_add(1, std::memory_order_relaxed);
              const auto msg = rst.error();
              berr.set(std::move(rst));
              emit_devfail(ui, orig, msg);
              dead_local = true;
            }
          }

          sync.arrive_and_wait();
          if (quit) break;
        }
      });
    }

    auto emit = [&](Step s) {
      cur = s;
      sync.arrive_and_wait();
      sync.arrive_and_wait();
    };

    auto coordinator = [&]() -> brokkr::core::Status {
      u64 overall_done = 0;

      std::size_t plan_off = 0;
      if (has_pit) {
        if (ui.on_item_active) ui.on_item_active(0);
        if (items.empty() && ui.on_progress) {
          const auto n = static_cast<u64>(pit_to_upload->size());
          ui.on_progress(0, n, 0, n);
          ui.on_progress(n, n, n, n);
        }
        if (ui.on_item_done) ui.on_item_done(0);
        plan_off = 1;
      }

      for (std::size_t idx = 0; idx < items.size(); ++idx) {
        if (failed_count.load(std::memory_order_relaxed) >= ndevs) break;

        const auto& item = items[idx];
        const std::size_t plan_idx = plan_off + idx;

        const std::string& file_name =
            item.spec.source_basename.empty() ? item.spec.basename : item.spec.source_basename;
        if (!file_name.empty()) spdlog::info("{}", file_name);

        if (ui.on_item_active) ui.on_item_active(plan_idx);

        const u64 item_total = item.spec.size;
        const u64 window = detail::xmit_window_bytes(item.part, cfg.buffer_bytes);
        u64 item_done = 0;

        const bool stream_lz4 = streams_lz4(item, use_lz4);

        spdlog::debug(
            "Flash item: part_id={} dev_type={} name='{}' pit_file='{}' source='{}' size={} window={} lz4={} mode={}",
            item.part.id, item.part.dev_type, item.part.name, item.part.file_name, item.spec.display, item.spec.size,
            window, item.spec.lz4, stream_lz4 ? "compressed-stream" : "raw");

        auto first = prefetcher->next();
        if (first && first->get().setup_failed) return brokkr::core::fail(*first->get().error);

        if (stream_lz4 && first) {
          const std::size_t block_sz = first->get().lz4_block_size;
          const u64 window_decomp = detail::lz4_window_decomp_bytes(window, block_sz);
          spdlog::debug("LZ4 stream: block_size={} total_decomp={} window_decomp={} blocks/window={}", block_sz,
                        first->get().lz4_content_size, window_decomp, window_decomp / block_sz);
        }

        if (ui.on_progress) ui.on_progress(overall_done, total, item_done, item_total);

        if (stream_lz4) {
          BRK_TRY(send_prefetched(*prefetcher, std::move(first), sync, cur, pkt, true, item.part.id, item.part.bin_type,
                                  item.part.dev_type, total, item_total, overall_done, item_done, ui, failed_count,
                                  ndevs, [&](const Window& w, u64 packets) {
                                    return [end = w.end, packets](u64 p) {
                                      const auto c1 = ((p + 1) * end) / packets;
                                      const auto c0 = (p * end) / packets;
                                      return c1 - c0;
                                    };
                                  }));
        } else {
          BRK_TRY(send_prefetched(*prefetcher, std::move(first), sync, cur, pkt, false, item.part.id,
                                  item.part.bin_type, item.part.dev_type, total, item_total, overall_done, item_done,
                                  ui, failed_count, ndevs, [&](const Window& w, u64 /*packets*/) {
                                    u64 rem2 = w.end;
                                    const u64 pkt64 = static_cast<u64>(pkt);
                                    return [rem2, pkt64](u64 /*p*/) mutable {
                                      const u64 add = std::min<u64>(pkt64, rem2);
                                      rem2 -= add;
                                      return add;
                                    };
                                  }));
        }

        if (ui.on_item_done) ui.on_item_done(plan_idx);
      }

      return {};
    };

    auto cst = coordinator();
    if (!cst) berr.set(std::move(cst));
    prefetcher.reset();

    emit({.op = Step::Op::Quit});
    for (auto& t : workers)
      if (t.joinable()) t.join();

    const std::size_t bad_in_flash = static_cast<std::size_t>(failed_count.load(std::memory_order_relaxed));
    failed_total += bad_in_flash;
    first_err.set(berr.status_or_ok());

    {
      std::vector<Target*> survivors;
      std::vector<std::size_t> survivors_idx;
      survivors.reserve(active.size());
      survivors_idx.reserve(active.size());

      for (std::size_t i = 0; i < active.size(); ++i)
        if (!dead[i]) {
          survivors.push_back(active[i]);
          survivors_idx.push_back(active_idx[i]);
        }
      active.swap(survivors);
      active_idx.swap(survivors_idx);
    }

    return {};
  });

  steps.emplace_back([&] -> brokkr::core::Status {
    if (!active.empty()) {
      auto st = shutdown_active(sm_final, final_stage(sm_final));
      if (!st) {
        first_err.set(st);
        for (auto idx : active_idx) emit_devfail(ui, idx, st.error());
        failed_total += active.size();
        return st;
      }
      flash_finished = std::chrono::steady_clock::now();
      if (cfg.post_close_delay_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.post_close_delay_ms));
    }
    return {};
  });

  for (auto& fn : steps) {
    auto st = fn();
    if (!st) return finish(std::move(st));
    if (cfg.step_delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(cfg.step_delay_ms));
  }

  return finish({});
}

} // namespace brokkr::odin
