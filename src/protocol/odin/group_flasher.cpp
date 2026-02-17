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
#include "core/thread_pool.hpp"
#include "io/lz4_frame.hpp"
#include "protocol/odin/pit_transfer.hpp"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace brokkr::odin {

namespace {

using u64 = std::uint64_t;

struct StopFirstError {
    std::mutex m;
    std::exception_ptr first;
    std::atomic_bool stop{false};

    void set(std::exception_ptr ep) {
        stop.store(true, std::memory_order_relaxed);
        std::lock_guard lk(m);
        if (!first) first = ep;
    }
    void rethrow() {
        std::lock_guard lk(m);
        if (first) std::rethrow_exception(first);
    }
};

constexpr std::string_view kHandshakeStr = "ODIN handshake";
constexpr std::string_view kPktFlashStr = "Negotiating transfer options";
constexpr std::string_view kPitDlStr = "Downloading PIT(s)";
constexpr std::string_view kPitUpStr = "Uploading PIT";
constexpr std::string_view kCpuCheck   = "Checking if devices are equal";
constexpr std::string_view kMapCheck   = "Verifying PIT mapping";
constexpr std::string_view kTotalSend  = "Sending total size";
constexpr std::string_view kFlashFast  = "Flashing (Speed: Enhanced)";
constexpr std::string_view kFlashNorm  = "Flashing (Speed: Normal)";
constexpr std::string_view kRebooting  = "Rebooting devices";

static std::string_view final_stage(OdinCommands::ShutdownMode m) {
    if (m == OdinCommands::ShutdownMode::ReDownload) return "Finalizing + redownload";
    if (m == OdinCommands::ShutdownMode::Reboot) return "Finalizing + reboot";
    return "Finalizing";
}

static OdinCommands::ShutdownMode shutdown_mode(const Cfg& cfg) {
    return cfg.redownload_after ? OdinCommands::ShutdownMode::ReDownload :
           cfg.reboot_after     ? OdinCommands::ShutdownMode::Reboot :
                                  OdinCommands::ShutdownMode::NoReboot;
}

static std::size_t choose_pkt(const std::vector<Target*>& devs, const Cfg& cfg) {
    return std::any_of(devs.begin(), devs.end(), [](Target* d){ return d->proto < ProtocolVersion::PROTOCOL_VER2; })
        ? cfg.pkt_any_old
        : cfg.pkt_all_v2plus;
}

static bool any_lz4(const std::vector<ImageSpec>& v) {
    return std::any_of(v.begin(), v.end(), [](const ImageSpec& s){ return s.lz4; });
}

static void read_exact(io::ByteSource& src, std::byte* dst, std::size_t n, std::string_view name) {
    for (std::size_t off = 0; off < n;) {
        const std::size_t got = src.read({dst + off, n - off});
        if (!got) throw std::runtime_error("Short read: " + std::string(name));
        off += got;
    }
}

static std::vector<ImageSpec> sources_common_mapping_or_empty(const std::vector<Target*>& devs,
                                                              const std::vector<ImageSpec>& sources)
{
    if (devs.empty()) return {};
    std::vector<ImageSpec> out;
    out.reserve(sources.size());

    for (const auto& s : sources) {
        const auto* ref = devs.front()->pit_table.find_by_file_name(s.basename);
        if (!ref) continue;

        for (auto* d : devs) {
            const auto* p = d->pit_table.find_by_file_name(s.basename);
            if (!p) { ref = nullptr; break; }
            if (p->id != ref->id || p->dev_type != ref->dev_type) throw std::runtime_error("PIT mapping differs across devices");
        }
        if (ref) out.push_back(s);
    }

    return out;
}

static brokkr::core::IByteTransport& link(Target& d) {
    if (!d.link) throw std::runtime_error("flash: null transport");
    return *d.link;
}

struct Step {
    enum class Op : std::uint8_t { Quit, Begin, Data, End };

    Op op = Op::Quit;
    bool comp = false;

    u64 a = 0;   // Begin: size, End: size
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
    Step s{.op=Step::Op::End,.comp=comp,.a=end_sz,.part_id=part_id,.dev_type=dev_type,.last=last};
    return s;
}

template <class PF, class MakeContrib>
static void send_prefetched(PF& pf,
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
                            StopFirstError& err,
                            MakeContrib make_contrib)
{
    const u64 pkt64 = static_cast<u64>(pkt);

    auto emit = [&](Step s) {
        cur = s;
        sync.arrive_and_wait();
        sync.arrive_and_wait();
    };

    for (;;) {
        if (err.stop.load(std::memory_order_relaxed)) break;

        auto lease = pf.next();
        if (!lease) break;

        auto& w = lease->get();
        const u64 rounded = static_cast<u64>(w.rounded);
        const u64 packets = rounded / pkt64;

        emit(st_begin(comp, w.begin));
        auto contrib = make_contrib(w, packets);

        for (u64 p = 0; p < packets; ++p) {
            if (err.stop.load(std::memory_order_relaxed)) break;

            emit(st_data(comp, w.data(), p * pkt64, pkt));
            const u64 add = contrib(p);

            item_done += add;
            overall_done += add;
            if (ui.on_progress) ui.on_progress(overall_done, total_bytes, item_done, item_total);
        }

        emit(st_end(comp, w.end, part_id, dev_type, w.last));
        if (w.last || err.stop.load(std::memory_order_relaxed)) break;
    }
}

} // namespace

