#include "DiagnosticsPage.h"
#include "../app/ApiClient.h"
#include "../util/Formatters.h"
#include "../widgets/GlowCard.h"
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QUrl>
#include <QSpacerItem>
#include <QTimer>
#include <QVBoxLayout>

namespace {

bool looksLikeProjectRoot(const QDir& dir)
{
    return QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("scripts/launcher.py")))
        && QFileInfo::exists(dir.absoluteFilePath(QStringLiteral("configs/default.yaml")));
}

QString locateProjectRoot()
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int depth = 0; depth < 6; ++depth) {
        if (looksLikeProjectRoot(dir)) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return QString();
}

QString locatePythonExecutable(const QString& projectRoot)
{
    const QStringList candidates = {
        QDir(projectRoot).absoluteFilePath(QStringLiteral(".venv/Scripts/python.exe")),
        QDir(projectRoot).absoluteFilePath(QStringLiteral(".venv/bin/python3")),
        QDir(projectRoot).absoluteFilePath(QStringLiteral(".venv/bin/python")),
    };
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    const QString python3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (!python3.isEmpty()) {
        return python3;
    }
    return QStandardPaths::findExecutable(QStringLiteral("python"));
}

bool startProjectLauncher(const QStringList& launcherArgs)
{
    const QString projectRoot = locateProjectRoot();
    if (projectRoot.isEmpty()) {
        return false;
    }
    const QString launcherScript = QDir(projectRoot).absoluteFilePath(QStringLiteral("scripts/launcher.py"));
    const QString python = locatePythonExecutable(projectRoot);
    if (!QFileInfo::exists(launcherScript) || python.isEmpty()) {
        return false;
    }
    return QProcess::startDetached(python, QStringList{launcherScript} + launcherArgs, projectRoot);
}

}  // namespace

// -----------------------------------------------------------------------
// Helper: build the Health Checks card
// -----------------------------------------------------------------------
static GlowCard* buildHealthCard(QWidget*& healthList)
{
    auto* card = new GlowCard(QStringLiteral("Health Checks"));
    auto* lay = card->contentLayout();
    lay->setSpacing(10);

    auto* desc = new QLabel(QStringLiteral(
        "Backend, hardware, config, directory, and environment health are checked here."));
    desc->setObjectName(QStringLiteral("controlDescription"));
    desc->setWordWrap(true);
    lay->addWidget(desc);

    healthList = new QWidget;
    healthList->setObjectName(QStringLiteral("diagList"));
    healthList->setAttribute(Qt::WA_StyledBackground, true);
    auto* listLay = new QVBoxLayout(healthList);
    listLay->setContentsMargins(0, 0, 0, 0);
    listLay->setSpacing(8);

    auto* placeholder = new QLabel(QStringLiteral("Health checks have not been loaded yet."));
    placeholder->setObjectName(QStringLiteral("diagEmpty"));
    placeholder->setWordWrap(true);
    listLay->addWidget(placeholder);

    lay->addWidget(healthList, 1);
    return card;
}

// -----------------------------------------------------------------------
// Helper: build the Hot Reload / Self-Repair card
// -----------------------------------------------------------------------
static GlowCard* buildRepairCard(
    QComboBox*& moduleSelect,
    QLabel*& moduleGuide,
    QLabel*& logSummary,
    QLabel*& reloadResult,
    QPushButton*& runChecksBtn,
    QPushButton*& reloadBtn,
    QPushButton*& selfHealBtn,
    QPushButton*& launchBackendBtn,
    QPushButton*& clearCacheBtn,
    QPushButton*& clearAllBtn)
{
    auto* card = new GlowCard(QStringLiteral("Hot Reload Module"));
    auto* lay = card->contentLayout();
    lay->setSpacing(10);

    auto* desc = new QLabel(QStringLiteral(
        "Use native recovery controls to launch the backend, hot-reload safe modules, "
        "clear caches, and re-run diagnostics."));
    desc->setObjectName(QStringLiteral("controlDescription"));
    desc->setWordWrap(true);
    lay->addWidget(desc);

    auto* moduleLabel = new QLabel(QStringLiteral("Reloadable Module"));
    moduleLabel->setObjectName(QStringLiteral("settingsLabel"));
    lay->addWidget(moduleLabel);

    moduleSelect = new QComboBox;
    moduleSelect->setMinimumHeight(36);
    lay->addWidget(moduleSelect);

    moduleGuide = new QLabel(QStringLiteral("Select a reload target to see what it affects."));
    moduleGuide->setObjectName(QStringLiteral("diagHelperText"));
    moduleGuide->setWordWrap(true);
    lay->addWidget(moduleGuide);

    auto* actionRow = new QHBoxLayout;
    actionRow->setSpacing(10);

    reloadBtn = new QPushButton(QStringLiteral("Hot Reload"));
    reloadBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
    reloadBtn->setMinimumHeight(38);
    actionRow->addWidget(reloadBtn, 1);

    runChecksBtn = new QPushButton(QStringLiteral("Run Checks"));
    runChecksBtn->setObjectName(QStringLiteral("actionBtn"));
    runChecksBtn->setMinimumHeight(38);
    actionRow->addWidget(runChecksBtn, 1);

    lay->addLayout(actionRow);

    auto* logSummaryLabel = new QLabel(QStringLiteral("Logs: --"));
    logSummaryLabel->setObjectName(QStringLiteral("diagHelperText"));
    logSummaryLabel->setWordWrap(true);
    lay->addWidget(logSummaryLabel);
    logSummary = logSummaryLabel;

    // Self-Repair Actions section
    auto* repairTitle = new QLabel(QStringLiteral("Self-Repair Actions"));
    repairTitle->setObjectName(QStringLiteral("controlTitle"));
    lay->addWidget(repairTitle);

    auto* utilityRow = new QHBoxLayout;
    utilityRow->setSpacing(10);

    launchBackendBtn = new QPushButton(QStringLiteral("Launch Backend"));
    launchBackendBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
    launchBackendBtn->setMinimumHeight(38);
    utilityRow->addWidget(launchBackendBtn, 1);

    clearCacheBtn = new QPushButton(QStringLiteral("Clear Cache"));
    clearCacheBtn->setObjectName(QStringLiteral("actionBtn"));
    clearCacheBtn->setMinimumHeight(38);
    utilityRow->addWidget(clearCacheBtn, 1);

    lay->addLayout(utilityRow);

    selfHealBtn = new QPushButton(QStringLiteral("Run Self-Heal"));
    selfHealBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
    selfHealBtn->setMinimumHeight(38);
    lay->addWidget(selfHealBtn);

    clearAllBtn = new QPushButton(QStringLiteral("Clear All Issues"));
    clearAllBtn->setObjectName(QStringLiteral("actionBtnDanger"));
    clearAllBtn->setMinimumHeight(38);
    lay->addWidget(clearAllBtn);

    reloadResult = new QLabel(QStringLiteral("Ready for diagnostics actions."));
    reloadResult->setObjectName(QStringLiteral("diagHelperText"));
    reloadResult->setWordWrap(true);
    reloadResult->setProperty("severity", QStringLiteral("info"));
    lay->addWidget(reloadResult);

    lay->addStretch(1);
    return card;
}

