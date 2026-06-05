#pragma once

#include <QPushButton>
#include <QColor>
#include <QElapsedTimer>
#include <QTimer>

// Toggle button that paints a shrinking green progress bar when a countdown
// is active. Fully green = full time remaining; fully gray = inactive/expired.
class DskTimerButton : public QPushButton {
    Q_OBJECT

public:
    explicit DskTimerButton(const QString &text, QWidget *parent = nullptr);

    // Call when state changes.
    // autoDurationMs == 0  →  no countdown, button stays solid green.
    // autoDurationMs  > 0  →  starts countdown animation from full to empty.
    void setActive(bool active, int autoDurationMs = 0);

    // Grid mode: text is centered and font is smaller.
    void setGridMode(bool grid);

    // Set the button color used in the INACTIVE state (green is always used for active/live).
    // Pass an invalid QColor to reset to the default dark gray.
    void setButtonColor(const QColor &color);

    // True while any DskTimerButton has an active QDrag::exec() in flight.
    // DskDock::refresh() checks this to avoid deleting widgets mid-drag.
    static bool isDragActive() { return s_dragActive; }

Q_SIGNALS:
    void dragEnded(); // emitted when QDrag::exec() returns

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    float progress() const;

    static bool   s_dragActive;  // shared across all buttons — only one drag at a time

    bool          m_active      = false;
    bool          m_gridMode    = false;
    bool          m_dragging    = false;
    int           m_totalMs     = 0;
    QColor        m_buttonColor = QColor(0x2c, 0x2c, 0x2c); // inactive color (default dark gray)
    QPoint        m_pressPos;
    QElapsedTimer m_elapsed;
    QTimer       *m_ticker      = nullptr;
};
