#include "./ui_mainwindow.h"
#include "mainwindow.h"

#include "db/Database.hpp"
#include "db/ConfigBuilder.hpp"
#include "db/traffic/TrafficLooper.hpp"
#include "rpc/gRPC.h"
#include "ui/widget/MessageBoxTimer.h"
#include "3rdparty/qv2ray/v2/proxy/QvProxyConfigurator.hpp"

#include <QInputDialog>
#include <QPushButton>
#include <QDesktopServices>
#include <QMessageBox>

// ext core

std::list<std::shared_ptr<NekoGui_sys::ExternalProcess>> CreateExtCFromExtR(const std::list<std::shared_ptr<NekoGui_fmt::ExternalBuildResult>> &extRs, bool start) {
    // plz run and start in same thread
    std::list<std::shared_ptr<NekoGui_sys::ExternalProcess>> l;
    for (const auto &extR: extRs) {
        std::shared_ptr<NekoGui_sys::ExternalProcess> extC(new NekoGui_sys::ExternalProcess());
        extC->tag = extR->tag;
        extC->program = extR->program;
        extC->arguments = extR->arguments;
        extC->env = extR->env;
        l.emplace_back(extC);
        //
        if (start) extC->Start();
    }
    return l;
}

// grpc

using namespace NekoGui_rpc;

void MainWindow::setup_grpc() {
    // Setup Connection
    defaultClient = new Client(
        [=](const QString &errStr) {
            MW_show_log("[Error] gRPC: " + errStr);
        },
        "127.0.0.1:" + Int2String(NekoGui::dataStore->core_port), NekoGui::dataStore->core_token);

    // Looper
    runOnNewThread([=] { NekoGui_traffic::trafficLooper->Loop(); });
}

void MainWindow::RunSpeedTest(const QString& config, bool useDefault, const QStringList& outboundTags, const QMap<QString, int>& tag2entID, int entID) {
    if (stopSpeedtest.load()) {
        MW_show_log("Profile test aborted");
        return;
    }

    std::list<std::shared_ptr<NekoGui_sys::ExternalProcess>> extCs;
    QSemaphore extSem;

    libcore::TestReq req;
    for (const auto &item: outboundTags) {
        req.add_outbound_tags(item.toStdString());
    }
    req.set_config(config.toStdString());
    req.set_url(NekoGui::dataStore->test_latency_url.toStdString());
    req.set_use_default_outbound(useDefault);
    req.set_max_concurrency(NekoGui::dataStore->test_concurrent);

    bool rpcOK;
    auto result = defaultClient->Test(&rpcOK, req);
    //
    if (!extCs.empty()) {
        runOnUiThread(
            [&] {
                for (const auto &extC: extCs) {
                    extC->Kill();
                }
                extSem.release();
            },
            DS_cores);
        extSem.acquire();
    }
    //
    if (!rpcOK) return;

    for (const auto &res: result.results()) {
        if (!tag2entID.empty()) {
            entID = tag2entID.count(QString(res.outbound_tag().c_str())) == 0 ? -1 : tag2entID[QString(res.outbound_tag().c_str())];
        }
        if (entID == -1) {
            MW_show_log("Something is very wrong, the subject ent cannot be found!");
            continue;
        }

        auto ent = NekoGui::profileManager->GetProfile(entID);
        if (ent == nullptr) {
            MW_show_log("Profile manager data is corrupted, try again.");
            continue;
        }

        if (res.error().empty()) {
            ent->latency = res.latency_ms();
        } else {
            if (QString(res.error().c_str()).contains("test aborted") ||
                QString(res.error().c_str()).contains("context canceled")) ent->latency=0;
            else {
                ent->latency = -1;
                MW_show_log(tr("[%1] test error: %2").arg(ent->bean->DisplayTypeAndName(), res.error().c_str()));
            }
        }
        ent->Save();
    }
}

