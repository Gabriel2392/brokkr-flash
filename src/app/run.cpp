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

#include "app/run.hpp"

#include "app/cli.hpp"
#include "app/interface.hpp"
#include "app/md5_verify.hpp"
#include "core/str.hpp"
#include "platform/platform_all.hpp"

#include "protocol/odin/flash.hpp"
#include "protocol/odin/group_flasher.hpp"
#include "protocol/odin/odin_cmd.hpp"
#include "protocol/odin/pit.hpp"
#include "protocol/odin/pit_transfer.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brokkr::app {

using brokkr::platform::UsbDeviceSysfsInfo;

static constexpr std::uint16_t SAMSUNG_VID = 0x04E8;
static constexpr std::uint16_t ODIN_PIDS[] = { 0x6601, 0x685D, 0x68C3 };

static std::vector<std::uint16_t> default_pids() { return { std::begin(ODIN_PIDS), std::end(ODIN_PIDS) }; }
static std::string short_id(const UsbDeviceSysfsInfo& d) { return std::to_string(d.busnum) + ":" + std::to_string(d.devnum); }
static bool is_pit_name(std::string_view base) { return brokkr::core::ends_with_ci(base, ".pit"); }

static void print_connected() {
    brokkr::platform::EnumerateFilter f{.vendor=SAMSUNG_VID,.products=default_pids()};
    for (const auto& d : brokkr::platform::enumerate_usb_devices_sysfs(f)) {
        std::cout << d.sysname << "\t" << short_id(d) << "\t"
                  << std::hex << "0x" << d.vendor << "\t0x" << d.product << std::dec
                  << "\t" << d.connected_duration_sec << "(sec)\n";
    }
}

static UsbDeviceSysfsInfo select_target_or_throw(const Options& opt) {
    if (!opt.target_sysname) throw std::runtime_error("No --target specified");
    auto info = brokkr::platform::find_by_sysname(*opt.target_sysname);
    if (!info) throw std::runtime_error("Target sysname not found: " + *opt.target_sysname);
    if (info->vendor != SAMSUNG_VID) throw std::runtime_error("Target is not Samsung VID 0x04e8");
    const auto pids = default_pids();
    if (std::find(pids.begin(), pids.end(), info->product) == pids.end()) throw std::runtime_error("Target PID not in allowlist");
    return *info;
}

static std::vector<UsbDeviceSysfsInfo> enumerate_targets(const Options& opt) {
    if (opt.target_sysname) return { select_target_or_throw(opt) };
    brokkr::platform::EnumerateFilter f{.vendor=SAMSUNG_VID,.products=default_pids()};
    return brokkr::platform::enumerate_usb_devices_sysfs(f);
}

static std::vector<std::byte> read_file_all(const std::filesystem::path& p) {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(p, ec);
    if (ec) throw std::runtime_error("Cannot stat file: " + p.string());

    constexpr std::uint64_t kMax = 256ull * 1024ull * 1024ull;
    if (static_cast<std::uint64_t>(sz) > kMax) throw std::runtime_error("File too large to read into memory: " + p.string());

    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) throw std::runtime_error("Cannot open file: " + p.string());

    std::vector<std::byte> buf(static_cast<std::size_t>(sz));
    if (!buf.empty()) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        if (!in.good()) throw std::runtime_error("Failed to read file: " + p.string());
    }
    return buf;
}

static void write_file_all(const std::filesystem::path& p, const std::vector<std::byte>& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) throw std::runtime_error("Cannot write file: " + p.string());
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out.good()) throw std::runtime_error("Write failed: " + p.string());
}

static std::vector<std::byte> read_all(brokkr::io::ByteSource& src) {
    const auto sz64 = src.size();
    if (sz64 > (256ull * 1024ull * 1024ull)) throw std::runtime_error("Refusing huge source: " + src.display_name());
    const auto n = static_cast<std::size_t>(sz64);

    std::vector<std::byte> out(n);
    for (std::size_t off = 0; off < n;) {
        const std::size_t got = src.read({out.data() + off, n - off});
        if (!got) throw std::runtime_error("Short read: " + src.display_name());
        off += got;
    }
    return out;
}

static std::shared_ptr<const std::vector<std::byte>> pit_from_specs(const std::vector<brokkr::odin::ImageSpec>& specs) {
    const brokkr::odin::ImageSpec* pit = nullptr;
    for (const auto& s : specs) if (is_pit_name(s.basename)) pit = &s;
    if (!pit) return {};

    auto src = pit->open();
    return std::make_shared<const std::vector<std::byte>>(read_all(*src));
}

