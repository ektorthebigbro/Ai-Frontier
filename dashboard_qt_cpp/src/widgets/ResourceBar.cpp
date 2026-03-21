#include "ResourceBar.h"
#include <QFont>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

ResourceBar::ResourceBar(const QString& label, QWidget* parent)
    : QWidget(parent), m_label(label)
{
    setFixedHeight(40);
    m_valueAnimation = new QPropertyAnimation(this, "displayedValue", this);
    m_valueAnimation->setDuration(320);
    m_valueAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

void ResourceBar::setValue(double fraction, const QString& detail)
{
    m_value = qBound(0.0, fraction, 1.0);
    m_detail = detail;
    if (m_valueAnimation) {
        m_valueAnimation->stop();
        m_valueAnimation->setStartValue(m_displayedValue);
        m_valueAnimation->setEndValue(m_value);
        m_valueAnimation->start();
    } else {
        m_displayedValue = m_value;
        update();
    }
}

void ResourceBar::setBarColor(const QColor& color)
{
    m_barColor = color;
    update();
}

void ResourceBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int textRowHeight = 20;
    const int barHeight = 8;
    const int barY = textRowHeight + 4;
    const int barRadius = 4;

    // Top row: label left, percentage + detail right
    {
        QFont font = p.font();
        font.setPixelSize(12);
        font.setWeight(QFont::Medium);
        p.setFont(font);

        // Label
        p.setPen(QColor(0xe6, 0xed, 0xf3));
        p.drawText(QRectF(0, 0, width() / 2, textRowHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, m_label);

        // Percentage + detail
        QString rightText = QString::number(static_cast<int>(m_displayedValue * 100)) + "%";
        if (!m_detail.isEmpty()) rightText += "  " + m_detail;
        p.setPen(QColor(0x8b, 0x94, 0x9e));
        p.drawText(QRectF(width() / 2, 0, width() / 2, textRowHeight),
                   Qt::AlignRight | Qt::AlignVCenter, rightText);
    }

    // Track
    {
        QPainterPath path;
        path.addRoundedRect(QRectF(0, barY, width(), barHeight), barRadius, barRadius);
        p.fillPath(path, QColor(0x1e, 0x2d, 0x3d));
    }

    // Fill bar with slight gradient brightness variation
    if (m_displayedValue > 0.0) {
        double fillWidth = width() * m_displayedValue;

        QLinearGradient grad(0, barY, fillWidth, barY);
        grad.setColorAt(0.0, m_barColor.lighter(110));
        grad.setColorAt(1.0, m_barColor);

        QPainterPath path;
        path.addRoundedRect(QRectF(0, barY, fillWidth, barHeight), barRadius, barRadius);
        p.fillPath(path, grad);
    }
}
