#include "JobsPage.h"

#include "../app/ApiClient.h"
#include "../util/Formatters.h"
#include "../widgets/GlowCard.h"
#include "../widgets/GradientProgressBar.h"
#include <algorithm>
#include <QBoxLayout>
#include <QColor>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QResizeEvent>
#include <QSet>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {

struct JobDisplayState {
    bool running = false;
    bool paused = false;
    QString statusText = QStringLiteral("Idle");
    QColor statusColor = QColor(QStringLiteral("#8b949e"));
};

QString prettyStageName(const QString& stage) {
    if (stage.isEmpty() || stage == QStringLiteral("idle")) {
        return QString();
    }
    QString text = stage;
    text.replace(QLatin1Char('_'), QLatin1Char(' '));
    text.replace(QLatin1Char('-'), QLatin1Char(' '));
    bool capitalize = true;
    for (QChar& ch : text) {
        if (capitalize && ch.isLetter()) {
            ch = ch.toUpper();
            capitalize = false;
        } else if (ch == QLatin1Char(' ')) {
            capitalize = true;
        } else {
            ch = ch.toLower();
        }
    }
    return text;
}

QStringList filteredLogLines(const QJsonArray& logRows) {
    QStringList preferred;
    for (const QJsonValue& value : logRows) {
        const QString line = value.toString().trimmed();
        if (line.isEmpty()) {
            continue;
        }
        if (line.contains(QStringLiteral("AI_PROGRESS|"))) {
            continue;
        }
        if (line.startsWith(QStringLiteral("[progress]"))) {
            continue;
        }

        if (line.startsWith(QStringLiteral("Requirement already satisfied:"), Qt::CaseInsensitive)) {
            continue;
        }
        if (line.startsWith(QStringLiteral("Collecting "), Qt::CaseInsensitive)) {
            continue;
        }
        if (line.contains(QStringLiteral(".metadata"), Qt::CaseInsensitive)) {
            continue;
        }
        preferred.append(line);
    }

    while (preferred.size() > 3) {
        preferred.removeFirst();
    }
    return preferred;
}

