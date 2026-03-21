#include "../backend.h"
#include "../common/backend_common.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace ControlCenterBackendCommon;

namespace {

QString renderConsoleProgressBar(double progress, int width = 28) {
    const double clamped = qBound(0.0, progress, 1.0);
    const int filled = qRound(clamped * width);
    return QString(width, QLatin1Char('#')).left(filled)
        + QString(width, QLatin1Char('-')).mid(filled);
}

QString simplifyProgressMessage(const QString& line) {
    QString text = line;
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

bool shouldEmitConsoleProgress(ManagedProcessState& state, double progress, const QString& key) {
    if (state.lastConsoleProgressKey == key && qAbs(state.lastConsoleProgress - progress) < 0.01) {
        return false;
    }
    state.lastConsoleProgress = progress;
    state.lastConsoleProgressKey = key;
    return true;
}

bool parseStructuredProgressLine(const QString& line,
                                 QString* job,
                                 QString* stage,
                                 QString* message,
                                 double* progress) {
    static const QRegularExpression marker(
        QStringLiteral("^AI_PROGRESS\\|([^|]+)\\|([^|]+)\\|([0-9]*\\.?[0-9]+)\\|(.*)$"));
    const QRegularExpressionMatch match = marker.match(line);
    if (!match.hasMatch()) {
        return false;
    }

    if (job) {
        *job = match.captured(1).trimmed();
    }
    if (stage) {
        *stage = match.captured(2).trimmed();
    }
    if (progress) {
        *progress = qBound(0.0, match.captured(3).toDouble(), 1.0);
    }
    if (message) {
        *message = simplifyProgressMessage(match.captured(4));
    }
    return true;
}

bool parseDownloadProgressLine(const QString& processName,
                               const QString& line,
                               QString* message,
                               double* progress) {
    if (processName != QStringLiteral("setup") && processName != QStringLiteral("prepare")) {
        return false;
    }

    const QString simplified = simplifyProgressMessage(line);
    const bool looksLikeTransfer =
        simplified.contains(QStringLiteral("download"), Qt::CaseInsensitive) ||
        simplified.contains(QStringLiteral("%|")) ||
        simplified.contains(QStringLiteral("MB/s"), Qt::CaseInsensitive) ||
        simplified.contains(QStringLiteral("GB/s"), Qt::CaseInsensitive) ||
        simplified.contains(QStringLiteral("kB/s"), Qt::CaseInsensitive);
    if (!looksLikeTransfer) {
        return false;
    }

    static const QRegularExpression pctExpr(QStringLiteral("(\\d{1,3}(?:\\.\\d+)?)%"));
    const QRegularExpressionMatch pctMatch = pctExpr.match(simplified);
    if (pctMatch.hasMatch()) {
        if (progress) {
            *progress = qBound(0.0, pctMatch.captured(1).toDouble() / 100.0, 1.0);
        }
        if (message) {
            *message = simplified;
        }
        return true;
    }

    static const QRegularExpression fractionExpr(
        QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*/\\s*(\\d+(?:\\.\\d+)?)\\s*([KMGTP]?i?B|[KMGTP]?B)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch fractionMatch = fractionExpr.match(simplified);
    if (!fractionMatch.hasMatch()) {
        return false;
    }

    const double current = fractionMatch.captured(1).toDouble();
    const double total = fractionMatch.captured(2).toDouble();
    if (total <= 0.0) {
        return false;
    }

    if (progress) {
        *progress = qBound(0.0, current / total, 1.0);
    }
    if (message) {
        *message = simplified;
    }
    return true;
}

bool shouldSuppressProcessNoiseLine(const QString& processName, const QString& line) {
    if (processName != QStringLiteral("setup") && processName != QStringLiteral("prepare")) {
        return false;
    }

    const QString simplified = simplifyProgressMessage(line);
    if (simplified.contains(QStringLiteral("HTTP Request:"), Qt::CaseInsensitive)) {
        return true;
    }
    if (simplified.contains(QStringLiteral("unauthenticated requests to the HF Hub"), Qt::CaseInsensitive)) {
        return true;
    }
    if (simplified.contains(QStringLiteral("huggingface_hub` cache-system uses symlinks by default"),
                            Qt::CaseInsensitive)) {
        return true;
    }
    if (simplified.contains(QStringLiteral("To support symlinks on Windows"), Qt::CaseInsensitive)) {
        return true;
    }
    if (simplified == QStringLiteral("warnings.warn(message)")) {
        return true;
    }

    static const QRegularExpression tqdmFetchExpr(
        QStringLiteral("^(?:\\[dry-run\\]\\s*)?Fetching\\s+\\d+\\s+files:"),
        QRegularExpression::CaseInsensitiveOption);
    if (tqdmFetchExpr.match(simplified).hasMatch()) {
        return true;
    }
    if (simplified.contains(QStringLiteral("it/s")) && simplified.contains(QLatin1Char('|'))) {
        return true;
    }
    return false;
}

double mappedDownloadProgress(const QString& jobName, const QString& stage, double progress) {
    const double clamped = qBound(0.0, progress, 1.0);
    if (jobName == QStringLiteral("setup")) {
        if (stage == QStringLiteral("install_dependencies")) {
            return 0.55 + (0.72 - 0.55) * clamped;
        }
        if (stage == QStringLiteral("install_torch")) {
            return 0.72 + (0.88 - 0.72) * clamped;
        }
    }
    if (jobName == QStringLiteral("prepare") && stage == QStringLiteral("model_download")) {
        return 0.04 + (0.14 - 0.04) * clamped;
    }
    return -1.0;
}

QString normalizeProcessFailureLine(const QString& line) {
    QString cleaned = line.trimmed();
    cleaned.remove(QRegularExpression(
        QStringLiteral("^\\d{4}-\\d{2}-\\d{2}\\s+\\d{2}:\\d{2}:\\d{2}(?:,\\d+)?\\s+[A-Z]+\\s+[^:]+:\\s*")));
    cleaned.remove(QRegularExpression(QStringLiteral("^(?:\\[native-backend\\]\\s*)")));
    return cleaned.trimmed();
}

QString latestProcessFailureMessage(const ManagedProcessState& state, const QString& fallbackLabel, int exitCode) {
    QString fallbackLine;
    for (auto it = state.logLines.crbegin(); it != state.logLines.crend(); ++it) {
        const QString line = it->trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("[progress]"))) {
            continue;
        }
        if (fallbackLine.isEmpty()) {
            fallbackLine = normalizeProcessFailureLine(line);
        }
        const bool looksSevere =
            line.contains(QStringLiteral("traceback"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("exception"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("failed"), Qt::CaseInsensitive) ||
            line.contains(QStringLiteral("abort"), Qt::CaseInsensitive);
        if (looksSevere) {
            const QString normalized = normalizeProcessFailureLine(line);
            if (!normalized.isEmpty()) {
                return QStringLiteral("%1 failed: %2").arg(fallbackLabel, normalized);
            }
        }
    }

    if (!fallbackLine.isEmpty()) {
        return QStringLiteral("%1 failed: %2").arg(fallbackLabel, fallbackLine);
    }
    return QStringLiteral("%1 exited with code %2").arg(fallbackLabel).arg(exitCode);
}

QByteArray okBytes(const QString& message, const QJsonObject& extra = {}) {
    QJsonObject payload = extra;
    payload.insert(QStringLiteral("ok"), true);
    payload.insert(QStringLiteral("message"), message);
    return jsonBytes(payload);
}

QByteArray failBytes(const QString& message, const QJsonObject& extra = {}) {
    QJsonObject payload = extra;
    payload.insert(QStringLiteral("ok"), false);
    payload.insert(QStringLiteral("error"), message);
    return jsonBytes(payload);
}

bool setProcessSuspended(qint64 processId, bool suspend) {
#ifdef Q_OS_WIN
    using NtProcessControl = LONG (WINAPI*)(HANDLE);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }
    const char* symbol = suspend ? "NtSuspendProcess" : "NtResumeProcess";
    auto function = reinterpret_cast<NtProcessControl>(GetProcAddress(ntdll, symbol));
    if (!function) {
        return false;
    }
    HANDLE handle = OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_QUERY_INFORMATION, FALSE, static_cast<DWORD>(processId));
    if (!handle) {
        return false;
    }
    const LONG result = function(handle);
    CloseHandle(handle);
    return result == 0;
#else
    Q_UNUSED(processId);
    Q_UNUSED(suspend);
    return false;
#endif
}

bool isProcessAlive(qint64 processId) {
#ifdef Q_OS_WIN
    if (processId <= 0) {
        return false;
    }
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
    if (!handle) {
        return false;
    }
    DWORD exitCode = 0;
    const bool ok = GetExitCodeProcess(handle, &exitCode) != 0 && exitCode == STILL_ACTIVE;
    CloseHandle(handle);
    return ok;
#else
    Q_UNUSED(processId);
    return false;
#endif
}

bool terminateProcessById(qint64 processId) {
#ifdef Q_OS_WIN
    if (processId <= 0) {
        return false;
    }
    HANDLE handle = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                FALSE,
                                static_cast<DWORD>(processId));
    if (!handle) {
        return false;
    }
    const bool ok = TerminateProcess(handle, 1) != 0;
    if (ok) {
        WaitForSingleObject(handle, 5000);
    }
    CloseHandle(handle);
    return ok;
#else
    Q_UNUSED(processId);
    return false;
#endif
}

QString normalizedPath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool pathEqualsOrWithinNormalized(const QString& path, const QString& root) {
    const QString normalized = normalizedPath(path);
    const QString normalizedRoot = normalizedPath(root);
    return normalized == normalizedRoot
        || normalized.startsWith(normalizedRoot + QLatin1Char('/'))
        || normalized.startsWith(normalizedRoot + QLatin1Char('\\'));
}

QString projectManagedPathFromConfig(const QString& rootPath,
                                     const QString& configuredPath,
                                     const QString& fallbackRelative) {
    const QString candidate = configuredPath.trimmed().isEmpty() ? fallbackRelative : configuredPath.trimmed();
    const QString absolute = QDir::isAbsolutePath(candidate)
        ? normalizedPath(candidate)
        : normalizedPath(QDir(rootPath).absoluteFilePath(candidate));
    if (!pathEqualsOrWithinNormalized(absolute, rootPath)) {
        return QString();
    }
    return absolute;
}

bool waitForProcessExitById(qint64 pid, int timeoutMs) {
    if (pid <= 0) {
        return true;
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (!isProcessAlive(pid)) {
            return true;
        }
        QThread::msleep(50);
    }
    return !isProcessAlive(pid);
}

