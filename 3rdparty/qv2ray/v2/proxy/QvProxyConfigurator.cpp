#include "QvProxyConfigurator.hpp"

#ifdef Q_OS_WIN
//
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
//
#include <wininet.h>
#include <ras.h>
#include <raserror.h>
#include <vector>
#endif

#include <QStandardPaths>
#include <QProcess>

#include "3rdparty/qv2ray/wrapper.hpp"
#include "include/global/Configs.hpp"

#define QV_MODULE_NAME "SystemProxy"

#define QSTRN(num) QString::number(num)

namespace Qv2ray::components::proxy {

    using ProcessArgument = QPair<QString, QStringList>;
#ifdef Q_OS_MACOS
    QStringList macOSgetNetworkServices() {
        QProcess p;
        p.setProgram("/usr/sbin/networksetup");
        p.setArguments(QStringList{"-listallnetworkservices"});
        p.start();
        p.waitForStarted();
        p.waitForFinished();
        LOG(p.errorString());
        auto str = p.readAllStandardOutput();
        auto lines = SplitLines(str);
        QStringList result;

        // Start from 1 since first line is unneeded.
        for (auto i = 1; i < lines.count(); i++) {
            // * means disabled.
            if (!lines[i].contains("*")) {
                result << lines[i];
            }
        }

        LOG("Found " + QSTRN(result.size()) + " network services: " + result.join(";"));
        return result;
    }

    // networksetup's plist backup copy needs root and prints to stderr, which QProcess::execute
    // would forward straight into the app log on every toggle.
    void macOSnetworkSetup(const QStringList &args) {
        QProcess p;
        p.setProgram("/usr/sbin/networksetup");
        p.setArguments(args);
        p.setStandardOutputFile(QProcess::nullDevice());
        p.setStandardErrorFile(QProcess::nullDevice());
        p.start();
        p.waitForStarted();
        p.waitForFinished();
    }
#endif
#ifdef Q_OS_WIN
#define NO_CONST(expr) const_cast<wchar_t *>(expr)
    // static auto DEFAULT_CONNECTION_NAME =
    // NO_CONST(L"DefaultConnectionSettings");
    ///
    /// INTERNAL FUNCTION
    bool __QueryProxyOptions() {
        INTERNET_PER_CONN_OPTION_LIST List;
        INTERNET_PER_CONN_OPTION Option[5];
        //
        unsigned long nSize = sizeof(INTERNET_PER_CONN_OPTION_LIST);
        Option[0].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
        Option[1].dwOption = INTERNET_PER_CONN_AUTODISCOVERY_FLAGS;
        Option[2].dwOption = INTERNET_PER_CONN_FLAGS;
        Option[3].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
        Option[4].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
        //
        List.dwSize = sizeof(INTERNET_PER_CONN_OPTION_LIST);
        List.pszConnection = nullptr; // NO_CONST(DEFAULT_CONNECTION_NAME);
        List.dwOptionCount = 5;
        List.dwOptionError = 0;
        List.pOptions = Option;

        if (!InternetQueryOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &List, &nSize)) {
            LOG("InternetQueryOption failed, GLE=" + QSTRN(GetLastError()));
        }

        LOG("System default proxy info:");

        if (Option[0].Value.pszValue != nullptr) {
            LOG(QString::fromWCharArray(Option[0].Value.pszValue));
        }

        if ((Option[2].Value.dwValue & PROXY_TYPE_AUTO_PROXY_URL) == PROXY_TYPE_AUTO_PROXY_URL) {
            LOG("PROXY_TYPE_AUTO_PROXY_URL");
        }

        if ((Option[2].Value.dwValue & PROXY_TYPE_AUTO_DETECT) == PROXY_TYPE_AUTO_DETECT) {
            LOG("PROXY_TYPE_AUTO_DETECT");
        }

        if ((Option[2].Value.dwValue & PROXY_TYPE_DIRECT) == PROXY_TYPE_DIRECT) {
            LOG("PROXY_TYPE_DIRECT");
        }

        if ((Option[2].Value.dwValue & PROXY_TYPE_PROXY) == PROXY_TYPE_PROXY) {
            LOG("PROXY_TYPE_PROXY");
        }

