#include "include/ui/stats/RuntimeStatsWidget.h"

#include "include/ui/mainwindow.h"
#include "include/ui/stats/dialog_endpoint_details.h"
#include "include/api/RPC.h"
#include "include/stats/traffic/TrafficLooper.hpp"
#include "include/database/DatabaseManager.h"
#include "include/database/SettingsRepo.h"
#include "include/global/Utils.hpp"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/CountryHelper.hpp"

#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

#include <algorithm>

namespace {
    const QColor kRuntimeThroneColor = Stats::kStatsAccentColor;
    const QColor kRuntimeCoreColor = Stats::kStatsHealthyColor;

    QString formatCpu(const Sys::ProcessMetrics::Sample& s) {
        return s.ok ? QString::number(s.cpuPercent, 'f', 1) + QStringLiteral("%") : QStringLiteral("—");
    }

    QString formatRam(const Sys::ProcessMetrics::Sample& s) {
        return s.ok ? ReadableSize(s.rssBytes) : QStringLiteral("—");
    }
}

RuntimeStatsWidget::RuntimeStatsWidget(QWidget* parent) : QWidget(parent), ui(new Ui::RuntimeStatsWidget) {
    ui->setupUi(this);

    ui->rootGrid->setColumnStretch(0, 1);
    ui->rootGrid->setRowStretch(0, 1);
    ui->processLayout->setStretch(1, 1);
    // Without this the slack spreads over the label columns too, stranding each value far from its caption.
    ui->formRunning->setColumnStretch(1, 1);
    ui->formRunning->setColumnStretch(3, 1);

    ui->cpuChart->setColors(kRuntimeThroneColor, kRuntimeCoreColor);
    ui->ramChart->setColors(kRuntimeThroneColor, kRuntimeCoreColor);
    ui->cpuChart->setFormatter([](double v) { return QString::number(v, 'f', 0) + QStringLiteral("%"); });
    ui->ramChart->setFormatter([](double v) { return ReadableSize(static_cast<qint64>(v)); });
    ui->cpuChart->setCaption(tr("CPU"));
    ui->ramChart->setCaption(tr("RAM"));
    ui->labelThroneName->setText(QStringLiteral("<span style=\"color:%1\">●</span> %2")
                                     .arg(kRuntimeThroneColor.name(), "Throne"));
    ui->labelCoreName->setText(QStringLiteral("<span style=\"color:%1\">●</span> %2")
                                   .arg(kRuntimeCoreColor.name(), tr("Core")));

    ui->groupEndpoints->setVisible(false);
    auto* endpoints = ui->endpointsTable;
    endpoints->verticalHeader()->setVisible(false);
    endpoints->setEditTriggers(QAbstractItemView::NoEditTriggers);
    endpoints->setSelectionBehavior(QAbstractItemView::SelectRows);
    endpoints->setSelectionMode(QAbstractItemView::SingleSelection);
    endpoints->horizontalHeader()->setStretchLastSection(false);
    endpoints->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    endpoints->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    endpoints->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(endpoints, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem*) { openEndpointDetails(selectedEndpointTag()); });

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, [this]() { refreshLive(); });
}

RuntimeStatsWidget::~RuntimeStatsWidget() {
    delete ui;
}

// A splitter collapses a pane by moving it off-screen, never by hiding it, so isVisible() alone still reads true.
bool RuntimeStatsWidget::panelActive() const {
    return isVisible() && !window()->isMinimized() && !visibleRegion().isEmpty();
}

void RuntimeStatsWidget::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // A hidden gap turns the next CPU delta into an average over that gap, and splices the sparklines.
    metrics_.reset();
    ui->cpuChart->clear();
    ui->ramChart->clear();
    // The panel outlives a visit now, so the egress snapshot has to go stale with it; lastProbeSecs_ still rate-limits.
    egressSnapshotDone_ = false;
    timer_->start();
    refreshLive();
}

void RuntimeStatsWidget::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    timer_->stop();
    // Nothing refreshes it while the panel is away, and it would sit there showing a frozen endpoint.
    if (details_) details_->close();
}

