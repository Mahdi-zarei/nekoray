#include "include/ui/profile/dialog_vpn_auth.h"

#include "include/ui/stats/dialog_endpoint_details.h"

#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMap>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QTimer>
#include <QUrl>

#include "include/api/RPC.h"
#include "include/global/Utils.hpp"
#include "include/ui/utils/NoWheelComboBox.h"

namespace {
    const QColor kVpnAuthErrorColor(0xE0, 0x5A, 0x5A);

    QString vpnAuthRemainingText(qint64 seconds) {
        if (seconds < 0) seconds = 0;
        const qint64 m = seconds / 60;
        const qint64 s = seconds % 60;
        if (m > 0) return QStringLiteral("%1m %2s").arg(m).arg(s);
        return QStringLiteral("%1s").arg(s);
    }

    void vpnAuthSetLabel(QLabel *label, const QString &text) {
        label->setText(text);
        label->setVisible(!text.isEmpty());
    }
}

DialogVpnAuth::DialogVpnAuth(QWidget *parent, const VpnAuthChallenge &_challenge, bool _localOnly)
    : QDialog(parent)
    , ui(new Ui::DialogVpnAuth)
{
    ui->setupUi(this);
    challenge = _challenge;
    localOnly = _localOnly;

    ui->label_endpoint->setText(tr("Endpoint: %1").arg(Stats::VpnEndpointDisplayName(challenge.endpointTag)));

    auto errorPalette = ui->label_error->palette();
    errorPalette.setColor(QPalette::WindowText, kVpnAuthErrorColor);
    ui->label_error->setPalette(errorPalette);

    vpnAuthSetLabel(ui->label_error, challenge.error);
    vpnAuthSetLabel(ui->label_banner, challenge.banner);
    vpnAuthSetLabel(ui->label_message, challenge.message);
    vpnAuthSetLabel(ui->label_url, {});
    vpnAuthSetLabel(ui->label_deadline, {});
    vpnAuthSetLabel(ui->label_status, {});

    const auto addStandard = [this](QDialogButtonBox::StandardButtons buttons) {
        ui->buttonBox->setStandardButtons(buttons);
    };

    if (challenge.kind == "credentials" || challenge.kind == "secret" || challenge.kind == "form") {
        if (challenge.kind == "credentials") buildCredentialFields();
        else if (challenge.kind == "secret") buildSecretField();
        else buildFormFields();
        addStandard(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        submitButton = ui->buttonBox->button(QDialogButtonBox::Ok);
        cancelButton = ui->buttonBox->button(QDialogButtonBox::Cancel);
        submitButton->setText(localOnly ? tr("Reconnect") : tr("Submit"));
        closeAction = localOnly ? CloseAction::None : CloseAction::Cancel;
    } else if (challenge.kind == "message") {
        if (challenge.message.isEmpty() && challenge.banner.isEmpty()) {
            vpnAuthSetLabel(ui->label_message, tr("The VPN server sent a message."));
        }
        addStandard(QDialogButtonBox::Ok);
        dismissButton = ui->buttonBox->button(QDialogButtonBox::Ok);
        closeAction = CloseAction::Acknowledge;
    } else if (challenge.kind == "open-url") {
        vpnAuthSetLabel(ui->label_url, challenge.url);
        vpnAuthSetLabel(ui->label_message,
                        challenge.message.isEmpty()
                            ? tr("Finish signing in at the address shown below, then close this window. "
                                 "The connection continues on its own once the server accepts it.")
                            : challenge.message);
        addStandard(QDialogButtonBox::Close | QDialogButtonBox::Cancel);
        dismissButton = ui->buttonBox->button(QDialogButtonBox::Close);
        cancelButton = ui->buttonBox->button(QDialogButtonBox::Cancel);
        if (!challenge.url.isEmpty()) {
            openUrlButton = ui->buttonBox->addButton(tr("Open in Browser"), QDialogButtonBox::ActionRole);
        }
        // The server resolves this one out of band, so dismissing must not cancel it.
        closeAction = CloseAction::None;
    } else if (challenge.kind == "browser") {
        vpnAuthSetLabel(ui->label_url, challenge.url);
        vpnAuthSetLabel(ui->label_message,
                        tr("This server requires single sign-on in a browser, which Throne does not "
                           "support yet. Cancel here and use a profile with direct credentials, or "
                           "supply an authentication cookie in the profile's advanced settings."));
        addStandard(QDialogButtonBox::Cancel);
        cancelButton = ui->buttonBox->button(QDialogButtonBox::Cancel);
        closeAction = CloseAction::Cancel;
    } else {
        vpnAuthSetLabel(ui->label_url, challenge.url);
        vpnAuthSetLabel(ui->label_message, tr("Unsupported authentication request: %1").arg(challenge.kind));
        addStandard(QDialogButtonBox::Cancel);
        cancelButton = ui->buttonBox->button(QDialogButtonBox::Cancel);
        closeAction = CloseAction::Cancel;
    }

    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, [this](QAbstractButton *button) {
        if (button == openUrlButton) {
            QDesktopServices::openUrl(QUrl(challenge.url));
            return;
        }
        if (button == submitButton) {
            submit();
            return;
        }
        if (button == cancelButton) {
            closeAction = CloseAction::Cancel;
            reject();
            return;
        }
        if (button == dismissButton) reject();
    });

    if (challenge.deadline > 0) {
        deadlineTimer = new QTimer(this);
        deadlineTimer->setInterval(1000);
        connect(deadlineTimer, &QTimer::timeout, this, [this] { refreshDeadline(); });
        deadlineTimer->start();
        vpnAuthSetLabel(ui->label_deadline,
                        tr("Expires in %1").arg(vpnAuthRemainingText(challenge.deadline - QDateTime::currentSecsSinceEpoch())));
    }

    fitToContent();
}

