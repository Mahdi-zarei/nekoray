#include "include/ui/setting/RouteItem.h"

#include "include/configs/generate.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/GroupsRepo.h"
#include "include/global/Configs.hpp"
#include "include/configs/sub/RouteUpdater.hpp"
#include "include/ui/setting/ThemeManager.hpp"

#include <srslist.h>

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSet>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QGridLayout>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTextEdit>

void adjustComboBoxWidth(const QComboBox *comboBox) {
    int maxWidth = 0;

    for (int i = 0; i < comboBox->count(); ++i) {
        QFontMetrics fontMetrics(comboBox->font());
        int itemWidth = fontMetrics.horizontalAdvance(comboBox->itemText(i));
        maxWidth = qMax(maxWidth, itemWidth);
    }

    maxWidth += 30;
    comboBox->view()->setMinimumWidth(maxWidth);
}

QString get_outbound_name(int id) {
    if (id == -1) return "proxy";
    if (id == -2) return "direct";
    if (id == Configs::warpBypassID) return "warp-bypass";
    if (auto profile = Configs::dataManager->profilesRepo->GetProfile(id)) return profile->name;
    return "INVALID OUTBOUND";
}

RouteItem::RouteItem(QWidget *parent, const std::shared_ptr<Configs::RouteProfile>& routeChain)
    : QDialog(parent), ui(new Ui::RouteItem) {
    ui->setupUi(this);

    chain = std::make_shared<Configs::RouteProfile>(*routeChain);

    if (chain->IsEmpty()) {
        auto routeItem = std::make_shared<Configs::RouteRule>();
        routeItem->name = "dns-hijack";
        routeItem->protocol = "dns";
        routeItem->action = "hijack-dns";
        chain->Rules << routeItem;
    }

    std::map<QString, int> valueMap;
    for (auto &item: chain->Rules) {
        auto baseName = item->name;
        int randPart;
        if (baseName == "") {
            randPart = int(GetRandomUint64() % 1000);
            baseName = "rule_" + Int2String(randPart);
            lastNum = std::max(lastNum, randPart);
        }
        while (true) {
            valueMap[baseName]++;
            if (valueMap[baseName] > 1) {
                valueMap[baseName]--;
                randPart = int(GetRandomUint64() % 1000);
                baseName = "rule_" + Int2String(randPart);
                lastNum = std::max(lastNum, randPart);
                continue;
            }
            item->name = baseName;
            break;
        }
        ui->route_items->addItem(item->name);
    }

    outbounds = {"proxy", "direct", "warp-bypass"};
    outboundMap[0] = -1;
    outboundMap[1] = -2;
    outboundMap[2] = Configs::warpBypassID;
    auto proxyListRaw = Configs::dataManager->profilesRepo->GetAllProfileIDNameMapped();
    QMap<int, QString> idToName;
    for (const auto& [id, name] : proxyListRaw) idToName.insert(id, name);
    auto groupIDs = Configs::dataManager->groupsRepo->GetGroupsTabOrder();
    for (auto groupID : groupIDs) {
        auto group = Configs::dataManager->groupsRepo->GetGroup(groupID);
        if (!group) continue;
        for (int profileID : group->profiles) {
            if (!idToName.contains(profileID)) continue;
            outboundMap[outboundMap.size()] = profileID;
            outbounds << QString("[" + group->name + "] ") + idToName[profileID];
        }
    }

    for (const auto& item : ruleSetList) {
        geo_items.append(QString::fromUtf8(item.first.data(), item.first.size()));
    }

    ui->route_name->setText(chain->name);
    ui->rule_preview->setReadOnly(true);
    ui->rule_attr_tabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    ui->rule_attr_tabs->setTabsClosable(true);
    connect(ui->rule_attr_tabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (currentIndex < 0) return;
        const QString tabText = ui->rule_attr_tabs->tabText(index);
        if (tabText == QStringLiteral("+")) return;
        applyAttributeVisibilityChange(tabText, false);
    });

    connect(ui->rule_attr_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (currentIndex >= 0 && !currentRuleIsEndpoint() && index >= 0 && index < ui->rule_attr_tabs->count())
            chain->Rules[currentIndex]->uiActiveAttributeTabLabel = ui->rule_attr_tabs->tabText(index);
        if (QWidget* w = ui->rule_attr_tabs->currentWidget()) {
            w->updateGeometry();
            w->adjustSize();
        }
        ui->rule_attr_tabs->updateGeometry();
    });

    ensurePlusTabBuiltOnce();

    ui->def_out->setCurrentText(Configs::outboundIDToString(chain->defaultOutboundID));

    QStringList ruleItems = {"domain:", "suffix:", "regex:", "keyword:", "ip:", "processName:", "processPath:", "ruleset:"};
    for (const auto& item : ruleSetList) {
        ruleItems.append("ruleset:" + QString::fromUtf8(item.first.data(), item.first.size()));
    }
    simpleDirect = new AutoCompleteTextEdit("", ruleItems, this);
    simpleBlock = new AutoCompleteTextEdit("", ruleItems, this);
    simpleProxy = new AutoCompleteTextEdit("", ruleItems, this);
    simpleWarpBypass = new AutoCompleteTextEdit("", ruleItems, this);

    ui->simple_direct_box->layout()->replaceWidget(ui->simple_direct, simpleDirect);
    ui->simple_block_box->layout()->replaceWidget(ui->simple_block, simpleBlock);
    ui->simple_proxy_box->layout()->replaceWidget(ui->simple_proxy, simpleProxy);
    ui->simple_warpbypass_box->layout()->replaceWidget(ui->simple_warpbypass, simpleWarpBypass);
    ui->simple_direct->hide();
    ui->simple_block->hide();
    ui->simple_proxy->hide();
    ui->simple_warpbypass->hide();

    simpleDirect->setPlainText(chain->GetSimpleRules(Configs::bypass));
    simpleBlock->setPlainText(chain->GetSimpleRules(Configs::block));
    simpleProxy->setPlainText(chain->GetSimpleRules(Configs::proxy));
    simpleWarpBypass->setPlainText(chain->GetSimpleRules(Configs::warpBypass));

    // Dispatch on page identity: literal tab indices misroute as soon as a page is inserted.
    lastTabPage = ui->tabWidget->currentWidget();
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, [=, this]() {
        QWidget* from = lastTabPage;
        lastTabPage = ui->tabWidget->currentWidget();

        if (from == ui->tab_2) {
            QString res;
            res += chain->UpdateSimpleRules(simpleDirect->toPlainText(), Configs::bypass);
            res += chain->UpdateSimpleRules(simpleBlock->toPlainText(), Configs::block);
            res += chain->UpdateSimpleRules(simpleProxy->toPlainText(), Configs::proxy);
            res += chain->UpdateSimpleRules(simpleWarpBypass->toPlainText(), Configs::warpBypass);
            if (!res.isEmpty()) {
                runOnUiThread([=] {
                    MessageBoxWarning(tr("Invalid rules"), tr("Some rules could not be added:\n") + res);
                });
            }
        }
        if (from == ui->tab_2 || from == ui->tab) {
            if (currentIndex >= 0)
                persistCurrentRuleAttrTabLabel();
        }
        // UpdateSimpleRules rebuilds and filters chain->Rules, so the selection is stale.
        if (from == ui->tab_2) currentIndex = -1;

        syncEndpointRules();

        if (lastTabPage == ui->tab_2) {
            simpleDirect->setPlainText(chain->GetSimpleRules(Configs::bypass));
            simpleBlock->setPlainText(chain->GetSimpleRules(Configs::block));
            simpleProxy->setPlainText(chain->GetSimpleRules(Configs::proxy));
            simpleWarpBypass->setPlainText(chain->GetSimpleRules(Configs::warpBypass));
        }
    });

    connect(ui->howtouse_button, &QPushButton::clicked, this, [=]() {
        runOnUiThread([=] {
            MessageBoxInfo(tr("Simple rule manual"), Configs::Information::SimpleRuleInfo);
        });
    });

    connect(ui->rule_name, &QLineEdit::textChanged, this, [=, this](const QString& text) {
        if (currentIndex == -1 || currentRuleIsEndpoint()) return;
        chain->Rules[currentIndex]->name = QString(text);
        auto ruleNameCursorPosition = ui->rule_name->cursorPosition();
        updateRouteItemsView();
        ui->rule_name->setCursorPosition(ruleNameCursorPosition);
    });

    connect(ui->route_items, &QListWidget::currentRowChanged, this, [=, this](const int idx) {
        if (idx == -1) {
            if (currentIndex >= 0)
                persistCurrentRuleAttrTabLabel();
            currentIndex = -1;
            updateRuleSection();
            return;
        }
        if (currentIndex >= 0)
            persistCurrentRuleAttrTabLabel();
        currentIndex = idx;
        updateRuleSection();
    });

    connect(ui->rule_action_combo, &QComboBox::currentTextChanged, this, [=, this](const QString& text) {
        if (currentIndex < 0 || currentRuleIsEndpoint()) return;
        chain->Rules[currentIndex]->set_field_value(QStringLiteral("action"), {text});
        updateRulePreview();
    });

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [=, this] {
        accept();
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, [=, this] {
       QDialog::reject();
    });

    deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);

    connect(deleteShortcut, &QShortcut::activated, this, [=, this] {
        // The shortcut is dialog-wide, so on the Endpoints page it must not reach the rule list.
        if (ui->tabWidget->currentWidget() == ui->endpointsTab) {
            const int row = ui->endpointList->currentRow();
            if (row >= 0) removeEndpointRow(ui->endpointList->item(row)->data(Qt::UserRole).toInt());
            return;
        }
        on_delete_route_item_clicked();
    });

    setupRemoteSection();
    setupEndpointsSection();

    updateRuleSection();
    const auto *scr = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    if (scr != nullptr) resize(sizeHint().boundedTo(scr->availableGeometry().size()));
}

