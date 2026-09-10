#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrlQuery>
#include "include/database/entities/RouteProfile.h"
#include <array>
#include <iostream>
#include <optional>

#include "include/database/ProfilesRepo.h"

#include "include/configs/common/OutboundFactory.h"
#include "include/configs/generate.h"
#include "include/global/Configs.hpp"

namespace Configs {
    bool isOutboundIDValid(int id) {
        switch (id) {
            case -1:
            case -2:
                return true;
            default:
                return Configs::dataManager->profilesRepo->GetProfile(id) != nullptr;
        }
    }

    int getOutboundID(const QString& name) {
        if (name == "proxy") return -1;
        if (name == "direct") return -2;
        if (const auto &profile = Configs::dataManager->profilesRepo->GetProfileByName(name)) return profile->id;

        return INVALID_ID;
    }

    static void collectRawOutboundIdsRec(const QJsonValue& node, QList<int>& out) {
        if (node.isObject()) {
            const QJsonObject o = node.toObject();
            for (auto it = o.begin(); it != o.end(); ++it) {
                if ((it.key() == "outbound" || it.key() == "final") && it.value().isDouble()) {
                    const int id = it.value().toInt();
                    if (!out.contains(id)) out.append(id);
                } else {
                    collectRawOutboundIdsRec(it.value(), out);
                }
            }
        } else if (node.isArray()) {
            for (const auto& e : node.toArray()) collectRawOutboundIdsRec(e, out);
        }
    }

    QList<int> RouteProfile::CollectRawOutboundIds(const QJsonObject& route) {
        QList<int> out;
        collectRawOutboundIdsRec(route, out);
        return out;
    }

    static QJsonValue translateRawOutboundsRec(const QJsonValue& node, const std::map<int, QString>& outboundMap) {
        if (node.isObject()) {
            const QJsonObject o = node.toObject();
            QJsonObject res;
            for (auto it = o.begin(); it != o.end(); ++it) {
                if ((it.key() == "outbound" || it.key() == "final") && it.value().isDouble()) {
                    const int id = it.value().toInt();
                    auto found = outboundMap.find(id);
                    res[it.key()] = found != outboundMap.end() ? QJsonValue(found->second) : QJsonValue("proxy");
                } else {
                    res[it.key()] = translateRawOutboundsRec(it.value(), outboundMap);
                }
            }
            return res;
        }
        if (node.isArray()) {
            QJsonArray res;
            for (const auto& e : node.toArray()) res.append(translateRawOutboundsRec(e, outboundMap));
            return res;
        }
        return node;
    }

    QJsonObject RouteProfile::TranslateRawOutbounds(const QJsonObject& route, const std::map<int, QString>& outboundMap) {
        return translateRawOutboundsRec(route, outboundMap).toObject();
    }

    static QJsonValue remapRawOutboundsByNameRec(const QJsonValue& node, const QJsonObject& names, QString* warnings) {
        if (node.isObject()) {
            const QJsonObject o = node.toObject();
            QJsonObject res;
            for (auto it = o.begin(); it != o.end(); ++it) {
                if ((it.key() == "outbound" || it.key() == "final") && it.value().isDouble()) {
                    const int id = it.value().toInt();
                    if (id < 0) { res[it.key()] = id; continue; }
                    const QString nm = names.value(QString::number(id)).toString();
                    std::shared_ptr<Profile> local;
                    if (!nm.isEmpty()) local = Configs::dataManager->profilesRepo->GetProfileByName(nm);
                    if (local) {
                        res[it.key()] = local->id;
                    } else {
                        res[it.key()] = static_cast<int>(proxyID);
                        if (warnings) warnings->append(QString("outbound \"%1\" not found, using proxy\n").arg(nm.isEmpty() ? QString::number(id) : nm));
                    }
                } else {
                    res[it.key()] = remapRawOutboundsByNameRec(it.value(), names, warnings);
                }
            }
            return res;
        }
        if (node.isArray()) {
            QJsonArray res;
            for (const auto& e : node.toArray()) res.append(remapRawOutboundsByNameRec(e, names, warnings));
            return res;
        }
        return node;
    }

    static QJsonObject remapRawOutboundsByName(const QJsonObject& route, const QJsonObject& names, QString* warnings) {
        return remapRawOutboundsByNameRec(route, names, warnings).toObject();
    }

