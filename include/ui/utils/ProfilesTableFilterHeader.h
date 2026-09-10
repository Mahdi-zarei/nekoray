#pragma once

#include <array>

#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QVector>
#include <QScrollBar>
#include <QToolButton>

#include "include/ui/utils/ProfilesTableModel.h"

class ProfilesTableFilterHeader : public QHeaderView {
    Q_OBJECT
public:
    explicit ProfilesTableFilterHeader(QWidget *parent = nullptr)
        : QHeaderView(Qt::Horizontal, parent) {
        setSectionsClickable(true);
        setSortIndicatorShown(true);
        setDefaultAlignment(Qt::AlignHCenter | Qt::AlignTop);

        type_filter = new QLineEdit(this->viewport()); 
        type_filter->setPlaceholderText(tr("Filter..."));
        type_filter->setClearButtonEnabled(true);
        connect(type_filter, &QLineEdit::textChanged, [this](const QString &text) {
            emit typeFilterChanged(text);
        });

        address_filter = new QLineEdit(this->viewport());
        address_filter->setPlaceholderText(tr("Filter..."));
        address_filter->setClearButtonEnabled(true);
        connect(address_filter, &QLineEdit::textChanged, [this](const QString &text) {
            emit addressFilterChanged(text);
        });

        name_filter = new QLineEdit(this->viewport()); 
        name_filter->setPlaceholderText(tr("Filter..."));
        name_filter->setClearButtonEnabled(true);
        connect(name_filter, &QLineEdit::textChanged, [this](const QString &text) {
            emit nameFilterChanged(text);
        });

        test_filter = new QLineEdit(this->viewport()); 
        test_filter->setPlaceholderText(tr("Filter by country..."));
        test_filter->setClearButtonEnabled(true);
        connect(test_filter, &QLineEdit::textChanged, [this](const QString &text) {
            emit testFilterChanged(text);
        });

        for (QLineEdit *edit : filterEdits()) edit->installEventFilter(this);

        connect(this, &QHeaderView::sectionResized, this, &ProfilesTableFilterHeader::adjustPositions);

        setFiltersVisible(false);
    }

    void setLastFilterColumn(int column) {
        m_lastFilterColumn = editForColumn(column) ? column : ProfilesTableModel::ColName;
    }

    bool filtersVisible() const { return m_filtersVisible; }

    void focusLastFilterField() {
        if (!m_filtersVisible) return;
        QLineEdit *edit = editForColumn(m_lastFilterColumn);
        // Tab/Backtab/Shortcut focus reasons make QLineEdit select all; OtherFocusReason does not.
        edit->setFocus(Qt::OtherFocusReason);
        edit->end(false);
    }

    QSize sizeHint() const override {
        QSize s = QHeaderView::sizeHint();
        if (m_filtersVisible) {
            s.setHeight(s.height() + 32);
        }
        return s;
    }

protected:
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

        if (event->type() == QEvent::KeyPress) {
            switch (static_cast<QKeyEvent*>(event)->key()) {
            case Qt::Key_Escape:
                emit closeRequested();
                return true;
            case Qt::Key_Down:
                emit focusTableRequested(true);
                return true;
            default:
                break;
            }
        }
        return QHeaderView::eventFilter(obj, event);
    }

public slots:
    void setFiltersVisible(bool visible) {
        m_filtersVisible = visible;

        QLineEdit *focused = focusedEdit();
        if (!visible) {
            if (focused) {
                m_lastFilterColumn = columnOf(focused);
                emit lastFilterColumnChanged(m_lastFilterColumn);
            }
            for (QLineEdit *edit : filterEdits()) edit->clear();
        }

        if (auto btn = qobject_cast<QToolButton*>(sender())) {
            btn->setToolTip(QString("%1\n%2").arg(visible ? tr("Disable Filter") : tr("Enable Filter"), QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText)));
        }

        for (QLineEdit *edit : filterEdits()) edit->setVisible(visible);

        emit geometriesChanged();

        if (visible) focusLastFilterField();
        else if (focused) emit focusTableRequested(false);
    }

    void adjustPositions() {
        if (!m_filtersVisible || !address_filter || !name_filter || !type_filter
            || !test_filter || count() < ProfilesTableModel::ColumnCount) {
	        return;
	    }

        const int editHeight = 24;
        const int topPos = height() - editHeight - 4;

        auto place = [&](QLineEdit *edit, int section) {
            edit->setGeometry(sectionViewportPosition(section) + 2, topPos, sectionSize(section) - 4, editHeight);
        };
        place(type_filter, ProfilesTableModel::ColType);
        place(address_filter, ProfilesTableModel::ColAddress);
        place(name_filter, ProfilesTableModel::ColName);
        place(test_filter, ProfilesTableModel::ColTestResult);
    }

signals:
    void typeFilterChanged(const QString &text);
    void addressFilterChanged(const QString &text);
    void nameFilterChanged(const QString &text);
    void testFilterChanged(const QString &text);
    void focusTableRequested(bool selectFirst);
    void lastFilterColumnChanged(int column);
    // The checkable toolbutton owns the visible state, so it has to be the one to untoggle us.
    void closeRequested();

private:
    std::array<QLineEdit*, 4> filterEdits() const {
        return {type_filter, address_filter, name_filter, test_filter};
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

    QLineEdit *focusedEdit() const {
        for (QLineEdit *edit : filterEdits()) {
            if (edit->hasFocus()) return edit;
        }
        return nullptr;
    }

    QLineEdit *editForColumn(int column) const {
        switch (column) {
        case ProfilesTableModel::ColType:       return type_filter;
        case ProfilesTableModel::ColAddress:    return address_filter;
        case ProfilesTableModel::ColName:       return name_filter;
        case ProfilesTableModel::ColTestResult: return test_filter;
        default:                                return nullptr;
        }
    }

    int columnOf(QLineEdit *edit) const {
        if (edit == type_filter)    return ProfilesTableModel::ColType;
        if (edit == address_filter) return ProfilesTableModel::ColAddress;
        if (edit == name_filter)    return ProfilesTableModel::ColName;
        if (edit == test_filter)    return ProfilesTableModel::ColTestResult;
        return -1;
    }

    QLineEdit* type_filter;
    QLineEdit* address_filter;
    QLineEdit* name_filter;
    QLineEdit* test_filter;
    bool m_filtersVisible = false;
    int m_lastFilterColumn = ProfilesTableModel::ColName;
};