RouteItem::~RouteItem() {
    delete ui;
}

void RouteItem::setupRemoteSection() {
    if (!chain->isRemote) {
        ui->remoteBox->hide();
        return;
    }
    ui->remoteUrlEdit->setText(chain->remoteURL);
    ui->autoUpdateCheck->setChecked(chain->autoUpdate);
    connect(ui->remotePreviewBtn, &QPushButton::clicked, this, [this] { fetchRemote(false); });
    connect(ui->remoteFetchBtn, &QPushButton::clicked, this, [this] { fetchRemote(true); });
}

void RouteItem::fetchRemote(bool applyToChain) {
    const QString url = ui->remoteUrlEdit->text().trimmed();
    if (!url.startsWith("http://", Qt::CaseInsensitive) && !url.startsWith("https://", Qt::CaseInsensitive)) {
        MessageBoxWarning(tr("Invalid URL"), tr("Enter a valid http(s) URL first."));
        return;
    }
    if (applyToChain && !chain->IsEmpty()) {
        if (QMessageBox::question(this, tr("Fetch from remote"),
                                  tr("This will replace the current rules with the ones fetched from the URL. Continue?"))
            != QMessageBox::StandardButton::Yes) {
            return;
        }
    }

    ui->remotePreviewBtn->setEnabled(false);
    ui->remoteFetchBtn->setEnabled(false);
    const QString origFetch = ui->remoteFetchBtn->text();
    const QString origPreview = ui->remotePreviewBtn->text();
    (applyToChain ? ui->remoteFetchBtn : ui->remotePreviewBtn)->setText(tr("Fetching..."));

    // UpdateProfile fills an empty name from the remote one; this stops it overwriting what the user typed.
    const QString currentName = ui->route_name->text();

    runOnNewThread([=, this] {
        auto target = applyToChain ? chain : std::make_shared<Configs::RouteProfile>(*chain);
        target->isRemote = true;
        target->remoteURL = url;
        target->name = currentName;
        QString warnings;
        const QString err = RouteUpdate::UpdateProfile(target, &warnings);
        runOnUiThread([=, this] {
            ui->remotePreviewBtn->setEnabled(true);
            ui->remoteFetchBtn->setEnabled(true);
            ui->remoteFetchBtn->setText(origFetch);
            ui->remotePreviewBtn->setText(origPreview);
            if (!err.isEmpty()) {
                MessageBoxWarning(tr("Could not fetch routing profile"), err);
                return;
            }
            if (applyToChain) {
                reloadRuleViewsFromChain();
                const QString msg = tr("Loaded %1 rule(s) from the remote URL.").arg(target->Rules.size());
                if (warnings.isEmpty()) MessageBoxInfo(tr("Fetched"), msg);
                else MessageBoxInfo(tr("Fetched with warnings"), msg + "\n\n" + warnings);
            } else {
                auto* dlg = new QDialog(this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->setWindowTitle(tr("Remote routing profile preview"));
                auto* lay = new QVBoxLayout(dlg);
                auto* header = new QLabel(tr("%1 — %2 rule(s)").arg(target->name.isEmpty() ? tr("(unnamed)") : target->name)
                                              .arg(target->Rules.size()), dlg);
                lay->addWidget(header);
                if (!warnings.isEmpty()) {
                    auto* warn = new QLabel(warnings, dlg);
                    warn->setStyleSheet(QStringLiteral("color: %1;").arg(themeManager()->tokens.danger.name()));
                    warn->setWordWrap(true);
                    lay->addWidget(warn);
                }
                auto* view = new QPlainTextEdit(dlg);
                view->setReadOnly(true);
                view->setPlainText(QJsonObject2QString(target->ToShareObject(), false));
                view->setLineWrapMode(QPlainTextEdit::NoWrap);
                lay->addWidget(view, 1);
                auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal, dlg);
                connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
                connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
                lay->addWidget(buttons);
                dlg->resize(560, 460);
                dlg->show();
            }
        });
    });
}