    QList<std::shared_ptr<RouteRule>> RouteProfile::get_simple_rules() {
        struct RuleConfig {
            ruleType type;
            QStringView action;
            std::optional<int> outboundID;
        };

        static constexpr std::array kConfigs = {
            RuleConfig{simpleAddressProxy, u"route", proxyID},
            RuleConfig{simpleAddressBypass, u"route", directID},
            RuleConfig{simpleAddressBlock, u"reject", std::nullopt},

            RuleConfig{simpleProcessNameProxy, u"route", proxyID},
            RuleConfig{simpleProcessNameBypass, u"route", directID},
            RuleConfig{simpleProcessNameBlock, u"reject", std::nullopt},

            RuleConfig{simpleProcessPathProxy, u"route", proxyID},
            RuleConfig{simpleProcessPathBypass, u"route", directID},
            RuleConfig{simpleProcessPathBlock, u"reject", std::nullopt},

            RuleConfig{simpleAddressWarpBypass, u"route", warpBypassID},
            RuleConfig{simpleProcessNameWarpBypass, u"route", warpBypassID},
            RuleConfig{simpleProcessPathWarpBypass, u"route", warpBypassID},
        };

        QList<std::shared_ptr<RouteRule>> rules;
        rules.reserve(kConfigs.size());
        for (const auto &config : kConfigs) {
            auto rule = std::make_shared<RouteRule>();
            rule->type = config.type;
            rule->action = config.action.toString();
            rule->name = ruleTypeToString(config.type);
            if (config.outboundID) rule->outboundID = *config.outboundID;
            rules.append(std::move(rule));
        }
        return rules;
    }

    void RouteProfile::reset_simple_rule(std::shared_ptr<RouteRule>& rule) {
        auto cleanRules = get_simple_rules();
        for (auto &r : cleanRules) {
            if (r->type == rule->type) {
                rule = std::move(r);
                return;
            }
        }
    }

    RouteProfile::RouteProfile(const RouteProfile& other) {
        id = other.id;
        name = QString(other.name);
        for (const auto& item: other.Rules) {
            Rules.push_back(std::make_shared<RouteRule>(*item));
        }
        defaultOutboundID = other.defaultOutboundID;
        isRaw = other.isRaw;
        rawRoute = other.rawRoute;
        preventModifications = other.preventModifications;
        isRemote = other.isRemote;
        remoteURL = other.remoteURL;
        autoUpdate = other.autoUpdate;
        remoteLastUpdate = other.remoteLastUpdate;
        endpointProfileIDs = other.endpointProfileIDs;
        innerHopEndpointIDs = other.innerHopEndpointIDs;
    }

    static void appendWarning(QString* warnings, const QString& msg) {
        if (warnings) warnings->append(msg + "\n");
    }

    // name/type are schema-only keys: skipped here, applied by the caller.
    static std::shared_ptr<RouteRule> parse_rule_object(const QJsonObject& obj, QString* warnings) {
        auto rule = std::make_shared<RouteRule>();
        for (const auto& key: obj.keys()) {
            if (key == "name" || key == "type") continue;
            auto val = obj.value(key);
            if (key == "outbound") {
                if (val.isDouble()) {
                    const int id = val.toInt();
                    if (isOutboundIDValid(id)) {
                        rule->outboundID = id;
                    } else {
                        appendWarning(warnings, QString("outbound id %1 not found, using proxy").arg(id));
                        rule->outboundID = proxyID;
                    }
                } else if (val.isString()) {
                    const int id = getOutboundID(val.toString());
                    if (id != INVALID_ID) {
                        rule->outboundID = id;
                    } else {
                        appendWarning(warnings, QString("outbound \"%1\" not found, using proxy").arg(val.toString()));
                        rule->outboundID = proxyID;
                    }
                }
            } else if (val.isArray()) {
                rule->set_field_value(key, QJsonArray2QListString(val.toArray()));
            } else if (val.isString()) {
                rule->set_field_value(key, {val.toString()});
            } else if (val.isBool()) {
                rule->set_field_value(key, {val.toBool() ? "true":"false"});
            }
        }
        return rule;
    }

    QList<std::shared_ptr<RouteRule>> RouteProfile::parseJsonArray(const QJsonArray& arr, QString* parseError, QString* warnings) {
        if (arr.empty()) {
            parseError->append("Input is not a valid json array");
            return {};
        }

        auto rules = QList<std::shared_ptr<RouteRule>>();
        auto ruleID = 1;
        for (const auto& item: arr) {
            if (!item.isObject()) {
                parseError->append(QString("expected array of json objects but have member of type '%1'").arg(item.type()));
                return {};
            }
            const QJsonObject ro = item.toObject();
            auto rule = parse_rule_object(ro, warnings);
            const QString nm = ro.value("name").toString();
            rule->name = nm.isEmpty() ? ("imported rule #" + Int2String(ruleID++)) : nm;
            rules << rule;
        }

        return rules;
    }

    // The repo hands back the live profile, so strip a round-tripped clone of it instead.
    static QJsonObject routeProfileStrippedConfig(const std::shared_ptr<Profile>& ent) {
        const std::shared_ptr<outbound> clone(NewOutboundByType(ent->type));
        if (clone->invalid) return {};
        clone->ParseFromJson(ent->outbound->ExportToJson());
        clone->StripCredentials();
        return clone->ExportToJson();
    }

