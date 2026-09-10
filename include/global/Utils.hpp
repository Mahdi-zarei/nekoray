// DO NOT INCLUDE THIS
#pragma once

#include <functional>
#include <memory>
#include <QObject>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QDebug>
#include <QApplication>
#include <QStyle>
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif

enum osType
{
    unknown = 0,
    Linux = 1,
    Windows = 2,
    Darwin = 3,
};

inline osType getOS()
{
#ifdef Q_OS_MACOS
    return Darwin;
#endif
#ifdef Q_OS_LINUX
    return Linux;
#endif
#ifdef Q_OS_WIN
    return Windows;
#endif
    return unknown;
}

inline QString getOSString() {
    auto os = getOS();
    if (os == Linux) {
        return "Linux";
    }
    if (os == Darwin) {
        return "Darwin";
    }
    if (os == Windows) {
        return "Windows";
    }
    if (os == unknown) {
        return "Unknown";
    }
    return "Unknown";
}

inline QString software_name;
inline QString software_core_name;

// Epoch-seconds, set once in main().
inline qint64 appStartEpoch = 0;

class QWidget;
inline QWidget *mainwindow;
inline std::function<void(QString)> MW_show_log;

enum class MwMessage {
    UpdateSettings,       // args: MwArg settings-change tokens
    RestartProgram,
    Raise,
    UpdateShortcuts,
    ProfileChanged,       // arg MwArg::RestartProxy when the saved profile is part of the running config
    GroupsChanged,
    SubscriptionFinished, // arg MwArg::Quiet skips the import-count line
    SubscriptionNewGroup,
    // args: { group id, then the id of every profile it deleted or replaced in place }
    SubscriptionGroupChanged,
    CoreCrashed,
    CoreStarted,          // args: { startedProfileId }
};

// String tokens carried in a MwMessage's argument list.
namespace MwArg {
    // UpdateSettings args.
    inline const QString Route        = QStringLiteral("route");
    inline const QString Vpn          = QStringLiteral("vpn");
    inline const QString NeedRestart  = QStringLiteral("needRestart");
    inline const QString ChoosePort   = QStringLiteral("choosePort");
    inline const QString DisableTray  = QStringLiteral("disableTray");
    inline const QString SystemDns    = QStringLiteral("systemDns");
    inline const QString TrayIcon     = QStringLiteral("trayIcon");
    inline const QString MaxLogLines  = QStringLiteral("maxLogLines");
    inline const QString DisableAdmin = QStringLiteral("disableAdmin");
    inline const QString ProfileListDisplay = QStringLiteral("profileListDisplay");
    // ProfileChanged arg.
    inline const QString RestartProxy = QStringLiteral("restartProxy");
    // SubscriptionFinished arg.
    inline const QString Quiet        = QStringLiteral("quiet");
}

inline std::function<void(MwMessage, QStringList)> MW_dialog_message;
// Set by MainWindow; marshals to the UI thread.
inline std::function<void(QString)> MW_handle_deeplink;

// Set by MainWindow; marshals to the UI thread.
inline std::function<void(QStringList)> MW_import_files;

// The pending buffer covers URLs arriving before the main window exists.
QString Deeplink_ExtractFromArgs(const QStringList &args);
void Deeplink_Submit(const QString &url);
void Deeplink_FlushPending();

// Paths may be given either as a plain path or as a file:// URL.
QStringList LaunchFiles_ExtractFromArgs(const QStringList &args, const QDir &launchDir);
void LaunchFiles_Submit(const QStringList &paths);
void LaunchFiles_FlushPending();

class QThread;
inline QThread *DS_cores;
inline QThread *LogThread;

#define FIRST_OR_SECOND(a, b) a.isEmpty() ? b : a

inline const QString UNICODE_LRO = QString::fromUtf8(QByteArray::fromHex("E280AD"));

#define Int2String(num) QString::number(num)

inline QString SubStrBefore(const QString &str, const QString &sub) {
    const qsizetype pos = str.indexOf(sub);
    return pos == -1 ? str : str.left(pos);
}

inline QString SubStrAfter(const QString &str, const QString &sub) {
    const qsizetype pos = str.indexOf(sub);
    return pos == -1 ? str : str.right(str.length() - pos - sub.length());
}

QString QStringList2Command(const QStringList &list);

QStringList SplitLines(const QString &_string);

QStringList SplitAndTrim(const QString& raw, const QString& seperator, bool keepEmpty = true);

QByteArray DecodeB64IfValid(const QString &input, QByteArray::Base64Options options = QByteArray::Base64Option::Base64Encoding);

class QUrlQuery;

