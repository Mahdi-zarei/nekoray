#include "include/ui/mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QModelIndex>
#include <QMutex>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QThread>
#include <QThreadPool>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <ranges>

#include "3rdparty/QrDecoder.h"
#include "3rdparty/qrcodegen.hpp"

#include "include/ui/utils/ScreenQrScanner.h"

#include "include/configs/generate.h"
#include "include/configs/sub/GroupUpdater.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/ui/mainWindow/MainWindowInternal.h"
#include "include/ui/profile/dialog_edit_profile.h"
#include "include/ui/utils/ProfilesTableModel.h"

namespace {
    constexpr int removeListPreviewLimit = 20;
}

void MainWindow::on_profilesTableView_doubleClicked(const QModelIndex &index) {
    if (!index.isValid() || !profilesTableModel) return;
    int id = index.data(ProfilesTableModel::ProfileIdRole).toInt();
    if (select_mode) {
        emit profile_selected(id);
        select_mode = false;
        refresh_status();
        return;
    }
    auto dialog = new DialogEditProfile("", id, this);
    connect(dialog, &QDialog::finished, dialog, &QDialog::deleteLater);
}

void MainWindow::on_menu_add_from_input_triggered() {
    auto dialog = new DialogEditProfile("autoselector", Configs::dataManager->settingsRepo->current_group, this);
    connect(dialog, &QDialog::finished, dialog, &QDialog::deleteLater);
}

void MainWindow::on_menu_add_from_clipboard_triggered() {
    auto clipboard = QApplication::clipboard()->text();
    import_or_handle_deeplink(clipboard);
}

void MainWindow::on_menu_clone_triggered() {
    auto entIDs = get_now_selected_list();
    if (entIDs.isEmpty()) return;

    auto btn = QMessageBox::question(this, tr("Clone"), tr("Clone %1 item(s)").arg(entIDs.count()));
    if (btn != QMessageBox::Yes) return;

    QStringList sls;
    auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
    for (const auto &ent: ents) {
        sls << ent->outbound->ExportJsonLink();
    }

    Subscription::updater()->ImportText(sls.join("\n"));
}

void MainWindow::on_menu_delete_repeat_triggered() {
    const auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (group == nullptr) return;

    // Duplicates are positional: one id can own several rows, and distinct ids can share a config (#1775).
    const auto groupIds = group->Profiles();
    const auto groupProfiles = Configs::dataManager->profilesRepo->GetProfileBatch(groupIds);

    QList<std::shared_ptr<Configs::Profile>> uniq;
    Configs::ProfileFilter::Uniq(groupProfiles, uniq, false);
    QSet<int> keepIds;
    for (const auto &ent: uniq) keepIds.insert(ent->id);

    QHash<int, std::shared_ptr<Configs::Profile>> byId;
    for (const auto &ent: groupProfiles) byId.insert(ent->id, ent);

    QList<int> newOrder;
    QList<int> del_ids;
    QSet<int> placed;
    int remove_display_count = 0;
    QString remove_display;
    for (int id: groupIds) {
        const bool repeatedSlot = placed.contains(id);
        placed.insert(id);
        if (!repeatedSlot && keepIds.contains(id)) {
            newOrder += id;
            continue;
        }
        if (!repeatedSlot) del_ids += id;
        if (const auto ent = byId.value(id); ent != nullptr && remove_display_count < removeListPreviewLimit) {
            remove_display += ent->outbound->DisplayTypeAndName() + " \n ";
            if (++remove_display_count == removeListPreviewLimit) remove_display += " ... ";
        }
    }

    const auto removed_rows = groupIds.size() - newOrder.size();
    if (removed_rows == 0) return;
    if (!Configs::dataManager->settingsRepo->skip_delete_confirmation &&
        QMessageBox::question(this, tr("Confirmation"), tr("Remove %1 item(s) ?").arg(removed_rows) + "\n" + remove_display) != QMessageBox::StandardButton::Yes) {
        return;
    }

    group->profiles = newOrder;
    Configs::dataManager->groupsRepo->Save(group);
    if (!del_ids.isEmpty()) Configs::dataManager->profilesRepo->BatchDeleteProfiles(del_ids, true);
    refresh_proxy_list({}, true, RefreshAnchor::Removal);
}

