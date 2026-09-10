#include "include/ui/mainwindow.h"

#include "include/ui/mainWindow/TestRunner.h"

#include "include/stats/traffic/TrafficLooper.hpp"
#include "include/stats/traffic/TrafficStatsManager.hpp"
#include "include/stats/autoselector/AutoSelectorMonitor.hpp"
#include "include/configs/AutoSelectorPlan.h"
#include "include/api/RPC.h"
#include "include/ui/utils/MessageBoxTimer.h"
#include "include/ui/stats/dialog_endpoint_details.h"

#include <QPushButton>
#include <QMessageBox>
#include <QJsonDocument>
#include <QFile>
#include <QRegularExpression>

#include "include/configs/generate.h"
#include "include/configs/common/xrayStreamSetting.h"
#include "include/database/GroupsRepo.h"
#include "include/database/OtpProfilesRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/global/OtpPlaceholder.hpp"
#include "include/global/RunningProfiles.hpp"
#include "include/global/VpnCredentialOverride.hpp"
#include "include/ui/profile/dialog_vpn_auth.h"

#include "include/sys/Process.hpp"

#include <algorithm>
#include <memory>

using namespace API;

void MainWindow::setup_rpc(QLocalSocket *socket) {
    defaultClient->Reconnect(socket);

    if (!rpc_started) {
        rpc_started = true;
        runOnNewThread([=, this] { Stats::trafficLooper->Loop(); });
        runOnNewThread([=, this] { Stats::connection_lister->Loop(); });
        runOnNewThread([=, this] { Stats::autoSelectorMonitor->Loop(); });
    }
}

bool MainWindow::set_system_dns(bool set, bool save_set) {
    if (!Configs::dataManager->settingsRepo->enable_dns_server) {
        MW_show_log(tr("You need to enable hijack DNS server first"));
        return false;
    }
    if (!get_elevated_permissions(ExitReason::RestartWithDns)) {
        return false;
    }
    bool rpcOK;
    QString res;
    if (set) {
        res = defaultClient->SetSystemDNS(&rpcOK, false);
    } else {
        res = defaultClient->SetSystemDNS(&rpcOK, true);
    }
    if (!rpcOK) {
        MW_show_log(tr("Failed to set system dns: ") + res);
        return false;
    }
    if (save_set) Configs::dataManager->settingsRepo->system_dns_set = set;
    return true;
}

int MainWindow::get_profile_to_start() {
    const auto ents = get_now_selected_list();
    if (ents.size() == 1) {
        return ents.first();
    }
    if (ents.isEmpty()) {
        if (last_running_profile_id >= 0 && Configs::dataManager->profilesRepo->GetProfile(last_running_profile_id) != nullptr) {
            return last_running_profile_id;
        }
        const int rememberId = Configs::dataManager->settingsRepo->remember_id;
        if (rememberId >= 0 && Configs::dataManager->profilesRepo->GetProfile(rememberId) != nullptr) {
            return rememberId;
        }
        const auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
        if (currentGroup) {
            const auto profiles = currentGroup->Profiles();
            if (!profiles.isEmpty()) {
                const int firstId = profiles.first();
                if (Configs::dataManager->profilesRepo->GetProfile(firstId) != nullptr) {
                    return firstId;
                }
            }
        }
    }
    return -1;
}