static void print_pit_table(const brokkr::odin::pit::PitTable& t) {
    std::cout << "PIT TABLE\n"
              << "cpu_bl_id: " << (t.cpu_bl_id.empty() ? "-" : t.cpu_bl_id) << "\n"
              << "com_tar2:  " << (t.com_tar2.empty() ? "-" : t.com_tar2) << "\n"
              << "lu_count:  " << t.lu_count << "\n"
              << "entries:   " << t.partitions.size() << "\n\n"
              << "------------------------------------------------------------\n";

    for (std::size_t i = 0; i < t.partitions.size(); ++i) {
        const auto& p = t.partitions[i];
        std::cout << "ENTRY (#" << i << ")\n"
                  << "  id:          " << p.id << "\n"
                  << "  dev_type:    " << p.dev_type << "\n"
                  << "  block_count: " << p.block_size << "\n"
                  << "  block_size:  " << p.block_bytes << "\n"
                  << "  file_size:   " << p.file_size << "\n"
                  << "  name:        " << (p.name.empty() ? "-" : p.name) << "\n"
                  << "  file_name:   " << (p.file_name.empty() ? "-" : p.file_name) << "\n"
                  << "------------------------------------------------------------\n";
    }
    std::cout.flush();
}

static bool has_flash_files(const Options& opt) {
    return opt.file_a || opt.file_b || opt.file_c || opt.file_s || opt.file_u;
}

static std::vector<std::filesystem::path> build_flash_inputs(const Options& opt) {
    std::vector<std::filesystem::path> v;
    v.reserve(5);
    if (opt.file_b) v.push_back(*opt.file_b);
    if (opt.file_a) v.push_back(*opt.file_a);
    if (opt.file_c) v.push_back(*opt.file_c);
    if (opt.file_s) v.push_back(*opt.file_s);
    if (opt.file_u) v.push_back(*opt.file_u);
    return v;
}

template <class Fn>
static decltype(auto) with_odin(brokkr::core::IByteTransport& link, const brokkr::odin::Cfg& cfg, Fn&& fn) {
    brokkr::odin::OdinCommands odin(link);
    link.set_timeout_ms(cfg.preflash_timeout_ms);
    odin.handshake(cfg.preflash_retries);
    (void)odin.get_version(cfg.preflash_retries);
    link.set_timeout_ms(cfg.flash_timeout_ms);
    return std::forward<Fn>(fn)(odin);
}

/* --- wireless --- */
// TODO: Test with WPS.