void clearLayoutContents(QLayout* layout) {
    if (!layout) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

int desiredColumnCount(int availableWidth) {
    if (availableWidth >= 1680) {
        return 3;
    }
    if (availableWidth >= 1080) {
        return 2;
    }
    return 1;
}

int desiredSummaryColumnCount(int availableWidth) {
    if (availableWidth >= 1560) {
        return 4;
    }
    if (availableWidth >= 940) {
        return 2;
    }
    return 1;
}

QString badgeStyle(const QColor& textColor, const QColor& fillColor, const QColor& borderColor) {
    return QStringLiteral(
        "color: %1; background: %2; border: 1px solid %3; border-radius: 10px; "
        "padding: 3px 9px; font-size: 11px; font-weight: 700;")
        .arg(textColor.name(QColor::HexRgb),
             fillColor.name(QColor::HexArgb),
             borderColor.name(QColor::HexArgb));
}

JobDisplayState describeJobState(const QString& name,
                                 const QJsonObject& job,
                                 const QJsonObject& process,
                                 const QJsonObject& state) {
    const QJsonObject autopilot = state.value(QStringLiteral("autopilot")).toObject();
    const bool autopilotActive = (name == QStringLiteral("autopilot")) && autopilot.value(QStringLiteral("active")).toBool(false);

    JobDisplayState display;
    display.running = autopilotActive || process.value(QStringLiteral("running")).toBool(false);
    display.paused = (name == QStringLiteral("autopilot"))
        ? autopilot.value(QStringLiteral("paused")).toBool(false)
        : process.value(QStringLiteral("paused")).toBool(false);

    const QString stage = job.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    const double progress = qBound(0.0, job.value(QStringLiteral("progress")).toDouble(), 1.0);

    if (display.paused) {
        display.statusText = QStringLiteral("Paused");
        display.statusColor = QColor(QStringLiteral("#e3b341"));
    } else if (autopilotActive) {
        display.statusText = QStringLiteral("Active");
        display.statusColor = QColor(QStringLiteral("#22c55e"));
    } else if (display.running) {
        display.statusText = QStringLiteral("Running");
        display.statusColor = QColor(QStringLiteral("#3fb950"));
    } else if (stage == QStringLiteral("failed")) {
        display.statusText = QStringLiteral("Failed");
        display.statusColor = QColor(QStringLiteral("#f85149"));
    } else if (stage == QStringLiteral("completed")) {
        display.statusText = QStringLiteral("Completed");
        display.statusColor = QColor(QStringLiteral("#22c55e"));
    } else if (stage == QStringLiteral("stopped")) {
        display.statusText = QStringLiteral("Stopped");
        display.statusColor = QColor(QStringLiteral("#8b949e"));
    } else if (stage == QStringLiteral("starting")) {
        display.statusText = QStringLiteral("Starting");
        display.statusColor = QColor(QStringLiteral("#58a6ff"));
    } else if (stage != QStringLiteral("idle") && (!job.value(QStringLiteral("message")).toString().trimmed().isEmpty() || progress > 0.0)) {
        display.statusText = QStringLiteral("Pending");
        display.statusColor = QColor(QStringLiteral("#79c0ff"));
    }

    return display;
}

QString jobDescription(const QString& name) {
    if (name == QStringLiteral("setup")) return QStringLiteral("Repair the Python environment, dependencies, and CUDA stack.");
    if (name == QStringLiteral("prepare")) return QStringLiteral("Download assets, build the tokenizer, and prepare the scored dataset.");
    if (name == QStringLiteral("training")) return QStringLiteral("Continue the model run from the latest durable checkpoint.");
    if (name == QStringLiteral("evaluate")) return QStringLiteral("Generate the latest evaluation report against the freshest checkpoint.");
    if (name == QStringLiteral("inference")) return QStringLiteral("Launch the inference API on the latest available model weights.");
    if (name == QStringLiteral("autopilot")) return QStringLiteral("Chain setup, preparation, training, and evaluation automatically.");
    return QStringLiteral("Managed by the native backend.");
}

QString prerequisiteJob(const QString& name) {
    if (name == QStringLiteral("prepare")) return QStringLiteral("setup");
    if (name == QStringLiteral("training")) return QStringLiteral("prepare");
    if (name == QStringLiteral("evaluate")) return QStringLiteral("training");
    return QString();
}

QString actionEndpointForJob(const QString& name, const QString& action = QString()) {
    if (name == QStringLiteral("training")) {
        if (action == QStringLiteral("pause")) return QStringLiteral("/api/actions/train/pause");
        if (action == QStringLiteral("resume")) return QStringLiteral("/api/actions/train/resume");
        if (action == QStringLiteral("stop")) return QStringLiteral("/api/actions/train/stop");
        return QStringLiteral("/api/actions/train/start");
    }
    if (name == QStringLiteral("autopilot")) {
        if (action == QStringLiteral("pause")) return QStringLiteral("/api/actions/autopilot/pause");
        if (action == QStringLiteral("resume")) return QStringLiteral("/api/actions/autopilot/resume");
        if (action == QStringLiteral("stop")) return QStringLiteral("/api/actions/autopilot/stop");
        return QStringLiteral("/api/actions/autopilot/start");
    }
    if (name == QStringLiteral("inference")) {
        if (action == QStringLiteral("pause")) return QStringLiteral("/api/actions/inference/pause");
        if (action == QStringLiteral("resume")) return QStringLiteral("/api/actions/inference/resume");
        if (action == QStringLiteral("stop")) return QStringLiteral("/api/actions/inference/stop");
        return QStringLiteral("/api/actions/inference/start");
    }
    if (name == QStringLiteral("setup") || name == QStringLiteral("prepare") || name == QStringLiteral("evaluate")) {
        if (action == QStringLiteral("pause")) return QStringLiteral("/api/actions/%1/pause").arg(name);
        if (action == QStringLiteral("resume")) return QStringLiteral("/api/actions/%1/resume").arg(name);
        return QStringLiteral("/api/actions/%1").arg(name);
    }
    return QString();
}

QString nextRecommendedJob(const QJsonObject& jobs) {
    if (jobs.value(QStringLiteral("setup")).toObject().value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("setup");
    }
    if (jobs.value(QStringLiteral("prepare")).toObject().value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("prepare");
    }
    if (jobs.value(QStringLiteral("training")).toObject().value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("training");
    }
    if (jobs.value(QStringLiteral("evaluate")).toObject().value(QStringLiteral("stage")).toString() != QStringLiteral("completed")) {
        return QStringLiteral("evaluate");
    }
    return QStringLiteral("inference");
}

QString recoveryWindowText(double seconds) {
    const qint64 totalSeconds = qMax<qint64>(0, static_cast<qint64>(seconds + 0.5));
    if (totalSeconds < 60) return QStringLiteral("%1s").arg(totalSeconds);
    if (totalSeconds < 3600) return QStringLiteral("%1m").arg((totalSeconds + 30) / 60);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    if (minutes <= 0) return QStringLiteral("%1h").arg(hours);
    return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
}

bool isJobBlocked(const QString& name, const QJsonObject& jobs) {
    const QString prereq = prerequisiteJob(name);
    if (prereq.isEmpty()) {
        return false;
    }
    return jobs.value(prereq).toObject().value(QStringLiteral("stage")).toString(QStringLiteral("idle")) != QStringLiteral("completed");
}

bool canResumeJob(const QString& name,
                  const QJsonObject& job,
                  const QJsonObject& process,
                  const QJsonObject& state,
                  const JobDisplayState& display) {
    const QString stage = job.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    const double progress = qBound(0.0, job.value(QStringLiteral("progress")).toDouble(), 1.0);
    const double updatedAt = job.value(QStringLiteral("updated_at")).toDouble(0.0);
    const QJsonObject recovery = state.value(QStringLiteral("recovery")).toObject();
    if (display.running || stage == QStringLiteral("completed")) {
        return false;
    }
    if (name == QStringLiteral("autopilot")) {
        return recovery.value(QStringLiteral("can_resume")).toBool(false)
            || state.value(QStringLiteral("autopilot")).toObject().value(QStringLiteral("paused")).toBool(false);
    }
    if (process.value(QStringLiteral("paused")).toBool(false) || stage == QStringLiteral("paused")) {
        return true;
    }
    if (stage == QStringLiteral("failed")) {
        if (recovery.value(QStringLiteral("can_resume")).toBool(false)
            && recovery.value(QStringLiteral("job")).toString() == name) {
            return true;
        }
        if (name == QStringLiteral("training")) {
            return state.value(QStringLiteral("checkpoint")).toObject().value(QStringLiteral("available")).toBool(false);
        }
        return false;
    }
    if (name == QStringLiteral("training")) {
        return state.value(QStringLiteral("checkpoint")).toObject().value(QStringLiteral("available")).toBool(false);
    }
    if (stage == QStringLiteral("stopped")) {
        return progress > 0.0 || updatedAt > 0.0;
    }
    return progress > 0.0 && updatedAt > 0.0;
}

QString focusActionLabelForJob(const QString& name) {
    if (name == QStringLiteral("autopilot")) {
        return QStringLiteral("Start Smart Run");
    }
    if (name == QStringLiteral("inference")) {
        return QStringLiteral("Start API");
    }
    if (name == QStringLiteral("training")) {
        return QStringLiteral("Start Training");
    }
    return QStringLiteral("Run %1").arg(Fmt::prettyJobName(name));
}

bool isPausedState(const QString& name,
                   const QJsonObject& job,
                   const QJsonObject& process,
                   const QJsonObject& state) {
    if (name == QStringLiteral("autopilot")) {
        const QJsonObject autopilot = state.value(QStringLiteral("autopilot")).toObject();
        const QJsonObject recovery = state.value(QStringLiteral("recovery")).toObject();
        return autopilot.value(QStringLiteral("paused")).toBool(false)
            || recovery.value(QStringLiteral("paused")).toBool(false);
    }
    return process.value(QStringLiteral("paused")).toBool(false)
        || job.value(QStringLiteral("stage")).toString() == QStringLiteral("paused");
}

QString continueEndpointForJob(const QString& name,
                               const QJsonObject& job,
                               const QJsonObject& process,
                               const QJsonObject& state) {
    const bool paused = isPausedState(name, job, process, state);
    if (name == QStringLiteral("autopilot")) {
        return QStringLiteral("/api/actions/autopilot/resume");
    }
    if (name == QStringLiteral("training")) {
        return paused ? QStringLiteral("/api/actions/train/resume")
                      : QStringLiteral("/api/actions/train/start");
    }
    if (name == QStringLiteral("inference")) {
        return paused ? QStringLiteral("/api/actions/inference/resume")
                      : QStringLiteral("/api/actions/inference/start");
    }
    if (name == QStringLiteral("setup") || name == QStringLiteral("prepare") || name == QStringLiteral("evaluate")) {
        return paused
            ? QStringLiteral("/api/actions/%1/resume").arg(name)
            : QStringLiteral("/api/actions/%1").arg(name);
    }
    return QString();
}

QString continueActionLabelForJob(const QString& name,
                                  const QJsonObject& job,
                                  const QJsonObject& process,
                                  const QJsonObject& state,
                                  bool autopilotControl = false) {
    const bool paused = isPausedState(name, job, process, state);
    if (autopilotControl || name == QStringLiteral("autopilot")) {
        return paused ? QStringLiteral("Resume Smart Run") : QStringLiteral("Continue Smart Run");
    }
    return QStringLiteral("%1 %2")
        .arg(paused ? QStringLiteral("Resume") : QStringLiteral("Continue"),
             Fmt::prettyJobName(name));
}

}  // namespace

