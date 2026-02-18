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

#include "core/prefetcher.hpp"
#include "io/lz4_frame.hpp"
#include "io/read_exact.hpp"
#include "protocol/odin/pit_transfer.hpp"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
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

constexpr std::string_view kHandshakeStr = "ODIN handshake";
constexpr std::string_view kPktFlashStr  = "Negotiating transfer options";
constexpr std::string_view kPitDlStr     = "Downloading PIT(s)";
constexpr std::string_view kPitUpStr     = "Uploading PIT";
constexpr std::string_view kCpuCheck     = "Checking if devices are equal";
constexpr std::string_view kMapCheck     = "Verifying PIT mapping";
constexpr std::string_view kTotalSend    = "Sending total size";
constexpr std::string_view kFlashFast    = "Flashing (Speed: Enhanced)";
constexpr std::string_view kFlashNorm    = "Flashing (Speed: Normal)";
constexpr std::string_view kRebooting    = "Rebooting devices";

static std::string_view final_stage(OdinCommands::ShutdownMode m) {
  if (m == OdinCommands::ShutdownMode::ReDownload) return "Finalizing + redownload";
  if (m == OdinCommands::ShutdownMode::Reboot) return "Finalizing + reboot";
  return "Finalizing";
}

static OdinCommands::ShutdownMode shutdown_mode_final(const Cfg& cfg) {
  return cfg.redownload_after ? OdinCommands::ShutdownMode::ReDownload :
         cfg.reboot_after     ? OdinCommands::ShutdownMode::Reboot :
                                OdinCommands::ShutdownMode::NoReboot;
}

static void log_summary(std::size_t total, std::size_t failed) {
  const std::size_t ok = (failed <= total) ? (total - failed) : 0;
  const std::size_t bad = (failed <= total) ? failed : total;
  spdlog::info("{} threads succeeded, {} failed.", ok, bad);
}

static void log_shutdown_action(OdinCommands::ShutdownMode m) {
  if (m == OdinCommands::ShutdownMode::ReDownload) { spdlog::info("Re-download"); return; }
  if (m == OdinCommands::ShutdownMode::Reboot) { spdlog::info("Reset"); return; }
  spdlog::info("No Reboot");
}

static void log_speed(bool enhanced) { spdlog::info("Speed: {}", enhanced ? "Enhanced" : "Normal"); }

static void emit_devfail(const Ui& ui, std::size_t orig_idx, const std::string& msg) {
  if (ui.on_error) ui.on_error("DEVFAIL idx=" + std::to_string(orig_idx) + " " + msg);
}

static brokkr::core::IByteTransport& link(Target& d) { return *d.link; }

static std::size_t choose_pkt(const std::vector<Target*>& devs, const Cfg& cfg) {
  return std::any_of(devs.begin(), devs.end(), [](Target* d){ return d->proto < ProtocolVersion::PROTOCOL_VER2; })
    ? cfg.pkt_any_old
    : cfg.pkt_all_v2plus;
}

static bool any_lz4(const std::vector<ImageSpec>& v) {
  return std::any_of(v.begin(), v.end(), [](const ImageSpec& s){ return s.lz4; });
}

static brokkr::core::Result<std::vector<ImageSpec>>
sources_common_mapping_or_empty(const std::vector<Target*>& devs,
                                const std::vector<ImageSpec>& sources) noexcept
{
  std::vector<ImageSpec> out;
  if (devs.empty()) return out;

  out.reserve(sources.size());

  for (const auto& s : sources) {
    const auto* ref = devs.front()->pit_table.find_by_file_name(s.basename);
    if (!ref) continue;

    for (auto* d : devs) {
      const auto* p = d->pit_table.find_by_file_name(s.basename);
      if (!p) goto next;
      if (p->id != ref->id || p->dev_type != ref->dev_type) return brokkr::core::fail("PIT mapping differs across devices");
    }

    out.push_back(s);
  next:
    (void)0;
  }

  return out;
}

struct Step {
  enum class Op : std::uint8_t { Quit, Begin, Data, End };
  Op op = Op::Quit;
  bool comp = false;

