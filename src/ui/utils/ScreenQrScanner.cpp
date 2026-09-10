#include "include/ui/utils/ScreenQrScanner.h"

#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QUuid>
#include <QWidget>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusInterface>
#endif

#include "3rdparty/QrDecoder.h"
#include "include/ui/mainwindow.h"

#ifdef Q_OS_LINUX
OrgFreedesktopPortalRequestInterface::OrgFreedesktopPortalRequestInterface(
  const QString& service,
  const QString& path,
  const QDBusConnection& connection,
  QObject* parent)
  : QDBusAbstractInterface(service,
                           path,
                           "org.freedesktop.portal.Request",
                           connection,
                           parent)
{}

OrgFreedesktopPortalRequestInterface::~OrgFreedesktopPortalRequestInterface() {}
#endif

namespace ScreenQr {
    namespace {
        constexpr int CAPTURE_DELAY_MS = 2500;

        bool IsWayland() {
            return qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland"
                   || qEnvironmentVariable("WAYLAND_DISPLAY").contains("wayland", Qt::CaseInsensitive);
        }
    }

    QPixmap GrabScreen(QScreen *screen, bool &ok) {
        QPixmap p;
        if (screen == nullptr) {
            ok = false;
            return p;
        }
        const QRect geom = screen->geometry();
#ifdef Q_OS_LINUX
        if (IsWayland()) {
            QDBusInterface screenshotInterface(
              QStringLiteral("org.freedesktop.portal.Desktop"),
              QStringLiteral("/org/freedesktop/portal/desktop"),
              QStringLiteral("org.freedesktop.portal.Screenshot"));

            QString token =
              QUuid::createUuid().toString().remove('-').remove('{').remove('}');

            auto* request = new OrgFreedesktopPortalRequestInterface(
              QStringLiteral("org.freedesktop.portal.Desktop"),
              "/org/freedesktop/portal/desktop/request/" +
                QDBusConnection::sessionBus().baseService().remove(':').replace('.','_') +
                "/" + token,
              QDBusConnection::sessionBus());

            QEventLoop loop;
            const auto gotSignal = [&p, &loop](uint status, const QVariantMap& map) {
                if (status == 0) {
                    QUrl uri = map.value("uri").toString();
                    QString uriString = uri.toLocalFile();
                    p = QPixmap(uriString);
                    p.setDevicePixelRatio(qApp->devicePixelRatio());
                    QFile imgFile(uriString);
                    imgFile.remove();
                }
                loop.quit();
            };

            // prevent racy situations and listen before calling screenshot
            QMetaObject::Connection conn = QObject::connect(
              request, &org::freedesktop::portal::Request::Response, gotSignal);

            screenshotInterface.call(
              QStringLiteral("Screenshot"),
              "",
              QMap<QString, QVariant>({ { "handle_token", QVariant(token) },
                                        { "interactive", QVariant(false) } }));

            loop.exec();
            QObject::disconnect(conn);
            request->Close().waitForFinished();
            request->deleteLater();

            if (p.isNull()) {
                ok = false;
            }
            return p;
        }
#endif
        return screen->grabWindow(0, geom.x(), geom.y(), geom.width(), geom.height());
    }

    QStringList ScanScreens(QWidget *hideDuringScan, bool &captured) {
        captured = false;

        const bool restore = hideDuringScan != nullptr && hideDuringScan->isVisible();
        if (restore) hideDuringScan->hide();

        // Spin rather than sleep: a blocking wait can stop the hide from painting at all.
        QEventLoop wait;
        QTimer::singleShot(CAPTURE_DELAY_MS, &wait, &QEventLoop::quit);
        wait.exec();

        QList<QPixmap> shots;
        const auto screens = IsWayland() ? QList<QScreen *>{QGuiApplication::primaryScreen()}
                                         : QGuiApplication::screens();
        for (QScreen *screen : screens) {
            bool ok = true;
            QPixmap shot = GrabScreen(screen, ok);
            if (ok && !shot.isNull()) {
                captured = true;
                shots.append(shot);
            }
        }

        if (restore) hideDuringScan->show();

        QStringList payloads;
        for (const auto &shot : shots) {
            for (const auto &text : QrDecoder().decode(shot.toImage())) {
                if (!text.isEmpty() && !payloads.contains(text)) payloads << text;
            }
        }
        return payloads;
    }
} // namespace ScreenQr
