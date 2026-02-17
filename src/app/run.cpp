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

#include <fmt/ranges.h>
#include <spdlog/spdlog.h>

namespace brokkr::app {

using brokkr::platform::UsbDeviceSysfsInfo;

static constexpr std::uint16_t SAMSUNG_VID = 0x04E8;
static constexpr std::uint16_t ODIN_PIDS[] = {0x6601, 0x685D, 0x68C3};

static std::vector<std::uint16_t> default_pids() {
  return {std::begin(ODIN_PIDS), std::end(ODIN_PIDS)};
}
static bool is_pit_name(std::string_view base) {
  return brokkr::core::ends_with_ci(base, ".pit");
}

static void print_connected(bool only) {
  brokkr::platform::EnumerateFilter f{.vendor = SAMSUNG_VID,
                                      .products = default_pids()};
  for (const auto &d : brokkr::platform::enumerate_usb_devices_sysfs(f)) {
    if (only) {
      std::cout << d.sysname << "\n";
      continue;
    }

    spdlog::info("Found device: {}", d.describe());
  }
}

static std::optional<UsbDeviceSysfsInfo> select_target(const Options &opt) {
  if (!opt.target_sysname) {
    spdlog::error("No target sysname specified");
    return {};
  }
  auto info = brokkr::platform::find_by_sysname(*opt.target_sysname);
  if (!info) {
    spdlog::error("No device found with sysname: {}", *opt.target_sysname);
    return {};
  }
  if (info->vendor != SAMSUNG_VID) {
    spdlog::error("Device {} has wrong VID: expected 0x{:04x}, got 0x{:04x}",
                  info->sysname, SAMSUNG_VID, info->vendor);
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

static std::vector<UsbDeviceSysfsInfo> enumerate_targets(const Options &opt) {
  if (opt.target_sysname) {
    auto tgt = select_target(opt);
    return tgt ? std::vector<UsbDeviceSysfsInfo>{*tgt}
               : std::vector<UsbDeviceSysfsInfo>{};
  }
  brokkr::platform::EnumerateFilter f{.vendor = SAMSUNG_VID,
                                      .products = default_pids()};
  return brokkr::platform::enumerate_usb_devices_sysfs(f);
}

struct File {

  using ByteArray = std::vector<std::byte>;
  using OptByteArray = std::optional<ByteArray>;
  constexpr static std::int64_t kMaxFileSize = 256ll * 1024ll * 1024ll;

  [[nodiscard]] static OptByteArray read_all(const std::filesystem::path &p) {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(p, ec);
    if (ec) {
      spdlog::error("Failed to get file size for {}: {}", p.string(),
                    ec.message());
      return {};
    }

    if (static_cast<std::uint64_t>(sz) > kMaxFileSize) {
      spdlog::error("File is too large ({} bytes): {}", sz, p.string());
      return {};
    }

    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) {
      spdlog::error("Cannot open file: {}", p.string());
      return {};
    }

    std::vector<std::byte> buf(static_cast<std::size_t>(sz));
    if (!buf.empty()) {
      in.read(reinterpret_cast<char *>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
      if (!in.good()) {
        spdlog::error("Read failed for file {}: only got {} bytes", p.string(),
                      in.gcount());
        return {};
      }
    }
    return buf;
  }

  [[nodiscard]] static bool write_all(const std::filesystem::path &p,
                                      const ByteArray &data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      spdlog::error("Cannot open file for writing: {}", p.string());
      return false;
    }
    out.write(reinterpret_cast<const char *>(data.data()),
              static_cast<std::streamsize>(data.size()));
    if (!out.good()) {
      spdlog::error("Write failed for file {}", p.string());
      return false;
    }
    return true;
  }

  [[nodiscard]] static OptByteArray read_all(brokkr::io::ByteSource &src) {
    const auto sz64 = src.size();
    if (sz64 > kMaxFileSize) {
      spdlog::error("Source is too large ({} bytes): {}", sz64,
                    src.display_name());
      return {};
    }
    const auto n = static_cast<std::size_t>(sz64);

    File::ByteArray out(n);
    for (std::size_t off = 0; off < n;) {
      const std::size_t got = src.read({out.data() + off, n - off});
      if (!got) {
        spdlog::error("Read failed for source {}: only got {} bytes after "
                      "reading {} total",
                      src.display_name(), got, off);
        return {};
      }
      off += got;
    }
    return out;
  }
};

static std::shared_ptr<const File::ByteArray>
pit_from_specs(const std::vector<brokkr::odin::ImageSpec> &specs) {
  const brokkr::odin::ImageSpec *pit = nullptr;
  for (const auto &s : specs)
    if (is_pit_name(s.basename))
      pit = &s;
  if (!pit)
    return {};

  auto src = pit->open();
  auto file = File::read_all(*src);
  if (!file) {
    spdlog::error("Failed to read PIT data from source: {}",
                  src->display_name());
    return {};
  }
  return std::make_shared<const File::ByteArray>(*file);
}

static void print_pit_table(const brokkr::odin::pit::PitTable &t) {
  spdlog::info("PIT TABLE");

  auto default_str = [](const std::string &s) { return s.empty() ? "-" : s; };

  spdlog::info("cpu_bl_id: {}", default_str(t.cpu_bl_id));
  spdlog::info("com_tar2:  {}", default_str(t.com_tar2));
  spdlog::info("lu_count:  {}", t.lu_count);
  spdlog::info("entries:   {}", t.partitions.size());
  spdlog::info("------------------------------------------------------------");

  for (std::size_t i = 0; i < t.partitions.size(); ++i) {
    const auto &p = t.partitions[i];
    spdlog::info("Partition #{}:", i);
    spdlog::info("id: {}", p.id);
    spdlog::info("dev_type: {}", p.dev_type);
    spdlog::info("block_count: {}", p.block_size);
    spdlog::info("block_size: {}", p.block_bytes);
    spdlog::info("file_size: {}", p.file_size);
    spdlog::info("name: {}", default_str(p.name));
    spdlog::info("file_name: {}", default_str(p.file_name));
    spdlog::info(
        "------------------------------------------------------------");
  }
  std::cout.flush();
}

static bool has_flash_files(const Options &opt) {
  return opt.file_a || opt.file_b || opt.file_c || opt.file_s || opt.file_u;
}

static std::vector<std::filesystem::path>
build_flash_inputs(const Options &opt) {
  std::vector<std::filesystem::path> v;
  v.reserve(5);
  if (opt.file_b)
    v.push_back(*opt.file_b);
  if (opt.file_a)
    v.push_back(*opt.file_a);
  if (opt.file_c)
    v.push_back(*opt.file_c);
  if (opt.file_s)
    v.push_back(*opt.file_s);
  if (opt.file_u)
    v.push_back(*opt.file_u);
  return v;
}

template <class Fn>
static decltype(auto) with_odin(brokkr::core::IByteTransport &link,
                                const brokkr::odin::Cfg &cfg, Fn &&fn) {
  brokkr::odin::OdinCommands odin(link);
  link.set_timeout_ms(cfg.preflash_timeout_ms);
  odin.handshake(cfg.preflash_retries);
  (void)odin.get_version(cfg.preflash_retries);
  link.set_timeout_ms(cfg.flash_timeout_ms);
  return std::forward<Fn>(fn)(odin);
}

/* --- wireless --- */
// TODO: Test with WPS.

RunResult run_wireless(const Options &opt) {
  if (opt.print_pit && opt.pit_print_in) {
    const auto bytes = File::read_all(*opt.pit_print_in);
    if (!bytes) {
      spdlog::error("Failed to read PIT file: {}", opt.pit_print_in->string());
      return RunResult::kIOFail;
    }
    print_pit_table(brokkr::odin::pit::parse({bytes->data(), bytes->size()}));
    return RunResult::Success;
  }

  auto lock =
      brokkr::platform::SingleInstanceLock::try_acquire("brokkr-engine");
  if (!lock) {
    spdlog::error("Another instance is already running");
    return RunResult::kOtherInstanceRunning;
  }

  FlashInterface ui(!opt.gui_mode, opt.gui_mode);
  ui.stage("Waiting for wireless device");

  brokkr::platform::TcpListener lst;
  if (!lst.bind_and_listen("0.0.0.0", 13579)) {
    ui.fail("Failed to bind to port 13579");
    return RunResult::kIOFail;
  }

  auto conn_ = lst.accept_one();
  if (!conn_) {
    ui.fail("Failed to accept incoming connection");
    return RunResult::kIOFail;
  }
  const std::string dev_id = fmt::format("wifi:{}", conn_->peer_label());

  auto shield =
      brokkr::core::SignalShield::enable([&](const char *sig_desc, int count) {
        ui.notice(fmt::format("{} ignored ({} times) - do not disconnect",
                              sig_desc, count));
      });

  brokkr::odin::Ui hooks;
  hooks.on_devices = [&](std::size_t n, const std::vector<std::string> &ids) {
    ui.devices(n, ids);
  };
  hooks.on_model = [&](const std::string &m) { ui.cpu_bl_id(m); };
  hooks.on_stage = [&](const std::string &s) { ui.stage(s); };
  hooks.on_plan = [&](const std::vector<brokkr::odin::PlanItem> &p,
                      std::uint64_t t) { ui.plan(p, t); };
  hooks.on_item_active = [&](std::size_t i) { ui.active(i); };
  hooks.on_item_done = [&](std::size_t i) { ui.done_item(i); };
  hooks.on_progress = [&](std::uint64_t od, std::uint64_t ot, std::uint64_t id,
                          std::uint64_t it) { ui.progress(od, ot, id, it); };
  hooks.on_error = [&](const std::string &msg) { ui.fail(msg); };
  hooks.on_done = [&] { ui.done("DONE"); };

  if (hooks.on_devices)
    hooks.on_devices(1, std::vector<std::string>{dev_id});

  brokkr::odin::Cfg cfg;
  cfg.reboot_after = opt.reboot_after_flash;
  cfg.redownload_after = opt.redownload;

  brokkr::odin::OdinCommands::ShutdownMode pit_shutdown_mode{};
  if (opt.redownload) {
    pit_shutdown_mode = brokkr::odin::OdinCommands::ShutdownMode::ReDownload;
  } else {
    if (opt.reboot_after_flash) {
      pit_shutdown_mode = brokkr::odin::OdinCommands::ShutdownMode::Reboot;
    } else {
      pit_shutdown_mode = brokkr::odin::OdinCommands::ShutdownMode::NoReboot;
    }
  }

  try {
    if (opt.print_pit && !opt.pit_print_in) {
      const auto bytes =
          with_odin(*conn_, cfg, [&](brokkr::odin::OdinCommands &odin) {
            return brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries);
          });
      print_pit_table(brokkr::odin::pit::parse({bytes.data(), bytes.size()}));
      brokkr::odin::OdinCommands(*conn_).shutdown(pit_shutdown_mode);
      return RunResult::Success;
    }

    if (opt.pit_get_out) {
      const auto bytes =
          with_odin(*conn_, cfg, [&](brokkr::odin::OdinCommands &odin) {
            return brokkr::odin::download_pit_bytes(odin, cfg.preflash_retries);
          });
      RunResult ret;
      if (!File::write_all(*opt.pit_get_out, bytes)) {
        ui.fail("Failed to save PIT to " + opt.pit_get_out->string());
        ret = RunResult::kIOFail;
      } else {
        ui.done("Saved PIT to " + opt.pit_get_out->string());
        ret = RunResult::Success;
      }
      brokkr::odin::OdinCommands(*conn_).shutdown(pit_shutdown_mode);
      return ret;
    }

    std::shared_ptr<const File::ByteArray> pit_to_upload;
    if (opt.pit_set_in) {
      auto bytes = File::read_all(*opt.pit_set_in);
      if (!bytes) {
        ui.fail("Failed to read PIT file: " + opt.pit_set_in->string());
        return RunResult::kIOFail;
      }
      auto pit_bytes = std::make_shared<File::ByteArray>(*bytes);
      if (!has_flash_files(opt)) {
        with_odin(*conn_, cfg, [&](brokkr::odin::OdinCommands &odin) {
          odin.set_pit({pit_bytes->data(), pit_bytes->size()},
                       cfg.preflash_retries);
          return true;
        });
        ui.done("Uploaded PIT from " + opt.pit_set_in->string());
        brokkr::odin::OdinCommands(*conn_).shutdown(pit_shutdown_mode);
        return RunResult::Success;
      }
      pit_to_upload = std::move(pit_bytes);
    }

    if (!has_flash_files(opt) && !opt.reboot_only) {
      std::cerr << usage_text();
      return RunResult::InvalidUsage;
    }

    brokkr::odin::Target t{.id = dev_id, .link = &*conn_};
    std::vector<brokkr::odin::Target *> devs{&t};

    if (opt.reboot_only) {
      brokkr::odin::flash(devs, {}, {}, cfg, hooks,
                          brokkr::odin::Mode::RebootOnly);
      return RunResult::Success;
    }

    const auto inputs = build_flash_inputs(opt);
    md5_verify(md5_jobs(inputs), ui);

    auto specs = brokkr::odin::expand_inputs_tar_or_raw(inputs);
    const bool dl_mode =
        std::ranges::any_of(specs, [](const brokkr::odin::ImageSpec &s) {
          return s.download_list_mode;
        });

    if (!pit_to_upload && !dl_mode) {
      pit_to_upload = pit_from_specs(specs);
    }

    std::vector<brokkr::odin::ImageSpec> srcs;
    for (auto &s : specs)
      if (!is_pit_name(s.basename))
        srcs.push_back(std::move(s));
    if (srcs.empty()) {
      spdlog::error("No valid flashable files");
      ui.fail("No valid flashable files");
      return RunResult::InvalidUsage;
    }

    brokkr::odin::flash(devs, srcs, pit_to_upload, cfg, hooks,
                        brokkr::odin::Mode::Flash);
    return RunResult::Success;

  } catch (const std::exception &e) {
    const std::string msg = std::string("ERROR: ") + e.what();
    ui.fail(msg);
    spdlog::error("Exception in wireless run: {}", e.what());
    return RunResult::Unknown;
  }
}

/* --- USB/main --- */

RunResult run(const Options &opt) {
  if (opt.print_connected) {
    // Enable the logging back (To show the device info) and print all devices,
    // not just the sysname.
    spdlog::set_level(spdlog::level::info);
    print_connected(false);
    return RunResult::Success;
  }

  if (opt.print_connected_only) {
    print_connected(true);
    return RunResult::Success;
  }

  if (opt.print_pit && opt.pit_print_in) {
    const auto bytes = File::read_all(*opt.pit_print_in);
    if (!bytes) {
      spdlog::error("Failed to read PIT file: {}", opt.pit_print_in->string());
      return RunResult::kIOFail;
    }
    print_pit_table(brokkr::odin::pit::parse({bytes->data(), bytes->size()}));
    return RunResult::Success;
  }

  auto lock =
      brokkr::platform::SingleInstanceLock::try_acquire("brokkr-engine");
  if (!lock) {
    spdlog::error("Another instance is already running");
    return RunResult::kOtherInstanceRunning;
  }

  auto targets = enumerate_targets(opt);
  if (targets.empty()) {
    spdlog::error("No supported devices found.");
    return RunResult::NoDevices;
  }

  brokkr::odin::Cfg cfg;
  cfg.reboot_after = opt.reboot_after_flash;
  cfg.redownload_after = opt.redownload;

  brokkr::odin::OdinCommands::ShutdownMode pit_shutdown_mode{};
  if (opt.redownload) {
    pit_shutdown_mode = brokkr::odin::OdinCommands::ShutdownMode::ReDownload;
  } else {
    if (opt.reboot_after_flash) {
      pit_shutdown_mode = brokkr::odin::OdinCommands::ShutdownMode::Reboot;
    } else {
      pit_shutdown_mode = brokkr::odin::OdinCommands::ShutdownMode::NoReboot;
    }
  }

  auto usb_one = [&](const UsbDeviceSysfsInfo &info, auto &&fn) {
    brokkr::platform::UsbFsDevice dev(info.devnode());
    dev.open_and_init();
    brokkr::platform::UsbFsConnection conn(dev);
    if (!conn.open()) {
      spdlog::error("Failed to open connection to device {}: {}", info.sysname,
                    info.devnode());
      return RunResult::ConnectionFail;
    }
    return fn(conn);
  };

  if (opt.print_pit && !opt.pit_print_in) {
    if (targets.size() != 1) {
      std::cerr << "--print-pit without a file requires exactly one device "
                   "(use --target).\n";
      return RunResult::InvalidUsage;
    }
    return usb_one(
        targets.front(), [&](brokkr::platform::UsbFsConnection &conn) {
          const auto bytes =
              with_odin(conn, cfg, [&](brokkr::odin::OdinCommands &odin) {
                return brokkr::odin::download_pit_bytes(odin,
                                                        cfg.preflash_retries);
              });
          print_pit_table(
              brokkr::odin::pit::parse({bytes.data(), bytes.size()}));
          brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode);
          return RunResult::Success;
        });
  }

