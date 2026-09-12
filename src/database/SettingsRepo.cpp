#include "include/database/SettingsRepo.h"
#include "NkrVersion.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QUuid>

#include "include/global/Utils.hpp"

namespace Configs {
    SettingsRepo::SettingsRepo(Database& database) : db(database) {
        initMaps();
        createTables();
        loadAllSettings();
        // An empty secret disables authentication on the API service outright.
        if (core_box_api_secret.isEmpty()) {
            core_box_api_secret = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
            Save();
        }
    }

    void SettingsRepo::initMaps() {
        boolMap = {
            {"disable_tray",                  &disable_tray},
            {"random_inbound_port",           &random_inbound_port},
            {"mux_padding",                   &mux_padding},
            {"mux_default_on",                &mux_default_on},
            {"fragment_default_on",           &fragment_default_on},
            {"tls_tricks_default_on",         &tls_tricks_default_on},
            {"tls_spoof_default_on",          &tls_spoof_default_on},
            {"quic_disable_path_mtu_discovery", &quic_disable_path_mtu_discovery},
            {"net_use_proxy",                 &net_use_proxy},
            {"remember_enable",               &remember_enable},
            {"skip_cert",                     &skip_cert},
            {"fakedns",                       &fake_dns},
            {"fakeip_disable_ipv6",           &fakeip_disable_ipv6},
            {"direct_dns_disable_ipv6",       &direct_dns_disable_ipv6},
            {"remote_dns_disable_ipv6",       &remote_dns_disable_ipv6},
            {"disable_traffic_stats",         &disable_traffic_stats},
            {"disable_traffic_aggregation",   &disable_traffic_aggregation},
            {"vpn_ipv6",                      &vpn_ipv6},
            {"vpn_strict_route",              &vpn_strict_route},
            {"vpn_auto_redirect",             &vpn_auto_redirect},
            {"vpn_l3_bridge",                 &vpn_l3_bridge},
            {"sub_clear",                     &sub_clear},
            {"sub_show_change_popup",         &sub_show_change_popup},
            {"net_insecure",                  &net_insecure},
            {"sub_send_hwid",                 &sub_send_hwid},
            {"start_minimal",                 &start_minimal},
            {"enable_ntp",                    &enable_ntp},
            {"enable_dns_server",             &enable_dns_server},
            {"dns_server_listen_lan",         &dns_server_listen_lan},
            {"enable_redirect",               &enable_redirect},
            {"system_dns_set",                &system_dns_set},
            {"windows_set_admin",             &windows_set_admin},
            {"disable_win_admin",             &disable_run_admin},
            {"enable_stats",                  &enable_stats},
            {"disable_privilege_req",         &disable_privilege_req},
            {"enable_tun_routing",            &enable_tun_routing},
            {"use_mozilla_certs",             &use_mozilla_certs},
            {"allow_beta_update",             &allow_beta_update},
            {"adblock_enable",                &adblock_enable},
            {"show_system_dns",               &show_system_dns},
            {"use_custom_icons",              &use_custom_icons},
            {"follow_status_in_taskbar",           &follow_status_in_taskbar},
            {"xray_mux_default_on",           &xray_mux_default_on},
            {"use_dns_object",                &use_dns_object},
            {"skip_delete_confirmation",      &skip_delete_confirmation},
            {"show_config_security",          &show_config_security},
            {"log_enable_include",            &log_enable_include},
            {"log_enable_exclude",            &log_enable_exclude},
            {"log_auto_scroll",               &log_auto_scroll},
            {"enable_warp",                   &enable_warp},
            {"warp_tos_accepted",             &warp_tos_accepted},
            {"enable_dns_routing",            &enable_dns_routing},
            {"inbound_auth",                  &inbound_auth},
            {"allow_stopping_active_profile", &allow_stopping_active_profile},
            {"disable_mixed_inbound",         &disable_mixed_inbound},
            {"url_scheme_auto_register",      &url_scheme_auto_register},
            {"system_proxy_enabled",          &remember_system_proxy},
            {"tun_mode_enabled",              &remember_tun},
            {"reset_proxy_on_disable_sp", &reset_proxy_on_disable_sp},
            {"set_socks_system_proxy",       &set_socks_system_proxy},
            {"dns_disable_cache", &dns_disable_cache},
            {"dns_disable_expire", &dns_disable_expire},
            {"dns_persist_cache", &dns_persist_cache},
            {"dns_reverse_mapping", &dns_reverse_mapping},
            {"disable_private_range_bypass", &disable_private_range_bypass},
            {"dns_optimistic", &dns_optimistic},
            {"dns_use_hosts", &dns_use_hosts},
            {"dns_predefined_enable", &dns_predefined_enable},
            {"connection_sort_asc", &connection_sort_asc},
        };

        intMap = {
            {"current_group",          &current_group},
            {"last_filter_column",     &last_filter_column},
            {"inbound_socks_port",     &inbound_socks_port},
            {"mux_concurrency",        &mux_concurrency},
            {"test_concurrent",        &test_concurrent},
            {"remember_id",            &remember_id},
            {"language",               &language},
            {"font_size",              &font_size},
            {"max_log_line",           &max_log_line},
            {"stats_tab",              &stats_tab},
            {"connection_sort",        &connection_sort},
            {"traffic_stats_retention_days", &traffic_stats_retention_days},
            {"sub_auto_update",        &sub_auto_update},
            {"route_auto_update",      &route_auto_update},
            {"vpn_mtu",                &vpn_mtu},
            {"ntp_server_port",        &ntp_server_port},
            {"dns_server_listen_port", &dns_server_listen_port},
            {"redirect_listen_port",   &redirect_listen_port},
            {"core_box_clash_api",     &core_box_clash_api},
            {"core_box_api_port",      &core_box_api_port},
            {"speed_test_mode",        &speed_test_mode},
            {"speed_test_timeout_ms",  &speed_test_timeout_ms},
            {"url_test_timeout_ms",    &url_test_timeout_ms},
            {"xray_mux_concurrency",   &xray_mux_concurrency},
            {"current_route_id",       &current_route_id},
            {"ruleset_mirror",         &ruleset_mirror},
            {"core_dns_in_port",       &core_dns_in_port},
            {"dns_cache_capacity", &dns_cache_capacity},
            {"h2_max_concurrent_streams", &h2_max_concurrent_streams},
            {"quic_initial_packet_size", &quic_initial_packet_size},
            {"warp_masque_http_mode",  &warp_masque_http_mode},
        };

        stringMap = {
            {"user_agent2",                &user_agent},
            {"test_url",                   &test_latency_url},
            {"direct_test_url",            &direct_test_url},
            {"inbound_address",            &inbound_address},
            {"log_level",                  &log_level},
            {"log_file_level",             &log_file_level},
            {"mux_protocol",               &mux_protocol},
            {"fragment_implementation",    &fragment_implementation},
            {"fragment_size",              &fragment_size},
            {"fragment_sleep",             &fragment_sleep},
            {"tls_spoof",                  &tls_spoof},
            {"tls_spoof_method",           &tls_spoof_method},
            {"h2_idle_timeout",            &h2_idle_timeout},
            {"h2_keep_alive_period",       &h2_keep_alive_period},
            {"h2_stream_receive_window",   &h2_stream_receive_window},
            {"h2_connection_receive_window", &h2_connection_receive_window},
            {"theme",                      &theme},
            {"custom_inbound",             &custom_inbound},
            {"custom_route",               &custom_route_global},
            {"font",                       &font},
            {"hk_mw",                      &hotkey_mainwindow},
            {"hk_group",                   &hotkey_group},
            {"hk_route",                   &hotkey_route},
            {"hk_spmenu",                  &hotkey_system_proxy_menu},
            {"hk_toggle",                  &hotkey_toggle_system_proxy},
            {"active_routing",             &active_routing},
            {"mw_size",                    &mw_size},
            {"vpn_impl",                   &vpn_implementation},
            {"vpn_tun_ipv4_cidr",          &vpn_tun_ipv4_cidr},
            {"vpn_tun_ipv6_cidr",          &vpn_tun_ipv6_cidr},
            {"sub_custom_hwid_params",     &sub_custom_hwid_params},
            {"splitter_state",             &splitter_state},
            {"utlsFingerprint",            &utlsFingerprint},
            {"core_box_clash_listen_addr", &core_box_clash_listen_addr},
            {"core_box_clash_api_secret",  &core_box_clash_api_secret},
            {"core_box_api_secret",        &core_box_api_secret},
            {"core_box_underlying_dns",    &core_box_underlying_dns},
            {"ntp_server_address",         &ntp_server_address},
            {"ntp_interval",               &ntp_interval},
            {"ntp_outbound",               &ntp_outbound},
            {"dns_v4_resp",                &dns_v4_resp},
            {"dns_v6_resp",                &dns_v6_resp},
            {"redirect_listen_address",    &redirect_listen_address},
            {"proxy_scheme",               &proxy_scheme},
            {"main_window_geometry",       &mainWindowGeometry},
            {"xray_log_level",             &xray_log_level},
            {"xray_geoip_url",             &xray_geoip_url},
            {"xray_geosite_url",           &xray_geosite_url},
            {"remote_dns",                 &remote_dns},
            {"direct_dns",                 &direct_dns},
            {"dns_object",                 &dns_object},
            {"dns_optimistic_timeout",     &dns_optimistic_timeout},
            {"dns_query_timeout",          &dns_query_timeout},
            {"dns_final_out",              &dns_final_out},
            {"domain_strategy",            &resolve_domain_strategy},
            {"outbound_domain_strategy",   &default_domain_strategy},
            {"simple_dl_url",              &simple_dl_url},
            {"warp_private_key",           &warp_private_key},
            {"warp_public_key",            &warp_public_key},
            {"warp_ep",                    &warp_ep},
            {"warp_mode",                  &warp_mode},
            {"warp_masque_private_key",    &warp_masque_private_key},
            {"warp_masque_peer_public_key", &warp_masque_peer_public_key},
            {"warp_masque_ep",             &warp_masque_ep},
            {"warp_masque_sni",            &warp_masque_sni},
            {"inbound_user",               &inbound_user},
            {"inbound_pass",               &inbound_pass},
            {"url_scheme_mirror",          &url_scheme_mirror},
        };

        stringListMap = {
            {"dns_server_rules",         &dns_server_rules},
            {"dns_predefined_rules",     &dns_predefined_rules},
            {"extra_core_paths",         &extraCorePaths},
            {"log_include_keyword",      &log_include_keyword},
            {"log_include_regex",        &log_include_regex},
            {"log_exclude_keyword",      &log_exclude_keyword},
            {"log_exclude_regex",        &log_exclude_regex},
            {"warp_ifc_addrs",           &warp_ifc_addrs},
            {"vpn_private_ranges",       &vpn_private_ranges},
            {"xray_geoip_url_history",   &xray_geoip_url_history},
            {"xray_geosite_url_history", &xray_geosite_url_history},
            {"dial_bind_ifc_history",    &dial_bind_interface_history},
            {"dial_inet4_bind_history",  &dial_inet4_bind_address_history},
            {"dial_inet6_bind_history",  &dial_inet6_bind_address_history},
            {"warp_reserved", &warp_reserved},
            {"warp_masque_ifc_addrs", &warp_masque_ifc_addrs},
        };
    }

