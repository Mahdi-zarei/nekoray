#include "include/ui/profile/edit_openconnect.h"

#include <QInputDialog>

#include "include/ui/profile/edit_openconnect_advanced.h"
#include "include/database/DatabaseManager.h"
#include "include/database/OtpProfilesRepo.h"
#include "include/global/OtpPlaceholder.hpp"
#include "include/global/Utils.hpp"

EditOpenConnect::EditOpenConnect(QWidget *parent) : QWidget(parent), ui(new Ui::EditOpenConnect) {
    ui->setupUi(this);

    ui->flavor->addItems({"", "anyconnect", "gp", "fortinet", "f5", "pulse", "nc"});
    ui->tunnel_dns->addItem(tr("None"), QString(Configs::kTunnelDnsNone));
    ui->tunnel_dns->addItem(tr("Prefer"), QString(Configs::kTunnelDnsPrefer));
    ui->tunnel_dns->addItem(tr("Strict"), QString(Configs::kTunnelDnsStrict));

    ui->otp_profile_l->setToolTip(tr("<html><head/><body><p>Binds an authenticator entry to this profile. %1 in the "
                                     "username, password, software token or form entry fields is replaced with a "
                                     "generated code at connect time.</p></body></html>")
                                      .arg(Configs::kOtpPlaceholder));
    ui->password->setPlaceholderText(tr("%1 is replaced at connect time").arg(Configs::kOtpPlaceholder));

    connect(ui->certificate_authority, &QPushButton::clicked, this,
            [=, this] { editPem(ui->certificate_authority, tr("CA Certificate"), CACHE.certificateAuthority); });
    connect(ui->client_certificate, &QPushButton::clicked, this,
            [=, this] { editPem(ui->client_certificate, tr("Client Certificate"), CACHE.clientCertificate); });
    connect(ui->client_key, &QPushButton::clicked, this,
            [=, this] { editPem(ui->client_key, tr("Client Key"), CACHE.clientKey); });

    connect(ui->advanced_button, &QPushButton::clicked, this, [=, this] {
        if (ent == nullptr) return;
        auto advanced = new EditOpenConnectAdvanced(this, ent);
        advanced->show();
    });
}

EditOpenConnect::~EditOpenConnect() {
    delete ui;
}

void EditOpenConnect::editPem(QPushButton *button, const QString &title, QStringList &target) {
    bool ok;
    auto txt = QInputDialog::getMultiLineText(this, title, "", target.join("\n"), &ok);
    if (!ok) return;
    target = txt.split("\n", Qt::SkipEmptyParts);
    if (editor_cache_updated) editor_cache_updated();
    else button->setText(target.isEmpty() ? tr("Not set") : tr("Already set"));
}

QList<QPair<QPushButton *, QString>> EditOpenConnect::get_editor_cached() {
    return {
        {ui->certificate_authority, CACHE.certificateAuthority.join("\n")},
        {ui->client_certificate, CACHE.clientCertificate.join("\n")},
        {ui->client_key, CACHE.clientKey.join("\n")},
    };
}

void EditOpenConnect::onStart(std::shared_ptr<Configs::Profile> _ent) {
    this->ent = _ent;
    auto outbound = this->ent->OpenConnect();

    ui->flavor->setCurrentText(outbound->flavor);
    ui->username->setText(outbound->username);
    ui->password->setText(outbound->password);
    ui->auth_group->setText(outbound->auth_group);
    ui->server_path->setText(outbound->server_path);
    ui->mtu->setText(outbound->mtu == 0 ? QString() : Int2String(outbound->mtu));
    ui->only_advertised_routes->setChecked(outbound->only_advertised_routes);
    auto tunnelDnsIndex = ui->tunnel_dns->findData(outbound->tunnel_dns);
    if (tunnelDnsIndex < 0) tunnelDnsIndex = ui->tunnel_dns->findData(QString(Configs::kTunnelDnsPrefer));
    ui->tunnel_dns->setCurrentIndex(tunnelDnsIndex);

    ui->otp_profile->addItem(tr("None"), -1);
    for (const auto &otp: Configs::dataManager->otpProfilesRepo->GetAllOtpProfiles()) {
        ui->otp_profile->addItem(otp->DisplayName(), otp->id);
    }
    auto otpIndex = ui->otp_profile->findData(outbound->otp_profile_id);
    ui->otp_profile->setCurrentIndex(otpIndex < 0 ? 0 : otpIndex);

    ui->insecure->setChecked(outbound->tls->insecure);
    ui->tls_server_name->setText(outbound->tls->server_name);
    ui->client_key_password->setText(outbound->tls->client_key_password);

    CACHE.certificateAuthority = outbound->tls->certificate_authority;
    CACHE.clientCertificate = outbound->tls->client_certificate;
    CACHE.clientKey = outbound->tls->client_key;
}

bool EditOpenConnect::onEnd() {
    auto outbound = this->ent->OpenConnect();

    outbound->flavor = ui->flavor->currentText().trimmed();
    outbound->username = ui->username->text().trimmed();
    outbound->password = ui->password->text();
    outbound->auth_group = ui->auth_group->text().trimmed();
    outbound->server_path = ui->server_path->text().trimmed();
    outbound->mtu = ui->mtu->text().trimmed().toInt();
    outbound->otp_profile_id = ui->otp_profile->currentData().toInt();
    outbound->only_advertised_routes = ui->only_advertised_routes->isChecked();
    outbound->tunnel_dns = ui->tunnel_dns->currentData().toString();

    outbound->tls->insecure = ui->insecure->isChecked();
    outbound->tls->server_name = ui->tls_server_name->text().trimmed();
    outbound->tls->certificate_authority = CACHE.certificateAuthority;
    outbound->tls->client_certificate = CACHE.clientCertificate;
    outbound->tls->client_key = CACHE.clientKey;
    outbound->tls->client_key_password = ui->client_key_password->text();

    return true;
}
