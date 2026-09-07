#include "include/ui/mainwindow.h"

#include <QAbstractItemView>
#include <QMenu>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

#include "include/configs/sub/GroupUpdater.hpp"
#include "include/database/GroupsRepo.h"
#include "include/ui/group/dialog_edit_group.h"
#include "include/ui/mainWindow/MainWindowInternal.h"
#include "include/ui/mainWindow/TestRunner.h"
#include "include/global/Utils.hpp"

void MainWindow::on_tabWidget_currentChanged(int index) {
    if (Configs::dataManager->settingsRepo->refreshing_group_list) return;
    const auto gid = tabIndex2GroupId(index);
    if (gid == Configs::dataManager->settingsRepo->current_group) return;
    show_group(gid);
}

void MainWindow::show_group(int gid) {
    if (Configs::dataManager->settingsRepo->refreshing_group) return;
    Configs::dataManager->settingsRepo->refreshing_group = true;

    const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
    if (group == nullptr) {
        MessageBoxWarning(tr("Error"), QString("No such group: %1").arg(gid));
        Configs::dataManager->settingsRepo->refreshing_group = false;
        return;
    }

    if (Configs::dataManager->settingsRepo->current_group != gid) {
        saveProfileFocusState();
        if (auto lastGroup = Configs::dataManager->groupsRepo->CurrentGroup()) {
            lastGroup->scroll_last_profile = ui->profilesTableView->firstVisibleRow();
            Configs::dataManager->groupsRepo->Save(lastGroup);
        }
        Configs::dataManager->settingsRepo->current_group = gid;
        Configs::dataManager->settingsRepo->Save();
    }

    ui->tabWidget->widget(groupId2TabIndex(gid))->layout()->addWidget(ui->profilesTableView);

    // Update top bar subscription card
    if (m_subInfoCard != nullptr) {
        m_subInfoCard->setGroup(group);
    }
    UpdateDataView(true);

    // Update tab tooltip
    int tabIdx = groupId2TabIndex(gid);
    if (tabIdx >= 0 && group != nullptr) {
        auto subInfo = group->GetSubUserInfo();
        QString title = subInfo.title.isEmpty() ? group->name : subInfo.title;

        if (group->url.isEmpty()) {
            ui->tabWidget->setTabToolTip(tabIdx, title);
        } else {
            QString html = QStringLiteral("<div style='max-width:320px;line-height:1.3;'>");
            html += QStringLiteral("<b>%1</b><br>").arg(title.toHtmlEscaped());
            html += QStringLiteral("<span style='opacity:0.8;'>%1</span><br>").arg(tr("Type: Subscription"));

            if (group->sub_last_update > 0) {
                html += QStringLiteral("%1: %2<br>").arg(tr("Last updated"), DisplayTime(group->sub_last_update, QLocale::ShortFormat));
            }
            if (group->sub_update_interval > 0) {
                html += QStringLiteral("%1: every %2h<br>").arg(tr("Auto-update"), QString::number(group->sub_update_interval));
            }
            if (subInfo.valid) {
                html += QStringLiteral("%1: %2<br>").arg(tr("Used"), ReadableSize(subInfo.used()));
                if (subInfo.total > 0) {
                    html += QStringLiteral("%1: %2 (%3: %4)<br>").arg(
                        tr("Total"), ReadableSize(subInfo.total), tr("Remaining"), ReadableSize(subInfo.remaining()));
                }
                if (subInfo.expire > 0) {
                    html += QStringLiteral("%1: %2<br>").arg(tr("Expires"), DisplayTime(subInfo.expire, QLocale::ShortFormat));
                }
                if (!subInfo.support_url.isEmpty()) {
                    html += QStringLiteral("%1: %2<br>").arg(tr("Support"), subInfo.support_url.toHtmlEscaped());
                }
                if (!subInfo.web_url.isEmpty()) {
                    html += QStringLiteral("%1: %2<br>").arg(tr("Portal"), subInfo.web_url.toHtmlEscaped());
                }
                if (!subInfo.announce.isEmpty()) {
                    html += QStringLiteral("<div style='margin-top:4px;padding-top:4px;border-top:1px solid rgba(128,128,128,0.3);'><b>%1:</b><br>%2</div>")
                        .arg(tr("Announcement"), subInfo.announce.toHtmlEscaped());
                }
            }
            html += QStringLiteral("</div>");
            ui->tabWidget->setTabToolTip(tabIdx, html);
        }
    }


    refresh_proxy_list({}, true);

    // scroll_last_profile came from firstVisibleRow(), so it is a proxy row.
    const int rowCount = profilesFilterModel->rowCount();
    int targetRow = group->scroll_last_profile;
    if (targetRow >= rowCount && rowCount > 0) targetRow = rowCount - 1;
    QTimer::singleShot(0, ui->profilesTableView, [=, this]() {
        if (targetRow >= 0) {
            if (QModelIndex idx = profilesFilterModel->index(targetRow, 0); idx.isValid()) {
                ui->profilesTableView->scrollTo(idx, QAbstractItemView::PositionAtTop);
            }
        }
        refresh_proxy_list_column_size();
    });

    Configs::dataManager->settingsRepo->refreshing_group = false;
}