bool scheduleDeleteOnReboot(const QString& path) {
#ifdef Q_OS_WIN
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    return MoveFileExW(native.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != 0;
#else
    Q_UNUSED(path);
    return false;
#endif
}

QString pendingDeletePathFor(const QString& modelId) {
    const QString root = rootPathFor(QStringLiteral(".tmp/pending_deletes"));
    ensureDir(root);
    return QDir(root).absoluteFilePath(
        QStringLiteral("%1__%2")
            .arg(sanitizeModelDir(modelId),
                 QString::number(QDateTime::currentMSecsSinceEpoch())));
}

QString nextAutopilotStage(const QString& stage) {
    if (stage == QStringLiteral("setup")) {
        return QStringLiteral("prepare");
    }
    if (stage == QStringLiteral("prepare")) {
        return QStringLiteral("training");
    }
    if (stage == QStringLiteral("training")) {
        return QStringLiteral("evaluate");
    }
    return QStringLiteral("completed");
}

QString normalizeAutopilotStageName(const QString& rawStage) {
    const QString stage = rawStage.trimmed().toLower();
    if (stage == QStringLiteral("environment")) return QStringLiteral("setup");
    if (stage == QStringLiteral("dataset_prep")) return QStringLiteral("prepare");
    if (stage == QStringLiteral("evaluation")) return QStringLiteral("evaluate");
    return stage;
}

QString autopilotStageProcessName(const QString& rawStage) {
    const QString stage = normalizeAutopilotStageName(rawStage);
    if (stage == QStringLiteral("setup")) return QStringLiteral("setup");
    if (stage == QStringLiteral("prepare")) return QStringLiteral("prepare");
    if (stage == QStringLiteral("training")) return QStringLiteral("training");
    if (stage == QStringLiteral("evaluate")) return QStringLiteral("evaluate");
    return QString();
}

QString autopilotFeedStageName(const QString& rawStage) {
    const QString stage = normalizeAutopilotStageName(rawStage);
    if (stage == QStringLiteral("setup")) return QStringLiteral("environment");
    if (stage == QStringLiteral("prepare")) return QStringLiteral("dataset_prep");
    if (stage == QStringLiteral("training")) return QStringLiteral("training");
    if (stage == QStringLiteral("evaluate")) return QStringLiteral("evaluation");
    if (stage == QStringLiteral("completed")) return QStringLiteral("completed");
    return stage;
}

double autopilotStageBaselineProgress(const QString& rawStage) {
    const QString stage = normalizeAutopilotStageName(rawStage);
    if (stage == QStringLiteral("setup")) return 0.05;
    if (stage == QStringLiteral("prepare")) return 0.18;
    if (stage == QStringLiteral("training")) return 0.35;
    if (stage == QStringLiteral("evaluate")) return 0.85;
    if (stage == QStringLiteral("completed")) return 1.0;
    return 0.0;
}

QString autopilotStageDisplayName(const QString& rawStage) {
    const QString stage = normalizeAutopilotStageName(rawStage);
    if (stage == QStringLiteral("setup")) return QStringLiteral("Environment Setup");
    if (stage == QStringLiteral("prepare")) return QStringLiteral("Data Preparation");
    if (stage == QStringLiteral("training")) return QStringLiteral("Training");
    if (stage == QStringLiteral("evaluate")) return QStringLiteral("Evaluation");
    if (stage == QStringLiteral("completed")) return QStringLiteral("Completed");
    return stage;
}

bool isAutopilotStageProcess(const QString& processName, const QString& stage) {
    const QString normalizedStage = normalizeAutopilotStageName(stage);
    return (processName == QStringLiteral("setup") && normalizedStage == QStringLiteral("setup"))
        || (processName == QStringLiteral("prepare") && normalizedStage == QStringLiteral("prepare"))
        || (processName == QStringLiteral("training") && normalizedStage == QStringLiteral("training"))
        || (processName == QStringLiteral("evaluate") && normalizedStage == QStringLiteral("evaluate"));
}

QStringList autopilotManagedJobs() {
    return {
        QStringLiteral("setup"),
        QStringLiteral("prepare"),
        QStringLiteral("training"),
        QStringLiteral("evaluate"),
    };
}

bool managedProcessLooksRunning(const ManagedProcessState& state) {
    if (state.process && state.process->state() != QProcess::NotRunning) {
        return true;
    }
    return state.recoveredPid > 0 && isProcessAlive(state.recoveredPid);
}

QString resolvedAutopilotControlJob(const QHash<QString, ManagedProcessState>& processes,
                                    const QJsonObject& autopilot,
                                    bool preferPaused) {
    const QString preferredStage = normalizeAutopilotStageName(autopilot.value(QStringLiteral("stage")).toString());
    const QString preferredJob = autopilotStageProcessName(preferredStage);
    auto matchesTargetState = [&](const QString& jobName) {
        if (!processes.contains(jobName)) {
            return false;
        }
        const ManagedProcessState state = processes.value(jobName);
        return preferPaused ? state.paused : managedProcessLooksRunning(state);
    };

    if (!preferredJob.isEmpty() && matchesTargetState(preferredJob)) {
        return preferredJob;
    }

    if (!preferredJob.isEmpty()) {
        const bool autopilotRequestedState = preferPaused
            ? autopilot.value(QStringLiteral("paused")).toBool(false)
            : autopilot.value(QStringLiteral("active")).toBool(false);
        if (autopilotRequestedState) {
            return preferredJob;
        }
    }

    return QString();
}

QString stateBackupPath(const QString& primaryPath) {
    return primaryPath + QStringLiteral(".bak");
}

void removeStateFileWithBackup(const QString& primaryPath) {
    QFile::remove(primaryPath);
    QFile::remove(stateBackupPath(primaryPath));
}

bool writeStateFileWithBackup(const QString& primaryPath,
                              const QString& text,
                              bool* primaryOk = nullptr,
                              bool* backupOk = nullptr) {
    const bool primaryWriteOk = writeTextFile(primaryPath, text);
    const bool backupWriteOk = writeTextFile(stateBackupPath(primaryPath), text);
    if (primaryOk) {
        *primaryOk = primaryWriteOk;
    }
    if (backupOk) {
        *backupOk = backupWriteOk;
    }
    return primaryWriteOk || backupWriteOk;
}

bool tryLoadStateObject(const QString& path,
                        QJsonObject* object,
                        QString* rawText,
                        QString* errorMessage) {
    QString text;
    if (!readTextFile(path, &text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("State file is unavailable");
        }
        return false;
    }

    QString parseError;
    if (!parseJsonObject(text.toUtf8(), object, &parseError)) {
        if (errorMessage) {
            *errorMessage = parseError;
        }
        return false;
    }
    if (rawText) {
        *rawText = text;
    }
    return true;
}

bool readStateObjectWithFallback(const QString& primaryPath,
                                 QJsonObject* object,
                                 QString* errorMessage,
                                 bool* usedBackup = nullptr,
                                 bool* restoredPrimaryFromBackup = nullptr) {
    if (usedBackup) {
        *usedBackup = false;
    }
    if (restoredPrimaryFromBackup) {
        *restoredPrimaryFromBackup = false;
    }

    QString primaryError;
    QString primaryText;
    if (tryLoadStateObject(primaryPath, object, &primaryText, &primaryError)) {
        return true;
    }

    const QString backupPath = stateBackupPath(primaryPath);
    QString backupError;
    QString backupText;
    if (tryLoadStateObject(backupPath, object, &backupText, &backupError)) {
        if (usedBackup) {
            *usedBackup = true;
        }
        const bool restoredPrimary = writeTextFile(primaryPath, backupText);
        if (restoredPrimaryFromBackup) {
            *restoredPrimaryFromBackup = restoredPrimary;
        }
        return true;
    }

    if (errorMessage) {
        *errorMessage = primaryError;
        if (!backupError.isEmpty()) {
            *errorMessage += QStringLiteral("; backup: %1").arg(backupError);
        }
    }
    return false;
}

}  // namespace

QString ControlCenterBackend::runtimeStatePath(const QString& name) const {
    return QDir(m_runtimeStateDir).absoluteFilePath(name + QStringLiteral(".json"));
}

QString ControlCenterBackend::autopilotRuntimeStatePath() const {
    return runtimeStatePath(QStringLiteral("autopilot"));
}

QString ControlCenterBackend::trainingPauseRequestPath() const {
    return QDir(m_runtimeStateDir).absoluteFilePath(QStringLiteral("training.pause.request"));
}

QStringList ControlCenterBackend::trainingResumeArgs() const {
    QStringList args = m_processes.value(QStringLiteral("training")).extraArgs;
    if (!args.contains(QStringLiteral("--resume"))) {
        args.append(QStringLiteral("--resume"));
    }
    return args;
}

bool ControlCenterBackend::requestTrainingPause() {
    const QJsonObject payload{
        {QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch()},
        {QStringLiteral("requested_by"), QStringLiteral("native_backend")},
    };
    const bool ok = writeTextFile(trainingPauseRequestPath(), QString::fromUtf8(jsonBytes(payload)));
    if (!ok) {
        recordIssue(QStringLiteral("process"), QStringLiteral("Could not persist training pause request"));
    }
    return ok;
}

void ControlCenterBackend::clearTrainingPauseRequest() {
    QFile::remove(trainingPauseRequestPath());
}

void ControlCenterBackend::finalizeManagedPausedState(const QString& name, const QString& message) {
    if (!m_processes.contains(name)) {
        return;
    }

    ManagedProcessState& state = m_processes[name];
    if (name == QStringLiteral("training")) {
        clearTrainingPauseRequest();
    }

    state.stopRequested = false;
    state.pauseRequested = false;
    state.paused = true;
    state.recoveredPid = 0;

    const QJsonObject diskSummary = inferJobStateFromDisk(name);
    double progress = diskSummary.value(QStringLiteral("progress")).toDouble();
    if (progress <= 0.0 && name == QStringLiteral("training")) {
        progress = 0.10;
    }

    persistManagedRuntimeState(name);
    appendProcessLog(state, QStringLiteral("[native-backend] %1 paused").arg(name));
    recordEvent(QStringLiteral("process_pause"), QStringLiteral("%1 paused").arg(state.label));
    recordJobUpdate(name, QStringLiteral("paused"), message, progress);
    invalidateRuntimeCaches();
}

void ControlCenterBackend::finalizeTrainingPausedState(const QString& message) {
    finalizeManagedPausedState(QStringLiteral("training"), message);
}

bool ControlCenterBackend::waitForTrainingPause(qint64 pid, int timeoutMs) {
    return waitForManagedPause(QStringLiteral("training"), pid, timeoutMs);
}

bool ControlCenterBackend::waitForManagedPause(const QString& name, qint64 pid, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 100);

        const ManagedProcessState state = m_processes.value(name);
        if (state.paused && !isManagedProcessRunning(state)) {
            return true;
        }

        const bool liveProcessAttached = state.process && state.process->state() != QProcess::NotRunning;
        if (!liveProcessAttached && !isProcessAlive(pid)) {
            return m_processes.value(name).paused;
        }

        QThread::msleep(100);
    }

    return m_processes.value(name).paused;
}

qint64 ControlCenterBackend::managedProcessId(const ManagedProcessState& state) const {
    if (state.process && state.process->state() != QProcess::NotRunning) {
        return state.process->processId();
    }
    return state.recoveredPid;
}

bool ControlCenterBackend::isManagedProcessRunning(const ManagedProcessState& state) const {
    const qint64 pid = managedProcessId(state);
    if (pid <= 0) {
        return false;
    }
    if (state.process && state.process->state() != QProcess::NotRunning) {
        return true;
    }
    return isProcessAlive(pid);
}

void ControlCenterBackend::persistManagedRuntimeState(const QString& name) {
    if (!m_processes.contains(name)) {
        return;
    }
    const ManagedProcessState& state = m_processes.value(name);
    const qint64 pid = managedProcessId(state);
    const bool running = pid > 0 && isManagedProcessRunning(state);
    const bool resumablePausedState = state.paused;
    if (!running && !resumablePausedState) {
        clearManagedRuntimeState(name);
        return;
    }

    QJsonObject payload{
        {QStringLiteral("name"), name},
        {QStringLiteral("pid"), static_cast<double>(running ? pid : 0)},
        {QStringLiteral("started_at"), state.startedAt},
        {QStringLiteral("paused"), state.paused},
        {QStringLiteral("pause_requested"), state.pauseRequested},
        {QStringLiteral("resumable"), state.paused},
        {QStringLiteral("script_path"), state.scriptPath},
        {QStringLiteral("args"), QJsonArray::fromStringList(state.extraArgs)},
        {QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch()},
    };
    if (name == QStringLiteral("training")) {
        payload.insert(QStringLiteral("resume_args"), QJsonArray::fromStringList(trainingResumeArgs()));
    }
    const QString payloadText = QString::fromUtf8(jsonBytes(payload));
    bool primaryOk = false;
    bool backupOk = false;
    if (!writeStateFileWithBackup(runtimeStatePath(name), payloadText, &primaryOk, &backupOk)) {
        recordIssue(QStringLiteral("process"), QStringLiteral("Could not persist runtime state for %1").arg(name));
    } else if (!primaryOk || !backupOk) {
        recordLog(QStringLiteral("warning"),
                  QStringLiteral("process"),
                  QStringLiteral("runtime_state_backup_partial"),
                  QStringLiteral("Runtime state persisted with a degraded backup path"),
                  QJsonObject{
                      {QStringLiteral("name"), name},
                      {QStringLiteral("path"), runtimeStatePath(name)},
                      {QStringLiteral("primary_ok"), primaryOk},
                      {QStringLiteral("backup_ok"), backupOk},
                  });
    } else {
        m_loggedStateBackupRecoveryPaths.remove(runtimeStatePath(name));
    }
}

void ControlCenterBackend::clearManagedRuntimeState(const QString& name) {
    removeStateFileWithBackup(runtimeStatePath(name));
    m_loggedStateBackupRecoveryPaths.remove(runtimeStatePath(name));
    if (!m_processes.contains(name)) {
        return;
    }
    ManagedProcessState& state = m_processes[name];
    if (!state.process || state.process->state() == QProcess::NotRunning) {
        state.recoveredPid = 0;
    }
}

void ControlCenterBackend::persistAutopilotRuntimeState() {
    ensureAutopilotState();
    if (!m_autopilot.value(QStringLiteral("active")).toBool(false)
        && m_autopilot.value(QStringLiteral("stage")).toString() != QStringLiteral("paused")) {
        removeStateFileWithBackup(autopilotRuntimeStatePath());
        return;
    }

    QJsonObject payload = m_autopilot;
    payload.insert(QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch());
    payload.insert(QStringLiteral("started_at"), payload.value(QStringLiteral("started_at")).toDouble(QDateTime::currentSecsSinceEpoch()));
    const QString payloadText = QString::fromUtf8(jsonBytes(payload));
    bool primaryOk = false;
    bool backupOk = false;
    if (!writeStateFileWithBackup(autopilotRuntimeStatePath(), payloadText, &primaryOk, &backupOk)) {
        recordIssue(QStringLiteral("process"), QStringLiteral("Could not persist autopilot runtime state"));
    } else if (!primaryOk || !backupOk) {
        recordLog(QStringLiteral("warning"),
                  QStringLiteral("process"),
                  QStringLiteral("autopilot_state_backup_partial"),
                  QStringLiteral("Autopilot runtime state persisted with a degraded backup path"),
                  QJsonObject{
                      {QStringLiteral("path"), autopilotRuntimeStatePath()},
                      {QStringLiteral("primary_ok"), primaryOk},
                      {QStringLiteral("backup_ok"), backupOk},
                  });
    } else {
        m_loggedStateBackupRecoveryPaths.remove(autopilotRuntimeStatePath());
    }
}