bool MainWindow::handleXrayGeoAssetError(const QString& error, const QString& contextName) {
    const bool refGeoip = error.contains("geoip.dat");
    const bool refGeosite = error.contains("geosite.dat");
    if (!refGeoip && !refGeosite) return false;

    runOnUiThread([=, this] {
        if (m_xrayGeoAssetBusy) return;
        m_xrayGeoAssetBusy = true;
        setTimeout([=, this] {
            const QString base = Configs::GetBasePath();
            const bool haveGeoip = QFile::exists(base + "/geoip.dat");
            const bool haveGeosite = QFile::exists(base + "/geosite.dat");

            const bool geoipLacksCategory = refGeoip && haveGeoip;
            const bool geositeLacksCategory = refGeosite && haveGeosite;
            if (geoipLacksCategory || geositeLacksCategory) {
                const QString whichFile = geositeLacksCategory ? "geosite.dat" : "geoip.dat";
                const QString ruleType = geositeLacksCategory ? "geosite" : "geoip";

                QString category;
                QRegularExpression re(QStringLiteral("code\\s+(\\S+)\\s+from"));
                const auto m = re.match(error);
                if (m.hasMatch()) category = m.captured(1);
                const QString needed = category.isEmpty()
                    ? tr("a required category")
                    : QStringLiteral("%1:%2").arg(ruleType, category);

                MessageBoxWarning(
                    tr("Geo asset missing category"),
                    tr("The Xray config \"%1\" needs \"%2\", but the installed %3 does "
                       "not contain it.\n\n"
                       "Re-downloading from the same source will not fix this — the data "
                       "file does not include that category. Set the GeoIP/GeoSite asset "
                       "URL in Settings to a source that provides \"%2\", then delete %3 "
                       "from the app folder and download it again.")
                        .arg(contextName, needed, whichFile));
                m_xrayGeoAssetBusy = false;
                return;
            }

            if (QMessageBox::question(this, tr("Geo asset files required"),
                    tr("The Xray config \"%1\" uses geoip/geosite routing rules, but the "
                       "required data files (geoip.dat / geosite.dat) are not installed.\n\n"
                       "Download them now?").arg(contextName)) != QMessageBox::Yes) {
                m_xrayGeoAssetBusy = false;
                return;
            }

            runOnNewThread([=, this] {
                QString dlErr;
                if (!haveGeoip) {
                    auto e = NetworkRequestHelper::DownloadAsset(Configs::dataManager->settingsRepo->xray_geoip_url, "geoip.dat");
                    if (!e.isEmpty()) dlErr += "geoip.dat: " + e + "\n";
                }
                if (!haveGeosite) {
                    auto e = NetworkRequestHelper::DownloadAsset(Configs::dataManager->settingsRepo->xray_geosite_url, "geosite.dat");
                    if (!e.isEmpty()) dlErr += "geosite.dat: " + e + "\n";
                }
                runOnUiThread([=, this] {
                    m_xrayGeoAssetBusy = false;
                    if (!dlErr.isEmpty()) {
                        MessageBoxWarning(tr("Geo asset download failed"), dlErr);
                    } else {
                        MW_show_log(tr("Downloaded Xray geo asset files."));
                        QMessageBox::information(this, tr("Geo assets installed"),
                            tr("Geo data files were downloaded successfully.\n\n"
                               "Please try again."));
                    }
                });
            });
        }, this, 300);
    });
    return true;
}