static int run_wireless(const Options& opt) {
    if (opt.print_pit && opt.pit_print_in) {
        const auto bytes = read_file_all(*opt.pit_print_in);
        print_pit_table(brokkr::odin::pit::parse({bytes.data(), bytes.size()}));
        return 0;
    }

    auto lock = brokkr::platform::SingleInstanceLock::try_acquire("brokkr-engine");
    if (!lock) { std::cerr << "Another instance is already running\n"; return 2; }

    FlashInterface ui;
    ui.stage("Waiting for wireless device");

    brokkr::platform::TcpListener lst;
    lst.bind_and_listen("0.0.0.0", 13579);

    auto conn = lst.accept_one();
    const std::string dev_id = "wifi:" + conn.peer_label();

    auto shield = brokkr::core::SignalShield::enable([&](const char* sig_desc, int count) {
        ui.notice(std::string(sig_desc) + " ignored (" + std::to_string(count) + ") - do not disconnect");
    });

    brokkr::odin::Ui hooks;
    hooks.on_devices = [&](std::size_t n, const std::vector<std::string>& ids) { ui.devices(n, ids); };
    hooks.on_model   = [&](const std::string& m) { ui.cpu_bl_id(m); };
    hooks.on_stage   = [&](const std::string& s) { ui.stage(s); };
    hooks.on_plan    = [&](const std::vector<brokkr::odin::PlanItem>& p, std::uint64_t t) { ui.plan(p, t); };
    hooks.on_item_active = [&](std::size_t i) { ui.active(i); };
    hooks.on_item_done   = [&](std::size_t i) { ui.done_item(i); };
    hooks.on_progress    = [&](std::uint64_t od, std::uint64_t ot, std::uint64_t id, std::uint64_t it) { ui.progress(od, ot, id, it); };
    hooks.on_error       = [&](const std::string& msg) { ui.fail(msg); };
    hooks.on_done        = [&] { ui.done("DONE"); };

    if (hooks.on_devices) hooks.on_devices(1, std::vector<std::string>{dev_id});

    brokkr::odin::Cfg cfg;
    cfg.reboot_after = opt.reboot_after_flash;
    cfg.redownload_after = opt.redownload;

    const auto pit_shutdown_mode =
        opt.redownload ? brokkr::odin::OdinCommands::ShutdownMode::ReDownload
                       : opt.reboot_after_flash ? brokkr::odin::OdinCommands::ShutdownMode::Reboot
                                                : brokkr::odin::OdinCommands::ShutdownMode::NoReboot;

    try {
        if (opt.print_pit && !opt.pit_print_in) {
            const auto bytes = with_odin(conn, cfg, [&](brokkr::odin::OdinCommands& odin) { return brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries); });
            print_pit_table(brokkr::odin::pit::parse({bytes.data(), bytes.size()}));
            brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode);
            return 0;
        }

        if (opt.pit_get_out) {
            const auto bytes = with_odin(conn, cfg, [&](brokkr::odin::OdinCommands& odin) { return brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries); });
            write_file_all(*opt.pit_get_out, bytes);
            ui.done("Saved PIT to " + opt.pit_get_out->string());
            brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode);
            return 0;
        }

        std::shared_ptr<const std::vector<std::byte>> pit_to_upload;
        if (opt.pit_set_in) {
            auto pit_bytes = std::make_shared<std::vector<std::byte>>(read_file_all(*opt.pit_set_in));
            if (!has_flash_files(opt)) {
                with_odin(conn, cfg, [&](brokkr::odin::OdinCommands& odin) { odin.set_pit({pit_bytes->data(), pit_bytes->size()}, cfg.preflash_retries); return 0; });
                ui.done("Uploaded PIT from " + opt.pit_set_in->string());
                brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode);
                return 0;
            }
            pit_to_upload = std::move(pit_bytes);
        }

        if (!has_flash_files(opt) && !opt.reboot_only) { std::cerr << usage_text(); return 1; }

        brokkr::odin::Target t{.id=dev_id,.link=&conn};
        std::vector<brokkr::odin::Target*> devs{&t};

        if (opt.reboot_only) { brokkr::odin::flash(devs, {}, {}, cfg, hooks, brokkr::odin::Mode::RebootOnly); return 0; }

        const auto inputs = build_flash_inputs(opt);
        md5_verify(md5_jobs(inputs), ui);

        auto specs = brokkr::odin::expand_inputs_tar_or_raw(inputs);
        const bool dl_mode = std::any_of(specs.begin(), specs.end(), [](const brokkr::odin::ImageSpec& s){ return s.download_list_mode; });

        if (!pit_to_upload && !dl_mode) pit_to_upload = pit_from_specs(specs);

        std::vector<brokkr::odin::ImageSpec> srcs;
        for (auto& s : specs) if (!is_pit_name(s.basename)) srcs.push_back(std::move(s));
        if (srcs.empty()) throw std::runtime_error("No flashable images.");

        brokkr::odin::flash(devs, srcs, pit_to_upload, cfg, hooks, brokkr::odin::Mode::Flash);
        return 0;

    } catch (const std::exception& e) {
        const std::string msg = std::string("ERROR: ") + e.what();
        ui.fail(msg);
        std::cerr << msg << "\n";
        return 1;
    }
}

/* --- USB/main --- */

