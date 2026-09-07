#include "include/configs/generate.h"
#include "include/api/RPC.h"
#include "include/configs/AutoSelectorPlan.h"
#include "include/global/Configs.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QHostAddress>
#include <QMutex>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QThreadPool>


#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"


#include "include/database/entities/Profile.h"
#include "include/global/VpnCredentialOverride.hpp"
#ifdef Q_OS_LINUX
#include "include/sys/linux/systemChecks.h"
#endif

#include <algorithm>
#include <string_view>
#include <srslist.h>

namespace {
    std::string_view ruleSetUrl(std::string_view key) {
        auto it = std::lower_bound(ruleSetList.begin(), ruleSetList.end(), key,
            [](const auto& e, std::string_view k) { return e.first < k; });
        return (it != ruleSetList.end() && it->first == key) ? it->second : std::string_view{};
    }

}

namespace Configs {
    namespace {

        // ------------------------------------------------------------- tags
        namespace tags {
            constexpr auto proxy = "proxy";
            constexpr auto direct = "direct";
            constexpr auto warpBypass = "warp-bypass";
            // Not bridgePrefix below, which names the unrelated sing-box <-> Xray socks bridges.
            constexpr auto l3Direct = "l3-direct";

            constexpr auto dnsRemote = "dns-remote";
            constexpr auto dnsDirect = "dns-direct";
            constexpr auto dnsLocal = "dns-local";
            constexpr auto dnsFake = "dns-fake";
            constexpr auto dnsTailscale = "dns-tailscale";
            constexpr auto dnsHosts = "dns-hosts";
            constexpr auto dnsVpnPrefix = "dns-vpn";

            constexpr auto dnsIn = "dns-in";
            constexpr auto mixedIn = "mixed-in";
            constexpr auto tunIn = "tun-in";
            constexpr auto redirectIn = "hijack";
            constexpr auto dnsServerIn = "hijack-dns";
            constexpr auto xrayFullConfigIn = "throne-bridge";

            constexpr auto adblockRuleSet = "throne-adblocksingbox";

            constexpr auto mainChainPrefix = "config";
            constexpr auto routeChainPrefix = "route";
            constexpr auto auxEndpointPrefix = "aux";
            constexpr auto poolChainPrefix = "pool";
            constexpr auto testChainPrefix = "proxy";
            constexpr auto testXrayFullPrefix = "xrayfull";
            constexpr auto bridgePrefix = "bridge";
        }

        QString hopTag(const QString &prefix, int index) { return prefix + "-" + Int2String(index); }

        QString bridgeTagFor(const QString &singIngressTag) {
            return QString(tags::bridgePrefix) + "-" + singIngressTag;
        }

        // -------------------------------------------------- prefixed selectors

        struct DomainSelectors {
            QJsonArray ruleSets;
            QJsonArray domains;
            QJsonArray suffixes;
            QJsonArray keywords;
            QJsonArray regexes;

            [[nodiscard]] bool hasInlineConditions() const {
                return !domains.isEmpty() || !suffixes.isEmpty() || !keywords.isEmpty() || !regexes.isEmpty();
            }
        };

        struct SelectorSink {
            QJsonArray *ruleSets = nullptr;
            QJsonArray *domains = nullptr;
            QJsonArray *suffixes = nullptr;
            QJsonArray *keywords = nullptr;
            QJsonArray *regexes = nullptr;
            QJsonArray *ipCIDRs = nullptr;
        };

        SelectorSink sinkFor(DomainSelectors &selectors) {
            return {
                .ruleSets = &selectors.ruleSets,
                .domains = &selectors.domains,
                .suffixes = &selectors.suffixes,
                .keywords = &selectors.keywords,
                .regexes = &selectors.regexes,
            };
        }

        void parseSelectorList(const QStringList &items, const SelectorSink &sink) {
            const std::pair<QLatin1String, QJsonArray *> kinds[] = {
                {QLatin1String("ruleset:"), sink.ruleSets},
                {QLatin1String("domain:"), sink.domains},
                {QLatin1String("suffix:"), sink.suffixes},
                {QLatin1String("keyword:"), sink.keywords},
                {QLatin1String("regex:"), sink.regexes},
                {QLatin1String("ip:"), sink.ipCIDRs},
            };
            for (const auto &rawItem : items) {
                const QString item = rawItem.trimmed();
                for (const auto &[prefix, target] : kinds) {
                    if (!item.startsWith(prefix)) continue;
                    const QString value = item.mid(prefix.size()).trimmed();
                    if (target != nullptr && !value.isEmpty()) *target << value;
                    break;
                }
            }
        }

        // ---------------------------------------------------------- build state

        struct DNSDeps {
            bool needDirectDnsRules = false;
            DomainSelectors direct;
            bool needProxyDnsRules = false;
            DomainSelectors proxy;
        };

        struct TunDeps {
            QJsonArray directIPSets;
            QJsonArray directIPCIDRs;
            // Private ranges the route profile aims somewhere other than direct, so the Tun carries them.
            QSet<QString> hijackedPrivateRanges;
        };

        struct RoutingDeps {
            QStringList neededRuleSets;
            std::map<int, QString> outboundMap;
            struct RouteOutboundGroup {
                QList<int> hopIDs;
                std::shared_ptr<Profile> chainWrapper;
            };
            QList<RouteOutboundGroup> routeOutboundGroups;
            QList<QList<int>> auxEndpointGroups;
        };

        struct BuildPrerequisites {
            DNSDeps dns;
            DomainSelectors hijack;
            TunDeps tun;
            RoutingDeps routing;
        };

        struct coreBridgeConfig {
            bool needed = false;
            int port = -1;
            QString auth;
            QString host = "127.0.0.1";
        };

        struct BuildContext {
            bool forTest = false;
            bool tunEnabled = false;
            bool l3Bridge = false;
            bool isResolvedUsed = false;
            bool singToXrayTransitioned = false;
            bool xrayToSingTransitioned = false;
            bool proxyUsesXray = false;
            std::shared_ptr<Profile> ent = std::make_shared<Profile>(nullptr, nullptr);
            BuildPrerequisites prerequisites;
            osType os = getOS();

            QString error;
            QJsonArray outbounds;
            QJsonArray endpoints;
            QJsonArray xrayOutbounds;
            // tag -> "openvpn" | "openconnect".
            QMap<QString, QString> vpnEndpointTags;
            QList<QString> vpnGateTags;
            QList<QString> vpnAuxTags;
            // The gated tunnel that carries the whole profile refuses any DNS it cannot answer.
            bool vpnBlockOutsideDns = false;
            QList<QString> xrayIngressTags;
            QList<QString> singIngressTags;
            QList<coreBridgeConfig> singToXrayBridges;
            QList<coreBridgeConfig> xrayToSingBridges;
            std::shared_ptr<BuildConfigResult> result = std::make_shared<BuildConfigResult>();
        };

        QString bridgeIngressMismatch(const BuildContext &ctx) {
            if (ctx.xrayToSingBridges.size() != ctx.singIngressTags.size())
                return "xray to sing-box bridges count does not match ingress tags count";
            return {};
        }

        // The core has no bridge backend for Windows on ARM, and a failed one aborts the config.
        bool l3BridgeEnabled(const BuildContext &ctx) {
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_ARM)
            return false;
#else
            return ctx.tunEnabled && !ctx.forTest && dataManager->settingsRepo->vpn_l3_bridge;
#endif
        }

        // preferred_by matches only during pre-match, leaving the copy inert at L4.
        QJsonObject l3BridgeTwin(const QJsonObject &rule) {
            auto twin = rule;
            twin["preferred_by"] = QJsonArray{tags::l3Direct};
            twin["action"] = "route";
            twin["outbound"] = tags::l3Direct;
            return twin;
        }

        // Twins must stay below the sniff rule, or UDP would reach them before it has a domain.
        QJsonArray withL3BridgeTwins(const QJsonArray &rules) {
            QJsonArray res;
            for (const auto &value : rules) {
                const auto rule = value.toObject();
                const auto action = rule.value("action").toString();
                if ((action.isEmpty() || action == "route") && !rule.contains("preferred_by") &&
                    rule.value("outbound").toString() == tags::direct) {
                    res.append(l3BridgeTwin(rule));
                }
                res.append(rule);
            }
            return res;
        }

        bool prefixesOverlap(const QString &lhs, const QString &rhs) {
            const auto a = QHostAddress::parseSubnet(lhs);
            const auto b = QHostAddress::parseSubnet(rhs);
            if (a.second < 0 || b.second < 0) return false;
            if (a.first.protocol() != b.first.protocol()) return false;
            return a.second <= b.second ? b.first.isInSubnet(a.first, a.second)
                                        : a.first.isInSubnet(b.first, b.second);
        }

        struct RawPrefix {
            QByteArray addr;
            int bits = -1;
        };

        RawPrefix parsePrefix(const QString &cidr) {
            const auto parsed = QHostAddress::parseSubnet(cidr);
            if (parsed.second < 0) return {};
            RawPrefix prefix;
            prefix.bits = parsed.second;
            if (parsed.first.protocol() == QAbstractSocket::IPv4Protocol) {
                const auto v4 = parsed.first.toIPv4Address();
                prefix.addr.resize(4);
                for (int i = 0; i < 4; ++i) prefix.addr[i] = char((v4 >> (24 - 8 * i)) & 0xFF);
            } else if (parsed.first.protocol() == QAbstractSocket::IPv6Protocol) {
                const auto v6 = parsed.first.toIPv6Address();
                prefix.addr = QByteArray(reinterpret_cast<const char *>(v6.c), 16);
            } else {
                return {};
            }
            return prefix;
        }

        QString prefixToString(const RawPrefix &prefix) {
            QHostAddress addr;
            if (prefix.addr.size() == 4) {
                quint32 v4 = 0;
                for (int i = 0; i < 4; ++i) v4 = (v4 << 8) | quint8(prefix.addr[i]);
                addr = QHostAddress(v4);
            } else {
                Q_IPV6ADDR v6;
                for (int i = 0; i < 16; ++i) v6[i] = quint8(prefix.addr[i]);
                addr = QHostAddress(v6);
            }
            return addr.toString() + "/" + QString::number(prefix.bits);
        }

        bool prefixContains(const RawPrefix &outer, const RawPrefix &inner) {
            if (outer.bits < 0 || inner.bits < 0) return false;
            if (outer.addr.size() != inner.addr.size() || outer.bits > inner.bits) return false;
            const int wholeBytes = outer.bits / 8;
            if (outer.addr.left(wholeBytes) != inner.addr.left(wholeBytes)) return false;
            const int restBits = outer.bits % 8;
            if (restBits == 0) return true;
            const auto mask = quint8(0xFF << (8 - restBits));
            return (quint8(outer.addr[wholeBytes]) & mask) == (quint8(inner.addr[wholeBytes]) & mask);
        }

        // sing-tun offers no way to re-add a subtracted route_exclude_address, so partly-routed ranges are pre-split.
        QStringList subtractPrefix(const QStringList &ranges, const QString &hole) {
            const auto cut = parsePrefix(hole);
            if (cut.bits < 0) return ranges;
            QStringList out;
            for (const auto &entry : ranges) {
                const auto range = parsePrefix(entry);
                if (prefixContains(cut, range)) continue;
                if (!prefixContains(range, cut)) {
                    out << entry;
                    continue;
                }
                for (int bits = range.bits + 1; bits <= cut.bits; ++bits) {
                    RawPrefix sibling{cut.addr, bits};
                    const int flipped = bits - 1;
                    sibling.addr[flipped / 8] = char(quint8(sibling.addr[flipped / 8]) ^ quint8(0x80 >> (flipped % 8)));
                    for (int i = bits; i < int(sibling.addr.size()) * 8; ++i)
                        sibling.addr[i / 8] = char(quint8(sibling.addr[i / 8]) & ~quint8(0x80 >> (i % 8)));
                    out << prefixToString(sibling);
                }
            }
            return out;
        }

        // ------------------------------------------------------- json fragments

        // sing-box matches process_path against the OS-native form.
        QJsonArray extraCoreProcessPaths(const QString &corePath) {
            auto path = corePath;
#ifdef Q_OS_WIN
            path.replace("/", "\\");
#endif
            return QJsonArray{path};
        }

        QJsonObject socksBridgeInbound(const QString &tag, const coreBridgeConfig &bridge) {
            return QJsonObject{
                {"type", "socks"},
                {"tag", tag},
                {"listen", bridge.host},
                {"listen_port", bridge.port},
                {"users", QJsonArray{QJsonObject{
                    {"username", bridge.auth},
                    {"password", bridge.auth},
                }}},
            };
        }

        QJsonObject xraySocksInbound(const QString &tag, const coreBridgeConfig &bridge) {
            return QJsonObject{
                {"tag", tag},
                {"listen", bridge.host},
                {"port", bridge.port},
                {"protocol", "socks"},
                {"settings", QJsonObject{
                    {"auth", "password"},
                    {"udp", true},
                    {"accounts", QJsonArray{QJsonObject{
                        {"user", bridge.auth},
                        {"pass", bridge.auth},
                    }}},
                }},
            };
        }

