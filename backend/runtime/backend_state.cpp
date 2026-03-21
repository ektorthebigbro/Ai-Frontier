#include "../backend.h"
#include "../common/backend_common.h"
#include "../config/simple_yaml.h"
#include <algorithm>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace ControlCenterBackendCommon;

namespace {

QString prettyJobName(const QString& job) {
    if (job == QStringLiteral("setup")) return QStringLiteral("Environment Setup");
    if (job == QStringLiteral("prepare")) return QStringLiteral("Prepare Data");
    if (job == QStringLiteral("training")) return QStringLiteral("Training");
    if (job == QStringLiteral("evaluate")) return QStringLiteral("Evaluation");
    if (job == QStringLiteral("inference")) return QStringLiteral("Inference");
    if (job == QStringLiteral("autopilot")) return QStringLiteral("Autopilot");
    if (job == QStringLiteral("server")) return QStringLiteral("Server");
    return job;
}

QString autopilotStageJobName(const QString& rawStage) {
    const QString stage = rawStage.trimmed().toLower();
    if (stage == QStringLiteral("setup") || stage == QStringLiteral("environment")) return QStringLiteral("setup");
    if (stage == QStringLiteral("prepare") || stage == QStringLiteral("dataset_prep")) return QStringLiteral("prepare");
    if (stage == QStringLiteral("training")) return QStringLiteral("training");
    if (stage == QStringLiteral("evaluate") || stage == QStringLiteral("evaluation")) return QStringLiteral("evaluate");
    return QString();
}

QString normalizeAutopilotSummaryStage(const QString& rawStage) {
    const QString stage = rawStage.trimmed().toLower();
    if (stage == QStringLiteral("environment")) return QStringLiteral("setup");
    if (stage == QStringLiteral("dataset_prep")) return QStringLiteral("prepare");
    if (stage == QStringLiteral("evaluation")) return QStringLiteral("evaluate");
    return stage;
}

double autopilotSummaryBaselineProgress(const QString& rawStage) {
    const QString stage = normalizeAutopilotSummaryStage(rawStage);
    if (stage == QStringLiteral("setup")) return 0.05;
    if (stage == QStringLiteral("prepare")) return 0.18;
    if (stage == QStringLiteral("training")) return 0.35;
    if (stage == QStringLiteral("evaluate")) return 0.85;
    if (stage == QStringLiteral("completed")) return 1.0;
    return 0.0;
}

QString latestMeaningfulProcessLogLine(const ManagedProcessState& state) {
    for (auto it = state.logLines.crbegin(); it != state.logLines.crend(); ++it) {
        const QString line = it->trimmed();
        if (line.isEmpty() || line.startsWith(QStringLiteral("[progress]"))) {
            continue;
        }
        return line;
    }
    return QString();
}

QString displayPathForClient(const QString& rootPath, const QString& rawPath) {
    if (rawPath.trimmed().isEmpty()) {
        return QString();
    }
    const QFileInfo info(rawPath);
    const QString absolute = QDir::cleanPath(info.absoluteFilePath());
    const QString cleanRoot = QDir::cleanPath(rootPath);
    if (absolute == cleanRoot) {
        return QStringLiteral(".");
    }
    if (absolute.startsWith(cleanRoot + QLatin1Char('/')) || absolute.startsWith(cleanRoot + QLatin1Char('\\'))) {
        return QDir(cleanRoot).relativeFilePath(absolute);
    }
    return info.fileName().isEmpty() ? absolute : info.fileName();
}

QJsonObject configForClient(const QJsonObject& config) {
    return redactSecrets(config).toObject();
}

QString configTextForClient(const QJsonObject& config) {
    return QString::fromUtf8(QJsonDocument(configForClient(config)).toJson(QJsonDocument::Indented));
}

QJsonObject readJsonObjectFile(const QString& path) {
    QString text;
    if (!readTextFile(path, &text)) {
        return {};
    }
    QJsonObject object;
    if (!parseJsonObject(text.toUtf8(), &object, nullptr)) {
        return {};
    }
    return object;
}

QString formatLossWindow(double seconds) {
    if (seconds <= 0.0) {
        return QStringLiteral("none");
    }
    return formatEta(seconds);
}

QStringList autopilotManagedJobs() {
    return {
        QStringLiteral("setup"),
        QStringLiteral("prepare"),
        QStringLiteral("training"),
        QStringLiteral("evaluate"),
    };
}

bool retryRemoveDir(const QString& path) {
    QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFile f(it.filePath());
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser);
    }
    return QDir(path).removeRecursively();
}

QJsonArray loadJsonlTail(const QString& path, int maxRows, int* invalidRows) {
    QJsonArray rows;
    const QStringList lines = readTailLinesFromFile(path, maxRows);

    int badCount = 0;
    for (const QString& line : std::as_const(lines)) {
        if (line.isEmpty()) {
            continue;
        }
        const auto doc = QJsonDocument::fromJson(line.toUtf8());
        if (!doc.isObject()) {
            ++badCount;
            continue;
        }
        rows.append(doc.object());
    }
    if (invalidRows) {
        *invalidRows = badCount;
    }
    return rows;
}

QJsonArray filterRowsSince(const QJsonArray& rows, double minTs) {
    if (minTs <= 0.0 || rows.isEmpty()) {
        return rows;
    }

    QJsonArray filtered;
    for (const QJsonValue& value : rows) {
        const QJsonObject row = value.toObject();
        if (row.value(QStringLiteral("ts")).toDouble() >= minTs) {
            filtered.append(row);
        }
    }
    return filtered;
}

bool isTerminalOrIdleStage(const QString& stage) {
    return stage.isEmpty()
        || stage == QStringLiteral("idle")
        || stage == QStringLiteral("completed")
        || stage == QStringLiteral("failed")
        || stage == QStringLiteral("stopped");
}

double fileTimestamp(const QFileInfo& info) {
    if (!info.exists() || !info.lastModified().isValid()) {
        return 0.0;
    }
    return info.lastModified().toSecsSinceEpoch();
}

bool isPlaceholderEntry(const QFileInfo& info) {
    const QString name = info.fileName();
    return name == QStringLiteral(".gitkeep") || name == QStringLiteral(".gitignore");
}

QFileInfoList topLevelMatches(const QString& dirPath, const QStringList& patterns) {
    const QDir dir(dirPath);
    if (!dir.exists()) {
        return {};
    }

    QFileInfoList filtered;
    const QFileInfoList matches = dir.entryInfoList(patterns,
                                                    QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                                    QDir::Time);
    for (const QFileInfo& info : matches) {
        if (!isPlaceholderEntry(info)) {
            filtered.append(info);
        }
    }
    return filtered;
}

bool hasTopLevelMatch(const QString& dirPath, const QStringList& patterns) {
    return !topLevelMatches(dirPath, patterns).isEmpty();
}

double newestMatchTimestamp(const QString& dirPath, const QStringList& patterns) {
    double newest = 0.0;
    for (const QFileInfo& info : topLevelMatches(dirPath, patterns)) {
        newest = qMax(newest, fileTimestamp(info));
    }
    return newest;
}

double newestDirectoryEntryTimestamp(const QString& dirPath) {
    const QDir dir(dirPath);
    if (!dir.exists()) {
        return 0.0;
    }

    double newest = 0.0;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                                    QDir::Time);
    for (const QFileInfo& info : entries) {
        if (isPlaceholderEntry(info)) {
            continue;
        }
        newest = qMax(newest, fileTimestamp(info));
    }
    return newest;
}

double newestDirectoryEntryTimestampExcluding(const QString& dirPath, const QSet<QString>& excludedPaths) {
    const QDir dir(dirPath);
    if (!dir.exists()) {
        return 0.0;
    }

    double newest = 0.0;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                                    QDir::Time);
    for (const QFileInfo& info : entries) {
        if (isPlaceholderEntry(info)) {
            continue;
        }
        const QString absolutePath = QDir::cleanPath(info.absoluteFilePath());
        if (excludedPaths.contains(absolutePath)) {
            continue;
        }
        newest = qMax(newest, fileTimestamp(info));
    }
    return newest;
}

bool hasMeaningfulDirectoryEntries(const QString& dirPath) {
    return newestDirectoryEntryTimestamp(dirPath) > 0.0;
}

bool hasMeaningfulDirectoryEntriesExcluding(const QString& dirPath, const QSet<QString>& excludedPaths) {
    return newestDirectoryEntryTimestampExcluding(dirPath, excludedPaths) > 0.0;
}

QJsonObject makeDerivedJobSummary(const QString& name,
                                  const QString& stage,
                                  const QString& message,
                                  double progress,
                                  double updatedAt) {
    const QString normalizedStage = stage.isEmpty() ? QStringLiteral("idle") : stage;
    const bool completed = normalizedStage == QStringLiteral("completed");
    return QJsonObject{
        {QStringLiteral("job"), name},
        {QStringLiteral("label"), prettyJobName(name)},
        {QStringLiteral("stage"), normalizedStage},
        {QStringLiteral("message"), message},
        {QStringLiteral("progress"), qBound(0.0, progress, 1.0)},
        {QStringLiteral("eta"), completed ? QStringLiteral("0s") : QStringLiteral("unknown")},
        {QStringLiteral("eta_seconds"), completed ? QJsonValue(0.0) : QJsonValue()},
        {QStringLiteral("updated_at"), updatedAt},
        {QStringLiteral("steps"), QJsonArray{}},
    };
}

QJsonObject summarizeJobRows(const QString& name, const QJsonArray& rows) {
    if (rows.isEmpty()) {
        return QJsonObject{
            {QStringLiteral("job"), name},
            {QStringLiteral("stage"), QStringLiteral("idle")},
            {QStringLiteral("message"), QString()},
            {QStringLiteral("progress"), 0.0},
            {QStringLiteral("eta"), QStringLiteral("unknown")},
            {QStringLiteral("eta_seconds"), QJsonValue()},
            {QStringLiteral("updated_at"), 0.0},
            {QStringLiteral("steps"), QJsonArray{}},
        };
    }

    QList<QJsonObject> orderedRows;
    orderedRows.reserve(rows.size());
    for (const QJsonValue& value : rows) {
        orderedRows.append(value.toObject());
    }
    std::sort(orderedRows.begin(), orderedRows.end(), [](const QJsonObject& left, const QJsonObject& right) {
        return left.value(QStringLiteral("ts")).toDouble() < right.value(QStringLiteral("ts")).toDouble();
    });

    // Keep only the latest run segment so restarted jobs do not inherit
    // stale progress from an older completed/failed run.
    QList<QJsonObject> activeRows;
    QString previousStage;
    auto isTerminalStage = [](const QString& stage) {
        return stage == QStringLiteral("completed")
            || stage == QStringLiteral("failed")
            || stage == QStringLiteral("stopped");
    };
    for (const QJsonObject& row : std::as_const(orderedRows)) {
        const QString stage = row.value(QStringLiteral("stage")).toString();
        if (stage == QStringLiteral("starting") && !activeRows.isEmpty() && isTerminalStage(previousStage)) {
            activeRows.clear();
        }
        activeRows.append(row);
        previousStage = stage;
    }

    double maxProgress = 0.0;
    double updatedAt = 0.0;
    QString latestStage = QStringLiteral("idle");
    QString latestMessage;
    QHash<QString, QJsonObject> deduped;
    QList<QPair<double, double>> progressSamples;

    for (const QJsonObject& row : std::as_const(activeRows)) {
        const double progress = qBound(0.0, row.value(QStringLiteral("progress")).toDouble(), 1.0);
        const double ts = row.value(QStringLiteral("ts")).toDouble();
        const QString stage = row.value(QStringLiteral("stage")).toString();
        const QString message = row.value(QStringLiteral("message")).toString();

        maxProgress = qMax(maxProgress, progress);
        updatedAt = qMax(updatedAt, ts);
        latestStage = stage;
        latestMessage = message;

        const QString dedupeKey = stage + QStringLiteral("::") + message;
        const auto existing = deduped.value(dedupeKey);
        if (existing.isEmpty() || progress >= existing.value(QStringLiteral("progress")).toDouble()) {
            deduped.insert(dedupeKey, row);
        }
        if (ts > 0.0 && (progressSamples.isEmpty() || progress > progressSamples.last().second)) {
            progressSamples.append(qMakePair(ts, progress));
        }
    }

    double etaSeconds = -1.0;
    QString eta = QStringLiteral("unknown");
    if (latestStage == QStringLiteral("completed")) {
        maxProgress = qMax(maxProgress, 1.0);
        etaSeconds = 0.0;
        eta = QStringLiteral("0s");
    } else if (latestStage != QStringLiteral("idle")
               && latestStage != QStringLiteral("failed")
               && latestStage != QStringLiteral("stopped")
               && latestStage != QStringLiteral("paused")
               && progressSamples.size() >= 2
               && maxProgress > 0.0
               && maxProgress < 1.0) {
        const auto first = progressSamples.first();
        const auto last = progressSamples.last();
        const double elapsed = qMax(0.0, last.first - first.first);
        const double gained = qMax(0.0, last.second - first.second);
        if (elapsed > 0.0 && gained > 0.0) {
            const double rate = gained / elapsed;
            etaSeconds = qMax(0.0, (1.0 - maxProgress) / rate);
            eta = formatEta(etaSeconds);
        }
    }

    QJsonArray steps;
    QList<QJsonObject> values = deduped.values();
    std::sort(values.begin(), values.end(), [](const QJsonObject& left, const QJsonObject& right) {
        return left.value(QStringLiteral("ts")).toDouble() < right.value(QStringLiteral("ts")).toDouble();
    });
    for (const QJsonObject& row : std::as_const(values)) {
        steps.append(row);
    }
    while (steps.size() > 10) {
        steps.removeFirst();
    }

    return QJsonObject{
        {QStringLiteral("job"), name},
        {QStringLiteral("label"), prettyJobName(name)},
        {QStringLiteral("stage"), latestStage},
        {QStringLiteral("message"), latestMessage},
        {QStringLiteral("progress"), maxProgress},
        {QStringLiteral("eta"), eta},
        {QStringLiteral("eta_seconds"), etaSeconds >= 0.0 ? QJsonValue(etaSeconds) : QJsonValue()},
        {QStringLiteral("updated_at"), updatedAt},
        {QStringLiteral("steps"), steps},
    };
}

