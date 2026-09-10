#pragma once

#include <QTextDocument>
#include <QTextEdit>

// QTextEdit inherits QAbstractScrollArea's invented 256x192 size hint, unrelated to its content.
class MyTextEdit : public QTextEdit {
public:
    explicit MyTextEdit(QWidget *parent = nullptr) : QTextEdit(parent) {
    }

    int visibleLines() const {
        return m_visibleLines;
    }

    void setVisibleLines(int lines) {
        m_visibleLines = lines;
        updateGeometry();
    }

    QSize sizeHint() const override {
        const QSize base = QTextEdit::sizeHint();
        if (m_visibleLines <= 0) return base;
        const QMargins m = contentsMargins();
        const int h = fontMetrics().lineSpacing() * m_visibleLines
                      + static_cast<int>(document()->documentMargin()) * 2
                      + m.top() + m.bottom()
                      + frameWidth() * 2;
        return {base.width(), h};
    }

private:
    int m_visibleLines = 5;
};
