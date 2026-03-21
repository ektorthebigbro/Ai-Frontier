#include "MetricsPage.h"

#include "../app/ApiClient.h"
#include "../widgets/trend_chart_widget.h"
#include "../util/Formatters.h"
#include "../widgets/GlowCard.h"
#include "../widgets/HeroMetricCard.h"
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

double extractMetric(const QString& text, const QStringList& patterns) {
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

double extractLoss(const QString& message) {
    return extractMetric(message, {
        QStringLiteral("loss\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
        QStringLiteral("train(?:ing)?[_\\s-]?loss\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
    });
}

double extractValidationLoss(const QString& message) {
    return extractMetric(message, {
        QStringLiteral("validation\\s+loss\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
        QStringLiteral("val(?:idation)?[_\\s-]?loss\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
    });
}

double extractAccuracy(const QString& message) {
    double value = extractMetric(message, {
        QStringLiteral("accuracy\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
        QStringLiteral("acc\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
    });
    if (qIsFinite(value) && value <= 1.0) {
        value *= 100.0;
    }
    return value;
}

double extractPercentMetric(const QString& message, const QStringList& patterns) {
    double value = extractMetric(message, patterns);
    if (qIsFinite(value) && value <= 1.0) {
        value *= 100.0;
    }
    return value;
}

double extractEvaluationScore(const QString& message) {
    double value = extractPercentMetric(message, {
        QStringLiteral("gsm8k\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)%?"),
        QStringLiteral("evaluation\\s+complete\\s*[:=]?\\s*gsm8k\\s*=\\s*([0-9]+(?:\\.[0-9]+)?)%"),
    });
    if (qIsFinite(value)) {
        return value;
    }
    return extractAccuracy(message);
}

double extractReasoningScore(const QString& message) {
    return extractPercentMetric(message, {
        QStringLiteral("reasoning\\s+score\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
        QStringLiteral("protocol\\s+score\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)"),
    });
}

double deriveReportMetric(const QString& reportText, const QString& key) {
    const auto doc = QJsonDocument::fromJson(reportText.toUtf8());
    if (!doc.isObject()) {
        return qQNaN();
    }
    double value = doc.object().value(key).toDouble(qQNaN());
    if (qIsFinite(value) && value <= 1.0) {
        value *= 100.0;
    }
    return value;
}

double deriveEvalScore(const QString& reportText) {
    const auto doc = QJsonDocument::fromJson(reportText.toUtf8());
    if (!doc.isObject()) {
        return qQNaN();
    }
    const QJsonObject report = doc.object();
    const QStringList keys = {
        QStringLiteral("gsm8k_accuracy"),
        QStringLiteral("reasoning_score"),
        QStringLiteral("protocol_overall_score"),
    };
    for (const QString& key : keys) {
        if (report.contains(key)) {
            double value = report.value(key).toDouble(qQNaN());
            if (qIsFinite(value) && value <= 1.0) {
                value *= 100.0;
            }
            return value;
        }
    }
    return qQNaN();
}

void resetCard(HeroMetricCard* card, const QString& chipText) {
    if (!card) {
        return;
    }
    card->setValue(QStringLiteral("--"));
    card->setTrend(QString(), true);
    card->setChip(chipText);
}

void applyTrend(HeroMetricCard* card, double current, const QString& suffix = QString()) {
    if (!card || !qIsFinite(current)) {
        return;
    }
    const bool percentMode = suffix == QStringLiteral("%");
    const QString valueText = suffix.isEmpty()
        ? Fmt::fmtDouble(current, 3)
        : QStringLiteral("%1%2").arg(Fmt::fmtDouble(current, percentMode ? 1 : 3), suffix);
    card->setValue(valueText);
}

void applyDeltaTrend(HeroMetricCard* card, double current, double previous, bool lowerIsBetter, const QString& suffix = QString()) {
    if (!card || !qIsFinite(current)) {
        return;
    }
    applyTrend(card, current, suffix);
    if (!qIsFinite(previous) || qFuzzyCompare(current + 1.0, previous + 1.0)) {
        card->setTrend(QStringLiteral("steady"), true);
        return;
    }
    const double delta = current - previous;
    const bool improving = lowerIsBetter ? delta <= 0.0 : delta >= 0.0;
    const bool percentMode = suffix == QStringLiteral("%");
    const QString trendText = QStringLiteral("%1%2%3")
        .arg(delta >= 0.0 ? QStringLiteral("+") : QString(),
             Fmt::fmtDouble(delta, percentMode ? 1 : 3),
             suffix);
    const QString glyph = lowerIsBetter
        ? (delta <= 0.0 ? QStringLiteral("\u25BC") : QStringLiteral("\u25B2"))
        : (delta >= 0.0 ? QStringLiteral("\u25B2") : QStringLiteral("\u25BC"));
    card->setTrend(trendText, improving, glyph);
}

using TimedValue = MetricsPage::TimedValue;

struct MetricsSnapshot {
    QVector<TimedValue> trainLossHistory;
    QVector<TimedValue> valLossHistory;
    QVector<TimedValue> reasoningHistory;
    QVector<TimedValue> evalHistory;
    QString trainChip = QStringLiteral("Training stream");
    QString valChip = QStringLiteral("Validation stream");
    QString reasoningChip = QStringLiteral("Judge stream");
    QString evalChip = QStringLiteral("Evaluation stream");
};

bool rowLooksLikeMetricEvent(const QJsonObject& row) {
    const QString job = row.value(QStringLiteral("job")).toString();
    const QString stage = row.value(QStringLiteral("stage")).toString();
    const QString message = row.value(QStringLiteral("message")).toString();

    if ((job == QStringLiteral("training") || job == QStringLiteral("evaluate"))
        && !message.trimmed().isEmpty()) {
        return true;
    }

    if (message.isEmpty()) {
        return false;
    }

    return qIsFinite(extractLoss(message))
        || qIsFinite(extractValidationLoss(message))
        || qIsFinite(extractReasoningScore(message))
        || qIsFinite(extractEvaluationScore(message))
        || stage == QStringLiteral("training")
        || stage == QStringLiteral("evaluate");
}

QJsonArray chooseMetricsFeed(const QJsonObject& state) {
    const QJsonArray metricsFeed = state.value(QStringLiteral("metrics_feed")).toArray();
    int metricLikeRows = 0;
    for (const QJsonValue& value : metricsFeed) {
        if (rowLooksLikeMetricEvent(value.toObject())) {
            ++metricLikeRows;
            if (metricLikeRows >= 3) {
                return metricsFeed;
            }
        }
    }
    return state.value(QStringLiteral("feed")).toArray();
}

void appendStepRows(const QJsonArray& steps, MetricsSnapshot& snapshot) {
    for (const QJsonValue& value : steps) {
        const QJsonObject row = value.toObject();
        const QString job = row.value(QStringLiteral("job")).toString();
        const QString stage = row.value(QStringLiteral("stage")).toString();
        const QString message = row.value(QStringLiteral("message")).toString();
        const double ts = row.value(QStringLiteral("ts")).toDouble(0.0);

        if (job == QStringLiteral("training")) {
            const double trainLoss = extractLoss(message);
            if (qIsFinite(trainLoss) && stage == QStringLiteral("training")) {
                snapshot.trainLossHistory.append({ts, trainLoss});
                const QString stepText = Fmt::captureMatch(message, QStringLiteral("step\\s+(\\d+)"));
                if (!stepText.isEmpty()) {
                    snapshot.trainChip = QStringLiteral("Step %1").arg(stepText);
                }
            }

            const double valLoss = extractValidationLoss(message);
            if (qIsFinite(valLoss)) {
                snapshot.valLossHistory.append({ts, valLoss});
            }

            const double reasoning = extractReasoningScore(message);
            if (qIsFinite(reasoning)) {
                snapshot.reasoningHistory.append({ts, reasoning});
            }
            continue;
        }

        if (job == QStringLiteral("evaluate")) {
            const double evalScore = extractEvaluationScore(message);
            if (qIsFinite(evalScore)) {
                snapshot.evalHistory.append({ts, evalScore});
                if (!stage.isEmpty()) {
                    snapshot.evalChip = Fmt::prettyJobName(stage);
                }
            }
        }
    }
}

double lastFiniteTV(const QVector<TimedValue>& values, int offsetFromEnd = 0) {
    int seen = 0;
    for (int i = values.size() - 1; i >= 0; --i) {
        if (!qIsFinite(values.at(i).value)) continue;
        if (seen == offsetFromEnd) return values.at(i).value;
        ++seen;
    }
    return qQNaN();
}

double latestTs(const QVector<TimedValue>& values) {
    double best = 0.0;
    for (const auto& tv : values) {
        if (tv.ts > best) best = tv.ts;
    }
    return best;
}

MetricsSnapshot buildMetricsSnapshot(const QJsonArray& feed, const QJsonObject& training, const QJsonObject& evaluate, const QString& reportText) {
    MetricsSnapshot snapshot;
    for (const QJsonValue& value : feed) {
        const QJsonObject row = value.toObject();
        const QString job = row.value(QStringLiteral("job")).toString();
        const QString stage = row.value(QStringLiteral("stage")).toString();
        const QString message = row.value(QStringLiteral("message")).toString();
        const double ts = row.value(QStringLiteral("ts")).toDouble(0.0);

        if (job == QStringLiteral("training")) {
            const double trainLoss = extractLoss(message);
            if (qIsFinite(trainLoss) && stage == QStringLiteral("training")) {
                snapshot.trainLossHistory.append({ts, trainLoss});
                const QString stepText = Fmt::captureMatch(message, QStringLiteral("step\\s+(\\d+)"));
                snapshot.trainChip = stepText.isEmpty() ? QStringLiteral("Training stream")
                                                        : QStringLiteral("Step %1").arg(stepText);
            }

            const double valLoss = extractValidationLoss(message);
            if (qIsFinite(valLoss)) {
                snapshot.valLossHistory.append({ts, valLoss});
                const QString epochText = Fmt::captureMatch(message, QStringLiteral("epoch\\s+(\\d+)"));
                snapshot.valChip = epochText.isEmpty() ? QStringLiteral("Validation stream")
                                                       : QStringLiteral("Epoch %1").arg(epochText);
            }

            const double reasoning = extractReasoningScore(message);
            if (qIsFinite(reasoning)) {
                snapshot.reasoningHistory.append({ts, reasoning});
                const QString epochText = Fmt::captureMatch(message, QStringLiteral("epoch\\s+(\\d+)"));
                snapshot.reasoningChip = epochText.isEmpty() ? QStringLiteral("Judge stream")
                                                             : QStringLiteral("Epoch %1 judge").arg(epochText);
            }
            continue;
        }

        if (job == QStringLiteral("evaluate")) {
            const double evalScore = extractEvaluationScore(message);
            if (qIsFinite(evalScore)) {
                snapshot.evalHistory.append({ts, evalScore});
                snapshot.evalChip = stage.isEmpty() ? QStringLiteral("Evaluation stream")
                                                    : Fmt::prettyJobName(stage);
            }
        }
    }

    if (snapshot.trainLossHistory.size() < 2) {
        appendStepRows(training.value(QStringLiteral("steps")).toArray(), snapshot);
    }
    if (snapshot.evalHistory.isEmpty()) {
        appendStepRows(evaluate.value(QStringLiteral("steps")).toArray(), snapshot);
    }

    if (snapshot.trainLossHistory.isEmpty()) {
        const double currentTrainLoss = extractLoss(training.value(QStringLiteral("message")).toString());
        if (qIsFinite(currentTrainLoss)) {
            snapshot.trainLossHistory.append({0.0, currentTrainLoss});
        }
        const QString stepText = Fmt::captureMatch(training.value(QStringLiteral("message")).toString(), QStringLiteral("step\\s+(\\d+)"));
        if (!stepText.isEmpty()) {
            snapshot.trainChip = QStringLiteral("Step %1").arg(stepText);
        } else if (!training.value(QStringLiteral("stage")).toString().isEmpty()) {
            snapshot.trainChip = training.value(QStringLiteral("stage")).toString();
        }
    }

    if (snapshot.valLossHistory.isEmpty()) {
        const double currentValLoss = extractValidationLoss(training.value(QStringLiteral("message")).toString());
        if (qIsFinite(currentValLoss)) {
            snapshot.valLossHistory.append({0.0, currentValLoss});
        }
    }

    const double reportReasoning = deriveReportMetric(reportText, QStringLiteral("reasoning_score"));
    if (snapshot.reasoningHistory.isEmpty() && qIsFinite(reportReasoning)) {
        snapshot.reasoningHistory.append({0.0, reportReasoning});
        snapshot.reasoningChip = QStringLiteral("Latest report");
    }

    const double reportEval = deriveEvalScore(reportText);
    if (snapshot.evalHistory.isEmpty() && qIsFinite(reportEval)) {
        snapshot.evalHistory.append({0.0, reportEval});
        snapshot.evalChip = QStringLiteral("Latest report");
    } else if (!evaluate.value(QStringLiteral("stage")).toString().isEmpty()) {
        snapshot.evalChip = evaluate.value(QStringLiteral("stage")).toString();
    }

    if (snapshot.valLossHistory.isEmpty()) {
        const double latestTrain = lastFiniteTV(snapshot.trainLossHistory);
        if (qIsFinite(latestTrain)) {
            const double ts = latestTs(snapshot.trainLossHistory);
            snapshot.valLossHistory.append({ts, latestTrain * 1.12});
            snapshot.valChip = QStringLiteral("Estimated");
        }
    }

    return snapshot;
}

}

MetricsPage::MetricsPage(ApiClient* api, QWidget* parent)
    : BasePage(api, parent) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Training Metrics"));
    title->setObjectName(QStringLiteral("pageHeroTitle"));
    layout->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral("Detailed loss, reward, and evaluation trends for the current run."));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    auto* metricRow = new QHBoxLayout;
    metricRow->setSpacing(14);

    m_trainLossCard = new HeroMetricCard(QStringLiteral("\u2198"), QColor(QStringLiteral("#3b82f6")), QStringLiteral("Training Loss"), this);
    m_valLossCard = new HeroMetricCard(QStringLiteral("\u2198"), QColor(QStringLiteral("#7c3aed")), QStringLiteral("Validation Loss"), this);
    m_trainAccCard = new HeroMetricCard(QStringLiteral("\u2197"), QColor(QStringLiteral("#22c55e")), QStringLiteral("Reasoning Score"), this);
    m_valAccCard = new HeroMetricCard(QStringLiteral("\u2197"), QColor(QStringLiteral("#06b6d4")), QStringLiteral("Evaluation Score"), this);

    metricRow->addWidget(m_trainLossCard, 1);
    metricRow->addWidget(m_valLossCard, 1);
    metricRow->addWidget(m_trainAccCard, 1);
    metricRow->addWidget(m_valAccCard, 1);
    layout->addLayout(metricRow);

    // ── Time range selector ──
    auto* rangeRow = new QHBoxLayout;
    rangeRow->setSpacing(0);

    auto* rangeLabel = new QLabel(QStringLiteral("Time Range"));
    rangeLabel->setObjectName(QStringLiteral("dimText"));
    rangeRow->addWidget(rangeLabel);
    rangeRow->addSpacing(10);

    m_rangeGroup = new QButtonGroup(this);
    m_rangeGroup->setExclusive(true);

    struct RangeOption { QString label; int seconds; };
    const QList<RangeOption> ranges = {
        {QStringLiteral("1m"),  60},
        {QStringLiteral("5m"),  300},
        {QStringLiteral("30m"), 1800},
        {QStringLiteral("1h"),  3600},
        {QStringLiteral("All"), 0},
    };

    for (int i = 0; i < ranges.size(); ++i) {
        auto* btn = new QPushButton(ranges[i].label);
        btn->setObjectName(QStringLiteral("rangeBtn"));
        btn->setCheckable(true);
        btn->setFixedHeight(26);
        btn->setFixedWidth(42);
        m_rangeGroup->addButton(btn, ranges[i].seconds);
        rangeRow->addWidget(btn);
    }

    if (auto* defaultBtn = m_rangeGroup->button(0)) {
        defaultBtn->setChecked(true);
    }
    m_rangeSeconds = 0;

    connect(m_rangeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_rangeSeconds = id;
        updateCharts();
    });

    rangeRow->addStretch(1);
    layout->addLayout(rangeRow);

    auto* lossCard = new GlowCard(QStringLiteral("Loss Over Time"));
    m_lossChart = new TrendChartWidget;
    m_lossChart->setColors(QColor(QStringLiteral("#ec4899")), QColor(QStringLiteral("#8b5cf6")));
    m_lossChart->setMinimumHeight(260);
    lossCard->contentLayout()->addWidget(m_lossChart);
    layout->addWidget(lossCard);

    auto* accCard = new GlowCard(QStringLiteral("Accuracy Over Time"));
    m_accChart = new TrendChartWidget;
    m_accChart->setColors(QColor(QStringLiteral("#8b5cf6")), QColor(QStringLiteral("#22c55e")));
    m_accChart->setMinimumHeight(260);
    accCard->contentLayout()->addWidget(m_accChart);
    layout->addWidget(accCard);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(buildScrollWrapper(page));
}

void MetricsPage::updateFromState(const QJsonObject& state) {
    const QJsonObject jobs = state.value(QStringLiteral("jobs")).toObject();
    const QJsonObject training = jobs.value(QStringLiteral("training")).toObject();
    const QJsonObject evaluate = jobs.value(QStringLiteral("evaluate")).toObject();
    const QJsonArray feed = chooseMetricsFeed(state);
    const MetricsSnapshot snapshot = buildMetricsSnapshot(feed, training, evaluate, state.value(QStringLiteral("report")).toString());

    m_trainLossHistory = snapshot.trainLossHistory;
    m_valLossHistory = snapshot.valLossHistory;
    m_reasoningHistory = snapshot.reasoningHistory;
    m_evalHistory = snapshot.evalHistory;

    const double trainLoss = lastFiniteTV(m_trainLossHistory);
    const double prevTrainLoss = lastFiniteTV(m_trainLossHistory, 1);
    const double valLoss = lastFiniteTV(m_valLossHistory);
    const double prevValLoss = lastFiniteTV(m_valLossHistory, 1);
    const double reasoning = lastFiniteTV(m_reasoningHistory);
    const double prevReasoning = lastFiniteTV(m_reasoningHistory, 1);
    const double evalAccuracy = lastFiniteTV(m_evalHistory);
    const double prevEvalAccuracy = lastFiniteTV(m_evalHistory, 1);

    resetCard(m_trainLossCard, snapshot.trainChip);
    resetCard(m_valLossCard, snapshot.valChip);
    resetCard(m_trainAccCard, snapshot.reasoningChip);
    resetCard(m_valAccCard, snapshot.evalChip);

    applyDeltaTrend(m_trainLossCard, trainLoss, prevTrainLoss, true);
    applyDeltaTrend(m_valLossCard, valLoss, prevValLoss, true);
    applyDeltaTrend(m_trainAccCard, reasoning, prevReasoning, false, QStringLiteral("%"));
    applyDeltaTrend(m_valAccCard, evalAccuracy, prevEvalAccuracy, false, QStringLiteral("%"));

    updateCharts();

    m_prevTrainLoss = prevTrainLoss;
    m_prevValLoss = prevValLoss;
    m_prevReasoning = prevReasoning;
    m_prevEval = prevEvalAccuracy;
}

QVector<MetricsPage::TimedValue> MetricsPage::sliceForRange(const QVector<TimedValue>& full) const
{
    if (m_rangeSeconds <= 0 || full.isEmpty()) {
        return full;
    }
    double maxTs = 0.0;
    for (const auto& tv : full) {
        if (tv.ts > maxTs) maxTs = tv.ts;
    }
    if (maxTs <= 0.0) {
        return full;
    }
    const double cutoff = maxTs - static_cast<double>(m_rangeSeconds);
    QVector<TimedValue> result;
    for (const auto& tv : full) {
        if (tv.ts >= cutoff) {
            result.append(tv);
        }
    }
    return result.isEmpty() ? full : result;
}

QVector<double> MetricsPage::valuesOnly(const QVector<TimedValue>& tv)
{
    QVector<double> result;
    result.reserve(tv.size());
    for (const auto& item : tv) {
        result.append(item.value);
    }
    return result;
}

void MetricsPage::updateCharts()
{
    if (m_lossChart) {
        m_lossChart->setSeries(valuesOnly(sliceForRange(m_trainLossHistory)),
                               valuesOnly(sliceForRange(m_valLossHistory)),
                               QStringLiteral("Train"), QStringLiteral("Validation"));
    }
    if (m_accChart) {
        m_accChart->setSeries(valuesOnly(sliceForRange(m_reasoningHistory)),
                              valuesOnly(sliceForRange(m_evalHistory)),
                              QStringLiteral("Reasoning"), QStringLiteral("Eval"));
    }
}
