#include "NekoGui.hpp"
#include "fmt/Preset.hpp"

#include <QFile>
#include <QDir>
#include <QApplication>
#include <QFileInfo>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#ifdef Q_OS_WIN
#include "sys/windows/guihelper.h"
#else
#ifdef Q_OS_LINUX
#include <sys/linux/LinuxCap.h>
#endif
#include <unistd.h>
#endif

namespace NekoGui_ConfigItem {

    // 添加关联
    void JsonStore::_add(configItem *item) {
        _map.insert(item->name, std::shared_ptr<configItem>(item));
    }

    QString JsonStore::_name(void *p) {
        for (const auto &_item: _map) {
            if (_item->ptr == p) return _item->name;
        }
        return {};
    }

    std::shared_ptr<configItem> JsonStore::_get(const QString &name) {
        // 直接 [] 会设置一个 nullptr ，所以先判断是否存在
        if (_map.contains(name)) {
            return _map[name];
        }
        return nullptr;
    }

    void JsonStore::_setValue(const QString &name, void *p) {
        auto item = _get(name);
        if (item == nullptr) return;

        switch (item->type) {
            case itemType::string:
                *(QString *) item->ptr = *(QString *) p;
                break;
            case itemType::boolean:
                *(bool *) item->ptr = *(bool *) p;
                break;
            case itemType::integer:
                *(int *) item->ptr = *(int *) p;
                break;
            case itemType::integer64:
                *(long long *) item->ptr = *(long long *) p;
                break;
            // others...
            case stringList:
            case integerList:
            case jsonStore:
                break;
        }
    }

    QJsonObject JsonStore::ToJson(const QStringList &without) {
        QJsonObject object;
        for (const auto &_item: _map) {
            auto item = _item.get();
            if (without.contains(item->name)) continue;
            switch (item->type) {
                case itemType::string:
                    // Allow Empty
                    object.insert(item->name, *(QString *) item->ptr);
                    break;
                case itemType::integer:
                    object.insert(item->name, *(int *) item->ptr);
                    break;
                case itemType::integer64:
                    object.insert(item->name, *(long long *) item->ptr);
                    break;
                case itemType::boolean:
                    object.insert(item->name, *(bool *) item->ptr);
                    break;
                case itemType::stringList: {
                    if (QListStr2QJsonArray(*(QList<QString> *) item->ptr).isEmpty()) continue;
                    object.insert(item->name, QListStr2QJsonArray(*(QList<QString> *) item->ptr));
                    break;
                }
                case itemType::integerList: {
                    if (QListInt2QJsonArray(*(QList<int> *) item->ptr).isEmpty()) continue;
                    object.insert(item->name, QListInt2QJsonArray(*(QList<int> *) item->ptr));
                    break;
                }
                case itemType::jsonStore:
                    // _add 时应关联对应 JsonStore 的指针
                    object.insert(item->name, ((JsonStore *) item->ptr)->ToJson());
                    break;
                case itemType::jsonStoreList:
                    QJsonArray jsonArray;
                    auto arr = *(QList<JsonStore*> *) item->ptr;
                    for ( JsonStore* obj : arr) {
                        jsonArray.push_back(obj->ToJson());
                    }
                    object.insert(item->name, jsonArray);
                    break;
            }
        }
        return object;
    }

    QByteArray JsonStore::ToJsonBytes() {
        QJsonDocument document;
        document.setObject(ToJson());
        return document.toJson(save_control_compact ? QJsonDocument::Compact : QJsonDocument::Indented);
    }

