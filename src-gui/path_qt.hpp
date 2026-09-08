#pragma once

#include "core/path_utf8.hpp"

#include <QString>

namespace brokkr::gui {

inline std::filesystem::path path_from_qstring(const QString& path) {
  return brokkr::core::path_from_utf8(path.toStdString());
}

}