JobsPage::JobsPage(ApiClient* api, QWidget* parent)
    : BasePage(api, parent),
      m_jobOrder({
          QStringLiteral("setup"),
          QStringLiteral("prepare"),
          QStringLiteral("training"),
          QStringLiteral("evaluate"),
          QStringLiteral("inference"),
          QStringLiteral("autopilot"),
      })
{
    auto* page = new QWidget;
    m_pageRoot = page;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(28, 26, 28, 28);
    lay->setSpacing(18);

    auto* titleBlock = new QWidget;
    auto* titleLay = new QVBoxLayout(titleBlock);
    titleLay->setContentsMargins(0, 0, 0, 0);
    titleLay->setSpacing(4);

    auto* title = new QLabel(QStringLiteral("Job Center"));
    title->setObjectName(QStringLiteral("pageTitle"));
    titleLay->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral(
        "Use this page as an operations console: see what is active, what can resume, what needs attention, and what should run next."));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    subtitle->setWordWrap(true);
    titleLay->addWidget(subtitle);
    lay->addWidget(titleBlock);

    m_headerLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_headerLayout->setContentsMargins(0, 0, 0, 0);
    m_headerLayout->setSpacing(16);

    m_headerPrimaryColumn = new QWidget;
    auto* headerPrimaryLayout = new QVBoxLayout(m_headerPrimaryColumn);
    headerPrimaryLayout->setContentsMargins(0, 0, 0, 0);
    headerPrimaryLayout->setSpacing(14);

    m_focusCard = new QWidget;
    m_focusCard->setObjectName(QStringLiteral("missionCard"));
    m_focusCard->setAttribute(Qt::WA_StyledBackground, true);
    m_focusCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* focusLay = new QVBoxLayout(m_focusCard);
    focusLay->setContentsMargins(20, 16, 20, 16);
    focusLay->setSpacing(0);

    auto* focusHeader = new QHBoxLayout;
    focusHeader->setContentsMargins(0, 0, 0, 0);
    focusHeader->setSpacing(10);
    auto* focusLabel = new QLabel(QStringLiteral("MISSION CONTROL"));
    focusLabel->setObjectName(QStringLiteral("missionCardLabel"));
    focusHeader->addWidget(focusLabel);
    focusHeader->addStretch(1);
    m_focusStatus = new QLabel(QStringLiteral("idle"));
    m_focusStatus->setObjectName(QStringLiteral("missionStatus"));
    focusHeader->addWidget(m_focusStatus);
    focusLay->addLayout(focusHeader);
    focusLay->addSpacing(10);

    m_focusName = new QLabel(QStringLiteral("No active workflow"));
    m_focusName->setObjectName(QStringLiteral("missionName"));
    m_focusName->setWordWrap(true);
    focusLay->addWidget(m_focusName);
    focusLay->addSpacing(6);

    m_focusSummary = new QLabel(QStringLiteral("Everything is ready for the next action."));
    m_focusSummary->setObjectName(QStringLiteral("missionSummary"));
    m_focusSummary->setWordWrap(true);
    focusLay->addWidget(m_focusSummary);
    focusLay->addSpacing(12);

    auto makeFocusPanel = [](const QString& labelText, QLabel*& valueLabel, const QString& valueText) {
        auto* panel = new QWidget;
        panel->setObjectName(QStringLiteral("jobsFocusPanel"));
        panel->setAttribute(Qt::WA_StyledBackground, true);
        auto* panelLay = new QVBoxLayout(panel);
        panelLay->setContentsMargins(12, 11, 12, 11);
        panelLay->setSpacing(5);

        auto* label = new QLabel(labelText);
        label->setObjectName(QStringLiteral("missionMetaLabel"));
        panelLay->addWidget(label);

        valueLabel = new QLabel(valueText);
        valueLabel->setObjectName(QStringLiteral("missionMetaValue"));
        valueLabel->setWordWrap(true);
        panelLay->addWidget(valueLabel);
        return panel;
    };

    auto* metaGrid = new QGridLayout;
    metaGrid->setContentsMargins(0, 0, 0, 0);
    metaGrid->setHorizontalSpacing(12);
    metaGrid->setVerticalSpacing(12);
    metaGrid->addWidget(
        makeFocusPanel(QStringLiteral("NEXT MOVE"),
                       m_focusNextValue,
                       QStringLiteral("Run Environment Setup next.")),
        0,
        0);
    metaGrid->addWidget(
        makeFocusPanel(QStringLiteral("RECOVERY"),
                       m_focusRecoveryValue,
                       QStringLiteral("No recovery point is needed yet.")),
        0,
        1);
    metaGrid->setColumnStretch(0, 1);
    metaGrid->setColumnStretch(1, 1);
    focusLay->addLayout(metaGrid);
    focusLay->addSpacing(12);

    auto* divider = new QWidget;
    divider->setObjectName(QStringLiteral("missionDivider"));
    divider->setAttribute(Qt::WA_StyledBackground, true);
    focusLay->addWidget(divider);
    focusLay->addSpacing(12);

    m_focusFooterLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    m_focusFooterLayout->setContentsMargins(0, 0, 0, 0);
    m_focusFooterLayout->setSpacing(12);

    m_focusActionBtn = new QPushButton(QStringLiteral("Run Next Step"));
    m_focusActionBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
    m_focusActionBtn->setMinimumHeight(40);
    m_focusActionBtn->setMinimumWidth(180);
    m_focusActionBtn->setProperty("actionPath", QStringLiteral("/api/actions/setup"));
    connect(m_focusActionBtn, &QPushButton::clicked, this, [this]() {
        if (!m_focusActionBtn) {
            return;
        }
        const QString actionPath = m_focusActionBtn->property("actionPath").toString();
        if (!actionPath.isEmpty()) {
            m_api->postAction(actionPath);
        }
    });
    m_focusFooterLayout->addWidget(m_focusActionBtn, 0, Qt::AlignLeft);
    m_focusFooterLayout->addStretch(1);

    m_focusMeta = new QLabel(QStringLiteral("0.0%  -  Updated recently"));
    m_focusMeta->setObjectName(QStringLiteral("jobsFocusMeta"));
    m_focusMeta->setWordWrap(true);
    m_focusFooterLayout->addWidget(m_focusMeta, 1, Qt::AlignVCenter);
    focusLay->addLayout(m_focusFooterLayout);

    headerPrimaryLayout->addWidget(m_focusCard);

    m_summaryContainer = new QWidget;
    m_summaryContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_summaryGrid = new QGridLayout(m_summaryContainer);
    m_summaryGrid->setContentsMargins(0, 0, 0, 0);
    m_summaryGrid->setHorizontalSpacing(12);
    m_summaryGrid->setVerticalSpacing(12);
    m_summaryGrid->setSizeConstraint(QLayout::SetMinimumSize);

    auto makeStat = [this](const QString& label, QLabel*& valueOut) -> GlowCard* {
        auto* card = new GlowCard(label);
        card->setMinimumHeight(92);
        valueOut = new QLabel(QStringLiteral("0"));
        valueOut->setObjectName(QStringLiteral("summaryChipValue"));
        valueOut->setWordWrap(true);
        card->contentLayout()->addWidget(valueOut);
        m_summaryCards.append(card);
        return card;
    };

    makeStat(QStringLiteral("RUNNING"), m_activeCount);
    makeStat(QStringLiteral("ACTIONABLE"), m_readyCount);
    makeStat(QStringLiteral("RESUMABLE"), m_resumableCount);
    makeStat(QStringLiteral("ATTENTION"), m_attentionCount);
    headerPrimaryLayout->addWidget(m_summaryContainer);
    m_headerLayout->addWidget(m_headerPrimaryColumn, 3, Qt::AlignTop);

    m_activityCard = new GlowCard(QStringLiteral("Recent Operations"));
    m_activityCard->setGlowColor(QColor(QStringLiteral("#2563eb")));
    auto* activityLay = m_activityCard->contentLayout();
    activityLay->setSpacing(12);
    m_activitySummary = new QLabel(QStringLiteral("Backend events, alerts, and recovery milestones will surface here."));
    m_activitySummary->setObjectName(QStringLiteral("jobsActivitySummary"));
    m_activitySummary->setWordWrap(true);
    activityLay->addWidget(m_activitySummary);

    m_activityListHost = new QWidget;
    m_activityList = new QVBoxLayout(m_activityListHost);
    m_activityList->setContentsMargins(0, 2, 0, 0);
    m_activityList->setSpacing(10);
    activityLay->addWidget(m_activityListHost);
    m_activityCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_activityCard->setMinimumWidth(360);
    m_headerLayout->addWidget(m_activityCard, 2, Qt::AlignTop);
    lay->addLayout(m_headerLayout);

    auto* sectionTitle = new QLabel(QStringLiteral("Managed Workflows"));
    sectionTitle->setObjectName(QStringLiteral("sectionTitle"));
    lay->addWidget(sectionTitle);

    auto* sectionSub = new QLabel(QStringLiteral(
        "Each block shows its current state, the fastest valid action, and the latest useful context from the backend."));
    sectionSub->setObjectName(QStringLiteral("pageSubtitle"));
    sectionSub->setWordWrap(true);
    lay->addWidget(sectionSub);

    m_jobContainer = new QWidget;
    m_jobContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_jobGrid = new QGridLayout(m_jobContainer);
    m_jobGrid->setContentsMargins(0, 0, 0, 0);
    m_jobGrid->setHorizontalSpacing(14);
    m_jobGrid->setVerticalSpacing(14);
    m_jobGrid->setSizeConstraint(QLayout::SetMinimumSize);
    lay->addWidget(m_jobContainer);
    lay->addStretch();

    auto* wrapper = buildScrollWrapper(page);
    auto* outerLay = new QVBoxLayout(this);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->addWidget(wrapper);

    ensureCards();
    rebuildSummaryGrid();
    updateResponsiveLayout();
}

void JobsPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

void JobsPage::ensureCards()
{
    if (!m_jobCards.isEmpty()) {
        return;
    }

    for (const QString& name : m_jobOrder) {
        JobCardUi ui;
        ui.card = new GlowCard(Fmt::prettyJobName(name), m_jobContainer);
        ui.card->setMinimumHeight(214);
        ui.card->setMinimumWidth(360);
        ui.card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

        auto* content = ui.card->contentLayout();
        content->setSpacing(12);

        auto* headerRow = new QHBoxLayout;
        headerRow->setContentsMargins(0, 0, 0, 0);
        headerRow->setSpacing(8);

        ui.statusLabel = new QLabel(QStringLiteral("Idle"));
        ui.statusLabel->setAlignment(Qt::AlignCenter);
        ui.statusLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        headerRow->addWidget(ui.statusLabel);

        ui.stageLabel = new QLabel;
        ui.stageLabel->setAlignment(Qt::AlignCenter);
        ui.stageLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        ui.stageLabel->hide();
        headerRow->addWidget(ui.stageLabel);

        headerRow->addStretch(1);

        ui.pctLabel = new QLabel(QStringLiteral("0.0%"));
        ui.pctLabel->setObjectName(QStringLiteral("summaryChipValue"));
        headerRow->addWidget(ui.pctLabel);
        content->addLayout(headerRow);

        ui.progressBar = new GradientProgressBar;
        ui.progressBar->setFixedHeight(16);
        ui.progressBar->setValue(0.0);
        content->addWidget(ui.progressBar);

        ui.messageLabel = new QLabel;
        ui.messageLabel->setObjectName(QStringLiteral("jobsCardMessage"));
        ui.messageLabel->setWordWrap(true);
        ui.messageLabel->setMaximumHeight(54);
        ui.messageLabel->hide();
        content->addWidget(ui.messageLabel);

        ui.etaLabel = new QLabel;
        ui.etaLabel->setObjectName(QStringLiteral("jobsCardMeta"));
        ui.etaLabel->setWordWrap(true);
        ui.etaLabel->hide();
        content->addWidget(ui.etaLabel);

        ui.noteLabel = new QLabel;
        ui.noteLabel->setObjectName(QStringLiteral("jobsCardNote"));
        ui.noteLabel->setWordWrap(true);
        ui.noteLabel->setMaximumHeight(58);
        ui.noteLabel->hide();
        content->addWidget(ui.noteLabel);

        ui.logLabel = new QLabel;
        ui.logLabel->setObjectName(QStringLiteral("jobLog"));
        ui.logLabel->setWordWrap(true);
        ui.logLabel->setMaximumHeight(54);
        ui.logLabel->hide();
        content->addWidget(ui.logLabel);

        content->addStretch(1);

        ui.actionRow = new QHBoxLayout;
        ui.actionRow->setContentsMargins(0, 6, 0, 0);
        ui.actionRow->setSpacing(8);
        content->addLayout(ui.actionRow);

        m_jobCards.insert(name, ui);
    }
}

void JobsPage::rebuildGrid()
{
    if (!m_jobGrid || !m_jobContainer) {
        return;
    }

    const int columns = desiredColumnCount(contentWidth());
    if (m_columnCount == columns && m_jobGrid->count() == m_jobOrder.size()) {
        return;
    }

    m_columnCount = columns;
    while (QLayoutItem* item = m_jobGrid->takeAt(0)) {
        delete item;
    }

    for (int i = 0; i < m_jobOrder.size(); ++i) {
        const JobCardUi ui = m_jobCards.value(m_jobOrder.at(i));
        if (!ui.card) {
            continue;
        }
        const int row = i / columns;
        const int col = i % columns;
        m_jobGrid->addWidget(ui.card, row, col);
    }

    for (int col = 0; col < 3; ++col) {
        m_jobGrid->setColumnStretch(col, col < columns ? 1 : 0);
    }
}

