#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <functional>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(const QString& baseUrl, QObject* parent = nullptr);

    void getJson(const QString& path,
                 const std::function<void(const QJsonObject&)>& onSuccess,
                 const std::function<void(const QString&)>& onError = nullptr);
    void fetchState();
    void fetchDiagnostics();
    void postAction(const QString& endpoint);
    void postConfig(const QJsonObject& config);
    void postConfigYaml(const QString& yaml);
    void postGenerate(const QJsonObject& payload);
    void postDiagnosticsReload(const QString& moduleName);
    void postDiagnosticsClear(const QString& key = QString());
    void postDiagnosticsRunChecks();
    void postDiagnosticsSelfHeal(bool aggressive = false);

    QString baseUrl() const { return m_baseUrl; }
    QNetworkAccessManager* network() const { return m_network; }

signals:
    void stateReceived(const QJsonObject& state);
    void stateFetchFailed(const QString& error);
    void diagnosticsReceived(const QJsonObject& diagnostics);
    void actionDone(const QJsonObject& response);
    void generateDone(const QJsonObject& response);
    void configSaved(bool ok, const QString& message);

private:
    QString m_baseUrl;
    QNetworkAccessManager* m_network;

    void handleReply(QNetworkReply* reply, const std::function<void(const QJsonObject&)>& onSuccess,
                     const std::function<void(const QString&)>& onError = nullptr);
};

#endif