// -----------------------------------------------------------------------
// Helper: build the Active Issues card
// -----------------------------------------------------------------------
static GlowCard* buildIssuesCard(QWidget*& issueList, QLabel*& issueCount)
{
    auto* card = new GlowCard(QStringLiteral("Active Issues"));
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    card->contentLayout()->setSpacing(12);

    auto* helper = new QLabel(QStringLiteral(
        "Runtime failures, blocked jobs, and process launch errors stay here until they recover or are cleared."));
    helper->setObjectName(QStringLiteral("controlDescription"));
    helper->setWordWrap(true);
    card->contentLayout()->addWidget(helper);

    auto* header = new QHBoxLayout;
    header->addStretch(1);
    issueCount = new QLabel(QStringLiteral("0"));
    issueCount->setObjectName(QStringLiteral("alertHeaderCount"));
    issueCount->setProperty("severity", QStringLiteral("clear"));
    header->addWidget(issueCount);
    card->contentLayout()->addLayout(header);

    issueList = new QWidget;
    issueList->setObjectName(QStringLiteral("diagList"));
    issueList->setAttribute(Qt::WA_StyledBackground, true);
    issueList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto* listLay = new QVBoxLayout(issueList);
    listLay->setContentsMargins(0, 0, 0, 0);
    listLay->setSpacing(10);

    auto* placeholder = new QLabel(QStringLiteral("No active runtime issues."));
    placeholder->setObjectName(QStringLiteral("diagEmpty"));
    placeholder->setWordWrap(true);
    listLay->addWidget(placeholder);

    card->contentLayout()->addWidget(issueList, 1);
    return card;
}

// -----------------------------------------------------------------------
// Helper: build the Fix History card
// -----------------------------------------------------------------------
static GlowCard* buildFixLogCard(QPlainTextEdit*& fixLog)
{
    auto* card = new GlowCard(QStringLiteral("Fix History"));
    auto* lay = card->contentLayout();
    lay->setSpacing(10);

    auto* helper = new QLabel(QStringLiteral(
        "Every hot reload or self-repair action is recorded here so you can see what changed at runtime."));
    helper->setObjectName(QStringLiteral("controlDescription"));
    helper->setWordWrap(true);
    lay->addWidget(helper);

    fixLog = new QPlainTextEdit;
    fixLog->setObjectName(QStringLiteral("reportArea"));
    fixLog->setReadOnly(true);
    fixLog->setMinimumHeight(220);
    fixLog->setPlainText(QStringLiteral("No fixes applied yet."));
    lay->addWidget(fixLog, 1);

    return card;
}

// -----------------------------------------------------------------------
// Helper: build the Backend Log card
// -----------------------------------------------------------------------
static GlowCard* buildBackendLogCard(QPlainTextEdit*& backendLog)
{
    auto* card = new GlowCard(QStringLiteral("Backend Log"));
    auto* lay = card->contentLayout();
    lay->setSpacing(10);

    auto* helper = new QLabel(QStringLiteral(
        "Structured native backend logs capture runtime warnings, errors, repairs, "
        "and server-side events in a durable JSONL stream."));
    helper->setObjectName(QStringLiteral("controlDescription"));
    helper->setWordWrap(true);
    lay->addWidget(helper);

    backendLog = new QPlainTextEdit;
    backendLog->setObjectName(QStringLiteral("reportArea"));
    backendLog->setReadOnly(true);
    backendLog->setMinimumHeight(220);
    backendLog->setPlainText(QStringLiteral("No backend log events yet."));
    lay->addWidget(backendLog, 1);

    return card;
}

