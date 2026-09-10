#include "include/ui/setting/dialog_manage_routes.h"

#include <QClipboard>

#include "include/global/GuiUtils.hpp"

#include <QFile>
#include <QMessageBox>
#include <QShortcut>
#include <QTimer>
#include <QToolTip>
#include <QDialog>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QScreen>
#include <QGridLayout>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <include/api/RPC.h>

#include "include/configs/generate.h"
#include "include/configs/sub/warp.h"
#include "include/configs/sub/RouteUpdater.hpp"

#include <srslist.h>
#include "include/database/RoutesRepo.h"
#include "include/ui/setting/RawRouteItem.h"

void DialogManageRoutes::reloadProfileItems() {
    if (chainList.empty()) {
        MessageBoxWarning(tr("Invalid state"), tr("The list of routing profiles is empty, this should be an unreachable state, crashes may occur now"));
        return;
    }

    QSignalBlocker blocker = QSignalBlocker(ui->route_prof); // currentIndexChanged during clear() crashes us
    ui->route_prof->clear();

    ui->route_profiles->clear();
    bool selectedChainGone = true;
    int i=0;
    for (const auto &item: chainList) {
        ui->route_prof->addItem(item->name);
        ui->route_profiles->addItem(item->name);
        if (item == currentRoute) {
            ui->route_prof->setCurrentIndex(i);
            selectedChainGone=false;
        }
        i++;
    }
    if (selectedChainGone) {
        currentRoute=chainList[0];
        ui->route_prof->setCurrentIndex(0);
    }
    blocker.unblock();
}

void DialogManageRoutes::set_dns_hijack_enability(const bool enable) const {
    ui->dnshijack_allow_lan->setEnabled(enable);
    ui->dnshijack_listenport->setEnabled(enable);
    ui->dnshijack_rules->setEnabled(enable);
    ui->dnshijack_v4resp->setEnabled(enable);
    ui->dnshijack_v6resp->setEnabled(enable);
}

void DialogManageRoutes::show_predefined_dns_editor() {
    auto w = new QDialog(this);
    w->setWindowTitle(tr("Predefined DNS Answers"));
    w->setWindowModality(Qt::ApplicationModal);

    auto layout = new QGridLayout(w);

    auto enable = new QCheckBox(tr("Enable predefined answers"), w);
    enable->setChecked(predefined_dns_enabled);
    layout->addWidget(enable, 0, 0);

    auto tEdit = new QPlainTextEdit(w);
    tEdit->setPlaceholderText(tr("One entry per line, hosts-file syntax:\n\n"
                                 "127.0.0.1 localhost\n"
                                 "10.0.0.5 nas.lan files.lan\n"
                                 "::1 localhost6\n\n"
                                 "A domain listed with only one address family is answered "
                                 "NXDOMAIN for the other, so the override cannot be bypassed."));
    tEdit->setPlainText(predefined_dns_text);
    tEdit->setEnabled(predefined_dns_enabled);
    layout->addWidget(tEdit, 1, 0);

    connect(enable, &QCheckBox::stateChanged, tEdit, [=](int state) {
        tEdit->setEnabled(state != Qt::Unchecked);
    });

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, w);
    layout->addWidget(buttons, 2, 0);

    connect(buttons, &QDialogButtonBox::accepted, w, [=, this] {
        QString badLine;
        QList<Configs::PredefinedDNSEntry> parsed;
        if (!Configs::ParsePredefinedDNS(tEdit->toPlainText().split("\n"), parsed, &badLine)) {
            MessageBoxWarning(tr("Invalid input"), tr("Not a valid predefined DNS entry:\n") + badLine);
            return;
        }
        predefined_dns_enabled = enable->isChecked();
        predefined_dns_text = tEdit->toPlainText();
        w->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, w, &QDialog::reject);

    w->resize(w->sizeHint().expandedTo(QSize(520, 360)).boundedTo(screen()->availableGeometry().size()));
    w->exec();
    w->deleteLater();
}

