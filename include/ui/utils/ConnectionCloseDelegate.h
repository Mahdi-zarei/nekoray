#pragma once

#include <QIcon>
#include <QStyledItemDelegate>

class ConnectionCloseDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    static constexpr int ColumnWidth = 26;
    static constexpr int IconSize = 12;

    explicit ConnectionCloseDelegate(QObject *parent = nullptr);

    void setIcon(const QIcon &icon);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

signals:
    void closeRequested(const QString &id);

protected:
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;

private:
    QIcon m_icon;
};
