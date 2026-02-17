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

#include "app/interface.hpp"
#include "app/md5_verify.hpp"

#include "core/str.hpp"
#include "platform/platform_all.hpp"

#include "protocol/odin/flash.hpp"
#include "protocol/odin/group_flasher.hpp"
#include "protocol/odin/pit.hpp"
#include "protocol/odin/pit_transfer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

namespace brokkr::app {

using brokkr::platform::UsbDeviceSysfsInfo;

static constexpr std::uint16_t SAMSUNG_VID = 0x04E8;
static constexpr std::uint16_t ODIN_PIDS[] = {0x6601, 0x685D, 0x68C3};

static std::vector<std::uint16_t> default_pids() { return {std::begin(ODIN_PIDS), std::end(ODIN_PIDS)}; }
static bool is_pit_name(std::string_view base) { return brokkr::core::ends_with_ci(base, ".pit"); }

static void print_connected(bool only) {
  brokkr::platform::EnumerateFilter f{.vendor = SAMSUNG_VID, .products = default_pids()};
  for (const auto& d : brokkr::platform::enumerate_usb_devices_sysfs(f)) {
    if (only) { std::cout << d.sysname << "\n"; continue; }
    spdlog::info("Found device: {}", d.describe());
  }
}

static std::optional<UsbDeviceSysfsInfo> select_target(const Options& opt) {
  if (!opt.target_sysname) { spdlog::error("No target sysname specified"); return {}; }

  auto info = brokkr::platform::find_by_sysname(*opt.target_sysname);
  if (!info) { spdlog::error("No device found with sysname: {}", *opt.target_sysname); return {}; }

  if (info->vendor != SAMSUNG_VID) {
    spdlog::error("Device {} has wrong VID: expected 0x{:04x}, got 0x{:04x}", info->sysname, SAMSUNG_VID, info->vendor);
    return {};
  }

  const auto pids = default_pids();
  if (std::ranges::find(pids, info->product) == pids.end()) {
    spdlog::error("Device {} has wrong PID: expected one of {}, got 0x{:04x}",
                  info->sysname, fmt::join(pids, " "), info->product);
    return {};
  }
  return *info;
}

static std::vector<UsbDeviceSysfsInfo> enumerate_targets(const Options& opt) {
  if (opt.target_sysname) {
    auto tgt = select_target(opt);
    return tgt ? std::vector<UsbDeviceSysfsInfo>{*tgt} : std::vector<UsbDeviceSysfsInfo>{};
  }
  brokkr::platform::EnumerateFilter f{.vendor = SAMSUNG_VID, .products = default_pids()};
  return brokkr::platform::enumerate_usb_devices_sysfs(f);
}

struct File {
  using ByteArray = std::vector<std::byte>;
  static constexpr std::uint64_t kMax = 256ull * 1024ull * 1024ull;

  static brokkr::core::Result<ByteArray> read_all(const std::filesystem::path& p) noexcept {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(p, ec);
    if (ec) return brokkr::core::Result<ByteArray>::Failf("Cannot stat file: {}", p.string());
    if (static_cast<std::uint64_t>(sz) > kMax) return brokkr::core::Result<ByteArray>::Failf("File too large: {}", p.string());

    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return brokkr::core::Result<ByteArray>::Failf("Cannot open file: {}", p.string());

    ByteArray buf(static_cast<std::size_t>(sz));
    if (!buf.empty()) {
      in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
      if (!in.good()) return brokkr::core::Result<ByteArray>::Failf("Read failed: {}", p.string());
    }
    return brokkr::core::Result<ByteArray>::Ok(std::move(buf));
  }

  static brokkr::core::Status write_all(const std::filesystem::path& p, const ByteArray& data) noexcept {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return brokkr::core::Status::Failf("Cannot write: {}", p.string());
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out.good()) return brokkr::core::Status::Failf("Write failed: {}", p.string());
    return brokkr::core::Status::Ok();
  }
};

static brokkr::core::Result<File::ByteArray> read_all_source(brokkr::io::ByteSource& src) noexcept {
  const auto sz64 = src.size();
  if (sz64 > File::kMax) return brokkr::core::Result<File::ByteArray>::Fail("Source too large: " + src.display_name());

  File::ByteArray out(static_cast<std::size_t>(sz64));
  for (std::size_t off = 0; off < out.size();) {
    const std::size_t got = src.read({out.data() + off, out.size() - off});
    if (!got) {
      auto st = src.status();
      if (!st.ok) return brokkr::core::Result<File::ByteArray>::Fail(std::move(st.msg));
      return brokkr::core::Result<File::ByteArray>::Fail("Short read: " + src.display_name());
    }
    off += got;
  }
  return brokkr::core::Result<File::ByteArray>::Ok(std::move(out));
}