void DialogManageRoutes::show_dns_advanced_editor() {
    auto w = new QDialog(this);
    w->setWindowTitle(tr("Advanced DNS Settings"));
    w->setWindowModality(Qt::ApplicationModal);

    auto layout = new QFormLayout(w);

    auto cacheCap = new QLineEdit(Int2String(dns_advanced.cache_capacity), w);
    cacheCap->setValidator(QRegExpValidator_Number);
    layout->addRow(tr("Cache Capacity"), cacheCap);

    auto queryTimeout = new QLineEdit(dns_advanced.query_timeout, w);
    queryTimeout->setPlaceholderText(tr("10s (default)"));
    layout->addRow(tr("Query Timeout"), queryTimeout);

    auto optimistic = new QCheckBox(tr("Optimistic Cache"), w);
    optimistic->setToolTip(tr("<html><head/><body><p>Keep serving an expired answer while it is "
                              "refreshed in the background. Cannot be combined with Disable Cache "
                              "or Disable Expire.</p></body></html>"));
    optimistic->setChecked(dns_advanced.optimistic);
    layout->addRow(optimistic);

    auto optimisticTimeout = new QLineEdit(dns_advanced.optimistic_timeout, w);
    optimisticTimeout->setPlaceholderText(tr("3d (default)"));
    layout->addRow(tr("Optimistic Timeout"), optimisticTimeout);

    auto disableCache = new QCheckBox(tr("Disable Cache"), w);
    disableCache->setChecked(dns_advanced.disable_cache);
    layout->addRow(disableCache);

    auto disableExpire = new QCheckBox(tr("Disable Expire"), w);
    disableExpire->setChecked(dns_advanced.disable_expire);
    layout->addRow(disableExpire);

    auto persistCache = new QCheckBox(tr("Save Cache To File"), w);
    persistCache->setToolTip(tr("<html><head/><body><p>Write cached DNS answers and FakeIP mappings to the core's cache "
                                "file so they survive a restart. Off by default: each entry costs a disk write.</p></body></html>"));
    persistCache->setChecked(dns_advanced.persist_cache);
    layout->addRow(persistCache);

    auto reverseMapping = new QCheckBox(tr("Reverse Mapping"), w);
    reverseMapping->setChecked(dns_advanced.reverse_mapping);
    layout->addRow(reverseMapping);

    // The core rejects optimistic alongside either cache switch, so the UI keeps them exclusive.
    auto syncConflicts = [=] {
        const bool cacheOff = disableCache->isChecked() || disableExpire->isChecked();
        if (cacheOff) optimistic->setChecked(false);
        optimistic->setEnabled(!cacheOff);
        optimisticTimeout->setEnabled(optimistic->isChecked());
        disableCache->setEnabled(!optimistic->isChecked());
        disableExpire->setEnabled(!optimistic->isChecked());
        // Disable Cache short-circuits the store path, so the file would never be written.
        persistCache->setEnabled(!disableCache->isChecked());
    };
    connect(optimistic, &QCheckBox::toggled, w, syncConflicts);
    connect(disableCache, &QCheckBox::toggled, w, syncConflicts);
    connect(disableExpire, &QCheckBox::toggled, w, syncConflicts);
    syncConflicts();

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, w);
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, w, [=, this] {
        for (const auto &field : {queryTimeout, optimisticTimeout}) {
            if (field->text().trimmed().isEmpty()) continue;
            if (Configs::IsValidDuration(field->text().trimmed())) continue;
            MessageBoxWarning(tr("Invalid settings"),
                              tr("Not a valid duration: ") + field->text().trimmed() +
                                  tr("\nUse a number followed by ns, us, ms, s, m, h or d."));
            return;
        }
        dns_advanced.cache_capacity = cacheCap->text().trimmed().toInt();
        dns_advanced.query_timeout = queryTimeout->text().trimmed();
        dns_advanced.optimistic = optimistic->isChecked();
        dns_advanced.optimistic_timeout = optimisticTimeout->text().trimmed();
        dns_advanced.disable_cache = disableCache->isChecked();
        dns_advanced.disable_expire = disableExpire->isChecked();
        dns_advanced.persist_cache = persistCache->isChecked();
        dns_advanced.reverse_mapping = reverseMapping->isChecked();
        w->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, w, &QDialog::reject);

    w->resize(w->sizeHint().boundedTo(screen()->availableGeometry().size()));
    w->exec();
    w->deleteLater();
}

void DialogManageRoutes::show_dns_object_editor() {
    auto w = new QDialog(this);
    w->setWindowTitle(tr("DNS Object"));
    w->setWindowModality(Qt::ApplicationModal);

    auto layout = new QGridLayout(w);

    auto tEdit = new QPlainTextEdit(w);
    tEdit->setPlaceholderText(DecodeB64IfValid("ewogICJzZXJ2ZXJzIjogW10sCiAgInJ1bGVzIjogW10sCiAgImZpbmFsIjogIiIsCiAgInN0cmF0ZWd5IjogIiIsCiAgImRpc2FibGVfY2FjaGUiOiBmYWxzZSwKICAiZGlzYWJsZV9leHBpcmUiOiBmYWxzZSwKICAiaW5kZXBlbmRlbnRfY2FjaGUiOiBmYWxzZSwKICAicmV2ZXJzZV9tYXBwaW5nIjogZmFsc2UsCiAgImZha2VpcCI6IHt9Cn0="));
    tEdit->setPlainText(dns_object_text);
    layout->addWidget(tEdit, 0, 0, 1, 2);

    auto format = new QPushButton(tr("Format"), w);
    layout->addWidget(format, 1, 0);
    connect(format, &QPushButton::clicked, w, [=] {
        auto obj = QString2QJsonObject(tEdit->toPlainText());
        if (obj.isEmpty()) {
            MessageBoxWarning(tr("Invalid input"), tr("The DNS object is not a valid JSON object"));
            return;
        }
        tEdit->setPlainText(QJsonObject2QString(obj, false));
    });

    auto document = new QPushButton(tr("Document"), w);
    layout->addWidget(document, 1, 1);
    connect(document, &QPushButton::clicked, w, [=] {
        MessageBoxInfo("DNS", "https://sing-box.sagernet.org/configuration/dns/");
    });

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, w);
    layout->addWidget(buttons, 2, 0, 1, 2);

    connect(buttons, &QDialogButtonBox::accepted, w, [=, this] {
        dns_object_text = tEdit->toPlainText();
        w->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, w, &QDialog::reject);

    w->resize(w->sizeHint().expandedTo(QSize(560, 420)).boundedTo(screen()->availableGeometry().size()));
    w->exec();
    w->deleteLater();
}