void JobsPage::rebuildSummaryGrid()
{
    if (!m_summaryGrid || !m_summaryContainer) {
        return;
    }

    const int availableWidth = m_summaryContainer->width() > 0
        ? m_summaryContainer->width()
        : (m_headerPrimaryColumn && m_headerPrimaryColumn->width() > 0
            ? m_headerPrimaryColumn->width()
            : contentWidth());
    const int columns = desiredSummaryColumnCount(availableWidth);
    if (m_summaryColumnCount == columns && m_summaryGrid->count() == m_summaryCards.size()) {
        return;
    }

    m_summaryColumnCount = columns;
    while (QLayoutItem* item = m_summaryGrid->takeAt(0)) {
        delete item;
    }

    for (int i = 0; i < m_summaryCards.size(); ++i) {
        GlowCard* card = m_summaryCards.at(i);
        if (!card) {
            continue;
        }
        const int row = i / columns;
        const int col = i % columns;
        m_summaryGrid->addWidget(card, row, col);
    }

    for (int col = 0; col < 4; ++col) {
        m_summaryGrid->setColumnStretch(col, col < columns ? 1 : 0);
    }
}

void JobsPage::updateResponsiveLayout()
{
    if (m_headerLayout && m_focusCard) {
        const bool compact = contentWidth() < 1380;
        if (m_headerCompact != compact) {
            m_headerCompact = compact;
            m_headerLayout->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        }
        if (m_headerPrimaryColumn) {
            m_headerLayout->setAlignment(m_headerPrimaryColumn, compact ? Qt::AlignLeft : Qt::AlignTop);
            m_headerPrimaryColumn->setMinimumWidth(compact ? 0 : 720);
            m_headerPrimaryColumn->setMaximumWidth(compact ? QWIDGETSIZE_MAX : 980);
            m_headerPrimaryColumn->setSizePolicy(compact ? QSizePolicy::Expanding : QSizePolicy::Preferred,
                                                 QSizePolicy::Fixed);
        }
        if (m_activityCard) {
            m_headerLayout->setAlignment(m_activityCard, compact ? Qt::AlignLeft : Qt::AlignTop);
            m_activityCard->setMinimumWidth(compact ? 0 : 380);
            m_activityCard->setMaximumWidth(compact ? QWIDGETSIZE_MAX : 500);
            m_activityCard->setSizePolicy(compact ? QSizePolicy::Expanding : QSizePolicy::Preferred,
                                          QSizePolicy::Fixed);
        }
        m_focusCard->setMinimumWidth(0);
        m_focusCard->setMaximumWidth(QWIDGETSIZE_MAX);
        m_focusCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (m_focusFooterLayout) {
            m_focusFooterLayout->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
            m_focusFooterLayout->setSpacing(compact ? 10 : 12);
        }
        if (m_focusMeta) {
            m_focusMeta->setAlignment(compact
                ? static_cast<Qt::Alignment>(Qt::AlignLeft | Qt::AlignVCenter)
                : static_cast<Qt::Alignment>(Qt::AlignRight | Qt::AlignVCenter));
        }
        if (m_focusActionBtn) {
            m_focusActionBtn->setSizePolicy(compact ? QSizePolicy::Expanding : QSizePolicy::Maximum,
                                            QSizePolicy::Fixed);
            m_focusActionBtn->setMinimumWidth(compact ? 0 : 180);
        }
    }

    rebuildSummaryGrid();
    rebuildGrid();
}

int JobsPage::contentWidth() const
{
    if (m_pageRoot && m_pageRoot->width() > 0) {
        return m_pageRoot->width();
    }
    if (contentsRect().width() > 0) {
        return contentsRect().width();
    }
    return width();
}

