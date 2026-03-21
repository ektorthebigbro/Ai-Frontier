#include "AppController.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QClipboard>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>

namespace {

QString resolveBackendUrl() {
    const QByteArray envUrl = qgetenv("AI_FRONTIER_BACKEND_URL");
    if (!envUrl.isEmpty()) {
        return QString::fromUtf8(envUrl);
    }
    const QStringList args = QCoreApplication::arguments();
    const int idx = args.indexOf(QStringLiteral("--backend-url"));
    if (idx >= 0 && idx + 1 < args.size()) {
        return args.at(idx + 1);
    }
    return QStringLiteral("http://127.0.0.1:8765");
}

const QString kBaseUrl = resolveBackendUrl();

QString captureMatch(const QString& text, const QString& pattern, int group = 1)
{
    const QRegularExpressionMatch match =
        QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).match(text);
    return match.hasMatch() ? match.captured(group) : QString();
}

QString compactGpuName(const QString& value)
{
    if (value.trimmed().isEmpty()) {
        return QStringLiteral("--");
    }

    QString text = value.trimmed();
    text.replace(QStringLiteral("NVIDIA "), QString());
    text.replace(QStringLiteral("GeForce "), QString());
    return text;
}

QString formatUptime(double seconds)
{
    const qint64 totalSeconds = qMax<qint64>(0, static_cast<qint64>(seconds));
    const qint64 days = totalSeconds / 86400;
    const qint64 hours = (totalSeconds % 86400) / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;

    if (days > 0) {
        return QStringLiteral("%1d %2h").arg(days).arg(hours);
    }
    if (hours > 0) {
        return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
    }
    if (minutes > 0) {
        return QStringLiteral("%1m").arg(minutes);
    }
    return QStringLiteral("%1s").arg(totalSeconds);
}

QString humanizeEndpointLabel(const QString& endpoint)
{
    QString label = endpoint.trimmed();
    if (label.startsWith(QStringLiteral("/api/"))) {
        label = label.mid(QStringLiteral("/api/").size());
    }
    if (label.startsWith(QStringLiteral("actions/"))) {
        label = label.mid(QStringLiteral("actions/").size());
    }

    label.replace(QLatin1Char('/'), QLatin1Char(' '));
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    label = label.simplified();
    if (!label.isEmpty()) {
        label[0] = label.at(0).toUpper();
    }
    return label;
}

bool looksLikeProjectRoot(const QDir& dir)
{
    return QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("scripts/launcher.py")))
        && QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("configs/default.yaml")));
}

QString locateProjectRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 6; ++depth) {
        if (looksLikeProjectRoot(dir)) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    return QString();
}

QString locateLauncherScript()
{
    const QString root = locateProjectRoot();
    if (root.isEmpty()) {
        return QString();
    }
    return QDir(root).absoluteFilePath(QStringLiteral("scripts/launcher.py"));
}

