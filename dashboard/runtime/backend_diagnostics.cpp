#include "../backend.h"
#include "../common/backend_common.h"
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>

using namespace ControlCenterBackendCommon;

namespace {

QByteArray diagnosticsOk(const QString& message, const QJsonObject& extra = {}) {
    QJsonObject payload = extra;
    payload.insert(QStringLiteral("ok"), true);
    payload.insert(QStringLiteral("message"), message);
    return jsonBytes(payload);
}

QByteArray diagnosticsFail(const QString& message) {
    return jsonBytes(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), message},
    });
}

QJsonObject makeHealthRow(const QString& check, const QString& status, const QString& message) {
    return QJsonObject{
        {QStringLiteral("check"), check},
        {QStringLiteral("status"), status},
        {QStringLiteral("message"), message},
    };
}

int countInvalidFeedRows(const QString& path, int maxRows) {
    const QStringList lines = readTailLinesFromFile(path, maxRows);
    int invalid = 0;
    for (const QString& line : std::as_const(lines)) {
        if (line.isEmpty()) {
            continue;
        }
        if (!QJsonDocument::fromJson(line.toUtf8()).isObject()) {
            ++invalid;
        }
    }
    return invalid;
}

QJsonArray loadJsonObjectTail(const QString& path, int maxRows) {
    QJsonArray rows;
    const QStringList lines = readTailLinesFromFile(path, maxRows);

    for (const QString& line : std::as_const(lines)) {
        if (line.isEmpty()) {
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8());
        if (doc.isObject()) {
            rows.append(doc.object());
        }
    }
    return rows;
}

void appendFixRow(QList<QJsonObject>& rows,
                  const QString& action,
                  const QString& target,
                  const QString& result,
                  const QString& severity = QStringLiteral("info"),
                  const QJsonObject& context = {}) {
    QJsonObject row{
        {QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch()},
        {QStringLiteral("action"), action},
        {QStringLiteral("target"), target},
        {QStringLiteral("result"), result},
        {QStringLiteral("severity"), severity},
    };
    if (!context.isEmpty()) {
        row.insert(QStringLiteral("context"), context);
    }
    rows.append(row);
    if (rows.size() > 48) {
        rows = rows.mid(rows.size() - 48);
    }
}

QString formatConsoleLogLine(const QString& severity,
                             const QString& category,
                             const QString& action,
                             const QString& message) {
    const QString ts = QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss"));
    return QStringLiteral("[%1] %2 %3/%4: %5")
        .arg(ts,
             severity.toUpper(),
             category,
             action,
             message);
}

}  // namespace

void ControlCenterBackend::recordFeedEvent(const QJsonObject& row) {
    ensureDir(QFileInfo(m_feedPath).absolutePath());
    QByteArray payload = QJsonDocument(row).toJson(QJsonDocument::Compact);
    payload.append('\n');
    appendTextFileLocked(m_feedPath, payload);
}

void ControlCenterBackend::recordLog(const QString& severity,
                                     const QString& category,
                                     const QString& action,
                                     const QString& message,
                                     const QJsonObject& context) {
    BackendLogEntry entry;
    entry.ts = QDateTime::currentSecsSinceEpoch();
    entry.severity = severity;
    entry.category = category;
    entry.action = action;
    entry.message = message;
    entry.context = context;
    m_recentBackendLogs.append(entry);
    if (m_recentBackendLogs.size() > 300) {
        m_recentBackendLogs = m_recentBackendLogs.mid(m_recentBackendLogs.size() - 300);
    }

    ensureDir(QFileInfo(m_backendLogPath).absolutePath());
    QJsonObject row{
        {QStringLiteral("ts"), entry.ts},
        {QStringLiteral("severity"), entry.severity},
        {QStringLiteral("category"), entry.category},
        {QStringLiteral("action"), entry.action},
        {QStringLiteral("message"), entry.message},
    };
    if (!context.isEmpty()) {
        row.insert(QStringLiteral("context"), context);
    }
    QByteArray payload = QJsonDocument(row).toJson(QJsonDocument::Compact);
    payload.append('\n');
    appendTextFileLocked(m_backendLogPath, payload);

    printConsoleLine(formatConsoleLogLine(severity, category, action, message),
                     severity == QStringLiteral("error"));
}

