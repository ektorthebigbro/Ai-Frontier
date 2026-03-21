#pragma once

#include "http/simple_http_server.h"
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <functional>

struct BackendIssueEntry {
    QString key;
    QString module;
    QString error;
    int count = 1;
    bool suppressed = false;
    double lastSeen = 0.0;
};

struct ManagedProcessState {
    QString name;
    QString label;
    QString scriptPath;
    QStringList extraArgs;
    QPointer<QProcess> process;
    QStringList logLines;
    bool stopRequested = false;
    bool paused = false;
    bool pauseRequested = false;
    double startedAt = 0.0;
    int lastExitCode = 0;
    qint64 recoveredPid = 0;
    double lastConsoleProgress = -1.0;
    QString lastConsoleProgressKey;
};

struct BackendRouteMetric {
    QString key;
    QString method;
    QString path;
    qint64 hits = 0;
    qint64 errorHits = 0;
    int lastStatusCode = 0;
    double avgLatencyMs = 0.0;
    double lastLatencyMs = 0.0;
    double lastSeen = 0.0;
};

struct BackendRequestLogEntry {
    double ts = 0.0;
    QString method;
    QString path;
    QString routeKey;
    QString clientAddress;
    int statusCode = 0;
    qint64 responseBytes = 0;
    double latencyMs = 0.0;
};

struct BackendLogEntry {
    double ts = 0.0;
    QString severity;
    QString category;
    QString action;
    QString message;
    QJsonObject context;
};

class ControlCenterBackend : public QObject {
    Q_OBJECT

public:
    explicit ControlCenterBackend(QObject* parent = nullptr);

    bool start(quint16 port);
    quint16 configuredPort() const;
    void registerRoutes();
    HttpResponse handleRequest(const HttpRequest& request);
    QString handleConsoleCommand(const QString& line, bool* quitRequested = nullptr);
    void printConsoleLine(const QString& line, bool isError = false) const;

private slots:
    void onProcessOutput(const QString& name);
    void onProcessFinished(const QString& name, int exitCode, QProcess::ExitStatus status);

private:
    void initializePaths();
    void initializeProcessCatalog();
    void loadConfigFromDisk(bool force = false);
    bool saveConfigObject(const QJsonObject& config);
    bool saveConfigText(const QString& text, QString* errorMessage = nullptr);

    HttpResponse handleRoot() const;
    HttpResponse handleState();
    HttpResponse handleConfig(const HttpRequest& request);
    HttpResponse handleAction(const QString& path);
    HttpResponse handleDiagnostics(const HttpRequest& request);
    HttpResponse handleFeedControl(const HttpRequest& request);
    HttpResponse handleGenerate(const HttpRequest& request);
    HttpResponse handleServerMeta() const;
    HttpResponse handleServerHealth() const;
    HttpResponse handleServerMetrics() const;
    HttpResponse handleServerRoutes() const;
    HttpResponse handleServerProcesses();
    HttpResponse handleServerRequestLog(const HttpRequest& request) const;
    HttpResponse handleServerLogs(const HttpRequest& request) const;
    HttpResponse handleServerLogEvents(const HttpRequest& request) const;
    HttpResponse handleServerFiles();
    HttpResponse handleServerRestartProcess(const HttpRequest& request);
    HttpResponse handleServerClearProcessLog(const HttpRequest& request);
    HttpResponse handleNotFound() const;
    HttpResponse handleMethodNotAllowed() const;
    HttpResponse runInstrumented(const HttpRequest& request,
                                 const QString& routeKey,
                                 const std::function<HttpResponse()>& handler);

    QJsonObject buildStatePayload();
    QJsonObject buildDiagnosticsPayload();
    QJsonObject buildHardwareSnapshot();
    QJsonObject buildServerHealthPayload() const;
    QJsonObject buildServerMetricsPayload() const;
    QJsonObject buildModelCatalog() const;
    QJsonObject buildModelCacheSummary();
    QJsonObject buildCheckpointSummary() const;
    QJsonObject buildRecoverySummary(const QJsonObject& jobs, const QJsonObject& processes) const;
    QJsonObject buildHistoryPayload(int actionLimit = 40, int fileLimit = 28);
    QJsonObject buildProcessSnapshot(const QString& name) const;
    QJsonObject summarizeJob(const QString& name);
    QJsonObject choosePrimaryJob(const QJsonObject& jobs, const QJsonObject& processes) const;
    QJsonArray buildRecentRequestRows(int maxRows) const;
    QJsonArray buildActionHistory(int maxRows) const;
    QJsonArray buildFileHistory(int maxRows);
    QJsonArray recentFeedRows(int maxRows) const;
    QJsonArray recentMetricsRows(int maxRows) const;
    QJsonArray recentAlerts(int maxRows) const;
    QJsonArray recentBackendLogRows(int maxRows) const;
    QString latestReportText() const;
    QString latestCheckpointPath() const;
    QJsonObject buildLogSummary() const;

    bool startManagedProcess(const QString& name, const QString& scriptPath, const QStringList& extraArgs = {});
    bool stopManagedProcess(const QString& name);
    bool pauseManagedProcess(const QString& name);
    bool resumeManagedProcess(const QString& name);
    bool restartManagedProcess(const QString& name);
    QStringList buildCommand(const QString& scriptPath, const QStringList& extraArgs) const;
    void appendProcessLog(ManagedProcessState& state, const QString& line);

