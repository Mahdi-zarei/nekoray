#pragma once

#include <array>

#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QToolButton>

#include "include/ui/utils/ConnectionsTableModel.h"

// Filter row over the connections table; traffic/speed hold formatted byte counts, so they get no field.
class ConnectionsFilterHeader : public QHeaderView {
    Q_OBJECT
public:
    struct Filters {
        QString source;
        QString dest;
        QString process;
        QString protocol;
        QString outbound;
    };

    explicit ConnectionsFilterHeader(QWidget *parent = nullptr)
        : QHeaderView(Qt::Horizontal, parent) {
        setSectionsClickable(true);
        setDefaultAlignment(Qt::AlignHCenter | Qt::AlignTop);

        source_filter = makeEdit();
        dest_filter = makeEdit();
        process_filter = makeEdit();
        protocol_filter = makeEdit();
        outbound_filter = makeEdit();

        connect(this, &QHeaderView::sectionResized, this, &ConnectionsFilterHeader::adjustPositions);

        setFiltersVisible(false);
    }

    bool filtersVisible() const { return m_filtersVisible; }

    // Hiding a column must clear its field, or the filter comes back with the column.
    void clearFilterFor(int column) {
        if (QLineEdit *edit = editForColumn(column)) edit->clear();
    }

    Filters filters() const {
        return {textFor(ConnectionsTableModel::ColSource), textFor(ConnectionsTableModel::ColDest),
                textFor(ConnectionsTableModel::ColProcess), textFor(ConnectionsTableModel::ColProtocol),
                textFor(ConnectionsTableModel::ColOutbound)};
    }

    QSize sizeHint() const override {
        QSize s = QHeaderView::sizeHint();
        if (m_filtersVisible) {
            s.setHeight(s.height() + 32);
        }
        return s;
    }

protected:
    // Protocol/Outbound are ResizeToContents, so without a floor their fields shrink to the header label's width.
    QSize sectionSizeFromContents(int logicalIndex) const override {
        QSize s = QHeaderView::sectionSizeFromContents(logicalIndex);
        if (m_filtersVisible && editForColumn(logicalIndex) != nullptr) {
            s.setWidth(qMax(s.width(), 120));
        }
        return s;
    }

    void updateGeometries() override {
        QHeaderView::updateGeometries();
        adjustPositions();
    }

    bool eventFilter(QObject *obj, QEvent *event) override {
        if (!qobject_cast<QLineEdit*>(obj)) return QHeaderView::eventFilter(obj, event);

        // Window shortcuts resolve before the key reaches the field, so bare Return/Del would fire menu actions.
        if (event->type() == QEvent::ShortcutOverride) {
            if (!isTextEditingKey(static_cast<QKeyEvent*>(event))) {
                return QHeaderView::eventFilter(obj, event);
            }
            event->accept();
            return true;
        }

        if (event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            emit closeRequested();
            return true;
        }
        return QHeaderView::eventFilter(obj, event);
    }

public slots:
    void setFiltersVisible(bool visible) {
        m_filtersVisible = visible;

        // Hiding must clear, or the list stays filtered with nothing explaining why.
        if (!visible) {
            for (QLineEdit *edit : filterEdits()) edit->clear();
        }

        if (auto btn = qobject_cast<QToolButton*>(sender())) {
            btn->setToolTip(visible ? tr("Disable Filter") : tr("Enable Filter"));
        }

        for (QLineEdit *edit : filterEdits()) edit->setVisible(visible);

        resizeSections();
        emit geometriesChanged();
        adjustPositions();

        // Tab/Backtab/Shortcut focus reasons make QLineEdit select all; OtherFocusReason does not.
        if (visible) {
            auto *first = isSectionHidden(ConnectionsTableModel::ColSource) ? dest_filter : source_filter;
            first->setFocus(Qt::OtherFocusReason);
        }
    }

    void adjustPositions() {
        if (!m_filtersVisible || count() < ConnectionsTableModel::ColumnCount) return;

        const int editHeight = 24;
        const int topPos = height() - editHeight - 4;

        // A hidden section reports width 0, so placing its edit would give it a negative width over the neighbour.
        auto place = [&](QLineEdit *edit, int section) {
            if (isSectionHidden(section)) {
                edit->hide();
                return;
            }
            edit->show();
            edit->setGeometry(sectionViewportPosition(section) + 2, topPos, sectionSize(section) - 4, editHeight);
        };
        place(source_filter, ConnectionsTableModel::ColSource);
        place(dest_filter, ConnectionsTableModel::ColDest);
        place(process_filter, ConnectionsTableModel::ColProcess);
        place(protocol_filter, ConnectionsTableModel::ColProtocol);
        place(outbound_filter, ConnectionsTableModel::ColOutbound);
    }

signals:
    void filtersChanged();
    // The checkable toolbutton owns the visible state, so it has to be the one to untoggle us.
    void closeRequested();

private:
    QLineEdit *makeEdit() {
        auto *edit = new QLineEdit(this->viewport());
        edit->setPlaceholderText(tr("Filter..."));
        edit->setClearButtonEnabled(true);
        edit->installEventFilter(this);
        connect(edit, &QLineEdit::textChanged, this, [this] { emit filtersChanged(); });
        return edit;
    }

    QLineEdit *editForColumn(int column) const {
        switch (column) {
        case ConnectionsTableModel::ColSource:   return source_filter;
        case ConnectionsTableModel::ColDest:     return dest_filter;
        case ConnectionsTableModel::ColProcess:  return process_filter;
        case ConnectionsTableModel::ColProtocol: return protocol_filter;
        case ConnectionsTableModel::ColOutbound: return outbound_filter;
        default:                                 return nullptr;
        }
    }

    // A hidden column must report no filter, or its stale text would keep filtering the table invisibly.
    QString textFor(int column) const {
        QLineEdit *edit = editForColumn(column);
        if (edit == nullptr || isSectionHidden(column)) return {};
        return edit->text();
    }

    std::array<QLineEdit*, 5> filterEdits() const {
        return {source_filter, dest_filter, process_filter, protocol_filter, outbound_filter};
    }

    static bool isTextEditingKey(QKeyEvent *key) {
        if (!(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            return key->key() < Qt::Key_F1 || key->key() > Qt::Key_F35;
        }
        for (auto standard : {QKeySequence::SelectAll, QKeySequence::Copy, QKeySequence::Cut,
                              QKeySequence::Paste, QKeySequence::Undo, QKeySequence::Redo,
                              QKeySequence::MoveToStartOfLine, QKeySequence::MoveToEndOfLine,
                              QKeySequence::SelectStartOfLine, QKeySequence::SelectEndOfLine,
                              QKeySequence::DeleteStartOfWord, QKeySequence::DeleteEndOfWord}) {
            if (key->matches(standard)) return true;
        }
        return false;
    }

    QLineEdit *source_filter;
    QLineEdit *dest_filter;
    QLineEdit *process_filter;
    QLineEdit *protocol_filter;
    QLineEdit *outbound_filter;
    bool m_filtersVisible = false;
};
