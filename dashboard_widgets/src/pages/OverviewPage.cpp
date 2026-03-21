#include "OverviewPage.h"
#include "../app/ApiClient.h"
#include "../util/Formatters.h"
#include "../widgets/GlowCard.h"
#include "../widgets/GradientProgressBar.h"
#include "../widgets/HeroMetricCard.h"
#include "../widgets/IconBadge.h"
#include "../widgets/PillBadge.h"
#include "../widgets/trend_chart_widget.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QtMath>
#include <algorithm>

namespace {

QString normalizeAutopilotStage(const QString& rawStage)
{
    const QString stage = rawStage.trimmed().toLower();
    if (stage == QStringLiteral("environment")) return QStringLiteral("setup");
    if (stage == QStringLiteral("dataset_prep")) return QStringLiteral("prepare");
    if (stage == QStringLiteral("evaluation")) return QStringLiteral("evaluate");
    return stage;
}

QString autopilotStageTitle(const QString& rawStage)
{
    const QString stage = normalizeAutopilotStage(rawStage);
    if (stage == QStringLiteral("setup")) return QStringLiteral("Environment");
    if (stage == QStringLiteral("prepare")) return QStringLiteral("Prepare Data");
    if (stage == QStringLiteral("training")) return QStringLiteral("Training");
    if (stage == QStringLiteral("evaluate")) return QStringLiteral("Evaluation");
    return QStringLiteral("Unknown");
}

QString autopilotStageDetail(const QString& rawStage)
{
    const QString stage = normalizeAutopilotStage(rawStage);
    if (stage == QStringLiteral("setup")) return QStringLiteral("Repair the Python environment, dependencies, and CUDA stack.");
    if (stage == QStringLiteral("prepare")) return QStringLiteral("Download assets, build the tokenizer, and prepare the scored dataset.");
    if (stage == QStringLiteral("training")) return QStringLiteral("Resume or continue the model run from the latest durable checkpoint.");
    if (stage == QStringLiteral("evaluate")) return QStringLiteral("Run the final report pass against the latest checkpoint.");
    return QString();
}

QString autopilotStageJobName(const QString& rawStage)
{
    const QString stage = normalizeAutopilotStage(rawStage);
    if (stage == QStringLiteral("setup")) return QStringLiteral("setup");
    if (stage == QStringLiteral("prepare")) return QStringLiteral("prepare");
    if (stage == QStringLiteral("training")) return QStringLiteral("training");
    if (stage == QStringLiteral("evaluate")) return QStringLiteral("evaluate");
    return QString();
}

QString recommendedAutopilotStageFromJobs(const QJsonObject& jobs)
{
    if (jobs.value(QStringLiteral("setup")).toObject().value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("setup");
    }
    if (jobs.value(QStringLiteral("prepare")).toObject().value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("prepare");
    }
    if (jobs.value(QStringLiteral("training")).toObject().value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("training");
    }
    if (jobs.value(QStringLiteral("evaluate")).toObject().value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("evaluate");
    }
    return QStringLiteral("training");
}

int autopilotStageIndex(const QString& rawStage)
{
    static const QStringList order = {
        QStringLiteral("setup"),
        QStringLiteral("prepare"),
        QStringLiteral("training"),
        QStringLiteral("evaluate"),
    };
    return order.indexOf(normalizeAutopilotStage(rawStage));
}

QString autopilotContinueEndpoint(const QString& rawStage)
{
    return QStringLiteral("/api/actions/autopilot/continue/%1").arg(normalizeAutopilotStage(rawStage));
}

QString recoveryWindowText(double seconds)
{
    const qint64 totalSeconds = qMax<qint64>(0, static_cast<qint64>(seconds + 0.5));
    if (totalSeconds < 60) return QStringLiteral("%1s").arg(totalSeconds);
    if (totalSeconds < 3600) return QStringLiteral("%1m").arg((totalSeconds + 30) / 60);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    if (minutes <= 0) return QStringLiteral("%1h").arg(hours);
    return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
}

void clearLayoutItems(QLayout* layout)
{
    if (!layout) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        if (QLayout* childLayout = item->layout()) {
            clearLayoutItems(childLayout);
            delete childLayout;
        }
        delete item;
    }
}

QString alertObjectName(const QString& severity)
{
    if (severity == QStringLiteral("error")) return QStringLiteral("alertError");
    if (severity == QStringLiteral("warning")) return QStringLiteral("alertWarning");
    return QStringLiteral("alertInfo");
}

double extractOverviewMetric(const QString& text, const QStringList& patterns)
{
    for (const QString& pattern : patterns) {
        const QString match = Fmt::captureMatch(text, pattern);
        bool ok = false;
        const double value = match.toDouble(&ok);
        if (ok) {
            return value;
        }
    }
    return qQNaN();
}

