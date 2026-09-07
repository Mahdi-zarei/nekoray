#pragma once

#include <QScrollArea>

// QScrollArea::sizeHint() caps at QSize(36 * h, 24 * h) font heights even with AdjustToContents (that policy
// only reaches QAbstractScrollArea::sizeHint(), which QScrollArea overrides), so the other scroll areas in the
// profile dialog only look right because their content stays small. Report the content's own hint instead;
// the small minimumSizeHint still lets the window shrink below it and scroll.
class ContentSizedScrollArea : public QScrollArea {
public:
    explicit ContentSizedScrollArea(QWidget *parent = nullptr) : QScrollArea(parent) {
    }

    QSize sizeHint() const override {
        if (widget() == nullptr) return QScrollArea::sizeHint();
        const int frame = 2 * frameWidth();
        return widget()->sizeHint() + QSize(frame, frame);
    }
};