void MainWindow::profile_start(int _id) {
    if (Configs::dataManager->settingsRepo->prepare_exit) return;
#ifdef Q_OS_LINUX
    if (Configs::dataManager->settingsRepo->enable_dns_server && Configs::dataManager->settingsRepo->dns_server_listen_port <= 1024) {
        if (!get_elevated_permissions()) {
            MW_show_log(QString("Failed to get admin access, cannot listen on port %1 without it").arg(Configs::dataManager->settingsRepo->dns_server_listen_port));
            return;
        }
    }
#endif

    std::shared_ptr<Configs::Profile> ent = nullptr;
    if (_id >= 0) {
        ent = Configs::dataManager->profilesRepo->GetProfile(_id);
    } else {
        const int startId = get_profile_to_start();
        if (startId >= 0) {
            ent = Configs::dataManager->profilesRepo->GetProfile(startId);
        }
    }
    if (ent == nullptr) return;

    last_running_profile_id = ent->id;

    if (select_mode) {
        emit profile_selected(ent->id);
        select_mode = false;
        refresh_status();
        return;
    }

    const auto group = Configs::dataManager->groupsRepo->GetGroup(ent->gid);
    if (group == nullptr || group->archive) return;

    // Ranking must run before the config is built and it blocks, so hop off the UI thread.
    if (ent->type == "autoselector" && !auto_selector_ranked) {
        const auto plan = Configs::PlanAutoSelector(ent);
        if (plan.error.isEmpty() && plan.needsRanking) {
            auto_selector_ranked = true;
            const int startId = ent->id;
            runOnNewThread([=, this] {
                rank_auto_selector(ent);
                runOnUiThread([=, this] {
                    auto_selector_ranked = false;
                    profile_start(startId);
                });
            });
            return;
        }
    }
    auto_selector_ranked = false;

    const auto result = Configs::BuildSingBoxConfig(ent);
    if (!result->error.isEmpty()) {
        MessageBoxWarning(tr("BuildConfig return error"), result->error);
        return;
    }
    auto profile_start_stage2 = [=, this] {
        libcore::LoadConfigReq req;
        req.core_config = QJsonObject2QString(result->coreConfig, true).toStdString();
        req.tun_ipv4_cidr = result->tunIPv4CIDR.toStdString();
        req.disable_stats = Configs::dataManager->settingsRepo->disable_traffic_stats;
        req.xray_config = QJsonObject2QString(result->xrayConfig, true).toStdString();
        req.need_xray = !result->xrayConfig.isEmpty();
        for (const auto &full : result->xrayFullConfigs) req.xray_full_configs.push_back(full.toStdString());
        if (req.need_xray || !req.xray_full_configs.empty()) {
            req.xray_outbound_dns_strategy = Configs::getXrayOutboundDomainStrategy().toStdString();
            if (auto selector = ent->AutoSelector(); selector != nullptr) {
                // The idle window must outlast the probe interval or the sidecar restarts every round.
                req.xray_lazy_start = true;
                req.xray_idle_seconds = std::max(120, selector->intervalSec * 2);
                // 0 = resident.
                req.xray_full_idle_seconds = 0;
            }
        }
        if (!result->extraCoreData->path.isEmpty())
        {
            req.need_extra_process = true;
            req.extra_process_path = result->extraCoreData->path.toStdString();
            req.extra_process_args = result->extraCoreData->args.toStdString();
            req.extra_process_conf = result->extraCoreData->config.toStdString();
            req.extra_no_out = result->extraCoreData->noLog;
        }
        bool rpcOK;
        const QString error = defaultClient->Start(&rpcOK, req);
        if (!rpcOK) {
            return false;
        }
        if (!error.isEmpty()) {
            if (handleXrayGeoAssetError(error, ent->outbound->DisplayTypeAndName())) {
                return false;
            }
            if (error.contains("Fwpm", Qt::CaseInsensitive)) {
                runOnUiThread([=, this] {
                    MessageBoxWarning(
                        tr("Strict routing unavailable"),
                        tr("Windows could not enable strict routing. Open Tun Settings, "
                           "disable Strict Route, and start the profile again.\n\n"
                           "Disabling Strict Route may cause DNS leaks.\n\nError: %1").arg(error));
                });
                return false;
            }
            if (error.contains("configure tun interface")) {
                runOnUiThread([=, this] {

                    QMessageBox msg(
                        QMessageBox::Information,
                        tr("Tun device misbehaving"),
                        tr("If you have trouble starting VPN, you can force reset Core process here and then try starting the profile again. The error is %1").arg(error),
                        QMessageBox::NoButton,
                        this
                    );
                    auto reset = msg.addButton(tr("Reset"), QMessageBox::ActionRole);
                    auto cancel = msg.addButton(tr("Cancel"), QMessageBox::ActionRole);

                    msg.setDefaultButton(cancel);
                    msg.setEscapeButton(cancel);

                    msg.exec();
                    if (msg.clickedButton() == reset) {
                        RestartCore();
                    }
                });
                return false;
            }
            runOnUiThread([=, this] { MessageBoxWarning("LoadConfig return error", error); });
            return false;
        }
        Stats::trafficLooper->SetChainGroups(result->chainGroups);
        Stats::trafficLooper->loop_enabled = true;
        Stats::connection_lister->suspend = false;
        Stats::autoSelectorMonitor->SetBuild(result->autoSelectors);
        if (!result->autoSelectors.isEmpty()) {
            const auto& info = result->autoSelectors.first();
            if (auto selector = ent->AutoSelector(); selector != nullptr) {
                QList<int> builtIDs;
                QHash<int, QString> names;
                for (const auto& [tag, member] : info.members) {
                    if (member == nullptr) continue;
                    builtIDs << member->id;
                    names.insert(member->id, member->outbound ? member->outbound->DisplayName() : member->name);
                }
                const auto now = QDateTime::currentSecsSinceEpoch();
                selector->lastBuilt = builtIDs;
                selector->lastBuiltAt = now;
                selector->RecordHistory(builtIDs, names, now);
                Configs::dataManager->profilesRepo->Save(ent);
                MW_show_log(tr("[Auto selector] Running the best %1 of %2 ranked profiles.")
                                .arg(builtIDs.size())
                                .arg(selector->pool.size()));
            }
        }

        Configs::dataManager->settingsRepo->UpdateStartedId(ent->id);
        // Must land after the stop this start may have run first: that stop clears the map.
        Stats::SetVpnEndpointProfiles(result->vpnEndpointProfiles);
        Configs::SetRunningProfiles(result->involvedProfiles);
        running = ent;
        if (Configs::dataManager->settingsRepo->spmode_system_proxy) set_system_proxy(true);

        const bool exitIsEndpoint = vpn_exit_endpoint(ent) != nullptr;

        runOnUiThread([=, this] {
            clearRestartNeeded();
            start_vpn_challenge_poll();
            refresh_status();
            refresh_proxy_list({ent->id});
            refresh_auto_selector_view();

            // "Only route advertised network" rejects this probe.
            if (exitIsEndpoint) return;

            auto resp = NetworkRequestHelper::HttpGet("http://ip-api.com/json/", false, true);
            if (resp.error.isEmpty()) {
                QJsonDocument doc = QJsonDocument::fromJson(resp.data);
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    QString city = obj["city"].toString();
                    QString countryName = obj["country"].toString();
                    QString countryCode = obj["countryCode"].toString();
                    if (running) running->runningCountryInfo = QString("%1 %2, %3").arg(CountryCodeToFlag(countryCode), countryName, city);
                    refresh_status();
                }
            }
        });

        return true;
    };

    if (!mu_starting.tryLock()) {
        MessageBoxWarning(software_name, tr("Another profile is starting..."));
        return;
    }
    if (!mu_stopping.tryLock()) {
        MessageBoxWarning(software_name, tr("Another profile is stopping..."));
        mu_starting.unlock();
        return;
    }
    mu_stopping.unlock();

    if (!Configs::dataManager->settingsRepo->core_running) {
        runOnThread(
            [=, this] {
                MW_show_log(tr("Try to start the config, but the core has not listened to the RPC port, so restart it..."));
                core_process->start_profile_when_core_is_up = ent->id;
                core_process->Restart();
            },
            DS_cores);
        mu_starting.unlock();
        return;
    }

    const auto restartMsgbox = new QMessageBox(QMessageBox::Question, software_name, tr("If there is no response for a long time, it is recommended to restart the software."),
                                         QMessageBox::Yes | QMessageBox::No, this);
    connect(restartMsgbox, &QMessageBox::accepted, this, [=,this] { MW_dialog_message(MwMessage::RestartProgram, {}); });
    const auto restartMsgboxTimer = new MessageBoxTimer(this, restartMsgbox, 10000);

    runOnUiThread([this] {
        m_profileConnecting = true;
        refresh_startstop_button();
    });

    runOnNewThread([=, this] {
        if (running != nullptr) {
            profile_stop(false, false, true);
            mu_stopping.lock();
            mu_stopping.unlock();
        }
        MW_show_log(">>>>>>>> " + tr("Starting profile %1").arg(ent->outbound->DisplayTypeAndName()));
        if (!profile_start_stage2()) {
            MW_show_log("<<<<<<<< " + tr("Failed to start profile %1").arg(ent->outbound->DisplayTypeAndName()));
        }
        mu_starting.unlock();
        runOnUiThread([=, this] {
            restartMsgboxTimer->cancel();
            restartMsgboxTimer->deleteLater();
            restartMsgbox->deleteLater();
            m_profileConnecting = false;
            refresh_startstop_button();
        });
    });
}