double extractOverviewLoss(const QString& text)
{
    return extractOverviewMetric(text, {
        QStringLiteral("loss\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
        QStringLiteral("train(?:ing)?[_\\s-]?loss\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
    });
}

double extractOverviewReward(const QString& text)
{
    return extractOverviewMetric(text, {
        QStringLiteral("reward(?:_score)?\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
        QStringLiteral("reasoning\\s+score\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
        QStringLiteral("protocol\\s+score\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
    });
}

bool overviewRowLooksMetricLike(const QJsonObject& row)
{
    const QString job = row.value(QStringLiteral("job")).toString();
    const QString message = row.value(QStringLiteral("message")).toString();
    if ((job == QStringLiteral("training") || job == QStringLiteral("evaluate"))
        && !message.trimmed().isEmpty()) {
        return true;
    }
    if (message.isEmpty()) {
        return false;
    }
    return qIsFinite(extractOverviewLoss(message))
        || qIsFinite(extractOverviewReward(message))
        || message.contains(QStringLiteral("step"), Qt::CaseInsensitive);
}

QJsonArray chooseOverviewFeed(const QJsonObject& state)
{
    const QJsonArray metricsFeed = state.value(QStringLiteral("metrics_feed")).toArray();
    int metricLikeRows = 0;
    for (const QJsonValue& value : metricsFeed) {
        if (overviewRowLooksMetricLike(value.toObject())) {
            ++metricLikeRows;
            if (metricLikeRows >= 3) {
                return metricsFeed;
            }
        }
    }
    return state.value(QStringLiteral("feed")).toArray();
}

void appendUniqueChartValue(QVector<double>& history, double value, int limit)
{
    if (!qIsFinite(value)) {
        return;
    }
    if (!history.isEmpty() && qFuzzyCompare(history.last() + 1.0, value + 1.0)) {
        return;
    }
    history.append(value);
    if (history.size() > limit) {
        history.remove(0, history.size() - limit);
    }
}

struct OverviewTrainingSnapshot {
    QVector<double> lossHistory;
    QVector<double> rewardHistory;
    QVector<double> progressHistory;
    QString latestStep = QStringLiteral("0");
    QString latestLoss = QStringLiteral("--");
    QString latestReward = QStringLiteral("--");
};

void appendOverviewTrainingRow(const QJsonObject& row, OverviewTrainingSnapshot& snapshot, int limit)
{
    const QString job = row.value(QStringLiteral("job")).toString();
    const QString stage = row.value(QStringLiteral("stage")).toString();
    const QString message = row.value(QStringLiteral("message")).toString();
    const double progress = row.value(QStringLiteral("progress")).toDouble(qQNaN());

    if ((job == QStringLiteral("training") || job == QStringLiteral("autopilot"))
        && qIsFinite(progress)) {
        appendUniqueChartValue(snapshot.progressHistory, progress * 100.0, limit);
    }

    if (job != QStringLiteral("training")) {
        return;
    }

    if (!message.isEmpty()) {
        const QString stepText = Fmt::captureMatch(message, QStringLiteral("step\\s+(\\d+)"));
        if (!stepText.isEmpty()) {
            snapshot.latestStep = stepText;
        }
    }

    if (stage != QStringLiteral("training") && stage != QStringLiteral("paused") && stage != QStringLiteral("recovery_point")) {
        return;
    }

    const double loss = extractOverviewLoss(message);
    if (qIsFinite(loss)) {
        appendUniqueChartValue(snapshot.lossHistory, loss, limit);
        snapshot.latestLoss = Fmt::fmtDouble(loss, 4);
    }

    const double reward = extractOverviewReward(message);
    if (qIsFinite(reward)) {
        appendUniqueChartValue(snapshot.rewardHistory, reward, limit);
        snapshot.latestReward = Fmt::fmtDouble(reward, 4);
    }
}

OverviewTrainingSnapshot buildOverviewTrainingSnapshot(const QJsonObject& state, const QJsonObject& training, double fallbackProgress)
{
    constexpr int kMaxHistory = 2000;
    OverviewTrainingSnapshot snapshot;

    const QJsonArray feed = chooseOverviewFeed(state);
    for (const QJsonValue& value : feed) {
        appendOverviewTrainingRow(value.toObject(), snapshot, kMaxHistory);
    }

    if (snapshot.lossHistory.size() < 2 || snapshot.progressHistory.size() < 2) {
        const QJsonArray steps = training.value(QStringLiteral("steps")).toArray();
        for (const QJsonValue& value : steps) {
            appendOverviewTrainingRow(value.toObject(), snapshot, kMaxHistory);
        }
    }

    const QString trainingMsg = training.value(QStringLiteral("message")).toString();
    const double currentLoss = extractOverviewLoss(trainingMsg);
    if (qIsFinite(currentLoss)) {
        appendUniqueChartValue(snapshot.lossHistory, currentLoss, kMaxHistory);
        snapshot.latestLoss = Fmt::fmtDouble(currentLoss, 4);
    }

    const double currentReward = extractOverviewReward(trainingMsg);
    if (qIsFinite(currentReward)) {
        appendUniqueChartValue(snapshot.rewardHistory, currentReward, kMaxHistory);
        snapshot.latestReward = Fmt::fmtDouble(currentReward, 4);
    }

    const QString stepText = Fmt::captureMatch(trainingMsg, QStringLiteral("step\\s+(\\d+)"));
    if (!stepText.isEmpty()) {
        snapshot.latestStep = stepText;
    }

    appendUniqueChartValue(snapshot.progressHistory, fallbackProgress * 100.0, kMaxHistory);
    return snapshot;
}

}  // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
OverviewPage::OverviewPage(ApiClient* api, QWidget* parent)
    : BasePage(api, parent)
{
    auto* page = new QWidget;
    auto* mainLay = new QVBoxLayout(page);
    mainLay->setContentsMargins(24, 22, 24, 20);
    mainLay->setSpacing(14);

    buildHeroSection(mainLay);
    buildMetricCards(mainLay);
    buildChartSection(mainLay);

    // --- Bottom row: control grid + system rail ---
    auto* contentRow = new QHBoxLayout;
    contentRow->setSpacing(18);

    auto* gridWidget = new QWidget;
    auto* gridLay = new QVBoxLayout(gridWidget);
    gridLay->setContentsMargins(0, 0, 0, 0);
    gridLay->setSpacing(0);
    buildControlGrid(gridLay);
    contentRow->addWidget(gridWidget, 1);

    auto* railWrap = new QWidget;
    railWrap->setObjectName(QStringLiteral("systemRail"));
    railWrap->setAttribute(Qt::WA_StyledBackground, true);
    railWrap->setFixedWidth(300);
    auto* railLay = new QVBoxLayout(railWrap);
    railLay->setContentsMargins(0, 0, 0, 0);
    railLay->setSpacing(14);
    buildSystemRail(railLay);
    railLay->addStretch(1);
    contentRow->addWidget(railWrap, 0, Qt::AlignTop);

    mainLay->addLayout(contentRow, 1);

    auto* wrapper = buildScrollWrapper(page);
    auto* outerLay = new QVBoxLayout(this);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->addWidget(wrapper);
}

// ---------------------------------------------------------------------------
// Hero section  (~100 lines)
// ---------------------------------------------------------------------------
void OverviewPage::buildHeroSection(QVBoxLayout* mainLay)
{
    // Title row with mission status top-right
    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(16);

    auto* titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(2);
    auto* title = new QLabel(QStringLiteral("AI Training Dashboard"));
    title->setObjectName(QStringLiteral("pageHeroTitle"));
    titleBlock->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("Real-time model training monitor"));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    subtitle->setWordWrap(true);
    titleBlock->addWidget(subtitle);
    titleBlock->addSpacing(22);

    auto* progressCard = new GlowCard(QStringLiteral("Workflow Progress"));
    progressCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* pLay = progressCard->contentLayout();

    auto* topRow = new QHBoxLayout;
    m_progressLabel = new QLabel(QStringLiteral("No active workflow"));
    m_progressLabel->setObjectName(QStringLiteral("dimText"));
    topRow->addWidget(m_progressLabel);
    topRow->addStretch(1);
    m_progressPct = new QLabel(QStringLiteral("0.0%"));
    m_progressPct->setObjectName(QStringLiteral("summaryChipValue"));
    topRow->addWidget(m_progressPct);
    pLay->addLayout(topRow);

    m_progressBar = new GradientProgressBar;
    m_progressBar->setFixedHeight(16);
    m_progressBar->setValue(0.0);
    pLay->addWidget(m_progressBar);
    titleBlock->addWidget(progressCard);
    headerRow->addLayout(titleBlock, 1);

    // Mission status card (top-right)
    auto* missionWidget = new QWidget;
    missionWidget->setObjectName(QStringLiteral("missionCard"));
    missionWidget->setAttribute(Qt::WA_StyledBackground, true);
    missionWidget->setFixedWidth(420);
    missionWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    auto* mwLay = new QVBoxLayout(missionWidget);
    mwLay->setContentsMargins(22, 18, 22, 20);
    mwLay->setSpacing(0);

    // Header: label + status badge
    auto* mHeader = new QHBoxLayout;
    mHeader->setSpacing(10);
    auto* mLabel = new QLabel(QStringLiteral("CURRENT MISSION"));
    mLabel->setObjectName(QStringLiteral("missionCardLabel"));
    mHeader->addWidget(mLabel);
    mHeader->addStretch(1);
    m_missionStatus = new PillBadge(QStringLiteral("Idle"), QColor(QStringLiteral("#8b949e")), missionWidget);
    mHeader->addWidget(m_missionStatus);
    mwLay->addLayout(mHeader);
    mwLay->addSpacing(14);

    // Mission name
    m_missionName = new QLabel(QStringLiteral("Autopilot"));
    m_missionName->setObjectName(QStringLiteral("missionName"));
    mwLay->addWidget(m_missionName);
    mwLay->addSpacing(8);

    // Summary message
    m_missionSummary = new QLabel(QStringLiteral("Waiting for activity."));
    m_missionSummary->setObjectName(QStringLiteral("missionSummary"));
    m_missionSummary->setWordWrap(true);
    mwLay->addWidget(m_missionSummary);
    mwLay->addSpacing(16);

    auto* progressHeader = new QHBoxLayout;
    progressHeader->setSpacing(10);
    auto* progressLabel = new QLabel(QStringLiteral("Workflow progress"));
    progressLabel->setObjectName(QStringLiteral("missionMetaLabel"));
    progressHeader->addWidget(progressLabel);
    progressHeader->addStretch(1);
    m_missionPct = new QLabel(QStringLiteral("0.0% complete | ETA unavailable"));
    m_missionPct->setObjectName(QStringLiteral("missionMetaValue"));
    progressHeader->addWidget(m_missionPct);
    mwLay->addLayout(progressHeader);
    mwLay->addSpacing(8);

    m_missionProgressBar = new GradientProgressBar;
    m_missionProgressBar->setFixedHeight(14);
    m_missionProgressBar->setValue(0.0);
    m_missionProgressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    mwLay->addWidget(m_missionProgressBar);
    mwLay->addSpacing(14);

    auto* footerRow = new QHBoxLayout;
    footerRow->setSpacing(12);
    auto* metaCol = new QVBoxLayout;
    metaCol->setSpacing(4);
    auto* updatedLabel = new QLabel(QStringLiteral("Last update"));
    updatedLabel->setObjectName(QStringLiteral("missionMetaLabel"));
    metaCol->addWidget(updatedLabel);
    m_missionUpdated = new QLabel(QStringLiteral("Updated recently"));
    m_missionUpdated->setObjectName(QStringLiteral("missionMetaValue"));
    metaCol->addWidget(m_missionUpdated);
    footerRow->addLayout(metaCol, 1);
    footerRow->addStretch(1);

    m_stopBtn = new QPushButton(QStringLiteral("\u25A0  Stop"));
    m_stopBtn->setObjectName(QStringLiteral("missionStopBtn"));
    m_stopBtn->setFixedHeight(50);
    m_stopBtn->setMinimumWidth(144);
    m_stopBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_stopBtn->setVisible(false);
    connect(m_stopBtn, &QPushButton::clicked, this, [this]() {
        if (m_currentPrimaryJob == QStringLiteral("autopilot")) {
            m_api->postAction(QStringLiteral("/api/actions/autopilot/stop"));
        } else if (m_currentPrimaryJob == QStringLiteral("training")) {
            m_api->postAction(QStringLiteral("/api/actions/train/stop"));
        } else if (m_currentPrimaryJob == QStringLiteral("inference")) {
            m_api->postAction(QStringLiteral("/api/actions/inference/stop"));
        }
    });
    footerRow->addWidget(m_stopBtn, 0, Qt::AlignRight | Qt::AlignVCenter);
    mwLay->addLayout(footerRow);

    headerRow->addWidget(missionWidget, 0, Qt::AlignTop);
    mainLay->addLayout(headerRow);

    // Hero metric cards
    auto* heroRow = new QHBoxLayout;
    heroRow->setSpacing(12);

    m_heroStatus = new HeroMetricCard(
        QStringLiteral("\u2665"),  // heart / pulse
        QColor(QStringLiteral("#22c55e")),
        QStringLiteral("Training Status"),
        this);
    m_heroStatus->setValue(QStringLiteral("Idle"));
    m_heroStatus->setChip(QStringLiteral("Epoch 0/0"), QColor(QStringLiteral("#22c55e")));

    m_heroLoss = new HeroMetricCard(
        QStringLiteral("\u2197"),  // trend arrow
        QColor(QStringLiteral("#3b82f6")),
        QStringLiteral("Current Loss"),
        this);
    m_heroLoss->setValue(QStringLiteral("--"));
    m_heroLoss->setChip(QStringLiteral("0%"), QColor(QStringLiteral("#3b82f6")));

    m_heroAccuracy = new HeroMetricCard(
        QStringLiteral("\u26A1"),  // lightning
        QColor(QStringLiteral("#eab308")),
        QStringLiteral("Accuracy"),
        this);
    m_heroAccuracy->setValue(QStringLiteral("--"));
    m_heroAccuracy->setChip(QStringLiteral("0%"), QColor(QStringLiteral("#eab308")));

    m_heroEta = new HeroMetricCard(
        QStringLiteral("\u23F1"),  // clock
        QColor(QStringLiteral("#ef4444")),
        QStringLiteral("Time Remaining"),
        this);
    m_heroEta->setValue(QStringLiteral("--"));
    m_heroEta->setChip(QStringLiteral("Est. completion"), QColor(QStringLiteral("#ef4444")));

    heroRow->addWidget(m_heroStatus, 1);
    heroRow->addWidget(m_heroLoss, 1);
    heroRow->addWidget(m_heroAccuracy, 1);
    heroRow->addWidget(m_heroEta, 1);
    mainLay->addLayout(heroRow);
}

// ---------------------------------------------------------------------------
// Progress / metric cards  (~60 lines)
// ---------------------------------------------------------------------------
void OverviewPage::buildMetricCards(QVBoxLayout* mainLay)
{
    // Metric cards row
    auto* metricRow = new QHBoxLayout;
    metricRow->setSpacing(12);

    auto makeMetric = [this](const QString& label, QLabel*& valueOut) -> GlowCard* {
        auto* card = new GlowCard(label);
        valueOut = new QLabel(QStringLiteral("--"));
        valueOut->setObjectName(QStringLiteral("summaryChipValue"));
        card->contentLayout()->addWidget(valueOut);
        return card;
    };

    metricRow->addWidget(makeMetric(QStringLiteral("STEP"),      m_metricStep),   1);
    metricRow->addWidget(makeMetric(QStringLiteral("LOSS"),      m_metricLoss),   1);
    metricRow->addWidget(makeMetric(QStringLiteral("REWARD"),    m_metricReward), 1);
    metricRow->addWidget(makeMetric(QStringLiteral("EVAL MEAN"), m_metricEval),   1);
    mainLay->addLayout(metricRow);
}

// ---------------------------------------------------------------------------
// Charts  (~80 lines)
// ---------------------------------------------------------------------------
void OverviewPage::buildChartSection(QVBoxLayout* mainLay)
{
    // â”€â”€ Time range selector â”€â”€
    auto* rangeRow = new QHBoxLayout;
    rangeRow->setSpacing(0);

    auto* rangeLabel = new QLabel(QStringLiteral("Time Range"));
    rangeLabel->setObjectName(QStringLiteral("dimText"));
    rangeRow->addWidget(rangeLabel);
    rangeRow->addSpacing(10);

    m_rangeGroup = new QButtonGroup(this);
    m_rangeGroup->setExclusive(true);

    // points at ~1.8s poll: 1mâ‰ˆ33, 5mâ‰ˆ167, 30mâ‰ˆ1000, 1hâ‰ˆ2000, All=0
    struct RangeOption { QString label; int points; };
    const QList<RangeOption> ranges = {
        {QStringLiteral("1m"),  33},
        {QStringLiteral("5m"),  167},
        {QStringLiteral("30m"), 1000},
        {QStringLiteral("1h"),  2000},
        {QStringLiteral("All"), 0},
    };

    for (int i = 0; i < ranges.size(); ++i) {
        auto* btn = new QPushButton(ranges[i].label);
        btn->setObjectName(QStringLiteral("rangeBtn"));
        btn->setCheckable(true);
        btn->setFixedHeight(26);
        btn->setFixedWidth(42);
        m_rangeGroup->addButton(btn, ranges[i].points);
        rangeRow->addWidget(btn);
    }

    // Default to 5m
    if (auto* defaultBtn = m_rangeGroup->button(167)) {
        defaultBtn->setChecked(true);
    }
    m_chartVisiblePoints = 167;

    connect(m_rangeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_chartVisiblePoints = id;
        updateCharts();
    });

    rangeRow->addStretch(1);
    mainLay->addLayout(rangeRow);

    // â”€â”€ Chart cards â”€â”€
    auto makeChartCard = [this](
        const QString& title,
        const QString& desc,
        TrendChartWidget*& chartOut,
        QLabel*& summaryOut) -> GlowCard*
    {
        auto* card = new GlowCard(title);
        auto* lay = card->contentLayout();

        auto* descLbl = new QLabel(desc);
        descLbl->setObjectName(QStringLiteral("controlDescription"));
        descLbl->setWordWrap(true);
        lay->addWidget(descLbl);

        chartOut = new TrendChartWidget;
        chartOut->setMinimumHeight(120);
        lay->addWidget(chartOut);

        summaryOut = new QLabel(QStringLiteral("--"));
        summaryOut->setObjectName(QStringLiteral("dimText"));
        lay->addWidget(summaryOut);

        return card;
    };

    auto* chartRow = new QHBoxLayout;
    chartRow->setSpacing(12);

    chartRow->addWidget(
        makeChartCard(
            QStringLiteral("Training Loss Trend"),
            QStringLiteral("Recent loss movement from the active training stream."),
            m_lossChart, m_lossChartSummary),
        1);

    chartRow->addWidget(
        makeChartCard(
            QStringLiteral("Reward & Progress"),
            QStringLiteral("Reward shaping signal against mission progress over time."),
            m_signalChart, m_signalChartSummary),
        1);

    chartRow->addWidget(
        makeChartCard(
            QStringLiteral("Runtime Load"),
            QStringLiteral("GPU activity and memory pressure sampled from recent dashboard polls."),
            m_runtimeChart, m_runtimeChartSummary),
        1);

    // Chart colors: Loss=pink/magenta  Signal=green/blue  Runtime=yellow/green
    if (m_lossChart)    m_lossChart->setColors(QColor(QStringLiteral("#ec4899")));
    if (m_signalChart)  m_signalChart->setColors(QColor(QStringLiteral("#19d39a")), QColor(QStringLiteral("#4da9ff")));
    if (m_runtimeChart) m_runtimeChart->setColors(QColor(QStringLiteral("#f59e0b")), QColor(QStringLiteral("#19d39a")));

    mainLay->addLayout(chartRow);
}

QVector<double> OverviewPage::sliceForRange(const QVector<double>& full) const
{
    if (m_chartVisiblePoints <= 0 || full.size() <= m_chartVisiblePoints) {
        return full;
    }
    return full.mid(full.size() - m_chartVisiblePoints);
}

void OverviewPage::updateCharts()
{
    if (m_lossChart)
        m_lossChart->setSeries(sliceForRange(m_lossHistory), {}, QStringLiteral("LOSS"));
    if (m_signalChart) {
        const bool hasReward = !m_rewardHistory.isEmpty();
        const auto primary = sliceForRange(hasReward ? m_rewardHistory : m_progressHistory);
        const auto secondary = sliceForRange(hasReward ? m_progressHistory : QVector<double>{});
        m_signalChart->setSeries(
            primary, secondary,
            hasReward ? QStringLiteral("REWARD") : QStringLiteral("PROGRESS"),
            hasReward ? QStringLiteral("PROGRESS") : QString());
    }
    if (m_runtimeChart)
        m_runtimeChart->setSeries(sliceForRange(m_gpuLoadHistory), sliceForRange(m_vramHistory),
                                  QStringLiteral("GPU"), QStringLiteral("VRAM"));
}

// ---------------------------------------------------------------------------
// Control grid  (~120 lines)
// ---------------------------------------------------------------------------
void OverviewPage::buildAutopilotMissionCard(QVBoxLayout* parentLay)
{
    auto* sectionLabel = new QLabel(QStringLiteral("Autopilot Mission"));
    sectionLabel->setObjectName(QStringLiteral("sectionTitle"));
    parentLay->addWidget(sectionLabel);

    auto* sectionSub = new QLabel(
        QStringLiteral("A live timeline of the full workflow. Click a completed or current block to resume the mission from that stage."));
    sectionSub->setObjectName(QStringLiteral("pageSubtitle"));
    sectionSub->setWordWrap(true);
    parentLay->addWidget(sectionSub);
    parentLay->addSpacing(6);

    auto* card = new GlowCard(QStringLiteral("Autopilot Command Deck"));
    card->setGlowColor(QColor(QStringLiteral("#7c3aed")));
    auto* lay = card->contentLayout();
    lay->setSpacing(12);

    auto* headerRow = new QHBoxLayout;
    headerRow->setSpacing(16);

    auto* summaryCol = new QVBoxLayout;
    summaryCol->setSpacing(6);
    m_autopilotMissionState = new QLabel(QStringLiteral("Idle"));
    m_autopilotMissionState->setObjectName(QStringLiteral("autopilotMissionState"));
    summaryCol->addWidget(m_autopilotMissionState);

    m_autopilotMissionSummary = new QLabel(QStringLiteral("Autopilot is waiting for the next instruction."));
    m_autopilotMissionSummary->setObjectName(QStringLiteral("autopilotMissionSummary"));
    m_autopilotMissionSummary->setWordWrap(true);
    summaryCol->addWidget(m_autopilotMissionSummary);

    headerRow->addLayout(summaryCol, 1);

    auto* actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_autopilotLaunchBtn = new QPushButton(QStringLiteral("Launch Smart Run"));
    m_autopilotLaunchBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
    m_autopilotLaunchBtn->setMinimumHeight(44);
    m_autopilotLaunchBtn->setMinimumWidth(196);
    m_autopilotLaunchBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_autopilotLaunchBtn->setProperty("actionPath", QStringLiteral("/api/actions/autopilot/start"));
    connect(m_autopilotLaunchBtn, &QPushButton::clicked, this, [this]() {
        const QString actionPath = m_autopilotLaunchBtn
            ? m_autopilotLaunchBtn->property("actionPath").toString()
            : QStringLiteral("/api/actions/autopilot/start");
        m_api->postAction(actionPath.isEmpty() ? QStringLiteral("/api/actions/autopilot/start") : actionPath);
    });
    actionRow->addWidget(m_autopilotLaunchBtn);

    m_autopilotPauseBtn = new QPushButton(QStringLiteral("Pause"));
    m_autopilotPauseBtn->setObjectName(QStringLiteral("actionBtn"));
    m_autopilotPauseBtn->setMinimumHeight(40);
    m_autopilotPauseBtn->setMinimumWidth(112);
    m_autopilotPauseBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_autopilotPauseBtn, &QPushButton::clicked, this, [this]() {
        m_api->postAction(QStringLiteral("/api/actions/autopilot/pause"));
    });
    actionRow->addWidget(m_autopilotPauseBtn);

    m_autopilotResumeBtn = new QPushButton(QStringLiteral("Resume"));
    m_autopilotResumeBtn->setObjectName(QStringLiteral("actionBtn"));
    m_autopilotResumeBtn->setMinimumHeight(40);
    m_autopilotResumeBtn->setMinimumWidth(112);
    m_autopilotResumeBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_autopilotResumeBtn, &QPushButton::clicked, this, [this]() {
        m_api->postAction(QStringLiteral("/api/actions/autopilot/resume"));
    });
    actionRow->addWidget(m_autopilotResumeBtn);

    auto* actionWrap = new QVBoxLayout;
    actionWrap->setContentsMargins(0, 0, 0, 0);
    actionWrap->setSpacing(0);
    actionWrap->addLayout(actionRow);
    headerRow->addLayout(actionWrap, 0);
    lay->addLayout(headerRow);

    auto* infoRow = new QHBoxLayout;
    infoRow->setSpacing(10);
    m_autopilotMissionMeta = new QLabel(QStringLiteral("Next recommended stage: Environment"));
    m_autopilotMissionMeta->setObjectName(QStringLiteral("autopilotMissionMeta"));
    m_autopilotMissionMeta->setWordWrap(true);
    m_autopilotMissionMeta->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    infoRow->addWidget(m_autopilotMissionMeta, 0);

    m_autopilotMissionRecovery = new QLabel(QStringLiteral("Recovery window will appear here when a block becomes active."));
    m_autopilotMissionRecovery->setObjectName(QStringLiteral("autopilotMissionRecovery"));
    m_autopilotMissionRecovery->setWordWrap(true);
    m_autopilotMissionRecovery->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    infoRow->addWidget(m_autopilotMissionRecovery, 1);
    lay->addLayout(infoRow);

    auto* timelineWrap = new QWidget;
    timelineWrap->setObjectName(QStringLiteral("autopilotTimelineWrap"));
    timelineWrap->setAttribute(Qt::WA_StyledBackground, true);
    auto* timelineRow = new QHBoxLayout(timelineWrap);
    timelineRow->setContentsMargins(0, 0, 0, 0);
    timelineRow->setSpacing(10);

    const QStringList stageIds = {
        QStringLiteral("setup"),
        QStringLiteral("prepare"),
        QStringLiteral("training"),
        QStringLiteral("evaluate"),
    };

    for (int i = 0; i < stageIds.size(); ++i) {
        if (i > 0) {
            auto* connector = new QWidget;
            connector->setObjectName(QStringLiteral("autopilotTimelineConnector"));
            connector->setAttribute(Qt::WA_StyledBackground, true);
            connector->setFixedHeight(4);
            connector->setMinimumWidth(22);
            connector->setMaximumWidth(28);
            timelineRow->addWidget(connector, 0, Qt::AlignVCenter);
            m_autopilotTimelineConnectors.append(connector);
        }

        AutopilotStageBlock block;
        block.stageId = stageIds[i];
        block.button = new QPushButton;
        block.button->setObjectName(QStringLiteral("autopilotStageButton"));
        block.button->setCursor(Qt::PointingHandCursor);
        block.button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        block.button->setMinimumHeight(146);
        block.button->setProperty("state", QStringLiteral("pending"));
        block.button->setProperty("clickable", false);

        auto* blockLay = new QVBoxLayout(block.button);
        blockLay->setContentsMargins(16, 14, 16, 14);
        blockLay->setSpacing(6);

        auto* headRow = new QHBoxLayout;
        headRow->setSpacing(8);
        block.stepLabel = new QLabel(QStringLiteral("%1  /  4").arg(i + 1));
        block.stepLabel->setObjectName(QStringLiteral("autopilotStageStep"));
        block.stepLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        headRow->addWidget(block.stepLabel);
        headRow->addStretch(1);
        block.stateLabel = new QLabel(QStringLiteral("Pending"));
        block.stateLabel->setObjectName(QStringLiteral("autopilotStageState"));
        block.stateLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        headRow->addWidget(block.stateLabel);
        blockLay->addLayout(headRow);

        block.titleLabel = new QLabel(autopilotStageTitle(block.stageId));
        block.titleLabel->setObjectName(QStringLiteral("autopilotStageTitle"));
        block.titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        blockLay->addWidget(block.titleLabel);

        block.detailLabel = new QLabel(autopilotStageDetail(block.stageId));
        block.detailLabel->setObjectName(QStringLiteral("autopilotStageDetail"));
        block.detailLabel->setWordWrap(true);
        block.detailLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        blockLay->addWidget(block.detailLabel);
        blockLay->addSpacing(2);
        blockLay->addStretch(1);

        block.progressLabel = new QLabel(QStringLiteral("0.0%"));
        block.progressLabel->setObjectName(QStringLiteral("autopilotStageProgress"));
        block.progressLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        blockLay->addWidget(block.progressLabel);

        block.progressBar = new GradientProgressBar;
        block.progressBar->setFixedHeight(12);
        block.progressBar->setValue(0.0);
        block.progressBar->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        blockLay->addWidget(block.progressBar);

        const QString stageId = block.stageId;
        connect(block.button, &QPushButton::clicked, this, [this, stageId]() {
            const QString actionPath = sender()
                ? sender()->property("actionPath").toString()
                : QString();
            m_api->postAction(actionPath.isEmpty() ? autopilotContinueEndpoint(stageId) : actionPath);
        });

        timelineRow->addWidget(block.button, 1);
        m_autopilotTimeline.append(block);
    }

    lay->addWidget(timelineWrap);

    m_autopilotMissionHint = new QLabel(
        QStringLiteral("Click any completed or active block to restart the mission from that stage."));
    m_autopilotMissionHint->setObjectName(QStringLiteral("autopilotMissionHint"));
    m_autopilotMissionHint->setWordWrap(true);
    lay->addWidget(m_autopilotMissionHint);

    parentLay->addWidget(card);
}