static QList<QPair<QString, int>> routeItemEndpointCandidates() {
    QList<QPair<QString, int>> candidates;
    QList<int> ids = Configs::dataManager->profilesRepo->GetProfileIdsByType("openvpn");
    ids += Configs::dataManager->profilesRepo->GetProfileIdsByType("openconnect");
    ids += Configs::dataManager->profilesRepo->GetProfileIdsByType("chain");
    for (const int id : ids) {
        const auto ent = Configs::dataManager->profilesRepo->GetProfile(id);
        if (ent == nullptr || ent->outbound == nullptr) continue;
        if (!Configs::CanBeAuxEndpoint(ent)) continue;
        candidates.append({ent->outbound->DisplayTypeAndName(), id});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return QString::localeAwareCompare(a.first, b.first) < 0;
    });
    return candidates;
}

static bool routeItemIsEndpointRule(const std::shared_ptr<Configs::RouteRule>& rule) {
    return rule != nullptr && rule->type == Configs::endpointPreferredBy;
}

static QString routeItemEndpointName(int profileId) {
    const auto ent = Configs::dataManager->profilesRepo->GetProfile(profileId);
    if (ent == nullptr || ent->outbound == nullptr) return QString::number(profileId);
    return ent->outbound->DisplayName();
}

static QString routeItemUniqueRuleName(const QString& base, const QSet<QString>& taken) {
    QString name = base;
    for (int n = 2; taken.contains(name); n++) name = base + " (" + Int2String(n) + ")";
    return name;
}

