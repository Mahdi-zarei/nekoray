#include "include/ui/widget/json/JsonCodeEdit.h"

#include <QFontDatabase>
#include <QHelpEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QPainter>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextEdit>
#include <QTimer>
#include <QToolTip>

#include "include/ui/widget/json/JsonTree.h"

namespace JsonEdit {
    namespace {
        constexpr int kDebounceMs = 200;
        constexpr int kMarkerWidth = 14;

        class Highlighter final : public QSyntaxHighlighter {
        public:
            explicit Highlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}

            void setDark(const bool dark) {
                m_key.setForeground(dark ? QColor(0x66, 0xd9, 0xe8) : QColor(0x0b, 0x72, 0x85));
                m_string.setForeground(dark ? QColor(0x8c, 0xe9, 0x9a) : QColor(0x2b, 0x8a, 0x3e));
                m_number.setForeground(dark ? QColor(0xff, 0xc0, 0x78) : QColor(0xe8, 0x59, 0x0c));
                m_literal.setForeground(dark ? QColor(0xb1, 0x97, 0xfc) : QColor(0x70, 0x48, 0xe8));
                m_comment.setForeground(dark ? QColor(0x86, 0x8e, 0x96) : QColor(0x86, 0x8e, 0x96));
                m_comment.setFontItalic(true);
                rehighlight();
            }

        protected:
            void highlightBlock(const QString& text) override {
                int i = 0;
                if (previousBlockState() == 1) {
                    const int end = text.indexOf(QStringLiteral("*/"));
                    if (end < 0) {
                        setFormat(0, static_cast<int>(text.size()), m_comment);
                        setCurrentBlockState(1);
                        return;
                    }
                    setFormat(0, end + 2, m_comment);
                    i = end + 2;
                }
                setCurrentBlockState(0);

                while (i < text.size()) {
                    const QChar c = text.at(i);
                    if (c == QLatin1Char('/') && i + 1 < text.size()) {
                        if (text.at(i + 1) == QLatin1Char('/')) {
                            setFormat(i, static_cast<int>(text.size()) - i, m_comment);
                            return;
                        }
                        if (text.at(i + 1) == QLatin1Char('*')) {
                            const int end = text.indexOf(QStringLiteral("*/"), i + 2);
                            if (end < 0) {
                                setFormat(i, static_cast<int>(text.size()) - i, m_comment);
                                setCurrentBlockState(1);
                                return;
                            }
                            setFormat(i, end + 2 - i, m_comment);
                            i = end + 2;
                            continue;
                        }
                    }
                    if (c == QLatin1Char('"')) {
                        const int start = i;
                        i++;
                        while (i < text.size()) {
                            if (text.at(i) == QLatin1Char('\\')) {
                                i += 2;
                                continue;
                            }
                            if (text.at(i) == QLatin1Char('"')) break;
                            i++;
                        }
                        i = qMin(i + 1, static_cast<int>(text.size()));
                        int lookahead = i;
                        while (lookahead < text.size() && text.at(lookahead).isSpace()) lookahead++;
                        const bool isKey = lookahead < text.size() && text.at(lookahead) == QLatin1Char(':');
                        setFormat(start, i - start, isKey ? m_key : m_string);
                        continue;
                    }
                    if (c.isDigit() || (c == QLatin1Char('-') && i + 1 < text.size() && text.at(i + 1).isDigit())) {
                        const int start = i;
                        i++;
                        while (i < text.size() && (text.at(i).isDigit() || text.at(i) == QLatin1Char('.') ||
                                                   text.at(i) == QLatin1Char('e') || text.at(i) == QLatin1Char('E') ||
                                                   text.at(i) == QLatin1Char('+') || text.at(i) == QLatin1Char('-'))) {
                            i++;
                        }
                        setFormat(start, i - start, m_number);
                        continue;
                    }
                    if (c.isLetter()) {
                        const int start = i;
                        while (i < text.size() && text.at(i).isLetter()) i++;
                        const QString word = text.mid(start, i - start);
                        if (word == QStringLiteral("true") || word == QStringLiteral("false") ||
                            word == QStringLiteral("null")) {
                            setFormat(start, i - start, m_literal);
                        }
                        continue;
                    }
                    i++;
                }
            }