void RuntimeStatsWidget::refreshLive() {
    if (!panelActive()) return;
    auto* mw = GetMainWindow();

    const auto selfSample = metrics_.sample(QCoreApplication::applicationPid());
    ui->vThroneRam->setText(formatRam(selfSample));
    ui->vThroneCpu->setText(formatCpu(selfSample));

    const qint64 corePid = mw ? mw->GetCorePid() : 0;
    const auto coreSample = corePid > 0 ? metrics_.sample(corePid) : Sys::ProcessMetrics::Sample{};
    ui->vCoreRam->setText(formatRam(coreSample));
    ui->vCoreCpu->setText(formatCpu(coreSample));

    ui->cpuChart->push(selfSample.ok ? selfSample.cpuPercent : 0.0,
                       coreSample.ok ? coreSample.cpuPercent : 0.0);
    ui->ramChart->push(selfSample.ok ? static_cast<double>(selfSample.rssBytes) : 0.0,
                       coreSample.ok ? static_cast<double>(coreSample.rssBytes) : 0.0);

    const auto rate = [](double bps) { return ReadableSize(static_cast<qint64>(bps)) + QStringLiteral("/s"); };
    if (Stats::trafficLooper && Stats::trafficLooper->proxy) {
        const auto p = Stats::trafficLooper->proxy;
        ui->vSpeedProxy->setText(QStringLiteral("↓ %1   ↑ %2").arg(rate(p->downlink_rate), rate(p->uplink_rate)));
    } else {
        ui->vSpeedProxy->setText(QStringLiteral("—"));
    }
    if (Stats::trafficLooper && Stats::trafficLooper->direct) {
        const auto dct = Stats::trafficLooper->direct;
        ui->vSpeedDirect->setText(QStringLiteral("↓ %1   ↑ %2").arg(rate(dct->downlink_rate), rate(dct->uplink_rate)));
    } else {
        ui->vSpeedDirect->setText(QStringLiteral("—"));
    }

    const auto nextUpd = [](int interval, qint64 last) -> QString {
        if (interval < 30) return RuntimeStatsWidget::tr("Disabled");
        const qint64 remaining = last > 0
            ? last + static_cast<qint64>(interval) * 60 - QDateTime::currentSecsSinceEpoch()
            : 0;
        if (remaining <= 0) return RuntimeStatsWidget::tr("Due now");
        return RuntimeStatsWidget::tr("in %1").arg(Stats::HumanizeDuration(remaining));
    };
    auto* settings = Configs::dataManager->settingsRepo.get();
    ui->vSubUpdate->setText(nextUpd(settings->sub_auto_update, settings->sub_auto_update_last));
    ui->vRouteUpdate->setText(nextUpd(settings->route_auto_update, settings->route_auto_update_last));

    qint64 dbBytes = 0;
    const QDir dir(QDir::currentPath());
    for (const QFileInfo& fi : dir.entryInfoList(QStringList{QStringLiteral("throne*.db*")}, QDir::Files))
        dbBytes += fi.size();
    ui->vDbSize->setText(ReadableSize(dbBytes));

    const qint64 up = appStartEpoch > 0 ? QDateTime::currentSecsSinceEpoch() - appStartEpoch : 0;
    ui->vUptime->setText(Stats::HumanizeDuration(up));

    const QString cfgName = mw ? mw->GetRunningConfigName() : QString();
    const qint64 nowSecs = QDateTime::currentSecsSinceEpoch();
    if (cfgName.isEmpty()) {
        ui->vCfgName->setText(tr("No active config"));
        ui->vOutIp->setText(QStringLiteral("—"));
        ui->vCountry->setText(QStringLiteral("—"));
        ui->vPing->setText(QStringLiteral("—"));
        lastProbedConfig_.clear();
        egressSnapshotDone_ = false;
    } else {
        ui->vCfgName->setText(cfgName);
        if (cfgName != lastProbedConfig_) {
            lastProbedConfig_ = cfgName;
            egressSnapshotDone_ = false;
            lastProbeSecs_ = 0;
            // The probe runs for seconds; without this the old config's egress sits under the new config's name.
            ui->vOutIp->setText(QStringLiteral("—"));
            ui->vCountry->setText(QStringLiteral("—"));
            ui->vPing->setText(QStringLiteral("—"));
        }
        if (!egressSnapshotDone_ && (lastProbeSecs_ == 0 || nowSecs - lastProbeSecs_ >= 30)) {
            lastProbeSecs_ = nowSecs;
            probeEgress();
        }
    }

    if (!connBusy_.exchange(true)) {
        QPointer<RuntimeStatsWidget> self(this);
        runOnNewThread([self]() {
            const auto conns = API::defaultClient->QueryConnections();
            int tcp = 0, udp = 0, total = 0;
            for (const auto& c : conns.active) {
                ++total;
                const QString net = QString::fromStdString(c.network.value());
                if (net == QStringLiteral("tcp")) ++tcp;
                else if (net == QStringLiteral("udp")) ++udp;
            }
            runOnUiThread([self, tcp, udp, total]() {
                if (!self) return;
                self->ui->vConns->setText(
                    RuntimeStatsWidget::tr("%1 active   ·   %2 TCP   ·   %3 UDP").arg(total).arg(tcp).arg(udp));
                self->connBusy_.store(false);
            });
        });
    }

    if (!vpnBusy_.exchange(true)) {
        QPointer<RuntimeStatsWidget> self(this);
        runOnNewThread([self]() {
            bool ok = false;
            // Empty tag list = every live endpoint; 0 ms = current state, never wait.
            const auto status = API::defaultClient->QueryVPNStatus(&ok, {}, 0);
            QList<Stats::VpnEndpointView> views;
            if (ok) {
                views.reserve(static_cast<qsizetype>(status.results.size()));
                for (const auto& result : status.results) views << Stats::MakeVpnEndpointView(result);
            }
            runOnUiThread([self, views]() {
                if (!self) return;
                self->applyEndpoints(views);
                self->vpnBusy_.store(false);
            });
        });
    }
}