void MainWindow::profile_stop(bool crash, bool block, bool manual) {
    if (running == nullptr) {
        return;
    }
    const auto id = running->id;

    auto profile_stop_stage2 = [=,this] {
        if (testRunner->isTestingCurrent()) {
            bool ok;
            defaultClient->StopTests(&ok);
            if (!ok) MW_show_log("Failed to stop profile tests!");
        }
        if (!crash) {
            bool rpcOK;
            const QString error = defaultClient->Stop(&rpcOK);
            if (rpcOK && !error.isEmpty()) {
                runOnUiThread([=,this] { MessageBoxWarning(tr("Stop return error"), error); });
                return false;
            } else if (!rpcOK) {
                return false;
            }
        }
        if (Configs::dataManager->settingsRepo->spmode_system_proxy) set_system_proxy(false);
        return true;
    };

    if (!mu_stopping.tryLock()) {
        return;
    }

    // profile_stop() is reached from a worker thread as well as the UI one.
    runOnUiThread([this] { UpdateConnectionList({}); });

    runOnUiThread([this] {
        m_profileDisconnecting = true;
        refresh_startstop_button();
    });

    Stats::autoSelectorMonitor->Clear();
    runOnUiThread([this] { refresh_auto_selector_view(); });

    runOnNewThread([=, this] {
        Stats::trafficLooper->loop_enabled = false;
        Stats::connection_lister->suspend = true;
        Stats::trafficLooper->loop_mutex.lock();
        Stats::trafficLooper->UpdateAll();
        Stats::trafficLooper->loop_mutex.unlock();
        Stats::trafficLooper->PersistTraffic();
        Stats::trafficStatsManager->Flush();

        // runOnUiThread is a no-op before qApp exists, so the teardown must not chase these.
        QMessageBox* restartMsgbox = nullptr;
        MessageBoxTimer* restartMsgboxTimer = nullptr;
        runOnUiThread([=, this, &restartMsgbox, &restartMsgboxTimer] {
            restartMsgbox = new QMessageBox(QMessageBox::Question, software_name, tr("If there is no response for a long time, it is recommended to restart the software."),
                             QMessageBox::Yes | QMessageBox::No, this);
            connect(restartMsgbox, &QMessageBox::accepted, this, [=, this] { MW_dialog_message(MwMessage::RestartProgram, {}); });
            restartMsgboxTimer = new MessageBoxTimer(this, restartMsgbox, 5000);
        }, true);

        // Snapshot: `running` is cleared below and a racing start can reassign it.
        const auto stopping = running;
        if (stopping != nullptr) {
            MW_show_log(">>>>>>>> " + tr("Stopping profile %1").arg(stopping->outbound->DisplayTypeAndName()));
        }
        if (!profile_stop_stage2()) {
            MW_show_log("<<<<<<<< " + tr("Failed to stop, please restart the program."));
        }

        if (manual) Configs::dataManager->settingsRepo->UpdateStartedId(Configs::NoProfileId);
        // Cleared here too: a restart prompt armed before the UI-thread teardown would have no id to start.
        Configs::ClearRunningProfiles();
        running = nullptr;

        runOnUiThread([=, this, &restartMsgboxTimer, &restartMsgbox] {
            if (restartMsgboxTimer != nullptr) {
                restartMsgboxTimer->cancel();
                restartMsgboxTimer->deleteLater();
            }
            if (restartMsgbox != nullptr) restartMsgbox->deleteLater();

            m_profileDisconnecting = false;
            clearRestartNeeded();
            // profile_start() stops the old run first, so only a stop outside a restart is the user's.
            const bool restartingForAuth = manual && id == m_vpnAuthRestartID;
            m_vpnAuthRestartID = -1;
            if (manual && !restartingForAuth) clear_vpn_credential_overrides();
            stop_vpn_challenge_poll();
            refresh_status();
            refresh_proxy_list({id});

            mu_stopping.unlock();
        }, true);
    }, block);
}