void OverviewPage::buildControlGrid(QVBoxLayout* parentLay)
{
    buildAutopilotMissionCard(parentLay);
    parentLay->addSpacing(10);

    m_advancedControlsWrap = new QWidget;
    auto* advancedLay = new QVBoxLayout(m_advancedControlsWrap);
    advancedLay->setContentsMargins(0, 0, 0, 0);
    advancedLay->setSpacing(12);

    auto* sectionLabel = new QLabel(QStringLiteral("Advanced Controls"));
    sectionLabel->setObjectName(QStringLiteral("sectionTitle"));
    advancedLay->addWidget(sectionLabel);

    auto* sectionSub = new QLabel(
        QStringLiteral("Only specialist controls stay here. The main workflow now lives entirely in the autopilot timeline."));
    sectionSub->setObjectName(QStringLiteral("pageSubtitle"));
    sectionSub->setWordWrap(true);
    advancedLay->addWidget(sectionSub);

    m_judgeCard = new GlowCard(QStringLiteral("Large Judge"));
    {
        auto* jLay = m_judgeCard->contentLayout();
        m_judgeCard->setGlowColor(QColor(QStringLiteral("#eab308")));

        auto* toggleRow = new QHBoxLayout;
        m_judgeLabel = new QLabel(QStringLiteral("Enabled"));
        m_judgeLabel->setObjectName(QStringLiteral("accentText"));
        toggleRow->addWidget(m_judgeLabel);
        toggleRow->addStretch(1);
        jLay->addLayout(toggleRow);

        m_judgeModelLabel = new QLabel(QStringLiteral("Loaded model: --"));
        m_judgeModelLabel->setObjectName(QStringLiteral("controlDescription"));
        m_judgeModelLabel->setWordWrap(true);
        jLay->addWidget(m_judgeModelLabel);

        auto* detail = new QLabel(QStringLiteral("Use the stronger frozen local judge for periodic quality scoring and reward shaping."));
        detail->setObjectName(QStringLiteral("controlMeta"));
        detail->setWordWrap(true);
        jLay->addWidget(detail);

        jLay->addStretch(1);

        m_judgeToggleBtn = new QPushButton(QStringLiteral("Disable Large Judge"));
        m_judgeToggleBtn->setObjectName(QStringLiteral("actionBtnDanger"));
        m_judgeToggleBtn->setMinimumHeight(36);
        connect(m_judgeToggleBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/large_judge/toggle"));
        });
        jLay->addWidget(m_judgeToggleBtn);
        m_judgeCard->setMinimumHeight(184);
    }
    advancedLay->addWidget(m_judgeCard);
    parentLay->addWidget(m_advancedControlsWrap);
    if (m_advancedControlsWrap) {
        m_advancedControlsWrap->setVisible(false);
    }
}