QString latestFileText(const QString& directoryPath, const QString& pattern) {
    QDir dir(directoryPath);
    if (!dir.exists()) {
        return QString();
    }
    const QFileInfoList files = dir.entryInfoList(QStringList{pattern}, QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        return QString();
    }
    QString text;
    readTextFile(files.first().absoluteFilePath(), &text);
    return text;
}

QString latestCheckpointCandidate(const QString& checkpointDir) {
    QDir dir(checkpointDir);
    if (!dir.exists()) {
        return QString();
    }
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo& entry : entries) {
        if (entry.isDir()) {
            const QString checkpointFile = QDir(entry.absoluteFilePath()).absoluteFilePath(QStringLiteral("checkpoint.pt"));
            if (QFileInfo::exists(checkpointFile)) {
                return entry.absoluteFilePath();
            }
        }
        if (entry.isFile()) {
            const QString suffix = entry.suffix().toLower();
            if (suffix == QStringLiteral("pt") || suffix == QStringLiteral("pth") || suffix == QStringLiteral("bin")) {
                return entry.absoluteFilePath();
            }
        }
    }
    return QString();
}

QJsonObject scanModelCache(const QString& cacheDirPath) {
    QJsonObject summary;
    QDir cacheDir(cacheDirPath);
    if (!cacheDir.exists()) {
        return summary;
    }

    const QFileInfoList entries = cacheDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries) {
        // Skip hidden dirs, lock files, and HF metadata dirs
        const QString dirName = entry.fileName();
        if (dirName.startsWith('.') || dirName == QStringLiteral("locks")
            || dirName == QStringLiteral("__pycache__") || dirName == QStringLiteral("temp")) {
            continue;
        }

        const QDir modelDir(entry.absoluteFilePath());
        const QFileInfoList files = modelDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        bool hasModel = false;
        bool hasTokenizer = false;
        qint64 totalBytes = 0;

        QList<QFileInfo> stack = files;
        while (!stack.isEmpty()) {
            const QFileInfo current = stack.takeFirst();
            if (current.isDir()) {
                const QDir nested(current.absoluteFilePath());
                const QFileInfoList nestedEntries = nested.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
                for (const QFileInfo& item : nestedEntries) {
                    stack.append(item);
                }
                continue;
            }
            totalBytes += current.size();
            const QString name = current.fileName();
            const QString suffix = current.suffix().toLower();
            hasModel = hasModel || suffix == QStringLiteral("safetensors") || suffix == QStringLiteral("bin");
            hasTokenizer = hasTokenizer || name.startsWith(QStringLiteral("tokenizer")) || name == QStringLiteral("special_tokens_map.json");
        }

        QString modelId = entry.fileName();
        if (modelId.startsWith(QStringLiteral("models--"))) {
            modelId = modelId.mid(QStringLiteral("models--").size()).replace(QStringLiteral("--"), QStringLiteral("/"));
        }

        summary.insert(modelId, QJsonObject{
            {QStringLiteral("cached"), hasModel && hasTokenizer},
            {QStringLiteral("has_model"), hasModel},
            {QStringLiteral("has_tokenizer"), hasTokenizer},
            {QStringLiteral("size_mb"), static_cast<double>(totalBytes) / (1024.0 * 1024.0)},
            {QStringLiteral("path"), entry.absoluteFilePath()},
        });
    }
    return summary;
}

QJsonObject queryNvidiaSmi() {
    QProcess proc;
    proc.start(QStringLiteral("nvidia-smi"), {
        QStringLiteral("--query-gpu=name,utilization.gpu,memory.used,memory.total,temperature.gpu"),
        QStringLiteral("--format=csv,noheader,nounits"),
    });
    if (!proc.waitForFinished(3000) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        return QJsonObject{};
    }

    const QString output = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    const QStringList parts = output.split(',', Qt::SkipEmptyParts);
    if (parts.size() < 5) {
        return QJsonObject{};
    }

    return QJsonObject{
        {QStringLiteral("gpu_name"), parts.at(0).trimmed()},
        {QStringLiteral("gpu_utilization"), parts.at(1).trimmed().toInt()},
        {QStringLiteral("gpu_memory_used_mb"), parts.at(2).trimmed().toInt()},
        {QStringLiteral("gpu_memory_total_mb"), parts.at(3).trimmed().toInt()},
        {QStringLiteral("gpu_temperature_c"), parts.at(4).trimmed().toInt()},
    };
}

QJsonObject queryMemoryStatus() {
    qint64 totalMb = 0;
    qint64 availMb = 0;
#ifdef Q_OS_WIN
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex)) {
        totalMb = static_cast<qint64>(statex.ullTotalPhys / (1024 * 1024));
        availMb = static_cast<qint64>(statex.ullAvailPhys / (1024 * 1024));
    }
#endif
    return QJsonObject{
        {QStringLiteral("ram_total_mb"), static_cast<double>(totalMb)},
        {QStringLiteral("ram_used_mb"), static_cast<double>(qMax<qint64>(0, totalMb - availMb))},
    };
}

QString relativeProjectPath(const QString& rootPath, const QString& absolutePath) {
    const QString cleanRoot = QDir::cleanPath(rootPath);
    const QString cleanPath = QDir::cleanPath(absolutePath);
    if (cleanPath.startsWith(cleanRoot)) {
        return QDir(cleanRoot).relativeFilePath(cleanPath);
    }
    return cleanPath;
}

QString titleCaseToken(QString text) {
    text.replace('_', ' ');
    if (!text.isEmpty()) {
        text[0] = text.at(0).toUpper();
    }
    return text;
}

QString historySignature(const QString& relativePath, double modifiedAt, qint64 sizeBytes, int entryCount) {
    return QStringLiteral("%1|%2|%3|%4")
        .arg(relativePath)
        .arg(QString::number(modifiedAt, 'f', 0))
        .arg(sizeBytes)
        .arg(entryCount);
}

int meaningfulEntryCount(const QString& dirPath) {
    const QDir dir(dirPath);
    if (!dir.exists()) {
        return 0;
    }
    int count = 0;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                                    QDir::Name);
    for (const QFileInfo& info : entries) {
        if (!isPlaceholderEntry(info)) {
            ++count;
        }
    }
    return count;
}

QJsonObject makeHistoryFileRow(const QString& rootPath,
                               const QString& category,
                               const QString& job,
                               const QString& stage,
                               const QString& label,
                               const QFileInfo& info,
                               qint64 sizeBytes = -1,
                               int entryCount = -1,
                               double modifiedAt = -1.0) {
    const QString absolutePath = QDir::cleanPath(info.absoluteFilePath());
    const QString relativePath = relativeProjectPath(rootPath, absolutePath);
    const bool isDir = info.isDir();
    const double effectiveModifiedAt = modifiedAt >= 0.0
        ? modifiedAt
        : (isDir ? qMax(fileTimestamp(info), newestDirectoryEntryTimestamp(absolutePath))
                 : fileTimestamp(info));
    const qint64 effectiveSizeBytes = sizeBytes >= 0 ? sizeBytes : (info.isFile() ? info.size() : 0);
    const int effectiveEntryCount = entryCount >= 0 ? entryCount : (isDir ? meaningfulEntryCount(absolutePath) : 0);
    return QJsonObject{
        {QStringLiteral("label"), label.isEmpty() ? info.fileName() : label},
        {QStringLiteral("name"), info.fileName()},
        {QStringLiteral("category"), category},
        {QStringLiteral("category_label"), titleCaseToken(category)},
        {QStringLiteral("job"), job},
        {QStringLiteral("job_label"), prettyJobName(job)},
        {QStringLiteral("stage"), stage},
        {QStringLiteral("stage_label"), titleCaseToken(stage)},
        {QStringLiteral("kind"), isDir ? QStringLiteral("directory") : QStringLiteral("file")},
        {QStringLiteral("path"), absolutePath},
        {QStringLiteral("relative_path"), relativePath},
        {QStringLiteral("size_bytes"), static_cast<double>(qMax<qint64>(0, effectiveSizeBytes))},
        {QStringLiteral("entry_count"), effectiveEntryCount},
        {QStringLiteral("modified_at"), effectiveModifiedAt},
        {QStringLiteral("signature"), historySignature(relativePath, effectiveModifiedAt, effectiveSizeBytes, effectiveEntryCount)},
    };
}

bool keepActionHistoryCategory(const QString& category) {
    return category == QStringLiteral("event")
        || category == QStringLiteral("process")
        || category == QStringLiteral("job")
        || category == QStringLiteral("data")
        || category == QStringLiteral("config")
        || category == QStringLiteral("repair")
        || category == QStringLiteral("alert");
}

void appendContextPaths(const QString& rootPath,
                        const QString& key,
                        const QJsonValue& value,
                        QStringList* paths,
                        int* activeCount) {
    const QString normalizedKey = key.toLower();
    const bool pathLikeKey = normalizedKey.contains(QStringLiteral("path"))
        || normalizedKey.contains(QStringLiteral("dir"))
        || normalizedKey == QStringLiteral("removed")
        || normalizedKey == QStringLiteral("failed");
    if (!pathLikeKey) {
        return;
    }

    if (value.isString()) {
        const QString raw = value.toString().trimmed();
        if (raw.isEmpty()) {
            return;
        }
        QString absolute = raw;
        if (!QDir::isAbsolutePath(absolute)) {
            absolute = QDir(rootPath).absoluteFilePath(raw);
        }
        absolute = QDir::cleanPath(absolute);
        paths->append(relativeProjectPath(rootPath, absolute));
        if (QFileInfo::exists(absolute)) {
            *activeCount += 1;
        }
        return;
    }

    if (value.isArray()) {
        const QJsonArray values = value.toArray();
        for (const QJsonValue& item : values) {
            appendContextPaths(rootPath, key, item, paths, activeCount);
        }
    }
}

