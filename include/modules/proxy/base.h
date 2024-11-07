#pragma once
#include <QJsonObject>

#include "include/global.h"

#include <QList>
#include <qmap.h>
#include <QString>

namespace ProxyType
{
    class BaseProxy
    {
    public:
        virtual ~BaseProxy() = default;

        // common stuff
        QString id;
        QString name;
        QString groupID;
        QString type;

        // full config params
        bool isFullConfig = false;
        QString fullConfig;

        // server params
        QString serverAddr = "127.0.0.1";
        int serverPort = 80;

        // mux params
        int muxEnable = Constants::MuxEnableModes::MuxDefault;
        QString muxType = Constants::MuxTypes::Smux;
        int muxMinStreams = 0;
        int muxMaxConns = 8;
        bool useBrutal = false;
        int brutalSpeed = 0; // in megabits per second

        // transport params
        bool useTransport = false;
        QString transportType;

        // HTTP, Websocket and HTTPUpgrade transport params
        QStringList host;
        QString path;
        QString method;
        QMap<QString, QString> headers;
        int maxEarlyData;
        QString earlyDataHeader;

        // GRPC params
        QString serviceName;

        // TLS params
        bool useTLS = false;
        QString tlsType = Constants::TLSTypes::TLS;
        QString serverName;
        bool allowInsecure = false;
        QStringList alpn;

        // reality TLS params
        QString publicKey;
        QString shortKey;

        // uTLS fingerprinting params
        bool useUTLS = false;
        QString fingerPrint = Constants::TLSFingerPrintTypes::Chrome;

        virtual bool supportsMux()
        {
            return false;
        }

        virtual bool supportsTLS()
        {
            return false;
        }

        virtual bool supportsTransport()
        {
            return false;
        }

        virtual QJsonObject buildProxy(QString const &tag)
        {
            return {};
        }
    };
}
