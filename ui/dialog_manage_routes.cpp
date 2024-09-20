#include "dialog_manage_routes.h"
#include "ui_dialog_manage_routes.h"
#include "db/Database.hpp"

#include "3rdparty/qv2ray/v2/ui/widgets/editors/w_JsonEditor.hpp"
#include "main/GuiUtils.hpp"
#include "fmt/Preset.hpp"

#include <QFile>
#include <QMessageBox>
#include <QShortcut>

void DialogManageRoutes::reloadProfileItems() {
    if (chainList.empty()) {
        MessageBoxWarning("Invalid state", "The list of routing profiles is empty, this should be an unreachable state, crashes may occur now");
        return;
    }

    QSignalBlocker blocker = QSignalBlocker(ui->route_prof); // apparently the currentIndexChanged will make us crash if we clear the QComboBox
    ui->route_prof->clear();

    ui->route_profiles->clear();
    bool selectedChainGone = true;
    int i=0;
    for (const auto &item: chainList) {
        ui->route_prof->addItem(item->name);
        ui->route_profiles->addItem(item->name);
        if (item->id == currentRouteProfileID) {
            ui->route_prof->setCurrentIndex(i);
            selectedChainGone=false;
        }
        i++;
    }
    if (selectedChainGone) {
        currentRouteProfileID=chainList[0]->id;
        ui->route_prof->setCurrentIndex(0);
    }
    blocker.unblock();
}

void DialogManageRoutes::set_dns_hijack_enability(const bool enable) const {
    ui->dnshijack_listenaddr->setEnabled(enable);
    ui->dnshijack_listenport->setEnabled(enable);
    ui->dnshijack_rules->setEnabled(enable);
    ui->dnshijack_v4resp->setEnabled(enable);
    ui->dnshijack_v6resp->setEnabled(enable);
}

bool DialogManageRoutes::validate_dns_rules(const QString &rawString) {
    auto rules = rawString.split("\n");
    for (const auto& rule : rules) {
        if (!rule.trimmed().isEmpty() && !rule.startsWith("ruleset:") && !rule.startsWith("domain:") && !rule.startsWith("suffix:") && !rule.startsWith("regex:")) return false;
    }
    return true;
}

