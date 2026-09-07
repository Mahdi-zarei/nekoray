#include "include/ui/profile/dialog_edit_profile.h"

#include "include/ui/profile/edit_http.h"
#include "include/ui/profile/edit_shadowsocks.h"
#include "include/ui/profile/edit_chain.h"
#include "include/ui/profile/edit_autoselector.h"
#include "include/ui/profile/edit_vmess.h"
#include "include/ui/profile/edit_vless.h"
#include "include/ui/profile/edit_anytls.h"
#include "include/ui/profile/edit_mieru.h"
#include "include/ui/profile/edit_snell.h"
#include "include/ui/profile/edit_wireguard.h"
#include "include/ui/profile/edit_openvpn.h"
#include "include/ui/profile/edit_openconnect.h"
#include "include/ui/profile/edit_tailscale.h"
#include "include/ui/profile/edit_ssh.h"
#include "include/ui/profile/edit_custom.h"
#include "include/ui/profile/edit_extra_core.h"

#include "include/ui/widget/json/JsonEditorDialog.h"
#include "include/global/GuiUtils.hpp"
#include "include/global/Utils.hpp"

#include <QInputDialog>
#include <QLabel>
#include <QScreen>
#include <QSet>

#include "include/configs/common/TLS.h"
#include "include/configs/common/utils.h"
#include "include/configs/common/xrayStreamSetting.h"
#include "include/database/ProfilesRepo.h"



#include "include/ui/profile/edit_advanced.h"
#include "include/ui/profile/edit_direct.h"
#include "include/ui/profile/edit_hysteria.h"
#include "include/ui/profile/edit_socks.h"
#include "include/ui/profile/edit_trojan.h"
#include "include/ui/profile/edit_tuic.h"
#include "include/ui/profile/edit_juicity.h"
#include "include/ui/profile/edit_trusttunnel.h"
#include "include/ui/profile/edit_naive.h"
#include "include/ui/profile/edit_shadowtls.h"
#include "include/ui/profile/edit_xrayvless.h"

#define LOAD_TYPE(a) ui->type->addItem(Configs::dataManager->profilesRepo->NewProfile(a)->outbound->DisplayType(), a);

namespace {
constexpr int kXrayXHTTPNetworkMinWidth = 760;

QWidget *effectiveFocusWidget(QWidget *widget) {
    QSet<QWidget *> visited;
    while (widget && widget->focusProxy() && !visited.contains(widget)) {
        visited.insert(widget);
        widget = widget->focusProxy();
    }
    return widget;
}

QList<QWidget *> collectTabOrder(QWidget *window, const QWidget *subtree = nullptr,
                                 const QWidget *marker = nullptr) {
    if (!window) return {};

    QList<QWidget *> tabOrder;
    QSet<QWidget *> visited;
    auto *current = window;
    while (true) {
        auto *next = current->nextInFocusChain();
        if (!next || next == window || visited.contains(next)) break;
        visited.insert(next);
        current = next;

        if (subtree && current != subtree && !subtree->isAncestorOf(current)) continue;

        const bool isMarker = current == marker;
        if (!isMarker && !(current->focusPolicy() & Qt::TabFocus)) continue;

        auto *focusWidget = isMarker ? current : effectiveFocusWidget(current);
        if (!focusWidget || tabOrder.contains(focusWidget)) continue;
        if (subtree && focusWidget != subtree && !subtree->isAncestorOf(focusWidget)) continue;
        tabOrder.append(focusWidget);
    }

    return tabOrder;
}

void rebuildTabOrder(const QList<QWidget *> &tabOrder) {
    if (tabOrder.size() < 2) return;

    for (qsizetype i = 1; i < tabOrder.size(); ++i) {
        QWidget::setTabOrder(tabOrder.at(i - 1), tabOrder.at(i));
    }
}
}

void DialogEditProfile::queueRefreshDialogLayout() {
    runOnThread([=,this] { fitToScreen(); }, this);
}

void DialogEditProfile::fitToScreen() {
    // adjustSize() clamps to 2/3 of the screen; size to the content and let the editor scroll only once the work area is smaller.
    layout()->activate();
    QSize want = sizeHint();
    if (const QScreen *scr = parentWidget() ? parentWidget()->screen() : screen()) {
        const QRect avail = scr->availableGeometry();
        want = want.boundedTo(QSize(avail.width() - 24, avail.height() - 72));
    }
    resize(want);
    adjustPosition(mainwindow);
}

void DialogEditProfile::toggleSingboxWidgets(bool show) {
    ui->stream_box->setVisible(show);
    ui->right_all_w->setVisible(show);
}

void DialogEditProfile::toggleXrayWidgets(bool show) {
    ui->xray_settings_box->setVisible(show);
    const auto hasVisibleXrayDetails = !ui->xray_security_box->isHidden() || !ui->xray_network_box->isHidden();
    ui->xray_widget->setVisible(show && hasVisibleXrayDetails);
}

