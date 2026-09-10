#include "include/ui/mainwindow.h"
#include "include/api/RPC.h"
#include "include/database/entities/RouteProfile.h"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/global/LocalNetwork.hpp"
#include "include/ui/utils/ConnectionCloseDelegate.h"
#include "include/ui/utils/ConnectionsFilterHeader.h"
#include "include/ui/utils/ConnectionsFilterProxyModel.h"
#include "include/ui/utils/ConnectionsTableModel.h"

#include <QHostAddress>

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QHeaderView>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>

namespace
{
    QIcon RecolorIcon(const QString& path, const QColor& color)
    {
        QPixmap pixmap(path);
        if (pixmap.isNull()) return QIcon(path);
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        painter.end();
        return QIcon(pixmap);
    }
}

void MainWindow::setupConnectionList()
{
    connectionsModel = new ConnectionsTableModel(this);
    connectionsFilterModel = new ConnectionsFilterProxyModel(this);
    connectionsFilterModel->setSourceModel(connectionsModel);
    ui->connections->setModel(connectionsFilterModel);

    // Order matters: setModel() after this would re-init the sections and drop the resize modes below.
    connectionFilterHeader = new ConnectionsFilterHeader(ui->connections);
    ui->connections->setHorizontalHeader(connectionFilterHeader);

    connectionCloseDelegate = new ConnectionCloseDelegate(this);
    ui->connections->setItemDelegateForColumn(ConnectionsTableModel::ColClose, connectionCloseDelegate);
    connect(connectionCloseDelegate, &ConnectionCloseDelegate::closeRequested, this,
            [this](const QString& id) { closeConnections({id}); });

    auto* header = ui->connections->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(ConnectionsTableModel::ColSource, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColDest, QHeaderView::Stretch);
    header->setSectionResizeMode(ConnectionsTableModel::ColProcess, QHeaderView::Stretch);
    header->setSectionResizeMode(ConnectionsTableModel::ColProtocol, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColOutbound, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColTraffic, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColSpeed, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColClose, QHeaderView::Fixed);
    ui->connections->setColumnWidth(ConnectionsTableModel::ColClose, ConnectionCloseDelegate::ColumnWidth);
    ui->connections->verticalHeader()->hide();

    header->setResizeContentsPrecision(20);
    ui->connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->connections->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->connections->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->connections->setAlternatingRowColors(true);
    ui->connections->setWordWrap(false);

    refreshConnectionCloseIcons();
    restoreConnectionSort();
    setupConnectionSortMenu();
    setupConnectionFilter();

    ui->connections->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->connections, &QWidget::customContextMenuRequested, this, &MainWindow::onConnectionContextMenu);

    connect(ui->connections, &QAbstractItemView::clicked, this, [this](const QModelIndex& index)
    {
        if (!index.isValid() || index.column() == ConnectionsTableModel::ColClose) return;
        const auto text = index.data(Qt::DisplayRole).toString();
        if (text.isEmpty()) return;

        QApplication::clipboard()->setText(text);
        const QPoint pos = ui->connections->viewport()->mapToGlobal(ui->connections->visualRect(index).center());
        QToolTip::showText(pos, tr("Copied!"), this);
        auto r = ++toolTipID;
        QTimer::singleShot(1500, this, [=,this] {
            if (r != toolTipID)
            {
                return;
            }
            QToolTip::hideText();
        });
    });

    syncConnectionSourceColumn();
}

void MainWindow::restoreConnectionSort()
{
    const auto* settings = Configs::dataManager->settingsRepo.get();
    int stored = settings->connection_sort;
    if (stored < Stats::Default || stored > Stats::BySource) return;
    // The Source header is unreachable while its column is hidden, so that sort would be stuck for good.
    if (stored == Stats::BySource && !LocalNetwork::LanInboundEnabled()) stored = Stats::Default;
    // Runs before setup_rpc() spawns the lister thread, so writing the pair unguarded is safe.
    Stats::connection_lister->restoreSort(static_cast<Stats::ConnectionSort>(stored), settings->connection_sort_asc);
}

void MainWindow::applyConnectionSort(Stats::ConnectionSort sort)
{
    Stats::connection_lister->setSort(sort);
    auto* settings = Configs::dataManager->settingsRepo.get();
    settings->connection_sort = Stats::connection_lister->getSort();
    settings->connection_sort_asc = Stats::connection_lister->isSortAscending();
    settings->Save();
    Stats::connection_lister->ForceUpdate();
}