static std::shared_ptr<Configs::RouteRule> routeItemMakeEndpointRule(int profileId) {
    auto rule = std::make_shared<Configs::RouteRule>();
    rule->type = Configs::endpointPreferredBy;
    rule->outboundID = profileId;
    // Nothing on it is user-editable; seeding attribute tabs from it would only offer edits.
    rule->uiAttributeTabsSeeded = true;
    return rule;
}

void RouteItem::setupEndpointsSection() {
    endpointCandidates = routeItemEndpointCandidates();
    QSet<int> listed;
    for (const int profileId : chain->endpointProfileIDs) {
        if (listed.contains(profileId)) continue;
        listed.insert(profileId);
        addEndpointRow(profileId, chain->innerHopEndpointIDs.contains(profileId));
    }

    syncEndpointRules();

    if (endpointCandidates.isEmpty() && ui->endpointList->count() == 0) {
        ui->tabWidget->setTabVisible(ui->tabWidget->indexOf(ui->endpointsTab), false);
        return;
    }

    connect(ui->endpointAddBtn, &QPushButton::clicked, this, [this] {
        const int idx = ui->endpointPicker->currentIndex();
        if (idx < 0) return;
        addEndpointRow(ui->endpointPicker->itemData(idx).toInt(), false);
        refreshEndpointCandidates();
        syncEndpointRules();
    });
    connect(ui->endpointRemoveBtn, &QPushButton::clicked, this, [this] {
        const int row = ui->endpointList->currentRow();
        if (row < 0) return;
        removeEndpointRow(ui->endpointList->item(row)->data(Qt::UserRole).toInt());
    });
    connect(ui->endpointList, &QListWidget::currentRowChanged, this, [this](const int row) {
        ui->endpointRemoveBtn->setEnabled(row >= 0);
    });

    refreshEndpointCandidates();
}

void RouteItem::refreshEndpointCandidates() const {
    QSet<int> used;
    for (int i = 0; i < ui->endpointList->count(); i++)
        used.insert(ui->endpointList->item(i)->data(Qt::UserRole).toInt());

    ui->endpointPicker->clear();
    for (const auto& [label, id] : endpointCandidates) {
        if (used.contains(id)) continue;
        ui->endpointPicker->addItem(label, id);
    }

    const bool canAdd = ui->endpointPicker->count() > 0;
    ui->endpointPicker->setEnabled(canAdd);
    ui->endpointAddBtn->setEnabled(canAdd);
    ui->endpointRemoveBtn->setEnabled(ui->endpointList->currentRow() >= 0);
}

void RouteItem::addEndpointRow(int profileId, bool innerHops) {
    const auto ent = Configs::dataManager->profilesRepo->GetProfile(profileId);
    const bool alive = ent != nullptr && ent->outbound != nullptr;
    const bool hasInnerHops = alive && !Configs::AuxEndpointInnerHops(profileId).isEmpty();

    auto* row = new QListWidgetItem(ui->endpointList);
    row->setData(Qt::UserRole, profileId);
    row->setData(Qt::UserRole + 1, innerHops && hasInnerHops);

    auto* holder = new QWidget(ui->endpointList);
    auto* rowLayout = new QHBoxLayout(holder);
    rowLayout->setContentsMargins(4, 2, 4, 2);
    auto* label = new QLabel(alive ? ent->outbound->DisplayTypeAndName()
                                   : tr("Profile #%1 — deleted, dropped when you save").arg(profileId), holder);
    if (!alive) {
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, QColor(0xc6, 0x28, 0x28));
        label->setPalette(pal);
    }
    rowLayout->addWidget(label);
    rowLayout->addStretch(1);
    if (hasInnerHops) {
        auto* toggle = new QCheckBox(tr("Allow routing to inner hops"), holder);
        toggle->setChecked(innerHops);
        toggle->setToolTip(tr("Give each OpenVPN/OpenConnect hop behind the exit its own rule, so traffic can be routed to it as well."));
        connect(toggle, &QCheckBox::toggled, this, [this, row](const bool on) {
            row->setData(Qt::UserRole + 1, on);
            syncEndpointRules();
        });
        rowLayout->addWidget(toggle);
    }
    row->setSizeHint(holder->sizeHint());
    ui->endpointList->setItemWidget(row, holder);
}

void RouteItem::removeEndpointRow(int profileId) {
    for (int i = 0; i < ui->endpointList->count(); i++) {
        if (ui->endpointList->item(i)->data(Qt::UserRole).toInt() != profileId) continue;
        delete ui->endpointList->takeItem(i);
        break;
    }
    refreshEndpointCandidates();
    syncEndpointRules();
}

QList<int> RouteItem::listedEndpointIDs() const {
    QList<int> ids;
    for (int i = 0; i < ui->endpointList->count(); i++) {
        const int id = ui->endpointList->item(i)->data(Qt::UserRole).toInt();
        if (ids.contains(id)) continue;
        const auto ent = Configs::dataManager->profilesRepo->GetProfile(id);
        if (ent == nullptr || ent->outbound == nullptr) continue;
        ids << id;
    }
    return ids;
}