static QString displayIssueModuleName(const QString& module)
{
    if (module == QStringLiteral("setup")
        || module == QStringLiteral("prepare")
        || module == QStringLiteral("training")
        || module == QStringLiteral("evaluate")
        || module == QStringLiteral("inference")
        || module == QStringLiteral("autopilot")) {
        return Fmt::prettyJobName(module);
    }
    return module;
}

static QString displayReloadModuleName(const QString& module)
{
    if (module == QStringLiteral("frontier.hardware")) {
        return QStringLiteral("Hardware Telemetry");
    }
    if (module == QStringLiteral("frontier.config")) {
        return QStringLiteral("Config Loader");
    }
    if (module == QStringLiteral("frontier.utils")) {
        return QStringLiteral("Shared Utilities");
    }
    if (module == QStringLiteral("frontier.model_management")) {
        return QStringLiteral("Model Catalog");
    }
    if (module == QStringLiteral("frontier.modeling")) {
        return QStringLiteral("Generator Model Code");
    }
    if (module == QStringLiteral("frontier.data")) {
        return QStringLiteral("Training Data Helpers");
    }
    if (module == QStringLiteral("dataset_pipeline.build_dataset")) {
        return QStringLiteral("Prepare Pipeline");
    }
    if (module == QStringLiteral("frontier.judging")) {
        return QStringLiteral("Judge Protocols");
    }
    return module;
}

static QString reloadModuleGuideText(const QString& module)
{
    if (module == QStringLiteral("frontier.hardware")) {
        return QStringLiteral("Refreshes GPU, VRAM, RAM, and hardware probe state without restarting managed workers.");
    }
    if (module == QStringLiteral("frontier.config")) {
        return QStringLiteral("Reloads configs/default.yaml from disk and refreshes runtime state that depends on config values.");
    }
    if (module == QStringLiteral("frontier.utils")) {
        return QStringLiteral("Clears shared runtime caches so the next worker launch uses updated utility helpers.");
    }
    if (module == QStringLiteral("frontier.model_management")) {
        return QStringLiteral("Refreshes model catalog visibility, cache summaries, and download state in the dashboard.");
    }
    if (module == QStringLiteral("frontier.modeling")) {
        return QStringLiteral("Applies generator model code changes. If inference is running it will restart; otherwise changes apply on the next inference or evaluation run.");
    }
    if (module == QStringLiteral("frontier.data")) {
        return QStringLiteral("Applies shared scoring and dataset helper changes. Prepare will restart if it is active; train will pick them up on the next run.");
    }
    if (module == QStringLiteral("dataset_pipeline.build_dataset")) {
        return QStringLiteral("Applies prepare-entrypoint changes like source loading, tokenizer training, and dataset writing. Prepare will restart immediately if it is active.");
    }
    if (module == QStringLiteral("frontier.judging")) {
        return QStringLiteral("Applies large-judge parsing and protocol logic. If inference is running it will restart; evaluation uses the updated judge logic on the next run.");
    }
    return QStringLiteral("Clears runtime caches so the next worker launch can pick up the updated module.");
}

static QString reloadTargetForIssueModule(const QString& module)
{
    if (module == QStringLiteral("prepare")) {
        return QStringLiteral("dataset_pipeline.build_dataset");
    }
    return module;
}

