#include "include/ui/widget/json/JsonIssueList.h"

#include <QStyle>

#include "include/ui/widget/json/JsonCodeEdit.h"
#include "include/ui/widget/json/JsonTree.h"

namespace JsonEdit {
    JsonIssueList::JsonIssueList(QWidget* parent) : QListWidget(parent) {
        setAlternatingRowColors(true);
        setSelectionMode(SingleSelection);
        setMaximumHeight(110);
        setVisible(false);
        connect(this, &QListWidget::itemClicked, this, [this](const QListWidgetItem* item) {
            if (m_editor != nullptr && item != nullptr) m_editor->goToOffset(item->data(Qt::UserRole).toInt());
        });
    }

    void JsonIssueList::attach(JsonCodeEdit* editor) {
        m_editor = editor;
        if (m_editor == nullptr) return;
        connect(m_editor, &JsonCodeEdit::issuesChanged, this, &JsonIssueList::refresh);
        refresh();
    }

    void JsonIssueList::refresh() {
        clear();
        if (m_editor == nullptr) {
            setVisible(false);
            return;
        }

        const QString text = m_editor->toPlainText();
        for (const auto& issue : m_editor->issues()) {
            int line = 0, column = 0;
            OffsetToLineColumn(text, issue.span.offset, &line, &column);
            auto* item = new QListWidgetItem(QObject::tr("Line %1: %2").arg(line).arg(issue.message), this);
            item->setData(Qt::UserRole, issue.span.offset);
            item->setIcon(style()->standardIcon(issue.severity == Severity::Error ? QStyle::SP_MessageBoxCritical
                                                                                  : QStyle::SP_MessageBoxWarning));
        }
        setVisible(count() > 0);
    }
}
