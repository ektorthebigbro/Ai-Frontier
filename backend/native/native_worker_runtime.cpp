#include "native_worker_runtime.h"

#include "../common/backend_common.h"
#include "../config/simple_yaml.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>

using namespace ControlCenterBackendCommon;

namespace {

QString feedPath() {
    return rootPathFor(QStringLiteral("logs/dashboard_metrics.jsonl"));
}

bool isPathInsideRoot(const QString& path, const QString& rootPath) {
    const QString normalizedPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString normalizedRoot = QDir::cleanPath(QFileInfo(rootPath).absoluteFilePath());
    return normalizedPath == normalizedRoot
        || normalizedPath.startsWith(normalizedRoot + QLatin1Char('/'))
        || normalizedPath.startsWith(normalizedRoot + QLatin1Char('\\'));
}

}  // namespace

QJsonObject NativeWorkerRuntime::loadConfig(const QString& configPath, QString* errorMessage) {
    QString text;
    if (!readTextFile(configPath, &text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not read config: %1").arg(configPath);
        }
        return {};
    }

    QString parseError;
    const QJsonValue parsed = SimpleYaml::parse(text, &parseError);
    if (!parseError.isEmpty() || !parsed.isObject()) {
        if (errorMessage) {
            *errorMessage = parseError.isEmpty()
                ? QStringLiteral("Config must parse to an object")
                : parseError;
        }
        return {};
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return parsed.toObject();
}

QString NativeWorkerRuntime::resolveProjectPath(const QString& projectRoot,
                                                const QString& configuredPath,
                                                const QString& fallbackRelative) {
    const QString candidate = configuredPath.trimmed().isEmpty()
        ? fallbackRelative
        : configuredPath.trimmed();
    const QString absolute = QDir::isAbsolutePath(candidate)
        ? QDir::cleanPath(QFileInfo(candidate).absoluteFilePath())
        : QDir(projectRoot).absoluteFilePath(candidate);
    if (isPathInsideRoot(absolute, projectRoot)) {
        return absolute;
    }
    return QDir(projectRoot).absoluteFilePath(fallbackRelative);
}

void NativeWorkerRuntime::emitProgress(const QString& job,
                                       const QString& stage,
                                       double progress,
                                       const QString& message) {
    QTextStream(stdout)
        << QStringLiteral("AI_PROGRESS|%1|%2|%3|%4\n")
               .arg(job,
                    stage,
                    QString::number(qBound(0.0, progress, 1.0), 'f', 3),
                    message)
        << Qt::flush;

    appendFeedRow(QJsonObject{
        {QStringLiteral("ts"), QDateTime::currentSecsSinceEpoch()},
        {QStringLiteral("job"), job},
        {QStringLiteral("stage"), stage},
        {QStringLiteral("message"), message},
        {QStringLiteral("progress"), qBound(0.0, progress, 1.0)},
    });
}

void NativeWorkerRuntime::appendFeedRow(const QJsonObject& row) {
    QByteArray payload = QJsonDocument(row).toJson(QJsonDocument::Compact);
    payload.append('\n');
    appendTextFileLocked(feedPath(), payload);
}

bool NativeWorkerRuntime::writeJsonFile(const QString& path, const QJsonValue& value) {
    const QByteArray bytes = value.isArray()
        ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented)
        : QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented);
    return writeTextFile(path, QString::fromUtf8(bytes));
}

bool NativeWorkerRuntime::writeTextFileStrict(const QString& path, const QString& text) {
    return writeTextFile(path, text);
}

QString NativeWorkerRuntime::dataDir(const NativeWorkerContext& context) {
    return resolveProjectPath(context.projectRoot,
                              context.config.value(QStringLiteral("datasets")).toObject()
                                  .value(QStringLiteral("data_dir")).toString(),
                              QStringLiteral("data/processed"));
}

QString NativeWorkerRuntime::cacheDir(const NativeWorkerContext& context) {
    return resolveProjectPath(context.projectRoot,
                              context.config.value(QStringLiteral("datasets")).toObject()
                                  .value(QStringLiteral("cache_dir")).toString(),
                              QStringLiteral("data/cache"));
}

QString NativeWorkerRuntime::reportDir(const NativeWorkerContext& context) {
    return resolveProjectPath(context.projectRoot,
                              context.config.value(QStringLiteral("evaluation")).toObject()
                                  .value(QStringLiteral("report_dir")).toString(),
                              QStringLiteral("artifacts"));
}

QString NativeWorkerRuntime::checkpointDir(const NativeWorkerContext& context) {
    return resolveProjectPath(context.projectRoot, QString(), QStringLiteral("checkpoints"));
}

QString NativeWorkerRuntime::trainingPauseRequestPath(const QString& projectRoot) {
    return QDir(projectRoot).absoluteFilePath(QStringLiteral(".tmp/runtime_state/training.pause.request"));
}

QString NativeWorkerRuntime::trainingRecoveryStatePath(const QString& projectRoot) {
    return QDir(projectRoot).absoluteFilePath(QStringLiteral(".tmp/runtime_state/training.recovery.json"));
}

bool NativeWorkerRuntime::trainingPauseRequested(const QString& projectRoot) {
    return QFileInfo::exists(trainingPauseRequestPath(projectRoot));
}

void NativeWorkerRuntime::clearTrainingPauseRequest(const QString& projectRoot) {
    QFile::remove(trainingPauseRequestPath(projectRoot));
}

bool NativeWorkerRuntime::writeTrainingRecoveryState(const QString& projectRoot, const QJsonObject& payload) {
    const QString path = trainingRecoveryStatePath(projectRoot);
    const QString tempPath = path + QStringLiteral(".tmp");
    if (!writeJsonFile(tempPath, payload)) {
        return false;
    }
    QFile::remove(path);
    return QFile::rename(tempPath, path);
}

QString NativeWorkerRuntime::latestCheckpointPath(const QString& checkpointRoot) {
    const QDir dir(checkpointRoot);
    if (!dir.exists()) {
        return QString();
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo& entry : entries) {
        const QString checkpointArtifact = QDir(entry.absoluteFilePath()).absoluteFilePath(QStringLiteral("checkpoint.pt"));
        if (QFileInfo::exists(checkpointArtifact)) {
            return entry.absoluteFilePath();
        }
    }
    return QString();
}