void JobsPage::updateJobCard(const QString& name,
                             const QJsonObject& job,
                             const QJsonObject& process,
                             const QJsonObject& state)
{
    if (!m_jobCards.contains(name)) {
        return;
    }

    JobCardUi& ui = m_jobCards[name];
    const JobDisplayState display = describeJobState(name, job, process, state);
    const QJsonObject jobs = state.value(QStringLiteral("jobs")).toObject();
    const QJsonObject recovery = state.value(QStringLiteral("recovery")).toObject();
    const QString stage = job.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
    const QString message = job.value(QStringLiteral("message")).toString().trimmed();
    const QString eta = job.value(QStringLiteral("eta")).toString(QStringLiteral("unknown"));
    const double progress = qBound(0.0, job.value(QStringLiteral("progress")).toDouble(), 1.0);
    const bool canResume = canResumeJob(name, job, process, state, display);
    const bool canPause = (name == QStringLiteral("autopilot"))
        ? recovery.value(QStringLiteral("can_pause")).toBool(false)
        : display.running;
    const bool pausedState = isPausedState(name, job, process, state);
    const QString blocker = (!display.running && !canResume && isJobBlocked(name, jobs))
        ? prerequisiteJob(name)
        : QString();

    if (ui.statusLabel) {
        QColor fill = display.statusColor;
        fill.setAlpha(38);
        QColor border = display.statusColor;
        border.setAlpha(96);
        ui.statusLabel->setText(display.statusText);
        ui.statusLabel->setStyleSheet(badgeStyle(display.statusColor, fill, border));
    }

    const QString stageText = prettyStageName(stage);
    if (ui.stageLabel) {
        const bool showStage = !stageText.isEmpty() && stageText.compare(display.statusText, Qt::CaseInsensitive) != 0;
        ui.stageLabel->setVisible(showStage);
        if (showStage) {
            QColor stageColor(QStringLiteral("#79c0ff"));
            QColor fill = stageColor;
            fill.setAlpha(28);
            QColor border = stageColor;
            border.setAlpha(84);
            ui.stageLabel->setText(stageText);
            ui.stageLabel->setStyleSheet(badgeStyle(stageColor, fill, border));
        }
    }

    if (ui.pctLabel) {
        ui.pctLabel->setText(Fmt::progressPct(progress));
    }
    if (ui.progressBar) {
        ui.progressBar->setValue(progress);
    }

    if (ui.messageLabel) {
        ui.messageLabel->setVisible(!message.isEmpty());
        ui.messageLabel->setText(message);
    }

    QStringList metaParts;
    if (canResume && !display.running) {
        metaParts.append(QStringLiteral("Resumable"));
    }
    const bool showEta = !display.paused
        && display.running
        && !eta.isEmpty()
        && eta != QStringLiteral("unknown")
        && stage != QStringLiteral("completed")
        && stage != QStringLiteral("failed")
        && stage != QStringLiteral("stopped");
    if (showEta) {
        metaParts.append(QStringLiteral("ETA: %1").arg(eta));
    }
    const double updatedAt = job.value(QStringLiteral("updated_at")).toDouble();
    if (updatedAt > 0.0) {
        metaParts.append(Fmt::relativeTime(updatedAt));
    }
    if (ui.etaLabel) {
        ui.etaLabel->setVisible(!metaParts.isEmpty());
        ui.etaLabel->setText(metaParts.join(QStringLiteral("  |  ")));
    }

    if (ui.noteLabel) {
        QString note;
        if (display.running) {
            if (name == QStringLiteral("autopilot")) {
                note = QStringLiteral("Autopilot is driving the active block and will chain the remaining workflow stages.");
            } else if (name == QStringLiteral("training")) {
                note = QStringLiteral("Pause writes a durable checkpoint so this block can resume from its latest recovery point.");
            } else {
                note = QStringLiteral("Pause stops this stage cleanly and resume continues from the latest recovered filesystem state.");
            }
        } else if (canResume) {
            if (name == QStringLiteral("training")) {
                note = QStringLiteral("Resume from the latest checkpoint recorded inside this block.");
            } else if (name == QStringLiteral("autopilot")) {
                note = QStringLiteral("Resume the current mission from the latest durable point inside the active block.");
            } else {
                note = QStringLiteral("Continue from the latest recovered filesystem state for this stage.");
            }
        } else if (!blocker.isEmpty()) {
            note = QStringLiteral("Waiting on %1 to complete first.").arg(Fmt::prettyJobName(blocker));
        } else if (stage == QStringLiteral("failed")) {
            note = QStringLiteral("Needs attention before rerunning. Review recent activity and the latest log snippet.");
        } else if (stage == QStringLiteral("completed")) {
            note = QStringLiteral("Completed successfully. Rerun this stage only if you want a fresh pass.");
        } else {
            note = jobDescription(name);
        }
        ui.noteLabel->setVisible(!note.isEmpty());
        ui.noteLabel->setText(note);
    }

    if (ui.logLabel) {
        const QStringList lines = filteredLogLines(process.value(QStringLiteral("log")).toArray());
        ui.logLabel->setVisible(!lines.isEmpty() && (display.running || canResume || stage == QStringLiteral("failed")));
        ui.logLabel->setText(lines.join(QStringLiteral("\n")));
    }

    clearLayoutContents(ui.actionRow);
    const QString stopEndpoint = actionEndpointForJob(name, QStringLiteral("stop"));
    if (canPause) {
        auto* pauseBtn = new QPushButton(QStringLiteral("Pause"));
        pauseBtn->setObjectName(QStringLiteral("actionBtn"));
        pauseBtn->setFixedHeight(30);
        const QString pauseEndpoint = actionEndpointForJob(name, QStringLiteral("pause"));
        connect(pauseBtn, &QPushButton::clicked, this, [this, pauseEndpoint]() {
            if (!pauseEndpoint.isEmpty()) {
                m_api->postAction(pauseEndpoint);
            }
        });
        ui.actionRow->addWidget(pauseBtn, 1);

        if (!stopEndpoint.isEmpty()) {
            auto* stopBtn = new QPushButton(QStringLiteral("Stop"));
            stopBtn->setObjectName(QStringLiteral("actionBtnDanger"));
            stopBtn->setFixedHeight(30);
            connect(stopBtn, &QPushButton::clicked, this, [this, stopEndpoint]() {
                m_api->postAction(stopEndpoint);
            });
            ui.actionRow->addWidget(stopBtn, 1);
        }
        return;
    }

    if (canResume) {
        auto* resumeBtn = new QPushButton(pausedState ? QStringLiteral("Resume")
                                                     : QStringLiteral("Continue"));
        resumeBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
        resumeBtn->setFixedHeight(30);
        const QString resumeEndpoint = continueEndpointForJob(name, job, process, state);
        connect(resumeBtn, &QPushButton::clicked, this, [this, resumeEndpoint]() {
            if (!resumeEndpoint.isEmpty()) {
                m_api->postAction(resumeEndpoint);
            }
        });
        ui.actionRow->addWidget(resumeBtn, 1);
        return;
    }

    auto* runBtn = new QPushButton;
    const bool blocked = !blocker.isEmpty();
    runBtn->setText(blocked
        ? QStringLiteral("Waiting on %1").arg(Fmt::prettyJobName(blocker))
        : (stage == QStringLiteral("completed") && name != QStringLiteral("autopilot")
            ? QStringLiteral("Run Again")
            : focusActionLabelForJob(name)));
    runBtn->setObjectName(blocked ? QStringLiteral("actionBtn") : QStringLiteral("actionBtnPrimary"));
    runBtn->setFixedHeight(30);
    runBtn->setEnabled(!blocked);
    const QString startEndpoint = actionEndpointForJob(name);
    connect(runBtn, &QPushButton::clicked, this, [this, startEndpoint]() {
        if (!startEndpoint.isEmpty()) {
            m_api->postAction(startEndpoint);
        }
    });
    ui.actionRow->addWidget(runBtn, 1);
}

void JobsPage::updateFromState(const QJsonObject& state)
{
    if (!m_jobContainer) {
        return;
    }

    ensureCards();
    m_lastState = state;

    const QJsonObject jobs = state.value(QStringLiteral("jobs")).toObject();
    const QJsonObject processes = state.value(QStringLiteral("processes")).toObject();
    const QJsonArray alerts = state.value(QStringLiteral("alerts")).toArray();

    int activeCount = 0;
    int readyCount = 0;
    int resumableCount = 0;
    int attentionCount = 0;

    for (const QString& name : m_jobOrder) {
        const QJsonObject job = jobs.value(name).toObject();
        const QJsonObject process = processes.value(name).toObject();
        const JobDisplayState display = describeJobState(name, job, process, state);
        const QString stage = job.value(QStringLiteral("stage")).toString(QStringLiteral("idle"));
        const bool canResume = canResumeJob(name, job, process, state, display);
        const bool blocked = !display.running && !canResume && isJobBlocked(name, jobs);

        if (display.running) {
            ++activeCount;
        }
        if (!display.running && !blocked && stage != QStringLiteral("failed")) {
            ++readyCount;
        }
        if (canResume) {
            ++resumableCount;
        }
        if (stage == QStringLiteral("failed")) {
            ++attentionCount;
        }

        updateJobCard(name, job, process, state);
    }

    for (const QJsonValue& value : alerts) {
        const QString severity = value.toObject().value(QStringLiteral("severity")).toString();
        if (severity == QStringLiteral("warning") || severity == QStringLiteral("error")) {
            ++attentionCount;
        }
    }

    if (m_activeCount) {
        m_activeCount->setText(QString::number(activeCount));
    }
    if (m_readyCount) {
        m_readyCount->setText(QString::number(readyCount));
    }
    if (m_resumableCount) {
        m_resumableCount->setText(QString::number(resumableCount));
    }
    if (m_attentionCount) {
        m_attentionCount->setText(QString::number(attentionCount));
    }

    updateFocusCard(state);
    updateActivityCard(state);
    updateResponsiveLayout();
}

