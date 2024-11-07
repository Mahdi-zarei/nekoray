#pragma once
#include <QJsonObject>

#include "include/global.h"
#include "include/common/json/qt_to_stl.h"
#include <nlohmann/json.hpp>

#include <QList>
#include <qmap.h>
#include <QString>

#include "include/common/json/stl_to_qt.h"

using json = nlohmann::json;

namespace Proxy
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
        QString muxEnableMode = Constants::MuxEnableModes::MuxDefault;
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

        // options
        QString domainStrategy;
        bool tcpFastOpen = false;

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

        virtual QString getExportLink()
        {
            return "";
        }

        virtual QJsonObject buildProxy(QString const& tag)
        {
            return {};
        }
    };

    inline void to_json(json& j, const BaseProxy& prx)
    {
        j = json{
            {"id", prx.id.toStdString()},
            {"name", prx.name.toStdString()},
            {"group_id", prx.groupID.toStdString()},
            {"type", prx.type.toStdString()},
            {"is_full_config", prx.isFullConfig},
            {"full_config", prx.fullConfig.toStdString()},
            {"server_addr", prx.serverAddr.toStdString()},
            {"server_port", prx.serverPort},
            {"mux_enable_mode", prx.muxEnableMode.toStdString()},
            {"mux_type", prx.muxType.toStdString()},
            {"mux_min_streams", prx.muxMinStreams},
            {"mux_max_conns", prx.muxMaxConns},
            {"use_brutal", prx.useBrutal},
            {"brutal_speed", prx.brutalSpeed},
            {"use_transport", prx.useTransport},
            {"transport_type", prx.transportType.toStdString()},
            {"host", Common::QStringList2Vector(prx.host)},
            {"path", prx.path.toStdString()},
            {"method", prx.method.toStdString()},
            {"headers", Common::QMapStrStr2Map(prx.headers)},
            {"max_early_data", prx.maxEarlyData},
            {"early_data_header", prx.earlyDataHeader.toStdString()},
            {"service_name", prx.serviceName.toStdString()},
            {"use_tls", prx.useTLS},
            {"tls_type", prx.tlsType.toStdString()},
            {"server_name", prx.serverName.toStdString()},
            {"alpn", Common::QStringList2Vector(prx.alpn)},
            {"public_key", prx.publicKey.toStdString()},
            {"short_key", prx.shortKey.toStdString()},
            {"use_utls", prx.useUTLS},
            {"finger_print", prx.fingerPrint.toStdString()},
            {"domain_strategy", prx.domainStrategy.toStdString()},
            {"tcp_fast_open", prx.tcpFastOpen},
        };
    }

    inline void from_json(const json& j, BaseProxy& prx)
    {
        prx.id = j.at("id").get<std::string>().c_str();
        prx.name = j.at("name").get<std::string>().c_str();
        prx.groupID = j.at("group_id").get<std::string>().c_str();
        prx.type = j.at("type").get<std::string>().c_str();
        prx.isFullConfig = j.at("is_full_config").get<bool>();
        prx.fullConfig = j.at("full_config").get<std::string>().c_str();
        prx.serverAddr = j.at("server_addr").get<std::string>().c_str();
        prx.serverPort = j.at("server_port").get<int>();
        prx.muxEnableMode = j.at("mux_enable_mode").get<std::string>().c_str();
        prx.muxType = j.at("mux_type").get<std::string>().c_str();
        prx.muxMinStreams = j.at("mux_min_streams").get<int>();
        prx.muxMaxConns = j.at("mux_max_conns").get<int>();
        prx.useBrutal = j.at("use_brutal").get<bool>();
        prx.brutalSpeed = j.at("brutal_speed").get<int>();
        prx.useTransport = j.at("use_transport").get<bool>();
        prx.transportType = j.at("transport_type").get<std::string>().c_str();
        prx.host = Common::Vector2QStringList(j.at("host").get<std::vector<std::string>>());
        prx.path = j.at("path").get<std::string>().c_str();
        prx.method = j.at("method").get<std::string>().c_str();
        prx.headers = Common::Map2QMapQStrQStr(j.at("headers").get<std::map<std::string, std::string>>());
        prx.maxEarlyData = j.at("max_early_data").get<int>();
        prx.earlyDataHeader = j.at("early_data_header").get<std::string>().c_str();
        prx.serviceName = j.at("service_name").get<std::string>().c_str();
        prx.useTLS = j.at("use_tls").get<bool>();
        prx.tlsType = j.at("tls_type").get<std::string>().c_str();
        prx.serverName = j.at("server_name").get<std::string>().c_str();
        prx.alpn = Common::Vector2QStringList(j.at("alpn").get<std::vector<std::string>>());
        prx.publicKey = j.at("public_key").get<std::string>().c_str();
        prx.shortKey = j.at("short_key").get<std::string>().c_str();
        prx.useUTLS = j.at("use_utls").get<bool>();
        prx.fingerPrint = j.at("finger_print").get<std::string>().c_str();
        prx.domainStrategy = j.at("domain_strategy").get<std::string>().c_str();
        prx.tcpFastOpen = j.at("tcp_fast_open").get<bool>();
    }
}