  if (opt.pit_get_out) {
    if (targets.size() != 1) {
      std::cerr << "PIT get requires exactly one device (use --target).\n";
      return RunResult::InvalidUsage;
    }
    return usb_one(
        targets.front(), [&](brokkr::platform::UsbFsConnection &conn) {
          const auto bytes =
              with_odin(conn, cfg, [&](brokkr::odin::OdinCommands &odin) {
                return brokkr::odin::download_pit_bytes(odin,
                                                        cfg.preflash_retries);
              });
          RunResult ret;
          if (!File::write_all(*opt.pit_get_out, bytes)) {
            spdlog::error("Failed to save PIT to {}",
                          opt.pit_get_out->string());
            ret = RunResult::kIOFail;
          } else {
            spdlog::info("Saved PIT to {}", opt.pit_get_out->string());
            ret = RunResult::Success;
          }
          brokkr::odin::OdinCommands(conn).shutdown(pit_shutdown_mode);
          return ret;
        });
  }

  std::shared_ptr<const File::ByteArray> pit_to_upload;

  if (opt.pit_set_in) {
    auto file = File::read_all(*opt.pit_set_in);
    if (!file) {
      spdlog::error("Failed to read PIT file: {}", opt.pit_set_in->string());
      return RunResult::kIOFail;
    }
    pit_to_upload = std::make_shared<File::ByteArray>(*file);
  }

