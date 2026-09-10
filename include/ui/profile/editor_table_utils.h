#pragma once

#include <QComboBox>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>

#include "include/ui/utils/NoWheelComboBox.h"

// Inline rather than file-local: a unity build puts both editors in one TU.

inline QString EditorNumText(qint64 value) {
    return value == 0 ? QString() : QString::number(value);
}

inline QString EditorCellText(const QTableWidget *table, int row, int column) {
    auto item = table->item(row, column);
    return item == nullptr ? QString() : item->text().trimmed();
}

inline QString EditorComboCellText(const QTableWidget *table, int row, int column) {
    auto combo = qobject_cast<QComboBox *>(table->cellWidget(row, column));
    return combo == nullptr ? QString() : combo->currentText();
}

inline void EditorSetComboCell(QTableWidget *table, int row, int column, const QStringList &items,
                               const QString &current) {
    auto combo = new NoWheelComboBox(table);
    combo->addItems(items);
    combo->setCurrentText(current);
    table->setCellWidget(row, column, combo);
}
