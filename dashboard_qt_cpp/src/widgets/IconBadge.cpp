#include "IconBadge.h"
#include <QPainter>
#include <QPaintEvent>
#include <QRadialGradient>
#include <QFont>

IconBadge::IconBadge(const QString& icon, const QColor& color, int size, QWidget* parent)
    : QWidget(parent), m_icon(icon), m_color(color), m_size(size)
{
    setFixedSize(m_size, m_size);
}

void IconBadge::setIcon(const QString& icon)
{
    m_icon = icon;
    update();
}

void IconBadge::setColor(const QColor& color)
{
    m_color = color;
    update();
}

void IconBadge::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF center(m_size / 2.0, m_size / 2.0);
    const double radius = m_size / 2.0;

    // 1. Filled circle with radial gradient (lighter center, darker edge)
    QRadialGradient gradient(center, radius);
    QColor lighter = m_color.lighter(130);
    gradient.setColorAt(0.0, lighter);
    gradient.setColorAt(1.0, m_color);
    p.setBrush(gradient);
    p.setPen(Qt::NoPen);
    p.drawEllipse(center, radius, radius);

    // 3. Subtle inner shadow (darker ring at edge)
    QRadialGradient shadowGrad(center, radius);
    shadowGrad.setColorAt(0.0, QColor(0, 0, 0, 0));
    shadowGrad.setColorAt(0.85, QColor(0, 0, 0, 0));
    shadowGrad.setColorAt(1.0, QColor(0, 0, 0, 50));
    p.setBrush(shadowGrad);
    p.setPen(Qt::NoPen);
    p.drawEllipse(center, radius, radius);

    // 2. Icon text centered, white, font-size = m_size * 0.45
    QFont font = p.font();
    font.setPixelSize(static_cast<int>(m_size * 0.45));
    p.setFont(font);
    p.setPen(Qt::white);
    p.drawText(rect(), Qt::AlignCenter, m_icon);
}
