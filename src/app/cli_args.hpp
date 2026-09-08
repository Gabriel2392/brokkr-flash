#pragma once

#include "core/status.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace brokkr::app {

struct CliArgs {
  bool help = false;
  bool list = false;
  bool reboot_download = false;
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


brokkr::core::Result<CliArgs> parse_cli_args(int argc, char* argv[]);

brokkr::core::Result<CliArgs> parse_process_cli_args(int argc, char* argv[]);
std::vector<std::filesystem::path> collect_inputs_in_gui_order(const CliArgs& args);

}
