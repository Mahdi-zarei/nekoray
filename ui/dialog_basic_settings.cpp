#include "dialog_basic_settings.h"
#include "ui_dialog_basic_settings.h"

#include "3rdparty/qv2ray/v2/ui/widgets/editors/w_JsonEditor.hpp"
#include "fmt/Preset.hpp"
#include "ui/ThemeManager.hpp"
#include "ui/Icon.hpp"
#include "main/GuiUtils.hpp"
#include "main/NekoGui.hpp"
#include "main/HTTPRequestHelper.hpp"

#include <QStyleFactory>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include <qfontdatabase.h>

class ExtraCoreWidget : public QWidget {
public:
    QString coreName;

    QLabel *label_name;
    MyLineEdit *lineEdit_path;
    QPushButton *pushButton_pick;

    explicit ExtraCoreWidget(QJsonObject *extraCore, const QString &coreName_,
                             QWidget *parent = nullptr)
        : QWidget(parent) {
        coreName = coreName_;
        label_name = new QLabel;
        label_name->setText(coreName);
        lineEdit_path = new MyLineEdit;
        lineEdit_path->setText(extraCore->value(coreName).toString());
        pushButton_pick = new QPushButton;
        pushButton_pick->setText(QObject::tr("Select"));
        auto layout = new QHBoxLayout;
        layout->addWidget(label_name);
        layout->addWidget(lineEdit_path);
        layout->addWidget(pushButton_pick);
        setLayout(layout);
        setContentsMargins(0, 0, 0, 0);
        //
        connect(pushButton_pick, &QPushButton::clicked, this, [=] {
            auto fn = QFileDialog::getOpenFileName(this, QObject::tr("Select"), QDir::currentPath(),
                                                   "", nullptr, QFileDialog::Option::ReadOnly);
            if (!fn.isEmpty()) {
                lineEdit_path->setText(fn);
            }
        });
        connect(lineEdit_path, &QLineEdit::textChanged, this, [=](const QString &newTxt) {
            extraCore->insert(coreName, newTxt);
        });
    }
};