void OverviewPage::updateAutopilotMission(const QJsonObject& state,
                                          const QJsonObject& jobs,
                                          const QJsonObject& processes,
                                          const QString& primaryJobId,
                                          const QString& primaryStage,
                                          const QString& primaryMsg,
                                          double primaryProg,
                                          const QString& primaryEta,
                                          double primaryTs)
{
    Q_UNUSED(primaryStage);
    const QJsonObject autopilot = state.value(QStringLiteral("autopilot")).toObject();
    const QJsonObject recovery = state.value(QStringLiteral("recovery")).toObject();
    const QString recoveryTooltip = recovery.value(QStringLiteral("tooltip")).toString();
    const QString pauseTooltip = recovery.value(QStringLiteral("pause_tooltip")).toString(recoveryTooltip);
    const QString resumeTooltip = recovery.value(QStringLiteral("resume_tooltip")).toString();
    const double pauseLossSeconds = recovery.value(QStringLiteral("pause_loss_seconds")).toDouble(0.0);
    const double workSinceRecoverySeconds = recovery.value(QStringLiteral("work_since_last_recovery_seconds")).toDouble(0.0);
    const QString lastRecoveryLabel = recovery.value(QStringLiteral("last_recovery_label")).toString();
    const QString nextRecoveryLabel = recovery.value(QStringLiteral("next_recovery_label")).toString();
    const QJsonValue nextRecoveryEtaValue = recovery.value(QStringLiteral("next_recovery_eta_seconds"));
    const double nextRecoveryEta = nextRecoveryEtaValue.isDouble()
        ? nextRecoveryEtaValue.toDouble(-1.0)
        : -1.0;
    const bool autopilotActive = autopilot.value(QStringLiteral("active")).toBool(false);
    const bool autopilotPaused = autopilot.value(QStringLiteral("paused")).toBool(false);
    QString autopilotStage = normalizeAutopilotStage(autopilot.value(QStringLiteral("stage")).toString());
    const QString autopilotMessage = autopilot.value(QStringLiteral("message")).toString();
    const QString controlJob = recovery.value(QStringLiteral("job")).toString();
    const QJsonObject controlProcess = processes.value(controlJob).toObject();
    const QJsonObject controlJobSummary = jobs.value(controlJob).toObject();
    const double controlJobProgress = qBound(0.0, controlJobSummary.value(QStringLiteral("progress")).toDouble(0.0), 1.0);
    const bool controlRunning = recovery.value(QStringLiteral("running")).toBool(false)
        || controlProcess.value(QStringLiteral("running")).toBool(false);
    const bool controlPaused = recovery.value(QStringLiteral("paused")).toBool(false)
        || controlProcess.value(QStringLiteral("paused")).toBool(false)
        || controlJobSummary.value(QStringLiteral("stage")).toString() == QStringLiteral("paused");
    const bool canPause = recovery.value(QStringLiteral("can_pause")).toBool(controlRunning);
    const bool canResume = recovery.value(QStringLiteral("can_resume")).toBool(controlPaused || (autopilotActive && autopilotPaused));
    const QString recommendedStage = recommendedAutopilotStageFromJobs(jobs);

    if (!controlJob.isEmpty() && (controlRunning || controlPaused || canResume)) {
        autopilotStage = normalizeAutopilotStage(controlJob);
    } else if (autopilotStage.isEmpty()
        || autopilotStage == QStringLiteral("idle")
        || autopilotStage == QStringLiteral("completed")
        || autopilotStage == QStringLiteral("failed")
        || autopilotStage == QStringLiteral("stopped")) {
        autopilotStage = recommendedStage;
    }

    const int activeIndex = autopilotStageIndex(autopilotStage);
    const int recommendedIndex = autopilotStageIndex(recommendedStage);

    QString missionStateText = QStringLiteral("Idle");
    if (controlPaused || (autopilotActive && autopilotPaused)) {
        missionStateText = QStringLiteral("Paused At %1").arg(autopilotStageTitle(autopilotStage));
    } else if (controlRunning || autopilotActive) {
        missionStateText = QStringLiteral("Running %1").arg(autopilotStageTitle(autopilotStage));
    } else if (canResume && !controlJob.isEmpty()) {
        missionStateText = QStringLiteral("Resume %1").arg(autopilotStageTitle(autopilotStage));
    } else if (autopilot.value(QStringLiteral("stage")).toString() == QStringLiteral("failed")) {
        missionStateText = QStringLiteral("Needs Attention");
    } else if (jobs.value(QStringLiteral("evaluate")).toObject().value(QStringLiteral("stage")).toString() == QStringLiteral("completed")) {
        missionStateText = QStringLiteral("Pipeline Complete");
    } else {
        missionStateText = QStringLiteral("Ready For %1").arg(autopilotStageTitle(recommendedStage));
    }

    QString summaryText = autopilotMessage;
    if ((controlRunning || controlPaused || canResume) && !controlJobSummary.value(QStringLiteral("message")).toString().trimmed().isEmpty()) {
        summaryText = controlJobSummary.value(QStringLiteral("message")).toString();
    } else if (summaryText.trimmed().isEmpty()) {
        summaryText = QStringLiteral("Autopilot will continue from %1 and chain the remaining stages automatically.")
            .arg(autopilotStageTitle(recommendedStage));
    }
    if (!autopilotActive
        && !(controlRunning || controlPaused || canResume)
        && primaryJobId != QStringLiteral("autopilot")
        && !primaryMsg.trimmed().isEmpty()) {
        summaryText = QStringLiteral("%1 Current workflow: %2")
            .arg(summaryText, primaryMsg);
    }

    if (m_autopilotMissionState) {
        m_autopilotMissionState->setText(missionStateText);
    }
    if (m_autopilotMissionSummary) {
        m_autopilotMissionSummary->setText(summaryText);
    }
    if (m_autopilotMissionMeta) {
        QString meta = QStringLiteral("Next stage: %1").arg(autopilotStageTitle(recommendedStage));
        if (!controlJob.isEmpty() && (controlRunning || controlPaused || canResume)) {
            meta = QStringLiteral("Current block: %1 | %2 block progress")
                .arg(Fmt::prettyJobName(controlJob), Fmt::progressPct(controlJobProgress));
        } else if (primaryJobId != QStringLiteral("autopilot")) {
            meta = QStringLiteral("Primary workflow: %1 | %2 complete")
                .arg(Fmt::prettyJobName(primaryJobId), Fmt::progressPct(primaryProg));
        }
        m_autopilotMissionMeta->setText(meta);
    }
    if (m_autopilotMissionRecovery) {
        QString recoveryText = QStringLiteral("Recovery will appear here when the current block becomes resumable.");
        if (recovery.value(QStringLiteral("available")).toBool(false)) {
            const QString windowText = recoveryWindowText(workSinceRecoverySeconds);
            const QString nextEtaText = nextRecoveryEta >= 0.0
                ? recoveryWindowText(nextRecoveryEta)
                : QStringLiteral("timing stabilizing");
            recoveryText = QStringLiteral("Latest recovery point: %1 | current window %2 | next point %3")
                .arg(lastRecoveryLabel.isEmpty() ? QStringLiteral("latest state") : lastRecoveryLabel,
                     windowText,
                     nextRecoveryLabel.isEmpty() ? nextEtaText : QStringLiteral("%1 in %2").arg(nextRecoveryLabel, nextEtaText));
            if (canPause) {
                recoveryText = QStringLiteral("Pause now: lose about %1 | %2")
                    .arg(recoveryWindowText(pauseLossSeconds), recoveryText);
            } else if (canResume) {
                recoveryText = QStringLiteral("Resume available for %1 | %2")
                    .arg(Fmt::prettyJobName(controlJob), recoveryText);
            }
        }
        m_autopilotMissionRecovery->setText(recoveryText);
    }
    if (m_autopilotMissionHint) {
        const QString hint = QStringLiteral("Resume continues the current failed or paused block. Click a completed block to restart the mission from there. %1 | %2")
            .arg(Fmt::relativeTime(primaryTs),
                 (primaryEta.isEmpty() || primaryEta == QStringLiteral("unknown")) ? QStringLiteral("ETA unavailable") : primaryEta);
        m_autopilotMissionHint->setText(hint);
    }

    if (m_autopilotLaunchBtn) {
        if (autopilotActive) {
            m_autopilotLaunchBtn->setText(QStringLiteral("Restart Smart Run"));
            m_autopilotLaunchBtn->setProperty("actionPath", QStringLiteral("/api/actions/autopilot/start"));
        } else if (canResume) {
            m_autopilotLaunchBtn->setText(QStringLiteral("Resume Smart Run"));
            m_autopilotLaunchBtn->setProperty("actionPath", QStringLiteral("/api/actions/autopilot/resume"));
        } else {
            m_autopilotLaunchBtn->setText(QStringLiteral("Launch Smart Run"));
            m_autopilotLaunchBtn->setProperty("actionPath", QStringLiteral("/api/actions/autopilot/start"));
        }
        Fmt::repolish(m_autopilotLaunchBtn);
    }
    if (m_autopilotPauseBtn) {
        m_autopilotPauseBtn->setText(QStringLiteral("Pause"));
        m_autopilotPauseBtn->setEnabled(canPause);
        m_autopilotPauseBtn->setToolTip(pauseTooltip.isEmpty()
            ? QStringLiteral("Pause stops the active block and keeps it resumable from the latest recovery point inside that block.")
            : pauseTooltip);
    }
    if (m_autopilotResumeBtn) {
        m_autopilotResumeBtn->setText(QStringLiteral("Resume"));
        m_autopilotResumeBtn->setEnabled(canResume);
        m_autopilotResumeBtn->setToolTip(resumeTooltip.isEmpty()
            ? QStringLiteral("Resume continues the active block from the latest recovery point inside that block.")
            : resumeTooltip);
    }

    for (auto& block : m_autopilotTimeline) {
        const QString jobName = autopilotStageJobName(block.stageId);
        const QJsonObject job = jobs.value(jobName).toObject();
        const QJsonObject process = processes.value(jobName).toObject();
        const QString stage = job.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
        const QString normalizedJobStage = normalizeAutopilotStage(stage);
        const double jobProgress = job.value(QStringLiteral("progress")).toDouble(0.0);
        const QString jobMessage = job.value(QStringLiteral("message")).toString();
        const bool jobRunning = process.value(QStringLiteral("running")).toBool(false);
        const bool jobPaused = process.value(QStringLiteral("paused")).toBool(false);
        const int index = autopilotStageIndex(block.stageId);
        bool prerequisitesComplete = true;
        for (int prev = 0; prev < index; ++prev) {
            const QString prevStage = m_autopilotTimeline.value(prev).stageId;
            const QString prevJobName = autopilotStageJobName(prevStage);
            const QString prevStageState = jobs.value(prevJobName).toObject().value(QStringLiteral("stage")).toString();
            if (prevStageState != QStringLiteral("completed") && !(autopilotActive && prev < activeIndex)) {
                prerequisitesComplete = false;
                break;
            }
        }

        QString uiState = QStringLiteral("pending");
        QString stateText = QStringLiteral("Pending");
        bool clickable = false;
        double displayProgress = 0.0;
        QString actionPath = autopilotContinueEndpoint(block.stageId);

        if (autopilotActive && index < activeIndex) {
            uiState = QStringLiteral("done");
            stateText = QStringLiteral("Done");
            clickable = true;
            displayProgress = 1.0;
        } else if (autopilotActive && index == activeIndex) {
            uiState = autopilotPaused ? QStringLiteral("paused") : QStringLiteral("active");
            stateText = autopilotPaused ? QStringLiteral("Paused") : QStringLiteral("Running");
            clickable = true;
            displayProgress = jobProgress;
        } else if (autopilotActive && index > activeIndex) {
            uiState = QStringLiteral("queued");
            stateText = QStringLiteral("Queued");
            clickable = false;
            displayProgress = 0.0;
        } else if (stage == QStringLiteral("completed")) {
            uiState = QStringLiteral("done");
            stateText = QStringLiteral("Done");
            clickable = true;
            displayProgress = 1.0;
        } else if (canResume && controlJob == jobName && stage == QStringLiteral("failed")) {
            uiState = QStringLiteral("failed");
            stateText = QStringLiteral("Resume");
            clickable = true;
            displayProgress = qMax(0.08, jobProgress);
            actionPath = QStringLiteral("/api/actions/autopilot/resume");
        } else if (stage == QStringLiteral("failed")) {
            uiState = QStringLiteral("failed");
            stateText = QStringLiteral("Failed");
            clickable = true;
            displayProgress = qMax(0.08, jobProgress);
        } else if (jobPaused || stage == QStringLiteral("paused")) {
            uiState = QStringLiteral("paused");
            stateText = QStringLiteral("Paused");
            clickable = true;
            displayProgress = qMax(0.08, jobProgress);
            if (canResume && controlJob == jobName) {
                actionPath = QStringLiteral("/api/actions/autopilot/resume");
            }
        } else if (canResume && controlJob == jobName) {
            uiState = QStringLiteral("ready");
            stateText = QStringLiteral("Resume");
            clickable = true;
            displayProgress = qMax(0.08, jobProgress);
            actionPath = QStringLiteral("/api/actions/autopilot/resume");
        } else if (prerequisitesComplete
                   && (stage == QStringLiteral("stopped") || stage == QStringLiteral("training"))
                   && block.stageId == QStringLiteral("training")) {
            uiState = QStringLiteral("ready");
            stateText = QStringLiteral("Ready");
            clickable = true;
            displayProgress = qMax(0.08, jobProgress);
        } else if (prerequisitesComplete && index == recommendedIndex) {
            uiState = QStringLiteral("ready");
            stateText = QStringLiteral("Ready");
            clickable = true;
            displayProgress = jobProgress;
        } else if (index < recommendedIndex) {
            uiState = QStringLiteral("done");
            stateText = QStringLiteral("Done");
            clickable = true;
            displayProgress = 1.0;
        }

        if (!jobRunning && !jobPaused && stage == QStringLiteral("idle") && index > recommendedIndex && !autopilotActive) {
            uiState = QStringLiteral("pending");
            stateText = QStringLiteral("Pending");
            clickable = false;
            displayProgress = 0.0;
        }

        const QString detailText = (autopilotActive && index == activeIndex && primaryJobId == QStringLiteral("autopilot"))
            ? primaryMsg
            : (jobMessage.trimmed().isEmpty() ? autopilotStageDetail(block.stageId) : jobMessage);

        if (block.stateLabel) {
            block.stateLabel->setText(stateText);
            block.stateLabel->setProperty("state", uiState);
            Fmt::repolish(block.stateLabel);
        }
        if (block.detailLabel) {
            block.detailLabel->setText(detailText);
        }
        if (block.progressLabel) {
            QString progressStatus = QStringLiteral("Locked");
            if (uiState == QStringLiteral("done")) {
                progressStatus = QStringLiteral("Complete");
            } else if (uiState == QStringLiteral("active")) {
                progressStatus = QStringLiteral("Live");
            } else if (uiState == QStringLiteral("paused") || (uiState == QStringLiteral("ready") && displayProgress > 0.0)) {
                progressStatus = QStringLiteral("Resumable");
            } else if (clickable) {
                progressStatus = QStringLiteral("Available");
            }
            block.progressLabel->setText(QStringLiteral("%1 | %2")
                .arg(Fmt::progressPct(displayProgress), progressStatus));
        }
        if (block.progressBar) {
            block.progressBar->setValue(displayProgress);
        }
        if (block.button) {
            block.button->setEnabled(clickable);
            block.button->setProperty("state", uiState);
            block.button->setProperty("clickable", clickable);
            block.button->setProperty("actionPath", clickable ? actionPath : QString());
            block.button->setToolTip(clickable
                ? ((canResume && controlJob == jobName && !controlRunning)
                    ? QStringLiteral("Resume %1 from its latest recovery point").arg(autopilotStageTitle(block.stageId))
                    : QStringLiteral("Continue autopilot from %1").arg(autopilotStageTitle(block.stageId)))
                : QStringLiteral("Finish the previous timeline blocks first"));
            Fmt::repolish(block.button);
        }
    }

    for (int i = 0; i < m_autopilotTimelineConnectors.size(); ++i) {
        auto* connector = m_autopilotTimelineConnectors[i];
        if (!connector) {
            continue;
        }
        QString stateName = QStringLiteral("pending");
        if (autopilotActive && i < activeIndex) {
            stateName = QStringLiteral("done");
        } else if (autopilotActive && i == activeIndex - 1) {
            stateName = autopilotPaused ? QStringLiteral("paused") : QStringLiteral("active");
        } else if (!autopilotActive && i < recommendedIndex) {
            stateName = QStringLiteral("done");
        }
        connector->setProperty("state", stateName);
        Fmt::repolish(connector);
    }
}

