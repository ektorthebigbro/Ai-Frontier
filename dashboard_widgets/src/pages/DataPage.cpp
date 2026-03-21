#include "DataPage.h"
#include "../app/ApiClient.h"
#include "../util/Formatters.h"
#include "../widgets/GlowCard.h"
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

void clearLayoutContents(QLayout* layout)
{
    if (!layout) {
        return;
    }
    QLayoutItem* item = nullptr;
    while ((item = layout->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
}

QString formatBytes(double rawBytes)
{
    const double bytes = qMax(0.0, rawBytes);
    if (bytes <= 0.0) {
        return QStringLiteral("Size unavailable");
    }
    if (bytes >= 1024.0 * 1024.0 * 1024.0) {
        return QStringLiteral("%1 GB").arg(QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2));
    }
    if (bytes >= 1024.0 * 1024.0) {
        return QStringLiteral("%1 MB").arg(QString::number(bytes / (1024.0 * 1024.0), 'f', 1));
    }
    if (bytes >= 1024.0) {
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 1));
    }
    return QStringLiteral("%1 B").arg(QString::number(bytes, 'f', 0));
}

QString titleCase(QString text)
{
    text.replace('_', ' ');
    if (!text.isEmpty()) {
        text[0] = text.at(0).toUpper();
    }
    return text;
}

QString toneForCategory(const QString& category)
{
    if (category == QStringLiteral("reports") || category == QStringLiteral("dependencies")) {
        return QStringLiteral("ok");
    }
    if (category == QStringLiteral("checkpoints") || category == QStringLiteral("training_logs")) {
        return QStringLiteral("warning");
    }
    if (category == QStringLiteral("model_cache")) {
        return QStringLiteral("accent");
    }
    return QStringLiteral("info");
}

QString toneForSeverity(const QString& severity)
{
    if (severity == QStringLiteral("error")) {
        return QStringLiteral("error");
    }
    if (severity == QStringLiteral("warning")) {
        return QStringLiteral("warning");
    }
    if (severity == QStringLiteral("info")) {
        return QStringLiteral("info");
    }
    return QStringLiteral("accent");
}

GlowCard* makeSectionCard(const QString& title,
                          const QColor& glow,
                          const QString& summary,
                          QWidget*& containerOut,
                          QLabel*& summaryOut)
{
    auto* card = new GlowCard(title);
    card->setGlowColor(glow);
    auto* lay = card->contentLayout();
    lay->setSpacing(12);

    summaryOut = new QLabel(summary);
    summaryOut->setObjectName(QStringLiteral("historyPanelSummary"));
    summaryOut->setWordWrap(true);
    lay->addWidget(summaryOut);

    containerOut = new QWidget;
    auto* containerLay = new QVBoxLayout(containerOut);
    containerLay->setContentsMargins(0, 0, 0, 0);
    containerLay->setSpacing(12);
    lay->addWidget(containerOut);

    return card;
}

QWidget* makeHistoryRow(const QString& tone)
{
    auto* item = new QWidget;
    item->setObjectName(QStringLiteral("historyRow"));
    item->setProperty("tone", tone);
    item->setAttribute(Qt::WA_StyledBackground, true);
    return item;
}

void openLocalArtifact(const QString& path)
{
    if (path.trimmed().isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
}

}  // namespace

