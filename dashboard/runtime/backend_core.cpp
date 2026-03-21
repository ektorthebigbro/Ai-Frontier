#include "../backend.h"
#include "../common/backend_common.h"
#include <exception>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QUrl>

using namespace ControlCenterBackendCommon;

namespace {

HttpResponse jsonResponse(const QJsonValue& value, int statusCode = 200) {
    HttpResponse response;
    response.statusCode = statusCode;
    response.body = jsonBytes(value);
    return response;
}

HttpResponse errorResponse(const QString& message, int statusCode = 400) {
    return jsonResponse(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), message},
    }, statusCode);
}

bool isTrustedLoopbackOrigin(const QString& rawHeader) {
    const QString trimmed = rawHeader.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }
    const QUrl url(trimmed);
    if (!url.isValid()) {
        return false;
    }
    const QString host = url.host().toLower();
    return host == QStringLiteral("127.0.0.1")
        || host == QStringLiteral("localhost")
        || host == QStringLiteral("[::1]")
        || host == QStringLiteral("::1");
}

bool requiresBrowserTrustCheck(const HttpRequest& request) {
    if (request.method != QStringLiteral("GET") && request.method != QStringLiteral("HEAD")) {
        return true;
    }
    return request.path == QStringLiteral("/api/state")
        || request.path == QStringLiteral("/api/config")
        || request.path.startsWith(QStringLiteral("/api/server"));
}

bool isMutationRequest(const HttpRequest& request) {
    return request.method != QStringLiteral("GET") && request.method != QStringLiteral("HEAD");
}

}  // namespace

ControlCenterBackend::ControlCenterBackend(QObject* parent)
    : QObject(parent) {
    initializePaths();
    initializeProcessCatalog();
    ensureAutopilotState();
    m_startedAt = QDateTime::currentDateTimeUtc();
    refreshRecoveredProcessStates();
    restoreAutopilotRuntimeState();

    m_reloadableModules = {
        QStringLiteral("frontier.hardware"),
        QStringLiteral("frontier.config"),
        QStringLiteral("frontier.utils"),
        QStringLiteral("frontier.model_management"),
        QStringLiteral("frontier.modeling"),
        QStringLiteral("frontier.data"),
        QStringLiteral("dataset_pipeline.build_dataset"),
        QStringLiteral("frontier.judging"),
    };

    registerRoutes();
    m_server.setFallbackHandler([this](const HttpRequest& request) { return handleRequest(request); });
    loadConfigFromDisk(true);
    recordLog(QStringLiteral("info"),
              QStringLiteral("backend"),
              QStringLiteral("startup"),
              QStringLiteral("Native backend initialized"),
              QJsonObject{
                  {QStringLiteral("version"), QCoreApplication::applicationVersion()},
                  {QStringLiteral("project_root"), m_rootPath},
              });
}

bool ControlCenterBackend::start(quint16 port) {
    if (m_config.isEmpty()) {
        qCritical() << "Native backend refused to start because configuration is unavailable";
        recordLog(QStringLiteral("error"),
                  QStringLiteral("backend"),
                  QStringLiteral("config_unavailable"),
                  QStringLiteral("Native backend refused to start without a valid config"));
        return false;
    }
    if (!m_server.listen(QHostAddress::LocalHost, port)) {
        qCritical() << "Native backend failed to listen on port" << port << ":" << m_server.errorString();
        recordLog(QStringLiteral("error"),
                  QStringLiteral("backend"),
                  QStringLiteral("listen"),
                  QStringLiteral("Native backend failed to listen"),
                  QJsonObject{
                      {QStringLiteral("port"), static_cast<int>(port)},
                      {QStringLiteral("error"), m_server.errorString()},
                  });
        return false;
    }
    const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    qInfo().noquote() << "AI Frontier native backend listening on" << baseUrl;
    recordLog(QStringLiteral("info"),
              QStringLiteral("backend"),
              QStringLiteral("listen"),
              QStringLiteral("Native backend listening"),
              QJsonObject{
                  {QStringLiteral("port"), static_cast<int>(m_server.serverPort())},
                  {QStringLiteral("base_url"), baseUrl},
              });
    return true;
}