void ControlCenterBackend::restoreAutopilotRuntimeState() {
    ensureAutopilotState();
    auto resetAutopilotToIdle = [this]() {
        m_autopilot = QJsonObject{
            {QStringLiteral("active"), false},
            {QStringLiteral("paused"), false},
            {QStringLiteral("stage"), QStringLiteral("idle")},
            {QStringLiteral("message"), QStringLiteral("Waiting for activity")},
        };
    };
    QJsonObject persisted;
    QString parseError;
    bool usedBackup = false;
    bool restoredPrimary = false;
    const QString runtimePath = autopilotRuntimeStatePath();
    if (!readStateObjectWithFallback(runtimePath, &persisted, &parseError, &usedBackup, &restoredPrimary)) {
        if (!QFileInfo::exists(autopilotRuntimeStatePath())
            && !QFileInfo::exists(stateBackupPath(autopilotRuntimeStatePath()))) {
            m_loggedStateBackupRecoveryPaths.remove(runtimePath);
            resetAutopilotToIdle();
            return;
        }
        recordLog(QStringLiteral("warning"),
                  QStringLiteral("process"),
                  QStringLiteral("autopilot_state_invalid"),
                  QStringLiteral("Ignoring invalid autopilot runtime state"),
                  QJsonObject{
                      {QStringLiteral("path"), autopilotRuntimeStatePath()},
                      {QStringLiteral("error"), parseError},
                  });
        removeStateFileWithBackup(autopilotRuntimeStatePath());
        m_loggedStateBackupRecoveryPaths.remove(runtimePath);
        resetAutopilotToIdle();
        return;
    }
    if (usedBackup && !m_loggedStateBackupRecoveryPaths.contains(runtimePath)) {
        m_loggedStateBackupRecoveryPaths.insert(runtimePath);
        recordLog(QStringLiteral("warning"),
                  QStringLiteral("process"),
                  QStringLiteral("autopilot_state_backup_recovered"),
                  QStringLiteral("Recovered autopilot runtime state from backup"),
                  QJsonObject{
                      {QStringLiteral("path"), runtimePath},
                      {QStringLiteral("restored_primary"), restoredPrimary},
                  });
    } else if (!usedBackup) {
        m_loggedStateBackupRecoveryPaths.remove(runtimePath);
    }
    const QString stage = persisted.value(QStringLiteral("stage")).toString();
    const QString stageProcess =
        stage == QStringLiteral("setup") || stage == QStringLiteral("environment") ? QStringLiteral("setup") :
        stage == QStringLiteral("prepare") || stage == QStringLiteral("dataset_prep") ? QStringLiteral("prepare") :
        stage == QStringLiteral("training") ? QStringLiteral("training") :
        stage == QStringLiteral("evaluate") || stage == QStringLiteral("evaluation") ? QStringLiteral("evaluate") :
        QString();

    const bool active = persisted.value(QStringLiteral("active")).toBool(false);
    const bool paused = persisted.value(QStringLiteral("paused")).toBool(false);
    const bool liveStageRunning = !stageProcess.isEmpty() && isManagedProcessRunning(m_processes.value(stageProcess));
    const bool resumablePausedStage = paused && !stageProcess.isEmpty();
    if (!active || stageProcess.isEmpty() || (!liveStageRunning && !resumablePausedStage)) {
        removeStateFileWithBackup(autopilotRuntimeStatePath());
        m_loggedStateBackupRecoveryPaths.remove(runtimePath);
        resetAutopilotToIdle();
        return;
    }

    m_autopilot = persisted;
    m_autopilot.insert(QStringLiteral("active"), true);
    m_autopilot.insert(QStringLiteral("paused"), paused);
    if (liveStageRunning) {
        m_autopilot.insert(QStringLiteral("message"),
                           persisted.value(QStringLiteral("message")).toString(QStringLiteral("Recovered after backend restart")));
    } else {
        m_autopilot.insert(QStringLiteral("message"),
                           persisted.value(QStringLiteral("message")).toString(QStringLiteral("Recovered paused autopilot state")));
    }
}

void ControlCenterBackend::refreshRecoveredProcessStates() {
    for (auto it = m_processes.begin(); it != m_processes.end(); ++it) {
        if (it.key() == QStringLiteral("autopilot")) {
            continue;
        }
        ManagedProcessState& state = it.value();
        if (state.process && state.process->state() != QProcess::NotRunning) {
            state.recoveredPid = 0;
            persistManagedRuntimeState(it.key());
            continue;
        }

        QJsonObject persisted;
        QString parseError;
        bool usedBackup = false;
        bool restoredPrimary = false;
        const QString runtimePath = runtimeStatePath(it.key());
        if (!readStateObjectWithFallback(runtimePath, &persisted, &parseError, &usedBackup, &restoredPrimary)) {
            if (!QFileInfo::exists(runtimePath)
                && !QFileInfo::exists(stateBackupPath(runtimePath))) {
                m_loggedStateBackupRecoveryPaths.remove(runtimePath);
                state.recoveredPid = 0;
                continue;
            }
            recordLog(QStringLiteral("warning"),
                      QStringLiteral("process"),
                      QStringLiteral("runtime_state_invalid"),
                      QStringLiteral("Ignoring invalid managed runtime state"),
                      QJsonObject{
                          {QStringLiteral("name"), it.key()},
                          {QStringLiteral("path"), runtimePath},
                          {QStringLiteral("error"), parseError},
                      });
            removeStateFileWithBackup(runtimePath);
            m_loggedStateBackupRecoveryPaths.remove(runtimePath);
            state.recoveredPid = 0;
            state.paused = false;
            state.pauseRequested = false;
            continue;
        }
        if (usedBackup && !m_loggedStateBackupRecoveryPaths.contains(runtimePath)) {
            m_loggedStateBackupRecoveryPaths.insert(runtimePath);
            recordLog(QStringLiteral("warning"),
                      QStringLiteral("process"),
                      QStringLiteral("runtime_state_backup_recovered"),
                      QStringLiteral("Recovered managed runtime state from backup"),
                      QJsonObject{
                          {QStringLiteral("name"), it.key()},
                          {QStringLiteral("path"), runtimePath},
                          {QStringLiteral("restored_primary"), restoredPrimary},
                      });
        } else if (!usedBackup) {
            m_loggedStateBackupRecoveryPaths.remove(runtimePath);
        }
        const qint64 pid = static_cast<qint64>(persisted.value(QStringLiteral("pid")).toDouble());
        const bool paused = persisted.value(QStringLiteral("paused")).toBool(false);
        const bool resumable = persisted.value(QStringLiteral("resumable")).toBool(paused);

        if (pid > 0 && isProcessAlive(pid)) {
            recordLog(QStringLiteral("warning"),
                      QStringLiteral("process"),
                      QStringLiteral("runtime_state_live_pid_ignored"),
                      QStringLiteral("Ignored live managed runtime PID after restart because process identity cannot be verified safely"),
                      QJsonObject{
                          {QStringLiteral("name"), it.key()},
                          {QStringLiteral("pid"), static_cast<double>(pid)},
                          {QStringLiteral("path"), runtimePath},
                      });
            removeStateFileWithBackup(runtimePath);
            m_loggedStateBackupRecoveryPaths.remove(runtimePath);
            state.recoveredPid = 0;
            state.paused = false;
            state.pauseRequested = false;
            continue;
        }

        if (!paused || !resumable) {
            removeStateFileWithBackup(runtimePath);
            m_loggedStateBackupRecoveryPaths.remove(runtimePath);
            state.recoveredPid = 0;
            state.paused = false;
            state.pauseRequested = false;
            continue;
        }

        state.recoveredPid = 0;
        state.startedAt = persisted.value(QStringLiteral("started_at")).toDouble(state.startedAt);
        state.paused = true;
        state.pauseRequested = false;

        const QString persistedScript = persisted.value(QStringLiteral("script_path")).toString();
        if (!persistedScript.isEmpty()) {
            state.scriptPath = persistedScript;
        }
        const QJsonArray persistedArgs = persisted.value(QStringLiteral("args")).toArray();
        if (!persistedArgs.isEmpty()) {
            QStringList args;
            for (const QJsonValue& value : persistedArgs) {
                args.append(value.toString());
            }
            state.extraArgs = args;
        }
    }
}

QStringList ControlCenterBackend::buildCommand(const QString& scriptPath, const QStringList& extraArgs) const {
    const QString suffix = QFileInfo(scriptPath).suffix().toLower();
    if (suffix == QStringLiteral("bat") || suffix == QStringLiteral("cmd")) {
        return QStringList{QStringLiteral("cmd"), QStringLiteral("/c"), scriptPath} + extraArgs;
    }
    if (suffix == QStringLiteral("sh")) {
        return QStringList{QStringLiteral("sh"), scriptPath} + extraArgs;
    }
    if (suffix == QStringLiteral("py")) {
        return QStringList{pythonPath(), scriptPath} + extraArgs;
    }
    return QStringList{scriptPath} + extraArgs;
}

bool ControlCenterBackend::startManagedProcess(const QString& name, const QString& scriptPath, const QStringList& extraArgs) {
    ManagedProcessState& state = m_processes[name];
    if (isManagedProcessRunning(state)) {
        return false;
    }
    if (scriptPath.trimmed().isEmpty()) {
        recordIssue(QStringLiteral("process"), QStringLiteral("%1 has no configured script path").arg(name));
        recordAlert(QStringLiteral("error"), QStringLiteral("%1 is misconfigured and cannot start").arg(state.label));
        return false;
    }
    const bool localScriptPath = QDir::isAbsolutePath(scriptPath)
        || scriptPath.contains(QLatin1Char('/'))
        || scriptPath.contains(QLatin1Char('\\'));
    if (localScriptPath && !QFileInfo::exists(scriptPath)) {
        recordIssue(QStringLiteral("process"), QStringLiteral("%1 script is missing").arg(name));
        recordAlert(QStringLiteral("error"), QStringLiteral("%1 script was not found: %2").arg(state.label, scriptPath));
        return false;
    }
    if (name == QStringLiteral("training")) {
        clearTrainingPauseRequest();
    }

    state.process = new QProcess(this);
    state.process->setWorkingDirectory(m_rootPath);
    state.process->setProcessEnvironment(backendEnvironment());
    state.process->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
    state.process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif

    const QStringList command = buildCommand(scriptPath, extraArgs);
    if (command.isEmpty() || command.first().trimmed().isEmpty()) {
        recordIssue(QStringLiteral("process"), QStringLiteral("%1 resolved to an empty launch command").arg(name));
        recordAlert(QStringLiteral("error"), QStringLiteral("%1 launch command is invalid").arg(state.label));
        return false;
    }
    state.scriptPath = scriptPath;
    state.extraArgs = extraArgs;
    state.logLines.clear();
    state.stopRequested = false;
    state.paused = false;
    state.pauseRequested = false;
    state.startedAt = QDateTime::currentSecsSinceEpoch();
    state.lastExitCode = 0;
    state.recoveredPid = 0;
    state.lastConsoleProgress = -1.0;
    state.lastConsoleProgressKey.clear();

    connect(state.process, &QProcess::readyRead, this, [this, name]() { onProcessOutput(name); });
    connect(state.process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, name](int exitCode, QProcess::ExitStatus status) {
                onProcessFinished(name, exitCode, status);
            });

    state.process->start(command.first(), command.mid(1));
    if (!state.process->waitForStarted(3000)) {
        const QString startupError = state.process->errorString();
        recordIssue(QStringLiteral("process"), QStringLiteral("%1 failed to start: %2").arg(name, startupError));
        recordAlert(QStringLiteral("error"), QStringLiteral("%1 failed to start").arg(state.label));
        appendProcessLog(state, QStringLiteral("[native-backend] start failed: %1").arg(startupError));
        state.process->deleteLater();
        state.process = nullptr;
        clearManagedRuntimeState(name);
        return false;
    }

    persistManagedRuntimeState(name);
    recordEvent(QStringLiteral("process_start"), QStringLiteral("%1 started").arg(state.label));
    recordJobUpdate(name, QStringLiteral("starting"), QStringLiteral("%1 starting").arg(state.label), 0.0);
    return true;
}

bool ControlCenterBackend::stopManagedProcess(const QString& name) {
    ManagedProcessState& state = m_processes[name];
    if (name == QStringLiteral("training")) {
        clearTrainingPauseRequest();
    }

    if (state.paused && !isManagedProcessRunning(state)) {
        const double progress = summarizeJob(name).value(QStringLiteral("progress")).toDouble();
        state.paused = false;
        state.pauseRequested = false;
        state.stopRequested = false;
        clearManagedRuntimeState(name);
        recordJobUpdate(name, QStringLiteral("stopped"), QStringLiteral("%1 stopped").arg(state.label), progress);
        invalidateRuntimeCaches();
        return true;
    }

    if (state.process && state.process->state() != QProcess::NotRunning) {
        state.stopRequested = true;
        state.pauseRequested = false;
        const qint64 pid = state.process->processId();
        state.process->terminate();
        if (!state.process->waitForFinished(5000)) {
            state.process->kill();
            if (!state.process->waitForFinished(3000) && isProcessAlive(pid)) {
                return false;
            }
        }
        return !isProcessAlive(pid);
    }

    if (!isProcessAlive(state.recoveredPid)) {
        clearManagedRuntimeState(name);
        return false;
    }

    state.stopRequested = true;
    state.pauseRequested = false;
    const double progress = summarizeJob(name).value(QStringLiteral("progress")).toDouble();
    if (!terminateProcessById(state.recoveredPid) || !waitForProcessExitById(state.recoveredPid, 3000)) {
        return false;
    }
    clearManagedRuntimeState(name);
    state.paused = false;
    recordJobUpdate(name, QStringLiteral("stopped"), QStringLiteral("%1 stopped").arg(state.label), progress);
    invalidateRuntimeCaches();
    return true;
}

