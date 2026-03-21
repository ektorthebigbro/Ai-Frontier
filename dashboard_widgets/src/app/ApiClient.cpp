#include "ApiClient.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

ApiClient::ApiClient(const QString& baseUrl, QObject* parent)
    : QObject(parent), m_baseUrl(baseUrl), m_network(new QNetworkAccessManager(this)) {}

// ---------------------------------------------------------------------------
// GET helpers
// ---------------------------------------------------------------------------

void ApiClient::getJson(const QString& path,
                        const std::function<void(const QJsonObject&)>& onSuccess,
                        const std::function<void(const QString&)>& onError) {
    QNetworkRequest req(QUrl(m_baseUrl + path));
    QNetworkReply* reply = m_network->get(req);
    handleReply(reply, onSuccess, onError);
}

void ApiClient::fetchState() {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/state")));
    QNetworkReply* reply = m_network->get(req);
    handleReply(
        reply,
        [this](const QJsonObject& obj) { emit stateReceived(obj); },
        [this](const QString& err) { emit stateFetchFailed(err); });
}

void ApiClient::fetchDiagnostics() {
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/api/diagnostics")));
    QNetworkReply* reply = m_network->get(req);
    handleReply(
        reply,
        [this](const QJsonObject& obj) { emit diagnosticsReceived(obj); });
}

// ---------------------------------------------------------------------------
// POST helpers
// ---------------------------------------------------------------------------

void ApiClient::postAction(const QString& endpoint) {
    QNetworkRequest req(QUrl(m_baseUrl + endpoint));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
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
        reply->deleteLater();

        auto extractErrorMessage = [reply]() -> QString {
            const QByteArray data = reply->readAll();
            if (!data.isEmpty()) {
                QJsonParseError err;
                const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
                if (err.error == QJsonParseError::NoError && doc.isObject()) {
                    const QJsonObject obj = doc.object();
                    const QString fromError = obj.value(QStringLiteral("error")).toString();
                    const QString fromMessage = obj.value(QStringLiteral("message")).toString();
                    if (!fromError.trimmed().isEmpty()) {
                        return fromError;
                    }
                    if (!fromMessage.trimmed().isEmpty()) {
                        return fromMessage;
                    }
                }
                const QString raw = QString::fromUtf8(data).trimmed();
                if (!raw.isEmpty()) {
                    return raw;
                }
            }
            return reply->errorString();
        };

        if (reply->error() != QNetworkReply::NoError) {
            const QString errMsg = extractErrorMessage();
            if (onError)
                onError(errMsg);
            else
                qWarning() << "ApiClient: unhandled network error:" << errMsg;
            return;
        }

        QByteArray data = reply->readAll();
        QJsonParseError parseErr;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseErr);

        if (parseErr.error != QJsonParseError::NoError) {
            if (onError)
                onError(QStringLiteral("JSON parse error: ") + parseErr.errorString());
            else
                qWarning() << "ApiClient: JSON parse error:" << parseErr.errorString();
            return;
        }

        if (onSuccess)
            onSuccess(doc.object());
    });
}
