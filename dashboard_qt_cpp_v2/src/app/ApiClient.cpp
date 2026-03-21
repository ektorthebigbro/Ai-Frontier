#include "ApiClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

QJsonObject parseObjectPayload(const QByteArray& data) {
    if (data.trimmed().isEmpty()) {
        return QJsonObject{};
    }

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        return QJsonObject{};
    }

    if (doc.isObject()) {
        return doc.object();
    }

    if (doc.isArray()) {
        return QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("items"), doc.array()},
        };
    }

    return QJsonObject{};
}

QString payloadMessage(const QJsonObject& payload) {
    const QString error = payload.value(QStringLiteral("error")).toString().trimmed();
    if (!error.isEmpty()) {
        return error;
    }

    const QString detail = payload.value(QStringLiteral("detail")).toString().trimmed();
    if (!detail.isEmpty()) {
        return detail;
    }

    const QString message = payload.value(QStringLiteral("message")).toString().trimmed();
    if (!message.isEmpty()) {
        return message;
    }

    return QString();
}

void setJsonHeaders(QNetworkRequest& request) {
    request.setRawHeader("Accept", "application/json");
}

}  // namespace

ApiClient::ApiClient(const QString& baseUrl, QObject* parent)
    : QObject(parent), m_baseUrl(baseUrl), m_network(new QNetworkAccessManager(this)) {}

// ---------------------------------------------------------------------------
// GET helpers
// ---------------------------------------------------------------------------

void ApiClient::getJson(const QString& path,
                        const std::function<void(const QJsonObject&)>& onSuccess,
                        const std::function<void(const QString&)>& onError) {
    QNetworkRequest req(QUrl(m_baseUrl + path));
    setJsonHeaders(req);
    QNetworkReply* reply = m_network->get(req);
    handleReply(reply, onSuccess, onError);
}

void ApiClient::fetchState() {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/state")));
    setJsonHeaders(req);
    QNetworkReply* reply = m_network->get(req);
    handleReply(
        reply,
        [this](const QJsonObject& obj) { emit stateReceived(obj); },
        [this](const QString& err) { emit stateFetchFailed(err); });
}

void ApiClient::fetchDiagnostics() {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/diagnostics")));
    setJsonHeaders(req);
    QNetworkReply* reply = m_network->get(req);
    handleReply(
        reply,
        [this](const QJsonObject& obj) { emit diagnosticsReceived(obj); },
        [this](const QString& err) { emit diagnosticsFetchFailed(err); });
}

// ---------------------------------------------------------------------------
// POST helpers
// ---------------------------------------------------------------------------

void ApiClient::postAction(const QString& endpoint) {
    QNetworkRequest req(QUrl(m_baseUrl + endpoint));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    setJsonHeaders(req);
    QNetworkReply* reply = m_network->post(req, QByteArray("{}"));
    handleReply(
        reply,
        [this](const QJsonObject& obj) { emit actionDone(obj); },
        [this](const QString& err) {
            emit actionDone(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), err},
                {QStringLiteral("message"), err},
            });
        });
}

void ApiClient::postConfig(const QJsonObject& config) {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/config")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    setJsonHeaders(req);
    QJsonDocument doc(config);
    QNetworkReply* reply = m_network->post(req, doc.toJson(QJsonDocument::Compact));
    handleReply(
        reply,
        [this](const QJsonObject& obj) {
            emit configSaved(true, obj.value(QStringLiteral("message")).toString());
        },
        [this](const QString& err) { emit configSaved(false, err); });
}

void ApiClient::postConfigYaml(const QString& yaml) {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/config")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain"));
    setJsonHeaders(req);
    QNetworkReply* reply = m_network->post(req, yaml.toUtf8());
    handleReply(
        reply,
        [this](const QJsonObject& obj) {
            emit configSaved(true, obj.value(QStringLiteral("message")).toString());
        },
        [this](const QString& err) { emit configSaved(false, err); });
}

