#pragma once

#include <QPixmap>
#include <QStringList>

class QScreen;
class QWidget;

namespace ScreenQr {
    // On Wayland this goes through the portal, which returns the whole desktop, not one screen.
    QPixmap GrabScreen(QScreen *screen, bool &ok);

    QStringList ScanScreens(QWidget *hideDuringScan, bool &captured);
} // namespace ScreenQr