static QString formatDeepDiveText(const QJsonObject& payload)
{
    if (!payload.value(QStringLiteral("ok")).toBool(false)) {
        return payload.value(QStringLiteral("error")).toString(QStringLiteral("Issue deep-dive failed."));
    }

    const QJsonObject issue = payload.value(QStringLiteral("issue")).toObject();
    const QJsonArray hints = payload.value(QStringLiteral("hints")).toArray();
    const QJsonArray relatedLogs = payload.value(QStringLiteral("related_logs")).toArray();
    const QJsonArray relatedProcesses = payload.value(QStringLiteral("related_processes")).toArray();
    const QJsonObject env = payload.value(QStringLiteral("environment")).toObject();
    const QJsonObject logSummary = payload.value(QStringLiteral("log_summary")).toObject();

    QStringList lines;
    lines << QStringLiteral("ISSUE")
          << QStringLiteral("-----")
          << QStringLiteral("Key: %1").arg(issue.value(QStringLiteral("key")).toString())
          << QStringLiteral("Module: %1").arg(issue.value(QStringLiteral("module")).toString())
          << QStringLiteral("Count: %1").arg(issue.value(QStringLiteral("count")).toInt())
          << QStringLiteral("Last seen: %1").arg(Fmt::diagTimestamp(issue.value(QStringLiteral("last_seen")).toDouble()))
          << QStringLiteral("Error: %1").arg(issue.value(QStringLiteral("error")).toString());

    if (!hints.isEmpty()) {
        lines << QString() << QStringLiteral("SUGGESTED NEXT STEPS") << QStringLiteral("--------------------");
        for (const QJsonValue& value : hints) {
            lines << QStringLiteral("- %1").arg(value.toString());
        }
    }

    lines << QString() << QStringLiteral("ENVIRONMENT") << QStringLiteral("-----------")
          << QStringLiteral("Python: %1").arg(env.value(QStringLiteral("python")).toString())
          << QStringLiteral("Cache dir: %1").arg(env.value(QStringLiteral("cache_dir")).toString())
          << QStringLiteral("Checkpoint dir: %1").arg(env.value(QStringLiteral("checkpoint_dir")).toString())
          << QStringLiteral("Pending deletes: %1").arg(env.value(QStringLiteral("pending_delete_count")).toInt())
          << QStringLiteral("Log summary: %1 error / %2 warning / %3 info")
                 .arg(logSummary.value(QStringLiteral("error_count")).toInt())
                 .arg(logSummary.value(QStringLiteral("warning_count")).toInt())
                 .arg(logSummary.value(QStringLiteral("info_count")).toInt());

    lines << QString() << QStringLiteral("MATCHED BACKEND LOGS") << QStringLiteral("--------------------");
    if (relatedLogs.isEmpty()) {
        lines << QStringLiteral("No matching structured backend log rows were found.");
    } else {
        for (const QJsonValue& value : relatedLogs) {
            const QJsonObject row = value.toObject();
            lines << QStringLiteral("[%1] %2 %3/%4")
                         .arg(Fmt::diagTimestamp(row.value(QStringLiteral("ts")).toDouble()))
                         .arg(row.value(QStringLiteral("severity")).toString().toUpper())
                         .arg(row.value(QStringLiteral("category")).toString())
                         .arg(row.value(QStringLiteral("action")).toString())
                  << row.value(QStringLiteral("message")).toString();
            const QJsonObject context = row.value(QStringLiteral("context")).toObject();
            if (!context.isEmpty()) {
                lines << QString::fromUtf8(QJsonDocument(context).toJson(QJsonDocument::Indented)).trimmed();
            }
            lines << QString();
        }
    }

    lines << QStringLiteral("RELATED PROCESS SNAPSHOT") << QStringLiteral("------------------------");
    if (relatedProcesses.isEmpty()) {
        lines << QStringLiteral("No related managed processes were attached to this issue.");
    } else {
        for (const QJsonValue& value : relatedProcesses) {
            const QJsonObject row = value.toObject();
            const QString name = row.value(QStringLiteral("name")).toString();
            const QJsonObject snap = row.value(QStringLiteral("snapshot")).toObject();
            lines << QStringLiteral("%1").arg(name.toUpper())
                  << QStringLiteral("  state: %1").arg(snap.value(QStringLiteral("state")).toString())
                  << QStringLiteral("  paused: %1").arg(snap.value(QStringLiteral("paused")).toBool() ? QStringLiteral("yes") : QStringLiteral("no"))
                  << QStringLiteral("  exit code: %1").arg(snap.value(QStringLiteral("last_exit_code")).toInt())
                  << QStringLiteral("  updated: %1").arg(Fmt::relativeTime(snap.value(QStringLiteral("updated_at")).toDouble()));
            const QJsonArray tail = row.value(QStringLiteral("tail_log")).toArray();
            if (tail.isEmpty()) {
                lines << QStringLiteral("  log: no recent process output");
            } else {
                lines << QStringLiteral("  recent log:");
                for (const QJsonValue& line : tail) {
                    lines << QStringLiteral("    %1").arg(line.toString());
                }
            }
            lines << QString();
        }
    }

    return lines.join(QStringLiteral("\n"));
}

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------
DiagnosticsPage::DiagnosticsPage(ApiClient* api, QWidget* parent)
    : BasePage(api, parent)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(28, 26, 28, 24);
    lay->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Diagnostics"));
    title->setObjectName(QStringLiteral("pageHeroTitle"));
    lay->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral(
        "Run health checks, inspect active runtime issues, and hot-reload safe modules "
        "without restarting the server."));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    subtitle->setWordWrap(true);
    lay->addWidget(subtitle);

    // Top row: health checks + repair controls
    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(16);

    auto* healthCard = buildHealthCard(m_healthList);
    topRow->addWidget(healthCard, 1);

    QPushButton* runChecksBtn = nullptr;
    QPushButton* reloadBtn = nullptr;
    QPushButton* selfHealBtn = nullptr;
    QPushButton* launchBackendBtn = nullptr;
    QPushButton* clearCacheBtn = nullptr;
    QPushButton* clearAllBtn = nullptr;
    auto* repairCard = buildRepairCard(
        m_moduleSelect, m_moduleGuide, m_logSummary, m_reloadResult,
        runChecksBtn, reloadBtn, selfHealBtn, launchBackendBtn, clearCacheBtn, clearAllBtn);
    repairCard->setMinimumWidth(340);
    repairCard->setMaximumWidth(420);
    topRow->addWidget(repairCard, 0);

    lay->addLayout(topRow);

    // Wire buttons
    connect(runChecksBtn, &QPushButton::clicked, this, &DiagnosticsPage::runHealthChecks);
    connect(reloadBtn, &QPushButton::clicked, this, &DiagnosticsPage::reloadModule);
    connect(selfHealBtn, &QPushButton::clicked, this, &DiagnosticsPage::runSelfHeal);
    connect(clearAllBtn, &QPushButton::clicked, this, &DiagnosticsPage::clearAllIssues);
    connect(m_moduleSelect, &QComboBox::currentIndexChanged, this, [this]() {
        if (!m_moduleGuide || !m_moduleSelect) {
            return;
        }
        m_moduleGuide->setText(reloadModuleGuideText(m_moduleSelect->currentData().toString()));
    });

    connect(launchBackendBtn, &QPushButton::clicked, this, [this]() {
        const bool started = startProjectLauncher({QStringLiteral("server")});
        if (m_reloadResult) {
            m_reloadResult->setText(started
                ? QStringLiteral("Backend launch requested. Waiting for connection...")
                : QStringLiteral("Could not launch backend from scripts/launcher.py"));
            m_reloadResult->setProperty("severity",
                started ? QStringLiteral("info") : QStringLiteral("error"));
            Fmt::repolish(m_reloadResult);
        }
        if (started) {
            QTimer::singleShot(1200, this, [this]() {
                m_api->fetchState();
                m_api->fetchDiagnostics();
            });
        }
    });

    connect(clearCacheBtn, &QPushButton::clicked, this, [this]() {
        m_api->postAction(QStringLiteral("/api/diagnostics/cache/clear"));
        if (m_reloadResult) {
            m_reloadResult->setText(QStringLiteral("Clearing runtime caches..."));
        }
    });

    // Bottom row: issues + repair history/logs
    auto* bottomRow = new QHBoxLayout;
    bottomRow->setSpacing(16);
    bottomRow->setAlignment(Qt::AlignTop);

    auto* issuesCard = buildIssuesCard(m_issueList, m_issueCount);
    bottomRow->addWidget(issuesCard, 3, Qt::AlignTop);

    auto* rightColumnHost = new QWidget;
    rightColumnHost->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    rightColumnHost->setMinimumWidth(360);
    rightColumnHost->setMaximumWidth(460);
    auto* rightColumn = new QVBoxLayout(rightColumnHost);
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(16);

    auto* fixLogCard = buildFixLogCard(m_fixLog);
    rightColumn->addWidget(fixLogCard, 1);

    auto* backendLogCard = buildBackendLogCard(m_backendLog);
    rightColumn->addWidget(backendLogCard, 1);

    bottomRow->addWidget(rightColumnHost, 2, Qt::AlignTop);

    lay->addLayout(bottomRow);
    lay->addStretch(1);

    connect(m_api, &ApiClient::actionDone, this, [this](const QJsonObject& response) {
        if (!m_reloadResult) {
            return;
        }
        const bool ok = response.value(QStringLiteral("ok")).toBool(true);
        const QString message = response.value(QStringLiteral("message")).toString(
            ok ? QStringLiteral("Diagnostics action completed.") : QStringLiteral("Diagnostics action failed."));
        m_reloadResult->setText(message);
        m_reloadResult->setProperty("severity", ok ? QStringLiteral("info") : QStringLiteral("error"));
        Fmt::repolish(m_reloadResult);
    });

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(buildScrollWrapper(page));
}