bool ControlCenterBackend::pauseManagedProcess(const QString& name) {
    ManagedProcessState& state = m_processes[name];
    const qint64 pid = managedProcessId(state);
    if (pid <= 0 || !isManagedProcessRunning(state) || state.paused || state.pauseRequested) {
        return false;
    }

    if (name == QStringLiteral("training")) {
        if (!requestTrainingPause()) {
            return false;
        }
        m_exclusiveOperationName = QStringLiteral("pause %1").arg(name);
        state.pauseRequested = true;
        state.stopRequested = false;
        persistManagedRuntimeState(name);
        appendProcessLog(state, QStringLiteral("[native-backend] checkpoint pause requested"));
        recordEvent(QStringLiteral("process_pause_requested"), QStringLiteral("%1 checkpoint pause requested").arg(state.label));
        const bool paused = waitForTrainingPause(pid, 120000);
        m_exclusiveOperationName.clear();
        if (!paused) {
            recordAlert(QStringLiteral("warning"), QStringLiteral("%1 pause is still waiting on checkpoint flush").arg(state.label));
        }
        return paused;
    }

    state.stopRequested = false;
    if (!setProcessSuspended(pid, true)) {
        return false;
    }
    finalizeManagedPausedState(name, QStringLiteral("%1 paused").arg(state.label));
    return true;
}

bool ControlCenterBackend::resumeManagedProcess(const QString& name) {
    ManagedProcessState& state = m_processes[name];
    if (!state.paused) {
        return false;
    }

    const qint64 pid = managedProcessId(state);
    if (pid > 0 && isManagedProcessRunning(state)) {
        if (!setProcessSuspended(pid, false)) {
            return false;
        }
        state.paused = false;
        state.pauseRequested = false;
        persistManagedRuntimeState(name);
        appendProcessLog(state, QStringLiteral("[native-backend] %1 resumed").arg(name));
        recordEvent(QStringLiteral("process_resume"), QStringLiteral("%1 resumed").arg(state.label));
        return true;
    }

    const QString scriptPath = state.scriptPath;
    const QStringList args = name == QStringLiteral("training") ? trainingResumeArgs() : state.extraArgs;
    if (scriptPath.isEmpty()) {
        return false;
    }

    state.paused = false;
    state.pauseRequested = false;
    clearManagedRuntimeState(name);
    if (!startManagedProcess(name, scriptPath, args)) {
        state.paused = true;
        persistManagedRuntimeState(name);
        return false;
    }

    appendProcessLog(state, QStringLiteral("[native-backend] %1 resumed").arg(name));
    recordEvent(QStringLiteral("process_resume"), QStringLiteral("%1 resumed").arg(state.label));
    return true;
}

bool ControlCenterBackend::restartManagedProcess(const QString& name) {
    ManagedProcessState& state = m_processes[name];
    const QString scriptPath = state.scriptPath;
    const QStringList extraArgs = state.extraArgs;
    if (scriptPath.isEmpty()) {
        return false;
    }
    stopManagedProcess(name);
    QThread::msleep(200);
    return startManagedProcess(name, scriptPath, extraArgs);
}

void ControlCenterBackend::appendProcessLog(ManagedProcessState& state, const QString& line) {
    state.logLines.append(line);
    while (state.logLines.size() > 200) {
        state.logLines.removeFirst();
    }
}

void ControlCenterBackend::onProcessOutput(const QString& name) {
    ManagedProcessState& state = m_processes[name];
    if (!state.process) {
        return;
    }
    const QString output = QString::fromUtf8(state.process->readAll());
    for (const QString& line : output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
        const QString cleaned = line.trimmed();
        if (cleaned.isEmpty()) {
            continue;
        }
        if (shouldSuppressProcessNoiseLine(name, cleaned)) {
            continue;
        }

        QString progressJob;
        QString progressStage;
        QString progressMessage;
        double progressValue = 0.0;
        if (parseStructuredProgressLine(cleaned, &progressJob, &progressStage, &progressMessage, &progressValue)) {
            if (progressJob == QStringLiteral("setup")) {
                recordJobUpdate(progressJob, progressStage, progressMessage, progressValue);
            }
            const QString key = QStringLiteral("structured:%1:%2").arg(progressStage, progressMessage);
            if (shouldEmitConsoleProgress(state, progressValue, key)) {
                const QString progressPct = QString::number(progressValue * 100.0, 'f', 1) + QLatin1Char('%');
                printConsoleLine(QStringLiteral("[progress:%1] [%2] %3 %4")
                    .arg(progressJob,
                         renderConsoleProgressBar(progressValue),
                         progressPct,
                         progressMessage));
                appendProcessLog(state, QStringLiteral("[progress] %1 %2 %3")
                    .arg(progressPct, progressStage, progressMessage));
            }
            continue;
        }

        if (parseDownloadProgressLine(name, cleaned, &progressMessage, &progressValue)) {
            const QString key = QStringLiteral("download:%1").arg(progressMessage.left(96));
            if (shouldEmitConsoleProgress(state, progressValue, key)) {
                const QString currentStage = summarizeJob(name).value(QStringLiteral("stage")).toString();
                const double mappedProgress = mappedDownloadProgress(name, currentStage, progressValue);
                if (mappedProgress >= 0.0) {
                    recordJobUpdate(name, currentStage, progressMessage, mappedProgress);
                }
                const QString progressPct = QString::number(progressValue * 100.0, 'f', 1) + QLatin1Char('%');
                printConsoleLine(QStringLiteral("[download:%1] [%2] %3 %4")
                    .arg(name,
                         renderConsoleProgressBar(progressValue),
                         progressPct,
                         progressMessage));
            }
            continue;
        }

        appendProcessLog(state, cleaned);
        const bool looksSevere =
            cleaned.contains(QStringLiteral("traceback"), Qt::CaseInsensitive) ||
            cleaned.contains(QStringLiteral("exception"), Qt::CaseInsensitive) ||
            cleaned.contains(QStringLiteral("error"), Qt::CaseInsensitive);
        printConsoleLine(QStringLiteral("[proc:%1] %2").arg(name, cleaned), looksSevere);
    }
    invalidateRuntimeCaches();
}

void ControlCenterBackend::onProcessFinished(const QString& name, int exitCode, QProcess::ExitStatus status) {
    ManagedProcessState& state = m_processes[name];
    state.lastExitCode = exitCode;
    state.recoveredPid = 0;
    const bool pauseRequested = state.pauseRequested;
    const bool stopRequested = state.stopRequested;

    const QString currentStage = summarizeJob(name).value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    const double progress = summarizeJob(name).value(QStringLiteral("progress")).toDouble();
    if (pauseRequested) {
        if (state.process) {
            state.process->deleteLater();
            state.process = nullptr;
        }
        if (name == QStringLiteral("training")) {
            if (status == QProcess::NormalExit && exitCode == 0) {
                finalizeTrainingPausedState(QStringLiteral("%1 paused and checkpoint saved").arg(state.label));
            } else {
                clearTrainingPauseRequest();
                state.pauseRequested = false;
                state.paused = false;
                clearManagedRuntimeState(name);
                recordAlert(QStringLiteral("error"), QStringLiteral("%1 pause failed during checkpoint save").arg(state.label));
                recordJobUpdate(name, QStringLiteral("failed"), QStringLiteral("%1 pause failed").arg(state.label), progress);
                invalidateRuntimeCaches();
            }
        } else {
            finalizeManagedPausedState(name, QStringLiteral("%1 paused").arg(state.label));
        }

        const bool autopilotActive = m_autopilot.value(QStringLiteral("active")).toBool();
        const QString autopilotStage = m_autopilot.value(QStringLiteral("stage")).toString();
        if (autopilotActive && isAutopilotStageProcess(name, autopilotStage)) {
            if (m_processes.value(name).paused) {
                m_autopilot.insert(QStringLiteral("paused"), true);
                m_autopilot.insert(QStringLiteral("message"), QStringLiteral("Autopilot paused"));
                persistAutopilotRuntimeState();
            } else {
                failAutopilot(QStringLiteral("%1 pause failed").arg(state.label), progress);
            }
        }
        return;
    }

    state.paused = false;
    if (stopRequested) {
        if (currentStage != QStringLiteral("stopped")) {
            recordJobUpdate(name, QStringLiteral("stopped"), QStringLiteral("%1 stopped").arg(state.label), progress);
        }
    } else if (status == QProcess::NormalExit && exitCode == 0) {
        const QString stage = name == QStringLiteral("inference") ? QStringLiteral("stopped") : QStringLiteral("completed");
        const QString message = name == QStringLiteral("inference")
            ? QStringLiteral("%1 exited").arg(state.label)
            : QStringLiteral("%1 complete").arg(state.label);
        recordJobUpdate(name, stage, message, stage == QStringLiteral("completed") ? 1.0 : progress);
    } else {
        const QString failureMessage = latestProcessFailureMessage(state, state.label, exitCode);
        recordAlert(QStringLiteral("error"), failureMessage);
        recordJobUpdate(name, QStringLiteral("failed"), failureMessage, progress);
    }

    if (state.process) {
        state.process->deleteLater();
        state.process = nullptr;
    }
    clearManagedRuntimeState(name);
    invalidateRuntimeCaches();

    const bool autopilotActive = m_autopilot.value(QStringLiteral("active")).toBool();
    const QString autopilotStage = m_autopilot.value(QStringLiteral("stage")).toString();
    if (!autopilotActive || !isAutopilotStageProcess(name, autopilotStage)) {
        return;
    }

    if (stopRequested) {
        failAutopilot(QStringLiteral("%1 was stopped before completion.").arg(state.label), progress);
        return;
    }

    if (status == QProcess::NormalExit && exitCode == 0) {
        const QString nextStage = nextAutopilotStage(autopilotStage);
        if (nextStage == QStringLiteral("completed")) {
            m_autopilot.insert(QStringLiteral("active"), false);
            m_autopilot.insert(QStringLiteral("paused"), false);
            m_autopilot.insert(QStringLiteral("stage"), QStringLiteral("completed"));
            m_autopilot.insert(QStringLiteral("message"), QStringLiteral("Autopilot complete"));
            persistAutopilotRuntimeState();
            recordJobUpdate(QStringLiteral("autopilot"), QStringLiteral("completed"), QStringLiteral("Autopilot complete"), 1.0);
            return;
        }
        m_autopilot.insert(QStringLiteral("stage"), nextStage);
        m_autopilot.insert(QStringLiteral("message"), QStringLiteral("Advancing to %1").arg(nextStage));
        persistAutopilotRuntimeState();
        advanceAutopilot();
        return;
    }

    const QString failureMessage = latestProcessFailureMessage(state, state.label, exitCode);
    failAutopilot(QStringLiteral("Autopilot halted because %1").arg(failureMessage),
                  progress,
                  false,
                  false);
}

void ControlCenterBackend::failAutopilot(const QString& message,
                                         double progress,
                                         bool emitAlert,
                                         bool trackIssue) {
    ensureAutopilotState();
    m_autopilot.insert(QStringLiteral("active"), false);
    m_autopilot.insert(QStringLiteral("paused"), false);
    m_autopilot.insert(QStringLiteral("stage"), QStringLiteral("failed"));
    m_autopilot.insert(QStringLiteral("message"), message);
    persistAutopilotRuntimeState();
    if (emitAlert) {
        recordAlert(QStringLiteral("error"), message);
    }
    recordJobUpdate(QStringLiteral("autopilot"),
                    QStringLiteral("failed"),
                    message,
                    progress >= 0.0 ? progress : summarizeJob(QStringLiteral("autopilot")).value(QStringLiteral("progress")).toDouble(),
                    trackIssue);
}

void ControlCenterBackend::ensureAutopilotState() {
    if (!m_autopilot.isEmpty()) {
        return;
    }
    m_autopilot = QJsonObject{
        {QStringLiteral("active"), false},
        {QStringLiteral("paused"), false},
        {QStringLiteral("stage"), QStringLiteral("idle")},
        {QStringLiteral("message"), QStringLiteral("Waiting for activity")},
    };
}

QString ControlCenterBackend::recommendedAutopilotStage() {
    const auto setup = summarizeJob(QStringLiteral("setup"));
    if (setup.value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("setup");
    }

    const auto prepare = summarizeJob(QStringLiteral("prepare"));
    if (prepare.value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("prepare");
    }

    const auto training = summarizeJob(QStringLiteral("training"));
    if (training.value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("training");
    }

    const auto evaluate = summarizeJob(QStringLiteral("evaluate"));
    if (evaluate.value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("evaluate");
    }

    return QStringLiteral("training");
}

bool ControlCenterBackend::startAutopilot() {
    ensureAutopilotState();
    if (m_autopilot.value(QStringLiteral("active")).toBool()) {
        return false;
    }
    const QString stage = recommendedAutopilotStage();
    m_autopilot.insert(QStringLiteral("active"), true);
    m_autopilot.insert(QStringLiteral("paused"), false);
    m_autopilot.insert(QStringLiteral("stage"), stage);
    m_autopilot.insert(QStringLiteral("message"),
                       QStringLiteral("Autopilot starting at %1").arg(autopilotStageDisplayName(stage)));
    m_autopilot.insert(QStringLiteral("started_at"), QDateTime::currentSecsSinceEpoch());
    persistAutopilotRuntimeState();
    recordJobUpdate(QStringLiteral("autopilot"),
                    autopilotFeedStageName(stage),
                    QStringLiteral("Autopilot starting at %1").arg(autopilotStageDisplayName(stage)),
                    autopilotStageBaselineProgress(stage));
    advanceAutopilot();
    return true;
}