        // The guard answers empty instead of falling through, which would hand the query to another server.
        void appendDnsRoute(QJsonArray &rules, const QJsonObject &conditions, const QString &server,
                            bool disableIPv6) {
            if (disableIPv6) {
                auto guard = conditions;
                guard["query_type"] = QJsonArray{"AAAA"};
                guard["action"] = "predefined";
                rules += guard;
            }
            auto route = conditions;
            route["action"] = "route";
            route["server"] = server;
            rules += route;
        }

        void appendDnsRoutingRules(QJsonArray &rules, const DomainSelectors &selectors,
                                   const QString &server, bool disableIPv6) {
            if (!selectors.ruleSets.isEmpty()) {
                appendDnsRoute(rules, QJsonObject{{"rule_set", selectors.ruleSets}}, server, disableIPv6);
            }
            if (selectors.hasInlineConditions()) {
                appendDnsRoute(rules, QJsonObject{
                    {"domain", selectors.domains},
                    {"domain_suffix", selectors.suffixes},
                    {"domain_keyword", selectors.keywords},
                    {"domain_regex", selectors.regexes},
                }, server, disableIPv6);
            }
        }

        QString genTunName() {
            auto tun_name = "throne-tun";
#ifdef Q_OS_MACOS
            tun_name = "";
#endif
            return tun_name;
        }

        // ------------------------------------------------------ profile queries

        bool isCustomFullConfig(const std::shared_ptr<Profile> &profile) {
            return profile->type == "custom" && profile->Custom() != nullptr &&
                   profile->Custom()->type == Custom::CustomFullConfig;
        }

        bool isXrayFullConfig(const std::shared_ptr<Profile> &profile) {
            return profile->outbound != nullptr && profile->outbound->IsXrayFullConfig();
        }

        bool usesXrayCore(const std::shared_ptr<Profile> &profile) {
            return profile->outbound != nullptr &&
                   (profile->outbound->IsXray() || profile->outbound->IsXrayFullConfig());
        }

        // Decided up front: the sidecar's DNS carve-outs must be in place before the first connection.
        bool proxyPathUsesXray(const std::shared_ptr<Profile> &ent)
        {
            if (ent->type == "chain") {
                if (auto chain = ent->Chain(); chain != nullptr) {
                    for (int pid : chain->list) {
                        auto hop = dataManager->profilesRepo->GetProfile(pid);
                        if (hop != nullptr && usesXrayCore(hop)) return true;
                    }
                }
                return false;
            }
            if (ent->type == "autoselector") {
                const auto plan = PlanAutoSelector(ent);
                for (int pid : plan.build) {
                    auto member = dataManager->profilesRepo->GetProfile(pid);
                    if (member != nullptr && usesXrayCore(member)) return true;
                }
                return false;
            }
            return usesXrayCore(ent);
        }

        std::shared_ptr<Profile> getWarpProfile() {
            const auto &settings = *dataManager->settingsRepo;
            auto warpProfile = std::make_shared<Profile>();
            warpProfile->name = "warp";
            warpProfile->id = warpProfileID;
            warpProfile->type = "wireguard";
            auto outbound = std::make_shared<wireguard>();
            outbound->name = "warp";
            outbound->server = settings.warp_ep.contains(":") ? SubStrBefore(settings.warp_ep, ":") : settings.warp_ep;
            outbound->server_port = settings.warp_ep.contains(":") ? SubStrAfter(settings.warp_ep, ":").toInt() : 2408;
            outbound->private_key = settings.warp_private_key;
            outbound->address = settings.warp_ifc_addrs;
            auto peer = std::make_shared<Peer>();
            peer->public_key = settings.warp_public_key;
            peer->address = outbound->server;
            peer->port = outbound->server_port;
            peer->reserved = QStringList2QListInt(settings.warp_reserved);
            peer->persistent_keepalive = "10";
            outbound->peer = peer;
            outbound->mtu = 1280;

            warpProfile->outbound = outbound;
            return warpProfile;
        }

        // ------------------------------------------------------- prerequisites

        std::shared_ptr<Profile> resolveExtraCoreProfile(const std::shared_ptr<Profile> &ent)
        {
            if (ent->outbound != nullptr && ent->outbound->IsExtraCore()) return ent;
            if (ent->type != "chain") return nullptr;
            auto chain = ent->Chain();
            if (chain == nullptr || chain->list.isEmpty()) return nullptr;
            auto firstEnt = dataManager->profilesRepo->GetProfile(chain->list[0]);
            if (firstEnt != nullptr && firstEnt->outbound != nullptr && firstEnt->outbound->IsExtraCore())
                return firstEnt;
            return nullptr;
        }

        QList<int> unwrapChain(int entID);

        void calculatePrerequisites(BuildContext &ctx) {
            const auto &settings = *dataManager->settingsRepo;
            ctx.tunEnabled = settings.spmode_vpn;
            ctx.os = getOS();
#ifdef Q_OS_LINUX
            ctx.isResolvedUsed = isSystemdResolvedDefaultResolver();
#endif
            auto &preReqs = ctx.prerequisites;

            auto routeChain = dataManager->routesRepo->GetRouteProfile(settings.current_route_id);
            if (routeChain == nullptr) {
                ctx.error = "Routing profile does not exist, try resetting the route profile in Routing Settings";
                return;
            }
            // A verbatim raw profile takes no twins, and an unreachable bridge still starts a tun.
            ctx.l3Bridge = l3BridgeEnabled(ctx) && !(routeChain->isRaw && routeChain->preventModifications);

            if (settings.enable_warp &&
                (settings.warp_private_key.isEmpty() ||
                 settings.warp_public_key.isEmpty() ||
                 settings.warp_ep.isEmpty() ||
                 settings.warp_ifc_addrs.isEmpty())) {
                ctx.error = "Warp is enabled but its config has not been generated. Please generate the Warp config first in Routing Settings.";
                return;
            }

            auto neededOutbounds = routeChain->get_used_outbounds();
            auto neededRuleSets = routeChain->get_used_rule_sets();
            preReqs.routing.outboundMap[-1] = tags::proxy;
            preReqs.routing.outboundMap[-2] = tags::direct;
            preReqs.routing.outboundMap[warpBypassID] = settings.enable_warp ? tags::warpBypass : tags::proxy;
            int suffix = 0;
            if (proxyPathUsesXray(ctx.ent)) ctx.proxyUsesXray = true;
            for (const auto &item: *neededOutbounds) {
                if (item < 0) continue;
                auto neededEnt = dataManager->profilesRepo->GetProfile(item);
                if (neededEnt == nullptr) {
                    ctx.error = "The routing profile is referencing outbounds that no longer exist, consider revising your settings";
                    return;
                }
                if ((neededEnt->outbound != nullptr && neededEnt->outbound->IsExtraCore()) || isCustomFullConfig(neededEnt) || isXrayFullConfig(neededEnt)) {
                    ctx.error = "Outbounds used in routing profile cannot use an extra core or be a custom full config";
                    return;
                }
                if (neededEnt->type == "chain") {
                    auto chain = neededEnt->Chain();
                    if (chain == nullptr || chain->list.isEmpty()) {
                        ctx.error = "Chain outbound in routing profile is empty or corrupted";
                        return;
                    }
                    for (int hopID : chain->list) {
                        auto hopEnt = dataManager->profilesRepo->GetProfile(hopID);
                        if (hopEnt == nullptr) {
                            ctx.error = "Chain outbound in routing profile contains a missing profile";
                            return;
                        }
                        if ((hopEnt->outbound != nullptr && hopEnt->outbound->IsExtraCore()) || isCustomFullConfig(hopEnt) || isXrayFullConfig(hopEnt) || hopEnt->type == "chain") {
                            ctx.error = "Chain hops in routing profile cannot use an extra core, a custom full config, or be of type chain";
                            return;
                        }
                        if (usesXrayCore(hopEnt)) ctx.proxyUsesXray = true;
                    }
                    preReqs.routing.outboundMap[item] = hopTag(tags::routeChainPrefix, suffix);
                    // Reversed to match the main-chain build order: outer hop first.
                    preReqs.routing.routeOutboundGroups << RoutingDeps::RouteOutboundGroup{{chain->list.rbegin(), chain->list.rend()}, neededEnt};
                    suffix += static_cast<int>(chain->list.size());
                } else {
                    if (usesXrayCore(neededEnt)) ctx.proxyUsesXray = true;
                    preReqs.routing.outboundMap[item] = hopTag(tags::routeChainPrefix, suffix++);
                    preReqs.routing.routeOutboundGroups << RoutingDeps::RouteOutboundGroup{QList<int>{item}, nullptr};
                }
            }

            if (!routeChain->endpointProfileIDs.isEmpty()) {
                QSet<int> usedProfileIDs;
                for (const auto &routeGroup : preReqs.routing.routeOutboundGroups) {
                    for (int hopID : routeGroup.hopIDs) usedProfileIDs << hopID;
                }
                const auto entHopIDs = unwrapChain(ctx.ent->id);
                for (int hopID : entHopIDs) usedProfileIDs << hopID;
                if (auto entGroup = dataManager->groupsRepo->GetGroup(ctx.ent->gid); entGroup != nullptr) {
                    if (entGroup->front_proxy_id >= 0) usedProfileIDs << entGroup->front_proxy_id;
                    if (entGroup->landing_proxy_id >= 0) usedProfileIDs << entGroup->landing_proxy_id;
                }
                int auxSuffix = 0;
                for (int endpointID : routeChain->endpointProfileIDs) {
                    auto endpointEnt = dataManager->profilesRepo->GetProfile(endpointID);
                    if (endpointEnt == nullptr || endpointEnt->outbound == nullptr) {
                        ctx.error = QObject::tr("The routing profile lists an endpoint profile (id %1) that no longer exists").arg(endpointID);
                        return;
                    }
                    const auto endpointName = endpointEnt->outbound->DisplayTypeAndName();
                    // unwrapChain reverses the stored list, so hop 0 is the exit that carries the tag.
                    const auto hopIDs = unwrapChain(endpointID);
                    if (hopIDs.isEmpty()) {
                        ctx.error = QObject::tr("%1 is listed as a routing profile endpoint but is empty or corrupted").arg(endpointName);
                        return;
                    }
                    auto exitEnt = dataManager->profilesRepo->GetProfile(hopIDs.first());
                    if (exitEnt == nullptr || exitEnt->outbound == nullptr) {
                        ctx.error = QObject::tr("%1 is listed as a routing profile endpoint but a hop of it no longer exists").arg(endpointName);
                        return;
                    }
                    if (exitEnt->type != "openvpn" && exitEnt->type != "openconnect") {
                        ctx.error = QObject::tr("%1 is listed as a routing profile endpoint, so its last hop must be an OpenVPN or OpenConnect profile").arg(endpointName);
                        return;
                    }
                    for (int hopID : hopIDs) {
                        auto hopEnt = dataManager->profilesRepo->GetProfile(hopID);
                        if (hopEnt == nullptr || hopEnt->outbound == nullptr) {
                            ctx.error = QObject::tr("%1 is listed as a routing profile endpoint but a hop of it no longer exists").arg(endpointName);
                            return;
                        }
                        if (hopEnt->outbound->IsExtraCore() || isCustomFullConfig(hopEnt) || isXrayFullConfig(hopEnt)
                            || hopEnt->type == "chain") {
                            ctx.error = QObject::tr("Hops of the routing profile endpoint %1 cannot use an extra core, a full config, or be a chain").arg(endpointName);
                            return;
                        }
                        // An auxiliary endpoint is reached only through preferred_by, so there is no ingress to bridge.
                        if (usesXrayCore(hopEnt)) {
                            ctx.error = QObject::tr("Hops of the routing profile endpoint %1 cannot run on the Xray core").arg(endpointName);
                            return;
                        }
                        if (usedProfileIDs.contains(hopID)) {
                            ctx.error = QObject::tr("%1 is used as an endpoint of the routing profile and by the started profile at the same time, remove it from one of them").arg(hopEnt->outbound->DisplayTypeAndName());
                            return;
                        }
                    }
                    if (usedProfileIDs.contains(endpointID)) {
                        ctx.error = QObject::tr("%1 is listed twice in the endpoints of the routing profile").arg(endpointName);
                        return;
                    }
                    usedProfileIDs << endpointID;
                    for (int hopID : hopIDs) usedProfileIDs << hopID;
                    preReqs.routing.auxEndpointGroups << hopIDs;
                    preReqs.routing.outboundMap[endpointID] = hopTag(tags::auxEndpointPrefix, auxSuffix);
                    auxSuffix += static_cast<int>(hopIDs.size());
                }
            }

            for (const auto &item: *neededRuleSets) {
                preReqs.routing.neededRuleSets << item;
            }

            if (settings.enable_dns_routing) {
                auto sets = routeChain->get_direct_sites();
                parseSelectorList(sets, sinkFor(preReqs.dns.direct));
                if (!sets.isEmpty()) preReqs.dns.needDirectDnsRules = true;

                // With a direct final DNS these need an explicit remote-DNS carve-out.
                auto proxySets = routeChain->get_proxy_sites();
                parseSelectorList(proxySets, sinkFor(preReqs.dns.proxy));
                if (!proxySets.isEmpty()) preReqs.dns.needProxyDnsRules = true;
            }
            if (auto group = dataManager->groupsRepo->GetGroup(ctx.ent->gid); group != nullptr)
            {
                QList<int> groupEnts;
                if (auto frontEntID = group->front_proxy_id; frontEntID >= 0) groupEnts << frontEntID;
                if (auto landingEntID = group->landing_proxy_id; landingEntID >= 0) groupEnts << landingEntID;
                for (const auto &id : groupEnts)
                {
                    if (auto pe = dataManager->profilesRepo->GetProfile(id); pe != nullptr && usesXrayCore(pe)) ctx.proxyUsesXray = true;
                }
            }

            if (settings.enable_dns_server) {
                parseSelectorList(settings.dns_server_rules, sinkFor(preReqs.hijack));
            }
            for (auto ruleSet : preReqs.hijack.ruleSets) {
                if (!preReqs.routing.neededRuleSets.contains(ruleSet.toString())) preReqs.routing.neededRuleSets.append(ruleSet.toString());
            }

            parseSelectorList(routeChain->get_direct_ips(), {
                .ruleSets = &preReqs.tun.directIPSets,
                .ipCIDRs = &preReqs.tun.directIPCIDRs,
            });

            for (const auto &cidr : routeChain->get_hijacked_ips()) {
                for (const auto &range : settings.vpn_private_ranges) {
                    if (prefixesOverlap(range, cidr)) preReqs.tun.hijackedPrivateRanges << range;
                }
            }

            auto extraCoreEnt = resolveExtraCoreProfile(ctx.ent);
            if (extraCoreEnt == nullptr) return;
            auto outbound = extraCoreEnt->ExtraCore();
            if (outbound == nullptr)
            {
                MW_show_log("INVALID ENT TYPE, NEEDED EXTRACORE GOT NULLPTR");
                ctx.error = "failed to cast to extracore, type is: " + extraCoreEnt->type;
                return;
            }
            auto &extraCoreData = *ctx.result->extraCoreData;
            extraCoreData.path = QFileInfo(outbound->extraCorePath).canonicalFilePath();
            extraCoreData.args = outbound->extraCoreArgs;
            extraCoreData.config = outbound->extraCoreConf;
            extraCoreData.noLog = outbound->noLogs;
        }

