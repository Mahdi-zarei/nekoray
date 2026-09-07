#include "include/ui/group/dialog_edit_group.h"

#include "include/ui/mainwindow_interface.h"

#include <QClipboard>
#include <QHash>
#include <QStringListModel>
#include <QCompleter>
#include <QTimer>
#include <QAbstractItemView>

#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"

#define ADJUST_SIZE runOnThread([=,this] { adjustSize(); adjustPosition(mainwindow); }, this);

DialogEditGroup::DialogEditGroup(const std::shared_ptr<Configs::Group> &ent, QWidget *parent) : QDialog(parent), ui(new Ui::DialogEditGroup) {
    ui->setupUi(this);
    this->ent = ent;

    connect(ui->type, &QComboBox::currentIndexChanged, this, [=,this](int index) {
        ui->cat_sub->setHidden(index == 0);
        ADJUST_SIZE
    });

    ui->name->setText(ent->name);
    ui->auto_clear_unavailable->setChecked(ent->auto_clear_unavailable);
    ui->skip_auto_update->setChecked(ent->skip_auto_update);
    ui->url->setText(ent->url);

    ui->sub_update_interval->setRange(0, 720);
    ui->sub_update_interval->setValue(ent->sub_update_interval);
    ui->sub_update_interval->setSpecialValueText(tr("Default (Auto / Global)"));
    ui->sub_update_interval->setSuffix(tr(" hours"));

    ui->type->setCurrentIndex(ent->url.isEmpty() ? 0 : 1);
    ui->type->currentIndexChanged(ui->type->currentIndex());
    ui->cat_share->setVisible(false);
    if (Configs::dataManager->profilesRepo->GetProfile(ent->front_proxy_id) == nullptr) {
        ent->front_proxy_id = -1;
        Configs::dataManager->groupsRepo->Save(ent);
    }
    if (Configs::dataManager->profilesRepo->GetProfile(ent->landing_proxy_id) == nullptr) {
        ent->landing_proxy_id = -1;
        Configs::dataManager->groupsRepo->Save(ent);
    }
    CACHE.front_proxy = ent->front_proxy_id;
    LANDING.landing_proxy = ent->landing_proxy_id;

    if (ent->id >= 0) { // already a group
        ui->type->setDisabled(true);
        if (!ent->Profiles().isEmpty()) {
            ui->cat_share->setVisible(true);
        }
    }

    auto proxyListRaw = Configs::dataManager->profilesRepo->GetAllProfileIDNameMapped();
    QHash<int, QString> idToName;
    idToName.reserve(proxyListRaw.size());
    for (const auto& [id, name] : proxyListRaw) idToName.insert(id, name);
    QList<std::pair<int, QString>> proxyList;
    // An auto selector moves server on its own, so it cannot hold a fixed front/landing slot.
    const auto selectorIDsRaw = Configs::dataManager->profilesRepo->GetProfileIdsByType("autoselector");
    const QSet<int> selectorIDs(selectorIDsRaw.begin(), selectorIDsRaw.end());
    auto groupIDs = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
    for (auto groupID: groupIDs) {
        auto group = Configs::dataManager->groupsRepo->GetGroup(groupID);
        if (!group) continue;
        const QString prefix = "[" + group->name + "] ";
        for (int profileID : group->profiles) {
            if (selectorIDs.contains(profileID)) continue;
            auto it = idToName.constFind(profileID);
            if (it == idToName.constEnd()) continue;
            proxyList << std::make_pair(profileID, prefix + it.value());
        }
    }
    QStringList proxyNameList;
    proxyNameToId.clear();
    proxyNameToId.insert("None", -1);
    ui->front_proxy->setMaxCount(200);
    ui->landing_proxy->setMaxCount(200);
    ui->front_proxy->blockSignals(true);
    ui->landing_proxy->blockSignals(true);
    ui->front_proxy->setUpdatesEnabled(false);
    ui->landing_proxy->setUpdatesEnabled(false);
    ui->front_proxy->addItem("None", QVariant(-1));
    ui->landing_proxy->addItem("None", QVariant(-1));
    const int comboCap = ui->front_proxy->maxCount();
    int comboCount = 1; // "None" already added
    for (const auto&[id, name] : proxyList) {
        proxyNameList.append(name);
        if (!proxyNameToId.contains(name)) {
            proxyNameToId.insert(name, id);
        }
        if (comboCount < comboCap) {
            ui->front_proxy->addItem(name, QVariant(id));
            ui->landing_proxy->addItem(name, QVariant(id));
            ++comboCount;
        }
    }
    ui->front_proxy->blockSignals(false);
    ui->landing_proxy->blockSignals(false);
    ui->front_proxy->setUpdatesEnabled(true);
    ui->landing_proxy->setUpdatesEnabled(true);

    auto proxyCompleterModel = new QStringListModel(proxyNameList, this);

    auto attachDebouncedCompleter = [this](QComboBox* combo, QCompleter* completer) {
        auto* lineEdit = combo->lineEdit();
        completer->setWidget(lineEdit);
        auto* debounce = new QTimer(this);
        debounce->setSingleShot(true);
        debounce->setInterval(300);
        connect(debounce, &QTimer::timeout, completer, [completer, lineEdit] {
            const QString text = lineEdit->text();
            if (text.isEmpty()) return;
            completer->setCompletionPrefix(text);
            completer->complete();
        });
        connect(lineEdit, &QLineEdit::textEdited, debounce, [debounce, completer](const QString& text) {
            if (text.isEmpty()) {
                debounce->stop();
                completer->popup()->hide();
                return;
            }
            debounce->start();
        });
        connect(completer, qOverload<const QString&>(&QCompleter::activated),
                         lineEdit, [combo, lineEdit](const QString& text) {
            lineEdit->setText(text);
            const int index = combo->findText(text, Qt::MatchExactly);
            if (index >= 0) {
                combo->setCurrentIndex(index);
            }
        });
    };

    ui->front_proxy->setEditable(true);
    ui->front_proxy->setCurrentText(get_proxy_name(CACHE.front_proxy));
    ui->front_proxy->setInsertPolicy(QComboBox::NoInsert);
    auto frontCompleter = new QCompleter(this);
    frontCompleter->setModel(proxyCompleterModel);
    frontCompleter->setCompletionMode(QCompleter::PopupCompletion);
    frontCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    frontCompleter->setFilterMode(Qt::MatchContains);
    ui->front_proxy->setCompleter(nullptr);
    attachDebouncedCompleter(ui->front_proxy, frontCompleter);
    connect(ui->front_proxy, &QComboBox::currentIndexChanged, this, [=,this](int index){
        CACHE.front_proxy = ui->front_proxy->itemData(index).value<int>();
    });

    ui->landing_proxy->setEditable(true);
    ui->landing_proxy->setCurrentText(get_proxy_name(LANDING.landing_proxy));
    ui->landing_proxy->setInsertPolicy(QComboBox::NoInsert);
    auto landingCompleter = new QCompleter(this);
    landingCompleter->setModel(proxyCompleterModel);
    landingCompleter->setCompletionMode(QCompleter::PopupCompletion);
    landingCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    landingCompleter->setFilterMode(Qt::MatchContains);
    ui->landing_proxy->setCompleter(nullptr);
    attachDebouncedCompleter(ui->landing_proxy, landingCompleter);
    connect(ui->landing_proxy, &QComboBox::currentIndexChanged, this, [=,this](int index){
        LANDING.landing_proxy = ui->landing_proxy->itemData(index).value<int>();
    });

    connect(ui->copy_links, &QPushButton::clicked, this, [=] {
        QStringList links;
        auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(ent->Profiles());
        for (const auto &profile: profiles) {
            links += profile->outbound->ExportToLink();
        }
        QApplication::clipboard()->setText(links.join("\n"));
        MessageBoxInfo(software_name, tr("Copied"));
    });
    connect(ui->copy_links_nkr, &QPushButton::clicked, this, [=] {
        QStringList links;
        auto profiles = Configs::dataManager->profilesRepo->GetProfileBatch(ent->Profiles());
        for (const auto &profile: profiles) {
            links += profile->outbound->ExportJsonLink();
        }
        QApplication::clipboard()->setText(links.join("\n"));
        MessageBoxInfo(software_name, tr("Copied"));
    });

    ui->name->setFocus();

    ADJUST_SIZE
}