QString RuntimeStatsWidget::selectedEndpointTag() const {
    const auto* selection = ui->endpointsTable->selectionModel();
    const auto rows = selection != nullptr ? selection->selectedRows() : QModelIndexList{};
    if (rows.isEmpty()) return {};
    const auto* item = ui->endpointsTable->item(rows.first().row(), 0);
    return item == nullptr ? QString() : item->data(Qt::UserRole).toString();
}

// A table's size hint ignores its rows, and the header's geometry is meaningless before show.
void RuntimeStatsWidget::fitEndpointTable() {
    auto* table = ui->endpointsTable;
    // Rows keep the vertical header's default section size until asked.
    table->resizeRowsToContents();
    // A cell widget is invisible to resizeRowsToContents, so the button sets the floor.
    for (int row = 0; row < table->rowCount(); row++) {
        if (auto* cell = table->cellWidget(row, 2))
            table->setRowHeight(row, std::max(table->rowHeight(row), cell->sizeHint().height()));
    }
    const int shown = std::min(table->rowCount(), 4);
    int height = table->horizontalHeader()->sizeHint().height() + 2 * table->frameWidth();
    for (int row = 0; row < shown; row++) height += table->rowHeight(row);
    table->setFixedHeight(height);
}

void RuntimeStatsWidget::applyEndpoints(const QList<Stats::VpnEndpointView>& views) {
    endpointViews_ = views;

    if (views.isEmpty()) {
        ui->groupEndpoints->setVisible(false);
        ui->endpointsTable->setRowCount(0);
        endpointTags_.clear();
        if (details_) details_->markGone();
        return;
    }

    QStringList tags;
    tags.reserve(views.size());
    for (const auto& view : views) tags << view.tag;

    ui->groupEndpoints->setVisible(true);

    auto* table = ui->endpointsTable;
    const bool rebuilt = tags != endpointTags_;
    if (rebuilt) {
        const QString keep = selectedEndpointTag();
        endpointTags_ = tags;
        table->setRowCount(static_cast<int>(views.size()));
        for (int row = 0; row < views.size(); row++) {
            for (int column = 0; column < 3; column++) {
                if (table->item(row, column) == nullptr) table->setItem(row, column, new QTableWidgetItem());
            }
            table->item(row, 0)->setData(Qt::UserRole, views[row].tag);
            auto* details = new QPushButton(tr("Details"), table);
            const QString tag = views[row].tag;
            connect(details, &QPushButton::clicked, this, [this, tag]() { openEndpointDetails(tag); });
            table->setCellWidget(row, 2, details);
        }
        const auto keptRow = static_cast<int>(tags.indexOf(keep));
        if (!keep.isEmpty() && keptRow >= 0) table->selectRow(keptRow);
        else if (!keep.isEmpty()) table->clearSelection();
    }

    for (int row = 0; row < views.size(); row++) {
        const auto& view = views[row];
        table->item(row, 0)->setText(view.displayName);
        table->item(row, 1)->setText(Stats::VpnStateText(view.state));
        table->item(row, 1)->setForeground(QBrush(Stats::VpnStateColor(view.state)));
        const QString hint = view.error.isEmpty() ? view.tag : view.tag + QLatin1Char(0x0a) + view.error;
        table->item(row, 0)->setToolTip(hint);
        table->item(row, 1)->setToolTip(hint);
    }
    if (rebuilt) fitEndpointTable();

    if (details_) {
        const auto found = std::find_if(views.cbegin(), views.cend(),
                                        [this](const Stats::VpnEndpointView& view) {
                                            return view.tag == details_->tag();
                                        });
        if (found != views.cend()) details_->applyStatus(*found);
        else details_->markGone();
    }
}

