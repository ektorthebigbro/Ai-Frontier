#include "DashboardWindow.h"

#include "ApiClient.h"
#include "../pages/ChatPage.h"
#include "../pages/DataPage.h"
#include "../pages/DiagnosticsPage.h"
#include "../pages/JobsPage.h"
#include "../pages/LiveFeedPage.h"
#include "../pages/MetricsPage.h"
#include "../pages/OverviewPage.h"
#include "../pages/ReportsPage.h"
#include "../pages/SettingsPage.h"
#include "../style/StyleEngine.h"
#include "../util/Formatters.h"
#include "../util/Helpers.h"
#include "../widgets/PillBadge.h"
#include "../widgets/StatusIndicator.h"
#include <QAbstractAnimation>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QDateTime>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

namespace {

QString compactGpuName(const QString& value) {
    if (value.isEmpty()) {
        return QStringLiteral("--");
    }
    QString text = value;
    text.replace(QStringLiteral("NVIDIA "), QString());
    text.replace(QStringLiteral("GeForce "), QString());
    return text;
}

QString resolveBackendUrl() {
    const QByteArray envUrl = qgetenv("AI_FRONTIER_BACKEND_URL");
    if (!envUrl.isEmpty()) {
        return QString::fromUtf8(envUrl);
    }
    const QStringList args = QCoreApplication::arguments();
    const int idx = args.indexOf(QStringLiteral("--backend-url"));
    if (idx >= 0 && idx + 1 < args.size()) {
        return args.at(idx + 1);
    }
    return QStringLiteral("http://127.0.0.1:8765");
}

}  // namespace

DashboardWindow::DashboardWindow(QWidget* parent)
    : QMainWindow(parent),
      m_baseUrl(resolveBackendUrl()),
      m_api(new ApiClient(m_baseUrl, this)),
      m_stateTimer(new QTimer(this)),
      m_diagnosticsTimer(new QTimer(this)),
      m_notificationTimer(new QTimer(this)) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setWindowTitle(QStringLiteral("AI Frontier - Training Control Center"));
    resize(1680, 1040);

    buildUi();
    setStyleSheet(StyleEngine::fullStyleSheet());
    registerSignals();

    m_stateTimer->setInterval(800);
    connect(m_stateTimer, &QTimer::timeout, this, &DashboardWindow::refreshState);
    m_stateTimer->start();

    m_diagnosticsTimer->setInterval(2500);
    connect(m_diagnosticsTimer, &QTimer::timeout, this, &DashboardWindow::refreshDiagnostics);
    m_diagnosticsTimer->start();

    refreshState();
    refreshDiagnostics();
}

void DashboardWindow::buildUi() {
    auto* root = new QWidget;
    root->setObjectName(QStringLiteral("appRoot"));

    auto* outer = new QVBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(buildChromeBar());

    auto* body = new QWidget;
    body->setObjectName(QStringLiteral("bodyShell"));
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(buildSidebar());

    buildPageStack();
    bodyLayout->addWidget(m_pages, 1);
    outer->addWidget(body, 1);
    outer->addWidget(buildStatusStrip());

    buildNotificationToast(root);

    setCentralWidget(root);
    statusBar()->hide();
}

QWidget* DashboardWindow::buildChromeBar() {
    m_titleBar = new QWidget;
    m_titleBar->setObjectName(QStringLiteral("chromeBar"));
    m_titleBar->installEventFilter(this);

    auto* layout = new QHBoxLayout(m_titleBar);
    layout->setContentsMargins(16, 6, 10, 6);
    layout->setSpacing(8);

    m_windowTitle = new QLabel(QStringLiteral("AI Frontier - Training Control Center"));
    m_windowTitle->setObjectName(QStringLiteral("chromeTitle"));
    layout->addWidget(m_windowTitle);
    layout->addStretch(1);

    auto makeButton = [this, layout](const QString& text, const QString& name, const auto& slot) {
        auto* button = new QPushButton(text);
        button->setObjectName(name);
        button->setFixedSize(38, 28);
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QPushButton::clicked, this, slot);
        layout->addWidget(button);
        return button;
    };

    m_minButton = makeButton(QStringLiteral("\u2013"), QStringLiteral("chromeMin"), [this]() { showMinimized(); });
    m_maxButton = makeButton(QStringLiteral("\u25A1"), QStringLiteral("chromeMax"), [this]() { toggleMaximized(); });
    m_closeButton = makeButton(QStringLiteral("\u2715"), QStringLiteral("chromeClose"), [this]() { close(); });
    return m_titleBar;
}