  if (!has_flash_files(opt) && !opt.reboot_only && !opt.pit_set_in) {
    std::cerr << usage_text();
    return RunResult::InvalidUsage;
  }

  std::vector<std::unique_ptr<brokkr::odin::UsbTarget>> storage;
  storage.reserve(targets.size());
  std::vector<brokkr::odin::UsbTarget *> devs;
  devs.reserve(targets.size());

  for (const auto &t : targets) {
    auto ctx = std::make_unique<brokkr::odin::UsbTarget>(t.devnode());
    devs.push_back(ctx.get());
    storage.push_back(std::move(ctx));
  }

  FlashInterface ui(!opt.gui_mode, opt.gui_mode);

  auto shield =
      brokkr::core::SignalShield::enable([&](const char *sig_desc, int count) {
        ui.notice(fmt::format("{} ignored ({} times) - do not disconnect",
                              sig_desc, count));
      });

  brokkr::odin::Ui hooks;
  hooks.on_devices = [&](std::size_t n, const std::vector<std::string> &ids) {
    ui.devices(n, ids);
  };
  hooks.on_model = [&](const std::string &m) { ui.cpu_bl_id(m); };
  hooks.on_stage = [&](const std::string &s) { ui.stage(s); };
  hooks.on_plan = [&](const std::vector<brokkr::odin::PlanItem> &p,
                      std::uint64_t t) { ui.plan(p, t); };
  hooks.on_item_active = [&](std::size_t i) { ui.active(i); };
  hooks.on_item_done = [&](std::size_t i) { ui.done_item(i); };
  hooks.on_progress = [&](std::uint64_t od, std::uint64_t ot, std::uint64_t id,
                          std::uint64_t it) { ui.progress(od, ot, id, it); };
  hooks.on_error = [&](const std::string &msg) { ui.fail(msg); };
  hooks.on_done = [&] { ui.done("DONE"); };