void MainWindow::on_menu_delete_triggered() {
    auto entIDs = get_now_selected_list();
    if (entIDs.count() == 0) return;
    if (Configs::dataManager->settingsRepo->skip_delete_confirmation || QMessageBox::question(this, tr("Confirmation"), QString(tr("Remove %1 item(s) ?")).arg(entIDs.count()))==QMessageBox::StandardButton::Yes) {
        Configs::dataManager->profilesRepo->BatchDeleteProfiles(entIDs, true);
        refresh_proxy_list({}, true, RefreshAnchor::Removal);
    }
}

void MainWindow::on_menu_reset_traffic_triggered() {
    auto entIDs = get_now_selected_list();
    if (entIDs.count() == 0) return;
    auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
    if (ents.empty()) return;
    for (const auto& ent: ents) {
        ent->ResetTraffic();
        Configs::dataManager->profilesRepo->SaveTraffic(ent);
    }
    if (auto group = Configs::dataManager->groupsRepo->GetGroup(ents.first()->gid); group &&
        group->calculated_column_width.size() > ProfilesTableModel::ColTraffic)
        group->calculated_column_width[ProfilesTableModel::ColTraffic] = 0;
    refresh_proxy_list(entIDs);
}

void MainWindow::on_menu_copy_links_triggered() {
    if (ui->masterLogBrowser->hasFocus()) {
        ui->masterLogBrowser->copy();
        return;
    }
    auto entIDs = get_now_selected_list();
    QStringList links;
    auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
    for (const auto &ent: ents) {
        auto link = ent->outbound->ExportToLink();
        if (link.isEmpty()) link = ent->outbound->ExportJsonLink();
        links += link;
    }
    if (links.length() == 0) return;
    QApplication::clipboard()->setText(links.join("\n"));
    MW_show_log(tr("Copied %1 item(s)").arg(links.length()));
}

void MainWindow::on_menu_copy_links_nkr_triggered() {
    auto entIDs = get_now_selected_list();
    QStringList links;
    auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
    for (const auto &ent: ents) {
        links += ent->outbound->ExportJsonLink();
    }
    if (links.length() == 0) return;
    QApplication::clipboard()->setText(links.join("\n"));
    MW_show_log(tr("Copied %1 item(s)").arg(links.length()));
}

void MainWindow::on_menu_export_config_triggered() {
    auto ents = get_now_selected_list();
    if (ents.count() != 1) return;
    auto ent = Configs::dataManager->profilesRepo->GetProfile(ents.first());

    auto result = Configs::BuildSingBoxConfig(ent);
    QString config_core = QJsonObject2QString(result->coreConfig, true);
    QApplication::clipboard()->setText(config_core);

    QMessageBox msg(QMessageBox::Information, tr("Config copied"), config_core);
    QPushButton *button_1 = msg.addButton(tr("Copy core config"), QMessageBox::YesRole);
    QPushButton *button_2 = msg.addButton(tr("Copy test config"), QMessageBox::YesRole);
    msg.addButton(QMessageBox::Ok);
    msg.setEscapeButton(QMessageBox::Ok);
    msg.setDefaultButton(QMessageBox::Ok);
    msg.exec();
    if (msg.clickedButton() == button_1) {
        result = BuildSingBoxConfig(ent);
        if (!result->error.isEmpty()) {
            MessageBoxWarning("Build config error", result->error);
            return;
        }
        config_core = QJsonObject2QString(result->coreConfig, true);
        QApplication::clipboard()->setText(config_core);
    } else if (msg.clickedButton() == button_2) {
        auto res = Configs::BuildTestConfig({ent});
        if (!res->error.isEmpty()) {
            MessageBoxWarning("Build Test config error", res->error);
            return;
        }
        if (!res->xrayFullConfigs.isEmpty()) config_core = res->xrayFullConfigs.first();
        else config_core = QJsonObject2QString(res->coreConfig, true);
        QApplication::clipboard()->setText(config_core);
    }
}

