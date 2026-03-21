#include "DataSplitBar.h"
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QFont>

DataSplitBar::DataSplitBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(36); // 12px bar + 4px gap + 16px labels
}

void DataSplitBar::setSegments(const QVector<SplitSegment>& segments)
{
    m_segments = segments;
    update();
}

void DataSplitBar::paintEvent(QPaintEvent* /*event*/)
{
    if (m_segments.isEmpty()) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int barHeight = 12;
    const int outerRadius = 6;
    const double totalWidth = static_cast<double>(width());

    // Clip the entire bar to a rounded rect so segments at the edges are rounded
    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(0, 0, totalWidth, barHeight), outerRadius, outerRadius);
    p.setClipPath(clipPath);

    // Draw each segment as a contiguous rectangle (clipping handles rounding)
    double x = 0.0;
    for (const auto& seg : m_segments) {
        double segWidth = totalWidth * qBound(0.0, seg.fraction, 1.0);
        if (segWidth < 1.0 && seg.fraction > 0.0) segWidth = 1.0;

        QRectF segRect(x, 0, segWidth, barHeight);
        p.fillRect(segRect, seg.color);
        x += segWidth;
    }

    p.setClipping(false);

    // Draw labels below each segment
    QFont font = p.font();
    font.setPixelSize(11);
    p.setFont(font);

    x = 0.0;
    for (const auto& seg : m_segments) {
        double segWidth = totalWidth * qBound(0.0, seg.fraction, 1.0);
        if (segWidth < 1.0 && seg.fraction > 0.0) segWidth = 1.0;

        QString labelText = seg.label + " " +
            QString::number(seg.fraction * 100.0, 'f', 1) + "%";

        p.setPen(seg.color);
        QRectF labelRect(x, barHeight + 4, segWidth, 16);
        p.drawText(labelRect, Qt::AlignLeft | Qt::AlignTop, labelText);

        x += segWidth;
    }
}
