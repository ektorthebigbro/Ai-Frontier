#include "GlowCard.h"
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QFont>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <cmath>

GlowCard::GlowCard(const QString& title, QWidget* parent)
    : QWidget(parent), m_title(title)
{
    setAttribute(Qt::WA_TranslucentBackground);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(18, 14, 18, 14);
    outerLayout->setSpacing(10);

    if (!m_title.isEmpty()) {
        // Reserve space for title drawn in paintEvent
        outerLayout->setContentsMargins(18, 34, 18, 14);
    }

    m_content = new QVBoxLayout();
    m_content->setContentsMargins(0, 0, 0, 0);
    m_content->setSpacing(10);
    outerLayout->addLayout(m_content);

    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(200);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_pulseAnimation = new QVariantAnimation(this);
    m_pulseAnimation->setStartValue(0.0);
    m_pulseAnimation->setEndValue(1.0);
    m_pulseAnimation->setDuration(3000);
    m_pulseAnimation->setLoopCount(-1);
    connect(m_pulseAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_pulsePhase = value.toReal();
        update();
    });
    m_pulseAnimation->start();
}

void GlowCard::setGlowColor(const QColor& color)
{
    m_glowColor = color;
    update();
}

void GlowCard::setHoverProgress(qreal value) {
    m_hoverProgress = qBound(0.0, value, 1.0);
    update();
}

void GlowCard::enterEvent(QEnterEvent* event) {
    if (m_hoverAnimation) {
        m_hoverAnimation->stop();
        m_hoverAnimation->setStartValue(m_hoverProgress);
        m_hoverAnimation->setEndValue(1.0);
        m_hoverAnimation->start();
    }
    QWidget::enterEvent(event);
}

void GlowCard::leaveEvent(QEvent* event) {
    if (m_hoverAnimation) {
        m_hoverAnimation->stop();
        m_hoverAnimation->setStartValue(m_hoverProgress);
        m_hoverAnimation->setEndValue(0.0);
        m_hoverAnimation->start();
    }
    QWidget::leaveEvent(event);
}

void GlowCard::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF cardRect = QRectF(rect()).adjusted(2, 2, -2, -2);

    // 1. Main card background
    {
        QPainterPath path;
        path.addRoundedRect(cardRect, m_radius, m_radius);
        QLinearGradient gradient(cardRect.topLeft(), cardRect.bottomRight());
        gradient.setColorAt(0.0, QColor(0x16, 0x1b, 0x22));
        gradient.setColorAt(0.5, QColor(0x13, 0x18, 0x20));
        gradient.setColorAt(1.0, QColor(0x0f, 0x14, 0x1c));
        p.fillPath(path, gradient);
    }

    // 2. Border - subtle with hover brightening
    {
        QColor borderColor(0x1e, 0x2d, 0x3d);
        if (m_hoverProgress > 0.0) {
            int r = borderColor.red()   + static_cast<int>((0x2a - 0x1e) * m_hoverProgress);
            int g = borderColor.green() + static_cast<int>((0x3a - 0x2d) * m_hoverProgress);
            int b = borderColor.blue()  + static_cast<int>((0x4e - 0x3d) * m_hoverProgress);
            borderColor = QColor(r, g, b);
        }
        p.setPen(QPen(borderColor, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(cardRect, m_radius, m_radius);
    }

    // 3. Subtle top-edge highlight on hover
    if (m_hoverProgress > 0.01) {
        QColor topColor(0x3b, 0x82, 0xf6);
        topColor.setAlpha(static_cast<int>(60 * m_hoverProgress));
        QPainterPath topPath;
        topPath.moveTo(cardRect.left() + m_radius, cardRect.top());
        topPath.lineTo(cardRect.right() - m_radius, cardRect.top());
        p.setPen(QPen(topColor, 1.5));
        p.drawPath(topPath);
    }

    // 4. Title text
    if (!m_title.isEmpty()) {
        QFont font = p.font();
        font.setPixelSize(13);
        font.setWeight(QFont::DemiBold);
        p.setFont(font);
        p.setPen(QColor(0xe6, 0xed, 0xf3));
        QRectF titleRect = cardRect.adjusted(18, 10, -18, 0);
        titleRect.setHeight(20);
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, m_title);
    }
}