void MainWindow::display_qr_link(bool nkrFormat) {
    auto ents = get_now_selected_list();
    if (ents.count() != 1) return;

    class W : public QDialog {
    public:
        QLabel *l = nullptr;
        QCheckBox *cb = nullptr;
        QPlainTextEdit *l2 = nullptr;
        QImage im;
        QString link;
        QString link_deep;

        void show_qr(const QSize &size) const {
            auto side = size.height() - 20 - l2->size().height() - cb->size().height();
            l->setPixmap(QPixmap::fromImage(im.scaled(side, side, Qt::KeepAspectRatio, Qt::FastTransformation),
                                            Qt::MonoOnly));
            l->resize(side, side);
        }

        void refresh(bool is_deep) {
            auto link_display = is_deep ? link_deep : link;
            l2->setPlainText(link_display);
            constexpr qint32 qr_padding = 2;
            try {
                qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(link_display.toUtf8().data(), qrcodegen::QrCode::Ecc::MEDIUM);
                qint32 sz = qr.getSize();
                im = QImage(sz + qr_padding * 2, sz + qr_padding * 2, QImage::Format_RGB32);
                QRgb black = qRgb(0, 0, 0);
                QRgb white = qRgb(255, 255, 255);
                im.fill(white);
                for (int y = 0; y < sz; y++)
                    for (int x = 0; x < sz; x++)
                        if (qr.getModule(x, y))
                            im.setPixel(x + qr_padding, y + qr_padding, black);
                show_qr(size());
            } catch (const std::exception &ex) {
                QMessageBox::warning(nullptr, "error", ex.what());
            }
        }

        W(const QString &link_, const QString &link_deep_, bool showDeep) {
            link = link_;
            link_deep = link_deep_;
            setLayout(new QVBoxLayout);
            setMinimumSize(256, 256);
            QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            sizePolicy.setHeightForWidth(true);
            setSizePolicy(sizePolicy);
            l = new QLabel();
            l->setMinimumSize(256, 256);
            l->setMargin(6);
            l->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            l->setScaledContents(true);
            layout()->addWidget(l);
            cb = new QCheckBox;
            cb->setText("Deep Link");
            // Set before wiring toggled, so refresh() below draws it just once.
            cb->setChecked(showDeep);
            layout()->addWidget(cb);
            l2 = new QPlainTextEdit();
            l2->setReadOnly(true);
            layout()->addWidget(l2);
            connect(cb, &QCheckBox::toggled, this, &W::refresh);
            refresh(showDeep);
        }

        void resizeEvent(QResizeEvent *resizeEvent) override {
            show_qr(resizeEvent->size());
        }
    };

    auto ent = Configs::dataManager->profilesRepo->GetProfile(ents.first());
    auto link = ent->outbound->ExportToLink();
    auto link_deep = ent->outbound->ExportJsonLink();
    auto w = new W(link, link_deep, nkrFormat || link.isEmpty());
    w->setWindowTitle(ent->outbound->DisplayTypeAndName());
    w->exec();
    w->deleteLater();
}

void MainWindow::parseQrImage(const QPixmap *image)
{
    const QVector<QString> texts = QrDecoder().decode(image->toImage());
    if (texts.isEmpty()) {
        MessageBoxInfo(software_name, tr("QR Code not found"));
    } else {
        for (const QString &text : texts) {
            MW_show_log("QR Code Result:\n" + text);
            import_text(text);
        }
    }
}

void MainWindow::on_menu_scan_qr_triggered() {
    bool captured = false;
    const auto texts = ScreenQr::ScanScreens(this, captured);

    if (!captured) {
        MessageBoxInfo(software_name, tr("Unable to capture screen"));
        return;
    }
    if (texts.isEmpty()) {
        MessageBoxInfo(software_name, tr("QR Code not found"));
        return;
    }
    for (const QString &text : texts) {
        MW_show_log("QR Code Result:\n" + text);
        import_text(text);
    }
}