DialogBasicSettings::DialogBasicSettings(QWidget *parent)
    : QDialog(parent), ui(new Ui::DialogBasicSettings) {
    ui->setupUi(this);
    ADD_ASTERISK(this);

    // Common
    ui->inbound_socks_port_l->setText(ui->inbound_socks_port_l->text().replace("Socks", "Mixed (SOCKS+HTTP)"));
    ui->log_level->addItems(QString("trace debug info warn error fatal panic").split(" "));
    ui->mux_protocol->addItems({"h2mux", "smux", "yamux"});
    ui->disable_stats->setChecked(NekoGui::dataStore->disable_traffic_stats);

    D_LOAD_STRING(inbound_address)
    D_LOAD_COMBO_STRING(log_level)
    CACHE.custom_inbound = NekoGui::dataStore->custom_inbound;
    D_LOAD_INT(inbound_socks_port)
    D_LOAD_INT(test_concurrent)
    D_LOAD_STRING(test_latency_url)

    connect(ui->custom_inbound_edit, &QPushButton::clicked, this, [=] {
        C_EDIT_JSON_ALLOW_EMPTY(custom_inbound)
    });

    // Style
    ui->connection_statistics_box->setDisabled(true);
    //
    D_LOAD_BOOL(start_minimal)
    D_LOAD_INT(max_log_line)
    //
    if (NekoGui::dataStore->traffic_loop_interval == 500) {
        ui->rfsh_r->setCurrentIndex(0);
    } else if (NekoGui::dataStore->traffic_loop_interval == 1000) {
        ui->rfsh_r->setCurrentIndex(1);
    } else if (NekoGui::dataStore->traffic_loop_interval == 2000) {
        ui->rfsh_r->setCurrentIndex(2);
    } else if (NekoGui::dataStore->traffic_loop_interval == 3000) {
        ui->rfsh_r->setCurrentIndex(3);
    } else if (NekoGui::dataStore->traffic_loop_interval == 5000) {
        ui->rfsh_r->setCurrentIndex(4);
    } else {
        ui->rfsh_r->setCurrentIndex(5);
    }
    //
    ui->language->setCurrentIndex(NekoGui::dataStore->language);
    connect(ui->language, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [=](int index) {
        CACHE.needRestart = true;
    });
    connect(ui->font, &QComboBox::currentTextChanged, this, [=](const QString &font) {
        qApp->setFont(font);
        NekoGui::dataStore->font = font;
        NekoGui::dataStore->Save();
        adjustSize();
    });
    for (int i=7;i<=26;i++) {
        ui->font_size->addItem(Int2String(i));
    }
    ui->font_size->setCurrentText(Int2String(qApp->font().pointSize()));
    connect(ui->font_size, &QComboBox::currentTextChanged, this, [=](const QString &sizeStr) {
        auto font = qApp->font();
        font.setPointSize(sizeStr.toInt());
        qApp->setFont(font);
        NekoGui::dataStore->font_size = sizeStr.toInt();
        NekoGui::dataStore->Save();
        adjustSize();
    });
    //
    ui->theme->addItems(QStyleFactory::keys());
    ui->theme->addItem("QDarkStyle");
    //
    bool ok;
    auto themeId = NekoGui::dataStore->theme.toInt(&ok);
    if (ok) {
        ui->theme->setCurrentIndex(themeId);
    } else {
        ui->theme->setCurrentText(NekoGui::dataStore->theme);
    }
    //
    connect(ui->theme, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [=](int index) {
        themeManager->ApplyTheme(ui->theme->currentText());
        NekoGui::dataStore->theme = ui->theme->currentText();
        NekoGui::dataStore->Save();
    });

    // Subscription

    ui->user_agent->setText(NekoGui::dataStore->user_agent);
    ui->user_agent->setPlaceholderText(NekoGui::dataStore->GetUserAgent(true));
    D_LOAD_BOOL(sub_use_proxy)
    D_LOAD_BOOL(sub_clear)
    D_LOAD_BOOL(sub_insecure)
    D_LOAD_INT_ENABLE(sub_auto_update, sub_auto_update_enable)

    // Core
    ui->groupBox_core->setTitle(software_core_name);

    // Assets
    ui->geoip_url->setEditable(true);
    ui->geosite_url->setEditable(true);
    ui->geoip_url->addItems(NekoGui::GeoAssets::GeoIPURLs);
    ui->geosite_url->addItems(NekoGui::GeoAssets::GeoSiteURLs);
    ui->geoip_url->setCurrentText(NekoGui::dataStore->geoip_download_url);
    ui->geosite_url->setCurrentText(NekoGui::dataStore->geosite_download_url);

    connect(ui->download_geo_btn, &QPushButton::clicked, this, [=]() {
        MW_dialog_message(Dialog_DialogBasicSettings, "DownloadAssets;"+ui->geoip_url->currentText()+";"+ui->geosite_url->currentText());
    });
    connect(ui->remove_srs_btn, &QPushButton::clicked, this, [=](){
       auto rsDir = QDir(RULE_SETS_DIR);
       auto entries = rsDir.entryList(QDir::Files);
       for (const auto &item: entries) {
           if (!QFile(RULE_SETS_DIR + "/" + item).remove()) {
               MW_show_log("Failed to remove " + item + ", stop the core then try again");
           }
       }
       MW_show_log("Removed all rule-set files");
    });

    //
    CACHE.extraCore = QString2QJsonObject(NekoGui::dataStore->extraCore->core_map);
    if (!CACHE.extraCore.contains("naive")) CACHE.extraCore.insert("naive", "");
    if (!CACHE.extraCore.contains("hysteria")) CACHE.extraCore.insert("hysteria", "");
    if (!CACHE.extraCore.contains("hysteria2")) CACHE.extraCore.insert("hysteria2", "");
    if (!CACHE.extraCore.contains("tuic")) CACHE.extraCore.insert("tuic", "");
    //
    auto extra_core_layout = ui->extra_core_box_scrollAreaWidgetContents->layout();
    for (const auto &s: CACHE.extraCore.keys()) {
        extra_core_layout->addWidget(new ExtraCoreWidget(&CACHE.extraCore, s));
    }
    //
    connect(ui->extra_core_add, &QPushButton::clicked, this, [=] {
        bool ok;
        auto s = QInputDialog::getText(nullptr, tr("Add"),
                                       tr("Please input the core name."),
                                       QLineEdit::Normal, "", &ok)
                     .trimmed();
        if (s.isEmpty() || !ok) return;
        if (CACHE.extraCore.contains(s)) return;
        extra_core_layout->addWidget(new ExtraCoreWidget(&CACHE.extraCore, s));
        CACHE.extraCore.insert(s, "");
    });
    connect(ui->extra_core_del, &QPushButton::clicked, this, [=] {
        bool ok;
        auto s = QInputDialog::getItem(nullptr, tr("Delete"),
                                       tr("Please select the core name."),
                                       CACHE.extraCore.keys(), 0, false, &ok);
        if (s.isEmpty() || !ok) return;
        for (int i = 0; i < extra_core_layout->count(); i++) {
            auto item = extra_core_layout->itemAt(i);
            auto ecw = dynamic_cast<ExtraCoreWidget *>(item->widget());
            if (ecw != nullptr && ecw->coreName == s) {
                ecw->deleteLater();
                CACHE.extraCore.remove(s);
                return;
            }
        }
    });

    // Mux
    D_LOAD_INT(mux_concurrency)
    D_LOAD_COMBO_STRING(mux_protocol)
    D_LOAD_BOOL(mux_padding)
    D_LOAD_BOOL(mux_default_on)

    // NTP
    ui->ntp_enable->setChecked(NekoGui::dataStore->enable_ntp);
    ui->ntp_server->setEnabled(NekoGui::dataStore->enable_ntp);
    ui->ntp_port->setEnabled(NekoGui::dataStore->enable_ntp);
    ui->ntp_interval->setEnabled(NekoGui::dataStore->enable_ntp);
    ui->ntp_server->setText(NekoGui::dataStore->ntp_server_address);
    ui->ntp_port->setText(Int2String(NekoGui::dataStore->ntp_server_port));
    ui->ntp_interval->setCurrentText(NekoGui::dataStore->ntp_interval);
    connect(ui->ntp_enable, &QCheckBox::stateChanged, this, [=](const bool &state) {
        ui->ntp_server->setEnabled(state);
        ui->ntp_port->setEnabled(state);
        ui->ntp_interval->setEnabled(state);
    });

    // Security

    ui->utlsFingerprint->addItems(Preset::SingBox::UtlsFingerPrint);

    D_LOAD_BOOL(skip_cert)
    ui->utlsFingerprint->setCurrentText(NekoGui::dataStore->utlsFingerprint);
}