        // ------------------------------------------------------- small sections

        void buildLogSection(BuildContext &ctx) {
            ctx.result->coreConfig.insert("log", QJsonObject{{"level", dataManager->settingsRepo->log_level}});
        }

        void buildNTPSection(BuildContext &ctx) {
            const auto &settings = *dataManager->settingsRepo;
            if (!settings.enable_ntp) return;
            ctx.result->coreConfig["ntp"] = QJsonObject{
                {"enabled", true},
                {"server", settings.ntp_server_address},
                {"server_port", settings.ntp_server_port},
                {"interval", settings.ntp_interval},
                {"detour", (settings.ntp_outbound == tags::proxy && !ctx.forTest) ? tags::proxy : tags::direct},
            };
        }

        void buildCertificateSection(BuildContext &ctx) {
            ctx.result->coreConfig.insert("certificate",
                QJsonObject{{"store", dataManager->settingsRepo->use_mozilla_certs ? "mozilla" : "system"}});
        }

        // ---------------------------------------------------------------- dns

        QJsonObject buildDnsObj(BuildContext &ctx, QString address) {
            if (address.startsWith("local")) {
                if (ctx.tunEnabled && ctx.isResolvedUsed) {
                    return {{"type", "underlying"}};
                }
                if (ctx.tunEnabled && ctx.os == Darwin) {
                    return {
                        {"type", "udp"},
                        {"server", dataManager->settingsRepo->core_box_underlying_dns}
                    };
                }
                return {{"type", "local"}};
            }
            if (address.startsWith("dhcp://")) {
                auto ifcName = address.replace("dhcp://", "");
                if (ifcName == "auto") ifcName = "";
                return {
                    {"type", "dhcp"},
                    {"interface", ifcName},
                };
            }
            QString addr = address;
            int port = -1;
            QString type = "udp";
            QString path = "";
            if (address.startsWith("tcp://")) {
                type = "tcp";
                addr = addr.replace("tcp://", "");
            }
            if (address.startsWith("tls://")) {
                type = "tls";
                addr = addr.replace("tls://", "");
            }
            if (address.startsWith("quic://")) {
                type = "quic";
                addr = addr.replace("quic://", "");
            }
            if (address.startsWith("https://")) {
                type = "https";
                addr = addr.replace("https://", "");
                auto slashIndex = addr.indexOf("/");
                if (slashIndex != -1) {
                    path = addr.mid(slashIndex);
                    addr = addr.left(slashIndex);
                }
            }
            if (address.startsWith("h3://")) {
                type = "h3";
                addr = addr.replace("h3://", "");
                auto slashIndex = addr.indexOf("/");
                if (slashIndex != -1) {
                    path = addr.mid(slashIndex);
                    addr = addr.left(slashIndex);
                }
            }
            if (addr.contains(":")) {
                auto spl = addr.split(":");
                addr = spl[0];
                port = spl[1].toInt();
            }
            QJsonObject res = {
                {"type", type},
                {"server", addr},
            };
            if (port != -1) res["server_port"] = port;
            if (!path.isEmpty()) res["path"] = path;
            return res;
        }

        QString upgradeUdpDnsToDoH(const QString &server) {
            static const QMap<QString, QString> known = {
                // Google
                {"8.8.8.8", "https://8.8.8.8/dns-query"},
                {"8.8.4.4", "https://8.8.4.4/dns-query"},
                // Cloudflare
                {"1.1.1.1", "https://1.1.1.1/dns-query"},
                {"1.0.0.1", "https://1.0.0.1/dns-query"},
                {"1.1.1.2", "https://1.1.1.2/dns-query"},
                {"1.0.0.2", "https://1.0.0.2/dns-query"},
                {"1.1.1.3", "https://1.1.1.3/dns-query"},
                {"1.0.0.3", "https://1.0.0.3/dns-query"},
                // Quad9
                {"9.9.9.9", "https://9.9.9.9/dns-query"},
                {"149.112.112.112", "https://149.112.112.112/dns-query"},
                // AdGuard
                {"94.140.14.14", "https://94.140.14.14/dns-query"},
                {"94.140.15.15", "https://94.140.15.15/dns-query"},
            };
            return known.value(server, "https://8.8.8.8/dns-query");
        }

        void buildDNSSection(BuildContext &ctx, bool useDnsObj = true) {
            const auto &settings = *dataManager->settingsRepo;
            if (getOS() == Darwin && settings.core_box_underlying_dns.isEmpty() && settings.spmode_vpn)
            {
                ctx.error = QObject::tr("Local DNS and Tun mode do not work together, please set an IP to be used as the Local DNS server in the Routing Settings -> Local override");
                return;
            }

            if (settings.use_dns_object && useDnsObj) {
                ctx.result->coreConfig["dns"] = QString2QJsonObject(settings.dns_object);
                return;
            }

            const auto &dns = ctx.prerequisites.dns;
            const auto &hijack = ctx.prerequisites.hijack;
            bool isTailscale = ctx.ent->type == "tailscale";
            bool independentCache = false;
            QJsonArray servers;
            QJsonArray rules;
            // Merged in front of `rules` at the end; the tailscale block below prepends.
            QJsonArray headRules;
            if (!ctx.forTest) {
                auto remoteDnsObj = buildDnsObj(ctx, settings.remote_dns);
                // overwrite remote dns to TCP based since Xray is shit
                if (ctx.proxyUsesXray && ( remoteDnsObj.value("type").toString() == "udp" || remoteDnsObj.value("type").toString() == "quic" )) {
                    remoteDnsObj = buildDnsObj(ctx, upgradeUdpDnsToDoH(remoteDnsObj.value("server").toString()));
                }
                remoteDnsObj["tag"] = tags::dnsRemote;
                remoteDnsObj["domain_resolver"] = tags::dnsLocal;
                remoteDnsObj["detour"] = tags::proxy;
                servers += remoteDnsObj;

                if (isTailscale)
                {
                    auto tailscale = ctx.ent->Tailscale();
                    if (tailscale != nullptr)
                    {
                        servers += QJsonObject{
                            {"type", "tailscale"},
                            {"tag", tags::dnsTailscale},
                            {"endpoint", tags::proxy},
                            {"accept_default_resolvers", tailscale->globalDNS},
                        };

                        rules.prepend(QJsonObject{
                            {"domain_suffix", QJsonArray{"ts.net", "tailscale.net"}},
                            {"action", "route"},
                            {"server", tags::dnsTailscale},
                        });
                    }

                    rules.prepend(QJsonObject{
                        {"domain", QJsonArray{
                            "controlplane.tailscale.com",
                            "login.tailscale.com",
                            "log.tailscale.io"
                        }},
                        {"domain_suffix", QJsonArray{
                            "tailscale.com",
                            "tailscale.net",
                            "tailscale.io"
                        }},
                        {"action", "route"},
                        {"server", tags::dnsDirect},
                    });
                }
            }

            auto directDnsObj = buildDnsObj(ctx, settings.direct_dns);
            directDnsObj["tag"] = tags::dnsDirect;
            directDnsObj["domain_resolver"] = tags::dnsLocal;
            servers.append(directDnsObj);

            if (!ctx.forTest && settings.dns_predefined_enable) {
                QList<PredefinedDNSEntry> predefined;
                if (!ParsePredefinedDNS(settings.dns_predefined_rules, predefined)) predefined.clear();

                // "*." is rewritten to the queried name by the core; a literal owner would have to parse as a zone name.
                auto emitFamily = [&](const QString &domain, const QStringList &addrs, const QString &type) {
                    // Refused rather than passed through, else the other family defeats the override.
                    if (addrs.isEmpty()) {
                        headRules += QJsonObject{
                            {"domain", domain},
                            {"action", "predefined"},
                            {"query_type", type},
                            {"rcode", "NXDOMAIN"},
                        };
                        return;
                    }
                    QJsonArray answers;
                    for (const auto &addr : addrs) answers += QString("*. IN %1 %2").arg(type, addr);
                    headRules += QJsonObject{
                        {"domain", domain},
                        {"action", "predefined"},
                        {"query_type", type},
                        {"rcode", "NOERROR"},
                        {"answer", answers},
                    };
                };

                for (const auto &entry : predefined) {
                    emitFamily(entry.domain, entry.v4, "A");
                    emitFamily(entry.domain, entry.v6, "AAAA");
                }
            }

            if (!ctx.forTest && settings.dns_use_hosts) {
                servers += QJsonObject{{"tag", tags::dnsHosts}, {"type", "hosts"}};
                // The transport NXDOMAINs whatever it cannot answer, hence the preferred_by gate and query_type limit.
                headRules += QJsonObject{
                    {"preferred_by", QJsonArray{tags::dnsHosts}},
                    {"query_type", QJsonArray{"A", "AAAA"}},
                    {"action", "route"},
                    {"server", tags::dnsHosts},
                    {"disable_cache", true},
                };
            }

            // A gated tunnel takes the fall-through only where unmatched traffic can reach it.
            QString vpnFinalDnsTag;
            bool vpnDirectFinalDns = false;
            if (!ctx.forTest) {
                for (auto it = ctx.vpnEndpointTags.cbegin(); it != ctx.vpnEndpointTags.cend(); ++it) {
                    const auto dnsTag = QString(tags::dnsVpnPrefix) + "-" + it.key();
                    QJsonObject server{
                        {"tag", dnsTag},
                        {"type", it.value()},
                        {"endpoint", it.key()},
                    };
                    if (ctx.vpnGateTags.contains(it.key())) {
                        server["accept_default_resolvers"] = true;
                        server["accept_search_domain"] = true;
                        if (it.key() == tags::proxy || it.key() == tags::warpBypass) {
                            // dns-remote detours through this tunnel, so direct is the only reachable fallback.
                            if (ctx.vpnBlockOutsideDns) { if (vpnFinalDnsTag.isEmpty()) vpnFinalDnsTag = dnsTag; }
                            else vpnDirectFinalDns = true;
                        }
                    }
                    servers += server;
                    headRules += QJsonObject{
                        {"preferred_by", QJsonArray{dnsTag}},
                        {"action", "route"},
                        {"server", dnsTag},
                    };
                }
            }

            // No dns-in carve-out: Xray resolves against dns-direct in-process now, so a query on that port is an ordinary local one.

            if (!ctx.forTest && !ctx.result->extraCoreData->path.isEmpty())
            {
                appendDnsRoute(rules, QJsonObject{{"process_path", extraCoreProcessPaths(ctx.result->extraCoreData->path)}},
                               tags::dnsDirect, settings.direct_dns_disable_ipv6);
            }

            if (settings.enable_dns_server && !ctx.forTest)
            {
                // Own rule per rule_set (AND-vs-OR); the non-empty guards stop a query_type-only rule hijacking everything.
                auto addHijackRules = [&](const QJsonObject &conditions) {
                    auto v4 = conditions;
                    v4["query_type"] = "A";
                    v4["action"] = "predefined";
                    v4["rcode"] = "NOERROR";
                    v4["answer"] = QString("*. IN A %1").arg(settings.dns_v4_resp);
                    rules += v4;

                    if (settings.dns_v6_resp.isEmpty()) return;
                    auto v6 = conditions;
                    v6["query_type"] = "AAAA";
                    v6["action"] = "predefined";
                    v6["rcode"] = "NOERROR";
                    v6["answer"] = QString("*. IN AAAA %1").arg(settings.dns_v6_resp);
                    rules += v6;
                };

                if (!hijack.ruleSets.isEmpty())
                {
                    addHijackRules(QJsonObject{{"rule_set", hijack.ruleSets}});
                }
                if (!hijack.domains.isEmpty() || !hijack.suffixes.isEmpty() || !hijack.regexes.isEmpty())
                {
                    addHijackRules(QJsonObject{
                                {"domain", hijack.domains},
                                {"domain_suffix", hijack.suffixes},
                                {"domain_regex", hijack.regexes},
                            });
                }
            }

            if (settings.fake_dns) {
                QJsonObject fakeServer{
                        {"tag", tags::dnsFake},
                        {"type", "fakeip"},
                        {"inet4_range", "198.18.0.0/15"},
                    };
                // No inet6_range makes the transport answer AAAA empty itself; the rule stays on both types.
                if (!settings.fakeip_disable_ipv6) fakeServer["inet6_range"] = "fc00::/18";
                servers += fakeServer;
                rules += QJsonObject{
                        {"query_type", QJsonArray{
                            "A",
                            "AAAA"
                        }},
                     {"action", "route"},
                     {"server", tags::dnsFake}
                };
                independentCache = true;
            }

            if (dns.needDirectDnsRules) {
                appendDnsRoutingRules(rules, dns.direct, tags::dnsDirect, settings.direct_dns_disable_ipv6);
            }

            // A test box builds no dns-remote server at all, so its fall-through goes out direct.
            const bool useDirectFinalDNS = ctx.forTest || settings.dns_final_out == tags::direct;

            if (!ctx.forTest && dns.needProxyDnsRules && useDirectFinalDNS) {
                appendDnsRoutingRules(rules, dns.proxy, tags::dnsRemote, settings.remote_dns_disable_ipv6);
            }

            const bool finalIsDirect = useDirectFinalDNS || vpnDirectFinalDns;
            appendDnsRoute(rules, QJsonObject{},
                           !vpnFinalDnsTag.isEmpty() ? vpnFinalDnsTag
                           : finalIsDirect ? QString(tags::dnsDirect)
                                           : QString(tags::dnsRemote),
                           vpnFinalDnsTag.isEmpty() && (finalIsDirect ? settings.direct_dns_disable_ipv6
                                                                      : settings.remote_dns_disable_ipv6));

            auto dnsLocalAddress = settings.core_box_underlying_dns.isEmpty() ? "local" : settings.core_box_underlying_dns;
            auto dnsLocalObj = buildDnsObj(ctx, dnsLocalAddress);
            dnsLocalObj["tag"] = tags::dnsLocal;
            servers += dnsLocalObj;

            if (!headRules.isEmpty()) {
                for (const auto &rule : rules) headRules.append(rule);
                rules = headRules;
            }

            auto dnsObj = QJsonObject{
                {"servers", servers},
                {"rules", rules},
                {"cache_capacity", settings.dns_cache_capacity},
            };
            if (settings.dns_disable_cache) dnsObj["disable_cache"] = true;
            if (settings.dns_disable_expire) dnsObj["disable_expire"] = true;
            if (settings.dns_reverse_mapping) dnsObj["reverse_mapping"] = true;
            if (independentCache) dnsObj["independent_cache"] = true;
            if (!settings.dns_query_timeout.isEmpty()) dnsObj["timeout"] = settings.dns_query_timeout;
            // The core refuses the config outright when optimistic meets either cache switch.
            if (settings.dns_optimistic && !settings.dns_disable_cache && !settings.dns_disable_expire) {
                if (settings.dns_optimistic_timeout.isEmpty()) dnsObj["optimistic"] = true;
                else dnsObj["optimistic"] = QJsonObject{
                    {"enabled", true},
                    {"timeout", settings.dns_optimistic_timeout},
                };
            }
            ctx.result->coreConfig["dns"] = dnsObj;
        }

