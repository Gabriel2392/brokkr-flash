#include "app/cli_args.hpp"
#include "app/pit_file.hpp"
#include "core/path_utf8.hpp"
#include "path_qt.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QUrl>

#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

namespace {
int checks = 0;
constexpr std::string_view payload = "desktop path fixture";

void check(bool ok, const std::string& message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}

void check_contents(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(file)), {});
  check(bytes == payload, "cannot read selected file: " + brokkr::core::path_to_utf8(path));
}


int probe_cli(int argc, char** argv) {
  const auto args = brokkr::app::parse_process_cli_args(argc, argv);
  check(static_cast<bool>(args), args ? "" : args.error());
  const auto inputs = brokkr::app::collect_inputs_in_gui_order(*args);
  check(inputs.size() == 5 && args->pit.has_value(), "all five slots and PIT must be present");
  for (const auto& input : inputs) {
    check_contents(input);
    std::cout << brokkr::core::path_to_utf8(input) << '\n';
  }
  const auto pit_path = brokkr::core::path_from_utf8(*args->pit);
  const auto pit = brokkr::app::read_pit_file(pit_path);
  check(pit && pit->size() == payload.size() && std::memcmp(pit->data(), payload.data(), payload.size()) == 0,
        "PIT read failed");
  std::cout << brokkr::core::path_to_utf8(pit_path) << '\n';
  return 0;
}

QByteArray run(const QString& program, const QStringList& args, QProcessEnvironment env, int expected_exit = 0) {
  QProcess child;
  child.setProcessEnvironment(env);
#if defined(_WIN32)

  child.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* process_args) {
    process_args->flags |= CREATE_NO_WINDOW;
  });
#endif
  child.start(program, args);
  check(child.waitForStarted(10000), "process did not start: " + child.errorString().toStdString());
  if (!child.waitForFinished(20000)) {
    child.kill();
    child.waitForFinished(5000);
    throw std::runtime_error("process timed out: " + program.toStdString());
  }
  const QByteArray out = child.readAllStandardOutput();
  const QByteArray err = child.readAllStandardError();
  check(child.exitStatus() == QProcess::NormalExit && child.exitCode() == expected_exit,
        "unexpected process exit: " + out.toStdString() + err.toStdString());
  check(!err.toLower().contains("stylesheet") && !err.contains("QCss::Parser - Failed to load file"),
        "Qt could not load the Unicode stylesheet: " + err.toStdString());
  return out;
}

void test_selected_paths(const QString& root) {
  const QStringList names = {QString::fromUtf8("Flix Pozna\xc5\x84"),
      QString::fromUtf8("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"),
      QString::fromUtf8("\xf0\x9f\x98\x80 & [phone]"), QString::fromUtf8("cafe\xcc\x81")};
  const std::array flags = {"-b", "-a", "-c", "-s", "-u", "--use-pit"};
  for (const auto& name : names) {
    const QString dir = root + "/" + name;
    check(QDir().mkpath(dir), "create Unicode fixture directory");
    QStringList args;
    QStringList expected;
    for (std::size_t i = 0; i < flags.size(); ++i) {
      const QString path = dir + "/" + name + QString::number(i) + (i == 5 ? ".pit" : ".tar.md5");
      QFile file(path);
      check(file.open(QIODevice::WriteOnly), "create selected file");
      check(file.write(payload.data(), static_cast<qint64>(payload.size())) == static_cast<qint64>(payload.size()),
            "write selected file");
      file.close();

      check_contents(brokkr::gui::path_from_qstring(path));
      check_contents(brokkr::gui::path_from_qstring(QUrl::fromLocalFile(path).toLocalFile()));
      args << flags[i] << QDir::toNativeSeparators(path);
      expected << QDir::toNativeSeparators(path).normalized(QString::NormalizationForm_C);
    }
    const auto output = run(QCoreApplication::applicationFilePath(), args, QProcessEnvironment::systemEnvironment());
    auto actual = QString::fromUtf8(output).replace("\r\n", "\n").split('\n', Qt::SkipEmptyParts);
    for (auto& path : actual) path = path.normalized(QString::NormalizationForm_C);
    check(actual == expected, "native CLI paths or slot order changed");
  }

  std::vector<std::string> shuffled = {"brokkr", "-u", "u.img", "-c", "c.img", "-s", "s.img",
      "-a", "a.img", "-b", "b.img", "--no-reboot", "--target", "target"};
  std::vector<char*> pointers;
  for (auto& arg : shuffled) pointers.push_back(arg.data());
  pointers.push_back(nullptr);
  const auto parsed = brokkr::app::parse_cli_args(static_cast<int>(shuffled.size()), pointers.data());
  check(parsed && parsed->no_reboot && parsed->target == "target", "other CLI options preserved");
  const std::vector<std::filesystem::path> ordered = {"b.img", "a.img", "c.img", "s.img", "u.img"};
  check(brokkr::app::collect_inputs_in_gui_order(*parsed) == ordered, "CLI option order must not change slot order");
  char executable[] = "brokkr";
  char missing_flag[] = "--use-pit";
  char* missing[] = {executable, missing_flag, nullptr};
  check(!brokkr::app::parse_cli_args(2, missing), "missing CLI value rejected");
}

void test_application(const QString& executable, const QString& root) {
  auto env = QProcessEnvironment::systemEnvironment();
  env.remove("BROKKR_SCREENSHOT");
  check(run(executable, {"--help"}, env).contains("Usage:"), "CLI help");

  const QString unicode = QString::fromUtf8("caf\xc3\xa9 \xf0\x9f\x98\x80");
  const QString unknown = "--unknown-" + unicode;
  const auto error = QString::fromUtf8(run(executable, {"--help", unknown}, env, 2));
  check(error.normalized(QString::NormalizationForm_C).contains(unknown.normalized(QString::NormalizationForm_C)),
        "CLI error retains Unicode");

  const QString stylesheet = root + "/caf" + QChar(0x00e9) + ".qss";
  QFile style(stylesheet);
  check(style.open(QIODevice::WriteOnly), "create Unicode stylesheet");
  style.write("QWidget { font-size: 12px; }\n");
  style.close();
  const QString screenshot = root + "/" + unicode + ".png";
  env.insert("QT_QPA_PLATFORM", "offscreen");
  env.insert("BROKKR_SCREENSHOT", screenshot);
  run(executable, {"-stylesheet", QDir::toNativeSeparators(stylesheet)}, env);
  QFile image(screenshot);
  check(image.open(QIODevice::ReadOnly) && image.read(8) == QByteArray::fromHex("89504e470d0a1a0a"),
        "GUI screenshot at Unicode path");
}
}

int main(int argc, char** argv) {
  try {
    if (argc > 1 && std::strcmp(argv[1], "-b") == 0) return probe_cli(argc, argv);
    QCoreApplication app(argc, argv);
    check(app.arguments().size() == 2, "expected Brokkr executable path");
    QTemporaryDir temp(QDir::tempPath() + "/brokkr-desktop-paths-XXXXXX");
    check(temp.isValid(), "create temporary directory");
    test_selected_paths(temp.path());
    test_application(app.arguments().at(1), temp.path());
    std::cout << "desktop_paths: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "desktop_paths: " << e.what() << '\n';
    return 1;
  }
}