QJsonObject builtInDefaultConfig() {
    return QJsonObject{
        {QStringLiteral("critic"), QJsonObject{
             {QStringLiteral("d_model"), 256},
             {QStringLiteral("enabled"), true},
             {QStringLiteral("learning_rate"), 0.0001},
             {QStringLiteral("n_heads"), 4},
             {QStringLiteral("n_layers"), 4},
             {QStringLiteral("reward_weight"), 0.1},
         }},
        {QStringLiteral("curriculum"), QJsonObject{
             {QStringLiteral("current_stage"), 1},
             {QStringLiteral("enabled"), true},
             {QStringLiteral("patience_epochs"), 3},
             {QStringLiteral("progression_threshold"), 0.02},
             {QStringLiteral("stages"), QJsonObject{
                  {QStringLiteral("1"), QJsonObject{
                       {QStringLiteral("focus"), QStringLiteral("basic_completion")},
                       {QStringLiteral("mix"), QJsonObject{
                            {QStringLiteral("basic"), 0.8},
                            {QStringLiteral("reasoning"), 0.2},
                        }},
                   }},
                  {QStringLiteral("2"), QJsonObject{
                       {QStringLiteral("focus"), QStringLiteral("instruction_following")},
                       {QStringLiteral("mix"), QJsonObject{
                            {QStringLiteral("basic"), 0.4},
                            {QStringLiteral("instruction"), 0.2},
                            {QStringLiteral("reasoning"), 0.4},
                        }},
                   }},
                  {QStringLiteral("3"), QJsonObject{
                       {QStringLiteral("focus"), QStringLiteral("reasoning")},
                       {QStringLiteral("mix"), QJsonObject{
                            {QStringLiteral("basic"), 0.2},
                            {QStringLiteral("instruction"), 0.3},
                            {QStringLiteral("reasoning"), 0.5},
                        }},
                   }},
                  {QStringLiteral("4"), QJsonObject{
                       {QStringLiteral("focus"), QStringLiteral("complex_reasoning")},
                       {QStringLiteral("mix"), QJsonObject{
                            {QStringLiteral("complex"), 0.3},
                            {QStringLiteral("instruction"), 0.3},
                            {QStringLiteral("reasoning"), 0.4},
                        }},
                   }},
                  {QStringLiteral("5"), QJsonObject{
                       {QStringLiteral("focus"), QStringLiteral("mastery")},
                       {QStringLiteral("mix"), QJsonObject{
                            {QStringLiteral("complex"), 0.4},
                            {QStringLiteral("instruction"), 0.3},
                            {QStringLiteral("reasoning"), 0.3},
                        }},
                   }},
              }},
         }},
        {QStringLiteral("dashboard"), QJsonObject{
             {QStringLiteral("poll_interval_ms"), 3000},
             {QStringLiteral("port"), 8765},
         }},
        {QStringLiteral("datasets"), QJsonObject{
             {QStringLiteral("cache_dir"), QStringLiteral("data/cache")},
             {QStringLiteral("data_dir"), QStringLiteral("data/processed")},
             {QStringLiteral("max_samples"), 100000},
             {QStringLiteral("quality_threshold"), 0.7},
             {QStringLiteral("source_workers"), 4},
             {QStringLiteral("sources"), QJsonArray{}},
             {QStringLiteral("tokenizer_vocab_size"), 32000},
             {QStringLiteral("trust_remote_code"), false},
         }},
        {QStringLiteral("evaluation"), QJsonObject{
             {QStringLiteral("benchmarks"), QJsonArray{
                  QStringLiteral("gsm8k"),
                  QStringLiteral("reasoning_probes"),
                  QStringLiteral("instruction_following"),
              }},
             {QStringLiteral("cpu_threads"), 4},
             {QStringLiteral("report_dir"), QStringLiteral("artifacts")},
             {QStringLiteral("samples_per_benchmark"), 100},
         }},
        {QStringLiteral("inference"), QJsonObject{
             {QStringLiteral("host"), QStringLiteral("127.0.0.1")},
             {QStringLiteral("max_length"), 512},
             {QStringLiteral("port"), 8766},
             {QStringLiteral("temperature"), 0.7},
             {QStringLiteral("top_k"), 50},
             {QStringLiteral("top_p"), 0.9},
         }},
        {QStringLiteral("large_judge"), QJsonObject{
             {QStringLiteral("auto_download_required_models"), true},
             {QStringLiteral("cache_dir"), QStringLiteral("data/cache/large_judge")},
             {QStringLiteral("enabled"), true},
             {QStringLiteral("fallback_model_ids"), QJsonArray{
                  QStringLiteral("Qwen/Qwen2.5-3B-Instruct"),
                  QStringLiteral("TinyLlama/TinyLlama-1.1B-Chat-v1.0"),
              }},
             {QStringLiteral("judge_interval_epochs"), 5},
             {QStringLiteral("model_id"), QStringLiteral("Qwen/Qwen2.5-1.5B-Instruct")},
             {QStringLiteral("protocols"), QJsonArray{
                  QStringLiteral("rubric_scoring"),
                  QStringLiteral("flaw_taxonomy"),
                  QStringLiteral("chosen_rejected"),
              }},
             {QStringLiteral("trust_remote_code"), false},
         }},
        {QStringLiteral("model"), QJsonObject{
             {QStringLiteral("d_model"), 768},
             {QStringLiteral("dropout"), 0.05},
             {QStringLiteral("gradient_checkpointing"), true},
             {QStringLiteral("max_seq_len"), 2048},
             {QStringLiteral("n_heads"), 12},
             {QStringLiteral("n_layers"), 14},
             {QStringLiteral("vocab_size"), 32000},
         }},
        {QStringLiteral("training"), QJsonObject{
             {QStringLiteral("batch_item_workers"), 4},
             {QStringLiteral("batch_size"), 8},
             {QStringLiteral("best_checkpoint_count"), 5},
             {QStringLiteral("checkpoint_every"), 100},
             {QStringLiteral("ema_decay"), 0.999},
             {QStringLiteral("eval_every"), 500},
             {QStringLiteral("gradient_accumulation_steps"), 4},
             {QStringLiteral("gradient_checkpointing"), true},
             {QStringLiteral("label_smoothing"), 0.1},
             {QStringLiteral("learning_rate"), 0.0002},
             {QStringLiteral("log_every"), 10},
             {QStringLiteral("max_epochs"), 100},
             {QStringLiteral("max_grad_norm"), 1.0},
             {QStringLiteral("max_steps"), QJsonValue()},
             {QStringLiteral("micro_batch_size"), 2},
             {QStringLiteral("mixed_precision"), true},
             {QStringLiteral("optimizer_cpu_offload"), true},
             {QStringLiteral("prefetch_batches"), 1},
             {QStringLiteral("synthetic_samples_per_epoch"), 50},
             {QStringLiteral("val_ratio"), 0.05},
             {QStringLiteral("vram_ceiling_gb"), 5.5},
             {QStringLiteral("warmup_steps"), 1000},
             {QStringLiteral("weight_decay"), 0.1},
         }},
    };
}

QJsonObject normalizeConfigObject(const QJsonObject& config, QStringList* addedTopLevelKeys = nullptr) {
    const QJsonObject defaults = builtInDefaultConfig();
    if (addedTopLevelKeys) {
        for (auto it = defaults.begin(); it != defaults.end(); ++it) {
            if (!config.contains(it.key())) {
                addedTopLevelKeys->append(it.key());
            }
        }
    }
    return deepMergeObjects(defaults, config);
}

QJsonObject referenceConfigTextObject(const QString& text) {
    if (text.trimmed().isEmpty()) {
        return QJsonObject{};
    }
    QString parseError;
    const QJsonValue parsed = SimpleYaml::parse(text, &parseError);
    if (!parseError.isEmpty() || !parsed.isObject()) {
        return QJsonObject{};
    }
    return parsed.toObject();
}

}  // namespace

void ControlCenterBackend::loadConfigFromDisk(bool force) {
    static bool recovering = false;

    QFileInfo info(m_configPath);
    if (!force && info.exists() && m_configMtime.isValid() && info.lastModified() <= m_configMtime) {
        return;
    }

    QString text;
    if (!readTextFile(m_configPath, &text)) {
        recordIssue(QStringLiteral("frontier.config"), QStringLiteral("Could not read config file"));
        if (!recovering) {
            QString resultMessage;
            recovering = true;
            if (recoverConfigFromBackup(&resultMessage)) {
                recordLog(QStringLiteral("warning"),
                          QStringLiteral("config"),
                          QStringLiteral("recover_backup"),
                          resultMessage,
                          QJsonObject{{QStringLiteral("path"), m_configPath}});
                loadConfigFromDisk(true);
            } else if (recoverConfigFromBuiltInDefaults(&resultMessage)) {
                recordLog(QStringLiteral("warning"),
                          QStringLiteral("config"),
                          QStringLiteral("recover_defaults"),
                          resultMessage,
                          QJsonObject{{QStringLiteral("path"), m_configPath}});
                loadConfigFromDisk(true);
            }
            recovering = false;
        }
        return;
    }

    QString parseError;
    const QJsonValue parsed = SimpleYaml::parse(text, &parseError);
    if (!parseError.isEmpty() || !parsed.isObject()) {
        recordIssue(QStringLiteral("frontier.config"), QStringLiteral("Config parse failed"));
        if (!recovering) {
            QString resultMessage;
            recovering = true;
            if (recoverConfigFromBackup(&resultMessage)) {
                recordLog(QStringLiteral("warning"),
                          QStringLiteral("config"),
                          QStringLiteral("recover_backup"),
                          resultMessage,
                          QJsonObject{
                              {QStringLiteral("path"), m_configPath},
                              {QStringLiteral("parse_error"), parseError},
                          });
                loadConfigFromDisk(true);
            } else if (recoverConfigFromBuiltInDefaults(&resultMessage)) {
                recordLog(QStringLiteral("warning"),
                          QStringLiteral("config"),
                          QStringLiteral("recover_defaults"),
                          resultMessage,
                          QJsonObject{
                              {QStringLiteral("path"), m_configPath},
                              {QStringLiteral("parse_error"), parseError},
                          });
                loadConfigFromDisk(true);
            }
            recovering = false;
        }
        return;
    }

    QStringList normalizedKeys;
    m_config = normalizeConfigObject(parsed.toObject(), &normalizedKeys);
    m_configText = text;
    m_configMtime = info.lastModified();
    backupConfigSnapshot(text);
    if (!normalizedKeys.isEmpty()) {
        normalizedKeys.sort();
        recordLog(QStringLiteral("warning"),
                  QStringLiteral("config"),
                  QStringLiteral("normalize_defaults"),
                  QStringLiteral("Loaded config with built-in fallback defaults for missing sections"),
                  QJsonObject{
                      {QStringLiteral("path"), m_configPath},
                      {QStringLiteral("missing_sections"), QJsonArray::fromStringList(normalizedKeys)},
                  });
    }
}

bool ControlCenterBackend::saveConfigObject(const QJsonObject& config) {
    QJsonObject mergedConfig = config;
    restoreRedactedSecretsIntoObject(&mergedConfig, m_config);
    const QJsonObject normalizedConfig = normalizeConfigObject(mergedConfig);
    const QString yamlText = SimpleYaml::dump(normalizedConfig);
    if (!writeTextFile(m_configPath, yamlText)) {
        recordIssue(QStringLiteral("frontier.config"), QStringLiteral("Could not save config object"));
        return false;
    }
    m_config = normalizedConfig;
    m_configText = yamlText;
    m_configMtime = QFileInfo(m_configPath).lastModified();
    backupConfigSnapshot(yamlText);
    invalidateRuntimeCaches();
    recordLog(QStringLiteral("info"),
              QStringLiteral("config"),
              QStringLiteral("save_object"),
              QStringLiteral("Configuration saved"),
              QJsonObject{{QStringLiteral("path"), m_configPath}});
    return true;
}

bool ControlCenterBackend::saveConfigText(const QString& text, QString* errorMessage) {
    QString parseError;
    const QJsonValue parsed = SimpleYaml::parse(text, &parseError);
    if (!parseError.isEmpty() || !parsed.isObject()) {
        recordIssue(QStringLiteral("frontier.config"), QStringLiteral("Refused to save invalid config text"));
        if (errorMessage) {
            *errorMessage = parseError.isEmpty()
                ? QStringLiteral("Config text must parse to a YAML object")
                : parseError;
        }
        return false;
    }
    const QJsonObject parsedObject = parsed.toObject();
    QJsonObject referenceObject = referenceConfigTextObject(m_configText);
    if (referenceObject.isEmpty()) {
        referenceObject = m_config;
    }
    QStringList missingTopLevelKeys;
    for (auto it = referenceObject.begin(); it != referenceObject.end(); ++it) {
        if (!parsedObject.contains(it.key())) {
            missingTopLevelKeys.append(it.key());
        }
    }
    if (!missingTopLevelKeys.isEmpty()) {
        std::sort(missingTopLevelKeys.begin(), missingTopLevelKeys.end());
        recordIssue(QStringLiteral("frontier.config"), QStringLiteral("Refused to save partial config text"));
        if (errorMessage) {
            *errorMessage = QStringLiteral("Config text is missing required top-level section(s): %1")
                .arg(missingTopLevelKeys.join(QStringLiteral(", ")));
        }
        return false;
    }

    QJsonObject mergedObject = parsedObject;
    restoreRedactedSecretsIntoObject(&mergedObject, m_config);
    const QJsonObject normalizedObject = normalizeConfigObject(mergedObject);
    const QString normalizedText = SimpleYaml::dump(normalizedObject);
    if (!writeTextFile(m_configPath, normalizedText)) {
        recordIssue(QStringLiteral("frontier.config"), QStringLiteral("Could not save config text"));
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write config text to disk");
        }
        return false;
    }
    m_config = normalizedObject;
    m_configText = normalizedText;
    m_configMtime = QFileInfo(m_configPath).lastModified();
    backupConfigSnapshot(normalizedText);
    invalidateRuntimeCaches();
    recordLog(QStringLiteral("info"),
              QStringLiteral("config"),
              QStringLiteral("save_text"),
              QStringLiteral("Configuration text saved"),
              QJsonObject{{QStringLiteral("path"), m_configPath}});
    return true;
}

bool ControlCenterBackend::backupConfigSnapshot(const QString& text) {
    if (text.trimmed().isEmpty()) {
        return false;
    }
    ensureDir(QFileInfo(m_configBackupPath).absolutePath());
    const bool ok = writeTextFile(m_configBackupPath, text);
    if (!ok) {
        recordLog(QStringLiteral("warning"),
                  QStringLiteral("config"),
                  QStringLiteral("backup"),
                  QStringLiteral("Could not update config backup"),
                  QJsonObject{{QStringLiteral("backup_path"), m_configBackupPath}});
    }
    return ok;
}

bool ControlCenterBackend::recoverConfigFromBackup(QString* resultMessage) {
    QString backupText;
    if (!readTextFile(m_configBackupPath, &backupText)) {
        if (resultMessage) {
            *resultMessage = QStringLiteral("Config backup is unavailable.");
        }
        return false;
    }

    QString parseError;
    const QJsonValue parsed = SimpleYaml::parse(backupText, &parseError);
    if (!parseError.isEmpty() || !parsed.isObject()) {
        if (resultMessage) {
            *resultMessage = QStringLiteral("Config backup is unreadable.");
        }
        return false;
    }

    const QJsonObject normalizedObject = normalizeConfigObject(parsed.toObject());
    const QString normalizedText = SimpleYaml::dump(normalizedObject);
    if (!writeTextFile(m_configPath, normalizedText)) {
        if (resultMessage) {
            *resultMessage = QStringLiteral("Failed to restore config backup to the main config path.");
        }
        return false;
    }

    m_config = normalizedObject;
    m_configText = normalizedText;
    m_configMtime = QFileInfo(m_configPath).lastModified();
    invalidateRuntimeCaches();
    if (resultMessage) {
        *resultMessage = QStringLiteral("Recovered the main config from the last known-good backup.");
    }
    return true;
}