    // A chain also carries its hops in `list` order, so hops[i] describes config["list"][i].
    static QJsonObject routeProfileEndpointToJson(int id, bool innerHops, QString* warnings) {
        const auto ent = Configs::dataManager->profilesRepo->GetProfile(id);
        if (ent == nullptr || ent->outbound == nullptr) {
            appendWarning(warnings, QString("endpoint profile id %1 no longer exists, not shared").arg(id));
            return {};
        }
        const QString label = ent->outbound->DisplayTypeAndName();
        QList<std::shared_ptr<Profile>> hops;
        if (const auto ch = ent->Chain(); ch != nullptr) {
            for (const int hopID: ch->list) {
                if (auto hop = Configs::dataManager->profilesRepo->GetProfile(hopID);
                    hop != nullptr && hop->outbound != nullptr) hops << hop;
            }
            if (hops.isEmpty() || hops.size() != ch->list.size()) {
                appendWarning(warnings, QString("endpoint %1 has missing hops, not shared").arg(label));
                return {};
            }
        }
        auto strippable = [](const std::shared_ptr<Profile>& p) {
            return p->outbound->SupportsCredentialStrip();
        };
        if (!strippable(ent) || !std::all_of(hops.begin(), hops.end(), strippable)) {
            appendWarning(warnings, QString("endpoint %1 uses a protocol whose credentials cannot be cleared, not shared").arg(label));
            return {};
        }

        QJsonObject entry;
        entry["id"] = id;
        entry["config"] = routeProfileStrippedConfig(ent);
        if (!hops.isEmpty()) {
            QJsonArray hopArr;
            for (const auto& hop: hops) {
                hopArr.append(QJsonObject{{"id", hop->id}, {"config", routeProfileStrippedConfig(hop)}});
            }
            entry["hops"] = hopArr;
            if (innerHops) entry["inner_hops"] = true;
        }
        return entry;
    }

    static QJsonArray routeProfileEndpointsToJson(const QList<int>& ids, const QList<int>& innerHopIDs, QString* warnings) {
        QJsonArray arr;
        for (const int id: ids) {
            if (auto entry = routeProfileEndpointToJson(id, innerHopIDs.contains(id), warnings); !entry.isEmpty()) arr.append(entry);
        }
        return arr;
    }

    static QString routeProfileIdentityKey(const std::shared_ptr<Profile>& ent) {
        return ent->type + "|" + QString::fromUtf8(QJsonDocument(ent->outbound->ExportIdentity()).toJson(QJsonDocument::Compact));
    }

    static int routeProfileMatchLocal(const std::shared_ptr<Profile>& candidate) {
        const QString key = routeProfileIdentityKey(candidate);
        for (const int id: Configs::dataManager->profilesRepo->GetProfileIdsByType(candidate->type)) {
            const auto ent = Configs::dataManager->profilesRepo->GetProfile(id);
            if (ent == nullptr || ent->outbound == nullptr) continue;
            if (routeProfileIdentityKey(ent) == key) return id;
        }
        return -1;
    }

    // hopMap rewrites a chain's machine-local hop ids onto the ones created here.
    static int routeProfileAdoptConfig(const QJsonObject& config, QString* warnings, const QMap<int, int>* hopMap) {
        auto ent = ProfilesRepo::NewProfile(config.value("type").toString());
        if (ent == nullptr || ent->outbound == nullptr || ent->outbound->invalid) {
            appendWarning(warnings, QString("shared endpoint uses unknown protocol \"%1\", dropped").arg(config.value("type").toString()));
            return -1;
        }
        ent->outbound->ParseFromJson(config);
        if (const auto ch = ent->Chain(); ch != nullptr) {
            QList<int> mapped;
            for (const int hopID: ch->list) {
                if (hopMap == nullptr || !hopMap->contains(hopID)) return -1;
                mapped << hopMap->value(hopID);
            }
            ch->list = mapped;
        }
        if (const int existing = routeProfileMatchLocal(ent); existing >= 0) return existing;
        if (!Configs::dataManager->profilesRepo->AddProfile(ent)) {
            appendWarning(warnings, QString("could not create endpoint profile \"%1\"").arg(ent->outbound->DisplayName()));
            return -1;
        }
        appendWarning(warnings, QString("created endpoint profile %1").arg(ent->outbound->DisplayTypeAndName()));
        return ent->id;
    }

    static int routeProfileAdoptEndpoint(const QJsonObject& entry, QString* warnings, QMap<int, int>* outHopMap = nullptr) {
        QMap<int, int> hopMap;
        for (const auto& item: entry.value("hops").toArray()) {
            const QJsonObject hop = item.toObject();
            const int original = hop.value("id").toInt(INVALID_ID);
            const int local = original == INVALID_ID ? -1 : routeProfileAdoptConfig(hop.value("config").toObject(), warnings, nullptr);
            if (local < 0) {
                appendWarning(warnings, "a shared endpoint chain has an unusable hop, dropped");
                return -1;
            }
            hopMap[original] = local;
        }
        if (outHopMap != nullptr) *outHopMap = hopMap;
        return routeProfileAdoptConfig(entry.value("config").toObject(), warnings, &hopMap);
    }

