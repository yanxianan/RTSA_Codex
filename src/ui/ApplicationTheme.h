#pragma once

#include <QPalette>

class QApplication;

namespace rtsa {

QPalette createApplicationPalette();
void applyApplicationTheme(QApplication& application);

} // namespace rtsa
