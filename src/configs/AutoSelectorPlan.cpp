#include "include/configs/AutoSelectorPlan.h"

#include "include/configs/outbounds/autoselector.h"
#include "include/configs/outbounds/custom.h"
#include "include/database/DatabaseManager.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/entities/Group.h"
#include "include/database/entities/Profile.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace Configs
{
    namespace
    {
        struct MemberFilters
        {
            QRegularExpression name;
            bool hasName = false;
            QSet<QString> countries;
        };

        MemberFilters buildFilters(const autoSelector *selector)
        {
            MemberFilters filters;
            if (!selector->nameFilter.trimmed().isEmpty()) {
                filters.name = QRegularExpression(selector->nameFilter,
                                                  QRegularExpression::CaseInsensitiveOption);
                filters.hasName = filters.name.isValid();
            }
            for (const auto &code : selector->countryFilter.split(',', Qt::SkipEmptyParts)) {
                filters.countries.insert(code.trimmed().toUpper());
            }
            return filters;
        }

        bool hasFreshResult(const std::shared_ptr<Profile> &member, const autoSelector *selector, qint64 now)
        {
            if (member == nullptr || member->latency == 0) return false;
            if (selector->resultValidityMins <= 0) return false;
            // Rows written before the timestamp column existed carry 0: expired, not fresh.
            if (member->latency_at <= 0) return false;
            return (now - member->latency_at) <= static_cast<qint64>(selector->resultValidityMins) * 60;
        }

        AutoSelectorSkip eligibilityOf(const std::shared_ptr<Profile> &member,
                                       const autoSelector *selector,
                                       const MemberFilters &filters,
                                       qint64 now)
        {
            if (member == nullptr || member->outbound == nullptr) return AutoSelectorSkip::Missing;
            if (member->type == "chain" || member->type == "autoselector") return AutoSelectorSkip::MetaType;
            if (member->type == "tailscale") return AutoSelectorSkip::Tailscale;
            if (member->type == "openvpn" || member->type == "openconnect")
                return AutoSelectorSkip::ManagementEndpoint;
            if (member->outbound->IsExtraCore()) return AutoSelectorSkip::ExtraCore;
            if (member->type == "custom") {
                const auto custom = member->Custom();
                if (custom == nullptr) return AutoSelectorSkip::Missing;
                // Only a sing-box full config is excluded; an Xray one gets its own instance.
                if (custom->type != Custom::CustomOutbound && custom->type != Custom::CustomXrayOutbound
                    && custom->type != Custom::CustomXrayFullConfig) {
                    return AutoSelectorSkip::FullConfig;
                }
                // A member whose JSON does not parse builds into an empty outbound that fails the shared build.
                if (QString2QJsonObject(custom->config).isEmpty()) return AutoSelectorSkip::Malformed;
            }
            if (filters.hasName && !filters.name.match(member->outbound->DisplayName()).hasMatch()) {
                return AutoSelectorSkip::NameFilter;
            }
            if (!filters.countries.isEmpty() && !filters.countries.contains(member->test_country.toUpper())) {
                return AutoSelectorSkip::CountryFilter;
            }
            // Only a fresh failure excludes a member; an old one would exile it permanently.
            if (selector->excludeUnavailable && member->latency < 0 && hasFreshResult(member, selector, now))
                return AutoSelectorSkip::Unavailable;
            return AutoSelectorSkip::None;
        }

        // Mirrors entIDListtoEntList's budget: a chain may hand off sing-box <-> Xray at most once.
        int coreTransitions(const std::shared_ptr<Profile> &landing,
                            const std::shared_ptr<Profile> &member,
                            const std::shared_ptr<Profile> &front)
        {
            int transitions = 0;
            bool inXray = false;
            for (const auto &hop : {landing, member, front}) {
                if (hop == nullptr || hop->outbound == nullptr) continue;
                const bool xray = hop->outbound->IsXray();
                if (xray != inXray) transitions++;
                inXray = xray;
            }
            return transitions;
        }

        // chainScanError's rules per member: hitting them in buildOutboundChain fails the whole build.
        bool xrayFullConfigFitsChain(const std::shared_ptr<Profile> &landing,
                                     const std::shared_ptr<Profile> &front)
        {
            if (front != nullptr) return false;
            if (landing == nullptr || landing->outbound == nullptr) return true;
            return !landing->outbound->IsXray() && !landing->outbound->IsXrayFullConfig()
                   && !landing->outbound->IsExtraCore();
        }

        // Untested (1) outranks failed (2) so a fresh subscription still gets explored.
        int latencyRank(int latency)
        {
            if (latency > 0) return 0;
            if (latency == 0) return 1;
            return 2;
        }

        int effectiveLatency(const std::shared_ptr<Profile> &member, const autoSelector *selector, qint64 now)
        {
            return hasFreshResult(member, selector, now) ? member->latency : 0;
        }

        int effectiveLatencyOf(int id, const autoSelector *selector, qint64 now)
        {
            return effectiveLatency(dataManager->profilesRepo->GetProfile(id), selector, now);
        }

        bool byLatency(int left, int right, const autoSelector *selector, qint64 now)
        {
            const int leftLatency = effectiveLatencyOf(left, selector, now);
            const int rightLatency = effectiveLatencyOf(right, selector, now);
            const int leftRank = latencyRank(leftLatency);
            const int rightRank = latencyRank(rightLatency);
            if (leftRank != rightRank) return leftRank < rightRank;
            if (leftRank == 0) return leftLatency < rightLatency;
            return false;
        }

        QList<int> eligibleMembers(const std::shared_ptr<Profile> &ent,
                                   const autoSelector *selector,
                                   AutoSelectorPlan *plan)
        {
            QList<int> members;
            auto group = dataManager->groupsRepo->GetGroup(selector->gid);
            if (group == nullptr) {
                if (plan != nullptr) plan->error = "Auto selector points at a group that no longer exists";
                return members;
            }
            const auto filters = buildFilters(selector);
            const auto now = QDateTime::currentSecsSinceEpoch();
            const auto landing = group->landing_proxy_id >= 0
                                     ? dataManager->profilesRepo->GetProfile(group->landing_proxy_id)
                                     : nullptr;
            const auto front = group->front_proxy_id >= 0
                                   ? dataManager->profilesRepo->GetProfile(group->front_proxy_id)
                                   : nullptr;
            QMap<AutoSelectorSkip, int> skips;
            QList<int> unavailable;
            for (int id : group->Profiles()) {
                if (id == ent->id) continue;
                auto member = dataManager->profilesRepo->GetProfile(id);
                if (plan != nullptr) plan->membersInGroup++;
                const auto skip = eligibilityOf(member, selector, filters, now);
                if (skip != AutoSelectorSkip::None && skip != AutoSelectorSkip::Unavailable) {
                    skips[skip]++;
                    continue;
                }
                if (member->outbound->IsXrayFullConfig()) {
                    if (!xrayFullConfigFitsChain(landing, front)) {
                        skips[AutoSelectorSkip::XrayFullChained]++;
                        continue;
                    }
                } else if (coreTransitions(landing, member, front) > 2) {
                    skips[AutoSelectorSkip::CoreTransitions]++;
                    continue;
                }
                if (skip == AutoSelectorSkip::Unavailable) {
                    unavailable << id;
                    continue;
                }
                members << id;
                if (plan != nullptr && effectiveLatency(member, selector, now) > 0) plan->rankedByTest++;
            }
            // Every member's last test failing is an outage, not a dead subscription: excluding
            // them all would leave nothing that could discover the servers came back.
            if (members.isEmpty() && !unavailable.isEmpty()) {
                members = unavailable;
                if (plan != nullptr) plan->keptUnavailable = static_cast<int>(unavailable.size());
            } else if (!unavailable.isEmpty()) {
                skips[AutoSelectorSkip::Unavailable] += static_cast<int>(unavailable.size());
            }
            if (plan != nullptr) {
                for (auto it = skips.constBegin(); it != skips.constEnd(); ++it) {
                    plan->skipped << qMakePair(it.key(), it.value());
                }
            }
            return members;
        }

        // The persisted order is the core's ranking prior: keep it, append newcomers by latency.
        QList<int> orderMembers(const QList<int> &members, const QList<int> &persistedPool,
                                const autoSelector *selector, qint64 now)
        {
            QSet<int> eligible(members.begin(), members.end());
            QList<int> ordered;
            ordered.reserve(members.size());
            QSet<int> placed;
            for (int id : persistedPool) {
                if (!eligible.contains(id) || placed.contains(id)) continue;
                ordered << id;
                placed.insert(id);
            }

            QList<int> newcomers;
            for (int id : members) {
                if (!placed.contains(id)) newcomers << id;
            }
            std::stable_sort(newcomers.begin(), newcomers.end(),
                             [selector, now](int left, int right) { return byLatency(left, right, selector, now); });
            ordered << newcomers;
            return ordered;
        }
    } // namespace

    QString AutoSelectorSkipReason(AutoSelectorSkip skip)
    {
        switch (skip) {
            case AutoSelectorSkip::Missing: return QObject::tr("missing profile");
            case AutoSelectorSkip::MetaType: return QObject::tr("chain or auto selector");
            case AutoSelectorSkip::CoreTransitions: return QObject::tr("needs too many core switches");
            case AutoSelectorSkip::ExtraCore: return QObject::tr("extra-core profile");
            case AutoSelectorSkip::FullConfig: return QObject::tr("full config profile");
            case AutoSelectorSkip::Malformed: return QObject::tr("config does not parse");
            case AutoSelectorSkip::Tailscale: return QObject::tr("Tailscale profile");
            case AutoSelectorSkip::ManagementEndpoint: return QObject::tr("OpenVPN or OpenConnect profile");
            case AutoSelectorSkip::NameFilter: return QObject::tr("filtered out by name");
            case AutoSelectorSkip::CountryFilter: return QObject::tr("filtered out by country");
            case AutoSelectorSkip::Unavailable: return QObject::tr("last test failed");
            case AutoSelectorSkip::XrayFullChained:
                return QObject::tr("Xray full config cannot be combined with the group's proxies");
            default: return {};
        }
    }

    AutoSelectorPlan PlanAutoSelector(const std::shared_ptr<Profile> &ent)
    {
        AutoSelectorPlan plan;
        if (ent == nullptr) {
            plan.error = "Null profile passed to PlanAutoSelector";
            return plan;
        }
        auto selector = ent->AutoSelector();
        if (selector == nullptr) {
            plan.error = "Profile is not an auto selector, data is corrupted";
            return plan;
        }
        selector->Normalize();
        plan.poolCapUsed = selector->poolCap;
        plan.buildLimitUsed = selector->buildLimit;

        const auto members = eligibleMembers(ent, selector, &plan);
        if (!plan.error.isEmpty()) return plan;
        plan.eligible = members.size();
        if (members.isEmpty()) {
            plan.error = "Auto selector has no usable members";
            return plan;
        }

        const auto now = QDateTime::currentSecsSinceEpoch();
        auto ordered = orderMembers(members, selector->pool, selector, now);
        plan.truncated = ordered.size() > selector->poolCap;
        if (plan.truncated) ordered = ordered.mid(0, selector->poolCap);
        plan.pool = ordered;
        plan.build = ordered.mid(0, std::min<qsizetype>(ordered.size(), selector->buildLimit));

        if (ordered.size() > selector->buildLimit) {
            int unranked = 0;
            for (int id : plan.build) {
                if (effectiveLatencyOf(id, selector, now) == 0) unranked++;
            }
            plan.needsRanking = unranked > 0;
        }
        return plan;
    }

    QList<int> AutoSelectorRankingCandidates(const std::shared_ptr<Profile> &ent)
    {
        if (ent == nullptr) return {};
        auto selector = ent->AutoSelector();
        if (selector == nullptr) return {};
        selector->Normalize();
        return eligibleMembers(ent, selector, nullptr);
    }

    QList<int> AutoSelectorUnmeasuredCandidates(const std::shared_ptr<Profile> &ent, const QList<int> &stale)
    {
        if (ent == nullptr) return {};
        auto selector = ent->AutoSelector();
        if (selector == nullptr) return {};

        const auto candidates = AutoSelectorRankingCandidates(ent);
        const QSet<int> staleSet(stale.begin(), stale.end());
        const auto now = QDateTime::currentSecsSinceEpoch();
        QList<int> needed;
        for (int id : candidates) {
            if (staleSet.contains(id)) {
                needed << id;
                continue;
            }
            if (effectiveLatencyOf(id, selector, now) == 0) needed << id;
        }
        return needed;
    }

    QList<int> RerankAutoSelectorPool(const std::shared_ptr<Profile> &ent)
    {
        if (ent == nullptr) return {};
        auto selector = ent->AutoSelector();
        if (selector == nullptr) return {};

        auto members = eligibleMembers(ent, selector, nullptr);
        const auto now = QDateTime::currentSecsSinceEpoch();
        std::stable_sort(members.begin(), members.end(),
                         [selector, now](int left, int right) { return byLatency(left, right, selector, now); });
        if (members.size() > selector->poolCap) members = members.mid(0, selector->poolCap);

        selector->pool = members;
        selector->poolRankedAt = QDateTime::currentSecsSinceEpoch();
        dataManager->profilesRepo->Save(ent);
        return members;
    }
}
