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

#include "app/cli.hpp"
#include "app/version.hpp"

#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string_view>

namespace brokkr::app {

static bool is_opt(std::string_view a, std::string_view opt) {
  return a == opt || (a.size() > opt.size() + 1 && a.starts_with(opt) && a[opt.size()] == '=');
}

static std::optional<std::string_view> opt_value(std::string_view a, std::string_view opt) {
  if (a == opt) return std::nullopt;
  if (a.starts_with(opt) && a.size() > opt.size() + 1 && a[opt.size()] == '=') return a.substr(opt.size() + 1);
  return std::nullopt;
}

static brokkr::core::Result<std::string_view> read_string_value(int& i, int argc, char** argv,
                                                                std::string_view a, std::string_view opt) noexcept
{
  if (auto ov = opt_value(a, opt)) return brokkr::core::Result<std::string_view>::Ok(*ov);
  if (i + 1 >= argc) return brokkr::core::Result<std::string_view>::Fail(std::string(opt) + " requires value");
  return brokkr::core::Result<std::string_view>::Ok(std::string_view(argv[++i]));
}

static brokkr::core::Result<std::filesystem::path> read_path_value(int& i, int argc, char** argv,
                                                                   std::string_view a, std::string_view opt) noexcept
{
  auto vr = read_string_value(i, argc, argv, a, opt);
  if (!vr) return brokkr::core::Result<std::filesystem::path>::Fail(std::move(vr.st.msg));
  return brokkr::core::Result<std::filesystem::path>::Ok(std::filesystem::path(std::string(vr.value)));
}

std::string usage_text() {
  std::string out;
  out.reserve(2048);

  out += "Brokkr Flash v";
  out += brokkr::app::version_string();
  out += "\n\n";

  out += R"(Usage:
  brokkr (-a <file> | -b <file> | -c <file> | -s <file> | -u <file>) [...]
  brokkr -w (-a/-b/-c/-s/-u ...)
  brokkr --target <sysname> (-a/-b/-c/-s/-u ...)
  brokkr --get-pit <out.pit>
  brokkr --set-pit <in.pit> (-a/-b/-c/-s/-u ...)
  brokkr --print-pit [<in.pit>]
  brokkr --print-connected(-only)
  brokkr --reboot
  brokkr --redownload [--set-pit/--get-pit/-a/-b/-c/-s/-u ...]
  brokkr --no-reboot

Options:
  --help
  --version
  --print-connected(-only)     print connected devices and exit. If --print-connected-only is used, only print sysnames, one per line, with no other output.
  --print-pit [<in.pit>]       if no file is provided, downloads PIT from device (single device only)
  -w, --wireless               wireless (Galaxy Watch).
  --target <sysname>           e.g. 1-1.4
  --get-pit <out.pit>          download PIT and save to file (single device only)
  --set-pit <in.pit>           select pit for mapping (if flashing) or upload pit. (multi-device)
  --reboot                     reboot all selected devices without flashing. Must be used alone.
  --redownload                 after operation, try to reboot back into Download Mode (Might not work with all devices)
  --no-reboot                  do not reboot after flashing (incompatible with --redownload)
  --verbose, -v                enable verbose logging
  --gui-mode                   enable GUI mode. This is mostly for brokkr-gui, and it changes some output formats to be more machine-friendly. It does not enable the GUI by itself.

Flash inputs:
  -a <AP file>
  -b <BL file>
  -c <CP file>
  -s <CSC file>
  -u <USERDATA file>

Compatibility aliases:
  --get                        alias of --get-pit
  --set                        alias of --set-pit
)";
  return out;
}