  u64 a = 0;
  const std::byte* base = nullptr;
  u64 off = 0;
  std::size_t n = 0;

  std::int32_t part_id = 0;
  std::int32_t dev_type = 0;
  bool last = false;
};

static Step st_begin(bool comp, u64 begin_sz) { return {.op=Step::Op::Begin,.comp=comp,.a=begin_sz}; }
static Step st_data(bool comp, const std::byte* base, u64 off, std::size_t n) { return {.op=Step::Op::Data,.comp=comp,.base=base,.off=off,.n=n}; }
static Step st_end(bool comp, u64 end_sz, std::int32_t part_id, std::int32_t dev_type, bool last) {
  return Step{.op=Step::Op::End,.comp=comp,.a=end_sz,.part_id=part_id,.dev_type=dev_type,.last=last};
}

template <class PF, class MakeContrib>
static brokkr::core::Status send_prefetched(PF& pf,
                                            std::barrier<>& sync,
                                            Step& cur,
                                            const std::size_t pkt,
                                            const bool comp,
                                            const std::int32_t part_id,
                                            const std::int32_t dev_type,
                                            const u64 total_bytes,
                                            const u64 item_total,
                                            u64& overall_done,
                                            u64& item_done,
                                            const Ui& ui,
                                            std::atomic_uint32_t& failed_count,
                                            const std::size_t ndevs,
                                            MakeContrib make_contrib) noexcept
{
  const u64 pkt64 = static_cast<u64>(pkt);

  auto emit = [&](Step s) { cur = s; sync.arrive_and_wait(); sync.arrive_and_wait(); };

  for (;;) {
    if (failed_count.load(std::memory_order_relaxed) >= ndevs) break;

    auto lease = pf.next();
    if (!lease) break;

    auto& w = lease->get();
    const u64 rounded = static_cast<u64>(w.rounded);
    const u64 packets = rounded / pkt64;

    emit(st_begin(comp, w.begin));
    auto contrib = make_contrib(w, packets);

    for (u64 p = 0; p < packets; ++p) {
      if (failed_count.load(std::memory_order_relaxed) >= ndevs) break;

      emit(st_data(comp, w.data(), p * pkt64, pkt));
      const u64 add = contrib(p);

      item_done += add;
      overall_done += add;
      if (ui.on_progress) ui.on_progress(overall_done, total_bytes, item_done, item_total);
    }

    emit(st_end(comp, w.end, part_id, dev_type, w.last));
    if (w.last || (failed_count.load(std::memory_order_relaxed) >= ndevs)) break;
  }

  auto pst = pf.status();
  return pst ? brokkr::core::Status{} : pst;
}

} // namespace

