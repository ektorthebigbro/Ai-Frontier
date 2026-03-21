#include "trend_chart_widget.h"

#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QtMath>

namespace {

QPainterPath buildPath(const QVector<double>& values, const QRectF& plot, double minValue, double maxValue) {
    QPainterPath path;
    if (values.isEmpty() || plot.width() <= 1.0 || plot.height() <= 1.0) {
        return path;
    }

    const double range = qMax(1e-6, maxValue - minValue);
    for (int i = 0; i < values.size(); ++i) {
        const double t = values.size() == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(values.size() - 1);
        const double x = plot.left() + t * plot.width();
        const double normalized = (values.at(i) - minValue) / range;
        const double y = plot.bottom() - normalized * plot.height();
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    return path;
}

}

TrendChartWidget::TrendChartWidget(QWidget* parent)
    : QWidget(parent),
      m_primaryColor(QColor(QStringLiteral("#19d39a"))),
      m_secondaryColor(QColor(QStringLiteral("#32b4ff"))) {
    setMinimumHeight(150);
    setAttribute(Qt::WA_TranslucentBackground, true);

    m_revealAnimation = new QVariantAnimation(this);
    m_revealAnimation->setStartValue(0.0);
    m_revealAnimation->setEndValue(1.0);
    m_revealAnimation->setDuration(550);
    m_revealAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_revealAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_revealProgress = value.toReal();
        update();
    });
}

void TrendChartWidget::setSeries(
    const QVector<double>& primary,
    const QVector<double>& secondary,
    const QString& primaryLabel,
    const QString& secondaryLabel
) {
    m_primary = primary;
    m_secondary = secondary;
    m_primaryLabel = primaryLabel;
    m_secondaryLabel = secondaryLabel;

    // Only animate reveal on first data arrival; subsequent updates just repaint smoothly
    if (!m_hasRevealed && !primary.isEmpty()) {
        m_hasRevealed = true;
        if (m_revealAnimation) {
            m_revealAnimation->stop();
            m_revealAnimation->start();
        }
    } else {
        m_revealProgress = 1.0;
        update();
    }
}

void TrendChartWidget::setColors(const QColor& primary, const QColor& secondary) {
    m_primaryColor = primary;
    if (secondary.isValid()) {
        m_secondaryColor = secondary;
    }
    update();
}

void TrendChartWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF outer = QRectF(rect()).adjusted(1, 1, -1, -1);

    // Background
    {
        QPainterPath bgPath;
        bgPath.addRoundedRect(outer, 12, 12);
        QLinearGradient bgGrad(outer.topLeft(), outer.bottomLeft());
        bgGrad.setColorAt(0.0, QColor(10, 18, 30, 80));
        bgGrad.setColorAt(1.0, QColor(8, 14, 24, 60));
        painter.fillPath(bgPath, bgGrad);
    }

    const QRectF plot = outer.adjusted(14, 18, -14, -22);
    if (plot.width() <= 0 || plot.height() <= 0) {
        return;
    }

    if (m_primary.isEmpty() && m_secondary.isEmpty()) {
        QFont f = painter.font();
        f.setPixelSize(12);
        painter.setFont(f);
        painter.setPen(QColor(QStringLiteral("#53677f")));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("Awaiting data"));
        return;
    }

    double minValue = 0.0;
    double maxValue = 0.0;
    bool seeded = false;
    auto scanValues = [&](const QVector<double>& values) {
        for (double value : values) {
            if (!qIsFinite(value)) {
                continue;
            }
            if (!seeded) {
                minValue = maxValue = value;
                seeded = true;
            } else {
                minValue = qMin(minValue, value);
                maxValue = qMax(maxValue, value);
            }
        }
    };
    scanValues(m_primary);
    scanValues(m_secondary);
    if (!seeded) {
        QFont f = painter.font();
        f.setPixelSize(12);
        painter.setFont(f);
        painter.setPen(QColor(QStringLiteral("#53677f")));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("Awaiting data"));
        return;
    }

    if (qFuzzyCompare(minValue + 1.0, maxValue + 1.0)) {
        minValue -= 1.0;
        maxValue += 1.0;
    } else {
        const double pad = (maxValue - minValue) * 0.12;
        minValue -= pad;
        maxValue += pad;
    }

    // Grid lines - subtle
    {
        QPen gridPen(QColor(28, 47, 72, 100), 1);
        gridPen.setStyle(Qt::DotLine);
        painter.setPen(gridPen);
        for (int i = 0; i < 4; ++i) {
            const double y = plot.top() + (plot.height() / 3.0) * i;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
    }

    painter.save();
    QRectF revealRect = plot;
    revealRect.setWidth(plot.width() * qBound<qreal>(0.0, m_revealProgress, 1.0));
    painter.setClipRect(revealRect.adjusted(-4, -20, 4, 20));

    const QPainterPath primaryPath = buildPath(m_primary, plot, minValue, maxValue);
    if (!primaryPath.isEmpty()) {
        // Area fill
        QPainterPath fillPath = primaryPath;
        fillPath.lineTo(plot.bottomRight());
        fillPath.lineTo(plot.bottomLeft());
        fillPath.closeSubpath();
        QLinearGradient fillGradient(plot.topLeft(), plot.bottomLeft());
        QColor fill = m_primaryColor;
        fill.setAlpha(60);
        QColor fillBottom = m_primaryColor;
        fillBottom.setAlpha(4);
        fillGradient.setColorAt(0.0, fill);
        fillGradient.setColorAt(1.0, fillBottom);
        painter.fillPath(fillPath, fillGradient);

        // Line
        painter.setPen(QPen(m_primaryColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(primaryPath);

        // Dots — show fewer when data is dense
        painter.setBrush(m_primaryColor);
        painter.setPen(Qt::NoPen);
        const int dotStep = m_primary.size() > 20 ? 3 : (m_primary.size() > 10 ? 2 : 1);
        for (int i = 0; i < m_primary.size(); ++i) {
            if (i != 0 && i != m_primary.size() - 1 && i % dotStep != 0) continue;
            const double t = m_primary.size() == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(m_primary.size() - 1);
            if (t > m_revealProgress) break;
            const double range = qMax(1e-6, maxValue - minValue);
            const double x = plot.left() + t * plot.width();
            const double y = plot.bottom() - ((m_primary.at(i) - minValue) / range) * plot.height();
            painter.drawEllipse(QPointF(x, y), 2.0, 2.0);
        }
    }

    const QPainterPath secondaryPath = buildPath(m_secondary, plot, minValue, maxValue);
    if (!secondaryPath.isEmpty()) {
        // Area fill for secondary series
        QPainterPath secFill = secondaryPath;
        secFill.lineTo(plot.bottomRight());
        secFill.lineTo(plot.bottomLeft());
        secFill.closeSubpath();
        QLinearGradient secGrad(plot.topLeft(), plot.bottomLeft());
        QColor secFillTop = m_secondaryColor;
        secFillTop.setAlpha(35);
        QColor secFillBot = m_secondaryColor;
        secFillBot.setAlpha(2);
        secGrad.setColorAt(0.0, secFillTop);
        secGrad.setColorAt(1.0, secFillBot);
        painter.fillPath(secFill, secGrad);

        // Solid line
        painter.setPen(QPen(m_secondaryColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(secondaryPath);

        // Dots for secondary
        painter.setBrush(m_secondaryColor);
        painter.setPen(Qt::NoPen);
        const int dotStep2 = m_secondary.size() > 20 ? 3 : (m_secondary.size() > 10 ? 2 : 1);
        for (int i = 0; i < m_secondary.size(); ++i) {
            if (i != 0 && i != m_secondary.size() - 1 && i % dotStep2 != 0) continue;
            const double t = m_secondary.size() == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(m_secondary.size() - 1);
            if (t > m_revealProgress) break;
            const double range = qMax(1e-6, maxValue - minValue);
            const double x = plot.left() + t * plot.width();
            const double y = plot.bottom() - ((m_secondary.at(i) - minValue) / range) * plot.height();
            painter.drawEllipse(QPointF(x, y), 2.0, 2.0);
        }
    }
    painter.restore();

    // Legend with color swatches
    if (!m_primaryLabel.isEmpty() || !m_secondaryLabel.isEmpty()) {
        QFont f(QStringLiteral("Segoe UI"), 8, QFont::DemiBold);
        painter.setFont(f);
        const QFontMetrics fm(f);
        const double legendY = outer.bottom() - 14;
        double x = plot.right();

        auto drawLegendItem = [&](const QString& label, const QColor& color) {
            const int textWidth = fm.horizontalAdvance(label);
            x -= textWidth;
            painter.setPen(QColor(QStringLiteral("#7a94ae")));
            painter.drawText(QPointF(x, legendY + 10), label);
            x -= 14;
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(QPointF(x + 4, legendY + 5), 3.5, 3.5);
            x -= 8;
        };

        if (!m_secondaryLabel.isEmpty()) {
            drawLegendItem(m_secondaryLabel, m_secondaryColor);
            x -= 6;
            painter.setPen(QColor(50, 70, 90));
            painter.drawText(QPointF(x - 2, legendY + 10), QStringLiteral("|"));
            x -= 12;
        }
        if (!m_primaryLabel.isEmpty()) {
            drawLegendItem(m_primaryLabel, m_primaryColor);
        }
    }
}
