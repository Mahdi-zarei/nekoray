#include "include/configs/outbounds/openvpn.h"

#include <QJsonArray>
#include <include/global/Utils.hpp>

#include "include/configs/common/utils.h"
#include "include/configs/sub/vpnFileImport.hpp"
#include "include/global/OtpPlaceholder.hpp"
#include "include/global/VpnCredentialOverride.hpp"

namespace Configs {
    namespace {
        // sing-box listables accept a bare string as well as an array of lines.
        QStringList ovpnListable(const QJsonValue& value)
        {
            if (value.isString()) return value.toString().split("\n", Qt::SkipEmptyParts);
            return QJsonArray2QListString(value.toArray());
        }
    }

    bool OpenVPNRemote::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("server")) server = object["server"].toString();
        if (object.contains("server_port")) server_port = object["server_port"].toInt();
        if (object.contains("network")) network = object["network"].toString();
        return true;
    }

    QJsonObject OpenVPNRemote::ExportToJson()
    {
        QJsonObject object;
        if (!server.isEmpty()) object["server"] = server;
        if (server_port > 0) object["server_port"] = server_port;
        if (!network.isEmpty()) object["network"] = network;
        return object;
    }

    BuildResult OpenVPNRemote::Build()
    {
        return {ExportToJson(), ""};
    }

    bool OpenVPNPullFilter::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("action")) action = object["action"].toString();
        if (object.contains("text")) text = object["text"].toString();
        return true;
    }

    QJsonObject OpenVPNPullFilter::ExportToJson()
    {
        QJsonObject object;
        if (action.isEmpty() && text.isEmpty()) return object;
        object["action"] = action;
        object["text"] = text;
        return object;
    }

    BuildResult OpenVPNPullFilter::Build()
    {
        return {ExportToJson(), ""};
    }

    bool OpenVPNControlWrap::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("type")) type = object["type"].toString();
        if (object.contains("key")) key = ovpnListable(object["key"]);
        if (object.contains("key_path")) key_path = object["key_path"].toString();
        if (object.contains("direction")) direction = object["direction"].toString();
        return true;
    }

    QJsonObject OpenVPNControlWrap::ExportToJson()
    {
        QJsonObject object;
        if (type.isEmpty()) return object;
        object["type"] = type;
        if (!key.isEmpty()) object["key"] = QListStr2QJsonArray(key);
        if (!key_path.isEmpty()) object["key_path"] = key_path;
        if (!direction.isEmpty()) object["direction"] = direction;
        return object;
    }

    BuildResult OpenVPNControlWrap::Build()
    {
        return {ExportToJson(), ""};
    }

    bool OpenVPNTLS::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        if (object.contains("server_name")) server_name = object["server_name"].toString();
        if (object.contains("server_name_type")) server_name_type = object["server_name_type"].toString();
        if (object.contains("certificate")) certificate = ovpnListable(object["certificate"]);
        if (object.contains("certificate_path")) certificate_path = object["certificate_path"].toString();
        if (object.contains("client_certificate")) client_certificate = ovpnListable(object["client_certificate"]);
        if (object.contains("client_certificate_path")) client_certificate_path = object["client_certificate_path"].toString();
        if (object.contains("client_key")) client_key = ovpnListable(object["client_key"]);
        if (object.contains("client_key_path")) client_key_path = object["client_key_path"].toString();
        if (object.contains("peer_fingerprint")) peer_fingerprint = ovpnListable(object["peer_fingerprint"]);
        if (object.contains("crl_path")) crl_path = object["crl_path"].toString();
        if (object.contains("remote_certificate_ku")) remote_certificate_ku = ovpnListable(object["remote_certificate_ku"]);
        if (object.contains("remote_certificate_eku")) remote_certificate_eku = object["remote_certificate_eku"].toString();
        if (object.contains("remote_certificate_tls")) remote_certificate_tls = object["remote_certificate_tls"].toString();
        if (object.contains("certificate_profile")) certificate_profile = object["certificate_profile"].toString();
        if (object.contains("ns_certificate_type")) ns_certificate_type = object["ns_certificate_type"].toString();
        if (object.contains("version_min")) version_min = object["version_min"].toString();
        if (object.contains("version_max")) version_max = object["version_max"].toString();
        if (object.contains("cipher")) cipher = object["cipher"].toString();
        if (object.contains("groups")) groups = object["groups"].toString();
        if (object.contains("control_wrap")) control_wrap->ParseFromJson(object["control_wrap"].toObject());
        return true;
    }

    QJsonObject OpenVPNTLS::ExportToJson()
    {
        QJsonObject object;
        if (!server_name.isEmpty()) object["server_name"] = server_name;
        if (!server_name_type.isEmpty()) object["server_name_type"] = server_name_type;
        if (!certificate.isEmpty()) object["certificate"] = QListStr2QJsonArray(certificate);
        if (!certificate_path.isEmpty()) object["certificate_path"] = certificate_path;
        if (!client_certificate.isEmpty()) object["client_certificate"] = QListStr2QJsonArray(client_certificate);
        if (!client_certificate_path.isEmpty()) object["client_certificate_path"] = client_certificate_path;
        if (!client_key.isEmpty()) object["client_key"] = QListStr2QJsonArray(client_key);
        if (!client_key_path.isEmpty()) object["client_key_path"] = client_key_path;
        if (!peer_fingerprint.isEmpty()) object["peer_fingerprint"] = QListStr2QJsonArray(peer_fingerprint);
        if (!crl_path.isEmpty()) object["crl_path"] = crl_path;
        if (!remote_certificate_ku.isEmpty()) object["remote_certificate_ku"] = QListStr2QJsonArray(remote_certificate_ku);
        if (!remote_certificate_eku.isEmpty()) object["remote_certificate_eku"] = remote_certificate_eku;
        if (!remote_certificate_tls.isEmpty()) object["remote_certificate_tls"] = remote_certificate_tls;
        if (!certificate_profile.isEmpty()) object["certificate_profile"] = certificate_profile;
        if (!ns_certificate_type.isEmpty()) object["ns_certificate_type"] = ns_certificate_type;
        if (!version_min.isEmpty()) object["version_min"] = version_min;
        if (!version_max.isEmpty()) object["version_max"] = version_max;
        if (!cipher.isEmpty()) object["cipher"] = cipher;
        if (!groups.isEmpty()) object["groups"] = groups;
        if (auto wrap = control_wrap->ExportToJson(); !wrap.isEmpty()) object["control_wrap"] = wrap;
        return object;
    }

    BuildResult OpenVPNTLS::Build()
    {
        return {ExportToJson(), ""};
    }

    bool openvpn::ParseFromLink(const QString& link)
    {
        return ParseOvpnConfig(link, *this);
    }

    bool openvpn::ParseFromJson(const QJsonObject& object)
    {
        if (object.isEmpty()) return false;
        auto type = object["type"].toString();
        if (type != "openvpn" && type != "openvpn-client") return false;
        outbound::ParseFromJson(object);

        if (object.contains("mode")) mode = object["mode"].toString();
        if (object.contains("network")) network = object["network"].toString();
        if (object.contains("servers")) {
            servers.clear();
            for (const auto& item : object["servers"].toArray()) {
                auto remote = std::make_shared<OpenVPNRemote>();
                if (remote->ParseFromJson(item.toObject())) servers.append(remote);
            }
        }
        if (object.contains("remote_random")) remote_random = object["remote_random"].toBool();
        if (object.contains("address")) address = ovpnListable(object["address"]);
        if (object.contains("peer_address")) peer_address = object["peer_address"].toString();
        if (object.contains("peer_address_ipv6")) peer_address_ipv6 = object["peer_address_ipv6"].toString();
        if (object.contains("topology")) topology = object["topology"].toString();
        if (object.contains("username")) username = object["username"].toString();
        if (object.contains("password")) password = object["password"].toString();
        if (object.contains("auth_retry")) auth_retry = object["auth_retry"].toString();
        if (object.contains("static_challenge")) static_challenge = object["static_challenge"].toString();
        if (object.contains("static_challenge_echo")) static_challenge_echo = object["static_challenge_echo"].toBool();
        if (object.contains("static_key")) static_key = ovpnListable(object["static_key"]);
        if (object.contains("static_key_path")) static_key_path = object["static_key_path"].toString();
        if (object.contains("key_direction")) key_direction = object["key_direction"].toString();
        if (object.contains("tls")) tls->ParseFromJson(object["tls"].toObject());
        if (object.contains("cipher")) cipher = object["cipher"].toString();
        if (object.contains("data_ciphers")) data_ciphers = ovpnListable(object["data_ciphers"]);
        if (object.contains("data_ciphers_fallback")) data_ciphers_fallback = object["data_ciphers_fallback"].toString();
        if (object.contains("auth")) auth = object["auth"].toString();
        if (object.contains("mss_fix")) mss_fix = object["mss_fix"].toInt();
        if (object.contains("mss_fix_disabled")) mss_fix_disabled = object["mss_fix_disabled"].toBool();
        if (object.contains("mss_fix_mode")) mss_fix_mode = object["mss_fix_mode"].toString();
        if (object.contains("fragment")) fragment = object["fragment"].toInt();
        if (object.contains("replay_window")) replay_window = object["replay_window"].toInt();
        if (object.contains("replay_window_time")) replay_window_time = object["replay_window_time"].toString();
        if (object.contains("compression")) compression = object["compression"].toString();
        if (object.contains("compression_lzo")) compression_lzo = object["compression_lzo"].toString();
        if (object.contains("allow_compression")) allow_compression = object["allow_compression"].toString();
        if (object.contains("route_no_pull")) route_no_pull = object["route_no_pull"].toBool();
        if (object.contains("pull_filters")) {
            pull_filters.clear();
            for (const auto& item : object["pull_filters"].toArray()) {
                auto filter = std::make_shared<OpenVPNPullFilter>();
                if (filter->ParseFromJson(item.toObject())) pull_filters.append(filter);
            }
        }
        if (object.contains("routes")) routes = ovpnListable(object["routes"]);
        if (object.contains("route_gateway")) route_gateway = object["route_gateway"].toString();
        if (object.contains("route_metric")) route_metric = object["route_metric"].toInt();
        if (object.contains("redirect_gateway")) redirect_gateway = object["redirect_gateway"].toBool();
        if (object.contains("redirect_gateway_flags")) redirect_gateway_flags = ovpnListable(object["redirect_gateway_flags"]);
        if (object.contains("redirect_private")) redirect_private = object["redirect_private"].toBool();
        if (object.contains("block_ipv6")) block_ipv6 = object["block_ipv6"].toBool();
        if (object.contains("ping_interval")) ping_interval = object["ping_interval"].toString();
        if (object.contains("ping_restart")) ping_restart = object["ping_restart"].toString();
        if (object.contains("ping_restart_disabled")) ping_restart_disabled = object["ping_restart_disabled"].toBool();
        if (object.contains("renegotiate_interval")) renegotiate_interval = object["renegotiate_interval"].toString();
        if (object.contains("renegotiate_disabled")) renegotiate_disabled = object["renegotiate_disabled"].toBool();
        if (object.contains("renegotiate_bytes")) renegotiate_bytes = object["renegotiate_bytes"].toInteger();
        if (object.contains("renegotiate_packets")) renegotiate_packets = object["renegotiate_packets"].toInteger();
        if (object.contains("tls_timeout")) tls_timeout = object["tls_timeout"].toString();
        if (object.contains("handshake_window")) handshake_window = object["handshake_window"].toString();
        if (object.contains("explicit_exit_notify")) explicit_exit_notify = object["explicit_exit_notify"].toInt();
        if (object.contains("system")) system = object["system"].toBool();
        if (object.contains("name")) interface_name = object["name"].toString();
        if (object.contains("mtu")) mtu = object["mtu"].toInt();
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

    QJsonObject openvpn::ExportToJson()
    {
        QJsonObject object;
        object["type"] = "openvpn";
        mergeJsonObjects(object, outbound::ExportToJson());

        if (!mode.isEmpty()) object["mode"] = mode;
        if (!network.isEmpty()) object["network"] = network;
        if (!servers.isEmpty()) {
            QJsonArray remotes;
            for (const auto& remote : servers) {
                if (auto item = remote->ExportToJson(); !item.isEmpty()) remotes.append(item);
            }
            if (!remotes.isEmpty()) object["servers"] = remotes;
        }
        if (remote_random) object["remote_random"] = true;
        if (!address.isEmpty()) object["address"] = QListStr2QJsonArray(address);
        if (!peer_address.isEmpty()) object["peer_address"] = peer_address;
        if (!peer_address_ipv6.isEmpty()) object["peer_address_ipv6"] = peer_address_ipv6;
        if (!topology.isEmpty()) object["topology"] = topology;
        if (!username.isEmpty()) object["username"] = username;
        if (!password.isEmpty()) object["password"] = password;
        if (!auth_retry.isEmpty()) object["auth_retry"] = auth_retry;
        if (!static_challenge.isEmpty()) object["static_challenge"] = static_challenge;
        if (static_challenge_echo) object["static_challenge_echo"] = true;
        if (!static_key.isEmpty()) object["static_key"] = QListStr2QJsonArray(static_key);
        if (!static_key_path.isEmpty()) object["static_key_path"] = static_key_path;
        if (!key_direction.isEmpty()) object["key_direction"] = key_direction;
        if (auto tlsObj = tls->ExportToJson(); !tlsObj.isEmpty()) object["tls"] = tlsObj;
        if (!cipher.isEmpty()) object["cipher"] = cipher;
        if (!data_ciphers.isEmpty()) object["data_ciphers"] = QListStr2QJsonArray(data_ciphers);
        if (!data_ciphers_fallback.isEmpty()) object["data_ciphers_fallback"] = data_ciphers_fallback;
        if (!auth.isEmpty()) object["auth"] = auth;
        if (mss_fix > 0) object["mss_fix"] = mss_fix;
        if (mss_fix_disabled) object["mss_fix_disabled"] = true;
        if (!mss_fix_mode.isEmpty()) object["mss_fix_mode"] = mss_fix_mode;
        if (fragment > 0) object["fragment"] = fragment;
        if (replay_window > 0) object["replay_window"] = replay_window;
        if (!replay_window_time.isEmpty()) object["replay_window_time"] = replay_window_time;
        if (!compression.isEmpty()) object["compression"] = compression;
        if (!compression_lzo.isEmpty()) object["compression_lzo"] = compression_lzo;
        if (!allow_compression.isEmpty()) object["allow_compression"] = allow_compression;
        if (route_no_pull) object["route_no_pull"] = true;
        if (!pull_filters.isEmpty()) {
            QJsonArray filters;
            for (const auto& filter : pull_filters) {
                if (auto item = filter->ExportToJson(); !item.isEmpty()) filters.append(item);
            }
            if (!filters.isEmpty()) object["pull_filters"] = filters;
        }
        if (!routes.isEmpty()) object["routes"] = QListStr2QJsonArray(routes);
        if (!route_gateway.isEmpty()) object["route_gateway"] = route_gateway;
        if (route_metric > 0) object["route_metric"] = route_metric;
        if (redirect_gateway) object["redirect_gateway"] = true;
        if (!redirect_gateway_flags.isEmpty()) object["redirect_gateway_flags"] = QListStr2QJsonArray(redirect_gateway_flags);
        if (redirect_private) object["redirect_private"] = true;
        if (block_ipv6) object["block_ipv6"] = true;
        if (!ping_interval.isEmpty()) object["ping_interval"] = ping_interval;
        if (!ping_restart.isEmpty()) object["ping_restart"] = ping_restart;
        if (ping_restart_disabled) object["ping_restart_disabled"] = true;
        if (!renegotiate_interval.isEmpty()) object["renegotiate_interval"] = renegotiate_interval;
        if (renegotiate_disabled) object["renegotiate_disabled"] = true;
        if (renegotiate_bytes > 0) object["renegotiate_bytes"] = renegotiate_bytes;
        if (renegotiate_packets > 0) object["renegotiate_packets"] = renegotiate_packets;
        if (!tls_timeout.isEmpty()) object["tls_timeout"] = tls_timeout;
        if (!handshake_window.isEmpty()) object["handshake_window"] = handshake_window;
        if (explicit_exit_notify > 0) object["explicit_exit_notify"] = explicit_exit_notify;
        if (system) object["system"] = true;
        if (!interface_name.isEmpty()) object["name"] = interface_name;
        if (mtu > 0) object["mtu"] = mtu;
        if (!udp_timeout.isEmpty()) object["udp_timeout"] = udp_timeout;
        if (!udp_mapping.isEmpty()) object["udp_mapping"] = udp_mapping;
        if (!udp_filtering.isEmpty()) object["udp_filtering"] = udp_filtering;
        if (udp_nat_max > 0) object["udp_nat_max"] = udp_nat_max;

        if (otp_profile_id >= 0) object["otp_profile_id"] = otp_profile_id;
        object["only_advertised_routes"] = only_advertised_routes;
        object["tunnel_dns"] = tunnel_dns;
        return object;
    }

    BuildResult openvpn::Build()
    {
        QJsonObject object;
        object["type"] = "openvpn-client";
        if (!name.isEmpty()) object["tag"] = name;
        mergeJsonObjects(object, dialFields->Build().object);

        // `server`/`server_port` and `servers` are mutually exclusive.
        if (servers.isEmpty()) {
            if (!server.isEmpty()) object["server"] = server;
            if (server_port > 0) object["server_port"] = server_port;
        } else {
            QJsonArray remotes;
            for (const auto& remote : servers) {
                if (auto item = remote->Build().object; !item.isEmpty()) remotes.append(item);
            }
            object["servers"] = remotes;
            if (remote_random) object["remote_random"] = true;
        }

        const auto creds = ResolveVpnCredentials(profile_id, username, password);
        // A baked code is replayed on every reconnect, so leave the challenge for the poller. The
        // core packs any challenge answer as SCRV1, so an {otp} inside the credentials cannot go
        // that way and still has to be built in.
        const bool otpInCredentials = creds.username.contains(kOtpPlaceholder) ||
                                      creds.password.contains(kOtpPlaceholder);
        const bool liveChallenge = otp_profile_id >= 0 && !static_challenge.isEmpty() &&
                                   !otpInCredentials && !BuildingTestConfig();

        auto otpCode = liveChallenge ? QString() : ResolveOtpCode(otp_profile_id);
        auto user = SubstituteOtp(creds.username, otpCode);
        auto pass = SubstituteOtp(creds.password, otpCode);
        // Upstream packs a static-challenge answer as SCRV1:<b64 password>:<b64 answer>.
        bool packedChallenge = !otpCode.isEmpty() && !static_challenge.isEmpty();
        if (packedChallenge) {
            pass = "SCRV1:" + QString::fromLatin1(pass.toUtf8().toBase64()) + ":" +
                   QString::fromLatin1(otpCode.toUtf8().toBase64());
        }
        if (!user.isEmpty()) object["username"] = user;
        if (!pass.isEmpty()) object["password"] = pass;
        if (!packedChallenge) {
            if (!static_challenge.isEmpty()) object["static_challenge"] = static_challenge;
            if (static_challenge_echo) object["static_challenge_echo"] = true;
        }

        if (!mode.isEmpty()) object["mode"] = mode;
        if (!network.isEmpty()) object["network"] = network;
        if (!address.isEmpty()) object["address"] = QListStr2QJsonArray(address);
        if (!peer_address.isEmpty()) object["peer_address"] = peer_address;
        if (!peer_address_ipv6.isEmpty()) object["peer_address_ipv6"] = peer_address_ipv6;
        if (!topology.isEmpty()) object["topology"] = topology;
        // "none" makes a rejected login terminal; anything else re-raises the challenge instead.
        if (!auth_retry.isEmpty()) object["auth_retry"] = auth_retry;
        else if (liveChallenge) object["auth_retry"] = "interact";
        if (!static_key.isEmpty()) object["static_key"] = QListStr2QJsonArray(static_key);
        if (!static_key_path.isEmpty()) object["static_key_path"] = static_key_path;
        if (!key_direction.isEmpty()) object["key_direction"] = key_direction;
        if (auto tlsObj = tls->Build().object; !tlsObj.isEmpty()) object["tls"] = tlsObj;
        if (!cipher.isEmpty()) object["cipher"] = cipher;
        if (!data_ciphers.isEmpty()) object["data_ciphers"] = QListStr2QJsonArray(data_ciphers);
        if (!data_ciphers_fallback.isEmpty()) object["data_ciphers_fallback"] = data_ciphers_fallback;
        if (!auth.isEmpty()) object["auth"] = auth;
        if (mss_fix > 0) object["mss_fix"] = mss_fix;
        if (mss_fix_disabled) object["mss_fix_disabled"] = true;
        if (!mss_fix_mode.isEmpty()) object["mss_fix_mode"] = mss_fix_mode;
        if (fragment > 0) object["fragment"] = fragment;
        if (replay_window > 0) object["replay_window"] = replay_window;
        if (!replay_window_time.isEmpty()) object["replay_window_time"] = replay_window_time;
        if (!compression.isEmpty()) object["compression"] = compression;
        if (!compression_lzo.isEmpty()) object["compression_lzo"] = compression_lzo;
        if (!allow_compression.isEmpty()) object["allow_compression"] = allow_compression;
        if (route_no_pull) object["route_no_pull"] = true;
        if (!pull_filters.isEmpty()) {
            QJsonArray filters;
            for (const auto& filter : pull_filters) {
                if (auto item = filter->Build().object; !item.isEmpty()) filters.append(item);
            }
            if (!filters.isEmpty()) object["pull_filters"] = filters;
        }
        if (!routes.isEmpty()) object["routes"] = QListStr2QJsonArray(routes);
        if (!route_gateway.isEmpty()) object["route_gateway"] = route_gateway;
        if (route_metric > 0) object["route_metric"] = route_metric;
        if (redirect_gateway) object["redirect_gateway"] = true;
        if (!redirect_gateway_flags.isEmpty()) object["redirect_gateway_flags"] = QListStr2QJsonArray(redirect_gateway_flags);
        if (redirect_private) object["redirect_private"] = true;
        if (block_ipv6) object["block_ipv6"] = true;
        if (!ping_interval.isEmpty()) object["ping_interval"] = ping_interval;
        if (!ping_restart.isEmpty()) object["ping_restart"] = ping_restart;
        if (ping_restart_disabled) object["ping_restart_disabled"] = true;
        if (!renegotiate_interval.isEmpty()) object["renegotiate_interval"] = renegotiate_interval;
        if (renegotiate_disabled) object["renegotiate_disabled"] = true;
        if (renegotiate_bytes > 0) object["renegotiate_bytes"] = renegotiate_bytes;
        if (renegotiate_packets > 0) object["renegotiate_packets"] = renegotiate_packets;
        if (!tls_timeout.isEmpty()) object["tls_timeout"] = tls_timeout;
        if (!handshake_window.isEmpty()) object["handshake_window"] = handshake_window;
        if (explicit_exit_notify > 0) object["explicit_exit_notify"] = explicit_exit_notify;
        if (system) object["system"] = true;
        if (!interface_name.isEmpty()) object["name"] = interface_name;
        if (mtu > 0) object["mtu"] = mtu;
        if (!udp_timeout.isEmpty()) object["udp_timeout"] = udp_timeout;
        if (!udp_mapping.isEmpty()) object["udp_mapping"] = udp_mapping;
        if (!udp_filtering.isEmpty()) object["udp_filtering"] = udp_filtering;
        if (udp_nat_max > 0) object["udp_nat_max"] = udp_nat_max;
        return {object, ""};
    }

    QString openvpn::DisplayType()
    {
        return "OpenVPN";
    }

    SecurityInfo openvpn::GetSecurity()
    {
        if (mode == "static_key") return {QObject::tr("Static Key"), {}, SecurityLevel::Weak};
        auto pinned = !tls->certificate.isEmpty() || !tls->certificate_path.isEmpty() || !tls->peer_fingerprint.isEmpty();
        if (!pinned) return {QObject::tr("Unverified TLS"), {}, SecurityLevel::Weak};
        return {QObject::tr("TLS"), {}, SecurityLevel::Secure};
    }

    bool openvpn::IsEndpoint()
    {
        return true;
    }

    bool openvpn::SupportsCredentialStrip() const
    {
        return true;
    }

    void openvpn::StripCredentials()
    {
        username.clear();
        password.clear();
        static_key.clear();
        static_key_path.clear();
        otp_profile_id = -1;
        tls->client_key.clear();
        tls->client_key_path.clear();
        tls->control_wrap->key.clear();
        tls->control_wrap->key_path.clear();
        // Absolute local paths: meaningless on another machine and they carry the OS user name.
        tls->certificate_path.clear();
        tls->client_certificate_path.clear();
        tls->crl_path.clear();
    }
}