DialogEditProfile::DialogEditProfile(const QString &_type, int profileOrGroupId, QWidget *parent)
    : QDialog(parent), ui(new Ui::DialogEditProfile) {
    ui->setupUi(this);

    outerTabOrder = collectTabOrder(this, nullptr, ui->fake);
    innerTabOrderIndex = outerTabOrder.indexOf(ui->fake);
    if (innerTabOrderIndex >= 0) outerTabOrder.removeAt(innerTabOrderIndex);

    auto setXrayXHTTPNetworkVisible = [=,this](bool visible) {
        ui->xray_network_scroll->setMinimumWidth(visible ? kXrayXHTTPNetworkMinWidth : 0);
        ui->xray_xhttp_box->setVisible(visible);
    };
    ui->dialog_layout->setStretch(0, 0);
    ui->dialog_layout->setStretch(1, 1);
    ui->dialog_layout->setStretch(2, 1);
    ui->dialog_layout->setAlignment(ui->left, Qt::AlignTop);
    ui->left->setAlignment(Qt::AlignTop);
    ui->right_layout->setAlignment(Qt::AlignTop);
    ui->verticalLayout_5->setAlignment(Qt::AlignTop);
    ui->verticalLayout_8->setAlignment(Qt::AlignTop);

    ui->xray_security->addItems({"", "tls", "reality"});
    ui->xray_network->addItems(Configs::XrayNetworks);
    ui->xray_fp->addItems(Configs::tlsFingerprints);
    setupXrayXHTTPControls();
    ui->xray_ed_length->setValidator(new QIntValidator(0, 8192));
    toggleXrayWidgets(false);

    network_title_base = ui->network_box->title();
    connect(ui->network, &QComboBox::currentTextChanged, this, [=,this](const QString &txt) {
        ui->network_box->setTitle(network_title_base.arg(txt));
        if (txt == "grpc") {
            ui->headers->setVisible(false);
            ui->headers_l->setVisible(false);
            ui->method->setVisible(false);
            ui->method_l->setVisible(false);
            ui->path->setVisible(false);
            ui->path_l->setVisible(false);
            ui->host->setVisible(false);
            ui->host_l->setVisible(false);
        } else if (txt == "ws" || txt == "httpupgrade") {
            ui->headers->setVisible(true);
            ui->headers_l->setVisible(true);
            ui->method->setVisible(false);
            ui->method_l->setVisible(false);
            ui->path->setVisible(true);
            ui->path_l->setVisible(true);
            ui->host->setVisible(true);
            ui->host_l->setVisible(true);
        } else if (txt == "http") {
            ui->headers->setVisible(true);
            ui->headers_l->setVisible(true);
            ui->method->setVisible(true);
            ui->method_l->setVisible(true);
            ui->path->setVisible(true);
            ui->path_l->setVisible(true);
            ui->host->setVisible(true);
            ui->host_l->setVisible(true);
        } else {
            ui->headers->setVisible(false);
            ui->headers_l->setVisible(false);
            ui->method->setVisible(false);
            ui->method_l->setVisible(false);
            ui->path->setVisible(false);
            ui->path_l->setVisible(false);
            ui->host->setVisible(false);
            ui->host_l->setVisible(false);
        }
        if (txt == "grpc") {
            ui->service_name->setVisible(true);
            ui->service_name_l->setVisible(true);
        } else {
            ui->service_name->setVisible(false);
            ui->service_name_l->setVisible(false);
        }
        if (txt == "ws") {
            ui->ws_early_data_length->setVisible(true);
            ui->ws_early_data_length_l->setVisible(true);
            ui->ws_early_data_name->setVisible(true);
            ui->ws_early_data_name_l->setVisible(true);
        } else {
            ui->ws_early_data_length->setVisible(false);
            ui->ws_early_data_length_l->setVisible(false);
            ui->ws_early_data_name->setVisible(false);
            ui->ws_early_data_name_l->setVisible(false);
        }
        if (!ui->utlsFingerprint->count()) ui->utlsFingerprint->addItems(Configs::tlsFingerprints);
        int networkBoxVisible = 0;
        for (auto label: ui->network_box->findChildren<QLabel *>()) {
            if (!label->isHidden()) networkBoxVisible++;
        }
        ui->network_box->setVisible(networkBoxVisible);
        queueRefreshDialogLayout();
    });
    ui->network->removeItem(0);

    connect(ui->security, &QComboBox::currentTextChanged, this, [=,this](const QString &txt) {
        if (txt == "tls") {
            ui->security_box->setVisible(true);
            ui->tls_camouflage_box->setVisible(true);
        } else {
            ui->security_box->setVisible(false);
            ui->tls_camouflage_box->setVisible(false);
        }
        queueRefreshDialogLayout();
    });
    emit ui->security->currentTextChanged(ui->security->currentText());

    // Fragment index 2 is Off; the fallback delay only exists in the built-in implementation.
    connect(ui->fragment, &QComboBox::currentIndexChanged, this, [=,this](int index)
    {
        ui->tls_frag_fall_delay->setEnabled(index != 2);
    });

    connect(ui->multiplex, &QComboBox::currentTextChanged, this, [=,this](const QString &txt) {
        if (txt == "Off") {
            ui->brutal_enable->setCheckState(Qt::CheckState::Unchecked);
            ui->brutal_box->setEnabled(false);
        } else {
            ui->brutal_box->setEnabled(true);
        }
    });

    connect(ui->advanced_button, &QPushButton::clicked, this, [=,this]() {
        auto advancedWidget = new EditAdvanced(this, ent);
        advancedWidget->show();
    });

    connect(ui->xray_mode, &QComboBox::currentTextChanged, this, [=,this](const QString &) {
        updateXrayXHTTPControls();
        queueRefreshDialogLayout();
    });
    connect(ui->xray_xpadding_obfs_mode, &QCheckBox::toggled, this, [=,this](bool) {
        updateXrayXHTTPControls();
        queueRefreshDialogLayout();
    });
    ui->xray_network_box->hide();
    connect(ui->xray_network, &QComboBox::currentTextChanged, this, [=,this](const QString &txt) {
        if (txt == "raw") {
            ui->xray_network_box->setVisible(false);
            if (ui->xray_security_box->isHidden()) ui->xray_widget->hide();
        }
        else {
            ui->xray_widget->show();
            ui->xray_network_box->setVisible(true);
            if (txt == "xhttp") {
                setXrayXHTTPNetworkVisible(true);
                ui->xray_ed_label->setVisible(false);
                ui->xray_ed_length->setVisible(false);
                ui->xray_headers_l->setVisible(true);
                ui->xray_headers->setVisible(true);
                ui->xray_multi_mode->setVisible(false);
                updateXrayXHTTPControls();
            } else {
                setXrayXHTTPNetworkVisible(false);
                if (txt == "grpc") {
                    ui->xray_ed_label->setVisible(false);
                    ui->xray_ed_length->setVisible(false);
                    ui->xray_headers_l->setVisible(false);
                    ui->xray_headers->setVisible(false);
                    ui->xray_multi_mode->setVisible(true);
                } else {
                    ui->xray_ed_label->setVisible(true);
                    ui->xray_ed_length->setVisible(true);
                    ui->xray_headers_l->setVisible(true);
                    ui->xray_headers->setVisible(true);
                    ui->xray_multi_mode->setVisible(false);
                }
            }
        }
        updateXrayCommons(txt);
        queueRefreshDialogLayout();
    });

    ui->xray_security_box->hide();
    connect(ui->xray_security, &QComboBox::currentTextChanged, this, [=,this](const QString &txt) {
        if (txt.isEmpty()) {
            ui->xray_security_box->setVisible(false);
            if (ui->xray_network_box->isHidden()) ui->xray_widget->hide();
        }
        else if (txt == "tls") {
            ui->xray_widget->show();
            ui->xray_security_box->setVisible(true);
            ui->xray_tls_only->setVisible(true);
            ui->xray_reality_box->setVisible(false);
        } else {
            ui->xray_widget->show();
            ui->xray_security_box->setVisible(true);
            ui->xray_tls_only->setVisible(false);
            ui->xray_reality_box->setVisible(true);
        }
        queueRefreshDialogLayout();
    });

    newEnt = _type != "";
    if (newEnt) {
        this->groupId = profileOrGroupId;
        this->type = _type;

        LOAD_TYPE("autoselector")
        LOAD_TYPE("socks")
        LOAD_TYPE("http")
        LOAD_TYPE("shadowsocks")
        LOAD_TYPE("trojan")
        LOAD_TYPE("vmess")
        LOAD_TYPE("vless")
        ui->type->addItem("VLESS (Xray)", "xrayvless");
        LOAD_TYPE("hysteria")
        LOAD_TYPE("tuic")
        LOAD_TYPE("juicity")
        LOAD_TYPE("naive")
        LOAD_TYPE("trusttunnel")
        LOAD_TYPE("anytls")
        LOAD_TYPE("mieru")
        LOAD_TYPE("snell")
        LOAD_TYPE("shadowtls")
        LOAD_TYPE("wireguard")
        LOAD_TYPE("openvpn")
        LOAD_TYPE("openconnect")
        LOAD_TYPE("tailscale")
        LOAD_TYPE("ssh")
        LOAD_TYPE("direct")
        ui->type->addItem(tr("Custom (%1 outbound)").arg(software_core_name), Configs::Custom::CustomOutbound);
        ui->type->addItem(tr("Custom (%1 config)").arg(software_core_name), Configs::Custom::CustomFullConfig);
        ui->type->addItem(tr("Custom (Xray outbound)"), Configs::Custom::CustomXrayOutbound);
        ui->type->addItem(tr("Custom (Xray config)"), Configs::Custom::CustomXrayFullConfig);
        ui->type->addItem(tr("Extra Core"), "extracore");
        LOAD_TYPE("chain")

        connect(ui->type, &QComboBox::currentIndexChanged, this, [=,this](int index) {
            typeSelected(ui->type->itemData(index).toString());
        });
    } else {
        this->ent = Configs::dataManager->profilesRepo->GetProfile(profileOrGroupId);
        if (this->ent == nullptr) return;
        this->type = ent->type;
        ui->type->setVisible(false);
        ui->type_l->setVisible(false);
    }

    typeSelected(this->type);
}