    // *idMap is original id -> local id, hops included, so the paired rules can be remapped.
    static QList<int> routeProfileEndpointsFromJson(const QJsonArray& arr, QString* warnings, bool materialize,
                                                   QMap<int, int>* idMap, QList<int>* innerHopIDs) {
        QList<int> ids;
        for (const auto& item: arr) {
            int originalID = INVALID_ID;
            int localID = -1;
            bool innerHops = false;
            QMap<int, int> hopMap;
            if (item.isDouble()) {
                originalID = item.toInt(INVALID_ID);
                localID = originalID;
            } else if (item.isObject()) {
                const QJsonObject entry = item.toObject();
                originalID = entry.value("id").toInt(INVALID_ID);
                innerHops = entry.value("inner_hops").toBool();
                if (!materialize) continue;
                if (originalID != INVALID_ID) localID = routeProfileAdoptEndpoint(entry, warnings, &hopMap);
            }
            if (originalID == INVALID_ID || localID < 0 || ids.contains(localID)) continue;
            const auto profile = Configs::dataManager->profilesRepo->GetProfile(localID);
            if (!CanBeAuxEndpoint(profile)) {
                appendWarning(warnings, QString("endpoint profile id %1 not usable here, dropped").arg(originalID));
                continue;
            }
            ids << localID;
            if (innerHops && innerHopIDs != nullptr) *innerHopIDs << localID;
            if (idMap) {
                (*idMap)[originalID] = localID;
                for (auto it = hopMap.cbegin(); it != hopMap.cend(); ++it) (*idMap)[it.key()] = it.value();
            }
        }
        return ids;
    }

    QJsonObject RouteProfile::ToShareObject(QString* warnings) {
        QJsonObject root;
        root["kind"] = "throne-route-profile";
        root["v"] = 1;
        root["name"] = name;
        QJsonArray endpointsArr;
        if (!endpointProfileIDs.isEmpty()) {
            endpointsArr = routeProfileEndpointsToJson(endpointProfileIDs, innerHopEndpointIDs, warnings);
            if (!endpointsArr.isEmpty()) root["endpoints"] = endpointsArr;
        }
        if (isRaw) {
            root["raw"] = true;
            root["prevent_modifications"] = preventModifications;
            const auto routeObj = QString2QJsonObject(rawRoute);
            root["route"] = routeObj;
            // id -> name of the referenced server profiles, so the importer can re-resolve them on another machine.
            QJsonObject names;
            for (const int oid : CollectRawOutboundIds(routeObj)) {
                if (oid < 0) continue; // predefined outbounds (proxy/direct/warp-bypass) are stable
                if (auto p = Configs::dataManager->profilesRepo->GetProfile(oid))
                    names[QString::number(oid)] = p->name;
            }
            root["outbound_names"] = names;
            return root;
        }
        root["default_outbound"] = outboundIDToString(defaultOutboundID);
        QSet<int> sharedEndpoints;
        for (const auto& entry: endpointsArr) {
            const QJsonObject obj = entry.toObject();
            sharedEndpoints << obj.value("id").toInt(INVALID_ID);
            // The inner hops travel with the entry, so their rules may travel too.
            if (!obj.value("inner_hops").toBool()) continue;
            for (const auto& hop: obj.value("hops").toArray()) sharedEndpoints << hop.toObject().value("id").toInt(INVALID_ID);
        }
        QJsonArray rulesArr;
        for (const auto& rule: Rules) {
            if (rule->type != custom && rule->isEmpty()) continue;
            // a rule and its endpoint drop or survive together
            if (rule->type == endpointPreferredBy && !sharedEndpoints.contains(rule->outboundID)) continue;
            auto obj = rule->to_share_json();
            if (obj.isEmpty()) continue;
            rulesArr.append(obj);
        }
        root["rules"] = rulesArr;
        return root;
    }

    QString RouteProfile::ToShareLink(QString* warnings) {
        const auto json = QJsonDocument(ToShareObject(warnings)).toJson(QJsonDocument::Compact);
        const auto b64 = json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        return QStringLiteral("throne://route/") + QString::fromLatin1(b64);
    }