QWidget* DashboardWindow::buildSidebar() {
    auto* sidebar = new QFrame;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(240);
    m_sidebar = sidebar;

    auto* layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(6);

    if (!m_navGroup) {
        m_navGroup = new QButtonGroup(this);
        m_navGroup->setExclusive(true);
    }

    auto* brandBox = new QWidget;
    auto* brandLay = new QHBoxLayout(brandBox);
    brandLay->setContentsMargins(0, 0, 0, 10);
    brandLay->setSpacing(12);

    auto* brandBadge = new QWidget;
    brandBadge->setObjectName(QStringLiteral("brandBadge"));
    brandBadge->setFixedSize(44, 44);
    m_brandBadge = brandBadge;
    auto* badgeLayout = new QGridLayout(brandBadge);
    badgeLayout->setContentsMargins(0, 0, 0, 0);

    auto* brandIcon = new QLabel(QStringLiteral("\U0001F916"));
    brandIcon->setAlignment(Qt::AlignCenter);
    brandIcon->setObjectName(QStringLiteral("brandIcon"));
    badgeLayout->addWidget(brandIcon, 0, 0, Qt::AlignCenter);

    m_brandStatusLight = Ui::makeDot(QStringLiteral("#ef4444"));
    m_brandStatusLight->setObjectName(QStringLiteral("brandStatusLight"));
    m_brandStatusLight->setProperty("connected", false);
    badgeLayout->addWidget(m_brandStatusLight, 0, 0, Qt::AlignRight | Qt::AlignBottom);
    brandLay->addWidget(brandBadge);

    auto* brandText = new QWidget;
    auto* textLay = new QVBoxLayout(brandText);
    textLay->setContentsMargins(0, 0, 0, 0);
    textLay->setSpacing(2);
    m_brandName = new QLabel(QStringLiteral("AI Frontier"));
    m_brandName->setObjectName(QStringLiteral("brandName"));
    m_brandSubtitle = new QLabel(QStringLiteral("Training Control Center"));
    m_brandSubtitle->setObjectName(QStringLiteral("brandVer"));
    textLay->addWidget(m_brandName);
    textLay->addWidget(m_brandSubtitle);
    brandLay->addWidget(brandText, 1);

    m_sidebarToggle = new QPushButton(QStringLiteral("\u25C0"));
    m_sidebarToggle->setObjectName(QStringLiteral("sidebarToggle"));
    m_sidebarToggle->setFixedSize(32, 32);
    m_sidebarToggle->setToolTip(QStringLiteral("Collapse sidebar"));
    connect(m_sidebarToggle, &QPushButton::clicked, this, [this]() {
        applySidebarCollapsed(!m_sidebarCollapsed);
    });
    brandLay->addWidget(m_sidebarToggle, 0, Qt::AlignTop);
    layout->addWidget(brandBox);
    m_hideWhenCollapsed.append(brandText);

    auto* modeBox = new QWidget;
    auto* modeLay = new QVBoxLayout(modeBox);
    modeLay->setContentsMargins(0, 6, 0, 0);
    modeLay->setSpacing(6);
    addNavSection(modeLay, QStringLiteral("MODE"));
    m_modeSelect = new QComboBox;
    m_modeSelect->setObjectName(QStringLiteral("modeSelect"));
    m_modeSelect->addItems({QStringLiteral("Basic Mode"), QStringLiteral("Expert Mode")});
    connect(m_modeSelect, &QComboBox::currentIndexChanged, this, &DashboardWindow::applyMode);
    modeLay->addWidget(m_modeSelect);
    layout->addWidget(modeBox);
    m_hideWhenCollapsed.append(modeBox);

    auto* mainViews = new QWidget;
    auto* mainLay = new QVBoxLayout(mainViews);
    mainLay->setContentsMargins(0, 8, 0, 0);
    mainLay->setSpacing(2);
    addNavSection(mainLay, QStringLiteral("VIEWS"));
    addNavButton(mainLay, {QStringLiteral("\u25C8"), QStringLiteral("Dashboard"), QString(), 0});
    addNavButton(mainLay, {QStringLiteral("\U0001F4C8"), QStringLiteral("Metrics"), QString(), 1});
    addNavButton(mainLay, {QStringLiteral("\u2699"), QStringLiteral("Settings"), QString(), 2});
    addNavButton(mainLay, {QStringLiteral("\U0001F4BE"), QStringLiteral("Data"), QString(), 3});
    layout->addWidget(mainViews);

    auto* operations = new QWidget;
    auto* opsLay = new QVBoxLayout(operations);
    opsLay->setContentsMargins(0, 8, 0, 0);
    opsLay->setSpacing(2);
    addNavSection(opsLay, QStringLiteral("OPERATIONS"));
    addNavButton(opsLay, {QStringLiteral("\u2630"), QStringLiteral("Jobs"), QString(), 4});
    addNavButton(opsLay, {QStringLiteral("\u25CE"), QStringLiteral("Chat"), QString(), 5});
    addNavButton(opsLay, {QStringLiteral("\u2637"), QStringLiteral("Reports"), QString(), 6});
    layout->addWidget(operations);

    auto* runtime = new QWidget;
    auto* runtimeLay = new QVBoxLayout(runtime);
    runtimeLay->setContentsMargins(0, 8, 0, 0);
    runtimeLay->setSpacing(2);
    addNavSection(runtimeLay, QStringLiteral("RUNTIME"));
    addNavButton(runtimeLay, {QStringLiteral("\u25C9"), QStringLiteral("Live Feed"), QString(), 7});
    addNavButton(runtimeLay, {QStringLiteral("\u2691"), QStringLiteral("Diagnostics"), QString(), 8});
    layout->addWidget(runtime);

    auto* quick = new QWidget;
    auto* quickLay = new QVBoxLayout(quick);
    quickLay->setContentsMargins(0, 10, 0, 0);
    quickLay->setSpacing(8);
    addNavSection(quickLay, QStringLiteral("QUICK ACCESS"));
    quickLay->addWidget(makeQuickAccessRow(QStringLiteral("Dataset Info"), &m_datasetBadge));
    quickLay->addWidget(makeQuickAccessRow(QStringLiteral("System Resources"), &m_systemBadge));
    quickLay->addWidget(makeQuickAccessRow(QStringLiteral("Training Logs"), &m_logsBadge));
    layout->addWidget(quick);
    m_hideWhenCollapsed.append(quick);

    layout->addStretch(1);

    auto* runtimeBox = new QWidget;
    auto* runtimeInfo = new QVBoxLayout(runtimeBox);
    runtimeInfo->setContentsMargins(0, 8, 0, 0);
    runtimeInfo->setSpacing(6);
    addNavSection(runtimeInfo, QStringLiteral("SYSTEM"));

    auto addRuntimeRow = [runtimeInfo](const QString& key, QLabel*& valueOut, const QString& dotColor) {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        row->addWidget(Ui::makeDot(dotColor));
        row->addWidget(Ui::makeRuntimeKey(key));
        valueOut = Ui::makeRuntimeVal();
        row->addWidget(valueOut, 1);
        runtimeInfo->addLayout(row);
    };

    addRuntimeRow(QStringLiteral("GPU"), m_gpuVal, QStringLiteral("#22c55e"));
    addRuntimeRow(QStringLiteral("Load"), m_loadVal, QStringLiteral("#f59e0b"));
    addRuntimeRow(QStringLiteral("RAM"), m_ramVal, QStringLiteral("#22c55e"));
    layout->addWidget(runtimeBox);
    m_hideWhenCollapsed.append(runtimeBox);

    if (!m_navButtons.isEmpty()) {
        m_navButtons.first()->setChecked(true);
    }

    return sidebar;
}