void DialogVpnAuth::fitToContent() {
    const auto *scr = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    const auto available = scr != nullptr ? scr->availableGeometry().size() : QSize(1024, 768);

    auto *content = ui->scroll_content;
    if (auto *contentLayout = content->layout(); contentLayout != nullptr) contentLayout->activate();

    const auto metrics = fontMetrics();
    const auto preferred = qMax(content->sizeHint().width(), metrics.averageCharWidth() * 56);
    const auto width = qMin(preferred, available.width() * 3 / 5);
    const auto natural = content->hasHeightForWidth() ? content->heightForWidth(width)
                                                      : content->sizeHint().height();
    const auto height = qMin(natural, available.height() * 3 / 5);

    // The reserved bar lane stops an appearing scrollbar re-wrapping the text taller.
    ui->scroll->setMinimumSize(width + ui->scroll->verticalScrollBar()->sizeHint().width(), height);
    // QScrollArea::sizeHint() clamps to 36x24 cells and follows its widget's narrow hint.
    ui->scroll->setMaximumHeight(height);
    resize(sizeHint().boundedTo(available));
    // The labels outside the scroll area only report a one-line hint, so re-ask at the real width.
    if (auto *dialogLayout = layout(); dialogLayout != nullptr && dialogLayout->hasHeightForWidth()) {
        const auto needed = dialogLayout->totalHeightForWidth(this->width());
        if (needed > this->height()) resize(this->width(), qMin(needed, available.height()));
    }
    ui->scroll->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->scroll->setMinimumSize(0, qMin(height, metrics.height() * 6));
}

DialogVpnAuth::~DialogVpnAuth()
{
    delete ui;
}

void DialogVpnAuth::addRow(const QString &label, QWidget *field) {
    ui->form_layout->addRow(label, field);
}

void DialogVpnAuth::buildCredentialFields() {
    usernameEdit = new QLineEdit(ui->form_container);
    usernameEdit->setText(challenge.username);
    addRow(tr("Username"), usernameEdit);

    passwordEdit = new QLineEdit(ui->form_container);
    passwordEdit->setEchoMode(QLineEdit::Password);
    addRow(tr("Password"), passwordEdit);

    if (!localOnly) {
        // The core packs this answer into SCRV1 alongside the password; omitting it fails the attempt.
        secretEdit = new QLineEdit(ui->form_container);
        if (!challenge.echo) secretEdit->setEchoMode(QLineEdit::Password);
        addRow(challenge.message.isEmpty() ? tr("Answer") : challenge.message, secretEdit);
        if (!challenge.message.isEmpty()) vpnAuthSetLabel(ui->label_message, {});
    }

    if (challenge.username.isEmpty()) usernameEdit->setFocus();
    else passwordEdit->setFocus();
}

void DialogVpnAuth::buildSecretField() {
    secretEdit = new QLineEdit(ui->form_container);
    if (!challenge.echo) secretEdit->setEchoMode(QLineEdit::Password);
    addRow(tr("Answer"), secretEdit);
    secretEdit->setFocus();
}