    void recordFeedEvent(const QJsonObject& row);
    void recordLog(const QString& severity,
                   const QString& category,
                   const QString& action,
                   const QString& message,
                   const QJsonObject& context = {});
    void recordEvent(const QString& type, const QString& message);
    void recordAlert(const QString& severity, const QString& message);
    void recordJobUpdate(const QString& job, const QString& stage, const QString& message, double progress, bool trackIssue = true);
    void recordRequestMetric(const QString& routeKey,
                             const HttpRequest& request,
                             int statusCode,
                             qint64 responseBytes,
                             double latencyMs);
    void invalidateRuntimeCaches();
    void noteInvalidFeedRows(int invalidRows);

    void recordIssue(const QString& module, const QString& error);
    void clearIssue(const QString& key);
    void clearIssuesForModule(const QString& module);
    void clearAllIssues();
    void clearAcknowledgedRuntimeState();
    QJsonArray currentIssues() const;
    QJsonArray fixLog() const;
    QJsonArray runHealthChecks();
    QJsonObject reloadModuleAction(const QString& moduleName);
    QJsonObject clearRuntimeCaches();
    QJsonObject runSelfHealAction(const QJsonObject& payload = {});
    QJsonObject buildIssueDeepDivePayload(const QString& key) const;
    bool backupConfigSnapshot(const QString& text);
    bool recoverConfigFromBackup(QString* resultMessage = nullptr);
    bool recoverConfigFromBuiltInDefaults(QString* resultMessage = nullptr);
    bool repairFeedFile(QStringList* actions = nullptr);
    QJsonObject clearFeedEvents();

    bool clearModelCache(QString* resultMessage = nullptr);
    QJsonObject checkDataIntegrity();
    bool removeModelFromCache(const QString& modelId, QString* resultMessage = nullptr);
    bool deleteManagedProjectPath(const QString& relativePath, QString* resultMessage = nullptr);
    bool clearActionHistory(QString* resultMessage = nullptr);
    bool deleteActionHistoryEntry(const QString& signature, QString* resultMessage = nullptr);
    QJsonObject cleanupProjectFiles(const QString& scope);

    void ensureAutopilotState();
    bool startAutopilot();
    bool stopAutopilot();
    bool pauseAutopilot();
    bool resumeAutopilot();
    bool continueAutopilotFromStage(const QString& stage);
    void advanceAutopilot();
    void failAutopilot(const QString& message,
                       double progress = -1.0,
                       bool emitAlert = true,
                       bool trackIssue = true);
    QString recommendedAutopilotStage();

    bool ensureInferenceReady(QString* errorMessage);
    QJsonObject proxyInferenceGenerate(const QJsonObject& payload, QString* errorMessage);
    QString consoleHelpText() const;
    QString formatConsoleProcessRow(const QString& name) const;
    QString runtimeStatePath(const QString& name) const;
    QString autopilotRuntimeStatePath() const;
    QString trainingPauseRequestPath() const;
    QStringList trainingResumeArgs() const;
    bool requestTrainingPause();
    bool waitForTrainingPause(qint64 pid, int timeoutMs);
    bool waitForManagedPause(const QString& name, qint64 pid, int timeoutMs);
    void clearTrainingPauseRequest();
    void finalizeManagedPausedState(const QString& name, const QString& message);
    void finalizeTrainingPausedState(const QString& message);
    void persistManagedRuntimeState(const QString& name);
    void clearManagedRuntimeState(const QString& name);
    void refreshRecoveredProcessStates();
    void persistAutopilotRuntimeState();
    void restoreAutopilotRuntimeState();
    bool isManagedProcessRunning(const ManagedProcessState& state) const;
    qint64 managedProcessId(const ManagedProcessState& state) const;
    double earliestRelevantFeedTimestamp() const;
    QJsonObject inferJobStateFromDisk(const QString& name) const;
    void maybeRecordRecoveryPointAlert(const QJsonObject& recovery);

    QString m_rootPath;
    QString m_configPath;
    QString m_configBackupPath;
    QString m_feedPath;
    QString m_backendLogPath;
    QString m_runtimeStateDir;
    QString m_reportDir;
    QString m_checkpointDir;
    QString m_setupScript;
    QDateTime m_startedAt;
    qint64 m_requestCount = 0;

    SimpleHttpServer m_server;
    QNetworkAccessManager m_network;
    QJsonObject m_config;
    QString m_configText;
    QDateTime m_configMtime;

    QHash<QString, ManagedProcessState> m_processes;
    QList<QJsonObject> m_alerts;
    QJsonObject m_autopilot;
    QHash<QString, BackendRouteMetric> m_routeMetrics;
    QList<BackendRequestLogEntry> m_recentRequests;
    QList<BackendLogEntry> m_recentBackendLogs;
    QHash<QString, QString> m_lastRecordedJobStage;
    QString m_lastRecoveryNotificationKey;
    bool m_recoveryNotificationPrimed = false;
    QSet<QString> m_loggedStateBackupRecoveryPaths;
    int m_lastInvalidFeedRowCount = 0;
    double m_feedClearCutoffTs = 0.0;

    QHash<QString, BackendIssueEntry> m_issues;
    QList<QJsonObject> m_fixHistory;
    QStringList m_reloadableModules;
    QSet<QString> m_pendingDeletePaths;
    QString m_exclusiveOperationName;

    QDateTime m_stateCacheUntil;
    QJsonObject m_stateCache;
    QDateTime m_hardwareCacheUntil;
    QJsonObject m_hardwareCache;
    QDateTime m_modelCacheUntil;
    QJsonObject m_modelCache;
};
