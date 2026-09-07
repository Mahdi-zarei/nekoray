#include "include/global/LocalNetwork.hpp"

#include <algorithm>

#include <QDateTime>
#include <QHostAddress>
#include <QMutex>
#include <QMutexLocker>
#include <QNetworkInterface>
#include <QSet>
#include <QStringList>
#include <QUdpSocket>

#include "include/global/Configs.hpp"

namespace LocalNetwork {
    namespace {
        // The own-address set is read per connection per poll; the interface scan behind LanAddress()
        // is far heavier and its answer changes far more rarely, so the two halves age separately.
        constexpr qint64 ownTtlMs = 10000;
        constexpr qint64 lanTtlMs = 30000;

        struct OwnCache {
            QSet<QString> addresses;
            qint64 atMs = 0;
        };

        struct LanCache {
            QString address;
            QString stickyDefault; // last default-route address that was a real candidate
            qint64 atMs = 0;
        };

        // One mutex per half, so the lister thread and the GUI thread never wait on each other.
        // Everything here is leaked deliberately: the lister thread runs until process exit and
        // would otherwise reach these after static destruction.
        template <typename T, int Tag>
        T &leaked() {
            static auto *value = new T();
            return *value;
        }

        QMutex &ownMutex() { return leaked<QMutex, 0>(); }
        QMutex &lanMutex() { return leaked<QMutex, 1>(); }
        OwnCache &ownCache() { return leaked<OwnCache, 2>(); }
        LanCache &lanCache() { return leaked<LanCache, 3>(); }

        bool Stale(const qint64 builtAtMs, const qint64 nowMs, const qint64 ttlMs) {
            if (builtAtMs == 0) return true;
            const auto age = nowMs - builtAtMs;
            return age < 0 || age >= ttlMs; // a backwards clock jump must not freeze the cache
        }

        QString Normalize(const QHostAddress &addr) {
            return addr.toString().section('%', 0, 0);
        }

        bool IsTunnelInterface(const QNetworkInterface &iface) {
            // Prefixes are device names (Qt derives the Windows one from the interface LUID: ethernet_N,
            // wireless_N, tunnel_N), so our own tun is matched by its friendly name as well.
            static const QStringList devicePrefixes = {"utun", "tun", "tap", "wg", "ppp", "zt"};
            const auto device = iface.name().toLower();
            // Windows appends a suffix ("throne-tun 2") when a stale adapter instance survives an unclean exit.
            if (device.startsWith(QStringLiteral("throne-tun")) ||
                iface.humanReadableName().toLower().startsWith(QStringLiteral("throne-tun"))) return true;
            return std::any_of(devicePrefixes.cbegin(), devicePrefixes.cend(),
                               [&device](const QString &prefix) { return device.startsWith(prefix); });
        }

        // Hypervisor switches look like ordinary private Ethernet, so they are ranked below real NICs
        // rather than dropped: on a Hyper-V external switch the vEthernet adapter IS the LAN interface.
        bool LooksVirtual(const QNetworkInterface &iface) {
            static const QStringList markers = {"vethernet", "virtualbox", "vmware", "vmnet", "hyper-v",
                                                "docker", "wsl", "npcap", "bluetooth", "virbr", "veth"};
            if (iface.type() == QNetworkInterface::Virtual) return true;
            const auto haystack = (iface.name() + ' ' + iface.humanReadableName()).toLower();
            return std::any_of(markers.cbegin(), markers.cend(),
                               [&haystack](const QString &marker) { return haystack.contains(marker); });
        }

