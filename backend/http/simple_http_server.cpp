#include "simple_http_server.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QRegularExpression>
#include <QUrlQuery>

namespace {

QString normalizedHeader(const QString& name) {
    return name.trimmed().toLower();
}

bool matchesPrefixPath(const QString& requestPath, const QString& routePath) {
    if (requestPath == routePath) {
        return true;
    }
    if (routePath.endsWith(QLatin1Char('/'))) {
        return requestPath.startsWith(routePath);
    }
    return requestPath.startsWith(routePath + QLatin1Char('/'));
}

HttpResponse parseErrorResponse(const QString& message, int statusCode = 400) {
    HttpResponse response;
    response.statusCode = statusCode;
    response.keepAlive = false;
    response.body = QJsonDocument(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), message},
    }).toJson(QJsonDocument::Compact);
    return response;
}

}  // namespace

SimpleHttpServer::SimpleHttpServer(QObject* parent)
    : QObject(parent) {
    connect(&m_server, &QTcpServer::newConnection, this, &SimpleHttpServer::onNewConnection);
}

void SimpleHttpServer::setHandler(RequestHandler handler) {
    m_handler = std::move(handler);
}

void SimpleHttpServer::setFallbackHandler(RequestHandler handler) {
    m_fallbackHandler = std::move(handler);
}

void SimpleHttpServer::addRoute(const QString& method, const QString& path, RequestHandler handler, const QString& description) {
    m_routes.push_back({method.trimmed().toUpper(), path, description, false, std::move(handler)});
}

void SimpleHttpServer::addPrefixRoute(const QString& method, const QString& pathPrefix, RequestHandler handler, const QString& description) {
    m_routes.push_back({method.trimmed().toUpper(), pathPrefix, description, true, std::move(handler)});
}

QJsonArray SimpleHttpServer::routesJson() const {
    QJsonArray routes;
    for (const RegisteredRoute& route : m_routes) {
        routes.append(QJsonObject{
            {QStringLiteral("method"), route.method},
            {QStringLiteral("path"), route.path},
            {QStringLiteral("description"), route.description},
            {QStringLiteral("prefix"), route.prefix},
        });
    }
    return routes;
}

void SimpleHttpServer::setMaxBodyBytes(qint64 maxBodyBytes) {
    m_maxBodyBytes = qMax<qint64>(1024, maxBodyBytes);
}

bool SimpleHttpServer::listen(const QHostAddress& address, quint16 port) {
    return m_server.listen(address, port);
}

QString SimpleHttpServer::errorString() const {
    return m_server.errorString();
}

quint16 SimpleHttpServer::serverPort() const {
    return m_server.serverPort();
}

void SimpleHttpServer::onNewConnection() {
    while (QTcpSocket* socket = m_server.nextPendingConnection()) {
        attachSocket(socket);
    }
}

void SimpleHttpServer::attachSocket(QTcpSocket* socket) {
    m_buffers.insert(socket, QByteArray{});
    m_partialRequestStartedAt.insert(socket, 0);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { tryHandleSocket(socket); });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        m_buffers.remove(socket);
        m_partialRequestStartedAt.remove(socket);
        socket->deleteLater();
    });
}

void SimpleHttpServer::tryHandleSocket(QTcpSocket* socket) {
    if (!socket || !m_buffers.contains(socket)) {
        return;
    }
    QByteArray& buf = m_buffers[socket];
    const bool wasEmpty = buf.isEmpty();
    buf.append(socket->readAll());
    if (wasEmpty && !buf.isEmpty()) {
        m_partialRequestStartedAt[socket] = QDateTime::currentMSecsSinceEpoch();
    }

    // Reject oversized headers (64 KB limit before header terminator)
    if (buf.indexOf("\r\n\r\n") < 0 && buf.size() > 65536) {
        writeResponse(socket, parseErrorResponse(QStringLiteral("Request headers too large"), 400));
        return;
    }
    if (!buf.isEmpty()) {
        const qint64 startedAt = m_partialRequestStartedAt.value(socket, 0);
        if (startedAt > 0 && (QDateTime::currentMSecsSinceEpoch() - startedAt) > 15000) {
            m_buffers[socket].clear();
            writeResponse(socket, parseErrorResponse(QStringLiteral("Request timed out"), 408));
            return;
        }
    }

    HttpRequest request;
    HttpResponse parseError;
    if (!tryParseRequest(socket, &request, &parseError)) {
        if (parseError.statusCode > 0) {
            writeResponse(socket, parseError);
        }
        return;
    }
    if (m_buffers.contains(socket)) {
        if (m_buffers[socket].isEmpty()) {
            m_partialRequestStartedAt[socket] = 0;
        } else {
            m_partialRequestStartedAt[socket] = QDateTime::currentMSecsSinceEpoch();
        }
    }

    const HttpResponse response = dispatch(request);
    writeResponse(socket, response);

    // Handle pipelined requests: if buffer still has data, process next request
    if (response.keepAlive && m_buffers.contains(socket) && !m_buffers[socket].isEmpty()) {
        QMetaObject::invokeMethod(this, [this, socket]() { tryHandleSocket(socket); }, Qt::QueuedConnection);
    }
}