static std::shared_ptr<const File::ByteArray> pit_from_specs(const std::vector<brokkr::odin::ImageSpec>& specs) {
  const brokkr::odin::ImageSpec* pit = nullptr;
  for (const auto& s : specs) if (is_pit_name(s.basename)) pit = &s;
  if (!pit) return {};

  auto sr = pit->open();
  if (!sr) { spdlog::error("PIT open failed: {}", sr.st.msg); return {}; }

  auto rr = read_all_source(*sr.value);
  if (!rr) { spdlog::error("PIT read failed: {}", rr.st.msg); return {}; }

  return std::make_shared<const File::ByteArray>(std::move(rr.value));
}

static void print_pit_table(const brokkr::odin::pit::PitTable& t) {
  auto d = [&](const std::string& s){ return s.empty() ? "-" : s; };

  spdlog::info("PIT TABLE");
  spdlog::info("cpu_bl_id: {}", d(t.cpu_bl_id));
  spdlog::info("com_tar2:  {}", d(t.com_tar2));
  spdlog::info("lu_count:  {}", t.lu_count);
  spdlog::info("entries:   {}", t.partitions.size());
  spdlog::info("------------------------------------------------------------");

  for (std::size_t i = 0; i < t.partitions.size(); ++i) {
    const auto& p = t.partitions[i];
    spdlog::info("Partition #{}:", i);
    spdlog::info("id: {}", p.id);
    spdlog::info("dev_type: {}", p.dev_type);
    spdlog::info("block_count: {}", p.block_size);
    spdlog::info("block_size: {}", p.block_bytes);
    spdlog::info("file_size: {}", p.file_size);
    spdlog::info("name: {}", d(p.name));
    spdlog::info("file_name: {}", d(p.file_name));
    spdlog::info("------------------------------------------------------------");
  }
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
static brokkr::core::Status with_odin_status(brokkr::core::IByteTransport& link,
                                            const brokkr::odin::Cfg& cfg,
                                            Fn&& fn) noexcept
{
  brokkr::odin::OdinCommands odin(link);
  link.set_timeout_ms(cfg.preflash_timeout_ms);

  auto st = odin.handshake(cfg.preflash_retries);
  if (!st.ok) return st;

  auto vr = odin.get_version(cfg.preflash_retries);
  if (!vr) return brokkr::core::Status::Fail(std::move(vr.st.msg));

  link.set_timeout_ms(cfg.flash_timeout_ms);
  return fn(odin);
}

template <class Fn>
static auto with_odin_result(brokkr::core::IByteTransport& link,
                            const brokkr::odin::Cfg& cfg,
                            Fn&& fn) noexcept
  -> std::invoke_result_t<Fn, brokkr::odin::OdinCommands&>
{
  using R = std::invoke_result_t<Fn, brokkr::odin::OdinCommands&>;

  brokkr::odin::OdinCommands odin(link);
  link.set_timeout_ms(cfg.preflash_timeout_ms);

  auto st = odin.handshake(cfg.preflash_retries);
  if (!st.ok) return R::Fail(std::move(st.msg));

  auto vr = odin.get_version(cfg.preflash_retries);
  if (!vr) return R::Fail(std::move(vr.st.msg));

  link.set_timeout_ms(cfg.flash_timeout_ms);
  return fn(odin);
}

static brokkr::core::Result<brokkr::platform::UsbFsConnection>
open_single_connection(const UsbDeviceSysfsInfo& info, const brokkr::odin::Cfg& cfg) noexcept {
  brokkr::platform::UsbFsDevice dev(info.devnode());
  auto dst = dev.open_and_init();
  if (!dst.ok) return brokkr::core::Result<brokkr::platform::UsbFsConnection>::Fail(std::move(dst.msg));

  brokkr::platform::UsbFsConnection conn(dev);
  auto cst = conn.open();
  if (!cst.ok) return brokkr::core::Result<brokkr::platform::UsbFsConnection>::Fail(std::move(cst.msg));

  conn.set_timeout_ms(cfg.preflash_timeout_ms);

  return brokkr::core::Result<brokkr::platform::UsbFsConnection>::Fail("internal: open_single_connection not supported by-value");
}

/* --- wireless --- */

RunResult run_wireless(const Options& opt) {
  if (opt.print_pit && opt.pit_print_in) {
    auto br = File::read_all(*opt.pit_print_in);
    if (!br) { spdlog::error("{}", br.st.msg); return RunResult::kIOFail; }

    auto pr = brokkr::odin::pit::parse({br.value.data(), br.value.size()});
    if (!pr) { spdlog::error("{}", pr.st.msg); return RunResult::kIOFail; }

    print_pit_table(pr.value);
    return RunResult::Success;
  }

  auto lock = brokkr::platform::SingleInstanceLock::try_acquire("brokkr-engine");
  if (!lock) { spdlog::error("Another instance is already running"); return RunResult::kOtherInstanceRunning; }

  FlashInterface ui(!opt.gui_mode, opt.gui_mode);
  ui.stage("Waiting for wireless device");

  brokkr::platform::TcpListener lst;
  auto lstst = lst.bind_and_listen("0.0.0.0", 13579);
  if (!lstst.ok) { ui.fail(lstst.msg); return RunResult::kIOFail; }

  auto acr = lst.accept_one();
  if (!acr) { ui.fail(acr.st.msg); return RunResult::kIOFail; }

  auto conn = std::move(acr.value);
  const std::string dev_id = fmt::format("wifi:{}", conn.peer_label());

  auto shield = brokkr::core::SignalShield::enable([&](const char* sig_desc, int count) {
    ui.notice(fmt::format("{} ignored ({} times) - do not disconnect", sig_desc, count));
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
    opt.redownload ? brokkr::odin::OdinCommands::ShutdownMode::ReDownload :
    opt.reboot_after_flash ? brokkr::odin::OdinCommands::ShutdownMode::Reboot :
                             brokkr::odin::OdinCommands::ShutdownMode::NoReboot;

  const auto pit_shutdown_mode_for_manual = pit_shutdown_mode;

  if (opt.print_pit && !opt.pit_print_in) {
    auto br = with_odin_result(conn, cfg, [&](brokkr::odin::OdinCommands& odin) {
      return brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries);
    });
    if (!br) { ui.fail(br.st.msg); return RunResult::kIOFail; }

    auto pr = brokkr::odin::pit::parse({br.value.data(), br.value.size()});
    if (!pr) { ui.fail(pr.st.msg); return RunResult::kIOFail; }

    print_pit_table(pr.value);
    (void)brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode_for_manual);
    return RunResult::Success;
  }

  if (opt.pit_get_out) {
    auto br = with_odin_result(conn, cfg, [&](brokkr::odin::OdinCommands& odin) {
      return brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries);
    });
    if (!br) { ui.fail(br.st.msg); return RunResult::kIOFail; }

    auto st = File::write_all(*opt.pit_get_out, br.value);
    if (!st.ok) { ui.fail(st.msg); return RunResult::kIOFail; }

    ui.done("Saved PIT to " + opt.pit_get_out->string());
    (void)brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode_for_manual);
    return RunResult::Success;
  }

  if (opt.pit_set_in && !has_flash_files(opt) && !opt.reboot_only) {
    auto br = File::read_all(*opt.pit_set_in);
    if (!br) { ui.fail(br.st.msg); return RunResult::kIOFail; }

    auto pit_to_upload = std::make_shared<const std::vector<std::byte>>(std::move(br.value));

    brokkr::odin::Target t{.id = dev_id, .link = &conn};
    std::vector<brokkr::odin::Target*> devs{&t};

    auto st = brokkr::odin::flash(devs, {}, pit_to_upload, cfg, hooks, brokkr::odin::Mode::PitSetOnly);
    if (!st.ok) { ui.fail(st.msg); return RunResult::kIOFail; }
    return RunResult::Success;
  }

  if (!has_flash_files(opt) && !opt.reboot_only) {
    std::cerr << usage_text();
    return RunResult::InvalidUsage;
  }

  brokkr::odin::Target t{.id = dev_id, .link = &conn};
  std::vector<brokkr::odin::Target*> devs{&t};

  if (opt.reboot_only) {
    auto st = brokkr::odin::flash(devs, {}, {}, cfg, hooks, brokkr::odin::Mode::RebootOnly);
    if (!st.ok) { ui.fail(st.msg); return RunResult::kIOFail; }
    return RunResult::Success;
  }

  std::shared_ptr<const std::vector<std::byte>> pit_to_upload;
  if (opt.pit_set_in) {
    auto br = File::read_all(*opt.pit_set_in);
    if (!br) { ui.fail(br.st.msg); return RunResult::kIOFail; }
    pit_to_upload = std::make_shared<const std::vector<std::byte>>(std::move(br.value));
  }

  const auto inputs = build_flash_inputs(opt);

  auto jobsr = md5_jobs(inputs);
  if (!jobsr) { ui.fail(jobsr.st.msg); return RunResult::kIOFail; }

  auto vst = md5_verify(jobsr.value, ui);
  if (!vst.ok) { ui.fail(vst.msg); return RunResult::kIOFail; }

  auto specs = brokkr::odin::expand_inputs_tar_or_raw(inputs);
  if (!specs) { ui.fail(specs.st.msg); return RunResult::kIOFail; }

  const bool dl_mode = std::ranges::any_of(specs.value, [](const brokkr::odin::ImageSpec& s){ return s.download_list_mode; });

  if (!pit_to_upload && !dl_mode) {
    auto pit = pit_from_specs(specs.value);
    if (pit) pit_to_upload = pit;
  }

  std::vector<brokkr::odin::ImageSpec> srcs;
  for (auto& s : specs.value) if (!is_pit_name(s.basename)) srcs.push_back(std::move(s));
  if (srcs.empty()) { ui.fail("No valid flashable files"); return RunResult::NoFlashFiles; }

  auto st = brokkr::odin::flash(devs, srcs, pit_to_upload, cfg, hooks, brokkr::odin::Mode::Flash);
  if (!st.ok) { ui.fail(st.msg); return RunResult::kIOFail; }

  return RunResult::Success;
}