DialogEditProfile::~DialogEditProfile() {
    delete ui;
}

void DialogEditProfile::typeSelected(const QString &newType) {
    QString customType;
    type = newType;
    bool validType = true;

    if (type == "http") {
        auto _innerWidget = new EditHttp(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "socks") {
        auto _innerWidget = new EditSocks(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "shadowsocks") {
        auto _innerWidget = new EditShadowSocks(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "chain") {
        auto _innerWidget = new EditChain(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "autoselector") {
        auto _innerWidget = new EditAutoSelector(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "vmess") {
        auto _innerWidget = new EditVMess(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if ( type == "vless") {
        auto _innerWidget = new EditVless(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
        connect(_innerWidget->_flow, &QComboBox::currentTextChanged, _innerWidget, [=,this](const QString &txt)
        {
            if (txt == "xtls-rprx-vision")
            {
                ui->multiplex->setDisabled(true);
            } else
            {
                ui->multiplex->setDisabled(false);
            }
        });
    } else if (type == "xrayvless") {
        auto _innerWidget = new EditXrayVless(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "trojan") {
        auto _innerWidget = new EditTrojan(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "hysteria") {
        auto _innerWidget = new EditHysteria(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;

        auto updateLayout = [_innerWidget, this]() {
            _innerWidget->editHysteriaLayout(
                _innerWidget->_protocol_version->currentText(),
                _innerWidget->_obfuscation_type->currentText()
            );
            // A realm profile is reached through the rendezvous service, not an address of its own.
            const auto realm = _innerWidget->_realm_enabled->isChecked()
                               && _innerWidget->_protocol_version->currentText() == "2";
            ui->address->setEnabled(!realm);
            ui->address_l->setEnabled(!realm);
            ui->port->setEnabled(!realm);
            ui->port_l->setEnabled(!realm);
            queueRefreshDialogLayout();
        };

        connect(_innerWidget->_protocol_version, &QComboBox::currentTextChanged, _innerWidget, [=](const QString &)
        {
            updateLayout();
        });
        connect(_innerWidget->_obfuscation_type, &QComboBox::currentTextChanged, _innerWidget, [=](const QString &)
        {
            updateLayout();
        });
        connect(_innerWidget->_realm_enabled, &QCheckBox::toggled, _innerWidget, [=](bool)
        {
            updateLayout();
        });
    } else if (type == "tuic") {
        auto _innerWidget = new EditTuic(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "juicity") {
        auto _innerWidget = new EditJuicity(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "trusttunnel") {
        auto _innerWidget = new EditTrustTunnel(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "anytls") {
        auto _innerWidget = new EditAnyTLS(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "mieru") {
        auto _innerWidget = new EditMieru(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "snell") {
        auto _innerWidget = new EditSnell(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "shadowtls") {
        auto _innerWidget = new EditShadowTLS(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "wireguard") {
        auto _innerWidget = new EditWireguard(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "openvpn") {
        auto _innerWidget = new EditOpenVPN(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "openconnect") {
        auto _innerWidget = new EditOpenConnect(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "tailscale") {
        auto _innerWidget = new EditTailScale(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "ssh") {
        auto _innerWidget = new EditSSH(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == Configs::Custom::CustomOutbound || type == Configs::Custom::CustomFullConfig ||
               type == Configs::Custom::CustomXrayOutbound || type == Configs::Custom::CustomXrayFullConfig ||
               type == "custom") {
        auto _innerWidget = new EditCustom(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
        customType = newEnt ? type : ent->Custom()->type;
        _innerWidget->preset_core = customType;
        type = "custom";
    } else if (type == "extracore")
    {
        auto _innerWidget = new EditExtraCore(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "naive") {
        auto _innerWidget = new EditNaive(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else if (type == "direct") {
        auto _innerWidget = new EditDirect(this);
        innerWidget = _innerWidget;
        innerEditor = _innerWidget;
    } else {
        validType = false;
    }

    if (!validType) {
        MessageBoxWarning(newType, "Wrong type");
        return;
    }

    if (newEnt) {
        this->ent = Configs::dataManager->profilesRepo->NewProfile(type);
        this->ent->gid = groupId;
    }

    auto showAddressPort = type != "chain"
                           && type != "autoselector"
                           && type != "direct"
                           && customType != Configs::Custom::CustomOutbound
                           && customType != Configs::Custom::CustomFullConfig
                           && customType != Configs::Custom::CustomXrayOutbound
                           && customType != Configs::Custom::CustomXrayFullConfig
                           && type != "extracore" && type != "tailscale";
    ui->address->setVisible(showAddressPort);
    ui->address_l->setVisible(showAddressPort);
    ui->port->setVisible(showAddressPort);
    ui->port_l->setVisible(showAddressPort);

    // A realm carries the endpoint itself; set unconditionally, since switching away must re-enable these.
    auto hysteria = ent->Hysteria();
    const bool realmAddress = hysteria != nullptr && hysteria->RealmActive();
    ui->address->setEnabled(!realmAddress);
    ui->address_l->setEnabled(!realmAddress);
    ui->port->setEnabled(!realmAddress);
    ui->port_l->setEnabled(!realmAddress);

    auto showAdvancedDialOption = type != "chain"
    && type != "autoselector"
    && type != "extracore" && type != "tailscale"
    && customType != Configs::Custom::CustomOutbound
    && customType != Configs::Custom::CustomFullConfig
    && customType != Configs::Custom::CustomXrayOutbound
    && customType != Configs::Custom::CustomXrayFullConfig;
    ui->advanced_button->setVisible(showAdvancedDialOption);

    if (ent->outbound->HasTLS() || ent->outbound->HasTransport()) {
        ui->right_all_w->setVisible(true);
        auto tls = ent->outbound->GetTLS();
        auto transport = ent->outbound->GetTransport();
        if (ent->outbound->MustTLS()) {
            ui->security->setCurrentText("tls");
            ui->security->setEnabled(false);
        } else {
            ui->security->setCurrentText(tls->enabled ? "tls" : "");
            ui->security->setEnabled(true);
        }
        ui->network->setCurrentText(transport->type);
        ui->path->setText(transport->path);
        ui->host->setText(transport->host);
        ui->method->setText(transport->method);
        ui->sni->setText(tls->server_name);
        ui->alpn->setText(tls->alpn.join(","));
        if (newEnt) {
            ui->utlsFingerprint->setCurrentText(Configs::dataManager->settingsRepo->utlsFingerprint);
        } else {
            ui->utlsFingerprint->setCurrentText(tls->utls->fingerPrint);
        }
        ui->fragment->setCurrentIndex(tls->getFragmentState());
        ui->tls_frag_fall_delay->setEnabled(tls->getFragmentState() != 2);
        ui->tls_frag_fall_delay->setText(tls->fragment_fallback_delay);
        ui->tls_rec_frag->setChecked(tls->record_fragment);
        ui->tls_tricks->setCurrentIndex(tls->getTlsTricksState());
        ui->insecure->setChecked(tls->insecure);
        ui->headers->setText(Configs::getHeadersString(transport->headers));
        ui->service_name->setText(transport->service_name);
        ui->ws_early_data_name->setText(transport->early_data_header_name);
        ui->ws_early_data_length->setText(Int2String(transport->max_early_data));
        ui->reality_pbk->setText(tls->reality->public_key);
        ui->reality_sid->setText(tls->reality->short_id);
        CACHE.certificate = tls->certificate;
    } else {
        ui->right_all_w->setVisible(false);
    }

    if (ent->outbound->IsXray()) {
        auto xrayStream = ent->outbound->GetXrayStream();
        auto xrayMux = ent->outbound->GetXrayMultiplex();

        updateXrayCommons(xrayStream->network);

        ui->xray_xpaddingbytes->setText(xrayStream->xhttp->xPaddingBytes);
        ui->xray_xpadding_obfs_mode->setChecked(xrayStream->xhttp->xPaddingObfsMode);
        ui->xray_xpadding_key->setText(xrayStream->xhttp->xPaddingKey);
        ui->xray_xpadding_header->setText(xrayStream->xhttp->xPaddingHeader);
        ui->xray_xpadding_placement->setCurrentText(xrayStream->xhttp->xPaddingPlacement);
        ui->xray_xpadding_method->setCurrentText(xrayStream->xhttp->xPaddingMethod);
        ui->xray_uplink_http_method->setCurrentText(xrayStream->xhttp->uplinkHTTPMethod);
        ui->xray_session_placement->setCurrentText(xrayStream->xhttp->sessionIDPlacement);
        ui->xray_session_key->setText(xrayStream->xhttp->sessionIDKey);
        ui->xray_session_id_table->setText(xrayStream->xhttp->sessionIDTable);
        ui->xray_session_id_length->setText(xrayStream->xhttp->sessionIDLength);
        ui->xray_seq_placement->setCurrentText(xrayStream->xhttp->seqPlacement);
        ui->xray_seq_key->setText(xrayStream->xhttp->seqKey);
        ui->xray_uplink_data_placement->setCurrentText(xrayStream->xhttp->uplinkDataPlacement);
        ui->xray_uplink_data_key->setText(xrayStream->xhttp->uplinkDataKey);
        ui->xray_uplink_chunk_size->setText(xrayStream->xhttp->uplinkChunkSize);
        ui->xray_no_grpc->setChecked(xrayStream->xhttp->noGRPCHeader);
        ui->xray_no_sse->setChecked(xrayStream->xhttp->noSSEHeader);
        ui->xray_scMaxEachPostBytes->setText(xrayStream->xhttp->scMaxEachPostBytes);
        ui->xray_scMinPostsIntervalMs->setText(xrayStream->xhttp->scMinPostsIntervalMs);
        ui->xray_scMaxBufferedPosts->setText(Int2String(xrayStream->xhttp->scMaxBufferedPosts));
        ui->xray_scStreamUpServerSecs->setText(xrayStream->xhttp->scStreamUpServerSecs);
        ui->xray_serverMaxHeaderBytes->setText(Int2String(xrayStream->xhttp->serverMaxHeaderBytes));
        ui->xray_max_concurrency->setText(xrayStream->xhttp->maxConcurrency);
        ui->xray_max_connections->setText(xrayStream->xhttp->maxConnections);
        ui->xray_hMaxRequestTimes->setText(xrayStream->xhttp->hMaxRequestTimes);
        ui->xray_hMaxReusableSecs->setText(xrayStream->xhttp->hMaxReusableSecs);
        ui->xray_max_reuse_times->setText(xrayStream->xhttp->cMaxReuseTimes);
        ui->xray_keep_alive_period->setText(Int2String(xrayStream->xhttp->hKeepAlivePeriod));
        CACHE.XrayDownloadSettings = xrayStream->xhttp->downloadSettings;
        CACHE.XrayFinalmask = xrayStream->finalmask;
        ui->xray_downloadsettings_edit->setText(xrayStream->xhttp->downloadSettings.isEmpty() ? "Not Set" : "Already Set");

        ui->xray_network->setCurrentText(xrayStream->network);
        ui->xray_security->setCurrentText(xrayStream->security);
        ui->xray_mux->setCurrentIndex(xrayMux->getMuxState());

        ui->xray_sni->setText(xrayStream->security == "tls" ? xrayStream->TLS->serverName : xrayStream->reality->serverName);
        ui->xray_fp->setCurrentText(xrayStream->security == "tls" ? xrayStream->TLS->fingerprint : xrayStream->reality->fingerprint);
        ui->xray_alpn->setText(xrayStream->TLS->alpn.join(","));
        ui->xray_pinned_peer_cert_sha256->setText(xrayStream->TLS->pinnedPeerCertSha256);
        ui->xray_verify_peer_cert_by_name->setText(xrayStream->TLS->verifyPeerCertByName);
        ui->xray_reality_pbk->setText(xrayStream->reality->password);
        ui->xray_reality_sid->setText(xrayStream->reality->shortId);
        ui->xray_reality_spiderx->setText(xrayStream->reality->spiderX);

        toggleXrayWidgets(true);
        toggleSingboxWidgets(false);
    } else {
        toggleXrayWidgets(false);
        toggleSingboxWidgets(true);
    }

    if (ent->outbound->HasMux()) {
        auto mux = ent->outbound->GetMux();
        ui->multiplex->setCurrentIndex(mux->getMuxState());
        ui->brutal_enable->setChecked(mux->brutal->enabled);
        ui->brutal_d_speed->setText(Int2String(mux->brutal->down_mbps));
        ui->brutal_u_speed->setText(Int2String(mux->brutal->up_mbps));
    }

    innerWidget->layout()->setContentsMargins(0, 0, 0, 0);
    ui->bean_scroll->setWidget(innerWidget); // deletes the previous editor
    // setWidget() does not announce the new size hint, and cached hints refresh one nesting level per event loop
    // pass, so invalidate up to the dialog before fitToScreen() measures it.
    for (QWidget *w = ui->bean_scroll; w != this; w = w->parentWidget()) w->updateGeometry();
    ui->bean->setTitle(ent->outbound->DisplayType());

    const auto innerTabOrder = collectTabOrder(this, innerWidget);
    if (!innerTabOrder.isEmpty() && innerTabOrderIndex >= 0 && innerTabOrderIndex <= outerTabOrder.size()) {
        auto completeTabOrder = outerTabOrder.mid(0, innerTabOrderIndex);
        completeTabOrder.append(innerTabOrder);
        completeTabOrder.append(outerTabOrder.mid(innerTabOrderIndex));
        rebuildTabOrder(completeTabOrder);
    }

    innerEditor->get_edit_dialog = [&]() { return static_cast<QWidget*>(this); };
    innerEditor->get_edit_text_name = [&]() { return ui->name->text(); };
    innerEditor->get_edit_text_serverAddress = [&]() { return ui->address->text(); };
    innerEditor->get_edit_text_serverPort = [&]() { return ui->port->text(); };
    innerEditor->set_edit_text_serverAddress = [&](const QString &v) { ui->address->setText(v); };
    innerEditor->set_edit_text_serverPort = [&](const QString &v) { ui->port->setText(v); };
    innerEditor->editor_cache_updated = [=,this] { editor_cache_updated_impl(); };
    innerEditor->onStart(ent);

    ui->name->setText(ent->outbound->name);
    ui->address->setText(ent->outbound->GetAddress());
    ui->port->setText(ent->outbound->GetPort());
    ui->port->setValidator(QRegExpValidator_Number);

    ADD_ASTERISK(this)
    if (ent->outbound->HasTransport()) {
        ui->network_l->setVisible(true);
        ui->network->setVisible(true);
        if (ui->network->currentText() == "tcp") {
            ui->network_box->setVisible(false);
        } else {
            ui->network_box->setVisible(true);
        }
    } else {
        ui->network_l->setVisible(false);
        ui->network->setVisible(false);
        ui->network_box->setVisible(false);
    }
    if (ent->outbound->HasTLS()) {
        ui->security->setVisible(true);
        ui->security_l->setVisible(true);
    } else {
        ui->security->setVisible(false);
        ui->security_l->setVisible(false);
        ui->security_box->setVisible(false);
        ui->tls_camouflage_box->setVisible(false);
    }
    if (ent->outbound->HasMux()) {
        ui->multiplex->setVisible(true);
        ui->multiplex_l->setVisible(true);
        ui->brutal_box->setVisible(true);
    } else {
        ui->multiplex->setVisible(false);
        ui->multiplex_l->setVisible(false);
        ui->brutal_box->setVisible(false);
    }
    int streamBoxVisible = 0;
    for (auto label: ui->stream_box->findChildren<QLabel *>()) {
        if (!label->isHidden() && label->parent() == ui->stream_box) streamBoxVisible++;
    }
    ui->stream_box->setVisible(streamBoxVisible);

    auto rightNoBox = (ui->security_box->isHidden() && ui->network_box->isHidden() && ui->tls_camouflage_box->isHidden());
    if (rightNoBox && !ent->outbound->HasTLS() && !ent->outbound->HasTransport() && !ui->right_all_w->isHidden()) {
        ui->right_all_w->setVisible(false);
    }

    editor_cache_updated_impl();
    runOnThread([=,this] {
        fitToScreen();
        if (isHidden()) show();
    }, this);
}

void DialogEditProfile::updateXrayCommons(QString network) {
    if (!ent->outbound->IsXray()) return;
    auto stream = ent->outbound->GetXrayStream();

    if (network == "xhttp") {
        ui->xray_host->setText(stream->xhttp->host);
        ui->xray_path->setText(stream->xhttp->path);
        ui->xray_mode->setCurrentText(stream->xhttp->mode);
        ui->xray_headers->setText(Configs::getHeadersString(stream->xhttp->headers));
        updateXrayXHTTPControls();
    } else if (network == "grpc") {
        ui->xray_host->setText(stream->grpc->authority);
        ui->xray_path->setText(stream->grpc->serviceName);
        ui->xray_multi_mode->setChecked(stream->grpc->multiMode);
    } else if (network == "ws") {
        ui->xray_host->setText(stream->ws->host);
        ui->xray_path->setText(stream->ws->path);
        ui->xray_ed_length->setText(QString::number(stream->ws->ed));
        ui->xray_headers->setText(Configs::getHeadersString(stream->ws->headers));
    } else if(network == "httpupgrade") {
        ui->xray_host->setText(stream->httpupgrade->host);
        ui->xray_path->setText(stream->httpupgrade->path);
        ui->xray_ed_length->setText(QString::number(stream->httpupgrade->ed));
        ui->xray_headers->setText(Configs::getHeadersString(stream->httpupgrade->headers));
    }
}

bool DialogEditProfile::validateHeaders() {
    return !ui->headers->text().contains("|");
}

bool DialogEditProfile::onEnd() {
    if (!innerEditor->onEnd()) {
        return false;
    }

    if (!validateHeaders()) return false;
    if (!validateXrayXHTTPSettings()) return false;

    ent->outbound->name = ui->name->text().trimmed();
    ent->outbound->SetAddress(ui->address->text().remove(' '));
    ent->outbound->SetPort(ui->port->text().trimmed().toInt());

    if (ent->outbound->HasTLS() || ent->outbound->HasTransport()) {
        auto tls = ent->outbound->GetTLS();
        auto transport = ent->outbound->GetTransport();
        transport->type = ui->network->currentText().trimmed();
        tls->enabled = ui->security->currentText() == "tls";
        transport->path = ui->path->text().trimmed();
        transport->host = ui->host->text().trimmed();
        tls->server_name = ui->sni->text().trimmed();
        tls->alpn = SplitAndTrim(ui->alpn->text(), ",", false);
        tls->utls->fingerPrint = ui->utlsFingerprint->currentText().trimmed();
        tls->utls->enabled = !tls->utls->fingerPrint.isEmpty();
        tls->saveFragmentState(ui->fragment->currentIndex());
        tls->fragment_fallback_delay = ui->tls_frag_fall_delay->text().trimmed();
        tls->record_fragment = ui->tls_rec_frag->isChecked();
        tls->saveTlsTricksState(ui->tls_tricks->currentIndex());
        tls->insecure = ui->insecure->isChecked();
        transport->headers = Configs::parseHeaderPairs(ui->headers->text());
        transport->method = ui->method->text().trimmed();
        transport->service_name = ui->service_name->text().trimmed();
        transport->early_data_header_name = ui->ws_early_data_name->text().trimmed();
        transport->max_early_data = ui->ws_early_data_length->text().trimmed().toInt();
        tls->reality->public_key = ui->reality_pbk->text().trimmed();
        tls->reality->short_id = ui->reality_sid->text().trimmed();
        tls->reality->enabled = !tls->reality->public_key.isEmpty();
        tls->certificate = CACHE.certificate;
    }
    if (ent->outbound->HasMux()) {
        auto mux = ent->outbound->GetMux();
        mux->saveMuxState(ui->multiplex->currentIndex());
        mux->brutal->enabled = ui->brutal_enable->isChecked();
        mux->brutal->down_mbps = ui->brutal_d_speed->text().trimmed().toInt();
        mux->brutal->up_mbps = ui->brutal_u_speed->text().trimmed().toInt();
    }
    if (ent->outbound->IsXray()) {
        auto xrayStream = ent->outbound->GetXrayStream();
        auto xrayMux = ent->outbound->GetXrayMultiplex();

        xrayStream->network = ui->xray_network->currentText().trimmed();
        xrayStream->security = ui->xray_security->currentText().trimmed();
        xrayStream->finalmask = CACHE.XrayFinalmask;
        xrayMux->saveMuxState(ui->xray_mux->currentIndex());

        auto sni = ui->xray_sni->text().trimmed();
        if (xrayStream->security == "tls") xrayStream->TLS->serverName = sni;
        else if (xrayStream->security == "reality") xrayStream->reality->serverName = sni;

        auto fp = ui->xray_fp->currentText().trimmed();
        if (xrayStream->security == "tls") xrayStream->TLS->fingerprint = fp;
        else if (xrayStream->security == "reality") xrayStream->reality->fingerprint = fp;

        xrayStream->TLS->alpn = SplitAndTrim(ui->xray_alpn->text(), ",", false);;
        xrayStream->TLS->pinnedPeerCertSha256 = ui->xray_pinned_peer_cert_sha256->text().trimmed();
        xrayStream->TLS->verifyPeerCertByName = ui->xray_verify_peer_cert_by_name->text().trimmed();
        xrayStream->reality->password = ui->xray_reality_pbk->text().trimmed();
        xrayStream->reality->shortId = ui->xray_reality_sid->text().trimmed();
        xrayStream->reality->spiderX = ui->xray_reality_spiderx->text().trimmed();

        if (xrayStream->network == "xhttp") {
            xrayStream->xhttp->host = ui->xray_host->text().trimmed();
            xrayStream->xhttp->path = ui->xray_path->text().trimmed();
            xrayStream->xhttp->mode = ui->xray_mode->currentText().trimmed();
            xrayStream->xhttp->headers = Configs::parseHeaderPairs(ui->xray_headers->text());
            xrayStream->xhttp->xPaddingBytes = ui->xray_xpaddingbytes->text().trimmed();
            xrayStream->xhttp->xPaddingObfsMode = ui->xray_xpadding_obfs_mode->isChecked();
            xrayStream->xhttp->xPaddingKey = ui->xray_xpadding_key->text().trimmed();
            xrayStream->xhttp->xPaddingHeader = ui->xray_xpadding_header->text().trimmed();
            xrayStream->xhttp->xPaddingPlacement = ui->xray_xpadding_placement->currentText().trimmed();
            xrayStream->xhttp->xPaddingMethod = ui->xray_xpadding_method->currentText().trimmed();
            xrayStream->xhttp->uplinkHTTPMethod = ui->xray_uplink_http_method->currentText().trimmed();
            xrayStream->xhttp->sessionIDPlacement = ui->xray_session_placement->currentText().trimmed();
            xrayStream->xhttp->sessionIDKey = ui->xray_session_key->text().trimmed();
            xrayStream->xhttp->sessionIDTable = ui->xray_session_id_table->text().trimmed();
            xrayStream->xhttp->sessionIDLength = ui->xray_session_id_length->text().trimmed();
            xrayStream->xhttp->seqPlacement = ui->xray_seq_placement->currentText().trimmed();
            xrayStream->xhttp->seqKey = ui->xray_seq_key->text().trimmed();
            xrayStream->xhttp->uplinkDataPlacement = ui->xray_uplink_data_placement->currentText().trimmed();
            xrayStream->xhttp->uplinkDataKey = ui->xray_uplink_data_key->text().trimmed();
            xrayStream->xhttp->uplinkChunkSize = ui->xray_uplink_chunk_size->text().trimmed();
            xrayStream->xhttp->noGRPCHeader = ui->xray_no_grpc->isChecked();
            xrayStream->xhttp->noSSEHeader = ui->xray_no_sse->isChecked();
            xrayStream->xhttp->scMaxEachPostBytes = ui->xray_scMaxEachPostBytes->text().trimmed();
            xrayStream->xhttp->scMinPostsIntervalMs = ui->xray_scMinPostsIntervalMs->text().trimmed();
            xrayStream->xhttp->scMaxBufferedPosts = ui->xray_scMaxBufferedPosts->text().trimmed().toLongLong();
            xrayStream->xhttp->scStreamUpServerSecs = ui->xray_scStreamUpServerSecs->text().trimmed();
            xrayStream->xhttp->serverMaxHeaderBytes = ui->xray_serverMaxHeaderBytes->text().trimmed().toInt();
            xrayStream->xhttp->maxConcurrency = ui->xray_max_concurrency->text().trimmed();
            xrayStream->xhttp->maxConnections = ui->xray_max_connections->text().trimmed();
            xrayStream->xhttp->hMaxRequestTimes = ui->xray_hMaxRequestTimes->text().trimmed();
            xrayStream->xhttp->hMaxReusableSecs = ui->xray_hMaxReusableSecs->text().trimmed();
            xrayStream->xhttp->cMaxReuseTimes = ui->xray_max_reuse_times->text().trimmed();
            xrayStream->xhttp->hKeepAlivePeriod = ui->xray_keep_alive_period->text().trimmed().toLongLong();
            xrayStream->xhttp->downloadSettings = xrayStream->xhttp->mode == "stream-one" ? QString() : CACHE.XrayDownloadSettings;
        } else if (xrayStream->network == "grpc") {
            xrayStream->grpc->authority = ui->xray_host->text().trimmed();
            xrayStream->grpc->serviceName = ui->xray_path->text().trimmed();
            xrayStream->grpc->multiMode = ui->xray_multi_mode->isChecked();
        } else if (xrayStream->network == "ws") {
            xrayStream->ws->host = ui->xray_host->text().trimmed();
            xrayStream->ws->path = ui->xray_path->text().trimmed();
            xrayStream->ws->ed = ui->xray_ed_length->text().trimmed().toInt();
            xrayStream->ws->headers = Configs::parseHeaderPairs(ui->xray_headers->text());
        } else if (xrayStream->network == "httpupgrade") {
            xrayStream->httpupgrade->host = ui->xray_host->text().trimmed();
            xrayStream->httpupgrade->path = ui->xray_path->text().trimmed();
            xrayStream->httpupgrade->ed = ui->xray_ed_length->text().trimmed().toInt();
            xrayStream->httpupgrade->headers = Configs::parseHeaderPairs(ui->xray_headers->text());
        }
    }

    return true;
}

void DialogEditProfile::accept() {
    if (!onEnd()) {
        return;
    }

    QStringList args;

    if (newEnt) {
        auto ok = Configs::dataManager->profilesRepo->AddProfile(ent);
        if (!ok) {
            MessageBoxWarning("???", "id exists");
        }
    } else {
        auto changed = Configs::dataManager->profilesRepo->Save(ent);
        if (changed && Configs::dataManager->settingsRepo->started_id == ent->id) args << MwArg::RestartProxy;
    }

    MW_dialog_message(MwMessage::ProfileChanged, args);
    QDialog::accept();
}

void DialogEditProfile::editor_cache_updated_impl() {
    if (CACHE.certificate.isEmpty()) {
        ui->certificate_edit->setText(tr("Not set"));
    } else {
        ui->certificate_edit->setText(tr("Already set"));
    }
    if (ent->outbound->IsXray()) {
        ui->xray_downloadsettings_edit->setText(CACHE.XrayDownloadSettings.isEmpty() ? "Not Set" : "Already Set");
        ui->xray_finalmask_edit->setText(CACHE.XrayFinalmask.isEmpty() ? "Not Set" : "Already Set");
    }
    for (auto a: innerEditor->get_editor_cached()) {
        if (a.second.isEmpty()) {
            a.first->setText(tr("Not set"));
        } else {
            a.first->setText(tr("Already set"));
        }
    }
}

void DialogEditProfile::on_certificate_edit_clicked() {
    bool ok;
    auto txt = QInputDialog::getMultiLineText(this, tr("Certificate"), "", CACHE.certificate.join("\n"), &ok);
    if (ok) {
        CACHE.certificate = txt.split("\n", Qt::SkipEmptyParts);
        editor_cache_updated_impl();
    }
}

void DialogEditProfile::on_xray_downloadsettings_edit_clicked() {
    auto editor = new JsonEdit::JsonEditorDialog(QString2QJsonObject(CACHE.XrayDownloadSettings), this);
    auto result = editor->OpenEditor();
    if (!result.isEmpty()) CACHE.XrayDownloadSettings = QJsonObject2QString(result, true);
    else CACHE.XrayDownloadSettings.clear();
    editor->deleteLater();

    editor_cache_updated_impl();
}

void DialogEditProfile::on_xray_finalmask_edit_clicked() {
    auto editor = new JsonEdit::JsonEditorDialog(CACHE.XrayFinalmask, this);
    auto result = editor->OpenEditor();
    CACHE.XrayFinalmask = result;
    editor->deleteLater();

    editor_cache_updated_impl();
}