void DashboardWindow::buildPageStack() {
    m_pages = new QStackedWidget;
    m_pages->setObjectName(QStringLiteral("pageStack"));

    m_overviewPage = new OverviewPage(m_api, this);    // 0
    m_metricsPage = new MetricsPage(m_api, this);      // 1
    m_settingsPage = new SettingsPage(m_api, this);    // 2
    m_dataPage = new DataPage(m_api, this);            // 3
    m_jobsPage = new JobsPage(m_api, this);            // 4
    m_chatPage = new ChatPage(m_api, this);            // 5
    m_reportsPage = new ReportsPage(m_api, this);      // 6
    m_liveFeedPage = new LiveFeedPage(m_api, this);    // 7
    m_diagnosticsPage = new DiagnosticsPage(m_api, this); // 8

    m_pages->addWidget(m_overviewPage);
    m_pages->addWidget(m_metricsPage);
    m_pages->addWidget(m_settingsPage);
    m_pages->addWidget(m_dataPage);
    m_pages->addWidget(m_jobsPage);
    m_pages->addWidget(m_chatPage);
    m_pages->addWidget(m_reportsPage);
    m_pages->addWidget(m_liveFeedPage);
    m_pages->addWidget(m_diagnosticsPage);
}

QWidget* DashboardWindow::buildStatusStrip() {
    auto* strip = new QWidget;
    strip->setObjectName(QStringLiteral("chromeBar"));

    auto* layout = new QHBoxLayout(strip);
    layout->setContentsMargins(16, 8, 16, 8);
    layout->setSpacing(12);

    m_statusIndicator = new StatusIndicator;
    m_statusIndicator->setStatus(QStringLiteral("Disconnected"), QColor(QStringLiteral("#ef4444")));
    layout->addWidget(m_statusIndicator);

    m_statusText = new QLabel(QStringLiteral("Waiting for backend"));
    m_statusText->setObjectName(QStringLiteral("dimText"));
    layout->addWidget(m_statusText);
    layout->addStretch(1);

    m_statusMeta = new QLabel(QStringLiteral("No recent updates"));
    m_statusMeta->setObjectName(QStringLiteral("dimText"));
    layout->addWidget(m_statusMeta);
    return strip;
}