        private:
            QTextCharFormat m_key;
            QTextCharFormat m_string;
            QTextCharFormat m_number;
            QTextCharFormat m_literal;
            QTextCharFormat m_comment;
        };

        class Gutter final : public QWidget {
        public:
            explicit Gutter(JsonCodeEdit* editor) : QWidget(editor), m_editor(editor) {}

            [[nodiscard]] QSize sizeHint() const override { return {m_editor->gutterWidth(), 0}; }

        protected:
            void paintEvent(QPaintEvent* event) override { m_editor->paintGutter(event); }

        private:
            JsonCodeEdit* m_editor;
        };
    }

    JsonCodeEdit::JsonCodeEdit(QWidget* parent) : QPlainTextEdit(parent) {
        setLineWrapMode(NoWrap);
        auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(QFontInfo(QPlainTextEdit::font()).pointSize());
        setFont(font);
        setTabStopDistance(4 * QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')));

        m_gutter = new Gutter(this);
        m_highlighter = new Highlighter(document());

        m_debounce = new QTimer(this);
        m_debounce->setSingleShot(true);
        m_debounce->setInterval(kDebounceMs);
        connect(m_debounce, &QTimer::timeout, this, &JsonCodeEdit::revalidate);

        connect(this, &QPlainTextEdit::blockCountChanged, this, [this] {
            setViewportMargins(gutterWidth(), 0, 0, 0);
        });
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, const int dy) {
            if (dy != 0) {
                m_gutter->scroll(0, dy);
            } else {
                m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
            }
            if (rect.contains(viewport()->rect())) setViewportMargins(gutterWidth(), 0, 0, 0);
        });
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, &JsonCodeEdit::refreshDecorations);
        connect(this, &QPlainTextEdit::textChanged, this, [this] { m_debounce->start(); });

        setViewportMargins(gutterWidth(), 0, 0, 0);
        applyTheme();
    }

    void JsonCodeEdit::setValidator(std::shared_ptr<Validator> validator) {
        m_validator = std::move(validator);
        revalidate();
    }

    bool JsonCodeEdit::hasErrors() const {
        for (const auto& issue : m_issues) {
            if (issue.severity == Severity::Error) return true;
        }
        return false;
    }

    QString JsonCodeEdit::statusText() const {
        if (toPlainText().trimmed().isEmpty()) return QObject::tr("Empty");
        int errors = 0;
        for (const auto& issue : m_issues) {
            if (issue.severity == Severity::Error) errors++;
        }
        if (errors > 0) return QObject::tr("%n problem(s)", "", errors);
        if (!m_issues.isEmpty()) return QObject::tr("%n warning(s)", "", static_cast<int>(m_issues.size()));
        return QObject::tr("Valid JSON");
    }

    bool JsonCodeEdit::formatDocument() {
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(toPlainText().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) return false;
        setPlainText(QString::fromUtf8(document.toJson(QJsonDocument::Indented)));
        return true;
    }

    void JsonCodeEdit::goToOffset(const int offset) {
        QTextCursor cursor = textCursor();
        cursor.setPosition(qBound(0, offset, static_cast<int>(document()->characterCount()) - 1));
        setTextCursor(cursor);
        centerCursor();
        setFocus();
    }

    int JsonCodeEdit::gutterWidth() const {
        int digits = 2;
        for (int lines = qMax(1, blockCount()); lines >= 100; lines /= 10) digits++;
        return kMarkerWidth + 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void JsonCodeEdit::paintGutter(QPaintEvent* event) {
        QPainter painter(m_gutter);
        painter.fillRect(event->rect(), palette().alternateBase());

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
        qreal bottom = top + blockBoundingRect(block).height();

        const QColor numberColor = palette().color(QPalette::Disabled, QPalette::WindowText);
        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                const int height = fontMetrics().height();
                if (m_errorBlocks.contains(blockNumber) || m_warningBlocks.contains(blockNumber)) {
                    const bool error = m_errorBlocks.contains(blockNumber);
                    painter.setRenderHint(QPainter::Antialiasing, true);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(error ? QColor(0xc6, 0x28, 0x28) : QColor(0xf0, 0x8c, 0x00));
                    painter.drawEllipse(QPointF(kMarkerWidth / 2.0, top + height / 2.0), 3.5, 3.5);
                    painter.setRenderHint(QPainter::Antialiasing, false);
                }
                painter.setPen(numberColor);
                painter.drawText(0, static_cast<int>(top), m_gutter->width() - 4, height,
                                 Qt::AlignRight | Qt::AlignVCenter, QString::number(blockNumber + 1));
            }
            block = block.next();
            top = bottom;
            bottom = top + blockBoundingRect(block).height();
            blockNumber++;
        }
    }

    void JsonCodeEdit::keyPressEvent(QKeyEvent* event) {
        if (handleAutoEdit(event)) return;
        QPlainTextEdit::keyPressEvent(event);
    }

    void JsonCodeEdit::resizeEvent(QResizeEvent* event) {
        QPlainTextEdit::resizeEvent(event);
        const QRect area = contentsRect();
        m_gutter->setGeometry(QRect(area.left(), area.top(), gutterWidth(), area.height()));
    }

    void JsonCodeEdit::changeEvent(QEvent* event) {
        QPlainTextEdit::changeEvent(event);
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) applyTheme();
    }

    bool JsonCodeEdit::event(QEvent* event) {
        if (event->type() != QEvent::ToolTip) return QPlainTextEdit::event(event);

        const auto* help = static_cast<QHelpEvent*>(event);
        const QTextCursor cursor = cursorForPosition(viewport()->mapFrom(this, help->pos()));
        const int position = cursor.position();
        QStringList messages;
        for (const auto& issue : m_issues) {
            const int start = issue.span.offset;
            const int end = start + qMax(1, issue.span.length);
            if (position >= start && position <= end) messages << issue.message;
        }
        if (messages.isEmpty()) {
            QToolTip::hideText();
        } else {
            QToolTip::showText(help->globalPos(), messages.join(QStringLiteral("\n")), this);
        }
        return true;
    }

    void JsonCodeEdit::revalidate() {
        m_issues.clear();

        const QString text = toPlainText();
        if (!text.trimmed().isEmpty()) {
            const ParseResult parsed = Parse(text);
            if (!parsed.ok) {
                m_issues.append({Severity::Error, parsed.error, {}, parsed.errorSpan});
            } else if (m_validator != nullptr) {
                m_issues = m_validator->Validate(parsed.root);
            }
        }

        refreshDecorations();
        emit issuesChanged();
    }

    void JsonCodeEdit::refreshDecorations() {
        QList<QTextEdit::ExtraSelection> selections;

        QTextEdit::ExtraSelection currentLine;
        currentLine.format.setBackground(palette().alternateBase());
        currentLine.format.setProperty(QTextFormat::FullWidthSelection, true);
        currentLine.cursor = textCursor();
        currentLine.cursor.clearSelection();
        selections.append(currentLine);

        m_errorBlocks.clear();
        m_warningBlocks.clear();
        const int end = static_cast<int>(document()->characterCount()) - 1;
        for (const auto& issue : m_issues) {
            const int start = qBound(0, issue.span.offset, end);
            const int stop = qBound(start, start + qMax(1, issue.span.length), end);

            QTextCursor cursor(document());
            cursor.setPosition(start);
            cursor.setPosition(stop, QTextCursor::KeepAnchor);
            if (issue.severity == Severity::Error) {
                m_errorBlocks.insert(cursor.blockNumber());
            } else {
                m_warningBlocks.insert(cursor.blockNumber());
            }

            if (cursor.hasSelection()) {
                QTextEdit::ExtraSelection selection;
                selection.cursor = cursor;
                selection.format.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
                selection.format.setUnderlineColor(issue.severity == Severity::Error ? QColor(0xc6, 0x28, 0x28)
                                                                                     : QColor(0xf0, 0x8c, 0x00));
                selections.append(selection);
            }
        }

        setExtraSelections(selections);
        m_gutter->update();
    }

    void JsonCodeEdit::applyTheme() {
        static_cast<Highlighter*>(m_highlighter)->setDark(palette().base().color().lightness() < 128);
    }

    QChar JsonCodeEdit::charBeforeCursor() const {
        const QTextCursor cursor = textCursor();
        if (cursor.positionInBlock() == 0) return {};
        return cursor.block().text().at(cursor.positionInBlock() - 1);
    }

    QChar JsonCodeEdit::charAfterCursor() const {
        const QTextCursor cursor = textCursor();
        const QString text = cursor.block().text();
        if (cursor.positionInBlock() >= text.length()) return {};
        return text.at(cursor.positionInBlock());
    }

    bool JsonCodeEdit::handleAutoEdit(QKeyEvent* event) {
        const int key = event->key();
        const Qt::KeyboardModifiers modifiers = event->modifiers();
        const bool plainOrShift = (modifiers & ~Qt::ShiftModifier) == 0;

        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && plainOrShift) {
            QTextCursor cursor = textCursor();
            if (cursor.hasSelection()) return false;
            QString indent;
            for (const QChar c : cursor.block().text()) {
                if (c == QLatin1Char(' ') || c == QLatin1Char('\t')) indent += c;
                else break;
            }
            const QChar before = charBeforeCursor();
            const QChar after = charAfterCursor();
            const bool pair = (before == QLatin1Char('{') && after == QLatin1Char('}')) ||
                              (before == QLatin1Char('[') && after == QLatin1Char(']'));
            const bool opens = before == QLatin1Char('{') || before == QLatin1Char('[');
            cursor.beginEditBlock();
            if (pair) {
                cursor.insertText(QStringLiteral("\n") + indent + QStringLiteral("  ") + QStringLiteral("\n") + indent);
                cursor.movePosition(QTextCursor::Up);
                cursor.movePosition(QTextCursor::EndOfBlock);
            } else if (opens) {
                cursor.insertText(QStringLiteral("\n") + indent + QStringLiteral("  "));
            } else {
                cursor.insertText(QStringLiteral("\n") + indent);
            }
            cursor.endEditBlock();
            setTextCursor(cursor);
            return true;
        }

        if (key == Qt::Key_Backspace && modifiers == Qt::NoModifier) {
            const QChar before = charBeforeCursor();
            const QChar after = charAfterCursor();
            if ((before == QLatin1Char('{') && after == QLatin1Char('}')) ||
                (before == QLatin1Char('[') && after == QLatin1Char(']')) ||
                (before == QLatin1Char('"') && after == QLatin1Char('"'))) {
                QTextCursor cursor = textCursor();
                cursor.deletePreviousChar();
                cursor.deleteChar();
                setTextCursor(cursor);
                return true;
            }
            return false;
        }

        const QString typedText = event->text();
        if (typedText.isEmpty()) return false;
        const QChar typed = typedText.at(0);

        if ((typed == QLatin1Char('}') || typed == QLatin1Char(']') || typed == QLatin1Char('"')) &&
            charAfterCursor() == typed) {
            QTextCursor cursor = textCursor();
            cursor.movePosition(QTextCursor::Right);
            setTextCursor(cursor);
            return true;
        }

        if (typed == QLatin1Char('{') || typed == QLatin1Char('[') || typed == QLatin1Char('"')) {
            if (textCursor().hasSelection()) return false;
            if (typed == QLatin1Char('"') && charAfterCursor().isLetterOrNumber()) return false;
            const QChar close = typed == QLatin1Char('{')
                                    ? QLatin1Char('}')
                                    : (typed == QLatin1Char('[') ? QLatin1Char(']') : QLatin1Char('"'));
            QTextCursor cursor = textCursor();
            cursor.insertText(QString(typed) + close);
            cursor.movePosition(QTextCursor::Left);
            setTextCursor(cursor);
            return true;
        }

        return false;
    }
}
