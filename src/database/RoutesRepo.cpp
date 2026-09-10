#include "include/database/RoutesRepo.h"
#include "include/global/Configs.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <QMutexLocker>
#include <QSet>

namespace Configs {
    RoutesRepo::RoutesRepo(Database& database) : db(database) {
        createTables();
    }

    void RoutesRepo::createTables() const {
        db.exec(R"(
            CREATE TABLE IF NOT EXISTS route_profiles (
                id INTEGER PRIMARY KEY,
                name TEXT NOT NULL DEFAULT '',
                default_outbound_id INTEGER NOT NULL DEFAULT -1,
                is_raw INTEGER NOT NULL DEFAULT 0,
                raw_route TEXT NOT NULL DEFAULT '',
                prevent_modifications INTEGER NOT NULL DEFAULT 0,
                is_remote INTEGER NOT NULL DEFAULT 0,
                remote_url TEXT NOT NULL DEFAULT '',
                auto_update INTEGER NOT NULL DEFAULT 0,
                remote_last_update INTEGER NOT NULL DEFAULT 0,
                endpoint_profile_ids TEXT NOT NULL DEFAULT '[]',
                inner_hop_endpoint_ids TEXT NOT NULL DEFAULT '[]',
                created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
                updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
            )
        )");
        if (!routeProfilesColumnExists("is_raw"))
            db.exec("ALTER TABLE route_profiles ADD COLUMN is_raw INTEGER NOT NULL DEFAULT 0");
        if (!routeProfilesColumnExists("raw_route"))
            db.exec("ALTER TABLE route_profiles ADD COLUMN raw_route TEXT NOT NULL DEFAULT ''");
        if (!routeProfilesColumnExists("prevent_modifications"))
            db.exec("ALTER TABLE route_profiles ADD COLUMN prevent_modifications INTEGER NOT NULL DEFAULT 0");
        if (!routeProfilesColumnExists("is_remote"))
            db.exec("ALTER TABLE route_profiles ADD COLUMN is_remote INTEGER NOT NULL DEFAULT 0");
        if (!routeProfilesColumnExists("remote_url"))
            db.exec("ALTER TABLE route_profiles ADD COLUMN remote_url TEXT NOT NULL DEFAULT ''");
        if (!routeProfilesColumnExists("auto_update"))
            db.exec("ALTER TABLE route_profiles ADD COLUMN auto_update INTEGER NOT NULL DEFAULT 0");
        if (!routeProfilesColumnExists("remote_last_update"))
            db.exec("ALTER TABLE route_profiles ADD COLUMN remote_last_update INTEGER NOT NULL DEFAULT 0");
        if (!routeProfilesColumnExists("endpoint_profile_ids"))
            db.exec("ALTER TABLE route_profiles ADD COLUMN endpoint_profile_ids TEXT NOT NULL DEFAULT '[]'");
        if (!routeProfilesColumnExists("inner_hop_endpoint_ids"))
            db.exec("ALTER TABLE route_profiles ADD COLUMN inner_hop_endpoint_ids TEXT NOT NULL DEFAULT '[]'");

        db.exec(R"(
            CREATE TABLE IF NOT EXISTS route_rules (
                route_profile_id INTEGER NOT NULL,
                rule_order INTEGER NOT NULL,
                name TEXT NOT NULL DEFAULT '',
                type INTEGER NOT NULL DEFAULT 0,
                ip_version TEXT,
                network TEXT,
                protocol TEXT,
                inbound_json TEXT,
                domain_json TEXT,
                domain_suffix_json TEXT,
                domain_keyword_json TEXT,
                domain_regex_json TEXT,
                source_ip_cidr_json TEXT,
                source_ip_is_private INTEGER NOT NULL DEFAULT 0,
                ip_cidr_json TEXT,
                ip_is_private INTEGER NOT NULL DEFAULT 0,
                source_port_json TEXT,
                source_port_range_json TEXT,
                port_json TEXT,
                port_range_json TEXT,
                process_name_json TEXT,
                process_path_json TEXT,
                process_path_regex_json TEXT,
                rule_set_json TEXT,
                invert INTEGER NOT NULL DEFAULT 0,
                outbound_id INTEGER NOT NULL DEFAULT -2,
                action TEXT NOT NULL DEFAULT 'route',
                reject_method TEXT,
                no_drop INTEGER NOT NULL DEFAULT 0,
                override_address TEXT,
                override_port TEXT,
                sniffers_json TEXT,
                sniff_override_dest INTEGER NOT NULL DEFAULT 0,
                strategy TEXT,
                wifi_ssid_json TEXT,
                wifi_bssid_json TEXT,
                tls_spoof TEXT,
                tls_spoof_method TEXT,
                PRIMARY KEY (route_profile_id, rule_order),
                FOREIGN KEY(route_profile_id) REFERENCES route_profiles(id) ON DELETE CASCADE
            )
        )");
        if (!routeRulesColumnExists("wifi_ssid_json"))
            db.exec("ALTER TABLE route_rules ADD COLUMN wifi_ssid_json TEXT");
        if (!routeRulesColumnExists("wifi_bssid_json"))
            db.exec("ALTER TABLE route_rules ADD COLUMN wifi_bssid_json TEXT");
        if (!routeRulesColumnExists("tls_spoof"))
            db.exec("ALTER TABLE route_rules ADD COLUMN tls_spoof TEXT");
        if (!routeRulesColumnExists("tls_spoof_method"))
            db.exec("ALTER TABLE route_rules ADD COLUMN tls_spoof_method TEXT");
    }