// ---------------------------------------------------------------------------
// System rail  (~80 lines)
// ---------------------------------------------------------------------------
void OverviewPage::buildSystemRail(QVBoxLayout* rail)
{
    // Alerts card
    {
        auto* card = new GlowCard(QStringLiteral("Alerts & System"));
        auto* lay = card->contentLayout();

        auto* header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        header->setSpacing(10);
        auto* statusLabel = new QLabel(QStringLiteral("Latest health state"));
        statusLabel->setObjectName(QStringLiteral("controlDescription"));
        header->addWidget(statusLabel, 1);
        m_alertHeaderCount = new QLabel(QStringLiteral("CLEAR"));
        m_alertHeaderCount->setObjectName(QStringLiteral("alertHeaderCount"));
        m_alertHeaderCount->setProperty("severity", QStringLiteral("clear"));
        m_alertHeaderCount->setAlignment(Qt::AlignCenter);
        header->addWidget(m_alertHeaderCount);
        lay->addLayout(header);

        m_alertList = new QWidget;
        m_alertList->setObjectName(QStringLiteral("alertContainer"));
        m_alertList->setAttribute(Qt::WA_StyledBackground, true);
        auto* alertInner = new QVBoxLayout(m_alertList);
        alertInner->setContentsMargins(10, 10, 10, 10);
        alertInner->setSpacing(4);
        auto* placeholder = new QLabel(QStringLiteral("No recent alerts or warnings."));
        placeholder->setObjectName(QStringLiteral("alertInfo"));
        placeholder->setWordWrap(true);
        alertInner->addWidget(placeholder);
        lay->addWidget(m_alertList);

        card->setMinimumHeight(150);
        rail->addWidget(card);
    }

    // Hardware card
    {
        auto* card = new GlowCard(QStringLiteral("Hardware"));
        m_hwInfo = new QLabel(QStringLiteral("GPU: --\nVRAM: --\nTemp: --\nRAM: --"));
        m_hwInfo->setObjectName(QStringLiteral("systemInfo"));
        m_hwInfo->setWordWrap(true);
        card->contentLayout()->addWidget(m_hwInfo);
        card->setMinimumHeight(140);
        rail->addWidget(card);
    }

    // Process Snapshot card
    {
        auto* card = new GlowCard(QStringLiteral("Process Snapshot"));
        m_processInfo = new QLabel(
            QStringLiteral("setup: idle\nprepare: idle\ntraining: idle\nevaluate: idle\ninference: idle\nautopilot: idle"));
        m_processInfo->setObjectName(QStringLiteral("systemInfo"));
        m_processInfo->setWordWrap(true);
        card->contentLayout()->addWidget(m_processInfo);
        card->setMinimumHeight(170);
        rail->addWidget(card);
    }

    // Project Assets card
    {
        auto* card = new GlowCard(QStringLiteral("Project Assets"));
        m_storageInfo = new QLabel(
            QStringLiteral("API: --\nCheckpoint: --\nCached judge models: --\nReport: --"));
        m_storageInfo->setObjectName(QStringLiteral("systemInfo"));
        m_storageInfo->setWordWrap(true);
        card->contentLayout()->addWidget(m_storageInfo);
        card->setMinimumHeight(150);
        rail->addWidget(card);
    }
}