void MainWindow::speedtest_current_group(const QList<std::shared_ptr<NekoGui::ProxyEntity>>& profiles) {
    if (profiles.isEmpty()) {
        return;
    }
    if (!speedtestRunning.tryLock()) {
        MessageBoxWarning(software_name, "The last speed test did not exit completely, please wait. If it persists, please restart the program.");
        return;
    }

    runOnNewThread([this, profiles]() {
        auto buildObject = NekoGui::BuildTestConfig(profiles);
        if (!buildObject->error.isEmpty()) {
            MW_show_log("Failed to build test config: " + buildObject->error);
            speedtestRunning.unlock();
            return;
        }

        std::atomic<int> counter(0);
        stopSpeedtest.store(false);
        auto testCount = buildObject->fullConfigs.size() + (!buildObject->outboundTags.empty());
        for (const auto &entID: buildObject->fullConfigs.keys()) {
            auto configStr = buildObject->fullConfigs[entID];
            auto func = [this, &counter, testCount, configStr, entID]() {
                MainWindow::RunSpeedTest(configStr, true, {}, {}, entID);
                counter++;
                if (counter.load() == testCount) {
                    speedtestRunning.unlock();
                }
            };
            speedTestThreadPool->start(func);
        }

        if (!buildObject->outboundTags.empty()) {
            auto func = [this, &buildObject, &counter, testCount]() {
                MainWindow::RunSpeedTest(QJsonObject2QString(buildObject->coreConfig, false), false, buildObject->outboundTags, buildObject->tag2entID);
                counter++;
                if (counter.load() == testCount) {
                    speedtestRunning.unlock();
                }
            };
            speedTestThreadPool->start(func);
        }
        if (testCount == 0) speedtestRunning.unlock();

        speedtestRunning.lock();
        speedtestRunning.unlock();
        runOnUiThread([=]{
            refresh_proxy_list();
            MW_show_log("Speedtest finished!");
        });
    });
}

void MainWindow::stopSpeedTests() {
    stopSpeedtest.store(true);
    bool ok;
    defaultClient->StopTests(&ok);

    if (!ok) {
        MW_show_log("Failed to stop tests");
    }
}

void MainWindow::url_test_current() {
    last_test_time = QTime::currentTime();
    ui->label_running->setText(tr("Testing"));

    runOnNewThread([=] {
        libcore::TestReq req;
        req.set_test_current(true);
        req.set_url(NekoGui::dataStore->test_latency_url.toStdString());

        bool rpcOK;
        auto result = defaultClient->Test(&rpcOK, req);
        if (!rpcOK) return;

        auto latency = result.results()[0].latency_ms();
        last_test_time = QTime::currentTime();

        runOnUiThread([=] {
            if (!result.results()[0].error().empty()) {
                MW_show_log(QString("UrlTest error: %1").arg(result.results()[0].error().c_str()));
            }
            if (latency <= 0) {
                ui->label_running->setText(tr("Test Result") + ": " + tr("Unavailable"));
            } else if (latency > 0) {
                ui->label_running->setText(tr("Test Result") + ": " + QString("%1 ms").arg(latency));
            }
        });
    });
}

void MainWindow::stop_core_daemon() {
    NekoGui_rpc::defaultClient->Exit();
}

bool MainWindow::set_system_dns(bool set, bool save_set) {
    if (!NekoGui::dataStore->enable_dns_server) {
        MW_show_log("You need to enable hijack DNS server first");
        return false;
    }
    if (!get_elevated_permissions(4)) {
        return false;
    }
    bool rpcOK;
    QStringList servers;
    bool is_dhcp = false;
    QString res;
    if (set) {
        bool ok;
        auto sysDefaults = defaultClient->GetSystemDNS(&ok);
        if (!ok) {
            MW_show_log("Failed to get system dns settings");
            return false;
        }
        QStringList sysDefServers;
        for (const auto& server : sysDefaults.servers()) {
            sysDefServers.append(server.c_str());
        }
        NekoGui::dataStore->system_dns_servers = sysDefServers;
        NekoGui::dataStore->is_dhcp = sysDefaults.is_dhcp();
        servers = {NekoGui::dataStore->dns_server_listen_addr};
        res = defaultClient->SetSystemDNS(&rpcOK, servers, is_dhcp, false);
    } else {
        servers = NekoGui::dataStore->system_dns_servers;
        is_dhcp = NekoGui::dataStore->is_dhcp;
        res = defaultClient->SetSystemDNS(&rpcOK, servers, is_dhcp, true);
    }
    if (!rpcOK) {
        MW_show_log("Failed to set system dns: " + res);
        return false;
    }
    if (save_set) NekoGui::dataStore->system_dns_set = set;
    return true;
}