void ApiClient::postGenerate(const QJsonObject& payload) {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/generate")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    setJsonHeaders(req);
    QJsonDocument doc(payload);
    QNetworkReply* reply = m_network->post(req, doc.toJson(QJsonDocument::Compact));
    handleReply(
        reply,
        [this](const QJsonObject& obj) { emit generateDone(obj); },
        [this](const QString& err) {
            emit generateDone(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), err},
                {QStringLiteral("message"), err},
            });
        });
}

void ApiClient::postDiagnosticsReload(const QString& moduleName) {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/diagnostics/reload")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    setJsonHeaders(req);
    QJsonObject body;
    body.insert(QStringLiteral("module"), moduleName);
    QNetworkReply* reply = m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    handleReply(
        reply,
        [this](const QJsonObject& obj) { emit actionDone(obj); },
        [this](const QString& err) {
            emit actionDone(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), err},
                {QStringLiteral("message"), err},
            });
        });
}

void ApiClient::postDiagnosticsClear(const QString& key) {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/diagnostics/clear")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    setJsonHeaders(req);
    QJsonObject body;
    if (!key.isEmpty())
        body.insert(QStringLiteral("key"), key);
    QNetworkReply* reply = m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    handleReply(
        reply,
        [this](const QJsonObject& obj) { emit actionDone(obj); },
        [this](const QString& err) {
            emit actionDone(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), err},
                {QStringLiteral("message"), err},
            });
        });
}

void ApiClient::postDiagnosticsRunChecks() {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/diagnostics/run-checks")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    setJsonHeaders(req);
    QNetworkReply* reply = m_network->post(req, QByteArray("{}"));
    handleReply(
        reply,
        [this](const QJsonObject& obj) { emit actionDone(obj); },
        [this](const QString& err) {
            emit actionDone(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), err},
                {QStringLiteral("message"), err},
            });
        });
}

void ApiClient::postDiagnosticsSelfHeal(bool aggressive) {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/diagnostics/self-heal")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    setJsonHeaders(req);
    QJsonObject body;
    body.insert(QStringLiteral("aggressive"), aggressive);
    QNetworkReply* reply = m_network->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    handleReply(
        reply,
        [this](const QJsonObject& obj) { emit actionDone(obj); },
        [this](const QString& err) {
            emit actionDone(QJsonObject{
                {QStringLiteral("ok"), false},
                {QStringLiteral("error"), err},
                {QStringLiteral("message"), err},
            });
        });
}

// ---------------------------------------------------------------------------
// Internal reply handler
// ---------------------------------------------------------------------------

void ApiClient::handleReply(QNetworkReply* reply,
                            const std::function<void(const QJsonObject&)>& onSuccess,
                            const std::function<void(const QString&)>& onError) {
    connect(reply, &QNetworkReply::finished, this, [reply, onSuccess, onError]() {
        const QByteArray data = reply->readAll();
        const QJsonObject payload = parseObjectPayload(data);
        const QString message = payloadMessage(payload);
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();

        auto fallbackError = [&]() -> QString {
            if (!message.isEmpty()) {
                return message;
            }
            const QString raw = QString::fromUtf8(data).trimmed();
            if (!raw.isEmpty()) {
                return raw;
            }
            return reply->errorString();
        };

        if (reply->error() != QNetworkReply::NoError) {
            QString errMsg = fallbackError();
            if (httpStatus >= 400 && !errMsg.startsWith(QStringLiteral("HTTP "))) {
                errMsg = QStringLiteral("HTTP %1: %2").arg(httpStatus).arg(errMsg);
            }
            if (onError)
                onError(errMsg);
            else
                qWarning() << "ApiClient: unhandled network error:" << errMsg;
            return;
        }

        if (data.trimmed().isEmpty()) {
            if (onSuccess) {
                onSuccess(QJsonObject{});
            }
            return;
        }

        if (payload.isEmpty()) {
            if (onError)
                onError(QStringLiteral("Expected a JSON object response from the backend."));
            else
                qWarning() << "ApiClient: unexpected response payload:" << data;
            return;
        }

        if (onSuccess)
            onSuccess(payload);
    });
}
