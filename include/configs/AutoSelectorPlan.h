#pragma once

#include <QList>
#include <QString>
#include <memory>

namespace Configs
{
    class Profile;
    class autoSelector;

    enum class AutoSelectorSkip
    {
        None = 0,
        Missing,
        MetaType,
        CoreTransitions,
        ExtraCore,
        FullConfig,
        Malformed,
        Tailscale,
        ManagementEndpoint,
        NameFilter,
        CountryFilter,
        Unavailable,
        XrayFullChained,
    };

    QString AutoSelectorSkipReason(AutoSelectorSkip skip);

    struct AutoSelectorPlan
    {
        // Eligible members, best first, capped at poolCap.
        QList<int> pool;
        // The prefix of pool that actually enters the config.
        QList<int> build;

        int membersInGroup = 0;
        int eligible = 0;
        int rankedByTest = 0;
        // Failed-last-test members kept anyway, because excluding them would have emptied the pool.
        int keptUnavailable = 0;
        QList<QPair<AutoSelectorSkip, int>> skipped;
        bool truncated = false;
        int poolCapUsed = 0;
        int buildLimitUsed = 0;
        // The caller should run a client-side URL test before building.
        bool needsRanking = false;

        QString error;

        [[nodiscard]] int skippedCount() const
        {
            int total = 0;
            for (const auto &entry : skipped) total += entry.second;
            return total;
        }
    };

    AutoSelectorPlan PlanAutoSelector(const std::shared_ptr<Profile> &ent);

    QList<int> AutoSelectorRankingCandidates(const std::shared_ptr<Profile> &ent);

    // `stale` = members whose stored result no longer reflects reality; they are re-measured.
    QList<int> AutoSelectorUnmeasuredCandidates(const std::shared_ptr<Profile> &ent,
                                                const QList<int> &stale = {});

    QList<int> RerankAutoSelectorPool(const std::shared_ptr<Profile> &ent);
}