void MainWindow::neko_start(int _id) {
    if (NekoGui::dataStore->prepare_exit) return;
#ifdef Q_OS_LINUX
    if (NekoGui::dataStore->enable_dns_server && NekoGui::dataStore->dns_server_listen_port <= 1024) {
        if (!get_elevated_permissions()) {
            MW_show_log(QString("Failed to get admin access, cannot listen on port %1 without it").arg(NekoGui::dataStore->dns_server_listen_port));
            return;
        }
    }
#endif

    auto ents = get_now_selected_list();
    auto ent = (_id < 0 && !ents.isEmpty()) ? ents.first() : NekoGui::profileManager->GetProfile(_id);
    if (ent == nullptr) return;

    if (select_mode) {
        emit profile_selected(ent->id);
        select_mode = false;
        refresh_status();
        return;
    }

    auto group = NekoGui::profileManager->GetGroup(ent->gid);
    if (group == nullptr || group->archive) return;

    auto result = BuildConfig(ent, false, false);
    if (!result->error.isEmpty()) {
        MessageBoxWarning("BuildConfig return error", result->error);
        return;
    }

    auto neko_start_stage2 = [=] {
        libcore::LoadConfigReq req;
        req.set_core_config(QJsonObject2QString(result->coreConfig, true).toStdString());
        req.set_disable_stats(NekoGui::dataStore->disable_traffic_stats);
        if (NekoGui::dataStore->traffic_loop_interval > 0) {
            req.add_stats_outbounds("proxy");
            req.add_stats_outbounds("direct");
        }
        //
        bool rpcOK;
        QString error = defaultClient->Start(&rpcOK, req);
        if (!rpcOK) {
            return false;
        }
        if (!error.isEmpty()) {
            if (error.contains("configure tun interface")) {
                runOnUiThread([=] {
                    auto r = QMessageBox::information(this, tr("Tun device misbehaving"),
                                                      tr("If you have trouble starting VPN, you can force reset nekobox_core process here and then try starting the profile again. The error is %1").arg(error),
                                                      tr("Reset"), tr("Cancel"), "",
                                                      1, 1);
                    if (r == 0) {
                        GetMainWindow()->StopVPNProcess();
                    }
                });
                return false;
            }
            runOnUiThread([=] { MessageBoxWarning("LoadConfig return error", error); });
            return false;
        }
        //
        NekoGui_traffic::trafficLooper->proxy = result->outboundStat.get();
        NekoGui_traffic::trafficLooper->items = result->outboundStats;
        NekoGui::dataStore->ignoreConnTag = result->ignoreConnTag;
        NekoGui_traffic::trafficLooper->loop_enabled = true;

        runOnUiThread(
            [=] {
                auto extCs = CreateExtCFromExtR(result->extRs, true);
                NekoGui_sys::running_ext.splice(NekoGui_sys::running_ext.end(), extCs);
            },
            DS_cores);

        NekoGui::dataStore->UpdateStartedId(ent->id);
        running = ent;

        runOnUiThread([=] {
            refresh_status();
            refresh_proxy_list(ent->id);
        });

        return true;
    };

    if (!mu_starting.tryLock()) {
        MessageBoxWarning(software_name, "Another profile is starting...");
        return;
    }
    if (!mu_stopping.tryLock()) {
        MessageBoxWarning(software_name, "Another profile is stopping...");
        mu_starting.unlock();
        return;
    }
    mu_stopping.unlock();

    // check core state
    if (!NekoGui::dataStore->core_running) {
        runOnUiThread(
            [=] {
                MW_show_log("Try to start the config, but the core has not listened to the grpc port, so restart it...");
                core_process->start_profile_when_core_is_up = ent->id;
                core_process->Restart();
            },
            DS_cores);
        mu_starting.unlock();
        return; // let CoreProcess call neko_start when core is up
    }

    // timeout message
    auto restartMsgbox = new QMessageBox(QMessageBox::Question, software_name, tr("If there is no response for a long time, it is recommended to restart the software."),
                                         QMessageBox::Yes | QMessageBox::No, this);
    connect(restartMsgbox, &QMessageBox::accepted, this, [=] { MW_dialog_message("", "RestartProgram"); });
    auto restartMsgboxTimer = new MessageBoxTimer(this, restartMsgbox, 5000);

    runOnNewThread([=] {
        // stop current running
        if (running != nullptr) {
            runOnUiThread([=] { neko_stop(false, true, true); });
            sem_stopped.acquire();
        }
        // do start
        MW_show_log(">>>>>>>> " + tr("Starting profile %1").arg(ent->bean->DisplayTypeAndName()));
        if (!neko_start_stage2()) {
            MW_show_log("<<<<<<<< " + tr("Failed to start profile %1").arg(ent->bean->DisplayTypeAndName()));
        }
        mu_starting.unlock();
        // cancel timeout
        runOnUiThread([=] {
            restartMsgboxTimer->cancel();
            restartMsgboxTimer->deleteLater();
            restartMsgbox->deleteLater();
#ifdef Q_OS_LINUX
            // Check systemd-resolved
            if (NekoGui::dataStore->spmode_vpn && NekoGui::dataStore->routing->direct_dns.startsWith("local") && ReadFileText("/etc/resolv.conf").contains("systemd-resolved")) {
                MW_show_log("[Warning] The default Direct DNS may not works with systemd-resolved, you may consider change your DNS settings.");
            }
#endif
        });
    });
}