void ControlCenterBackend::recordEvent(const QString& type, const QString& message) {
    recordFeedEvent(QJsonObject{
        {QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch()},
        {QStringLiteral("type"), type},
        {QStringLiteral("message"), message},
    });
    recordLog(QStringLiteral("info"), QStringLiteral("event"), type, message);
    invalidateRuntimeCaches();
}

void ControlCenterBackend::recordAlert(const QString& severity, const QString& message) {
    const QJsonObject row{
        {QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch()},
        {QStringLiteral("severity"), severity},
        {QStringLiteral("message"), message},
    };
    m_alerts.append(row);
    if (m_alerts.size() > 100) {
        m_alerts = m_alerts.mid(m_alerts.size() - 100);
    }
    QJsonObject feedRow = row;
    feedRow.insert(QStringLiteral("type"), QStringLiteral("alert"));
    recordFeedEvent(feedRow);
    recordLog(severity, QStringLiteral("alert"), QStringLiteral("alert"), message);
    invalidateRuntimeCaches();
}

void ControlCenterBackend::recordJobUpdate(const QString& job,
                                           const QString& stage,
                                           const QString& message,
                                           double progress,
                                           bool trackIssue) {
    const QString normalizedStage = stage.trimmed().isEmpty() ? QStringLiteral("idle") : stage.trimmed();
    const double boundedProgress = qBound(0.0, progress, 1.0);
    if (!job.trimmed().isEmpty()) {
        if (normalizedStage == QStringLiteral("failed") && trackIssue) {
            const QString issueMessage = message.trimmed().isEmpty()
                ? QStringLiteral("%1 failed").arg(job)
                : message.trimmed();
            recordIssue(job, issueMessage);
        } else {
            clearIssuesForModule(job);
        }
    }
    recordFeedEvent(QJsonObject{
        {QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch()},
        {QStringLiteral("job"), job},
        {QStringLiteral("stage"), normalizedStage},
        {QStringLiteral("message"), message},
        {QStringLiteral("progress"), boundedProgress},
    });
    const QString previousStage = m_lastRecordedJobStage.value(job);
    const bool terminalStage =
        normalizedStage == QStringLiteral("completed")
        || normalizedStage == QStringLiteral("failed")
        || normalizedStage == QStringLiteral("stopped")
        || normalizedStage == QStringLiteral("paused");
    if (previousStage != normalizedStage || terminalStage) {
        m_lastRecordedJobStage.insert(job, normalizedStage);
        QString severity = QStringLiteral("info");
        if (normalizedStage == QStringLiteral("failed")) {
            severity = QStringLiteral("error");
        } else if (normalizedStage == QStringLiteral("paused")
                   || normalizedStage == QStringLiteral("stopped")) {
            severity = QStringLiteral("warning");
        }
        recordLog(severity,
                  QStringLiteral("job"),
                  QStringLiteral("stage"),
                  QStringLiteral("%1 entered %2").arg(job, normalizedStage),
                  QJsonObject{
                      {QStringLiteral("job"), job},
                      {QStringLiteral("stage"), normalizedStage},
                      {QStringLiteral("message"), message},
                      {QStringLiteral("progress"), boundedProgress},
                  });
    }
    invalidateRuntimeCaches();
}

void ControlCenterBackend::invalidateRuntimeCaches() {
    m_stateCacheUntil = QDateTime();
    m_hardwareCacheUntil = QDateTime();
    m_modelCacheUntil = QDateTime();
}

void ControlCenterBackend::noteInvalidFeedRows(int invalidRows) {
    const int boundedInvalidRows = qMax(0, invalidRows);
    if (boundedInvalidRows == 0) {
        if (m_lastInvalidFeedRowCount > 0) {
            clearIssuesForModule(QStringLiteral("feed"));
        }
        m_lastInvalidFeedRowCount = 0;
        return;
    }
    if (boundedInvalidRows == m_lastInvalidFeedRowCount) {
        return;
    }
    m_lastInvalidFeedRowCount = boundedInvalidRows;
    recordIssue(QStringLiteral("feed"), QStringLiteral("Skipped %1 invalid feed rows").arg(boundedInvalidRows));
}

