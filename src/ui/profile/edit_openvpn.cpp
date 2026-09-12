#include "include/ui/profile/edit_openvpn.h"

#include <QInputDialog>

#include "include/ui/profile/edit_openvpn_advanced.h"
#include "include/database/DatabaseManager.h"
#include "include/database/OtpProfilesRepo.h"
#include "include/global/OtpPlaceholder.hpp"
#include "include/global/Utils.hpp"

EditOpenVPN::EditOpenVPN(QWidget *parent) : QWidget(parent), ui(new Ui::EditOpenVPN) {
    ui->setupUi(this);

    ui->network->addItems({"", "udp", "udp4", "udp6", "tcp", "tcp4", "tcp6"});
    ui->control_wrap_type->addItems({"", "tls_auth", "tls_crypt", "tls_crypt_v2"});
    ui->control_wrap_direction->addItems({"", "server", "client"});
    ui->tunnel_dns->addItem(tr("None"), QString(Configs::kTunnelDnsNone));
    ui->tunnel_dns->addItem(tr("Prefer"), QString(Configs::kTunnelDnsPrefer));
    ui->tunnel_dns->addItem(tr("Strict"), QString(Configs::kTunnelDnsStrict));

    ui->otp_profile_l->setToolTip(tr("<html><head/><body><p>Binds an authenticator entry to this profile. %1 in the "
                                     "username or password is replaced with a generated code at connect time, and a "
                                     "static challenge is answered with the same code.</p></body></html>")
                                      .arg(Configs::kOtpPlaceholder));
    ui->password->setPlaceholderText(tr("%1 is replaced at connect time").arg(Configs::kOtpPlaceholder));

    connect(ui->certificate, &QPushButton::clicked, this,
            [=, this] { editPem(ui->certificate, tr("CA Certificate"), CACHE.certificate); });
    connect(ui->client_certificate, &QPushButton::clicked, this,
            [=, this] { editPem(ui->client_certificate, tr("Client Certificate"), CACHE.clientCertificate); });
    connect(ui->client_key, &QPushButton::clicked, this,
            [=, this] { editPem(ui->client_key, tr("Client Key"), CACHE.clientKey); });
    connect(ui->control_wrap_key, &QPushButton::clicked, this,
            [=, this] { editPem(ui->control_wrap_key, tr("Control Channel Wrap Key"), CACHE.controlWrapKey); });

    connect(ui->advanced_button, &QPushButton::clicked, this, [=, this] {
        if (ent == nullptr) return;
        auto advanced = new EditOpenVPNAdvanced(this, ent);
        advanced->show();
    });
}

EditOpenVPN::~EditOpenVPN() {
    delete ui;
}

void EditOpenVPN::editPem(QPushButton *button, const QString &title, QStringList &target) {
    bool ok;
    auto txt = QInputDialog::getMultiLineText(this, title, "", target.join("\n"), &ok);
    if (!ok) return;
    target = txt.split("\n", Qt::SkipEmptyParts);
    if (editor_cache_updated) editor_cache_updated();
    else button->setText(target.isEmpty() ? tr("Not set") : tr("Already set"));
}

QList<QPair<QPushButton *, QString>> EditOpenVPN::get_editor_cached() {
    return {
        {ui->certificate, CACHE.certificate.join("\n")},
        {ui->client_certificate, CACHE.clientCertificate.join("\n")},
        {ui->client_key, CACHE.clientKey.join("\n")},
        {ui->control_wrap_key, CACHE.controlWrapKey.join("\n")},
    };
}

void EditOpenVPN::onStart(std::shared_ptr<Configs::Profile> _ent) {
    this->ent = _ent;
    auto outbound = this->ent->OpenVPN();

    ui->network->setCurrentText(outbound->network);
    ui->username->setText(outbound->username);
    ui->password->setText(outbound->password);
    ui->static_challenge->setText(outbound->static_challenge);
    ui->static_challenge_echo->setChecked(outbound->static_challenge_echo);
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

    ui->control_wrap_type->setCurrentText(outbound->tls->control_wrap->type);
    ui->control_wrap_direction->setCurrentText(outbound->tls->control_wrap->direction);

    CACHE.certificate = outbound->tls->certificate;
    CACHE.clientCertificate = outbound->tls->client_certificate;
    CACHE.clientKey = outbound->tls->client_key;
    CACHE.controlWrapKey = outbound->tls->control_wrap->key;
}

bool EditOpenVPN::onEnd() {
    auto outbound = this->ent->OpenVPN();

    outbound->network = ui->network->currentText().trimmed();
    outbound->username = ui->username->text().trimmed();
    outbound->password = ui->password->text();
    outbound->static_challenge = ui->static_challenge->text();
    outbound->static_challenge_echo = ui->static_challenge_echo->isChecked();
    outbound->mtu = ui->mtu->text().trimmed().toInt();
    outbound->otp_profile_id = ui->otp_profile->currentData().toInt();
    outbound->only_advertised_routes = ui->only_advertised_routes->isChecked();
    outbound->tunnel_dns = ui->tunnel_dns->currentData().toString();

    outbound->tls->certificate = CACHE.certificate;
    outbound->tls->client_certificate = CACHE.clientCertificate;
    outbound->tls->client_key = CACHE.clientKey;
    outbound->tls->control_wrap->type = ui->control_wrap_type->currentText().trimmed();
    outbound->tls->control_wrap->key = CACHE.controlWrapKey;
    outbound->tls->control_wrap->direction = ui->control_wrap_direction->currentText().trimmed();

    return true;
}