    bool RoutesRepo::routeRulesColumnExists(const char* columnName) const {
        auto pragma = db.query("PRAGMA table_info(route_rules)");
        if (!pragma) return false;
        while (pragma->executeStep()) {
            if (pragma->getColumn(1).getText() == std::string(columnName)) return true;
        }
        return false;
    }

    bool RoutesRepo::routeProfilesColumnExists(const char* columnName) const {
        auto pragma = db.query("PRAGMA table_info(route_profiles)");
        if (!pragma) return false;
        while (pragma->executeStep()) {
            if (pragma->getColumn(1).getText() == std::string(columnName)) return true;
        }
        return false;
    }

    QJsonObject RoutesRepo::routeRuleToJson(const RouteRule* rule) const {
        QJsonObject json;
        
        json["name"] = rule->name;
        json["type"] = rule->type;
        json["ip_version"] = rule->ip_version;
        json["network"] = rule->network;
        json["protocol"] = rule->protocol;
        json["inbound"] = QListStr2QJsonArray(rule->inbound);
        json["domain"] = QListStr2QJsonArray(rule->domain);
        json["domain_suffix"] = QListStr2QJsonArray(rule->domain_suffix);
        json["domain_keyword"] = QListStr2QJsonArray(rule->domain_keyword);
        json["domain_regex"] = QListStr2QJsonArray(rule->domain_regex);
        json["source_ip_cidr"] = QListStr2QJsonArray(rule->source_ip_cidr);
        json["source_ip_is_private"] = rule->source_ip_is_private;
        json["ip_cidr"] = QListStr2QJsonArray(rule->ip_cidr);
        json["ip_is_private"] = rule->ip_is_private;
        json["source_port"] = QListStr2QJsonArray(rule->source_port);
        json["source_port_range"] = QListStr2QJsonArray(rule->source_port_range);
        json["port"] = QListStr2QJsonArray(rule->port);
        json["port_range"] = QListStr2QJsonArray(rule->port_range);
        json["process_name"] = QListStr2QJsonArray(rule->process_name);
        json["process_path"] = QListStr2QJsonArray(rule->process_path);
        json["process_path_regex"] = QListStr2QJsonArray(rule->process_path_regex);
        json["wifi_ssid"] = QListStr2QJsonArray(rule->wifi_ssid);
        json["wifi_bssid"] = QListStr2QJsonArray(rule->wifi_bssid);
        json["rule_set"] = QListStr2QJsonArray(rule->rule_set);
        json["invert"] = rule->invert;
        json["outboundID"] = rule->outboundID;
        json["action"] = rule->action;
        json["rejectMethod"] = rule->rejectMethod;
        json["no_drop"] = rule->no_drop;
        json["override_address"] = rule->override_address;
        json["override_port"] = rule->override_port;
        json["tls_spoof"] = rule->tls_spoof;
        json["tls_spoof_method"] = rule->tls_spoof_method;
        json["sniffers"] = QListStr2QJsonArray(rule->sniffers);
        json["sniffOverrideDest"] = rule->sniffOverrideDest;
        json["strategy"] = rule->strategy;
        
        return json;
    }

