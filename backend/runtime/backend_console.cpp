#include "../backend.h"
#include "../common/backend_common.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTextStream>

using namespace ControlCenterBackendCommon;

namespace {

QString normalizeProcessName(const QString& raw) {
    const QString name = raw.trimmed().toLower();
    if (name == QStringLiteral("train")) return QStringLiteral("training");
    if (name == QStringLiteral("eval")) return QStringLiteral("evaluate");
    if (name == QStringLiteral("infer")) return QStringLiteral("inference");
    return name;
}

QString joinHealthRows(const QJsonArray& health) {
    QStringList lines;
    for (const QJsonValue& value : health) {
        const QJsonObject row = value.toObject();
        lines.append(QStringLiteral("%1 [%2] %3")
                         .arg(row.value(QStringLiteral("check")).toString(),
                              row.value(QStringLiteral("status")).toString(),
                              row.value(QStringLiteral("message")).toString()));
    }
    return lines.join(QLatin1Char('\n'));
}

QString joinLogRows(const QJsonArray& logs) {
    QStringList lines;
    for (const QJsonValue& value : logs) {
        const QJsonObject row = value.toObject();
        const QString when = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(row.value(QStringLiteral("ts")).toDouble()))
                                 .toString(QStringLiteral("HH:mm:ss"));
        lines.append(QStringLiteral("[%1] %2/%3 %4")
                         .arg(when,
                              row.value(QStringLiteral("severity")).toString(QStringLiteral("info")).toUpper(),
                              row.value(QStringLiteral("category")).toString(),
                              row.value(QStringLiteral("message")).toString()));
    }
    return lines.join(QLatin1Char('\n'));
}

}  // namespace

void ControlCenterBackend::printConsoleLine(const QString& line, bool isError) const {
    QTextStream stream(isError ? stderr : stdout);
    stream << line << Qt::endl;
    stream.flush();
}

QString ControlCenterBackend::consoleHelpText() const {
    return QStringLiteral(
        "Commands:\n"
        "  help\n"
        "  status\n"
        "  health\n"
        "  processes\n"
        "  logs [count]\n"
        "  start <setup|prepare|training|evaluate|inference|autopilot>\n"
        "  stop <setup|prepare|training|evaluate|inference|autopilot>\n"
        "  pause <setup|prepare|training|evaluate|inference|autopilot>\n"
        "  resume <setup|prepare|training|evaluate|inference|autopilot>\n"
        "  restart <setup|prepare|training|evaluate|inference>\n"
        "  reload <module>\n"
        "  self-heal [aggressive]\n"
        "  clear-issues\n"
        "  clear-cache\n"
        "  quit");
}

QString ControlCenterBackend::formatConsoleProcessRow(const QString& name) const {
    const QJsonObject process = buildProcessSnapshot(name);
    const QJsonObject job = const_cast<ControlCenterBackend*>(this)->summarizeJob(name);
    QString status = process.value(QStringLiteral("status")).toString();
    if (status.isEmpty()) {
        status = process.value(QStringLiteral("running")).toBool() ? QStringLiteral("running") : QStringLiteral("idle");
        if (process.value(QStringLiteral("paused")).toBool()) {
            status = QStringLiteral("paused");
        }
    }
    const QString stage = job.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    const QString progress = QString::number(job.value(QStringLiteral("progress")).toDouble() * 100.0, 'f', 1) + QLatin1Char('%');
    return QStringLiteral("%1: %2 | stage=%3 | progress=%4")
        .arg(name, status, stage, progress);
}

