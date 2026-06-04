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

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(50, 20); }
    bool  hasHeightForWidth() const override { return true; }
    int   heightForWidth(int w) const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;

private:
    float progress() const;

    bool          m_active      = false;
    bool          m_gridMode    = false;
    bool          m_dragging    = false;
    int           m_totalMs     = 0;
    QColor        m_buttonColor = QColor(0x2c, 0x2c, 0x2c); // inactive color (default dark gray)
    QPoint        m_pressPos;
    QElapsedTimer m_elapsed;
    QTimer       *m_ticker      = nullptr;
};
