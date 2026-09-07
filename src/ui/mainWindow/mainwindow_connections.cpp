#include "include/ui/mainwindow.h"
#include "include/api/RPC.h"
#include "include/global/LocalNetwork.hpp"
#include "include/ui/utils/ConnectionCloseDelegate.h"
#include "include/ui/utils/ConnectionsFilterHeader.h"
#include "include/ui/utils/ConnectionsFilterProxyModel.h"
#include "include/ui/utils/ConnectionsTableModel.h"

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
    // The material set is pure black, so it has to be tinted for dark themes.
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
    // The close column has no text, so ResizeToContents would collapse it to nothing.
    header->setSectionResizeMode(ConnectionsTableModel::ColClose, QHeaderView::Fixed);
    ui->connections->setColumnWidth(ConnectionsTableModel::ColClose, ConnectionCloseDelegate::ColumnWidth);
    ui->connections->verticalHeader()->hide();

    // Otherwise the five content-sized columns re-measure up to 1000 rows whenever a poll changes the count.
    header->setResizeContentsPrecision(20);
    ui->connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->connections->setWordWrap(false);

    refreshConnectionCloseIcons();
    restoreConnectionSort();
    setupConnectionSortMenu();
    setupConnectionFilter();

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

    // The corner widget spans the whole tab bar, so it stays put and only greys out away from the connections tab.
    auto syncEnabled = [=,this] { corner->setEnabled(ui->stats_widget->currentWidget() == ui->connections_tab); };
    connect(ui->stats_widget, &QTabWidget::currentChanged, this, [syncEnabled](int) { syncEnabled(); });
    syncEnabled();

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
    // refresh_status() drives this on a 2s tick, so bail out unless the state actually flipped.
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
    // Blocks until the core has walked every id, and "close all listed" hands it the whole table.
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
