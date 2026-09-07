#pragma once

#include "Database.h"
#include "include/global/Const.hpp"
#include <QMutexLocker>
#include <QJsonObject>
#include <QMap>
#include <QKeySequence>
#include <atomic>

#ifdef Q_OS_WIN
#include "include/sys/windows/WinVersion.h"
#endif

namespace Configs {
    // Loopback/broadcast are deliberately absent: routing them into the tun breaks the sing-box <-> Xray bridges and local DNS.
    inline QStringList defaultTunPrivateRanges() {
        return {"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "169.254.0.0/16", "224.0.0.0/4"};
    }

    class SettingsRepo {
    private:
        Database& db;

        QMap<QString, bool*>        boolMap;
        QMap<QString, int*>         intMap;
        QMap<QString, QString*>     stringMap;
        QMap<QString, QStringList*> stringListMap;

        void initMaps();
        void createTables() const;
        void loadAllSettings();
        void saveAllSettings() const;

    public:
        bool noSave = false;

        explicit SettingsRepo(Database& database);
        
        bool Save();
        
        // Runtime state and flags below are never persisted.
        QString core_socket_name = "";
        int started_id = NoProfileId;
        bool core_running = false;
        bool prepare_exit = false;
        bool spmode_vpn = false;
        bool spmode_system_proxy = false;
        QString appdataDir = "";
        QStringList ignoreConnTag = {};
        int imported_count = 0;
        bool refreshing_group_list = false;
        bool refreshing_group = false;
        std::atomic<int> resolve_count = 0;

        QStringList argv = {};
        bool flag_use_appdata = false;
        bool flag_many = false;
        bool flag_tray = false;
        bool flag_debug = false;
        bool flag_restart_tun_on = false;
        bool flag_dns_set = false;
        
        // Persisted settings.
        QString mainWindowGeometry;
        QString log_level = "info";
        QString test_latency_url = "http://cp.cloudflare.com/";
        // Fetched WITHOUT any proxy, so it must be reachable directly; empty falls back to the OS.
        QString direct_test_url = "";
        int url_test_timeout_ms = 3000;
        bool disable_tray = false;
        int test_concurrent = 10;
        bool disable_traffic_stats = false;
        int current_group = 0;
        QString mux_protocol = "smux";
        bool mux_padding = false;
        int mux_concurrency = 8;
        bool mux_default_on = false;
        // "built-in" = sing-box tls.fragment, "custom" = hiddify dialer-level tls_fragment.
        QString fragment_implementation = "built-in";
        bool fragment_default_on = false;
        // "min-max" ranges, custom implementation only: bytes per ClientHello fragment, and ms to sleep between bursts.
        QString fragment_size = "10-100";
        QString fragment_sleep = "2-5";
        // TLS tricks = mixed-case SNI.
        bool tls_tricks_default_on = false;
        // SNI to forge and how the real server rejects the forged segment; profiles with an empty field of their own inherit these.
        QString tls_spoof = "";
        QString tls_spoof_method = "";
        bool tls_spoof_default_on = false;

        // Shared by HTTP/2 and the QUIC outbounds; empty / 0 means "leave the core's default" and is omitted from the config.
        QString h2_idle_timeout = "";
        QString h2_keep_alive_period = "";
        QString h2_stream_receive_window = "";
        QString h2_connection_receive_window = "";
        int h2_max_concurrent_streams = 0;
        int quic_initial_packet_size = 0;
        bool quic_disable_path_mtu_discovery = false;
        QString theme = "0";
        int language = 0;
        QString font = "";
        int font_size = 0;
        QString mw_size = "";
        bool log_enable_include = false;
        bool log_enable_exclude = false;
        QStringList log_include_keyword = {};
        QStringList log_include_regex = {};
        QStringList log_exclude_keyword = {};
        QStringList log_exclude_regex = {};
        bool log_auto_scroll = true;
        bool start_minimal = false;
        int max_log_line = 200;
        // On-disk diagnostic log only; log_level is the core's browser verbosity.
        QString log_file_level = "debug";
        QString splitter_state = "";
        bool enable_stats = true;
        int stats_tab = 0; // either connection or log
        // Stats::ConnectionSort; 0 == Stats::Default, the core's own ordering.
        int connection_sort = 0;
        bool connection_sort_asc = false;
        // Days of hour-resolution history to retain (the 48h minute-resolution window is fixed); clamped to >= 1 in use.
        int traffic_stats_retention_days = 90;
        bool disable_traffic_aggregation = false;
        int speed_test_mode = TestConfig::FULL;
        int speed_test_timeout_ms = 5000;
        QString simple_dl_url = "http://cachefly.cachefly.net/1mb.test";
        bool allow_beta_update = false;
        bool show_system_dns = false;
        bool use_custom_icons = false;
        bool follow_status_in_taskbar = true;
        bool skip_delete_confirmation = false;
        bool show_config_security = false;
        // -1 until a filter column has been used.
        int last_filter_column = -1;

        // Mirror of the throne:// registration we last wrote to the OS; startup re-registers only when it differs.
        QString url_scheme_mirror = "";
        bool url_scheme_auto_register = true;

        // Network
        bool net_use_proxy = false;
        bool net_insecure = false;
        bool reset_proxy_on_disable_sp = false;

        // Subscription
        QString user_agent = ""; // set at main.cpp
        // Sign encodes enabled (negative = off), magnitude = interval minutes (ignored if < 30); *_last is epoch seconds.
        int sub_auto_update = -30;
        qint64 sub_auto_update_last = 0;
        bool sub_clear = false;
        bool sub_show_change_popup = true;
        bool sub_send_hwid = false;
        QString sub_custom_hwid_params = "";
        bool allow_stopping_active_profile = false;

        // Security
        bool skip_cert = false;
        QString utlsFingerprint = "";
        bool disable_run_admin = false; // windows only
        bool use_mozilla_certs = false;

        // Remember
        bool remember_system_proxy = false;
        bool remember_tun = false;
        int remember_id = NoProfileId;
        bool remember_enable = false;
        bool windows_set_admin = false;
        QMap<QString, QKeySequence> shortcuts;

        // Routing
        int current_route_id = 1;
        // Same sign-encoded interval scheme as sub_auto_update.
        int route_auto_update = -1440;
        qint64 route_auto_update_last = 0;
        QString remote_dns = "https://8.8.8.8/dns-query";
        bool remote_dns_disable_ipv6 = false;
        QString direct_dns = "localhost";
        bool direct_dns_disable_ipv6 = false;
        int dns_cache_capacity = 65536;
        bool dns_disable_cache = false;
        bool dns_disable_expire = false;
        bool dns_reverse_mapping = false;
        bool enable_dns_routing = true;
        bool use_dns_object = false;
        QString dns_object = "";
        QString dns_final_out = "remote";
        bool dns_optimistic = false;
        QString dns_optimistic_timeout = "";
        QString dns_query_timeout = "";
        bool dns_use_hosts = false;
        bool dns_predefined_enable = true;
        QStringList dns_predefined_rules = {"127.0.0.1 localhost"};
        QString resolve_domain_strategy = "";
        QString default_domain_strategy = "";
        int ruleset_mirror = Mirrors::CLOUDFLARE;

        // Socks & HTTP Inbound
        bool disable_mixed_inbound = false;
        QString inbound_address = "127.0.0.1";
        int inbound_socks_port = 2080; // Mixed, actually
        bool random_inbound_port = false;
        QString custom_inbound = "{\"inbounds\": []}";
        QString proxy_scheme = "{ip}:{port}";
        bool inbound_auth = false;
        QString inbound_user = "";
        QString inbound_pass = "";

        // Routing
        QString custom_route_global = "{\"rules\": []}";
        QString active_routing = "Default";
        bool adblock_enable = false;

        // VPN
        bool fake_dns = false;
        bool fakeip_disable_ipv6 = false;
        bool enable_tun_routing = false;
#ifdef Q_OS_MACOS
        QString vpn_implementation = "gvisor";
        bool vpn_strict_route = false;
#elif defined(Q_OS_WIN)
        QString vpn_implementation = WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_10_1507) ? "system" : "gvisor";
        bool vpn_strict_route = WinVersion::IsBuildNumGreaterOrEqual(BuildNumber::Windows_10_1507);
#else
        QString vpn_implementation = "system";
        bool vpn_strict_route = false;
#endif
        // Linux only: newer kernels need `auto_redirect` for the system/mixed stacks to pass traffic, at the cost of acting as a gateway.
        bool vpn_auto_redirect = true;
        // Only UDP and ICMP reach the bridge: pre-match aborts at the sniff rule for TCP.
        bool vpn_l3_bridge = false;
        int vpn_mtu = 1500;
        bool disable_private_range_bypass = false;
        QStringList vpn_private_ranges = defaultTunPrivateRanges();
        bool vpn_ipv6 = false;
        QString vpn_tun_ipv4_cidr = "172.19.0.1/24";
        QString vpn_tun_ipv6_cidr = "fdfe:dcba:9876::1/96";
        bool disable_privilege_req = false;

        // NTP
        bool enable_ntp = false;
        QString ntp_server_address = "";
        int ntp_server_port = 0;
        QString ntp_interval = "";
        QString ntp_outbound = "direct"; // "direct" or "proxy"

        // Warp
        bool enable_warp = false;
        QString warp_private_key = "";
        QString warp_public_key = "";
        QStringList warp_ifc_addrs = {};
        QString warp_ep = "";
        QStringList warp_reserved = {};

        // Hijack
        bool enable_dns_server = false;
        bool dns_server_listen_lan = false;
        int dns_server_listen_port = 53;
        QString dns_v4_resp = "127.0.0.1";
        QString dns_v6_resp = "::1";
        QStringList dns_server_rules = {};
        bool enable_redirect = false;
        QString redirect_listen_address = "127.0.0.1";
        int redirect_listen_port = 443;

        // System dns
        bool system_dns_set = false;

        // Hotkey
        QString hotkey_mainwindow = "";
        QString hotkey_group = "";
        QString hotkey_route = "";
        QString hotkey_system_proxy_menu = "";
        QString hotkey_toggle_system_proxy = "";

        // Core
        int core_box_clash_api = -9090;
        QString core_box_clash_listen_addr = "127.0.0.1";
        QString core_box_clash_api_secret = "";
        // Port only publishes the dashboard; the service itself also carries the stats tracker.
        int core_box_api_port = -9091;
        QString core_box_api_secret = "";
        QString core_box_underlying_dns = "";
        int core_dns_in_port = 5533;

        // Xray
        QString xray_log_level = "warning";
        int xray_mux_concurrency = 8;
        bool xray_mux_default_on = false;
        Xray::XrayVlessPreference xray_vless_preference = Xray::XhttpAndReality;
        // Fetched on demand into GetBasePath(), which the core exposes to Xray via XRAY_LOCATION_ASSET.
        QString xray_geoip_url = "https://github.com/Loyalsoldier/v2ray-rules-dat/raw/release/geoip.dat";
        QString xray_geosite_url = "https://github.com/Loyalsoldier/v2ray-rules-dat/raw/release/geosite.dat";

        // Extra Core Paths
        QStringList extraCorePaths = {};

        // Last 5 custom entries per field.
        QStringList dial_bind_interface_history = {};
        QStringList dial_inet4_bind_address_history = {};
        QStringList dial_inet6_bind_address_history = {};

        void UpdateStartedId(int id);

        [[nodiscard]] QString GetUserAgent(bool isDefault = false) const;
        
        [[nodiscard]] QStringList GetExtraCorePaths() const;
        bool AddExtraCorePath(const QString &path);
    };
}
