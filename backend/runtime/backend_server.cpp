#include "../backend.h"
#include "../common/backend_common.h"
#include <QFileInfo>
#include <QJsonDocument>

using namespace ControlCenterBackendCommon;

namespace {

HttpResponse jsonResponse(const QJsonValue& value, int statusCode = 200) {
    HttpResponse response;
    response.statusCode = statusCode;
    response.body = jsonBytes(value);
    return response;
}

QJsonObject parseBodyObject(const HttpRequest& request) {
    return parseJsonObject(request.body);
}

QString requestString(const HttpRequest& request, const QString& key) {
    if (request.query.contains(key)) {
        return request.query.value(key);
    }
    return parseBodyObject(request).value(key).toString();
}

QString routeKey(const QString& method, const QString& path) {
    return method + QStringLiteral(" ") + path;
}

QString displayPathForClient(const QString& rootPath, const QString& rawPath) {
    const QFileInfo info(rawPath);
    const QString absolute = QDir::cleanPath(info.absoluteFilePath());
    const QString cleanRoot = QDir::cleanPath(rootPath);
    if (absolute.startsWith(cleanRoot + QLatin1Char('/')) || absolute.startsWith(cleanRoot + QLatin1Char('\\'))) {
        return QDir(cleanRoot).relativeFilePath(absolute);
    }
    if (absolute == cleanRoot) {
        return QStringLiteral(".");
    }
    return info.fileName().isEmpty() ? absolute : info.fileName();
}

QString routeMetricLookupKey(const QJsonObject& route) {
    const QString method = route.value(QStringLiteral("method")).toString();
    const QString path = route.value(QStringLiteral("path")).toString();
    if (!route.value(QStringLiteral("prefix")).toBool(false)) {
        return routeKey(method, path);
    }
    if (path.endsWith(QLatin1Char('/'))) {
        return routeKey(method, path + QLatin1Char('*'));
    }
    return routeKey(method, path + QStringLiteral("/*"));
}

}  // namespace