bool DialogManageRoutes::validate_dns_rules(const QString &rawString) {
    auto rules = rawString.split("\n");
    for (const auto& rawRule : rules) {
        const QString rule = rawRule.trimmed();
        if (!rule.isEmpty() && !rule.startsWith("ruleset:") && !rule.startsWith("domain:") && !rule.startsWith("suffix:") && !rule.startsWith("regex:")) return false;
    }
    return true;
}

DialogManageRoutes::DialogManageRoutes(QWidget *parent) : QDialog(parent), ui(new Ui::DialogManageRoutes) {
    ui->setupUi(this);
    auto profiles = Configs::dataManager->routesRepo->GetAllRouteProfiles();
    for (const auto &item: profiles) {
        chainList << item;
    }
    if (chainList.empty()) {
        auto defaultChain = Configs::RouteProfile::GetDefaultChain();
        Configs::dataManager->routesRepo->AddRouteProfile(defaultChain);
        chainList.append(defaultChain);
    }
    currentRoute = Configs::dataManager->routesRepo->GetRouteProfile(Configs::dataManager->settingsRepo->current_route_id);
    if (currentRoute == nullptr) currentRoute = chainList[0];

    // Both strategy pickers must stay on one shared item order.
    ui->default_domain_strategy->addItems(Configs::DomainStrategy::DomainStrategy);
    ui->domainStrategyCombo->addItems(Configs::DomainStrategy::DomainStrategy);
    ui->local_override->setText(Configs::dataManager->settingsRepo->core_box_underlying_dns);
    ui->enable_fakeip->setChecked(Configs::dataManager->settingsRepo->fake_dns);
    ui->fakeip_disable_ipv6->setChecked(Configs::dataManager->settingsRepo->fakeip_disable_ipv6);
    ui->fakeip_disable_ipv6->setEnabled(Configs::dataManager->settingsRepo->fake_dns);
    connect(ui->enable_fakeip, &QCheckBox::toggled, ui->fakeip_disable_ipv6, &QCheckBox::setEnabled);

    dns_advanced = {
        Configs::dataManager->settingsRepo->dns_cache_capacity,
        Configs::dataManager->settingsRepo->dns_disable_cache,
        Configs::dataManager->settingsRepo->dns_disable_expire,
        Configs::dataManager->settingsRepo->dns_persist_cache,
        Configs::dataManager->settingsRepo->dns_reverse_mapping,
        Configs::dataManager->settingsRepo->dns_optimistic,
        Configs::dataManager->settingsRepo->dns_optimistic_timeout,
        Configs::dataManager->settingsRepo->dns_query_timeout,
    };
    connect(ui->dns_advanced, &QPushButton::clicked, this, &DialogManageRoutes::show_dns_advanced_editor);

    dns_object_text = Configs::dataManager->settingsRepo->dns_object;
    connect(ui->dns_object_edit, &QPushButton::clicked, this, &DialogManageRoutes::show_dns_object_editor);
    connect(ui->use_dns_object, &QCheckBox::stateChanged, this, [=,this](int state) {
        auto useDNSObject = state == Qt::Checked;
        ui->simple_dns_box->setDisabled(useDNSObject);
        ui->dns_advanced->setDisabled(useDNSObject);
        ui->dns_object_edit->setDisabled(!useDNSObject);
    });
    ui->use_dns_object->stateChanged(Qt::Unchecked);
    ui->ruleset_mirror->setCurrentIndex(Configs::dataManager->settingsRepo->ruleset_mirror);
    ui->default_domain_strategy->setCurrentText(Configs::dataManager->settingsRepo->default_domain_strategy);
    ui->domainStrategyCombo->setCurrentText(Configs::dataManager->settingsRepo->resolve_domain_strategy);
    ui->use_dns_object->setChecked(Configs::dataManager->settingsRepo->use_dns_object);
    ui->remote_dns->setCurrentText(Configs::dataManager->settingsRepo->remote_dns);
    ui->remote_dns_disable_ipv6->setChecked(Configs::dataManager->settingsRepo->remote_dns_disable_ipv6);
    ui->direct_dns->setCurrentText(Configs::dataManager->settingsRepo->direct_dns);
    ui->direct_dns_disable_ipv6->setChecked(Configs::dataManager->settingsRepo->direct_dns_disable_ipv6);
    ui->dns_final_out->setCurrentText(Configs::dataManager->settingsRepo->dns_final_out);
    ui->enable_dns_routing->setChecked(Configs::dataManager->settingsRepo->enable_dns_routing);
    ui->respect_hosts->setChecked(Configs::dataManager->settingsRepo->dns_use_hosts);
    predefined_dns_enabled = Configs::dataManager->settingsRepo->dns_predefined_enable;
    predefined_dns_text = Configs::dataManager->settingsRepo->dns_predefined_rules.join("\n");
    connect(ui->predefined_dns, &QPushButton::clicked, this, &DialogManageRoutes::show_predefined_dns_editor);
    reloadProfileItems();

    connect(ui->route_profiles, &QListWidget::itemDoubleClicked, this, [=,this](const QListWidgetItem* item){
        on_edit_route_clicked();
    });

    connect(ui->route_prof, &QComboBox::currentIndexChanged, this, &DialogManageRoutes::updateCurrentRouteProfile);

    deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), this);

    connect(deleteShortcut, &QShortcut::activated, this, [=,this]{
        on_delete_route_clicked();
    });

    // Scoped to the list, or these would hijack copy/paste in the dialog's text fields.
    auto exportShortcut = new QShortcut(QKeySequence::Copy, ui->route_profiles);
    exportShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(exportShortcut, &QShortcut::activated, this, [=,this]{
        on_export_route_clicked();
    });

    auto importShortcut = new QShortcut(QKeySequence::Paste, ui->route_profiles);
    importShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(importShortcut, &QShortcut::activated, this, [=,this]{
        on_import_route_clicked();
    });

    ui->dnshijack_enable->setChecked(Configs::dataManager->settingsRepo->enable_dns_server);
    set_dns_hijack_enability(Configs::dataManager->settingsRepo->enable_dns_server);
    ui->dnshijack_allow_lan->setChecked(Configs::dataManager->settingsRepo->dns_server_listen_lan);
    ui->dnshijack_listenport->setValidator(QRegExpValidator_Number);
    ui->dnshijack_listenport->setText(Int2String(Configs::dataManager->settingsRepo->dns_server_listen_port));
    ui->dnshijack_v4resp->setText(Configs::dataManager->settingsRepo->dns_v4_resp);
    ui->dnshijack_v6resp->setText(Configs::dataManager->settingsRepo->dns_v6_resp);
    connect(ui->dnshijack_what, &QPushButton::clicked, this, [=,this] {
        MessageBoxInfo("What is this?", Configs::Information::HijackInfo);
    });

    QStringList ruleItems = {"domain:", "suffix:", "regex:"};
    for (const auto& item : ruleSetList) {
        ruleItems.append("ruleset:" + QString::fromUtf8(item.first.data(), item.first.size()));
    }
    rule_editor = new AutoCompleteTextEdit("", ruleItems, this);
    ui->hijack_box->layout()->replaceWidget(ui->dnshijack_rules, rule_editor);
    rule_editor->setPlainText(Configs::dataManager->settingsRepo->dns_server_rules.join("\n"));
    ui->dnshijack_rules->hide();