// -----------------------------------------------------------------------
// updateFromState
// -----------------------------------------------------------------------
void DiagnosticsPage::updateFromState(const QJsonObject& state)
{
    const auto diagnostics = state[QStringLiteral("diagnostics")].toObject();
    const auto issues = diagnostics[QStringLiteral("issues")].toArray();
    const auto logSummary = diagnostics[QStringLiteral("log_summary")].toObject();
    const int count = issues.size();
    if (m_issueCount) {
        if (count > 0) {
            m_issueCount->setText(QStringLiteral("%1 issue%2")
                .arg(count).arg(count == 1 ? QString() : QStringLiteral("s")));
            m_issueCount->setProperty("severity", QStringLiteral("error"));
        } else {
            m_issueCount->setText(QStringLiteral("clear"));
            m_issueCount->setProperty("severity", QStringLiteral("clear"));
        }
        Fmt::repolish(m_issueCount);
    }
    if (m_logSummary) {
        const int errors = logSummary[QStringLiteral("error_count")].toInt();
        const int warnings = logSummary[QStringLiteral("warning_count")].toInt();
        const int infos = logSummary[QStringLiteral("info_count")].toInt();
        const QString lastMessage = logSummary[QStringLiteral("last_message")].toString();
        m_logSummary->setText(QStringLiteral("Logs: %1 error, %2 warning, %3 info%4")
                                  .arg(errors)
                                  .arg(warnings)
                                  .arg(infos)
                                  .arg(lastMessage.isEmpty() ? QString() : QStringLiteral(" | %1").arg(lastMessage)));
    }
}