DialogManageRoutes::DialogManageRoutes(QWidget *parent) : QDialog(parent), ui(new Ui::DialogManageRoutes) {
    ui->setupUi(this);
    auto profiles = NekoGui::profileManager->routes;
    for (const auto &item: profiles) {
        chainList << item.second;
    }
    if (chainList.empty()) {
        auto defaultChain = NekoGui::RoutingChain::GetDefaultChain();
        NekoGui::profileManager->AddRouteChain(defaultChain);
        chainList.append(defaultChain);
    }
    currentRouteProfileID = NekoGui::dataStore->routing->current_route_id;
    if (currentRouteProfileID < 0) currentRouteProfileID = chainList[0]->id;

    QStringList qsValue = {""};
    QString dnsHelpDocumentUrl;

    ui->default_out->setCurrentText(NekoGui::dataStore->routing->def_outbound);
    ui->outbound_domain_strategy->addItems(Preset::SingBox::DomainStrategy);
    ui->domainStrategyCombo->addItems(Preset::SingBox::DomainStrategy);
    qsValue += QString("prefer_ipv4 prefer_ipv6 ipv4_only ipv6_only").split(" ");
    ui->dns_object->setPlaceholderText(DecodeB64IfValid("ewogICJzZXJ2ZXJzIjogW10sCiAgInJ1bGVzIjogW10sCiAgImZpbmFsIjogIiIsCiAgInN0cmF0ZWd5IjogIiIsCiAgImRpc2FibGVfY2FjaGUiOiBmYWxzZSwKICAiZGlzYWJsZV9leHBpcmUiOiBmYWxzZSwKICAiaW5kZXBlbmRlbnRfY2FjaGUiOiBmYWxzZSwKICAicmV2ZXJzZV9tYXBwaW5nIjogZmFsc2UsCiAgImZha2VpcCI6IHt9Cn0="));
    dnsHelpDocumentUrl = "https://sing-box.sagernet.org/configuration/dns/";

    ui->direct_dns_strategy->addItems(qsValue);
    ui->remote_dns_strategy->addItems(qsValue);
    ui->enable_fakeip->setChecked(NekoGui::dataStore->fake_dns);
    //
    connect(ui->use_dns_object, &QCheckBox::stateChanged, this, [=](int state) {
        auto useDNSObject = state == Qt::Checked;
        ui->simple_dns_box->setDisabled(useDNSObject);
        ui->dns_object->setDisabled(!useDNSObject);
    });
    ui->use_dns_object->stateChanged(Qt::Unchecked); // uncheck to uncheck
    connect(ui->dns_document, &QPushButton::clicked, this, [=] {
        MessageBoxInfo("DNS", dnsHelpDocumentUrl);
    });
    connect(ui->format_dns_object, &QPushButton::clicked, this, [=] {
        auto obj = QString2QJsonObject(ui->dns_object->toPlainText());
        if (obj.isEmpty()) {
            MessageBoxInfo("DNS", "invaild json");
        } else {
            ui->dns_object->setPlainText(QJsonObject2QString(obj, false));
        }
    });
    ui->sniffing_mode->setCurrentIndex(NekoGui::dataStore->routing->sniffing_mode);
    ui->outbound_domain_strategy->setCurrentText(NekoGui::dataStore->routing->outbound_domain_strategy);
    ui->domainStrategyCombo->setCurrentText(NekoGui::dataStore->routing->domain_strategy);
    ui->use_dns_object->setChecked(NekoGui::dataStore->routing->use_dns_object);
    ui->dns_object->setPlainText(NekoGui::dataStore->routing->dns_object);
    ui->remote_dns->setCurrentText(NekoGui::dataStore->routing->remote_dns);
    ui->remote_dns_strategy->setCurrentText(NekoGui::dataStore->routing->remote_dns_strategy);
    ui->direct_dns->setCurrentText(NekoGui::dataStore->routing->direct_dns);
    ui->direct_dns_strategy->setCurrentText(NekoGui::dataStore->routing->direct_dns_strategy);
    ui->dns_final_out->setCurrentText(NekoGui::dataStore->routing->dns_final_out);
    reloadProfileItems();

    connect(ui->route_profiles, &QListWidget::itemDoubleClicked, this, [=](const QListWidgetItem* item){
        on_edit_route_clicked();
    });

    connect(ui->route_prof, SIGNAL(currentIndexChanged(int)), this, SLOT(updateCurrentRouteProfile(int)));

    deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);

    connect(deleteShortcut, &QShortcut::activated, this, [=]{
        on_delete_route_clicked();
    });

    // hijack
    ui->dnshijack_enable->setChecked(NekoGui::dataStore->enable_dns_server);
    set_dns_hijack_enability(NekoGui::dataStore->enable_dns_server);
    ui->dnshijack_listenaddr->setText(NekoGui::dataStore->dns_server_listen_addr);
    ui->dnshijack_listenport->setValidator(QRegExpValidator_Number);
    ui->dnshijack_listenport->setText(Int2String(NekoGui::dataStore->dns_server_listen_port));
    ui->dnshijack_rules->setText(NekoGui::dataStore->dns_server_rules.join("\n"));
    ui->dnshijack_v4resp->setText(NekoGui::dataStore->dns_v4_resp);
    ui->dnshijack_v6resp->setText(NekoGui::dataStore->dns_v6_resp);
#ifndef Q_OS_LINUX
    ui->dnshijack_listenport->setVisible(false);
    ui->dnshijack_listenport_l->setVisible(false);
#endif

    ui->redirect_enable->setChecked(NekoGui::dataStore->enable_redirect);
    ui->redirect_listenaddr->setEnabled(NekoGui::dataStore->enable_redirect);
    ui->redirect_listenaddr->setText(NekoGui::dataStore->redirect_listen_address);
    ui->redirect_listenport->setEnabled(NekoGui::dataStore->enable_redirect);
    ui->redirect_listenport->setValidator(QRegExpValidator_Number);
    ui->redirect_listenport->setText(Int2String(NekoGui::dataStore->redirect_listen_port));

    connect(ui->dnshijack_enable, &QCheckBox::checkStateChanged, this, [=](bool state) {
        set_dns_hijack_enability(state);
    });
    connect(ui->redirect_enable, &QCheckBox::checkStateChanged, this, [=](bool state) {
        ui->redirect_listenaddr->setEnabled(state);
        ui->redirect_listenport->setEnabled(state);
    });

    ADD_ASTERISK(this)
}

void DialogManageRoutes::updateCurrentRouteProfile(int idx) {
    currentRouteProfileID = chainList[idx]->id;
}

DialogManageRoutes::~DialogManageRoutes() {
    delete ui;
}