bool ControlCenterBackend::recoverConfigFromBuiltInDefaults(QString* resultMessage) {
    const QJsonObject defaults = builtInDefaultConfig();
    const QString yamlText = SimpleYaml::dump(defaults);
    if (!writeTextFile(m_configPath, yamlText)) {
        if (resultMessage) {
            *resultMessage = QStringLiteral("Built-in default config recovery could not write %1").arg(m_configPath);
        }
        return false;
    }

    m_config = defaults;
    m_configText = yamlText;
    m_configMtime = QFileInfo(m_configPath).lastModified();
    backupConfigSnapshot(yamlText);
    invalidateRuntimeCaches();
    if (resultMessage) {
        *resultMessage = QStringLiteral("Recovered the main config from built-in safe defaults.");
    }
    return true;
}

QJsonArray ControlCenterBackend::recentFeedRows(int maxRows) const {
    int invalidRows = 0;
    QJsonArray rows = loadJsonlTail(m_feedPath, maxRows, &invalidRows);
    const_cast<ControlCenterBackend*>(this)->noteInvalidFeedRows(invalidRows);
    return filterRowsSince(rows, earliestRelevantFeedTimestamp());
}

QJsonArray ControlCenterBackend::recentMetricsRows(int maxRows) const {
    return buildRecentRequestRows(qBound(1, maxRows, 200));
}

QJsonArray ControlCenterBackend::recentAlerts(int maxRows) const {
    QJsonArray alerts;
    const int start = qMax(0, m_alerts.size() - maxRows);
    for (int i = start; i < m_alerts.size(); ++i) {
        alerts.append(m_alerts.at(i));
    }
    return alerts;
}

QString ControlCenterBackend::latestReportText() const {
    return latestFileText(m_reportDir, QStringLiteral("eval_report_*.json"));
}

QString ControlCenterBackend::latestCheckpointPath() const {
    return latestCheckpointCandidate(m_checkpointDir);
}

double ControlCenterBackend::earliestRelevantFeedTimestamp() const {
    double minTs = qMax(static_cast<double>(m_startedAt.toSecsSinceEpoch()), m_feedClearCutoffTs);
    for (auto it = m_processes.begin(); it != m_processes.end(); ++it) {
        if (it.key() == QStringLiteral("autopilot")) {
            continue;
        }
        const ManagedProcessState& state = it.value();
        if (isManagedProcessRunning(state) && state.startedAt > 0.0) {
            minTs = qMin(minTs, state.startedAt);
        }
    }
    if (m_autopilot.value(QStringLiteral("active")).toBool(false)) {
        const double autopilotStartedAt = m_autopilot.value(QStringLiteral("started_at")).toDouble(minTs);
        if (autopilotStartedAt > 0.0) {
            minTs = qMin(minTs, autopilotStartedAt);
        }
    }
    return qMax(minTs, m_feedClearCutoffTs);
}

QJsonObject ControlCenterBackend::inferJobStateFromDisk(const QString& name) const {
    if (name == QStringLiteral("setup")) {
        const QString venvDir = rootPathFor(QStringLiteral(".venv"));
        const QString pythonExe = firstExistingPath(venvPythonCandidates());
        const QString sitePackagesDir = firstExistingPath(venvSitePackagesCandidates());
        const QString effectivePythonPath = pythonExe.isEmpty() ? venvPythonCandidates().first() : pythonExe;
        const QString effectiveSitePackagesDir = sitePackagesDir.isEmpty()
            ? venvSitePackagesCandidates().first()
            : sitePackagesDir;
        const QFileInfo venvInfo(venvDir);
        const QFileInfo pythonInfo(effectivePythonPath);
        const QFileInfo sitePackagesInfo(effectiveSitePackagesDir);

        double updatedAt = qMax(fileTimestamp(venvInfo), fileTimestamp(pythonInfo));
        updatedAt = qMax(updatedAt, fileTimestamp(sitePackagesInfo));

        if (!venvInfo.exists()) {
            return makeDerivedJobSummary(name, QStringLiteral("idle"), QString(), 0.0, 0.0);
        }
        if (!pythonInfo.exists()) {
            return makeDerivedJobSummary(name,
                                         QStringLiteral("create_venv"),
                                         QStringLiteral("Virtual environment directory exists, but Python is missing"),
                                         0.08,
                                         updatedAt);
        }

        const QList<QStringList> dependencyGroups = {
            {QStringLiteral("transformers*")},
            {QStringLiteral("datasets*")},
            {QStringLiteral("fastapi*")},
            {QStringLiteral("uvicorn*")},
            {QStringLiteral("sentencepiece*")},
            {QStringLiteral("numpy*")},
            {QStringLiteral("tensorboard*")},
            {QStringLiteral("psutil*")},
            {QStringLiteral("yaml"), QStringLiteral("PyYAML*")},
        };
        const QStringList torchMarkers = {
            QStringLiteral("torch"),
            QStringLiteral("torch-*.dist-info"),
            QStringLiteral("torch*.dist-info"),
            QStringLiteral("torchgen*"),
            QStringLiteral("functorch*"),
        };
        int dependencyGroupsPresent = 0;
        double dependencyUpdatedAt = 0.0;
        for (const QStringList& group : dependencyGroups) {
            if (hasTopLevelMatch(effectiveSitePackagesDir, group)) {
                ++dependencyGroupsPresent;
                dependencyUpdatedAt = qMax(dependencyUpdatedAt, newestMatchTimestamp(effectiveSitePackagesDir, group));
            }
        }
        const bool hasDependencies = dependencyGroupsPresent >= dependencyGroups.size();
        const bool hasTorch = hasTopLevelMatch(effectiveSitePackagesDir, torchMarkers);
        updatedAt = qMax(updatedAt, dependencyUpdatedAt);
        updatedAt = qMax(updatedAt, newestMatchTimestamp(effectiveSitePackagesDir, torchMarkers));

        if (!hasDependencies) {
            return makeDerivedJobSummary(name,
                                         QStringLiteral("install_dependencies"),
                                         QStringLiteral("Virtual environment exists, but Python dependencies are incomplete"),
                                         0.55,
                                         updatedAt);
        }
        if (!hasTorch) {
            return makeDerivedJobSummary(name,
                                         QStringLiteral("install_torch"),
                                         QStringLiteral("Dependencies are installed, but CUDA-enabled PyTorch is missing"),
                                         0.72,
                                         updatedAt);
        }

        return makeDerivedJobSummary(name,
                                     QStringLiteral("completed"),
                                     QStringLiteral("Environment is ready"),
                                     1.0,
                                     updatedAt);
    }

    if (name == QStringLiteral("prepare")) {
        const QString dataDir = QDir(m_rootPath).absoluteFilePath(
            m_config.value(QStringLiteral("datasets")).toObject().value(QStringLiteral("data_dir")).toString(QStringLiteral("data/processed")));
        const QString sourceCacheDir = QDir(m_rootPath).absoluteFilePath(
            m_config.value(QStringLiteral("datasets")).toObject().value(QStringLiteral("cache_dir")).toString(QStringLiteral("data/cache")));
        const QString judgeCacheDir = QDir(m_rootPath).absoluteFilePath(
            m_config.value(QStringLiteral("large_judge")).toObject().value(QStringLiteral("cache_dir")).toString(QStringLiteral("data/cache/large_judge")));
        const QSet<QString> sourceCacheExclusions{QDir::cleanPath(judgeCacheDir)};
        const QFileInfo tokenizerInfo(QDir(dataDir).absoluteFilePath(QStringLiteral("tokenizer.model")));
        const QFileInfo corpusInfo(QDir(dataDir).absoluteFilePath(QStringLiteral("tokenizer_corpus.txt")));
        const QFileInfo datasetInfo(QDir(dataDir).absoluteFilePath(QStringLiteral("train_scored.jsonl")));

        const double outputUpdatedAt = newestDirectoryEntryTimestamp(dataDir);
        double updatedAt = outputUpdatedAt;
        const double sourceCacheUpdatedAt = newestDirectoryEntryTimestampExcluding(sourceCacheDir, sourceCacheExclusions);
        const double judgeCacheUpdatedAt = newestDirectoryEntryTimestamp(judgeCacheDir);
        updatedAt = qMax(updatedAt, fileTimestamp(tokenizerInfo));
        updatedAt = qMax(updatedAt, fileTimestamp(corpusInfo));
        updatedAt = qMax(updatedAt, fileTimestamp(datasetInfo));
        updatedAt = qMax(updatedAt, sourceCacheUpdatedAt);
        updatedAt = qMax(updatedAt, judgeCacheUpdatedAt);

        if (tokenizerInfo.exists() && datasetInfo.exists()) {
            return makeDerivedJobSummary(name,
                                         QStringLiteral("completed"),
                                         QStringLiteral("Prepared dataset and tokenizer are available"),
                                         1.0,
                                         updatedAt);
        }
        if (tokenizerInfo.exists()) {
            return makeDerivedJobSummary(name,
                                         QStringLiteral("writing"),
                                         QStringLiteral("Tokenizer exists, but the scored dataset file is missing"),
                                         0.90,
                                         updatedAt);
        }
        if (corpusInfo.exists() || outputUpdatedAt > 0.0) {
            return makeDerivedJobSummary(name,
                                         QStringLiteral("tokenizer"),
                                         QStringLiteral("Preparation artifacts exist, but the final dataset is incomplete"),
                                         0.70,
                                         updatedAt);
        }
        if (hasMeaningfulDirectoryEntriesExcluding(sourceCacheDir, sourceCacheExclusions)) {
            return makeDerivedJobSummary(name,
                                         QStringLiteral("dataset_source"),
                                         QStringLiteral("Dataset source cache exists, but processed outputs are not complete"),
                                         0.25,
                                         updatedAt);
        }
        if (hasMeaningfulDirectoryEntries(judgeCacheDir)) {
            return makeDerivedJobSummary(name,
                                         QStringLiteral("model_ready"),
                                         QStringLiteral("Required models are cached, but data preparation has not finished"),
                                         0.14,
                                         updatedAt);
        }
        return makeDerivedJobSummary(name, QStringLiteral("idle"), QString(), 0.0, 0.0);
    }

    if (name == QStringLiteral("training")) {
        const QString checkpointPath = latestCheckpointPath();
        const QString tensorboardDir = QDir(rootPathFor(QStringLiteral("logs"))).absoluteFilePath(QStringLiteral("tensorboard"));
        const double tensorboardUpdatedAt = newestDirectoryEntryTimestamp(tensorboardDir);
        const int maxEpochs = qMax(0, m_config.value(QStringLiteral("training")).toObject().value(QStringLiteral("max_epochs")).toInt(0));

        if (!checkpointPath.isEmpty()) {
            const QFileInfo checkpointInfo(checkpointPath);
            const QString checkpointName = checkpointInfo.fileName();
            double progress = 0.50;
            QString stage = QStringLiteral("stopped");
            QString message = QStringLiteral("Latest checkpoint available: %1").arg(checkpointName);

            static const QRegularExpression finalPattern(QStringLiteral("^checkpoint_e(\\d+)_final$"));
            static const QRegularExpression stepPattern(QStringLiteral("^checkpoint_e(\\d+)_s(\\d+)$"));
            const QRegularExpressionMatch finalMatch = finalPattern.match(checkpointName);
            const QRegularExpressionMatch stepMatch = stepPattern.match(checkpointName);
            if (finalMatch.hasMatch()) {
                const int epoch = finalMatch.captured(1).toInt();
                const bool fullRunCompleted = maxEpochs > 0 && (epoch + 1) >= maxEpochs;
                stage = fullRunCompleted ? QStringLiteral("completed") : QStringLiteral("stopped");
                progress = maxEpochs > 0 ? qBound(0.10, static_cast<double>(epoch + 1) / maxEpochs, 1.0) : 0.50;
                message = fullRunCompleted
                    ? QStringLiteral("Training completed and final checkpoint is available: %1").arg(checkpointName)
                    : QStringLiteral("Epoch-final checkpoint available: %1").arg(checkpointName);
            } else if (stepMatch.hasMatch()) {
                const int epoch = stepMatch.captured(1).toInt();
                progress = maxEpochs > 0
                    ? qBound(0.10, static_cast<double>(epoch + 1) / maxEpochs, 0.99)
                    : 0.50;
            }

            return makeDerivedJobSummary(name, stage, message, progress, fileTimestamp(checkpointInfo));
        }

        if (tensorboardUpdatedAt > 0.0) {
            return makeDerivedJobSummary(name,
                                         QStringLiteral("stopped"),
                                         QStringLiteral("Training logs exist, but no checkpoint has been saved yet"),
                                         0.10,
                                         tensorboardUpdatedAt);
        }

        return makeDerivedJobSummary(name, QStringLiteral("idle"), QString(), 0.0, 0.0);
    }

    if (name == QStringLiteral("evaluate")) {
        const QDir reportDir(m_reportDir);
        if (!reportDir.exists()) {
            return makeDerivedJobSummary(name, QStringLiteral("idle"), QString(), 0.0, 0.0);
        }

        const QFileInfoList reports = reportDir.entryInfoList(QStringList{QStringLiteral("eval_report_*.json")},
                                                              QDir::Files | QDir::NoDotAndDotDot,
                                                              QDir::Time);
        if (reports.isEmpty()) {
            return makeDerivedJobSummary(name, QStringLiteral("idle"), QString(), 0.0, 0.0);
        }

        const QFileInfo reportInfo = reports.first();
        return makeDerivedJobSummary(name,
                                     QStringLiteral("completed"),
                                     QStringLiteral("Latest evaluation report: %1").arg(reportInfo.fileName()),
                                     1.0,
                                     fileTimestamp(reportInfo));
    }

    if (name == QStringLiteral("inference") || name == QStringLiteral("autopilot")) {
        return makeDerivedJobSummary(name, QStringLiteral("idle"), QString(), 0.0, 0.0);
    }

    return makeDerivedJobSummary(name, QStringLiteral("idle"), QString(), 0.0, 0.0);
}