    std::shared_ptr<RouteProfile> RouteProfile::FromShareInput(const QString& input, QString* fatalError, QString* warnings, bool* wasOldArray, bool materializeEndpoints) {
        if (wasOldArray) *wasOldArray = false;
        QString text = input.trimmed();
        if (text.isEmpty()) {
            fatalError->append("Empty input");
            return nullptr;
        }

        if (text.startsWith("throne://route/", Qt::CaseInsensitive)) {
            const QUrl u(text);
            if (!u.isValid()) {
                fatalError->append("Deep link is invalid");
                return nullptr;
            }
            text = u.path().mid(1);
            if (text.isEmpty()) {
                fatalError->append("Deep link has no data");
                return nullptr;
            }
        }

        QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (doc.isNull()) {
            doc = QJsonDocument::fromJson(QByteArray::fromBase64(text.toUtf8(), QByteArray::Base64UrlEncoding));
            if (doc.isNull())
                doc = QJsonDocument::fromJson(QByteArray::fromBase64(text.toUtf8()));
        }
        if (doc.isNull()) {
            fatalError->append("Input is not valid JSON, base64, or a Throne route link");
            return nullptr;
        }

        if (doc.isObject()) {
            const QJsonObject root = doc.object();
            if (root.value("kind").toString() != QStringLiteral("throne-route-profile")) {
                fatalError->append("Unrecognized route object");
                return nullptr;
            }
            if (root.value("raw").toBool()) {
                auto profile = std::make_shared<RouteProfile>();
                profile->id = -1;
                profile->isRaw = true;
                profile->name = root.value("name").toString();
                profile->preventModifications = root.value("prevent_modifications").toBool();
                profile->endpointProfileIDs = routeProfileEndpointsFromJson(root.value("endpoints").toArray(), warnings, materializeEndpoints, nullptr, &profile->innerHopEndpointIDs);
                QJsonObject routeObj = root.value("route").toObject();
                routeObj = remapRawOutboundsByName(routeObj, root.value("outbound_names").toObject(), warnings);
                profile->rawRoute = QJsonObject2QString(routeObj, false);
                return profile;
            }
            auto profile = std::make_shared<RouteProfile>();
            profile->id = -1;
            profile->name = root.value("name").toString();
            profile->defaultOutboundID = stringToOutboundID(root.value("default_outbound").toString());
            QMap<int, int> endpointIDMap;
            profile->endpointProfileIDs = routeProfileEndpointsFromJson(root.value("endpoints").toArray(), warnings, materializeEndpoints, &endpointIDMap, &profile->innerHopEndpointIDs);
            int fallbackNum = 1;
            for (const auto& v: root.value("rules").toArray()) {
                if (!v.isObject()) continue;
                const QJsonObject ro = v.toObject();
                const ruleType type = tokenToRuleType(ro.value("type").toString());
                // an endpoint rule's outbound is the sharer's profile id; it must survive the proxy fallback
                auto rule = parse_rule_object(ro, type == endpointPreferredBy ? nullptr : warnings);
                rule->type = type;
                if (type == endpointPreferredBy) {
                    rule->outboundID = endpointIDMap.value(ro.value("outbound").toInt(INVALID_ID), INVALID_ID);
                }
                rule->name = ro.value("name").toString();
                if (rule->name.isEmpty()) rule->name = "rule_" + Int2String(fallbackNum++);
                profile->Rules << rule;
            }
            profile->SyncEndpointRules();
            return profile;
        }

        if (doc.isArray()) {
            QString fe;
            auto rules = parseJsonArray(doc.array(), &fe, warnings);
            if (!fe.isEmpty()) {
                fatalError->append(fe);
                return nullptr;
            }
            auto profile = std::make_shared<RouteProfile>();
            profile->id = -1;
            profile->Rules = rules;
            if (wasOldArray) *wasOldArray = true;
            return profile;
        }

        fatalError->append("Unsupported input");
        return nullptr;
    }

    QList<std::shared_ptr<RouteProfile>> RouteProfile::FromRemoteRoutesLink(const QString& input, bool* wasRemoteRouteLink, QString* error) {
        if (wasRemoteRouteLink) *wasRemoteRouteLink = false;
        const QString text = input.trimmed();
        if (!text.startsWith("throne://remoteroute/", Qt::CaseInsensitive)) return {};
        if (wasRemoteRouteLink) *wasRemoteRouteLink = true;

        const QUrl u(text);
        if (!u.isValid()) {
            if (error) *error = "Deep link is invalid";
            return {};
        }
        QString base64 = u.path().mid(1);
        if (base64.isEmpty()) {
            if (error) *error = "Deep link has no data";
            return {};
        }
        const QString data = DecodeB64IfValid(base64);
        if (data.isEmpty()) {
            if (error) *error = "Base64 is invalid.";
            return {};
        }

        QList<std::shared_ptr<RouteProfile>> res;
        for (const auto& v : data.split('\n', Qt::SkipEmptyParts)) {
            const QString eurl = v.trimmed();
            if (!eurl.startsWith("http://", Qt::CaseInsensitive) && !eurl.startsWith("https://", Qt::CaseInsensitive)) continue;
            const QUrl link(eurl);
            if (!link.isValid()) continue;

            auto profile = std::make_shared<RouteProfile>();
            profile->id = -1;
            profile->isRemote = true;
            profile->remoteURL = link.toString(QUrl::RemoveFragment);
            profile->name = link.fragment();
            if (profile->name.isEmpty()) profile->name = QUrl(eurl).host();
            res << profile;
        }
        if (res.isEmpty() && error) *error = "The link did not contain any valid http(s) routing profile URLs.";
        return res;
    }