void DialogManageRoutes::accept() {
    if (chainList.empty()) {
        MessageBoxInfo("Invalid settings", "Routing profile cannot be empty");
        return;
    }
    if (!validate_dns_rules(ui->dnshijack_rules->toPlainText())) {
        MessageBoxInfo("Invalid settings", "DNS Rules are not valid");
        return;
    }

    NekoGui::dataStore->routing->sniffing_mode = ui->sniffing_mode->currentIndex();
    NekoGui::dataStore->routing->domain_strategy = ui->domainStrategyCombo->currentText();
    NekoGui::dataStore->routing->outbound_domain_strategy = ui->outbound_domain_strategy->currentText();
    NekoGui::dataStore->routing->use_dns_object = ui->use_dns_object->isChecked();
    NekoGui::dataStore->routing->dns_object = ui->dns_object->toPlainText();
    NekoGui::dataStore->routing->remote_dns = ui->remote_dns->currentText();
    NekoGui::dataStore->routing->remote_dns_strategy = ui->remote_dns_strategy->currentText();
    NekoGui::dataStore->routing->direct_dns = ui->direct_dns->currentText();
    NekoGui::dataStore->routing->direct_dns_strategy = ui->direct_dns_strategy->currentText();
    NekoGui::dataStore->routing->dns_final_out = ui->dns_final_out->currentText();
    NekoGui::dataStore->fake_dns = ui->enable_fakeip->isChecked();

    NekoGui::profileManager->UpdateRouteChains(chainList);
    NekoGui::dataStore->routing->current_route_id = currentRouteProfileID;
    NekoGui::dataStore->routing->def_outbound = ui->default_out->currentText();

    NekoGui::dataStore->enable_dns_server = ui->dnshijack_enable->isChecked();
    NekoGui::dataStore->dns_server_listen_addr = ui->dnshijack_listenaddr->text();
    NekoGui::dataStore->dns_server_listen_port = ui->dnshijack_listenport->text().toInt();
    NekoGui::dataStore->dns_v4_resp = ui->dnshijack_v4resp->text();
    NekoGui::dataStore->dns_v6_resp = ui->dnshijack_v6resp->text();
    auto rawRules = ui->dnshijack_rules->toPlainText().split("\n");
    QStringList dnsRules;
    for (const auto& rawRule : rawRules) {
        if (rawRule.trimmed().isEmpty()) continue;
        dnsRules.append(rawRule.trimmed());
    }
    NekoGui::dataStore->dns_server_rules = dnsRules;

    NekoGui::dataStore->enable_redirect = ui->redirect_enable->isChecked();
    NekoGui::dataStore->redirect_listen_address = ui->redirect_listenaddr->text();
    NekoGui::dataStore->redirect_listen_port = ui->redirect_listenport->text().toInt();

    //
    QStringList msg{"UpdateDataStore"};
    msg << "RouteChanged";
    MW_dialog_message("", msg.join(","));

    QDialog::accept();
}

void DialogManageRoutes::on_new_route_clicked() {
    routeChainWidget = new RouteItem(this, NekoGui::ProfileManager::NewRouteChain());
    routeChainWidget->setWindowModality(Qt::ApplicationModal);
    routeChainWidget->show();
    connect(routeChainWidget, &RouteItem::settingsChanged, this, [=](const std::shared_ptr<NekoGui::RoutingChain>& chain) {
        chainList << chain;
        reloadProfileItems();
    });
}

void DialogManageRoutes::on_clone_route_clicked() {
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;

    auto chainCopy = std::make_shared<NekoGui::RoutingChain>(*chainList[idx]);
    chainCopy->name = chainCopy->name + " clone";
    chainCopy->id = -1;
    chainList.append(chainCopy);
    reloadProfileItems();
}

void DialogManageRoutes::on_edit_route_clicked() {
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;

    routeChainWidget = new RouteItem(this, chainList[idx]);
    routeChainWidget->setWindowModality(Qt::ApplicationModal);
    routeChainWidget->show();
    connect(routeChainWidget, &RouteItem::settingsChanged, this, [=](const std::shared_ptr<NekoGui::RoutingChain>& chain) {
        if (chain->isViewOnly()) return;
        chainList[idx] = chain;
        reloadProfileItems();
    });

}

void DialogManageRoutes::on_delete_route_clicked() {
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;
    if (chainList.size() == 1) {
        MessageBoxWarning("Invalid operation", "Routing Profiles cannot be empty, try adding another profile or editing this one");
        return;
    }

    auto profileToDel = chainList[idx];
    if (profileToDel->isViewOnly()) {
        MessageBoxInfo("Profile is Read-only", "Cannot delete built-in profiles");
        return;
    }
    chainList.removeAt(idx);
    if (profileToDel->id == currentRouteProfileID) {
        currentRouteProfileID = chainList[0]->id;
    }
    reloadProfileItems();
}