#include "include/ui/group/GroupItem.h"

#include "include/ui/group/dialog_edit_group.h"
#include "include/global/GuiUtils.hpp"
#include "include/configs/sub/GroupUpdater.hpp"

#include <QMessageBox>

#include "include/database/GroupsRepo.h"
#include "include/ui/mainwindow.h"

QString ParseSubInfo(const QString &info, int updateIntervalHours = 0) {
    auto sub = Configs::ParseSubUserInfo(info);
    if (!sub.valid) return "";

    QStringList parts;
    QString usedStr = ReadableSize(sub.used());
    QString totalStr = (sub.total > 0) ? ReadableSize(sub.total) : QString::fromUtf8("\u221E");
    QString remainStr = (sub.total > 0) ? ReadableSize(sub.remaining()) : QString::fromUtf8("\u221E");

    parts << QObject::tr("Used: %1 / %2 (%3 remain)").arg(usedStr, totalStr, remainStr);

    if (sub.expire > 0) {
        qint64 now = QDateTime::currentSecsSinceEpoch();
        if (sub.isExpired()) {
            parts << QObject::tr("Expired (%1)").arg(DisplayTime(sub.expire, QLocale::ShortFormat));
        } else {
            qint64 diffSecs = sub.expire - now;
            if (diffSecs < 86400) {
                qint64 diffHours = std::max<qint64>(1, diffSecs / 3600);
                parts << QObject::tr("Expires in %1h (%2)").arg(diffHours).arg(DisplayTime(sub.expire, QLocale::ShortFormat));
            } else {
                qint64 diffDays = diffSecs / 86400;
                parts << QObject::tr("Expires in %1d (%2)").arg(diffDays).arg(DisplayTime(sub.expire, QLocale::ShortFormat));
            }
        }
    }

    if (updateIntervalHours > 0) {
        parts << QObject::tr("Auto-update: %1h").arg(updateIntervalHours);
    }

    return parts.join(" | ");
}

GroupItem::GroupItem(QWidget *parent, const std::shared_ptr<Configs::Group> &ent, QListWidgetItem *item) : QWidget(parent), ui(new Ui::GroupItem) {
    ui->setupUi(this);
    this->setLayoutDirection(Qt::LeftToRight);

    this->parentWindow = parent;
    this->ent = ent;
    this->item = item;
    if (ent == nullptr) return;

    connect(this, &GroupItem::edit_clicked, this, &GroupItem::on_edit_clicked);
    connect(Subscription::updater(), &Subscription::GroupUpdater::asyncUpdateCallback, this, [=,this](int gid) { if (gid == this->ent->id) refresh_data(); });

    refresh_data();
}

GroupItem::~GroupItem() {
    delete ui;
}

void GroupItem::refresh_data() {
    ui->name->setText(ent->name);

    auto type = ent->url.isEmpty() ? tr("Basic") : tr("Subscription");
    if (ent->archive) type = tr("Archive") + " " + type;
    type += " (" + Int2String(ent->Profiles().length()) + ")";
    ui->type->setText(type);

    if (ent->url.isEmpty()) {
        ui->url->hide();
        ui->subinfo->hide();
        ui->update_sub->hide();
    } else {
        ui->url->setText(ent->url);
        QStringList info;
        if (ent->sub_last_update != 0) {
            info << tr("Last update: %1").arg(DisplayTime(ent->sub_last_update, QLocale::ShortFormat));
        }
        auto subinfo = ParseSubInfo(ent->info, ent->sub_update_interval);
        if (!subinfo.isEmpty()) {
            info << subinfo;
        }
        if (info.isEmpty()) {
            ui->subinfo->hide();
        } else {
            ui->subinfo->show();
            ui->subinfo->setText(info.join(" | "));
        }
    }
    runOnThread(
        [=,this] {
            adjustSize();
            item->setSizeHint(sizeHint());
            dynamic_cast<QWidget *>(parent())->adjustSize();
        },
        this);
}

void GroupItem::on_update_sub_clicked() {
    Subscription::updater()->RefreshGroup(ent->id, nullptr, true);
}

void GroupItem::on_edit_clicked() {
    auto dialog = new DialogEditGroup(ent, parentWindow);
    connect(dialog, &QDialog::finished, this, [=,this] {
        if (dialog->result() == QDialog::Accepted) {
            Configs::dataManager->groupsRepo->Save(ent);
            refresh_data();
            MW_dialog_message(MwMessage::GroupsChanged, {});
        }
        dialog->deleteLater();
    });
    dialog->show();
}

void GroupItem::on_remove_clicked() {
    if (Configs::dataManager->groupsRepo->GetAllGroupIds().size() <= 1) return;
    if (QMessageBox::question(this, tr("Confirmation"), tr("Remove %1?").arg(ent->name)) ==
        QMessageBox::StandardButton::Yes) {
        GetMainWindow()->profile_stop(false, true, false);
        Configs::dataManager->groupsRepo->DeleteGroup(ent->id);
        MW_dialog_message(MwMessage::GroupsChanged, {});
        delete item;
    }
}