    QJsonArray RouteProfile::get_route_rules(bool forView, std::map<int, QString> outboundMap) {
        QJsonArray res;
        bool added_adblock = false;
        auto createAdblockRule = []() -> QJsonObject {
            QJsonObject obj;
            obj["action"] = "reject";
            QJsonArray jarray;
            jarray.append("throne-adblocksingbox");
            obj["rule_set"] = jarray;
            return obj;
        };
        for (const auto &item: Rules) {
            if (item->type != custom && item->isEmpty()) continue;
            auto outboundTag = QString();
            if (outboundMap.contains(item->outboundID)) outboundTag = outboundMap[item->outboundID];
            // an endpoint that left the list has no tag to gate on; skip instead of aborting the build
            if (!forView && item->type == endpointPreferredBy && outboundTag.isEmpty()) {
                MW_show_log("Skipping an endpoint rule whose endpoint is no longer in the routing profile");
                continue;
            }
            auto rule_json = item->get_rule_json(forView, outboundTag);
            if (rule_json.empty()) {
                MW_show_log("Aborted generating routing section, an error has occurred");
                return {};
            }
            if (!added_adblock && Configs::dataManager->settingsRepo->adblock_enable && rule_json["action"] == "route") {
                res += createAdblockRule();
                added_adblock = true;
            }                
            res += rule_json;
        }
        if (!added_adblock && Configs::dataManager->settingsRepo->adblock_enable)
            res += createAdblockRule();

        return res;
    }

    std::shared_ptr<RouteRule> RouteProfile::MakeEndpointRule(int endpointProfileID) {
        auto rule = std::make_shared<RouteRule>();
        rule->type = endpointPreferredBy;
        rule->outboundID = endpointProfileID;
        rule->name = ruleTypeToString(endpointPreferredBy);
        if (const auto prof = Configs::dataManager->profilesRepo->GetProfile(endpointProfileID);
            prof != nullptr && prof->outbound != nullptr)
            rule->name = QObject::tr("%1 route prefer").arg(prof->outbound->DisplayName());
        return rule;
    }

    QList<int> RouteProfile::endpointRuleTargets() const {
        QList<int> targets;
        for (const int id: endpointProfileIDs) {
            if (targets.contains(id)) continue;
            targets << id;
            if (!innerHopEndpointIDs.contains(id)) continue;
            for (const int hopID: AuxEndpointInnerHops(id)) {
                if (!targets.contains(hopID)) targets << hopID;
            }
        }
        return targets;
    }

    void RouteProfile::SyncEndpointRules() {
        if (isRaw) return;
        const auto targets = endpointRuleTargets();
        QList<std::shared_ptr<RouteRule>> kept;
        QSet<int> paired;
        for (const auto& rule: Rules) {
            if (rule->type == endpointPreferredBy) {
                if (!targets.contains(rule->outboundID) || paired.contains(rule->outboundID)) continue;
                paired << rule->outboundID;
            }
            kept << rule;
        }
        for (const int id: targets) {
            if (!paired.contains(id)) kept << MakeEndpointRule(id);
        }
        Rules = kept;
    }

    std::shared_ptr<RouteProfile> RouteProfile::GetDefaultChain() {
        auto defaultChain = std::make_shared<RouteProfile>();
        defaultChain->name = "Default";
        auto defaultRule = std::make_shared<RouteRule>();
        defaultRule->name = "Route DNS";
        defaultRule->action = "hijack-dns";
        defaultRule->protocol = "dns";
        defaultChain->Rules << defaultRule;
        return defaultChain;
    }

    std::shared_ptr<QList<int>> RouteProfile::get_used_outbounds() {
        auto res = std::make_shared<QList<int>>();
        if (isRaw) {
            // Raw ids must be collected too, so their servers get built and their domains added to direct DNS.
            *res = CollectRawOutboundIds(QString2QJsonObject(rawRoute));
            return res;
        }
        for (const auto& item: Rules) {
            // its id names an endpoint built from endpointProfileIDs, not a routing outbound
            if (item->type == endpointPreferredBy) continue;
            res->push_back(item->outboundID);
        }
        return res;
    }

    std::shared_ptr<QStringList> RouteProfile::get_used_rule_sets() {
        auto res = std::make_shared<QStringList>();
        for (const auto& item: Rules) {
            for (const auto& ruleItem: item->rule_set) {
                res->push_back(ruleItem);
            }
        }
        return res;
    }

    QStringList RouteProfile::get_direct_sites() {
        auto res = QStringList();
        for (const auto& item: Rules) {
            if (item->outboundID == directID && item->action == "route") {
                for (const auto& rset: item->rule_set) {
                    if (rset.startsWith("geosite-")) res << QString("ruleset:" + rset);
                }
                for (const auto& domain: item->domain) {
                    res << QString("domain:" + domain);
                }
                for (const auto& suffix: item->domain_suffix) {
                    res << QString("suffix:" + suffix);
                }
                for (const auto& keyword: item->domain_keyword) {
                    res << QString("keyword:" + keyword);
                }
                for (const auto& regex: item->domain_regex) {
                    res << QString("regex:" + regex);
                }
            }
        }
        return res;
    }