void MainWindow::on_menu_clear_test_result_triggered() {
    auto entIDs = Configs::dataManager->groupsRepo->CurrentGroup()->Profiles();
    auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(entIDs);
    if (ents.empty()) return;
    for (const auto &ent: ents) {
        ent->ClearTestResults();
    }
    Configs::dataManager->profilesRepo->SaveBatch(ents);
    if (auto group = Configs::dataManager->groupsRepo->GetGroup(ents.first()->gid); group &&
        group->calculated_column_width.size() > ProfilesTableModel::ColTestResult)
        group->calculated_column_width[ProfilesTableModel::ColTestResult] = 0;
    refresh_proxy_list();
}

void MainWindow::on_menu_select_all_triggered() {
    if (ui->masterLogBrowser->hasFocus()) {
        ui->masterLogBrowser->selectAll();
        return;
    }
    ui->profilesTableView->selectAll();
}

void MainWindow::on_menu_update_subscription_triggered() {
    auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (group->url.isEmpty()) return;
    if (mw_sub_updating) return;
    mw_sub_updating = true;
    Subscription::updater()->RefreshGroup(group->id, [&] { mw_sub_updating = false; }, true);
}

void MainWindow::on_menu_remove_unavailable_triggered() {
    clearUnavailableProfiles();
}

void MainWindow::on_menu_remove_invalid_triggered() {
    runOnNewThread([=,this]
    {
        QList<std::shared_ptr<Configs::Profile>> out_del;

     auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
     if (currentGroup == nullptr) return;
     std::atomic counter(0);
     QMutex mu;
     QMutex access;
     int profileSize = currentGroup->Profiles().size();
     // Empty group: no worker unlocks mu, so the join below would block forever.
     if (profileSize == 0) return;
     mu.lock();
     for (const auto& profileID : currentGroup->Profiles()) {
         auto profile = Configs::dataManager->profilesRepo->GetProfile(profileID);
         parallelCoreCallPool->start([&out_del, profile, &counter, &mu, profileSize, &access]
         {
             if (!IsValid(profile))
             {
                 access.lock();
                 out_del += profile;
                 access.unlock();
             }
             if (++counter == profileSize) mu.unlock();
         });
     }
     mu.lock();
     mu.unlock();

     int remove_display_count = 0;
     QString remove_display;
     for (const auto &ent: out_del) {
         remove_display += ent->outbound->DisplayTypeAndName() + "\n";
         if (++remove_display_count == removeListPreviewLimit) {
             remove_display += "...";
             break;
         }
     }

     runOnUiThread([=,this]
     {
         if (!out_del.empty() &&
         (Configs::dataManager->settingsRepo->skip_delete_confirmation || QMessageBox::question(this, tr("Confirmation"), tr("Remove %1 Invalid item(s) ?").arg(out_del.length()) + "\n" + remove_display) == QMessageBox::StandardButton::Yes)) {
         QList<int> del_ids;
         for (const auto &ent: out_del) {
             del_ids += ent->id;
         }
         Configs::dataManager->profilesRepo->BatchDeleteProfiles(del_ids, true);
         refresh_proxy_list({}, true, RefreshAnchor::Removal);
     }
     });
    });
}

void MainWindow::on_menu_remove_insecure_triggered() {
    auto currentGroup = Configs::dataManager->groupsRepo->CurrentGroup();
    if (currentGroup == nullptr) return;
    auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(currentGroup->Profiles());

    QList<int> del_ids;
    QString remove_display;
    int remove_display_count = 0;
    for (const auto& profile : profiles) {
        if (!profile || !profile->outbound) continue;
        if (!profile->outbound->GetSecurity().isDangerous()) continue;
        del_ids += profile->id;
        if (remove_display_count < removeListPreviewLimit) {
            remove_display += profile->outbound->DisplayTypeAndName() + "\n";
            if (++remove_display_count == removeListPreviewLimit) remove_display += "...";
        }
    }

    if (del_ids.isEmpty()) {
        QMessageBox::information(this, tr("Remove Insecure Configs"), tr("No insecure configs found."));
        return;
    }
    if (Configs::dataManager->settingsRepo->skip_delete_confirmation ||
        QMessageBox::question(this, tr("Confirmation"),
            tr("Remove %1 insecure config(s)?").arg(del_ids.length()) + "\n" + remove_display) == QMessageBox::StandardButton::Yes) {
        Configs::dataManager->profilesRepo->BatchDeleteProfiles(del_ids, true);
        refresh_proxy_list({}, true, RefreshAnchor::Removal);
    }
}

