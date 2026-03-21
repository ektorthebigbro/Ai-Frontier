#include "StatusIndicator.h"
#include <QFont>
#include <QPaintEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <cmath>

StatusIndicator::StatusIndicator(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(24);

    m_pulseAnimation = new QVariantAnimation(this);
    m_pulseAnimation->setStartValue(0.0);
    m_pulseAnimation->setEndValue(1.0);
    m_pulseAnimation->setDuration(1800);
    m_pulseAnimation->setLoopCount(-1);
    connect(m_pulseAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_pulsePhase = value.toReal();
        update();
    });
    m_pulseAnimation->start();
}

void StatusIndicator::setStatus(const QString& text, const QColor& dotColor)
{
    m_text = text;
    m_dotColor = dotColor;
    update();
}

void StatusIndicator::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int dotSize = 8;
    const int dotX = 4;
    const int dotY = (height() - dotSize) / 2;
    const int textOffset = dotX + dotSize + 8;
    const qreal haloScale = 1.0 + 0.6 * (0.5 + 0.5 * std::sin(m_pulsePhase * 6.283185307179586));

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(m_dotColor.red(), m_dotColor.green(), m_dotColor.blue(), 40));
    const qreal haloSize = dotSize * haloScale + 6.0;
    p.drawEllipse(QRectF(dotX - (haloSize - dotSize) / 2.0, dotY - (haloSize - dotSize) / 2.0, haloSize, haloSize));

    p.setPen(Qt::NoPen);
    p.setBrush(m_dotColor);
    p.drawEllipse(dotX, dotY, dotSize, dotSize);

    // Status text
    QFont font = p.font();
    font.setPixelSize(12);
    p.setFont(font);
    p.setPen(QColor(0xe6, 0xed, 0xf3));
    p.drawText(QRectF(textOffset, 0, width() - textOffset, height()),
               Qt::AlignLeft | Qt::AlignVCenter, m_text);
}