    QStringList RouteProfile::get_proxy_sites() {
        auto res = QStringList();
        for (const auto& item: Rules) {
            if (item->outboundID == proxyID && item->action == "route") {
                for (const auto& rset: item->rule_set) {
                    if (rset.startsWith("geosite-")) res << QString("ruleset:" + rset);
                }
                for (const auto& domain: item->domain) {
                    res << QString("domain:" + domain);
                }
                for (const auto& suffix: item->domain_suffix) {
                    res << QString("suffix:" + suffix);
                }
                for (const auto& keyword: item->domain_keyword) {
                    res << QString("keyword:" + keyword);
                }
                for (const auto& regex: item->domain_regex) {
                    res << QString("regex:" + regex);
                }
            }
        }
        return res;
    }

    QStringList RouteProfile::get_direct_ips()
    {
        auto res = QStringList();
        for (const auto& item: Rules) {
            if (item->outboundID == directID && item->action == "route") {
                for (const auto& rset: item->rule_set) {
                    if (rset.startsWith("geoip-")) res << QString("ruleset:" + rset);
                }
                for (const auto& domain: item->ip_cidr) {
                    res << QString("ip:" + domain);
                }
            }
        }
        return res;
    }

    QStringList RouteProfile::get_hijacked_ips()
    {
        auto res = QStringList();
        for (const auto& item: Rules) {
            if (item->action == "route" && item->outboundID == directID) continue;
            if (item->action != "route" && item->action != "reject") continue;
            // ip_is_private covers every range the Tun bypass carves out, so it hijacks all of them at once.
            if (item->ip_is_private) res << dataManager->settingsRepo->vpn_private_ranges;
            for (const auto& cidr: item->ip_cidr) res << cidr;
        }
        return res;
    }

    bool RouteProfile::IsEmpty() {
        if (isRaw) return rawRoute.trimmed().isEmpty();
        for (const auto& item: Rules) {
            if (!item->isEmpty()) return false;
        }
        return true;
    }

    void RouteProfile::ResetRules() {
        Rules.clear();
    }

    void RouteProfile::ResetSimpleRule(ruleType type) {
        for (std::shared_ptr<RouteRule> &item: Rules) {
            if (item->type == type) {
                reset_simple_rule(item);
                return;
            }
        }
        auto cleanRules = get_simple_rules();
        for (std::shared_ptr<RouteRule> &item: cleanRules) {
            if (item->type == type) {
                Rules << item;
                return;
            }
        }
    }

    QString RouteProfile::GetSimpleRules(simpleAction action)
    {
        QList<int> types;
        if (action == proxy) {
            types << simpleAddressProxy;
            types << simpleProcessNameProxy;
            types << simpleProcessPathProxy;
        } else if (action == bypass) {
            types << simpleAddressBypass;
            types << simpleProcessNameBypass;
            types << simpleProcessPathBypass;
        } else if (action == warpBypass) {
            types << simpleAddressWarpBypass;
            types << simpleProcessNameWarpBypass;
            types << simpleProcessPathWarpBypass;
        } else {
            types << simpleAddressBlock;
            types << simpleProcessNameBlock;
            types << simpleProcessPathBlock;
        }
        QString res;
        for (const auto& item: Rules)
        {
            if (types.contains(item->type))
            {
                for (const auto& domain : item->domain) res += QString("domain:" + domain + "\n");
                for (const auto& domain_suffix : item->domain_suffix) res += QString("suffix:" + domain_suffix + "\n");
                for (const auto& domain_keyword : item->domain_keyword) res += QString("keyword:" + domain_keyword + "\n");
                for (const auto& domain_regex : item->domain_regex) res += QString("regex:" + domain_regex + "\n");
                for (const auto& rule_set : item->rule_set) res += QString("ruleset:" + rule_set + "\n");
                for (const auto& ip_cidr : item->ip_cidr) res += QString("ip:" + ip_cidr + "\n");
                for (const auto& process_name : item->process_name) res += QString("processName:" + process_name + "\n");
                for (const auto& process_path : item->process_path) res += QString("processPath:" + process_path + "\n");
            }
        }
        return res;
    }


    QString RouteProfile::UpdateSimpleRules(const QString& content, simpleAction action)
    {
        QString res;
        auto items = content.split("\n");
        QList<ruleType> types;
        if (action == proxy) {
            types << simpleAddressProxy;
            types << simpleProcessNameProxy;
            types << simpleProcessPathProxy;
        } else if (action == bypass) {
            types << simpleAddressBypass;
            types << simpleProcessNameBypass;
            types << simpleProcessPathBypass;
        } else if (action == warpBypass) {
            types << simpleAddressWarpBypass;
            types << simpleProcessNameWarpBypass;
            types << simpleProcessPathWarpBypass;
        } else {
            types << simpleAddressBlock;
            types << simpleProcessNameBlock;
            types << simpleProcessPathBlock;
        }
        for (auto t : types) {
            ResetSimpleRule(t);
        }
        for (const auto& rawLine : items) {
            const QString raw = rawLine.trimmed();
            if (raw.isEmpty()) continue;
            auto type = get_rule_type(raw, action);
            if (type == custom) {
                res += "invalid rule:" + raw + "\n";
                continue;
            }
            auto rule = get_simple_rule_by_type(type);
            if (!rule) {
                res += "internal error, failed to get rule for: " + ruleTypeToString(type) + "\n";
                continue;
            }
            if (!add_simple_rule(raw, rule, type)) {
                res += "invalid rule:" + raw + "\n";
            }
        }
        FilterEmptyRules();
        return res;
    }

