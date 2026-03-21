#include "GradientProgressBar.h"
#include <QFont>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVariantAnimation>

GradientProgressBar::GradientProgressBar(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(14);
    setMinimumWidth(40);

    m_valueAnimation = new QPropertyAnimation(this, "displayedValue", this);
    m_valueAnimation->setDuration(320);
    m_valueAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_shimmerAnimation = new QVariantAnimation(this);
    m_shimmerAnimation->setStartValue(0.0);
    m_shimmerAnimation->setEndValue(1.0);
    m_shimmerAnimation->setDuration(2400);
    m_shimmerAnimation->setLoopCount(-1);
    connect(m_shimmerAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_shimmerPhase = value.toReal();
        update();
    });
    m_shimmerAnimation->start();
}

void GradientProgressBar::setValue(double value)
{
    const double clamped = qBound(0.0, value, 1.0);
    if (qAbs(m_value - clamped) < 0.0005) {
        return;
    }
    m_value = clamped;
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

void GradientProgressBar::setText(const QString& text)
{
    m_text = text;
    update();
}

void GradientProgressBar::setDisplayedValue(qreal value) {
    m_displayedValue = qBound<qreal>(0.0, value, 1.0);
    update();
}

QSize GradientProgressBar::sizeHint() const
{
    return QSize(220, m_text.isEmpty() ? 16 : 32);
}

QSize GradientProgressBar::minimumSizeHint() const
{
    return QSize(40, m_text.isEmpty() ? 12 : 28);
}

void GradientProgressBar::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int availableHeight = qMax(4, m_text.isEmpty() ? height() : height() - 18);
    const int barHeight = qMax(4, qMin(12, availableHeight - 2));
    const int barY = qMax(0, (availableHeight - barHeight) / 2);
    const int radius = qMax(2, barHeight / 2);
    const int barWidth = width() - 2;
    const int barX = 1;

    // 1. Track background
    {
        QPainterPath path;
        path.addRoundedRect(QRectF(barX, barY, barWidth, barHeight), radius, radius);
        p.fillPath(path, QColor(0x1e, 0x2d, 0x3d));
    }

    // 2. Fill with gradient blue -> purple -> pink
    if (m_displayedValue > 0.001) {
        const double fillWidth = qMax(barHeight * 1.0, barWidth * m_displayedValue);

        QLinearGradient grad(barX, 0, barX + barWidth, 0);
        grad.setColorAt(0.0, QColor(0x3b, 0x82, 0xf6)); // blue
        grad.setColorAt(0.5, QColor(0xa8, 0x55, 0xf7)); // purple
        grad.setColorAt(1.0, QColor(0xec, 0x48, 0x99)); // pink

        QPainterPath path;
        path.addRoundedRect(QRectF(barX, barY, fillWidth, barHeight), radius, radius);
        p.fillPath(path, grad);

        // Shimmer
        p.save();
        p.setClipPath(path);
        const qreal shimmerX = (fillWidth + 60.0) * m_shimmerPhase - 30.0;
        QLinearGradient shimmer(barX + shimmerX - 20.0, 0, barX + shimmerX + 20.0, 0);
        shimmer.setColorAt(0.0, QColor(255, 255, 255, 0));
        shimmer.setColorAt(0.5, QColor(255, 255, 255, 50));
        shimmer.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.fillRect(QRectF(barX, barY, fillWidth, barHeight), shimmer);
        p.restore();
    }

    // 3. Optional label text below
    if (!m_text.isEmpty()) {
        QFont font = p.font();
        font.setPixelSize(11);
        font.setBold(false);
        p.setFont(font);
        p.setPen(QColor(0x8b, 0x94, 0x9e));
        p.drawText(QRectF(barX, barY + barHeight + 5, barWidth, 16), Qt::AlignLeft | Qt::AlignTop, m_text);
    }
}
