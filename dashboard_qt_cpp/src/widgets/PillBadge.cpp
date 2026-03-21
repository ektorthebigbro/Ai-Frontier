#include "PillBadge.h"
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QFont>

PillBadge::PillBadge(const QString& text, const QColor& color, QWidget* parent)
    : QLabel(text, parent), m_color(color)
{
    setFixedHeight(24);
    setContentsMargins(12, 0, 12, 0);
    setAlignment(Qt::AlignCenter);

    QFont f = font();
    f.setPixelSize(11);
    f.setWeight(QFont::DemiBold);
    setFont(f);

    // Make background transparent so paintEvent handles it
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet("background: transparent;");
}

void PillBadge::setColor(const QColor& color)
{
    m_color = color;
    update();
}

void PillBadge::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const int radius = 12;

    // Fill with color at alpha 40
    {
        QColor fillColor = m_color;
        fillColor.setAlpha(40);
        QPainterPath path;
        path.addRoundedRect(r, radius, radius);
        p.fillPath(path, fillColor);
    }

    // 1px border with color at alpha 120
    {
        QColor borderColor = m_color;
        borderColor.setAlpha(120);
        p.setPen(QPen(borderColor, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, radius, radius);
    }

    // Text centered in full-opacity color
    {
        QFont f = font();
        f.setPixelSize(11);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(m_color);
        p.drawText(rect(), Qt::AlignCenter, text());
    }
}