        // ------------------------------------------------------------ inbounds

        void buildInboundSection(BuildContext &ctx) {
            if (ctx.forTest) return;
            const auto &settings = *dataManager->settingsRepo;
            const auto &tun = ctx.prerequisites.tun;
            QJsonArray inbounds;

            if (!settings.disable_mixed_inbound) {
                QJsonObject inboundObj;
                inboundObj["tag"] = tags::mixedIn;
                inboundObj["type"] = "mixed";
                inboundObj["listen"] = settings.inbound_address;
                inboundObj["listen_port"] = settings.inbound_socks_port;
                if (settings.inbound_auth) {
                    inboundObj["users"] = QJsonArray{
                        QJsonObject{
                                                {"username", settings.inbound_user},
                                                {"password", settings.inbound_pass}
                        }
                    };
                }
                inbounds += inboundObj;
            }

            if (settings.spmode_vpn) {
                QJsonObject inboundObj;
                inboundObj["tag"] = tags::tunIn;
                inboundObj["type"] = "tun";
                inboundObj["interface_name"] = genTunName();
                inboundObj["auto_route"] = true;
                inboundObj["mtu"] = settings.vpn_mtu;
                inboundObj["stack"] = settings.vpn_implementation;
                inboundObj["strict_route"] = settings.vpn_strict_route;
                if (ctx.os == Linux && settings.vpn_auto_redirect) inboundObj["auto_redirect"] = true;
                const auto tunIPv4CIDR = settings.vpn_tun_ipv4_cidr;
                const auto tunIPv6CIDR = settings.vpn_tun_ipv6_cidr;
                ctx.result->tunIPv4CIDR = tunIPv4CIDR;
                auto tunAddress = QJsonArray{tunIPv4CIDR};
                if (settings.vpn_ipv6) tunAddress += tunIPv6CIDR;
                inboundObj["address"] = tunAddress;

                // sing-tun subtracts route_exclude_address from the routes it installs, so a rule aimed at an excluded range never fires (#1741).
                QJsonArray routeExcludeAddrs;
                QStringList excludedRanges;
                if (!settings.disable_private_range_bypass) {
                    routeExcludeAddrs = {"127.0.0.0/8", "255.255.255.255/32"};
                    for (const auto &range : settings.vpn_private_ranges) {
                        if (!tun.hijackedPrivateRanges.contains(range)) excludedRanges << range;
                    }
                }
                QJsonArray routeExcludeSets;
                if (settings.enable_tun_routing)
                {
                    for (auto item: tun.directIPCIDRs) excludedRanges << item.toString();
                    for (auto item: tun.directIPSets) routeExcludeSets << item;
                }

                // macOS puts the system DNS inside the Tun subnet, so bypassing that range black-holes every query (#1738).
                if (ctx.os == Darwin) excludedRanges = subtractPrefix(excludedRanges, tunIPv4CIDR);
                for (const auto &range : excludedRanges) routeExcludeAddrs << range;
                inboundObj["route_exclude_address"] = routeExcludeAddrs;
                if (!routeExcludeSets.isEmpty()) inboundObj["route_exclude_address_set"] = routeExcludeSets;
                inbounds += inboundObj;
            }

            inbounds.prepend(QJsonObject{
                {"tag", tags::dnsIn},
                {"type", "direct"},
                {"listen", "127.0.0.1"},
                {"listen_port", settings.core_dns_in_port}
            });

            if (settings.enable_redirect) {
                inbounds.prepend(QJsonObject{
                    {"tag", tags::redirectIn},
                    {"type", "direct"},
                    {"listen", settings.redirect_listen_address},
                    {"listen_port", settings.redirect_listen_port},
                });
            }
            if (settings.enable_dns_server) {
                inbounds.prepend(QJsonObject{
                    {"tag", tags::dnsServerIn},
                    {"type", "direct"},
                    {"listen", settings.dns_server_listen_lan ? "0.0.0.0" : "127.1.1.1"},
                    {"listen_port", settings.dns_server_listen_port},
                });
            }

            QJSONARRAY_ADD(inbounds, QString2QJsonObject(settings.custom_inbound)["inbounds"].toArray())
            ctx.result->coreConfig["inbounds"] = inbounds;
        }

        // ----------------------------------------------------------- outbounds

        struct chainScan {
            int hopCount = 0;
            int extraCoreCount = 0;
            int extraCoreIdx = -1;
            int xrayFullConfigCount = 0;
            int xrayFullConfigIdx = -1;
            int xrayHopCount = 0;
            bool hasCustomFullConfig = false;
            int coreTransitions = 0;
        };

        QString chainScanError(const chainScan &scan) {
            if (scan.hasCustomFullConfig)
                return "Custom full config profiles cannot be used in a chain; only custom outbound profiles are chainable";
            if (scan.extraCoreCount > 1)
                return "Only one extra-core profile is allowed in a chain";
            if (scan.xrayFullConfigCount > 1)
                return "Only one custom Xray full config profile is allowed in a chain";
            if (scan.extraCoreCount > 0 && scan.xrayFullConfigCount > 0)
                return "Extra-core and custom Xray full config profiles cannot be combined in a chain";
            if (scan.xrayFullConfigCount > 0 && scan.xrayHopCount > 0)
                return "Custom Xray full config cannot be combined with other Xray hops in a chain (only one Xray instance is supported at a time)";
            // An extra core must be the last hop so its local socks server is dialed directly; sing-box does no hops after it.
            if (scan.extraCoreCount == 1 && scan.hopCount > 1 && scan.extraCoreIdx != scan.hopCount - 1)
                return "Extra-core profiles can only be the final hop in a chain (top of the chain editor)";
            // Same for a custom Xray full config: traffic exits through its socks bridge into the user's Xray.
            if (scan.xrayFullConfigCount == 1 && scan.hopCount > 1 && scan.xrayFullConfigIdx != scan.hopCount - 1)
                return "Custom Xray full config can only be the final hop in a chain (top of the chain editor)";
            if (scan.coreTransitions > 2)
                return "Too many core transitions, the valid sequence is: (optional sing-box chain)->(optional xray chain)->(optional sing-box chain)";
            return {};
        }

        void entIDListtoEntList(BuildContext &ctx, const QList<int> &entIDs, QList<std::shared_ptr<Profile>> &ents, QString &error)
        {
            chainScan scan;
            bool inXray = false;
            for (auto id : entIDs)
            {
                if (id == warpProfileID) {
                    if (inXray) {
                        ctx.xrayToSingTransitioned = true;
                        scan.coreTransitions++;
                    }
                    inXray = false;
                    ents.append(getWarpProfile());
                    continue;
                }
                auto ent = dataManager->profilesRepo->GetProfile(id);
                if (ent == nullptr)
                {
                    error = "Null proxy in chain, you may want to check your configs";
                    return;
                }
                if (!inXray && ent->outbound->IsXray()) {
                    ctx.singToXrayTransitioned = true;
                    scan.coreTransitions++;
                }
                if (inXray && !ent->outbound->IsXray()) {
                    ctx.xrayToSingTransitioned = true;
                    scan.coreTransitions++;
                }
                inXray = ent->outbound->IsXray();
                if (ent->type == "chain")
                {
                    error = "Chain in Chain is not allowed";
                    return;
                }
                // A selector resolves to a different member over time; a chain hop has to stay put.
                if (ent->type == "autoselector")
                {
                    error = "An auto selector cannot be used as a hop; it is not a fixed server";
                    return;
                }
                if (ent->outbound != nullptr && ent->outbound->IsExtraCore()) {
                    scan.extraCoreCount++;
                    scan.extraCoreIdx = static_cast<int>(ents.size());
                }
                if (ent->outbound != nullptr && ent->outbound->IsXrayFullConfig()) {
                    scan.xrayFullConfigCount++;
                    scan.xrayFullConfigIdx = static_cast<int>(ents.size());
                }
                if (ent->outbound != nullptr && ent->outbound->IsXray()) {
                    scan.xrayHopCount++;
                }
                if (isCustomFullConfig(ent)) scan.hasCustomFullConfig = true;
                ents.append(ent);
            }
            scan.hopCount = static_cast<int>(ents.size());
            if (auto scanError = chainScanError(scan); !scanError.isEmpty()) error = scanError;
        }

        QList<int> unwrapChain(int entID) {
            auto ent = dataManager->profilesRepo->GetProfile(entID);
            if (ent == nullptr)
            {
                return {};
            }
            if (ent->type == "chain") {
                auto chain = ent->Chain();
                if (chain == nullptr) return {};
                return {chain->list.rbegin(), chain->list.rend()};
            }
            return {entID};
        }

