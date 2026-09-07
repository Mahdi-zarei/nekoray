#pragma once

#include <QSortFilterProxyModel>
#include <QString>

class ConnectionsTableModel;

// Substring filter over the five text columns; traffic/speed hold formatted byte counts, so they get no field.
class ConnectionsFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit ConnectionsFilterProxyModel(QObject *parent = nullptr);

    // An empty string disables that test.
    void setFilters(const QString &source, const QString &dest, const QString &process,
                    const QString &protocol, const QString &outbound);

    bool hasActiveFilter() const;

    ConnectionsTableModel *connectionsModel() const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_source;
    QString m_dest;
    QString m_process;
    QString m_protocol;
    QString m_outbound;
};