void DashboardWindow::buildNotificationToast(QWidget* parent) {
    m_notificationToast = new QWidget(parent);
    m_notificationToast->setObjectName(QStringLiteral("notificationToast"));
    m_notificationToast->setAttribute(Qt::WA_StyledBackground, true);
    m_notificationToast->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_notificationToast->setFixedWidth(420);
    m_notificationToast->hide();

    auto* layout = new QVBoxLayout(m_notificationToast);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(4);

    m_notificationTitle = new QLabel(QStringLiteral("Notification"));
    m_notificationTitle->setObjectName(QStringLiteral("notificationTitle"));
    layout->addWidget(m_notificationTitle);

    m_notificationMessage = new QLabel(QStringLiteral("--"));
    m_notificationMessage->setObjectName(QStringLiteral("notificationMessage"));
    m_notificationMessage->setWordWrap(true);
    layout->addWidget(m_notificationMessage);

    auto* effect = new QGraphicsOpacityEffect(m_notificationToast);
    effect->setOpacity(0.0);
    m_notificationToast->setGraphicsEffect(effect);

    m_notificationOpacity = new QPropertyAnimation(effect, "opacity", this);
    m_notificationOpacity->setDuration(180);
    m_notificationOpacity->setEasingCurve(QEasingCurve::OutCubic);

    m_notificationTimer->setSingleShot(true);
    connect(m_notificationOpacity, &QPropertyAnimation::finished, this, [this]() {
        if (m_notificationOpacity && m_notificationOpacity->endValue().toDouble() < 0.5 && m_notificationToast) {
            m_notificationToast->hide();
        }
    });
    connect(m_notificationTimer, &QTimer::timeout, this, [this]() {
        if (!m_notificationToast || !m_notificationOpacity) {
            return;
        }
        m_notificationOpacity->stop();
        m_notificationOpacity->setStartValue(1.0);
        m_notificationOpacity->setEndValue(0.0);
        m_notificationOpacity->start();
    });

    positionNotificationToast();
}