bool ControlCenterBackend::continueAutopilotFromStage(const QString& rawStage) {
    ensureAutopilotState();
    const QString stage = normalizeAutopilotStageName(rawStage);
    if (autopilotStageProcessName(stage).isEmpty()) {
        return false;
    }

    const QString currentStage = normalizeAutopilotStageName(m_autopilot.value(QStringLiteral("stage")).toString());
    const QString currentJob = autopilotStageProcessName(currentStage);

    m_autopilot.insert(QStringLiteral("active"), false);
    m_autopilot.insert(QStringLiteral("paused"), false);
    persistAutopilotRuntimeState();

    if (!currentJob.isEmpty()) {
        stopManagedProcess(currentJob);
    }

    m_autopilot.insert(QStringLiteral("active"), true);
    m_autopilot.insert(QStringLiteral("paused"), false);
    m_autopilot.insert(QStringLiteral("stage"), stage);
    m_autopilot.insert(QStringLiteral("started_at"), QDateTime::currentSecsSinceEpoch());
    m_autopilot.insert(QStringLiteral("message"),
                       QStringLiteral("Continuing from %1").arg(autopilotStageDisplayName(stage)));
    persistAutopilotRuntimeState();
    recordJobUpdate(QStringLiteral("autopilot"),
                    autopilotFeedStageName(stage),
                    QStringLiteral("Continuing from %1").arg(autopilotStageDisplayName(stage)),
                    autopilotStageBaselineProgress(stage));
    advanceAutopilot();
    return true;
}

bool ControlCenterBackend::stopAutopilot() {
    ensureAutopilotState();
    if (!m_autopilot.value(QStringLiteral("active")).toBool(false)
        && !m_autopilot.value(QStringLiteral("paused")).toBool(false)) {
        return false;
    }
    const QString stage = m_autopilot.value(QStringLiteral("stage")).toString();
    if (stage == QStringLiteral("setup")) stopManagedProcess(QStringLiteral("setup"));
    if (stage == QStringLiteral("prepare")) stopManagedProcess(QStringLiteral("prepare"));
    if (stage == QStringLiteral("training")) stopManagedProcess(QStringLiteral("training"));
    if (stage == QStringLiteral("evaluate")) stopManagedProcess(QStringLiteral("evaluate"));
    m_autopilot.insert(QStringLiteral("active"), false);
    m_autopilot.insert(QStringLiteral("paused"), false);
    m_autopilot.insert(QStringLiteral("stage"), QStringLiteral("stopped"));
    m_autopilot.insert(QStringLiteral("message"), QStringLiteral("Autopilot stopped"));
    persistAutopilotRuntimeState();
    recordJobUpdate(QStringLiteral("autopilot"), QStringLiteral("stopped"), QStringLiteral("Autopilot stopped"), summarizeJob(QStringLiteral("autopilot")).value(QStringLiteral("progress")).toDouble());
    return true;
}

bool ControlCenterBackend::pauseAutopilot() {
    ensureAutopilotState();
    const QString job = resolvedAutopilotControlJob(m_processes, m_autopilot, false);
    if (job.isEmpty()) {
        return false;
    }
    const QString stage = normalizeAutopilotStageName(job);
    const QString stageLabel = autopilotStageDisplayName(stage);
    if (!pauseManagedProcess(job)) {
        return false;
    }
    const double startedAt = m_processes.value(job).startedAt > 0.0
        ? m_processes.value(job).startedAt
        : m_autopilot.value(QStringLiteral("started_at")).toDouble(QDateTime::currentSecsSinceEpoch());
    m_autopilot.insert(QStringLiteral("active"), true);
    m_autopilot.insert(QStringLiteral("paused"), true);
    m_autopilot.insert(QStringLiteral("stage"), stage);
    m_autopilot.insert(QStringLiteral("started_at"), startedAt);
    m_autopilot.insert(QStringLiteral("message"), QStringLiteral("Autopilot paused at %1").arg(stageLabel));
    persistAutopilotRuntimeState();
    recordJobUpdate(QStringLiteral("autopilot"), QStringLiteral("paused"), QStringLiteral("Paused current block: %1").arg(stageLabel), summarizeJob(QStringLiteral("autopilot")).value(QStringLiteral("progress")).toDouble());
    return true;
}

bool ControlCenterBackend::resumeAutopilot() {
    ensureAutopilotState();
    QString job = resolvedAutopilotControlJob(m_processes, m_autopilot, true);
    if (job.isEmpty()) {
        job = resolvedAutopilotControlJob(m_processes, m_autopilot, false);
    }
    QString stage = normalizeAutopilotStageName(job);
    if (job.isEmpty()) {
        stage = normalizeAutopilotStageName(m_autopilot.value(QStringLiteral("stage")).toString());
        job = autopilotStageProcessName(stage);
    }
    if (job.isEmpty()) {
        stage = normalizeAutopilotStageName(recommendedAutopilotStage());
        job = autopilotStageProcessName(stage);
    }
    if (job.isEmpty()) {
        return false;
    }
    const QString stageLabel = autopilotStageDisplayName(stage);
    const ManagedProcessState stageState = m_processes.value(job);

    if (stageState.paused && !resumeManagedProcess(job)) {
        return false;
    }

    const bool runningAfterResume = managedProcessLooksRunning(m_processes.value(job));
    m_autopilot.insert(QStringLiteral("paused"), false);
    m_autopilot.insert(QStringLiteral("active"), true);
    m_autopilot.insert(QStringLiteral("stage"), stage);
    m_autopilot.insert(QStringLiteral("started_at"), stageState.startedAt > 0.0
        ? stageState.startedAt
        : m_autopilot.value(QStringLiteral("started_at")).toDouble(QDateTime::currentSecsSinceEpoch()));
    m_autopilot.insert(QStringLiteral("message"), QStringLiteral("Continuing current block: %1").arg(stageLabel));
    persistAutopilotRuntimeState();
    recordJobUpdate(QStringLiteral("autopilot"),
                    autopilotFeedStageName(stage),
                    QStringLiteral("Continuing current block: %1").arg(stageLabel),
                    summarizeJob(QStringLiteral("autopilot")).value(QStringLiteral("progress")).toDouble());
    if (!runningAfterResume) {
        advanceAutopilot();
    }
    return true;
}

void ControlCenterBackend::advanceAutopilot() {
    ensureAutopilotState();
    if (!m_autopilot.value(QStringLiteral("active")).toBool() || m_autopilot.value(QStringLiteral("paused")).toBool()) {
        return;
    }

    const QString stage = m_autopilot.value(QStringLiteral("stage")).toString(QStringLiteral("setup"));
    if (stage == QStringLiteral("setup")) {
        if (startManagedProcess(QStringLiteral("setup"), m_setupScript)) {
            recordJobUpdate(QStringLiteral("autopilot"), QStringLiteral("environment"), QStringLiteral("Environment setup"), 0.05);
            persistAutopilotRuntimeState();
            return;
        }
        failAutopilot(QStringLiteral("Autopilot could not start Environment Setup."), 0.05);
        return;
    }

    if (stage == QStringLiteral("prepare")) {
        if (startManagedProcess(QStringLiteral("prepare"), m_processes.value(QStringLiteral("prepare")).scriptPath, m_processes.value(QStringLiteral("prepare")).extraArgs)) {
            recordJobUpdate(QStringLiteral("autopilot"), QStringLiteral("dataset_prep"), QStringLiteral("Data preparation"), 0.18);
            persistAutopilotRuntimeState();
            return;
        }
        failAutopilot(QStringLiteral("Autopilot could not start Data Preparation."), 0.18);
        return;
    }

    if (stage == QStringLiteral("training")) {
        if (startManagedProcess(QStringLiteral("training"),
                                m_processes.value(QStringLiteral("training")).scriptPath,
                                trainingResumeArgs())) {
            recordJobUpdate(QStringLiteral("autopilot"), QStringLiteral("training"), QStringLiteral("Training"), 0.35);
            persistAutopilotRuntimeState();
            return;
        }
        failAutopilot(QStringLiteral("Autopilot could not start Training."), 0.35);
        return;
    }

    if (stage == QStringLiteral("evaluate")) {
        if (startManagedProcess(QStringLiteral("evaluate"), m_processes.value(QStringLiteral("evaluate")).scriptPath, m_processes.value(QStringLiteral("evaluate")).extraArgs)) {
            recordJobUpdate(QStringLiteral("autopilot"), QStringLiteral("evaluation"), QStringLiteral("Running evaluation"), 0.85);
            persistAutopilotRuntimeState();
            return;
        }
        failAutopilot(QStringLiteral("Autopilot could not start Evaluation."), 0.85);
        return;
    }
}