void MainWindow::on_menu_resolve_selected_triggered() {
    auto profiles = get_now_selected_list();
    if (profiles.isEmpty()) return;

    if (mw_sub_updating) return;
    mw_sub_updating = true;
    Configs::dataManager->settingsRepo->resolve_count = profiles.count();

    auto ents = Configs::dataManager->profilesRepo->GetProfileBatch(profiles);
    for (const auto &profile: ents) {
        profile->outbound->ResolveDomainToIP([=,this] {
            Configs::dataManager->profilesRepo->Save(profile);
            refresh_proxy_list({profile->id});
            if (--Configs::dataManager->settingsRepo->resolve_count != 0) return;
            mw_sub_updating = false;
        });
    }
}

void MainWindow::on_menu_resolve_domain_triggered() {
    auto currGroup = Configs::dataManager->groupsRepo->GetGroup(Configs::dataManager->settingsRepo->current_group);
    if (currGroup == nullptr) return;

    auto profiles = currGroup->Profiles();
    if (profiles.isEmpty()) return;

    if (QMessageBox::question(this,
                              tr("Confirmation"),
                              tr("Replace domain server addresses with their resolved IPs?")) != QMessageBox::StandardButton::Yes) {
        return;
    }
    if (mw_sub_updating) return;
    mw_sub_updating = true;
    Configs::dataManager->settingsRepo->resolve_count = profiles.count();

    for (const auto id: profiles) {
        auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
        profile->outbound->ResolveDomainToIP([=,this] {
            Configs::dataManager->profilesRepo->Save(profile);
            refresh_proxy_list({profile->id});
            if (--Configs::dataManager->settingsRepo->resolve_count != 0) return;
            mw_sub_updating = false;
        });
    }
}

void MainWindow::on_profilesTableView_customContextMenuRequested(const QPoint &pos) {
    ui->menu_server->popup(ui->profilesTableView->viewport()->mapToGlobal(pos));
}

QList<int> MainWindow::get_now_selected_list() {
    QList<int> list;
    if (!profilesTableModel) return list;
    QModelIndexList indices = ui->profilesTableView->selectionModel()->selectedRows(0);
    for (const QModelIndex &idx : indices) {
        list << idx.data(ProfilesTableModel::ProfileIdRole).toInt();
    }
    return list;
}

QList<int> MainWindow::get_selected_or_group() {
    auto selected_or_group = ui->menu_server->property("selected_or_group").toInt();
    QList<int> profileIDs;
    if (selected_or_group > 0) {
        profileIDs = get_now_selected_list();
        if (profileIDs.isEmpty() && selected_or_group == 2) profileIDs = Configs::dataManager->groupsRepo->CurrentGroup()->Profiles();
    } else {
        profileIDs = Configs::dataManager->groupsRepo->CurrentGroup()->Profiles();
    }
    return profileIDs;
}

void MainWindow::saveProfileFocusState() {
    auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (group == nullptr) return;

    if (!profilesTableModel) return;

    // hasFocus() is false while the header's filter fields hold the caret.
    m_profilesTableHadFocus = ui->profilesTableView->hasFocus();
    m_profilesScrollValue = ui->profilesTableView->verticalScrollBar()->value();

    QModelIndexList indices = ui->profilesTableView->selectionModel()->selectedRows(0);
    group->selectedProfilesIdIdxPairs.clear();

    for (const QModelIndex &idx : indices) {
        group->selectedProfilesIdIdxPairs << std::make_pair(idx.data(ProfilesTableModel::ProfileIdRole).toInt(), idx.row());
    }
}