#ifndef Q_OS_LINUX
    ui->dnshijack_listenport->setVisible(false);
    ui->dnshijack_listenport_l->setVisible(false);
#endif

    ui->redirect_enable->setChecked(Configs::dataManager->settingsRepo->enable_redirect);
    ui->redirect_listenaddr->setEnabled(Configs::dataManager->settingsRepo->enable_redirect);
    ui->redirect_listenaddr->setText(Configs::dataManager->settingsRepo->redirect_listen_address);
    ui->redirect_listenport->setEnabled(Configs::dataManager->settingsRepo->enable_redirect);
    ui->redirect_listenport->setValidator(QRegExpValidator_Number);
    ui->redirect_listenport->setText(Int2String(Configs::dataManager->settingsRepo->redirect_listen_port));

    connect(ui->dnshijack_enable, &QCheckBox::stateChanged, this, [=,this](bool state) {
        set_dns_hijack_enability(state);
    });
    connect(ui->redirect_enable, &QCheckBox::stateChanged, this, [=,this](bool state) {
        ui->redirect_listenaddr->setEnabled(state);
        ui->redirect_listenport->setEnabled(state);
    });

    ui->enable_warp->setChecked(Configs::dataManager->settingsRepo->enable_warp);
    ui->warp_private_key->setText(Configs::dataManager->settingsRepo->warp_private_key);
    ui->warp_public_key->setText(Configs::dataManager->settingsRepo->warp_public_key);
    ui->warp_ifc_addrs->setText(Configs::dataManager->settingsRepo->warp_ifc_addrs.join(","));
    ui->warp_ep->setText(Configs::dataManager->settingsRepo->warp_ep);
    ui->warp_reserved->setText(Configs::dataManager->settingsRepo->warp_reserved.join(","));
    connect(ui->warp_autogen, &QPushButton::clicked, this, [=,this] {
        auto originalText = ui->warp_autogen->text();
        ui->warp_autogen->setText("Getting keypair...");
        bool ok;
        auto keyPair = API::defaultClient->GenWgKeyPair(&ok);
        if (!ok) {
            runOnUiThread([=] {
               MessageBoxWarning("Failed to get key pair", keyPair.error->c_str());
            });
            ui->warp_autogen->setText(originalText);
            return;
        }
        ui->warp_autogen->setText("Generating config...");
        QString error;
        auto conf = Configs_network::genWarpConfig(&error, keyPair.private_key->c_str(), keyPair.public_key->c_str());
        if (!error.isEmpty()) {
            runOnUiThread([=] {
                MessageBoxWarning("Failed to generate warp config", error);
            });
            ui->warp_autogen->setText(originalText);
            return;
        }
        ui->warp_private_key->setText(conf->privateKey);
        ui->warp_public_key->setText(conf->publicKey);
        ui->warp_ep->setText(conf->endpoint);
        ui->warp_ifc_addrs->setText(conf->ipv4Address + "/32," + conf->ipv6Address + "/128");
        ui->warp_reserved->setText(QListInt2QListString(conf->reserved).join(","));
        ui->warp_autogen->setText("Success!");
        setTimeout([=,this] { ui->warp_autogen->setText(originalText); }, this, 2000);
    });

    ADD_ASTERISK(this)
    // Frozen .ui geometry clips this dialog once a translation outgrows it.
    resize(sizeHint().expandedTo(size()).boundedTo(screen()->availableGeometry().size()));
}