void ControlCenterBackend::recordIssue(const QString& module, const QString& error) {
    const QString key = module + QStringLiteral("::") + error;
    BackendIssueEntry entry = m_issues.value(key);
    const bool isNew = entry.key.isEmpty();
    entry.key = key;
    entry.module = module;
    entry.error = error;
    entry.count = isNew ? 1 : entry.count + 1;
    entry.lastSeen = QDateTime::currentSecsSinceEpoch();
    entry.suppressed = entry.count >= 5;
    m_issues.insert(key, entry);
    recordLog(entry.count > 1 ? QStringLiteral("warning") : QStringLiteral("error"),
              QStringLiteral("issue"),
              module,
              error,
              QJsonObject{
                  {QStringLiteral("key"), key},
                  {QStringLiteral("count"), entry.count},
                  {QStringLiteral("suppressed"), entry.suppressed},
              });
    invalidateRuntimeCaches();
}

void ControlCenterBackend::clearIssue(const QString& key) {
    const BackendIssueEntry entry = m_issues.value(key);
    m_issues.remove(key);
    if (!entry.key.isEmpty()) {
        recordLog(QStringLiteral("info"),
                  QStringLiteral("issue"),
                  QStringLiteral("clear"),
                  QStringLiteral("Cleared tracked issue"),
                  QJsonObject{
                      {QStringLiteral("key"), entry.key},
                      {QStringLiteral("module"), entry.module},
                  });
    }
    invalidateRuntimeCaches();
}

void ControlCenterBackend::clearIssuesForModule(const QString& module) {
    if (module.trimmed().isEmpty()) {
        return;
    }

    QStringList keysToRemove;
    for (auto it = m_issues.cbegin(); it != m_issues.cend(); ++it) {
        if (it.value().module.compare(module, Qt::CaseInsensitive) == 0) {
            keysToRemove.append(it.key());
        }
    }

    for (const QString& key : std::as_const(keysToRemove)) {
        clearIssue(key);
    }
}

void ControlCenterBackend::clearAllIssues() {
    const int count = m_issues.size();
    m_issues.clear();
    recordLog(QStringLiteral("info"),
              QStringLiteral("issue"),
              QStringLiteral("clear_all"),
              QStringLiteral("Cleared all tracked issues"),
              QJsonObject{{QStringLiteral("count"), count}});
    invalidateRuntimeCaches();
}

void ControlCenterBackend::clearAcknowledgedRuntimeState() {
    // Cut over at "now" so newly written feed rows become visible immediately
    // even if the previous feed contained skewed future timestamps.
    m_feedClearCutoffTs = (static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0) + 0.001;
    const int clearedAlerts = m_alerts.size();
    m_alerts.clear();
    QFile::remove(m_feedPath);
    m_lastInvalidFeedRowCount = 0;

    if (!m_autopilot.value(QStringLiteral("active")).toBool(false)
        && !m_autopilot.value(QStringLiteral("paused")).toBool(false)
        && m_autopilot.value(QStringLiteral("stage")).toString() == QStringLiteral("failed")) {
        ensureAutopilotState();
        m_autopilot.insert(QStringLiteral("active"), false);
        m_autopilot.insert(QStringLiteral("paused"), false);
        m_autopilot.insert(QStringLiteral("stage"), QStringLiteral("idle"));
        m_autopilot.insert(QStringLiteral("message"), QStringLiteral("Waiting for activity"));
        QFile::remove(autopilotRuntimeStatePath());
        QFile::remove(autopilotRuntimeStatePath() + QStringLiteral(".bak"));
    }

    recordLog(QStringLiteral("info"),
              QStringLiteral("issue"),
              QStringLiteral("clear_acknowledged_runtime_state"),
              QStringLiteral("Cleared acknowledged runtime alerts and stale terminal state"),
              QJsonObject{{QStringLiteral("alerts_cleared"), clearedAlerts}});
    invalidateRuntimeCaches();
}

QJsonArray ControlCenterBackend::currentIssues() const {
    QJsonArray issues;
    QList<BackendIssueEntry> entries = m_issues.values();
    std::sort(entries.begin(), entries.end(), [](const BackendIssueEntry& left, const BackendIssueEntry& right) {
        return left.lastSeen > right.lastSeen;
    });
    for (const BackendIssueEntry& entry : entries) {
        issues.append(QJsonObject{
            {QStringLiteral("key"), entry.key},
            {QStringLiteral("module"), entry.module},
            {QStringLiteral("error"), entry.error},
            {QStringLiteral("count"), entry.count},
            {QStringLiteral("suppressed"), entry.suppressed},
            {QStringLiteral("last_seen"), entry.lastSeen},
        });
    }
    return issues;
}

