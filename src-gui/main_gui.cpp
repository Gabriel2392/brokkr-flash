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

#include "brokkr_wrapper.hpp"
#include "platform/platform_all.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    auto lock = brokkr::platform::SingleInstanceLock::try_acquire("brokkr-engine");
    if (!lock) {
        QMessageBox::critical(nullptr, "Brokkr Flasher", "Another instance is already running.");
        return 2;
    }

    BrokkrWrapper window;
    window.show();
    return app.exec();
}
