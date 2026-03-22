#pragma once

#include "native_worker_context.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

class NativeWorkerRuntime final {
public:
    static QJsonObject loadConfig(const QString& configPath, QString* errorMessage);
    static QString resolveProjectPath(const QString& projectRoot,
                                      const QString& configuredPath,
                                      const QString& fallbackRelative);
    static void emitProgress(const QString& job,
                             const QString& stage,
                             double progress,
                             const QString& message);
    static void appendFeedRow(const QJsonObject& row);
    static bool writeJsonFile(const QString& path, const QJsonValue& value);
    static bool writeTextFileStrict(const QString& path, const QString& text);
    static QString dataDir(const NativeWorkerContext& context);
    static QString cacheDir(const NativeWorkerContext& context);
    static QString reportDir(const NativeWorkerContext& context);
    static QString checkpointDir(const NativeWorkerContext& context);
    static QString trainingPauseRequestPath(const QString& projectRoot);
    static QString trainingRecoveryStatePath(const QString& projectRoot);
    static bool trainingPauseRequested(const QString& projectRoot);
    static void clearTrainingPauseRequest(const QString& projectRoot);
    static bool writeTrainingRecoveryState(const QString& projectRoot, const QJsonObject& payload);
    static QString latestCheckpointPath(const QString& checkpointRoot);
};