// -----------------------------------------------------------------------
// updateFromDiagnostics
// -----------------------------------------------------------------------
void DiagnosticsPage::updateFromDiagnostics(const QJsonObject& diag)
{
    m_lastDiagnostics = diag;
    const auto health = diag[QStringLiteral("health")].toArray();
    const auto issues = diag[QStringLiteral("issues")].toArray();
    const auto fixes = diag[QStringLiteral("fixes_applied")].toArray();
    const auto logs = diag[QStringLiteral("logs")].toArray();
    const auto logSummary = diag[QStringLiteral("log_summary")].toObject();
    const auto reloadable = diag[QStringLiteral("reloadable_modules")].toArray();

    // Populate module selector
    if (m_moduleSelect) {
        const QString selected = m_moduleSelect->currentData().toString();
        const bool block = m_moduleSelect->blockSignals(true);
        m_moduleSelect->clear();
        int selectedIndex = -1;
        for (int i = 0; i < reloadable.size(); ++i) {
            const QString mod = reloadable.at(i).toString();
            m_moduleSelect->addItem(displayReloadModuleName(mod), mod);
            if (mod == selected) {
                selectedIndex = i;
            }
        }
        if (selectedIndex >= 0) {
            m_moduleSelect->setCurrentIndex(selectedIndex);
        } else if (m_moduleSelect->count() > 0) {
            m_moduleSelect->setCurrentIndex(0);
        }
        m_moduleSelect->blockSignals(block);
        if (m_moduleGuide) {
            if (m_moduleSelect->count() > 0) {
                m_moduleGuide->setText(reloadModuleGuideText(m_moduleSelect->currentData().toString()));
            } else {
                m_moduleGuide->setText(QStringLiteral("No reload targets are currently available."));
            }
        }
    }
    if (m_logSummary) {
        const int errors = logSummary[QStringLiteral("error_count")].toInt();
        const int warnings = logSummary[QStringLiteral("warning_count")].toInt();
        const int infos = logSummary[QStringLiteral("info_count")].toInt();
        const QString lastMessage = logSummary[QStringLiteral("last_message")].toString();
        m_logSummary->setText(QStringLiteral("Logs: %1 error, %2 warning, %3 info%4")
                                  .arg(errors)
                                  .arg(warnings)
                                  .arg(infos)
                                  .arg(lastMessage.isEmpty() ? QString() : QStringLiteral(" | %1").arg(lastMessage)));
    }

    // Clear and rebuild health list
    auto clearLayout = [](QWidget* host) {
        if (!host || !host->layout()) return;
        QLayoutItem* item = nullptr;
        while ((item = host->layout()->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
    };

    clearLayout(m_healthList);
    if (m_healthList && m_healthList->layout()) {
        if (health.isEmpty()) {
            auto* row = new QLabel(QStringLiteral("No health data returned."));
            row->setObjectName(QStringLiteral("diagEmpty"));
            row->setWordWrap(true);
            m_healthList->layout()->addWidget(row);
        } else {
            for (const auto& value : health) {
                const auto obj = value.toObject();
                const QString status = obj[QStringLiteral("status")].toString(QStringLiteral("info"));

                auto* row = new QWidget;
                row->setObjectName(QStringLiteral("diagRow"));
                row->setAttribute(Qt::WA_StyledBackground, true);
                auto* rowLay = new QHBoxLayout(row);
                rowLay->setContentsMargins(12, 10, 12, 10);
                rowLay->setSpacing(10);

                // Color-coded badge: ok=green, warning=yellow, error=red
                auto* badge = new QLabel(status.toUpper());
                badge->setObjectName(QStringLiteral("diagBadge"));
                badge->setProperty("status", status);
                if (status == QStringLiteral("ok")) {
                    badge->setStyleSheet(QStringLiteral("color: #22c55e;"));
                } else if (status == QStringLiteral("warning")) {
                    badge->setStyleSheet(QStringLiteral("color: #eab308;"));
                } else if (status == QStringLiteral("error")) {
                    badge->setStyleSheet(QStringLiteral("color: #ef4444;"));
                }
                rowLay->addWidget(badge, 0, Qt::AlignTop);

                auto* textCol = new QVBoxLayout;
                textCol->setContentsMargins(0, 0, 0, 0);
                textCol->setSpacing(3);
                auto* name = new QLabel(
                    obj[QStringLiteral("check")].toString().replace('_', ' ').toUpper());
                name->setObjectName(QStringLiteral("boldText"));
                textCol->addWidget(name);
                auto* msg = new QLabel(obj[QStringLiteral("message")].toString());
                msg->setObjectName(QStringLiteral("controlDescription"));
                msg->setWordWrap(true);
                textCol->addWidget(msg);
                rowLay->addLayout(textCol, 1);

                m_healthList->layout()->addWidget(row);
            }
        }
    }

    // Clear and rebuild issue list
    clearLayout(m_issueList);
    const int issueCount = issues.size();
    if (m_issueCount) {
        if (issueCount > 0) {
            m_issueCount->setText(QStringLiteral("%1 issue%2")
                .arg(issueCount).arg(issueCount == 1 ? QString() : QStringLiteral("s")));
            m_issueCount->setProperty("severity", QStringLiteral("error"));
        } else {
            m_issueCount->setText(QStringLiteral("clear"));
            m_issueCount->setProperty("severity", QStringLiteral("clear"));
        }
        Fmt::repolish(m_issueCount);
    }

    if (m_issueList && m_issueList->layout()) {
        if (issues.isEmpty()) {
            auto* row = new QWidget;
            row->setObjectName(QStringLiteral("diagIssueRow"));
            row->setAttribute(Qt::WA_StyledBackground, true);
            auto* rowLay = new QVBoxLayout(row);
            rowLay->setContentsMargins(14, 12, 14, 12);
            rowLay->setSpacing(6);

            auto* title = new QLabel(QStringLiteral("No active runtime issues"));
            title->setObjectName(QStringLiteral("boldText"));
            rowLay->addWidget(title);

            auto* meta = new QLabel(QStringLiteral(
                "Failures, blocked jobs, and runtime process errors will appear here when diagnostics needs attention."));
            meta->setObjectName(QStringLiteral("diagEmpty"));
            meta->setWordWrap(true);
            rowLay->addWidget(meta);

            m_issueList->layout()->addWidget(row);
        } else {
            for (const auto& value : issues) {
                const auto obj = value.toObject();
                const QString key = obj[QStringLiteral("key")].toString();
                const QString module = obj[QStringLiteral("module")].toString();
                const QString error = obj[QStringLiteral("error")].toString();
                const int count = obj[QStringLiteral("count")].toInt(1);
                const bool suppressed = obj[QStringLiteral("suppressed")].toBool(false);
                const double lastSeen = obj[QStringLiteral("last_seen")].toDouble(0.0);

                auto* row = new QWidget;
                row->setObjectName(QStringLiteral("diagIssueRow"));
                row->setAttribute(Qt::WA_StyledBackground, true);
                auto* rowLay = new QVBoxLayout(row);
                rowLay->setContentsMargins(12, 10, 12, 10);
                rowLay->setSpacing(6);

                // Top: module name + count
                auto* top = new QHBoxLayout;
                const QString displayModule = displayIssueModuleName(module);
                auto* titleLabel = new QLabel(displayModule);
                titleLabel->setObjectName(QStringLiteral("boldText"));
                top->addWidget(titleLabel);
                top->addStretch(1);
                auto* countLabel = new QLabel(QStringLiteral("x%1").arg(count));
                countLabel->setObjectName(QStringLiteral("diagMeta"));
                top->addWidget(countLabel);
                rowLay->addLayout(top);

                // Action row
                auto* actionRow = new QHBoxLayout;
                actionRow->setSpacing(8);
                auto* dismissBtn = new QPushButton(QStringLiteral("Clear"));
                dismissBtn->setObjectName(QStringLiteral("actionBtn"));
                dismissBtn->setMinimumHeight(30);
                dismissBtn->setMinimumWidth(72);
                connect(dismissBtn, &QPushButton::clicked, this, [this, key]() {
                    m_api->postDiagnosticsClear(key);
                });

                auto* diveBtn = new QPushButton(QStringLiteral("Dive Deep"));
                diveBtn->setObjectName(QStringLiteral("actionBtn"));
                diveBtn->setMinimumHeight(30);
                diveBtn->setMinimumWidth(96);
                connect(diveBtn, &QPushButton::clicked, this, [this, obj]() {
                    showIssueDeepDive(obj);
                });

                auto* reloadIssueBtn = new QPushButton(QStringLiteral("Reload"));
                reloadIssueBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
                reloadIssueBtn->setMinimumHeight(30);
                reloadIssueBtn->setMinimumWidth(76);
                connect(reloadIssueBtn, &QPushButton::clicked, this, [this, module]() {
                    m_api->postDiagnosticsReload(reloadTargetForIssueModule(module));
                });
                actionRow->addWidget(diveBtn);
                actionRow->addWidget(dismissBtn);
                actionRow->addWidget(reloadIssueBtn);
                actionRow->addStretch(1);
                rowLay->addLayout(actionRow);

                // Error text
                auto* errorLabel = new QLabel(error);
                errorLabel->setObjectName(QStringLiteral("alertError"));
                errorLabel->setWordWrap(true);
                rowLay->addWidget(errorLabel);

                // Meta: last seen time
                auto* meta = new QLabel(QStringLiteral("Last seen: %1 | %2")
                    .arg(Fmt::diagTimestamp(lastSeen))
                    .arg(suppressed
                        ? QStringLiteral("suppressed after repeated failures")
                        : QStringLiteral("active")));
                meta->setObjectName(QStringLiteral("diagMeta"));
                meta->setWordWrap(true);
                rowLay->addWidget(meta);

                m_issueList->layout()->addWidget(row);
            }
        }
    }

    // Update fix log
    if (m_fixLog) {
        if (fixes.isEmpty()) {
            m_fixLog->setPlainText(QStringLiteral("No fixes applied yet."));
        } else {
            QStringList lines;
            for (const auto& value : fixes) {
                const auto obj = value.toObject();
                const QString when = Fmt::diagTimestamp(
                    obj[QStringLiteral("ts")].toDouble(0.0));
                const QString action = obj[QStringLiteral("action")].toString();
                const QString target = obj[QStringLiteral("target")].toString();
                const QString result = obj[QStringLiteral("result")].toString();
                lines.append(QStringLiteral("[%1] %2  %3\n%4")
                    .arg(when, action, target, result));
            }
            m_fixLog->setPlainText(lines.join(QStringLiteral("\n\n")));
        }
    }

    if (m_backendLog) {
        if (logs.isEmpty()) {
            m_backendLog->setPlainText(QStringLiteral("No backend log events yet."));
        } else {
            QStringList lines;
            for (const auto& value : logs) {
                const QJsonObject obj = value.toObject();
                const QString when = Fmt::diagTimestamp(obj[QStringLiteral("ts")].toDouble(0.0));
                const QString severity = obj[QStringLiteral("severity")].toString(QStringLiteral("info")).toUpper();
                const QString category = obj[QStringLiteral("category")].toString();
                const QString action = obj[QStringLiteral("action")].toString();
                const QString message = obj[QStringLiteral("message")].toString();
                QString line = QStringLiteral("[%1] %2  %3/%4\n%5")
                                   .arg(when, severity, category, action, message);
                const QJsonObject context = obj[QStringLiteral("context")].toObject();
                if (!context.isEmpty()) {
                    line += QStringLiteral("\n")
                        + QString::fromUtf8(QJsonDocument(context).toJson(QJsonDocument::Compact));
                }
                lines.append(line);
            }
            m_backendLog->setPlainText(lines.join(QStringLiteral("\n\n")));
        }
    }

    // Default reload result text
    if (m_reloadResult && m_reloadResult->text().isEmpty()) {
        m_reloadResult->setText(QStringLiteral("Ready for diagnostics actions."));
        m_reloadResult->setProperty("severity", QStringLiteral("info"));
        Fmt::repolish(m_reloadResult);
    }
}

void DiagnosticsPage::showIssueDeepDive(const QJsonObject& issue)
{
    const QString key = issue.value(QStringLiteral("key")).toString();
    if (key.isEmpty()) {
        return;
    }

    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setObjectName(QStringLiteral("deepDiveDialog"));
    dialog->setWindowTitle(QStringLiteral("Issue Deep Dive"));
    dialog->setModal(true);
    dialog->resize(980, 700);

    auto* lay = new QVBoxLayout(dialog);
    lay->setContentsMargins(22, 22, 22, 22);
    lay->setSpacing(16);

    auto* shell = new GlowCard;
    shell->setObjectName(QStringLiteral("deepDiveShell"));
    shell->contentLayout()->setSpacing(14);
    lay->addWidget(shell, 1);

    auto* eyebrow = new QLabel(QStringLiteral("Diagnostics Deep Dive"));
    eyebrow->setObjectName(QStringLiteral("deepDiveEyebrow"));
    shell->contentLayout()->addWidget(eyebrow);

    auto* title = new QLabel(QStringLiteral("Issue Deep Dive"));
    title->setObjectName(QStringLiteral("pageHeroTitle"));
    shell->contentLayout()->addWidget(title);

    auto* summary = new QLabel(QStringLiteral("%1 | x%2 | %3")
                                   .arg(displayIssueModuleName(issue.value(QStringLiteral("module")).toString()))
                                   .arg(issue.value(QStringLiteral("count")).toInt())
                                   .arg(Fmt::diagTimestamp(issue.value(QStringLiteral("last_seen")).toDouble())));
    summary->setObjectName(QStringLiteral("deepDiveSummary"));
    summary->setWordWrap(true);
    shell->contentLayout()->addWidget(summary);

    auto* helper = new QLabel(QStringLiteral(
        "Matched logs, related processes, and recovery hints for the selected issue are loaded below."));
    helper->setObjectName(QStringLiteral("pageSubtitle"));
    helper->setWordWrap(true);
    shell->contentLayout()->addWidget(helper);

    auto* details = new QPlainTextEdit;
    details->setObjectName(QStringLiteral("reportArea"));
    details->setReadOnly(true);
    details->setPlainText(QStringLiteral("Loading issue diagnostics ..."));
    shell->contentLayout()->addWidget(details, 1);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(10);
    auto* copyBtn = new QPushButton(QStringLiteral("Copy Report"));
    copyBtn->setObjectName(QStringLiteral("actionBtn"));
    copyBtn->setMinimumHeight(38);
    buttonRow->addWidget(copyBtn);
    buttonRow->addStretch(1);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"));
    closeBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
    closeBtn->setMinimumHeight(38);
    connect(closeBtn, &QPushButton::clicked, dialog, &QDialog::accept);
    buttonRow->addWidget(closeBtn);
    shell->contentLayout()->addLayout(buttonRow);

    QPointer<QPlainTextEdit> detailsPtr(details);
    QPointer<QDialog> dialogPtr(dialog);
    connect(copyBtn, &QPushButton::clicked, dialog, [detailsPtr]() {
        if (detailsPtr) {
            QApplication::clipboard()->setText(detailsPtr->toPlainText());
        }
    });

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), key);
    m_api->getJson(QStringLiteral("/api/diagnostics/issue?%1").arg(query.toString(QUrl::FullyEncoded)),
                   [detailsPtr](const QJsonObject& payload) {
                       if (!detailsPtr) {
                           return;
                       }
                       if (!payload.value(QStringLiteral("ok")).toBool(true)
                           && payload.value(QStringLiteral("error")).toString() == QStringLiteral("Unknown issue key")) {
                           detailsPtr->setPlainText(QStringLiteral(
                               "This issue is no longer active.\n\nRefresh Diagnostics and reopen the deep dive."));
                           return;
                       }
                       detailsPtr->setPlainText(formatDeepDiveText(payload));
                   },
                   [detailsPtr, key](const QString& error) {
                       if (!detailsPtr) {
                           return;
                       }
                       detailsPtr->setPlainText(QStringLiteral("Could not load deep-dive report for %1\n\n%2").arg(key, error));
                   });

    if (dialogPtr) {
        dialogPtr->exec();
    }
}