    std::shared_ptr<RouteRule> RoutesRepo::routeRuleFromJson(const QJsonObject& json) const {
        auto rule = std::make_shared<RouteRule>();
        
        rule->name = json["name"].toString();
        rule->type = json["type"].toInt();
        rule->ip_version = json["ip_version"].toString();
        rule->network = json["network"].toString();
        rule->protocol = json["protocol"].toString();
        rule->inbound = QJsonArray2QListString(json["inbound"].toArray());
        rule->domain = QJsonArray2QListString(json["domain"].toArray());
        rule->domain_suffix = QJsonArray2QListString(json["domain_suffix"].toArray());
        rule->domain_keyword = QJsonArray2QListString(json["domain_keyword"].toArray());
        rule->domain_regex = QJsonArray2QListString(json["domain_regex"].toArray());
        rule->source_ip_cidr = QJsonArray2QListString(json["source_ip_cidr"].toArray());
        rule->source_ip_is_private = json["source_ip_is_private"].toBool();
        rule->ip_cidr = QJsonArray2QListString(json["ip_cidr"].toArray());
        rule->ip_is_private = json["ip_is_private"].toBool();
        rule->source_port = QJsonArray2QListString(json["source_port"].toArray());
        rule->source_port_range = QJsonArray2QListString(json["source_port_range"].toArray());
        rule->port = QJsonArray2QListString(json["port"].toArray());
        rule->port_range = QJsonArray2QListString(json["port_range"].toArray());
        rule->process_name = QJsonArray2QListString(json["process_name"].toArray());
        rule->process_path = QJsonArray2QListString(json["process_path"].toArray());
        rule->process_path_regex = QJsonArray2QListString(json["process_path_regex"].toArray());
        rule->wifi_ssid = QJsonArray2QListString(json["wifi_ssid"].toArray());
        rule->wifi_bssid = QJsonArray2QListString(json["wifi_bssid"].toArray());
        rule->rule_set = QJsonArray2QListString(json["rule_set"].toArray());
        rule->invert = json["invert"].toBool();
        rule->outboundID = json["outboundID"].toInt();
        rule->action = json["action"].toString();
        rule->rejectMethod = json["rejectMethod"].toString();
        rule->no_drop = json["no_drop"].toBool();
        rule->override_address = json["override_address"].toString();
        rule->override_port = json["override_port"].toString();
        rule->tls_spoof = json["tls_spoof"].toString();
        rule->tls_spoof_method = json["tls_spoof_method"].toString();
        rule->sniffers = QJsonArray2QListString(json["sniffers"].toArray());
        rule->sniffOverrideDest = json["sniffOverrideDest"].toBool();
        rule->strategy = json["strategy"].toString();
        
        return rule;
    }

    QJsonObject RoutesRepo::routeProfileToJson(const RouteProfile* routeProfile) const {
        QJsonObject json;
        
        json["id"] = routeProfile->id;
        json["name"] = routeProfile->name;
        json["defaultOutboundID"] = routeProfile->defaultOutboundID;
        json["isRaw"] = routeProfile->isRaw;
        json["rawRoute"] = routeProfile->rawRoute;
        json["preventModifications"] = routeProfile->preventModifications;
        json["isRemote"] = routeProfile->isRemote;
        json["remoteURL"] = routeProfile->remoteURL;
        json["autoUpdate"] = routeProfile->autoUpdate;
        json["remoteLastUpdate"] = routeProfile->remoteLastUpdate;

        QJsonArray endpointsArray;
        for (const int endpointID : routeProfile->endpointProfileIDs) endpointsArray.append(endpointID);
        json["endpointProfileIDs"] = endpointsArray;

        QJsonArray innerHopsArray;
        for (const int endpointID : routeProfile->innerHopEndpointIDs) innerHopsArray.append(endpointID);
        json["innerHopEndpointIDs"] = innerHopsArray;

        QJsonArray rulesArray;
        for (const auto& rule : routeProfile->Rules) {
            rulesArray.append(routeRuleToJson(rule.get()));
        }
        json["rules"] = rulesArray;
        
        return json;
    }

    std::shared_ptr<RouteProfile> RoutesRepo::routeProfileFromJson(const QJsonObject& json) const {
        auto routeProfile = std::make_shared<RouteProfile>();
        
        routeProfile->id = json["id"].toInt();
        routeProfile->name = json["name"].toString();
        routeProfile->defaultOutboundID = json["defaultOutboundID"].toInt();
        routeProfile->isRaw = json["isRaw"].toBool();
        routeProfile->rawRoute = json["rawRoute"].toString();
        routeProfile->preventModifications = json["preventModifications"].toBool();
        routeProfile->isRemote = json["isRemote"].toBool();
        routeProfile->remoteURL = json["remoteURL"].toString();
        routeProfile->autoUpdate = json["autoUpdate"].toBool();
        routeProfile->remoteLastUpdate = static_cast<qint64>(json["remoteLastUpdate"].toDouble());
        for (const auto& endpointValue : json["endpointProfileIDs"].toArray()) {
            if (endpointValue.isDouble()) routeProfile->endpointProfileIDs.append(endpointValue.toInt());
        }
        for (const auto& endpointValue : json["innerHopEndpointIDs"].toArray()) {
            if (endpointValue.isDouble()) routeProfile->innerHopEndpointIDs.append(endpointValue.toInt());
        }

        if (json.contains("rules") && json["rules"].isArray()) {
            QJsonArray rulesArray = json["rules"].toArray();
            for (const auto& ruleValue : rulesArray) {
                if (ruleValue.isObject()) {
                    auto rule = routeRuleFromJson(ruleValue.toObject());
                    routeProfile->Rules.append(rule);
                }
            }
        }
        
        return routeProfile;
    }

