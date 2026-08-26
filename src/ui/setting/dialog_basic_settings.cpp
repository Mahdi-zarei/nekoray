#include "include/ui/setting/dialog_basic_settings.h"

#include "include/ui/widget/json/JsonEditorDialog.h"
#include "include/ui/widget/json/SchemaStore.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/setting/Icon.hpp"
#include "include/global/GuiUtils.hpp"
#include "include/global/Configs.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/DeviceDetailsHelper.hpp"

#include <QStyleFactory>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include <QBrush>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <qfontdatabase.h>
#include <QDateTime>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <QCheckBox>
#include <QScreen>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>

#include "include/ui/mainwindow.h"

DialogBasicSettings::DialogBasicSettings(QWidget *parent)
    : QDialog(parent), ui(new Ui::DialogBasicSettings) {
    ui->setupUi(this);
    ADD_ASTERISK(this);

    ui->inbound_socks_port_l->setText(ui->inbound_socks_port_l->text().replace("Socks", "Mixed (SOCKS+HTTP)"));
    ui->log_level->addItems(QString("trace debug info warn error fatal panic").split(" "));
    ui->xray_loglevel->addItems(Configs::Xray::XrayLogLevels);
    ui->disable_stats->setChecked(Configs::dataManager->settingsRepo->disable_traffic_stats);
    ui->proxy_scheme->setCurrentText(Configs::dataManager->settingsRepo->proxy_scheme);

    D_LOAD_STRING(inbound_address)
    CACHE.custom_inbound = Configs::dataManager->settingsRepo->custom_inbound;
    D_LOAD_INT(inbound_socks_port)
    ui->random_listen_port->setChecked(Configs::dataManager->settingsRepo->random_inbound_port);
    D_LOAD_INT(test_concurrent)
    D_LOAD_STRING(test_latency_url)
    D_LOAD_BOOL(disable_tray)
    ui->reset_proxy_on_disable_sp->setChecked(Configs::dataManager->settingsRepo->reset_proxy_on_disable_sp);
    D_LOAD_BOOL(set_socks_system_proxy)
    ui->url_timeout->setText(Int2String(Configs::dataManager->settingsRepo->url_test_timeout_ms));
    ui->speedtest_mode->setCurrentIndex(Configs::dataManager->settingsRepo->speed_test_mode);
    ui->test_timeout->setText(Int2String(Configs::dataManager->settingsRepo->speed_test_timeout_ms));
    ui->simple_down_url->setText(Configs::dataManager->settingsRepo->simple_dl_url);
    ui->allow_beta->setChecked(Configs::dataManager->settingsRepo->allow_beta_update);
    ui->disable_mixed_inbound->setChecked(Configs::dataManager->settingsRepo->disable_mixed_inbound);
    D_LOAD_BOOL(inbound_auth)
    D_LOAD_STRING(inbound_user)
    D_LOAD_STRING(inbound_pass)

    connect(ui->custom_inbound_edit, &QPushButton::clicked, this, [=,this] {
        C_EDIT_JSON_ALLOW_EMPTY(custom_inbound, JsonEdit::SingBox::Config)
    });
    connect(ui->disable_tray, &QCheckBox::stateChanged, this, [=,this](const bool &) {
        CACHE.updateDisableTray = true;
    });
    connect(ui->random_listen_port, &QCheckBox::stateChanged, this, [=,this](const bool &state)
    {
        if (state)
        {
            ui->inbound_socks_port->setDisabled(true);
        } else
        {
            ui->inbound_socks_port->setDisabled(false);
        }
    });

#ifndef Q_OS_WIN
    ui->proxy_scheme_l->hide();
    ui->proxy_scheme->hide();
    ui->windows_no_admin->hide();
#endif

#ifndef Q_OS_LINUX
    // Only the Linux path writes per-protocol desktop proxy entries.
    ui->set_socks_system_proxy->hide();
#endif

    ui->max_log_line->setText(QString::number(Configs::dataManager->settingsRepo->max_log_line));
    D_LOAD_BOOL(log_auto_scroll)
    ui->log_level->setCurrentText(Configs::dataManager->settingsRepo->log_level);
    ui->xray_loglevel->setCurrentText(Configs::dataManager->settingsRepo->xray_log_level);
    ui->enable_log_include->setChecked(Configs::dataManager->settingsRepo->log_enable_include);
    ui->enable_log_exclude->setChecked(Configs::dataManager->settingsRepo->log_enable_exclude);
    ui->log_include_keyword->setText(Configs::dataManager->settingsRepo->log_include_keyword.join("\n"));
    ui->log_exclude_keyword->setText(Configs::dataManager->settingsRepo->log_exclude_keyword.join("\n"));
    ui->log_include_regex->setText(Configs::dataManager->settingsRepo->log_include_regex.join("\n"));
    ui->log_exclude_regex->setText(Configs::dataManager->settingsRepo->log_exclude_regex.join("\n"));
    applyRegexHighlighting();

    connect(ui->log_include_regex, &QTextEdit::textChanged, this, [this] { applyRegexHighlighting(); });
    connect(ui->log_exclude_regex, &QTextEdit::textChanged, this, [this] { applyRegexHighlighting(); });

    ui->connection_statistics->setChecked(Configs::dataManager->settingsRepo->enable_stats);
    ui->disable_traffic_aggregation->setChecked(Configs::dataManager->settingsRepo->disable_traffic_aggregation);
    ui->show_sys_dns->setChecked(Configs::dataManager->settingsRepo->show_system_dns);
    connect(ui->show_sys_dns, &QCheckBox::stateChanged, this, [=]
    {
        CACHE.updateSystemDns = true;
    });
#ifndef Q_OS_WIN
    ui->show_sys_dns->hide();
#endif
    D_LOAD_BOOL(start_minimal)
    ui->skip_delete_confirm->setChecked(Configs::dataManager->settingsRepo->skip_delete_confirmation);
    D_LOAD_BOOL(show_config_security)
    ui->language->setCurrentIndex(Configs::dataManager->settingsRepo->language);
    connect(ui->language, &QComboBox::currentIndexChanged, this, [=,this](int index) {
        CACHE.needRestart = true;
    });
    connect(ui->font, &QComboBox::currentTextChanged, this, [=,this](const QString &fontName) {
        auto font = qApp->font();
        font.setFamily(fontName);
        qApp->setFont(font);
        Configs::dataManager->settingsRepo->font = fontName;
        Configs::dataManager->settingsRepo->Save();
        adjustSize();
    });
    for (int i=7;i<=26;i++) {
        ui->font_size->addItem(Int2String(i));
    }
    ui->font_size->setCurrentText(Int2String(qApp->font().pointSize()));
    connect(ui->font_size, &QComboBox::currentTextChanged, this, [=,this](const QString &sizeStr) {
        auto font = qApp->font();
        font.setPointSize(sizeStr.toInt());
        qApp->setFont(font);
        Configs::dataManager->settingsRepo->font_size = sizeStr.toInt();
        Configs::dataManager->settingsRepo->Save();
        adjustSize();
    });
    ui->theme->addItems(QStyleFactory::keys());
    ui->theme->addItem("QDarkStyle");
    // Custom stylesheet themes, not QStyleFactory keys.
    ui->theme->addItems({"FlatGray", "LightBlue", "SoftPink", "BlackSoft"});
    ui->enable_custom_icon->setChecked(Configs::dataManager->settingsRepo->use_custom_icons);
    connect(ui->select_custom_icon, &QPushButton::clicked, this, [=, this] {
        auto n = QMessageBox::information(this, "Custom Icon Manual", tr(Configs::Information::CustomIconManual.toStdString().c_str()), QMessageBox::Open | QMessageBox::Cancel);
        if (n == QMessageBox::Open) {
            auto fileNames = QFileDialog::getOpenFileNames(this,
                tr("Select png icons"), QDir::homePath(), tr("Image Files (*.png)"));
            QString errors;
            for (const auto& fileName : fileNames) {
                CACHE.updateTrayIcon = true;
                QFileInfo fileInfo(fileName);
                if (auto pixMap = QPixmap(fileName); pixMap.isNull()) errors += "Failed to load " + fileName + "\n";
                else if (pixMap.width() != pixMap.height()) errors += "Image does not have equal width and height: " + fileName + "\n";
                else if (!Configs::Information::iconNames.contains(fileInfo.fileName())) errors += "Icon name is not valid: " + fileInfo.fileName() + "\n";
                else {
                    QFile::remove(QDir("icons").filePath(fileInfo.fileName()));
                    if (!QFile::copy(fileName, QDir("icons").filePath(fileInfo.fileName()))) errors += "Failed to copy " + fileName + "\n";
                }
            }
            if (!errors.isEmpty()) {
                QMessageBox::warning(this, "Select custom image error", errors);
            }
        }
    });
    bool ok;
    auto themeId = Configs::dataManager->settingsRepo->theme.toInt(&ok);
    if (ok) {
        ui->theme->setCurrentIndex(themeId);
    } else {
        ui->theme->setCurrentText(Configs::dataManager->settingsRepo->theme);
    }
    connect(ui->theme, &QComboBox::currentIndexChanged, this, [=,this](int index) {
        themeManager->ApplyTheme(ui->theme->currentText());
        Configs::dataManager->settingsRepo->theme = ui->theme->currentText();
        Configs::dataManager->settingsRepo->Save();
    });

    ui->user_agent->setText(Configs::dataManager->settingsRepo->user_agent);
    ui->user_agent->setPlaceholderText(Configs::dataManager->settingsRepo->GetUserAgent(true));
    D_LOAD_BOOL(net_use_proxy)
    D_LOAD_BOOL(allow_stopping_active_profile)
    D_LOAD_BOOL(sub_clear)
    D_LOAD_BOOL(sub_show_change_popup)
    D_LOAD_BOOL(net_insecure)
    D_LOAD_BOOL(sub_send_hwid)
    D_LOAD_STRING(sub_custom_hwid_params)
    D_LOAD_INT_ENABLE(sub_auto_update, sub_auto_update_enable)
    D_LOAD_INT_ENABLE(route_auto_update, route_auto_update_enable)
    auto details = GetDeviceDetails();
	ui->sub_send_hwid->setToolTip(
        ui->sub_send_hwid->toolTip()
            .arg(details.hwid.isEmpty() ? "N/A" : details.hwid,
                details.os.isEmpty() ? "N/A" : details.os,
                details.osVersion.isEmpty() ? "N/A" : details.osVersion,
                details.model.isEmpty() ? "N/A" : details.model));

    ui->dns_in_port->setValidator(new QIntValidator(1, 65535, ui->dns_in_port));
    ui->dns_in_port->setText(Int2String(Configs::dataManager->settingsRepo->core_dns_in_port));

    ui->core_box_clash_listen_addr->setText(Configs::dataManager->settingsRepo->core_box_clash_listen_addr);
    ui->core_box_clash_api->setValidator(new QIntValidator(1, 65535, ui->core_box_clash_api));
    ui->core_box_clash_api->setText(Configs::dataManager->settingsRepo->core_box_clash_api > 0
                                        ? Int2String(Configs::dataManager->settingsRepo->core_box_clash_api)
                                        : "");
    ui->core_box_clash_api_secret->setText(Configs::dataManager->settingsRepo->core_box_clash_api_secret);

    ui->core_box_api_port->setValidator(new QIntValidator(1, 65535, ui->core_box_api_port));
    ui->core_box_api_port->setText(Configs::dataManager->settingsRepo->core_box_api_port > 0
                                       ? Int2String(Configs::dataManager->settingsRepo->core_box_api_port)
                                       : "");
    ui->core_box_api_secret->setText(Configs::dataManager->settingsRepo->core_box_api_secret);
    connect(ui->core_box_api_regen, &QPushButton::clicked, this, [this] {
        ui->core_box_api_secret->setText(QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-'));
    });

    ui->vless_xray_pref->addItems(Configs::Xray::XrayVlessPreferenceString);
    ui->vless_xray_pref->setCurrentIndex(Configs::dataManager->settingsRepo->xray_vless_preference);
    D_LOAD_STRING(xray_geoip_url)
    D_LOAD_STRING(xray_geosite_url)
    ui->xray_geoip_url->setPlaceholderText("https://github.com/Loyalsoldier/v2ray-rules-dat/raw/release/geoip.dat");
    ui->xray_geosite_url->setPlaceholderText("https://github.com/Loyalsoldier/v2ray-rules-dat/raw/release/geosite.dat");

    ui->ntp_enable->setChecked(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_server->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_port->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_interval->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_outbound->setEnabled(Configs::dataManager->settingsRepo->enable_ntp);
    ui->ntp_server->setText(Configs::dataManager->settingsRepo->ntp_server_address);
    ui->ntp_port->setText(Int2String(Configs::dataManager->settingsRepo->ntp_server_port));
    ui->ntp_interval->setCurrentText(Configs::dataManager->settingsRepo->ntp_interval);
    ui->ntp_outbound->setCurrentText(Configs::dataManager->settingsRepo->ntp_outbound);
    connect(ui->ntp_enable, &QCheckBox::stateChanged, this, [=,this](const bool &state) {
        ui->ntp_server->setEnabled(state);
        ui->ntp_port->setEnabled(state);
        ui->ntp_interval->setEnabled(state);
        ui->ntp_outbound->setEnabled(state);
    });

    ui->disable_priv_req->setChecked(Configs::dataManager->settingsRepo->disable_privilege_req);
    ui->windows_no_admin->setChecked(Configs::dataManager->settingsRepo->disable_run_admin);
    ui->mozilla_cert->setChecked(Configs::dataManager->settingsRepo->use_mozilla_certs);

    D_LOAD_BOOL(skip_cert)

    // The .ui geometry underruns real font metrics/translations and Qt then overlaps the rows (#1671).
    QSize want = sizeHint();
    if (const QScreen *scr = parent ? parent->screen() : screen()) {
        const QRect avail = scr->availableGeometry();
        want = want.boundedTo(QSize(avail.width() - 24, avail.height() - 72));
    }
    resize(want);
}

DialogBasicSettings::~DialogBasicSettings() {
    delete ui;
}

static void highlightRegexLines(QTextEdit *edit) {
    if (!edit || !edit->document()) return;
    edit->blockSignals(true);
    QTextDocument *doc = edit->document();
    QRegularExpression validator;
    for (int i = 0; i < doc->blockCount(); ++i) {
        QTextBlock block = doc->findBlockByNumber(i);
        QString line = block.text();
        QTextBlockFormat fmt = block.blockFormat();
        if (line.trimmed().isEmpty()) {
            fmt.setBackground(Qt::NoBrush);
            QTextCursor cur(block);
            cur.setBlockFormat(fmt);
            continue;
        }
        validator.setPattern(line);
        fmt.setBackground(QBrush(validator.isValid() ? Qt::darkGreen : Qt::darkRed));
        QTextCursor cur(block);
        cur.setBlockFormat(fmt);
    }
    edit->blockSignals(false);
}

void DialogBasicSettings::applyRegexHighlighting() {
    highlightRegexLines(ui->log_include_regex);
    highlightRegexLines(ui->log_exclude_regex);
}

void DialogBasicSettings::accept() {
    bool needChoosePort = false;

    D_SAVE_STRING(inbound_address)
    Configs::dataManager->settingsRepo->custom_inbound = CACHE.custom_inbound;
    D_SAVE_INT(inbound_socks_port)
    if (!Configs::dataManager->settingsRepo->random_inbound_port && ui->random_listen_port->isChecked())
    {
        needChoosePort = true;
    }
    Configs::dataManager->settingsRepo->random_inbound_port = ui->random_listen_port->isChecked();
    D_SAVE_INT(test_concurrent)
    D_SAVE_STRING(test_latency_url)
    D_SAVE_BOOL(disable_tray)
    Configs::dataManager->settingsRepo->proxy_scheme = ui->proxy_scheme->currentText().toLower();
    Configs::dataManager->settingsRepo->speed_test_mode = ui->speedtest_mode->currentIndex();
    Configs::dataManager->settingsRepo->simple_dl_url = ui->simple_down_url->text();
    Configs::dataManager->settingsRepo->url_test_timeout_ms = ui->url_timeout->text().toInt();
    Configs::dataManager->settingsRepo->speed_test_timeout_ms = ui->test_timeout->text().toInt();
    Configs::dataManager->settingsRepo->allow_beta_update = ui->allow_beta->isChecked();
    Configs::dataManager->settingsRepo->disable_mixed_inbound = ui->disable_mixed_inbound->isChecked();
    Configs::dataManager->settingsRepo->reset_proxy_on_disable_sp = ui->reset_proxy_on_disable_sp->isChecked();
    D_SAVE_BOOL(set_socks_system_proxy)
    D_SAVE_BOOL(inbound_auth)
    D_SAVE_STRING(inbound_user)
    D_SAVE_STRING(inbound_pass)

    auto oldMaxLogLines = Configs::dataManager->settingsRepo->max_log_line;
    Configs::dataManager->settingsRepo->max_log_line = ui->max_log_line->text().toInt();
    if (oldMaxLogLines != Configs::dataManager->settingsRepo->max_log_line) CACHE.updateMaxLogLines = true;
    Configs::dataManager->settingsRepo->log_level = ui->log_level->currentText();
    Configs::dataManager->settingsRepo->xray_log_level = ui->xray_loglevel->currentText();
    Configs::dataManager->settingsRepo->log_enable_include = ui->enable_log_include->isChecked();
    Configs::dataManager->settingsRepo->log_enable_exclude = ui->enable_log_exclude->isChecked();
    D_SAVE_BOOL(log_auto_scroll)
    Configs::dataManager->settingsRepo->log_include_keyword = SplitAndTrim(ui->log_include_keyword->toPlainText(), "\n", false);
    Configs::dataManager->settingsRepo->log_exclude_keyword = SplitAndTrim(ui->log_exclude_keyword->toPlainText(), "\n", false);

    Configs::dataManager->settingsRepo->log_include_regex.clear();
    Configs::dataManager->settingsRepo->log_exclude_regex.clear();
    QRegularExpression regexValidator;
    for (QStringList log_include_lines = SplitAndTrim(ui->log_include_regex->toPlainText(), "\n", false); const QString &line : log_include_lines) {
        if (regexValidator.setPattern(line); regexValidator.isValid()) Configs::dataManager->settingsRepo->log_include_regex << line;
    }
    for (QStringList log_exclude_lines = SplitAndTrim(ui->log_exclude_regex->toPlainText(), "\n", false); const QString &line : log_exclude_lines) {
        if (regexValidator.setPattern(line); regexValidator.isValid()) Configs::dataManager->settingsRepo->log_exclude_regex << line;
    }

    Configs::dataManager->settingsRepo->enable_stats = ui->connection_statistics->isChecked();
    Configs::dataManager->settingsRepo->disable_traffic_aggregation = ui->disable_traffic_aggregation->isChecked();
    Configs::dataManager->settingsRepo->language = ui->language->currentIndex();
    auto oldUseCustomIcon = Configs::dataManager->settingsRepo->use_custom_icons;
    Configs::dataManager->settingsRepo->use_custom_icons = ui->enable_custom_icon->isChecked();
    if (oldUseCustomIcon != Configs::dataManager->settingsRepo->use_custom_icons) CACHE.updateTrayIcon = true;
    D_SAVE_BOOL(start_minimal)
    Configs::dataManager->settingsRepo->skip_delete_confirmation = ui->skip_delete_confirm->isChecked();
    bool profileListDisplayChanged =
        Configs::dataManager->settingsRepo->show_config_security != ui->show_config_security->isChecked();
    D_SAVE_BOOL(show_config_security)
    Configs::dataManager->settingsRepo->show_system_dns = ui->show_sys_dns->isChecked();

    if (Configs::dataManager->settingsRepo->max_log_line <= 0) {
        Configs::dataManager->settingsRepo->max_log_line = 200;
    }

    // The PeriodicRunner reads these intervals live; no timer needs restarting.

    Configs::dataManager->settingsRepo->user_agent = ui->user_agent->text();
    D_SAVE_BOOL(net_use_proxy)
    D_SAVE_BOOL(allow_stopping_active_profile)
    D_SAVE_BOOL(sub_clear)
    D_SAVE_BOOL(sub_show_change_popup)
    D_SAVE_BOOL(net_insecure)
    D_SAVE_BOOL(sub_send_hwid)
    D_SAVE_STRING(sub_custom_hwid_params)
    D_SAVE_INT_ENABLE(sub_auto_update, sub_auto_update_enable)
    D_SAVE_INT_ENABLE(route_auto_update, route_auto_update_enable)

    Configs::dataManager->settingsRepo->disable_traffic_stats = ui->disable_stats->isChecked();
    Configs::dataManager->settingsRepo->core_dns_in_port = ui->dns_in_port->text().toInt();
    Configs::dataManager->settingsRepo->core_box_clash_listen_addr = ui->core_box_clash_listen_addr->text();
    Configs::dataManager->settingsRepo->core_box_clash_api = ui->core_box_clash_api->text().toInt();
    Configs::dataManager->settingsRepo->core_box_clash_api_secret = ui->core_box_clash_api_secret->text();
    Configs::dataManager->settingsRepo->core_box_api_port = ui->core_box_api_port->text().toInt();
    // Blank means "no authentication" to sing-box, so never let the field clear it.
    if (const auto secret = ui->core_box_api_secret->text(); !secret.isEmpty())
        Configs::dataManager->settingsRepo->core_box_api_secret = secret;

    Configs::dataManager->settingsRepo->xray_vless_preference = static_cast<Configs::Xray::XrayVlessPreference>(ui->vless_xray_pref->currentIndex());
    D_SAVE_STRING(xray_geoip_url)
    D_SAVE_STRING(xray_geosite_url)

    Configs::dataManager->settingsRepo->enable_ntp = ui->ntp_enable->isChecked();
    Configs::dataManager->settingsRepo->ntp_server_address = ui->ntp_server->text();
    Configs::dataManager->settingsRepo->ntp_server_port = ui->ntp_port->text().toInt();
    Configs::dataManager->settingsRepo->ntp_interval = ui->ntp_interval->currentText();
    Configs::dataManager->settingsRepo->ntp_outbound = ui->ntp_outbound->currentText();

    D_SAVE_BOOL(skip_cert)
    Configs::dataManager->settingsRepo->disable_privilege_req = ui->disable_priv_req->isChecked();
    if (Configs::dataManager->settingsRepo->disable_run_admin != ui->windows_no_admin->isChecked()) CACHE.updateDisableAdmin = true;
    Configs::dataManager->settingsRepo->disable_run_admin = ui->windows_no_admin->isChecked();
    Configs::dataManager->settingsRepo->use_mozilla_certs = ui->mozilla_cert->isChecked();

    QStringList changes;
    if (CACHE.needRestart) changes << MwArg::NeedRestart;
    if (CACHE.updateDisableTray) changes << MwArg::DisableTray;
    if (CACHE.updateSystemDns) changes << MwArg::SystemDns;
    if (CACHE.updateTrayIcon) changes << MwArg::TrayIcon;
    if (CACHE.updateMaxLogLines) changes << MwArg::MaxLogLines;
    if (CACHE.updateDisableAdmin) changes << MwArg::DisableAdmin;
    if (needChoosePort) changes << MwArg::ChoosePort;
    if (profileListDisplayChanged) changes << MwArg::ProfileListDisplay;
    MW_dialog_message(MwMessage::UpdateSettings, changes);
    QDialog::accept();
}

// v1 archives carry no "parts" metadata and are read back as full snapshots.
static constexpr quint32 BACKUP_FORMAT_VERSION = 2;
static constexpr int BACKUP_CONTENT_VERSION = 2;

static Configs::BackupParts BackupPartsFromMeta(quint32 formatVersion, const QJsonObject& meta,
                                                const QMap<QString, QByteArray>& files) {
    Configs::BackupParts p;
    bool hasIcons = false;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (it.key().startsWith("icons/")) { hasIcons = true; break; }
    }
    if (formatVersion >= 2 && meta.contains("parts")) {
        const QJsonObject po = meta["parts"].toObject();
        p.profiles = po["profiles"].toBool() && files.contains("database");
        p.routes = po["routes"].toBool() && files.contains("database");
        p.settings = po["settings"].toBool() && files.contains("database");
        p.otp = po["otp"].toBool() && files.contains("database");
        p.icons = po["icons"].toBool() && hasIcons;
    } else {
        p.profiles = p.routes = p.settings = files.contains("database");
        p.icons = hasIcons;
    }
    return p;
}

void DialogBasicSettings::downloadXrayGeoAsset(const QString &url, const QString &fileName) {
    const QString effectiveUrl = url.trimmed();
    if (effectiveUrl.isEmpty()) {
        QMessageBox::warning(this, tr("Download geo asset"),
            tr("Please enter a URL for %1 first.").arg(fileName));
        return;
    }
    MW_show_log(tr("Downloading Xray geo asset: %1").arg(fileName));
    // DownloadAsset drives a blocking event loop; don't capture the dialog, it may close first.
    runOnNewThread([effectiveUrl, fileName] {
        const auto err = NetworkRequestHelper::DownloadAsset(effectiveUrl, fileName);
        runOnUiThread([err, fileName] {
            if (err.isEmpty()) {
                MW_show_log(QObject::tr("Downloaded Xray geo asset: %1").arg(fileName));
                QMessageBox::information(GetMainWindow(), QObject::tr("Download geo asset"),
                    QObject::tr("%1 was downloaded successfully.").arg(fileName));
            } else {
                MessageBoxWarning(QObject::tr("Download geo asset"),
                    QObject::tr("Failed to download %1:\n%2").arg(fileName, err));
            }
        });
    });
}

void DialogBasicSettings::on_xray_geoip_download_clicked() {
    QString url = ui->xray_geoip_url->text().trimmed();
    if (url.isEmpty()) url = ui->xray_geoip_url->placeholderText();
    downloadXrayGeoAsset(url, "geoip.dat");
}

void DialogBasicSettings::on_xray_geosite_download_clicked() {
    QString url = ui->xray_geosite_url->text().trimmed();
    if (url.isEmpty()) url = ui->xray_geosite_url->placeholderText();
    downloadXrayGeoAsset(url, "geosite.dat");
}

void DialogBasicSettings::on_backup_create_clicked() {
    Configs::BackupParts parts;
    parts.profiles = ui->backup_inc_profiles->isChecked();
    parts.routes = ui->backup_inc_routes->isChecked();
    parts.settings = ui->backup_inc_settings->isChecked();
    parts.otp = ui->backup_inc_otp->isChecked();
    parts.icons = ui->backup_inc_icons->isChecked();

    if (!parts.any()) {
        QMessageBox::warning(this, tr("Create Backup"),
            tr("Select at least one part to include in the backup."));
        return;
    }

    if (parts.settings) Configs::dataManager->settingsRepo->Save();

    QString filePath = QFileDialog::getSaveFileName(
        this,
        tr("Create Backup"),
        QDir::homePath() + "/Throne-backup.thrbackup",
        tr("Throne Backup (*.thrbackup)")
    );
    if (filePath.isEmpty()) return;

    QMap<QString, QByteArray> files;

    if (parts.anyDb()) {
        QString tempDbPath = QDir::temp().filePath("Thr_backup_tmp.db");
        QFile::remove(tempDbPath);

        try {
            Configs::dataManager->getDatabase().backupSelective(tempDbPath.toStdString(), parts);
        } catch (std::exception& e) {
            QFile::remove(tempDbPath);
            QMessageBox::critical(this, tr("Backup Failed"),
                tr("Failed to create database snapshot: %1").arg(e.what()));
            return;
        }

        QFile tempDbFile(tempDbPath);
        if (!tempDbFile.open(QIODevice::ReadOnly)) {
            QFile::remove(tempDbPath);
            QMessageBox::critical(this, tr("Backup Failed"), tr("Failed to read database snapshot."));
            return;
        }
        files["database"] = tempDbFile.readAll();
        tempDbFile.close();
        QFile::remove(tempDbPath);
    }

    if (parts.icons) {
        QDir iconsDir("icons");
        if (iconsDir.exists()) {
            for (const QFileInfo& entry : iconsDir.entryInfoList(QDir::Files)) {
                QFile iconFile(entry.absoluteFilePath());
                if (iconFile.open(QIODevice::ReadOnly)) {
                    files["icons/" + entry.fileName()] = iconFile.readAll();
                }
            }
        }
    }

    QFile outFile(filePath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Backup Failed"),
            tr("Cannot write to: %1").arg(filePath));
        return;
    }

    QDataStream stream(&outFile);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);

    stream.writeRawData("THRN", 4);
    stream << BACKUP_FORMAT_VERSION;

    QJsonObject partsObj;
    partsObj["profiles"] = parts.profiles;
    partsObj["routes"] = parts.routes;
    partsObj["settings"] = parts.settings;
    partsObj["otp"] = parts.otp;
    partsObj["icons"] = parts.icons;

    QJsonObject meta;
    meta["backup_version"] = BACKUP_CONTENT_VERSION;
    meta["created_at"] = QDateTime::currentDateTime().toString(Qt::TextDate);
    meta["platform"] = QSysInfo::kernelType();
    meta["parts"] = partsObj;
    stream << QString::fromUtf8(QJsonDocument(meta).toJson(QJsonDocument::Compact));

    stream << files;
    outFile.close();

    QStringList included;
    if (parts.profiles) included << tr("Profiles");
    if (parts.routes) included << tr("Routing profiles");
    if (parts.settings) included << tr("Settings");
    if (parts.otp) included << tr("OTP profiles");
    if (parts.icons) included << tr("Custom icons");

    QMessageBox::information(this, tr("Backup Created"),
        tr("Backup created successfully:\n%1\n\nIncluded: %2")
            .arg(filePath, included.join(", ")));
}