void MainWindow::setupConnectionFilter()
{
    auto* btnFilter = new QToolButton(this);
    btnFilter->setIcon(QIcon(":/icon/filter.png"));
    btnFilter->setToolTip(tr("Enable Filter"));
    btnFilter->setCheckable(true);
    connect(btnFilter, &QToolButton::toggled, connectionFilterHeader, &ConnectionsFilterHeader::setFiltersVisible);
    connect(connectionFilterHeader, &ConnectionsFilterHeader::closeRequested, btnFilter, [btnFilter] { btnFilter->setChecked(false); });

    connectionCloseAllButton = new QToolButton(this);
    connectionCloseAllButton->setIcon(connectionCloseIcon);
    connectionCloseAllButton->setToolTip(tr("Close every connection listed below"));
    connect(connectionCloseAllButton, &QToolButton::clicked, this, [this] { closeConnections(listedConnectionIds()); });

    auto* corner = new QWidget(this);
    auto* cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(2);
    cornerLayout->addWidget(btnFilter);
    cornerLayout->addWidget(connectionCloseAllButton);
    ui->stats_widget->setCornerWidget(corner, Qt::TopRightCorner);

    auto syncCorner = [=,this] { corner->setVisible(ui->stats_widget->currentWidget() == ui->connections_tab); };
    connect(ui->stats_widget, &QTabWidget::currentChanged, this, [syncCorner](int) { syncCorner(); });
    syncCorner();

    connectionFilterDebounce = new QTimer(this);
    connectionFilterDebounce->setSingleShot(true);
    connectionFilterDebounce->setInterval(50);
    connect(connectionFilterDebounce, &QTimer::timeout, this, [this] { applyConnectionFilters(); });
    connect(connectionFilterHeader, &ConnectionsFilterHeader::filtersChanged, this, [this] { connectionFilterDebounce->start(); });
}

void MainWindow::applyConnectionFilters()
{
    const auto filters = connectionFilterHeader->filters();
    connectionsFilterModel->setFilters(filters.source, filters.dest, filters.process, filters.protocol,
                                       filters.outbound);
}

void MainWindow::syncConnectionSourceColumn()
{
    if (connectionsModel == nullptr) return;
    const bool show = LocalNetwork::LanInboundEnabled();
    if (ui->connections->isColumnHidden(ConnectionsTableModel::ColSource) == !show) return;

    ui->connections->setColumnHidden(ConnectionsTableModel::ColSource, !show);
    connectionFilterHeader->adjustPositions();
    // Both must be cleared here: a hidden header can be reached by neither the filter field nor a sort click.
    if (!show) {
        connectionFilterHeader->clearFilterFor(ConnectionsTableModel::ColSource);
        if (Stats::connection_lister->getSort() == Stats::BySource) applyConnectionSort(Stats::Default);
    }
    applyConnectionFilters();
}

void MainWindow::setupConnectionSortMenu()
{
    auto* header = ui->connections->horizontalHeader();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QWidget::customContextMenuRequested, this, [=,this](const QPoint& pos)
    {
        const int columnIndex = header->logicalIndexAt(pos);
        const bool isTraffic = columnIndex == ConnectionsTableModel::ColTraffic;
        const bool isSpeed = columnIndex == ConnectionsTableModel::ColSpeed;
        if (!isTraffic && !isSpeed) return;

        struct SortOption { Stats::ConnectionSort value; QString label; };
        const QList<SortOption> options = isTraffic
            ? QList<SortOption>{
                { Stats::ByTraffic, tr("Total") },
                { Stats::ByDownload, tr("Downloaded") },
                { Stats::ByUpload, tr("Uploaded") } }
            : QList<SortOption>{
                { Stats::BySpeed, tr("Total") },
                { Stats::ByDownloadSpeed, tr("Download Speed") },
                { Stats::ByUploadSpeed, tr("Upload Speed") } };

        QMenu menu(this);
        auto* sortByLabel = menu.addAction(tr("Sort By:"));
        sortByLabel->setEnabled(false);

        const auto current = Stats::connection_lister->getSort();
        for (const auto& opt : options)
        {
            auto* act = menu.addAction(opt.label);
            act->setData(static_cast<int>(opt.value));
            act->setCheckable(true);
            act->setChecked(current == opt.value);
        }

        auto* chosen = menu.exec(header->mapToGlobal(pos));
        if (chosen == nullptr || !chosen->data().isValid()) return;

        applyConnectionSort(static_cast<Stats::ConnectionSort>(chosen->data().toInt()));
    });
}

void MainWindow::refreshConnectionCloseIcons()
{
    // ApplyTheme() fires PaletteChange from the constructor, before setupUi() has built the table.
    if (connectionCloseDelegate == nullptr) return;

    connectionCloseIcon = RecolorIcon(":/icon/material/cancel.png", palette().color(QPalette::ButtonText));
    connectionCloseDelegate->setIcon(connectionCloseIcon);
    if (connectionCloseAllButton != nullptr) connectionCloseAllButton->setIcon(connectionCloseIcon);
    ui->connections->viewport()->update();
}

QStringList MainWindow::listedConnectionIds() const
{
    QStringList ids;
    const int rows = connectionsFilterModel->rowCount();
    ids.reserve(rows);
    for (int row = 0; row < rows; row++)
    {
        const auto id = connectionsFilterModel->index(row, 0).data(ConnectionsTableModel::ConnIdRole).toString();
        if (!id.isEmpty()) ids << id;
    }
    return ids;
}