    void RoutesRepo::saveToDatabase(const RouteProfile* routeProfile, int id) const {
        try {
            db.execThrow("BEGIN IMMEDIATE");
            saveToDatabaseInTx(routeProfile, id);
            db.execThrow("COMMIT");
        } catch (std::exception& e) {
            try { db.execThrow("ROLLBACK"); } catch (...) {}
            NotifyError("RoutesRepo::saveToDatabase", e);
        }
    }

    void RoutesRepo::saveToDatabaseInTx(const RouteProfile* routeProfile, int id) const {
        QJsonArray endpointsArray;
        for (const int endpointID : routeProfile->endpointProfileIDs) endpointsArray.append(endpointID);
        const QString endpointsJson = QString::fromUtf8(QJsonDocument(endpointsArray).toJson(QJsonDocument::Compact));

        QJsonArray innerHopsArray;
        for (const int endpointID : routeProfile->innerHopEndpointIDs) innerHopsArray.append(endpointID);
        const QString innerHopsJson = QString::fromUtf8(QJsonDocument(innerHopsArray).toJson(QJsonDocument::Compact));

        db.execThrow(R"(
            INSERT INTO route_profiles (id, name, default_outbound_id, is_raw, raw_route, prevent_modifications,
                is_remote, remote_url, auto_update, remote_last_update, endpoint_profile_ids,
                inner_hop_endpoint_ids)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(id) DO UPDATE SET
                name = excluded.name, default_outbound_id = excluded.default_outbound_id,
                is_raw = excluded.is_raw, raw_route = excluded.raw_route,
                prevent_modifications = excluded.prevent_modifications,
                is_remote = excluded.is_remote, remote_url = excluded.remote_url,
                auto_update = excluded.auto_update, remote_last_update = excluded.remote_last_update,
                endpoint_profile_ids = excluded.endpoint_profile_ids,
                inner_hop_endpoint_ids = excluded.inner_hop_endpoint_ids,
                updated_at = strftime('%s', 'now')
        )",
            id,
            routeProfile->name.toStdString(),
            routeProfile->defaultOutboundID,
            routeProfile->isRaw ? 1 : 0,
            routeProfile->rawRoute.toStdString(),
            routeProfile->preventModifications ? 1 : 0,
            routeProfile->isRemote ? 1 : 0,
            routeProfile->remoteURL.toStdString(),
            routeProfile->autoUpdate ? 1 : 0,
            static_cast<long long>(routeProfile->remoteLastUpdate),
            endpointsJson.toStdString(),
            innerHopsJson.toStdString()
        );

        db.execThrow("DELETE FROM route_rules WHERE route_profile_id = ?", id);

