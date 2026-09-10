#include "include/ui/mainwindow.h"

#include "include/ui/mainWindow/TestRunner.h"

#include "include/configs/AutoSelectorPlan.h"
#include "include/database/ProfilesRepo.h"

#include <QSemaphore>

#include <memory>

// Blocks; call off the UI thread.
void MainWindow::rank_auto_selector(const std::shared_ptr<Configs::Profile>& ent, const QList<int>& stale) {
    if (ent == nullptr || ent->type != "autoselector") return;

    const auto needed = Configs::AutoSelectorUnmeasuredCandidates(ent, stale);
    if (needed.isEmpty()) {
        const auto ranked = Configs::RerankAutoSelectorPool(ent);
        MW_show_log(tr("[Auto selector] Reusing existing test results; ranked %1 profiles.").arg(ranked.size()));
        return;
    }

    MW_show_log(tr("[Auto selector] Measuring %1 not-yet-tested profiles...").arg(needed.size()));
    // Wait on the sweep's completion signal, not the session lock: this thread already holds it.
    QSemaphore sweepDone;
    testRunner->runUrlTests(needed, [&sweepDone] { sweepDone.release(); });
    sweepDone.acquire();

    const auto ranked = Configs::RerankAutoSelectorPool(ent);
    MW_show_log(tr("[Auto selector] Ranked %1 profiles.").arg(ranked.size()));
}

void MainWindow::on_subscription_group_changed(int gid, const QList<int>& disturbed) {
    if (gid < 0) return;
    const QSet<int> disturbedSet(disturbed.begin(), disturbed.end());
    int restartID = -1;

    for (int id : Configs::dataManager->profilesRepo->GetProfileIdsByType("autoselector")) {
        auto ent = Configs::dataManager->profilesRepo->GetProfile(id);
        if (ent == nullptr) continue;
        auto selector = ent->AutoSelector();
        if (selector == nullptr || selector->gid != gid) continue;

        const auto gone = [](int memberID) {
            return Configs::dataManager->profilesRepo->GetProfile(memberID) == nullptr;
        };
        const auto prunedPool = selector->pool.removeIf(gone);
        const auto prunedBuilt = selector->lastBuilt.removeIf(gone);
        if (prunedPool > 0 || prunedBuilt > 0) Configs::dataManager->profilesRepo->Save(ent);

        if (running == nullptr || running->id != ent->id) continue;
        // A replaced member keeps its id, so only the disturbed set spots it.
        bool rebuild = prunedBuilt > 0;
        for (int memberID : selector->lastBuilt) {
            if (!disturbedSet.contains(memberID)) continue;
            rebuild = true;
            break;
        }
        if (rebuild) restartID = ent->id;
    }

    if (restartID < 0) return;
    MW_show_log(tr("[Auto selector] The subscription replaced profiles it was running on — rebuilding."));
    profile_start(restartID);
}

void MainWindow::on_auto_selector_exhausted(int profileID) {
    auto ent = Configs::dataManager->profilesRepo->GetProfile(profileID);
    if (ent == nullptr || running == nullptr || running->id != profileID) return;

    MW_show_log(tr("[Auto selector] Every running profile stopped working — rebuilding from the "
                   "next best candidates."));
    runOnNewThread([=, this] {
        QList<int> stale;
        if (auto selector = ent->AutoSelector(); selector != nullptr) stale = selector->lastBuilt;
        rank_auto_selector(ent, stale);
        runOnUiThread([=, this] {
            if (running == nullptr || running->id != profileID) return;
            profile_start(profileID);
        });
    });
}
