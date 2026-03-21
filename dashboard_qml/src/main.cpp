#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include <QColor>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTextStream>
#include <QtCore/qstring.h>
#include <QtQuickControls2/QQuickStyle>
#include <cstdio>

using namespace Qt::StringLiterals;

#include "AppController.h"

static QFile* logFile = nullptr;

void debugMessageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    Q_UNUSED(ctx)

    if (!logFile) {
        return;
    }

    QTextStream out(logFile);
    switch (type) {
        case QtDebugMsg:    out << "[DEBUG] "; break;
        case QtInfoMsg:     out << "[INFO]  "; break;
        case QtWarningMsg:  out << "[WARN]  "; break;
        case QtCriticalMsg: out << "[CRIT]  "; break;
        case QtFatalMsg:    out << "[FATAL] "; break;
    }
    out << msg << "\n";
    out.flush();
}

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("AI Frontier");
    app.setApplicationVersion("3.0.0");
    app.setOrganizationName("AI Frontier");
    const bool verboseQtLogging = qEnvironmentVariableIntValue("AI_FRONTIER_QT_VERBOSE_LOG") > 0;

    const QString logPath = QCoreApplication::applicationDirPath() + QStringLiteral("/debug_log.txt");
    logFile = new QFile(logPath);
    if (logFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qInstallMessageHandler(debugMessageHandler);
    }

    qDebug() << "=== AI Frontier Dashboard starting ===";
    qDebug() << "Debug log:" << logPath;

    if (verboseQtLogging) {
        qputenv("QSG_INFO", "1");
        QLoggingCategory::setFilterRules(u"qt.scenegraph.*=true\nqt.qml.*=true\nqt.rhi.*=true"_s);
        qDebug() << "Verbose Qt logging enabled";
    } else {
        QLoggingCategory::setFilterRules(u"qt.scenegraph.*=false\nqt.qml.*=false\nqt.rhi.*=false"_s);
    }

    // Force Vulkan for best shader compatibility (matches Refract reference).
    // Shaders are compiled to all backends (SPIR-V + HLSL + MSL) so D3D11/Metal
    // would also work if this line is removed.
    qputenv("QSG_RHI_BACKEND", QByteArrayLiteral("vulkan"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    qDebug() << "Graphics backend: Vulkan";

    QQuickStyle::setStyle("Basic");

    // Force dark palette so all Qt Quick Controls default backgrounds are dark
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(4, 5, 6));
    darkPalette.setColor(QPalette::WindowText, QColor(243, 246, 249));
    darkPalette.setColor(QPalette::Base, QColor(14, 18, 23));
    darkPalette.setColor(QPalette::AlternateBase, QColor(16, 20, 24));
    darkPalette.setColor(QPalette::Text, QColor(243, 246, 249));
    darkPalette.setColor(QPalette::Button, QColor(16, 20, 24));
    darkPalette.setColor(QPalette::ButtonText, QColor(243, 246, 249));
    darkPalette.setColor(QPalette::Highlight, QColor(188, 202, 213));
    darkPalette.setColor(QPalette::HighlightedText, QColor(4, 5, 6));
    app.setPalette(darkPalette);

    qmlRegisterSingletonType<AppController>(
        "AiFrontier.Backend", 1, 0, "AppController",
        [](QQmlEngine*, QJSEngine*) -> QObject* {
            return new AppController();
        }
    );

    QQmlApplicationEngine engine;

    QObject::connect(&engine, &QQmlApplicationEngine::warnings,
        [](const QList<QQmlError>& warnings) {
            for (const auto& w : warnings) {
                qWarning() << "QML warning:" << w.toString();
            }
        }
    );

    const QUrl url(u"qrc:/qt/qml/AiFrontier/src/qml/main.qml"_s);
    qDebug() << "Loading QML from:" << url;
    qDebug() << "Import paths:" << engine.importPathList();

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated,
        &app, [url](QObject* obj, const QUrl& objUrl) {
            if (!obj && url == objUrl) {
                qCritical() << "FATAL: Failed to load QML from" << objUrl;
                QCoreApplication::exit(-1);
            }
        },
        Qt::QueuedConnection
    );

    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "FATAL: No root objects created. QML failed to load.";
        return -1;
    }

    return app.exec();
}