        struct hopChainOptions {
            QString prefix;
            bool includeProxy = false;
            bool link = true;
            int startSuffix = 0;
            bool markIngress = false;
            bool warpWrap = false;
            bool auxiliary = false;
        };

        void buildSingboxChain(BuildContext &ctx, const QList<std::shared_ptr<Profile>> &ents, const hopChainOptions &opts) {
            for (int idx = 0; idx < ents.size(); idx++)
            {
                auto tag = hopTag(opts.prefix, opts.startSuffix + idx);
                QString nextTag;
                if (idx < ents.size() - 1) nextTag = hopTag(opts.prefix, opts.startSuffix + idx + 1);
                if (opts.includeProxy && idx == 0) tag = tags::proxy;
                // idx 0 is warp under the tag "proxy", so idx 1 takes "warp-bypass" for rules to name.
                if (opts.warpWrap && idx == 1) tag = tags::warpBypass;
                if (opts.markIngress && idx == 0) ctx.singIngressTags << tag;
                const auto& ent = ents[idx];
                // Only the head hop (and warp's wrapped outbound) gets a tag rules can name.
                const bool addressableHop = idx == 0 || (opts.warpWrap && idx == 1);
                if (addressableHop && (ent->type == "openvpn" || ent->type == "openconnect")) {
                    ctx.result->vpnEndpointProfiles.insert(tag, ent->id);
                    const auto *ovpn = ent->OpenVPN();
                    const auto *ocon = ent->OpenConnect();
                    const bool gated = !opts.auxiliary &&
                                       ((ovpn != nullptr && ovpn->only_advertised_routes) ||
                                        (ocon != nullptr && ocon->only_advertised_routes));
                    const bool tunnelDNS = (ovpn != nullptr && ovpn->use_tunnel_dns) ||
                                           (ocon != nullptr && ocon->use_tunnel_dns);
                    // dns-remote detours through this tunnel, so it cannot be the fallback here.
                    const bool carriesProfile = tag == tags::proxy || tag == tags::warpBypass;
                    if (tunnelDNS || (gated && carriesProfile)) ctx.vpnEndpointTags.insert(tag, ent->type);
                    if (gated && carriesProfile)
                        ctx.vpnBlockOutsideDns = (ovpn != nullptr && ovpn->block_outside_dns) ||
                                                 (ocon != nullptr && ocon->block_outside_dns);
                    if (gated) ctx.vpnGateTags << tag;
                }
                auto [object, error] = ent->outbound->Build();
                if (!error.isEmpty())
                {
                    ctx.error += error;
                    return;
                }
                object["tag"] = tag;
                // Realm reads its STUN resolver off this key only; without it the hosts go through DNS rules.
                if (auto hy = ent->Hysteria(); hy != nullptr && hy->RealmActive())
                    object["domain_resolver"] = QJsonObject{{"server", tags::dnsDirect}};
                if (!nextTag.isEmpty() && opts.link) object["detour"] = nextTag;
                if (opts.warpWrap && idx == 0) object["detour"] = tags::warpBypass;
                if (ent->outbound->IsEndpoint())
                {
                    ctx.endpoints.append(object);
                } else
                {
                    ctx.outbounds.append(object);
                }
            }
        }

        void buildXrayChain(BuildContext &ctx, const QList<std::shared_ptr<Profile>> &ents, const hopChainOptions &opts,
                            const coreBridgeConfig &bridgeConfig) {
            for (int idx = 0; idx < ents.size(); idx++)
            {
                auto tag = hopTag(opts.prefix, opts.startSuffix + idx);
                QString nextTag;
                if (idx < ents.size() - 1 || bridgeConfig.needed) nextTag = hopTag(opts.prefix, opts.startSuffix + idx + 1);
                if (opts.includeProxy && idx == 0) tag = tags::proxy;
                if (idx == 0) ctx.xrayIngressTags << tag;
                const auto& ent = ents[idx];
                auto [object, error] = ent->outbound->BuildXray();
                if (!error.isEmpty())
                {
                    ctx.error += error;
                    return;
                }
                object["tag"] = tag;
                if (!nextTag.isEmpty() && (opts.link || bridgeConfig.needed)) object["proxySettings"] = QJsonObject{
                    {"tag", nextTag},
                    {"transportLayer", true}
                };
                ctx.xrayOutbounds.append(object);
            }
            if (bridgeConfig.needed) {
                ctx.xrayOutbounds.append(QJsonObject{
                    {"tag", hopTag(opts.prefix, opts.startSuffix + static_cast<int>(ents.size()))},
                    {"protocol", "socks"},
                    {"settings", QJsonObject{
                        {"address", bridgeConfig.host},
                        {"port", bridgeConfig.port},
                        {"user", bridgeConfig.auth},
                        {"pass", bridgeConfig.auth},
                    }},
                });
            }
        }

        struct ChainBuildRequest {
            QList<int> hopIDs;
            QString prefix;
            bool includeProxy = false;
            bool link = true;
            int startSuffix = 0;
            // Pre-probed bridge ports; -1 lets the chain probe its own.
            int singToXrayPort = -1;
            int xrayToSingPort = -1;
            int xrayFullConfigPort = -1;
            // Sibling configs from one subscription repeat these ports and would fail to bind.
            bool soleXrayInbound = false;
            bool warpWrap = false;
            bool auxiliary = false;
        };

        QString buildOutboundChain(BuildContext &ctx, const ChainBuildRequest &req)
        {
            const auto ingressTag = req.includeProxy ? QString(tags::proxy) : hopTag(req.prefix, req.startSuffix);

            ctx.singToXrayTransitioned = false;
            ctx.xrayToSingTransitioned = false;
            QList<std::shared_ptr<Profile>> ents;
            entIDListtoEntList(ctx, req.hopIDs, ents, ctx.error);
            if (!ctx.error.isEmpty()) return ingressTag;

            if (!ents.isEmpty() && ents.last()->outbound != nullptr && ents.last()->outbound->IsXrayFullConfig()) {
                auto custom = ents.last()->Custom();
                if (custom == nullptr) {
                    ctx.error = "Failed to cast to Custom for Xray full config hop";
                    return ingressTag;
                }
                auto userXrayConfig = QString2QJsonObject(custom->config);
                if (userXrayConfig.isEmpty()) {
                    ctx.error = "Custom Xray full config is not valid JSON";
                    return ingressTag;
                }
                // A pre-probed 0 means the caller's probe failed; re-probe rather than bake in a dead port.
                int port = req.xrayFullConfigPort;
                if (port <= 0) port = MkManyPorts(1, custom->bridgeHost)[0];
                if (port <= 0) {
                    ctx.error = "Could not reserve a local port for the custom Xray full config bridge";
                    return ingressTag;
                }
                custom->bridgePort = port;
                custom->bridgeAuth = GetRandomString(32);

                auto bridgeInbound = xraySocksInbound(tags::xrayFullConfigIn,
                    {true, custom->bridgePort, custom->bridgeAuth, custom->bridgeHost});
                bridgeInbound["sniffing"] = QJsonObject{
                    {"enabled", true},
                    {"destOverride", QJsonArray{"http", "tls", "quic"}},
                    {"routeOnly", false}
                };
                auto inbounds = (ctx.forTest || req.soleXrayInbound) ? QJsonArray()
                                                                     : userXrayConfig["inbounds"].toArray();
                inbounds.prepend(bridgeInbound);
                userXrayConfig["inbounds"] = inbounds;

                ctx.result->xrayConfig = userXrayConfig;
                ctx.result->isXrayNeeded = true;
            }

            QList<std::shared_ptr<Profile>> initialSingEnts;
            QList<std::shared_ptr<Profile>> xrayEnts;
            QList<std::shared_ptr<Profile>> tailingSingEnts;
            for (const auto& ent : ents) {
                if (ent->outbound->IsXray()) xrayEnts.append(ent);
                else {
                    if (xrayEnts.isEmpty()) initialSingEnts.append(ent);
                    else tailingSingEnts.append(ent);
                }
            }
            QList<int> ports;
            // A pre-probed 0 means the caller's probe failed; re-probe rather than bake in a dead port.
            auto bridgePort = [&ports](int given) {
                if (given > 0) return given;
                if (ports.isEmpty()) ports = MkManyPorts(2);
                return ports.takeFirst();
            };
            if (ctx.singToXrayTransitioned) {
                const int port = bridgePort(req.singToXrayPort);
                if (port <= 0) {
                    ctx.error = "Could not reserve a local port for the sing-box -> Xray bridge";
                    return ingressTag;
                }
                coreBridgeConfig singToXrayBridgeConf = {true, port, GetRandomString(32)};
                ctx.singToXrayBridges << singToXrayBridgeConf;
                auto bridgeEnt = ProfilesRepo::NewProfile("socks");
                auto socksOutbound = bridgeEnt->Socks();
                socksOutbound->username = singToXrayBridgeConf.auth;
                socksOutbound->password = singToXrayBridgeConf.auth;
                socksOutbound->server = singToXrayBridgeConf.host;
                socksOutbound->server_port = singToXrayBridgeConf.port;
                initialSingEnts << bridgeEnt;
            }
            coreBridgeConfig xrayToSingBridgeConf;
            if (ctx.xrayToSingTransitioned) {
                const int port = bridgePort(req.xrayToSingPort);
                if (port <= 0) {
                    ctx.error = "Could not reserve a local port for the Xray -> sing-box bridge";
                    return ingressTag;
                }
                xrayToSingBridgeConf = {true, port, GetRandomString(32)};
                ctx.xrayToSingBridges << xrayToSingBridgeConf;
            }

            const hopChainOptions leadingOpts{
                .prefix = req.prefix,
                .includeProxy = req.includeProxy,
                .link = req.link,
                .startSuffix = req.startSuffix,
                .markIngress = false,
                .warpWrap = req.warpWrap,
                .auxiliary = req.auxiliary,
            };
            const int tailingStartSuffix = req.startSuffix + static_cast<int>(initialSingEnts.size());
            if (!initialSingEnts.isEmpty()) {
                buildSingboxChain(ctx, initialSingEnts, leadingOpts);
            }
            if (!xrayEnts.isEmpty()) {
                buildXrayChain(ctx, xrayEnts, leadingOpts, xrayToSingBridgeConf);
            }
            if (!tailingSingEnts.isEmpty()) {
                buildSingboxChain(ctx, tailingSingEnts, {
                    .prefix = req.prefix,
                    .includeProxy = false,
                    .link = req.link,
                    .startSuffix = tailingStartSuffix,
                    .markIngress = true,
                    .warpWrap = false,
                });
            }

            if (!ents.isEmpty()) {
                TrafficChainGroup group;
                group.profiles = ents;
                if (!tailingSingEnts.isEmpty()) {
                    group.watchTag = hopTag(req.prefix, tailingStartSuffix);
                } else {
                    group.watchTag = ingressTag;
                }
                ctx.result->chainGroups.append(group);
            }
            return ingressTag;
        }

        // Counted the same way entIDListtoEntList counts, so the ports line up with the bridges buildOutboundChain creates.
        struct memberBridges
        {
            bool singToXray = false;
            bool xrayToSing = false;
            bool xrayFullConfig = false;

            [[nodiscard]] int count() const
            {
                return (singToXray ? 1 : 0) + (xrayToSing ? 1 : 0) + (xrayFullConfig ? 1 : 0);
            }
        };

        memberBridges bridgesFor(const QList<int> &hopIDs)
        {
            memberBridges needed;
            bool inXray = false;
            for (int id : hopIDs)
            {
                auto hop = dataManager->profilesRepo->GetProfile(id);
                if (hop == nullptr || hop->outbound == nullptr) continue;
                if (hop->outbound->IsXrayFullConfig()) needed.xrayFullConfig = true;
                const bool xray = hop->outbound->IsXray();
                if (xray && !inXray) needed.singToXray = true;
                if (!xray && inXray) needed.xrayToSing = true;
                inXray = xray;
            }
            return needed;
        }

        // Concurrent because the RPC channel multiplexes by request id and the core answers each in its own goroutine.
        QSet<int> invalidProfileIDs(const QList<int> &ids)
        {
            QSet<int> invalid;
            if (ids.isEmpty()) return invalid;
            QMutex mu;
            QThreadPool pool;
            pool.setMaxThreadCount(10);
            for (int id : ids)
            {
                pool.start([&, id] {
                    const auto ent = dataManager->profilesRepo->GetProfile(id);
                    if (ent == nullptr || IsValid(ent)) return;
                    QMutexLocker lock(&mu);
                    invalid.insert(id);
                });
            }
            pool.waitForDone();
            return invalid;
        }

