#include "HeroMetricCard.h"
#include "IconBadge.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QFont>

HeroMetricCard::HeroMetricCard(const QString& icon, const QColor& iconColor,
                               const QString& title, QWidget* parent)
    : QWidget(parent), m_iconColor(iconColor)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumHeight(130);

    m_badge = new IconBadge(icon, iconColor, 44, this);

    m_title = new QLabel(title, this);
    m_title->setStyleSheet(
        "color: #8b949e; font-size: 12px; font-weight: 500; background: transparent;");

    m_value = new QLabel("--", this);
    m_value->setStyleSheet(
        "color: #ffffff; font-size: 28px; font-weight: bold; background: transparent;");

    m_chip = new QLabel(this);
    m_chip->setVisible(false);
    m_chip->setStyleSheet(
        "background: transparent; font-size: 11px; font-weight: 600;");

    m_trend = new QLabel(this);
    m_trend->setVisible(false);
    m_trend->setStyleSheet(
        "background: transparent; font-size: 11px; font-weight: 600;");

    // Layout: icon top-left, chip/trend top-right, title+value below
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 14, 18, 14);
    mainLayout->setSpacing(6);

    // Top row: icon badge on left, chip+trend on right
    auto* topRow = new QHBoxLayout();
    topRow->setContentsMargins(0, 0, 0, 0);
    topRow->setSpacing(8);
    topRow->addWidget(m_badge, 0, Qt::AlignLeft | Qt::AlignVCenter);
    topRow->addStretch(1);

    // Chip and trend stacked to the right
    auto* chipTrendLayout = new QVBoxLayout();
    chipTrendLayout->setContentsMargins(0, 0, 0, 0);
    chipTrendLayout->setSpacing(2);
    chipTrendLayout->addWidget(m_chip, 0, Qt::AlignRight);
    chipTrendLayout->addWidget(m_trend, 0, Qt::AlignRight);
    topRow->addLayout(chipTrendLayout);

    mainLayout->addLayout(topRow);
    mainLayout->addStretch(1);

    // Bottom: title then value
    mainLayout->addWidget(m_title);
    mainLayout->addWidget(m_value);
}

void HeroMetricCard::setValue(const QString& value)
{
    m_value->setText(value);
}

void HeroMetricCard::setChip(const QString& text, const QColor& chipColor)
{
    QColor c = chipColor.isValid() ? chipColor : m_iconColor;
    m_chip->setText(text);
    m_chip->setVisible(!text.isEmpty());
    m_chip->setStyleSheet(QString(
        "background: rgba(%1,%2,%3,35); color: rgb(%1,%2,%3); "
        "border: 1px solid rgba(%1,%2,%3,90); border-radius: 9px; "
        "padding: 2px 10px; font-size: 11px; font-weight: 600;")
        .arg(c.red()).arg(c.green()).arg(c.blue()));
}

void HeroMetricCard::setTrend(const QString& text, bool positive, const QString& glyph)
{
    const QString arrow = glyph.isEmpty()
        ? (positive ? QString::fromUtf8("\xe2\x96\xb2") : QString::fromUtf8("\xe2\x96\xbc"))
        : glyph;
    QString color = positive ? "#22c55e" : "#ef4444";
    m_trend->setText(arrow + " " + text);
    m_trend->setVisible(!text.isEmpty());
    m_trend->setStyleSheet(QString(
        "color: %1; font-size: 11px; font-weight: 600; background: transparent;")
        .arg(color));
}

void HeroMetricCard::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int radius = 14;
    const QRectF cardRect = QRectF(rect()).adjusted(2, 2, -2, -2);

    // Card background - subtle gradient
    {
        QPainterPath path;
        path.addRoundedRect(cardRect, radius, radius);
        QLinearGradient gradient(cardRect.topLeft(), cardRect.bottomRight());
        gradient.setColorAt(0.0, QColor(0x16, 0x1b, 0x22));
        gradient.setColorAt(0.5, QColor(0x13, 0x18, 0x20));
        gradient.setColorAt(1.0, QColor(0x0f, 0x14, 0x1c));
        p.fillPath(path, gradient);
    }

    // Border - subtle tinted
    {
        QColor borderColor = m_iconColor;
        borderColor.setAlpha(50);
        p.setPen(QPen(borderColor, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(cardRect, radius, radius);
    }

    // Top edge highlight
    {
        QColor topColor = m_iconColor;
        topColor.setAlpha(35);
        QPainterPath topPath;
        topPath.moveTo(cardRect.left() + radius, cardRect.top());
        topPath.lineTo(cardRect.right() - radius, cardRect.top());
        p.setPen(QPen(topColor, 1.0));
        p.drawPath(topPath);
    }
}