DialogEditGroup::~DialogEditGroup() {
    delete ui;
}

int DialogEditGroup::resolve_proxy_selection(QComboBox *combo, int fallback) const {
    const QString text = combo->currentText().trimmed();
    if (text.isEmpty() || text == "None") {
        return -1;
    }

    const int index = combo->findText(text, Qt::MatchExactly);
    if (index >= 0) {
        return combo->itemData(index).value<int>();
    }

    auto it = proxyNameToId.constFind(text);
    if (it != proxyNameToId.constEnd()) {
        return it.value();
    }

    return fallback;
}

void DialogEditGroup::accept() {
    if (ent->id >= 0) { // already a group
        if (!ent->url.isEmpty() && ui->url->text().isEmpty()) {
            MessageBoxWarning(tr("Warning"), tr("Please input URL"));
            return;
        }
    }
    ent->name = ui->name->text().trimmed();
    ent->auto_clear_unavailable = ui->auto_clear_unavailable->isChecked();
    ent->url = ui->url->text().trimmed();
    ent->skip_auto_update = ui->skip_auto_update->isChecked();
    ent->sub_update_interval = ui->sub_update_interval->value();
    ent->front_proxy_id = resolve_proxy_selection(ui->front_proxy, CACHE.front_proxy);
    ent->landing_proxy_id = resolve_proxy_selection(ui->landing_proxy, LANDING.landing_proxy);
    QDialog::accept();
}

QString DialogEditGroup::get_proxy_name(int id) {
    if (id == -1) return "None";
    if (auto profile = Configs::dataManager->profilesRepo->GetProfile(id)) {
        auto group = Configs::dataManager->groupsRepo->GetGroup(profile->gid);
        if (group == nullptr) return "INVALID";
        return QString("[" + group->name + "] ") + profile->name;
    }
    return "INVALID";
}