        QString buildAutoSelectorGroup(BuildContext &ctx, const std::shared_ptr<Group> &group, bool warpWrap)
        {
            const auto &settings = *dataManager->settingsRepo;
            auto selector = ctx.ent->AutoSelector();
            if (selector == nullptr)
            {
                ctx.error = "Ent is nullptr after cast to auto selector, data is corrupted";
                return {};
            }
            selector->Normalize();
            const auto plan = PlanAutoSelector(ctx.ent);
            if (!plan.error.isEmpty())
            {
                ctx.error = plan.error;
                return {};
            }

            // With warp in front every member sits behind the single "proxy"-tagged warp outbound, so the group takes warp-bypass.
            const QString groupTag = warpWrap ? tags::warpBypass : tags::proxy;
            const int chainGroupsBefore = static_cast<int>(ctx.result->chainGroups.size());

            AutoSelectorBuildInfo info;
            info.groupTag = groupTag;
            info.profile = ctx.ent;

            // One MkManyPorts call for every member: probing per member can deal the same port twice.
            struct plannedMember
            {
                std::shared_ptr<Profile> ent;
                QList<int> hopIDs;
                memberBridges bridges;
            };
            QList<plannedMember> planned;
            int bridgeCount = 0;
            // One member the core rejects fails the whole group's start, so drop it instead.
            const auto invalid = invalidProfileIDs(plan.build);
            for (int id : plan.build)
            {
                if (invalid.contains(id)) continue;
                auto member = dataManager->profilesRepo->GetProfile(id);
                if (member == nullptr) continue;
                QList<int> hopIDs;
                if (group->landing_proxy_id >= 0) hopIDs.append(group->landing_proxy_id);
                hopIDs.append(id);
                if (group->front_proxy_id >= 0) hopIDs.append(group->front_proxy_id);
                const auto bridges = bridgesFor(hopIDs);
                bridgeCount += bridges.count();
                planned.append({member, hopIDs, bridges});
            }
            auto bridgePorts = MkManyPorts(bridgeCount);
            int portIdx = 0;
            
            const auto builtAt = QDateTime::currentSecsSinceEpoch();
            QJsonArray warm;
            QString pinnedTag;

            QJsonArray memberTags;
            int idx = 0;
            for (const auto &[member, hopIDs, bridges] : planned)
            {
                const int singToXrayPort = bridges.singToXray ? bridgePorts[portIdx++] : -1;
                const int xrayToSingPort = bridges.xrayToSing ? bridgePorts[portIdx++] : -1;
                const int xrayFullConfigPort = bridges.xrayFullConfig ? bridgePorts[portIdx++] : -1;

                const auto tag = buildOutboundChain(ctx, {
                    .hopIDs = hopIDs,
                    .prefix = hopTag(tags::poolChainPrefix, idx),
                    .singToXrayPort = singToXrayPort,
                    .xrayToSingPort = xrayToSingPort,
                    .xrayFullConfigPort = xrayFullConfigPort,
                    .soleXrayInbound = bridges.xrayFullConfig,
                });
                if (!ctx.error.isEmpty()) return {};
                // buildOutboundChain has one xrayConfig slot; drain it per member.
                if (bridges.xrayFullConfig)
                {
                    if (ctx.result->xrayConfig.isEmpty())
                    {
                        ctx.error = "Custom Xray full config member produced no Xray config";
                        return {};
                    }
                    ctx.result->xrayFullConfigs << QJsonObject2QString(ctx.result->xrayConfig, false);
                    ctx.result->xrayConfig = QJsonObject();
                    ctx.result->isXrayNeeded = false;
                }
                memberTags.append(tag);
                info.members.append({tag, member});
                if (member->id == selector->pinnedID) pinnedTag = tag;
                if (member->latency != 0 && member->latency_at > 0 && selector->resultValidityMins > 0)
                {
                    if (const auto age = builtAt - member->latency_at;
                        age >= 0 && age <= static_cast<qint64>(selector->resultValidityMins) * 60)
                    {
                        warm.append(QJsonObject{
                            {"tag", tag},
                            // rtt 0 is how the core reads "known bad" rather than "never measured".
                            {"rtt", member->latency > 0 ? member->latency : 0},
                            {"age", static_cast<double>(age)},
                        });
                    }
                }
                if (!ctx.result->chainGroups.isEmpty())
                    ctx.result->chainGroups.last().profiles.append(ctx.ent);
                idx++;
            }
            if (memberTags.isEmpty())
            {
                ctx.error = "Auto selector produced no usable members";
                return {};
            }

            if (warpWrap)
            {
                // Bytes land on the warp outbound now, so the per-member watch tags would all read zero.
                QList<std::shared_ptr<Profile>> credited;
                while (ctx.result->chainGroups.size() > chainGroupsBefore)
                    credited << ctx.result->chainGroups.takeLast().profiles;
                TrafficChainGroup warpGroup;
                warpGroup.watchTag = tags::proxy;
                warpGroup.profiles = credited;
                ctx.result->chainGroups.append(warpGroup);
            }

            QJsonObject groupObject{
                {"type", "auto-selector"},
                {"tag", groupTag},
                {"outbounds", memberTags},
                {"url", selector->testURL.isEmpty() ? settings.test_latency_url : selector->testURL},
                {"interval", Int2String(selector->intervalSec) + "s"},
                {"bench_interval", Int2String(selector->benchIntervalSec) + "s"},
                {"watch_interval", Int2String(selector->watchIntervalSec) + "s"},
                {"active_size", selector->activeSize},
                {"sampling", selector->sampling},
                {"tolerance", selector->toleranceMs},
                {"expected", selector->expected},
                {"dial_retries", selector->dialRetries},
                {"interrupt_exist_connections", selector->interruptOnSwitch},
            };
            if (!warm.isEmpty()) groupObject["warm"] = warm;
            if (!pinnedTag.isEmpty()) groupObject["pinned"] = pinnedTag;
            if (selector->maxRTTms > 0) groupObject["max_rtt"] = Int2String(selector->maxRTTms) + "ms";
            // Never the latency test URL: that one is fetched through the proxy, and is routinely
            // blocked directly. Omitted when unset, so the core uses the OS network state.
            const auto connectivityURL = selector->connectivityURL.isEmpty() ? settings.direct_test_url
                                                                             : selector->connectivityURL;
            if (!connectivityURL.isEmpty()) groupObject["connectivity_url"] = connectivityURL;
            if (selector->balance)
            {
                groupObject["balance"] = true;
                groupObject["balance_mode"] = selector->balanceMode;
                groupObject["balance_interval"] = Int2String(selector->balanceIntervalSec) + "s";
            }
            ctx.outbounds.append(groupObject);
            ctx.result->autoSelectors.append(info);
            return groupTag;
        }

        // The group already holds warp-bypass, so warp itself is emitted here as "proxy" and points at it.
        void buildWarpInFrontOfSelector(BuildContext &ctx)
        {
            auto warpEnt = getWarpProfile();
            auto [warpObject, warpError] = warpEnt->outbound->Build();
            if (!warpError.isEmpty())
            {
                ctx.error += warpError;
                return;
            }
            warpObject["tag"] = tags::proxy;
            warpObject["detour"] = tags::warpBypass;
            if (warpEnt->outbound->IsEndpoint()) ctx.endpoints.append(warpObject);
            else ctx.outbounds.append(warpObject);
        }

        void buildOutboundsSection(BuildContext &ctx) {
            auto group = dataManager->groupsRepo->GetGroup(ctx.ent->gid);
            if (group == nullptr)
            {
                ctx.error = "No group found for ent, data is corrupted";
                return;
            }
            const bool warpWrap = dataManager->settingsRepo->enable_warp;
            if (ctx.ent->type == "autoselector")
            {
                buildAutoSelectorGroup(ctx, group, warpWrap);
                if (!ctx.error.isEmpty()) return;
                if (warpWrap)
                {
                    buildWarpInFrontOfSelector(ctx);
                    if (!ctx.error.isEmpty()) return;
                }
            }
            else
            {
                QList<int> entIDs;
                if (group->landing_proxy_id >= 0) entIDs.prepend(group->landing_proxy_id);
                if (ctx.ent->type == "chain")
                {
                    auto chain = ctx.ent->Chain();
                    if (chain == nullptr)
                    {
                        ctx.error = "Ent is nullptr after cast to chain, data is corrupted";
                        return;
                    }
                    entIDs.reserve(entIDs.size() + chain->list.size());
                    std::copy(chain->list.crbegin(), chain->list.crend(),
                             std::back_inserter(entIDs));
                } else
                {
                    entIDs.append(ctx.ent->id);
                }
                if (group->front_proxy_id >= 0) entIDs.append(group->front_proxy_id);
                if (warpWrap) {
                    entIDs.prepend(warpProfileID);
                }
                buildOutboundChain(ctx, {
                    .hopIDs = entIDs,
                    .prefix = tags::mainChainPrefix,
                    .includeProxy = true,
                    .warpWrap = warpWrap,
                });

                if (ctx.ent->type == "chain" && !ctx.result->chainGroups.isEmpty()) {
                    ctx.result->chainGroups.last().profiles.append(ctx.ent);
                }
            }

            int routeSuffix = 0;
            for (const auto& routeGroup : ctx.prerequisites.routing.routeOutboundGroups) {
                buildOutboundChain(ctx, {
                    .hopIDs = routeGroup.hopIDs,
                    .prefix = tags::routeChainPrefix,
                    .link = routeGroup.hopIDs.size() > 1,
                    .startSuffix = routeSuffix,
                });
                if (routeGroup.chainWrapper != nullptr && !ctx.result->chainGroups.isEmpty()) {
                    ctx.result->chainGroups.last().profiles.append(routeGroup.chainWrapper);
                }
                routeSuffix += static_cast<int>(routeGroup.hopIDs.size());
            }

            // preferred_by resolves an endpoint out of the endpoint manager, so nothing detours into these.
            if (!ctx.forTest) {
                int auxSuffix = 0;
                for (const auto &hopIDs : ctx.prerequisites.routing.auxEndpointGroups) {
                    const auto tag = buildOutboundChain(ctx, {
                        .hopIDs = hopIDs,
                        .prefix = tags::auxEndpointPrefix,
                        .includeProxy = false,
                        .link = hopIDs.size() > 1,
                        .startSuffix = auxSuffix,
                        .auxiliary = true,
                    });
                    if (!ctx.error.isEmpty()) return;
                    ctx.vpnAuxTags << tag;
                    auxSuffix += static_cast<int>(hopIDs.size());
                }
            }

            if (auto mismatch = bridgeIngressMismatch(ctx); !mismatch.isEmpty()) {
                ctx.error = mismatch;
                return;
            }
            QJsonArray inboundArr;
            if (ctx.result->coreConfig.contains("inbounds")) {
                inboundArr = ctx.result->coreConfig["inbounds"].toArray();
            }
            for (qsizetype idx = 0; idx < ctx.xrayToSingBridges.size(); idx++) {
                inboundArr.append(socksBridgeInbound(bridgeTagFor(ctx.singIngressTags[idx]), ctx.xrayToSingBridges[idx]));
            }
            ctx.result->coreConfig["inbounds"] = inboundArr;

            ctx.outbounds.append(QJsonObject{
            {"type", "direct"},
            {"tag", tags::direct}
            });

            if (ctx.l3Bridge) {
                ctx.outbounds.append(QJsonObject{
                {"type", "bridge"},
                {"tag", tags::l3Direct}
                });
            }

            ctx.result->coreConfig["endpoints"] = ctx.endpoints;
            ctx.result->coreConfig["outbounds"] = ctx.outbounds;
        }

        // --------------------------------------------------------------- route

        QJsonArray buildRuleSetArray(const BuildContext &ctx) {
            QJsonArray ruleSetArray;
            for (const auto &item: ctx.prerequisites.routing.neededRuleSets) {
                if (auto url = QUrl(item); url.isValid() && url.fileName().contains(".srs")) {
                    ruleSetArray += QJsonObject{
                                {"type", "remote"},
                                {"tag", get_rule_set_name(item)},
                                {"format", "binary"},
                                {"url", item},
                            };
                }
                else
                    if (auto url = ruleSetUrl(item.toStdString()); !url.empty()) {
                        ruleSetArray += QJsonObject{
                                    {"type", "remote"},
                                    {"tag", item},
                                    {"format", "binary"},
                                    {"url", get_jsdelivr_link(QString::fromUtf8(url.data(), url.size()))},
                                };
                    }
            }

            if (dataManager->settingsRepo->adblock_enable) {
                ruleSetArray += QJsonObject{
                            {"type", "remote"},
                            {"tag", tags::adblockRuleSet},
                            {"format", "binary"},
                            {"url", get_jsdelivr_link("https://raw.githubusercontent.com/217heidai/adblockfilters/main/rules/adblocksingbox.srs")},
                        };
            }
            return ruleSetArray;
        }

