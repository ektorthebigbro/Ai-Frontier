#ifndef TREND_CHART_WIDGET_H
#define TREND_CHART_WIDGET_H

#include <QColor>
#include <QVector>
#include <QWidget>

class QVariantAnimation;

class TrendChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrendChartWidget(QWidget* parent = nullptr);

    void setSeries(
        const QVector<double>& primary,
        const QVector<double>& secondary = {},
        const QString& primaryLabel = QString(),
        const QString& secondaryLabel = QString()
    );
    void setColors(const QColor& primary, const QColor& secondary = QColor());

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<double> m_primary;
    QVector<double> m_secondary;
    QColor m_primaryColor;
    QColor m_secondaryColor;
    QString m_primaryLabel;
    QString m_secondaryLabel;
    qreal m_revealProgress = 1.0;
    bool m_hasRevealed = false;
    QVariantAnimation* m_revealAnimation = nullptr;
};

#endif  // TREND_CHART_WIDGET_H