/* --- USB/main --- */

RunResult run(const Options& opt) {
  if (opt.print_connected) { spdlog::set_level(spdlog::level::info); print_connected(false); return RunResult::Success; }
  if (opt.print_connected_only) { print_connected(true); return RunResult::Success; }

  if (opt.print_pit && opt.pit_print_in) {
    auto br = File::read_all(*opt.pit_print_in);
    if (!br) { spdlog::error("{}", br.st.msg); return RunResult::kIOFail; }

    auto pr = brokkr::odin::pit::parse({br.value.data(), br.value.size()});
    if (!pr) { spdlog::error("{}", pr.st.msg); return RunResult::kIOFail; }

    print_pit_table(pr.value);
    return RunResult::Success;
  }

  auto lock = brokkr::platform::SingleInstanceLock::try_acquire("brokkr-engine");
  if (!lock) { spdlog::error("Another instance is already running"); return RunResult::kOtherInstanceRunning; }

  auto targets = enumerate_targets(opt);
  if (targets.empty()) { spdlog::error("No supported devices found."); return RunResult::NoDevices; }

  brokkr::odin::Cfg cfg;
  cfg.reboot_after = opt.reboot_after_flash;
  cfg.redownload_after = opt.redownload;

  const auto pit_shutdown_mode =
    opt.redownload ? brokkr::odin::OdinCommands::ShutdownMode::ReDownload :
    opt.reboot_after_flash ? brokkr::odin::OdinCommands::ShutdownMode::Reboot :
                             brokkr::odin::OdinCommands::ShutdownMode::NoReboot;

  if (opt.print_pit && !opt.pit_print_in) {
    if (targets.size() != 1) {
      spdlog::error("--print-pit without a file requires exactly one device (use --target)");
      return RunResult::InvalidUsage;
    }

    brokkr::platform::UsbFsDevice dev(targets.front().devnode());
    auto dst = dev.open_and_init();
    if (!dst.ok) { spdlog::error("{}", dst.msg); return RunResult::kIOFail; }

    brokkr::platform::UsbFsConnection conn(dev);
    auto cst = conn.open();
    if (!cst.ok) { spdlog::error("{}", cst.msg); return RunResult::kIOFail; }

    auto br = with_odin_result(conn, cfg, [&](brokkr::odin::OdinCommands& odin) {
      return brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries);
    });
    if (!br) { spdlog::error("{}", br.st.msg); return RunResult::kIOFail; }

    auto pr = brokkr::odin::pit::parse({br.value.data(), br.value.size()});
    if (!pr) { spdlog::error("{}", pr.st.msg); return RunResult::kIOFail; }

    print_pit_table(pr.value);
    (void)brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode);
    return RunResult::Success;
  }

  if (opt.pit_get_out) {
    if (targets.size() != 1) {
      spdlog::error("--get-pit requires exactly one device (use --target)");
      return RunResult::InvalidUsage;
    }

    brokkr::platform::UsbFsDevice dev(targets.front().devnode());
    auto dst = dev.open_and_init();
    if (!dst.ok) { spdlog::error("{}", dst.msg); return RunResult::kIOFail; }

    brokkr::platform::UsbFsConnection conn(dev);
    auto cst = conn.open();
    if (!cst.ok) { spdlog::error("{}", cst.msg); return RunResult::kIOFail; }

    auto br = with_odin_result(conn, cfg, [&](brokkr::odin::OdinCommands& odin) {
      return brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries);
    });
    if (!br) { spdlog::error("{}", br.st.msg); return RunResult::kIOFail; }

    auto st = File::write_all(*opt.pit_get_out, br.value);
    if (!st.ok) { spdlog::error("{}", st.msg); return RunResult::kIOFail; }

    spdlog::info("Saved PIT to {}", opt.pit_get_out->string());
    (void)brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode);
    return RunResult::Success;
  }

  std::shared_ptr<const std::vector<std::byte>> pit_to_upload;
  if (opt.pit_set_in) {
    auto br = File::read_all(*opt.pit_set_in);
    if (!br) { spdlog::error("{}", br.st.msg); return RunResult::kIOFail; }
    pit_to_upload = std::make_shared<const std::vector<std::byte>>(std::move(br.value));
  }

  std::vector<std::unique_ptr<brokkr::odin::UsbTarget>> storage;
  storage.reserve(targets.size());
  std::vector<brokkr::odin::UsbTarget*> devs;
  devs.reserve(targets.size());

  for (const auto& t : targets) {
    auto ctx = std::make_unique<brokkr::odin::UsbTarget>(t.devnode());
    devs.push_back(ctx.get());
    storage.push_back(std::move(ctx));
  }

  FlashInterface ui(!opt.gui_mode, opt.gui_mode);

  auto shield = brokkr::core::SignalShield::enable([&](const char* sig_desc, int count) {
    ui.notice(fmt::format("{} ignored ({} times) - do not disconnect", sig_desc, count));
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

  if (opt.reboot_only) {
    auto st = brokkr::odin::flash(devs, {}, {}, cfg, hooks, brokkr::odin::Mode::RebootOnly);
    if (!st.ok) { ui.fail(st.msg); return RunResult::kIOFail; }
    return RunResult::Success;
  }

  if (opt.pit_set_in && !has_flash_files(opt)) {
    if (!pit_to_upload || pit_to_upload->empty()) {
      ui.fail("PIT upload requested but PIT bytes are empty");
      return RunResult::kIOFail;
    }
    auto st = brokkr::odin::flash(devs, {}, pit_to_upload, cfg, hooks, brokkr::odin::Mode::PitSetOnly);
    if (!st.ok) { ui.fail(st.msg); return RunResult::kIOFail; }
    return RunResult::Success;
  }

  if (!has_flash_files(opt)) {
    std::cerr << usage_text();
    return RunResult::InvalidUsage;
  }

  const auto inputs = build_flash_inputs(opt);

  auto jobsr = md5_jobs(inputs);
  if (!jobsr) { ui.fail(jobsr.st.msg); return RunResult::kIOFail; }

  auto vst = md5_verify(jobsr.value, ui);
  if (!vst.ok) { ui.fail(vst.msg); return RunResult::kIOFail; }

  auto specs = brokkr::odin::expand_inputs_tar_or_raw(inputs);
  if (!specs) { ui.fail(specs.st.msg); return RunResult::kIOFail; }

  const bool dl_mode = std::ranges::any_of(specs.value, [](const brokkr::odin::ImageSpec& s){ return s.download_list_mode; });

  if (!pit_to_upload && !dl_mode) {
    auto pit = pit_from_specs(specs.value);
    if (pit) pit_to_upload = pit;
  }

  std::vector<brokkr::odin::ImageSpec> srcs;
  srcs.reserve(specs.value.size());
  for (auto& s : specs.value) if (!is_pit_name(s.basename)) srcs.push_back(std::move(s));
  if (srcs.empty()) { ui.fail("No valid flashable files"); return RunResult::NoFlashFiles; }

  auto st = brokkr::odin::flash(devs, srcs, pit_to_upload, cfg, hooks, brokkr::odin::Mode::Flash);
  if (!st.ok) { ui.fail(st.msg); return RunResult::kIOFail; }

  return RunResult::Success;
}

} // namespace brokkr::app