  try {
    if (opt.reboot_only) {
      brokkr::odin::flash(devs, {}, {}, cfg, hooks,
                          brokkr::odin::Mode::RebootOnly);
      return RunResult::Success;
    }
    if (opt.pit_set_in && !has_flash_files(opt)) {
      brokkr::odin::flash(devs, {}, pit_to_upload, cfg, hooks,
                          brokkr::odin::Mode::PitSetOnly);
      return RunResult::Success;
    }

    const auto inputs = build_flash_inputs(opt);
    md5_verify(md5_jobs(inputs), ui);

    auto specs = brokkr::odin::expand_inputs_tar_or_raw(inputs);
    const bool dl_mode =
        std::ranges::any_of(specs, [](const brokkr::odin::ImageSpec &s) {
          return s.download_list_mode;
        });

    if (!pit_to_upload && !dl_mode) {
      pit_to_upload = pit_from_specs(specs);
    }

    std::vector<brokkr::odin::ImageSpec> srcs;
    srcs.reserve(specs.size());
    for (auto &s : specs)
      if (!is_pit_name(s.basename))
        srcs.push_back(std::move(s));
    if (srcs.empty()) {
      ui.fail("No valid flashable files");
      spdlog::error("No valid flashable files");
      return RunResult::NoFlashFiles;
    }

    brokkr::odin::flash(devs, srcs, pit_to_upload, cfg, hooks,
                        brokkr::odin::Mode::Flash);
    return RunResult::Success;

  } catch (const std::exception &e) {
    ui.fail(fmt::format("Error: {}", e.what()));
    spdlog::error("Exception in run: {}", e.what());
    return RunResult::Unknown;
  }
}

} // namespace brokkr::app