void DialogManageRoutes::updateCurrentRouteProfile(int idx) {
    currentRoute = chainList[idx];
}

DialogManageRoutes::~DialogManageRoutes() {
    delete ui;
}

void DialogManageRoutes::accept() {
    if (chainList.empty()) {
        MessageBoxInfo(tr("Invalid settings"), tr("Routing profile cannot be empty"));
        return;
    }
    if (!validate_dns_rules(rule_editor->toPlainText())) {
        MessageBoxInfo(tr("Invalid settings"), tr("DNS Rules are not valid"));
        return;
    }

    Configs::dataManager->settingsRepo->ruleset_mirror = ui->ruleset_mirror->currentIndex();
    Configs::dataManager->settingsRepo->resolve_domain_strategy = ui->domainStrategyCombo->currentText();
    Configs::dataManager->settingsRepo->default_domain_strategy = ui->default_domain_strategy->currentText();
    Configs::dataManager->settingsRepo->use_dns_object = ui->use_dns_object->isChecked();
    Configs::dataManager->settingsRepo->dns_object = dns_object_text;
    Configs::dataManager->settingsRepo->remote_dns = ui->remote_dns->currentText().trimmed();
    Configs::dataManager->settingsRepo->remote_dns_disable_ipv6 = ui->remote_dns_disable_ipv6->isChecked();
    Configs::dataManager->settingsRepo->dns_cache_capacity = dns_advanced.cache_capacity;
    Configs::dataManager->settingsRepo->dns_disable_cache = dns_advanced.disable_cache;
    Configs::dataManager->settingsRepo->dns_disable_expire = dns_advanced.disable_expire;
    Configs::dataManager->settingsRepo->dns_persist_cache = dns_advanced.persist_cache;
    Configs::dataManager->settingsRepo->dns_reverse_mapping = dns_advanced.reverse_mapping;
    Configs::dataManager->settingsRepo->dns_optimistic = dns_advanced.optimistic;
    Configs::dataManager->settingsRepo->dns_optimistic_timeout = dns_advanced.optimistic_timeout;
    Configs::dataManager->settingsRepo->dns_query_timeout = dns_advanced.query_timeout;
    Configs::dataManager->settingsRepo->direct_dns = ui->direct_dns->currentText().trimmed();
    Configs::dataManager->settingsRepo->direct_dns_disable_ipv6 = ui->direct_dns_disable_ipv6->isChecked();
    Configs::dataManager->settingsRepo->core_box_underlying_dns = ui->local_override->text().trimmed();
    Configs::dataManager->settingsRepo->dns_final_out = ui->dns_final_out->currentText();
    Configs::dataManager->settingsRepo->fake_dns = ui->enable_fakeip->isChecked();
    Configs::dataManager->settingsRepo->fakeip_disable_ipv6 = ui->fakeip_disable_ipv6->isChecked();
    Configs::dataManager->settingsRepo->enable_dns_routing = ui->enable_dns_routing->isChecked();
    Configs::dataManager->settingsRepo->dns_use_hosts = ui->respect_hosts->isChecked();
    Configs::dataManager->settingsRepo->dns_predefined_enable = predefined_dns_enabled;
    QStringList predefinedRules;
    for (const auto &line : predefined_dns_text.split("\n")) {
        if (!line.trimmed().isEmpty()) predefinedRules.append(line.trimmed());
    }
    Configs::dataManager->settingsRepo->dns_predefined_rules = predefinedRules;

    Configs::dataManager->routesRepo->UpdateRouteProfiles(chainList);
    Configs::dataManager->settingsRepo->current_route_id = currentRoute->id;

    Configs::dataManager->settingsRepo->enable_dns_server = ui->dnshijack_enable->isChecked();
    Configs::dataManager->settingsRepo->dns_server_listen_port = ui->dnshijack_listenport->text().trimmed().toInt();
    Configs::dataManager->settingsRepo->dns_v4_resp = ui->dnshijack_v4resp->text().trimmed();
    Configs::dataManager->settingsRepo->dns_v6_resp = ui->dnshijack_v6resp->text().trimmed();
    auto rawRules = rule_editor->toPlainText().split("\n");
    QStringList dnsRules;
    for (const auto& rawRule : rawRules) {
        if (rawRule.trimmed().isEmpty()) continue;
        dnsRules.append(rawRule.trimmed());
    }
    Configs::dataManager->settingsRepo->dns_server_rules = dnsRules;

    Configs::dataManager->settingsRepo->dns_server_listen_lan = ui->dnshijack_allow_lan->isChecked();
    Configs::dataManager->settingsRepo->enable_redirect = ui->redirect_enable->isChecked();
    Configs::dataManager->settingsRepo->redirect_listen_address = ui->redirect_listenaddr->text().trimmed();
    Configs::dataManager->settingsRepo->redirect_listen_port = ui->redirect_listenport->text().trimmed().toInt();

    Configs::dataManager->settingsRepo->enable_warp = ui->enable_warp->isChecked();
    Configs::dataManager->settingsRepo->warp_ep = ui->warp_ep->text().trimmed();
    Configs::dataManager->settingsRepo->warp_ifc_addrs = SplitAndTrim(ui->warp_ifc_addrs->text(), ",", false);
    Configs::dataManager->settingsRepo->warp_private_key = ui->warp_private_key->text().trimmed();
    Configs::dataManager->settingsRepo->warp_public_key = ui->warp_public_key->text().trimmed();
    Configs::dataManager->settingsRepo->warp_reserved = SplitAndTrim(ui->warp_reserved->text(), ",", false);

    MW_dialog_message(MwMessage::UpdateSettings, {MwArg::Route});

    QDialog::accept();
}

