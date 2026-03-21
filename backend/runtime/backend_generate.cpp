#include "../backend.h"
#include "../common/backend_common.h"
#include <QEventLoop>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <QTimer>

using namespace ControlCenterBackendCommon;

namespace {

QString extractErrorMessage(const QByteArray& bytes, const QString& fallbackMessage) {
    const QString raw = QString::fromUtf8(bytes).trimmed();
    if (raw.isEmpty()) {
        return fallbackMessage;
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseErr);
    if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject object = doc.object();
        const QString error = object.value(QStringLiteral("error")).toString().trimmed();
        if (!error.isEmpty()) {
            return error;
        }
        const QString detail = object.value(QStringLiteral("detail")).toString().trimmed();
        if (!detail.isEmpty()) {
            return detail;
        }
        const QString message = object.value(QStringLiteral("message")).toString().trimmed();
        if (!message.isEmpty()) {
            return message;
        }
    }

    return raw;
}

QJsonObject blockingJsonRequest(
    QNetworkAccessManager* network,
    const QString& method,
    const QUrl& url,
    const QJsonObject& payload,
    int timeoutMs,
    QString* errorMessage
) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply* reply = nullptr;
    if (method == QStringLiteral("GET")) {
        reply = network->get(request);
    } else {
        reply = network->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    if (timer.isActive()) {
        timer.stop();
    } else {
        reply->abort();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Request timed out");
        }
        reply->deleteLater();
        return QJsonObject{};
    }

    const QByteArray bytes = reply->readAll();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        QJsonObject errorObject;
        QJsonParseError parseErr;
        const QJsonDocument errorDoc = QJsonDocument::fromJson(bytes, &parseErr);
        if (parseErr.error == QJsonParseError::NoError && errorDoc.isObject()) {
            errorObject = errorDoc.object();
        } else {
            const QString raw = QString::fromUtf8(bytes).trimmed();
            if (!raw.isEmpty()) {
                errorObject.insert(QStringLiteral("error"), raw);
            }
        }
        if (errorMessage) {
            const QString detail = extractErrorMessage(bytes, reply->errorString());
            *errorMessage = statusCode >= 400
                ? QStringLiteral("HTTP %1: %2").arg(statusCode).arg(detail)
                : detail;
        }
        if (!errorObject.isEmpty()) {
            errorObject.insert(QStringLiteral("__http_status"), statusCode > 0 ? statusCode : 503);
            errorObject.insert(QStringLiteral("ok"), false);
        }
        reply->deleteLater();
        return errorObject;
    }

    reply->deleteLater();
    const auto doc = QJsonDocument::fromJson(bytes);
    if (!doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid JSON response");
        }
        return QJsonObject{};
    }
    return doc.object();
}

}  // namespace

bool ControlCenterBackend::ensureInferenceReady(QString* errorMessage) {
    const ManagedProcessState& inference = m_processes.value(QStringLiteral("inference"));
    if (!inference.process || inference.process->state() == QProcess::NotRunning) {
        if (!startManagedProcess(QStringLiteral("inference"), m_processes.value(QStringLiteral("inference")).scriptPath, m_processes.value(QStringLiteral("inference")).extraArgs)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Could not start inference server");
            }
            return false;
        }
    }

    const QJsonObject inf = m_config.value(QStringLiteral("inference")).toObject();
    const QString host = inf.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1"));
    const int port = inf.value(QStringLiteral("port")).toInt(8766);
    const QUrl healthUrl(QStringLiteral("http://%1:%2/health").arg(host).arg(port));

    QString healthError;
    for (int attempt = 0; attempt < 15; ++attempt) {
        const QJsonObject health = blockingJsonRequest(&m_network, QStringLiteral("GET"), healthUrl, QJsonObject{}, 1500, &healthError);
        if (!health.isEmpty() && health.value(QStringLiteral("status")).toString() == QStringLiteral("ok")) {
            return true;
        }
        QThread::msleep(500);
    }

    if (errorMessage) {
        *errorMessage = healthError.isEmpty() ? QStringLiteral("Inference server did not become healthy") : healthError;
    }
    return false;
}

QJsonObject ControlCenterBackend::proxyInferenceGenerate(const QJsonObject& payload, QString* errorMessage) {
    const QJsonObject inf = m_config.value(QStringLiteral("inference")).toObject();
    const QString host = inf.value(QStringLiteral("host")).toString(QStringLiteral("127.0.0.1"));
    const int port = inf.value(QStringLiteral("port")).toInt(8766);
    const QUrl generateUrl(QStringLiteral("http://%1:%2/generate").arg(host).arg(port));
    return blockingJsonRequest(&m_network, QStringLiteral("POST"), generateUrl, payload, 120000, errorMessage);
}

HttpResponse ControlCenterBackend::handleGenerate(const HttpRequest& request) {
    QJsonObject payload;
    QString parseError;
    if (!parseJsonObject(request.body, &payload, &parseError)) {
        return HttpResponse{400, "application/json; charset=utf-8", jsonBytes(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), parseError.isEmpty() ? QStringLiteral("Invalid JSON request") : parseError},
        })};
    }
    if (payload.value(QStringLiteral("prompt")).toString().trimmed().isEmpty()) {
        return HttpResponse{400, "application/json; charset=utf-8", jsonBytes(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Prompt is required")},
        })};
    }

    QString errorMessage;
    if (!ensureInferenceReady(&errorMessage)) {
        recordIssue(QStringLiteral("inference"), errorMessage);
        return HttpResponse{503, "application/json; charset=utf-8", jsonBytes(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), errorMessage},
        })};
    }

    const QJsonObject result = proxyInferenceGenerate(payload, &errorMessage);
    const int upstreamStatus = result.value(QStringLiteral("__http_status")).toInt();
    if (upstreamStatus > 0) {
        QJsonObject cleaned = result;
        cleaned.remove(QStringLiteral("__http_status"));
        if (!cleaned.contains(QStringLiteral("ok"))) {
            cleaned.insert(QStringLiteral("ok"), false);
        }
        recordIssue(QStringLiteral("inference"), cleaned.value(QStringLiteral("error")).toString(errorMessage));
        return HttpResponse{
            upstreamStatus,
            "application/json; charset=utf-8",
            jsonBytes(cleaned),
        };
    }

    if (result.isEmpty()) {
        recordIssue(QStringLiteral("inference"), errorMessage);
        return HttpResponse{503, "application/json; charset=utf-8", jsonBytes(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), errorMessage},
        })};
    }

    return HttpResponse{200, "application/json; charset=utf-8", jsonBytes(result)};
}
