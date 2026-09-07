#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QString>

#include "include/stats/connections/connectionLister.hpp"

// Holds one poll snapshot; the lister already ordered it, so row identity is purely positional.
class ConnectionsTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum { ConnIdRole = Qt::UserRole };

    enum Column {
        ColSource = 0,
        ColDest,
        ColProcess,
        ColProtocol,
        ColOutbound,
        ColTraffic,
        ColSpeed,
        ColClose,
        ColumnCount,
    };

    explicit ConnectionsTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setConnections(const QList<Stats::ConnectionMetadata> &connections);

    // Valid until the next setConnections().
    const Stats::ConnectionMetadata *metaAt(int row) const;

    const QString &destText(int row) const;
    const QString &protocolText(int row) const;

private:
    struct Derived {
        QString dest;
        QString protocol;
        bool valid = false;
    };

    void ensureDerived(int row) const;

    QList<Stats::ConnectionMetadata> m_rows;
    mutable QList<Derived> m_derived;
};