brokkr::core::Status flash(std::vector<Target*>& devs,
                           const std::vector<ImageSpec>& sources,
                           std::shared_ptr<const std::vector<std::byte>> pit_to_upload,
                           const Cfg& cfg,
                           Ui ui) noexcept
{
  if (devs.empty()) return brokkr::core::fail("flash: no devices");
  for (auto* d : devs) if (!d || !d->link || !d->link->connected()) return brokkr::core::fail("flash: transport not connected");

  const bool has_sources = !sources.empty();
  const bool has_pit = pit_to_upload && !pit_to_upload->empty();

  enum class Intent : std::uint8_t { RebootOnly, PitOnly, Flash };
  const Intent intent = has_sources ? Intent::Flash : (has_pit ? Intent::PitOnly : Intent::RebootOnly);

  const std::size_t total_devices = devs.size();
  std::size_t failed_total = 0;

  FirstError first_err;
  const auto sm_final = shutdown_mode_final(cfg);

  auto stage = [&](std::string_view s) { if (ui.on_stage) ui.on_stage(std::string(s)); };

  std::vector<Target*> active = devs;
  std::vector<std::size_t> active_idx;
  active_idx.reserve(devs.size());
  for (std::size_t i = 0; i < devs.size(); ++i) active_idx.push_back(i);

  auto fanout_keep = [&](auto&& fn) -> brokkr::core::Status {
    if (active.empty()) return brokkr::core::fail("No active devices");

    std::vector<brokkr::core::Status> sts(active.size(), brokkr::core::Status{});
    std::vector<std::jthread> ts;
    ts.reserve(active.size());

    for (std::size_t i = 0; i < active.size(); ++i) ts.emplace_back([&, i] { sts[i] = fn(*active[i]); });
    for (auto& t : ts) if (t.joinable()) t.join();

    std::vector<Target*> next;
    std::vector<std::size_t> next_idx;
    next.reserve(active.size());
    next_idx.reserve(active.size());

    for (std::size_t i = 0; i < active.size(); ++i) {
      if (sts[i]) { next.push_back(active[i]); next_idx.push_back(active_idx[i]); continue; }

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

  auto finish = [&](brokkr::core::Status st, bool call_done_always) -> brokkr::core::Status {
    if (!st) { log_summary(total_devices, total_devices); return st; }
    if (call_done_always && ui.on_done) ui.on_done();
    log_summary(total_devices, failed_total);
    return (failed_total > 0 || first_err.has_error()) ? first_err.status_or_ok()
                                                       : (ui.on_done ? (ui.on_done(), brokkr::core::Status{}) : brokkr::core::Status{});
  };

  auto set_flash_timeout_active = [&] { for (auto* d : active) link(*d).set_timeout_ms(cfg.flash_timeout_ms); };

  std::size_t pkt = 0;
  std::vector<PlanItem> plan;
  std::vector<FlashItem> items;
  std::vector<ImageSpec> effective_sources;
  u64 total = 0;

  auto shutdown_active = [&](OdinCommands::ShutdownMode m, std::string_view stg) -> brokkr::core::Status {
    log_shutdown_action(m);
    stage(stg);
    return fanout_keep([&](Target& d) { return OdinCommands(link(d)).shutdown(m); });
  };

  auto steps = std::vector<std::move_only_function<brokkr::core::Status()>>{};

  steps.emplace_back([&] {
    spdlog::info("HANDSHAKE");
    stage(kHandshakeStr);

    return fanout_keep([&](Target& d) -> brokkr::core::Status {
      auto& c = link(d);
      c.set_timeout_ms(cfg.preflash_timeout_ms);
      OdinCommands odin(c);

      BRK_TRY(odin.handshake(cfg.preflash_retries));
      BRK_TRYV(vr, odin.get_version(cfg.preflash_retries));

      d.init = vr;
      d.proto = d.init.protocol();
      return {};
    });
  });

  if (intent != Intent::RebootOnly) {
    steps.emplace_back([&] -> brokkr::core::Status {
      if (active.empty()) return brokkr::core::fail("No active devices");

      pkt = choose_pkt(active, cfg);
      stage(kPktFlashStr);

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
  }

  if (has_pit) {
    steps.emplace_back([&] {
      spdlog::info("Uploading PIT");
      stage(kPitUpStr);
      return fanout_keep([&](Target& d) {
        return OdinCommands(link(d)).set_pit({pit_to_upload->data(), pit_to_upload->size()}, cfg.preflash_retries);
      });
    });
  }

  if (intent != Intent::PitOnly) {
    steps.emplace_back([&] {
      spdlog::info("Get PIT for mapping");
      stage(kPitDlStr);
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
  }

  if (intent == Intent::RebootOnly) {
    steps.emplace_back([&] { return shutdown_active(cfg.reboot_after ? OdinCommands::ShutdownMode::Reboot : OdinCommands::ShutdownMode::NoReboot, kRebooting); });
  }

  if (intent == Intent::PitOnly) {
    steps.emplace_back([&] -> brokkr::core::Status {
      auto pr = pit::parse({pit_to_upload->data(), pit_to_upload->size()});
      if (!pr) return brokkr::core::fail(std::move(pr.error()));
      if (ui.on_model) ui.on_model(pr->cpu_bl_id);

      if (ui.on_plan) {
        PlanItem pi{.kind=PlanItem::Kind::Pit,.part_name="PIT (repartition)",.pit_file_name="PIT",.source_base="PIT",.size=pit_to_upload->size()};
        ui.on_plan({std::move(pi)}, static_cast<u64>(pit_to_upload->size()));
      }
      if (ui.on_item_active) ui.on_item_active(0);
      if (ui.on_progress) {
        const auto n = static_cast<u64>(pit_to_upload->size());
        ui.on_progress(0, n, 0, n);
      }
      if (ui.on_progress) {
        const auto n = static_cast<u64>(pit_to_upload->size());
        ui.on_progress(n, n, n, n);
      }
      if (ui.on_item_done) ui.on_item_done(0);
      return {};
    });

    steps.emplace_back([&] { return shutdown_active(sm_final, final_stage(sm_final)); });
  }

  if (intent == Intent::Flash) {
    steps.emplace_back([&] -> brokkr::core::Status {
      if (active.empty()) return brokkr::core::fail("No active devices");

      stage(kCpuCheck);
      const std::string ref = active.front()->pit_table.cpu_bl_id;
      if (ref.empty()) return brokkr::core::fail("PIT cpu_bl_id missing");
      for (auto* d : active) if (d->pit_table.cpu_bl_id != ref) return brokkr::core::fail("cpu_bl_id mismatch across devices");
      if (ui.on_model) ui.on_model(ref);
      return {};
    });

    steps.emplace_back([&] -> brokkr::core::Status {
      spdlog::info("Verifying PIT mapping");
      stage(kMapCheck);

      BRK_TRYV(eff, sources_common_mapping_or_empty(active, sources));
      effective_sources = std::move(eff);

      BRK_TRYV(items2, map_to_pit(active.front()->pit_table, effective_sources));
      items = std::move(items2);

      total = 0;
      for (const auto& it : items) BRK_TRY(detail::checked_add_u64(total, it.spec.size, "TOTALSIZE"));

      plan.clear();
      plan.reserve(items.size() + (has_pit ? 1u : 0u));

      if (has_pit) plan.push_back(PlanItem{.kind=PlanItem::Kind::Pit,.part_name="PIT (repartition)",.pit_file_name="PIT",.source_base="PIT",.size=pit_to_upload->size()});
      for (const auto& it : items) {
        plan.push_back(PlanItem{
          .kind=PlanItem::Kind::Part,
          .part_id=it.part.id,
          .dev_type=it.part.dev_type,
          .part_name=!it.part.name.empty()?it.part.name:it.part.file_name,
          .pit_file_name=it.part.file_name,
          .source_base=it.spec.source_basename.empty()?it.spec.basename:it.spec.source_basename,
          .size=it.spec.size
        });
      }

      if (ui.on_plan) ui.on_plan(plan, total);
      return {};
    });

    steps.emplace_back([&] {
      stage(kTotalSend);
      return fanout_keep([&](Target& d) { return OdinCommands(link(d)).send_total_size(total, d.proto, cfg.preflash_retries); });
    });

    steps.emplace_back([&] -> brokkr::core::Status {
      spdlog::info("Flashing has begun!");

      const bool use_lz4 = any_lz4(effective_sources) &&
        std::all_of(active.begin(), active.end(), [](Target* d){ return d->init.supports_compressed_download(); });

      log_speed(use_lz4);
      stage(use_lz4 ? kFlashFast : kFlashNorm);

      const std::size_t ndevs = active.size();
      if (!ndevs) return brokkr::core::fail("No active devices");

      Step cur{};
      std::barrier sync(static_cast<std::ptrdiff_t>(ndevs + 1));
      FirstError berr;
      std::atomic_uint32_t failed_count{0};
      std::vector<std::uint8_t> dead(ndevs, 0);

      auto exec = [&](OdinCommands& odin, const Step& s) -> brokkr::core::Status {
        if (s.op == Step::Op::Begin) return s.comp ? odin.begin_download_compressed(static_cast<std::int32_t>(s.a))
                                                   : odin.begin_download(static_cast<std::int32_t>(s.a));
        if (s.op == Step::Op::Data) {
          BRK_TRY(odin.send_raw({s.base + static_cast<std::ptrdiff_t>(s.off), s.n}));
          BRK_TRYV(_, odin.recv_checked_response(static_cast<std::int32_t>(RqtCommandType::RQT_EMPTY), nullptr));
          return {};
        }
        if (s.op == Step::Op::End) return s.comp ? odin.end_download_compressed(static_cast<std::int32_t>(s.a), s.part_id, s.dev_type, s.last)
                                                 : odin.end_download(static_cast<std::int32_t>(s.a), s.part_id, s.dev_type, s.last);
        return {};
      };

      std::vector<std::jthread> workers;
      workers.reserve(ndevs);

      for (std::size_t i = 0; i < ndevs; ++i) {
        auto* d = active[i];
        const std::size_t orig = active_idx[i];

        workers.emplace_back([&, d, i, orig](std::stop_token stt) {
          OdinCommands odin(link(*d));
          bool dead_local = false;

          for (;;) {
            sync.arrive_and_wait();
            const Step s = cur;

            const bool quit = (s.op == Step::Op::Quit) || stt.stop_requested();
            if (!quit && !dead_local) {
              auto rst = exec(odin, s);
              if (!rst) {
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

      auto emit = [&](Step s) { cur = s; sync.arrive_and_wait(); sync.arrive_and_wait(); };

      auto coordinator = [&]() -> brokkr::core::Status {
        u64 overall_done = 0;

        std::size_t plan_off = 0;
        if (has_pit) {
          if (ui.on_item_active) ui.on_item_active(0);
          if (ui.on_item_done) ui.on_item_done(0);
          plan_off = 1;
        }

        for (std::size_t idx = 0; idx < items.size(); ++idx) {
          if (failed_count.load(std::memory_order_relaxed) >= ndevs) break;

          const auto& item = items[idx];
          const std::size_t plan_idx = plan_off + idx;

          if (ui.on_item_active) ui.on_item_active(plan_idx);

          const u64 item_total = item.spec.size;
          u64 item_done = 0;

          if (item.spec.lz4 && use_lz4) {
            struct Slot {
              std::vector<std::byte> stream;
              u64 begin = 0, end = 0, rounded = 0;
              bool last = false;
              const std::byte* data() const { return stream.data(); }
            };

            BRK_TRYV(src0, item.spec.open());
            BRK_TRYV(reader, io::Lz4BlockStreamReader::open(std::move(src0)));

            const u64 total_decomp = reader.content_size();
            if (!total_decomp) return brokkr::core::fail("LZ4 content size is zero: " + item.spec.display);

            const std::size_t max_blocks = detail::lz4_nonfinal_block_limit(cfg.buffer_bytes);
            if (!max_blocks) return brokkr::core::fail("buffer_bytes too small for compressed download (needs >= 1MiB)");

            u64 sent = 0;

            brokkr::core::TwoSlotPrefetcher<Slot> pf(
              [&](Slot& s, std::stop_token stt) -> brokkr::core::Result<bool> {
                if (stt.stop_requested() || sent >= total_decomp) return false;

                const u64 rem = total_decomp - sent;
                const bool last = rem <= static_cast<u64>(max_blocks) * detail::kOneMiB;
                const u64 decomp_sz = last ? rem : static_cast<u64>(max_blocks) * detail::kOneMiB;

                const std::size_t blocks = !last ? static_cast<std::size_t>(decomp_sz / detail::kOneMiB)
                                                 : reader.blocks_remaining_1m();

                s.stream.clear();
                s.stream.reserve(blocks * (static_cast<std::size_t>(detail::kOneMiB) + 4));

                BRK_TRYV(comp, reader.read_n_blocks(blocks, s.stream));
                const u64 comp_sz = static_cast<u64>(comp);
                const u64 rounded_sz = detail::round_up64(comp_sz, pkt);

                s.stream.resize(static_cast<std::size_t>(rounded_sz), std::byte{0});
                s.begin = comp_sz;
                s.end = decomp_sz;
                s.rounded = rounded_sz;
                s.last = last;

                sent += decomp_sz;
                return true;
              },
              [&](Slot& s) { s.stream.reserve(max_blocks * (static_cast<std::size_t>(detail::kOneMiB) + 4)); }
            );

            if (ui.on_progress) ui.on_progress(overall_done, total, item_done, item_total);

            BRK_TRY(send_prefetched(
              pf, sync, cur, pkt, true, item.part.id, item.part.dev_type, total, item_total,
              overall_done, item_done, ui, failed_count, ndevs,
              [&](const Slot& w, u64 packets) {
                return [end=w.end, packets](u64 p) {
                  const auto c1 = ((p + 1) * end) / packets;
                  const auto c0 = (p * end) / packets;
                  return c1 - c0;
                };
              }
            ));

          } else {
            struct Slot {
              std::vector<std::byte> buf;
              u64 begin = 0, end = 0;
              std::size_t rounded = 0;
              bool last = false;
              const std::byte* data() const { return buf.data(); }
            };

            std::unique_ptr<io::ByteSource> src;

            if (item.spec.lz4) {
              BRK_TRYV(s0, item.spec.open());
              BRK_TRYV(d0, io::open_lz4_decompressed(std::move(s0)));
              src = std::move(d0);
            } else {
              BRK_TRYV(s0, item.spec.open());
              src = std::move(s0);
            }

            const u64 file_sz = src->size();
            if (!file_sz) return brokkr::core::fail("Empty source: " + item.spec.display);

            const std::size_t max_rounded = static_cast<std::size_t>(detail::round_up64(static_cast<u64>(cfg.buffer_bytes), static_cast<u64>(pkt)));
            u64 sent = 0;

            brokkr::core::TwoSlotPrefetcher<Slot> pf(
              [&](Slot& s, std::stop_token stt) -> brokkr::core::Result<bool> {
                if (stt.stop_requested() || sent >= file_sz) return false;

                const u64 rem = file_sz - sent;
                const u64 actual = std::min<u64>(rem, cfg.buffer_bytes);
                const u64 rounded_u64 = detail::round_up64(actual, pkt);
                const auto rounded = static_cast<std::size_t>(rounded_u64);

                s.buf.resize(rounded);

                auto rst = io::read_exact(*src, std::span<std::byte>(s.buf.data(), static_cast<std::size_t>(actual)));
                if (!rst) return brokkr::core::fail(std::move(rst.error()));

                if (rounded_u64 > actual) std::memset(s.buf.data() + static_cast<std::size_t>(actual), 0, rounded - static_cast<std::size_t>(actual));

                s.rounded = rounded;
                s.begin = rounded_u64;
                s.end = actual;
                s.last = (sent + actual >= file_sz);

                sent += actual;
                return true;
              },
              [&](Slot& s) { s.buf.reserve(max_rounded); }
            );

            if (ui.on_progress) ui.on_progress(overall_done, total, item_done, item_total);

            BRK_TRY(send_prefetched(
              pf, sync, cur, pkt, false, item.part.id, item.part.dev_type, total, item_total,
              overall_done, item_done, ui, failed_count, ndevs,
              [&](const Slot& w, u64 /*packets*/) {
                u64 rem2 = w.end;
                const u64 pkt64 = static_cast<u64>(pkt);
                return [rem2, pkt64](u64 /*p*/) mutable {
                  const u64 add = std::min<u64>(pkt64, rem2);
                  rem2 -= add;
                  return add;
                };
              }
            ));
          }

          if (ui.on_item_done) ui.on_item_done(plan_idx);
        }

        return {};
      };

      auto cst = coordinator();
      if (!cst) berr.set(std::move(cst));

      emit({.op=Step::Op::Quit});
      for (auto& t : workers) if (t.joinable()) t.join();

      const std::size_t bad_in_flash = static_cast<std::size_t>(failed_count.load(std::memory_order_relaxed));
      failed_total += bad_in_flash;
      if (bad_in_flash) first_err.set(berr.status_or_ok());

      {
        std::vector<Target*> survivors;
        std::vector<std::size_t> survivors_idx;
        survivors.reserve(active.size());
        survivors_idx.reserve(active.size());

        for (std::size_t i = 0; i < active.size(); ++i) if (!dead[i]) { survivors.push_back(active[i]); survivors_idx.push_back(active_idx[i]); }
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
      }
      return {};
    });
  }

  for (auto& fn : steps) {
    auto st = fn();
    if (!st) return finish(st, intent != Intent::Flash);
  }

  return finish({}, intent != Intent::Flash);
}

} // namespace brokkr::odin