QString GetQueryValue(const QUrlQuery &q, const QString &key, const QString &def = "");

QString GetRandomString(int randomStringLength);

quint64 GetRandomUint64();

class QJsonObject;
class QJsonArray;

QJsonObject QString2QJsonObject(const QString &jsonString);

QString QJsonObject2QString(const QJsonObject &jsonObject, bool compact);

QJsonArray QListInt2QJsonArray(const QList<int> &list);

QJsonArray QListStr2QJsonArray(const QList<QString> &list);

QList<int> QJsonArray2QListInt(const QJsonArray &arr);

QJsonObject QMapString2QJsonObject(const QMap<QString,QString> &mp);

QList<QString> QListInt2QListString(const QList<int> &list);

QList<int> QStringList2QListInt(const QList<QString> &list);

#define QJSONARRAY_ADD(arr, add) \
    for (const auto &a: (add)) { \
        (arr) += a;              \
    }
#define QJSONOBJECT_COPY(src, dst, key) \
    if (src.contains(key)) dst[key] = src[key];
#define QJSONOBJECT_COPY2(src, dst, src_key, dst_key) \
    if (src.contains(src_key)) dst[dst_key] = src[src_key];

QList<QString> QJsonArray2QListString(const QJsonArray &arr);

QJsonArray QString2QJsonArray(const QString& str);

QByteArray ReadFile(const QString &path);

QString ReadFileText(const QString &path);

bool IsIpAddress(const QString &str);

bool IsIpAddressV4(const QString &str);

bool IsIpAddressV6(const QString &str);

inline QString UnwrapIPV6Host(QString &str) {
    return str.replace("[", "").replace("]", "");
}

inline QString WrapIPV6Host(QString &str) {
    if (!IsIpAddressV6(str)) return str;
    return "[" + UnwrapIPV6Host(str) + "]";
}

inline QString DisplayAddress(QString serverAddress, int serverPort) {
    if (serverAddress.isEmpty() && serverPort == 0) return {};
    return WrapIPV6Host(serverAddress) + ":" + Int2String(serverPort);
}

inline QString DisplayDest(const QString& dest, QString domain)
{
    if (domain.isEmpty() || dest.split(":").first() == domain) return dest;
    return dest + " (" + domain + ")";
}

// Returns 0 when no port could be reserved; pass the interface you will bind (Windows rejects the dual-stack any-address with IPv6 off).
int MkPort(const QString &address = {});

QList<int> MkManyPorts(int num, const QString &address = "127.0.0.1");

QString DisplayTime(long long time, int formatType = 0);

QString ReadableSize(const qint64 &size);

inline bool InRange(unsigned x, unsigned low, unsigned high) {
    return (low <= x && x <= high);
}

inline bool IsValidPort(int port) {
    return InRange(port, 1, 65535);
}

QWidget *GetMessageBoxParent();

int MessageBoxWarning(const QString &title, const QString &text);

// Non-modal and coalescing: a repeat while the box is open only updates its text, so a recurring background failure never stacks windows. UI thread only.
void ShowPassiveWarning(const QString &title, const QString &text);

// Thread-safe: queues ShowPassiveWarning on the UI thread and returns at once, even when called from the UI thread.
void PostPassiveWarning(const QString &title, const QString &text);

int MessageBoxInfo(const QString &title, const QString &text);

void MessageBoxScrollable(const QString &title, const QString &text);

int MessageBoxCheck(const QString &title, const QString &text, const QString &checkBoxText, bool &isChecked);

void ActivateWindow(QWidget *w);

void HideWindow(QWidget *w);

void runOnUiThread(const std::function<void()> &callback, bool wait = false);

void runOnNewThread(const std::function<void()> &callback, bool wait = false);

void runOnThread(const std::function<void()> &callback, QObject *parent, bool wait = false);

template<typename EMITTER, typename SIGNAL, typename RECEIVER, typename ReceiverFunc>
inline void connectOnce(EMITTER *emitter, SIGNAL signal, RECEIVER *receiver, ReceiverFunc f,
                        Qt::ConnectionType connectionType = Qt::AutoConnection) {
    auto connection = std::make_shared<QMetaObject::Connection>();
    auto onTriggered = [connection, f](auto... arguments) {
        std::invoke(f, arguments...);
        QObject::disconnect(*connection);
    };

    *connection = QObject::connect(emitter, signal, receiver, onTriggered, connectionType);
}

void setTimeout(const std::function<void()> &callback, QObject *obj, int timeout = 0);

inline bool isDarkMode() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#endif
    return qApp->style()->standardPalette().window().color().lightness() < qApp->style()->standardPalette().windowText().color().lightness();
}