    void JsonStore::FromJson(QJsonObject object) {
        for (const auto &key: object.keys()) {
            if (_map.count(key) == 0) {
                continue;
            }

            auto value = object[key];
            auto item = _map[key].get();

            if (item == nullptr)
                continue; // 故意忽略

            // 根据类型修改ptr的内容
            switch (item->type) {
                case itemType::string:
                    if (value.type() != QJsonValue::String) {
                        continue;
                    }
                    *(QString *) item->ptr = value.toString();
                    break;
                case itemType::integer:
                    if (value.type() != QJsonValue::Double) {
                        continue;
                    }
                    *(int *) item->ptr = value.toInt();
                    break;
                case itemType::integer64:
                    if (value.type() != QJsonValue::Double) {
                        continue;
                    }
                    *(long long *) item->ptr = value.toDouble();
                    break;
                case itemType::boolean:
                    if (value.type() != QJsonValue::Bool) {
                        continue;
                    }
                    *(bool *) item->ptr = value.toBool();
                    break;
                case itemType::stringList:
                    if (value.type() != QJsonValue::Array) {
                        continue;
                    }
                    *(QList<QString> *) item->ptr = QJsonArray2QListString(value.toArray());
                    break;
                case itemType::integerList:
                    if (value.type() != QJsonValue::Array) {
                        continue;
                    }
                    *(QList<int> *) item->ptr = QJsonArray2QListInt(value.toArray());
                    break;
                case itemType::jsonStore:
                    if (value.type() != QJsonValue::Object) {
                        continue;
                    }
                    ((JsonStore *) item->ptr)->FromJson(value.toObject());
                    break;
            }
        }

        if (callback_after_load != nullptr) callback_after_load();
    }

    void JsonStore::FromJsonBytes(const QByteArray &data) {
        QJsonParseError error{};
        auto document = QJsonDocument::fromJson(data, &error);

        if (error.error != error.NoError) {
            qDebug() << "QJsonParseError" << error.errorString();
            return;
        }

        FromJson(document.object());
    }

    bool JsonStore::Save() {
        if (callback_before_save != nullptr) callback_before_save();
        if (save_control_no_save) return false;

        auto save_content = ToJsonBytes();
        auto changed = last_save_content != save_content;
        last_save_content = save_content;

        QFile file;
        file.setFileName(fn);
        file.open(QIODevice::ReadWrite | QIODevice::Truncate);
        file.write(save_content);
        file.close();

        return changed;
    }

    bool JsonStore::Load() {
        QFile file;
        file.setFileName(fn);

        if (!file.exists() && !load_control_must) {
            return false;
        }

        bool ok = file.open(QIODevice::ReadOnly);
        if (!ok) {
            MessageBoxWarning("error", "can not open config " + fn + "\n" + file.errorString());
        } else {
            last_save_content = file.readAll();
            FromJsonBytes(last_save_content);
        }

        file.close();
        return ok;
    }

} // namespace NekoGui_ConfigItem

namespace NekoGui {

    DataStore *dataStore = new DataStore();

    // datastore