void RuntimeStatsWidget::openEndpointDetails(const QString& tag) {
    if (tag.isEmpty()) return;
    const auto found = std::find_if(endpointViews_.cbegin(), endpointViews_.cend(),
                                    [&tag](const Stats::VpnEndpointView& view) { return view.tag == tag; });
    if (found == endpointViews_.cend()) return;

    if (details_ && details_->tag() == tag) {
        details_->raise();
        details_->activateWindow();
        return;
    }
    if (details_) details_->close();

    details_ = new DialogEndpointDetails(*found, window());
    details_->setAttribute(Qt::WA_DeleteOnClose);
    details_->show();
    details_->raise();
    details_->activateWindow();
}

void RuntimeStatsWidget::probeEgress() {
    if (probing_.exchange(true)) return;

    auto* mw = GetMainWindow();
    if (mw == nullptr || mw->GetRunningConfigName().isEmpty()) {
        probing_.store(false);
        return;
    }

    ui->vPing->setText(QStringLiteral("…"));
    if (ui->vOutIp->text() == QStringLiteral("—")) ui->vOutIp->setText(QStringLiteral("…"));
    if (ui->vCountry->text() == QStringLiteral("—")) ui->vCountry->setText(QStringLiteral("…"));

    QPointer<RuntimeStatsWidget> self(this);
    runOnNewThread([self]() {
        QString pingText;
        {
            libcore::TestReq req;
            req.test_current = true;
            req.url = Configs::dataManager->settingsRepo->test_latency_url.toStdString();
            bool ok = false;
            const auto res = API::defaultClient->Test(&ok, req);
            if (ok && !res.results.empty()) {
                const int lat = res.results[0].latency_ms.value();
                const auto vpnText = lat > 0 ? QString() : MainWindow::liveVpnConnectOkText();
                pingText = lat > 0 ? QStringLiteral("%1 ms").arg(lat)
                                   : (vpnText.isEmpty() ? RuntimeStatsWidget::tr("Unavailable") : vpnText);
            } else {
                pingText = RuntimeStatsWidget::tr("N/A");
            }
        }

        QString ipText = RuntimeStatsWidget::tr("N/A");
        QString countryText = RuntimeStatsWidget::tr("N/A");
        bool egressOk = false;
        const auto resp = NetworkRequestHelper::HttpGet(QStringLiteral("http://ip-api.com/json/"), false, true);
        if (resp.error.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(resp.data);
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                const QString ip = obj[QStringLiteral("query")].toString();
                const QString countryName = obj[QStringLiteral("country")].toString();
                const QString countryCode = obj[QStringLiteral("countryCode")].toString();
                const QString city = obj[QStringLiteral("city")].toString();
                if (!ip.isEmpty()) {
                    ipText = ip;
                    egressOk = true;
                }
                if (!countryName.isEmpty()) {
                    countryText = CountryCodeToFlag(countryCode) + QStringLiteral(" ") + countryName;
                    if (!city.isEmpty()) countryText += QStringLiteral(", ") + city;
                }
            }
        }

        runOnUiThread([self, pingText, ipText, countryText, egressOk]() {
            if (!self) return;
            if (!self->egressSnapshotDone_) {
                self->ui->vPing->setText(pingText);
                self->ui->vOutIp->setText(ipText);
                self->ui->vCountry->setText(countryText);
                if (egressOk) self->egressSnapshotDone_ = true;
            }
            self->probing_.store(false);
        });
    });
}