QJsonArray ControlCenterBackend::fixLog() const {
    QJsonArray fixes;
    for (const QJsonObject& row : m_fixHistory) {
        fixes.append(row);
    }
    return fixes;
}

QJsonArray ControlCenterBackend::recentBackendLogRows(int maxRows) const {
    const int limit = qBound(1, maxRows, 300);
    QList<QJsonObject> mergedRows;
    mergedRows.reserve(limit * 2);
    QSet<QByteArray> seenRows;

    const QJsonArray diskRows = loadJsonObjectTail(m_backendLogPath, limit);
    for (const QJsonValue& value : diskRows) {
        const QJsonObject row = value.toObject();
        const QByteArray signature = QJsonDocument(row).toJson(QJsonDocument::Compact);
        if (seenRows.contains(signature)) {
            continue;
        }
        seenRows.insert(signature);
        mergedRows.append(row);
    }

    const int start = qMax(0, m_recentBackendLogs.size() - limit);
    for (int i = start; i < m_recentBackendLogs.size(); ++i) {
        const BackendLogEntry& entry = m_recentBackendLogs.at(i);
        QJsonObject row{
            {QStringLiteral("ts"), entry.ts},
            {QStringLiteral("severity"), entry.severity},
            {QStringLiteral("category"), entry.category},
            {QStringLiteral("action"), entry.action},
            {QStringLiteral("message"), entry.message},
        };
        if (!entry.context.isEmpty()) {
            row.insert(QStringLiteral("context"), entry.context);
        }
        const QByteArray signature = QJsonDocument(row).toJson(QJsonDocument::Compact);
        if (seenRows.contains(signature)) {
            continue;
        }
        seenRows.insert(signature);
        mergedRows.append(row);
    }

    std::sort(mergedRows.begin(), mergedRows.end(), [](const QJsonObject& left, const QJsonObject& right) {
        return left.value(QStringLiteral("ts")).toDouble() > right.value(QStringLiteral("ts")).toDouble();
    });
    if (mergedRows.size() > limit) {
        mergedRows = mergedRows.mid(0, limit);
    }

    QJsonArray rows;
    for (const QJsonObject& row : std::as_const(mergedRows)) {
        rows.append(row);
    }
    return rows;
}

QJsonObject ControlCenterBackend::buildLogSummary() const {
    QHash<QString, int> counts;
    const QJsonArray rows = recentBackendLogRows(200);
    QString lastSeverity = QStringLiteral("info");
    QString lastMessage;
    double lastTs = 0.0;
    for (const QJsonValue& value : rows) {
        const QJsonObject row = value.toObject();
        const QString severity = row.value(QStringLiteral("severity")).toString(QStringLiteral("info"));
        counts[severity] += 1;
        const double ts = row.value(QStringLiteral("ts")).toDouble();
        if (ts >= lastTs) {
            lastTs = ts;
            lastSeverity = severity;
            lastMessage = row.value(QStringLiteral("message")).toString();
        }
    }

    return QJsonObject{
        {QStringLiteral("total"), rows.size()},
        {QStringLiteral("error_count"), counts.value(QStringLiteral("error"))},
        {QStringLiteral("warning_count"), counts.value(QStringLiteral("warning"))},
        {QStringLiteral("info_count"), counts.value(QStringLiteral("info"))},
        {QStringLiteral("last_severity"), lastSeverity},
        {QStringLiteral("last_message"), lastMessage},
        {QStringLiteral("last_ts"), lastTs},
    };
}