        if (!InternetQueryOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &List, &nSize)) {
            LOG("InternetQueryOption failed,GLE=" + QSTRN(GetLastError()));
        }

        if (Option[4].Value.pszValue != nullptr) {
            LOG(QString::fromStdWString(Option[4].Value.pszValue));
        }

        INTERNET_VERSION_INFO Version;
        nSize = sizeof(INTERNET_VERSION_INFO);
        InternetQueryOption(nullptr, INTERNET_OPTION_VERSION, &Version, &nSize);

        if (Option[0].Value.pszValue != nullptr) {
            GlobalFree(Option[0].Value.pszValue);
        }

        if (Option[3].Value.pszValue != nullptr) {
            GlobalFree(Option[3].Value.pszValue);
        }

        if (Option[4].Value.pszValue != nullptr) {
            GlobalFree(Option[4].Value.pszValue);
        }

        return false;
    }
    bool __SetProxyOptions(LPWSTR proxy_full_addr, bool isPAC) {
        INTERNET_PER_CONN_OPTION_LIST list;
        DWORD dwBufSize = sizeof(list);
        // Fill the list structure.
        list.dwSize = sizeof(list);
        // NULL == LAN, otherwise connectoid name.
        list.pszConnection = nullptr;

        if (nullptr == proxy_full_addr) {
            LOG("Clearing system proxy");
            //
            list.dwOptionCount = 1;
            list.pOptions = new INTERNET_PER_CONN_OPTION[1];

            // Ensure that the memory was allocated.
            if (nullptr == list.pOptions) {
                // Return if the memory wasn't allocated.
                return false;
            }

            // Set flags.
            list.pOptions[0].dwOption = INTERNET_PER_CONN_FLAGS;
            list.pOptions[0].Value.dwValue = PROXY_TYPE_DIRECT;
        } else if (isPAC) {
            LOG("Setting system proxy for PAC");
            //
            list.dwOptionCount = 2;
            list.pOptions = new INTERNET_PER_CONN_OPTION[2];

            if (nullptr == list.pOptions) {
                return false;
            }

            // Set flags.
            list.pOptions[0].dwOption = INTERNET_PER_CONN_FLAGS;
            list.pOptions[0].Value.dwValue = PROXY_TYPE_DIRECT | PROXY_TYPE_AUTO_PROXY_URL;
            // Set proxy name.
            list.pOptions[1].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
            list.pOptions[1].Value.pszValue = proxy_full_addr;
        } else {
            LOG("Setting system proxy for Global Proxy");
            //
            list.dwOptionCount = 2;
            list.pOptions = new INTERNET_PER_CONN_OPTION[2];

            if (nullptr == list.pOptions) {
                return false;
            }

            // Set flags.
            list.pOptions[0].dwOption = INTERNET_PER_CONN_FLAGS;
            list.pOptions[0].Value.dwValue = PROXY_TYPE_DIRECT | PROXY_TYPE_PROXY;
            // Set proxy name.
            list.pOptions[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
            list.pOptions[1].Value.pszValue = proxy_full_addr;
            // Set proxy override.
            // list.pOptions[2].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
            // auto localhost = L"localhost";
            // list.pOptions[2].Value.pszValue = NO_CONST(localhost);
        }

        // Set proxy for LAN.
        if (!InternetSetOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, dwBufSize)) {
            LOG("InternetSetOption failed for LAN, GLE=" + QSTRN(GetLastError()));
        }

        RASENTRYNAME entry;
        entry.dwSize = sizeof(entry);
        std::vector<RASENTRYNAME> entries;
        DWORD size = sizeof(entry), count;
        LPRASENTRYNAME entryAddr = &entry;
        auto ret = RasEnumEntries(nullptr, nullptr, entryAddr, &size, &count);
        if (ERROR_BUFFER_TOO_SMALL == ret) {
            entries.resize(count);
            entries[0].dwSize = sizeof(RASENTRYNAME);
            entryAddr = entries.data();
            ret = RasEnumEntries(nullptr, nullptr, entryAddr, &size, &count);
        }
        if (ERROR_SUCCESS != ret) {
            LOG("Failed to list entry names");
            return false;
        }

        // Set proxy for each connectoid.
        for (DWORD i = 0; i < count; ++i) {
            list.pszConnection = entryAddr[i].szEntryName;
            if (!InternetSetOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, dwBufSize)) {
                LOG("InternetSetOption failed for connectoid " + QString::fromWCharArray(list.pszConnection) + ", GLE=" + QSTRN(GetLastError()));
            }
        }

        delete[] list.pOptions;
        InternetSetOption(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
        InternetSetOption(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
        return true;
    }
#endif

    void SetSystemProxy(int httpPort, int socksPort, QString scheme, bool setSocksSystemProxy) {
        const QString &address = "127.0.0.1";
        bool hasHTTP = (httpPort > 0 && httpPort < 65536);
        bool hasSOCKS = (socksPort > 0 && socksPort < 65536);

#ifdef Q_OS_WIN
        if (!hasHTTP) {
            LOG("Nothing?");
            return;
        } else {
            LOG("Qv2ray will set system proxy to use HTTP");
        }
#else
        if (!hasHTTP && !hasSOCKS) {
            LOG("Nothing?");
            return;
        }

        if (hasHTTP) {
            LOG("Qv2ray will set system proxy to use HTTP");
        }

        if (hasSOCKS) {
            LOG("Qv2ray will set system proxy to use SOCKS");
        }
#endif

#ifdef Q_OS_WIN
        if (scheme == "http") scheme = "http://{ip}:{port}";
        else if (scheme == "socks") scheme = "socks={ip}:{port}";
        scheme = scheme.replace("{ip}", address)
                  .replace("{port}", Int2String(socksPort));
        //
        LOG("Windows proxy string: " + scheme);
        auto proxyStrW = new WCHAR[scheme.length() + 1];
        wcscpy(proxyStrW, scheme.toStdWString().c_str());
        //
        __QueryProxyOptions();

        if (!__SetProxyOptions(proxyStrW, false)) {
            LOG("Failed to set proxy.");
        }

        __QueryProxyOptions();
#elif defined(Q_OS_LINUX)
        QList<ProcessArgument> actions;
        //
        bool isKDE = qEnvironmentVariable("XDG_CURRENT_DESKTOP") == "KDE" ||
                     qEnvironmentVariable("XDG_CURRENT_DESKTOP") == "Trinity";
        const auto configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        QString kwriteconfigCmd = qEnvironmentVariable("KDE_SESSION_VERSION") == "5" ? "kwriteconfig5" : qEnvironmentVariable("KDE_SESSION_VERSION") == "6" ? "kwriteconfig6" : "kwriteconfig";

        //
        // Configure HTTP Proxies for HTTP, FTP and HTTPS.
        //
        // GNOME and KDE encode "use an HTTP proxy for FTP" differently. KDE's ftpProxy
        // takes the http:// URL below and works fine, but GNOME's ftp schema means "a
        // real FTP proxy lives here", which is never true of the mixed inbound, and the
        // session exports it as ftp_proxy=ftp://... where curl and friends choke on it.
        // So the ftp entry is written for KDE only.
        if (hasHTTP) {
            // iterate over protocols...
            for (const auto &protocol: QStringList{"http", "ftp", "https"}) {
                // for GNOME:
                if (protocol != "ftp") {
                    actions << ProcessArgument{"gsettings",
                                               {"set", "org.gnome.system.proxy." + protocol, "host", address}};
                    actions << ProcessArgument{"gsettings",
                                               {"set", "org.gnome.system.proxy." + protocol, "port", QSTRN(httpPort)}};
                }

                // for KDE:
                if (isKDE) {
                    actions << ProcessArgument{kwriteconfigCmd,
                                               {"--file", configPath + "/kioslaverc", //
                                                "--group", "Proxy Settings",          //
                                                "--key", protocol + "Proxy",          //
                                                "http://" + address + " " + QSTRN(httpPort)}};
                }
            }
        }

        // Drop any ftp entry an older version wrote, so it stops generating ftp_proxy.
        actions << ProcessArgument{"gsettings", {"reset", "org.gnome.system.proxy.ftp", "host"}};
        actions << ProcessArgument{"gsettings", {"reset", "org.gnome.system.proxy.ftp", "port"}};

        // Configure SOCKS5 Proxies
        //
        // for GNOME: opt-in only. The entry itself is valid, but the session exports it
        // as all_proxy=socks://..., a scheme several CLI tools reject. Non-HTTP traffic
        // still reaches the tunnel without it, through use-same-proxy below. Anything
        // else -- opted out, or no socks port -- means the entry should not be there.
        if (hasSOCKS && setSocksSystemProxy) {
            actions << ProcessArgument{"gsettings", {"set", "org.gnome.system.proxy.socks", "host", address}};
            actions << ProcessArgument{"gsettings",
                                       {"set", "org.gnome.system.proxy.socks", "port", QSTRN(socksPort)}};
        } else {
            actions << ProcessArgument{"gsettings", {"reset", "org.gnome.system.proxy.socks", "host"}};
            actions << ProcessArgument{"gsettings", {"reset", "org.gnome.system.proxy.socks", "port"}};
        }

        // for KDE: always. kioslaverc is read by KIO and never becomes an environment
        // variable, so the problem above does not exist here.
        if (hasSOCKS && isKDE) {
            actions << ProcessArgument{kwriteconfigCmd,
                                       {"--file", configPath + "/kioslaverc", //
                                        "--group", "Proxy Settings",          //
                                        "--key", "socksProxy",                //
                                        "socks://" + address + " " + QSTRN(socksPort)}};
        }

        // Setting Proxy Mode to Manual
        {
            // for GNOME:
            {
                // use-same-proxy makes GLib fall back to the HTTP proxy for schemes with
                // no entry of their own. Without it a user who turned it off gets
                // direct:// for everything non-HTTP, leaking traffic past the tunnel.
                actions << ProcessArgument{"gsettings",
                                           {"set", "org.gnome.system.proxy", "use-same-proxy", "true"}};
                actions << ProcessArgument{"gsettings", {"set", "org.gnome.system.proxy", "mode", "manual"}};
            }

            // for KDE:
            if (isKDE) {
                actions << ProcessArgument{kwriteconfigCmd,
                                           {"--file", configPath + "/kioslaverc", //
                                            "--group", "Proxy Settings",          //
                                            "--key", "ProxyType", "1"}};
            }
        }

        // Notify kioslaves to reload system proxy configuration.
        if (isKDE) {
            actions << ProcessArgument{"dbus-send",
                                       {"--type=signal", "/KIO/Scheduler",                 //
                                        "org.kde.KIO.Scheduler.reparseSlaveConfiguration", //
                                        "string:''"}};
        }
        // Execute them all!
        //
        // note: do not use std::all_of / any_of / none_of,
        // because those are short-circuit and cannot guarantee atomicity.
        QList<bool> results;
        for (const auto &action: actions) {
            // execute and get the code
            const auto returnCode = QProcess::execute(action.first, action.second);
            // print out the commands and result codes
            DEBUG(QString("[%1] Program: %2, Args: %3").arg(returnCode).arg(action.first).arg(action.second.join(";")));
            // give the code back
            results << (returnCode == QProcess::NormalExit);
        }

        if (results.count(true) != actions.size()) {
            LOG("Something wrong when setting proxies.");
        }
#else

        for (const auto &service: macOSgetNetworkServices()) {
            LOG("Setting proxy for interface: " + service);
            if (hasHTTP) {
                macOSnetworkSetup({"-setwebproxystate", service, "on"});
                macOSnetworkSetup({"-setsecurewebproxystate", service, "on"});
                macOSnetworkSetup({"-setwebproxy", service, address, QSTRN(httpPort)});
                macOSnetworkSetup({"-setsecurewebproxy", service, address, QSTRN(httpPort)});
            }

            if (hasSOCKS) {
                macOSnetworkSetup({"-setsocksfirewallproxystate", service, "on"});
                macOSnetworkSetup({"-setsocksfirewallproxy", service, address, QSTRN(socksPort)});
            }
        }

#endif
    }

    void ClearSystemProxy() {
        LOG("Clearing System Proxy");

#ifdef Q_OS_WIN
        if (!__SetProxyOptions(nullptr, false)) {
            LOG("Failed to clear proxy.");
        }
#elif defined(Q_OS_LINUX)
        QList<ProcessArgument> actions;
        const bool isKDE = qEnvironmentVariable("XDG_CURRENT_DESKTOP") == "KDE" ||
                           qEnvironmentVariable("XDG_CURRENT_DESKTOP") == "Trinity";
        const auto configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        // Setting System Proxy Mode to: None
        {
            // for GNOME:
            {
                actions << ProcessArgument{"gsettings", {"set", "org.gnome.system.proxy", "mode", "none"}};
            }

            // for KDE:
            if (isKDE) {
                actions << ProcessArgument{qEnvironmentVariable("KDE_SESSION_VERSION") == "5" ? "kwriteconfig5" : qEnvironmentVariable("KDE_SESSION_VERSION") == "6" ? "kwriteconfig6" : "kwriteconfig",
                                           {"--file", configRoot + "/kioslaverc", //
                                            "--group", "Proxy Settings",          //
                                            "--key", "ProxyType", "0"}};
            }
        }

        // Notify kioslaves to reload system proxy configuration.
        if (isKDE) {
            actions << ProcessArgument{"dbus-send",
                                       {"--type=signal", "/KIO/Scheduler",                 //
                                        "org.kde.KIO.Scheduler.reparseSlaveConfiguration", //
                                        "string:''"}};
        }

        // Execute the Actions
        for (const auto &action: actions) {
            // execute and get the code
            const auto returnCode = QProcess::execute(action.first, action.second);
            // print out the commands and result codes
            DEBUG(QString("[%1] Program: %2, Args: %3").arg(returnCode).arg(action.first).arg(action.second.join(";")));
        }

#else
        for (const auto &service: macOSgetNetworkServices()) {
            LOG("Clearing proxy for interface: " + service);
            macOSnetworkSetup({"-setautoproxystate", service, "off"});
            macOSnetworkSetup({"-setwebproxystate", service, "off"});
            macOSnetworkSetup({"-setsecurewebproxystate", service, "off"});
            macOSnetworkSetup({"-setsocksfirewallproxystate", service, "off"});
        }

#endif
    }
} // namespace Qv2ray::components::proxy
