#include "app/cli_args.hpp"
#include "core/path_utf8.hpp"

#include <exception>
#include <memory>
#include <utility>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <shellapi.h>
#endif

namespace brokkr::app {

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
    if (arg == "--reboot-download") {
      out.reboot_download = true;
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

std::vector<std::filesystem::path> collect_inputs_in_gui_order(const CliArgs& args) {
  std::vector<std::filesystem::path> out;
  if (args.bl) out.emplace_back(brokkr::core::path_from_utf8(*args.bl));
  if (args.ap) out.emplace_back(brokkr::core::path_from_utf8(*args.ap));
  if (args.cp) out.emplace_back(brokkr::core::path_from_utf8(*args.cp));
  if (args.csc) out.emplace_back(brokkr::core::path_from_utf8(*args.csc));
  if (args.userdata) out.emplace_back(brokkr::core::path_from_utf8(*args.userdata));
  return out;
}

brokkr::core::Result<CliArgs> parse_process_cli_args(int argc, char* argv[]) {
#if defined(_WIN32)
  (void)argc;
  (void)argv;
  int wide_argc = 0;
  auto free_args = [](wchar_t** p) { ::LocalFree(p); };
  const std::unique_ptr<wchar_t*, decltype(free_args)> wide_argv(
      ::CommandLineToArgvW(::GetCommandLineW(), &wide_argc), free_args);
  if (!wide_argv) return brokkr::core::fail("Cannot read the Windows command line.");

  try {
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(wide_argc));
    for (int i = 0; i < wide_argc; ++i)
      storage.push_back(brokkr::core::path_to_utf8(std::filesystem::path(wide_argv.get()[i])));

    std::vector<char*> pointers;
    pointers.reserve(storage.size() + 1);
    for (auto& arg : storage) pointers.push_back(arg.data());
    pointers.push_back(nullptr);
    return parse_cli_args(wide_argc, pointers.data());
  } catch (const std::exception& e) {
    return brokkr::core::failf("Cannot decode the Windows command line: {}", e.what());
  }
#else
  return parse_cli_args(argc, argv);
#endif
}

}