HttpResponse ControlCenterBackend::handleRequest(const HttpRequest& request) {
    return runInstrumented(request, QStringLiteral("UNMATCHED %1").arg(request.path), [this]() {
        return handleNotFound();
    });
}

void ControlCenterBackend::initializePaths() {
    m_rootPath = projectRootPath();
    m_configPath = rootPathFor(QStringLiteral("configs/default.yaml"));
    m_configBackupPath = rootPathFor(QStringLiteral("configs/default.yaml.bak"));
    m_feedPath = rootPathFor(QStringLiteral("logs/dashboard_metrics.jsonl"));
    m_backendLogPath = rootPathFor(QStringLiteral("logs/native_backend_log.jsonl"));
    m_runtimeStateDir = rootPathFor(QStringLiteral(".tmp/runtime_state"));
    m_reportDir = rootPathFor(QStringLiteral("artifacts"));
    m_checkpointDir = rootPathFor(QStringLiteral("checkpoints"));
    m_setupScript = launcherScriptPath();
    ensureDir(rootPathFor(QStringLiteral("logs")));
    ensureDir(rootPathFor(QStringLiteral(".tmp")));
    ensureDir(m_runtimeStateDir);
}

void ControlCenterBackend::initializeProcessCatalog() {
    const QString root = m_rootPath;
    m_processes.insert(QStringLiteral("setup"),
                       {QStringLiteral("setup"),
                        QStringLiteral("Environment Setup"),
                        m_setupScript,
                        {QStringLiteral("setup")}});
    m_processes.insert(QStringLiteral("prepare"), {QStringLiteral("prepare"), QStringLiteral("Data Preparation"), QDir(root).absoluteFilePath(QStringLiteral("dataset_pipeline/build_dataset.py")), {QStringLiteral("--config"), m_configPath}});
    m_processes.insert(QStringLiteral("training"), {QStringLiteral("training"), QStringLiteral("Model Training"), QDir(root).absoluteFilePath(QStringLiteral("training/train_system.py")), {QStringLiteral("--config"), m_configPath}});
    m_processes.insert(QStringLiteral("evaluate"), {QStringLiteral("evaluate"), QStringLiteral("Evaluation"), QDir(root).absoluteFilePath(QStringLiteral("evaluation/evaluate.py")), {QStringLiteral("--config"), m_configPath}});
    m_processes.insert(QStringLiteral("inference"), {QStringLiteral("inference"), QStringLiteral("Inference Server"), QDir(root).absoluteFilePath(QStringLiteral("inference/server.py")), {QStringLiteral("--config"), m_configPath}});
    m_processes.insert(QStringLiteral("autopilot"), {QStringLiteral("autopilot"), QStringLiteral("Autopilot"), QString(), {}});
}

quint16 ControlCenterBackend::configuredPort() const {
    const QJsonObject dashboard = m_config.value(QStringLiteral("dashboard")).toObject();
    const int port = dashboard.value(QStringLiteral("port")).toInt(8765);
    return (port > 0 && port <= 65535) ? static_cast<quint16>(port) : 8765;
}

HttpResponse ControlCenterBackend::handleRoot() const {
    return jsonResponse(QJsonObject{
        {QStringLiteral("service"), QStringLiteral("AI Frontier Native Backend API")},
        {QStringLiteral("dashboard"), QStringLiteral("qt-only")},
        {QStringLiteral("version"), QCoreApplication::applicationVersion()},
        {QStringLiteral("state_endpoint"), QStringLiteral("/api/state")},
        {QStringLiteral("diagnostics_endpoint"), QStringLiteral("/api/diagnostics")},
        {QStringLiteral("config_path"), m_configPath},
        {QStringLiteral("routes_endpoint"), QStringLiteral("/api/server/routes")},
    });
}

HttpResponse ControlCenterBackend::handleNotFound() const {
    HttpResponse response;
    response.statusCode = 404;
    response.body = jsonBytes(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QStringLiteral("Route not found")},
    });
    return response;
}

HttpResponse ControlCenterBackend::handleMethodNotAllowed() const {
    HttpResponse response;
    response.statusCode = 405;
    response.body = jsonBytes(QJsonObject{
        {QStringLiteral("ok"), false},
        {QStringLiteral("error"), QStringLiteral("Method not allowed")},
    });
    return response;
}