HttpResponse ControlCenterBackend::handleAction(const QString& path) {
    auto ok = [](const QString& message) { return HttpResponse{200, "application/json; charset=utf-8", okBytes(message)}; };
    auto fail = [](const QString& message, int status = 409) { return HttpResponse{status, "application/json; charset=utf-8", failBytes(message)}; };

    if (path == QStringLiteral("/api/actions/setup")) {
        return startManagedProcess(QStringLiteral("setup"), m_setupScript) ? ok(QStringLiteral("Environment setup started")) : fail(QStringLiteral("Environment setup is already running"));
    }
    if (path == QStringLiteral("/api/actions/setup/pause")) {
        return pauseManagedProcess(QStringLiteral("setup")) ? ok(QStringLiteral("Environment setup paused")) : fail(QStringLiteral("Environment setup is not running"));
    }
    if (path == QStringLiteral("/api/actions/setup/resume")) {
        return resumeManagedProcess(QStringLiteral("setup")) ? ok(QStringLiteral("Environment setup resumed")) : fail(QStringLiteral("Environment setup is not paused"));
    }
    if (path == QStringLiteral("/api/actions/prepare")) {
        return startManagedProcess(QStringLiteral("prepare"), m_processes.value(QStringLiteral("prepare")).scriptPath, m_processes.value(QStringLiteral("prepare")).extraArgs) ? ok(QStringLiteral("Data preparation started")) : fail(QStringLiteral("Data preparation is already running"));
    }
    if (path == QStringLiteral("/api/actions/prepare/pause")) {
        return pauseManagedProcess(QStringLiteral("prepare")) ? ok(QStringLiteral("Data preparation paused")) : fail(QStringLiteral("Data preparation is not running"));
    }
    if (path == QStringLiteral("/api/actions/prepare/resume")) {
        return resumeManagedProcess(QStringLiteral("prepare")) ? ok(QStringLiteral("Data preparation resumed")) : fail(QStringLiteral("Data preparation is not paused"));
    }
    if (path == QStringLiteral("/api/actions/train/start")) {
        ManagedProcessState& state = m_processes[QStringLiteral("training")];
        if (state.paused) {
            return resumeManagedProcess(QStringLiteral("training")) ? ok(QStringLiteral("Training resumed")) : fail(QStringLiteral("Training could not resume"));
        }
        return startManagedProcess(QStringLiteral("training"), state.scriptPath, trainingResumeArgs())
            ? ok(QStringLiteral("Training started"))
            : fail(QStringLiteral("Training is already running"));
    }
    if (path == QStringLiteral("/api/actions/train/pause")) {
        return pauseManagedProcess(QStringLiteral("training")) ? ok(QStringLiteral("Training paused")) : fail(QStringLiteral("Training is not running"));
    }
    if (path == QStringLiteral("/api/actions/train/resume")) {
        return resumeManagedProcess(QStringLiteral("training")) ? ok(QStringLiteral("Training resumed")) : fail(QStringLiteral("Training is not paused"));
    }
    if (path == QStringLiteral("/api/actions/train/stop")) {
        return stopManagedProcess(QStringLiteral("training")) ? ok(QStringLiteral("Training stop requested")) : fail(QStringLiteral("Training is not running"));
    }
    if (path == QStringLiteral("/api/actions/evaluate")) {
        return startManagedProcess(QStringLiteral("evaluate"), m_processes.value(QStringLiteral("evaluate")).scriptPath, m_processes.value(QStringLiteral("evaluate")).extraArgs) ? ok(QStringLiteral("Evaluation started")) : fail(QStringLiteral("Evaluation is already running"));
    }
    if (path == QStringLiteral("/api/actions/evaluate/pause")) {
        return pauseManagedProcess(QStringLiteral("evaluate")) ? ok(QStringLiteral("Evaluation paused")) : fail(QStringLiteral("Evaluation is not running"));
    }
    if (path == QStringLiteral("/api/actions/evaluate/resume")) {
        return resumeManagedProcess(QStringLiteral("evaluate")) ? ok(QStringLiteral("Evaluation resumed")) : fail(QStringLiteral("Evaluation is not paused"));
    }
    if (path == QStringLiteral("/api/actions/inference/start")) {
        return startManagedProcess(QStringLiteral("inference"), m_processes.value(QStringLiteral("inference")).scriptPath, m_processes.value(QStringLiteral("inference")).extraArgs) ? ok(QStringLiteral("Inference API started")) : fail(QStringLiteral("Inference API is already running"));
    }
    if (path == QStringLiteral("/api/actions/inference/pause")) {
        return pauseManagedProcess(QStringLiteral("inference")) ? ok(QStringLiteral("Inference API paused")) : fail(QStringLiteral("Inference API is not running"));
    }
    if (path == QStringLiteral("/api/actions/inference/resume")) {
        return resumeManagedProcess(QStringLiteral("inference")) ? ok(QStringLiteral("Inference API resumed")) : fail(QStringLiteral("Inference API is not paused"));
    }
    if (path == QStringLiteral("/api/actions/inference/stop")) {
        return stopManagedProcess(QStringLiteral("inference")) ? ok(QStringLiteral("Inference API stop requested")) : fail(QStringLiteral("Inference API is not running"));
    }
    if (path == QStringLiteral("/api/actions/autopilot/start")) {
        return startAutopilot() ? ok(QStringLiteral("Autopilot started")) : fail(QStringLiteral("Autopilot is already active"));
    }
    if (path == QStringLiteral("/api/actions/autopilot/pause")) {
        return pauseAutopilot() ? ok(QStringLiteral("Current block paused")) : fail(QStringLiteral("No active block is available to pause"));
    }
    if (path == QStringLiteral("/api/actions/autopilot/resume")) {
        return resumeAutopilot() ? ok(QStringLiteral("Current block resumed")) : fail(QStringLiteral("No resumable block is available"));
    }
    if (path.startsWith(QStringLiteral("/api/actions/autopilot/continue/"))) {
        const QString rawStage = path.mid(QStringLiteral("/api/actions/autopilot/continue/").size()).trimmed();
        return continueAutopilotFromStage(rawStage)
            ? ok(QStringLiteral("Autopilot continuing from %1").arg(autopilotStageDisplayName(rawStage)))
            : fail(QStringLiteral("Autopilot could not continue from that stage"));
    }
    if (path == QStringLiteral("/api/actions/autopilot/stop")) {
        return stopAutopilot() ? ok(QStringLiteral("Autopilot stopped")) : fail(QStringLiteral("Autopilot is not active"));
    }
    // ── Data management actions ──
    if (path == QStringLiteral("/api/actions/data/clear_cache")) {
        QString msg;
        if (clearModelCache(&msg)) {
            return ok(msg);
        }
        recordLog(QStringLiteral("error"), QStringLiteral("data"), QStringLiteral("clear_cache"),
                  QStringLiteral("Partial failure: %1").arg(msg));
        return fail(msg, 500);
    }
    if (path == QStringLiteral("/api/actions/data/check")) {
        const QJsonObject result = checkDataIntegrity();
        const int status = result.value(QStringLiteral("ok")).toBool(true) ? 200 : 500;
        return HttpResponse{status, "application/json; charset=utf-8",
            QJsonDocument(result).toJson(QJsonDocument::Compact)};
    }
    if (path == QStringLiteral("/api/actions/data/redownload")) {
        if (m_setupScript.isEmpty() || !QFileInfo::exists(m_setupScript)) {
            const QString msg = QStringLiteral("Setup script not found: %1").arg(m_setupScript);
            recordLog(QStringLiteral("error"), QStringLiteral("data"), QStringLiteral("redownload"), msg);
            return fail(msg, 500);
        }
        if (startManagedProcess(QStringLiteral("setup"), m_setupScript)) {
            recordJobUpdate(QStringLiteral("setup"), QStringLiteral("starting"),
                            QStringLiteral("Re-downloading dependencies"), 0.0);
            return ok(QStringLiteral("Re-download started (setup + prepare will chain)"));
        }
        return fail(QStringLiteral("Setup is already running"));
    }
    if (path.startsWith(QStringLiteral("/api/actions/data/remove_model/"))) {
        const QString rawId = path.mid(QStringLiteral("/api/actions/data/remove_model/").size());
        if (rawId.isEmpty()) {
            return fail(QStringLiteral("Model ID is required"), 400);
        }
        const QString modelId = QUrl::fromPercentEncoding(rawId.toUtf8());
        QString msg;
        if (removeModelFromCache(modelId, &msg)) {
            return ok(msg);
        }
        // Distinguish not-found vs delete failure
        const bool isLocked = msg.contains(QStringLiteral("locked")) || msg.contains(QStringLiteral("could not delete"));
        return fail(msg, isLocked ? 500 : 404);
    }
    if (path == QStringLiteral("/api/actions/data/clear_action_history")) {
        QString msg;
        return clearActionHistory(&msg) ? ok(msg) : fail(msg, 500);
    }
    if (path.startsWith(QStringLiteral("/api/actions/data/delete_action/"))) {
        const QString rawSignature = path.mid(QStringLiteral("/api/actions/data/delete_action/").size());
        if (rawSignature.isEmpty()) {
            return fail(QStringLiteral("Action signature is required"), 400);
        }
        const QString signature = QUrl::fromPercentEncoding(rawSignature.toUtf8());
        QString msg;
        return deleteActionHistoryEntry(signature, &msg) ? ok(msg) : fail(msg, 404);
    }
    if (path.startsWith(QStringLiteral("/api/actions/data/delete_path/"))) {
        const QString rawPath = path.mid(QStringLiteral("/api/actions/data/delete_path/").size());
        if (rawPath.isEmpty()) {
            return fail(QStringLiteral("Managed path is required"), 400);
        }
        const QString relativePath = QUrl::fromPercentEncoding(rawPath.toUtf8());
        QString msg;
        return deleteManagedProjectPath(relativePath, &msg)
            ? ok(msg)
            : fail(msg, QFileInfo(QDir(m_rootPath).absoluteFilePath(relativePath)).exists() ? 500 : 404);
    }
    if (path.startsWith(QStringLiteral("/api/actions/data/cleanup/"))) {
        const QString scope = path.mid(QStringLiteral("/api/actions/data/cleanup/").size()).trimmed();
        const QJsonObject result = cleanupProjectFiles(scope);
        const int status = result.value(QStringLiteral("ok")).toBool(false) ? 200 : 500;
        return HttpResponse{status, "application/json; charset=utf-8", jsonBytes(result)};
    }
    if (path == QStringLiteral("/api/actions/large_judge/toggle")) {
        QJsonObject judge = m_config.value(QStringLiteral("large_judge")).toObject();
        const bool enabled = judge.value(QStringLiteral("enabled")).toBool(true);
        judge.insert(QStringLiteral("enabled"), !enabled);
        QJsonObject nextConfig = m_config;
        nextConfig.insert(QStringLiteral("large_judge"), judge);
        return saveConfigObject(nextConfig) ? ok(!enabled ? QStringLiteral("Large judge enabled") : QStringLiteral("Large judge disabled"))
                                            : fail(QStringLiteral("Could not toggle large judge"), 500);
    }
    return fail(QStringLiteral("Unknown action"), 404);
}

// ── Data management implementations ──

static bool forceRemoveDir(const QString& path) {
    // On Windows, HuggingFace cache files may be read-only. Clear attributes first.
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFile f(it.filePath());
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
    }
    return QDir(path).removeRecursively();
}

static bool forceRemovePath(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return true;
    }
    if (info.isDir()) {
        return forceRemoveDir(path);
    }

    QFile file(path);
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
    return file.remove();
}

static bool clearDirectoryContents(const QString& dirPath,
                                   QStringList* removedEntries,
                                   QStringList* failedEntries,
                                   bool preserveRepoPlaceholders = true,
                                   const QSet<QString>& excludedPaths = {}) {
    const QDir dir(dirPath);
    if (!dir.exists()) {
        return true;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                                    QDir::Name);
    for (const QFileInfo& entry : entries) {
        const QString name = entry.fileName();
        if (preserveRepoPlaceholders
            && (name == QStringLiteral(".gitkeep") || name == QStringLiteral(".gitignore"))) {
            continue;
        }

        const QString fullPath = normalizedPath(entry.absoluteFilePath());
        bool excluded = excludedPaths.contains(fullPath);
        if (!excluded) {
            for (const QString& excludedPath : excludedPaths) {
                if (fullPath.startsWith(excludedPath + QLatin1Char('/'))
                    || fullPath.startsWith(excludedPath + QLatin1Char('\\'))) {
                    excluded = true;
                    break;
                }
            }
        }
        if (excluded) {
            continue;
        }

        if (forceRemovePath(fullPath)) {
            if (removedEntries) {
                removedEntries->append(fullPath);
            }
            continue;
        }
        if (scheduleDeleteOnReboot(fullPath)) {
            if (removedEntries) {
                removedEntries->append(fullPath + QStringLiteral(" (queued for reboot cleanup)"));
            }
            continue;
        }
        if (failedEntries) {
            failedEntries->append(fullPath);
        }
    }

    return !failedEntries || failedEntries->isEmpty();
}

static bool pathEqualsOrWithin(const QString& path, const QString& root) {
    return pathEqualsOrWithinNormalized(path, root);
}

bool ControlCenterBackend::clearModelCache(QString* resultMessage) {
    const QString cacheDir = QDir(m_rootPath).absoluteFilePath(
        m_config.value(QStringLiteral("large_judge")).toObject()
            .value(QStringLiteral("cache_dir")).toString(QStringLiteral("data/cache/large_judge")));

    QDir dir(cacheDir);
    if (!dir.exists()) {
        const QString msg = QStringLiteral("Cache directory does not exist: %1").arg(cacheDir);
        recordLog(QStringLiteral("info"), QStringLiteral("data"), QStringLiteral("clear_cache"), msg);
        if (resultMessage) *resultMessage = msg;
        return true;
    }

    int removed = 0;
    QStringList failed;
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        const QString fullPath = normalizedPath(entry.absoluteFilePath());
        if (forceRemoveDir(fullPath)) {
            m_pendingDeletePaths.remove(fullPath);
            ++removed;
        } else {
            const QString quarantinePath = pendingDeletePathFor(entry.fileName());
            bool queued = false;
            if (QDir().rename(fullPath, quarantinePath)) {
                if (forceRemoveDir(quarantinePath)) {
                    ++removed;
                    queued = true;
                } else if (scheduleDeleteOnReboot(quarantinePath)) {
                    ++removed;
                    queued = true;
                }
            } else if (scheduleDeleteOnReboot(fullPath)) {
                m_pendingDeletePaths.insert(fullPath);
                ++removed;
                queued = true;
            }

            if (!queued) {
                failed.append(entry.fileName());
            }
        }
    }

    invalidateRuntimeCaches();
    m_modelCacheUntil = QDateTime();

    if (!failed.isEmpty()) {
        const QString msg = QStringLiteral("Cleared %1 model(s), but %2 could not be removed (may be locked): %3")
            .arg(removed).arg(failed.size()).arg(failed.join(QStringLiteral(", ")));
        recordLog(QStringLiteral("warning"), QStringLiteral("data"), QStringLiteral("clear_cache"), msg);
        recordAlert(QStringLiteral("warning"), msg);
        recordIssue(QStringLiteral("data"), QStringLiteral("Some cached models could not be deleted"));
        if (resultMessage) *resultMessage = msg;
        return removed > 0;
    }

    const QString msg = QStringLiteral("Cleared %1 cached model(s) from %2").arg(removed).arg(cacheDir);
    recordEvent(QStringLiteral("data_clear_cache"), msg);
    recordLog(QStringLiteral("info"), QStringLiteral("data"), QStringLiteral("clear_cache"), msg);
    if (resultMessage) *resultMessage = msg;
    return true;
}