void DialogManageRoutes::on_new_route_clicked() {
    QMenu menu(this);
    menu.addAction(tr("Structured profile"));
    auto* rawAct = menu.addAction(tr("Raw profile"));
    auto* remoteAct = menu.addAction(tr("Remote profile"));
    auto* chosen = menu.exec(ui->new_route->mapToGlobal(QPoint(0, ui->new_route->height())));
    if (chosen == nullptr) return;

    auto newProfile = Configs::dataManager->routesRepo->NewRouteProfile();
    auto onCreated = [=, this](const std::shared_ptr<Configs::RouteProfile>& chain) {
        chainList << chain;
        reloadProfileItems();
    };
    if (chosen == rawAct) {
        newProfile->isRaw = true;
        auto* rawWidget = new RawRouteItem(this, newProfile);
        rawWidget->setWindowModality(Qt::ApplicationModal);
        rawWidget->show();
        connect(rawWidget, &RawRouteItem::settingsChanged, this, onCreated);
    } else {
        // Remote profiles are structured underneath; RouteItem grows a "Remote source" section when isRemote.
        if (chosen == remoteAct) newProfile->isRemote = true;
        routeChainWidget = new RouteItem(this, newProfile);
        routeChainWidget->setWindowModality(Qt::ApplicationModal);
        routeChainWidget->show();
        connect(routeChainWidget, &RouteItem::settingsChanged, this, onCreated);
    }
}

void DialogManageRoutes::on_export_route_clicked()
{
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;

    QString warnings;
    QApplication::clipboard()->setText(chainList[idx]->ToShareLink(&warnings));
    if (!warnings.isEmpty()) MessageBoxInfo(tr("Exported with warnings"), warnings);

    QToolTip::showText(QCursor::pos(), tr("Copied!"), this);
    int r = ++tooltipID;
    QTimer::singleShot(1500, [=,this] {
        if (tooltipID != r) return;
        QToolTip::hideText();
    });
}

