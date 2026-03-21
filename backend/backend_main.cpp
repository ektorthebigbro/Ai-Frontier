#include "backend.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QMetaObject>
#include <thread>
#include <iostream>

#ifndef AI_FRONTIER_VERSION
#define AI_FRONTIER_VERSION "3.0.0"
#endif

namespace {

void startConsoleLoop(ControlCenterBackend* backend) {
    std::thread([backend]() {
        if (!backend) {
            return;
        }

        backend->printConsoleLine(QStringLiteral("AI Frontier server console ready. Type 'help' for commands."));
        backend->printConsoleLine(QStringLiteral("server> "));

        std::string raw;
        while (std::getline(std::cin, raw)) {
            const QString line = QString::fromStdString(raw);
            QString result;
            bool quitRequested = false;
            QMetaObject::invokeMethod(
                backend,
                [&]() {
                    result = backend->handleConsoleCommand(line, &quitRequested);
                    if (quitRequested) {
                        QCoreApplication::quit();
                    }
                },
                Qt::BlockingQueuedConnection);

            if (!result.trimmed().isEmpty()) {
                backend->printConsoleLine(result, quitRequested);
            }
            if (quitRequested) {
                break;
            }
            backend->printConsoleLine(QStringLiteral("server> "));
        }
    }).detach();
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AI Frontier Backend API"));
    app.setApplicationVersion(QStringLiteral(AI_FRONTIER_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AI Frontier native backend server"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{QStringLiteral("p"), QStringLiteral("port")},
                      QStringLiteral("Port to listen on (default: from config or 8765)"),
                      QStringLiteral("port")});
    parser.process(app);

    ControlCenterBackend backend;

    quint16 port = 8765;
    if (parser.isSet(QStringLiteral("port"))) {
        bool ok = false;
        const int parsed = parser.value(QStringLiteral("port")).toInt(&ok);
        if (ok && parsed > 0 && parsed <= 65535) {
            port = static_cast<quint16>(parsed);
        } else {
            qCritical() << "Invalid port value:" << parser.value(QStringLiteral("port"));
            return 1;
        }
    } else {
        port = backend.configuredPort();
    }

    if (!backend.start(port)) {
        return 1;
    }
    startConsoleLoop(&backend);

    return app.exec();
}