QJsonArray ControlCenterBackend::runHealthChecks() {
    QJsonArray health;
    health.append(makeHealthRow(QStringLiteral("backend"), QStringLiteral("ok"), QStringLiteral("Native backend API is running")));
    health.append(makeHealthRow(QStringLiteral("config"), m_config.isEmpty() ? QStringLiteral("error") : QStringLiteral("ok"),
                                m_config.isEmpty() ? QStringLiteral("Config is empty or unreadable") : QStringLiteral("Config loaded successfully")));

    const QJsonObject hardware = buildHardwareSnapshot();
    const bool gpuDetected = hardware.value(QStringLiteral("gpu_memory_total_mb")).toInt() > 0;
    health.append(makeHealthRow(QStringLiteral("gpu"), gpuDetected ? QStringLiteral("ok") : QStringLiteral("warning"),
                                gpuDetected ? QStringLiteral("GPU telemetry available") : QStringLiteral("GPU telemetry unavailable")));

    const QStringList directories = {
        rootPathFor(QStringLiteral("logs")),
        rootPathFor(QStringLiteral("checkpoints")),
        rootPathFor(QStringLiteral("artifacts")),
        rootPathFor(QStringLiteral("data")),
    };
    for (const QString& path : directories) {
        const QFileInfo info(path);
        health.append(makeHealthRow(QStringLiteral("dir_%1").arg(info.fileName()),
                                    info.exists() ? QStringLiteral("ok") : QStringLiteral("warning"),
                                    info.exists() ? QStringLiteral("%1 exists").arg(info.fileName())
                                                  : QStringLiteral("%1 is missing").arg(info.fileName())));
    }

    const QString python = pythonPath();
    health.append(makeHealthRow(QStringLiteral("python"),
                                QFileInfo::exists(python) || python == QStringLiteral("python") ? QStringLiteral("ok") : QStringLiteral("warning"),
                                QStringLiteral("Worker runtime: %1").arg(python)));

    const int invalidRows = countInvalidFeedRows(m_feedPath, 64);
    health.append(makeHealthRow(QStringLiteral("feed"),
                                invalidRows == 0 ? QStringLiteral("ok") : QStringLiteral("warning"),
                                invalidRows == 0 ? QStringLiteral("Feed parses cleanly") : QStringLiteral("Skipped %1 invalid feed rows").arg(invalidRows)));
    const QFileInfo logInfo(m_backendLogPath);
    health.append(makeHealthRow(QStringLiteral("backend_log"),
                                logInfo.exists() ? QStringLiteral("ok") : QStringLiteral("warning"),
                                logInfo.exists() ? QStringLiteral("Structured backend log file is present")
                                                 : QStringLiteral("Structured backend log file will be created on first event")));
    return health;
}

QJsonObject ControlCenterBackend::reloadModuleAction(const QString& moduleName) {
    if (!m_reloadableModules.contains(moduleName)) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Module is not reloadable")}};
    }

    QString resultMessage;
    if (moduleName == QStringLiteral("frontier.hardware")) {
        m_hardwareCacheUntil = QDateTime();
        buildHardwareSnapshot();
        clearIssue(QStringLiteral("frontier.hardware::GPU telemetry unavailable"));
        resultMessage = QStringLiteral("Hardware probes refreshed without restarting the backend.");
    } else if (moduleName == QStringLiteral("frontier.config")) {
        loadConfigFromDisk(true);
        invalidateRuntimeCaches();
        resultMessage = QStringLiteral("Config reloaded from disk.");
    } else if (moduleName == QStringLiteral("frontier.model_management")) {
        m_modelCacheUntil = QDateTime();
        buildModelCacheSummary();
        resultMessage = QStringLiteral("Model catalog and cache summary refreshed.");
    } else if (moduleName == QStringLiteral("frontier.modeling") || moduleName == QStringLiteral("frontier.judging")) {
        if (m_processes.value(QStringLiteral("inference")).process) {
            restartManagedProcess(QStringLiteral("inference"));
            resultMessage = QStringLiteral("Inference server restarted to pick up updated model code.");
        } else {
            resultMessage = QStringLiteral("Model changes will apply on the next inference or evaluation run.");
        }
    } else if (moduleName == QStringLiteral("frontier.data")
               || moduleName == QStringLiteral("dataset_pipeline.build_dataset")) {
        if (m_processes.value(QStringLiteral("prepare")).process) {
            restartManagedProcess(QStringLiteral("prepare"));
            resultMessage = QStringLiteral("Dataset preparation restarted with the updated prepare pipeline.");
        } else {
            resultMessage = QStringLiteral("Prepare pipeline changes will apply on the next prepare run.");
        }
    } else {
        invalidateRuntimeCaches();
        resultMessage = QStringLiteral("Runtime caches cleared. The next worker launch will use the updated module code.");
    }

    appendFixRow(m_fixHistory, QStringLiteral("hot_reload"), moduleName, resultMessage);
    recordLog(QStringLiteral("info"),
              QStringLiteral("repair"),
              QStringLiteral("hot_reload"),
              resultMessage,
              QJsonObject{{QStringLiteral("module"), moduleName}});
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("message"), resultMessage}};
}