void MainWindow::start_vpn_challenge_poll() {
    if (m_vpnChallengeTimer == nullptr) {
        m_vpnChallengeTimer = new QTimer(this);
        m_vpnChallengeTimer->setInterval(2000);
        connect(m_vpnChallengeTimer, &QTimer::timeout, this, [this] { poll_vpn_challenges(); });
    }
    reset_vpn_endpoint_tracking();
    m_vpnChallengeTimer->start();
}

void MainWindow::stop_vpn_challenge_poll() {
    if (m_vpnChallengeTimer != nullptr) m_vpnChallengeTimer->stop();
    reset_vpn_endpoint_tracking();
    Stats::SetVpnEndpointProfiles({});
    Configs::ClearRunningProfiles();
    if (m_vpnAuthDialog != nullptr) m_vpnAuthDialog->close();
}

// Per-run only: the auto-restart budget must not be cleared here, the restart it counts comes through.
void MainWindow::reset_vpn_endpoint_tracking() {
    m_vpnChallengeSeen.clear();
    m_vpnEndpointState.clear();
    m_vpnEndpointLastState.clear();
    m_vpnTroubleSummary.clear();
    m_vpnTroubleDetail.clear();
    m_vpnOtpLastCode.clear();
    m_vpnOtpRejects.clear();
    m_vpnChallengeAnswering.clear();
    dataViewHtmlGenerator_.setVpnEndpointStatus({}, {}, false);
    UpdateDataView(true);
}

void MainWindow::poll_vpn_challenges() {
    if (Configs::dataManager->settingsRepo->started_id < 0) return;
    if (m_vpnChallengeBusy.exchange(true)) return;

    runOnNewThread([this] {
        bool rpcOK = false;
        const auto status = defaultClient->QueryVPNStatus(&rpcOK, {});

        QList<VpnAuthChallenge> pending;
        QList<QPair<QString, QString>> authFailures;
        QList<VpnEndpointState> endpointStates;
        if (rpcOK) {
            for (const auto &res : status.results) {
                const auto tag = QString::fromStdString(res.tag.value());

                VpnEndpointState endpointState;
                endpointState.tag = tag;
                endpointState.state = QString::fromStdString(res.state.value());
                endpointState.error = QString::fromStdString(res.error.value());
                endpointState.connected = res.connected.value();
                endpointState.authFailed = res.auth_failed.value();
                endpointStates.append(endpointState);

                if (endpointState.authFailed) {
                    authFailures.append({tag, endpointState.error});
                }
                if (!res.challenge.has_value()) continue;
                const auto &raw = res.challenge.value();

                VpnAuthChallenge challenge;
                challenge.endpointTag = QString::fromStdString(raw.endpoint_tag.value());
                if (challenge.endpointTag.isEmpty()) challenge.endpointTag = tag;
                challenge.id = QString::fromStdString(raw.id.value());
                challenge.kind = QString::fromStdString(raw.kind.value());
                challenge.username = QString::fromStdString(raw.username.value());
                challenge.message = QString::fromStdString(raw.message.value());
                challenge.banner = QString::fromStdString(raw.banner.value());
                challenge.error = QString::fromStdString(raw.error.value());
                challenge.url = QString::fromStdString(raw.url.value());
                challenge.echo = raw.echo.value();
                challenge.deadline = raw.deadline.value();
                for (const auto &rawField : raw.fields) {
                    VpnAuthField field;
                    field.submissionKey = QString::fromStdString(rawField.submission_key.value());
                    field.name = QString::fromStdString(rawField.name.value());
                    field.label = QString::fromStdString(rawField.label.value());
                    if (field.label.isEmpty()) field.label = field.name;
                    field.kind = QString::fromStdString(rawField.kind.value());
                    field.value = QString::fromStdString(rawField.value.value());
                    for (const auto &rawOption : rawField.options) {
                        field.options.append({QString::fromStdString(rawOption.value.value()),
                                              QString::fromStdString(rawOption.label.value())});
                    }
                    challenge.fields.append(field);
                }
                pending.append(challenge);
            }
        }

        runOnUiThread([=, this] {
            m_vpnChallengeBusy.store(false);
            if (m_vpnChallengeTimer == nullptr || !m_vpnChallengeTimer->isActive()) return;
            update_vpn_endpoint_states(endpointStates);
            for (const auto &challenge : pending) {
                if (auto_answer_vpn_challenge(challenge)) continue;
                show_vpn_challenge(challenge);
            }
            for (const auto &[tag, error] : authFailures) show_vpn_auth_failure(tag, error);
        });
    });
}