    DataStore::DataStore() : JsonStore() {
        _add(new configItem("extraCore", dynamic_cast<JsonStore *>(extraCore), itemType::jsonStore));

        _add(new configItem("user_agent2", &user_agent, itemType::string));
        _add(new configItem("test_url", &test_latency_url, itemType::string));
        _add(new configItem("current_group", &current_group, itemType::integer));
        _add(new configItem("inbound_address", &inbound_address, itemType::string));
        _add(new configItem("inbound_socks_port", &inbound_socks_port, itemType::integer));
        _add(new configItem("log_level", &log_level, itemType::string));
        _add(new configItem("mux_protocol", &mux_protocol, itemType::string));
        _add(new configItem("mux_concurrency", &mux_concurrency, itemType::integer));
        _add(new configItem("mux_padding", &mux_padding, itemType::boolean));
        _add(new configItem("mux_default_on", &mux_default_on, itemType::boolean));
        _add(new configItem("traffic_loop_interval", &traffic_loop_interval, itemType::integer));
        _add(new configItem("test_concurrent", &test_concurrent, itemType::integer));
        _add(new configItem("theme", &theme, itemType::string));
        _add(new configItem("custom_inbound", &custom_inbound, itemType::string));
        _add(new configItem("custom_route", &custom_route_global, itemType::string));
        _add(new configItem("sub_use_proxy", &sub_use_proxy, itemType::boolean));
        _add(new configItem("remember_id", &remember_id, itemType::integer));
        _add(new configItem("remember_enable", &remember_enable, itemType::boolean));
        _add(new configItem("language", &language, itemType::integer));
        _add(new configItem("spmode2", &remember_spmode, itemType::stringList));
        _add(new configItem("skip_cert", &skip_cert, itemType::boolean));
        _add(new configItem("hk_mw", &hotkey_mainwindow, itemType::string));
        _add(new configItem("hk_group", &hotkey_group, itemType::string));
        _add(new configItem("hk_route", &hotkey_route, itemType::string));
        _add(new configItem("hk_spmenu", &hotkey_system_proxy_menu, itemType::string));
        _add(new configItem("hk_toggle", &hotkey_toggle_system_proxy, itemType::string));
        _add(new configItem("fakedns", &fake_dns, itemType::boolean));
        _add(new configItem("active_routing", &active_routing, itemType::string));
        _add(new configItem("mw_size", &mw_size, itemType::string));
        _add(new configItem("disable_traffic_stats", &disable_traffic_stats, itemType::boolean));
        _add(new configItem("vpn_impl", &vpn_implementation, itemType::string));
        _add(new configItem("vpn_mtu", &vpn_mtu, itemType::integer));
        _add(new configItem("vpn_ipv6", &vpn_ipv6, itemType::boolean));
        _add(new configItem("vpn_strict_route", &vpn_strict_route, itemType::boolean));
        _add(new configItem("auto_redirect", &auto_redirect, itemType::boolean));
        _add(new configItem("sub_clear", &sub_clear, itemType::boolean));
        _add(new configItem("sub_insecure", &sub_insecure, itemType::boolean));
        _add(new configItem("sub_auto_update", &sub_auto_update, itemType::integer));
        _add(new configItem("log_ignore", &log_ignore, itemType::stringList));
        _add(new configItem("start_minimal", &start_minimal, itemType::boolean));
        _add(new configItem("max_log_line", &max_log_line, itemType::integer));
        _add(new configItem("splitter_state", &splitter_state, itemType::string));
        _add(new configItem("utlsFingerprint", &utlsFingerprint, itemType::string));
        _add(new configItem("core_box_clash_api", &core_box_clash_api, itemType::integer));
        _add(new configItem("core_box_clash_listen_addr", &core_box_clash_listen_addr, itemType::string));
        _add(new configItem("core_box_clash_api_secret", &core_box_clash_api_secret, itemType::string));
        _add(new configItem("core_box_underlying_dns", &core_box_underlying_dns, itemType::string));
        _add(new configItem("enable_gso", &enable_gso, itemType::boolean));
        _add(new configItem("enable_ntp", &enable_ntp, itemType::boolean));
        _add(new configItem("ntp_server_address", &ntp_server_address, itemType::string));
        _add(new configItem("ntp_server_port", &ntp_server_port, itemType::integer));
        _add(new configItem("ntp_interval", &ntp_interval, itemType::string));
        _add(new configItem("geoip_download_url", &geoip_download_url, itemType::string));
        _add(new configItem("geosite_download_url", &geosite_download_url, itemType::string));
        _add(new configItem("enable_dns_server", &enable_dns_server, itemType::boolean));
        _add(new configItem("dns_server_listen_addr", &dns_server_listen_addr, itemType::string));
        _add(new configItem("dns_server_listen_port", &dns_server_listen_port, itemType::integer));
        _add(new configItem("dns_v4_resp", &dns_v4_resp, itemType::string));
        _add(new configItem("dns_v6_resp", &dns_v6_resp, itemType::string));
        _add(new configItem("dns_server_rules", &dns_server_rules, itemType::stringList));
        _add(new configItem("enable_redirect", &enable_redirect, itemType::boolean));
        _add(new configItem("redirect_listen_address", &redirect_listen_address, itemType::string));
        _add(new configItem("redirect_listen_port", &redirect_listen_port, itemType::integer));
        _add(new configItem("system_dns_set", &system_dns_set, itemType::boolean));
        _add(new configItem("is_dhcp", &is_dhcp, itemType::boolean));
        _add(new configItem("system_dns_servers", &system_dns_servers, itemType::stringList));
    }