QList<int> RouteItem::listedInnerHopEndpointIDs() const {
    QList<int> ids;
    const QList<int> listed = listedEndpointIDs();
    for (int i = 0; i < ui->endpointList->count(); i++) {
        const auto* item = ui->endpointList->item(i);
        const int id = item->data(Qt::UserRole).toInt();
        if (!item->data(Qt::UserRole + 1).toBool() || ids.contains(id) || !listed.contains(id)) continue;
        ids << id;
    }
    return ids;
}

int RouteItem::innerHopOwner(int profileId) const {
    for (const int id : listedInnerHopEndpointIDs()) {
        if (Configs::AuxEndpointInnerHops(id).contains(profileId)) return id;
    }
    return -1;
}

void RouteItem::setEndpointRowInnerHops(int profileId, bool innerHops) {
    for (int i = 0; i < ui->endpointList->count(); i++) {
        auto* item = ui->endpointList->item(i);
        if (item->data(Qt::UserRole).toInt() != profileId) continue;
        auto* holder = ui->endpointList->itemWidget(item);
        auto* toggle = holder == nullptr ? nullptr : holder->findChild<QCheckBox*>();
        // Its own toggled handler writes the row data back and re-syncs.
        if (toggle != nullptr && toggle->isChecked() != innerHops) {
            toggle->setChecked(innerHops);
            return;
        }
        item->setData(Qt::UserRole + 1, innerHops);
        syncEndpointRules();
        return;
    }
}

void RouteItem::syncEndpointRules() {
    chain->endpointProfileIDs = listedEndpointIDs();
    chain->innerHopEndpointIDs = listedInnerHopEndpointIDs();
    const QList<int> targets = chain->endpointRuleTargets();
    const auto selected = currentIndex >= 0 && currentIndex < chain->Rules.size()
                              ? chain->Rules[currentIndex]
                              : nullptr;

    QSet<int> paired;
    QSet<QString> names;
    QList<std::shared_ptr<Configs::RouteRule>> kept;
    for (const auto& rule : chain->Rules) {
        if (routeItemIsEndpointRule(rule)) {
            if (!targets.contains(rule->outboundID) || paired.contains(rule->outboundID)) continue;
            paired.insert(rule->outboundID);
        } else {
            names.insert(rule->name);
        }
        kept << rule;
    }
    chain->Rules = kept;

    for (const int id : targets) {
        if (!paired.contains(id)) chain->Rules << routeItemMakeEndpointRule(id);
    }

    for (const auto& rule : chain->Rules) {
        if (!routeItemIsEndpointRule(rule)) continue;
        rule->name = routeItemUniqueRuleName(
            RouteItem::tr("%1 route prefer").arg(routeItemEndpointName(rule->outboundID)), names);
        names.insert(rule->name);
    }

    currentIndex = selected == nullptr ? -1 : static_cast<int>(chain->Rules.indexOf(selected));
    updateRouteItemsView();
    updateRuleSection();
}

bool RouteItem::currentRuleIsEndpoint() const {
    return currentIndex >= 0 && currentIndex < chain->Rules.size()
        && routeItemIsEndpointRule(chain->Rules[currentIndex]);
}

void RouteItem::applyRuleEditLock() {
    const bool managed = currentRuleIsEndpoint();
    ui->rule_managed_note->setVisible(managed);
    ui->rule_name->setEnabled(!managed);
    ui->rule_attr_tabs->setEnabled(!managed);
    if (managed) ui->rule_action_combo->setEnabled(false);
}

void RouteItem::reloadRuleViewsFromChain() {
    currentIndex = -1;
    ui->route_name->setText(chain->name);
    syncEndpointRules();
    simpleDirect->setPlainText(chain->GetSimpleRules(Configs::bypass));
    simpleBlock->setPlainText(chain->GetSimpleRules(Configs::block));
    simpleProxy->setPlainText(chain->GetSimpleRules(Configs::proxy));
    simpleWarpBypass->setPlainText(chain->GetSimpleRules(Configs::warpBypass));
    ui->def_out->setCurrentText(Configs::outboundIDToString(chain->defaultOutboundID));
}

void RouteItem::accept() {
    chain->name = ui->route_name->text().trimmed();

    if (chain->name == "") {
        MessageBoxWarning(tr("Invalid operation"), tr("Cannot create Route Profile with empty name"));
        return;
    }

    if (chain->isRemote) {
        const QString url = ui->remoteUrlEdit->text().trimmed();
        if (url.isEmpty()) {
            MessageBoxWarning(tr("Invalid operation"), tr("Remote routing profiles need a URL."));
            return;
        }
        chain->remoteURL = url;
        chain->autoUpdate = ui->autoUpdateCheck->isChecked();
    }

    QString res;
    res += chain->UpdateSimpleRules(simpleDirect->toPlainText(), Configs::bypass);
    res += chain->UpdateSimpleRules(simpleBlock->toPlainText(), Configs::block);
    res += chain->UpdateSimpleRules(simpleProxy->toPlainText(), Configs::proxy);
    res += chain->UpdateSimpleRules(simpleWarpBypass->toPlainText(), Configs::warpBypass);
    if (!res.isEmpty()) {
        runOnUiThread([=] {
            MessageBoxWarning(tr("Invalid rules"), tr("Some rules could not be added, fix them before saving:\n") + res);
        });
        return;
    }
    chain->FilterEmptyRules();

    // Endpoints whose profile is gone drop out here, and syncEndpointRules() drops their rules with them.
    const int missingEndpoints = static_cast<int>(ui->endpointList->count() - listedEndpointIDs().size());
    syncEndpointRules();

    // A remote profile may be saved before its first fetch; only plain profiles must be non-empty.
    if (!chain->isRemote && chain->IsEmpty()) {
        MessageBoxInfo(tr("Empty Route Profile"), tr("No valid rules are in the profile"));
        return;
    }

    chain->defaultOutboundID = Configs::stringToOutboundID(ui->def_out->currentText());

    if (missingEndpoints > 0) {
        MessageBoxInfo(tr("Endpoints"),
                       tr("%1 endpoint profile(s) no longer exist and were removed from this routing profile.")
                           .arg(missingEndpoints));
    }

    emit settingsChanged(chain);

    QDialog::accept();
}

