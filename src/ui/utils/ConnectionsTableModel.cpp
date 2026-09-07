#include "include/ui/utils/ConnectionsTableModel.h"
#include "include/global/Utils.hpp"

#include <QCoreApplication>

namespace {
    const QString kEmptyText;

    // These moved out of mainwindow.ui; plain tr() here would orphan every existing .ts entry.
    QString mwTr(const char *source) {
        return QCoreApplication::translate("MainWindow", source);
    }
}

ConnectionsTableModel::ConnectionsTableModel(QObject *parent)
    : QAbstractTableModel(parent) {}

int ConnectionsTableModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_rows.size());
}

int ConnectionsTableModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return ColumnCount;
}

Qt::ItemFlags ConnectionsTableModel::flags(const QModelIndex &index) const {
    // Enabled is what routes the click to the delegate; selectable would leave a cell highlighted behind it.
    if (index.column() == ColClose) return Qt::ItemIsEnabled;
    return QAbstractTableModel::flags(index);
}

void ConnectionsTableModel::ensureDerived(int row) const {
    Derived &d = m_derived[row];
    if (d.valid) return;
    const auto &c = m_rows.at(row);
    d.dest = DisplayDest(c.dest, c.domain);
    d.protocol = c.protocol.isEmpty() ? c.network : c.network + " (" + c.protocol + ")";
    d.valid = true;
}

const Stats::ConnectionMetadata *ConnectionsTableModel::metaAt(int row) const {
    if (row < 0 || row >= m_rows.size()) return nullptr;
    return &m_rows.at(row);
}

const QString &ConnectionsTableModel::destText(int row) const {
    if (row < 0 || row >= m_rows.size()) return kEmptyText;
    ensureDerived(row);
    return m_derived.at(row).dest;
}

const QString &ConnectionsTableModel::protocolText(int row) const {
    if (row < 0 || row >= m_rows.size()) return kEmptyText;
    ensureDerived(row);
    return m_derived.at(row).protocol;
}

QVariant ConnectionsTableModel::data(const QModelIndex &index, int role) const {
    const int row = index.row();
    if (!index.isValid() || row < 0 || row >= m_rows.size()) return {};
    const auto &c = m_rows.at(row);

    if (role == ConnIdRole) return c.id;

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColSource: return c.sourceDisplay;
        case ColDest: return destText(row);
        case ColProcess: return c.process;
        case ColProtocol: return protocolText(row);
        case ColOutbound: return c.outbound;
        case ColTraffic: return ReadableSize(c.upload) + "↑" + " " + ReadableSize(c.download) + "↓";
        case ColSpeed: return ReadableSize(c.uploadSpeed) + "/s↑" + " " + ReadableSize(c.downloadSpeed) + "/s↓";
        default: return {};
        }
    }

    if (role == Qt::ToolTipRole && index.column() == ColClose) {
        return mwTr("Close this connection");
    }

    return {};
}

QVariant ConnectionsTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal) return {};

    if (role == Qt::DisplayRole) {
        switch (section) {
        case ColSource: return mwTr("Source");
        case ColDest: return mwTr("Destination (Domain)");
        case ColProcess: return mwTr("Process");
        case ColProtocol: return mwTr("Protocol");
        case ColOutbound: return mwTr("Outbound");
        case ColTraffic: return mwTr("Traffic");
        case ColSpeed: return mwTr("Speed");
        default: return {};
        }
    }

    if (role == Qt::ToolTipRole) {
        switch (section) {
        case ColSource: return mwTr("Click To Sort By Source");
        case ColDest: return mwTr("Click To Disable Sorting");
        case ColProcess: return mwTr("Click To Sort By Process");
        case ColProtocol: return mwTr("Click To Sort By Protocol");
        case ColOutbound: return mwTr("Click To Sort By Outbound");
        case ColTraffic: return mwTr("Click to sort by traffic; right-click to choose total/down/up");
        case ColSpeed: return mwTr("Click to sort by speed; right-click to choose total/down/up");
        default: return {};
        }
    }

    return {};
}

// Only the length delta needs structural signals; the shared prefix takes one dataChanged.
void ConnectionsTableModel::setConnections(const QList<Stats::ConnectionMetadata> &connections) {
    const int oldCount = static_cast<int>(m_rows.size());
    const int newCount = static_cast<int>(connections.size());

    auto commit = [&] {
        m_rows = connections;
        m_derived.clear();
        m_derived.resize(newCount);
    };

    if (newCount > oldCount) {
        beginInsertRows({}, oldCount, newCount - 1);
        commit();
        endInsertRows();
    } else if (newCount < oldCount) {
        beginRemoveRows({}, newCount, oldCount - 1);
        commit();
        endRemoveRows();
    } else {
        if (newCount == 0) return;
        commit();
    }

    const int overlap = qMin(oldCount, newCount);
    if (overlap > 0) {
        emit dataChanged(index(0, 0), index(overlap - 1, ColumnCount - 1));
    }
}