HttpResponse ControlCenterBackend::runInstrumented(const HttpRequest& request,
                                                  const QString& routeKey,
                                                  const std::function<HttpResponse()>& handler) {
    ++m_requestCount;

    if (requiresBrowserTrustCheck(request)) {
        const QString origin = request.headers.value(QStringLiteral("origin"));
        const QString referer = request.headers.value(QStringLiteral("referer"));
        const QString fetchSite = request.headers.value(QStringLiteral("sec-fetch-site")).trimmed().toLower();
        const bool trustedOrigin = isTrustedLoopbackOrigin(origin);
        const bool trustedReferer = isTrustedLoopbackOrigin(referer);
        const bool trustedFetchSite = fetchSite.isEmpty()
            || fetchSite == QStringLiteral("same-origin")
            || fetchSite == QStringLiteral("same-site")
            || fetchSite == QStringLiteral("none");
        if (!trustedOrigin || !trustedReferer || !trustedFetchSite) {
            HttpResponse forbidden = errorResponse(QStringLiteral("Cross-origin requests are not allowed"), 403);
            forbidden.keepAlive = false;
            recordRequestMetric(routeKey, request, forbidden.statusCode, forbidden.body.size(), 0.0);
            return forbidden;
        }
    }
    if (!m_exclusiveOperationName.isEmpty() && isMutationRequest(request)) {
        HttpResponse busy = errorResponse(QStringLiteral("Backend is busy with %1").arg(m_exclusiveOperationName), 409);
        busy.keepAlive = false;
        recordRequestMetric(routeKey, request, busy.statusCode, busy.body.size(), 0.0);
        return busy;
    }

    QElapsedTimer timer;
    timer.start();

    HttpResponse response;
    try {
        response = handler ? handler() : handleNotFound();
    } catch (const std::exception& ex) {
        const QString error = QString::fromUtf8(ex.what());
        recordIssue(QStringLiteral("native.backend"), error);
        recordLog(QStringLiteral("error"),
                  QStringLiteral("backend"),
                  QStringLiteral("exception"),
                  QStringLiteral("Unhandled backend exception"),
                  QJsonObject{
                      {QStringLiteral("route"), routeKey},
                      {QStringLiteral("method"), request.method},
                      {QStringLiteral("path"), request.path},
                      {QStringLiteral("error"), error},
                  });
        response = errorResponse(QStringLiteral("Unhandled backend exception: %1").arg(error), 500);
    } catch (...) {
        recordIssue(QStringLiteral("native.backend"), QStringLiteral("Unhandled unknown backend exception"));
        recordLog(QStringLiteral("error"),
                  QStringLiteral("backend"),
                  QStringLiteral("exception"),
                  QStringLiteral("Unhandled unknown backend exception"),
                  QJsonObject{
                      {QStringLiteral("route"), routeKey},
                      {QStringLiteral("method"), request.method},
                      {QStringLiteral("path"), request.path},
                  });
        response = errorResponse(QStringLiteral("Unhandled backend exception"), 500);
    }

    response.keepAlive = request.keepAlive;
    recordRequestMetric(routeKey, request, response.statusCode, response.body.size(), static_cast<double>(timer.elapsed()));
    if (response.statusCode >= 500) {
        recordLog(QStringLiteral("error"),
                  QStringLiteral("http"),
                  QStringLiteral("response"),
                  QStringLiteral("Request completed with server error"),
                  QJsonObject{
                      {QStringLiteral("route"), routeKey},
                      {QStringLiteral("status_code"), response.statusCode},
                      {QStringLiteral("latency_ms"), static_cast<double>(timer.elapsed())},
                  });
    } else if (timer.elapsed() >= 1500) {
        recordLog(QStringLiteral("warning"),
                  QStringLiteral("http"),
                  QStringLiteral("slow_request"),
                  QStringLiteral("Slow request observed"),
                  QJsonObject{
                      {QStringLiteral("route"), routeKey},
                      {QStringLiteral("status_code"), response.statusCode},
                      {QStringLiteral("latency_ms"), static_cast<double>(timer.elapsed())},
                  });
    }
    return response;
}
