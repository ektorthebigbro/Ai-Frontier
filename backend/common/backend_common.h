#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStandardPaths>
#include <QThread>

namespace ControlCenterBackendCommon {

inline bool looksLikeProjectRoot(const QDir& dir) {
    return QFileInfo(dir.absoluteFilePath(QStringLiteral("configs/default.yaml"))).exists()
        && QFileInfo(dir.absoluteFilePath(QStringLiteral("pyproject.toml"))).exists();
}

inline QString projectRootPath() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 12; ++depth) {
        if (looksLikeProjectRoot(dir)) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    QDir fallback(QCoreApplication::applicationDirPath());
    fallback.cdUp();
    fallback.cdUp();
    return fallback.absolutePath();
}

inline QString rootPathFor(const QString& relative) {
    return QDir(projectRootPath()).absoluteFilePath(relative);
}

inline QString isoNow() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

inline QString formatEta(double seconds) {
    if (seconds < 0.0) {
        return QStringLiteral("unknown");
    }
    const int total = qRound(seconds);
    if (total < 60) {
        return QStringLiteral("%1s").arg(total);
    }
    const int minutes = total / 60;
    const int secs = total % 60;
    if (minutes < 60) {
        return QStringLiteral("%1m %2s").arg(minutes).arg(secs);
    }
    const int hours = minutes / 60;
    const int mins = minutes % 60;
    return QStringLiteral("%1h %2m").arg(hours).arg(mins);
}

inline QJsonObject deepMergeObjects(QJsonObject base, const QJsonObject& patch) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it->isObject() && base.value(it.key()).isObject()) {
            base.insert(it.key(), deepMergeObjects(base.value(it.key()).toObject(), it->toObject()));
            continue;
        }
        base.insert(it.key(), *it);
    }
    return base;
}

inline bool parseJsonObject(const QByteArray& bytes, QJsonObject* outObject, QString* errorMessage = nullptr) {
    if (!outObject) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No JSON output target was provided");
        }
        return false;
    }

    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *outObject = QJsonObject{};
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        }
        return false;
    }
    if (!doc.isObject()) {
        *outObject = QJsonObject{};
        if (errorMessage) {
            *errorMessage = QStringLiteral("Expected a JSON object payload");
        }
        return false;
    }

    *outObject = doc.object();
    return true;
}

inline QJsonObject parseJsonObject(const QByteArray& bytes) {
    QJsonObject object;
    parseJsonObject(bytes, &object, nullptr);
    return object;
}

inline QByteArray jsonBytes(const QJsonValue& value) {
    if (value.isArray()) {
        return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
    }
    if (value.isObject()) {
        return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("value"), value}}).toJson(QJsonDocument::Compact);
}

inline bool readTextFile(const QString& path, QString* outText) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    *outText = QString::fromUtf8(file.readAll());
    return true;
}

inline bool writeTextFile(const QString& path, const QString& text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray data = text.toUtf8();
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

inline void ensureDir(const QString& path) {
    QDir().mkpath(path);
}

inline bool appendTextFileLocked(const QString& path, const QByteArray& data, int timeoutMs = 1500) {
    ensureDir(QFileInfo(path).absolutePath());
    const QString lockPath = path + QStringLiteral(".lock");
    QElapsedTimer timer;
    timer.start();

    while (true) {
        QFile lockFile(lockPath);
        if (lockFile.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text)) {
            lockFile.write(QByteArray::number(QCoreApplication::applicationPid()));
            lockFile.close();

            QFile file(path);
            const bool opened = file.open(QIODevice::Append | QIODevice::Text);
            bool ok = false;
            if (opened) {
                ok = file.write(data) == data.size();
                file.close();
            }
            QFile::remove(lockPath);
            return opened && ok;
        }

        const QFileInfo lockInfo(lockPath);
        if (lockInfo.exists() && lockInfo.lastModified().secsTo(QDateTime::currentDateTimeUtc()) > 30) {
            QFile::remove(lockPath);
            continue;
        }
        if (timer.elapsed() >= timeoutMs) {
            return false;
        }
        QThread::msleep(10);
    }
}