QJsonObject ControlCenterBackend::buildCheckpointSummary() const {
    const QString path = latestCheckpointPath();
    QJsonArray entries;
    QDir checkpointDir(m_checkpointDir);
    if (checkpointDir.exists()) {
        const QFileInfoList checkpointEntries = checkpointDir.entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
            QDir::Time);
        int added = 0;
        for (const QFileInfo& info : checkpointEntries) {
            QString checkpointPath;
            if (info.isDir()) {
                const QString checkpointFile = QDir(info.absoluteFilePath()).absoluteFilePath(QStringLiteral("checkpoint.pt"));
                if (!QFileInfo::exists(checkpointFile)) {
                    continue;
                }
                checkpointPath = info.absoluteFilePath();
            } else {
                const QString suffix = info.suffix().toLower();
                if (suffix != QStringLiteral("pt")
                    && suffix != QStringLiteral("pth")
                    && suffix != QStringLiteral("bin")) {
                    continue;
                }
                checkpointPath = info.absoluteFilePath();
            }

            entries.append(QJsonObject{
                {QStringLiteral("name"), info.fileName()},
                {QStringLiteral("path"), displayPathForClient(m_rootPath, checkpointPath)},
                {QStringLiteral("modified_at"), fileTimestamp(info)},
                {QStringLiteral("is_latest"), QDir::cleanPath(checkpointPath) == QDir::cleanPath(path)},
            });
            if (++added >= 16) {
                break;
            }
        }
    }

    return QJsonObject{
        {QStringLiteral("available"), !path.isEmpty()},
        {QStringLiteral("path"), displayPathForClient(m_rootPath, path)},
        {QStringLiteral("name"), path.isEmpty() ? QString() : QFileInfo(path).fileName()},
        {QStringLiteral("entries"), entries},
    };
}

QJsonArray ControlCenterBackend::buildActionHistory(int maxRows) const {
    const int limit = qBound(5, maxRows, 120);
    const QJsonArray sourceRows = recentBackendLogRows(qMin(limit * 4, 320));
    QList<QJsonObject> rows;
    rows.reserve(sourceRows.size());
    for (const QJsonValue& value : sourceRows) {
        const QJsonObject row = value.toObject();
        const QString category = row.value(QStringLiteral("category")).toString();
        if (!keepActionHistoryCategory(category)) {
            continue;
        }

        const QJsonObject context = row.value(QStringLiteral("context")).toObject();
        QStringList relatedPaths;
        int activePathCount = 0;
        for (auto it = context.begin(); it != context.end(); ++it) {
            appendContextPaths(m_rootPath, it.key(), it.value(), &relatedPaths, &activePathCount);
        }
        relatedPaths.removeDuplicates();
        activePathCount = 0;
        for (const QString& path : relatedPaths) {
            const QString absolutePath = QDir::isAbsolutePath(path)
                ? path
                : QDir(m_rootPath).absoluteFilePath(path);
            if (QFileInfo::exists(absolutePath)) {
                ++activePathCount;
            }
        }

        QJsonObject entry = row;
        entry.insert(QStringLiteral("signature"),
                     actionHistorySignature(row.value(QStringLiteral("ts")).toDouble(),
                                            row.value(QStringLiteral("severity")).toString(),
                                            row.value(QStringLiteral("category")).toString(),
                                            row.value(QStringLiteral("action")).toString(),
                                            row.value(QStringLiteral("message")).toString()));
        entry.insert(QStringLiteral("category_label"), titleCaseToken(category));
        entry.insert(QStringLiteral("action_label"), titleCaseToken(row.value(QStringLiteral("action")).toString()));
        entry.insert(QStringLiteral("paths"), QJsonArray::fromStringList(relatedPaths));
        entry.insert(QStringLiteral("active_path_count"), activePathCount);
        entry.insert(QStringLiteral("path_count"), relatedPaths.size());
        entry.insert(QStringLiteral("active"), relatedPaths.isEmpty() || activePathCount > 0);
        rows.append(entry);
    }

    std::sort(rows.begin(), rows.end(), [](const QJsonObject& left, const QJsonObject& right) {
        return left.value(QStringLiteral("ts")).toDouble() > right.value(QStringLiteral("ts")).toDouble();
    });
    if (rows.size() > limit) {
        rows = rows.mid(0, limit);
    }

    QJsonArray result;
    for (const QJsonObject& row : std::as_const(rows)) {
        result.append(row);
    }
    return result;
}

QJsonArray ControlCenterBackend::buildFileHistory(int maxRows) {
    const int limit = qBound(6, maxRows, 80);
    QList<QJsonObject> rows;

    auto appendIfExists = [this, &rows](const QString& category,
                                        const QString& job,
                                        const QString& stage,
                                        const QString& label,
                                        const QString& path,
                                        qint64 sizeBytes = -1,
                                        int entryCount = -1,
                                        double modifiedAt = -1.0) {
        const QFileInfo info(path);
        if (!info.exists()) {
            return;
        }
        rows.append(makeHistoryFileRow(m_rootPath, category, job, stage, label, info, sizeBytes, entryCount, modifiedAt));
    };

    const QString venvDir = rootPathFor(QStringLiteral(".venv"));
    appendIfExists(QStringLiteral("dependencies"),
                   QStringLiteral("setup"),
                   QStringLiteral("completed"),
                   QStringLiteral("Python environment"),
                   venvDir,
                   -1,
                   meaningfulEntryCount(venvDir),
                   newestDirectoryEntryTimestamp(venvDir));
    appendIfExists(QStringLiteral("dependencies"),
                   QStringLiteral("setup"),
                   QStringLiteral("completed"),
                   QStringLiteral("Virtualenv Python"),
                   firstExistingPath(venvPythonCandidates()));

    const QString discoveredSitePackagesDir = firstExistingPath(venvSitePackagesCandidates());
    const QString sitePackagesDir = discoveredSitePackagesDir.isEmpty()
        ? venvSitePackagesCandidates().first()
        : discoveredSitePackagesDir;
    const QFileInfoList dependencyMarkers = topLevelMatches(sitePackagesDir, QStringList{
        QStringLiteral("torch"),
        QStringLiteral("transformers*"),
        QStringLiteral("datasets*"),
        QStringLiteral("tensorboard*"),
    });
    for (const QFileInfo& info : dependencyMarkers) {
        rows.append(makeHistoryFileRow(m_rootPath,
                                       QStringLiteral("dependencies"),
                                       QStringLiteral("setup"),
                                       QStringLiteral("install_dependencies"),
                                       QStringLiteral("Installed package marker"),
                                       info));
    }

    const QString dataDir = QDir(m_rootPath).absoluteFilePath(
        m_config.value(QStringLiteral("datasets")).toObject().value(QStringLiteral("data_dir")).toString(QStringLiteral("data/processed")));
    appendIfExists(QStringLiteral("processed_data"),
                   QStringLiteral("prepare"),
                   QStringLiteral("tokenizer"),
                   QStringLiteral("Tokenizer model"),
                   QDir(dataDir).absoluteFilePath(QStringLiteral("tokenizer.model")));
    appendIfExists(QStringLiteral("processed_data"),
                   QStringLiteral("prepare"),
                   QStringLiteral("dataset_source"),
                   QStringLiteral("Tokenizer corpus"),
                   QDir(dataDir).absoluteFilePath(QStringLiteral("tokenizer_corpus.txt")));
    appendIfExists(QStringLiteral("processed_data"),
                   QStringLiteral("prepare"),
                   QStringLiteral("writing"),
                   QStringLiteral("Prepared scored dataset"),
                   QDir(dataDir).absoluteFilePath(QStringLiteral("train_scored.jsonl")));

    const QJsonObject modelCache = buildModelCacheSummary().value(QStringLiteral("large_judge")).toObject();
    for (auto it = modelCache.begin(); it != modelCache.end(); ++it) {
        const QJsonObject row = it.value().toObject();
        const QString path = row.value(QStringLiteral("path")).toString();
        const double sizeMb = row.value(QStringLiteral("size_mb")).toDouble(0.0);
        appendIfExists(QStringLiteral("model_cache"),
                       QStringLiteral("prepare"),
                       QStringLiteral("model_download"),
                       it.key(),
                       path,
                       static_cast<qint64>(sizeMb * 1024.0 * 1024.0),
                       meaningfulEntryCount(path),
                       newestDirectoryEntryTimestamp(path));
    }

    QDir checkpointDir(m_checkpointDir);
    if (checkpointDir.exists()) {
        const QFileInfoList checkpoints = checkpointDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
        int added = 0;
        for (const QFileInfo& info : checkpoints) {
            if (isPlaceholderEntry(info)) {
                continue;
            }
            const double modifiedAt = info.isDir()
                ? qMax(fileTimestamp(info), newestDirectoryEntryTimestamp(info.absoluteFilePath()))
                : fileTimestamp(info);
            rows.append(makeHistoryFileRow(m_rootPath,
                                           QStringLiteral("checkpoints"),
                                           QStringLiteral("training"),
                                           QStringLiteral("checkpoint"),
                                           QStringLiteral("Training checkpoint"),
                                           info,
                                           -1,
                                           info.isDir() ? meaningfulEntryCount(info.absoluteFilePath()) : 0,
                                           modifiedAt));
            if (++added >= 8) {
                break;
            }
        }
    }

    const QString tensorboardDir = rootPathFor(QStringLiteral("logs/tensorboard"));
    QDir tbDir(tensorboardDir);
    if (tbDir.exists()) {
        const QFileInfoList tbEntries = tbDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
        int added = 0;
        for (const QFileInfo& info : tbEntries) {
            if (isPlaceholderEntry(info)) {
                continue;
            }
            rows.append(makeHistoryFileRow(m_rootPath,
                                           QStringLiteral("training_logs"),
                                           QStringLiteral("training"),
                                           QStringLiteral("metrics"),
                                           QStringLiteral("TensorBoard run"),
                                           info,
                                           -1,
                                           info.isDir() ? meaningfulEntryCount(info.absoluteFilePath()) : 0,
                                           info.isDir() ? qMax(fileTimestamp(info), newestDirectoryEntryTimestamp(info.absoluteFilePath()))
                                                        : fileTimestamp(info)));
            if (++added >= 4) {
                break;
            }
        }
    }

    QDir reportDir(m_reportDir);
    if (reportDir.exists()) {
        const QFileInfoList reports = reportDir.entryInfoList(QStringList{QStringLiteral("eval_report_*.json")},
                                                              QDir::Files | QDir::NoDotAndDotDot,
                                                              QDir::Time);
        int added = 0;
        for (const QFileInfo& info : reports) {
            rows.append(makeHistoryFileRow(m_rootPath,
                                           QStringLiteral("reports"),
                                           QStringLiteral("evaluate"),
                                           QStringLiteral("completed"),
                                           QStringLiteral("Evaluation report"),
                                           info));
            if (++added >= 8) {
                break;
            }
        }
    }

    appendIfExists(QStringLiteral("logs"),
                   QStringLiteral("server"),
                   QStringLiteral("metrics"),
                   QStringLiteral("Workflow metrics log"),
                   m_feedPath);
    appendIfExists(QStringLiteral("logs"),
                   QStringLiteral("server"),
                   QStringLiteral("audit"),
                   QStringLiteral("Backend event log"),
                   m_backendLogPath);

    std::sort(rows.begin(), rows.end(), [](const QJsonObject& left, const QJsonObject& right) {
        return left.value(QStringLiteral("modified_at")).toDouble() > right.value(QStringLiteral("modified_at")).toDouble();
    });
    if (rows.size() > limit) {
        rows = rows.mid(0, limit);
    }

    QJsonArray result;
    for (const QJsonObject& row : std::as_const(rows)) {
        result.append(row);
    }
    return result;
}

QJsonObject ControlCenterBackend::buildHistoryPayload(int actionLimit, int fileLimit) {
    const QJsonArray actions = buildActionHistory(actionLimit);
    const QJsonArray files = buildFileHistory(fileLimit);
    double lastActionTs = 0.0;
    for (const QJsonValue& value : actions) {
        lastActionTs = qMax(lastActionTs, value.toObject().value(QStringLiteral("ts")).toDouble());
    }
    double lastFileTs = 0.0;
    for (const QJsonValue& value : files) {
        lastFileTs = qMax(lastFileTs, value.toObject().value(QStringLiteral("modified_at")).toDouble());
    }
    return QJsonObject{
        {QStringLiteral("actions"), actions},
        {QStringLiteral("files"), files},
        {QStringLiteral("summary"), QJsonObject{
             {QStringLiteral("action_count"), actions.size()},
             {QStringLiteral("file_count"), files.size()},
             {QStringLiteral("last_action_ts"), lastActionTs},
             {QStringLiteral("last_file_ts"), lastFileTs},
         }},
    };
}

