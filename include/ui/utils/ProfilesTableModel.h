#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QHash>
#include <QColor>
#include <memory>

namespace Configs {
    class Profile;
}

class ProfilesTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum { ProfileIdRole = Qt::UserRole };

    enum Column {
        ColType = 0,
        ColAddress,
        ColName,
        ColTestResult,
        ColTraffic,
        ColumnCount,
    };

    struct FilterKey {
        QString type;
        QString address;
        QString name;
        QString country;
        int port = 0;
    };

    explicit ProfilesTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    Qt::DropActions supportedDropActions() const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void refreshTable(const QList<int> &ids = {}, bool mayNeedReset = false);

    void refreshProfileId(int profileId);

    void emplaceProfiles(int row1, int row2);

    int indexOfProfile(int id);

    QString rowLabel(int sourceRow, int displayRow) const;

    // Null if the profile could not be loaded; valid until the next model change.
    const FilterKey *filterKeyAt(int row) const;

private:
    void ensureCached(int profileId) const;
    void evictOne() const;
    void setProfileIds(const QList<int> &ids);
    void ensureFilterIndex() const;

    QList<int> m_profileIds;
    mutable QHash<int, int> id2row;
    mutable QHash<int, std::shared_ptr<Configs::Profile>> m_cache;
    mutable QList<int> m_lruOrder;
    int m_cacheSize = 100;

    mutable QHash<int, FilterKey> m_filterKeys;
    mutable bool m_filterIndexBuilt = false;
};