        void buildRouteSection(BuildContext &ctx) {
            const auto &settings = *dataManager->settingsRepo;
            auto routeChain = dataManager->routesRepo->GetRouteProfile(settings.current_route_id);
            if (routeChain == nullptr) {
                ctx.error = "Routing profile does not exist, try resetting the route profile in Routing Settings";
                return;
            }
            routeChain = std::make_shared<RouteProfile>(*routeChain);
            const auto &routeDeps = ctx.prerequisites.routing;

            QJsonObject rawRouteObj;
            if (routeChain->isRaw) {
                rawRouteObj = QString2QJsonObject(routeChain->rawRoute);
                if (rawRouteObj.isEmpty()) {
                    ctx.error = "Raw routing profile is not a valid JSON object";
                    return;
                }
                rawRouteObj = RouteProfile::TranslateRawOutbounds(rawRouteObj, routeDeps.outboundMap);
                if (routeChain->preventModifications) {
                    // The raw JSON is verbatim, but endpoint tags are internal so the user cannot name them.
                    if (!ctx.vpnAuxTags.isEmpty()) {
                        auto rawRules = rawRouteObj.value("rules").toArray();
                        for (const auto &auxTag : ctx.vpnAuxTags) {
                            rawRules.append(QJsonObject{
                                {"preferred_by", QJsonArray{auxTag}},
                                {"action", "route"},
                                {"outbound", auxTag},
                            });
                        }
                        rawRouteObj["rules"] = rawRules;
                    }
                    ctx.result->coreConfig["route"] = rawRouteObj;
                    return;
                }
            }

            struct InjectedRules {
                QJsonObject sniff;
                QJsonObject resolve;
                QJsonObject dnsHijack;
                QJsonObject dnsInReject;
                QJsonObject redirectSniff;
            } injected;

            if (!routeChain->isRaw) {
                injected.sniff = QJsonObject{{"action", "sniff"}};
                if (!settings.resolve_domain_strategy.isEmpty()) {
                    injected.resolve = QJsonObject{
                        {"inbound", QJsonArray{tags::mixedIn, tags::tunIn}},
                        {"action", "resolve"},
                        {"strategy", settings.resolve_domain_strategy},
                    };
                }
                injected.dnsHijack = QJsonObject{
                    {"protocol", "dns"},
                    {"action", "hijack-dns"},
                };
                if (settings.enable_redirect && !ctx.forTest) {
                    injected.redirectSniff = QJsonObject{
                        {"inbound", QJsonArray{tags::redirectIn}},
                        {"action", "sniff"},
                        {"override_destination", true},
                    };
                }
            }
            if (!ctx.forTest) {
                injected.dnsInReject = QJsonObject{
                    {"inbound", tags::dnsIn},
                    {"action", "reject"},
                };
            }

            auto profileRules = routeChain->isRaw ? rawRouteObj.value("rules").toArray()
                                                  : routeChain->get_route_rules(false, routeDeps.outboundMap);
            if (ctx.l3Bridge) profileRules = withL3BridgeTwins(profileRules);

            QJsonObject extraCoreDirect;
            if (!ctx.result->extraCoreData->path.isEmpty())
            {
                extraCoreDirect = QJsonObject{
                    {"action", "route"},
                    {"process_path", extraCoreProcessPaths(ctx.result->extraCoreData->path)},
                    {"outbound", tags::direct},
                };
            }

            auto ruleSetArray = buildRuleSetArray(ctx);

            if (auto mismatch = bridgeIngressMismatch(ctx); !mismatch.isEmpty()) {
                ctx.error = mismatch;
                return;
            }
            QJsonArray bridgeRules;
            for (qsizetype idx = 0; idx < ctx.xrayToSingBridges.size(); idx++) {
                bridgeRules.append(QJsonObject{
                    {"inbound", bridgeTagFor(ctx.singIngressTags[idx])},
                    {"action", "route"},
                    {"outbound", ctx.singIngressTags[idx]},
                });
            }

            // raw profiles bring their own rule_set definitions; merge them after ours.
            if (routeChain->isRaw) {
                for (const auto& rs : rawRouteObj.value("rule_set").toArray()) ruleSetArray.append(rs);
            }

            const int defOut = routeChain->defaultOutboundID;
            const QString finalTag = routeChain->isRaw
                ? (rawRouteObj.contains("final") ? rawRouteObj.value("final").toString() : QString(tags::proxy))
                : defOut == blockID       ? QString(tags::direct)
                : defOut == warpBypassID  ? QString(settings.enable_warp ? tags::warpBypass : tags::proxy)
                                          : outboundIDToString(defOut);

            // An endpoint rule the user positioned already emitted this tag's gate.
            auto carriesGate = [](const QJsonArray &rules, const QString &tag) {
                for (const auto &r : rules) {
                    for (const auto &p : r.toObject().value("preferred_by").toArray())
                        if (p.toString() == tag) return true;
                }
                return false;
            };
            QJsonArray vpnAuxRules;
            for (const auto &auxTag : ctx.vpnAuxTags) {
                if (!routeChain->isRaw && carriesGate(profileRules, auxTag)) continue;
                vpnAuxRules.append(QJsonObject{
                    {"preferred_by", QJsonArray{auxTag}},
                    {"action", "route"},
                    {"outbound", auxTag},
                });
            }

            // defOut == blockID also yields finalTag "direct", but rejects before reaching it.
            QJsonArray l3BridgeFinalRules;
            if (ctx.l3Bridge && finalTag == tags::direct && (routeChain->isRaw || defOut == directID)) {
                l3BridgeFinalRules.append(QJsonObject{
                    {"preferred_by", QJsonArray{tags::l3Direct}},
                    {"action", "route"},
                    {"outbound", tags::l3Direct},
                });
            }

            QJsonArray vpnFallthroughRules;
            if (!ctx.forTest && ctx.vpnGateTags.contains(finalTag)) {
                vpnFallthroughRules.append(QJsonObject{
                    {"preferred_by", QJsonArray{finalTag}},
                    {"action", "route"},
                    {"outbound", finalTag},
                });
                vpnFallthroughRules.append(QJsonObject{{"action", "reject"}});
            }

            QJsonArray routeRules;
            for (const auto& r : bridgeRules) routeRules.append(r);
            if (!extraCoreDirect.isEmpty()) routeRules.append(extraCoreDirect);
            auto appendIfSet = [&routeRules](const QJsonObject& r) { if (!r.isEmpty()) routeRules.append(r); };
            appendIfSet(injected.sniff);
            appendIfSet(injected.resolve);
            appendIfSet(injected.dnsHijack);
            appendIfSet(injected.dnsInReject);
            appendIfSet(injected.redirectSniff);
            for (const auto& r : profileRules) routeRules.append(r);
            for (const auto& r : vpnAuxRules) routeRules.append(r);
            for (const auto& r : l3BridgeFinalRules) routeRules.append(r);
            // final still names the tunnel, but nothing may reach it unmatched.
            for (const auto& r : vpnFallthroughRules) routeRules.append(r);
            if (!routeChain->isRaw && defOut == blockID) {
                routeRules.append(QJsonObject{{"action", "reject"}});
            }

            QJsonObject route = routeChain->isRaw ? rawRouteObj : QJsonObject{};
            route["rules"] = routeRules;
            route["rule_set"] = ruleSetArray;
            if (routeChain->isRaw) {
                if (!route.contains("final")) route["final"] = tags::proxy;
            } else {
                route["final"] = finalTag;
            }
            if (settings.enable_stats && !route.contains("find_process"))  route["find_process"] = true;
            if (!route.contains("default_domain_resolver"))
                route["default_domain_resolver"] = QJsonObject{
                                        {"server", tags::dnsDirect},
                                        {"strategy", getDirectDomainStrategy()}};
            if (settings.spmode_vpn && !route.contains("auto_detect_interface")) route["auto_detect_interface"] = true;

            ctx.result->coreConfig["route"] = route;
        }

        // -------------------------------------------------------- experimental

        void buildExperimentalSection(BuildContext &ctx) {
            if (ctx.forTest) return;
            const auto &settings = *dataManager->settingsRepo;

            QJsonObject experimentalObj;
            // Only the yacd listener now; the stats tracker comes from buildServicesSection.
            if (settings.core_box_clash_api > 0) {
                experimentalObj["clash_api"] = QJsonObject{
                    {"external_controller", settings.core_box_clash_listen_addr + ":" + Int2String(settings.core_box_clash_api)},
                    {"secret", settings.core_box_clash_api_secret},
                    {"external_ui", "dashboard"},
                };
            }

            experimentalObj["cache_file"] = QJsonObject{
                {"enabled", true},
                {"store_fakeip", true},
                {"store_dns", true}
            };

            ctx.result->coreConfig["experimental"] = experimentalObj;
        }

        // ------------------------------------------------------------- services

        // The core builds the traffic tracker from the mere presence of an api service.
        void buildServicesSection(BuildContext &ctx) {
            if (ctx.forTest) return;
            const auto &settings = *dataManager->settingsRepo;

            const bool dashboard = settings.core_box_api_port > 0;
            if (!dashboard && !settings.enable_stats) return;

            QJsonObject api = {
                {"type", "api"},
                {"listen", "127.0.0.1"},
                {"listen_port", dashboard ? settings.core_box_api_port : 0},
                {"secret", settings.core_box_api_secret},
            };
            if (dashboard) {
                // Defaults to "*", i.e. any page the user visits could reach loopback.
                api["access_control_allow_origin"] = QJsonArray{
                    "http://127.0.0.1:" + Int2String(settings.core_box_api_port)
                };
                api["dashboard"] = QJsonObject{
                    {"enabled", true},
                    {"path", apiDashboardDir},
                };
            }

            ctx.result->coreConfig["services"] = QJsonArray{api};
        }

        // ----------------------------------------------------------------- xray

        void buildXrayConfig(BuildContext &ctx) {
            if (ctx.xrayOutbounds.isEmpty()) return;
            ctx.result->isXrayNeeded = true;
            QJsonArray inbounds;
            QJsonArray routeRules;

            if (ctx.xrayIngressTags.size() != ctx.singToXrayBridges.size()) {
                ctx.error = "xray ingress tags size does not match bridge count!";
                return;
            }

            for (qsizetype i = 0; i < ctx.xrayIngressTags.size(); i++) {
                const auto outboundTag = ctx.xrayIngressTags[i];
                const auto inboundTag = outboundTag + "-" + "inbound";
                inbounds << xraySocksInbound(inboundTag, ctx.singToXrayBridges[i]);
                routeRules << QJsonObject{
                    {"type", "field"},
                    {"inboundTag", QJsonArray{inboundTag}},
                    {"outboundTag", outboundTag}
                };
            }

            ctx.result->xrayConfig["log"] = QJsonObject{
            {"loglevel", dataManager->settingsRepo->xray_log_level},
            {"access", dataManager->settingsRepo->xray_log_level == "info" ? "" : "none"}
            };
            ctx.result->xrayConfig["inbounds"] = inbounds;
            ctx.result->xrayConfig["outbounds"] = ctx.xrayOutbounds;
            ctx.result->xrayConfig["routing"] = QJsonObject{
                {"domainStrategy", "AsIs"},
                {"rules", routeRules},
            };
        }

        // ------------------------------------------------------- test candidates

        enum class testCandidate {
            Build,           // ordinary profile or chain, built into the shared test box
            XrayFullConfig,  // opaque user Xray config, gets its own Xray instance
            Skip,
        };

        struct testCandidateKind {
            testCandidate kind = testCandidate::Build;
            const char *skipReason = nullptr;
        };