bool ControlCenterBackend::removeModelFromCache(const QString& modelId, QString* resultMessage) {
    if (modelId.isEmpty()) {
        const QString msg = QStringLiteral("Model ID is empty");
        recordLog(QStringLiteral("warning"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
        if (resultMessage) *resultMessage = msg;
        return false;
    }

    const QString cacheDir = QDir(m_rootPath).absoluteFilePath(
        m_config.value(QStringLiteral("large_judge")).toObject()
            .value(QStringLiteral("cache_dir")).toString(QStringLiteral("data/cache/large_judge")));

    QDir baseDir(cacheDir);
    if (!baseDir.exists()) {
        const QString msg = QStringLiteral("Cache directory does not exist: %1").arg(cacheDir);
        recordLog(QStringLiteral("warning"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
        if (resultMessage) *resultMessage = msg;
        return false;
    }

    // Also check the cached model summary for the stored path
    const QJsonObject cache = buildModelCacheSummary();
    const QJsonObject judgeCache = cache.value(QStringLiteral("large_judge")).toObject();
    const QString storedPath = judgeCache.value(modelId).toObject().value(QStringLiteral("path")).toString();

    // Build candidate directory names: direct, slash→dash, models-- prefix, stored path
    const QString sanitized = sanitizeModelDir(modelId);
    const QString slashToDash = QString(modelId).replace('/', QStringLiteral("--"));
    QStringList candidates = { modelId, slashToDash, sanitized };

    for (const QString& candidate : candidates) {
        const QString fullPath = normalizedPath(baseDir.absoluteFilePath(candidate));
        if (m_pendingDeletePaths.contains(fullPath)) {
            const QString msg = QStringLiteral("Model removal is already queued: %1").arg(modelId);
            recordLog(QStringLiteral("info"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
            if (resultMessage) *resultMessage = msg;
            return true;
        }
        QDir target(fullPath);
        if (target.exists()) {
            const QString prepareStage = summarizeJob(QStringLiteral("prepare")).value(QStringLiteral("stage")).toString();
            const QString prepareMsg = summarizeJob(QStringLiteral("prepare")).value(QStringLiteral("message")).toString();
            const QString setupStage = summarizeJob(QStringLiteral("setup")).value(QStringLiteral("stage")).toString();
            const QString setupMsg = summarizeJob(QStringLiteral("setup")).value(QStringLiteral("message")).toString();
            const QString currentJudgeModel =
                m_config.value(QStringLiteral("large_judge")).toObject().value(QStringLiteral("model_id")).toString();

            const bool prepareDownloadingThis =
                (prepareStage.contains(QStringLiteral("download"), Qt::CaseInsensitive) ||
                 prepareMsg.contains(QStringLiteral("download"), Qt::CaseInsensitive)) &&
                prepareMsg.contains(modelId, Qt::CaseInsensitive);
            const bool setupDownloadingThis =
                (setupStage.contains(QStringLiteral("download"), Qt::CaseInsensitive) ||
                 setupMsg.contains(QStringLiteral("download"), Qt::CaseInsensitive)) &&
                setupMsg.contains(modelId, Qt::CaseInsensitive);
            const bool modelLikelyLoaded = (currentJudgeModel == modelId);

            QStringList releaseActions;
            if (prepareDownloadingThis && stopManagedProcess(QStringLiteral("prepare"))) {
                releaseActions.append(QStringLiteral("Stopped Prepare Data to release model download handles."));
            }
            if (setupDownloadingThis && stopManagedProcess(QStringLiteral("setup"))) {
                releaseActions.append(QStringLiteral("Stopped Environment Setup to release model download handles."));
            }
            if (modelLikelyLoaded && stopManagedProcess(QStringLiteral("inference"))) {
                releaseActions.append(QStringLiteral("Stopped Inference API to release loaded model handles."));
            }
            if (modelLikelyLoaded && stopManagedProcess(QStringLiteral("evaluate"))) {
                releaseActions.append(QStringLiteral("Stopped Evaluation to release loaded model handles."));
            }
            if (!releaseActions.isEmpty()) {
                QThread::msleep(300);
                recordLog(QStringLiteral("warning"),
                          QStringLiteral("data"),
                          QStringLiteral("remove_model_release_lock"),
                          releaseActions.join(QStringLiteral(" ")),
                          QJsonObject{{QStringLiteral("model_id"), modelId}});
            }

            const bool activelyInUse = prepareDownloadingThis || setupDownloadingThis || modelLikelyLoaded;

            if (forceRemoveDir(fullPath)) {
                m_pendingDeletePaths.remove(fullPath);
                invalidateRuntimeCaches();
                m_modelCacheUntil = QDateTime();
                const QString msg = QStringLiteral("Removed cached model: %1").arg(modelId);
                recordEvent(QStringLiteral("data_remove_model"), msg);
                recordLog(QStringLiteral("info"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
                if (resultMessage) *resultMessage = msg;
                return true;
            }

            const QString quarantinePath = pendingDeletePathFor(modelId);
            if (QDir().rename(fullPath, quarantinePath)) {
                if (forceRemoveDir(quarantinePath)) {
                    invalidateRuntimeCaches();
                    m_modelCacheUntil = QDateTime();
                    const QString msg = QStringLiteral("Removed cached model after quarantining it: %1").arg(modelId);
                    recordEvent(QStringLiteral("data_remove_model"), msg);
                    recordLog(QStringLiteral("info"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
                    if (resultMessage) *resultMessage = msg;
                    return true;
                }
                if (scheduleDeleteOnReboot(quarantinePath)) {
                    invalidateRuntimeCaches();
                    m_modelCacheUntil = QDateTime();
                    const QString msg = QStringLiteral("Model files were quarantined and queued for deletion after restart: %1").arg(modelId);
                    recordAlert(QStringLiteral("warning"), msg);
                    recordLog(QStringLiteral("warning"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
                    if (resultMessage) *resultMessage = msg;
                    return true;
                }
            }

            if (activelyInUse) {
                m_pendingDeletePaths.insert(fullPath);
                invalidateRuntimeCaches();
                m_modelCacheUntil = QDateTime();
                const QString msg = QStringLiteral(
                    "Model is currently in use. Removal has been queued and will be retried automatically: %1")
                    .arg(modelId);
                recordAlert(QStringLiteral("warning"), msg);
                recordLog(QStringLiteral("warning"),
                          QStringLiteral("data"),
                          QStringLiteral("remove_model_pending"),
                          msg,
                          QJsonObject{
                              {QStringLiteral("model_id"), modelId},
                              {QStringLiteral("path"), fullPath},
                              {QStringLiteral("prepare_downloading"), prepareDownloadingThis},
                              {QStringLiteral("setup_downloading"), setupDownloadingThis},
                              {QStringLiteral("model_loaded"), modelLikelyLoaded},
                          });
                if (resultMessage) *resultMessage = msg;
                return true;
            }

            if (scheduleDeleteOnReboot(fullPath)) {
                m_pendingDeletePaths.insert(fullPath);
                invalidateRuntimeCaches();
                m_modelCacheUntil = QDateTime();
                const QString msg = QStringLiteral("Model is locked right now, but deletion has been queued and it will disappear from the dashboard cache view: %1").arg(modelId);
                recordAlert(QStringLiteral("warning"), msg);
                recordLog(QStringLiteral("warning"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
                if (resultMessage) *resultMessage = msg;
                return true;
            }

            const QString msg = QStringLiteral("Found model directory but could not delete or queue it for removal: %1").arg(fullPath);
            recordLog(QStringLiteral("error"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
            recordIssue(QStringLiteral("data"), QStringLiteral("Could not delete cached model: %1").arg(modelId));
            if (resultMessage) *resultMessage = msg;
            return false;
        }
    }

    // Try the stored path from the cache summary directly
    const QString normalizedStoredPath = normalizedPath(storedPath);
    if (!storedPath.isEmpty() && QDir(normalizedStoredPath).exists()) {
        if (m_pendingDeletePaths.contains(normalizedStoredPath)) {
            const QString msg = QStringLiteral("Model removal is already queued: %1").arg(modelId);
            recordLog(QStringLiteral("info"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
            if (resultMessage) *resultMessage = msg;
            return true;
        }
        if (forceRemoveDir(normalizedStoredPath)) {
            m_pendingDeletePaths.remove(normalizedStoredPath);
            invalidateRuntimeCaches();
            m_modelCacheUntil = QDateTime();
            const QString msg = QStringLiteral("Removed cached model: %1").arg(modelId);
            recordEvent(QStringLiteral("data_remove_model"), msg);
            recordLog(QStringLiteral("info"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
            if (resultMessage) *resultMessage = msg;
            return true;
        }
        if (scheduleDeleteOnReboot(normalizedStoredPath)) {
            m_pendingDeletePaths.insert(normalizedStoredPath);
            invalidateRuntimeCaches();
            m_modelCacheUntil = QDateTime();
            const QString msg = QStringLiteral("Model is locked right now, but deletion has been queued: %1").arg(modelId);
            recordAlert(QStringLiteral("warning"), msg);
            recordLog(QStringLiteral("warning"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
            if (resultMessage) *resultMessage = msg;
            return true;
        }
        const QString msg = QStringLiteral("Could not delete or queue model directory for removal: %1").arg(normalizedStoredPath);
        recordLog(QStringLiteral("error"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
        if (resultMessage) *resultMessage = msg;
        return false;
    }

    const QString msg = QStringLiteral("Model not found in cache: %1").arg(modelId);
    recordLog(QStringLiteral("warning"), QStringLiteral("data"), QStringLiteral("remove_model"), msg);
    if (resultMessage) *resultMessage = msg;
    return false;
}

bool ControlCenterBackend::clearActionHistory(QString* resultMessage) {
    m_recentBackendLogs.clear();
    bool ok = forceRemovePath(m_backendLogPath);
    if (!ok) {
        ok = writeTextFile(m_backendLogPath, QString());
    }
    invalidateRuntimeCaches();
    const QString msg = ok
        ? QStringLiteral("Action history cleared")
        : QStringLiteral("Could not clear action history");
    printConsoleLine(QStringLiteral("[data] %1").arg(msg), !ok);
    if (resultMessage) {
        *resultMessage = msg;
    }
    return ok;
}

bool ControlCenterBackend::deleteActionHistoryEntry(const QString& signature, QString* resultMessage) {
    const QString trimmedSignature = signature.trimmed();
    if (trimmedSignature.isEmpty()) {
        if (resultMessage) {
            *resultMessage = QStringLiteral("Action history signature is required");
        }
        return false;
    }

    int removedCount = 0;
    QStringList keptLines;
    QString text;
    if (readTextFile(m_backendLogPath, &text)) {
        const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QJsonObject row;
            if (!parseJsonObject(line.toUtf8(), &row, nullptr)) {
                keptLines.append(line);
                continue;
            }
            const QString rowSignature = actionHistorySignature(row.value(QStringLiteral("ts")).toDouble(),
                                                                row.value(QStringLiteral("severity")).toString(),
                                                                row.value(QStringLiteral("category")).toString(),
                                                                row.value(QStringLiteral("action")).toString(),
                                                                row.value(QStringLiteral("message")).toString());
            if (rowSignature == trimmedSignature) {
                ++removedCount;
                continue;
            }
            keptLines.append(line);
        }
    }

    QList<BackendLogEntry> keptEntries;
    keptEntries.reserve(m_recentBackendLogs.size());
    for (const BackendLogEntry& entry : std::as_const(m_recentBackendLogs)) {
        const QString rowSignature = actionHistorySignature(entry.ts,
                                                            entry.severity,
                                                            entry.category,
                                                            entry.action,
                                                            entry.message);
        if (rowSignature == trimmedSignature) {
            ++removedCount;
            continue;
        }
        keptEntries.append(entry);
    }
    m_recentBackendLogs = keptEntries;

    if (removedCount <= 0) {
        if (resultMessage) {
            *resultMessage = QStringLiteral("Action history entry was not found");
        }
        return false;
    }

    bool ok = true;
    if (keptLines.isEmpty()) {
        ok = forceRemovePath(m_backendLogPath);
        if (!ok) {
            ok = writeTextFile(m_backendLogPath, QString());
        }
    } else {
        ok = writeTextFile(m_backendLogPath, keptLines.join(QLatin1Char('\n')) + QLatin1Char('\n'));
    }

    invalidateRuntimeCaches();
    const QString msg = ok
        ? QStringLiteral("Action history entry removed")
        : QStringLiteral("Could not rewrite action history");
    printConsoleLine(QStringLiteral("[data] %1").arg(msg), !ok);
    if (resultMessage) {
        *resultMessage = msg;
    }
    return ok;
}

bool ControlCenterBackend::deleteManagedProjectPath(const QString& relativePath, QString* resultMessage) {
    const QString trimmedPath = QDir::cleanPath(relativePath.trimmed());
    if (trimmedPath.isEmpty() || trimmedPath == QStringLiteral(".") || trimmedPath == QStringLiteral("..")) {
        if (resultMessage) {
            *resultMessage = QStringLiteral("A managed project path is required");
        }
        return false;
    }

    const QString absolutePath = normalizedPath(QDir(m_rootPath).absoluteFilePath(trimmedPath));
    const QString root = normalizedPath(m_rootPath);
    if (!(absolutePath == root
          || absolutePath.startsWith(root + QLatin1Char('/'))
          || absolutePath.startsWith(root + QLatin1Char('\\')))) {
        if (resultMessage) {
            *resultMessage = QStringLiteral("Refusing to delete a path outside the project root");
        }
        return false;
    }

    const QString logsDir = rootPathFor(QStringLiteral("logs"));
    const QString tensorboardDir = rootPathFor(QStringLiteral("logs/tensorboard"));
    const QString cacheDir = QDir(m_rootPath).absoluteFilePath(
        m_config.value(QStringLiteral("datasets")).toObject().value(QStringLiteral("cache_dir")).toString(QStringLiteral("data/cache")));
    const QString dataDir = QDir(m_rootPath).absoluteFilePath(
        m_config.value(QStringLiteral("datasets")).toObject().value(QStringLiteral("data_dir")).toString(QStringLiteral("data/processed")));

    const QStringList allowedRoots = {
        rootPathFor(QStringLiteral(".venv")),
        rootPathFor(QStringLiteral(".tmp/runtime_state")),
        rootPathFor(QStringLiteral(".tmp/runtime_workers")),
        cacheDir,
        dataDir,
        m_checkpointDir,
        m_reportDir,
        logsDir,
    };

    bool allowed = false;
    for (const QString& allowedRoot : allowedRoots) {
        if (pathEqualsOrWithin(absolutePath, allowedRoot)) {
            allowed = true;
            break;
        }
    }
    if (!allowed) {
        if (resultMessage) {
            *resultMessage = QStringLiteral("That path is not a managed project artifact");
        }
        return false;
    }

    const QFileInfo targetInfo(absolutePath);
    if (!targetInfo.exists()) {
        if (resultMessage) {
            *resultMessage = QStringLiteral("Managed artifact not found: %1").arg(trimmedPath);
        }
        return false;
    }

    const bool isBackendLog = absolutePath == normalizedPath(m_backendLogPath);
    const bool isFeedLog = absolutePath == normalizedPath(m_feedPath);
    const bool isTensorboard = pathEqualsOrWithin(absolutePath, tensorboardDir);
    const bool disruptiveDelete = !pathEqualsOrWithin(absolutePath, logsDir) || isTensorboard;
    if (disruptiveDelete) {
        stopAutopilot();
        const QStringList managedNames = {
            QStringLiteral("setup"),
            QStringLiteral("prepare"),
            QStringLiteral("training"),
            QStringLiteral("evaluate"),
            QStringLiteral("inference"),
        };
        for (const QString& name : managedNames) {
            stopManagedProcess(name);
        }
        QThread::msleep(250);
    }

    if (isBackendLog) {
        m_recentBackendLogs.clear();
    }

    bool ok = forceRemovePath(absolutePath);
    bool queuedForReboot = false;
    if (!ok) {
        queuedForReboot = scheduleDeleteOnReboot(absolutePath);
        ok = queuedForReboot;
        if (queuedForReboot) {
            m_pendingDeletePaths.insert(absolutePath);
        }
    } else {
        m_pendingDeletePaths.remove(absolutePath);
    }

    refreshRecoveredProcessStates();
    restoreAutopilotRuntimeState();
    invalidateRuntimeCaches();
    m_modelCacheUntil = QDateTime();

    const QString msg = ok
        ? (queuedForReboot
               ? QStringLiteral("Managed artifact queued for deletion after restart: %1").arg(trimmedPath)
               : QStringLiteral("Managed artifact deleted: %1").arg(trimmedPath))
        : QStringLiteral("Could not delete managed artifact: %1").arg(trimmedPath);

    if (!isBackendLog && !isFeedLog) {
        if (ok && !queuedForReboot) {
            recordEvent(QStringLiteral("data_delete_path"), msg);
            recordLog(QStringLiteral("info"),
                      QStringLiteral("data"),
                      QStringLiteral("delete_path"),
                      msg,
                      QJsonObject{{QStringLiteral("path"), trimmedPath}});
        } else if (ok) {
            recordAlert(QStringLiteral("warning"), msg);
            recordLog(QStringLiteral("warning"),
                      QStringLiteral("data"),
                      QStringLiteral("delete_path"),
                      msg,
                      QJsonObject{{QStringLiteral("path"), trimmedPath}});
        } else {
            recordLog(QStringLiteral("error"),
                      QStringLiteral("data"),
                      QStringLiteral("delete_path"),
                      msg,
                      QJsonObject{{QStringLiteral("path"), trimmedPath}});
        }
    } else {
        printConsoleLine(QStringLiteral("[data] %1").arg(msg), !ok);
    }

    if (resultMessage) {
        *resultMessage = msg;
    }
    return ok;
}

QJsonObject ControlCenterBackend::cleanupProjectFiles(const QString& scope) {
    const QString normalizedScope = scope.trimmed().toLower();
    if (normalizedScope.isEmpty()) {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("message"), QStringLiteral("Cleanup scope is required")},
        };
    }

    struct CleanupTarget {
        QString label;
        QString path;
        bool preservePlaceholders = true;
        QSet<QString> excludedPaths;
    };

    QList<CleanupTarget> targets;
    const QString logsDir = rootPathFor(QStringLiteral("logs"));
    const QString cacheDir = QDir(m_rootPath).absoluteFilePath(
        m_config.value(QStringLiteral("datasets")).toObject().value(QStringLiteral("cache_dir")).toString(QStringLiteral("data/cache")));
    const QString dataDir = QDir(m_rootPath).absoluteFilePath(
        m_config.value(QStringLiteral("datasets")).toObject().value(QStringLiteral("data_dir")).toString(QStringLiteral("data/processed")));
    const QString judgeCacheDir = QDir(m_rootPath).absoluteFilePath(
        m_config.value(QStringLiteral("large_judge")).toObject().value(QStringLiteral("cache_dir")).toString(QStringLiteral("data/cache/large_judge")));

    if (normalizedScope == QStringLiteral("dependencies") || normalizedScope == QStringLiteral("venv")) {
        targets = {
            {QStringLiteral("virtualenv"), rootPathFor(QStringLiteral(".venv")), false, {}},
            {QStringLiteral("runtime_state"), rootPathFor(QStringLiteral(".tmp/runtime_state")), false, {}},
            {QStringLiteral("runtime_workers"), rootPathFor(QStringLiteral(".tmp/runtime_workers")), false, {}},
        };
    } else if (normalizedScope == QStringLiteral("cache")) {
        targets = {
            {QStringLiteral("cache"), cacheDir, true, {}},
        };
    } else if (normalizedScope == QStringLiteral("dataset_cache")
               || normalizedScope == QStringLiteral("source_cache")) {
        targets = {
            {QStringLiteral("dataset_cache"), cacheDir, true, QSet<QString>{normalizedPath(judgeCacheDir)}},
        };
    } else if (normalizedScope == QStringLiteral("processed")
               || normalizedScope == QStringLiteral("datasets")
               || normalizedScope == QStringLiteral("data")) {
        targets = {
            {QStringLiteral("processed_data"), dataDir, true, {}},
        };
    } else if (normalizedScope == QStringLiteral("checkpoints")) {
        targets = {
            {QStringLiteral("checkpoints"), m_checkpointDir, true, {}},
        };
    } else if (normalizedScope == QStringLiteral("artifacts")
               || normalizedScope == QStringLiteral("reports")) {
        targets = {
            {QStringLiteral("artifacts"), m_reportDir, true, {}},
        };
    } else if (normalizedScope == QStringLiteral("logs")) {
        targets = {
            {QStringLiteral("logs"), logsDir, true, {}},
        };
    } else if (normalizedScope == QStringLiteral("all")
               || normalizedScope == QStringLiteral("all_generated")
               || normalizedScope == QStringLiteral("generated")) {
        targets = {
            {QStringLiteral("virtualenv"), rootPathFor(QStringLiteral(".venv")), false, {}},
            {QStringLiteral("runtime_state"), rootPathFor(QStringLiteral(".tmp/runtime_state")), false, {}},
            {QStringLiteral("runtime_workers"), rootPathFor(QStringLiteral(".tmp/runtime_workers")), false, {}},
            {QStringLiteral("cache"), cacheDir, true, {}},
            {QStringLiteral("processed_data"), dataDir, true, {}},
            {QStringLiteral("checkpoints"), m_checkpointDir, true, {}},
            {QStringLiteral("artifacts"), m_reportDir, true, {}},
            {QStringLiteral("logs"), logsDir, true, {}},
        };
    } else {
        return QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("message"), QStringLiteral("Unsupported cleanup scope: %1").arg(scope)},
        };
    }

    const bool disruptiveCleanup = normalizedScope != QStringLiteral("logs");
    if (disruptiveCleanup) {
        stopAutopilot();
        const QStringList managedNames = {
            QStringLiteral("setup"),
            QStringLiteral("prepare"),
            QStringLiteral("training"),
            QStringLiteral("evaluate"),
            QStringLiteral("inference"),
        };
        for (const QString& name : managedNames) {
            stopManagedProcess(name);
        }
        QThread::msleep(250);
    }

    QStringList removedEntries;
    QStringList failedEntries;
    for (const CleanupTarget& target : targets) {
        clearDirectoryContents(target.path,
                               &removedEntries,
                               &failedEntries,
                               target.preservePlaceholders,
                               target.excludedPaths);
    }

    refreshRecoveredProcessStates();
    restoreAutopilotRuntimeState();
    invalidateRuntimeCaches();
    m_modelCacheUntil = QDateTime();
    if (normalizedScope == QStringLiteral("logs")) {
        m_recentBackendLogs.clear();
    }

    const bool ok = failedEntries.isEmpty();
    const QString message = ok
        ? QStringLiteral("Cleanup complete for '%1' (%2 item(s) removed)").arg(normalizedScope).arg(removedEntries.size())
        : QStringLiteral("Cleanup for '%1' removed %2 item(s), but %3 item(s) could not be deleted")
              .arg(normalizedScope)
              .arg(removedEntries.size())
              .arg(failedEntries.size());
    if (normalizedScope == QStringLiteral("logs")) {
        printConsoleLine(QStringLiteral("[data] %1").arg(message), !ok);
    } else {
        recordLog(ok ? QStringLiteral("info") : QStringLiteral("warning"),
                  QStringLiteral("data"),
                  QStringLiteral("cleanup"),
                  message,
                  QJsonObject{
                      {QStringLiteral("scope"), normalizedScope},
                      {QStringLiteral("removed_count"), removedEntries.size()},
                      {QStringLiteral("failed_count"), failedEntries.size()},
                  });
        if (ok) {
            recordEvent(QStringLiteral("data_cleanup"), message);
        } else {
            recordAlert(QStringLiteral("warning"), message);
        }
    }

    return QJsonObject{
        {QStringLiteral("ok"), ok},
        {QStringLiteral("scope"), normalizedScope},
        {QStringLiteral("message"), message},
        {QStringLiteral("removed"), QJsonArray::fromStringList(removedEntries)},
        {QStringLiteral("failed"), QJsonArray::fromStringList(failedEntries)},
    };
}

QJsonObject ControlCenterBackend::checkDataIntegrity() {
    QJsonObject result;
    QJsonArray warnings;
    QJsonArray errors;

    // Check config file
    const QFileInfo configInfo(m_configPath);
    result.insert(QStringLiteral("config_exists"), configInfo.exists());
    result.insert(QStringLiteral("config_size"), configInfo.exists() ? configInfo.size() : 0);
    if (!configInfo.exists()) {
        errors.append(QStringLiteral("Config file not found: %1").arg(m_configPath));
    } else if (m_config.isEmpty()) {
        errors.append(QStringLiteral("Config file exists but could not be parsed"));
    }

    // Check dataset sources and whether their paths exist
    const auto datasets = m_config.value(QStringLiteral("datasets")).toObject();
    const auto sources = datasets.value(QStringLiteral("sources")).toArray();
    result.insert(QStringLiteral("dataset_sources"), sources.size());
    if (sources.isEmpty()) {
        warnings.append(QStringLiteral("No dataset sources configured"));
    }
    QJsonArray sourceDetails;
    for (const auto& srcVal : sources) {
        const auto src = srcVal.toObject();
        const QString name = src.value(QStringLiteral("name")).toString(QStringLiteral("unnamed"));
        const QString path = src.value(QStringLiteral("path")).toString();
        QJsonObject detail;
        detail.insert(QStringLiteral("name"), name);
        detail.insert(QStringLiteral("type"), src.value(QStringLiteral("type")).toString(QStringLiteral("unknown")));
        if (!path.isEmpty()) {
            const QString fullPath = QDir(m_rootPath).absoluteFilePath(path);
            const bool exists = QFileInfo::exists(fullPath);
            detail.insert(QStringLiteral("path"), path);
            detail.insert(QStringLiteral("path_exists"), exists);
            if (!exists) {
                warnings.append(QStringLiteral("Dataset source '%1' path not found: %2").arg(name, path));
            }
        }
        sourceDetails.append(detail);
    }
    result.insert(QStringLiteral("sources"), sourceDetails);

    // Check checkpoint directory
    const QDir ckptDir(m_checkpointDir);
    const bool ckptExists = ckptDir.exists();
    const int ckptCount = ckptExists
        ? ckptDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot).size() : 0;
    result.insert(QStringLiteral("checkpoint_dir_exists"), ckptExists);
    result.insert(QStringLiteral("checkpoint_count"), ckptCount);
    if (!ckptExists) {
        warnings.append(QStringLiteral("Checkpoint directory does not exist: %1").arg(m_checkpointDir));
    }

    // Check model cache
    const QString cacheDir = QDir(m_rootPath).absoluteFilePath(
        m_config.value(QStringLiteral("large_judge")).toObject()
            .value(QStringLiteral("cache_dir")).toString(QStringLiteral("data/cache/large_judge")));
    const QDir cache(cacheDir);
    const bool cacheExists = cache.exists();
    const int cachedModels = cacheExists
        ? cache.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot).size() : 0;
    result.insert(QStringLiteral("cached_models"), cachedModels);
    result.insert(QStringLiteral("cache_dir"), cacheDir);
    result.insert(QStringLiteral("cache_dir_exists"), cacheExists);

    // Calculate total cache size
    qint64 totalCacheBytes = 0;
    if (cacheExists) {
        const QFileInfoList cacheEntries = cache.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& entry : cacheEntries) {
            QDirIterator it(entry.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                totalCacheBytes += it.fileInfo().size();
            }
        }
    }
    result.insert(QStringLiteral("cache_size_mb"), static_cast<qint64>(totalCacheBytes / (1024 * 1024)));

    // Check report directory
    const QDir reportDir(m_reportDir);
    const int reportCount = reportDir.exists()
        ? reportDir.entryInfoList(QDir::Files).size() : 0;
    result.insert(QStringLiteral("report_count"), reportCount);

    // Check feed log
    const QFileInfo feedInfo(m_feedPath);
    result.insert(QStringLiteral("feed_log_exists"), feedInfo.exists());
    result.insert(QStringLiteral("feed_log_size_kb"), feedInfo.exists() ? feedInfo.size() / 1024 : 0);
    if (feedInfo.exists() && feedInfo.size() > 50 * 1024 * 1024) {
        warnings.append(QStringLiteral("Feed log is very large (%1 MB), consider clearing it").arg(feedInfo.size() / (1024 * 1024)));
    }

    // Check backend log
    const QFileInfo backendLogInfo(m_backendLogPath);
    result.insert(QStringLiteral("backend_log_exists"), backendLogInfo.exists());
    result.insert(QStringLiteral("backend_log_size_kb"), backendLogInfo.exists() ? backendLogInfo.size() / 1024 : 0);

    // Overall status
    const bool hasErrors = !errors.isEmpty();
    result.insert(QStringLiteral("ok"), !hasErrors);
    result.insert(QStringLiteral("warnings"), warnings);
    result.insert(QStringLiteral("errors"), errors);
    result.insert(QStringLiteral("checked_at"), isoNow());

    const QString severity = hasErrors ? QStringLiteral("error")
        : !warnings.isEmpty() ? QStringLiteral("warning")
        : QStringLiteral("info");
    const QString msg = QStringLiteral("Data integrity check: %1 error(s), %2 warning(s)")
        .arg(errors.size()).arg(warnings.size());
    recordLog(severity, QStringLiteral("data"), QStringLiteral("integrity_check"), msg);
    recordEvent(QStringLiteral("data_check"), msg);

    if (hasErrors) {
        recordIssue(QStringLiteral("data"), QStringLiteral("Data integrity check found errors"));
    }

    return result;
}
