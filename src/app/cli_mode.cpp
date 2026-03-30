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

#include "app/cli_mode.hpp"

#include "app/md5_verify.hpp"
#include "core/status.hpp"
#include "platform/platform_all.hpp"
#include "protocol/odin/flash.hpp"
#include "protocol/odin/group_flasher.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace brokkr::app {

namespace {

constexpr std::uint16_t kSamsungVid = 0x04E8;
constexpr std::uint16_t kOdinPids[] = {0x6601, 0x685D, 0x68C3};
constexpr std::uint16_t kWirelessPort = 13579;

struct CliArgs {
  bool help = false;
  bool list = false;
  bool wireless = false;
  bool no_reboot = false;

  std::optional<std::string> target;
  std::optional<std::string> pit;

  std::optional<std::string> bl;
  std::optional<std::string> ap;
  std::optional<std::string> cp;
  std::optional<std::string> csc;
  std::optional<std::string> userdata;
};

struct Provider {
  std::vector<std::unique_ptr<brokkr::odin::UsbTarget>> usb;
  std::vector<brokkr::odin::Target> owned;
  std::vector<brokkr::odin::Target*> ptrs;
  std::optional<brokkr::platform::TcpConnection> wireless_conn;
};

bool is_odin_product(std::uint16_t pid) {
  return std::find(std::begin(kOdinPids), std::end(kOdinPids), pid) != std::end(kOdinPids);
}

bool is_cli_trigger(std::string_view arg) {
  static const std::unordered_set<std::string_view> kTriggers = {
      "-h", "--help", "--list", "--wireless", "--no-reboot", "--use-pit", "--target",
      "-b", "-a", "-c", "-s", "-u",
  };
  return kTriggers.contains(arg);
}

void configure_cli_logger() {
  auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  sink->set_pattern("%v");
  auto logger = std::make_shared<spdlog::logger>("cli", spdlog::sinks_init_list{sink});
#ifndef NDEBUG
  logger->set_level(spdlog::level::debug);
#else
  logger->set_level(spdlog::level::info);
#endif
  spdlog::set_default_logger(std::move(logger));
}

void print_usage() {
  std::cout
      << "Usage:\n"
      << "  brokkr [CLI options]\n\n"
      << "CLI options (any of these switches CLI mode):\n"
      << "  -h, --help                 Show this help\n"
      << "  --list                     List Samsung devices usable by --target\n"
      << "  -b <path.tar[.md5]>        BL file\n"
      << "  -a <path.tar[.md5]>        AP file\n"
      << "  -c <path.tar[.md5]>        CP file\n"
      << "  -s <path.tar[.md5]>        CSC file\n"
      << "  -u <path.tar[.md5]>        USERDATA file\n"
      << "  --use-pit <path.pit>       Optional PIT file\n"
      << "  --no-reboot                Do not reboot at end\n"
      << "  --wireless                 Flash via wireless listener\n"
      << "  --target <sysname>         Same target semantics as GUI\n\n"
      << "Notes:\n"
      << "  - At least one file is required from: -b -a -c -s -u --use-pit\n"
      << "  - --wireless cannot be used with --target\n"
      << "  - If no valid CLI option is present, GUI mode is launched\n";
}

brokkr::core::Result<CliArgs> parse_cli_args(int argc, char* argv[]) {
  CliArgs out;

  auto require_value = [&](int& i, const char* flag) -> brokkr::core::Result<std::string> {
    if (i + 1 >= argc) return brokkr::core::fail(std::string("Missing value for ") + flag);
    ++i;
    return std::string(argv[i]);
  };

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];

    if (arg == "-h" || arg == "--help") {
      out.help = true;
      continue;
    }
    if (arg == "--list") {
      out.list = true;
      continue;
    }
    if (arg == "--wireless") {
      out.wireless = true;
      continue;
    }
    if (arg == "--no-reboot") {
      out.no_reboot = true;
      continue;
    }
    if (arg == "--target") {
      BRK_TRYV(v, require_value(i, "--target"));
      out.target = std::move(v);
      continue;
    }
    if (arg == "--use-pit") {
      BRK_TRYV(v, require_value(i, "--use-pit"));
      out.pit = std::move(v);
      continue;
    }
    if (arg == "-b") {
      BRK_TRYV(v, require_value(i, "-b"));
      out.bl = std::move(v);
      continue;
    }
    if (arg == "-a") {
      BRK_TRYV(v, require_value(i, "-a"));
      out.ap = std::move(v);
      continue;
    }
    if (arg == "-c") {
      BRK_TRYV(v, require_value(i, "-c"));
      out.cp = std::move(v);
      continue;
    }
    if (arg == "-s") {
      BRK_TRYV(v, require_value(i, "-s"));
      out.csc = std::move(v);
      continue;
    }
    if (arg == "-u") {
      BRK_TRYV(v, require_value(i, "-u"));
      out.userdata = std::move(v);
      continue;
    }