        testCandidateKind classifyTestCandidate(const std::shared_ptr<Profile> &profile)
        {
            if (profile->outbound != nullptr && profile->outbound->IsExtraCore())
                return {testCandidate::Skip, "Skipping extra-core conf"};
            if (profile->outbound != nullptr && profile->outbound->IsXrayFullConfig())
                return {testCandidate::XrayFullConfig, nullptr};
            if (profile->type == "chain")
            {
                if (auto chain = profile->Chain(); chain != nullptr) {
                    for (int hopID : chain->list) {
                        auto hopEnt = dataManager->profilesRepo->GetProfile(hopID);
                        if (hopEnt != nullptr && hopEnt->outbound != nullptr &&
                            (hopEnt->outbound->IsExtraCore() || hopEnt->outbound->IsXrayFullConfig()))
                            return {testCandidate::Skip, "Skipping chain with terminal (extra-core or Xray full config) hop (cannot test)"};
                    }
                }
                return {testCandidate::Build, nullptr};
            }
            if (profile->type == "tailscale")
                return {testCandidate::Skip, "Skipping Tailscale conf"};
            if (profile->type == "autoselector")
                return {testCandidate::Skip, "Skipping auto selector conf (test its members instead)"};
            return {testCandidate::Build, nullptr};
        }

    } // namespace

    bool ParsePredefinedDNS(const QStringList& lines, QList<PredefinedDNSEntry>& out, QString* error) {
        QMap<QString, int> indexOf;
        for (const auto& rawLine : lines) {
            auto line = rawLine;
            if (const auto hash = line.indexOf('#'); hash != -1) line = line.left(hash);
            const auto fields = line.simplified().split(' ', Qt::SkipEmptyParts);
            if (fields.isEmpty()) continue;

            QHostAddress addr;
            if (fields.size() < 2 || !addr.setAddress(fields[0])) {
                if (error != nullptr) *error = rawLine.trimmed();
                return false;
            }
            addr.setScopeId({});
            const bool isV6 = addr.protocol() == QAbstractSocket::IPv6Protocol;

            for (qsizetype i = 1; i < fields.size(); i++) {
                auto domain = fields[i].toLower();
                while (domain.endsWith('.')) domain.chop(1);
                if (domain.isEmpty()) {
                    if (error != nullptr) *error = rawLine.trimmed();
                    return false;
                }
                if (!indexOf.contains(domain)) {
                    indexOf[domain] = static_cast<int>(out.size());
                    out.append(PredefinedDNSEntry{domain, {}, {}});
                }
                auto& bucket = isV6 ? out[indexOf[domain]].v6 : out[indexOf[domain]].v4;
                if (const auto text = addr.toString(); !bucket.contains(text)) bucket.append(text);
            }
        }
        return true;
    }

    bool IsValidDuration(const QString& text) {
        static const QRegularExpression re(R"(^(?:\d+(?:\.\d+)?(?:ns|us|ms|s|m|h|d))+$)");
        return re.match(text).hasMatch();
    }

    std::shared_ptr<BuildConfigResult> BuildSingBoxConfig(const std::shared_ptr<Profile>& ent) {
        if (ent->type == "custom")
        {
            auto res = std::make_shared<BuildConfigResult>();
            auto custom = ent->Custom();
            if (custom == nullptr)
            {
                res->error = "Corrupted data, needed custom ent, got nullptr";
                return res;
            }
            if (custom->type == Custom::CustomFullConfig)
            {
                res->coreConfig = custom->Build().object;
                return res;
            }
        }

        BuildContext ctx;
        ctx.ent = ent;

        auto failed = [&ctx] {
            if (ctx.error.isEmpty()) return false;
            MW_show_log("Config build error:" + ctx.error);
            ctx.result->error = ctx.error;
            return true;
        };

        calculatePrerequisites(ctx);
        if (failed()) return ctx.result;

        buildLogSection(ctx);
        buildNTPSection(ctx);
        buildCertificateSection(ctx);
        buildInboundSection(ctx);
        if (failed()) return ctx.result;

        buildOutboundsSection(ctx);
        if (failed()) return ctx.result;

        // Ordered after the outbounds: it needs the tags the VPN endpoint hops landed on.
        buildDNSSection(ctx);
        if (failed()) return ctx.result;

        buildRouteSection(ctx);
        if (failed()) return ctx.result;

        buildExperimentalSection(ctx);
        if (failed()) return ctx.result;

        buildServicesSection(ctx);
        if (failed()) return ctx.result;

        buildXrayConfig(ctx);
        if (failed()) return ctx.result;

        return ctx.result;
    }

    bool CanBeAuxEndpoint(const std::shared_ptr<Profile>& ent)
    {
        if (ent == nullptr || ent->outbound == nullptr) return false;
        if (ent->type == "openvpn" || ent->type == "openconnect") return true;
        if (ent->type != "chain") return false;
        // unwrapChain reverses the stored list, so hop 0 is the exit.
        const auto hopIDs = unwrapChain(ent->id);
        if (hopIDs.isEmpty()) return false;
        const auto exitEnt = dataManager->profilesRepo->GetProfile(hopIDs.first());
        if (exitEnt == nullptr) return false;
        return exitEnt->type == "openvpn" || exitEnt->type == "openconnect";
    }

    bool IsValid(const std::shared_ptr<Profile>& ent)
    {
        if (ent->type == "autoselector")
        {
            const auto plan = PlanAutoSelector(ent);
            if (!plan.error.isEmpty())
            {
                MW_show_log("Invalid auto selector: " + plan.error);
                return false;
            }
            return !plan.build.isEmpty();
        }
        if (ent->type == "chain")
        {
            auto chain = ent->Chain();
            if (chain == nullptr)
            {
                MW_show_log("Corrupted data, needed chain ent, got nullptr");
                return false;
            }
            for (int eId : chain->list)
            {
                auto e = dataManager->profilesRepo->GetProfile(eId);
                if (e == nullptr)
                {
                    MW_show_log("Null ent in validator");
                    return false;
                }
                if (!IsValid(e))
                {
                    MW_show_log("Invalid ent in chain: ID=" + QString::number(eId));
                    return false;
                }
            }
            return true;
        }
        QJsonObject conf;
        bool fullConf = false;
        if (ent->type == "custom")
        {
            auto custom = ent->Custom();
            if (custom == nullptr)
            {
                MW_show_log("Corrupted data in isValid, needed custom ent, got nullptr");
                return false;
            }
            if (custom->type == Custom::CustomFullConfig)
            {
                conf = QString2QJsonObject(custom->config);
                fullConf = true;
            }
            if (custom->type == Custom::CustomXrayFullConfig)
            {
                auto xrayConf = QString2QJsonObject(custom->config);
                if (xrayConf.isEmpty()) {
                    MW_show_log("Custom Xray full config is not valid JSON");
                    return false;
                }
                // Throne never runs these; it prepends its own bridge inbound instead.
                xrayConf.remove("inbounds");
                bool ok;
                auto resp = API::defaultClient->CheckConfig(&ok, QJsonObject2QString(xrayConf, true), true);
                if (!ok)
                {
                    MW_show_log("Failed to Call the Core: " + resp);
                    return false;
                }
                if (resp.isEmpty()) return true;
                // Left to fail at test time so handleXrayGeoAssetError() can name the missing category.
                if (resp.contains("geoip.dat") || resp.contains("geosite.dat")) return true;
                MW_show_log("Invalid Xray ent " + ent->outbound->name + ": " + resp);
                return false;
            }
        }
        // Xray outbounds carry only a dummy sing-box Build(); validate the real one via the Xray core.
        if (!fullConf && ent->outbound->IsXray())
        {
            auto [out, err] = ent->outbound->BuildXray();
            if (!err.isEmpty())
            {
                MW_show_log("Invalid Xray ent " + ent->outbound->name + ": " + err);
                return false;
            }
            QJsonObject xrayConf{
                {"outbounds", QJsonArray{out}},
            };
            bool ok;
            auto resp = API::defaultClient->CheckConfig(&ok, QJsonObject2QString(xrayConf, true), true);
            if (!ok)
            {
                MW_show_log("Failed to Call the Core: " + resp);
                return false;
            }
            if (resp.isEmpty()) return true;
            MW_show_log("Invalid Xray ent " + ent->outbound->name + ": " + resp);
            return false;
        }
        if (!fullConf)
        {
            auto out = ent->outbound->Build();
            auto outArr = QJsonArray{out.object};
            auto key = ent->outbound->IsEndpoint() ? "endpoints" : "outbounds";
            conf = {
                {key, outArr},
                };
        }
        bool ok;
        conf.insert("log", QJsonObject{{"level", dataManager->settingsRepo->log_level}});
        auto resp = API::defaultClient->CheckConfig(&ok, QJsonObject2QString(conf, true));
        if (!ok)
        {
            MW_show_log("Failed to Call the Core: " + resp);
            return false;
        }
        if (resp.isEmpty()) return true;
        MW_show_log("Invalid ent " + ent->outbound->name + ": " + resp);
        return false;
    }

    std::shared_ptr<BuildTestConfigResult> BuildTestConfig(const QList<std::shared_ptr<Profile> > &profiles)
    {
        auto res = std::make_shared<BuildTestConfigResult>();
        // outbound::Build() cannot see BuildContext::forTest.
        SetBuildingTestConfig(true);
        const auto clearTestBuildFlag = qScopeGuard([] { SetBuildingTestConfig(false); });
        BuildContext ctx;
        ctx.forTest = true;
        buildDNSSection(ctx, false);
        if (!ctx.error.isEmpty())
        {
            res->error = ctx.error;
            return res;
        }
        buildLogSection(ctx);
        buildCertificateSection(ctx);
        buildNTPSection(ctx);
        int suffix = 1;

        int xrayPortIdx=0;
        int xrayCount=0;
        int chainCount=0;
        for (const auto& proxy : profiles) {
            if (proxy->outbound->IsXray()) xrayCount++;
            if (proxy->type == "chain") chainCount++;
        }
        // assume all chains transition twice and allocate port for them.
        auto xrayPorts = MkManyPorts(xrayCount + 2*chainCount);

        for (const auto& item : profiles)
        {
            const auto candidate = classifyTestCandidate(item);
            if (candidate.kind == testCandidate::Skip)
            {
                MW_show_log(candidate.skipReason);
                continue;
            }
            if (candidate.kind == testCandidate::XrayFullConfig)
            {
                if (!IsValid(item)) {
                    MW_show_log("Skipping invalid custom Xray full config: " + item->outbound->name);
                    item->SetLatency(-1);
                    continue;
                }
                // Clear the single xrayConfig slot per full config so they all share one sing-box.
                auto tag = buildOutboundChain(ctx, {
                    .hopIDs = {item->id},
                    .prefix = hopTag(tags::testXrayFullPrefix, item->id),
                });
                if (!ctx.error.isEmpty()) {
                    res->error = ctx.error;
                    return res;
                }
                if (!ctx.result->isXrayNeeded || ctx.result->xrayConfig.isEmpty()) {
                    MW_show_log("Custom Xray full config produced no Xray config: " + item->outbound->name);
                    item->SetLatency(-1);
                    continue;
                }
                res->xrayFullConfigs << QJsonObject2QString(ctx.result->xrayConfig, false);
                ctx.result->xrayConfig = QJsonObject();
                ctx.result->isXrayNeeded = false;
                res->outboundTags << tag;
                res->tag2entID.insert(tag, item->id);
                continue;
            }
            if (!IsValid(item)) {
                MW_show_log("Skipping invalid config: " + item->outbound->name);
                item->SetLatency(-1);
                continue;
            }
            if (item->type == "custom")
            {
                auto custom = item->Custom();
                if (custom == nullptr)
                {
                    MW_show_log("Corrupted data in build test config");
                    res->error = "Corrupted data in build test config";
                    return res;
                }
                if (custom->type == Custom::CustomFullConfig)
                {
                    auto obj = QString2QJsonObject(custom->config);
                    obj["inbounds"] = QJsonArray();
                    res->fullConfigs[item->id] = QJsonObject2QString(obj, true);
                    continue;
                }
            }
            auto IDs = unwrapChain(item->id);
            auto group = dataManager->groupsRepo->GetGroup(item->gid);
            if (group == nullptr) {
                res->error = "Null group on profile, data is corrupted";
                return res;
            }
            if (group->landing_proxy_id >= 0) IDs.prepend(group->landing_proxy_id);
            if (group->front_proxy_id >= 0) IDs.append(group->front_proxy_id);
            int singToXrayPort = -1;
            int xrayToSingPort = -1;
            if (item->outbound->IsXray()) singToXrayPort = xrayPorts[xrayPortIdx++];
            if (item->type == "chain") {
                singToXrayPort = xrayPorts[xrayPortIdx++];
                xrayToSingPort = xrayPorts[xrayPortIdx++];
            }
            auto tag = buildOutboundChain(ctx, {
                .hopIDs = IDs,
                .prefix = hopTag(tags::testChainPrefix, suffix),
                .singToXrayPort = singToXrayPort,
                .xrayToSingPort = xrayToSingPort,
            });
            if (!ctx.error.isEmpty()) {
                res->error = ctx.error;
                return res;
            }
            res->outboundTags << tag;
            res->tag2entID.insert(tag, item->id);
            suffix++;
        }
        buildXrayConfig(ctx);
        if (!ctx.error.isEmpty()) {
            res->error = ctx.error;
            return res;
        }
        ctx.outbounds << QJsonObject{{"type", "direct"}, {"tag", tags::direct}};
        ctx.result->coreConfig["outbounds"] = ctx.outbounds;
        ctx.result->coreConfig["endpoints"] = ctx.endpoints;
        QJsonArray inboundArr;
        for (const auto &bridgeConf : ctx.xrayToSingBridges) {
            inboundArr.append(socksBridgeInbound(
                QString(tags::bridgePrefix) + "-" + Int2String(bridgeConf.port), bridgeConf));
        }
        QJsonArray routeRules;
        // A running Tun owns the OS resolver, so the sidecar resolves against the probe box's dns-direct instead.
        if (ctx.result->isXrayNeeded || !res->xrayFullConfigs.isEmpty()) {
            res->xrayDnsStrategy = getXrayOutboundDomainStrategy();
        }
        QJsonObject routeObj{
                {"auto_detect_interface", true},
                {"default_domain_resolver", QJsonObject{
                        {"server", tags::dnsDirect},
                        {"strategy", getDirectDomainStrategy()},
                   }}
        };
        if (!routeRules.isEmpty()) routeObj["rules"] = routeRules;
        ctx.result->coreConfig["route"] = routeObj;
        ctx.result->coreConfig["inbounds"] = inboundArr;
        res->coreConfig = ctx.result->coreConfig;
        res->xrayConfig = ctx.result->xrayConfig;
        res->isXrayNeeded = ctx.result->isXrayNeeded;

        return res;
    }
}