DataPage::DataPage(ApiClient* api, QWidget* parent)
    : BasePage(api, parent)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(28, 26, 28, 28);
    lay->setSpacing(18);

    auto* title = new QLabel(QStringLiteral("Data & Dependencies"));
    title->setObjectName(QStringLiteral("pageTitle"));
    lay->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral(
        "Manage datasets, dependency cache, and the live artifact trail with clearer sectioned controls."));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    subtitle->setWordWrap(true);
    lay->addWidget(subtitle);

    auto* summaryRow = new QHBoxLayout;
    summaryRow->setSpacing(14);

    auto makeSummaryCard = [](const QString& label, QLabel*& valueOut) -> GlowCard* {
        auto* card = new GlowCard(label);
        valueOut = new QLabel(QStringLiteral("--"));
        valueOut->setObjectName(QStringLiteral("summaryChipValue"));
        card->contentLayout()->addWidget(valueOut);
        return card;
    };

    summaryRow->addWidget(makeSummaryCard(QStringLiteral("TOTAL SAMPLES"), m_totalSamples), 1);
    summaryRow->addWidget(makeSummaryCard(QStringLiteral("CACHE STATUS"), m_cacheStatus), 1);
    summaryRow->addWidget(makeSummaryCard(QStringLiteral("DEPENDENCIES"), m_depSummary), 1);
    lay->addLayout(summaryRow);
    lay->addSpacing(4);

    auto* datasetsCard = makeSectionCard(QStringLiteral("Datasets"),
                                         QColor(QStringLiteral("#3b82f6")),
                                         QStringLiteral("Configured sources and rebuild actions for the preparation pipeline."),
                                         m_datasetContainer,
                                         m_datasetSummary);
    {
        auto* datasetLay = datasetsCard->contentLayout();
        auto* actionsRow = new QHBoxLayout;
        actionsRow->setContentsMargins(0, 2, 0, 2);
        actionsRow->setSpacing(10);

        auto* rebuildBtn = new QPushButton(QStringLiteral("Rebuild Dataset"));
        rebuildBtn->setObjectName(QStringLiteral("actionBtn"));
        rebuildBtn->setMinimumHeight(36);
        connect(rebuildBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/prepare"));
        });
        actionsRow->addWidget(rebuildBtn);

        auto* checkBtn = new QPushButton(QStringLiteral("Check Integrity"));
        checkBtn->setObjectName(QStringLiteral("actionBtn"));
        checkBtn->setMinimumHeight(36);
        connect(checkBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/data/check"));
        });
        actionsRow->addWidget(checkBtn);

        auto* redownloadBtn = new QPushButton(QStringLiteral("Re-download All"));
        redownloadBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
        redownloadBtn->setMinimumHeight(36);
        connect(redownloadBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/data/redownload"));
        });
        actionsRow->addWidget(redownloadBtn);

        auto* clearProcessedBtn = new QPushButton(QStringLiteral("Delete Prepared Data"));
        clearProcessedBtn->setObjectName(QStringLiteral("actionBtnDanger"));
        clearProcessedBtn->setMinimumHeight(36);
        connect(clearProcessedBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/data/cleanup/data"));
        });
        actionsRow->addWidget(clearProcessedBtn);

        auto* clearSourceCacheBtn = new QPushButton(QStringLiteral("Delete Source Cache"));
        clearSourceCacheBtn->setObjectName(QStringLiteral("actionBtnDanger"));
        clearSourceCacheBtn->setMinimumHeight(36);
        connect(clearSourceCacheBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/data/cleanup/dataset_cache"));
        });
        actionsRow->addWidget(clearSourceCacheBtn);
        actionsRow->addStretch(1);
        datasetLay->insertLayout(1, actionsRow);
    }
    lay->addWidget(datasetsCard);

    auto* cacheCard = makeSectionCard(QStringLiteral("Cached Models"),
                                      QColor(QStringLiteral("#22c55e")),
                                      QStringLiteral("Installed dependency cache, judge models, and environment recovery actions."),
                                      m_depContainer,
                                      m_depSectionSummary);
    {
        auto* cacheLay = cacheCard->contentLayout();
        auto* actionsRow = new QHBoxLayout;
        actionsRow->setContentsMargins(0, 2, 0, 2);
        actionsRow->setSpacing(10);

        auto* clearCacheBtn = new QPushButton(QStringLiteral("Clear All Cached Models"));
        clearCacheBtn->setObjectName(QStringLiteral("actionBtnDanger"));
        clearCacheBtn->setMinimumHeight(36);
        connect(clearCacheBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/data/clear_cache"));
        });
        actionsRow->addWidget(clearCacheBtn);

        auto* deleteEnvBtn = new QPushButton(QStringLiteral("Delete Environment"));
        deleteEnvBtn->setObjectName(QStringLiteral("actionBtnDanger"));
        deleteEnvBtn->setMinimumHeight(36);
        connect(deleteEnvBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/data/cleanup/dependencies"));
        });
        actionsRow->addWidget(deleteEnvBtn);

        auto* envBtn = new QPushButton(QStringLiteral("Refresh Environment"));
        envBtn->setObjectName(QStringLiteral("actionBtn"));
        envBtn->setMinimumHeight(36);
        connect(envBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/setup"));
        });
        actionsRow->addWidget(envBtn);
        actionsRow->addStretch(1);
        cacheLay->insertLayout(1, actionsRow);
    }
    lay->addWidget(cacheCard);

    auto* lineageTitle = new QLabel(QStringLiteral("Lineage & Audit"));
    lineageTitle->setObjectName(QStringLiteral("sectionTitle"));
    lay->addWidget(lineageTitle);

    auto* lineageSubtitle = new QLabel(QStringLiteral(
        "Current artifacts and recent workflow actions are grouped as a readable project trail rather than a raw dump."));
    lineageSubtitle->setObjectName(QStringLiteral("pageSubtitle"));
    lineageSubtitle->setWordWrap(true);
    lay->addWidget(lineageSubtitle);

    auto* fileHistoryCard = makeSectionCard(QStringLiteral("Artifact Lineage"),
                                            QColor(QStringLiteral("#3b82f6")),
                                            QStringLiteral("Tracking generated outputs and runtime logs that still exist on disk."),
                                            m_fileHistoryContainer,
                                            m_fileHistorySummary);
    {
        auto* fileLay = fileHistoryCard->contentLayout();
        auto* actionsRow = new QHBoxLayout;
        actionsRow->setContentsMargins(0, 2, 0, 2);
        actionsRow->setSpacing(10);

        auto* clearLogsBtn = new QPushButton(QStringLiteral("Delete All Logs"));
        clearLogsBtn->setObjectName(QStringLiteral("actionBtnDanger"));
        clearLogsBtn->setMinimumHeight(36);
        connect(clearLogsBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/data/cleanup/logs"));
        });
        actionsRow->addWidget(clearLogsBtn);
        actionsRow->addStretch(1);
        fileLay->insertLayout(1, actionsRow);
    }
    lay->addWidget(fileHistoryCard);

    auto* actionHistoryCard = makeSectionCard(QStringLiteral("Action Trail"),
                                              QColor(QStringLiteral("#8b5cf6")),
                                              QStringLiteral("Recent durable actions with their live file links."),
                                              m_actionHistoryContainer,
                                              m_actionHistorySummary);
    {
        auto* actionLay = actionHistoryCard->contentLayout();
        auto* actionsRow = new QHBoxLayout;
        actionsRow->setContentsMargins(0, 2, 0, 2);
        actionsRow->setSpacing(10);

        auto* clearTrailBtn = new QPushButton(QStringLiteral("Clear Action Trail"));
        clearTrailBtn->setObjectName(QStringLiteral("actionBtnDanger"));
        clearTrailBtn->setMinimumHeight(36);
        connect(clearTrailBtn, &QPushButton::clicked, this, [this]() {
            m_api->postAction(QStringLiteral("/api/actions/data/clear_action_history"));
        });
        actionsRow->addWidget(clearTrailBtn);
        actionsRow->addStretch(1);
        actionLay->insertLayout(1, actionsRow);
    }
    lay->addWidget(actionHistoryCard);

    lay->addStretch();

    auto* wrapper = buildScrollWrapper(page);
    auto* outerLay = new QVBoxLayout(this);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->addWidget(wrapper);
}