void ControlCenterBackend::registerRoutes() {
    auto route = [this](const QString& key, HttpResponse (ControlCenterBackend::*fn)(const HttpRequest&)) {
        return [this, key, fn](const HttpRequest& request) {
            return runInstrumented(request, key, [this, fn, &request]() { return (this->*fn)(request); });
        };
    };
    auto routeNoArgMutable = [this](const QString& key, HttpResponse (ControlCenterBackend::*fn)()) {
        return [this, key, fn](const HttpRequest& request) {
            return runInstrumented(request, key, [this, fn]() { return (this->*fn)(); });
        };
    };
    auto routeConst = [this](const QString& key, HttpResponse (ControlCenterBackend::*fn)(const HttpRequest&) const) {
        return [this, key, fn](const HttpRequest& request) {
            return runInstrumented(request, key, [this, fn, &request]() { return (this->*fn)(request); });
        };
    };
    auto routeNoArg = [this](const QString& key, HttpResponse (ControlCenterBackend::*fn)() const) {
        return [this, key, fn](const HttpRequest& request) {
            return runInstrumented(request, key, [this, fn]() { return (this->*fn)(); });
        };
    };
    auto prefixAction = [this](const QString& key) {
        return [this, key](const HttpRequest& request) {
            return runInstrumented(request, key, [this, &request]() { return handleAction(request.path); });
        };
    };

    m_server.setMaxBodyBytes(32 * 1024 * 1024);
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/"), routeNoArg(routeKey(QStringLiteral("GET"), QStringLiteral("/")), &ControlCenterBackend::handleRoot), QStringLiteral("Backend service root"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/state"), routeNoArgMutable(routeKey(QStringLiteral("GET"), QStringLiteral("/api/state")), &ControlCenterBackend::handleState), QStringLiteral("Dashboard state payload"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/config"), route(routeKey(QStringLiteral("GET"), QStringLiteral("/api/config")), &ControlCenterBackend::handleConfig), QStringLiteral("Get merged runtime config"));
    m_server.addRoute(QStringLiteral("POST"), QStringLiteral("/api/config"), route(routeKey(QStringLiteral("POST"), QStringLiteral("/api/config")), &ControlCenterBackend::handleConfig), QStringLiteral("Save config patch or YAML"));
    m_server.addPrefixRoute(QStringLiteral("POST"), QStringLiteral("/api/actions/"), prefixAction(routeKey(QStringLiteral("POST"), QStringLiteral("/api/actions/*"))), QStringLiteral("Native control actions"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/diagnostics"), route(routeKey(QStringLiteral("GET"), QStringLiteral("/api/diagnostics")), &ControlCenterBackend::handleDiagnostics), QStringLiteral("Runtime diagnostics"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/diagnostics/issue"), route(routeKey(QStringLiteral("GET"), QStringLiteral("/api/diagnostics/issue")), &ControlCenterBackend::handleDiagnostics), QStringLiteral("Diagnostics deep dive for one issue"));
    m_server.addPrefixRoute(QStringLiteral("POST"), QStringLiteral("/api/diagnostics"), route(routeKey(QStringLiteral("POST"), QStringLiteral("/api/diagnostics/*")), &ControlCenterBackend::handleDiagnostics), QStringLiteral("Diagnostics control actions"));
    m_server.addRoute(QStringLiteral("POST"), QStringLiteral("/api/feed/clear"), route(routeKey(QStringLiteral("POST"), QStringLiteral("/api/feed/clear")), &ControlCenterBackend::handleFeedControl), QStringLiteral("Clear live feed events"));
    m_server.addRoute(QStringLiteral("POST"), QStringLiteral("/api/generate"), route(routeKey(QStringLiteral("POST"), QStringLiteral("/api/generate")), &ControlCenterBackend::handleGenerate), QStringLiteral("Generate through managed inference"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/server/meta"), routeNoArg(routeKey(QStringLiteral("GET"), QStringLiteral("/api/server/meta")), &ControlCenterBackend::handleServerMeta), QStringLiteral("Native backend metadata"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/server/health"), routeNoArg(routeKey(QStringLiteral("GET"), QStringLiteral("/api/server/health")), &ControlCenterBackend::handleServerHealth), QStringLiteral("Server health summary"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/server/metrics"), routeNoArg(routeKey(QStringLiteral("GET"), QStringLiteral("/api/server/metrics")), &ControlCenterBackend::handleServerMetrics), QStringLiteral("Server request and route metrics"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/server/routes"), routeNoArg(routeKey(QStringLiteral("GET"), QStringLiteral("/api/server/routes")), &ControlCenterBackend::handleServerRoutes), QStringLiteral("Registered HTTP routes"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/server/processes"), routeNoArgMutable(routeKey(QStringLiteral("GET"), QStringLiteral("/api/server/processes")), &ControlCenterBackend::handleServerProcesses), QStringLiteral("Managed process snapshot"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/server/requests"), routeConst(routeKey(QStringLiteral("GET"), QStringLiteral("/api/server/requests")), &ControlCenterBackend::handleServerRequestLog), QStringLiteral("Recent request log"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/server/logs"), routeConst(routeKey(QStringLiteral("GET"), QStringLiteral("/api/server/logs")), &ControlCenterBackend::handleServerLogs), QStringLiteral("Tail managed process logs"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/server/log-events"), routeConst(routeKey(QStringLiteral("GET"), QStringLiteral("/api/server/log-events")), &ControlCenterBackend::handleServerLogEvents), QStringLiteral("Structured backend log events"));
    m_server.addRoute(QStringLiteral("GET"), QStringLiteral("/api/server/files"), routeNoArgMutable(routeKey(QStringLiteral("GET"), QStringLiteral("/api/server/files")), &ControlCenterBackend::handleServerFiles), QStringLiteral("Project file snapshot"));
    m_server.addRoute(QStringLiteral("POST"), QStringLiteral("/api/server/processes/restart"), route(routeKey(QStringLiteral("POST"), QStringLiteral("/api/server/processes/restart")), &ControlCenterBackend::handleServerRestartProcess), QStringLiteral("Restart one managed process"));
    m_server.addRoute(QStringLiteral("POST"), QStringLiteral("/api/server/processes/clear-log"), route(routeKey(QStringLiteral("POST"), QStringLiteral("/api/server/processes/clear-log")), &ControlCenterBackend::handleServerClearProcessLog), QStringLiteral("Clear one managed process log"));
}

HttpResponse ControlCenterBackend::handleServerMeta() const {
    const qint64 uptimeSeconds = m_startedAt.secsTo(QDateTime::currentDateTimeUtc());
    const QString baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    return jsonResponse(QJsonObject{
        {QStringLiteral("service"), QStringLiteral("AI Frontier Native Backend API")},
        {QStringLiteral("version"), QStringLiteral("2.6.0")},
        {QStringLiteral("uptime_seconds"), static_cast<double>(uptimeSeconds)},
        {QStringLiteral("started_at"), m_startedAt.toString(Qt::ISODate)},
        {QStringLiteral("request_count"), static_cast<double>(m_requestCount)},
        {QStringLiteral("base_url"), baseUrl},
        {QStringLiteral("project_root"), QStringLiteral(".")},
        {QStringLiteral("config_path"), displayPathForClient(m_rootPath, m_configPath)},
        {QStringLiteral("config_backup_path"), displayPathForClient(m_rootPath, m_configBackupPath)},
        {QStringLiteral("feed_path"), displayPathForClient(m_rootPath, m_feedPath)},
        {QStringLiteral("backend_log_path"), displayPathForClient(m_rootPath, m_backendLogPath)},
        {QStringLiteral("checkpoint_dir"), displayPathForClient(m_rootPath, m_checkpointDir)},
    });
}

HttpResponse ControlCenterBackend::handleServerHealth() const {
    return jsonResponse(buildServerHealthPayload());
}

HttpResponse ControlCenterBackend::handleServerMetrics() const {
    return jsonResponse(buildServerMetricsPayload());
}

