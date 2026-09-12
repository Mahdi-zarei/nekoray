#include "include/configs/outbounds/openconnect.h"

#include <QJsonArray>
#include <QUrl>
#include <include/global/Utils.hpp>

#include "include/configs/common/utils.h"
#include "include/configs/sub/vpnFileImport.hpp"
#include "include/global/OtpPlaceholder.hpp"
#include "include/global/VpnCredentialOverride.hpp"

namespace Configs {
    namespace {
        // sing-box listables accept a bare string as well as an array of lines.
        QStringList ocListable(const QJsonValue& value)
        {
            if (value.isString()) return value.toString().split("\n", Qt::SkipEmptyParts);
            return QJsonArray2QListString(value.toArray());
        }

        constexpr int kOpenConnectDefaultPort = 443;
    }

    bool OpenConnectToken::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("mode")) mode = object["mode"].toString();
        if (object.contains("secret")) secret = object["secret"].toString();
        if (object.contains("secret_path")) secret_path = object["secret_path"].toString();
        if (object.contains("pin")) pin = object["pin"].toString();
        if (object.contains("password")) password = object["password"].toString();
        if (object.contains("device_id")) device_id = object["device_id"].toString();
        if (object.contains("counter")) counter = object["counter"].toInteger();
        return true;
    }

    QJsonObject OpenConnectToken::ExportToJson()
    {
        QJsonObject object;
        if (mode.isEmpty()) return object;
        object["mode"] = mode;
        if (!secret.isEmpty()) object["secret"] = secret;
        if (!secret_path.isEmpty()) object["secret_path"] = secret_path;
        if (!pin.isEmpty()) object["pin"] = pin;
        if (!password.isEmpty()) object["password"] = password;
        if (!device_id.isEmpty()) object["device_id"] = device_id;
        if (counter > 0) object["counter"] = counter;
        return object;
    }

    BuildResult OpenConnectToken::Build()
    {
        return {ExportToJson(), ""};
    }

    bool OpenConnectMobile::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("platform_version")) platform_version = object["platform_version"].toString();
        if (object.contains("device_type")) device_type = object["device_type"].toString();
        if (object.contains("device_unique_id")) device_unique_id = object["device_unique_id"].toString();
        return true;
    }

    QJsonObject OpenConnectMobile::ExportToJson()
    {
        QJsonObject object;
        if (platform_version.isEmpty() && device_type.isEmpty() && device_unique_id.isEmpty()) return object;
        object["platform_version"] = platform_version;
        object["device_type"] = device_type;
        object["device_unique_id"] = device_unique_id;
        return object;
    }

    BuildResult OpenConnectMobile::Build()
    {
        return {ExportToJson(), ""};
    }

    bool OpenConnectWrapper::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("wrapper_path")) wrapper_path = object["wrapper_path"].toString();
        return true;
    }

    QJsonObject OpenConnectWrapper::ExportToJson()
    {
        QJsonObject object;
        if (!wrapper_path.isEmpty()) object["wrapper_path"] = wrapper_path;
        return object;
    }

    BuildResult OpenConnectWrapper::Build()
    {
        return {ExportToJson(), ""};
    }

    bool OpenConnectTNCCCertificate::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("certificate")) certificate = ocListable(object["certificate"]);
        if (object.contains("certificate_path")) certificate_path = object["certificate_path"].toString();
        return true;
    }

    QJsonObject OpenConnectTNCCCertificate::ExportToJson()
    {
        QJsonObject object;
        if (!certificate.isEmpty()) object["certificate"] = QListStr2QJsonArray(certificate);
        if (!certificate_path.isEmpty()) object["certificate_path"] = certificate_path;
        return object;
    }

    BuildResult OpenConnectTNCCCertificate::Build()
    {
        return {ExportToJson(), ""};
    }

    bool OpenConnectTNCC::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("wrapper_path")) wrapper_path = object["wrapper_path"].toString();
        if (object.contains("device_id")) device_id = object["device_id"].toString();
        if (object.contains("user_agent")) user_agent = object["user_agent"].toString();
        if (object.contains("machine_identification_enabled")) machine_identification_enabled = object["machine_identification_enabled"].toBool();
        if (object.contains("certificates")) {
            certificates.clear();
            for (const auto& item : object["certificates"].toArray()) {
                auto cert = std::make_shared<OpenConnectTNCCCertificate>();
                if (cert->ParseFromJson(item.toObject())) certificates.append(cert);
            }
        }
        return true;
    }

    QJsonObject OpenConnectTNCC::ExportToJson()
    {
        QJsonObject object;
        if (!wrapper_path.isEmpty()) object["wrapper_path"] = wrapper_path;
        if (!device_id.isEmpty()) object["device_id"] = device_id;
        if (!user_agent.isEmpty()) object["user_agent"] = user_agent;
        if (machine_identification_enabled) object["machine_identification_enabled"] = true;
        if (!certificates.isEmpty()) {
            QJsonArray certs;
            for (const auto& cert : certificates) {
                if (auto item = cert->ExportToJson(); !item.isEmpty()) certs.append(item);
            }
            if (!certs.isEmpty()) object["certificates"] = certs;
        }
        return object;
    }

    BuildResult OpenConnectTNCC::Build()
    {
        return {ExportToJson(), ""};
    }

    bool OpenConnectFortinetHostCheck::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("hostcheck")) hostcheck = object["hostcheck"].toString();
        if (object.contains("check_virtual_desktop")) check_virtual_desktop = object["check_virtual_desktop"].toString();
        return true;
    }

    QJsonObject OpenConnectFortinetHostCheck::ExportToJson()
    {
        QJsonObject object;
        // An empty hostcheck disables the feature, so the rest is meaningless alone.
        if (hostcheck.isEmpty()) return object;
        object["hostcheck"] = hostcheck;
        if (!check_virtual_desktop.isEmpty()) object["check_virtual_desktop"] = check_virtual_desktop;
        return object;
    }

    BuildResult OpenConnectFortinetHostCheck::Build()
    {
        return {ExportToJson(), ""};
    }

    bool OpenConnectFormEntry::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("form_id")) form_id = object["form_id"].toString();
        if (object.contains("submission_key")) submission_key = object["submission_key"].toString();
        if (object.contains("name")) name = object["name"].toString();
        if (object.contains("value")) value = object["value"].toString();
        if (object.contains("promote")) promote = object["promote"].toBool();
        return true;
    }

    QJsonObject OpenConnectFormEntry::ExportToJson()
    {
        QJsonObject object;
        if (submission_key.isEmpty() && (form_id.isEmpty() || name.isEmpty())) return object;
        if (!form_id.isEmpty()) object["form_id"] = form_id;
        if (!submission_key.isEmpty()) object["submission_key"] = submission_key;
        if (!name.isEmpty()) object["name"] = name;
        if (!value.isEmpty()) object["value"] = value;
        if (promote) object["promote"] = true;
        return object;
    }

    BuildResult OpenConnectFormEntry::Build()
    {
        return {ExportToJson(), ""};
    }

    bool OpenConnectTLS::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("insecure")) insecure = object["insecure"].toBool();
        if (object.contains("server_name")) server_name = object["server_name"].toString();
        if (object.contains("peer_fingerprint")) peer_fingerprint = ocListable(object["peer_fingerprint"]);
        if (object.contains("system_trust_disabled")) system_trust_disabled = object["system_trust_disabled"].toBool();
        if (object.contains("certificate_authority")) certificate_authority = ocListable(object["certificate_authority"]);
        if (object.contains("certificate_authority_path")) certificate_authority_path = object["certificate_authority_path"].toString();
        if (object.contains("client_certificate")) client_certificate = ocListable(object["client_certificate"]);
        if (object.contains("client_certificate_path")) client_certificate_path = object["client_certificate_path"].toString();
        if (object.contains("client_key")) client_key = ocListable(object["client_key"]);
        if (object.contains("client_key_path")) client_key_path = object["client_key_path"].toString();
        if (object.contains("client_key_password")) client_key_password = object["client_key_password"].toString();
        if (object.contains("mca_certificate")) mca_certificate = ocListable(object["mca_certificate"]);
        if (object.contains("mca_certificate_path")) mca_certificate_path = object["mca_certificate_path"].toString();
        if (object.contains("mca_key")) mca_key = ocListable(object["mca_key"]);
        if (object.contains("mca_key_path")) mca_key_path = object["mca_key_path"].toString();
        if (object.contains("mca_key_password")) mca_key_password = object["mca_key_password"].toString();
        return true;
    }

    QJsonObject OpenConnectTLS::ExportToJson()
    {
        QJsonObject object;
        if (insecure) object["insecure"] = true;
        if (!server_name.isEmpty()) object["server_name"] = server_name;
        if (!peer_fingerprint.isEmpty()) object["peer_fingerprint"] = QListStr2QJsonArray(peer_fingerprint);
        if (system_trust_disabled) object["system_trust_disabled"] = true;
        if (!certificate_authority.isEmpty()) object["certificate_authority"] = QListStr2QJsonArray(certificate_authority);
        if (!certificate_authority_path.isEmpty()) object["certificate_authority_path"] = certificate_authority_path;
        if (!client_certificate.isEmpty()) object["client_certificate"] = QListStr2QJsonArray(client_certificate);
        if (!client_certificate_path.isEmpty()) object["client_certificate_path"] = client_certificate_path;
        if (!client_key.isEmpty()) object["client_key"] = QListStr2QJsonArray(client_key);
        if (!client_key_path.isEmpty()) object["client_key_path"] = client_key_path;
        if (!client_key_password.isEmpty()) object["client_key_password"] = client_key_password;
        if (!mca_certificate.isEmpty()) object["mca_certificate"] = QListStr2QJsonArray(mca_certificate);
        if (!mca_certificate_path.isEmpty()) object["mca_certificate_path"] = mca_certificate_path;
        if (!mca_key.isEmpty()) object["mca_key"] = QListStr2QJsonArray(mca_key);
        if (!mca_key_path.isEmpty()) object["mca_key_path"] = mca_key_path;
        if (!mca_key_password.isEmpty()) object["mca_key_password"] = mca_key_password;
        return object;
    }

    BuildResult OpenConnectTLS::Build()
    {
        return {ExportToJson(), ""};
    }

    bool openconnect::ParseFromLink(const QString& link)
    {
        return ParseOpenConnectProfile(link, *this);
    }

    bool openconnect::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty() || object["type"].toString() != "openconnect") return false;
        outbound::ParseFromJson(object);
        if (object.contains("server")) SetServerUrl(object["server"].toString());
        if (object.contains("server_path")) server_path = object["server_path"].toString();

        if (object.contains("flavor")) flavor = object["flavor"].toString();
        if (object.contains("username")) username = object["username"].toString();
        if (object.contains("password")) password = object["password"].toString();
        if (object.contains("auth_group")) auth_group = object["auth_group"].toString();
        if (object.contains("cookie")) cookie = object["cookie"].toString();
        if (object.contains("token")) token->ParseFromJson(object["token"].toObject());
        if (object.contains("reported_os")) reported_os = object["reported_os"].toString();
        if (object.contains("user_agent")) user_agent = object["user_agent"].toString();
        if (object.contains("version")) version = object["version"].toString();
        if (object.contains("local_hostname")) local_hostname = object["local_hostname"].toString();
        if (object.contains("mobile")) mobile->ParseFromJson(object["mobile"].toObject());
        if (object.contains("csd")) csd->ParseFromJson(object["csd"].toObject());
        if (object.contains("hip")) hip->ParseFromJson(object["hip"].toObject());
        if (object.contains("tncc")) tncc->ParseFromJson(object["tncc"].toObject());
        if (object.contains("fortinet_host_check")) fortinet_host_check->ParseFromJson(object["fortinet_host_check"].toObject());
        if (object.contains("no_udp")) no_udp = object["no_udp"].toBool();
        if (object.contains("dtls_local_port")) dtls_local_port = object["dtls_local_port"].toInt();
        if (object.contains("compression_disabled")) compression_disabled = object["compression_disabled"].toBool();
        if (object.contains("compression_mode")) compression_mode = object["compression_mode"].toString();
        if (object.contains("ipv6_disabled")) ipv6_disabled = object["ipv6_disabled"].toBool();
        if (object.contains("http_keepalive_disabled")) http_keepalive_disabled = object["http_keepalive_disabled"].toBool();
        if (object.contains("xml_post_disabled")) xml_post_disabled = object["xml_post_disabled"].toBool();
        if (object.contains("external_auth_disabled")) external_auth_disabled = object["external_auth_disabled"].toBool();
        if (object.contains("password_authentication_disabled")) password_authentication_disabled = object["password_authentication_disabled"].toBool();
        if (object.contains("tcp_keep_alive_enabled")) tcp_keep_alive_enabled = object["tcp_keep_alive_enabled"].toBool();
        if (object.contains("pfs")) pfs = object["pfs"].toBool();
        if (object.contains("mtu")) mtu = object["mtu"].toInt();
        if (object.contains("base_mtu")) base_mtu = object["base_mtu"].toInt();
        if (object.contains("dpd_interval")) dpd_interval = object["dpd_interval"].toString();
        if (object.contains("reconnect_timeout")) reconnect_timeout = object["reconnect_timeout"].toString();
        if (object.contains("trojan_interval")) trojan_interval = object["trojan_interval"].toString();
        if (object.contains("queue_length")) queue_length = object["queue_length"].toInt();
        if (object.contains("allow_insecure_crypto")) allow_insecure_crypto = object["allow_insecure_crypto"].toBool();
        if (object.contains("tls")) tls->ParseFromJson(object["tls"].toObject());
        if (object.contains("form_entries")) {
            form_entries.clear();
            for (const auto& item : object["form_entries"].toArray()) {
                auto entry = std::make_shared<OpenConnectFormEntry>();
                if (entry->ParseFromJson(item.toObject())) form_entries.append(entry);
            }
        }
        if (object.contains("system")) system = object["system"].toBool();
        if (object.contains("name")) interface_name = object["name"].toString();
        if (object.contains("udp_timeout")) udp_timeout = object["udp_timeout"].toString();
        if (object.contains("udp_mapping")) udp_mapping = object["udp_mapping"].toString();
        if (object.contains("udp_filtering")) udp_filtering = object["udp_filtering"].toString();
        if (object.contains("udp_nat_max")) udp_nat_max = object["udp_nat_max"].toInt();

        if (object.contains("otp_profile_id")) otp_profile_id = object["otp_profile_id"].toInt();
        if (object.contains("only_advertised_routes")) only_advertised_routes = object["only_advertised_routes"].toBool();
        // Profiles saved before the mode existed carry the two checkboxes it replaced.
        if (object.contains("tunnel_dns")) tunnel_dns = object["tunnel_dns"].toString();
        else if (object["block_outside_dns"].toBool()) tunnel_dns = kTunnelDnsStrict;
        else if (object.contains("use_tunnel_dns") && !object["use_tunnel_dns"].toBool()) tunnel_dns = kTunnelDnsNone;
        return true;
    }

    QJsonObject openconnect::ExportToJson()
    {
        QJsonObject object;
        object["type"] = "openconnect";
        mergeJsonObjects(object, outbound::ExportToJson());
        if (!server_path.isEmpty()) object["server_path"] = server_path;

        if (!flavor.isEmpty()) object["flavor"] = flavor;
        if (!username.isEmpty()) object["username"] = username;
        if (!password.isEmpty()) object["password"] = password;
        if (!auth_group.isEmpty()) object["auth_group"] = auth_group;
        if (!cookie.isEmpty()) object["cookie"] = cookie;
        if (auto tokenObj = token->ExportToJson(); !tokenObj.isEmpty()) object["token"] = tokenObj;
        if (!reported_os.isEmpty()) object["reported_os"] = reported_os;
        if (!user_agent.isEmpty()) object["user_agent"] = user_agent;
        if (!version.isEmpty()) object["version"] = version;
        if (!local_hostname.isEmpty()) object["local_hostname"] = local_hostname;
        if (auto mobileObj = mobile->ExportToJson(); !mobileObj.isEmpty()) object["mobile"] = mobileObj;
        if (auto csdObj = csd->ExportToJson(); !csdObj.isEmpty()) object["csd"] = csdObj;
        if (auto hipObj = hip->ExportToJson(); !hipObj.isEmpty()) object["hip"] = hipObj;
        if (auto tnccObj = tncc->ExportToJson(); !tnccObj.isEmpty()) object["tncc"] = tnccObj;
        if (auto fortinetObj = fortinet_host_check->ExportToJson(); !fortinetObj.isEmpty()) object["fortinet_host_check"] = fortinetObj;
        if (no_udp) object["no_udp"] = true;
        if (dtls_local_port > 0) object["dtls_local_port"] = dtls_local_port;
        if (compression_disabled) object["compression_disabled"] = true;
        if (!compression_mode.isEmpty()) object["compression_mode"] = compression_mode;
        if (ipv6_disabled) object["ipv6_disabled"] = true;
        if (http_keepalive_disabled) object["http_keepalive_disabled"] = true;
        if (xml_post_disabled) object["xml_post_disabled"] = true;
        if (external_auth_disabled) object["external_auth_disabled"] = true;
        if (password_authentication_disabled) object["password_authentication_disabled"] = true;
        if (tcp_keep_alive_enabled) object["tcp_keep_alive_enabled"] = true;
        if (pfs) object["pfs"] = true;
        if (mtu > 0) object["mtu"] = mtu;
        if (base_mtu > 0) object["base_mtu"] = base_mtu;
        if (!dpd_interval.isEmpty()) object["dpd_interval"] = dpd_interval;
        if (!reconnect_timeout.isEmpty()) object["reconnect_timeout"] = reconnect_timeout;
        if (!trojan_interval.isEmpty()) object["trojan_interval"] = trojan_interval;
        if (queue_length > 0) object["queue_length"] = queue_length;
        if (allow_insecure_crypto) object["allow_insecure_crypto"] = true;
        if (auto tlsObj = tls->ExportToJson(); !tlsObj.isEmpty()) object["tls"] = tlsObj;
        if (!form_entries.isEmpty()) {
            QJsonArray entries;
            for (const auto& entry : form_entries) {
                if (auto item = entry->ExportToJson(); !item.isEmpty()) entries.append(item);
            }
            if (!entries.isEmpty()) object["form_entries"] = entries;
        }
        if (system) object["system"] = true;
        if (!interface_name.isEmpty()) object["name"] = interface_name;
        if (!udp_timeout.isEmpty()) object["udp_timeout"] = udp_timeout;
        if (!udp_mapping.isEmpty()) object["udp_mapping"] = udp_mapping;
        if (!udp_filtering.isEmpty()) object["udp_filtering"] = udp_filtering;
        if (udp_nat_max > 0) object["udp_nat_max"] = udp_nat_max;

        if (otp_profile_id >= 0) object["otp_profile_id"] = otp_profile_id;
        object["only_advertised_routes"] = only_advertised_routes;
        object["tunnel_dns"] = tunnel_dns;
        return object;
    }

    BuildResult openconnect::Build()
    {
        QJsonObject object;
        object["type"] = "openconnect";
        if (!name.isEmpty()) object["tag"] = name;
        mergeJsonObjects(object, dialFields->Build().object);
        if (auto url = ComposeServer(); !url.isEmpty()) object["server"] = url;

        const auto creds = ResolveVpnCredentials(profile_id, username, password);
        auto otpCode = ResolveOtpCode(otp_profile_id);
        if (auto user = SubstituteOtp(creds.username, otpCode); !user.isEmpty()) object["username"] = user;
        if (auto pass = SubstituteOtp(creds.password, otpCode); !pass.isEmpty()) object["password"] = pass;

        if (!flavor.isEmpty()) object["flavor"] = flavor;
        if (!auth_group.isEmpty()) object["auth_group"] = auth_group;
        if (!cookie.isEmpty()) object["cookie"] = cookie;
        if (auto tokenObj = token->Build().object; !tokenObj.isEmpty()) {
            if (tokenObj.contains("pin")) tokenObj["pin"] = SubstituteOtp(tokenObj["pin"].toString(), otpCode);
            if (tokenObj.contains("password")) tokenObj["password"] = SubstituteOtp(tokenObj["password"].toString(), otpCode);
            object["token"] = tokenObj;
        }
        if (!reported_os.isEmpty()) object["reported_os"] = reported_os;
        if (!user_agent.isEmpty()) object["user_agent"] = user_agent;
        if (!version.isEmpty()) object["version"] = version;
        if (!local_hostname.isEmpty()) object["local_hostname"] = local_hostname;
        if (auto mobileObj = mobile->Build().object; !mobileObj.isEmpty()) object["mobile"] = mobileObj;
        if (auto csdObj = csd->Build().object; !csdObj.isEmpty()) object["csd"] = csdObj;
        if (auto hipObj = hip->Build().object; !hipObj.isEmpty()) object["hip"] = hipObj;
        if (auto tnccObj = tncc->Build().object; !tnccObj.isEmpty()) object["tncc"] = tnccObj;
        if (auto fortinetObj = fortinet_host_check->Build().object; !fortinetObj.isEmpty()) object["fortinet_host_check"] = fortinetObj;
        if (no_udp) object["no_udp"] = true;
        if (dtls_local_port > 0) object["dtls_local_port"] = dtls_local_port;
        if (compression_disabled) object["compression_disabled"] = true;
        if (!compression_mode.isEmpty()) object["compression_mode"] = compression_mode;
        if (ipv6_disabled) object["ipv6_disabled"] = true;
        if (http_keepalive_disabled) object["http_keepalive_disabled"] = true;
        if (xml_post_disabled) object["xml_post_disabled"] = true;
        if (external_auth_disabled) object["external_auth_disabled"] = true;
        if (password_authentication_disabled) object["password_authentication_disabled"] = true;
        if (tcp_keep_alive_enabled) object["tcp_keep_alive_enabled"] = true;
        if (pfs) object["pfs"] = true;
        if (mtu > 0) object["mtu"] = mtu;
        if (base_mtu > 0) object["base_mtu"] = base_mtu;
        if (!dpd_interval.isEmpty()) object["dpd_interval"] = dpd_interval;
        if (!reconnect_timeout.isEmpty()) object["reconnect_timeout"] = reconnect_timeout;
        if (!trojan_interval.isEmpty()) object["trojan_interval"] = trojan_interval;
        if (queue_length > 0) object["queue_length"] = queue_length;
        if (allow_insecure_crypto) object["allow_insecure_crypto"] = true;
        if (auto tlsObj = tls->Build().object; !tlsObj.isEmpty()) object["tls"] = tlsObj;
        if (!form_entries.isEmpty()) {
            QJsonArray entries;
            for (const auto& entry : form_entries) {
                auto item = entry->Build().object;
                if (item.isEmpty()) continue;
                const auto rawValue = item["value"].toString();
                // A form entry is never re-asked, so a baked code would be replayed forever;
                // withholding it turns the field into a challenge the poller answers live.
                if (otp_profile_id >= 0 && !BuildingTestConfig() && rawValue.contains(kOtpPlaceholder)) continue;
                if (item.contains("value")) item["value"] = SubstituteOtp(rawValue, otpCode);
                entries.append(item);
            }
            if (!entries.isEmpty()) object["form_entries"] = entries;
        }
        if (system) object["system"] = true;
        if (!interface_name.isEmpty()) object["name"] = interface_name;
        if (!udp_timeout.isEmpty()) object["udp_timeout"] = udp_timeout;
        if (!udp_mapping.isEmpty()) object["udp_mapping"] = udp_mapping;
        if (!udp_filtering.isEmpty()) object["udp_filtering"] = udp_filtering;
        if (udp_nat_max > 0) object["udp_nat_max"] = udp_nat_max;
        return {object, ""};
    }

    QString openconnect::ComposeServer()
    {
        if (server.isEmpty()) return {};
        auto host = server;
        auto url = WrapIPV6Host(host);
        if (server_port > 0 && server_port != kOpenConnectDefaultPort) url += ":" + Int2String(server_port);
        if (!server_path.isEmpty()) {
            url += server_path.startsWith("/") ? server_path : "/" + server_path;
        }
        return url;
    }

    void openconnect::SetServerUrl(const QString& url)
    {
        auto text = url.trimmed();
        if (text.isEmpty()) return;
        if (!text.contains("://")) {
            // A bare IPv6 literal has to be bracketed before QUrl will parse it.
            auto host = text;
            text = WrapIPV6Host(host);
            text.prepend("https://");
        }
        auto parsed = QUrl(text);
        if (!parsed.isValid() || parsed.host().isEmpty()) return;
        server = parsed.host();
        if (parsed.port() > 0) server_port = parsed.port();
        server_path = parsed.path();
        if (server_path == "/") server_path.clear();
    }

    QString openconnect::DisplayAddress()
    {
        return ComposeServer();
    }

    QString openconnect::DisplayType()
    {
        return "OpenConnect";
    }

    SecurityInfo openconnect::GetSecurity()
    {
        if (tls->insecure) return {QObject::tr("Insecure TLS"), {}, SecurityLevel::Weak};
        return {QObject::tr("TLS"), {}, SecurityLevel::Secure};
    }

    bool openconnect::IsEndpoint()
    {
        return true;
    }

    bool openconnect::SupportsCredentialStrip() const
    {
        return true;
    }

    void openconnect::StripCredentials()
    {
        username.clear();
        password.clear();
        cookie.clear();
        otp_profile_id = -1;
        // Every field of the soft-token object is secret-bearing, so drop the whole object.
        token = std::make_shared<OpenConnectToken>();
        tls->client_key.clear();
        tls->client_key_path.clear();
        tls->client_key_password.clear();
        tls->mca_key.clear();
        tls->mca_key_path.clear();
        tls->mca_key_password.clear();
        for (const auto& entry : form_entries) entry->value.clear();
        // Absolute local paths: meaningless on another machine and they carry the OS user name.
        tls->certificate_authority_path.clear();
        tls->client_certificate_path.clear();
        tls->mca_certificate_path.clear();
        token->secret_path.clear();
        csd->wrapper_path.clear();
        hip->wrapper_path.clear();
        tncc->wrapper_path.clear();
        for (const auto& cert : tncc->certificates) cert->certificate_path.clear();
    }
}