        int ruleOrder = 0;
        for (const auto& rule : routeProfile->Rules) {
            QJsonArray inboundArray = QListStr2QJsonArray(rule->inbound);
            QJsonArray domainArray = QListStr2QJsonArray(rule->domain);
            QJsonArray domainSuffixArray = QListStr2QJsonArray(rule->domain_suffix);
            QJsonArray domainKeywordArray = QListStr2QJsonArray(rule->domain_keyword);
            QJsonArray domainRegexArray = QListStr2QJsonArray(rule->domain_regex);
            QJsonArray sourceIpCidrArray = QListStr2QJsonArray(rule->source_ip_cidr);
            QJsonArray ipCidrArray = QListStr2QJsonArray(rule->ip_cidr);
            QJsonArray sourcePortArray = QListStr2QJsonArray(rule->source_port);
            QJsonArray sourcePortRangeArray = QListStr2QJsonArray(rule->source_port_range);
            QJsonArray portArray = QListStr2QJsonArray(rule->port);
            QJsonArray portRangeArray = QListStr2QJsonArray(rule->port_range);
            QJsonArray processNameArray = QListStr2QJsonArray(rule->process_name);
            QJsonArray processPathArray = QListStr2QJsonArray(rule->process_path);
            QJsonArray processPathRegexArray = QListStr2QJsonArray(rule->process_path_regex);
            QJsonArray ruleSetArray = QListStr2QJsonArray(rule->rule_set);
            QJsonArray sniffersArray = QListStr2QJsonArray(rule->sniffers);
            QJsonArray wifiSsidArray = QListStr2QJsonArray(rule->wifi_ssid);
            QJsonArray wifiBssidArray = QListStr2QJsonArray(rule->wifi_bssid);
            
            QString inboundJson = QString::fromUtf8(QJsonDocument(inboundArray).toJson(QJsonDocument::Compact));
            QString domainJson = QString::fromUtf8(QJsonDocument(domainArray).toJson(QJsonDocument::Compact));
            QString domainSuffixJson = QString::fromUtf8(QJsonDocument(domainSuffixArray).toJson(QJsonDocument::Compact));
            QString domainKeywordJson = QString::fromUtf8(QJsonDocument(domainKeywordArray).toJson(QJsonDocument::Compact));
            QString domainRegexJson = QString::fromUtf8(QJsonDocument(domainRegexArray).toJson(QJsonDocument::Compact));
            QString sourceIpCidrJson = QString::fromUtf8(QJsonDocument(sourceIpCidrArray).toJson(QJsonDocument::Compact));
            QString ipCidrJson = QString::fromUtf8(QJsonDocument(ipCidrArray).toJson(QJsonDocument::Compact));
            QString sourcePortJson = QString::fromUtf8(QJsonDocument(sourcePortArray).toJson(QJsonDocument::Compact));
            QString sourcePortRangeJson = QString::fromUtf8(QJsonDocument(sourcePortRangeArray).toJson(QJsonDocument::Compact));
            QString portJson = QString::fromUtf8(QJsonDocument(portArray).toJson(QJsonDocument::Compact));
            QString portRangeJson = QString::fromUtf8(QJsonDocument(portRangeArray).toJson(QJsonDocument::Compact));
            QString processNameJson = QString::fromUtf8(QJsonDocument(processNameArray).toJson(QJsonDocument::Compact));
            QString processPathJson = QString::fromUtf8(QJsonDocument(processPathArray).toJson(QJsonDocument::Compact));
            QString processPathRegexJson = QString::fromUtf8(QJsonDocument(processPathRegexArray).toJson(QJsonDocument::Compact));
            QString ruleSetJson = QString::fromUtf8(QJsonDocument(ruleSetArray).toJson(QJsonDocument::Compact));
            QString sniffersJson = QString::fromUtf8(QJsonDocument(sniffersArray).toJson(QJsonDocument::Compact));
            QString wifiSsidJson = QString::fromUtf8(QJsonDocument(wifiSsidArray).toJson(QJsonDocument::Compact));
            QString wifiBssidJson = QString::fromUtf8(QJsonDocument(wifiBssidArray).toJson(QJsonDocument::Compact));
            
            db.execThrow(R"(
                INSERT INTO route_rules
                (route_profile_id, rule_order, name, type, ip_version, network, protocol,
                 inbound_json, domain_json, domain_suffix_json, domain_keyword_json, domain_regex_json,
                 source_ip_cidr_json, source_ip_is_private, ip_cidr_json, ip_is_private,
                 source_port_json, source_port_range_json, port_json, port_range_json,
                 process_name_json, process_path_json, process_path_regex_json, rule_set_json,
                 invert, outbound_id, action, reject_method, no_drop,
                 override_address, override_port, sniffers_json, sniff_override_dest, strategy,
                 wifi_ssid_json, wifi_bssid_json, tls_spoof, tls_spoof_method)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            )",
                id,
                ruleOrder++,
                rule->name.toStdString(),
                rule->type,
                rule->ip_version.toStdString(),
                rule->network.toStdString(),
                rule->protocol.toStdString(),
                inboundJson.toStdString(),
                domainJson.toStdString(),
                domainSuffixJson.toStdString(),
                domainKeywordJson.toStdString(),
                domainRegexJson.toStdString(),
                sourceIpCidrJson.toStdString(),
                rule->source_ip_is_private ? 1 : 0,
                ipCidrJson.toStdString(),
                rule->ip_is_private ? 1 : 0,
                sourcePortJson.toStdString(),
                sourcePortRangeJson.toStdString(),
                portJson.toStdString(),
                portRangeJson.toStdString(),
                processNameJson.toStdString(),
                processPathJson.toStdString(),
                processPathRegexJson.toStdString(),
                ruleSetJson.toStdString(),
                rule->invert ? 1 : 0,
                rule->outboundID,
                rule->action.toStdString(),
                rule->rejectMethod.toStdString(),
                rule->no_drop ? 1 : 0,
                rule->override_address.toStdString(),
                rule->override_port.toStdString(),
                sniffersJson.toStdString(),
                rule->sniffOverrideDest ? 1 : 0,
                rule->strategy.toStdString(),
                wifiSsidJson.toStdString(),
                wifiBssidJson.toStdString(),
                rule->tls_spoof.toStdString(),
                rule->tls_spoof_method.toStdString()
            );
        }
    }

    QJsonObject RoutesRepo::ruleJsonFromRow(SQLite::Statement& stmt, int baseCol) const {
        QJsonObject ruleJson;
        ruleJson["name"] = QString::fromStdString(stmt.getColumn(baseCol + 0).getText());
        ruleJson["type"] = stmt.getColumn(baseCol + 1).getInt();
        ruleJson["ip_version"] = QString::fromStdString(stmt.getColumn(baseCol + 2).getText());
        ruleJson["network"] = QString::fromStdString(stmt.getColumn(baseCol + 3).getText());
        ruleJson["protocol"] = QString::fromStdString(stmt.getColumn(baseCol + 4).getText());
        
        auto parseJsonArray = [](const std::string& s) {
            QJsonDocument doc = QJsonDocument::fromJson(QString::fromStdString(s).toUtf8());
            return doc.isArray() ? doc.array() : QJsonArray();
        };
        ruleJson["inbound"] = parseJsonArray(stmt.getColumn(baseCol + 5).getText());
        ruleJson["domain"] = parseJsonArray(stmt.getColumn(baseCol + 6).getText());
        ruleJson["domain_suffix"] = parseJsonArray(stmt.getColumn(baseCol + 7).getText());
        ruleJson["domain_keyword"] = parseJsonArray(stmt.getColumn(baseCol + 8).getText());
        ruleJson["domain_regex"] = parseJsonArray(stmt.getColumn(baseCol + 9).getText());
        ruleJson["source_ip_cidr"] = parseJsonArray(stmt.getColumn(baseCol + 10).getText());
        ruleJson["source_ip_is_private"] = stmt.getColumn(baseCol + 11).getInt() != 0;
        ruleJson["ip_cidr"] = parseJsonArray(stmt.getColumn(baseCol + 12).getText());
        ruleJson["ip_is_private"] = stmt.getColumn(baseCol + 13).getInt() != 0;
        ruleJson["source_port"] = parseJsonArray(stmt.getColumn(baseCol + 14).getText());
        ruleJson["source_port_range"] = parseJsonArray(stmt.getColumn(baseCol + 15).getText());
        ruleJson["port"] = parseJsonArray(stmt.getColumn(baseCol + 16).getText());
        ruleJson["port_range"] = parseJsonArray(stmt.getColumn(baseCol + 17).getText());
        ruleJson["process_name"] = parseJsonArray(stmt.getColumn(baseCol + 18).getText());
        ruleJson["process_path"] = parseJsonArray(stmt.getColumn(baseCol + 19).getText());
        ruleJson["process_path_regex"] = parseJsonArray(stmt.getColumn(baseCol + 20).getText());
        ruleJson["rule_set"] = parseJsonArray(stmt.getColumn(baseCol + 21).getText());
        ruleJson["invert"] = stmt.getColumn(baseCol + 22).getInt() != 0;
        ruleJson["outboundID"] = stmt.getColumn(baseCol + 23).getInt();
        ruleJson["action"] = QString::fromStdString(stmt.getColumn(baseCol + 24).getText());
        ruleJson["rejectMethod"] = QString::fromStdString(stmt.getColumn(baseCol + 25).getText());
        ruleJson["no_drop"] = stmt.getColumn(baseCol + 26).getInt() != 0;
        ruleJson["override_address"] = QString::fromStdString(stmt.getColumn(baseCol + 27).getText());
        ruleJson["override_port"] = QString::fromStdString(stmt.getColumn(baseCol + 28).getText());
        ruleJson["sniffers"] = parseJsonArray(stmt.getColumn(baseCol + 29).getText());
        ruleJson["sniffOverrideDest"] = stmt.getColumn(baseCol + 30).getInt() != 0;
        ruleJson["strategy"] = QString::fromStdString(stmt.getColumn(baseCol + 31).getText());
        ruleJson["wifi_ssid"] = parseJsonArray(stmt.getColumn(baseCol + 32).getText());
        ruleJson["wifi_bssid"] = parseJsonArray(stmt.getColumn(baseCol + 33).getText());
        ruleJson["tls_spoof"] = QString::fromStdString(stmt.getColumn(baseCol + 34).getText());
        ruleJson["tls_spoof_method"] = QString::fromStdString(stmt.getColumn(baseCol + 35).getText());
        return ruleJson;
    }

    std::shared_ptr<RouteProfile> RoutesRepo::routeProfileFromProfileRow(SQLite::Statement& stmt) const {
        QJsonObject json;
        json["id"] = stmt.getColumn(0).getInt();
        json["name"] = QString::fromStdString(stmt.getColumn(1).getText());
        json["defaultOutboundID"] = stmt.getColumn(2).getInt();
        json["isRaw"] = stmt.getColumn(3).getInt() != 0;
        json["rawRoute"] = QString::fromStdString(stmt.getColumn(4).getText());
        json["preventModifications"] = stmt.getColumn(5).getInt() != 0;
        json["isRemote"] = stmt.getColumn(6).getInt() != 0;
        json["remoteURL"] = QString::fromStdString(stmt.getColumn(7).getText());
        json["autoUpdate"] = stmt.getColumn(8).getInt() != 0;
        json["remoteLastUpdate"] = static_cast<double>(stmt.getColumn(9).getInt64());
        const auto endpointsDoc = QJsonDocument::fromJson(QString::fromStdString(stmt.getColumn(10).getText()).toUtf8());
        json["endpointProfileIDs"] = endpointsDoc.isArray() ? endpointsDoc.array() : QJsonArray();
        const auto innerHopsDoc = QJsonDocument::fromJson(QString::fromStdString(stmt.getColumn(11).getText()).toUtf8());
        json["innerHopEndpointIDs"] = innerHopsDoc.isArray() ? innerHopsDoc.array() : QJsonArray();
        json["rules"] = QJsonArray();
        return routeProfileFromJson(json);
    }

    void RoutesRepo::loadRulesForProfileIdsChunk(const QList<int>& profileIds, std::map<int, std::shared_ptr<RouteProfile>>& byId) const {
        if (profileIds.isEmpty()) return;
        QString idList;
        for (int i = 0; i < profileIds.size(); ++i) {
            if (i > 0) idList += ",";
            idList += QString::number(profileIds[i]);
        }
        std::string sql =
            "SELECT route_profile_id, name, type, ip_version, network, protocol, "
            "inbound_json, domain_json, domain_suffix_json, domain_keyword_json, domain_regex_json, "
            "source_ip_cidr_json, source_ip_is_private, ip_cidr_json, ip_is_private, "
            "source_port_json, source_port_range_json, port_json, port_range_json, "
            "process_name_json, process_path_json, process_path_regex_json, rule_set_json, "
            "invert, outbound_id, action, reject_method, no_drop, "
            "override_address, override_port, sniffers_json, sniff_override_dest, strategy, "
            "wifi_ssid_json, wifi_bssid_json, tls_spoof, tls_spoof_method "
            "FROM route_rules WHERE route_profile_id IN (" + idList.toStdString() + ") ORDER BY route_profile_id, rule_order";
        auto rulesQuery = db.query(sql);
        if (!rulesQuery) return;
        while (rulesQuery->executeStep()) {
            int profileId = rulesQuery->getColumn(0).getInt();
            auto it = byId.find(profileId);
            if (it != byId.end()) {
                it->second->Rules.append(routeRuleFromJson(ruleJsonFromRow(*rulesQuery, 1)));
            }
        }
    }

    std::shared_ptr<RouteProfile> RoutesRepo::loadFromDatabase(int id) const {
        auto profileQuery = db.query(R"(
            SELECT id, name, default_outbound_id, is_raw, raw_route, prevent_modifications,
                   is_remote, remote_url, auto_update, remote_last_update, endpoint_profile_ids,
                   inner_hop_endpoint_ids
            FROM route_profiles WHERE id = ?
        )", id);
        if (!profileQuery || !profileQuery->executeStep()) {
            return nullptr;
        }
        
        auto routeProfile = routeProfileFromProfileRow(*profileQuery);
        
        auto rulesQuery = db.query(R"(
            SELECT name, type, ip_version, network, protocol,
                   inbound_json, domain_json, domain_suffix_json, domain_keyword_json, domain_regex_json,
                   source_ip_cidr_json, source_ip_is_private, ip_cidr_json, ip_is_private,
                   source_port_json, source_port_range_json, port_json, port_range_json,
                   process_name_json, process_path_json, process_path_regex_json, rule_set_json,
                   invert, outbound_id, action, reject_method, no_drop,
                   override_address, override_port, sniffers_json, sniff_override_dest, strategy,
                   wifi_ssid_json, wifi_bssid_json, tls_spoof, tls_spoof_method
            FROM route_rules WHERE route_profile_id = ? ORDER BY rule_order
        )", id);
        if (rulesQuery) {
            while (rulesQuery->executeStep()) {
                routeProfile->Rules.append(routeRuleFromJson(ruleJsonFromRow(*rulesQuery, 0)));
            }
        }
        
        return routeProfile;
    }

    std::shared_ptr<RouteProfile> RoutesRepo::NewRouteProfile() {
        return std::make_shared<RouteProfile>();
    }

    bool RoutesRepo::AddRouteProfile(std::shared_ptr<RouteProfile>& routeProfile) {
        if (routeProfile->id >= 0) return false;
        int newId = NewRouteProfileID();
        routeProfile->id = newId;
        QMutexLocker locker(&mutex);
        identityMap[newId] = std::weak_ptr<RouteProfile>(routeProfile);
        saveToDatabase(routeProfile.get(), routeProfile->id);
        return true;
    }

    std::shared_ptr<RouteProfile> RoutesRepo::GetRouteProfile(int id) const {
        QMutexLocker locker(&mutex);
        if (auto it = identityMap.find(id); it != identityMap.end()) {
            if (auto shared = it->second.lock()) return shared;
            identityMap.erase(it);
        }
        auto routeProfile = loadFromDatabase(id);
        if (!routeProfile) return nullptr;
        identityMap[id] = std::weak_ptr<RouteProfile>(routeProfile);
        return routeProfile;
    }

    void RoutesRepo::DeleteRouteProfile(int id) {
        QMutexLocker locker(&mutex);
        identityMap.erase(id);
        db.exec("DELETE FROM route_profiles WHERE id = ?", id);
    }

    void RoutesRepo::UpdateRouteProfiles(const QList<std::shared_ptr<RouteProfile>>& routeProfiles) {
        QSet<int> existingIds;
        auto query = db.query("SELECT id FROM route_profiles");
        if (query) {
            while (query->executeStep()) {
                existingIds.insert(query->getColumn(0).getInt());
            }
        }

        QMutexLocker locker(&mutex);
        QSet<int> newIds;
        for (const auto& routeProfile : routeProfiles) {
            newIds.insert(routeProfile->id);
            if (routeProfile->id < 0) {
                routeProfile->id = NewRouteProfileID();
            }
            saveToDatabase(routeProfile.get(), routeProfile->id);
        }

        std::vector<int> toDelete;
        for (int id : existingIds) {
            if (!newIds.contains(id)) toDelete.push_back(id);
        }

        for (const auto& routeProfile : routeProfiles) {
            identityMap[routeProfile->id] = std::weak_ptr<RouteProfile>(routeProfile);
        }
        for (int id : toDelete) identityMap.erase(id);
        if (!toDelete.empty()) {
            db.execDeleteByIdIn("route_profiles", "id", toDelete);
        }
    }

    QList<int> RoutesRepo::GetAllRouteProfileIds() const {
        QList<int> ids;
        auto query = db.query("SELECT id FROM route_profiles ORDER BY id");
        if (query) {
            while (query->executeStep()) {
                ids.append(query->getColumn(0).getInt());
            }
        }
        return ids;
    }

    QList<std::shared_ptr<RouteProfile>> RoutesRepo::GetAllRouteProfiles() const {
        QList<std::shared_ptr<RouteProfile>> routeProfiles;
        std::map<int, std::shared_ptr<RouteProfile>> byId;
        QList<int> idsInOrder;
        QSet<int> cachedProfiles;

        auto profileQuery = db.query("SELECT id, name, default_outbound_id, is_raw, raw_route, prevent_modifications, is_remote, remote_url, auto_update, remote_last_update, endpoint_profile_ids, inner_hop_endpoint_ids FROM route_profiles ORDER BY id");
        if (!profileQuery) return routeProfiles;

        QMutexLocker locker(&mutex);
        while (profileQuery->executeStep()) {
            int id = profileQuery->getColumn(0).getInt();
            std::shared_ptr<RouteProfile> profile;
            auto it = identityMap.find(id);
            if (it != identityMap.end()) {
                if (auto shared = it->second.lock()) {
                    byId[id] = shared;
                    idsInOrder.append(id);
                    cachedProfiles.insert(id);
                    continue;
                }
                identityMap.erase(it);
            }
            profile = routeProfileFromProfileRow(*profileQuery);
            byId[id] = profile;
            idsInOrder.append(id);
            identityMap[id] = std::weak_ptr<RouteProfile>(profile);
        }

        if (byId.empty()) return routeProfiles;

        for (int off = 0; off < idsInOrder.size(); off += Configs::BATCH_LIMIT_READ) {
            int end = std::min(off + Configs::BATCH_LIMIT_READ, static_cast<int>(idsInOrder.size()));
            QList<int> chunk;
            for (int i = off; i < end; ++i) {
                if (!cachedProfiles.contains(idsInOrder[i])) chunk.append(idsInOrder[i]);
            }
            if (chunk.isEmpty()) continue;
            loadRulesForProfileIdsChunk(chunk, byId);
        }
        
        for (int id : idsInOrder) {
            routeProfiles.append(byId[id]);
        }
        return routeProfiles;
    }

    int RoutesRepo::NewRouteProfileID() const {
        auto query = db.query("UPDATE entity_ids SET route_profile_last_id = route_profile_last_id + 1 RETURNING route_profile_last_id");
        if (query && query->executeStep()) {
            return query->getColumn(0).getInt();
        }
        
        return 0;
    }

    bool RoutesRepo::Save(const std::shared_ptr<RouteProfile>& routeProfile) {
        if (!routeProfile) {
            return false;
        }
        
        if (routeProfile->id < 0) {
            return false;
        }
        
        QMutexLocker locker(&mutex);
        saveToDatabase(routeProfile.get(), routeProfile->id);
        identityMap[routeProfile->id] = std::weak_ptr<RouteProfile>(routeProfile);
        
        return true;
    }
}