    void RouteProfile::FilterEmptyRules() {
        QList<std::shared_ptr<RouteRule>> newRules;
        for (const auto& rule : Rules) {
            if (!rule->isEmpty()) newRules.append(rule);
        }
        Rules = newRules;
    }

    bool RouteProfile::AppendSimpleRule(const QString& rawRule, simpleAction action) {
        const QString raw = rawRule.trimmed();
        if (raw.isEmpty()) return false;

        auto type = get_rule_type(raw, action);
        if (type == custom) return false;

        auto rule = get_simple_rule_by_type(type);
        const bool isNewRule = rule == nullptr;
        if (isNewRule) {
            for (auto &item : get_simple_rules()) {
                if (item->type == type) {
                    rule = item;
                    break;
                }
            }
        }
        if (!rule) return false;

        if (!add_simple_rule(raw, rule, type)) return false;
        // Published only once the value stuck, so a rejected line needs no FilterEmptyRules() sweep to undo it.
        if (isNewRule) Rules.append(rule);
        return true;
    }

    bool RouteProfile::add_simple_rule(const QString& content, const std::shared_ptr<RouteRule>& rule, ruleType type)
    {
        if (type == simpleAddressProxy || type == simpleAddressBypass || type == simpleAddressBlock || type == simpleAddressWarpBypass) return add_simple_address_rule(content, rule);
        else return add_simple_process_rule(content, rule);
    }

    bool RouteProfile::add_simple_address_rule(const QString& content, const std::shared_ptr<RouteRule>& rule)
    {
        auto colonIdx = content.indexOf(':');
        if (colonIdx == -1) return false;
        const QString address = content.mid(colonIdx+1).trimmed();
        if (address.isEmpty()) return false;
        const QString subType = content.left(colonIdx).trimmed();
        if (subType == "domain") {
            if (!rule->domain.contains(address)) rule->domain.append(address);
            return true;
        } else if (subType == "suffix") {
            if (!rule->domain_suffix.contains(address)) rule->domain_suffix.append(address);
            return true;
        } else if (subType == "keyword") {
            if (!rule->domain_keyword.contains(address)) rule->domain_keyword.append(address);
            return true;
        } else if (subType == "regex") {
            if (!rule->domain_regex.contains(address)) rule->domain_regex.append(address);
            return true;
        } else if (subType == "ruleset") {
            if (!rule->rule_set.contains(address)) rule->rule_set.append(address);
            return true;
        } else if (subType == "ip") {
            if (!rule->ip_cidr.contains(address)) rule->ip_cidr.append(address);
            return true;
        } else {
            return false;
        }
    }

    bool RouteProfile::add_simple_process_rule(const QString& content, const std::shared_ptr<RouteRule>& rule)
    {
        if (!content.contains(":")) return false;
        const QString prefix = content.first(content.indexOf(':')).trimmed();
        const QString address = content.section(':', 1).trimmed();
        if (address.isEmpty()) return false;
        if (prefix == "processPath")
        {
            if (!rule->process_path.contains(address)) rule->process_path.append(address);
            return true;
        } else if (prefix == "processName")
        {
            if (!rule->process_name.contains(address)) rule->process_name.append(address);
            return true;
        } else
        {
            return false;
        }
    }

    std::shared_ptr<RouteRule> RouteProfile::get_simple_rule_by_type(ruleType type) {
        for (const auto &r : Rules) {
            if (r->type == type) return r;
        }
        return nullptr;
    }

    ruleType RouteProfile::get_rule_type(const QString& content, simpleAction action) {
        if (content.startsWith("domain") ||
            content.startsWith("suffix") ||
            content.startsWith("keyword") ||
            content.startsWith("regex") ||
            content.startsWith("ruleset") ||
            content.startsWith("ip")) {
            if (action == proxy) return simpleAddressProxy;
            if (action == bypass) return simpleAddressBypass;
            if (action == warpBypass) return simpleAddressWarpBypass;
            return simpleAddressBlock;
        }
        if (content.startsWith("processName")) {
            if (action == proxy) return simpleProcessNameProxy;
            if (action == bypass) return simpleProcessNameBypass;
            if (action == warpBypass) return simpleProcessNameWarpBypass;
            return simpleProcessNameBlock;
        }
        if (content.startsWith("processPath")) {
            if (action == proxy) return simpleProcessPathProxy;
            if (action == bypass) return simpleProcessPathBypass;
            if (action == warpBypass) return simpleProcessPathWarpBypass;
            return simpleProcessPathBlock;
        }
        return custom;
    }
}