namespace {
    constexpr int kMaxVpnOtpRejects = 3;
    constexpr int kMaxVpnAutoRestarts = 3;
    constexpr qint64 kVpnAutoRestartCooldownSecs = 20;

    QString vpnFieldHaystack(const VpnAuthField &field) {
        return (field.name + QChar(' ') + field.label).toLower();
    }

    bool vpnFieldLooksLikeToken(const VpnAuthField &field) {
        static const QStringList hints = {QStringLiteral("token"),        QStringLiteral("otp"),
                                          QStringLiteral("passcode"),     QStringLiteral("one-time"),
                                          QStringLiteral("onetime"),      QStringLiteral("second"),
                                          QStringLiteral("challenge"),    QStringLiteral("verification"),
                                          QStringLiteral("authenticator")};
        const auto haystack = vpnFieldHaystack(field);
        return std::any_of(hints.begin(), hints.end(),
                           [&](const QString &hint) { return haystack.contains(hint); });
    }

    bool vpnFieldLooksLikeUsername(const VpnAuthField &field) {
        const auto haystack = vpnFieldHaystack(field);
        return haystack.contains(QStringLiteral("user")) || haystack.contains(QStringLiteral("login")) ||
               haystack.contains(QStringLiteral("account"));
    }

    bool buildVpnFormAnswer(const VpnAuthChallenge &challenge, const Configs::openconnect *ocon,
                            const QString &user, const QString &pass, const QString &code,
                            QMap<QString, QString> *out) {
        if (ocon == nullptr || challenge.fields.isEmpty()) return false;
        bool passwordUsed = false;
        for (const auto &field : challenge.fields) {
            QString value;
            bool resolved = false;
            for (const auto &entry : ocon->form_entries) {
                if (entry == nullptr || entry->promote) continue;
                const bool byKey = !entry->submission_key.isEmpty() &&
                                   entry->submission_key == field.submissionKey;
                // A field reaches a challenge only when the core matched no entry, so a name match can only be the withheld {otp} entry.
                const bool byName = entry->submission_key.isEmpty() && !entry->name.isEmpty() &&
                                    entry->name == field.name &&
                                    entry->value.contains(Configs::kOtpPlaceholder);
                if (!byKey && !byName) continue;
                value = Configs::SubstituteOtp(entry->value, code);
                resolved = true;
            }
            if (!resolved && field.kind == QStringLiteral("password")) {
                if (!passwordUsed && !vpnFieldLooksLikeToken(field) && !pass.isEmpty()) {
                    value = Configs::SubstituteOtp(pass, code);
                    passwordUsed = true;
                } else {
                    value = code;
                }
                resolved = true;
            }
            if (!resolved && vpnFieldLooksLikeUsername(field) && !user.isEmpty()) {
                value = Configs::SubstituteOtp(user, code);
                resolved = true;
            }
            if (!resolved && !field.value.isEmpty()) {
                value = field.value;
                resolved = true;
            }
            if (!resolved) return false;
            out->insert(field.submissionKey, value);
        }
        return !out->isEmpty();
    }
}

void MainWindow::update_vpn_endpoint_states(const QList<VpnEndpointState> &states) {
    QString exitState;
    QStringList summaryParts;
    QStringList detailParts;
    QHash<QString, QString> seenStates;
    bool problem = false;

    for (const auto &state : states) {
        const auto name = Stats::VpnEndpointDisplayName(state.tag);
        const auto text = vpn_state_text(state.state, state.error);
        if (state.tag == QStringLiteral("proxy")) exitState = text;
        if (state.connected) m_vpnOtpRejects.remove(state.tag);

        const auto previous = m_vpnEndpointLastState.value(state.tag);
        seenStates.insert(state.tag, state.state);
        if (previous != state.state && !previous.isEmpty()) MW_show_log(tr("[VPN] %1: %2").arg(name, text));

        if (state.connected) continue;
        if (state.authFailed || state.state == QStringLiteral("error")) problem = true;
        summaryParts << QStringLiteral("%1: %2").arg(name, Stats::VpnStateText(state.state));
        detailParts << (state.error.isEmpty() ? QStringLiteral("%1: %2").arg(name, Stats::VpnStateText(state.state))
                                              : QStringLiteral("%1: %2").arg(name, state.error));
    }
    m_vpnEndpointLastState = seenStates;

    QString summary;
    if (!summaryParts.isEmpty()) {
        summary = (problem ? tr("VPN endpoint problem") : tr("VPN endpoint")) + ": " +
                  QStringList(summaryParts.mid(0, 2)).join(QStringLiteral("; "));
        if (summaryParts.size() > 2) summary += QStringLiteral(" (+%1)").arg(summaryParts.size() - 2);
    }
    const auto detail = QStringList(detailParts.mid(0, 3)).join(QStringLiteral(" | "));

    if (summary == m_vpnTroubleSummary && detail == m_vpnTroubleDetail && exitState == m_vpnEndpointState) return;
    m_vpnTroubleSummary = summary;
    m_vpnTroubleDetail = detail;
    m_vpnEndpointState = exitState;
    dataViewHtmlGenerator_.setVpnEndpointStatus(summary, detail, problem);
    UpdateDataView(true);
    refresh_status();
}