QJsonObject ControlCenterBackend::buildModelCatalog() const {
    return QJsonObject{
        {QStringLiteral("large_judge"), QJsonArray{
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("Qwen/Qwen2.5-1.5B-Instruct")},
                 {QStringLiteral("label"), QStringLiteral("Qwen 2.5 1.5B Instruct")},
                 {QStringLiteral("description"), QStringLiteral("Recommended laptop-safe default")},
                 {QStringLiteral("vram_estimate_gb"), 3.0},
                 {QStringLiteral("recommended"), true},
             },
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("Qwen/Qwen2.5-3B-Instruct")},
                 {QStringLiteral("label"), QStringLiteral("Qwen 2.5 3B Instruct")},
                 {QStringLiteral("description"), QStringLiteral("Stronger but heavier")},
                 {QStringLiteral("vram_estimate_gb"), 5.5},
                 {QStringLiteral("recommended"), false},
             },
             QJsonObject{
                 {QStringLiteral("id"), QStringLiteral("TinyLlama/TinyLlama-1.1B-Chat-v1.0")},
                 {QStringLiteral("label"), QStringLiteral("TinyLlama 1.1B Chat")},
                 {QStringLiteral("description"), QStringLiteral("Smallest fallback option")},
                 {QStringLiteral("vram_estimate_gb"), 2.0},
                 {QStringLiteral("recommended"), false},
             },
         }},
    };
}

QJsonObject ControlCenterBackend::buildModelCacheSummary() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_modelCacheUntil.isValid() && now < m_modelCacheUntil) {
        return m_modelCache;
    }

    const QString cacheDir = QDir(m_rootPath).absoluteFilePath(
        m_config.value(QStringLiteral("large_judge")).toObject().value(QStringLiteral("cache_dir")).toString(QStringLiteral("data/cache/large_judge")));
    QJsonObject scanned = scanModelCache(cacheDir);

    QMutableSetIterator<QString> pendingIt(m_pendingDeletePaths);
    while (pendingIt.hasNext()) {
        const QString pendingPath = pendingIt.next();
        if (!QFileInfo::exists(pendingPath)) {
            pendingIt.remove();
            continue;
        }
        if (retryRemoveDir(pendingPath)) {
            pendingIt.remove();
            recordLog(QStringLiteral("info"),
                      QStringLiteral("data"),
                      QStringLiteral("pending_delete_retry"),
                      QStringLiteral("Removed previously locked cached model on retry"),
                      QJsonObject{{QStringLiteral("path"), pendingPath}});
        }
    }

    QJsonObject filtered;
    for (auto it = scanned.begin(); it != scanned.end(); ++it) {
        QJsonObject row = it.value().toObject();
        const QString path = QDir::cleanPath(row.value(QStringLiteral("path")).toString());
        if (m_pendingDeletePaths.contains(path)) {
            continue;
        }
        row.insert(QStringLiteral("path"), displayPathForClient(m_rootPath, path));
        filtered.insert(it.key(), row);
    }

    m_modelCache = QJsonObject{{QStringLiteral("large_judge"), filtered}};
    m_modelCacheUntil = now.addSecs(5);
    return m_modelCache;
}

QJsonObject ControlCenterBackend::buildHardwareSnapshot() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_hardwareCacheUntil.isValid() && now < m_hardwareCacheUntil) {
        return m_hardwareCache;
    }

    QJsonObject smi = queryNvidiaSmi();
    QJsonObject mem = queryMemoryStatus();
    if (smi.isEmpty()) {
        smi = QJsonObject{
            {QStringLiteral("gpu_name"), QStringLiteral("Unknown")},
            {QStringLiteral("gpu_utilization"), 0},
            {QStringLiteral("gpu_memory_used_mb"), 0},
            {QStringLiteral("gpu_memory_total_mb"), 0},
            {QStringLiteral("gpu_temperature_c"), 0},
        };
    }

    smi.insert(QStringLiteral("gpu_active"),
               smi.value(QStringLiteral("gpu_utilization")).toInt() > 5 ||
               smi.value(QStringLiteral("gpu_memory_used_mb")).toInt() > 128);
    smi.insert(QStringLiteral("ram_used_mb"), mem.value(QStringLiteral("ram_used_mb")));
    smi.insert(QStringLiteral("ram_total_mb"), mem.value(QStringLiteral("ram_total_mb")));
    m_hardwareCache = smi;
    m_hardwareCacheUntil = now.addMSecs(900);
    return m_hardwareCache;
}

QJsonObject ControlCenterBackend::buildProcessSnapshot(const QString& name) const {
    const ManagedProcessState state = m_processes.value(name);
    const bool running = name == QStringLiteral("autopilot")
        ? m_autopilot.value(QStringLiteral("active")).toBool(false)
        : isManagedProcessRunning(state);
    const QJsonObject job = const_cast<ControlCenterBackend*>(this)->summarizeJob(name);
    const double startedAt = name == QStringLiteral("autopilot")
        ? m_autopilot.value(QStringLiteral("started_at")).toDouble(state.startedAt)
        : state.startedAt;
    const bool paused = name == QStringLiteral("autopilot")
        ? m_autopilot.value(QStringLiteral("paused")).toBool(false)
        : state.paused;
    const bool recovered = name != QStringLiteral("autopilot")
        && state.process.isNull()
        && state.recoveredPid > 0
        && isManagedProcessRunning(state);
    const QString processStatus = paused
        ? QStringLiteral("paused")
        : running
            ? QStringLiteral("running")
            : job.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    const double updatedAt = job.value(QStringLiteral("updated_at")).toDouble(startedAt);
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("running"), running},
        {QStringLiteral("paused"), paused},
        {QStringLiteral("status"), processStatus},
        {QStringLiteral("log"), QJsonArray{}},
        {QStringLiteral("last_log_line"), latestMeaningfulProcessLogLine(state)},
        {QStringLiteral("pid"), static_cast<double>(running && name != QStringLiteral("autopilot") ? managedProcessId(state) : 0)},
        {QStringLiteral("recovered"), recovered},
        {QStringLiteral("started_at"), startedAt},
        {QStringLiteral("returncode"), running ? QJsonValue() : QJsonValue(state.lastExitCode)},
        {QStringLiteral("last_exit_code"), running ? QJsonValue() : QJsonValue(state.lastExitCode)},
        {QStringLiteral("updated_at"), updatedAt},
    };
}

QJsonObject ControlCenterBackend::summarizeJob(const QString& name) {
    const ManagedProcessState state = m_processes.value(name);
    const bool running = name == QStringLiteral("autopilot")
        ? m_autopilot.value(QStringLiteral("active")).toBool(false)
        : isManagedProcessRunning(state);
    QJsonArray allRows = recentFeedRows(1000);
    if (name == QStringLiteral("autopilot") && running) {
        const double autopilotStartedAt = m_autopilot.value(QStringLiteral("started_at")).toDouble();
        if (autopilotStartedAt > 0.0) {
            allRows = filterRowsSince(allRows, autopilotStartedAt);
        }
    } else if (running && state.startedAt > 0.0) {
        allRows = filterRowsSince(allRows, state.startedAt);
    }
    QJsonArray jobRows;
    for (const QJsonValue& value : allRows) {
        const QJsonObject row = value.toObject();
        if (row.value(QStringLiteral("job")).toString() == name) {
            jobRows.append(row);
        }
    }
    const QJsonObject summary = summarizeJobRows(name, jobRows);
    if (running) {
        if (name == QStringLiteral("autopilot")) {
            const QString summaryStage = normalizeAutopilotSummaryStage(
                summary.value(QStringLiteral("stage")).toString(m_autopilot.value(QStringLiteral("stage")).toString()));
            const QString activeJob = autopilotStageJobName(summaryStage);
            const QJsonObject activeSummary = activeJob.isEmpty() ? QJsonObject{} : summarizeJob(activeJob);
            if (!activeSummary.isEmpty()) {
                QJsonObject autopilotSummary = summary;
                if (autopilotSummary.isEmpty() || autopilotSummary.value(QStringLiteral("stage")).toString().isEmpty()) {
                    autopilotSummary = makeDerivedJobSummary(
                        name,
                        summaryStage.isEmpty() ? QStringLiteral("idle") : summaryStage,
                        m_autopilot.value(QStringLiteral("message")).toString(QStringLiteral("Waiting for activity")),
                        0.0,
                        0.0);
                }
                autopilotSummary.insert(
                    QStringLiteral("stage"),
                    summaryStage.isEmpty() ? QStringLiteral("idle") : summaryStage);
                autopilotSummary.insert(
                    QStringLiteral("message"),
                    autopilotSummary.value(QStringLiteral("message")).toString().isEmpty()
                        ? m_autopilot.value(QStringLiteral("message")).toString(QStringLiteral("Waiting for activity"))
                        : autopilotSummary.value(QStringLiteral("message")).toString());
                autopilotSummary.insert(
                    QStringLiteral("progress"),
                    activeSummary.value(QStringLiteral("progress")).toDouble(
                        autopilotSummary.value(QStringLiteral("progress")).toDouble(
                            autopilotSummaryBaselineProgress(summaryStage))));
                autopilotSummary.insert(
                    QStringLiteral("updated_at"),
                    qMax(autopilotSummary.value(QStringLiteral("updated_at")).toDouble(0.0),
                         qMax(activeSummary.value(QStringLiteral("updated_at")).toDouble(0.0),
                              qMax(m_autopilot.value(QStringLiteral("ts")).toDouble(0.0),
                                   m_autopilot.value(QStringLiteral("started_at")).toDouble(0.0)))));
                autopilotSummary.insert(QStringLiteral("eta"), activeSummary.value(QStringLiteral("eta")));
                autopilotSummary.insert(QStringLiteral("eta_seconds"), activeSummary.value(QStringLiteral("eta_seconds")));
                return autopilotSummary;
            }
        }

        const QString liveStage = summary.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
        const double liveUpdatedAt = summary.value(QStringLiteral("updated_at")).toDouble();
        if (liveStage != QStringLiteral("idle") || liveUpdatedAt > 0.0) {
            return summary;
        }

        if (name == QStringLiteral("training")) {
            const QJsonObject recovery = readJsonObjectFile(
                QDir(m_runtimeStateDir).absoluteFilePath(QStringLiteral("training.recovery.json")));
            if (recovery.value(QStringLiteral("active")).toBool(false)) {
                return makeDerivedJobSummary(
                    name,
                    recovery.value(QStringLiteral("paused")).toBool(false)
                        ? QStringLiteral("paused")
                        : QStringLiteral("training"),
                    recovery.value(QStringLiteral("message")).toString(QStringLiteral("Training is active")),
                    recovery.value(QStringLiteral("progress")).toDouble(0.0),
                    qMax(recovery.value(QStringLiteral("ts")).toDouble(0.0), state.startedAt));
            }
        }

        if (name == QStringLiteral("autopilot")) {
            const QString stage = normalizeAutopilotSummaryStage(
                m_autopilot.value(QStringLiteral("stage")).toString(QStringLiteral("idle")));
            const QString activeJob = autopilotStageJobName(stage);
            const QJsonObject activeSummary = activeJob.isEmpty() ? QJsonObject{} : summarizeJob(activeJob);
            QJsonObject autopilotSummary = makeDerivedJobSummary(
                name,
                stage.isEmpty() ? QStringLiteral("idle") : stage,
                m_autopilot.value(QStringLiteral("message")).toString(QStringLiteral("Waiting for activity")),
                qMax(autopilotSummaryBaselineProgress(stage),
                     activeSummary.value(QStringLiteral("progress")).toDouble(0.0)),
                qMax(m_autopilot.value(QStringLiteral("ts")).toDouble(0.0),
                     qMax(m_autopilot.value(QStringLiteral("started_at")).toDouble(0.0),
                          activeSummary.value(QStringLiteral("updated_at")).toDouble(0.0))));
            if (!activeSummary.isEmpty()) {
                autopilotSummary.insert(QStringLiteral("eta"), activeSummary.value(QStringLiteral("eta")));
                autopilotSummary.insert(QStringLiteral("eta_seconds"), activeSummary.value(QStringLiteral("eta_seconds")));
            }
            return autopilotSummary;
        }

        const QString liveMessage = latestMeaningfulProcessLogLine(state);
        return makeDerivedJobSummary(
            name,
            QStringLiteral("starting"),
            liveMessage.isEmpty() ? QStringLiteral("%1 is running").arg(state.label) : liveMessage,
            inferJobStateFromDisk(name).value(QStringLiteral("progress")).toDouble(0.0),
            qMax(state.startedAt, static_cast<double>(QDateTime::currentSecsSinceEpoch())));
    }

    const QJsonObject diskSummary = inferJobStateFromDisk(name);
    if (name != QStringLiteral("autopilot") && state.paused) {
        QJsonObject pausedSummary = diskSummary;
        const QFileInfo runtimeInfo(runtimeStatePath(name));
        const double updatedAt = qMax(pausedSummary.value(QStringLiteral("updated_at")).toDouble(),
                                      fileTimestamp(runtimeInfo));
        const QString baseMessage = pausedSummary.value(QStringLiteral("message")).toString();
        pausedSummary.insert(QStringLiteral("stage"), QStringLiteral("paused"));
        pausedSummary.insert(QStringLiteral("message"),
                             baseMessage.isEmpty()
                                 ? QStringLiteral("%1 paused and ready to resume").arg(state.label)
                                 : QStringLiteral("%1 paused. %2").arg(state.label, baseMessage));
        if (name == QStringLiteral("training")) {
            pausedSummary.insert(QStringLiteral("progress"),
                                 qMax(0.10, pausedSummary.value(QStringLiteral("progress")).toDouble()));
        }
        pausedSummary.insert(QStringLiteral("updated_at"), updatedAt);
        pausedSummary.insert(QStringLiteral("eta"), QStringLiteral("paused"));
        pausedSummary.insert(QStringLiteral("eta_seconds"), QJsonValue());
        return pausedSummary;
    }
    const QString liveStage = summary.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    const double liveUpdatedAt = summary.value(QStringLiteral("updated_at")).toDouble();
    const QString diskStage = diskSummary.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    const bool hasCurrentSessionTerminalState =
        liveUpdatedAt >= m_startedAt.toSecsSinceEpoch()
        && (liveStage == QStringLiteral("failed") || liveStage == QStringLiteral("stopped"));
    if (hasCurrentSessionTerminalState) {
        if (liveStage == QStringLiteral("stopped") && diskStage == QStringLiteral("idle")) {
            return diskSummary;
        }
        return summary;
    }

    if (liveUpdatedAt >= m_startedAt.toSecsSinceEpoch() && liveStage == QStringLiteral("completed")) {
        return diskStage == QStringLiteral("idle")
            ? summarizeJobRows(name, QJsonArray{})
            : diskSummary;
    }

    return diskSummary;
}