void MainWindow::neko_set_spmode_system_proxy(bool enable, bool save) {
    if (enable != NekoGui::dataStore->spmode_system_proxy) {
#ifndef Q_OS_LINUX
        bool ok;
        auto error = defaultClient->SetSystemProxy(&ok, enable);
        if (!ok) {
            MW_show_log("Failed to set system proxy with error " + error);
            ui->checkBox_SystemProxy->setChecked(false);
            refresh_status();
            return;
        }
#else
        if (enable) {
            auto socks_port = NekoGui::dataStore->inbound_socks_port;
            SetSystemProxy(socks_port, socks_port);
        } else {
            ClearSystemProxy();
        }
#endif
    }

    if (save) {
        NekoGui::dataStore->remember_spmode.removeAll("system_proxy");
        if (enable && NekoGui::dataStore->remember_enable) {
            NekoGui::dataStore->remember_spmode.append("system_proxy");
        }
        NekoGui::dataStore->Save();
    }

    NekoGui::dataStore->spmode_system_proxy = enable;
    refresh_status();
}

void MainWindow::neko_stop(bool crash, bool sem, bool manual) {
    auto id = NekoGui::dataStore->started_id;
    if (id < 0) {
        if (sem) sem_stopped.release();
        return;
    }

    runOnUiThread(
        [=] {
            while (!NekoGui_sys::running_ext.empty()) {
                auto extC = NekoGui_sys::running_ext.front();
                extC->Kill();
                NekoGui_sys::running_ext.pop_front();
            }
        },
        DS_cores);

    auto neko_stop_stage2 = [=] {
        if (!crash) {
            bool rpcOK;
            QString error = defaultClient->Stop(&rpcOK);
            if (rpcOK && !error.isEmpty()) {
                runOnUiThread([=] { MessageBoxWarning("Stop return error", error); });
                return false;
            } else if (!rpcOK) {
                return false;
            }
        }
        return true;
    };

    if (!mu_stopping.tryLock()) {
        if (sem) sem_stopped.release();
        return;
    }

    // timeout message
    auto restartMsgbox = new QMessageBox(QMessageBox::Question, software_name, tr("If there is no response for a long time, it is recommended to restart the software."),
                                         QMessageBox::Yes | QMessageBox::No, this);
    connect(restartMsgbox, &QMessageBox::accepted, this, [=] { MW_dialog_message("", "RestartProgram"); });
    auto restartMsgboxTimer = new MessageBoxTimer(this, restartMsgbox, 5000);

    NekoGui_traffic::trafficLooper->loop_enabled = false;
    NekoGui_traffic::trafficLooper->loop_mutex.lock();
    if (NekoGui::dataStore->traffic_loop_interval != 0) {
        NekoGui_traffic::trafficLooper->UpdateAll();
        for (const auto &item: NekoGui_traffic::trafficLooper->items) {
            NekoGui::profileManager->GetProfile(item->id)->Save();
            refresh_proxy_list(item->id);
        }
    }
    NekoGui_traffic::trafficLooper->loop_mutex.unlock();

    restartMsgboxTimer->cancel();
    restartMsgboxTimer->deleteLater();
    restartMsgbox->deleteLater();

    runOnNewThread([=] {
        // do stop
        MW_show_log(">>>>>>>> " + tr("Stopping profile %1").arg(running->bean->DisplayTypeAndName()));
        if (!neko_stop_stage2()) {
            MW_show_log("<<<<<<<< " + tr("Failed to stop, please restart the program."));
        }

        if (manual) NekoGui::dataStore->UpdateStartedId(-1919);
        NekoGui::dataStore->need_keep_vpn_off = false;
        running = nullptr;

        runOnUiThread([=] {
            refresh_status();
            refresh_proxy_list(id);

            mu_stopping.unlock();
            if (sem) sem_stopped.release();
        });
    });
}