void DataPage::updateFromState(const QJsonObject& state)
{
    const QJsonObject config = state.value(QStringLiteral("config")).toObject();
    const QJsonObject datasets = config.value(QStringLiteral("datasets")).toObject();
    const int maxSamples = datasets.value(QStringLiteral("max_samples")).toInt();
    const QJsonArray sources = datasets.value(QStringLiteral("sources")).toArray();
    const QJsonObject history = state.value(QStringLiteral("history")).toObject();
    const QJsonArray fileHistory = history.value(QStringLiteral("files")).toArray();
    const QJsonArray actionHistory = history.value(QStringLiteral("actions")).toArray();

    if (m_totalSamples) {
        m_totalSamples->setText(maxSamples > 0
            ? QStringLiteral("%1k").arg(qMax(1, maxSamples / 1000))
            : QStringLiteral("--"));
    }

    const QJsonObject cache = state.value(QStringLiteral("model_cache")).toObject();
    const QJsonObject judgeCache = cache.value(QStringLiteral("large_judge")).toObject();
    int cachedCount = 0;
    int totalMb = 0;
    for (auto it = judgeCache.begin(); it != judgeCache.end(); ++it) {
        const QJsonObject obj = it.value().toObject();
        if (obj.value(QStringLiteral("cached")).toBool(false)) {
            ++cachedCount;
            totalMb += obj.value(QStringLiteral("size_mb")).toInt(0);
        }
    }

    if (m_cacheStatus) {
        m_cacheStatus->setText(cachedCount > 0
            ? QStringLiteral("%1 models (%2 MB)").arg(cachedCount).arg(totalMb)
            : QStringLiteral("No models cached"));
    }
    if (m_depSummary) {
        m_depSummary->setText(QStringLiteral("%1 cached").arg(cachedCount));
    }

    if (m_datasetSummary) {
        m_datasetSummary->setText(QStringLiteral("%1 configured source(s) ready for preparation, cache cleanup, and integrity checks.")
            .arg(sources.size()));
    }
    if (m_depSectionSummary) {
        m_depSectionSummary->setText(cachedCount > 0
            ? QStringLiteral("%1 cached model(s) available. Dependencies, environment state, and cached models are all removable here.")
                .arg(cachedCount)
            : QStringLiteral("No cached models are present. Environment, dependencies, and cache recovery actions stay here."));
    }

    if (m_datasetContainer) {
        QLayout* dsLayout = m_datasetContainer->layout();
        clearLayoutContents(dsLayout);
        if (sources.isEmpty()) {
            auto* empty = new QLabel(QStringLiteral("No dataset sources configured. Check configs/default.yaml."));
            empty->setObjectName(QStringLiteral("dimText"));
            dsLayout->addWidget(empty);
        } else {
            for (const QJsonValue& srcVal : sources) {
                const QJsonObject src = srcVal.toObject();
                const QString name = src.value(QStringLiteral("name")).toString();
                const QString type = src.value(QStringLiteral("type")).toString(QStringLiteral("unknown"));
                const double weight = src.value(QStringLiteral("weight")).toDouble(1.0);

                auto* card = new GlowCard(name.isEmpty() ? QStringLiteral("Dataset") : name);
                auto* cLay = card->contentLayout();
                cLay->setSpacing(8);

                auto* row = new QHBoxLayout;
                row->setSpacing(14);

                auto* typeLabel = new QLabel(QStringLiteral("Type: %1").arg(type));
                typeLabel->setObjectName(QStringLiteral("dimText"));
                row->addWidget(typeLabel);

                auto* weightLabel = new QLabel(QStringLiteral("Weight: %1").arg(Fmt::fmtDouble(weight, 2)));
                weightLabel->setObjectName(QStringLiteral("dimText"));
                row->addWidget(weightLabel);
                row->addStretch(1);

                cLay->addLayout(row);
                dsLayout->addWidget(card);
            }
        }
    }

    if (m_depContainer) {
        QLayout* depLayout = m_depContainer->layout();
        clearLayoutContents(depLayout);
        if (judgeCache.isEmpty()) {
            auto* empty = new QLabel(QStringLiteral("No cached models found."));
            empty->setObjectName(QStringLiteral("dimText"));
            depLayout->addWidget(empty);
        } else {
            for (auto it = judgeCache.begin(); it != judgeCache.end(); ++it) {
                const QJsonObject obj = it.value().toObject();
                const bool isCached = obj.value(QStringLiteral("cached")).toBool(false);
                const int sizeMb = obj.value(QStringLiteral("size_mb")).toInt(0);
                const bool hasModel = obj.value(QStringLiteral("has_model")).toBool(false);
                const bool hasTokenizer = obj.value(QStringLiteral("has_tokenizer")).toBool(false);
                const QString modelId = it.key();

                auto* card = new GlowCard(modelId);
                card->setMinimumHeight(118);
                auto* cLay = card->contentLayout();
                cLay->setSpacing(6);

                auto* bodyRow = new QHBoxLayout;
                bodyRow->setContentsMargins(0, 2, 0, 0);
                bodyRow->setSpacing(18);

                auto* metaCol = new QVBoxLayout;
                metaCol->setContentsMargins(0, 0, 0, 0);
                metaCol->setSpacing(8);

                auto* summaryRow = new QHBoxLayout;
                summaryRow->setContentsMargins(0, 0, 0, 0);
                summaryRow->setSpacing(14);

                auto* statusLabel = new QLabel(isCached
                    ? QStringLiteral("\u2714 Ready")
                    : QStringLiteral("\u26A0 Incomplete"));
                statusLabel->setObjectName(isCached
                    ? QStringLiteral("accentText")
                    : QStringLiteral("dimText"));
                summaryRow->addWidget(statusLabel);

                if (sizeMb > 0) {
                    auto* sizeLabel = new QLabel(QStringLiteral("%1 MB").arg(sizeMb));
                    sizeLabel->setObjectName(QStringLiteral("dimText"));
                    summaryRow->addWidget(sizeLabel);
                }

                summaryRow->addStretch(1);
                metaCol->addLayout(summaryRow);

                auto* assetRow = new QHBoxLayout;
                assetRow->setContentsMargins(0, 0, 0, 0);
                assetRow->setSpacing(14);

                auto* modelFlag = new QLabel(
                    hasModel ? QStringLiteral("Weights \u2714") : QStringLiteral("Weights \u2718"));
                modelFlag->setObjectName(QStringLiteral("dimText"));
                assetRow->addWidget(modelFlag);

                auto* tokFlag = new QLabel(
                    hasTokenizer ? QStringLiteral("Tokenizer \u2714") : QStringLiteral("Tokenizer \u2718"));
                tokFlag->setObjectName(QStringLiteral("dimText"));
                assetRow->addWidget(tokFlag);
                assetRow->addStretch(1);
                metaCol->addLayout(assetRow);

                bodyRow->addLayout(metaCol, 1);

                auto* deleteBtn = new QPushButton(QStringLiteral("Delete"));
                deleteBtn->setObjectName(QStringLiteral("actionBtnDanger"));
                deleteBtn->setFixedHeight(34);
                deleteBtn->setMinimumWidth(88);
                connect(deleteBtn, &QPushButton::clicked, this, [this, modelId]() {
                    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(modelId));
                    m_api->postAction(QStringLiteral("/api/actions/data/remove_model/%1").arg(encoded));
                });
                bodyRow->addWidget(deleteBtn, 0, Qt::AlignRight | Qt::AlignVCenter);

                cLay->addLayout(bodyRow);
                depLayout->addWidget(card);
            }
        }
    }

    if (m_fileHistorySummary) {
        double latestTs = 0.0;
        for (const QJsonValue& value : fileHistory) {
            latestTs = qMax(latestTs, value.toObject().value(QStringLiteral("modified_at")).toDouble());
        }
        m_fileHistorySummary->setText(QStringLiteral("%1 live artifact or log item(s) still define the current project state. %2")
            .arg(fileHistory.size())
            .arg(Fmt::relativeTime(latestTs)));
    }

    if (m_fileHistoryContainer) {
        QLayout* fileLayout = m_fileHistoryContainer->layout();
        clearLayoutContents(fileLayout);
        if (fileHistory.isEmpty()) {
            auto* empty = new QLabel(QStringLiteral("No active generated files are currently tracked."));
            empty->setObjectName(QStringLiteral("dimText"));
            fileLayout->addWidget(empty);
        } else {
            for (const QJsonValue& value : fileHistory) {
                const QJsonObject row = value.toObject();
                const int entryCount = row.value(QStringLiteral("entry_count")).toInt(0);
                const QString tone = toneForCategory(row.value(QStringLiteral("category")).toString());

                auto* item = makeHistoryRow(tone);
                auto* itemLay = new QVBoxLayout(item);
                itemLay->setContentsMargins(16, 14, 16, 14);
                itemLay->setSpacing(10);

                auto* topRow = new QHBoxLayout;
                topRow->setSpacing(8);

                auto* categoryLabel = new QLabel(row.value(QStringLiteral("category_label")).toString());
                categoryLabel->setObjectName(QStringLiteral("historyBadge"));
                categoryLabel->setProperty("tone", tone);
                topRow->addWidget(categoryLabel);

                auto* stageLabel = new QLabel(row.value(QStringLiteral("stage_label")).toString());
                stageLabel->setObjectName(QStringLiteral("historyBadge"));
                stageLabel->setProperty("tone", QStringLiteral("neutral"));
                topRow->addWidget(stageLabel);
                topRow->addStretch(1);

                auto* timeLabel = new QLabel(Fmt::relativeTime(row.value(QStringLiteral("modified_at")).toDouble()));
                timeLabel->setObjectName(QStringLiteral("historyMeta"));
                topRow->addWidget(timeLabel);
                itemLay->addLayout(topRow);

                auto* titleLabel = new QLabel(row.value(QStringLiteral("label")).toString(QStringLiteral("Artifact")));
                titleLabel->setObjectName(QStringLiteral("historyTitle"));
                itemLay->addWidget(titleLabel);

                auto* pathLabel = new QLabel(row.value(QStringLiteral("relative_path")).toString());
                pathLabel->setObjectName(QStringLiteral("historyPath"));
                pathLabel->setWordWrap(true);
                itemLay->addWidget(pathLabel);

                QStringList metaBits;
                metaBits << row.value(QStringLiteral("job_label")).toString()
                         << titleCase(row.value(QStringLiteral("kind")).toString())
                         << formatBytes(row.value(QStringLiteral("size_bytes")).toDouble());
                if (entryCount > 0) {
                    metaBits << QStringLiteral("%1 item(s)").arg(entryCount);
                }
                auto* metaLabel = new QLabel(metaBits.join(QStringLiteral("  |  ")));
                metaLabel->setObjectName(QStringLiteral("historyMeta"));
                metaLabel->setWordWrap(true);
                itemLay->addWidget(metaLabel);

                auto* actionRow = new QHBoxLayout;
                actionRow->setContentsMargins(0, 2, 0, 0);
                actionRow->setSpacing(8);
                actionRow->addStretch(1);

                const QString absolutePath = row.value(QStringLiteral("path")).toString();
                const QString relativePath = row.value(QStringLiteral("relative_path")).toString();

                auto* openBtn = new QPushButton(QStringLiteral("Open"));
                openBtn->setObjectName(QStringLiteral("actionBtn"));
                openBtn->setFixedHeight(34);
                openBtn->setMinimumWidth(84);
                connect(openBtn, &QPushButton::clicked, this, [absolutePath]() {
                    openLocalArtifact(absolutePath);
                });
                actionRow->addWidget(openBtn);

                auto* deleteBtn = new QPushButton(QStringLiteral("Delete"));
                deleteBtn->setObjectName(QStringLiteral("actionBtnDanger"));
                deleteBtn->setFixedHeight(34);
                deleteBtn->setMinimumWidth(84);
                connect(deleteBtn, &QPushButton::clicked, this, [this, relativePath]() {
                    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(relativePath));
                    m_api->postAction(QStringLiteral("/api/actions/data/delete_path/%1").arg(encoded));
                });
                actionRow->addWidget(deleteBtn);
                itemLay->addLayout(actionRow);

                fileLayout->addWidget(item);
            }
        }
    }

    if (m_actionHistorySummary) {
        int liveLinkedActions = 0;
        for (const QJsonValue& value : actionHistory) {
            if (value.toObject().value(QStringLiteral("active")).toBool(true)) {
                ++liveLinkedActions;
            }
        }
        m_actionHistorySummary->setText(QStringLiteral("%1 recent action(s), %2 still linked to live files or current runtime state.")
            .arg(actionHistory.size())
            .arg(liveLinkedActions));
    }

    if (m_actionHistoryContainer) {
        QLayout* actionLayout = m_actionHistoryContainer->layout();
        clearLayoutContents(actionLayout);
        if (actionHistory.isEmpty()) {
            auto* empty = new QLabel(QStringLiteral("No persisted action history yet."));
            empty->setObjectName(QStringLiteral("dimText"));
            actionLayout->addWidget(empty);
        } else {
            for (const QJsonValue& value : actionHistory) {
                const QJsonObject row = value.toObject();
                const QString severity = row.value(QStringLiteral("severity")).toString();
                const QString tone = toneForSeverity(severity);

                auto* item = makeHistoryRow(tone);
                auto* itemLay = new QVBoxLayout(item);
                itemLay->setContentsMargins(16, 14, 16, 14);
                itemLay->setSpacing(10);

                auto* topRow = new QHBoxLayout;
                topRow->setSpacing(8);

                auto* actionLabel = new QLabel(QStringLiteral("%1 / %2")
                    .arg(row.value(QStringLiteral("category_label")).toString(),
                         row.value(QStringLiteral("action_label")).toString()));
                actionLabel->setObjectName(QStringLiteral("historyBadge"));
                actionLabel->setProperty("tone", tone);
                topRow->addWidget(actionLabel);

                auto* severityLabel = new QLabel(titleCase(severity));
                severityLabel->setObjectName(QStringLiteral("historyBadge"));
                severityLabel->setProperty("tone", severity == QStringLiteral("info") ? QStringLiteral("neutral") : tone);
                topRow->addWidget(severityLabel);
                topRow->addStretch(1);

                auto* timeLabel = new QLabel(Fmt::relativeTime(row.value(QStringLiteral("ts")).toDouble()));
                timeLabel->setObjectName(QStringLiteral("historyMeta"));
                topRow->addWidget(timeLabel);
                itemLay->addLayout(topRow);

                auto* titleLabel = new QLabel(row.value(QStringLiteral("message")).toString());
                titleLabel->setObjectName(QStringLiteral("historyTitle"));
                titleLabel->setWordWrap(true);
                itemLay->addWidget(titleLabel);

                const QJsonObject context = row.value(QStringLiteral("context")).toObject();
                const QString jobName = context.value(QStringLiteral("job")).toString();
                const QString stageName = context.value(QStringLiteral("stage")).toString();
                const QJsonArray paths = row.value(QStringLiteral("paths")).toArray();
                if (!paths.isEmpty()) {
                    QStringList pathList;
                    for (const QJsonValue& pathValue : paths) {
                        pathList.append(pathValue.toString());
                    }
                    auto* pathLabel = new QLabel(pathList.join(QStringLiteral("  |  ")));
                    pathLabel->setObjectName(QStringLiteral("historyPath"));
                    pathLabel->setWordWrap(true);
                    itemLay->addWidget(pathLabel);
                }

                QStringList metaBits;
                if (!jobName.isEmpty()) {
                    metaBits << Fmt::prettyJobName(jobName);
                }
                if (!stageName.isEmpty()) {
                    metaBits << titleCase(stageName);
                }
                const int activePathCount = row.value(QStringLiteral("active_path_count")).toInt(0);
                const int pathCount = row.value(QStringLiteral("path_count")).toInt(0);
                if (pathCount > 0) {
                    metaBits << QStringLiteral("%1/%2 linked path(s) still active").arg(activePathCount).arg(pathCount);
                }
                if (metaBits.isEmpty()) {
                    metaBits << QStringLiteral("Recorded by native backend");
                }
                auto* metaLabel = new QLabel(metaBits.join(QStringLiteral("  |  ")));
                metaLabel->setObjectName(QStringLiteral("historyMeta"));
                metaLabel->setWordWrap(true);
                itemLay->addWidget(metaLabel);

                auto* actionRow = new QHBoxLayout;
                actionRow->setContentsMargins(0, 2, 0, 0);
                actionRow->setSpacing(8);
                actionRow->addStretch(1);

                const QString signature = row.value(QStringLiteral("signature")).toString();
                auto* deleteBtn = new QPushButton(QStringLiteral("Delete"));
                deleteBtn->setObjectName(QStringLiteral("actionBtnDanger"));
                deleteBtn->setFixedHeight(34);
                deleteBtn->setMinimumWidth(84);
                connect(deleteBtn, &QPushButton::clicked, this, [this, signature]() {
                    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(signature));
                    m_api->postAction(QStringLiteral("/api/actions/data/delete_action/%1").arg(encoded));
                });
                actionRow->addWidget(deleteBtn);
                itemLay->addLayout(actionRow);

                actionLayout->addWidget(item);
            }
        }
    }
}