void MainWindow::closeConnections(const QStringList& ids)
{
    if (ids.isEmpty()) return;
    runOnNewThread([ids] {
        bool rpcOK = false;
        const auto err = API::defaultClient->CloseConnections(&rpcOK, ids);
        if (!rpcOK || !err.isEmpty())
        {
            MW_show_log(tr("Failed to close connections: %1").arg(err.isEmpty() ? tr("IPC error") : err));
            return;
        }
        Stats::connection_lister->ForceUpdate();
    });
}

void MainWindow::UpdateConnectionList(const QList<Stats::ConnectionMetadata>& connections)
{
    if (connectionsModel == nullptr) return;
    connectionsModel->setConnections(connections);
}

QString MainWindow::routeRuleAppendBlocker() const
{
    const auto& dm = Configs::dataManager;
    const auto currentRoute = dm->routesRepo->GetRouteProfile(dm->settingsRepo->current_route_id);
    if (!currentRoute) return tr("No active routing profile found.");
    if (currentRoute->preventModifications) return tr("The current routing profile is locked against modifications.");
    if (currentRoute->isRaw) return tr("The current routing profile is raw JSON.");
    if (currentRoute->isRemote && currentRoute->autoUpdate) return tr("The current routing profile auto-updates from a URL.");
    return {};
}

bool MainWindow::addRuleToCurrentRoute(const QString& rawRule, Configs::simpleAction action)
{
    auto fail = [this](const QString& msg) {
        MW_show_log(msg);
        return false;
    };

    if (const auto blocker = routeRuleAppendBlocker(); !blocker.isEmpty()) return fail(blocker);

    const auto& dm = Configs::dataManager;
    const auto currentRoute = dm->routesRepo->GetRouteProfile(dm->settingsRepo->current_route_id);
    if (!currentRoute) return fail(tr("No active routing profile found."));

    if (!currentRoute->AppendSimpleRule(rawRule, action))
        return fail(tr("Failed to add routing rule: %1").arg(rawRule));

    if (!dm->routesRepo->Save(currentRoute))
        return fail(tr("Failed to save routing rule: %1").arg(rawRule));

    MW_show_log(tr("Appended %1 to the %2 rules of \"%3\"")
                    .arg(rawRule, Configs::simpleActionToString(action), currentRoute->name));
    noteRestartNeeded(tr("Routing"));
    return true;
}

void MainWindow::onConnectionContextMenu(const QPoint& pos)
{
    const QModelIndex proxyIndex = ui->connections->indexAt(pos);
    if (!proxyIndex.isValid()) return;

    const QModelIndex sourceIndex = connectionsFilterModel->mapToSource(proxyIndex);
    if (!sourceIndex.isValid()) return;

    const auto* meta = connectionsModel->metaAt(sourceIndex.row());
    if (!meta) return;

    const QString domain = meta->domain.trimmed();
    const QString process = meta->process.trimmed();
    const QString host = domain.isEmpty() ? Stats::EndpointHost(meta->dest.trimmed()) : domain;
    if (host.isEmpty() && process.isEmpty()) return;

    ui->connections->setCurrentIndex(proxyIndex);

    const bool isDomain = QHostAddress(host).isNull();
    const QString addressRule = isDomain ? ("suffix:" + host) : ("ip:" + host);
    const QString processRule = "processName:" + process;

    QMenu menu(this);
    const QPoint globalPos = ui->connections->viewport()->mapToGlobal(pos);

    auto showTip = [this](const QString& text) {
        QToolTip::showText(QCursor::pos(), text, this);
        auto r = ++toolTipID;
        QTimer::singleShot(2000, this, [=, this] {
            if (r == toolTipID) QToolTip::hideText();
        });
    };

    struct RouteAction { Configs::simpleAction action; QString label; };
    const RouteAction routeActions[] = {
        { Configs::bypass, tr("Direct") },
        { Configs::proxy,  tr("Proxy") },
        { Configs::block,  tr("Block") },
    };

    const QString blocker = routeRuleAppendBlocker();

    auto addRouteSubmenu = [&](const QString& title, const QString& rule) {
        auto* sub = menu.addMenu(title);
        if (!blocker.isEmpty())
        {
            sub->setEnabled(false);
            sub->menuAction()->setToolTip(blocker);
            return;
        }
        for (const auto& ra : routeActions)
        {
            auto* act = sub->addAction(ra.label);
            connect(act, &QAction::triggered, this, [this, rule, ra, showTip] {
                if (addRuleToCurrentRoute(rule, ra.action))
                    showTip(tr("Appended to the %1 rules:\n%2").arg(ra.label, rule));
            });
        }
    };

    menu.setToolTipsVisible(true);
    if (!host.isEmpty()) addRouteSubmenu(tr("Append \"%1\" to").arg(host), addressRule);
    if (!process.isEmpty()) addRouteSubmenu(tr("Append process \"%1\" to").arg(process), processRule);

    menu.exec(globalPos);
}
