#pragma once
#include "include/configs/common/Outbound.h"

namespace Configs
{
    class OpenConnectToken : public baseConfig
    {
        public:
        QString mode;
        QString secret;
        QString secret_path;
        QString pin;
        QString password;
        QString device_id;
        qint64 counter = 0;

        bool ParseFromJson(const QJsonObject& object) override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
    };

    class OpenConnectMobile : public baseConfig
    {
        public:
        QString platform_version;
        QString device_type;
        QString device_unique_id;

        bool ParseFromJson(const QJsonObject& object) override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
    };

    // Shared shape of the `csd` and `hip` objects.
    class OpenConnectWrapper : public baseConfig
    {
        public:
        QString wrapper_path;

        bool ParseFromJson(const QJsonObject& object) override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
    };

    class OpenConnectTNCCCertificate : public baseConfig
    {
        public:
        QStringList certificate;
        QString certificate_path;

        bool ParseFromJson(const QJsonObject& object) override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
    };

    class OpenConnectTNCC : public baseConfig
    {
        public:
        // wrapper_path conflicts with every other field here.
        QString wrapper_path;
        QString device_id;
        QString user_agent;
        bool machine_identification_enabled = false;
        QList<std::shared_ptr<OpenConnectTNCCCertificate>> certificates;

        bool ParseFromJson(const QJsonObject& object) override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
    };

    class OpenConnectFortinetHostCheck : public baseConfig
    {
        public:
        QString hostcheck;
        QString check_virtual_desktop;

        bool ParseFromJson(const QJsonObject& object) override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
    };

    class OpenConnectFormEntry : public baseConfig
    {
        public:
        QString form_id;
        QString submission_key;
        QString name;
        QString value;
        bool promote = false;

        bool ParseFromJson(const QJsonObject& object) override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
    };

    class OpenConnectTLS : public baseConfig
    {
        public:
        bool insecure = false;
        QString server_name;
        QStringList certificate_authority;
        QStringList client_certificate;
        QStringList client_key;
        QString client_key_password;

        // Each *_path conflicts with its inline value.
        QStringList peer_fingerprint;
        bool system_trust_disabled = false;
        QString certificate_authority_path;
        QString client_certificate_path;
        QString client_key_path;
        QStringList mca_certificate;
        QString mca_certificate_path;
        QStringList mca_key;
        QString mca_key_path;
        QString mca_key_password;

        bool ParseFromJson(const QJsonObject& object) override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;
    };

    class openconnect : public outbound
    {
        public:
        QString flavor;
        QString username;
        QString password;
        QString auth_group;
        QString server_path;
        int mtu = 0;
        std::shared_ptr<OpenConnectTLS> tls = std::make_shared<OpenConnectTLS>();
        // Throne-only, never sent to the core.
        int otp_profile_id = -1;
        bool only_advertised_routes = true;
        QString tunnel_dns = kTunnelDnsPrefer;

        QString cookie;
        std::shared_ptr<OpenConnectToken> token = std::make_shared<OpenConnectToken>();
        QString reported_os;
        QString user_agent;
        QString version;
        QString local_hostname;
        std::shared_ptr<OpenConnectMobile> mobile = std::make_shared<OpenConnectMobile>();
        std::shared_ptr<OpenConnectWrapper> csd = std::make_shared<OpenConnectWrapper>();
        std::shared_ptr<OpenConnectWrapper> hip = std::make_shared<OpenConnectWrapper>();
        std::shared_ptr<OpenConnectTNCC> tncc = std::make_shared<OpenConnectTNCC>();
        std::shared_ptr<OpenConnectFortinetHostCheck> fortinet_host_check = std::make_shared<OpenConnectFortinetHostCheck>();
        QList<std::shared_ptr<OpenConnectFormEntry>> form_entries;
        bool no_udp = false;
        int dtls_local_port = 0;
        bool compression_disabled = false;
        QString compression_mode;
        bool ipv6_disabled = false;
        bool http_keepalive_disabled = false;
        bool xml_post_disabled = false;
        bool external_auth_disabled = false;
        bool password_authentication_disabled = false;
        bool tcp_keep_alive_enabled = false;
        bool pfs = false;
        int base_mtu = 0;
        QString dpd_interval;
        QString reconnect_timeout;
        QString trojan_interval;
        int queue_length = 0;
        bool allow_insecure_crypto = false;
        bool system = false;
        // sing-box "name" (system interface name); outbound::name is the tag.
        QString interface_name;
        QString udp_timeout;
        QString udp_mapping;
        QString udp_filtering;
        int udp_nat_max = 0;

        bool ParseFromLink(const QString& link) override;
        bool ParseFromJson(const QJsonObject& object) override;
        QJsonObject ExportToJson() override;
        BuildResult Build() override;

        QString DisplayAddress() override;
        QString DisplayType() override;
        SecurityInfo GetSecurity() override;
        bool IsEndpoint() override;
        bool SupportsCredentialStrip() const override;
        void StripCredentials() override;

        // The core takes a single HTTPS URL instead of server + server_port.
        QString ComposeServer();
        void SetServerUrl(const QString& url);
    };
}