void DialogVpnAuth::buildFormFields() {
    QWidget *first = nullptr;
    for (const auto &field : challenge.fields) {
        const auto label = field.label.isEmpty() ? field.submissionKey : field.label;
        QWidget *widget = nullptr;
        if (field.kind == "select") {
            auto *combo = new NoWheelComboBox(ui->form_container);
            for (const auto &option : field.options) {
                combo->addItem(option.second.isEmpty() ? option.first : option.second, option.first);
            }
            const auto preselect = combo->findData(field.value);
            if (preselect >= 0) combo->setCurrentIndex(preselect);
            widget = combo;
        } else {
            auto *edit = new QLineEdit(ui->form_container);
            if (field.kind == "password") edit->setEchoMode(QLineEdit::Password);
            edit->setText(field.value);
            widget = edit;
        }
        addRow(label, widget);
        formWidgets.append({field, widget});
        if (first == nullptr) first = widget;
    }
    if (first != nullptr) first->setFocus();
}

void DialogVpnAuth::setFieldsEnabled(bool enabled) {
    ui->form_container->setEnabled(enabled);
    if (submitButton != nullptr) submitButton->setEnabled(enabled);
}

void DialogVpnAuth::refreshDeadline() {
    const auto remaining = challenge.deadline - QDateTime::currentSecsSinceEpoch();
    if (remaining <= 0) {
        if (deadlineTimer != nullptr) deadlineTimer->stop();
        // The core drops an expired challenge itself, so cancelling it would only error.
        closeAction = CloseAction::None;
        vpnAuthSetLabel(ui->label_status, tr("This request expired."));
        reject();
        return;
    }
    vpnAuthSetLabel(ui->label_deadline, tr("Expires in %1").arg(vpnAuthRemainingText(remaining)));
}

QString DialogVpnAuth::enteredUsername() const {
    return usernameEdit != nullptr ? usernameEdit->text() : QString();
}

QString DialogVpnAuth::enteredPassword() const {
    return passwordEdit != nullptr ? passwordEdit->text() : QString();
}

void DialogVpnAuth::submit() {
    if (submitting) return;
    if (localOnly) {
        finishing = true;
        done(QDialog::Accepted);
        return;
    }
    submitting = true;
    setFieldsEnabled(false);
    vpnAuthSetLabel(ui->label_status, tr("Submitting..."));

    QString username, password, secret;
    QMap<QString, QString> formValues;
    if (usernameEdit != nullptr) username = usernameEdit->text();
    if (passwordEdit != nullptr) password = passwordEdit->text();
    if (secretEdit != nullptr) secret = secretEdit->text();
    for (const auto &[field, widget] : formWidgets) {
        if (auto *combo = qobject_cast<QComboBox *>(widget); combo != nullptr) {
            formValues.insert(field.submissionKey, combo->currentData().toString());
        } else if (auto *edit = qobject_cast<QLineEdit *>(widget); edit != nullptr) {
            formValues.insert(field.submissionKey, edit->text());
        }
    }

    const auto tag = challenge.endpointTag;
    const auto id = challenge.id;
    QPointer<DialogVpnAuth> self(this);
    runOnNewThread([self, tag, id, username, password, secret, formValues] {
        bool rpcOK = false;
        const auto error = API::defaultClient->SubmitVPNChallenge(&rpcOK, tag, id, username, password, secret, formValues);
        runOnUiThread([self, error] {
            if (self == nullptr) return;
            self->submitting = false;
            if (error.isEmpty()) {
                self->finishing = true;
                self->done(QDialog::Accepted);
                return;
            }
            self->setFieldsEnabled(true);
            vpnAuthSetLabel(self->ui->label_status, error);
        });
    });
}

void DialogVpnAuth::reject() {
    if (finishing) {
        QDialog::reject();
        return;
    }
    finishing = true;
    if (closeAction != CloseAction::None) {
        const auto tag = challenge.endpointTag;
        const auto id = challenge.id;
        const auto acknowledge = closeAction == CloseAction::Acknowledge;
        runOnNewThread([tag, id, acknowledge] {
            bool rpcOK = false;
            if (acknowledge) API::defaultClient->SubmitVPNChallenge(&rpcOK, tag, id, {}, {}, {});
            else API::defaultClient->CancelVPNChallenge(&rpcOK, tag, id);
        });
    }
    QDialog::reject();
}