QJsonObject ControlCenterBackend::clearRuntimeCaches() {
    invalidateRuntimeCaches();
    appendFixRow(m_fixHistory,
                 QStringLiteral("cache_clear"),
                 QStringLiteral("native_backend"),
                 QStringLiteral("Runtime caches cleared"));
    recordLog(QStringLiteral("info"),
              QStringLiteral("repair"),
              QStringLiteral("cache_clear"),
              QStringLiteral("Runtime caches cleared"));
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("message"), QStringLiteral("Runtime caches cleared")}};
}

QJsonObject ControlCenterBackend::clearFeedEvents() {
    m_feedClearCutoffTs = (static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0) + 0.001;
    QFile::remove(m_feedPath);
    clearIssuesForModule(QStringLiteral("feed"));
    invalidateRuntimeCaches();
    recordLog(QStringLiteral("info"),
              QStringLiteral("feed"),
              QStringLiteral("clear"),
              QStringLiteral("Live feed cleared"));
    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("message"), QStringLiteral("Live feed cleared")},
    };
}

bool ControlCenterBackend::repairFeedFile(QStringList* actions) {
    QFile file(m_feedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QStringList validLines;
    int invalidRows = 0;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
        if (!doc.isObject()) {
            ++invalidRows;
            continue;
        }
        validLines.append(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
    }

    if (invalidRows == 0) {
        return false;
    }
    const QByteArray rewritten = validLines.join(QLatin1Char('\n')).toUtf8() + QByteArray("\n");
    if (!rewriteTextFileLocked(m_feedPath, rewritten)) {
        return false;
    }
    if (actions) {
        actions->append(QStringLiteral("Rewrote feed log after removing %1 invalid row(s).").arg(invalidRows));
    }
    recordLog(QStringLiteral("warning"),
              QStringLiteral("repair"),
              QStringLiteral("feed_repair"),
              QStringLiteral("Removed invalid feed rows"),
              QJsonObject{{QStringLiteral("invalid_rows"), invalidRows}});
    return true;
}

QJsonObject ControlCenterBackend::buildIssueDeepDivePayload(const QString& key) const {
    const BackendIssueEntry entry = m_issues.value(key);
    if (entry.key.isEmpty()) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Unknown issue key")},
        };
    }

    QStringList hints;
    QStringList relatedProcesses;
    if (entry.module == QStringLiteral("data")) {
        relatedProcesses << QStringLiteral("setup") << QStringLiteral("prepare")
                         << QStringLiteral("inference") << QStringLiteral("evaluate");
        hints << QStringLiteral("If this is a cached-model lock, stop any process currently downloading or using that model before deleting it.")
              << QStringLiteral("If deletion is queued, the dashboard will treat the model as pending removal until handles are released or the machine restarts.");
    } else if (entry.module == QStringLiteral("process")) {
        relatedProcesses << QStringLiteral("setup") << QStringLiteral("prepare")
                         << QStringLiteral("training") << QStringLiteral("evaluate")
                         << QStringLiteral("inference");
        hints << QStringLiteral("Inspect the related process log for the first non-empty error line; worker launch failures usually show up there before they appear in the issue tracker.");
    } else if (entry.module.contains(QStringLiteral("hardware"), Qt::CaseInsensitive)) {
        relatedProcesses << QStringLiteral("setup") << QStringLiteral("training")
                         << QStringLiteral("inference");
        hints << QStringLiteral("Hardware issues often clear after a module hot reload or a short-lived worker restart if telemetry handles were stale.");
    } else {
        relatedProcesses << QStringLiteral("setup") << QStringLiteral("prepare")
                         << QStringLiteral("training") << QStringLiteral("evaluate")
                         << QStringLiteral("inference");
    }

    const QJsonArray recentLogs = recentBackendLogRows(120);
    QJsonArray matchedLogs;
    for (const QJsonValue& value : recentLogs) {
        const QJsonObject row = value.toObject();
        const QString category = row.value(QStringLiteral("category")).toString();
        const QString action = row.value(QStringLiteral("action")).toString();
        const QString message = row.value(QStringLiteral("message")).toString();
        const QJsonObject context = row.value(QStringLiteral("context")).toObject();
        const QString contextText = QString::fromUtf8(QJsonDocument(context).toJson(QJsonDocument::Compact));
        const bool directMatch =
            category.compare(entry.module, Qt::CaseInsensitive) == 0
            || action.contains(entry.module, Qt::CaseInsensitive)
            || message.contains(entry.error, Qt::CaseInsensitive)
            || message.contains(entry.module, Qt::CaseInsensitive)
            || contextText.contains(entry.module, Qt::CaseInsensitive)
            || contextText.contains(entry.error, Qt::CaseInsensitive);
        if (directMatch) {
            matchedLogs.append(row);
        }
    }

    QJsonArray processRows;
    for (const QString& processName : std::as_const(relatedProcesses)) {
        if (!m_processes.contains(processName)) {
            continue;
        }
        QJsonObject row{
            {QStringLiteral("name"), processName},
            {QStringLiteral("snapshot"), buildProcessSnapshot(processName)},
        };
        QStringList tail = m_processes.value(processName).logLines;
        if (tail.size() > 30) {
            tail = tail.mid(tail.size() - 30);
        }
        row.insert(QStringLiteral("tail_log"), QJsonArray::fromStringList(tail));
        processRows.append(row);
    }

    const QJsonObject environment{
        {QStringLiteral("python"), pythonPath()},
        {QStringLiteral("cache_dir"), QDir(m_rootPath).absoluteFilePath(
             m_config.value(QStringLiteral("large_judge")).toObject()
                 .value(QStringLiteral("cache_dir")).toString(QStringLiteral("data/cache/large_judge")))},
        {QStringLiteral("checkpoint_dir"), m_checkpointDir},
        {QStringLiteral("backend_log_path"), m_backendLogPath},
        {QStringLiteral("feed_path"), m_feedPath},
        {QStringLiteral("pending_delete_count"), m_pendingDeletePaths.size()},
    };

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("issue"), QJsonObject{
             {QStringLiteral("key"), entry.key},
             {QStringLiteral("module"), entry.module},
             {QStringLiteral("error"), entry.error},
             {QStringLiteral("count"), entry.count},
             {QStringLiteral("suppressed"), entry.suppressed},
             {QStringLiteral("last_seen"), entry.lastSeen},
         }},
        {QStringLiteral("hints"), QJsonArray::fromStringList(hints)},
        {QStringLiteral("related_logs"), matchedLogs},
        {QStringLiteral("related_processes"), processRows},
        {QStringLiteral("log_summary"), buildLogSummary()},
        {QStringLiteral("environment"), environment},
    };
}