void MainWindow::restoreProfileFocusState(RefreshAnchor anchor) {
    auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (group == nullptr || !profilesTableModel) return;

    auto *view = ui->profilesTableView;
    // show_group() skips the save and restores scroll itself from scroll_last_profile.
    const bool restoreViewport = !Configs::dataManager->settingsRepo->refreshing_group;

    if (restoreViewport && m_profilesTableHadFocus) view->setFocus();

    QList<int> newIndexes;
    for (auto &id: group->selectedProfilesIdIdxPairs | std::views::keys) {
        if (auto sourceRow = profilesTableModel->indexOfProfile(id); sourceRow != -1) {
            if (auto newIdx = profilesFilterModel->toProxyRow(sourceRow); newIdx != -1) newIndexes << newIdx;
        }
    }

    if (!newIndexes.isEmpty()) {
        selectProfileRows(newIndexes);
    } else if (anchor == RefreshAnchor::Removal && !group->selectedProfilesIdIdxPairs.isEmpty()) {
        // Rows arrive in selection order, so the topmost one is the smallest, not the first.
        int desiredIndex = std::ranges::min(group->selectedProfilesIdIdxPairs | std::views::values);
        desiredIndex = std::min(desiredIndex, profilesFilterModel->rowCount() - 1);
        if (desiredIndex >= 0) selectProfileRows({desiredIndex});
    }

    if (restoreViewport) view->verticalScrollBar()->setValue(m_profilesScrollValue);
}

void MainWindow::selectProfileRows(const QList<int> &rows) {
    if (rows.isEmpty() || !profilesFilterModel) return;

    auto *view = ui->profilesTableView;
    // setCurrentIndex() scrolls the current row into sight unless autoScroll is off.
    const bool autoScroll = view->hasAutoScroll();
    view->setAutoScroll(false);

    QItemSelection selection;
    for (int row : rows) {
        QModelIndex left  = profilesFilterModel->index(row, 0);
        QModelIndex right = profilesFilterModel->index(row, profilesFilterModel->columnCount() - 1);
        selection.select(left, right);
    }
    view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    view->selectionModel()->setCurrentIndex(profilesFilterModel->index(rows.first(), 0), QItemSelectionModel::NoUpdate);

    view->setAutoScroll(autoScroll);
}

void MainWindow::focusProfilesTable(bool selectFirst) {
    auto *view = ui->profilesTableView;
    view->setFocus();
    if (!selectFirst || !profilesFilterModel || profilesFilterModel->rowCount() == 0) return;
    selectProfileRows({0});
    view->scrollToTop();
}

void MainWindow::clearUnavailableProfiles(bool confirm, QList<int> profileIDs) {
    QList<int> del_ids;
    int remove_display_count = 0;
    QString remove_display;

    auto group = Configs::dataManager->groupsRepo->CurrentGroup();
    if (!group) return;

    if (profileIDs.isEmpty()) profileIDs = group->Profiles();

    auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(profileIDs);
    for (const auto &profile: profiles) {
        if (profile->latency < 0 && profile->latency != Configs::kLatencyConnectOnly) {
            del_ids += profile->id;
            if (++remove_display_count == removeListPreviewLimit) {
                remove_display += "...";
            }else if (remove_display_count < removeListPreviewLimit) remove_display += profile->outbound->DisplayTypeAndName() + "\n";
        }
    }

    auto clearFunc = [&, this] {
        Configs::dataManager->profilesRepo->BatchDeleteProfiles(del_ids);
        refresh_proxy_list({}, true, RefreshAnchor::Removal);
    };

    if (!del_ids.isEmpty()) {
        if (confirm && !Configs::dataManager->settingsRepo->skip_delete_confirmation) {
            if (QMessageBox::question(this, tr("Confirmation"), tr("Remove %1 Unavailable item(s) ?").arg(del_ids.length()) + "\n" + remove_display) == QMessageBox::StandardButton::Yes) {
                clearFunc();
            }
        } else {
            clearFunc();
        }
    }
}