bool MainWindow::auto_answer_vpn_challenge(const VpnAuthChallenge &challenge) {
    if (challenge.id.isEmpty() || running == nullptr) return false;
    if (m_vpnChallengeAnswering.contains(challenge.endpointTag)) return true;

    const int profileID = Stats::VpnEndpointProfileID(challenge.endpointTag);
    if (profileID < 0) return false;
    const auto ent = Configs::dataManager->profilesRepo->GetProfile(profileID);
    if (ent == nullptr) return false;

    const auto *ovpn = ent->OpenVPN();
    const auto *ocon = ent->OpenConnect();
    int otpID = -1;
    QString storedUser, storedPass;
    if (ovpn != nullptr) {
        otpID = ovpn->otp_profile_id;
        storedUser = ovpn->username;
        storedPass = ovpn->password;
    } else if (ocon != nullptr) {
        otpID = ocon->otp_profile_id;
        storedUser = ocon->username;
        storedPass = ocon->password;
    }
    if (otpID < 0) return false;

    const auto otpProfile = Configs::dataManager->otpProfilesRepo->GetOtpProfile(otpID);
    if (otpProfile == nullptr || !otpProfile->Validate().isEmpty()) return false;

    if (!challenge.error.isEmpty()) {
        if (m_vpnOtpRejects.value(challenge.endpointTag) >= kMaxVpnOtpRejects) return false;
        // The same digits were just refused; hold until the TOTP window rolls instead of replaying.
        if (otpProfile->type == OTP::Type::TOTP &&
            otpProfile->CurrentCode() == m_vpnOtpLastCode.value(challenge.endpointTag)) {
            return true;
        }
    }

    const auto creds = Configs::ResolveVpnCredentials(profileID, storedUser, storedPass);
    const bool credentialsKind = ovpn != nullptr && challenge.kind == QStringLiteral("credentials");

    // Settle the shape before minting: resolving a HOTP code spends a counter step.
    if (ovpn != nullptr) {
        if (challenge.kind != QStringLiteral("secret") && !credentialsKind) return false;
        if (credentialsKind && creds.username.isEmpty() && creds.password.isEmpty()) return false;
    } else if (challenge.kind != QStringLiteral("form")) {
        return false;
    }

    const auto code = Configs::ResolveOtpCode(otpID);
    if (code.isEmpty()) return false;

    QString user, pass, secret;
    QMap<QString, QString> formValues;
    if (ovpn != nullptr) {
        // The core packs the answer as SCRV1 whatever it gets, so the code rides the secret slot.
        secret = code;
        if (credentialsKind) {
            user = Configs::SubstituteOtp(creds.username, code);
            pass = Configs::SubstituteOtp(creds.password, code);
        }
    } else if (!buildVpnFormAnswer(challenge, ocon, creds.username, creds.password, code, &formValues)) {
        return false;
    }

    if (!challenge.error.isEmpty()) {
        m_vpnOtpRejects[challenge.endpointTag] = m_vpnOtpRejects.value(challenge.endpointTag) + 1;
    }
    m_vpnOtpLastCode.insert(challenge.endpointTag, code);
    submit_vpn_challenge_answer(challenge, user, pass, secret, formValues);
    return true;
}

void MainWindow::submit_vpn_challenge_answer(const VpnAuthChallenge &challenge, const QString &username,
                                             const QString &password, const QString &secret,
                                             const QMap<QString, QString> &formValues) {
    const auto tag = challenge.endpointTag;
    const auto id = challenge.id;
    const auto name = Stats::VpnEndpointDisplayName(tag);
    m_vpnChallengeAnswering.insert(tag);

    runOnNewThread([=, this] {
        bool rpcOK = false;
        const auto error = defaultClient->SubmitVPNChallenge(&rpcOK, tag, id, username, password, secret, formValues);
        runOnUiThread([=, this] {
            m_vpnChallengeAnswering.remove(tag);
            if (!rpcOK) {
                MW_show_log(tr("[VPN] %1: the core did not answer the sign-in prompt.").arg(name));
            } else if (!error.isEmpty()) {
                MW_show_log(tr("[VPN] %1: could not answer the sign-in prompt: %2").arg(name, error));
            } else {
                MW_show_log(tr("[VPN] %1: signed in again with a new one-time code.").arg(name));
            }
        });
    });
}