void JobsPage::updateFocusCard(const QJsonObject& state)
{
    if (!m_focusCard || !m_focusStatus || !m_focusName || !m_focusSummary || !m_focusMeta
        || !m_focusNextValue || !m_focusRecoveryValue || !m_focusActionBtn) {
        return;
    }

    const QJsonObject primaryJob = state.value(QStringLiteral("primary_job")).toObject();
    const QString primaryName = primaryJob.value(QStringLiteral("job")).toString();
    const QJsonObject jobs = state.value(QStringLiteral("jobs")).toObject();
    const QJsonObject processes = state.value(QStringLiteral("processes")).toObject();
    const QJsonObject recovery = state.value(QStringLiteral("recovery")).toObject();
    const QJsonObject autopilot = state.value(QStringLiteral("autopilot")).toObject();

    const QString effectiveName = primaryName.isEmpty() ? QStringLiteral("autopilot") : primaryName;
    QJsonObject effectiveJob = jobs.value(effectiveName).toObject();
    if (effectiveJob.isEmpty()) {
        effectiveJob = primaryJob;
    }
    const QJsonObject effectiveProcess = processes.value(effectiveName).toObject();
    const JobDisplayState display = describeJobState(effectiveName, effectiveJob, effectiveProcess, state);

    const QString recommendedJob = nextRecommendedJob(jobs);
    const QString recoveryJob = recovery.value(QStringLiteral("job")).toString();
    const bool canPause = recovery.value(QStringLiteral("can_pause")).toBool(false);
    const bool canResume = recovery.value(QStringLiteral("can_resume")).toBool(false);
    const bool autopilotControl = primaryName == QStringLiteral("autopilot")
        || autopilot.value(QStringLiteral("active")).toBool(false)
        || autopilot.value(QStringLiteral("paused")).toBool(false);
    const QString recoveryActionJob = recoveryJob.isEmpty() ? recommendedJob : recoveryJob;
    const QJsonObject recoveryActionState = jobs.value(recoveryActionJob).toObject();
    const QJsonObject recoveryActionProcess = processes.value(recoveryActionJob).toObject();
    const QJsonObject focusSourceJob = (primaryName.isEmpty() && canResume && !recoveryActionState.isEmpty())
        ? recoveryActionState
        : primaryJob;
    const QString stageText = prettyStageName(focusSourceJob.value(QStringLiteral("stage")).toString());
    const QString message = focusSourceJob.value(QStringLiteral("message")).toString().trimmed();
    const double progress = qBound(0.0, focusSourceJob.value(QStringLiteral("progress")).toDouble(), 1.0);
    const QString eta = focusSourceJob.value(QStringLiteral("eta")).toString();
    const double updatedAt = focusSourceJob.value(QStringLiteral("updated_at")).toDouble();

    QString focusStatusText = display.statusText;
    QColor focusStatusColor = display.statusColor;
    if (canResume && !canPause) {
        focusStatusText = QStringLiteral("Resumable");
        focusStatusColor = QColor(QStringLiteral("#e3b341"));
    } else if (primaryName.isEmpty()) {
        focusStatusText = QStringLiteral("Ready");
        focusStatusColor = QColor(QStringLiteral("#79c0ff"));
    }

    m_focusStatus->setText(focusStatusText);
    m_focusStatus->setStyleSheet(QStringLiteral("color: %1;").arg(focusStatusColor.name(QColor::HexRgb)));
    if (primaryName.isEmpty() && canResume && !recoveryActionJob.isEmpty()) {
        m_focusName->setText(QStringLiteral("%1 Recovery Ready").arg(Fmt::prettyJobName(recoveryActionJob)));
    } else {
        m_focusName->setText(primaryName.isEmpty()
            ? QStringLiteral("Pipeline Standby")
            : Fmt::prettyJobName(primaryName));
    }

    QString summary = message;
    if (summary.isEmpty()) {
        if (!stageText.isEmpty()) {
            summary = stageText;
        } else if (primaryName.isEmpty() && canResume && !recoveryActionJob.isEmpty()) {
            summary = QStringLiteral("%1 can continue from the latest recovery point recovered for this runtime.")
                .arg(Fmt::prettyJobName(recoveryActionJob));
        } else if (primaryName.isEmpty()) {
            summary = QStringLiteral("%1 is the next stage in sequence. Nothing is running right now.")
                .arg(Fmt::prettyJobName(recommendedJob));
        } else {
            summary = QStringLiteral("%1 is waiting for the next operator decision.").arg(Fmt::prettyJobName(primaryName));
        }
    }
    m_focusSummary->setText(summary);

    QString nextDecision = QStringLiteral("Run %1 next to keep the pipeline moving.").arg(Fmt::prettyJobName(recommendedJob));
    QString recoveryText = QStringLiteral("No active recovery window yet.");
    QString actionLabel = focusActionLabelForJob(recommendedJob);
    QString actionPath = actionEndpointForJob(recommendedJob);

    if (canPause && !recoveryJob.isEmpty()) {
        nextDecision = autopilotControl
            ? QStringLiteral("The current block is live. Pause only if you need to hold the full workflow in place.")
            : QStringLiteral("Pause %1 if you need to hold its current state.").arg(Fmt::prettyJobName(recoveryJob));
        actionLabel = autopilotControl
            ? QStringLiteral("Pause Current Block")
            : QStringLiteral("Pause %1").arg(Fmt::prettyJobName(recoveryJob));
        actionPath = autopilotControl
            ? QStringLiteral("/api/actions/autopilot/pause")
            : actionEndpointForJob(recoveryJob, QStringLiteral("pause"));
    } else if (canResume && !recoveryJob.isEmpty()) {
        nextDecision = QStringLiteral("%1 %2 from its latest recovery point.")
            .arg(isPausedState(recoveryJob, recoveryActionState, recoveryActionProcess, state)
                     ? QStringLiteral("Resume")
                     : QStringLiteral("Continue"),
                 Fmt::prettyJobName(recoveryJob));
        actionLabel = continueActionLabelForJob(recoveryJob,
                                                recoveryActionState,
                                                recoveryActionProcess,
                                                state,
                                                autopilotControl);
        actionPath = autopilotControl
            ? QStringLiteral("/api/actions/autopilot/resume")
            : continueEndpointForJob(recoveryJob, recoveryActionState, recoveryActionProcess, state);
    }

    if (recovery.value(QStringLiteral("available")).toBool(false)) {
        const QString lastRecoveryLabel = recovery.value(QStringLiteral("last_recovery_label")).toString();
        const QString nextRecoveryLabel = recovery.value(QStringLiteral("next_recovery_label")).toString();
        const double pauseLossSeconds = recovery.value(QStringLiteral("pause_loss_seconds")).toDouble(0.0);
        QStringList recoveryBits;
        if (!lastRecoveryLabel.isEmpty() && lastRecoveryLabel != QStringLiteral("No recovery point needed")) {
            recoveryBits.append(QStringLiteral("Latest: %1").arg(lastRecoveryLabel));
        }
        if (pauseLossSeconds > 0.0 && canPause) {
            recoveryBits.append(QStringLiteral("Pause risk: %1").arg(recoveryWindowText(pauseLossSeconds)));
        }
        if (!nextRecoveryLabel.isEmpty()) {
            recoveryBits.append(QStringLiteral("Next: %1").arg(nextRecoveryLabel));
        }
        if (!recoveryBits.isEmpty()) {
            recoveryText = recoveryBits.join(QStringLiteral("  |  "));
        }
    }

    m_focusNextValue->setText(nextDecision);
    m_focusRecoveryValue->setText(recoveryText);

    QStringList metaParts;
    metaParts.append(Fmt::progressPct(progress));
    if (!stageText.isEmpty()) {
        metaParts.append(stageText);
    }
    if (!eta.isEmpty() && eta != QStringLiteral("unknown")) {
        metaParts.append(QStringLiteral("ETA: %1").arg(eta));
    }
    if (updatedAt > 0.0) {
        metaParts.append(Fmt::relativeTime(updatedAt));
    }
    m_focusMeta->setText(metaParts.join(QStringLiteral("  |  ")));

    m_focusActionBtn->setText(actionLabel);
    m_focusActionBtn->setProperty("actionPath", actionPath);
    m_focusActionBtn->setObjectName(canPause ? QStringLiteral("actionBtn") : QStringLiteral("actionBtnPrimary"));
    m_focusActionBtn->setEnabled(!actionPath.isEmpty());
    Fmt::repolish(m_focusActionBtn);
}