    void DataStore::UpdateStartedId(int id) {
        started_id = id;
        remember_id = id;
        Save();
    }

    QString DataStore::GetUserAgent(bool isDefault) const {
        if (user_agent.isEmpty()) {
            isDefault = true;
        }
        if (isDefault) {
            QString version = SubStrBefore(NKR_VERSION, "-");
            if (!version.contains(".")) version = "2.0";
            return "Nekoray" + version;
        }
        return user_agent;
    }

    // preset routing
    Routing::Routing(int preset) : JsonStore() {
        if (!Preset::SingBox::DomainStrategy.contains(domain_strategy)) domain_strategy = "";
        if (!Preset::SingBox::DomainStrategy.contains(outbound_domain_strategy)) outbound_domain_strategy = "";
        _add(new configItem("current_route_id", &this->current_route_id, itemType::integer));
        _add(new configItem("default_outbound", &this->def_outbound, itemType::string));
        //
        _add(new configItem("remote_dns", &this->remote_dns, itemType::string));
        _add(new configItem("remote_dns_strategy", &this->remote_dns_strategy, itemType::string));
        _add(new configItem("direct_dns", &this->direct_dns, itemType::string));
        _add(new configItem("direct_dns_strategy", &this->direct_dns_strategy, itemType::string));
        _add(new configItem("domain_strategy", &this->domain_strategy, itemType::string));
        _add(new configItem("outbound_domain_strategy", &this->outbound_domain_strategy, itemType::string));
        _add(new configItem("sniffing_mode", &this->sniffing_mode, itemType::integer));
        _add(new configItem("use_dns_object", &this->use_dns_object, itemType::boolean));
        _add(new configItem("dns_object", &this->dns_object, itemType::string));
        _add(new configItem("dns_final_out", &this->dns_final_out, itemType::string));
    }

    QStringList Routing::List() {
        return {"Default"};
    }

    // NO default extra core

    ExtraCore::ExtraCore() : JsonStore() {
        _add(new configItem("core_map", &this->core_map, itemType::string));
    }

    QString ExtraCore::Get(const QString &id) const {
        auto obj = QString2QJsonObject(core_map);
        for (const auto &c: obj.keys()) {
            if (c == id) return obj[id].toString();
        }
        return "";
    }

    void ExtraCore::Set(const QString &id, const QString &path) {
        auto obj = QString2QJsonObject(core_map);
        obj[id] = path;
        core_map = QJsonObject2QString(obj, true);
    }

    void ExtraCore::Delete(const QString &id) {
        auto obj = QString2QJsonObject(core_map);
        obj.remove(id);
        core_map = QJsonObject2QString(obj, true);
    }

    // System Utils

    QString FindCoreAsset(const QString &name) {
        QStringList search{QApplication::applicationDirPath()};
        search << "/usr/share/sing-geoip";
        search << "/usr/share/sing-geosite";
        search << "/usr/share/v2ray";
        search << "/usr/share/sing-box";
        search << "/usr/local/share/v2ray";
        search << "/opt/v2ray";
        for (const auto &dir: search) {
            if (dir.isEmpty()) continue;
            QFileInfo asset(dir + "/" + name);
            if (asset.exists()) {
                return asset.absoluteFilePath();
            }
        }
        return {};
    }

    QString FindNekoBoxCoreRealPath() {
        auto fn = QApplication::applicationDirPath() + "/nekobox_core";
        auto fi = QFileInfo(fn);
        if (fi.isSymLink()) return fi.symLinkTarget();
        return fn;
    }

    short isAdminCache = -1;

    // IsAdmin 主要判断：有无权限启动 Tun
    bool IsAdmin() {
        if (isAdminCache >= 0) return isAdminCache;

        bool admin = false;
#ifdef Q_OS_WIN
        admin = Windows_IsInAdmin();
#endif
#ifdef Q_OS_LINUX
        admin = QFileInfo(FindNekoBoxCoreRealPath()).groupId() == 0;
#endif
#ifdef Q_OS_MACOS
        admin = geteuid() == 0;
#endif

        isAdminCache = admin;
        return admin;
    };

} // namespace NekoGui
