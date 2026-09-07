#include "include/ui/utils/ConnectionsFilterProxyModel.h"
#include "include/ui/utils/ConnectionsTableModel.h"

ConnectionsFilterProxyModel::ConnectionsFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {
    // Every poll rewrites the rows in place, so acceptance has to be re-tested on dataChanged.
    setDynamicSortFilter(true);
}

ConnectionsTableModel *ConnectionsFilterProxyModel::connectionsModel() const {
    return qobject_cast<ConnectionsTableModel *>(sourceModel());
}

bool ConnectionsFilterProxyModel::hasActiveFilter() const {
    return !m_source.isEmpty() || !m_dest.isEmpty() || !m_process.isEmpty()
           || !m_protocol.isEmpty() || !m_outbound.isEmpty();
}

void ConnectionsFilterProxyModel::setFilters(const QString &source, const QString &dest, const QString &process,
                                             const QString &protocol, const QString &outbound) {
    if (m_source == source && m_dest == dest && m_process == process
        && m_protocol == protocol && m_outbound == outbound) return;
    m_source = source;
    m_dest = dest;
    m_process = process;
    m_protocol = protocol;
    m_outbound = outbound;
    invalidateRowsFilter();
}

bool ConnectionsFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &) const {
    if (!hasActiveFilter()) return true;

    auto *model = connectionsModel();
    if (model == nullptr) return true;

    const auto *meta = model->metaAt(sourceRow);
    if (meta == nullptr) return false;

    if (!m_source.isEmpty() && !meta->sourceDisplay.contains(m_source, Qt::CaseInsensitive)) return false;
    if (!m_process.isEmpty() && !meta->process.contains(m_process, Qt::CaseInsensitive)) return false;
    if (!m_outbound.isEmpty() && !meta->outbound.contains(m_outbound, Qt::CaseInsensitive)) return false;
    // Composed strings last: only these two can cost an allocation on first touch.
    if (!m_dest.isEmpty() && !model->destText(sourceRow).contains(m_dest, Qt::CaseInsensitive)) return false;
    if (!m_protocol.isEmpty() && !model->protocolText(sourceRow).contains(m_protocol, Qt::CaseInsensitive)) return false;
    return true;
}
