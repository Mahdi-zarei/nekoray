#include "include/ui/mainWindow/TestRunner.h"

#include "include/ui/mainwindow.h"

#include "include/api/RPC.h"
#include "include/configs/generate.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/stats/traffic/TrafficStatsManager.hpp"

#include <QSemaphore>
#include <QThread>
#include <QThreadPool>

#include <utility>

using namespace API;

namespace {
    constexpr int kTestBatchSize = 100;
    constexpr int kLatencyPollIntervalMs = 200;
    constexpr int kSpeedPollIntervalMs = 100;

    QList<int> withoutAutoSelectors(const QList<int>& profileIDs) {
        const auto selectors = Configs::dataManager->profilesRepo->GetProfileIdsByType("autoselector");
        if (selectors.isEmpty()) return profileIDs;
        const QSet<int> skip(selectors.begin(), selectors.end());
        QList<int> filtered;
        filtered.reserve(profileIDs.size());
        for (int id : profileIDs) {
            if (!skip.contains(id)) filtered << id;
        }
        return filtered;
    }

    bool isTestAborted(const QString& error) {
        return error.contains("test aborted") || error.contains("context canceled");
    }

    bool isVpnProfile(const std::shared_ptr<Configs::Profile>& ent) {
        return ent != nullptr && (ent->type == "openvpn" || ent->type == "openconnect");
    }

    constexpr int kVpnStatusWaitMs = 10000;

    int resolveEntID(const QMap<QString, int>& tag2entID, const std::string& tag, int fallback) {
        if (tag2entID.isEmpty()) return fallback;
        return tag2entID.value(QString::fromStdString(tag), -1);
    }

    // Target is deduced, not named: access control applies to naming a private type.
    template <typename Req, typename Target>
    void fillCommonTestReq(Req& req, const Target& target) {
        for (const auto& tag : target.outboundTags) req.outbound_tags.push_back(tag.toStdString());
        req.config = target.coreConfig.toStdString();
        req.use_default_outbound = target.useDefaultOutbound;
        req.xray_config = target.xrayConfig.toStdString();
        req.need_xray = !target.xrayConfig.isEmpty();
        req.xray_outbound_dns_strategy = target.xrayDnsStrategy.toStdString();
        for (const auto& xc : target.xrayFullConfigs) req.xray_full_configs.push_back(xc.toStdString());
    }

    // Stopping does not join: the poll may itself sit in a 30s RPC and must not stall the batch.
    class ResultPoller {
    public:
        ResultPoller(std::function<void()> tick, int intervalMs)
            : stop_(std::make_shared<std::atomic<bool>>(false)) {
            runOnNewThread([stop = stop_, tick = std::move(tick), intervalMs] {
                while (!stop->load()) {
                    QThread::msleep(intervalMs);
                    if (stop->load()) break;
                    tick();
                }
            });
        }

        ~ResultPoller() { stop_->store(true); }

        ResultPoller(const ResultPoller&) = delete;
        ResultPoller& operator=(const ResultPoller&) = delete;

    private:
        std::shared_ptr<std::atomic<bool>> stop_;
    };
}

bool TestRunner::isRunning() {
    if (!session_.tryLock()) return true;
    session_.unlock();
    return false;
}

void TestRunner::stop() {
    stopRequested_.store(true);
    bool ok;
    defaultClient->StopTests(&ok);

    if (!ok) {
        MW_show_log(MainWindow::tr("Failed to stop tests"));
    }
}

QString TestRunner::contextName(int entID) const {
    if (entID != -1) {
        if (auto e = Configs::dataManager->profilesRepo->GetProfile(entID)) return e->outbound->DisplayTypeAndName();
    }
    return MainWindow::tr("a tested profile");
}

