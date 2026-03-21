#ifndef OVERVIEW_PAGE_H
#define OVERVIEW_PAGE_H

#include "BasePage.h"
#include <QLabel>
#include <QProgressBar>
#include <QVector>

class QButtonGroup;
class QVBoxLayout;
class QPushButton;
class GlowCard;
class GradientProgressBar;
class HeroMetricCard;
class PillBadge;
class TrendChartWidget;

class OverviewPage : public BasePage {
    Q_OBJECT
public:
    explicit OverviewPage(ApiClient* api, QWidget* parent = nullptr);
    void updateFromState(const QJsonObject& state) override;
    bool hasAdvancedPanels() const override { return true; }
    void setAdvancedMode(bool advanced) override;

private:
    struct AutopilotStageBlock {
        QString stageId;
        QPushButton* button = nullptr;
        QLabel* stepLabel = nullptr;
        QLabel* titleLabel = nullptr;
        QLabel* stateLabel = nullptr;
        QLabel* detailLabel = nullptr;
        QLabel* progressLabel = nullptr;
        GradientProgressBar* progressBar = nullptr;
    };

    void buildHeroSection(QVBoxLayout* mainLay);
    void buildMetricCards(QVBoxLayout* mainLay);
    void buildChartSection(QVBoxLayout* mainLay);
    void buildControlGrid(QVBoxLayout* mainLay);
    void buildSystemRail(QVBoxLayout* rail);
    void buildAutopilotMissionCard(QVBoxLayout* parentLay);
    void updateCharts();
    void updateAutopilotMission(const QJsonObject& state,
                                const QJsonObject& jobs,
                                const QJsonObject& processes,
                                const QString& primaryJobId,
                                const QString& primaryStage,
                                const QString& primaryMsg,
                                double primaryProg,
                                const QString& primaryEta,
                                double primaryTs);
    QVector<double> sliceForRange(const QVector<double>& full) const;

    // Hero metrics
    HeroMetricCard* m_heroStatus = nullptr;
    HeroMetricCard* m_heroLoss = nullptr;
    HeroMetricCard* m_heroAccuracy = nullptr;
    HeroMetricCard* m_heroEta = nullptr;

    // Progress
    GradientProgressBar* m_progressBar = nullptr;
    QLabel* m_progressLabel = nullptr;
    QLabel* m_progressPct = nullptr;

    // Mission card (compact top-right)
    PillBadge* m_missionStatus = nullptr;
    QLabel* m_missionName = nullptr;
    QLabel* m_missionSummary = nullptr;
    QLabel* m_missionUpdated = nullptr;
    QLabel* m_missionPct = nullptr;
    GradientProgressBar* m_missionProgressBar = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QString m_currentPrimaryJob;

    // Metric cards
    QLabel* m_metricStep = nullptr;
    QLabel* m_metricLoss = nullptr;
    QLabel* m_metricReward = nullptr;
    QLabel* m_metricEval = nullptr;

    // Charts
    TrendChartWidget* m_lossChart = nullptr;
    TrendChartWidget* m_signalChart = nullptr;
    TrendChartWidget* m_runtimeChart = nullptr;
    QLabel* m_lossChartSummary = nullptr;
    QLabel* m_signalChartSummary = nullptr;
    QLabel* m_runtimeChartSummary = nullptr;
    QButtonGroup* m_rangeGroup = nullptr;
    int m_chartVisiblePoints = 0;  // 0 = all
    QVector<double> m_lossHistory;
    QVector<double> m_rewardHistory;
    QVector<double> m_progressHistory;
    QVector<double> m_gpuLoadHistory;
    QVector<double> m_vramHistory;
    QVector<double> m_ramHistory;

    // Control section
    QLabel* m_autopilotMissionState = nullptr;
    QLabel* m_autopilotMissionSummary = nullptr;
    QLabel* m_autopilotMissionMeta = nullptr;
    QLabel* m_autopilotMissionRecovery = nullptr;
    QLabel* m_autopilotMissionHint = nullptr;
    QPushButton* m_autopilotLaunchBtn = nullptr;
    QPushButton* m_autopilotPauseBtn = nullptr;
    QPushButton* m_autopilotResumeBtn = nullptr;
    QWidget* m_advancedControlsWrap = nullptr;
    QVector<AutopilotStageBlock> m_autopilotTimeline;
    QVector<QWidget*> m_autopilotTimelineConnectors;
    QLabel* m_judgeLabel = nullptr;
    QLabel* m_judgeModelLabel = nullptr;
    QPushButton* m_judgeToggleBtn = nullptr;

    // System rail
    QLabel* m_alertHeaderCount = nullptr;
    QWidget* m_alertList = nullptr;
    QLabel* m_hwInfo = nullptr;
    QLabel* m_processInfo = nullptr;
    QLabel* m_storageInfo = nullptr;

    // Advanced panels
    GlowCard* m_judgeCard = nullptr;
};
#endif
