#pragma once

#include <QComboBox>
#include <QWheelEvent>

class NoWheelComboBox : public QComboBox {
public:
    explicit NoWheelComboBox(QWidget *parent = nullptr) : QComboBox(parent) {
        // Only strip the wheel bit: macOS deliberately gives non-editable combos TabFocus.
        if (focusPolicy() == Qt::WheelFocus) setFocusPolicy(Qt::StrongFocus);
    }

protected:
    // Ignoring rather than accepting is what lets QApplication walk the wheel up to the scroll area.
    void wheelEvent(QWheelEvent *event) override { event->ignore(); }
};