bool SimpleHttpServer::tryParseRequest(QTcpSocket* socket, HttpRequest* request, HttpResponse* parseErrorResponseOut) {
    QByteArray& buffer = m_buffers[socket];
    const int headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return false;
    }

    const QByteArray headerBytes = buffer.left(headerEnd);
    const QList<QByteArray> lines = headerBytes.split('\n');
    if (lines.isEmpty()) {
        buffer.clear();
        if (parseErrorResponseOut) {
            *parseErrorResponseOut = parseErrorResponse(QStringLiteral("Malformed request"));
        }
        return false;
    }

    const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
    if (requestLine.size() != 3 || !requestLine.at(2).startsWith("HTTP/1.")) {
        buffer.clear();
        if (parseErrorResponseOut) {
            *parseErrorResponseOut = parseErrorResponse(QStringLiteral("Malformed request line"));
        }
        return false;
    }

    qint64 contentLength = 0;
    bool sawContentLength = false;
    bool sawTransferEncoding = false;
    QHash<QString, QString> headers;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray raw = lines.at(i).trimmed();
        const int colon = raw.indexOf(':');
        if (colon < 0) {
            buffer.clear();
            if (parseErrorResponseOut) {
                *parseErrorResponseOut = parseErrorResponse(QStringLiteral("Malformed request header"));
            }
            return false;
        }
        const QString key = normalizedHeader(QString::fromUtf8(raw.left(colon)));
        const QString value = QString::fromUtf8(raw.mid(colon + 1)).trimmed();
        if (headers.contains(key) && key != QStringLiteral("content-length")) {
            headers.insert(key, value);
        } else {
            headers.insert(key, value);
        }
        if (key == QStringLiteral("content-length")) {
            bool ok = false;
            const qint64 parsedLength = value.toLongLong(&ok);
            const QRegularExpression digitsOnly(QStringLiteral("^\\d+$"));
            if (!ok || !digitsOnly.match(value).hasMatch() || parsedLength < 0) {
                buffer.clear();
                if (parseErrorResponseOut) {
                    *parseErrorResponseOut = parseErrorResponse(QStringLiteral("Invalid Content-Length header"));
                }
                return false;
            }
            if (sawContentLength && parsedLength != contentLength) {
                buffer.clear();
                if (parseErrorResponseOut) {
                    *parseErrorResponseOut = parseErrorResponse(QStringLiteral("Conflicting Content-Length headers"));
                }
                return false;
            }
            contentLength = parsedLength;
            sawContentLength = true;
        } else if (key == QStringLiteral("transfer-encoding") && !value.isEmpty()) {
            sawTransferEncoding = true;
        }
    }
    if (sawTransferEncoding) {
        buffer.clear();
        if (parseErrorResponseOut) {
            *parseErrorResponseOut = parseErrorResponse(QStringLiteral("Transfer-Encoding is not supported"));
        }
        return false;
    }

    if (contentLength > m_maxBodyBytes) {
        buffer.clear();
        if (parseErrorResponseOut) {
            *parseErrorResponseOut = parseErrorResponse(QStringLiteral("Request body too large"), 413);
        }
        return false;
    }

    const qint64 totalSize = static_cast<qint64>(headerEnd) + 4 + contentLength;
    if (buffer.size() < totalSize) {
        return false;
    }

    request->method = QString::fromUtf8(requestLine.at(0)).trimmed().toUpper();
    request->rawTarget = QString::fromUtf8(requestLine.at(1)).trimmed();
    request->url = QUrl::fromEncoded(requestLine.at(1));
    request->path = request->url.path().isEmpty() ? request->rawTarget : request->url.path();
    request->headers = headers;
    const QUrlQuery query(request->url);
    for (const auto& item : query.queryItems()) {
        request->query.insert(item.first, item.second);
    }
    request->clientAddress = socket->peerAddress().toString();
    const QString connectionHeader = request->headers.value(QStringLiteral("connection"));
    const bool http11 = requestLine.at(2).trimmed() == "HTTP/1.1";
    request->keepAlive = http11
        ? connectionHeader.compare(QStringLiteral("close"), Qt::CaseInsensitive) != 0
        : connectionHeader.compare(QStringLiteral("keep-alive"), Qt::CaseInsensitive) == 0;
    request->body = buffer.mid(headerEnd + 4, static_cast<int>(contentLength));
    buffer.remove(0, static_cast<int>(totalSize));
    return true;
}