void DashboardWindow::addNavSection(QVBoxLayout* layout, const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("sectionLabel"));
    layout->addWidget(label);
    m_sidebarLabels.append(label);
}

void DashboardWindow::addNavButton(QVBoxLayout* layout, const NavItem& item) {
    const QString fullText = QStringLiteral("  %1  %2").arg(item.icon, item.title);
    auto* button = new QPushButton(fullText);
    button->setCheckable(true);
    button->setAutoExclusive(false);
    button->setObjectName(QStringLiteral("navButton"));
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    button->setMinimumHeight(36);
    button->setProperty("fullText", fullText);
    button->setProperty("compactText", item.icon);
    button->setProperty("collapsed", false);
    button->setToolTip(item.title);
    if (m_navGroup) {
        m_navGroup->addButton(button, item.pageIndex);
    }

    connect(button, &QPushButton::clicked, this, [this, button, pageIndex = item.pageIndex]() {
        button->setChecked(true);
        m_pages->setCurrentIndex(pageIndex);
        QWidget* current = m_pages->currentWidget();
        if (!current) {
            return;
        }

        auto* effect = new QGraphicsOpacityEffect(current);
        current->setGraphicsEffect(effect);
        auto* fade = new QPropertyAnimation(effect, "opacity");
        fade->setDuration(220);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->setEasingCurve(QEasingCurve::OutCubic);

        const QPoint endPos = current->pos();
        auto* slide = new QPropertyAnimation(current, "pos");
        slide->setDuration(260);
        slide->setStartValue(endPos + QPoint(18, 0));
        slide->setEndValue(endPos);
        slide->setEasingCurve(QEasingCurve::OutCubic);

        auto* group = new QParallelAnimationGroup(current);
        group->addAnimation(fade);
        group->addAnimation(slide);
        connect(group, &QParallelAnimationGroup::finished, current, [current, effect]() {
            if (current->graphicsEffect() == effect) {
                current->setGraphicsEffect(nullptr);
            }
        });
        group->start(QAbstractAnimation::DeleteWhenStopped);
    });

    layout->addWidget(button);
    m_navButtons.append(button);
}

QWidget* DashboardWindow::makeQuickAccessRow(const QString& label, PillBadge** badgeOut) {
    auto* container = new QWidget;
    container->setObjectName(QStringLiteral("quickAccessRow"));
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(8);

    auto* textLabel = new QLabel(label);
    textLabel->setObjectName(QStringLiteral("quickAccessLabel"));
    layout->addWidget(textLabel);
    layout->addStretch(1);

    auto* badge = new PillBadge(QStringLiteral("--"));
    layout->addWidget(badge);
    if (badgeOut) {
        *badgeOut = badge;
    }
    return container;
}