DialogBasicSettings::~DialogBasicSettings() {
    delete ui;
}

void DialogBasicSettings::accept() {
    // Common

    D_SAVE_STRING(inbound_address)
    D_SAVE_COMBO_STRING(log_level)
    NekoGui::dataStore->custom_inbound = CACHE.custom_inbound;
    D_SAVE_INT(inbound_socks_port)
    D_SAVE_INT(test_concurrent)
    D_SAVE_STRING(test_latency_url)

    // Style

    NekoGui::dataStore->language = ui->language->currentIndex();
    D_SAVE_BOOL(start_minimal)
    D_SAVE_INT(max_log_line)

    if (NekoGui::dataStore->max_log_line <= 0) {
        NekoGui::dataStore->max_log_line = 200;
    }

    if (ui->rfsh_r->currentIndex() == 0) {
        NekoGui::dataStore->traffic_loop_interval = 500;
    } else if (ui->rfsh_r->currentIndex() == 1) {
        NekoGui::dataStore->traffic_loop_interval = 1000;
    } else if (ui->rfsh_r->currentIndex() == 2) {
        NekoGui::dataStore->traffic_loop_interval = 2000;
    } else if (ui->rfsh_r->currentIndex() == 3) {
        NekoGui::dataStore->traffic_loop_interval = 3000;
    } else if (ui->rfsh_r->currentIndex() == 4) {
        NekoGui::dataStore->traffic_loop_interval = 5000;
    } else {
        NekoGui::dataStore->traffic_loop_interval = 0;
    }

    // Subscription

    if (ui->sub_auto_update_enable->isChecked()) {
        TM_auto_update_subsctiption_Reset_Minute(ui->sub_auto_update->text().toInt());
    } else {
        TM_auto_update_subsctiption_Reset_Minute(0);
    }

    NekoGui::dataStore->user_agent = ui->user_agent->text();
    D_SAVE_BOOL(sub_use_proxy)
    D_SAVE_BOOL(sub_clear)
    D_SAVE_BOOL(sub_insecure)
    D_SAVE_INT_ENABLE(sub_auto_update, sub_auto_update_enable)

    // Core
    NekoGui::dataStore->extraCore->core_map = QJsonObject2QString(CACHE.extraCore, true);
    NekoGui::dataStore->disable_traffic_stats = ui->disable_stats->isChecked();

    // Assets
    NekoGui::dataStore->geoip_download_url = ui->geoip_url->currentText();
    NekoGui::dataStore->geosite_download_url = ui->geosite_url->currentText();

    // Mux
    D_SAVE_INT(mux_concurrency)
    D_SAVE_COMBO_STRING(mux_protocol)
    D_SAVE_BOOL(mux_padding)
    D_SAVE_BOOL(mux_default_on)

    // NTP
    NekoGui::dataStore->enable_ntp = ui->ntp_enable->isChecked();
    NekoGui::dataStore->ntp_server_address = ui->ntp_server->text();
    NekoGui::dataStore->ntp_server_port = ui->ntp_port->text().toInt();
    NekoGui::dataStore->ntp_interval = ui->ntp_interval->currentText();

    // Security

    D_SAVE_BOOL(skip_cert)
    NekoGui::dataStore->utlsFingerprint = ui->utlsFingerprint->currentText();

    QStringList str{"UpdateDataStore"};
    if (CACHE.needRestart) str << "NeedRestart";
    MW_dialog_message(Dialog_DialogBasicSettings, str.join(","));
    QDialog::accept();
}