HttpResponse SimpleHttpServer::dispatch(const HttpRequest& request) const {
    if (request.method == QStringLiteral("ERROR") && request.path == QStringLiteral("/too-large")) {
        HttpResponse response;
        response.statusCode = 400;
        response.body = QByteArray("{\"ok\":false,\"error\":\"Request body too large\"}");
        return response;
    }

    QStringList allowedMethods;
    for (const RegisteredRoute& route : m_routes) {
        const bool pathMatch = route.prefix ? matchesPrefixPath(request.path, route.path) : request.path == route.path;
        if (!pathMatch) {
            continue;
        }
        allowedMethods.append(route.method);
        if (request.method == QStringLiteral("OPTIONS")) {
            continue;
        }
        const bool methodMatch = route.method == request.method || (request.method == QStringLiteral("HEAD") && route.method == QStringLiteral("GET"));
        if (!methodMatch) {
            continue;
        }
        if (route.handler) {
            HttpResponse response = route.handler(request);
            if (request.method == QStringLiteral("HEAD")) {
                response.body.clear();
            }
            return response;
        }
    }

    if (!allowedMethods.isEmpty()) {
        if (request.method == QStringLiteral("OPTIONS")) {
            HttpResponse response;
            response.statusCode = 204;
            response.contentType = "text/plain; charset=utf-8";
            response.headers.insert("Allow", allowedMethods.join(", ").toUtf8());
            response.body.clear();
            return response;
        }
        // Path matched but method did not -- return 405
        HttpResponse response;
        response.statusCode = 405;
        response.contentType = "application/json; charset=utf-8";
        response.headers.insert("Allow", allowedMethods.join(", ").toUtf8());
        response.body = "{\"ok\":false,\"error\":\"Method not allowed\"}";
        return response;
    }

    if (m_fallbackHandler) {
        return m_fallbackHandler(request);
    }
    if (m_handler) {
        return m_handler(request);
    }
    HttpResponse response;
    response.statusCode = 500;
    response.contentType = "text/plain; charset=utf-8";
    response.body = "No handler";
    return response;
}

void SimpleHttpServer::writeResponse(QTcpSocket* socket, const HttpResponse& response) {
    QByteArray raw;
    raw += "HTTP/1.1 " + QByteArray::number(response.statusCode) + " " + statusReason(response.statusCode) + "\r\n";
    raw += "Date: " + QDateTime::currentDateTimeUtc().toString(Qt::RFC2822Date).toUtf8() + "\r\n";
    raw += "Server: ai-frontier-native-backend\r\n";
    raw += "Content-Type: " + response.contentType + "\r\n";
    raw += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    raw += QByteArray("Connection: ") + (response.keepAlive ? "keep-alive" : "close") + "\r\n";
    raw += "Cache-Control: no-store\r\n";
    raw += "Access-Control-Allow-Origin: http://127.0.0.1\r\n";
    raw += "Access-Control-Allow-Headers: content-type\r\n";
    raw += "Access-Control-Allow-Methods: GET, POST, OPTIONS, HEAD\r\n";
    for (auto it = response.headers.begin(); it != response.headers.end(); ++it) {
        if (it.key().contains('\r') || it.key().contains('\n')
            || it.value().contains('\r') || it.value().contains('\n')) {
            continue;
        }
        raw += it.key() + ": " + it.value() + "\r\n";
    }
    raw += "\r\n";
    raw += response.body;
    socket->write(raw);
    if (response.keepAlive) {
        socket->flush();
        return;
    }
    socket->disconnectFromHost();
}

QByteArray SimpleHttpServer::statusReason(int statusCode) {
    switch (statusCode) {
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 408: return "Request Timeout";
    case 413: return "Payload Too Large";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 422: return "Unprocessable Entity";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "OK";
    }
}