// ---------------------------------------------------------------------------
// setAdvancedMode
// ---------------------------------------------------------------------------
void OverviewPage::setAdvancedMode(bool advanced)
{
    if (m_advancedControlsWrap) m_advancedControlsWrap->setVisible(advanced);
}

// ---------------------------------------------------------------------------
// updateFromState  (~150 lines)
// ---------------------------------------------------------------------------
void OverviewPage::updateFromState(const QJsonObject& state)
{
    // --- Hardware ---
    const auto hw = state[QStringLiteral("hardware")].toObject();
    const QString gpuName   = hw[QStringLiteral("gpu_name")].toString();
    const int gpuUtil       = hw[QStringLiteral("gpu_utilization")].toInt();
    const int gpuMemUsed    = hw[QStringLiteral("gpu_memory_used_mb")].toInt();
    const int gpuMemTotal   = hw[QStringLiteral("gpu_memory_total_mb")].toInt();
    const int gpuTemp       = hw[QStringLiteral("gpu_temperature_c")].toInt();
    const int ramUsed       = hw[QStringLiteral("ram_used_mb")].toInt();
    const int ramTotal      = hw[QStringLiteral("ram_total_mb")].toInt();
    const double gpuMemPct  = gpuMemTotal > 0 ? (double(gpuMemUsed) / double(gpuMemTotal)) * 100.0 : 0.0;
    const double ramPct     = ramTotal > 0 ? (double(ramUsed) / double(ramTotal)) * 100.0 : 0.0;

    // --- Primary job ---
    const auto primaryValue = state[QStringLiteral("primary_job")];
    QString primaryJobId  = QStringLiteral("autopilot");
    QString primaryStage  = QStringLiteral("idle");
    QString primaryMsg    = QStringLiteral("Waiting for the next command.");
    double  primaryProg   = 0.0;
    QString primaryEta    = QStringLiteral("unknown");
    double  primaryTs     = 0.0;

    if (primaryValue.isObject()) {
        const auto pj = primaryValue.toObject();
        primaryJobId = pj[QStringLiteral("job")].toString(primaryJobId);
        primaryStage = pj[QStringLiteral("stage")].toString(primaryStage);
        primaryMsg   = pj[QStringLiteral("message")].toString(primaryMsg);
        primaryProg  = pj[QStringLiteral("progress")].toDouble(0.0);
        primaryEta   = pj[QStringLiteral("eta")].toString(primaryEta);
        const auto steps = pj[QStringLiteral("steps")].toArray();
        if (!steps.isEmpty())
            primaryTs = steps.last().toObject()[QStringLiteral("ts")].toDouble(0.0);
    }

    const QString displayStage = primaryStage.isEmpty() ? QStringLiteral("idle") : primaryStage;
    const QString displayEta   = (primaryEta.isEmpty() || primaryEta == QStringLiteral("unknown"))
                                     ? QStringLiteral("ETA unavailable") : primaryEta;
    const QString progressText = Fmt::progressPct(primaryProg);
    if (primaryTs <= 0.0) {
        primaryTs = primaryValue.toObject()[QStringLiteral("updated_at")].toDouble(0.0);
    }

    // --- Jobs / training message ---
    const auto jobs     = state[QStringLiteral("jobs")].toObject();
    const auto processes = state[QStringLiteral("processes")].toObject();
    const auto recovery = state[QStringLiteral("recovery")].toObject();
    const auto training = jobs[QStringLiteral("training")].toObject();
    const QString trainingMsg = training[QStringLiteral("message")].toString();
    const QString stepText    = Fmt::captureMatch(trainingMsg, QStringLiteral("step\\s+(\\d+)"));
    const QString lossText    = Fmt::captureMatch(trainingMsg, QStringLiteral("loss=([\\d.]+)"));
    const QString rewardText  = Fmt::captureMatch(trainingMsg, QStringLiteral("reward(?:_score)?=([\\d.]+)"));
    const QString accText     = Fmt::captureMatch(trainingMsg, QStringLiteral("acc(?:uracy)?=([\\d.]+)"));
    const QString epochCur    = Fmt::captureMatch(trainingMsg, QStringLiteral("epoch\\s+(\\d+)"));
    const QString epochTotal  = Fmt::captureMatch(trainingMsg, QStringLiteral("/(\\d+)"));
    const QString reportText  = state[QStringLiteral("report")].toString();

    // Active process count
    const int activeCount = std::count_if(
        processes.begin(), processes.end(),
        [](const QJsonValue& v) { return v.toObject().value(QStringLiteral("running")).toBool(); });
    const bool trainingPaused = processes.value(QStringLiteral("training")).toObject().value(QStringLiteral("paused")).toBool(false);
    const QString controlJobId = recovery.value(QStringLiteral("job")).toString();
    const auto controlJob = jobs.value(controlJobId).toObject();
    const bool controlCardActive = primaryJobId == QStringLiteral("autopilot")
        && !controlJobId.isEmpty()
        && (recovery.value(QStringLiteral("running")).toBool(false)
            || recovery.value(QStringLiteral("paused")).toBool(false)
            || recovery.value(QStringLiteral("can_resume")).toBool(false));
    const double controlProg = qBound(0.0, controlJob.value(QStringLiteral("progress")).toDouble(0.0), 1.0);
    const QString controlEtaRaw = controlJob.value(QStringLiteral("eta")).toString();
    const QString controlEta = (controlEtaRaw.isEmpty() || controlEtaRaw == QStringLiteral("unknown"))
        ? displayEta
        : controlEtaRaw;
    const double controlTs = controlJob.value(QStringLiteral("updated_at")).toDouble(primaryTs);
    const QString missionProgressText = Fmt::progressPct(controlCardActive ? controlProg : primaryProg);
    const QString missionEta = controlCardActive ? controlEta : displayEta;
    const double missionTs = controlCardActive ? controlTs : primaryTs;
    const OverviewTrainingSnapshot chartSnapshot = buildOverviewTrainingSnapshot(
        state,
        training,
        controlCardActive ? controlProg : primaryProg);
    const QString derivedStepText = chartSnapshot.latestStep;
    const QString derivedLossText = chartSnapshot.latestLoss;
    const QString derivedRewardText = chartSnapshot.latestReward;
    m_lossHistory = chartSnapshot.lossHistory;
    m_rewardHistory = chartSnapshot.rewardHistory;
    m_progressHistory = chartSnapshot.progressHistory;

    // --- Hero metric cards ---
    if (m_heroStatus) {
        QString statusValue = QStringLiteral("Connected");
        if (displayStage == QStringLiteral("failed")) {
            statusValue = QStringLiteral("Needs Attention");
        } else if (trainingPaused) {
            statusValue = QStringLiteral("Training Paused");
        } else if (displayStage != QStringLiteral("idle")) {
            statusValue = Fmt::prettyJobName(primaryJobId);
        } else if (activeCount > 0 && primaryJobId != QStringLiteral("autopilot")) {
            statusValue = QStringLiteral("Background Active");
        }
        const QString statusChip = !epochCur.isEmpty() && !epochTotal.isEmpty()
            ? QStringLiteral("Epoch %1/%2").arg(epochCur, epochTotal)
            : QStringLiteral("%1 | %2 active").arg(displayStage, QString::number(activeCount));
        m_heroStatus->setValue(statusValue);
        m_heroStatus->setChip(statusChip, QColor(QStringLiteral("#22c55e")));
    }
    if (m_heroLoss) {
        m_heroLoss->setValue(lossText.isEmpty() ? derivedLossText : lossText);
        m_heroLoss->setChip(
            (stepText.isEmpty() ? derivedStepText : stepText) == QStringLiteral("0")
                ? QStringLiteral("Training stream")
                : QStringLiteral("Step %1").arg(stepText.isEmpty() ? derivedStepText : stepText),
            QColor(QStringLiteral("#3b82f6")));
        if (!m_lossHistory.isEmpty() && m_lossHistory.size() > 1) {
            double prev = m_lossHistory[m_lossHistory.size() - 2];
            double cur  = m_lossHistory.last();
            double pct  = prev > 0.0 ? ((cur - prev) / prev) * 100.0 : 0.0;
            const QString glyph = pct <= 0.0 ? QStringLiteral("\u25BC") : QStringLiteral("\u25B2");
            m_heroLoss->setTrend(QStringLiteral("%1%").arg(Fmt::fmtDouble(pct, 1)), pct <= 0.0, glyph);
        }
    }
    if (m_heroAccuracy) {
        const QString evalSummary = Fmt::evalSummary(reportText);
        const QString accuracyValue = (evalSummary != QStringLiteral("--"))
            ? evalSummary
            : (accText.isEmpty() ? QStringLiteral("--") : QStringLiteral("%1%").arg(accText));
        const QString accuracyChip = (evalSummary != QStringLiteral("--"))
            ? QStringLiteral("Eval %1").arg(evalSummary)
            : ((rewardText.isEmpty() ? derivedRewardText : rewardText) == QStringLiteral("--")
                ? displayStage
                : QStringLiteral("Reward %1").arg(rewardText.isEmpty() ? derivedRewardText : rewardText));
        m_heroAccuracy->setValue(accuracyValue);
        m_heroAccuracy->setChip(accuracyChip, QColor(QStringLiteral("#eab308")));
    }
    if (m_heroEta) {
        m_heroEta->setValue(displayStage == QStringLiteral("idle") ? QStringLiteral("--") : displayEta);
        m_heroEta->setChip(QStringLiteral("Est. completion"), QColor(QStringLiteral("#ef4444")));
    }

    // --- Progress ---
    if (m_progressBar)   m_progressBar->setValue(primaryProg);
    if (m_progressLabel) {
        QString progressLabel = QStringLiteral("No active workflow");
        if (primaryJobId == QStringLiteral("training") && !epochCur.isEmpty() && !epochTotal.isEmpty()) {
            progressLabel = QStringLiteral("Epoch %1 of %2").arg(epochCur, epochTotal);
        } else if (displayStage != QStringLiteral("idle")) {
            progressLabel = QStringLiteral("%1 | %2").arg(Fmt::prettyJobName(primaryJobId), displayStage);
        }
        m_progressLabel->setText(progressLabel);
    }
    if (m_progressPct)   m_progressPct->setText(progressText);

    // --- Mission card (compact, top-right) ---
    m_currentPrimaryJob = primaryJobId;
    if (m_missionStatus) {
        QString missionStage = displayStage;
        missionStage.replace('_', ' ');
        if (!missionStage.isEmpty()) {
            missionStage[0] = missionStage[0].toUpper();
        }
        QColor missionColor(QStringLiteral("#58a6ff"));
        if (displayStage == QStringLiteral("failed")) {
            missionColor = QColor(QStringLiteral("#f85149"));
        } else if (displayStage == QStringLiteral("idle") || displayStage == QStringLiteral("stopped")) {
            missionColor = QColor(QStringLiteral("#8b949e"));
        } else if (displayStage == QStringLiteral("completed")) {
            missionColor = QColor(QStringLiteral("#3fb950"));
        }
        m_missionStatus->setText(missionStage);
        m_missionStatus->setColor(missionColor);
    }
    if (m_missionName) {
        m_missionName->setText(controlCardActive
            ? Fmt::prettyJobName(controlJobId)
            : Fmt::prettyJobName(primaryJobId));
    }
    if (m_missionSummary) {
        m_missionSummary->setText(controlCardActive
            ? Fmt::prettyJobName(primaryJobId)
            : Fmt::missionFoot(primaryJobId, displayStage, primaryMsg));
    }
    if (m_missionPct) {
        m_missionPct->setText(controlCardActive
            ? QStringLiteral("%1 block progress | %2").arg(missionProgressText, missionEta)
            : QStringLiteral("%1 complete | %2").arg(missionProgressText, missionEta));
    }
    if (m_missionProgressBar) m_missionProgressBar->setValue(controlCardActive ? controlProg : primaryProg);
    if (m_missionUpdated) m_missionUpdated->setText(Fmt::relativeTime(missionTs));
    if (m_stopBtn) {
        const bool isActive = displayStage != QStringLiteral("idle")
                              && displayStage != QStringLiteral("completed")
                              && displayStage != QStringLiteral("stopped")
                              && displayStage != QStringLiteral("failed");
        const bool canStop = m_currentPrimaryJob == QStringLiteral("training")
                             || m_currentPrimaryJob == QStringLiteral("autopilot")
                             || m_currentPrimaryJob == QStringLiteral("inference");
        m_stopBtn->setVisible(isActive && canStop);
    }

    updateAutopilotMission(state,
                           jobs,
                           processes,
                           primaryJobId,
                           displayStage,
                           primaryMsg,
                           primaryProg,
                           displayEta,
                           primaryTs);

    // --- Metric cards ---
    if (m_metricStep)   m_metricStep->setText(stepText.isEmpty() ? derivedStepText : stepText);
    if (m_metricLoss)   m_metricLoss->setText(lossText.isEmpty() ? derivedLossText : lossText);
    if (m_metricReward) m_metricReward->setText(rewardText.isEmpty() ? derivedRewardText : rewardText);
    if (m_metricEval)   m_metricEval->setText(Fmt::evalSummary(reportText));

    // --- History (charts use stream-derived series; runtime uses poll snapshots) ---
    constexpr int kMaxHistory = 2000;
    Fmt::appendHistory(m_gpuLoadHistory,  gpuUtil, kMaxHistory);
    Fmt::appendHistory(m_vramHistory,     gpuMemPct, kMaxHistory);
    Fmt::appendHistory(m_ramHistory,      ramPct, kMaxHistory);

    // --- Charts (sliced by selected time range) ---
    updateCharts();

    if (m_lossChartSummary) {
        const QVector<double> visibleLoss = sliceForRange(m_lossHistory);
        const QString lossMin = visibleLoss.isEmpty()
            ? QStringLiteral("--")
            : Fmt::fmtDouble(*std::min_element(visibleLoss.cbegin(), visibleLoss.cend()), 4);
        m_lossChartSummary->setText(QStringLiteral("Latest %1 | Step %2 | Window min %3")
            .arg(lossText.isEmpty() ? derivedLossText : lossText,
                 stepText.isEmpty() ? derivedStepText : stepText,
                 lossMin));
    }
    if (m_signalChartSummary) {
        const QString signalRewardText = rewardText.isEmpty() ? derivedRewardText : rewardText;
        m_signalChartSummary->setText(QStringLiteral("%1 | Mission %2 | Stage %3")
            .arg(signalRewardText == QStringLiteral("--")
                    ? QStringLiteral("Reward unavailable")
                    : QStringLiteral("Reward %1").arg(signalRewardText),
                 missionProgressText,
                 displayStage));
    }
    if (m_runtimeChartSummary) {
        m_runtimeChartSummary->setText(QStringLiteral("GPU %1 | VRAM %2 | RAM %3 | %4 C")
            .arg(QStringLiteral("%1%").arg(gpuUtil))
            .arg(QStringLiteral("%1%").arg(Fmt::fmtDouble(gpuMemPct, 1)))
            .arg(QStringLiteral("%1%").arg(Fmt::fmtDouble(ramPct, 1)))
            .arg(gpuTemp > 0 ? QString::number(gpuTemp) : QStringLiteral("--")));
    }

    // --- Judge ---
    const auto config = state[QStringLiteral("config")].toObject();
    const auto judgeConfig = config[QStringLiteral("large_judge")].toObject();
    const bool judgeEnabled = judgeConfig[QStringLiteral("enabled")].toBool(true);
    const QString judgeModelId = judgeConfig[QStringLiteral("model_id")].toString();

    if (m_judgeLabel)
        m_judgeLabel->setText(judgeEnabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
    if (m_judgeModelLabel)
        m_judgeModelLabel->setText(QStringLiteral("Loaded model: %1").arg(judgeModelId.isEmpty() ? QStringLiteral("--") : judgeModelId));
    if (m_judgeToggleBtn) {
        m_judgeToggleBtn->setObjectName(judgeEnabled ? QStringLiteral("actionBtnDanger") : QStringLiteral("actionBtnPrimary"));
        m_judgeToggleBtn->setText(judgeEnabled ? QStringLiteral("Disable Large Judge") : QStringLiteral("Enable Large Judge"));
        Fmt::repolish(m_judgeToggleBtn);
    }

    // --- Hardware info ---
    if (m_hwInfo) {
        m_hwInfo->setText(QStringLiteral("GPU: %1\nLoad: %2%\nVRAM: %3/%4 MB\nTemp: %5 C\nRAM: %6/%7 GB")
            .arg(gpuName.isEmpty() ? QStringLiteral("Unknown") : gpuName)
            .arg(gpuUtil).arg(gpuMemUsed).arg(gpuMemTotal).arg(gpuTemp)
            .arg(Fmt::fmtDouble(ramUsed / 1024.0, 1))
            .arg(Fmt::fmtDouble(ramTotal / 1024.0, 1)));
    }

    // --- Process snapshot ---
    if (m_processInfo) {
        QStringList lines;
        const QStringList names = {
            QStringLiteral("setup"), QStringLiteral("prepare"), QStringLiteral("training"),
            QStringLiteral("evaluate"), QStringLiteral("inference"), QStringLiteral("autopilot")
        };
        for (const QString& name : names) {
            const auto job     = jobs.value(name).toObject();
            const auto process = processes.value(name).toObject();
            const bool running = process.value(QStringLiteral("running")).toBool(false);
            const bool paused  = process.value(QStringLiteral("paused")).toBool(false);
            const QString stage = job.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
            const double prog   = job.value(QStringLiteral("progress")).toDouble(0.0);
            QString st = running ? QStringLiteral("running") : stage;
            if (name == QStringLiteral("training") && trainingPaused) {
                st = QStringLiteral("paused");
            }
            if (paused) st = QStringLiteral("paused");
            lines.append(QStringLiteral("%1 | %2 | %3").arg(Fmt::prettyJobName(name), st, Fmt::progressPct(prog)));
        }
        m_processInfo->setText(lines.join(QStringLiteral("\n")));
    }

    // --- Storage / assets ---
    if (m_storageInfo) {
        const auto checkpoint = state[QStringLiteral("checkpoint")].toObject();
        const bool hasCkpt = checkpoint.value(QStringLiteral("available")).toBool(false);
        const QString ckptName = checkpoint.value(QStringLiteral("name")).toString(QStringLiteral("--"));
        const bool hasReport = !reportText.trimmed().isEmpty();
        const auto cache = state[QStringLiteral("model_cache")].toObject();
        const auto judgeCache = cache[QStringLiteral("large_judge")].toObject();
        int cachedCount = 0;
        int totalMb = 0;
        for (auto it = judgeCache.begin(); it != judgeCache.end(); ++it) {
            const auto obj = it.value().toObject();
            if (obj.value(QStringLiteral("cached")).toBool(false)) {
                ++cachedCount;
                totalMb += obj.value(QStringLiteral("size_mb")).toInt(0);
            }
        }
        m_storageInfo->setText(QStringLiteral("API: %1\nCheckpoint: %2\nCached judge models: %3 (%4 MB)\nReport: %5")
            .arg(m_api->baseUrl(),
                 hasCkpt ? ckptName : QStringLiteral("None"))
            .arg(cachedCount).arg(totalMb)
            .arg(hasReport ? QStringLiteral("Available") : QStringLiteral("None")));
    }

    if (m_alertHeaderCount) {
        const QJsonArray alerts = state.value(QStringLiteral("alerts")).toArray();
        QString headerText = QStringLiteral("CLEAR");
        QString severity = QStringLiteral("clear");
        if (!alerts.isEmpty()) {
            const QJsonObject latestAlert = alerts.last().toObject();
            severity = latestAlert.value(QStringLiteral("severity")).toString(QStringLiteral("info"));
            headerText = severity == QStringLiteral("error")
                ? QStringLiteral("ERROR")
                : (severity == QStringLiteral("warning") ? QStringLiteral("WARN") : QStringLiteral("INFO"));
        }
        m_alertHeaderCount->setText(headerText);
        m_alertHeaderCount->setProperty("severity", severity);
        Fmt::repolish(m_alertHeaderCount);
    }
    if (m_alertList) {
        auto* alertInner = qobject_cast<QVBoxLayout*>(m_alertList->layout());
        clearLayoutItems(alertInner);
        const QJsonArray alerts = state.value(QStringLiteral("alerts")).toArray();
        if (alerts.isEmpty()) {
            auto* placeholder = new QLabel(QStringLiteral("No recent alerts or warnings."));
            placeholder->setObjectName(QStringLiteral("alertInfo"));
            placeholder->setWordWrap(true);
            alertInner->addWidget(placeholder);
        } else {
            for (int i = alerts.size() - 1, shown = 0; i >= 0 && shown < 3; --i, ++shown) {
                const QJsonObject alert = alerts.at(i).toObject();
                const QString severity = alert.value(QStringLiteral("severity")).toString(QStringLiteral("info"));
                const QString message = alert.value(QStringLiteral("message")).toString();
                const double ts = alert.value(QStringLiteral("ts")).toDouble();
                auto* label = new QLabel(QStringLiteral("%1  ·  %2").arg(message, Fmt::relativeTime(ts)));
                label->setObjectName(alertObjectName(severity));
                label->setWordWrap(true);
                alertInner->addWidget(label);
            }
        }
        alertInner->addStretch(1);
    }
}

