#include "include/ui/utils/ProfilesTableVerticalHeader.h"
#include "include/ui/utils/ProfilesFilterProxyModel.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include <QPainter>
#include <QTableView>
#include <QFontMetrics>

ProfilesTableVerticalHeader::ProfilesTableVerticalHeader(QWidget *parent)
    : QHeaderView(Qt::Vertical, parent) {
    setSectionsClickable(true);
}

void ProfilesTableVerticalHeader::setProfilesModel(ProfilesTableModel *model, ProfilesFilterProxyModel *proxy) {
    if (m_model == model && m_proxy == proxy) return;
    if (m_model) disconnect(m_model, nullptr, this, nullptr);
    if (m_proxy) disconnect(m_proxy, nullptr, this, nullptr);
    m_model = model;
    m_proxy = proxy;
    if (m_model) {
        connect(m_model, &ProfilesTableModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
            for (int r = topLeft.row(); r <= bottomRight.row(); ++r) {
                const int section = m_proxy ? m_proxy->toProxyRow(r) : r;
                if (section >= 0) updateSection(section);
            }
        });
        connect(m_model, &ProfilesTableModel::modelReset, this, [this]() {
            updateWidthFromRowCount();
            update();
        });
    }
    if (m_proxy) {
        connect(m_proxy, &QAbstractItemModel::rowsInserted, this, [this] { update(); });
        connect(m_proxy, &QAbstractItemModel::rowsRemoved, this, [this] { update(); });
    }
    updateWidthFromRowCount();
    update();
}

void ProfilesTableVerticalHeader::updateWidthFromRowCount() {
    const QFontMetrics fm(font());
    int rows = m_model ? m_model->rowCount() : 0;
    QString maxNum = (rows <= 0) ? QStringLiteral("1  ") : (QString::number(rows) + QStringLiteral("  "));
    int wNum = fm.horizontalAdvance(maxNum);
    int wCheck = fm.horizontalAdvance(QStringLiteral("✓"));
    int w = qMax(wNum, wCheck) + 8;
    setMinimumWidth(w);
    setMaximumWidth(w);
}

void ProfilesTableVerticalHeader::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const {
    painter->save();
    painter->fillRect(rect, palette().color(QPalette::Button));
    if (rect.width() > 1) {
        painter->setPen(palette().color(QPalette::Mid));
        painter->drawLine(rect.topRight(), rect.bottomRight());
    }
    QString text;
    if (m_model) {
        const int sourceRow = m_proxy ? m_proxy->toSourceRow(logicalIndex) : logicalIndex;
        text = m_model->rowLabel(sourceRow, logicalIndex);
    } else {
        text = QString::number(logicalIndex + 1) + QStringLiteral("  ");
    }
    painter->setPen(palette().color(foregroundRole()));
    painter->drawText(rect, Qt::AlignCenter, text);
    painter->restore();
}
