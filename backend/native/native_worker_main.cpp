#include "native_worker_context.h"
#include "native_worker_module.h"
#include "native_worker_runtime.h"

#include "../common/backend_common.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPluginLoader>
#include <QTextStream>

using namespace ControlCenterBackendCommon;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ai_frontier_native_worker"));
    app.setApplicationVersion(QStringLiteral(AI_FRONTIER_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AI Frontier C++20 worker host"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption jobOption(QStringLiteral("job"),
                                       QStringLiteral("Native job to execute."),
                                       QStringLiteral("job"));
    const QCommandLineOption configOption(QStringLiteral("config"),
                                          QStringLiteral("Config path."),
                                          QStringLiteral("path"),
                                          rootPathFor(QStringLiteral("configs/default.yaml")));
    const QCommandLineOption moduleOption(QStringLiteral("module"),
                                          QStringLiteral("Hot-reloadable module path."),
                                          QStringLiteral("path"),
                                          nativeWorkerModulePath());
    parser.addOption(jobOption);
    parser.addOption(configOption);
    parser.addOption(moduleOption);
    parser.process(app);

    const QString jobName = parser.value(jobOption).trimmed().toLower();
    if (jobName.isEmpty()) {
        QTextStream(stderr) << "Missing required --job argument.\n";
        return 2;
    }

    const QString configPath = remapProjectPath(parser.value(configOption));
    QString configError;
    const QJsonObject config = NativeWorkerRuntime::loadConfig(configPath, &configError);
    if (config.isEmpty()) {
        QTextStream(stderr) << QStringLiteral("Could not load worker config: %1\n").arg(configError);
        return 1;
    }

    const QString modulePath = remapProjectPath(parser.value(moduleOption));
    if (modulePath.trimmed().isEmpty() || !QFileInfo::exists(modulePath)) {
        QTextStream(stderr) << QStringLiteral("Native runtime module was not found: %1\n").arg(modulePath);
        return 1;
    }

    QPluginLoader loader(modulePath);
    QObject* pluginInstance = loader.instance();
    if (!pluginInstance) {
        QTextStream(stderr) << QStringLiteral("Could not load native runtime module: %1\n").arg(loader.errorString());
        return 1;
    }

    auto* module = qobject_cast<INativeWorkerModule*>(pluginInstance);
    if (!module) {
        QTextStream(stderr) << QStringLiteral("Loaded plugin does not implement INativeWorkerModule: %1\n").arg(modulePath);
        loader.unload();
        return 1;
    }

    NativeWorkerContext context{
        .jobName = jobName,
        .projectRoot = projectRootPath(),
        .configPath = configPath,
        .modulePath = modulePath,
        .arguments = parser.positionalArguments(),
        .config = config,
    };

    const int exitCode = module->run(context);
    loader.unload();
    return exitCode;
}