brokkr::core::Result<Options> parse_cli(int argc, char** argv) noexcept {
  Options o;

  auto any_flash_file = [&] {
    return o.file_a || o.file_b || o.file_c || o.file_s || o.file_u;
  };

  for (int i = 1; i < argc; ++i) {
    std::string_view a = argv[i];

    if (a == "--help" || a == "-h") { o.help = true; continue; }
    if (a == "--version") { o.version = true; continue; }

    if (a == "--print-connected") {
      o.print_connected = true;
      spdlog::set_level(spdlog::level::off);
      continue;
    }
    if (a == "--print-connected-only") {
      o.print_connected_only = true;
      spdlog::set_level(spdlog::level::off);
      continue;
    }
    if (a == "--gui-mode") { o.gui_mode = true; continue; }

    if (a == "--print-pit" || is_opt(a, "--print-pit")) {
      o.print_pit = true;

      if (auto ov = opt_value(a, "--print-pit")) {
        o.pit_print_in = std::filesystem::path(std::string(*ov));
      } else {
        if (i + 1 < argc) {
          std::string_view nxt = argv[i + 1];
          if (!nxt.empty() && !nxt.starts_with("-")) {
            o.pit_print_in = std::filesystem::path(std::string(nxt));
            ++i;
          }
        }
      }
      continue;
    }

    if (a == "--wireless" || a == "-w") { o.wireless = true; continue; }

    if (a == "--reboot") { o.reboot_only = true; continue; }
    if (a == "--redownload") { o.redownload = true; continue; }

    if (a == "--no-reboot") { o.reboot_after_flash = false; continue; }

    if (a == "--verbose" || a == "-v") {
      if (!o.print_connected && !o.print_connected_only) spdlog::set_level(spdlog::level::debug);
      continue;
    }

    if (a == "--target" || is_opt(a, "--target")) {
      auto vr = read_string_value(i, argc, argv, a, "--target");
      if (!vr) return brokkr::core::Result<Options>::Fail(std::move(vr.st.msg));
      o.target_sysname = std::string(vr.value);
      continue;
    }

    if (a == "--get-pit" || is_opt(a, "--get-pit")) {
      auto pr = read_path_value(i, argc, argv, a, "--get-pit");
      if (!pr) return brokkr::core::Result<Options>::Fail(std::move(pr.st.msg));
      o.pit_get_out = std::move(pr.value);
      continue;
    }
    if (a == "--set-pit" || is_opt(a, "--set-pit")) {
      auto pr = read_path_value(i, argc, argv, a, "--set-pit");
      if (!pr) return brokkr::core::Result<Options>::Fail(std::move(pr.st.msg));
      o.pit_set_in = std::move(pr.value);
      continue;
    }

    if (a == "--get" || is_opt(a, "--get")) {
      auto pr = read_path_value(i, argc, argv, a, "--get");
      if (!pr) return brokkr::core::Result<Options>::Fail(std::move(pr.st.msg));
      o.pit_get_out = std::move(pr.value);
      continue;
    }
    if (a == "--set" || is_opt(a, "--set")) {
      auto pr = read_path_value(i, argc, argv, a, "--set");
      if (!pr) return brokkr::core::Result<Options>::Fail(std::move(pr.st.msg));
      o.pit_set_in = std::move(pr.value);
      continue;
    }

    if (a == "-a" || is_opt(a, "-a")) { auto pr = read_path_value(i, argc, argv, a, "-a"); if (!pr) return brokkr::core::Result<Options>::Fail(std::move(pr.st.msg)); o.file_a = std::move(pr.value); continue; }
    if (a == "-b" || is_opt(a, "-b")) { auto pr = read_path_value(i, argc, argv, a, "-b"); if (!pr) return brokkr::core::Result<Options>::Fail(std::move(pr.st.msg)); o.file_b = std::move(pr.value); continue; }
    if (a == "-c" || is_opt(a, "-c")) { auto pr = read_path_value(i, argc, argv, a, "-c"); if (!pr) return brokkr::core::Result<Options>::Fail(std::move(pr.st.msg)); o.file_c = std::move(pr.value); continue; }
    if (a == "-s" || is_opt(a, "-s")) { auto pr = read_path_value(i, argc, argv, a, "-s"); if (!pr) return brokkr::core::Result<Options>::Fail(std::move(pr.st.msg)); o.file_s = std::move(pr.value); continue; }
    if (a == "-u" || is_opt(a, "-u")) { auto pr = read_path_value(i, argc, argv, a, "-u"); if (!pr) return brokkr::core::Result<Options>::Fail(std::move(pr.st.msg)); o.file_u = std::move(pr.value); continue; }

    if (a.starts_with("-")) {
      return brokkr::core::Result<Options>::Fail("Unknown option: " + std::string(a));
    }

    return brokkr::core::Result<Options>::Fail("Positional arguments are not supported: " + std::string(a));
  }

  if (o.wireless) {
    if (o.target_sysname) return brokkr::core::Result<Options>::Fail("--wireless cannot be used with --target");
    if (o.print_connected || o.print_connected_only) return brokkr::core::Result<Options>::Fail("--wireless cannot be used with --print-connected(-only)");

    const bool has_wireless_op = o.reboot_only || o.pit_get_out.has_value() ||
                                 o.pit_set_in.has_value() || any_flash_file();
    if (!has_wireless_op) {
      return brokkr::core::Result<Options>::Fail("--wireless requires either --reboot, --get/--get-pit, --set/--set-pit, or flash inputs (-a/-b/-c/-s/-u)");
    }
  }

  if (o.print_pit) {
    const bool has_other_ops =
      o.pit_get_out.has_value() || o.pit_set_in.has_value() || any_flash_file() || o.reboot_only;

    if (has_other_ops) return brokkr::core::Result<Options>::Fail("--print-pit cannot be combined with flashing, --get/--set, or --reboot");
  }

  if (o.pit_get_out && o.pit_set_in) return brokkr::core::Result<Options>::Fail("Cannot use --get-pit and --set-pit together");
  if (o.pit_get_out && any_flash_file()) return brokkr::core::Result<Options>::Fail("--get-pit cannot be combined with flash inputs");

  if (o.reboot_only && !o.reboot_after_flash) return brokkr::core::Result<Options>::Fail("--reboot cannot be used with --no-reboot");

  const bool has_other_ops = (o.pit_get_out.has_value() || o.pit_set_in.has_value() || any_flash_file());
  if (o.reboot_only && has_other_ops) o.reboot_only = false;

  if (o.redownload && !o.reboot_after_flash) return brokkr::core::Result<Options>::Fail("--redownload cannot be used with --no-reboot");
  if (o.redownload && o.reboot_only) return brokkr::core::Result<Options>::Fail("--redownload cannot be used with --reboot");

  if (o.redownload) {
    const bool allowed_context =
      o.pit_get_out.has_value() || o.pit_set_in.has_value() || any_flash_file() || o.print_pit;
    if (!allowed_context) {
      return brokkr::core::Result<Options>::Fail("--redownload must be used with some other operation (e.g. flashing, --get/--set, or --print-pit)");
    }
  }

  if (argc == 1 || (argc == 2 && o.gui_mode)) o._no_args = true;
  return brokkr::core::Result<Options>::Ok(std::move(o));
}

} // namespace brokkr::app