    return brokkr::core::fail("Unknown argument: " + arg);
  }

  return out;
}

std::vector<brokkr::platform::UsbDeviceSysfsInfo> enumerate_samsung_targets() {
  brokkr::platform::EnumerateFilter f{.vendor = kSamsungVid};
  return brokkr::platform::enumerate_usb_devices_sysfs(f);
}

std::optional<brokkr::platform::UsbDeviceSysfsInfo> select_samsung_target(std::string_view sysname) {
  if (sysname.empty()) return std::nullopt;
  auto info = brokkr::platform::find_by_sysname(sysname);
  if (!info || info->vendor != kSamsungVid) return std::nullopt;
  return info;
}

std::optional<brokkr::platform::UsbDeviceSysfsInfo> select_odin_target(std::string_view sysname) {
  auto info = select_samsung_target(sysname);
  if (!info || !is_odin_product(info->product)) return std::nullopt;
  return info;
}

std::vector<std::filesystem::path> collect_inputs_in_gui_order(const CliArgs& args) {
  std::vector<std::filesystem::path> out;
  if (args.bl) out.emplace_back(*args.bl);
  if (args.ap) out.emplace_back(*args.ap);
  if (args.cp) out.emplace_back(*args.cp);
  if (args.csc) out.emplace_back(*args.csc);
  if (args.userdata) out.emplace_back(*args.userdata);
  return out;
}

bool has_any_file_selected(const CliArgs& args) {
  return args.bl.has_value() || args.ap.has_value() || args.cp.has_value() || args.csc.has_value() ||
         args.userdata.has_value() || args.pit.has_value();
}

std::shared_ptr<const std::vector<std::byte>> load_pit_if_needed(const CliArgs& args) {
  if (!args.pit) return {};

  const std::filesystem::path p = *args.pit;
  std::error_code ec;
  const auto sz = std::filesystem::file_size(p, ec);
  if (ec) {
    spdlog::error("Cannot stat PIT file.");
    return {};
  }

  std::vector<std::byte> buf(static_cast<std::size_t>(sz));
  std::ifstream in(p, std::ios::binary);
  if (!in.is_open()) {
    spdlog::error("Cannot open PIT file.");
    return {};
  }

  if (!buf.empty()) {
    in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    if (!in.good()) {
      spdlog::error("Failed to read PIT file.");
      return {};
    }
  }

  return std::make_shared<const std::vector<std::byte>>(std::move(buf));
}

std::string map_global_error_to_cli_message(const std::string& err) {
  const auto has_ic = [&](std::string_view needle) {
    auto it = std::search(err.begin(), err.end(), needle.begin(), needle.end(), [](char a, char b) {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(a))) ==
             static_cast<char>(std::tolower(static_cast<unsigned char>(b)));
    });
    return it != err.end();
  };

  if (has_ic("mismatch across devices") || has_ic("differs across devices")) {
    return "The connected devices do not match!";
  }

  return err;
}

int list_devices_cli() {
  const auto devs = enumerate_samsung_targets();
  if (devs.empty()) {
    std::cout << "No Samsung devices found.\n";
    return 0;
  }

  for (const auto& d : devs) {
    std::cout << d.sysname << "\t" << (is_odin_product(d.product) ? "Odin Mode" : "Not in Odin Mode") << "\n";
  }

  return 0;
}