void RouteItem::updateRouteItemsView() {
    const QSignalBlocker listBlocker(ui->route_items);
    ui->route_items->clear();
    if (chain->IsEmpty()) return;

    for (const auto& item: chain->Rules) {
        ui->route_items->addItem(item->name);
        if (!routeItemIsEndpointRule(item)) continue;
        auto* row = ui->route_items->item(ui->route_items->count() - 1);
        QFont font = row->font();
        font.setItalic(true);
        row->setFont(font);
        row->setToolTip(tr("Endpoint rule: move it to choose where the endpoint claims traffic. Managed by the Endpoints tab."));
    }
    if (currentIndex != -1) ui->route_items->setCurrentRow(currentIndex);
}

void RouteItem::syncRuleActionCombo() {
    if (currentIndex < 0) return;
    ui->rule_action_combo->blockSignals(true);
    ui->rule_action_combo->clear();
    ui->rule_action_combo->addItems(Configs::RouteRule::get_values_for_field(QStringLiteral("action")));
    ui->rule_action_combo->setCurrentText(chain->Rules[currentIndex]->action);
    adjustComboBoxWidth(ui->rule_action_combo);
    auto rule = chain->Rules[currentIndex];
    if (rule->canEditAttr("action")) {
        ui->rule_action_combo->setEnabled(true);
    } else {
        ui->rule_action_combo->setEnabled(false);
    }
    ui->rule_action_combo->blockSignals(false);
}

QWidget* RouteItem::makeAttributeEditorPage(const QString& attr) {
    auto* container = new QWidget;
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(8, 8, 8, 8);
    const auto rule = chain->Rules[currentIndex];
    const bool editable = rule->canEditAttr(attr);

    const auto addDisabled = [editable](QWidget* w) { w->setEnabled(editable); };

    switch (Configs::RouteRule::get_input_type(attr)) {
        case Configs::trufalse: {
            auto* cb = new QComboBox(container);
            cb->addItems({QStringLiteral("false"), QStringLiteral("true")});
            cb->setCurrentText(rule->get_current_value_bool(attr));
            connect(cb, &QComboBox::currentTextChanged, this, [this, attr](const QString& t) {
                if (currentIndex < 0) return;
                chain->Rules[currentIndex]->set_field_value(attr, {t});
                updateRulePreview();
            });
            addDisabled(cb);
            lay->addWidget(cb);
            break;
        }
        case Configs::select: {
            auto* cb = new QComboBox(container);
            if (attr == QStringLiteral("outbound")) {
                cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                cb->addItems(outbounds);
                cb->setCurrentText(get_outbound_name(rule->outboundID));
                connect(cb, &QComboBox::currentTextChanged, this, [this, cb] {
                    if (currentIndex < 0) return;
                    chain->Rules[currentIndex]->set_field_value(QStringLiteral("outbound"),
                        {QString::number(outboundMap[cb->currentIndex()])});
                    updateRulePreview();
                });
            } else {
                cb->addItems(Configs::RouteRule::get_values_for_field(attr));
                const auto cur = rule->get_current_value_string(attr);
                cb->setCurrentText(cur.isEmpty() ? QString() : cur[0]);
                connect(cb, &QComboBox::currentTextChanged, this, [this, attr](const QString& t) {
                    if (currentIndex < 0) return;
                    chain->Rules[currentIndex]->set_field_value(attr, {t});
                    updateRulePreview();
                });
            }
            addDisabled(cb);
            adjustComboBoxWidth(cb);
            lay->addWidget(cb);
            break;
        }
        case Configs::text: {
            if (attr == QStringLiteral("rule_set")) {
                auto* ed = new AutoCompleteTextEdit("", geo_items, container);
                ed->setPlainText(rule->get_current_value_string(attr).join('\n'));
                ed->setMinimumHeight(100);
                ed->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                connect(ed, &QPlainTextEdit::textChanged, this, [this, attr, ed] {
                    if (currentIndex < 0) return;
                    chain->Rules[currentIndex]->set_field_value(attr, ed->toPlainText().split('\n'));
                    updateRulePreview();
                });
                addDisabled(ed);
                lay->addWidget(ed, 1);
            } else {
                auto* te = new QPlainTextEdit(container);
                te->setPlainText(rule->get_current_value_string(attr).join('\n'));
                te->setMinimumHeight(100);
                te->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                connect(te, &QPlainTextEdit::textChanged, this, [this, attr, te] {
                    if (currentIndex < 0) return;
                    chain->Rules[currentIndex]->set_field_value(attr, te->toPlainText().split('\n'));
                    updateRulePreview();
                });
                addDisabled(te);
                lay->addWidget(te, 1);
            }
            break;
        }
    }
    container->setMinimumHeight(100);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    return container;
}