void flash(std::vector<Target*>& devs,
           const std::vector<ImageSpec>& sources,
           std::shared_ptr<const std::vector<std::byte>> pit_to_upload,
           const Cfg& cfg,
           Ui ui,
           Mode mode)
{
    if (devs.empty()) throw std::runtime_error("flash: no devices");
    for (auto* d : devs) if (!d || !d->link || !d->link->connected()) throw std::runtime_error("flash: transport not connected");

    StopFirstError err;
    brokkr::core::ThreadPool pool(devs.size());

    auto stage = [&](std::string_view s) { if (ui.on_stage) ui.on_stage(std::string(s)); };

    auto fanout = [&](auto&& fn) {
        for (auto* d : devs) pool.submit([&, d]{ try { fn(*d); } catch (...) { err.set(std::current_exception()); } });
        pool.wait();
        err.rethrow();
    };

    auto handshake_all = [&] {
        stage(kHandshakeStr);
        fanout([&](Target& d) {
            auto& c = link(d);
            c.set_timeout_ms(cfg.preflash_timeout_ms);
            OdinCommands odin(c);
            odin.handshake(cfg.preflash_retries);
            d.init = odin.get_version(cfg.preflash_retries);
            d.proto = d.init.protocol();
        });
    };

    auto set_flash_timeout_all = [&] {
        for (auto* d : devs) link(*d).set_timeout_ms(cfg.flash_timeout_ms);
    };

    auto setup_pkt = [&] -> std::size_t {
        const std::size_t pkt = choose_pkt(devs, cfg);
        stage(kPktFlashStr);
        fanout([&](Target& d) {
            if (d.proto < ProtocolVersion::PROTOCOL_VER2) return;
            auto& c = link(d);
            c.set_timeout_ms(cfg.preflash_timeout_ms);
            OdinCommands(c).setup_transfer_options(static_cast<std::int32_t>(pkt), cfg.preflash_retries);
        });
        set_flash_timeout_all();
        return pkt;
    };

    auto pit_upload_all = [&] {
        if (!pit_to_upload || pit_to_upload->empty()) return;
        stage(kPitUpStr);
        fanout([&](Target& d) {
            OdinCommands(link(d)).set_pit({pit_to_upload->data(), pit_to_upload->size()}, cfg.preflash_retries);
        });
    };

    auto pit_download_all = [&] {
        stage(kPitDlStr);
        set_flash_timeout_all();
        fanout([&](Target& d) {
            OdinCommands odin(link(d));
            d.pit_bytes = download_pit_bytes(odin);
            d.pit_table = pit::parse({d.pit_bytes.data(), d.pit_bytes.size()});
        });
    };

    auto cpu_bl_id_check = [&] {
        if (devs.empty()) return;
        if (devs.size() > 1) {
            stage(kCpuCheck);
            const std::string ref = devs.front()->pit_table.cpu_bl_id;
            if (ref.empty()) throw std::runtime_error("PIT cpu_bl_id missing");
            for (auto* d : devs) if (d->pit_table.cpu_bl_id != ref) throw std::runtime_error("cpu_bl_id mismatch across devices");
            if (ui.on_model) ui.on_model(ref);
        } else {
            if (ui.on_model) ui.on_model(devs.front()->pit_table.cpu_bl_id);
        }
    };

    auto shutdown_all = [&] {
        const auto sm = shutdown_mode(cfg);
        stage(final_stage(sm));
        fanout([&](Target& d) { OdinCommands(link(d)).shutdown(sm); });
    };

    if (mode == Mode::PitSetOnly) {
        if (!pit_to_upload || pit_to_upload->empty()) throw std::runtime_error("PitSetOnly requires non-empty pit_to_upload");

        handshake_all();
        (void)setup_pkt();

        if (ui.on_model) ui.on_model(pit::parse({pit_to_upload->data(), pit_to_upload->size()}).cpu_bl_id);

        if (ui.on_plan) {
            PlanItem pi{.kind=PlanItem::Kind::Pit,.part_name="PIT (repartition)",.pit_file_name="PIT",.source_base="PIT",.size=pit_to_upload->size()};
            ui.on_plan({std::move(pi)}, static_cast<u64>(pit_to_upload->size()));
        }
        if (ui.on_item_active) ui.on_item_active(0);
        if (ui.on_progress) {
            const auto n = static_cast<u64>(pit_to_upload->size());
            ui.on_progress(0, n, 0, n);
        }

        pit_upload_all();

        if (ui.on_progress) {
            const auto n = static_cast<u64>(pit_to_upload->size());
            ui.on_progress(n, n, n, n);
        }
        if (ui.on_item_done) ui.on_item_done(0);

        shutdown_all();
        if (ui.on_done) ui.on_done();
        return;
    }

    if (mode == Mode::RebootOnly) {
        handshake_all();
        pit_download_all();

        stage(kRebooting);
        fanout([&](Target& d) {
            OdinCommands(link(d)).shutdown(cfg.reboot_after ? OdinCommands::ShutdownMode::Reboot
                                                           : OdinCommands::ShutdownMode::NoReboot);
        });

        if (ui.on_done) ui.on_done();
        return;
    }

    if (sources.empty()) throw std::runtime_error("flash: no sources");

    handshake_all();
    const std::size_t pkt = setup_pkt();

    const bool has_pit = pit_to_upload && !pit_to_upload->empty();
    if (has_pit) pit_upload_all();
    pit_download_all();
    cpu_bl_id_check();

    stage(kMapCheck);
    const auto effective_sources = sources_common_mapping_or_empty(devs, sources);
    const auto items = map_to_pit(devs.front()->pit_table, effective_sources);

    u64 total = 0;
    for (const auto& it : items) detail::checked_add_u64(total, it.spec.size, "TOTALSIZE");

    std::vector<PlanItem> plan;
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

    stage(kTotalSend);
    fanout([&](Target& d) { OdinCommands(link(d)).send_total_size(total, d.proto, cfg.preflash_retries); });

    const bool use_lz4 = any_lz4(effective_sources) &&
        std::all_of(devs.begin(), devs.end(), [](Target* d){ return d->init.supports_compressed_download(); });

    stage(use_lz4 ? kFlashFast : kFlashNorm);

    Step cur{};
    std::barrier sync(static_cast<std::ptrdiff_t>(devs.size() + 1));

    auto exec = [&](OdinCommands& odin, const Step& s) {
        if (s.op == Step::Op::Begin) {
            if (s.comp) odin.begin_download_compressed(static_cast<std::int32_t>(s.a));
            else        odin.begin_download(static_cast<std::int32_t>(s.a));
            return;
        }
        if (s.op == Step::Op::Data) {
            odin.send_raw({s.base + static_cast<std::ptrdiff_t>(s.off), s.n});
            (void)odin.recv_checked_response(static_cast<std::int32_t>(RqtCommandType::RQT_EMPTY), nullptr);
            return;
        }
        if (s.op == Step::Op::End) {
            if (s.comp) odin.end_download_compressed(static_cast<std::int32_t>(s.a), s.part_id, s.dev_type, s.last);
            else        odin.end_download(static_cast<std::int32_t>(s.a), s.part_id, s.dev_type, s.last);
        }
    };

    std::vector<std::jthread> workers;
    workers.reserve(devs.size());

    for (auto* d : devs) {
        workers.emplace_back([&, d](std::stop_token st) {
            OdinCommands odin(link(*d));
            bool dead = false;

            for (;;) {
                sync.arrive_and_wait();
                const Step s = cur;

                const bool quit = (s.op == Step::Op::Quit) || st.stop_requested();
                if (!quit && !dead) {
                    try { exec(odin, s); }
                    catch (...) { err.set(std::current_exception()); dead = true; }
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

    auto coordinator = [&] {
        u64 overall_done = 0;

        std::size_t plan_off = 0;
        if (has_pit) {
            if (ui.on_item_active) ui.on_item_active(0);
            if (ui.on_item_done) ui.on_item_done(0);
            plan_off = 1;
        }

        for (std::size_t idx = 0; idx < items.size(); ++idx) {
            if (err.stop.load(std::memory_order_relaxed)) break;

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

                io::Lz4BlockStreamReader r(item.spec.open());
                const u64 total_decomp = r.content_size();
                if (!total_decomp) throw std::runtime_error("LZ4 content size is zero: " + item.spec.display);

                const std::size_t max_blocks = detail::lz4_nonfinal_block_limit(cfg.buffer_bytes);
                if (!max_blocks) throw std::runtime_error("buffer_bytes too small for compressed download (needs >= 1MiB)");

                u64 sent = 0;

                brokkr::core::TwoSlotPrefetcher<Slot> pf(
                    [&](Slot& s, std::stop_token st) -> bool {
                        if (st.stop_requested() || sent >= total_decomp) return false;
                        auto w = detail::prepare_lz4_window(r, sent, max_blocks, pkt, s.stream);
                        s.begin = w.comp_size;
                        s.end = w.decomp_size;
                        s.rounded = w.rounded_size;
                        s.last = w.last;
                        sent += w.decomp_size;
                        return true;
                    },
                    [&](Slot& s) { s.stream.reserve(max_blocks * (static_cast<std::size_t>(detail::kOneMiB) + 4)); }
                );

                if (ui.on_progress) ui.on_progress(overall_done, total, item_done, item_total);

                send_prefetched(
                    pf, sync, cur, pkt, true, item.part.id, item.part.dev_type, total, item_total,
                    overall_done, item_done, ui, err,
                    [&](const Slot& w, u64 packets) {
                        return [end=w.end, packets](u64 p) {
                            const auto c1 = ((p + 1) * end) / packets;
                            const auto c0 = (p * end) / packets;
                            return c1 - c0;
                        };
                    }
                );
            } else {
                struct Slot {
                    std::vector<std::byte> buf;
                    u64 begin = 0, end = 0;
                    std::size_t rounded = 0;
                    bool last = false;

                    const std::byte* data() const { return buf.data(); }
                };

                std::unique_ptr<io::ByteSource> src =
                    item.spec.lz4 ? io::open_lz4_decompressed(item.spec.open()) : item.spec.open();

                const u64 file_sz = src->size();
                if (!file_sz) throw std::runtime_error("Empty source: " + item.spec.display);

                const std::size_t max_rounded = static_cast<std::size_t>(
                    detail::round_up64(static_cast<u64>(cfg.buffer_bytes), static_cast<u64>(pkt))
                );

                u64 sent = 0;

                brokkr::core::TwoSlotPrefetcher<Slot> pf(
                    [&](Slot& s, std::stop_token st) -> bool {
                        if (st.stop_requested() || sent >= file_sz) return false;

                        const u64 rem = file_sz - sent;
                        const u64 actual = std::min<u64>(rem, cfg.buffer_bytes);
                        const u64 rounded_u64 = detail::round_up64(actual, pkt);
                        const auto rounded = static_cast<std::size_t>(rounded_u64);

                        s.buf.resize(rounded);
                        read_exact(*src, s.buf.data(), static_cast<std::size_t>(actual), item.spec.display);
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

                send_prefetched(
                    pf, sync, cur, pkt, false, item.part.id, item.part.dev_type, total, item_total,
                    overall_done, item_done, ui, err,
                    [&](const Slot& w, u64 /*packets*/) {
                        u64 rem = w.end;
                        const u64 pkt64 = static_cast<u64>(pkt);
                        return [rem, pkt64](u64 /*p*/) mutable {
                            const u64 add = std::min<u64>(pkt64, rem);
                            rem -= add;
                            return add;
                        };
                    }
                );
            }

            if (ui.on_item_done) ui.on_item_done(plan_idx);
        }
    };

    try { coordinator(); }
    catch (...) { err.set(std::current_exception()); }

    emit({.op=Step::Op::Quit});
    for (auto& t : workers) if (t.joinable()) t.join();

    err.rethrow();
    shutdown_all();
    if (ui.on_done) ui.on_done();
}

void flash(std::vector<UsbTarget*>& devs,
           const std::vector<ImageSpec>& sources,
           std::shared_ptr<const std::vector<std::byte>> pit_to_upload,
           const Cfg& cfg,
           Ui ui,
           Mode mode)
{
    if (devs.empty()) throw std::runtime_error("flash: no devices");

    if (ui.on_devices) {
        std::vector<std::string> ids;
        ids.reserve(devs.size());
        for (auto* d : devs) ids.push_back(d->devnode);
        ui.on_devices(devs.size(), ids);
    }

    if (ui.on_stage) ui.on_stage("Opening USB devices");
    for (auto* d : devs) {
        d->dev.open_and_init();
        if (!d->conn.open()) throw std::runtime_error("Failed to open USB connection: " + d->devnode);
        d->conn.set_timeout_ms(cfg.preflash_timeout_ms);
    }

    std::vector<Target> tmp;
    tmp.reserve(devs.size());
    std::vector<Target*> ptrs;
    ptrs.reserve(devs.size());

    for (auto* d : devs) tmp.emplace_back(Target{.id=d->devnode,.link=&d->conn,.init=d->init,.proto=d->proto,.pit_bytes=d->pit_bytes,.pit_table=d->pit_table});
    for (auto& t : tmp) ptrs.emplace_back(&t);

    flash(ptrs, sources, pit_to_upload, cfg, ui, mode);

    for (std::size_t i = 0; i < devs.size(); ++i) {
        devs[i]->init = tmp[i].init;
        devs[i]->proto = tmp[i].proto;
        devs[i]->pit_bytes = std::move(tmp[i].pit_bytes);
        devs[i]->pit_table = std::move(tmp[i].pit_table);
    }
}

} // namespace brokkr::odin