brokkr::core::Result<Provider> make_provider(const CliArgs& args) {
  Provider p;

  if (args.wireless) {
    brokkr::platform::TcpListener listener;
    BRK_TRY(listener.bind_and_listen("0.0.0.0", kWirelessPort));

    spdlog::info("Waiting for connection on port {}", kWirelessPort);

    for (;;) {
      auto ar = listener.accept_one();
      if (ar) {
        p.wireless_conn.emplace(std::move(*ar));
        break;
      }
      if (ar.error() != "accept: timeout") return brokkr::core::fail(std::move(ar.error()));
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    p.owned.push_back(brokkr::odin::Target{.id = "wireless", .link = &*p.wireless_conn});
    p.ptrs.push_back(&p.owned.back());
    return p;
  }

  std::vector<brokkr::platform::UsbDeviceSysfsInfo> targets;
  if (args.target && !args.target->empty()) {
    auto info = select_samsung_target(*args.target);
    if (!info) return brokkr::core::fail("Target sysname not found.");
    if (!is_odin_product(info->product)) {
      return brokkr::core::fail("The device is not in Odin Mode, reboot to download mode first.");
    }
    targets.push_back(*info);
  } else {
    const auto all_samsung = enumerate_samsung_targets();
    if (all_samsung.empty()) return brokkr::core::fail("No connected devices detected.");

    for (const auto& d : all_samsung) {
      if (is_odin_product(d.product)) {
        targets.push_back(d);
      } else {
        spdlog::info("{} is not in Odin Mode hence ignored", d.sysname);
      }
    }

    if (targets.empty()) return brokkr::core::fail("None of the devices are in Odin Mode");
  }

  p.usb.reserve(targets.size());
  p.owned.reserve(targets.size());
  p.ptrs.reserve(targets.size());

  for (const auto& td : targets) {
    auto ut = std::make_unique<brokkr::odin::UsbTarget>(td.devnode());

    auto st = ut->dev.open_and_init();
    if (!st) return brokkr::core::fail(std::move(st.error()));

    auto cst = ut->conn.open();
    if (!cst) return brokkr::core::fail(std::move(cst.error()));

    p.owned.push_back(brokkr::odin::Target{.id = ut->devnode, .link = &ut->conn});
    p.ptrs.push_back(&p.owned.back());
    p.usb.push_back(std::move(ut));
  }

  return p;
}

int run_flash_cli(const CliArgs& args) {
  if (!has_any_file_selected(args)) {
    spdlog::error("No files selected.");
    return 2;
  }

  if (args.wireless && args.target && !args.target->empty()) {
    spdlog::error("Wireless cannot be used together with Target Sysname.");
    return 2;
  }

  auto pit_to_upload = load_pit_if_needed(args);
  if (args.pit && !pit_to_upload) return 1;

  auto inputs = collect_inputs_in_gui_order(args);

  brokkr::odin::Cfg cfg;
  cfg.reboot_after = !args.no_reboot;

  std::atomic_bool saw_per_device_fail{false};
  std::optional<brokkr::core::SignalShield> sig_guard;
  bool flash_signal_shield_attempted = false;
  brokkr::odin::Ui ui;
  ui.on_stage = [&](const std::string& s) {
    if (flash_signal_shield_attempted) return;
    if (s.rfind("Flashing", 0) != 0) return;

    flash_signal_shield_attempted = true;
    sig_guard = brokkr::core::SignalShield::enable([](const char* sig_desc, int count) {
      spdlog::warn("{} received during active flash operation - ignoring ({})", sig_desc, count);
    });
    if (!sig_guard) {
      spdlog::warn("Failed to enable signal shielding; interrupts may terminate this flash run.");
    }
  };
  ui.on_progress = [](std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) {
    // CLI intentionally suppresses percentage/progress bars.
  };
  ui.on_error = [&](const std::string& s) {
    if (s.rfind("DEVFAIL idx=", 0) == 0) saw_per_device_fail.store(true, std::memory_order_relaxed);
    spdlog::error("{}", s);
  };

  auto provider_r = make_provider(args);
  if (!provider_r) {
    spdlog::error("{}", map_global_error_to_cli_message(provider_r.error()));
    return 1;
  }
  auto provider = std::move(*provider_r);

  std::vector<brokkr::odin::ImageSpec> specs;
  if (!inputs.empty()) {
    auto jobsr = brokkr::app::md5_jobs(inputs);
    if (!jobsr) {
      spdlog::error("{}", jobsr.error());
      return 1;
    }

    for (const auto& job : *jobsr) {
      std::string name = job.path.filename().string();
      if (name.empty()) name = job.path.string();

      if (name.size() >= 11)
        name = name.substr(0, 10) + "...";

      spdlog::info("Checking MD5/XXH3 on {}", name);
    }

    auto vst = brokkr::app::md5_verify(*jobsr, ui);
    if (!vst) {
      spdlog::error("{}", vst.error());
      return 1;
    }

    auto specsr = brokkr::odin::expand_inputs_tar_or_raw(inputs);
    if (!specsr) {
      spdlog::error("{}", specsr.error());
      return 1;
    }
    specs = std::move(*specsr);
  }

  auto fst = brokkr::odin::flash(provider.ptrs, specs, pit_to_upload, cfg, ui);
  if (!fst) {
    if (!saw_per_device_fail.load(std::memory_order_relaxed)) {
      spdlog::error("{}", map_global_error_to_cli_message(fst.error()));
    }
    return 1;
  }

  return 0;
}

} // namespace

bool should_run_cli(int argc, char* argv[]) noexcept {
  for (int i = 1; i < argc; ++i) {
    if (is_cli_trigger(argv[i])) return true;
  }
  return false;
}

int run_cli(int argc, char* argv[]) {
  configure_cli_logger();

  auto lock = brokkr::platform::SingleInstanceLock::try_acquire("brokkr-engine");
  if (!lock) {
    spdlog::error("Another instance is already running.");
    return 2;
  }

  auto argsr = parse_cli_args(argc, argv);
  if (!argsr) {
    spdlog::error("{}", argsr.error());
    print_usage();
    return 2;
  }
  const CliArgs args = std::move(*argsr);

  if (args.help) {
    print_usage();
    return 0;
  }

  if (args.list) return list_devices_cli();

  return run_flash_cli(args);
}

} // namespace brokkr::app