void DialogBasicSettings::on_backup_restore_clicked() {
    QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Restore Backup"),
        QDir::homePath(),
        tr("Throne Backup (*.thrbackup)")
    );
    if (filePath.isEmpty()) return;

    QFile inFile(filePath);
    if (!inFile.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Restore Failed"),
            tr("Cannot open backup file: %1").arg(filePath));
        return;
    }

    QDataStream stream(&inFile);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setVersion(QDataStream::Qt_6_0);

    char magic[4];
    if (stream.readRawData(magic, 4) != 4 || strncmp(magic, "THRN", 4) != 0) {
        QMessageBox::critical(this, tr("Restore Failed"),
            tr("Not a valid Throne backup file."));
        return;
    }

    quint32 formatVersion;
    stream >> formatVersion;
    if (formatVersion < 1 || formatVersion > BACKUP_FORMAT_VERSION) {
        QMessageBox::critical(this, tr("Restore Failed"),
            tr("Unsupported backup format version: %1.\nThis backup may have been created with a newer version of the application.")
                .arg(formatVersion));
        return;
    }

    QString metaJson;
    stream >> metaJson;
    QJsonObject meta = QJsonDocument::fromJson(metaJson.toUtf8()).object();
    QString createdAt = meta["created_at"].toString();

    QMap<QString, QByteArray> files;
    stream >> files;
    inFile.close();

    Configs::BackupParts avail = BackupPartsFromMeta(formatVersion, meta, files);
    if (!avail.any()) {
        QMessageBox::critical(this, tr("Restore Failed"),
            tr("This backup file does not contain any restorable data."));
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Restore Backup"));
    auto* layout = new QVBoxLayout(&dlg);
    auto* header = new QLabel(
        tr("Backup created on %1.\nSelect which parts to restore:")
            .arg(createdAt.isEmpty() ? tr("unknown date") : createdAt), &dlg);
    header->setWordWrap(true);
    layout->addWidget(header);

    auto* cbProfiles = new QCheckBox(tr("Profiles (groups and proxies)"), &dlg);
    auto* cbRoutes = new QCheckBox(tr("Routing profiles"), &dlg);
    auto* cbSettings = new QCheckBox(tr("Settings"), &dlg);
    auto* cbOtp = new QCheckBox(tr("OTP profiles"), &dlg);
    auto* cbIcons = new QCheckBox(tr("Custom icons"), &dlg);
    for (auto* cb : {cbProfiles, cbRoutes, cbSettings, cbOtp, cbIcons}) cb->setChecked(true);
    cbProfiles->setEnabled(avail.profiles);
    cbProfiles->setChecked(avail.profiles);
    cbRoutes->setEnabled(avail.routes);
    cbRoutes->setChecked(avail.routes);
    cbSettings->setEnabled(avail.settings);
    cbSettings->setChecked(avail.settings);
    cbOtp->setEnabled(avail.otp);
    cbOtp->setChecked(avail.otp);
    cbIcons->setEnabled(avail.icons);
    cbIcons->setChecked(avail.icons);
    layout->addWidget(cbProfiles);
    layout->addWidget(cbRoutes);
    layout->addWidget(cbSettings);
    layout->addWidget(cbOtp);
    layout->addWidget(cbIcons);

    auto* warn = new QLabel(
        tr("Each selected part replaces the current data. This cannot be undone.\n"
           "Throne will restart to complete the restore."), &dlg);
    warn->setWordWrap(true);
    layout->addWidget(warn);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Restore"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    Configs::BackupParts chosen;
    chosen.profiles = avail.profiles && cbProfiles->isChecked();
    chosen.routes = avail.routes && cbRoutes->isChecked();
    chosen.settings = avail.settings && cbSettings->isChecked();
    chosen.otp = avail.otp && cbOtp->isChecked();
    chosen.icons = avail.icons && cbIcons->isChecked();

    if (!chosen.any()) {
        QMessageBox::warning(this, tr("Restore Backup"),
            tr("Select at least one part to restore."));
        return;
    }

    if (chosen.anyDb()) {
        QString tempDbPath = QDir::temp().filePath("Thr_restore_tmp.db");
        QFile::remove(tempDbPath);
        QFile tempDbFile(tempDbPath);
        if (!tempDbFile.open(QIODevice::WriteOnly)) {
            QMessageBox::critical(this, tr("Restore Failed"),
                tr("Failed to create temporary file for restore."));
            return;
        }
        tempDbFile.write(files["database"]);
        tempDbFile.close();

        try {
            Configs::dataManager->getDatabase().restoreSelective(tempDbPath.toStdString(), chosen);
        } catch (std::exception& e) {
            QFile::remove(tempDbPath);
            QMessageBox::critical(this, tr("Restore Failed"),
                tr("Failed to restore database: %1").arg(e.what()));
            return;
        }
        QFile::remove(tempDbPath);
    }

    if (chosen.icons) {
        QDir iconsDir("icons");
        iconsDir.mkpath(".");
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            if (it.key().startsWith("icons/")) {
                QString iconName = it.key().mid(6);
                if (iconName.isEmpty()) continue;
                QFile iconFile(iconsDir.filePath(iconName));
                if (iconFile.open(QIODevice::WriteOnly)) {
                    iconFile.write(it.value());
                }
            }
        }
    }

    // The exit path's settingsRepo->Save() would write the stale in-memory values back over the restore.
    if (chosen.settings) Configs::dataManager->settingsRepo->noSave = true;

    QMessageBox::information(this, tr("Restore Complete"),
        tr("Backup restored successfully. Throne will now restart for the changes to take effect."));
    MW_dialog_message(MwMessage::RestartProgram, {});
    QDialog::reject();
}