void DashboardWindow::registerSignals() {
    connect(m_api, &ApiClient::stateReceived, this, &DashboardWindow::handleState);
    connect(m_api, &ApiClient::diagnosticsReceived, this, &DashboardWindow::handleDiagnostics);
    connect(m_api, &ApiClient::stateFetchFailed, this, [this](const QString& error) {
        setConnected(false);
        const QString normalizedError = error.trimmed().isEmpty()
            ? QStringLiteral("Backend unavailable")
            : error.trimmed();
        if (m_statusText) {
            m_statusText->setText(normalizedError);
        }
        const bool shouldNotify = !m_backendDisconnectNotified
            || normalizedError != m_lastBackendDisconnectError;
        m_backendDisconnectNotified = true;
        m_lastBackendDisconnectError = normalizedError;
        if (shouldNotify) {
            showNotification(QStringLiteral("Backend Connection"), normalizedError, QStringLiteral("error"));
        }
    });
    connect(m_api, &ApiClient::actionDone, this, [this](const QJsonObject& response) {
        const bool ok = response.value(QStringLiteral("ok")).toBool(true);
        const QString message = response.value(QStringLiteral("message")).toString(
            response.value(QStringLiteral("error")).toString(ok ? QStringLiteral("Action completed.") : QStringLiteral("Action failed.")));
        if (!message.trimmed().isEmpty()) {
            showNotification(ok ? QStringLiteral("Action Complete") : QStringLiteral("Action Failed"),
                             message,
                             ok ? QStringLiteral("info") : QStringLiteral("error"));
        }
        refreshState();
        refreshDiagnostics();
    });
    connect(m_api, &ApiClient::configSaved, this, [this](bool ok, const QString& message) {
        if (m_statusText) {
            m_statusText->setText(ok ? QStringLiteral("Configuration saved") : QStringLiteral("Configuration save failed"));
        }
        if (m_statusMeta && !message.isEmpty()) {
            m_statusMeta->setText(message);
        }
        if (!message.trimmed().isEmpty()) {
            showNotification(ok ? QStringLiteral("Configuration Saved") : QStringLiteral("Configuration Error"),
                             message,
                             ok ? QStringLiteral("info") : QStringLiteral("error"));
        }
        refreshState();
    });
}

void DashboardWindow::refreshState() {
    m_api->fetchState();
}

void DashboardWindow::refreshDiagnostics() {
    m_api->fetchDiagnostics();
}

void DashboardWindow::handleState(const QJsonObject& state) {
    setConnected(true);
    m_backendDisconnectNotified = false;
    m_lastBackendDisconnectError.clear();
    updateSidebarMetrics(state);

    m_overviewPage->updateFromState(state);
    m_metricsPage->updateFromState(state);
    m_settingsPage->updateFromState(state);
    m_dataPage->updateFromState(state);
    m_jobsPage->updateFromState(state);
    m_chatPage->updateFromState(state);
    m_reportsPage->updateFromState(state);
    m_liveFeedPage->updateFromState(state);
    m_diagnosticsPage->updateFromState(state);

    const QJsonObject primaryJob = state.value(QStringLiteral("primary_job")).toObject();
    const QString stage = primaryJob.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    const double progress = primaryJob.value(QStringLiteral("progress")).toDouble();
    const QString eta = primaryJob.value(QStringLiteral("eta")).toString(QStringLiteral("ETA unavailable"));
    const QColor statusColor = stage == QStringLiteral("failed")
        ? QColor(QStringLiteral("#ef4444"))
        : (stage == QStringLiteral("idle") ? QColor(QStringLiteral("#22c55e")) : QColor(QStringLiteral("#3b82f6")));

    if (m_statusIndicator) {
        const QString indicatorText = stage == QStringLiteral("idle")
            ? QStringLiteral("Ready")
            : Fmt::prettyJobName(primaryJob.value(QStringLiteral("job")).toString());
        m_statusIndicator->setStatus(indicatorText, statusColor);
    }
    if (m_statusText) {
        m_statusText->setText(primaryJob.value(QStringLiteral("message")).toString(QStringLiteral("Waiting for activity")));
    }
    if (m_statusMeta) {
        m_statusMeta->setText(QStringLiteral("%1 - %2 - %3")
            .arg(Fmt::progressPct(progress))
            .arg(eta)
            .arg(Fmt::relativeTime(primaryJob.value(QStringLiteral("updated_at")).toDouble())));
    }

    const QJsonArray alerts = state.value(QStringLiteral("alerts")).toArray();
    if (!alerts.isEmpty()) {
        const QJsonObject latestAlert = alerts.last().toObject();
        const double ts = latestAlert.value(QStringLiteral("ts")).toDouble();
        if (ts > m_lastAlertTs) {
            m_lastAlertTs = ts;
            const QString severity = latestAlert.value(QStringLiteral("severity")).toString(QStringLiteral("info"));
            const QString message = latestAlert.value(QStringLiteral("message")).toString();
            showNotification(QStringLiteral("Backend Alert"), message, severity);
        }
    }
}