QString ControlCenterBackend::handleConsoleCommand(const QString& rawLine, bool* quitRequested) {
    if (quitRequested) {
        *quitRequested = false;
    }

    const QString line = rawLine.trimmed();
    if (line.isEmpty()) {
        return QString();
    }

    const QStringList parts = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString cmd = parts.first().toLower();
    const QString arg1 = parts.size() > 1 ? parts.at(1) : QString();
    const QString arg2 = parts.size() > 2 ? parts.at(2) : QString();

    const QSet<QString> mutatingCommands{
        QStringLiteral("start"),
        QStringLiteral("stop"),
        QStringLiteral("pause"),
        QStringLiteral("resume"),
        QStringLiteral("restart"),
        QStringLiteral("reload"),
        QStringLiteral("self-heal"),
        QStringLiteral("clear-issues"),
        QStringLiteral("clear-cache"),
    };
    if (!m_exclusiveOperationName.isEmpty() && mutatingCommands.contains(cmd)) {
        return QStringLiteral("Backend is busy with %1. Try again shortly.").arg(m_exclusiveOperationName);
    }

    recordLog(QStringLiteral("info"),
              QStringLiteral("console"),
              QStringLiteral("command"),
              QStringLiteral("Console command received"),
              QJsonObject{{QStringLiteral("line"), line}});

    if (cmd == QStringLiteral("status") || cmd == QStringLiteral("processes")) {
        refreshRecoveredProcessStates();
        restoreAutopilotRuntimeState();
    }

    if (cmd == QStringLiteral("help")) {
        return consoleHelpText();
    }

    if (cmd == QStringLiteral("quit") || cmd == QStringLiteral("exit")) {
        if (quitRequested) {
            *quitRequested = true;
        }
        return QStringLiteral("Shutting down native backend...");
    }

    if (cmd == QStringLiteral("status")) {
        const QJsonObject state = buildStatePayload();
        const QJsonObject server = state.value(QStringLiteral("server")).toObject();
        const QJsonObject primary = state.value(QStringLiteral("primary_job")).toObject();
        return QStringLiteral("uptime=%1s | requests=%2 | primary=%3 | stage=%4 | progress=%5 | message=%6")
            .arg(QString::number(server.value(QStringLiteral("uptime_seconds")).toDouble(), 'f', 0),
                 QString::number(server.value(QStringLiteral("request_count")).toDouble(), 'f', 0),
                 primary.value(QStringLiteral("job")).toString(QStringLiteral("none")),
                 primary.value(QStringLiteral("stage")).toString(QStringLiteral("idle")),
                 QString::number(primary.value(QStringLiteral("progress")).toDouble() * 100.0, 'f', 1) + QLatin1Char('%'),
                 primary.value(QStringLiteral("message")).toString(QStringLiteral("Waiting for activity")));
    }

    if (cmd == QStringLiteral("health")) {
        return joinHealthRows(runHealthChecks());
    }

    if (cmd == QStringLiteral("processes")) {
        QStringList lines;
        const QStringList names = {
            QStringLiteral("setup"), QStringLiteral("prepare"), QStringLiteral("training"),
            QStringLiteral("evaluate"), QStringLiteral("inference"), QStringLiteral("autopilot"),
        };
        for (const QString& name : names) {
            lines.append(formatConsoleProcessRow(name));
        }
        return lines.join(QLatin1Char('\n'));
    }

    if (cmd == QStringLiteral("logs")) {
        const int limit = qBound(1, arg1.isEmpty() ? 20 : arg1.toInt(), 50);
        return joinLogRows(recentBackendLogRows(limit));
    }

    if (cmd == QStringLiteral("reload")) {
        const QString module = line.section(QLatin1Char(' '), 1).trimmed();
        if (module.isEmpty()) {
            return QStringLiteral("Usage: reload <module>");
        }
        return reloadModuleAction(module).value(QStringLiteral("message")).toString(QStringLiteral("Reload failed"));
    }

    if (cmd == QStringLiteral("self-heal")) {
        QJsonObject payload;
        payload.insert(QStringLiteral("aggressive"), arg1.compare(QStringLiteral("aggressive"), Qt::CaseInsensitive) == 0);
        const QJsonObject result = runSelfHealAction(payload);
        QStringList lines{result.value(QStringLiteral("message")).toString()};
        for (const QJsonValue& value : result.value(QStringLiteral("actions")).toArray()) {
            lines.append(QStringLiteral("- %1").arg(value.toString()));
        }
        return lines.join(QLatin1Char('\n'));
    }

    if (cmd == QStringLiteral("clear-issues")) {
        clearAllIssues();
        return QStringLiteral("Tracked issues cleared.");
    }

    if (cmd == QStringLiteral("clear-cache")) {
        return clearRuntimeCaches().value(QStringLiteral("message")).toString(QStringLiteral("Runtime caches cleared"));
    }

    const QString name = normalizeProcessName(arg1);
    if (cmd == QStringLiteral("start")) {
        if (name == QStringLiteral("autopilot")) return startAutopilot() ? QStringLiteral("Autopilot started.") : QStringLiteral("Autopilot is already active.");
        if (!m_processes.contains(name)) return QStringLiteral("Unknown process name.");
        const ManagedProcessState& state = m_processes.value(name);
        return startManagedProcess(name, state.scriptPath, state.extraArgs)
            ? QStringLiteral("%1 started.").arg(name)
            : QStringLiteral("%1 is already running or failed to start.").arg(name);
    }

    if (cmd == QStringLiteral("stop")) {
        if (name == QStringLiteral("autopilot")) return stopAutopilot() ? QStringLiteral("Autopilot stopped.") : QStringLiteral("Autopilot is not active.");
        if (!m_processes.contains(name)) return QStringLiteral("Unknown process name.");
        return stopManagedProcess(name) ? QStringLiteral("%1 stop requested.").arg(name)
                                        : QStringLiteral("%1 is not running.").arg(name);
    }

    if (cmd == QStringLiteral("pause")) {
        if (name == QStringLiteral("autopilot")) return pauseAutopilot() ? QStringLiteral("Autopilot paused.") : QStringLiteral("Autopilot is not active.");
        if (!m_processes.contains(name) || name == QStringLiteral("autopilot")) return QStringLiteral("Unknown process name.");
        return pauseManagedProcess(name) ? QStringLiteral("%1 paused.").arg(name)
                                        : QStringLiteral("%1 is not running.").arg(name);
    }

    if (cmd == QStringLiteral("resume")) {
        if (name == QStringLiteral("autopilot")) return resumeAutopilot() ? QStringLiteral("Autopilot resumed.") : QStringLiteral("Autopilot is not paused or active.");
        if (!m_processes.contains(name) || name == QStringLiteral("autopilot")) return QStringLiteral("Unknown process name.");
        return resumeManagedProcess(name) ? QStringLiteral("%1 resumed.").arg(name)
                                         : QStringLiteral("%1 is not paused.").arg(name);
    }

    if (cmd == QStringLiteral("restart")) {
        if (!m_processes.contains(name) || name == QStringLiteral("autopilot")) {
            return QStringLiteral("Unknown or unsupported process name.");
        }
        return restartManagedProcess(name) ? QStringLiteral("%1 restarted.").arg(name)
                                           : QStringLiteral("%1 could not be restarted.").arg(name);
    }

    if (cmd == QStringLiteral("json")) {
        return QString::fromUtf8(QJsonDocument(buildStatePayload()).toJson(QJsonDocument::Indented));
    }

    Q_UNUSED(arg2);
    return QStringLiteral("Unknown command. Type 'help' for available commands.");
}