void RouteItem::ensurePlusTabBuiltOnce() {
    if (ruleAttrPlusList) return;

    auto* container = new QWidget;
    auto* lay = new QVBoxLayout(container);
    lay->setContentsMargins(8, 8, 8, 8);

    auto* hint = new QLabel(tr("Check attributes to show as tabs; unchecking clears their values."), container);
    hint->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ruleAttrPlusList = new QListWidget(container);
    ruleAttrPlusList->setMinimumHeight(100);
    ruleAttrPlusList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ruleAttrPlusList->setObjectName(QStringLiteral("route_rule_attr_plus_list"));
    ruleAttrPlusList->viewport()->installEventFilter(this);
    for (const QString& attr : Configs::RouteRule::tab_attributes()) {
        auto* it = new QListWidgetItem(attr);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
        ruleAttrPlusList->addItem(it);
    }
    lay->addWidget(hint);
    lay->addWidget(ruleAttrPlusList);
    container->setMinimumHeight(100);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    ui->rule_attr_tabs->addTab(container, QStringLiteral("+"));
    ui->rule_attr_tabs->tabBar()->setTabButton(ui->rule_attr_tabs->count() - 1, QTabBar::RightSide, nullptr);
}

void RouteItem::removeAllAttributeTabsExceptPlus() {
    ensurePlusTabBuiltOnce();
    while (ui->rule_attr_tabs->count() > 1)
        ui->rule_attr_tabs->removeTab(0);
}

void RouteItem::syncPlusListCheckStatesFromRule() {
    if (!ruleAttrPlusList) return;
    const QSignalBlocker b(ruleAttrPlusList);
    if (currentIndex < 0) {
        ruleAttrPlusList->setEnabled(false);
        for (int i = 0; i < ruleAttrPlusList->count(); ++i)
            ruleAttrPlusList->item(i)->setCheckState(Qt::Unchecked);
        return;
    }
    ruleAttrPlusList->setEnabled(true);
    const auto rule = chain->Rules[currentIndex];
    for (int i = 0; i < ruleAttrPlusList->count(); ++i) {
        auto* it = ruleAttrPlusList->item(i);
        it->setCheckState(rule->uiVisibleAttributes.contains(it->text()) ? Qt::Checked : Qt::Unchecked);
        if (!rule->canEditAttr(it->text())) {
            it->setHidden(true);
        } else {
            it->setHidden(false);
        }
    }
}

void RouteItem::persistCurrentRuleAttrTabLabel() {
    if (currentIndex < 0 || currentRuleIsEndpoint()) return;
    const int idx = ui->rule_attr_tabs->currentIndex();
    if (idx < 0 || idx >= ui->rule_attr_tabs->count()) return;
    chain->Rules[currentIndex]->uiActiveAttributeTabLabel = ui->rule_attr_tabs->tabText(idx);
}

void RouteItem::applyStoredRuleAttrTabSelection() {
    int sel = 0;
    if (currentIndex >= 0) {
        const QString& pref = chain->Rules[currentIndex]->uiActiveAttributeTabLabel;
        if (!pref.isEmpty()) {
            for (int i = 0; i < ui->rule_attr_tabs->count(); ++i) {
                if (ui->rule_attr_tabs->tabText(i) == pref) {
                    sel = i;
                    break;
                }
            }
        }
    }
    ui->rule_attr_tabs->setCurrentIndex(sel);
}

void RouteItem::applyAttributeVisibilityChange(const QString& attr, bool visible) {
    if (currentIndex < 0 || currentRuleIsEndpoint()) return;
    persistCurrentRuleAttrTabLabel();
    auto r = chain->Rules[currentIndex];
    r->uiAttributeTabsSeeded = true;
    if (visible)
        r->uiVisibleAttributes.insert(attr);
    else {
        r->uiVisibleAttributes.remove(attr);
        r->clear_attribute_value(attr);
    }
    rebuildRuleAttributeTabs();
    updateRulePreview();
}