void TestRunner::applyUrlResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::URLTestResp& res,
                                const QHash<QString, bool>* vpnConnected) {
    const auto error = QString::fromStdString(res.error.value());
    if (error.isEmpty()) {
        ent->SetLatency(res.latency_ms.value());
    } else if (isTestAborted(error)) {
        ent->SetLatency(0);
    } else if (vpnConnected != nullptr && isVpnProfile(ent)
               && vpnConnected->value(QString::fromStdString(res.outbound_tag.value()), false)) {
        ent->SetLatency(Configs::kLatencyConnectOnly);
    } else {
        ent->SetLatency(-1);
        MW_show_log(MainWindow::tr("[%1] test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
    }
    Configs::dataManager->profilesRepo->Save(ent);
}

void TestRunner::applyIpResult(const std::shared_ptr<Configs::Profile>& ent, const libcore::IPTestRes& res) {
    const auto error = QString::fromStdString(res.error.value());
    if (error.isEmpty()) {
        ent->ip_out = QString::fromStdString(res.ip.value());
        ent->test_country = QString::fromStdString(res.country_code.value());
    } else {
        if (!isTestAborted(error)) {
            MW_show_log(MainWindow::tr("[%1] IP test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
        }
        ent->ip_out.clear();
        ent->test_country.clear();
    }
    Configs::dataManager->profilesRepo->Save(ent);
}

void TestRunner::runUrlProbe(const Target& target) {
    if (stopRequested_.load()) {
        MW_show_log(MainWindow::tr("Profile test aborted"));
        return;
    }

    libcore::TestReq req;
    fillCommonTestReq(req, target);
    req.url = Configs::dataManager->settingsRepo->test_latency_url.toStdString();
    req.max_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.test_timeout_ms = Configs::dataManager->settingsRepo->url_test_timeout_ms;

    // The test box dies with the RPC, so the verdict has to be asked for up front.
    for (auto it = target.tag2entID.cbegin(); it != target.tag2entID.cend(); ++it) {
        if (isVpnProfile(Configs::dataManager->profilesRepo->GetProfile(it.value()))) {
            req.vpn_endpoint_tags.push_back(it.key().toStdString());
        }
    }
    if (!req.vpn_endpoint_tags.empty()) req.vpn_status_timeout_ms = kVpnStatusWaitMs;

    bool rpcOK = false;
    QString coreError;
    libcore::TestResp result;
    {
        ResultPoller poller([this, gen = sessionGen_.load(), tag2entID = target.tag2entID] {
            if (staleGen(gen)) return;
            bool ok = false;
            const auto resp = defaultClient->QueryURLTest(&ok);
            // Checked again: this poll can sit in the RPC while its batch ends and the tags are reused.
            if (staleGen(gen)) return;
            if (!ok || resp.results.empty()) return;

            QList<int> updated;
            for (const auto& res : resp.results) {
                mw_->dataViewHtmlGenerator_.addTestProgress();
                mw_->UpdateDataView();
                const int entid = resolveEntID(tag2entID, res.outbound_tag.value(), -1);
                if (entid == -1) continue;
                auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
                if (ent == nullptr) continue;
                applyUrlResult(ent, res);
                updated << entid;
            }
            if (updated.isEmpty()) return;
            mw_->UpdateDataView(true);
            runOnUiThread([=, this] { mw_->refresh_proxy_list(updated); });
        }, kLatencyPollIntervalMs);

        result = defaultClient->Test(&rpcOK, req, &coreError);
    }

    if (!rpcOK || result.results.empty()) {
        if (!rpcOK) mw_->handleXrayGeoAssetError(coreError, contextName(target.entID));
        return;
    }

    QHash<QString, bool> vpnConnected;
    for (const auto& st : result.vpn_status) {
        vpnConnected.insert(QString::fromStdString(st.tag.value()), st.connected.value());
    }

    for (const auto& res : result.results) {
        const int entid = resolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) {
            MW_show_log(MainWindow::tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }
        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) {
            MW_show_log(MainWindow::tr("Profile manager data is corrupted, try again."));
            continue;
        }
        applyUrlResult(ent, res, &vpnConnected);
    }
}

void TestRunner::runIpProbe(const Target& target) {
    if (stopRequested_.load()) {
        MW_show_log(MainWindow::tr("Profile test aborted"));
        return;
    }

    libcore::IPTestRequest req;
    fillCommonTestReq(req, target);
    req.max_concurrency = Configs::dataManager->settingsRepo->test_concurrent;
    req.test_timeout_ms = Configs::dataManager->settingsRepo->url_test_timeout_ms;

    bool rpcOK = false;
    QString coreError;
    libcore::IPTestResp result;
    {
        ResultPoller poller([this, gen = sessionGen_.load(), tag2entID = target.tag2entID] {
            if (staleGen(gen)) return;
            bool ok = false;
            const auto resp = defaultClient->QueryIPTest(&ok);
            if (staleGen(gen)) return;
            if (!ok || resp.results.empty()) return;

            QList<int> updated;
            for (const auto& res : resp.results) {
                mw_->dataViewHtmlGenerator_.addTestProgress();
                mw_->UpdateDataView();
                const int entid = resolveEntID(tag2entID, res.outbound_tag.value(), -1);
                if (entid == -1) continue;
                auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
                if (ent == nullptr) continue;
                applyIpResult(ent, res);
                updated << entid;
            }
            if (updated.isEmpty()) return;
            mw_->UpdateDataView(true);
            runOnUiThread([=, this] { mw_->refresh_proxy_list(updated); });
        }, kLatencyPollIntervalMs);

        result = defaultClient->IPTest(&rpcOK, req, &coreError);
    }

    if (!rpcOK || result.results.empty()) {
        if (!rpcOK) mw_->handleXrayGeoAssetError(coreError, contextName(target.entID));
        return;
    }

    for (const auto& res : result.results) {
        const int entid = resolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) {
            MW_show_log(MainWindow::tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }
        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) {
            MW_show_log(MainWindow::tr("Profile manager data is corrupted, try again."));
            continue;
        }
        applyIpResult(ent, res);
    }
}

void TestRunner::runUrlTests(const QList<int>& profileIDs, const std::function<void()>& onFinished) {
    runLatencyGroup(LatencyKind::Url, profileIDs, onFinished);
}

void TestRunner::runIpTests(const QList<int>& profileIDs) {
    runLatencyGroup(LatencyKind::Ip, profileIDs, {});
}

void TestRunner::runLatencyGroup(LatencyKind kind, const QList<int>& requestedIDs,
                                 const std::function<void()>& onFinished) {
    const bool isUrl = kind == LatencyKind::Url;
    const auto panelKind = isUrl ? DataViewHtmlGenerator::LatencyTestPanelState::Kind::Url
                                 : DataViewHtmlGenerator::LatencyTestPanelState::Kind::Ip;
    // Must fire on every exit path — a caller may be blocked on it.
    const auto finish = [onFinished] { if (onFinished) onFinished(); };

    const auto profileIDs = withoutAutoSelectors(requestedIDs);
    if (profileIDs.isEmpty()) {
        finish();
        return;
    }
    if (!session_.tryLock()) {
        MessageBoxWarning(software_name, isUrl
            ? MainWindow::tr("The last url test did not exit completely, please wait. If it persists, please restart the program.")
            : MainWindow::tr("The last test did not exit completely, please wait. If it persists, please restart the program."));
        finish();
        return;
    }
    sessionGen_.fetch_add(1);

    runOnNewThread([this, profileIDs, panelKind, isUrl, finish]() {
        stopRequested_.store(false);
        mw_->dataViewHtmlGenerator_.seedLatencyTest(panelKind, profileIDs.size());
        mw_->UpdateDataView(true);

        auto runBatch = [this, isUrl](const QList<std::shared_ptr<Configs::Profile>>& profileSlice, const QList<int>& ids) {
            // Per batch, not per probe: a batch's probes drain each other's results, and tags restart each batch.
            sessionGen_.fetch_add(1);
            auto buildObject = Configs::BuildTestConfig(profileSlice);
            if (!buildObject->error.isEmpty()) {
                MW_show_log(MainWindow::tr("Failed to build test config for batch: ") + buildObject->error);
                return;
            }

            const int testCount = buildObject->fullConfigs.size() + (buildObject->outboundTags.empty() ? 0 : 1);
            if (testCount == 0) return;

            QSemaphore batchDone;
            const auto probe = [this, isUrl, &batchDone](const Target& target) {
                mw_->parallelCoreCallPool->start([this, isUrl, target, &batchDone] {
                    const QSemaphoreReleaser releaser(batchDone);
                    if (isUrl) runUrlProbe(target);
                    else runIpProbe(target);
                });
            };

            for (const auto& entID : buildObject->fullConfigs.keys()) {
                Target target;
                target.coreConfig = buildObject->fullConfigs[entID];
                target.useDefaultOutbound = true;
                target.entID = entID;
                probe(target);
            }
            if (!buildObject->outboundTags.empty()) {
                Target target;
                target.coreConfig = QJsonObject2QString(buildObject->coreConfig, false);
                target.xrayConfig = buildObject->isXrayNeeded ? QJsonObject2QString(buildObject->xrayConfig, false) : "";
                target.xrayFullConfigs = buildObject->xrayFullConfigs;
                target.outboundTags = buildObject->outboundTags;
                target.tag2entID = buildObject->tag2entID;
                target.xrayDnsStrategy = buildObject->xrayDnsStrategy;
                probe(target);
            }
            batchDone.acquire(testCount);

            MW_show_log(isUrl ? "URL test for batch done." : "IP test for batch done.");
            runOnUiThread([=, this] {
                mw_->refresh_proxy_list(ids);
            });
        };

        std::shared_ptr<Configs::Group> currentGroup;
        for (int i = 0; i < profileIDs.length(); i += kTestBatchSize) {
            if (stopRequested_.load()) break;
            const auto profileIDsSlice = profileIDs.mid(i, kTestBatchSize);
            auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(profileIDsSlice);
            if (isUrl && !currentGroup && !profiles.isEmpty()) {
                currentGroup = Configs::dataManager->groupsRepo->GetGroup(profiles[0]->gid);
            }
            runBatch(profiles, profileIDsSlice);
        }

        mw_->dataViewHtmlGenerator_.clearTestSections();
        mw_->UpdateDataView(true);
        session_.unlock();
        finish();

        if (currentGroup != nullptr && currentGroup->auto_clear_unavailable) {
            MW_show_log("URL test finished, clearing unavailable profiles...");
            runOnUiThread([=, this] {
               mw_->clearUnavailableProfiles(false, profileIDs);
            });
        }
        MW_show_log(isUrl ? MainWindow::tr("URL test finished!") : MainWindow::tr("IP test finished!"));
    });
}

void TestRunner::runSpeedTests(const QList<int>& requestedIDs, bool testCurrent)
{
    // A live-connection test stays valid for a selector: it measures whichever member carries traffic.
    const auto profileIDs = testCurrent ? requestedIDs : withoutAutoSelectors(requestedIDs);
    if (profileIDs.isEmpty() && !testCurrent) {
        return;
    }
    if (!session_.tryLock()) {
        MessageBoxWarning(software_name, MainWindow::tr("The last test did not finish completely, please wait. If it persists, please restart the program."));
        return;
    }
    sessionGen_.fetch_add(1);

    testingCurrent_.store(testCurrent);

    runOnNewThread([this, profileIDs, testCurrent]() {
        stopRequested_.store(false);
        { QMutexLocker lk(&creditMu_); credited_.clear(); }
        if (!testCurrent)
        {
            mw_->dataViewHtmlGenerator_.seedSpeedTest(profileIDs.size());
            mw_->UpdateDataView(true);
            auto runBatch = [this](const QList<std::shared_ptr<Configs::Profile>>& profileSlice) {
                auto buildObject = Configs::BuildTestConfig(profileSlice);
                if (!buildObject->error.isEmpty()) {
                    MW_show_log(MainWindow::tr("Failed to build batch test config: ") + buildObject->error);
                    return;
                }

                for (auto it = buildObject->fullConfigs.cbegin(); it != buildObject->fullConfigs.cend(); ++it) {
                    Target target;
                    target.coreConfig = it.value();
                    target.useDefaultOutbound = true;
                    target.entID = it.key();
                    runSpeedProbe(target);
                }

                if (!buildObject->outboundTags.empty()) {
                    Target target;
                    target.coreConfig = QJsonObject2QString(buildObject->coreConfig, false);
                    target.xrayConfig = buildObject->isXrayNeeded ? QJsonObject2QString(buildObject->xrayConfig, true) : "";
                    target.xrayFullConfigs = buildObject->xrayFullConfigs;
                    target.outboundTags = buildObject->outboundTags;
                    target.tag2entID = buildObject->tag2entID;
                    target.xrayDnsStrategy = buildObject->xrayDnsStrategy;
                    runSpeedProbe(target);
                }
            };
            const int stepSize = Configs::dataManager->settingsRepo->speed_test_mode == Configs::TestConfig::COUNTRY ? kTestBatchSize : 1;
            for (int i = 0; i < profileIDs.length(); i += stepSize) {
                if (stopRequested_.load()) break;
                const auto profileIDsSlice = profileIDs.mid(i, stepSize);
                auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(profileIDsSlice);
                runBatch(profiles);
            }
        } else
        {
            mw_->dataViewHtmlGenerator_.seedSpeedTest(1);
            Target target;
            target.testCurrent = true;
            runSpeedProbe(target);
            testingCurrent_.store(false);
        }
        mw_->dataViewHtmlGenerator_.clearTestSections();
        mw_->UpdateDataView(true);
        session_.unlock();
        runOnUiThread([=,this]{
            mw_->refresh_proxy_list(profileIDs);
            MW_show_log(MainWindow::tr("Speedtest finished!"));
        });
    });
}

void TestRunner::creditTraffic(const std::shared_ptr<Configs::Profile>& profile, const QString& tag, qint64 curUp, qint64 curDown)
{
    if (profile == nullptr || tag.isEmpty()) return;
    if (Configs::dataManager->settingsRepo->disable_traffic_stats) return;
    QMutexLocker lk(&creditMu_);
    auto& base = credited_[tag];
    const qint64 dUp = curUp >= base.first ? curUp - base.first : curUp;
    const qint64 dDown = curDown >= base.second ? curDown - base.second : curDown;
    base = qMakePair(curUp, curDown);
    if (dUp <= 0 && dDown <= 0) return;

    Stats::trafficStatsManager->AddConfigDelta(profile->id, dUp, dDown);
    Stats::trafficStatsManager->AddAppDelta(Stats::SPEEDTEST_APP_NAME, "", dUp, dDown);

    profile->traffic_uplink += dUp;
    profile->traffic_downlink += dDown;
    Configs::dataManager->profilesRepo->SaveTraffic(profile);
}

void TestRunner::pollSpeedTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen)
{
    bool ok = false;
    const auto res = defaultClient->QueryCurrentSpeedTests(&ok);
    if (staleGen(gen)) return;
    if (!ok || !res.is_running.value())
    {
        return;
    }
    const libcore::SpeedTestResult result = res.result.value();
    const auto tag = QString::fromStdString(result.outbound_tag.value());
    // value(tag, -1), not operator[]: a const QMap yields 0 for a missing key.
    auto profile = testCurrent ? mw_->running
                               : Configs::dataManager->profilesRepo->GetProfile(tag2entID.value(tag, -1));
    if (profile == nullptr)
    {
        return;
    }
    creditTraffic(profile, tag, result.ul_bytes.value(), result.dl_bytes.value());
    runOnUiThread([this, profile, result]
    {
        mw_->dataViewHtmlGenerator_.setSpeedtestProgress(profile->outbound->name, result);
        mw_->UpdateDataView();

        if (result.error.value().empty() && !result.cancelled.value())
        {
            if (!result.dl_speed.value().empty()) profile->dl_speed = QString::fromStdString(result.dl_speed.value());
            if (!result.ul_speed.value().empty()) profile->ul_speed = QString::fromStdString(result.ul_speed.value());
            if (profile->latency <= 0 && result.latency.value() > 0) profile->SetLatency(result.latency.value());
            if (!result.server_country.value().empty()) profile->test_country = CountryNameToCode(QString::fromStdString(result.server_country.value()));
            mw_->refresh_proxy_list({profile->id});
        }
    });
}

void TestRunner::pollCountryTest(const QMap<QString, int>& tag2entID, bool testCurrent, quint64 gen)
{
    bool ok = false;
    const auto res = defaultClient->QueryCountryTestResults(&ok);
    if (staleGen(gen)) return;
    if (!ok || res.results.empty())
    {
        return;
    }
    for (const auto& result : res.results)
    {
        mw_->dataViewHtmlGenerator_.addTestProgress();
        mw_->UpdateDataView();
        const auto tag = QString::fromStdString(result.outbound_tag.value());
        auto profile = testCurrent ? mw_->running
                                   : Configs::dataManager->profilesRepo->GetProfile(tag2entID.value(tag, -1));
        if (profile == nullptr)
        {
            continue;
        }
        runOnUiThread([this, profile, result]
        {
            if (result.error.value().empty() && !result.cancelled.value())
            {
                if (profile->latency <= 0 && result.latency.value() > 0) profile->SetLatency(result.latency.value());
                if (!result.server_country.value().empty()) profile->test_country = CountryNameToCode(QString::fromStdString(result.server_country.value()));
                mw_->refresh_proxy_list({profile->id});
            }
        });
    }
    mw_->UpdateDataView(true);
}

void TestRunner::runSpeedProbe(const Target& target)
{
    if (stopRequested_.load()) {
        MW_show_log(MainWindow::tr("Profile speed test aborted"));
        return;
    }

    sessionGen_.fetch_add(1);

    const auto speedtestConf = Configs::dataManager->settingsRepo->speed_test_mode;
    libcore::SpeedTestRequest req;
    fillCommonTestReq(req, target);
    req.test_download = speedtestConf == Configs::TestConfig::FULL || speedtestConf == Configs::TestConfig::DL;
    req.test_upload = speedtestConf == Configs::TestConfig::FULL || speedtestConf == Configs::TestConfig::UL;
    req.simple_download = speedtestConf == Configs::TestConfig::SIMPLEDL;
    req.simple_download_addr = Configs::dataManager->settingsRepo->simple_dl_url.toStdString();
    req.test_current = target.testCurrent;
    req.timeout_ms = Configs::dataManager->settingsRepo->speed_test_timeout_ms;
    req.only_country = speedtestConf == Configs::TestConfig::COUNTRY;
    req.country_concurrency = Configs::dataManager->settingsRepo->test_concurrent;

    if (speedtestConf != Configs::TestConfig::COUNTRY) {
        mw_->dataViewHtmlGenerator_.addTestProgress();
        mw_->UpdateDataView();
    }

    const int contextID = target.testCurrent ? (mw_->running ? mw_->running->id : -1) : target.entID;

    bool rpcOK = false;
    QString coreError;
    libcore::SpeedTestResponse result;
    {
        ResultPoller poller([this, gen = sessionGen_.load(), tag2entID = target.tag2entID, testCurrent = target.testCurrent, speedtestConf] {
            if (staleGen(gen)) return;
            if (speedtestConf == Configs::TestConfig::COUNTRY) pollCountryTest(tag2entID, testCurrent, gen);
            else pollSpeedTest(tag2entID, testCurrent, gen);
        }, kSpeedPollIntervalMs);

        result = defaultClient->SpeedTest(&rpcOK, req, &coreError);
    }

    if (!rpcOK || result.results.empty()) {
        if (!rpcOK) mw_->handleXrayGeoAssetError(coreError, contextName(contextID));
        return;
    }

    for (const auto& res : result.results) {
        const int entid = target.testCurrent
                              ? (mw_->running ? mw_->running->id : -1)
                              : resolveEntID(target.tag2entID, res.outbound_tag.value(), target.entID);
        if (entid == -1) {
            MW_show_log(MainWindow::tr("Something is very wrong, the subject ent cannot be found!"));
            continue;
        }

        auto ent = Configs::dataManager->profilesRepo->GetProfile(entid);
        if (ent == nullptr) {
            MW_show_log(MainWindow::tr("Profile manager data is corrupted, try again."));
            continue;
        }

        creditTraffic(ent, QString::fromStdString(res.outbound_tag.value()),
                      res.ul_bytes.value(), res.dl_bytes.value());

        if (res.cancelled.value()) continue;

        const auto error = QString::fromStdString(res.error.value());
        if (error.isEmpty()) {
            ent->dl_speed = QString::fromStdString(res.dl_speed.value());
            ent->ul_speed = QString::fromStdString(res.ul_speed.value());
            if (ent->latency <= 0 && res.latency.value() > 0) ent->SetLatency(res.latency.value());
            if (!res.server_country.value().empty()) ent->test_country = CountryNameToCode(QString::fromStdString(res.server_country.value()));
        } else {
            ent->dl_speed = "N/A";
            ent->ul_speed = "N/A";
            ent->SetLatency(-1);
            ent->test_country = "";
            MW_show_log(MainWindow::tr("[%1] speed test error: %2").arg(ent->outbound->DisplayTypeAndName(), error));
        }
        Configs::dataManager->profilesRepo->Save(ent);
    }
}