bool MainWindow::auto_restart_for_vpn_auth(const QString &endpointTag, int profileID) {
    const auto ent = Configs::dataManager->profilesRepo->GetProfile(profileID);
    if (ent == nullptr || running == nullptr) return false;

    int otpID = -1;
    if (const auto *ovpn = ent->OpenVPN(); ovpn != nullptr) otpID = ovpn->otp_profile_id;
    else if (const auto *ocon = ent->OpenConnect(); ocon != nullptr) {
        if (ocon->password_authentication_disabled) return false;
        otpID = ocon->otp_profile_id;
    }
    if (otpID < 0) return false;
    if (m_vpnAutoRestarts.value(profileID) >= kMaxVpnAutoRestarts) return false;

    const auto now = QDateTime::currentSecsSinceEpoch();
    if (now - m_vpnAutoRestartAt < kVpnAutoRestartCooldownSecs) return true;

    m_vpnAutoRestarts[profileID] = m_vpnAutoRestarts.value(profileID) + 1;
    m_vpnAutoRestartAt = now;

    MW_show_log(tr("[VPN] %1 rejected the saved credentials; restarting the profile with a new one-time code.")
                    .arg(Stats::VpnEndpointDisplayName(endpointTag)));
    const int runningID = running->id;
    m_vpnAuthRestartID = runningID;
    profile_start(runningID);
    return true;
}

void MainWindow::show_vpn_challenge(const VpnAuthChallenge &challenge) {
    if (challenge.id.isEmpty()) return;
    if (m_vpnAuthDialog != nullptr) return;

    const auto key = challenge.endpointTag + QChar(0x1F) + challenge.id;
    if (m_vpnChallengeSeen.contains(key)) return;
    m_vpnChallengeSeen.insert(key);

    auto *dialog = new DialogVpnAuth(this, challenge);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_vpnAuthDialog = dialog;
    dialog->show();
    ActivateWindow(dialog);
}

void MainWindow::show_vpn_auth_failure(const QString &endpointTag, const QString &error) {
    constexpr int maxPrompts = 3;

    if (m_vpnAuthDialog != nullptr || running == nullptr) return;
    const int profileID = Stats::VpnEndpointProfileID(endpointTag);
    if (profileID < 0) return;

    const auto key = endpointTag + QChar(0x1F) + "auth-failed";
    if (m_vpnChallengeSeen.contains(key)) return;

    // The core reads credentials once, at build time; rebuilding is the only way to hand it a new code.
    if (auto_restart_for_vpn_auth(endpointTag, profileID)) return;

    m_vpnChallengeSeen.insert(key);
    if (m_vpnAuthPrompted.value(profileID) >= maxPrompts) return;

    const auto ent = Configs::dataManager->profilesRepo->GetProfile(profileID);
    if (ent == nullptr) return;

    VpnAuthChallenge challenge;
    challenge.endpointTag = endpointTag;
    challenge.kind = "credentials";
    if (const auto *ovpn = ent->OpenVPN(); ovpn != nullptr) challenge.username = ovpn->username;
    else if (const auto *ocon = ent->OpenConnect(); ocon != nullptr) {
        // The core reports a refused password-less login the same way, and credentials cannot fix it.
        if (ocon->password_authentication_disabled) return;
        challenge.username = ocon->username;
    }
    else return;
    challenge.error = error;
    challenge.message = tr("The server refused the credentials saved with this profile. Enter the ones "
                           "to use for this session; the profile itself is left unchanged.");

    m_vpnAuthPrompted[profileID] = m_vpnAuthPrompted.value(profileID) + 1;
    MW_show_log(tr("[VPN] %1 rejected the saved credentials.").arg(Stats::VpnEndpointDisplayName(endpointTag)));

    const int runningID = running->id;
    auto *dialog = new DialogVpnAuth(this, challenge, true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_vpnAuthDialog = dialog;
    connect(dialog, &QDialog::accepted, this, [=, this] {
        if (running == nullptr || running->id != runningID) return;
        Configs::SetVpnCredentialOverride(profileID, {dialog->enteredUsername(), dialog->enteredPassword()});
        m_vpnAuthRestartID = runningID;
        profile_start(runningID);
    });
    dialog->show();
    ActivateWindow(dialog);
}

void MainWindow::clear_vpn_credential_overrides() {
    for (auto it = m_vpnAuthPrompted.constBegin(); it != m_vpnAuthPrompted.constEnd(); ++it) {
        Configs::ClearVpnCredentialOverride(it.key());
    }
    m_vpnAuthPrompted.clear();
    m_vpnAutoRestarts.clear();
    m_vpnAutoRestartAt = 0;
}
