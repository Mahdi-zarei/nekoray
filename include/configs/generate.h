#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include "include/database/entities/Profile.h"

namespace Configs
{
    // Not "dashboard": that one is the Clash external_ui dir, holding a different UI.
    inline constexpr auto apiDashboardDir = "sb-dashboard";

    class ExtraCoreData
    {
        public:
        QString path;
        QString args;
        QString config;
        bool noLog = false;
    };

    struct TrafficChainGroup {
        QString watchTag;
        QList<std::shared_ptr<Profile>> profiles;
    };

    struct AutoSelectorBuildInfo {
        QString groupTag;
        std::shared_ptr<Profile> profile;
        QList<QPair<QString, std::shared_ptr<Profile>>> members;
    };

    class BuildConfigResult {
    public:
        QString error;
        QJsonObject coreConfig;
        QString tunIPv4CIDR;
        bool isXrayNeeded = false;
        QJsonObject xrayConfig;
        // Opaque full configs, one instance each; never merged into xrayConfig.
        QStringList xrayFullConfigs;
        std::shared_ptr<ExtraCoreData> extraCoreData = std::make_shared<ExtraCoreData>();

        QList<TrafficChainGroup> chainGroups;
        QList<AutoSelectorBuildInfo> autoSelectors;
        // Endpoint hop tag -> profile id, so a live status can be named after its profile.
        QMap<QString, int> vpnEndpointProfiles;
        // Every profile the config was built from, chain hops and route members included.
        QSet<int> involvedProfiles;
    };

    class BuildTestConfigResult {
    public:
        QString error;
        QMap<int, QString> fullConfigs;
        QStringList xrayFullConfigs;
        QMap<QString, int> tag2entID;
        QJsonObject coreConfig;
        QJsonObject xrayConfig;
        bool isXrayNeeded = false;
        QStringList outboundTags;
        QString xrayDnsStrategy;
    };

    inline QString get_jsdelivr_link(QString link)
    {
        if(Configs::dataManager->settingsRepo->ruleset_mirror == Mirrors::GITHUB)
            return link;
        if(auto url = QUrl(link); url.isValid() && url.host() == "raw.githubusercontent.com")
        {
            QStringList list = url.path().split('/');
            QString result;
            switch(Configs::dataManager->settingsRepo->ruleset_mirror) {
            case Mirrors::GCORE: result = "https://gcore.jsdelivr.net/gh"; break;
            case Mirrors::QUANTIL: result = "https://quantil.jsdelivr.net/gh"; break;
            case Mirrors::FASTLY: result = "https://fastly.jsdelivr.net/gh"; break;
            case Mirrors::CDN: result = "https://cdn.jsdelivr.net/gh"; break;
            default: result = "https://testingcf.jsdelivr.net/gh";
            }

            int index = 0;
            foreach(QString item, list)
            {
                if(!item.isEmpty())
                {
                    if(index == 2)
                        result += "@" + item;
                    else
                        result += "/" + item;
                    index++;
                }
            }
            return result;
        }
        return link;
    }

    constexpr int warpProfileID = -2408;

    struct PredefinedDNSEntry {
        QString domain;
        QStringList v4;
        QStringList v6;
    };

    // Hosts-file syntax: "<address> <domain> [domain...]", '#' comments, repeated domains accumulate.
    bool ParsePredefinedDNS(const QStringList &lines, QList<PredefinedDNSEntry> &out, QString *error = nullptr);

    // sing-box duration grammar: one or more "<number><unit>" with unit ns/us/ms/s/m/h/d.
    bool IsValidDuration(const QString &text);

    std::shared_ptr<BuildConfigResult> BuildSingBoxConfig(const std::shared_ptr<Profile> &ent);

    bool IsValid(const std::shared_ptr<Profile> &ent);

    // Eligible: an openvpn/openconnect profile, or a chain whose exit hop is one, never the reverse.
    bool CanBeAuxEndpoint(const std::shared_ptr<Profile> &ent);

    // Hops behind the exit of a chain endpoint that are endpoints themselves, exit-first.
    QList<int> AuxEndpointInnerHops(int endpointProfileID);

    std::shared_ptr<BuildTestConfigResult> BuildTestConfig(const QList<std::shared_ptr<Profile> > &profiles);
}
