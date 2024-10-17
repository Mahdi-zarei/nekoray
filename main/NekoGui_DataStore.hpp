// DO NOT INCLUDE THIS

namespace NekoGui {

    class Routing : public JsonStore {
    public:
        int current_route_id = 0;
        QString def_outbound = "proxy";

        // DNS
        QString remote_dns = "tls://8.8.8.8";
        QString remote_dns_strategy = "";
        QString direct_dns = "localhost";
        QString direct_dns_strategy = "";
        bool use_dns_object = false;
        QString dns_object = "";
        QString dns_final_out = "proxy";

        // Misc
        QString domain_strategy = "AsIs";
        QString outbound_domain_strategy = "AsIs";
        int sniffing_mode = SniffingMode::FOR_ROUTING;

        explicit Routing(int preset = 0);

        static QStringList List();
    };

    class ExtraCore : public JsonStore {
    public:
        QString core_map;

        explicit ExtraCore();

        [[nodiscard]] QString Get(const QString &id) const;

        void Set(const QString &id, const QString &path);

        void Delete(const QString &id);
    };

    class DataStore : public JsonStore {
    public:
        // Running

        QString core_token;
        int core_port = 19810;
        int started_id = -1919;
        bool core_running = false;
        bool prepare_exit = false;
        bool spmode_vpn = false;
        bool spmode_system_proxy = false;
        bool need_keep_vpn_off = false;
        QString appdataDir = "";
        QStringList ignoreConnTag = {};

        std::unique_ptr<Routing> routing;
        int imported_count = 0;
        bool refreshing_group_list = false;
        bool refreshing_group = false;
        std::atomic<int> resolve_count = 0;

        // Flags
        QStringList argv = {};
        bool flag_use_appdata = false;
        bool flag_many = false;
        bool flag_tray = false;
        bool flag_debug = false;
        bool flag_restart_tun_on = false;
        bool flag_reorder = false;
        bool flag_dns_set = false;

        // Saved

        // Misc
        QString log_level = "info";
        QString test_latency_url = "http://cp.cloudflare.com/";
        int test_concurrent = 10;
        int traffic_loop_interval = 500;
        bool disable_traffic_stats = false;
        int current_group = 0; // group id
        QString mux_protocol = "smux";
        bool mux_padding = false;
        int mux_concurrency = 8;
        bool mux_default_on = false;
        QString theme = "0";
        int language = 0;
        QString font = "";
        int font_size = 0;
        QString mw_size = "";
        QStringList log_ignore = {};
        bool start_minimal = false;
        int max_log_line = 200;
        QString splitter_state = "";

        // Subscription
        QString user_agent = ""; // set at main.cpp
        bool sub_use_proxy = false;
        bool sub_clear = false;
        bool sub_insecure = false;
        int sub_auto_update = -30;

        // Assets
        QString geoip_download_url = "";
        QString geosite_download_url = "";

        // Security
        bool skip_cert = false;
        QString utlsFingerprint = "";

        // Remember
        QStringList remember_spmode = {};
        int remember_id = -1919;
        bool remember_enable = false;

        // Socks & HTTP Inbound
        QString inbound_address = "127.0.0.1";
        int inbound_socks_port = 2080; // Mixed, actually
        QString custom_inbound = "{\"inbounds\": []}";

        // Routing
        QString custom_route_global = "{\"rules\": []}";
        QString active_routing = "Default";

        // VPN
        bool fake_dns = false;
        bool enable_gso = false;
        bool auto_redirect = false;
        QString vpn_implementation = "gvisor";
        int vpn_mtu = 1500;
        bool vpn_ipv6 = false;
        bool vpn_strict_route = true;

        // NTP
        bool enable_ntp = false;
        QString ntp_server_address = "";
        int ntp_server_port = 0;
        QString ntp_interval = "";

        // Hijack
        bool enable_dns_server = false;
        QString dns_server_listen_addr = "127.0.0.1";
        int dns_server_listen_port = 53;
        QString dns_v4_resp = "127.0.0.1";
        QString dns_v6_resp = "::1";
        QStringList dns_server_rules = {};
        bool enable_redirect = false;
        QString redirect_listen_address = "127.0.0.1";
        int redirect_listen_port = 443;

        // System dns
        bool system_dns_set = false;
        bool is_dhcp = false;
        QStringList system_dns_servers = {};

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
        QString core_box_underlying_dns = "";

        // Other Core
        ExtraCore *extraCore = new ExtraCore;

        // Methods

        DataStore();

        void UpdateStartedId(int id);

        [[nodiscard]] QString GetUserAgent(bool isDefault = false) const;
    };

    extern DataStore *dataStore;

} // namespace NekoGui
