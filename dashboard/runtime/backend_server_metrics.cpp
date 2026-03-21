#include "../backend.h"
#include "../common/backend_common.h"
#include <algorithm>

using namespace ControlCenterBackendCommon;

void ControlCenterBackend::recordRequestMetric(const QString& routeKey,
                                               const HttpRequest& request,
                                               int statusCode,
                                               qint64 responseBytes,
                                               double latencyMs) {
    BackendRouteMetric metric = m_routeMetrics.value(routeKey);
    if (metric.key.isEmpty()) {
        metric.key = routeKey;
        metric.method = request.method;
        metric.path = request.path;
    }

    const qint64 nextHits = metric.hits + 1;
    metric.avgLatencyMs = nextHits <= 1
        ? latencyMs
        : ((metric.avgLatencyMs * static_cast<double>(metric.hits)) + latencyMs) / static_cast<double>(nextHits);
    metric.hits = nextHits;
    if (statusCode >= 400) {
        ++metric.errorHits;
    }
    metric.lastStatusCode = statusCode;
    metric.lastLatencyMs = latencyMs;
    metric.lastSeen = QDateTime::currentSecsSinceEpoch();
    m_routeMetrics.insert(routeKey, metric);

    m_recentRequests.append(BackendRequestLogEntry{
        static_cast<double>(QDateTime::currentSecsSinceEpoch()),
        request.method,
        request.path,
        routeKey,
        request.clientAddress,
        statusCode,
        responseBytes,
        latencyMs,
    });
    if (m_recentRequests.size() > 200) {
        m_recentRequests = m_recentRequests.mid(m_recentRequests.size() - 200);
    }
}

QJsonArray ControlCenterBackend::buildRecentRequestRows(int maxRows) const {
    QJsonArray rows;
    const int start = qMax(0, m_recentRequests.size() - maxRows);
    for (int i = m_recentRequests.size() - 1; i >= start; --i) {
        const BackendRequestLogEntry& entry = m_recentRequests.at(i);
        rows.append(QJsonObject{
            {QStringLiteral("ts"), entry.ts},
            {QStringLiteral("method"), entry.method},
            {QStringLiteral("path"), entry.path},
            {QStringLiteral("route_key"), entry.routeKey},
            {QStringLiteral("client"), entry.clientAddress},
            {QStringLiteral("status_code"), entry.statusCode},
            {QStringLiteral("response_bytes"), static_cast<double>(entry.responseBytes)},
            {QStringLiteral("latency_ms"), entry.latencyMs},
        });
    }
    return rows;
}

QJsonObject ControlCenterBackend::buildServerHealthPayload() const {
    int errorRoutes = 0;
    int runningProcesses = 0;
    for (auto it = m_routeMetrics.begin(); it != m_routeMetrics.end(); ++it) {
        if (it->lastStatusCode >= 400) {
            ++errorRoutes;
        }
    }
    for (auto it = m_processes.begin(); it != m_processes.end(); ++it) {
        const ManagedProcessState& state = it.value();
        if (it.key() == QStringLiteral("autopilot")) {
            if (m_autopilot.value(QStringLiteral("active")).toBool(false)) {
                ++runningProcesses;
            }
            continue;
        }
        if (isManagedProcessRunning(state)) {
            ++runningProcesses;
        }
    }

    const QJsonArray healthChecks = const_cast<ControlCenterBackend*>(this)->runHealthChecks();
    QString overall = QStringLiteral("ok");
    for (const QJsonValue& value : healthChecks) {
        const QString status = value.toObject().value(QStringLiteral("status")).toString();
        if (status == QStringLiteral("error")) {
            overall = QStringLiteral("error");
            break;
        }
        if (status == QStringLiteral("warning")) {
            overall = QStringLiteral("warning");
        }
    }

    return QJsonObject{
        {QStringLiteral("ok"), overall != QStringLiteral("error")},
        {QStringLiteral("status"), overall},
        {QStringLiteral("issue_count"), currentIssues().size()},
        {QStringLiteral("log_summary"), buildLogSummary()},
        {QStringLiteral("running_processes"), runningProcesses},
        {QStringLiteral("error_routes"), errorRoutes},
        {QStringLiteral("checkpoint_available"), !latestCheckpointPath().isEmpty()},
        {QStringLiteral("checks"), healthChecks},
    };
}

QJsonObject ControlCenterBackend::buildServerMetricsPayload() const {
    QJsonArray routes;
    QList<BackendRouteMetric> metrics = m_routeMetrics.values();
    std::sort(metrics.begin(), metrics.end(), [](const BackendRouteMetric& left, const BackendRouteMetric& right) {
        return left.hits > right.hits;
    });

    qint64 totalHits = 0;
    double totalLatency = 0.0;
    qint64 errorHits = 0;
    for (const BackendRouteMetric& metric : std::as_const(metrics)) {
        totalHits += metric.hits;
        totalLatency += metric.avgLatencyMs * static_cast<double>(metric.hits);
        errorHits += metric.errorHits;
        routes.append(QJsonObject{
            {QStringLiteral("key"), metric.key},
            {QStringLiteral("method"), metric.method},
            {QStringLiteral("path"), metric.path},
            {QStringLiteral("hits"), static_cast<double>(metric.hits)},
            {QStringLiteral("error_hits"), static_cast<double>(metric.errorHits)},
            {QStringLiteral("avg_latency_ms"), metric.avgLatencyMs},
            {QStringLiteral("last_latency_ms"), metric.lastLatencyMs},
            {QStringLiteral("last_status_code"), metric.lastStatusCode},
            {QStringLiteral("last_seen"), metric.lastSeen},
        });
    }

    const double avgLatency = totalHits > 0 ? totalLatency / static_cast<double>(totalHits) : 0.0;
    return QJsonObject{
        {QStringLiteral("ok"), true},
        {QStringLiteral("summary"), QJsonObject{
             {QStringLiteral("request_count"), static_cast<double>(m_requestCount)},
             {QStringLiteral("tracked_requests"), static_cast<double>(totalHits)},
             {QStringLiteral("avg_latency_ms"), avgLatency},
             {QStringLiteral("error_rate"), totalHits > 0 ? static_cast<double>(errorHits) / static_cast<double>(totalHits) : 0.0},
             {QStringLiteral("uptime_seconds"), static_cast<double>(m_startedAt.secsTo(QDateTime::currentDateTimeUtc()))},
         }},
        {QStringLiteral("log_summary"), buildLogSummary()},
        {QStringLiteral("routes"), routes},
        {QStringLiteral("recent_requests"), buildRecentRequestRows(40)},
    };
}