void DialogBasicSettings::on_set_custom_icon_clicked() {
    auto title = ui->set_custom_icon->text();
    QString user_icon_path = "./" + software_name.toLower() + ".png";
    auto c = QMessageBox::question(this, title, tr("Please select a PNG file."),
                                   tr("Select"), tr("Reset"), tr("Cancel"), 2, 2);
    if (c == 0) {
        auto fn = QFileDialog::getOpenFileName(this, QObject::tr("Select"), QDir::currentPath(),
                                               "*.png", nullptr, QFileDialog::Option::ReadOnly);
        QImage img(fn);
        if (img.isNull() || img.height() != img.width()) {
            MessageBoxWarning(title, tr("Please select a valid square image."));
            return;
        }
        QFile::remove(user_icon_path);
        QFile::copy(fn, user_icon_path);
    } else if (c == 1) {
        QFile::remove(user_icon_path);
    } else {
        return;
    }
    MW_dialog_message(Dialog_DialogBasicSettings, "UpdateIcon");
}

void DialogBasicSettings::on_core_settings_clicked() {
    auto w = new QDialog(this);
    w->setWindowTitle(software_core_name + " Core Options");
    auto layout = new QGridLayout;
    w->setLayout(layout);
    //
    auto line = -1;
    QCheckBox *core_box_enable_clash_api;
    MyLineEdit *core_box_clash_api;
    MyLineEdit *core_box_clash_api_secret;
    MyLineEdit *core_box_underlying_dns;
    MyLineEdit *core_box_clash_listen_addr;
    //
    auto core_box_underlying_dns_l = new QLabel(tr("Override underlying DNS"));
    core_box_underlying_dns_l->setToolTip(tr(
        "It is recommended to leave it blank, but it sometimes does not work, at this time you can set this option.\n"
        "For NekoRay, this rewrites the underlying(localhost) DNS in Tun Mode.\n"
        "For NekoBox, this rewrites the underlying(localhost) DNS in Tun Mode, normal mode, and also URL Test."));
    core_box_underlying_dns = new MyLineEdit;
    core_box_underlying_dns->setText(NekoGui::dataStore->core_box_underlying_dns);
    core_box_underlying_dns->setMinimumWidth(300);
    layout->addWidget(core_box_underlying_dns_l, ++line, 0);
    layout->addWidget(core_box_underlying_dns, line, 1);
    //
    auto core_box_enable_clash_api_l = new QLabel("Enable Clash API");
    core_box_enable_clash_api = new QCheckBox;
    core_box_enable_clash_api->setChecked(NekoGui::dataStore->core_box_clash_api > 0);
    layout->addWidget(core_box_enable_clash_api_l, ++line, 0);
    layout->addWidget(core_box_enable_clash_api, line, 1);
    //
    auto core_box_clash_listen_addr_l = new QLabel("Clash Api Listen Address");
    core_box_clash_listen_addr = new MyLineEdit;
    core_box_clash_listen_addr->setText(NekoGui::dataStore->core_box_clash_listen_addr);
    layout->addWidget(core_box_clash_listen_addr_l, ++line, 0);
    layout->addWidget(core_box_clash_listen_addr, line, 1);
    //
    auto core_box_clash_api_l = new QLabel("Clash API Listen Port");
    core_box_clash_api = new MyLineEdit;
    core_box_clash_api->setText(Int2String(std::abs(NekoGui::dataStore->core_box_clash_api)));
    layout->addWidget(core_box_clash_api_l, ++line, 0);
    layout->addWidget(core_box_clash_api, line, 1);
    //
    auto core_box_clash_api_secret_l = new QLabel("Clash API Secret");
    core_box_clash_api_secret = new MyLineEdit;
    core_box_clash_api_secret->setText(NekoGui::dataStore->core_box_clash_api_secret);
    layout->addWidget(core_box_clash_api_secret_l, ++line, 0);
    layout->addWidget(core_box_clash_api_secret, line, 1);
    //
    auto box = new QDialogButtonBox;
    box->setOrientation(Qt::Horizontal);
    box->setStandardButtons(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    connect(box, &QDialogButtonBox::accepted, w, [=] {
        NekoGui::dataStore->core_box_underlying_dns = core_box_underlying_dns->text();
        NekoGui::dataStore->core_box_clash_api = core_box_clash_api->text().toInt() * (core_box_enable_clash_api->isChecked() ? 1 : -1);
        NekoGui::dataStore->core_box_clash_listen_addr = core_box_clash_listen_addr->text();
        NekoGui::dataStore->core_box_clash_api_secret = core_box_clash_api_secret->text();
        MW_dialog_message(Dialog_DialogBasicSettings, "UpdateDataStore");
        w->accept();
    });
    connect(box, &QDialogButtonBox::rejected, w, &QDialog::reject);
    layout->addWidget(box, ++line, 1);
    //
    ADD_ASTERISK(w)
    w->exec();
    w->deleteLater();
}