QJsonObject ControlCenterBackend::choosePrimaryJob(const QJsonObject& jobs, const QJsonObject& processes) const {
    const QStringList priority = {
        QStringLiteral("autopilot"),
        QStringLiteral("training"),
        QStringLiteral("prepare"),
        QStringLiteral("setup"),
        QStringLiteral("evaluate"),
        QStringLiteral("inference"),
    };

    for (const QString& name : priority) {
        if (processes.value(name).toObject().value(QStringLiteral("running")).toBool()) {
            return jobs.value(name).toObject();
        }
    }

    QJsonObject best;
    double bestTs = 0.0;
    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
        const QJsonObject job = it->toObject();
        const double updatedAt = job.value(QStringLiteral("updated_at")).toDouble();
        const QString stage = job.value(QStringLiteral("stage")).toString();
        if (stage != QStringLiteral("idle") && updatedAt >= bestTs) {
            best = job;
            bestTs = updatedAt;
        }
    }
    return best;
}

QJsonObject ControlCenterBackend::buildRecoverySummary(const QJsonObject& jobs, const QJsonObject& processes) const {
    const double now = QDateTime::currentSecsSinceEpoch();
    QJsonObject summary{
        {QStringLiteral("available"), false},
        {QStringLiteral("job"), QString()},
        {QStringLiteral("job_label"), QStringLiteral("No active block")},
        {QStringLiteral("running"), false},
        {QStringLiteral("paused"), false},
        {QStringLiteral("can_pause"), false},
        {QStringLiteral("can_resume"), false},
        {QStringLiteral("mode"), QStringLiteral("none")},
        {QStringLiteral("pause_loss_seconds"), 0.0},
        {QStringLiteral("work_since_last_recovery_seconds"), 0.0},
        {QStringLiteral("last_recovery_label"), QStringLiteral("No recovery point needed")},
        {QStringLiteral("last_recovery_at"), QJsonValue()},
        {QStringLiteral("next_recovery_label"), QStringLiteral("Recovery is not needed right now")},
        {QStringLiteral("next_recovery_eta_seconds"), QJsonValue()},
        {QStringLiteral("tooltip"), QStringLiteral("No active block to pause.")},
        {QStringLiteral("pause_tooltip"), QStringLiteral("No active block to pause.")},
        {QStringLiteral("resume_tooltip"), QStringLiteral("No paused block is ready to resume.")},
        {QStringLiteral("notification_key"), QString()},
    };

    const bool autopilotActive = m_autopilot.value(QStringLiteral("active")).toBool(false);
    const bool autopilotPaused = m_autopilot.value(QStringLiteral("paused")).toBool(false);
    const QString preferredJob = autopilotStageJobName(m_autopilot.value(QStringLiteral("stage")).toString());
    const auto jobStage = [&](const QString& jobName) {
        return jobs.value(jobName).toObject().value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    };
    const auto isTerminalStage = [&](const QString& stage) {
        return stage == QStringLiteral("idle")
            || stage == QStringLiteral("completed")
            || stage == QStringLiteral("failed")
            || stage == QStringLiteral("stopped");
    };
    const auto isProcessRunning = [&](const QString& jobName) {
        return processes.value(jobName).toObject().value(QStringLiteral("running")).toBool(false);
    };
    const auto isProcessPaused = [&](const QString& jobName) {
        const QJsonObject process = processes.value(jobName).toObject();
        return process.value(QStringLiteral("paused")).toBool(false)
            || jobStage(jobName) == QStringLiteral("paused");
    };
    const auto looksResumableFromState = [&](const QString& jobName) {
        if (jobName.isEmpty()) {
            return false;
        }
        const QString stage = jobStage(jobName);
        if (isProcessPaused(jobName)) {
            return true;
        }
        return autopilotActive
            && autopilotPaused
            && preferredJob == jobName
            && !isTerminalStage(stage);
    };
    const auto looksRelevantFromStage = [&](const QString& jobName) {
        if (jobName.isEmpty()) {
            return false;
        }
        const QString stage = jobStage(jobName);
        return !stage.isEmpty() && !isTerminalStage(stage);
    };
    const auto looksResumableFromDisk = [&](const QString& jobName) {
        if (jobName.isEmpty()) {
            return false;
        }
        const QJsonObject liveJob = jobs.value(jobName).toObject();
        const QJsonObject diskJob = inferJobStateFromDisk(jobName);
        const QString liveStage = liveJob.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
        const QString diskStage = diskJob.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
        const double progress = qMax(liveJob.value(QStringLiteral("progress")).toDouble(0.0),
                                     diskJob.value(QStringLiteral("progress")).toDouble(0.0));
        const double updatedAt = qMax(liveJob.value(QStringLiteral("updated_at")).toDouble(0.0),
                                      diskJob.value(QStringLiteral("updated_at")).toDouble(0.0));

        if (jobName == QStringLiteral("training")) {
            const bool hasCheckpoint = !latestCheckpointPath().isEmpty();
            if (diskStage == QStringLiteral("completed")) {
                return false;
            }
            if (diskStage == QStringLiteral("paused")) {
                return true;
            }
            if (diskStage == QStringLiteral("stopped")
                || diskStage == QStringLiteral("training")
                || diskStage == QStringLiteral("starting")
                || liveStage == QStringLiteral("failed")
                || liveStage == QStringLiteral("stopped")) {
                return hasCheckpoint;
            }
            return hasCheckpoint && (progress > 0.0 || updatedAt > 0.0);
        }

        const QString stage = diskStage == QStringLiteral("idle") ? liveStage : diskStage;
        if (stage == QStringLiteral("completed")) {
            return false;
        }
        if (stage == QStringLiteral("paused")) {
            return true;
        }
        if (stage == QStringLiteral("failed")) {
            return progress > 0.0 || updatedAt > 0.0;
        }
        if (stage == QStringLiteral("stopped")) {
            return progress > 0.0 || updatedAt > 0.0;
        }
        if (stage != QStringLiteral("idle")) {
            return true;
        }
        return progress > 0.0 && updatedAt > 0.0;
    };

    QString runningJob;
    if (!preferredJob.isEmpty() && isProcessRunning(preferredJob)) {
        runningJob = preferredJob;
    }
    if (runningJob.isEmpty()) {
        for (const QString& name : autopilotManagedJobs()) {
            if (isProcessRunning(name)) {
                runningJob = name;
                break;
            }
        }
    }

    QString pausedJob;
    if (!preferredJob.isEmpty() && looksResumableFromState(preferredJob)) {
        pausedJob = preferredJob;
    }
    if (pausedJob.isEmpty()) {
        for (const QString& name : autopilotManagedJobs()) {
            if (looksResumableFromState(name)) {
                pausedJob = name;
                break;
            }
        }
    }

    QString diskResumableJob;
    QString recommendedJob;
    for (const QString& name : autopilotManagedJobs()) {
        if (jobs.value(name).toObject().value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
            recommendedJob = name;
            break;
        }
    }
    if (recommendedJob.isEmpty()) {
        recommendedJob = QStringLiteral("training");
    }
    if (!preferredJob.isEmpty() && looksResumableFromDisk(preferredJob)) {
        diskResumableJob = preferredJob;
    }
    if (diskResumableJob.isEmpty() && !recommendedJob.isEmpty() && looksResumableFromDisk(recommendedJob)) {
        diskResumableJob = recommendedJob;
    }
    if (diskResumableJob.isEmpty()) {
        double bestTs = 0.0;
        for (const QString& name : autopilotManagedJobs()) {
            if (!looksResumableFromDisk(name)) {
                continue;
            }
            const double updatedAt = jobs.value(name).toObject().value(QStringLiteral("updated_at")).toDouble(0.0);
            if (updatedAt >= bestTs) {
                bestTs = updatedAt;
                diskResumableJob = name;
            }
        }
    }

    QString currentJob = !runningJob.isEmpty() ? runningJob : pausedJob;
    if (currentJob.isEmpty()) {
        currentJob = diskResumableJob;
    }
    if (currentJob.isEmpty() && looksRelevantFromStage(preferredJob)) {
        currentJob = preferredJob;
    }
    if (currentJob.isEmpty()) {
        return summary;
    }

    const QJsonObject job = jobs.value(currentJob).toObject();
    const QJsonObject process = processes.value(currentJob).toObject();
    const bool running = currentJob == runningJob || process.value(QStringLiteral("running")).toBool(false);
    const bool paused = currentJob == pausedJob
        || process.value(QStringLiteral("paused")).toBool(false)
        || job.value(QStringLiteral("stage")).toString() == QStringLiteral("paused");
    const bool canPause = !runningJob.isEmpty();
    const bool canResume = !pausedJob.isEmpty() || !diskResumableJob.isEmpty();
    const QString jobLabel = prettyJobName(currentJob);

    summary.insert(QStringLiteral("available"), true);
    summary.insert(QStringLiteral("job"), currentJob);
    summary.insert(QStringLiteral("job_label"), jobLabel);
    summary.insert(QStringLiteral("running"), running);
    summary.insert(QStringLiteral("paused"), paused);
    summary.insert(QStringLiteral("can_pause"), canPause);
    summary.insert(QStringLiteral("can_resume"), canResume);

    if (currentJob == QStringLiteral("training")) {
        const QString recoveryPath = QDir(m_runtimeStateDir).absoluteFilePath(QStringLiteral("training.recovery.json"));
        const QJsonObject recovery = readJsonObjectFile(recoveryPath);
        const QString latestCheckpoint = latestCheckpointPath();
        QString lastRecoveryLabel = recovery.value(QStringLiteral("last_checkpoint_name")).toString();
        double lastRecoveryAt = recovery.value(QStringLiteral("last_checkpoint_ts")).toDouble(0.0);
        if (lastRecoveryLabel.isEmpty() && !latestCheckpoint.isEmpty()) {
            lastRecoveryLabel = QFileInfo(latestCheckpoint).fileName();
        }
        if (lastRecoveryAt <= 0.0 && !latestCheckpoint.isEmpty()) {
            lastRecoveryAt = fileTimestamp(QFileInfo(latestCheckpoint));
        }

        const bool recoveryActive = recovery.value(QStringLiteral("active")).toBool(false);
        const bool recoveryPaused = recovery.value(QStringLiteral("paused")).toBool(false);
        const double recoveryWindowStartedAt = recovery.value(QStringLiteral("recovery_window_started_at")).toDouble(0.0);
        double workSinceLastRecovery = recovery.value(QStringLiteral("recovery_window_elapsed_seconds")).toDouble(-1.0);
        if (recoveryActive && !recoveryPaused && recoveryWindowStartedAt > 0.0) {
            workSinceLastRecovery = qMax(0.0, now - recoveryWindowStartedAt);
        } else if (workSinceLastRecovery < 0.0) {
            workSinceLastRecovery = recovery.value(QStringLiteral("seconds_since_last_checkpoint")).toDouble(-1.0);
        }
        if (workSinceLastRecovery < 0.0 && lastRecoveryAt > 0.0) {
            workSinceLastRecovery = qMax(0.0, now - lastRecoveryAt);
        }
        if (workSinceLastRecovery < 0.0) {
            workSinceLastRecovery = 0.0;
        }

        double nextRecoveryEta = recovery.value(QStringLiteral("eta_to_next_checkpoint_seconds")).toDouble(-1.0);
        const double recoverySnapshotTs = recovery.value(QStringLiteral("ts")).toDouble(0.0);
        const double expectedRecoveryWindow = recovery.value(QStringLiteral("recovery_window_expected_seconds")).toDouble(-1.0);
        const int nextCheckpointStep = recovery.value(QStringLiteral("next_checkpoint_step")).toInt(-1);
        const int nextCheckpointInSteps = recovery.value(QStringLiteral("next_checkpoint_in_steps")).toInt(-1);
        const int lastCheckpointStep = recovery.value(QStringLiteral("last_checkpoint_step")).toInt(0);
        const int currentStep = recovery.value(QStringLiteral("global_step")).toInt(lastCheckpointStep);

        if (recoveryActive && !recoveryPaused) {
            if (expectedRecoveryWindow >= 0.0) {
                nextRecoveryEta = qMax(0.0, expectedRecoveryWindow - workSinceLastRecovery);
            } else if (nextRecoveryEta >= 0.0 && recoverySnapshotTs > 0.0) {
                nextRecoveryEta = qMax(0.0, nextRecoveryEta - qMax(0.0, now - recoverySnapshotTs));
            }
        }

        QString nextRecoveryLabel = QStringLiteral("Pause will create a checkpoint on demand");
        if (nextCheckpointStep > 0) {
            nextRecoveryLabel = QStringLiteral("Checkpoint at step %1").arg(nextCheckpointStep);
            if (nextCheckpointInSteps >= 0) {
                nextRecoveryLabel += QStringLiteral(" (%1 steps away)").arg(nextCheckpointInSteps);
            }
        }

        QString pauseTooltip;
        if (lastRecoveryLabel.isEmpty()) {
            pauseTooltip = QStringLiteral(
                "Pause stops the active %1 block. No recovery point has been written yet, so the current work window at risk is about %2.")
                .arg(jobLabel, formatLossWindow(workSinceLastRecovery));
        } else {
            pauseTooltip = QStringLiteral(
                "Pause stops the active %1 block. Latest recovery point inside this block: %2. Current uncheckpointed work window: about %3.")
                .arg(jobLabel, lastRecoveryLabel, formatLossWindow(workSinceLastRecovery));
        }
        if (nextRecoveryEta >= 0.0) {
            pauseTooltip += QStringLiteral(" Next automatic recovery point: %1 in about %2.")
                .arg(nextRecoveryLabel, formatEta(nextRecoveryEta));
        } else if (nextCheckpointStep > 0) {
            pauseTooltip += QStringLiteral(" Next automatic recovery point: %1. Timing is still stabilizing.")
                .arg(nextRecoveryLabel);
        }

        QString resumeTooltip;
        if (lastRecoveryLabel.isEmpty()) {
            resumeTooltip = QStringLiteral(
                "Resume continues the current %1 block from the best recovery point available inside this block.")
                .arg(jobLabel);
        } else {
            resumeTooltip = QStringLiteral(
                "Resume continues the current %1 block from %2, the latest recovery point inside this block.")
                .arg(jobLabel, lastRecoveryLabel);
        }
        if (nextRecoveryEta >= 0.0) {
            resumeTooltip += QStringLiteral(" After resuming, the next automatic recovery point is about %2 away at %1.")
                .arg(nextRecoveryLabel, formatEta(nextRecoveryEta));
        }
        if (paused) {
            pauseTooltip += QStringLiteral(" Training is already paused.");
            resumeTooltip += QStringLiteral(" Training is currently paused and ready to continue.");
        } else if (running) {
            resumeTooltip += QStringLiteral(" Training is still running right now.");
        } else {
            resumeTooltip += QStringLiteral(" Training can resume from the latest checkpoint recovered from disk.");
        }

        summary.insert(QStringLiteral("mode"), QStringLiteral("checkpoint"));
        summary.insert(QStringLiteral("pause_loss_seconds"), paused ? 0.0 : workSinceLastRecovery);
        summary.insert(QStringLiteral("work_since_last_recovery_seconds"), workSinceLastRecovery);
        summary.insert(QStringLiteral("last_recovery_label"), lastRecoveryLabel.isEmpty() ? QStringLiteral("No checkpoint yet") : lastRecoveryLabel);
        summary.insert(QStringLiteral("last_recovery_at"), lastRecoveryAt > 0.0 ? QJsonValue(lastRecoveryAt) : QJsonValue());
        summary.insert(QStringLiteral("next_recovery_label"), nextRecoveryLabel);
        summary.insert(QStringLiteral("next_recovery_eta_seconds"), nextRecoveryEta >= 0.0 ? QJsonValue(nextRecoveryEta) : QJsonValue());
        summary.insert(QStringLiteral("tooltip"), pauseTooltip);
        summary.insert(QStringLiteral("pause_tooltip"), pauseTooltip);
        summary.insert(QStringLiteral("resume_tooltip"), resumeTooltip);
        summary.insert(QStringLiteral("current_step"), currentStep);
        summary.insert(QStringLiteral("last_checkpoint_step"), lastCheckpointStep);
        summary.insert(QStringLiteral("notification_key"),
                       !lastRecoveryLabel.isEmpty() && lastRecoveryAt > 0.0
                           ? QStringLiteral("%1@%2").arg(lastRecoveryLabel).arg(QString::number(static_cast<qint64>(lastRecoveryAt)))
                           : QString());
        return summary;
    }

    QString pauseTooltip = QStringLiteral(
        "Pause stops the active %1 block. Resume continues that same block from the latest recovery point within its filesystem state, so expected repeated work after a normal pause is minimal.")
        .arg(jobLabel);
    QString resumeTooltip = QStringLiteral(
        "Resume continues the current %1 block from the latest recovery point within its filesystem state.")
        .arg(jobLabel);
    if (paused) {
        pauseTooltip += QStringLiteral(" This block is already paused.");
        resumeTooltip += QStringLiteral(" This block is already paused and ready to continue.");
    } else if (running) {
        resumeTooltip += QStringLiteral(" The block is still running right now.");
    } else {
        resumeTooltip += QStringLiteral(" This block can resume from the latest recovered filesystem state.");
    }

    summary.insert(QStringLiteral("mode"), QStringLiteral("filesystem"));
    summary.insert(QStringLiteral("pause_loss_seconds"), 0.0);
    summary.insert(QStringLiteral("work_since_last_recovery_seconds"), 0.0);
    summary.insert(QStringLiteral("last_recovery_label"), QStringLiteral("Current filesystem state"));
    summary.insert(QStringLiteral("last_recovery_at"), job.value(QStringLiteral("updated_at")));
    summary.insert(QStringLiteral("next_recovery_label"), QStringLiteral("Recovery is continuous for this block"));
    summary.insert(QStringLiteral("next_recovery_eta_seconds"), QJsonValue(0.0));
    summary.insert(QStringLiteral("tooltip"), pauseTooltip);
    summary.insert(QStringLiteral("pause_tooltip"), pauseTooltip);
    summary.insert(QStringLiteral("resume_tooltip"), resumeTooltip);
    return summary;
}