inline bool rewriteTextFileLocked(const QString& path, const QByteArray& data, int timeoutMs = 3000) {
    ensureDir(QFileInfo(path).absolutePath());
    const QString lockPath = path + QStringLiteral(".lock");
    QElapsedTimer timer;
    timer.start();

    while (true) {
        QFile lockFile(lockPath);
        if (lockFile.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text)) {
            lockFile.write(QByteArray::number(QCoreApplication::applicationPid()));
            lockFile.close();

            const bool ok = writeTextFile(path, QString::fromUtf8(data));
            QFile::remove(lockPath);
            return ok;
        }

        const QFileInfo lockInfo(lockPath);
        if (lockInfo.exists() && lockInfo.lastModified().secsTo(QDateTime::currentDateTimeUtc()) > 30) {
            QFile::remove(lockPath);
            continue;
        }
        if (timer.elapsed() >= timeoutMs) {
            return false;
        }
        QThread::msleep(10);
    }
}

inline QStringList readTailLinesFromFile(const QString& path, int maxLines, qint64 maxBytes = 512 * 1024) {
    if (maxLines <= 0) {
        return {};
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const qint64 fileSize = file.size();
    if (fileSize <= 0) {
        return {};
    }

    const qint64 chunkSize = 64 * 1024;
    qint64 bytesToRead = qMin(fileSize, qMax<qint64>(chunkSize, maxBytes));
    qint64 start = fileSize - bytesToRead;
    QByteArray data;

    while (true) {
        if (!file.seek(start)) {
            break;
        }
        data = file.read(fileSize - start);
        if (data.count('\n') >= maxLines + 1 || start == 0 || (fileSize - start) >= maxBytes) {
            break;
        }
        if (start == 0) {
            break;
        }
        bytesToRead = qMin(fileSize, bytesToRead + chunkSize);
        start = qMax<qint64>(0, fileSize - bytesToRead);
    }

    QStringList lines = QString::fromUtf8(data).split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                                      Qt::SkipEmptyParts);
    if (lines.size() > maxLines) {
        lines = lines.mid(lines.size() - maxLines);
    }
    return lines;
}

inline QStringList tailLines(const QString& text, int maxLines) {
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    if (lines.size() > maxLines) {
        lines = lines.mid(lines.size() - maxLines);
    }
    return lines;
}

inline bool isSensitiveConfigKey(const QString& key) {
    const QString normalized = key.trimmed().toLower();
    return normalized == QStringLiteral("token")
        || normalized == QStringLiteral("api_key")
        || normalized == QStringLiteral("apikey")
        || normalized == QStringLiteral("secret")
        || normalized == QStringLiteral("password")
        || normalized.endsWith(QStringLiteral("_token"))
        || normalized.endsWith(QStringLiteral("_secret"))
        || normalized.endsWith(QStringLiteral("_password"))
        || normalized.endsWith(QStringLiteral("_api_key"));
}

inline QString redactedSecretPlaceholder() {
    return QStringLiteral("__REDACTED__");
}

inline QJsonValue redactSecrets(const QJsonValue& value) {
    if (value.isObject()) {
        QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (isSensitiveConfigKey(it.key()) && (it->isString() || it->isDouble() || it->isBool())) {
                object.insert(it.key(), redactedSecretPlaceholder());
            } else {
                object.insert(it.key(), redactSecrets(*it));
            }
        }
        return object;
    }
    if (value.isArray()) {
        QJsonArray array;
        for (const QJsonValue& entry : value.toArray()) {
            array.append(redactSecrets(entry));
        }
        return array;
    }
    return value;
}