bool isNewer(QString version) {
    version = version.mid(8); // take out nekoray-
    auto parts = version.split('.');
    auto currentParts = QString(NKR_VERSION).split('.');
    std::vector<int> verNums;
    std::vector<int> currNums;
    // add base version first
    verNums.push_back(parts[0].toInt());
    verNums.push_back(parts[1].toInt());
    verNums.push_back(parts[2].split('-')[0].toInt());

    currNums.push_back(currentParts[0].toInt());
    currNums.push_back(currentParts[1].toInt());
    currNums.push_back(currentParts[2].split('-')[0].toInt());

    // base version is equal or greater, check release mode
    int releaseMode;
    int partialVer = 0;
    if (parts[2].split('-').size() > 1 && parts[2].split('-')[1].toInt() == 0 /* this makes sure it is not a number*/) {
        partialVer = parts[3].split('-')[0].toInt();
        auto str = parts[2].split('-')[1];
        if (str == "rc") releaseMode = 3;
        if (str == "beta") releaseMode = 2;
        if (str == "alpha") releaseMode = 1;
    } else {
        releaseMode = 4;
    }
    verNums.push_back(releaseMode);
    verNums.push_back(partialVer);

    int currReleaseMode;
    int currentPartialVer = 0;
    if (currentParts[2].split('-').size() > 1 && currentParts[2].split('-')[1].toInt() == 0 /* this makes sure it is not a number*/) {
        currentPartialVer = currentParts[3].split('-')[0].toInt();
        auto str = currentParts[2].split('-')[1];
        if (str == "rc") currReleaseMode = 3;
        if (str == "beta") currReleaseMode = 2;
        if (str == "alpha") currReleaseMode = 1;
    } else {
        currReleaseMode = 4;
    }
    currNums.push_back(currReleaseMode);
    currNums.push_back(currentPartialVer);

    for (int i=0;i<verNums.size();i++) {
        if (verNums[i] > currNums[i]) return true;
        if (verNums[i] < currNums[i]) return false;
    }

    return false;
}

void MainWindow::CheckUpdate() {
    // on new thread...
    bool ok;
    libcore::UpdateReq request;
    request.set_action(libcore::UpdateAction::Check);
    auto response = NekoGui_rpc::defaultClient->Update(&ok, request);
    if (!ok) return;

    auto err = response.error();
    if (!err.empty()) {
        runOnUiThread([=] {
            MessageBoxWarning(QObject::tr("Update"), err.c_str());
        });
        return;
    }

    if (response.release_download_url() == nullptr || !isNewer(QString(response.assets_name().c_str()))) {
        runOnUiThread([=] {
            MessageBoxInfo(QObject::tr("Update"), QObject::tr("No update"));
        });
        return;
    }

    runOnUiThread([=] {
        auto allow_updater = !NekoGui::dataStore->flag_use_appdata;
        auto note_pre_release = response.is_pre_release() ? " (Pre-release)" : "";
        QMessageBox box(QMessageBox::Question, QObject::tr("Update") + note_pre_release,
                        QObject::tr("Update found: %1\nRelease note:\n%2").arg(response.assets_name().c_str(), response.release_note().c_str()));
        //
        QAbstractButton *btn1 = nullptr;
        if (allow_updater) {
            btn1 = box.addButton(QObject::tr("Update"), QMessageBox::AcceptRole);
        }
        QAbstractButton *btn2 = box.addButton(QObject::tr("Open in browser"), QMessageBox::AcceptRole);
        box.addButton(QObject::tr("Close"), QMessageBox::RejectRole);
        box.exec();
        //
        if (btn1 == box.clickedButton() && allow_updater) {
            // Download Update
            runOnNewThread([=] {
                bool ok2;
                libcore::UpdateReq request2;
                request2.set_action(libcore::UpdateAction::Download);
                auto response2 = NekoGui_rpc::defaultClient->Update(&ok2, request2);
                runOnUiThread([=] {
                    if (response2.error().empty()) {
                        auto q = QMessageBox::question(nullptr, QObject::tr("Update"),
                                                       QObject::tr("Update is ready, restart to install?"));
                        if (q == QMessageBox::StandardButton::Yes) {
                            this->exit_reason = 1;
                            on_menu_exit_triggered();
                        }
                    } else {
                        MessageBoxWarning(QObject::tr("Update"), response2.error().c_str());
                    }
                });
            });
        } else if (btn2 == box.clickedButton()) {
            QDesktopServices::openUrl(QUrl(response.release_url().c_str()));
        }
    });
}