    void SettingsRepo::createTables() const {
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS settings (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            )
        )");
    }

    void SettingsRepo::loadAllSettings() {
        auto query = db.query("SELECT key, value FROM settings");
        if (!query) return;

        while (query->executeStep()) {
            const QString key = QString::fromStdString(query->getColumn(0).getText());
            const QString str = QString::fromStdString(query->getColumn(1).getText());

            if (key == "shortcuts") {
                if (const auto doc = QJsonDocument::fromJson(str.toUtf8()); doc.isObject()) {
                    auto obj = doc.object();
                    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                        qDebug() << it.key() << it.value();
                        shortcuts[it.key()] = QKeySequence(it.value().toString());
                    }
                    continue;
                }
            }
            if (key == "xray_vless_preference") {
                bool ok = false;
                int v = str.toInt(&ok);
                xray_vless_preference = static_cast<Xray::XrayVlessPreference>(ok ? v : 0);
                continue;
            }
            // Pre-1.14 DNS rule strategies: only the v4-only case survives as a filter, the rest were no-ops.
            if (key == "direct_dns_strategy") {
                direct_dns_disable_ipv6 = str == "ipv4_only";
                continue;
            }
            if (key == "remote_dns_strategy") {
                remote_dns_disable_ipv6 = str == "ipv4_only";
                continue;
            }
            if (key == "sub_auto_update_last") {
                sub_auto_update_last = str.toLongLong();
                continue;
            }
            if (key == "route_auto_update_last") {
                route_auto_update_last = str.toLongLong();
                continue;
            }
            if (auto boolVal = boolMap.find(key); boolVal != boolMap.end()) {
                *boolVal.value() = str == "true" || str == "1";
                continue;
            }
            if (auto intVal = intMap.find(key); intVal != intMap.end()) {
                bool ok = false;
                *intVal.value() = str.toInt(&ok);
                if (!ok) *intVal.value() = 0;
                continue;
            }

            if (auto strListVal = stringListMap.find(key); strListVal != stringListMap.end()) {
                if (const auto doc = QJsonDocument::fromJson(str.toUtf8()); doc.isArray()) {
                    const auto arr = doc.array();
                    QStringList list;
                    list.reserve(arr.size());
                    for (const auto& val : arr) list << val.toString();
                    *strListVal.value() = std::move(list);
                }
                continue;
            }
            if (auto strVal = stringMap.find(key); strVal != stringMap.end()) {
                *strVal.value() = str;
                continue;
            }
        }
        // Nothing writes these back, so drop them or they keep overriding the migrated flags on every load.
        db.exec("DELETE FROM settings WHERE key IN ('direct_dns_strategy', 'remote_dns_strategy')");
    }

    void SettingsRepo::saveAllSettings() const {
        if (noSave) return;

        std::vector<std::pair<std::string, std::string>> keyValues;
        keyValues.reserve(boolMap.size() + intMap.size() + stringMap.size() + stringListMap.size() + 4);

        const auto addPair = [&keyValues](const QString& key, const auto& value) {
            keyValues.emplace_back(key.toStdString(), value);
        };

        for (auto it = boolMap.begin(); it != boolMap.end(); ++it)
            addPair(it.key(), *it.value() ? "true" : "false");

        for (auto it = intMap.begin(); it != intMap.end(); ++it)
            addPair(it.key(), QString::number(*it.value()).toStdString());

        for (auto it = stringMap.begin(); it != stringMap.end(); ++it)
            addPair(it.key(), it.value()->toStdString());

        for (auto it = stringListMap.begin(); it != stringListMap.end(); ++it) {
            addPair(it.key(), QJsonDocument(QJsonArray::fromStringList(*it.value())).toJson(QJsonDocument::Compact).toStdString());
        }

        {
            QJsonObject obj;
            for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it)
                obj[it.key()] = it.value().toString();
            addPair(QStringLiteral("shortcuts"), QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString());
        }

        addPair(QStringLiteral("xray_vless_preference"),
            std::to_string(static_cast<int>(xray_vless_preference)));

        // qint64 timestamps: out of range for the int map, so persisted here.
        addPair(QStringLiteral("sub_auto_update_last"),
            std::to_string(sub_auto_update_last));
        addPair(QStringLiteral("route_auto_update_last"),
            std::to_string(route_auto_update_last));

        db.execBatchSettingsReplace(keyValues);
    }

    void SettingsRepo::UpdateStartedId(int id) {
        started_id = id;
        remember_id = id;
        Save();
    }

    static QStringView SubStrBefore(QStringView str, QStringView sub) {
        const qsizetype pos = str.indexOf(sub);
        return pos == -1 ? str : str.left(pos);
    }

    QString SettingsRepo::GetUserAgent(bool isDefault) const {
        if (user_agent.isEmpty() || isDefault) {
            const QStringView version = SubStrBefore(QStringLiteral(NKR_VERSION), u"-");
            if (version.contains(u'.')) {
                return QStringLiteral("Throne/") + version.toString();
            }
            return QStringLiteral("Throne/1.0.0");
        }
        return user_agent;
    }

    bool SettingsRepo::Save() {
        saveAllSettings();
        return true;
    }

    QStringList SettingsRepo::GetExtraCorePaths() const {
        return extraCorePaths;
    }

    bool SettingsRepo::AddExtraCorePath(const QString &path) {
        if (extraCorePaths.contains(path)) {
            return false;
        }
        extraCorePaths.append(path);
        return true;
    }
}