QJsonObject ControlCenterBackend::runSelfHealAction(const QJsonObject& payload) {
    const bool aggressive = payload.value(QStringLiteral("aggressive")).toBool(false);
    QStringList actions;

    const QStringList directories = {
        rootPathFor(QStringLiteral("logs")),
        rootPathFor(QStringLiteral(".tmp")),
        rootPathFor(QStringLiteral("artifacts")),
        rootPathFor(QStringLiteral("checkpoints")),
        rootPathFor(QStringLiteral("data")),
    };
    for (const QString& path : directories) {
        if (!QFileInfo::exists(path)) {
            ensureDir(path);
            actions.append(QStringLiteral("Created missing directory: %1").arg(QFileInfo(path).fileName()));
        }
    }

    loadConfigFromDisk(true);
    if (m_config.isEmpty()) {
        QString resultMessage;
        if (recoverConfigFromBackup(&resultMessage)) {
            actions.append(resultMessage);
        } else if (recoverConfigFromBuiltInDefaults(&resultMessage)) {
            actions.append(resultMessage);
        }
    }

    repairFeedFile(&actions);
    clearRuntimeCaches();
    actions.append(QStringLiteral("Cleared runtime caches."));

    if (aggressive) {
        const QStringList restartable = {QStringLiteral("inference"), QStringLiteral("evaluate")};
        for (const QString& name : restartable) {
            const ManagedProcessState& state = m_processes.value(name);
            if (state.process && state.process->state() != QProcess::NotRunning) {
                restartManagedProcess(name);
                actions.append(QStringLiteral("Restarted %1 during aggressive self-heal.").arg(state.label));
            }
        }
    }

    const QJsonArray health = runHealthChecks();
    const QString message = actions.isEmpty()
        ? QStringLiteral("Self-heal ran cleanly. No corrective actions were needed.")
        : QStringLiteral("Self-heal completed %1 corrective action(s).").arg(actions.size());
    appendFixRow(m_fixHistory,
                 QStringLiteral("self_heal"),
                 aggressive ? QStringLiteral("aggressive") : QStringLiteral("standard"),
                 message,
                 actions.isEmpty() ? QStringLiteral("info") : QStringLiteral("warning"),
                 QJsonObject{{QStringLiteral("actions"), QJsonArray::fromStringList(actions)}});
    recordLog(actions.isEmpty() ? QStringLiteral("info") : QStringLiteral("warning"),
              QStringLiteral("repair"),
              QStringLiteral("self_heal"),
              message,
              QJsonObject{
                  {QStringLiteral("aggressive"), aggressive},
                  {QStringLiteral("actions"), QJsonArray::fromStringList(actions)},
              });

    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("message"), message},
        {QStringLiteral("actions"), QJsonArray::fromStringList(actions)},
        {QStringLiteral("health"), health},
    };
}