bool RouteItem::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto* vp = qobject_cast<QWidget*>(watched);
        if (vp && vp->parentWidget()) {
            auto* lw = qobject_cast<QListWidget*>(vp->parentWidget());
            if (lw && lw->objectName() == QLatin1String("route_rule_attr_plus_list")) {
                auto* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton) {
                    if (QListWidgetItem* item = lw->itemAt(me->pos())) {
                        lw->setCurrentItem(item);
                        const bool toChecked = (item->checkState() != Qt::Checked);
                        {
                            const QSignalBlocker b(lw);
                            item->setCheckState(toChecked ? Qt::Checked : Qt::Unchecked);
                        }
                        applyAttributeVisibilityChange(item->text(), toChecked);
                        return true;
                    }
                }
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void RouteItem::rebuildRuleAttributeTabs() {
    ensurePlusTabBuiltOnce();

    ui->rule_attr_tabs->blockSignals(true);
    removeAllAttributeTabsExceptPlus();

    if (currentIndex < 0) {
        syncPlusListCheckStatesFromRule();
        applyStoredRuleAttrTabSelection();
        ui->rule_attr_tabs->blockSignals(false);
        return;
    }

    const auto rule = chain->Rules[currentIndex];
    if (!routeItemIsEndpointRule(rule)) {
        for (const QString& attr : Configs::RouteRule::tab_attributes()) {
            if (!rule->uiVisibleAttributes.contains(attr)) continue;
            const int beforePlus = ui->rule_attr_tabs->count() - 1;
            ui->rule_attr_tabs->insertTab(beforePlus, makeAttributeEditorPage(attr), attr);
        }
    }

    syncPlusListCheckStatesFromRule();
    applyStoredRuleAttrTabSelection();

    ui->rule_attr_tabs->blockSignals(false);
}

void RouteItem::updateRuleSection() {
    if (currentIndex < 0) {
        {
            const QSignalBlocker nameBlocker(ui->rule_name);
            ui->rule_name->clear();
        }
        ui->rule_preview->clear();
        ui->rule_action_combo->blockSignals(true);
        ui->rule_action_combo->clear();
        ui->rule_action_combo->blockSignals(false);
        rebuildRuleAttributeTabs();
        applyRuleEditLock();
        return;
    }

    auto rule = chain->Rules[currentIndex];
    {
        const QSignalBlocker nameBlocker(ui->rule_name);
        ui->rule_name->setText(rule->name);
    }
    if (routeItemIsEndpointRule(rule)) {
        updateRulePreview();
        const QSignalBlocker actionBlocker(ui->rule_action_combo);
        ui->rule_action_combo->clear();
        rebuildRuleAttributeTabs();
        applyRuleEditLock();
        return;
    }
    rule->ensure_ui_visible_attribute_tabs_seeded();
    syncRuleActionCombo();
    rebuildRuleAttributeTabs();
    applyRuleEditLock();
    updateRulePreview();
}

void RouteItem::updateRulePreview() {
    if (currentIndex == -1) return;
    // The endpoint rule is a position marker; its gate is generated at build time.
    if (currentRuleIsEndpoint()) {
        ui->rule_preview->setPlainText(
            tr("This rule installs a 'preferred by' rule so that the networks advertised by the "
               "endpoint %1 get routed into the endpoint tunnel.")
                .arg(routeItemEndpointName(chain->Rules[currentIndex]->outboundID)));
        return;
    }

    ui->rule_preview->setPlainText(QJsonObject2QString(chain->Rules[currentIndex]->get_rule_json(true), false));
}

void RouteItem::on_new_route_item_clicked() {
    auto routeItem = std::make_shared<Configs::RouteRule>();
    routeItem->name = "rule_" + Int2String(++lastNum);
    chain->Rules << routeItem;
    currentIndex = chain->Rules.size() - 1;

    updateRouteItemsView();
    updateRuleSection();
}

void RouteItem::on_moveup_route_item_clicked() {
    if (currentIndex == -1 || currentIndex == 0) return;
    auto curr = chain->Rules[currentIndex];
    chain->Rules[currentIndex] = chain->Rules[currentIndex - 1];
    chain->Rules[currentIndex - 1] = curr;
    currentIndex--;
    updateRouteItemsView();
}

void RouteItem::on_movedown_route_item_clicked() {
    if (currentIndex == -1 || currentIndex == chain->Rules.size() - 1) return;
    auto curr = chain->Rules[currentIndex];
    chain->Rules[currentIndex] = chain->Rules[currentIndex + 1];
    chain->Rules[currentIndex + 1] = curr;
    currentIndex++;
    updateRouteItemsView();
}

void RouteItem::on_delete_route_item_clicked() {
    if (currentIndex == -1) return;
    if (currentRuleIsEndpoint()) {
        const int endpointID = chain->Rules[currentIndex]->outboundID;
        if (const int owner = innerHopOwner(endpointID); owner >= 0) {
            if (QMessageBox::question(this, tr("Endpoint rule"),
                                      tr("This rule belongs to \"%1\", an inner hop of the endpoint \"%2\".\n\n"
                                         "Stop routing to that endpoint's inner hops?")
                                          .arg(routeItemEndpointName(endpointID), routeItemEndpointName(owner)))
                != QMessageBox::StandardButton::Yes) {
                return;
            }
            setEndpointRowInnerHops(owner, false);
            return;
        }
        if (QMessageBox::question(this, tr("Endpoint rule"),
                                  tr("This rule belongs to the endpoint \"%1\" and cannot be deleted on its own.\n\n"
                                     "Remove that endpoint from this routing profile as well?")
                                      .arg(routeItemEndpointName(endpointID)))
            != QMessageBox::StandardButton::Yes) {
            return;
        }
        removeEndpointRow(endpointID);
        return;
    }
    chain->Rules.removeAt(currentIndex);
    if (chain->Rules.empty()) currentIndex = -1;
    else {
        currentIndex--;
        if (currentIndex == -1) currentIndex = 0;
    }
    updateRouteItemsView();
    updateRuleSection();
}