void JobsPage::updateActivityCard(const QJsonObject& state)
{
    if (!m_activitySummary || !m_activityList) {
        return;
    }

    struct ActivityItem {
        double ts = 0.0;
        QString badge;
        QColor color;
        QString title;
        QString meta;
        QString stamp;
    };

    QVector<ActivityItem> items;
    QSet<QString> seenKeys;
    const QJsonArray alerts = state.value(QStringLiteral("alerts")).toArray();
    const QJsonArray actions = state.value(QStringLiteral("history")).toObject().value(QStringLiteral("actions")).toArray();

    auto pushUnique = [&items, &seenKeys](ActivityItem item) {
        const QString key = item.badge + QStringLiteral("|")
            + item.title + QStringLiteral("|")
            + item.meta + QStringLiteral("|")
            + QString::number(static_cast<qint64>(item.ts));
        if (seenKeys.contains(key)) {
            return;
        }
        seenKeys.insert(key);
        items.append(item);
    };

    for (int i = alerts.size() - 1; i >= 0; --i) {
        const QJsonObject alert = alerts.at(i).toObject();
        const QString severity = alert.value(QStringLiteral("severity")).toString(QStringLiteral("info"));
        QColor color(QStringLiteral("#79c0ff"));
        if (severity == QStringLiteral("warning")) {
            color = QColor(QStringLiteral("#e3b341"));
        } else if (severity == QStringLiteral("error")) {
            color = QColor(QStringLiteral("#f85149"));
        } else if (severity == QStringLiteral("success")) {
            color = QColor(QStringLiteral("#22c55e"));
        }
        pushUnique(ActivityItem{
            alert.value(QStringLiteral("ts")).toDouble(),
            severity.toUpper(),
            color,
            alert.value(QStringLiteral("message")).toString(QStringLiteral("Backend alert")),
            QStringLiteral("Alert"),
            Fmt::relativeTime(alert.value(QStringLiteral("ts")).toDouble())
        });
    }

    for (int i = actions.size() - 1; i >= 0; --i) {
        const QJsonObject row = actions.at(i).toObject();
        const QString severity = row.value(QStringLiteral("severity")).toString(QStringLiteral("info"));
        QColor color(QStringLiteral("#79c0ff"));
        if (severity == QStringLiteral("warning")) {
            color = QColor(QStringLiteral("#e3b341"));
        } else if (severity == QStringLiteral("error")) {
            color = QColor(QStringLiteral("#f85149"));
        } else if (severity == QStringLiteral("success")) {
            color = QColor(QStringLiteral("#22c55e"));
        }

        QStringList metaBits;
        const QJsonObject context = row.value(QStringLiteral("context")).toObject();
        const QString jobName = context.value(QStringLiteral("job")).toString();
        const QString stageName = context.value(QStringLiteral("stage")).toString();
        if (!jobName.isEmpty()) {
            metaBits.append(Fmt::prettyJobName(jobName));
        }
        if (!stageName.isEmpty()) {
            metaBits.append(prettyStageName(stageName));
        }

        pushUnique(ActivityItem{
            row.value(QStringLiteral("ts")).toDouble(),
            severity == QStringLiteral("info") ? QStringLiteral("EVENT") : severity.toUpper(),
            color,
            row.value(QStringLiteral("message")).toString(QStringLiteral("Backend activity")),
            metaBits.isEmpty() ? QStringLiteral("Backend activity") : metaBits.join(QStringLiteral("  |  ")),
            Fmt::relativeTime(row.value(QStringLiteral("ts")).toDouble())
        });
    }

    std::sort(items.begin(), items.end(), [](const ActivityItem& left, const ActivityItem& right) {
        return left.ts > right.ts;
    });
    while (items.size() > 3) {
        items.removeLast();
    }

    m_activitySummary->setText(items.isEmpty()
        ? QStringLiteral("No recent operational events yet.")
        : QStringLiteral("Showing the %1 most relevant recent alerts and backend actions for the current runtime.")
            .arg(items.size()));

    clearLayoutContents(m_activityList);
    if (items.isEmpty()) {
        auto* placeholder = new QLabel(QStringLiteral("No recent operational events."));
        placeholder->setObjectName(QStringLiteral("dimText"));
        m_activityList->addWidget(placeholder);
        return;
    }

    for (const ActivityItem& item : std::as_const(items)) {
        auto* row = new QWidget;
        row->setObjectName(QStringLiteral("jobsActivityItem"));
        row->setAttribute(Qt::WA_StyledBackground, true);
        auto* rowLay = new QVBoxLayout(row);
        rowLay->setContentsMargins(14, 12, 14, 12);
        rowLay->setSpacing(6);

        auto* topRow = new QHBoxLayout;
        topRow->setContentsMargins(0, 0, 0, 0);
        topRow->setSpacing(8);

        auto* badge = new QLabel(item.badge);
        QColor fill = item.color;
        fill.setAlpha(30);
        QColor border = item.color;
        border.setAlpha(100);
        badge->setStyleSheet(badgeStyle(item.color, fill, border));
        topRow->addWidget(badge);
        topRow->addStretch(1);

        auto* stamp = new QLabel(item.stamp);
        stamp->setObjectName(QStringLiteral("jobsActivityStamp"));
        topRow->addWidget(stamp);
        rowLay->addLayout(topRow);

        auto* title = new QLabel(item.title);
        title->setObjectName(QStringLiteral("jobsActivityTitle"));
        title->setWordWrap(true);
        rowLay->addWidget(title);

        auto* meta = new QLabel(item.meta);
        meta->setObjectName(QStringLiteral("jobsActivityMeta"));
        meta->setWordWrap(true);
        rowLay->addWidget(meta);

        m_activityList->addWidget(row);
    }
}