void DialogManageRoutes::applyImportedProfile(const std::shared_ptr<Configs::RouteProfile>& profile, bool wasOldArray)
{
    if (wasOldArray) {
        // A legacy rule array carries no name / default outbound, so it cannot be saved unedited.
        auto shell = Configs::dataManager->routesRepo->NewRouteProfile();
        shell->Rules = profile->Rules;
        routeChainWidget = new RouteItem(this, shell);
        routeChainWidget->setWindowModality(Qt::ApplicationModal);
        routeChainWidget->show();
        connect(routeChainWidget, &RouteItem::settingsChanged, this, [=, this](const std::shared_ptr<Configs::RouteProfile>& chain) {
            chainList << chain;
            reloadProfileItems();
        });
    } else {
        chainList << profile;
        currentRoute = profile;
        reloadProfileItems();
    }
}

bool DialogManageRoutes::tryImportRemoteRoutesLink(const QString& text)
{
    bool wasRemoteRouteLink = false;
    QString error;
    auto profiles = Configs::RouteProfile::FromRemoteRoutesLink(text, &wasRemoteRouteLink, &error);
    if (!wasRemoteRouteLink) return false;

    if (profiles.isEmpty()) {
        MessageBoxWarning(tr("Add remote routing profiles"),
                          error.isEmpty() ? tr("No valid remote routing profiles in the link.") : error);
        return true;
    }

    QString prompt = tr("Add these remote routing profiles?") + "\n";
    for (int i = 0; i < profiles.size(); ++i) {
        prompt += QString("\n%1. %2  (%3: %4)")
                      .arg(i + 1)
                      .arg(profiles[i]->remoteURL, tr("auto update"), profiles[i]->autoUpdate ? tr("On") : tr("Off"));
    }
    if (QMessageBox::question(this, tr("Add remote routing profiles"), prompt) != QMessageBox::StandardButton::Yes) {
        return true; // it was a remoteRoute link; the user declined
    }

    for (const auto& p : profiles) chainList << p;
    reloadProfileItems();
    updateRemoteProfiles(profiles);
    return true;
}

void DialogManageRoutes::on_import_route_clicked()
{
    const QString clip = QApplication::clipboard()->text().trimmed();
    if (tryImportRemoteRoutesLink(clip)) return;
    if (!clip.isEmpty()) {
        QString fatal, warnings;
        bool wasOldArray = false;
        if (auto profile = Configs::RouteProfile::FromShareInput(clip, &fatal, &warnings, &wasOldArray)) {
            const QString what = wasOldArray ? tr("a routing rule list")
                                             : tr("routing profile \"%1\"").arg(profile->name);
            QString prompt = tr("Import %1 from the clipboard?").arg(what);
            // endpoints are already created by the parse above, before this prompt
            if (!warnings.isEmpty()) prompt += "\n\n" + tr("Note:") + "\n" + warnings.trimmed();
            if (QMessageBox::question(this, tr("Import from clipboard"), prompt)
                == QMessageBox::StandardButton::Yes) {
                applyImportedProfile(profile, wasOldArray);
                return;
            }
        }
    }

    auto w = new QDialog(this);
    w->setWindowTitle(tr("Import routing profile"));
    w->setWindowModality(Qt::ApplicationModal);

    auto layout = new QGridLayout(w);
    auto tEdit = new QTextEdit(w);
    tEdit->setPlaceholderText(tr("Paste a Throne route link, a remoteRoute link, a base64 blob, or a JSON rule array"));
    layout->addWidget(tEdit, 0, 0);

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, w);
    layout->addWidget(buttons, 1, 0);

    connect(buttons, &QDialogButtonBox::accepted, w, [=, this] {
        if (tryImportRemoteRoutesLink(tEdit->toPlainText())) { w->accept(); return; }
        QString fatal, warnings;
        bool wasOldArray = false;
        auto profile = Configs::RouteProfile::FromShareInput(tEdit->toPlainText(), &fatal, &warnings, &wasOldArray);
        if (!profile) {
            MessageBoxWarning(tr("Invalid input"), tr("Could not import this routing profile:\n") + fatal);
            return;
        }
        if (!warnings.isEmpty()) MessageBoxInfo(tr("Imported with warnings"), warnings);
        applyImportedProfile(profile, wasOldArray);
        w->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, w, &QDialog::reject);

    w->exec();
    w->deleteLater();
}

void DialogManageRoutes::on_clone_route_clicked() {
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;

    auto chainCopy = std::make_shared<Configs::RouteProfile>(*chainList[idx]);
    chainCopy->name = chainCopy->name + " clone";
    chainCopy->id = -1;
    chainList.append(chainCopy);
    reloadProfileItems();
}