        bool IsUsableInterface(const QNetworkInterface &iface) {
            const auto flags = iface.flags();
            if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)) return false;
            if (flags.testFlag(QNetworkInterface::IsLoopBack) || flags.testFlag(QNetworkInterface::IsPointToPoint)) return false;
            return !IsTunnelInterface(iface);
        }

        bool IsPrivate(const QHostAddress &addr) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                const quint32 v = addr.toIPv4Address();
                return (v & 0xFF000000u) == 0x0A000000u ||
                       (v & 0xFFF00000u) == 0xAC100000u ||
                       (v & 0xFFFF0000u) == 0xC0A80000u;
            }
            return addr.isUniqueLocalUnicast();
        }

        int TypeRank(const QNetworkInterface::InterfaceType type) {
            return (type == QNetworkInterface::Ethernet || type == QNetworkInterface::Wifi) ? 0 : 1;
        }

        struct Candidate {
            QString address;
            int virtualRank = 1;
            int privateRank = 1;
            int typeRank = 1;
            int interfaceIndex = 0;
        };

        bool Better(const Candidate &a, const Candidate &b) {
            if (a.virtualRank != b.virtualRank) return a.virtualRank < b.virtualRank;
            if (a.privateRank != b.privateRank) return a.privateRank < b.privateRank;
            if (a.typeRank != b.typeRank) return a.typeRank < b.typeRank;
            if (a.interfaceIndex != b.interfaceIndex) return a.interfaceIndex < b.interfaceIndex;
            return a.address < b.address;
        }

        // A UDP connect sends nothing; it only makes the kernel bind the default route's source address.
        QString DefaultRouteAddress() {
            QUdpSocket sock;
            sock.connectToHost(QHostAddress(QStringLiteral("1.1.1.1")), 53);
            if (sock.state() != QAbstractSocket::ConnectedState && !sock.waitForConnected(50)) return {};
            const auto local = sock.localAddress();
            return local.isNull() ? QString() : Normalize(local);
        }

        QSet<QString> ScanOwnAddresses() {
            QSet<QString> own;
            const auto allAddresses = QNetworkInterface::allAddresses();
            for (const auto &addr : allAddresses) {
                if (addr.isNull()) continue;
                own.insert(Normalize(addr));
            }
            return own;
        }

        QString ScanLanAddress(QString &sticky) {
            QList<Candidate> v4;
            QList<Candidate> v6;
            const auto interfaces = QNetworkInterface::allInterfaces();
            for (const auto &iface : interfaces) {
                if (!IsUsableInterface(iface)) continue;
                const auto entries = iface.addressEntries();
                for (const auto &entry : entries) {
                    const auto ip = entry.ip();
                    if (ip.isNull() || ip.isLoopback() || ip.isLinkLocal() || ip.isMulticast()) continue;
                    const Candidate candidate{Normalize(ip), LooksVirtual(iface) ? 1 : 0,
                                              IsPrivate(ip) ? 0 : 1, TypeRank(iface.type()), iface.index()};
                    (ip.protocol() == QAbstractSocket::IPv4Protocol ? v4 : v6).append(candidate);
                }
            }

            // IPv6 is only a fallback, so an IPv6-only LAN still gets an address to point clients at.
            auto &pool = v4.isEmpty() ? v6 : v4;
            if (pool.isEmpty()) return {};
            std::sort(pool.begin(), pool.end(), Better);

            auto find = [&pool](const QString &address) {
                return address.isEmpty()
                           ? pool.cend()
                           : std::find_if(pool.cbegin(), pool.cend(),
                                          [&address](const Candidate &c) { return c.address == address; });
            };

            auto match = find(DefaultRouteAddress());
            if (match != pool.cend()) sticky = match->address;
            // The tun is never a candidate, so once it owns the default route the probe stops matching;
            // the last real default we saw beats the ranking's arbitrary tie-break on a multi-homed box.
            else match = find(sticky);

            const auto chosen = match != pool.cend() ? match->address : pool.first().address;
            return chosen.contains(':') ? "[" + chosen + "]" : chosen;
        }

        const Configs::SettingsRepo *settings() {
            if (Configs::dataManager == nullptr) return nullptr;
            return Configs::dataManager->settingsRepo.get();
        }
    }

    // A non-loopback bind is what actually exposes the inbound, so an explicit LAN address counts too, not just the wildcard.
    bool LanInboundEnabled() {
        const auto *s = settings();
        if (s == nullptr || s->disable_mixed_inbound) return false;
        const QHostAddress addr(s->inbound_address);
        return !addr.isNull() && !addr.isLoopback();
    }

    bool LanInboundIsWildcard() {
        const auto *s = settings();
        if (s == nullptr) return false;
        const QHostAddress addr(s->inbound_address);
        return addr == QHostAddress(QHostAddress::AnyIPv4) || addr == QHostAddress(QHostAddress::AnyIPv6);
    }

    QString LanAddress() {
        QMutexLocker lock(&lanMutex());
        auto &c = lanCache();
        const auto nowMs = QDateTime::currentMSecsSinceEpoch();
        if (Stale(c.atMs, nowMs, lanTtlMs)) {
            c.address = ScanLanAddress(c.stickyDefault);
            c.atMs = nowMs;
        }
        return c.address;
    }

    // Deliberately cheaper than the LanAddress() scan: this one runs per connection per poll.
    bool IsOwnAddress(const QString &host) {
        auto bare = host.trimmed();
        bare.remove('[').remove(']');
        if (bare.isEmpty()) return false;

        QHostAddress parsed;
        if (!parsed.setAddress(bare)) return false;
        if (parsed.isLoopback()) return true;

        QMutexLocker lock(&ownMutex());
        auto &c = ownCache();
        const auto nowMs = QDateTime::currentMSecsSinceEpoch();
        if (Stale(c.atMs, nowMs, ownTtlMs)) {
            c.addresses = ScanOwnAddresses();
            c.atMs = nowMs;
        }
        return c.addresses.contains(Normalize(parsed));
    }
}