void ControlCenterBackend::maybeRecordRecoveryPointAlert(const QJsonObject& recovery) {
    const QString notificationKey = recovery.value(QStringLiteral("notification_key")).toString();
    if (!m_recoveryNotificationPrimed) {
        m_recoveryNotificationPrimed = true;
        m_lastRecoveryNotificationKey = notificationKey;
        return;
    }
    if (notificationKey.isEmpty() || notificationKey == m_lastRecoveryNotificationKey) {
        return;
    }
    m_lastRecoveryNotificationKey = notificationKey;
    const double recoveryAt = recovery.value(QStringLiteral("last_recovery_at")).toDouble();
    if (recoveryAt > 0.0 && recoveryAt < m_startedAt.toSecsSinceEpoch()) {
        return;
    }
    recordAlert(QStringLiteral("info"),
                QStringLiteral("Recovery point ready for %1: %2")
                    .arg(recovery.value(QStringLiteral("job_label")).toString(QStringLiteral("Training")),
                         recovery.value(QStringLiteral("last_recovery_label")).toString(QStringLiteral("latest checkpoint"))));
}

QJsonObject ControlCenterBackend::buildStatePayload() {
    refreshRecoveredProcessStates();
    restoreAutopilotRuntimeState();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_stateCacheUntil.isValid() && now < m_stateCacheUntil) {
        return m_stateCache;
    }

    QJsonObject jobs;
    QJsonObject processes;
    const QStringList jobOrder = {
        QStringLiteral("setup"),
        QStringLiteral("prepare"),
        QStringLiteral("training"),
        QStringLiteral("evaluate"),
        QStringLiteral("inference"),
        QStringLiteral("autopilot"),
    };

    for (const QString& name : jobOrder) {
        QJsonObject summary = summarizeJob(name);
        const QJsonObject process = buildProcessSnapshot(name);
        summary.insert(QStringLiteral("running"), process.value(QStringLiteral("running")));
        jobs.insert(name, summary);
        processes.insert(name, process);
    }

    const QString report = latestReportText();
    const QJsonArray issues = currentIssues();
    const QJsonObject logSummary = buildLogSummary();
    const QJsonObject history = buildHistoryPayload();
    const QJsonObject recovery = buildRecoverySummary(jobs, processes);
    maybeRecordRecoveryPointAlert(recovery);
    m_stateCache = QJsonObject{
        {QStringLiteral("server"), QJsonObject{
             {QStringLiteral("type"), QStringLiteral("native-cpp")},
             {QStringLiteral("version"), QStringLiteral("2.6.0")},
             {QStringLiteral("uptime_seconds"), static_cast<double>(m_startedAt.secsTo(QDateTime::currentDateTimeUtc()))},
             {QStringLiteral("request_count"), static_cast<double>(m_requestCount)},
             {QStringLiteral("route_count"), static_cast<double>(m_server.routesJson().size())},
             {QStringLiteral("log_summary"), logSummary},
         }},
        {QStringLiteral("hardware"), buildHardwareSnapshot()},
        {QStringLiteral("jobs"), jobs},
        {QStringLiteral("processes"), processes},
        {QStringLiteral("primary_job"), choosePrimaryJob(jobs, processes)},
        {QStringLiteral("checkpoint"), buildCheckpointSummary()},
        {QStringLiteral("config"), configForClient(m_config)},
        {QStringLiteral("config_text"), configTextForClient(m_config)},
        {QStringLiteral("report"), report},
        {QStringLiteral("alerts"), recentAlerts(20)},
        {QStringLiteral("recovery"), recovery},
        {QStringLiteral("model_catalog"), buildModelCatalog()},
        {QStringLiteral("model_cache"), buildModelCacheSummary()},
        {QStringLiteral("history"), history},
        {QStringLiteral("autopilot"), m_autopilot},
        {QStringLiteral("feed"), recentFeedRows(50)},
        {QStringLiteral("metrics_feed"), recentMetricsRows(1200)},
        {QStringLiteral("diagnostics"), QJsonObject{
             {QStringLiteral("issues"), issues},
             {QStringLiteral("issue_count"), issues.size()},
             {QStringLiteral("log_summary"), logSummary},
             {QStringLiteral("cache"), QJsonObject{
                  {QStringLiteral("state_cached"), m_stateCacheUntil > now},
                  {QStringLiteral("hardware_cached"), m_hardwareCacheUntil > now},
                  {QStringLiteral("model_cache_cached"), m_modelCacheUntil > now},
              }},
         }},
    };
    m_stateCacheUntil = now.addMSecs(500);
    return m_stateCache;
}

HttpResponse ControlCenterBackend::handleState() {
    return HttpResponse{200, "application/json; charset=utf-8", jsonBytes(buildStatePayload())};
}

HttpResponse ControlCenterBackend::handleConfig(const HttpRequest& request) {
    if (request.method == QStringLiteral("GET")) {
        loadConfigFromDisk();
        return HttpResponse{200, "application/json; charset=utf-8", jsonBytes(QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("config"), configForClient(m_config)},
            {QStringLiteral("config_text"), configTextForClient(m_config)},
        })};
    }
    if (request.method != QStringLiteral("POST")) {
        return handleMethodNotAllowed();
    }

    const QString contentType = request.headers.value(QStringLiteral("content-type")).toLower();
    if (contentType.startsWith(QStringLiteral("application/json"))) {
        QJsonObject patch;
        QString parseError;
        if (!parseJsonObject(request.body, &patch, &parseError)) {
            return HttpResponse{400, "application/json; charset=utf-8", jsonBytes(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), parseError.isEmpty() ? QStringLiteral("Invalid JSON config patch") : parseError},
            })};
        }
        const QJsonObject merged = deepMergeObjects(m_config, patch);
        if (!saveConfigObject(merged)) {
            return HttpResponse{500, "application/json; charset=utf-8", jsonBytes(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("Could not save config")},
            })};
        }
        return HttpResponse{200, "application/json; charset=utf-8", jsonBytes(QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("config"), configForClient(m_config)},
        })};
    }

    QString saveError;
    if (!saveConfigText(QString::fromUtf8(request.body), &saveError)) {
        const bool invalidConfig = !saveError.isEmpty();
        return HttpResponse{invalidConfig ? 400 : 500, "application/json; charset=utf-8", jsonBytes(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), saveError.isEmpty() ? QStringLiteral("Could not save config text") : saveError},
        })};
    }

    return HttpResponse{200, "application/json; charset=utf-8", jsonBytes(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("config"), configForClient(m_config)},
    })};
}