void DialogManageRoutes::on_edit_route_clicked() {
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;

    auto onEdited = [=, this](const std::shared_ptr<Configs::RouteProfile>& chain) {
        if (currentRoute == chainList[idx]) currentRoute = chain;
        chainList[idx] = chain;
        reloadProfileItems();
    };

    if (chainList[idx]->isRaw) {
        auto* rawWidget = new RawRouteItem(this, chainList[idx]);
        rawWidget->setWindowModality(Qt::ApplicationModal);
        rawWidget->show();
        connect(rawWidget, &RawRouteItem::settingsChanged, this, onEdited);
    } else {
        routeChainWidget = new RouteItem(this, chainList[idx]);
        routeChainWidget->setWindowModality(Qt::ApplicationModal);
        routeChainWidget->show();
        connect(routeChainWidget, &RouteItem::settingsChanged, this, onEdited);
    }
}

void DialogManageRoutes::on_delete_route_clicked() {
    auto idx = ui->route_profiles->currentRow();
    if (idx < 0) return;
    if (chainList.size() == 1) {
        MessageBoxWarning(tr("Invalid operation"), tr("Routing Profiles cannot be empty, try adding another profile or editing this one"));
        return;
    }

    auto profileToDel = chainList[idx];
    chainList.removeAt(idx);
    if (profileToDel == currentRoute) {
        currentRoute = chainList[0];
    }
    reloadProfileItems();
}

void DialogManageRoutes::on_update_route_clicked() {
    if (routeUpdateRunning) {
        QMenu menu(this);
        auto* cancelAct = menu.addAction(tr("Cancel"));
        auto* chosen = menu.exec(ui->update_route->mapToGlobal(QPoint(0, ui->update_route->height())));
        // Guard against the batch having finished while the menu was open.
        if (chosen == cancelAct && routeUpdateRunning) {
            routeUpdateCancel = true;
            ui->update_route->setText(tr("Cancelling..."));
        }
        return;
    }

    const int idx = ui->route_profiles->currentRow();
    const bool selIsRemote = idx >= 0 && chainList[idx]->isRemote;

    QMenu menu(this);
    QAction* updateSelAct = selIsRemote ? menu.addAction(tr("Update selected")) : nullptr;
    auto* updateAllAct = menu.addAction(tr("Update all"));
    auto* chosen = menu.exec(ui->update_route->mapToGlobal(QPoint(0, ui->update_route->height())));
    if (chosen == nullptr) return;

    if (chosen == updateSelAct) {
        updateRemoteProfiles({chainList[idx]});
        return;
    }

    if (chosen == updateAllAct) {
        QList<std::shared_ptr<Configs::RouteProfile>> remotes;
        for (const auto& p : chainList) {
            if (p->isRemote && !p->remoteURL.trimmed().isEmpty()) remotes << p;
        }
        if (remotes.isEmpty()) {
            MessageBoxInfo(tr("No remote profiles"), tr("There are no remote routing profiles to update."));
            return;
        }
        updateRemoteProfiles(remotes);
    }
}

void DialogManageRoutes::updateRemoteProfiles(const QList<std::shared_ptr<Configs::RouteProfile>>& profiles) {
    if (routeUpdateRunning || profiles.isEmpty()) return;
    routeUpdateRunning = true;
    routeUpdateCancel = false;
    const int total = profiles.size();

    // The button stays enabled during the run so its click can offer Cancel (see the slot above).
    auto progressText = [total](int current) {
        return total <= 1 ? tr("Updating...") : tr("Updating (%1 / %2)").arg(current).arg(total);
    };
    ui->update_route->setText(progressText(1));

    runOnNewThread([=, this] {
        QStringList failures;
        int ok = 0;
        for (int i = 0; i < profiles.size(); ++i) {
            if (routeUpdateCancel.load()) break;
            const int current = i + 1;
            runOnUiThread([=, this] {
                if (routeUpdateRunning && !routeUpdateCancel.load())
                    ui->update_route->setText(progressText(current));
            });
            QString warnings;
            const QString err = RouteUpdate::UpdateProfile(profiles[i], &warnings);
            if (err.isEmpty()) ok++;
            else failures << (profiles[i]->name + ": " + err);
        }
        const bool cancelled = routeUpdateCancel.load();
        runOnUiThread([=, this] {
            routeUpdateRunning = false;
            ui->update_route->setText(tr("Update"));
            reloadProfileItems();
            if (cancelled) {
                MessageBoxInfo(tr("Update cancelled"),
                               tr("Cancelled: updated %1 of %2, %3 failed.").arg(ok).arg(total).arg(failures.size()));
            } else if (failures.isEmpty()) {
                MessageBoxInfo(tr("Update complete"), tr("Updated %1 remote routing profile(s).").arg(ok));
            } else {
                MessageBoxWarning(tr("Update finished with errors"),
                                  tr("Updated %1, failed %2:\n%3").arg(ok).arg(failures.size()).arg(failures.join("\n")));
            }
        });
    });
}