void MainWindow::refresh_groups() {
    Configs::dataManager->settingsRepo->refreshing_group_list = true;

    for (int i = ui->tabWidget->count() - 1; i > 0; i--) {
        ui->tabWidget->removeTab(i);
    }

    int index = 0;
    for (const auto &gid: Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (index == 0) {
            ui->tabWidget->setTabText(0, group->name);
        } else {
            auto widget2 = new QWidget();
            auto layout2 = new QVBoxLayout();
            layout2->setContentsMargins(QMargins());
            layout2->setSpacing(0);
            widget2->setLayout(layout2);
            ui->tabWidget->addTab(widget2, group->name);
        }
        ui->tabWidget->tabBar()->setTabData(index, gid);
        index++;
    }

    if (Configs::dataManager->groupsRepo->CurrentGroup() == nullptr) {
        Configs::dataManager->settingsRepo->current_group = -1;
        ui->tabWidget->setCurrentIndex(groupId2TabIndex(0));
        const auto tabOrder = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
        show_group(tabOrder.count() > 0 ? tabOrder.first() : 0);
    } else {
        ui->tabWidget->setCurrentIndex(groupId2TabIndex(Configs::dataManager->settingsRepo->current_group));
        show_group(Configs::dataManager->settingsRepo->current_group);
    }

    Configs::dataManager->settingsRepo->refreshing_group_list = false;
}

// The strip right of the last tab belongs to the tabWidget, not the tab bar.
void MainWindow::on_tabWidget_customContextMenuRequested(const QPoint &p) {
    show_group_tab_menu(ui->tabWidget->tabBar()->mapFrom(ui->tabWidget, p));
}

void MainWindow::show_group_tab_menu(const QPoint &p) {
    const int clickedIndex = ui->tabWidget->tabBar()->tabAt(p);
    if (clickedIndex == -1) {
        QMenu menu(this);
        connect(menu.addAction(tr("Add new Group")), &QAction::triggered, this, [=,this]{
            auto ent = Configs::dataManager->groupsRepo->NewGroup();
            auto dialog = new DialogEditGroup(ent, this);
            const int ret = dialog->exec();
            dialog->deleteLater();

            if (ret == QDialog::Accepted) {
                Configs::dataManager->groupsRepo->AddGroup(ent);
                MW_dialog_message(MwMessage::GroupsChanged, {});
            }
        });

        menu.exec(ui->tabWidget->tabBar()->mapToGlobal(p));
        return;
    }

    ui->tabWidget->setCurrentIndex(clickedIndex);
    QMenu menu(this);

    const auto clickedGroup = Configs::dataManager->groupsRepo->GetGroup(Configs::dataManager->groupsRepo->GetGroupsTabOrder()[clickedIndex]);

    connect(menu.addAction(tr("Add new Group")), &QAction::triggered, this, [=,this]{
        auto ent = Configs::dataManager->groupsRepo->NewGroup();
        auto dialog = new DialogEditGroup(ent, this);
        const int ret = dialog->exec();
        dialog->deleteLater();

        if (ret == QDialog::Accepted) {
            Configs::dataManager->groupsRepo->AddGroup(ent);
            MW_dialog_message(MwMessage::GroupsChanged, {});
        }
    });
    connect(menu.addAction(tr("Edit selected Group")), &QAction::triggered, this, [=,this]{
        const auto id = Configs::dataManager->groupsRepo->GetGroupsTabOrder()[clickedIndex];
        auto ent = Configs::dataManager->groupsRepo->GetGroup(id);
        auto dialog = new DialogEditGroup(ent, this);
        connect(dialog, &QDialog::finished, this, [=,this] {
            if (dialog->result() == QDialog::Accepted) {
                Configs::dataManager->groupsRepo->Save(ent);
                MW_dialog_message(MwMessage::GroupsChanged, {});
            }
            dialog->deleteLater();
        });
        dialog->show();
    });
    if (Configs::dataManager->groupsRepo->GetAllGroupIds().size() > 1) {
        connect(menu.addAction(tr("Delete selected Group")), &QAction::triggered, this, [=,this] {
            const auto id = Configs::dataManager->groupsRepo->GetGroupsTabOrder()[clickedIndex];
            if (QMessageBox::question(this, tr("Confirmation"), tr("Remove %1?").arg(Configs::dataManager->groupsRepo->GetGroup(id)->name)) ==
                QMessageBox::StandardButton::Yes) {
                if (running != nullptr) {
                    if (running->gid == id) profile_stop(false, true, false);
                }
                Configs::dataManager->groupsRepo->DeleteGroup(id);
                MW_dialog_message(MwMessage::GroupsChanged, {});
            }
        });
    }
    if (clickedGroup != nullptr && !clickedGroup->url.isEmpty()) {
        connect(menu.addAction(tr("Update subscription")), &QAction::triggered, this, [=,this]{
            const auto id = Configs::dataManager->groupsRepo->GetGroupsTabOrder()[clickedIndex];
            auto group = Configs::dataManager->groupsRepo->GetGroup(id);
            if (group->url.isEmpty()) return;
            if (mw_sub_updating) return;
            mw_sub_updating = true;
            Subscription::updater()->RefreshGroup(group->id, [&] { mw_sub_updating = false; }, true);
        });
    }
    if (clickedGroup != nullptr) {
        connect(menu.addAction(tr("Url Test selected Group")), &QAction::triggered, this, [=,this]{
            testRunner->runUrlTests(clickedGroup->Profiles());
        });
        connect(menu.addAction(tr("Speed Test selected Group")), &QAction::triggered, this, [=,this]{
            testRunner->runSpeedTests(clickedGroup->Profiles());
        });
    }
    menu.exec(ui->tabWidget->tabBar()->mapToGlobal(p));
}
