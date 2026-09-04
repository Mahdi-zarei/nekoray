#pragma once

#include "include/database/entities/RouteRule.h"
#include <QUrl>
#include <QJsonArray>
#include <QStringList>

namespace Configs {
    const int INVALID_ID = -99999;

    enum simpleAction{bypass, block, proxy, warpBypass};
    inline QString simpleActionToString(simpleAction action)
    {
        if (action == bypass) return {"direct"};
        if (action == block) return {"block"};
        if (action == proxy) return {"proxy"};
        if (action == warpBypass) return {"warp-bypass"};
        return {"invalid"};
    }

    class RouteProfile {
    public:
        int id = -1;
        QString name = "";
        QList<std::shared_ptr<RouteRule>> Rules;
        int defaultOutboundID = proxyID;

        // rawRoute is a whole sing-box `route` JSON object; preventModifications uses it verbatim, otherwise Throne still injects its plumbing.
        bool isRaw = false;
        QString rawRoute = "";
        bool preventModifications = false;

        // Remote profiles stay structured and user-editable; an update re-fetches remoteURL and overwrites Rules, keeping the local name.
        bool isRemote = false;
        QString remoteURL = "";
        bool autoUpdate = false;
        qint64 remoteLastUpdate = 0; // epoch seconds

        // Profile ids of openvpn/openconnect profiles run alongside this routing profile.
        QList<int> endpointProfileIDs;

        RouteProfile() = default;

        RouteProfile(const RouteProfile& other);

        static QList<std::shared_ptr<RouteRule>> parseJsonArray(const QJsonArray& arr, QString* parseError, QString* warnings = nullptr);

        QJsonArray get_route_rules(bool forView = false, std::map<int, QString> outboundMap = {});

        // Endpoints travel as whole configs with credentials cleared; one that cannot is skipped into *warnings.
        QJsonObject ToShareObject(QString* warnings = nullptr);
        // ToShareObject() compacted, base64url-encoded, wrapped as throne://route/<...>
        QString ToShareLink(QString* warnings = nullptr);
        // *wasOldArray = legacy bare rule array (no name / default outbound); materializeEndpoints=false matches shared endpoints without creating local ones.
        static std::shared_ptr<RouteProfile> FromShareInput(const QString& input, QString* fatalError, QString* warnings, bool* wasOldArray, bool materializeEndpoints = true);

        // *wasRemoteRouteLink is true even when the payload is invalid; a non-remoteRoute input returns {} with it false so callers can fall through.
        static QList<std::shared_ptr<RouteProfile>> FromRemoteRoutesLink(const QString& input, bool* wasRemoteRouteLink, QString* error);

        static QList<int> CollectRawOutboundIds(const QJsonObject& route);
        static QJsonObject TranslateRawOutbounds(const QJsonObject& route, const std::map<int, QString>& outboundMap);

        static std::shared_ptr<RouteProfile> GetDefaultChain();

        // The positional placeholder paired with an endpoint, correlated by type + outboundID.
        static std::shared_ptr<RouteRule> MakeEndpointRule(int endpointProfileID);

        // One endpointPreferredBy rule per listed endpoint; prunes orphans, appends missing. Raw: no-op.
        void SyncEndpointRules();

        std::shared_ptr<QList<int>> get_used_outbounds();

        std::shared_ptr<QStringList> get_used_rule_sets();

        QStringList get_direct_sites();

        QStringList get_proxy_sites();

        QStringList get_direct_ips();

        // CIDRs pulled away from a direct exit, so Tun knows which ranges it must carry; rule-sets aren't resolvable at build time and are left out.
        QStringList get_hijacked_ips();

        bool IsEmpty();

        void ResetRules();

        void ResetSimpleRule(ruleType type);

        QString GetSimpleRules(simpleAction action);

        QString UpdateSimpleRules(const QString& content, simpleAction action);

        bool AppendSimpleRule(const QString& rawRule, simpleAction action);

        void FilterEmptyRules();
    private:
        static bool add_simple_rule(const QString& content, const std::shared_ptr<RouteRule>& rule, ruleType type);

        static bool add_simple_address_rule(const QString& content, const std::shared_ptr<RouteRule>& rule);

        static bool add_simple_process_rule(const QString& content, const std::shared_ptr<RouteRule>& rule);

        std::shared_ptr<RouteRule> get_simple_rule_by_type(ruleType type);

        static ruleType get_rule_type(const QString& content, simpleAction action);

        static QList<std::shared_ptr<RouteRule>> get_simple_rules();

        static void reset_simple_rule(std::shared_ptr<RouteRule>& rule);
    };
} // namespace Configs