QString locatePythonExecutable(const QString& projectRoot)
{
    const QStringList candidates = {
        QDir(projectRoot).absoluteFilePath(QStringLiteral(".venv/Scripts/python.exe")),
        QDir(projectRoot).absoluteFilePath(QStringLiteral(".venv/bin/python3")),
        QDir(projectRoot).absoluteFilePath(QStringLiteral(".venv/bin/python")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    const QString python3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (!python3.isEmpty()) {
        return python3;
    }
    return QStandardPaths::findExecutable(QStringLiteral("python"));
}

QString resolveProjectPath(const QString& rawPath)
{
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QFileInfo info(trimmed);
    if (info.isAbsolute()) {
        return info.absoluteFilePath();
    }

    const QString projectRoot = locateProjectRoot();
    if (projectRoot.isEmpty()) {
        return QFileInfo(trimmed).absoluteFilePath();
    }

    return QFileInfo(QDir(projectRoot).absoluteFilePath(trimmed)).absoluteFilePath();
}

bool startProjectLauncher(const QStringList& launcherArgs)
{
    const QString projectRoot = locateProjectRoot();
    const QString launcherScript = locateLauncherScript();
    const QString python = locatePythonExecutable(projectRoot);
    if (projectRoot.isEmpty() || launcherScript.isEmpty() || python.isEmpty()) {
        return false;
    }
    return QProcess::startDetached(python, QStringList{launcherScript} + launcherArgs, projectRoot);
}

QString actionEndpointForJob(const QString& action, const QString& jobName)
{
    const QString normalizedAction = action.trimmed().toLower();
    QString normalizedJob = jobName.trimmed().toLower();
    if (normalizedJob.isEmpty() || normalizedJob == QStringLiteral("server")) {
        normalizedJob = QStringLiteral("training");
    }

    if (normalizedJob == QStringLiteral("training")) {
        if (normalizedAction == QStringLiteral("pause")) return QStringLiteral("/api/actions/train/pause");
        if (normalizedAction == QStringLiteral("resume")) return QStringLiteral("/api/actions/train/resume");
        if (normalizedAction == QStringLiteral("stop")) return QStringLiteral("/api/actions/train/stop");
        return QStringLiteral("/api/actions/train/start");
    }

    if (normalizedJob == QStringLiteral("autopilot")) {
        if (normalizedAction == QStringLiteral("pause")) return QStringLiteral("/api/actions/autopilot/pause");
        if (normalizedAction == QStringLiteral("resume")) return QStringLiteral("/api/actions/autopilot/resume");
        if (normalizedAction == QStringLiteral("stop")) return QStringLiteral("/api/actions/autopilot/stop");
        return QStringLiteral("/api/actions/autopilot/start");
    }

    if (normalizedJob == QStringLiteral("inference")) {
        if (normalizedAction == QStringLiteral("pause")) return QStringLiteral("/api/actions/inference/pause");
        if (normalizedAction == QStringLiteral("resume")) return QStringLiteral("/api/actions/inference/resume");
        if (normalizedAction == QStringLiteral("stop")) return QStringLiteral("/api/actions/inference/stop");
        return QStringLiteral("/api/actions/inference/start");
    }

    if (normalizedAction == QStringLiteral("pause")) return QStringLiteral("/api/actions/%1/pause").arg(normalizedJob);
    if (normalizedAction == QStringLiteral("resume")) return QStringLiteral("/api/actions/%1/resume").arg(normalizedJob);
    if (normalizedAction == QStringLiteral("stop")) return QStringLiteral("/api/actions/%1").arg(normalizedJob);
    return QStringLiteral("/api/actions/%1").arg(normalizedJob);
}

QString diagTimestamp(double ts)
{
    if (ts <= 0.0) {
        return QStringLiteral("unknown");
    }
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(ts + 0.5)).toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QJsonObject parseReportObject(const QString& reportText)
{
    const QByteArray bytes = reportText.trimmed().toUtf8();
    if (bytes.isEmpty()) {
        return QJsonObject{};
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject{};
    }
    return doc.object();
}

QString formatDeepDiveText(const QJsonObject& payload)
{
    if (!payload.value(QStringLiteral("ok")).toBool(false)) {
        return payload.value(QStringLiteral("error")).toString(
            QStringLiteral("Issue deep-dive failed."));
    }

    const QJsonObject issue = payload.value(QStringLiteral("issue")).toObject();
    const QJsonArray hints = payload.value(QStringLiteral("hints")).toArray();
    const QJsonArray relatedLogs = payload.value(QStringLiteral("related_logs")).toArray();
    const QJsonArray relatedProcesses = payload.value(QStringLiteral("related_processes")).toArray();
    const QJsonObject env = payload.value(QStringLiteral("environment")).toObject();
    const QJsonObject logSummary = payload.value(QStringLiteral("log_summary")).toObject();

    QStringList lines;
    lines << QStringLiteral("ISSUE")
          << QStringLiteral("-----")
          << QStringLiteral("Key: %1").arg(issue.value(QStringLiteral("key")).toString())
          << QStringLiteral("Module: %1").arg(issue.value(QStringLiteral("module")).toString())
          << QStringLiteral("Count: %1").arg(issue.value(QStringLiteral("count")).toInt())
          << QStringLiteral("Last seen: %1").arg(diagTimestamp(
                 issue.value(QStringLiteral("last_seen")).toDouble(
                     issue.value(QStringLiteral("last_seen_ts")).toDouble())))
          << QStringLiteral("Error: %1").arg(issue.value(QStringLiteral("error")).toString(
                 issue.value(QStringLiteral("message")).toString()));

    if (!hints.isEmpty()) {
        lines << QString()
              << QStringLiteral("SUGGESTED NEXT STEPS")
              << QStringLiteral("--------------------");
        for (const QJsonValue& value : hints) {
            lines << QStringLiteral("- %1").arg(value.toString());
        }
    }

    lines << QString()
          << QStringLiteral("ENVIRONMENT")
          << QStringLiteral("-----------")
          << QStringLiteral("Python: %1").arg(env.value(QStringLiteral("python")).toString())
          << QStringLiteral("Cache dir: %1").arg(env.value(QStringLiteral("cache_dir")).toString())
          << QStringLiteral("Checkpoint dir: %1").arg(env.value(QStringLiteral("checkpoint_dir")).toString())
          << QStringLiteral("Pending deletes: %1").arg(env.value(QStringLiteral("pending_delete_count")).toInt())
          << QStringLiteral("Log summary: %1 error / %2 warning / %3 info")
                 .arg(logSummary.value(QStringLiteral("error_count")).toInt())
                 .arg(logSummary.value(QStringLiteral("warning_count")).toInt())
                 .arg(logSummary.value(QStringLiteral("info_count")).toInt());

    lines << QString()
          << QStringLiteral("MATCHED BACKEND LOGS")
          << QStringLiteral("--------------------");
    if (relatedLogs.isEmpty()) {
        lines << QStringLiteral("No matching structured backend log rows were found.");
    } else {
        for (const QJsonValue& value : relatedLogs) {
            const QJsonObject row = value.toObject();
            lines << QStringLiteral("[%1] %2 %3/%4")
                         .arg(diagTimestamp(row.value(QStringLiteral("ts")).toDouble()))
                         .arg(row.value(QStringLiteral("severity")).toString().toUpper())
                         .arg(row.value(QStringLiteral("category")).toString())
                         .arg(row.value(QStringLiteral("action")).toString())
                  << row.value(QStringLiteral("message")).toString();
            const QJsonObject context = row.value(QStringLiteral("context")).toObject();
            if (!context.isEmpty()) {
                lines << QString::fromUtf8(QJsonDocument(context).toJson(QJsonDocument::Indented)).trimmed();
            }
            lines << QString();
        }
    }

    lines << QStringLiteral("RELATED PROCESS SNAPSHOT")
          << QStringLiteral("------------------------");
    if (relatedProcesses.isEmpty()) {
        lines << QStringLiteral("No related managed processes were attached to this issue.");
    } else {
        for (const QJsonValue& value : relatedProcesses) {
            const QJsonObject row = value.toObject();
            const QString name = row.value(QStringLiteral("name")).toString();
            const QJsonObject snap = row.value(QStringLiteral("snapshot")).toObject();
            lines << QStringLiteral("%1").arg(name.toUpper())
                  << QStringLiteral("  state: %1").arg(snap.value(QStringLiteral("state")).toString())
                  << QStringLiteral("  paused: %1").arg(snap.value(QStringLiteral("paused")).toBool()
                         ? QStringLiteral("yes")
                         : QStringLiteral("no"))
                  << QStringLiteral("  exit code: %1").arg(snap.value(QStringLiteral("last_exit_code")).toInt())
                  << QStringLiteral("  updated: %1").arg(diagTimestamp(snap.value(QStringLiteral("updated_at")).toDouble()));
            const QJsonArray tail = row.value(QStringLiteral("tail_log")).toArray();
            if (tail.isEmpty()) {
                lines << QStringLiteral("  log: no recent process output");
            } else {
                lines << QStringLiteral("  recent log:");
                for (const QJsonValue& line : tail) {
                    lines << QStringLiteral("    %1").arg(line.toString());
                }
            }
            lines << QString();
        }
    }

    return lines.join(QStringLiteral("\n"));
}

}  // namespace

AppController::AppController(QObject* parent)
    : QObject(parent)
    , m_api(new ApiClient(kBaseUrl, this))
    , m_stateTimer(new QTimer(this))
    , m_diagTimer(new QTimer(this))
{
    connect(m_api, &ApiClient::stateReceived, this, &AppController::onStateReceived);
    connect(m_api, &ApiClient::stateFetchFailed, this, &AppController::onStateFetchFailed);
    connect(m_api, &ApiClient::diagnosticsReceived, this, &AppController::onDiagnosticsReceived);
    connect(m_api, &ApiClient::diagnosticsFetchFailed, this, &AppController::onDiagnosticsFetchFailed);
    connect(m_api, &ApiClient::actionDone, this, &AppController::onActionDone);
    connect(m_api, &ApiClient::generateDone, this, &AppController::onGenerateDone);
    connect(m_api, &ApiClient::configSaved, this, &AppController::onConfigSaved);

    connect(m_stateTimer, &QTimer::timeout, this, &AppController::pollState);
    m_stateTimer->setInterval(1800);
    m_stateTimer->start();

    connect(m_diagTimer, &QTimer::timeout, this, &AppController::pollDiagnostics);
    m_diagTimer->setInterval(4000);
    m_diagTimer->start();

    pollState();
    pollDiagnostics();
}

void AppController::pollState()
{
    if (m_stateRequestInFlight) {
        return;
    }
    m_stateRequestInFlight = true;
    m_api->fetchState();
}

void AppController::pollDiagnostics()
{
    if (m_diagRequestInFlight) {
        return;
    }
    m_diagRequestInFlight = true;
    m_api->fetchDiagnostics();
}

void AppController::onStateReceived(const QJsonObject& state)
{
    m_stateRequestInFlight = false;
    const bool wasConnected = m_connected;
    m_connected = true;
    if (!wasConnected) {
        emit connectedChanged();
    }

    if (!m_connectionError.isEmpty()) {
        m_connectionError.clear();
        emit connectionErrorChanged();
    }

    const QJsonObject primary = state.value(QStringLiteral("primary_job")).toObject();
    const QJsonObject jobs = state.value(QStringLiteral("jobs")).toObject();
    const QJsonObject training = jobs.value(QStringLiteral("training")).toObject();
    const QJsonObject hardware = state.value(QStringLiteral("hardware")).toObject();
    const QJsonObject config = state.value(QStringLiteral("config")).toObject();
    const QJsonObject trainingConfig = config.value(QStringLiteral("training")).toObject();
    const QJsonObject datasetsConfig = config.value(QStringLiteral("datasets")).toObject();
    const QJsonObject modelConfig = config.value(QStringLiteral("model")).toObject();
    const QJsonObject server = state.value(QStringLiteral("server")).toObject();
    const QString trainingMessage = training.value(QStringLiteral("message")).toString();
    const QJsonObject reportObject = parseReportObject(state.value(QStringLiteral("report")).toString());

    const QString stepText = captureMatch(trainingMessage, QStringLiteral("step\\s+(\\d+)"));
    const QString lossText = captureMatch(trainingMessage, QStringLiteral("loss=([\\d.]+)"));
    const QString rewardText = captureMatch(trainingMessage, QStringLiteral("reward(?:_score)?=([\\d.]+)"));
    const QString accuracyText = captureMatch(trainingMessage, QStringLiteral("acc(?:uracy)?=([\\d.]+)"));
    const QString epochCurrentText = captureMatch(trainingMessage, QStringLiteral("epoch\\s+(\\d+)"));
    const QString epochTotalText = captureMatch(trainingMessage, QStringLiteral("epoch\\s+\\d+\\s*/\\s*(\\d+)"));

    const QJsonObject metrics = state.value(QStringLiteral("metrics")).toObject();
    m_trainLoss = metrics.value(QStringLiteral("train_loss")).toDouble(
        lossText.isEmpty() ? reportObject.value(QStringLiteral("avg_loss")).toDouble(0.0)
                           : lossText.toDouble());
    m_valLoss = metrics.value(QStringLiteral("val_loss")).toDouble(
        reportObject.value(QStringLiteral("val_loss")).toDouble(
            reportObject.value(QStringLiteral("validation_loss")).toDouble(0.0)));

    const double accuracyFromMetrics = metrics.value(QStringLiteral("accuracy")).toDouble(-1.0);
    if (accuracyFromMetrics >= 0.0) {
        m_accuracy = accuracyFromMetrics;
    } else if (reportObject.value(QStringLiteral("accuracy")).isDouble()) {
        m_accuracy = reportObject.value(QStringLiteral("accuracy")).toDouble(0.0);
    } else if (reportObject.value(QStringLiteral("gsm8k_accuracy")).isDouble()) {
        m_accuracy = reportObject.value(QStringLiteral("gsm8k_accuracy")).toDouble(0.0);
    } else {
        m_accuracy = accuracyText.isEmpty() ? 0.0 : (accuracyText.toDouble() / 100.0);
    }

    m_progress = qBound(0.0, primary.value(QStringLiteral("progress")).toDouble(0.0), 1.0);
    m_currentJob = primary.value(QStringLiteral("job")).toString();
    m_stage = primary.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    m_status = primary.isEmpty() ? QStringLiteral("idle") : m_stage;

    const int configEpochs = trainingConfig.value(QStringLiteral("max_epochs")).toInt(0);
    m_epoch = epochCurrentText.isEmpty() ? 0 : epochCurrentText.toInt();
    m_totalEpochs = epochTotalText.isEmpty() ? configEpochs : epochTotalText.toInt();

    m_learningRate = trainingConfig.value(QStringLiteral("learning_rate")).toDouble(0.0);
    m_batchSize = trainingConfig.value(QStringLiteral("batch_size")).toInt(
        trainingConfig.value(QStringLiteral("micro_batch_size")).toInt(0));

    const int modelLayers = modelConfig.value(QStringLiteral("n_layers")).toInt(0);
    const int modelHeads = modelConfig.value(QStringLiteral("n_heads")).toInt(0);
    const int modelDim = modelConfig.value(QStringLiteral("d_model")).toInt(0);
    if (modelLayers > 0 && modelHeads > 0 && modelDim > 0) {
        m_modelArch = QStringLiteral("%1L / %2H / d%3").arg(modelLayers).arg(modelHeads).arg(modelDim);
    } else {
        m_modelArch = QStringLiteral("Training Control Center");
    }

    m_gpuUsage = hardware.value(QStringLiteral("gpu_utilization")).toDouble(0.0);
    m_cpuLoad = 0.0;
    const double ramUsed = hardware.value(QStringLiteral("ram_used_mb")).toDouble(0.0);
    const double ramTotal = hardware.value(QStringLiteral("ram_total_mb")).toDouble(0.0);
    m_ramUsage = ramTotal > 0.0 ? (ramUsed / ramTotal) * 100.0 : 0.0;
    m_uptime = formatUptime(server.value(QStringLiteral("uptime_seconds")).toDouble(0.0));

    if (m_trainLoss > 0.0) appendHistory(m_trainLossHistory, m_trainLoss, 2000);
    if (m_valLoss > 0.0) appendHistory(m_valLossHistory, m_valLoss, 2000);
    if (m_accuracy > 0.0) appendHistory(m_accuracyHistory, m_accuracy, 2000);
    const double rewardValue = rewardText.isEmpty()
        ? reportObject.value(QStringLiteral("avg_reward")).toDouble(-1.0)
        : rewardText.toDouble();
    if (rewardValue >= 0.0) appendHistory(m_rewardHistory, rewardValue, 2000);
    if (!primary.isEmpty()) appendHistory(m_progressHistory, m_progress * 100.0, 2000);
    appendHistory(m_gpuUsageHistory, m_gpuUsage, 2000);
    appendHistory(m_ramUsageHistory, m_ramUsage, 2000);

    m_fullState = state.toVariantMap();
    m_fullState.insert(QStringLiteral("yaml_config"), state.value(QStringLiteral("config_text")).toString());
    m_fullState.insert(QStringLiteral("logs"), state.value(QStringLiteral("feed")).toArray().toVariantList());

    QVariantMap system;
    system.insert(QStringLiteral("gpu_usage"), m_gpuUsage);
    system.insert(QStringLiteral("cpu_load"), m_cpuLoad);
    system.insert(QStringLiteral("ram_usage"), m_ramUsage);
    system.insert(QStringLiteral("uptime"), m_uptime);
    system.insert(QStringLiteral("gpu_name"), compactGpuName(hardware.value(QStringLiteral("gpu_name")).toString()));
    m_fullState.insert(QStringLiteral("system"), system);

    QVariantMap dataset;
    const int maxSamples = datasetsConfig.value(QStringLiteral("max_samples")).toInt(0);
    const double valRatio = trainingConfig.value(QStringLiteral("val_ratio")).toDouble(0.05);
    const double trainRatio = qBound(0.0, 1.0 - valRatio, 1.0);
    dataset.insert(QStringLiteral("train_ratio"), trainRatio);
    dataset.insert(QStringLiteral("val_ratio"), qBound(0.0, valRatio, 1.0));
    dataset.insert(QStringLiteral("total_samples"), maxSamples);
    dataset.insert(QStringLiteral("train_samples"), static_cast<int>(maxSamples * trainRatio));
    dataset.insert(QStringLiteral("val_samples"), static_cast<int>(maxSamples * qBound(0.0, valRatio, 1.0)));
    m_fullState.insert(QStringLiteral("dataset"), dataset);

    QVariantMap derivedMetrics;
    derivedMetrics.insert(QStringLiteral("train_loss"), m_trainLoss);
    derivedMetrics.insert(QStringLiteral("val_loss"), m_valLoss);
    derivedMetrics.insert(QStringLiteral("accuracy"), m_accuracy);
    if (rewardValue >= 0.0) {
        derivedMetrics.insert(QStringLiteral("reward"), rewardValue);
    }
    m_fullState.insert(QStringLiteral("metrics"), derivedMetrics);

    QVariantMap backendConnection;
    backendConnection.insert(QStringLiteral("connected"), true);
    backendConnection.insert(QStringLiteral("base_url"), kBaseUrl);
    backendConnection.insert(QStringLiteral("project_root"), locateProjectRoot());
    backendConnection.insert(QStringLiteral("launcher_script"), locateLauncherScript());
    backendConnection.insert(QStringLiteral("last_sync_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    backendConnection.insert(QStringLiteral("last_error"), QString());
    m_fullState.insert(QStringLiteral("backend_connection"), backendConnection);

    const QVariantMap partialDiagnostics = state.value(QStringLiteral("diagnostics")).toObject().toVariantMap();
    if (!partialDiagnostics.isEmpty()) {
        QVariantMap mergedDiagnostics = m_diagnostics;
        for (auto it = partialDiagnostics.begin(); it != partialDiagnostics.end(); ++it) {
            mergedDiagnostics.insert(it.key(), it.value());
        }
        m_diagnostics = mergedDiagnostics;
    }

    emit stateChanged();
    emit diagnosticsChanged();
}

void AppController::onStateFetchFailed(const QString& error)
{
    m_stateRequestInFlight = false;
    if (m_connected) {
        m_connected = false;
        emit connectedChanged();
    }

    if (m_connectionError != error) {
        m_connectionError = error;
        emit connectionErrorChanged();
    }

    QVariantMap backendConnection = m_fullState.value(QStringLiteral("backend_connection")).toMap();
    backendConnection.insert(QStringLiteral("connected"), false);
    backendConnection.insert(QStringLiteral("base_url"), kBaseUrl);
    backendConnection.insert(QStringLiteral("project_root"), locateProjectRoot());
    backendConnection.insert(QStringLiteral("launcher_script"), locateLauncherScript());
    backendConnection.insert(QStringLiteral("last_error"), error);
    backendConnection.insert(QStringLiteral("last_failure_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    m_fullState.insert(QStringLiteral("backend_connection"), backendConnection);
    emit stateChanged();
}

void AppController::onDiagnosticsReceived(const QJsonObject& diag)
{
    m_diagnostics = diag.toVariantMap();
    m_diagnostics.remove(QStringLiteral("fetch_error"));
    m_diagRequestInFlight = false;
    emit diagnosticsChanged();
}

void AppController::onDiagnosticsFetchFailed(const QString& error)
{
    m_diagRequestInFlight = false;
    m_diagnostics.insert(QStringLiteral("fetch_error"), error);
    emit diagnosticsChanged();
}

void AppController::onActionDone(const QJsonObject& response)
{
    const bool ok = response.value(QStringLiteral("ok")).toBool(true);
    const QString msg = response.value(QStringLiteral("message")).toString(
        response.value(QStringLiteral("error")).toString());

    if (m_pendingIsReport) {
        m_pendingIsReport = false;
    }

    if (!m_pendingAction.isEmpty()) {
        const QString resolvedMessage = msg.isEmpty()
            ? (ok ? QStringLiteral("Success") : QStringLiteral("Action failed"))
            : msg;
        emit actionCompleted(m_pendingAction, ok, resolvedMessage);
        emit toastRequested(
            ok ? QStringLiteral("%1: %2").arg(m_pendingAction, resolvedMessage)
               : QStringLiteral("Action failed: %1").arg(resolvedMessage),
            ok ? QStringLiteral("success") : QStringLiteral("error"));
        m_pendingAction.clear();
    }

    pollState();
    pollDiagnostics();
}

void AppController::onGenerateDone(const QJsonObject& response)
{
    if (m_pendingIsChat) {
        m_pendingIsChat = false;
        const bool ok = !response.contains(QStringLiteral("error"));
        const QString generatedText = response.value(QStringLiteral("generated")).toString(
            response.value(QStringLiteral("response")).toString());
        const QString errorText = response.value(QStringLiteral("error")).toString(
            response.value(QStringLiteral("message")).toString());
        if (ok) {
            emit chatResponseReceived(generatedText.isEmpty()
                                          ? QStringLiteral("[No text returned]")
                                          : generatedText);
        } else {
            emit chatResponseReceived(QStringLiteral("[Error: %1]").arg(
                errorText.isEmpty() ? QStringLiteral("no response") : errorText));
            emit toastRequested(QStringLiteral("Chat request failed"), QStringLiteral("error"));
        }
    } else if (m_pendingIsReport) {
        m_pendingIsReport = false;
        const bool ok = !response.contains(QStringLiteral("error"));
        emit toastRequested(
            ok ? QStringLiteral("Report generated") : QStringLiteral("Report failed"),
            ok ? QStringLiteral("success") : QStringLiteral("error"));
    }

    pollState();
    pollDiagnostics();
}

void AppController::onConfigSaved(bool ok, const QString& message)
{
    emit toastRequested(
        ok ? (message.isEmpty() ? QStringLiteral("Config saved") : message)
           : (message.isEmpty() ? QStringLiteral("Save failed") : message),
        ok ? QStringLiteral("success") : QStringLiteral("error"));

    pollState();
    pollDiagnostics();
}

void AppController::sendAction(const QString& action, const QVariantMap& params)
{
    Q_UNUSED(params)
    postActionPath(actionEndpointForJob(action, m_currentJob));
}

void AppController::postActionPath(const QString& endpoint)
{
    if (endpoint.trimmed().isEmpty()) {
        return;
    }
    m_pendingAction = humanizeEndpointLabel(endpoint);
    m_api->postAction(endpoint);
}

void AppController::saveConfig(const QVariantMap& config)
{
    QJsonObject obj;
    for (auto it = config.begin(); it != config.end(); ++it) {
        obj[it.key()] = QJsonValue::fromVariant(it.value());
    }
    m_api->postConfig(obj);
}

void AppController::saveConfigYaml(const QString& yaml)
{
    m_api->postConfigYaml(yaml);
}

void AppController::sendChatMessage(const QString& message, const QVariantMap& params)
{
    QJsonObject body;
    body.insert(QStringLiteral("prompt"), message);
    for (auto it = params.begin(); it != params.end(); ++it) {
        body.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    m_pendingIsChat = true;
    m_pendingIsReport = false;
    m_api->postGenerate(body);
}

void AppController::runDiagnosticChecks()
{
    m_pendingAction = QStringLiteral("Diagnostics");
    m_api->postDiagnosticsRunChecks();
}

void AppController::selfHeal()
{
    m_pendingAction = QStringLiteral("Self-heal");
    m_api->postDiagnosticsSelfHeal();
}

void AppController::reloadModule(const QString& module)
{
    m_pendingAction = module + QStringLiteral(" reload");
    m_api->postDiagnosticsReload(module);
}

void AppController::clearDiagnostics()
{
    m_pendingAction = QStringLiteral("Clear diagnostics");
    m_api->postDiagnosticsClear();
}

void AppController::clearDiagnosticKey(const QString& key)
{
    m_pendingAction = QStringLiteral("Clear issue");
    m_api->postDiagnosticsClear(key);
}

void AppController::fetchDiagnosticIssue(const QString& key)
{
    const QString trimmedKey = key.trimmed();
    if (trimmedKey.isEmpty()) {
        emit diagnosticIssueLoaded({}, QStringLiteral("No issue key provided."), false);
        return;
    }

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), trimmedKey);
    m_api->getJson(
        QStringLiteral("/api/diagnostics/issue?%1").arg(query.toString(QUrl::FullyEncoded)),
        [this](const QJsonObject& payload) {
            const bool ok = payload.value(QStringLiteral("ok")).toBool(true);
            if (!ok && payload.value(QStringLiteral("error")).toString() == QStringLiteral("Unknown issue key")) {
                emit diagnosticIssueLoaded(
                    payload.value(QStringLiteral("issue")).toObject().toVariantMap(),
                    QStringLiteral(
                        "This issue is no longer active.\n\nRefresh Diagnostics and reopen the deep dive."),
                    false);
                return;
            }

            emit diagnosticIssueLoaded(
                payload.value(QStringLiteral("issue")).toObject().toVariantMap(),
                ok ? formatDeepDiveText(payload)
                   : payload.value(QStringLiteral("error")).toString(
                         QStringLiteral("Issue deep-dive failed.")),
                ok);
        },
        [this, trimmedKey](const QString& error) {
            QVariantMap issue;
            issue.insert(QStringLiteral("key"), trimmedKey);
            emit diagnosticIssueLoaded(
                issue,
                QStringLiteral("Could not load deep-dive report for %1\n\n%2").arg(trimmedKey, error),
                false);
        });
}

void AppController::launchBackend()
{
    const bool started = startProjectLauncher({QStringLiteral("server")});

    emit toastRequested(
        started ? QStringLiteral("Backend launch requested")
                : QStringLiteral("Could not launch backend from scripts/launcher.py"),
        started ? QStringLiteral("success") : QStringLiteral("error"));

    if (started) {
        QTimer::singleShot(1200, this, [this]() {
            pollState();
            pollDiagnostics();
        });
    }
}

void AppController::generateReport()
{
    m_pendingIsReport = true;
    m_pendingIsChat = false;
    m_pendingAction = QStringLiteral("Evaluation");
    m_api->postAction(QStringLiteral("/api/actions/evaluate"));
}

void AppController::openLocalPath(const QString& path)
{
    const QFileInfo info(resolveProjectPath(path));
    if (path.trimmed().isEmpty() || !info.exists()) {
        emit toastRequested(QStringLiteral("File not found"), QStringLiteral("error"));
        return;
    }

    const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
    emit toastRequested(
        opened ? QStringLiteral("Opened artifact") : QStringLiteral("Could not open file"),
        opened ? QStringLiteral("success") : QStringLiteral("error"));
}

void AppController::copyText(const QString& text, const QString& successMessage)
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        emit toastRequested(QStringLiteral("Clipboard is unavailable"), QStringLiteral("error"));
        return;
    }

    clipboard->setText(text);
    emit toastRequested(
        successMessage.trimmed().isEmpty() ? QStringLiteral("Copied to clipboard") : successMessage.trimmed(),
        QStringLiteral("success"));
}

void AppController::appendHistory(QVariantList& history, double value, int limit)
{
    if (!qIsFinite(value)) {
        return;
    }

    history.append(value);
    while (history.size() > limit) {
        history.removeFirst();
    }
}