int run(const Options& opt) {
    if (opt.wireless) return run_wireless(opt);
    if (opt.print_connected) { print_connected(); return 0; }

    if (opt.print_pit && opt.pit_print_in) {
        const auto bytes = read_file_all(*opt.pit_print_in);
        print_pit_table(brokkr::odin::pit::parse({bytes.data(), bytes.size()}));
        return 0;
    }

    auto lock = brokkr::platform::SingleInstanceLock::try_acquire("brokkr-engine");
    if (!lock) { std::cerr << "Another instance is already running\n"; return 2; }

    auto targets = enumerate_targets(opt);
    if (targets.empty()) { std::cerr << "No supported devices found.\n"; return 3; }

    brokkr::odin::Cfg cfg;
    cfg.reboot_after = opt.reboot_after_flash;
    cfg.redownload_after = opt.redownload;

    const auto pit_shutdown_mode =
        opt.redownload ? brokkr::odin::OdinCommands::ShutdownMode::ReDownload
                       : opt.reboot_after_flash ? brokkr::odin::OdinCommands::ShutdownMode::Reboot
                                                : brokkr::odin::OdinCommands::ShutdownMode::NoReboot;

    auto usb_one = [&](const UsbDeviceSysfsInfo& info, auto&& fn) {
        brokkr::platform::UsbFsDevice dev(info.devnode());
        dev.open_and_init();
        brokkr::platform::UsbFsConnection conn(dev);
        if (!conn.open()) throw std::runtime_error("Failed to open USB connection");
        return fn(conn);
    };

    if (opt.print_pit && !opt.pit_print_in) {
        if (targets.size() != 1) { std::cerr << "--print-pit without a file requires exactly one device (use --target).\n"; return 4; }
        usb_one(targets.front(), [&](brokkr::platform::UsbFsConnection& conn) {
            const auto bytes = with_odin(conn, cfg, [&](brokkr::odin::OdinCommands& odin) { return brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries); });
            print_pit_table(brokkr::odin::pit::parse({bytes.data(), bytes.size()}));
            brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode);
            return 0;
        });
        return 0;
    }

    if (opt.pit_get_out) {
        if (targets.size() != 1) { std::cerr << "PIT get requires exactly one device (use --target).\n"; return 4; }
        usb_one(targets.front(), [&](brokkr::platform::UsbFsConnection& conn) {
            const auto bytes = with_odin(conn, cfg, [&](brokkr::odin::OdinCommands& odin) { return brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries); });
            write_file_all(*opt.pit_get_out, bytes);
            std::cout << "Saved PIT to " << opt.pit_get_out->string() << "\n";
            brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode);
            return 0;
        });
        return 0;
    }

    std::shared_ptr<const std::vector<std::byte>> pit_to_upload;
    if (opt.pit_set_in) pit_to_upload = std::make_shared<std::vector<std::byte>>(read_file_all(*opt.pit_set_in));

    if (!has_flash_files(opt) && !opt.reboot_only && !opt.pit_set_in) { std::cerr << usage_text(); return 1; }

    std::vector<std::unique_ptr<brokkr::odin::UsbTarget>> storage;
    storage.reserve(targets.size());
    std::vector<brokkr::odin::UsbTarget*> devs;
    devs.reserve(targets.size());

    for (const auto& t : targets) {
        auto ctx = std::make_unique<brokkr::odin::UsbTarget>(short_id(t), t.devnode());
        devs.push_back(ctx.get());
        storage.push_back(std::move(ctx));
    }

    FlashInterface ui;

    auto shield = brokkr::core::SignalShield::enable([&](const char* sig_desc, int count) {
        ui.notice(std::string(sig_desc) + " ignored (" + std::to_string(count) + ") - do not disconnect");
    });

    brokkr::odin::Ui hooks;
    hooks.on_devices = [&](std::size_t n, const std::vector<std::string>& ids) { ui.devices(n, ids); };
    hooks.on_model   = [&](const std::string& m) { ui.cpu_bl_id(m); };
    hooks.on_stage   = [&](const std::string& s) { ui.stage(s); };
    hooks.on_plan    = [&](const std::vector<brokkr::odin::PlanItem>& p, std::uint64_t t) { ui.plan(p, t); };
    hooks.on_item_active = [&](std::size_t i) { ui.active(i); };
    hooks.on_item_done   = [&](std::size_t i) { ui.done_item(i); };
    hooks.on_progress    = [&](std::uint64_t od, std::uint64_t ot, std::uint64_t id, std::uint64_t it) { ui.progress(od, ot, id, it); };
    hooks.on_error       = [&](const std::string& msg) { ui.fail(msg); };
    hooks.on_done        = [&] { ui.done("DONE"); };

    try {
        if (opt.reboot_only) { brokkr::odin::flash(devs, {}, {}, cfg, hooks, brokkr::odin::Mode::RebootOnly); return 0; }
        if (opt.pit_set_in && !has_flash_files(opt)) { brokkr::odin::flash(devs, {}, pit_to_upload, cfg, hooks, brokkr::odin::Mode::PitSetOnly); return 0; }

        const auto inputs = build_flash_inputs(opt);
        md5_verify(md5_jobs(inputs), ui);

        auto specs = brokkr::odin::expand_inputs_tar_or_raw(inputs);
        const bool dl_mode = std::any_of(specs.begin(), specs.end(), [](const brokkr::odin::ImageSpec& s){ return s.download_list_mode; });

        if (!pit_to_upload && !dl_mode) pit_to_upload = pit_from_specs(specs);

        std::vector<brokkr::odin::ImageSpec> srcs;
        srcs.reserve(specs.size());
        for (auto& s : specs) if (!is_pit_name(s.basename)) srcs.push_back(std::move(s));
        if (srcs.empty()) throw std::runtime_error("No flashable images.");

        brokkr::odin::flash(devs, srcs, pit_to_upload, cfg, hooks, brokkr::odin::Mode::Flash);
        return 0;

    } catch (const std::exception& e) {
        const std::string msg = std::string("ERROR: ") + e.what();
        ui.fail(msg);
        std::cerr << msg << "\n";
        return 1;
    }
}

} // namespace brokkr::app
