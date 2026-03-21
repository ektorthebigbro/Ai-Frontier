#ifndef FORMATTERS_H
#define FORMATTERS_H
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QVector>
#include <QStyle>
#include <QtMath>

namespace Fmt {

inline QString prettyJobName(const QString& job) {
    if (job == QStringLiteral("setup")) return QStringLiteral("Environment Setup");
    if (job == QStringLiteral("prepare")) return QStringLiteral("Prepare Data");
    if (job == QStringLiteral("training")) return QStringLiteral("Training");
    if (job == QStringLiteral("evaluate")) return QStringLiteral("Evaluation");
    if (job == QStringLiteral("inference")) return QStringLiteral("Inference");
    if (job == QStringLiteral("autopilot")) return QStringLiteral("Autopilot");
    if (job == QStringLiteral("server")) return QStringLiteral("Server");
    if (job.isEmpty()) return QStringLiteral("Autopilot");
    QString title = job;
    title.replace('_', ' ');
    if (!title.isEmpty()) title[0] = title[0].toUpper();
    return title;
}

inline QString captureMatch(const QString& text, const QString& pattern, int group = 1) {
    const auto match = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).match(text);
    return match.hasMatch() ? match.captured(group) : QString();
}

inline QString progressPct(double progress) {
    return QStringLiteral("%1%").arg(QString::number(qBound(0.0, progress, 1.0) * 100.0, 'f', 1));
}

inline QString relativeTime(double ts) {
    if (ts <= 0.0) return QStringLiteral("Updated recently");
    const qint64 delta = qMax<qint64>(0, QDateTime::currentSecsSinceEpoch() - static_cast<qint64>(ts));
    if (delta < 60) return QStringLiteral("Updated just now");
    if (delta < 3600) return QStringLiteral("Updated %1m ago").arg(delta / 60);
    if (delta < 86400) return QStringLiteral("Updated %1h ago").arg(delta / 3600);
    return QStringLiteral("Updated %1d ago").arg(delta / 86400);
}

inline QString diagTimestamp(double ts) {
    if (ts <= 0.0) return QStringLiteral("unknown");
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(ts)).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

inline QString evalSummary(const QString& reportText) {
    if (reportText.trimmed().isEmpty()) return QStringLiteral("--");
    const auto doc = QJsonDocument::fromJson(reportText.toUtf8());
    if (!doc.isObject()) return QStringLiteral("--");
    const auto obj = doc.object();
    if (obj.contains(QStringLiteral("protocol_overall_score")))
        return QString::number(obj.value(QStringLiteral("protocol_overall_score")).toDouble(), 'f', 2);
    if (obj.contains(QStringLiteral("reasoning_score")))
        return QString::number(obj.value(QStringLiteral("reasoning_score")).toDouble(), 'f', 2);
    if (obj.contains(QStringLiteral("gsm8k_accuracy")))
        return QStringLiteral("%1%").arg(QString::number(obj.value(QStringLiteral("gsm8k_accuracy")).toDouble() * 100.0, 'f', 1));
    return QStringLiteral("--");
}

inline QString missionFoot(const QString& jobName, const QString& stage, const QString& message) {
    if (stage == QStringLiteral("idle") || stage.isEmpty()) return QStringLiteral("No active task yet.");
    if (stage == QStringLiteral("completed")) return QStringLiteral("%1 completed successfully.").arg(prettyJobName(jobName));
    if (stage == QStringLiteral("failed")) return QStringLiteral("%1 needs attention.").arg(prettyJobName(jobName));
    if (stage == QStringLiteral("stopped")) return QStringLiteral("%1 was stopped.").arg(prettyJobName(jobName));
    if (!message.isEmpty()) return message;
    return QStringLiteral("%1 is in progress.").arg(prettyJobName(jobName));
}

inline void appendHistory(QVector<double>& history, double value, int limit = 90) {
    if (!qIsFinite(value)) return;
    history.append(value);
    if (history.size() > limit) history.remove(0, history.size() - limit);
}

inline QString fmtDouble(double value, int decimals = 2, const QString& fallback = QStringLiteral("--")) {
    if (!qIsFinite(value)) return fallback;
    return QString::number(value, 'f', decimals);
}

inline void repolish(QWidget* widget) {
    if (!widget) return;
    if (widget->style()) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
    widget->update();
}

}  // namespace Fmt
#endif