void DashboardWindow::handleDiagnostics(const QJsonObject& diagnostics) {
    m_diagnosticsPage->updateFromDiagnostics(diagnostics);
}

void DashboardWindow::updateSidebarMetrics(const QJsonObject& state) {
    const QJsonObject hardware = state.value(QStringLiteral("hardware")).toObject();
    const QJsonObject config = state.value(QStringLiteral("config")).toObject();
    const QJsonArray feed = state.value(QStringLiteral("feed")).toArray();

    if (m_gpuVal) {
        m_gpuVal->setText(compactGpuName(hardware.value(QStringLiteral("gpu_name")).toString()));
    }
    if (m_loadVal) {
        const int gpuUtil = hardware.value(QStringLiteral("gpu_utilization")).toInt(-1);
        m_loadVal->setText(gpuUtil >= 0 ? QStringLiteral("%1%").arg(gpuUtil) : QStringLiteral("--"));
    }
    if (m_ramVal) {
        const double used = hardware.value(QStringLiteral("ram_used_mb")).toDouble() / 1024.0;
        const double total = hardware.value(QStringLiteral("ram_total_mb")).toDouble() / 1024.0;
        m_ramVal->setText(total > 0.0 ? QStringLiteral("%1 / %2 GB").arg(Fmt::fmtDouble(used, 1), Fmt::fmtDouble(total, 1)) : QStringLiteral("--"));
    }

    const QJsonObject datasets = config.value(QStringLiteral("datasets")).toObject();
    const int maxSamples = datasets.value(QStringLiteral("max_samples")).toInt();
    if (m_datasetBadge) {
        m_datasetBadge->setText(maxSamples > 0 ? QStringLiteral("%1k").arg(qMax(1, maxSamples / 1000)) : QStringLiteral("--"));
    }
    if (m_systemBadge) {
        const int gpuUtil = hardware.value(QStringLiteral("gpu_utilization")).toInt(-1);
        m_systemBadge->setText(gpuUtil >= 0 ? QStringLiteral("%1%").arg(gpuUtil) : QStringLiteral("--"));
    }
    if (m_logsBadge) {
        m_logsBadge->setText(feed.isEmpty() ? QStringLiteral("Idle") : QStringLiteral("Live"));
    }
}

void DashboardWindow::setConnected(bool connected) {
    if (!m_brandStatusLight) {
        return;
    }
    m_brandStatusLight->setProperty("connected", connected);
    m_brandStatusLight->setToolTip(connected ? QStringLiteral("Backend connected") : QStringLiteral("Backend disconnected"));
    Fmt::repolish(m_brandStatusLight);
}

void DashboardWindow::applyMode(int index) {
    const bool advanced = index > 0;
    m_overviewPage->setAdvancedMode(advanced);
    m_settingsPage->setAdvancedMode(advanced);
}

