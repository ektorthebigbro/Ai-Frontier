#pragma once

#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QVector>
#include <functional>

struct HttpRequest {
    QString method;
    QString path;
    QString rawTarget;
    QUrl url;
    QHash<QString, QString> headers;
    QHash<QString, QString> query;
    QString clientAddress;
    QByteArray body;
    bool keepAlive = false;
};

struct HttpResponse {
    int statusCode = 200;
    QByteArray contentType = "application/json; charset=utf-8";
    QByteArray body = "{}";
    QHash<QByteArray, QByteArray> headers;
    bool keepAlive = false;
};

class SimpleHttpServer : public QObject {
    Q_OBJECT

public:
    using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;
    struct RouteEntry {
        QString method;
        QString path;
        QString description;
        bool prefix = false;
    };

    explicit SimpleHttpServer(QObject* parent = nullptr);

    void setHandler(RequestHandler handler);
    void setFallbackHandler(RequestHandler handler);
    void addRoute(const QString& method, const QString& path, RequestHandler handler, const QString& description = QString());
    void addPrefixRoute(const QString& method, const QString& pathPrefix, RequestHandler handler, const QString& description = QString());
    QJsonArray routesJson() const;
    void setMaxBodyBytes(qint64 maxBodyBytes);
    bool listen(const QHostAddress& address, quint16 port);
    QString errorString() const;
    quint16 serverPort() const;

private slots:
    void onNewConnection();

private:
    struct RegisteredRoute {
        QString method;
        QString path;
        QString description;
        bool prefix = false;
        RequestHandler handler;
    };

    void attachSocket(QTcpSocket* socket);
    void tryHandleSocket(QTcpSocket* socket);
    bool tryParseRequest(QTcpSocket* socket, HttpRequest* request, HttpResponse* parseErrorResponse = nullptr);
    HttpResponse dispatch(const HttpRequest& request) const;
    void writeResponse(QTcpSocket* socket, const HttpResponse& response);
    static QByteArray statusReason(int statusCode);

    QTcpServer m_server;
    RequestHandler m_handler;
    RequestHandler m_fallbackHandler;
    QVector<RegisteredRoute> m_routes;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QHash<QTcpSocket*, qint64> m_partialRequestStartedAt;
    qint64 m_maxBodyBytes = 16 * 1024 * 1024;
};