QJsonObject ControlCenterBackend::buildDiagnosticsPayload() {
    return QJsonObject{
        {QStringLiteral("health"), runHealthChecks()},
        {QStringLiteral("issues"), currentIssues()},
        {QStringLiteral("fixes_applied"), fixLog()},
        {QStringLiteral("logs"), recentBackendLogRows(40)},
        {QStringLiteral("log_summary"), buildLogSummary()},
        {QStringLiteral("reloadable_modules"), QJsonArray::fromStringList(m_reloadableModules)},
        {QStringLiteral("backend"), QJsonObject{
             {QStringLiteral("type"), QStringLiteral("native-cpp")},
             {QStringLiteral("version"), QStringLiteral("2.6.0")},
             {QStringLiteral("generated_at"), isoNow()},
             {QStringLiteral("self_heal_supported"), true},
             {QStringLiteral("structured_logging"), true},
         }},
    };
}

HttpResponse ControlCenterBackend::handleDiagnostics(const HttpRequest& request) {
    if (request.method == QStringLiteral("GET") && request.path == QStringLiteral("/api/diagnostics")) {
        return HttpResponse{200, "application/json; charset=utf-8", jsonBytes(buildDiagnosticsPayload())};
    }
    if (request.method == QStringLiteral("GET") && request.path == QStringLiteral("/api/diagnostics/issue")) {
        return HttpResponse{200, "application/json; charset=utf-8",
                            jsonBytes(buildIssueDeepDivePayload(request.query.value(QStringLiteral("key"))))};
    }
    if (request.method != QStringLiteral("POST")) {
        return handleMethodNotAllowed();
    }

    QJsonObject payload;
    if (!request.body.trimmed().isEmpty()) {
        QString parseError;
        if (!parseJsonObject(request.body, &payload, &parseError)) {
            return HttpResponse{400, "application/json; charset=utf-8", jsonBytes(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), parseError.isEmpty() ? QStringLiteral("Invalid JSON diagnostics payload") : parseError},
            })};
        }
    }
    if (request.path == QStringLiteral("/api/diagnostics/reload")) {
        return HttpResponse{200, "application/json; charset=utf-8",
                            jsonBytes(reloadModuleAction(payload.value(QStringLiteral("module")).toString()))};
    }
    if (request.path == QStringLiteral("/api/diagnostics/clear")) {
        const QString key = payload.value(QStringLiteral("key")).toString();
        if (key.isEmpty()) {
            clearAllIssues();
        } else {
            clearIssue(key);
        }
        return HttpResponse{200, "application/json; charset=utf-8", diagnosticsOk(QStringLiteral("Tracked issues cleared"))};
    }
    if (request.path == QStringLiteral("/api/diagnostics/run-checks")) {
        return HttpResponse{200, "application/json; charset=utf-8", diagnosticsOk(QStringLiteral("Health checks refreshed"),
            QJsonObject{{QStringLiteral("health"), runHealthChecks()}})};
    }
    if (request.path == QStringLiteral("/api/diagnostics/cache/clear")) {
        return HttpResponse{200, "application/json; charset=utf-8", jsonBytes(clearRuntimeCaches())};
    }
    if (request.path == QStringLiteral("/api/diagnostics/self-heal")) {
        return HttpResponse{200, "application/json; charset=utf-8", jsonBytes(runSelfHealAction(payload))};
    }

    return handleNotFound();
}

HttpResponse ControlCenterBackend::handleFeedControl(const HttpRequest& request) {
    if (request.method != QStringLiteral("POST")) {
        return handleMethodNotAllowed();
    }
    if (request.path == QStringLiteral("/api/feed/clear")) {
        return HttpResponse{200, "application/json; charset=utf-8", jsonBytes(clearFeedEvents())};
    }
    return handleNotFound();
}
