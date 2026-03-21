#ifndef METRICS_PAGE_H
#define METRICS_PAGE_H

#include "BasePage.h"
#include <QLabel>
#include <QVector>

class QButtonGroup;
class TrendChartWidget;
class HeroMetricCard;

class MetricsPage : public BasePage {
    Q_OBJECT
public:
    struct TimedValue { double ts; double value; };

    explicit MetricsPage(ApiClient* api, QWidget* parent = nullptr);
    void updateFromState(const QJsonObject& state) override;

private:

    void updateCharts();
    QVector<TimedValue> sliceForRange(const QVector<TimedValue>& full) const;
    static QVector<double> valuesOnly(const QVector<TimedValue>& tv);

    HeroMetricCard* m_trainLossCard = nullptr;
    HeroMetricCard* m_valLossCard = nullptr;
    HeroMetricCard* m_trainAccCard = nullptr;
    HeroMetricCard* m_valAccCard = nullptr;

    TrendChartWidget* m_lossChart = nullptr;
    TrendChartWidget* m_accChart = nullptr;

    QButtonGroup* m_rangeGroup = nullptr;
    int m_rangeSeconds = 0;

    QVector<TimedValue> m_trainLossHistory;
    QVector<TimedValue> m_valLossHistory;
    QVector<TimedValue> m_reasoningHistory;
    QVector<TimedValue> m_evalHistory;

    double m_prevTrainLoss = -1.0;
    double m_prevValLoss = -1.0;
    double m_prevReasoning = -1.0;
    double m_prevEval = -1.0;
};
#endif
