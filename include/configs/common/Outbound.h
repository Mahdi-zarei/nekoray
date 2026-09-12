#pragma once
#include <QHostInfo>
#include <utility>
#include "DialFields.h"
#include "QUICFields.h"
#include "multiplex.h"
#include "TLS.h"
#include "transport.h"
#include "xrayMultiplex.h"
#include "xrayStreamSetting.h"
#include "include/configs/baseConfig.h"

namespace Configs
{
    inline QStringList vPacketEncoding = {"", "packetaddr", "xudp"};

    // openvpn/openconnect main profile: ignore the pushed resolvers, use them for the names they claim, or also for every remote query.
    inline constexpr auto kTunnelDnsNone = "none";
    inline constexpr auto kTunnelDnsPrefer = "prefer";
    inline constexpr auto kTunnelDnsStrict = "strict";

    // Ordered worst-to-best; the ordering is relied on when sorting by security.
    enum class SecurityLevel {
        Unknown = 0,
        None,
        Weak,
        Secure,
    };

    struct SecurityInfo {
        QString label;
        QString transport;
        SecurityLevel level = SecurityLevel::Unknown;

        bool isDangerous() const {
            return level == SecurityLevel::None || level == SecurityLevel::Weak;
        }
    };

    // Empty for the transports not worth showing (plain tcp / xray raw).
    QString DisplayTransportName(const QString& type);

    class outbound : public baseConfig
    {
    public:
        QString name;
        QString server;
        int server_port = 0;
        bool invalid = false;
        // Owning profile, stamped by ProfilesRepo; not part of the serialised outbound.
        int profile_id = -1;
        std::shared_ptr<DialFields> dialFields = std::make_shared<DialFields>();

        void ResolveDomainToIP(const std::function<void()> &onFinished) {
            bool noResolve = false;
            auto serverAddr = GetAddress();
            if (IsIpAddress(serverAddr) || serverAddr.isEmpty()) noResolve = true;
            if (noResolve) {
                onFinished();
                return;
            }
            QHostInfo::lookupHost(serverAddr, QApplication::instance(), [=, this](const QHostInfo &host) {
                auto addrs = host.addresses();
                if (!addrs.isEmpty()) SetAddress(addrs.first().toString());
                onFinished();
            });
        }

        virtual void SetAddress(QString newAddr) {
            server = std::move(newAddr);
        }

        virtual QString GetAddress()
        {
            return server;
        }

        virtual void SetPort(int newPort) {
            server_port = newPort;
        }

        virtual QString GetPort() {
            return QString::number(server_port);
        }

        virtual QString DisplayAddress()
        {
            return ::DisplayAddress(server, server_port);
        }

        virtual QString DisplayName()
        {
            if (name.isEmpty()) {
                return DisplayAddress();
            }
            return name;
        }

        virtual QString DisplayType() { return {}; };

        QString DisplayTypeAndName()
        {
            return QString("[%1] %2").arg(DisplayType(), DisplayName());
        }

        virtual SecurityInfo GetSecurity();

        QString DisplaySecurity();

    protected:
        SecurityInfo SecurityFromTLS(const QString& transport);

    public:

        virtual bool IsXray() { return false; }

        virtual bool IsExtraCore() { return false; }

        virtual bool IsXrayFullConfig() { return false; }

        virtual bool HasMux() { return false; }

        virtual bool HasTransport() { return false; }

        virtual bool HasTLS() { return false; }

        virtual bool MustTLS() { return false; }

        virtual bool LimitedTLS() { return false; }

        virtual bool HasQUIC() { return false; }

        virtual std::shared_ptr<TLS> GetTLS() { return std::make_shared<TLS>(); }

        virtual std::shared_ptr<QUICFields> GetQUIC() { return std::make_shared<QUICFields>(); }

        virtual std::shared_ptr<Transport> GetTransport() { return std::make_shared<Transport>(); }

        virtual std::shared_ptr<Multiplex> GetMux() { return std::make_shared<Multiplex>(); }

        virtual std::shared_ptr<xrayStreamSetting> GetXrayStream() { return std::make_shared<xrayStreamSetting>(); }

        virtual std::shared_ptr<xrayMultiplex> GetXrayMultiplex() { return std::make_shared<xrayMultiplex>(); }

        virtual bool IsEndpoint() { return false; };

        virtual bool SupportsCredentialStrip() const { return false; }

        virtual void StripCredentials() {}

        virtual BuildResult BuildXray() { return {}; }

        QString ExportJsonLink(bool stripMetadata = false) {
            auto json = ExportToJson();
            if (stripMetadata) {
                json.remove("tag");
            }
            const auto b64 = QJsonObject2QString(json, true)
                                .toUtf8()
                                .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
            return QStringLiteral("throne://add/") + QString::fromLatin1(b64);
        }

        bool ParseFromLink(const QString& link) override;
        bool ParseFromJson(const QJsonObject& object) override;
        bool ParseFromClash(const clash::Proxies& object) override;
        QString ExportToLink() override;
        QJsonObject ExportToJson() override;
        QJsonObject ExportIdentity() override;
        BuildResult Build() override;
    };
}