// -----------------------------------------------------------------------
// runHealthChecks
// -----------------------------------------------------------------------
void DiagnosticsPage::runHealthChecks()
{
    if (m_reloadResult) {
        m_reloadResult->setText(QStringLiteral("Running health checks ..."));
    }
    m_api->postDiagnosticsRunChecks();
}

// -----------------------------------------------------------------------
// reloadModule
// -----------------------------------------------------------------------
void DiagnosticsPage::reloadModule()
{
    const QString module = m_moduleSelect
        ? m_moduleSelect->currentData().toString() : QString();
    if (module.isEmpty()) {
        if (m_reloadResult) {
            m_reloadResult->setText(QStringLiteral("Select a module before reloading."));
        }
        return;
    }
    if (m_reloadResult) {
        m_reloadResult->setText(QStringLiteral("Reloading %1 ...").arg(module));
    }
    m_api->postDiagnosticsReload(module);
}

// -----------------------------------------------------------------------
// runSelfHeal
// -----------------------------------------------------------------------
void DiagnosticsPage::runSelfHeal()
{
    if (m_reloadResult) {
        m_reloadResult->setText(QStringLiteral("Running native self-heal ..."));
        m_reloadResult->setProperty("severity", QStringLiteral("warning"));
        Fmt::repolish(m_reloadResult);
    }
    m_api->postDiagnosticsSelfHeal(false);
}

// -----------------------------------------------------------------------
// clearAllIssues
// -----------------------------------------------------------------------
void DiagnosticsPage::clearAllIssues()
{
    m_api->postDiagnosticsClear();
    if (m_reloadResult) {
        m_reloadResult->setText(QStringLiteral("Cleared tracked issues."));
        m_reloadResult->setProperty("severity", QStringLiteral("info"));
        Fmt::repolish(m_reloadResult);
    }
}
