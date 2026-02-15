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

#include <stdexcept>
#include <string_view>

namespace brokkr::app {

static bool is_opt(std::string_view a, std::string_view opt) {
    return a == opt || (a.size() > opt.size() + 1 && a.starts_with(opt) && a[opt.size()] == '=');
}

static std::optional<std::string_view> opt_value(std::string_view a, std::string_view opt) {
    if (a == opt) return std::nullopt;
    if (a.starts_with(opt) && a.size() > opt.size() + 1 && a[opt.size()] == '=') {
        return a.substr(opt.size() + 1);
    }
    return std::nullopt;
}

static std::filesystem::path read_path_value(int& i, int argc, char** argv, std::string_view a, std::string_view opt) {
    std::string_view v;
    if (auto ov = opt_value(a, opt)) v = *ov;
    else {
        if (i + 1 >= argc) throw std::runtime_error(std::string(opt) + " requires value");
        v = argv[++i];
    }
    return std::filesystem::path(std::string(v));
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
  brokkr --print-connected
  brokkr --reboot
  brokkr --redownload [--set-pit/--get-pit/-a/-b/-c/-s/-u ...]
  brokkr --no-reboot

Options:
  --help
  --version
  --print-connected
  --print-pit [<in.pit>]       if no file is provided, downloads PIT from device (single device only)
  -w, --wireless               wireless (Galaxy Watch).
  --target <sysname>           e.g. 1-1.4
  --get-pit <out.pit>          download PIT and save to file (single device only)
  --set-pit <in.pit>           select pit for mapping (if flashing) or upload pit. (multi-device)
  --reboot                     reboot all selected devices without flashing. Must be used alone.
  --redownload                 after operation, try to reboot back into Download Mode (Might not work with all devices)
  --no-reboot                  do not reboot after flashing (incompatible with --redownload)

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

Options parse_cli(int argc, char** argv) {
    Options o;

    auto any_flash_file = [&] {
        return o.file_a || o.file_b || o.file_c || o.file_s || o.file_u;
    };

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];

        if (a == "--help" || a == "-h") { o.help = true; continue; }
        if (a == "--version") { o.version = true; continue; }
        if (a == "--print-connected") { o.print_connected = true; continue; }

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

        if (a == "--target" || is_opt(a, "--target")) {
            std::string_view v;
            if (auto ov = opt_value(a, "--target")) v = *ov;
            else {
                if (i + 1 >= argc) throw std::runtime_error("--target requires value");
                v = argv[++i];
            }
            o.target_sysname = std::string(v);
            continue;
        }

        if (a == "--get-pit" || is_opt(a, "--get-pit")) {
            o.pit_get_out = read_path_value(i, argc, argv, a, "--get-pit");
            continue;
        }
        if (a == "--set-pit" || is_opt(a, "--set-pit")) {
            o.pit_set_in = read_path_value(i, argc, argv, a, "--set-pit");
            continue;
        }

        if (a == "--get" || is_opt(a, "--get")) {
            o.pit_get_out = read_path_value(i, argc, argv, a, "--get");
            continue;
        }
        if (a == "--set" || is_opt(a, "--set")) {
            o.pit_set_in = read_path_value(i, argc, argv, a, "--set");
            continue;
        }

        if (a == "-a" || is_opt(a, "-a")) { o.file_a = read_path_value(i, argc, argv, a, "-a"); continue; }
        if (a == "-b" || is_opt(a, "-b")) { o.file_b = read_path_value(i, argc, argv, a, "-b"); continue; }
        if (a == "-c" || is_opt(a, "-c")) { o.file_c = read_path_value(i, argc, argv, a, "-c"); continue; }
        if (a == "-s" || is_opt(a, "-s")) { o.file_s = read_path_value(i, argc, argv, a, "-s"); continue; }
        if (a == "-u" || is_opt(a, "-u")) { o.file_u = read_path_value(i, argc, argv, a, "-u"); continue; }

        if (a.starts_with("-")) {
            throw std::runtime_error("Unknown option: " + std::string(a));
        }

        throw std::runtime_error("Positional inputs are not supported. Use -a/-b/-c/-s/-u.");
    }

    if (o.wireless) {
        if (o.target_sysname) throw std::runtime_error("--wireless cannot be used with --target");
        if (o.print_connected) throw std::runtime_error("--wireless cannot be used with --print-connected");

        const bool has_wireless_op =
            o.reboot_only ||
            o.pit_get_out.has_value() ||
            o.pit_set_in.has_value() ||
            any_flash_file();

        if (!has_wireless_op) {
            throw std::runtime_error(
                "--wireless requires either --reboot, --get/--get-pit, --set/--set-pit, or flash inputs (-a/-b/-c/-s/-u)"
            );
        }
    }

    if (o.print_pit) {
        const bool has_other_ops =
            o.pit_get_out.has_value() ||
            o.pit_set_in.has_value() ||
            any_flash_file() ||
            o.reboot_only;

        if (has_other_ops) {
            throw std::runtime_error("--print-pit must be used alone (it cannot be combined with flashing, --get/--set, or --reboot)");
        }
    }

    if (o.pit_get_out && o.pit_set_in) {
        throw std::runtime_error("Cannot use --get-pit and --set-pit together");
    }
    if (o.pit_get_out && any_flash_file()) {
        throw std::runtime_error("--get-pit does not accept flash inputs");
    }

    if (o.reboot_only && !o.reboot_after_flash) {
        throw std::runtime_error("--reboot cannot be used with --no-reboot");
    }

    const bool has_other_ops = (o.pit_get_out.has_value() || o.pit_set_in.has_value() || any_flash_file());
    if (o.reboot_only && has_other_ops) {
        o.reboot_only = false;
    }

    if (o.redownload && !o.reboot_after_flash) {
        throw std::runtime_error("--redownload cannot be used with --no-reboot");
    }
    if (o.redownload && o.reboot_only) {
        throw std::runtime_error("--redownload cannot be used with --reboot");
    }
    if (o.redownload) {
        const bool allowed_context =
            o.pit_get_out.has_value() ||
            o.pit_set_in.has_value() ||
            any_flash_file() ||
            o.print_pit;
        if (!allowed_context) {
            throw std::runtime_error("--redownload cannot be used alone");
        }
    }

    return o;
}

} // namespace brokkr::app
