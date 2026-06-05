#include "dsk-timer-button.hpp"

#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

bool DskTimerButton::s_dragActive = false;

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

void DskTimerButton::setButtonColor(const QColor &color)
{
    m_buttonColor = color.isValid() ? color : QColor(0x2c, 0x2c, 0x2c);
    update();
}

void DskTimerButton::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_pressPos = e->pos();
        m_dragging = false;
    }
    QPushButton::mousePressEvent(e);
}

void DskTimerButton::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging && (e->buttons() & Qt::LeftButton) &&
        (e->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        m_dragging = true;

        // Release pressed state before handing off to drag system
        setDown(false);

        auto *drag = new QDrag(this);
        auto *mime = new QMimeData();
        mime->setData("application/x-dsk-source", text().toUtf8());
        drag->setMimeData(mime);

        // Render button as semi-transparent drag ghost
        QPixmap pix(size());
        pix.fill(Qt::transparent);
        render(&pix);
        QPixmap ghost(size());
        ghost.fill(Qt::transparent);
        QPainter gp(&ghost);
        gp.setOpacity(0.65);
        gp.drawPixmap(0, 0, pix);
        gp.end();
        drag->setPixmap(ghost);
        drag->setHotSpot(m_pressPos);

        s_dragActive = true;
        drag->exec(Qt::MoveAction);
        s_dragActive = false;
        m_dragging = false;
        emit dragEnded(); // lets DskDock run any refresh it deferred during the drag
        return;
    }
    QPushButton::mouseMoveEvent(e);
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

    // Active = always green (on-air indicator). Inactive = user's chosen color.
    QColor green = pressed ? QColor(0x1e, 0x84, 0x49)
                 : hovered ? QColor(0x2e, 0xcc, 0x71)
                           : QColor(0x27, 0xae, 0x60);
    QColor inact = pressed ? m_buttonColor.darker(115)
                 : hovered ? m_buttonColor.lighter(115)
                           : m_buttonColor;

    // Background fill
    p.setPen(Qt::NoPen);
    p.setBrush(m_active ? green : inact);
    p.drawRoundedRect(r, 4, 4);

    // Timer bar: inactive color grows from the left as countdown depletes
    if (m_active && m_totalMs > 0 && prog < 1.0f) {
        QPainterPath clip;
        clip.addRoundedRect(r, 4, 4);
        p.setClipPath(clip);

        float inactW = r.width() * (1.0f - prog);
        p.setBrush(inact);
        p.drawRect(QRectF(r.left(), r.top(), inactW, r.height()));
        p.setClipping(false);
    }

    // Border: dark green when live, subtle dark when inactive
    p.setPen(QPen(m_active ? QColor(0x1e, 0x84, 0x49) : QColor("#444"), 2));
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
