#pragma once
#include <QList>
#include <QString>

namespace Constants
{
    namespace ProxyTypes
    {
        inline const QString Socks = "socks";
        inline const QString HTTP = "http";
        inline const QString ShadowSocks = "shadowsocks";
        inline const QString Trojan = "trojan";
        inline const QString VMess = "vmess";
        inline const QString VLESS = "vless";
        inline const QString ShadowTLS = "shadowtls";
        inline const QString Hysteria = "hysteria";
        inline const QString Hysterya2 = "hysteria2";
        inline const QString Tuic = "tuic";
        inline const QString WireGuard = "wireguard";
        inline const QString SSH = "ssh";
        inline const QString Chain = "chain";

        inline const QStringList proxyTypes = {
            Socks, HTTP, ShadowSocks, Trojan, VMess, VLESS, ShadowTLS, Hysteria, Hysterya2, Tuic, WireGuard, SSH, Chain
        };

        inline bool validate(QString const &str)
        {
            return proxyTypes.contains(str);
        }
    }

    namespace MuxEnableModes
    {
        inline int MuxDefault = 0;
        inline int MuxOn = 1;
        inline int MuxOff = 2;
    }

    namespace MuxTypes
    {
        inline const QString Smux = "smux";
        inline const QString Yamux = "yamux";
        inline const QString H2mux = "h2mux";

        inline const QStringList muxTypes = {Smux, Yamux, H2mux};

        inline bool validate(QString const& str)
        {
            return muxTypes.contains(str);
        }
    }

    namespace TransportTypes
    {
        inline const QString HTTP = "http";
        inline const QString WebSocket = "ws";
        inline const QString QUIC = "quic";
        inline const QString GRPC = "grpc";
        inline const QString HTTPUpgrade = "httpupgrade";

        inline const QStringList transportTypes = {HTTP, WebSocket, QUIC, GRPC, HTTPUpgrade};

        inline bool validate(QString const& str)
        {
            return transportTypes.contains(str);
        }
    }

    namespace TLSTypes
    {
        inline const QString TLS = "tls";
        inline const QString Reality = "reality";

        inline const QStringList tlsTypes = {TLS, Reality};

        inline bool validate(QString const& str)
        {
            return tlsTypes.contains(str);
        }
    }

    namespace TLSFingerPrintTypes
    {
        inline const QString Chrome = "chrome";
        inline const QString Firefox = "firefox";
        inline const QString Edge = "edge";
        inline const QString Safari = "safari";
        inline const QString Browser360 = "360";
        inline const QString Qq = "qq";
        inline const QString Ios = "ios";
        inline const QString Android = "android";
        inline const QString Random = "random";
        inline const QString Randomized = "randomized";

        inline const QStringList tlsFingerPrintTypes = {
            Chrome, Firefox, Edge, Safari, Browser360, Qq, Ios, Android, Random, Randomized
        };

        inline bool validate(QString const& str)
        {
            return tlsFingerPrintTypes.contains(str);
        }
    }
}