void DashboardWindow::applySidebarCollapsed(bool collapsed) {
    m_sidebarCollapsed = collapsed;
    const int targetWidth = collapsed ? 64 : 240;
    if (m_sidebar) {
        auto* minAnim = new QPropertyAnimation(m_sidebar, "minimumWidth", this);
        minAnim->setDuration(220);
        minAnim->setStartValue(m_sidebar->width());
        minAnim->setEndValue(targetWidth);
        minAnim->setEasingCurve(QEasingCurve::OutCubic);

        auto* maxAnim = new QPropertyAnimation(m_sidebar, "maximumWidth", this);
        maxAnim->setDuration(220);
        maxAnim->setStartValue(m_sidebar->width());
        maxAnim->setEndValue(targetWidth);
        maxAnim->setEasingCurve(QEasingCurve::OutCubic);

        auto* group = new QParallelAnimationGroup(this);
        group->addAnimation(minAnim);
        group->addAnimation(maxAnim);
        group->start(QAbstractAnimation::DeleteWhenStopped);
    }
    if (m_sidebarToggle) {
        m_sidebarToggle->setText(collapsed ? QStringLiteral("\u25B6") : QStringLiteral("\u25C0"));
        m_sidebarToggle->setToolTip(collapsed ? QStringLiteral("Expand sidebar")
                                              : QStringLiteral("Collapse sidebar"));
    }
    if (m_brandBadge) {
        m_brandBadge->setVisible(!collapsed);
    }
    for (QPushButton* button : std::as_const(m_navButtons)) {
        if (!button) {
            continue;
        }
        button->setText(collapsed ? button->property("compactText").toString() : button->property("fullText").toString());
        button->setMinimumHeight(collapsed ? 40 : 36);
        button->setProperty("collapsed", collapsed);
        Fmt::repolish(button);
    }
    for (QLabel* label : std::as_const(m_sidebarLabels)) {
        if (label) {
            label->setVisible(!collapsed);
        }
    }
    for (QWidget* widget : std::as_const(m_hideWhenCollapsed)) {
        if (widget) {
            widget->setVisible(!collapsed);
        }
    }
    if (m_pages && m_pages->currentWidget()) {
        QWidget* current = m_pages->currentWidget();
        current->move(0, 0);
        current->setGeometry(m_pages->rect());
    }
    positionNotificationToast();
}

void DashboardWindow::showNotification(const QString& title, const QString& message, const QString& severity) {
    if (!m_notificationToast || !m_notificationTitle || !m_notificationMessage) {
        return;
    }
    const QString normalizedTitle = title.trimmed().isEmpty() ? QStringLiteral("Notification") : title.trimmed();
    const QString normalizedMessage = message.trimmed().isEmpty()
        ? QStringLiteral("No details available.")
        : message.trimmed();
    const QString fingerprint = QStringLiteral("%1|%2|%3")
        .arg(severity, normalizedTitle, normalizedMessage);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (fingerprint == m_lastNotificationFingerprint && (nowMs - m_lastNotificationAtMs) < 5000) {
        return;
    }
    m_lastNotificationFingerprint = fingerprint;
    m_lastNotificationAtMs = nowMs;

    m_notificationToast->setProperty("severity", severity);
    m_notificationTitle->setText(normalizedTitle);
    m_notificationMessage->setText(normalizedMessage);
    Fmt::repolish(m_notificationToast);
    positionNotificationToast();
    m_notificationToast->show();
    m_notificationToast->raise();

    if (m_notificationOpacity) {
        m_notificationOpacity->stop();
        m_notificationOpacity->setStartValue(0.0);
        m_notificationOpacity->setEndValue(1.0);
        m_notificationOpacity->start();
    }
    if (m_notificationTimer) {
        m_notificationTimer->start(severity == QStringLiteral("error") ? 7000 : 4500);
    }
}

void DashboardWindow::positionNotificationToast() {
    if (!m_notificationToast || !centralWidget()) {
        return;
    }
    const int margin = 22;
    const QSize hint = m_notificationToast->sizeHint();
    const int width = qMax(360, hint.width());
    const int height = qMax(84, hint.height());
    m_notificationToast->resize(width, height);
    m_notificationToast->move(
        centralWidget()->width() - width - margin,
        centralWidget()->height() - height - 74);
}
