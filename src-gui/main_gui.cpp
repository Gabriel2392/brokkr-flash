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

#include <QApplication>
#include <QMessageBox>

#include "app/cli_mode.hpp"
#include "platform/platform_all.hpp"
#include "brokkr_wrapper.hpp"

#if defined(_WIN32)
  #include <windows.h>
#endif

int main(int argc, char* argv[]) {
  const bool cli_mode = brokkr::app::should_run_cli(argc, argv);

  if (cli_mode) {
    return brokkr::app::run_cli(argc, argv);
  }

#if defined(_WIN32)
  if (HWND cw = ::GetConsoleWindow(); cw) {
    ::ShowWindow(cw, SW_HIDE);
    (void)::FreeConsole();
  }
#endif

  QApplication app(argc, argv);

  auto lock = brokkr::platform::SingleInstanceLock::try_acquire("brokkr-engine");
  if (!lock) {
    QMessageBox::critical(nullptr, "Brokkr Flash", "Another instance is already running.");
    return 2;
  }

  BrokkrWrapper window;
  window.show();
  return app.exec();
}