inline void restoreRedactedSecretsIntoObject(QJsonObject* target, const QJsonObject& source) {
    if (!target) {
        return;
    }
    for (auto it = target->begin(); it != target->end(); ++it) {
        const QString key = it.key();
        const QJsonValue sourceValue = source.value(key);
        if (isSensitiveConfigKey(key) && it->isString()
            && it->toString() == redactedSecretPlaceholder()
            && sourceValue.isString()) {
            target->insert(key, sourceValue);
            continue;
        }
        if (it->isObject() && sourceValue.isObject()) {
            QJsonObject child = it->toObject();
            restoreRedactedSecretsIntoObject(&child, sourceValue.toObject());
            target->insert(key, child);
            continue;
        }
        if (it->isArray() && sourceValue.isArray()) {
            const QJsonArray targetArray = it->toArray();
            const QJsonArray sourceArray = sourceValue.toArray();
            QJsonArray merged;
            for (int index = 0; index < targetArray.size(); ++index) {
                if (index < sourceArray.size()
                    && targetArray.at(index).isObject()
                    && sourceArray.at(index).isObject()) {
                    QJsonObject child = targetArray.at(index).toObject();
                    restoreRedactedSecretsIntoObject(&child, sourceArray.at(index).toObject());
                    merged.append(child);
                } else {
                    merged.append(targetArray.at(index));
                }
            }
            target->insert(key, merged);
        }
    }
}

inline QProcessEnvironment backendEnvironment() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString root = projectRootPath();
    const QString tempDir = rootPathFor(QStringLiteral(".tmp/runtime_workers"));
    ensureDir(tempDir);
    const QString existingPythonPath = env.value(QStringLiteral("PYTHONPATH"));
    env.insert(QStringLiteral("PYTHONPATH"),
               existingPythonPath.isEmpty()
                   ? root
                   : root + QDir::listSeparator() + existingPythonPath);
    if (!env.contains(QStringLiteral("CUDA_VISIBLE_DEVICES"))) {
        env.insert(QStringLiteral("CUDA_VISIBLE_DEVICES"), QStringLiteral("0"));
    }
    if (!env.contains(QStringLiteral("HIP_VISIBLE_DEVICES"))) {
        env.insert(QStringLiteral("HIP_VISIBLE_DEVICES"), QStringLiteral("0"));
    }
    env.insert(QStringLiteral("TEMP"), tempDir);
    env.insert(QStringLiteral("TMP"), tempDir);
    env.insert(QStringLiteral("TMPDIR"), tempDir);
    return env;
}

inline QStringList venvPythonCandidates() {
    return {
        rootPathFor(QStringLiteral(".venv/Scripts/python.exe")),
        rootPathFor(QStringLiteral(".venv/bin/python3")),
        rootPathFor(QStringLiteral(".venv/bin/python")),
    };
}

inline QString firstExistingPath(const QStringList& candidates) {
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return QString();
}

inline QStringList venvSitePackagesCandidates() {
    QStringList candidates{rootPathFor(QStringLiteral(".venv/Lib/site-packages"))};
    const QDir libDir(rootPathFor(QStringLiteral(".venv/lib")));
    const QFileInfoList entries = libDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& info : entries) {
        if (info.fileName().startsWith(QStringLiteral("python"))) {
            candidates.append(QDir(info.absoluteFilePath()).absoluteFilePath(QStringLiteral("site-packages")));
        }
    }
    return candidates;
}

inline QString pythonPath() {
    const QString venvPython = firstExistingPath(venvPythonCandidates());
    if (!venvPython.isEmpty()) {
        return venvPython;
    }
    const QString python3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (!python3.isEmpty()) {
        return python3;
    }
    const QString python = QStandardPaths::findExecutable(QStringLiteral("python"));
    if (!python.isEmpty()) {
        return python;
    }
    return QStringLiteral("python");
}

inline QString launcherScriptPath() {
    return rootPathFor(QStringLiteral("scripts/launcher.py"));
}

inline QString sanitizeModelDir(const QString& modelId) {
    QString name = modelId;
    name.replace(QStringLiteral("/"), QStringLiteral("--"));
    return QStringLiteral("models--%1").arg(name);
}

inline QString actionHistorySignature(double ts,
                                      const QString& severity,
                                      const QString& category,
                                      const QString& action,
                                      const QString& message) {
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(QString::number(ts, 'f', 3),
             severity,
             category,
             action,
             message);
}

}  // namespace ControlCenterBackendCommon