HttpResponse ControlCenterBackend::handleServerRoutes() const {
    QJsonArray routes = m_server.routesJson();
    for (int i = 0; i < routes.size(); ++i) {
        QJsonObject route = routes.at(i).toObject();
        const QString key = routeMetricLookupKey(route);
        if (m_routeMetrics.contains(key)) {
            const BackendRouteMetric metric = m_routeMetrics.value(key);
            route.insert(QStringLiteral("hits"), static_cast<double>(metric.hits));
            route.insert(QStringLiteral("error_hits"), static_cast<double>(metric.errorHits));
            route.insert(QStringLiteral("avg_latency_ms"), metric.avgLatencyMs);
            route.insert(QStringLiteral("last_latency_ms"), metric.lastLatencyMs);
            route.insert(QStringLiteral("last_status_code"), metric.lastStatusCode);
            route.insert(QStringLiteral("last_seen"), metric.lastSeen);
        }
        routes[i] = route;
    }
    return jsonResponse(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("routes"), routes},
    });
}

HttpResponse ControlCenterBackend::handleServerProcesses() {
    refreshRecoveredProcessStates();
    restoreAutopilotRuntimeState();
    QJsonObject processes;
    for (auto it = m_processes.begin(); it != m_processes.end(); ++it) {
        processes.insert(it.key(), buildProcessSnapshot(it.key()));
    }
    return jsonResponse(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("processes"), processes},
    });
}

HttpResponse ControlCenterBackend::handleServerRequestLog(const HttpRequest& request) const {
    const int rawLimit = request.query.value(QStringLiteral("limit")).toInt();
    const int limit = qBound(5, rawLimit > 0 ? rawLimit : 50, 200);
    return jsonResponse(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("requests"), buildRecentRequestRows(limit)},
    });
}

HttpResponse ControlCenterBackend::handleServerLogs(const HttpRequest& request) const {
    const QString name = request.query.value(QStringLiteral("name"));
    const int rawLines = request.query.value(QStringLiteral("lines")).toInt();
    const int lines = qBound(1, rawLines > 0 ? rawLines : 30, 200);
    if (name.isEmpty() || !m_processes.contains(name)) {
        return jsonResponse(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Unknown or missing process name")},
        }, 400);
    }

    const ManagedProcessState state = m_processes.value(name);
    QStringList tail = state.logLines;
    if (tail.size() > lines) {
        tail = tail.mid(tail.size() - lines);
    }
    return jsonResponse(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("name"), name},
        {QStringLiteral("log"), QJsonArray::fromStringList(tail)},
    });
}

HttpResponse ControlCenterBackend::handleServerLogEvents(const HttpRequest& request) const {
    const int rawLogLimit = request.query.value(QStringLiteral("limit")).toInt();
    const int limit = qBound(10, rawLogLimit > 0 ? rawLogLimit : 40, 300);
    return jsonResponse(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("summary"), buildLogSummary()},
        {QStringLiteral("logs"), recentBackendLogRows(limit)},
    });
}

HttpResponse ControlCenterBackend::handleServerFiles() {
    const QString latestCkpt = latestCheckpointPath();
    const QJsonObject history = buildHistoryPayload();
    return jsonResponse(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("files"), QJsonObject{
             {QStringLiteral("config_path"), displayPathForClient(m_rootPath, m_configPath)},
             {QStringLiteral("config_backup_path"), displayPathForClient(m_rootPath, m_configBackupPath)},
             {QStringLiteral("feed_path"), displayPathForClient(m_rootPath, m_feedPath)},
             {QStringLiteral("backend_log_path"), displayPathForClient(m_rootPath, m_backendLogPath)},
             {QStringLiteral("report_dir"), displayPathForClient(m_rootPath, m_reportDir)},
             {QStringLiteral("checkpoint_dir"), displayPathForClient(m_rootPath, m_checkpointDir)},
             {QStringLiteral("latest_checkpoint"), displayPathForClient(m_rootPath, latestCkpt)},
             {QStringLiteral("latest_report_available"), !latestReportText().isEmpty()},
             {QStringLiteral("model_cache"), buildModelCacheSummary()},
         }},
        {QStringLiteral("history"), history},
    });
}

HttpResponse ControlCenterBackend::handleServerRestartProcess(const HttpRequest& request) {
    const QString name = requestString(request, QStringLiteral("name"));
    if (name.isEmpty() || !m_processes.contains(name) || name == QStringLiteral("autopilot")) {
        return jsonResponse(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Unknown or unsupported process name")},
        }, 400);
    }
    if (!restartManagedProcess(name)) {
        return jsonResponse(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Could not restart process")},
        }, 409);
    }
    return jsonResponse(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("message"), QStringLiteral("%1 restarted").arg(name)},
    });
}

HttpResponse ControlCenterBackend::handleServerClearProcessLog(const HttpRequest& request) {
    const QString name = requestString(request, QStringLiteral("name"));
    if (name.isEmpty() || !m_processes.contains(name)) {
        return jsonResponse(QJsonObject{
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"), QStringLiteral("Unknown or missing process name")},
        }, 400);
    }
    m_processes[name].logLines.clear();
    invalidateRuntimeCaches();
    return jsonResponse(QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("message"), QStringLiteral("%1 log cleared").arg(name)},
    });
}
