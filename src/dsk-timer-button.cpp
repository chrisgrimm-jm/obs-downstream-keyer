#include "dsk-timer-button.hpp"

#include <QPainter>
#include <QPainterPath>

DskTimerButton::DskTimerButton(const QString &text, QWidget *parent)
    : QPushButton(text, parent)
{
    setAttribute(Qt::WA_Hover);

    m_ticker = new QTimer(this);
    m_ticker->setInterval(50);
    connect(m_ticker, &QTimer::timeout, this, QOverload<>::of(&DskTimerButton::update));
}

void DskTimerButton::setActive(bool active, int autoDurationMs)
{
    m_active  = active;
    m_totalMs = autoDurationMs;

    if (m_ticker->isActive())
        m_ticker->stop();

    if (active && autoDurationMs > 0) {
        m_elapsed.start();
        m_ticker->start();
    }
    update();
}

void DskTimerButton::setGridMode(bool grid)
{
    m_gridMode = grid;
    update();
}

float DskTimerButton::progress() const
{
    if (!m_active)   return 0.0f;
    if (m_totalMs <= 0) return 1.0f;
    float p = 1.0f - (float)m_elapsed.elapsed() / (float)m_totalMs;
    return p < 0.0f ? 0.0f : p;
}

void DskTimerButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    bool  hovered = underMouse();
    bool  pressed = isDown();
    float prog    = progress();

    QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);

    QColor green = pressed ? QColor("#1e8449")
                 : hovered ? QColor("#2ecc71")
                           : QColor("#27ae60");
    QColor gray  = pressed ? QColor("#222")
                 : hovered ? QColor("#3a3a3a")
                           : QColor("#2c2c2c");

    // Background fill
    p.setPen(Qt::NoPen);
    p.setBrush(m_active ? green : gray);
    p.drawRoundedRect(r, 4, 4);

    // Depleted (right) portion when counting down
    if (m_active && m_totalMs > 0 && prog < 1.0f) {
        QPainterPath clip;
        clip.addRoundedRect(r, 4, 4);
        p.setClipPath(clip);

        float grayW = r.width() * (1.0f - prog);
        p.setBrush(gray);
        p.drawRect(QRectF(r.left(), r.top(), grayW, r.height()));
        p.setClipping(false);
    }

    // Border
    p.setPen(QPen(m_active ? QColor("#1e8449") : QColor("#444"), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 4, 4);

    // Label
    QFont f = font();
    f.setPointSize(m_gridMode ? 10 : 12);
    f.setBold(m_active);
    p.setFont(f);
    p.setPen(m_active ? QColor("#fff") : QColor("#aaa"));
    int textFlags = m_gridMode ? Qt::AlignCenter
                               : (Qt::AlignVCenter | Qt::AlignLeft);
    int pad = m_gridMode ? 4 : 8;
    p.drawText(rect().adjusted(pad, 0, -pad, 0), textFlags, text());
}
