#include "SettingsPage.h"

#include "../app/ApiClient.h"
#include "../widgets/GlowCard.h"
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QWidget* makeDivider() {
    auto* d = new QWidget;
    d->setObjectName(QStringLiteral("missionDivider"));
    d->setAttribute(Qt::WA_StyledBackground, true);
    d->setFixedHeight(1);
    return d;
}

} // namespace

SettingsPage::SettingsPage(ApiClient* api, QWidget* parent)
    : BasePage(api, parent)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(24, 22, 24, 20);
    lay->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Configuration"));
    title->setObjectName(QStringLiteral("pageTitle"));
    lay->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral(
        "Model selection, training parameters, and curriculum settings."));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    subtitle->setWordWrap(true);
    lay->addWidget(subtitle);

    // ═══════════════════════════════════════════════
    // Two-column layout
    // ═══════════════════════════════════════════════
    auto* cols = new QHBoxLayout;
    cols->setSpacing(16);

    // ── Left column: Model Selection ──
    {
        auto* modelCard = new GlowCard(QStringLiteral("MODEL SELECTION"));
        auto* mLay = modelCard->contentLayout();
        mLay->setSpacing(12);

        auto addField = [mLay](const QString& label, QWidget* widget) {
            auto* lbl = new QLabel(label);
            lbl->setObjectName(QStringLiteral("settingsLabel"));
            mLay->addWidget(lbl);
            mLay->addWidget(widget);
        };

        m_primaryModel = new QComboBox;
        m_primaryModel->setMinimumHeight(36);
        addField(QStringLiteral("Primary Model"), m_primaryModel);

        m_fallbackModel = new QComboBox;
        m_fallbackModel->setMinimumHeight(36);
        addField(QStringLiteral("Fallback Model"), m_fallbackModel);

        mLay->addWidget(makeDivider());

        m_customModelId = new QLineEdit;
        m_customModelId->setPlaceholderText(QStringLiteral("e.g. Qwen/Qwen2.5-1.5B-Instruct"));
        addField(QStringLiteral("Custom Model ID (overrides dropdown)"), m_customModelId);

        m_judgeAutoDownload = new QComboBox;
        m_judgeAutoDownload->addItems({QStringLiteral("Enabled"), QStringLiteral("Disabled")});
        m_judgeAutoDownload->setMinimumHeight(36);
        addField(QStringLiteral("Auto-Download Models"), m_judgeAutoDownload);

        m_modelCacheHint = new QLabel(QStringLiteral("Cached models: checking..."));
        m_modelCacheHint->setObjectName(QStringLiteral("dimText"));
        m_modelCacheHint->setWordWrap(true);
        mLay->addWidget(m_modelCacheHint);

        cols->addWidget(modelCard, 1);
    }

    // ── Right column: Training + Curriculum ──
    {
        auto* rightCol = new QVBoxLayout;
        rightCol->setSpacing(14);

        // Training card
        auto* trainCard = new GlowCard(QStringLiteral("TRAINING PARAMETERS"));
        auto* tLay = trainCard->contentLayout();
        tLay->setSpacing(12);

        auto addField = [tLay](const QString& label, QWidget* widget) {
            auto* lbl = new QLabel(label);
            lbl->setObjectName(QStringLiteral("settingsLabel"));
            tLay->addWidget(lbl);
            tLay->addWidget(widget);
        };

        m_batchSizeCombo = new QComboBox;
        m_batchSizeCombo->addItems({
            QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("4"),
            QStringLiteral("8"), QStringLiteral("16"), QStringLiteral("32")
        });
        m_batchSizeCombo->setCurrentText(QStringLiteral("8"));
        m_batchSizeCombo->setMinimumHeight(36);
        addField(QStringLiteral("Batch Size"), m_batchSizeCombo);

        m_lrCombo = new QComboBox;
        m_lrCombo->addItems({
            QStringLiteral("1e-5"), QStringLiteral("3e-5"), QStringLiteral("1e-4"),
            QStringLiteral("2e-4"), QStringLiteral("3e-4"), QStringLiteral("5e-4"),
            QStringLiteral("1e-3")
        });
        m_lrCombo->setCurrentText(QStringLiteral("2e-4"));
        m_lrCombo->setEditable(true);
        m_lrCombo->setMinimumHeight(36);
        addField(QStringLiteral("Learning Rate"), m_lrCombo);

        auto* twoCol = new QHBoxLayout;
        twoCol->setSpacing(12);

        auto* vramLay = new QVBoxLayout;
        auto* vramLabel = new QLabel(QStringLiteral("VRAM Ceiling (GB)"));
        vramLabel->setObjectName(QStringLiteral("settingsLabel"));
        vramLay->addWidget(vramLabel);
        m_vramCeilEdit = new QLineEdit(QStringLiteral("5.5"));
        m_vramCeilEdit->setMinimumHeight(36);
        vramLay->addWidget(m_vramCeilEdit);
        twoCol->addLayout(vramLay, 1);

        auto* offloadLay = new QVBoxLayout;
        auto* offloadLabel = new QLabel(QStringLiteral("CPU Offload"));
        offloadLabel->setObjectName(QStringLiteral("settingsLabel"));
        offloadLay->addWidget(offloadLabel);
        m_cpuOffloadCombo = new QComboBox;
        m_cpuOffloadCombo->addItems({QStringLiteral("Enabled"), QStringLiteral("Disabled")});
        m_cpuOffloadCombo->setMinimumHeight(36);
        offloadLay->addWidget(m_cpuOffloadCombo);
        twoCol->addLayout(offloadLay, 1);

        tLay->addLayout(twoCol);

        m_mixedPrecisionCombo = new QComboBox;
        m_mixedPrecisionCombo->addItems({QStringLiteral("Enabled"), QStringLiteral("Disabled")});
        m_mixedPrecisionCombo->setMinimumHeight(36);
        addField(QStringLiteral("Mixed Precision (FP16)"), m_mixedPrecisionCombo);

        rightCol->addWidget(trainCard);

        // Curriculum card
        auto* curCard = new GlowCard(QStringLiteral("CURRICULUM"));
        auto* cLay = curCard->contentLayout();
        cLay->setSpacing(10);

        auto* cLabel = new QLabel(QStringLiteral("Current Stage"));
        cLabel->setObjectName(QStringLiteral("settingsLabel"));
        cLay->addWidget(cLabel);

        m_curriculumStage = new QComboBox;
        m_curriculumStage->setMinimumHeight(36);
        m_curriculumStage->addItems({
            QStringLiteral("Stage 1 - Basic Completion"),
            QStringLiteral("Stage 2 - Instruction Following"),
            QStringLiteral("Stage 3 - Reasoning"),
            QStringLiteral("Stage 4 - Complex Reasoning"),
            QStringLiteral("Stage 5 - Mastery"),
        });
        cLay->addWidget(m_curriculumStage);

        auto* curDesc = new QLabel(QStringLiteral(
            "Each stage progressively increases task complexity and data mixing ratios."));
        curDesc->setObjectName(QStringLiteral("dimText"));
        curDesc->setWordWrap(true);
        cLay->addWidget(curDesc);

        rightCol->addWidget(curCard);
        rightCol->addStretch(1);

        auto* rightWidget = new QWidget;
        rightWidget->setLayout(rightCol);
        cols->addWidget(rightWidget, 1);
    }

    lay->addLayout(cols);

    // ── Save button ──
    auto* saveBtn = new QPushButton(QStringLiteral("Save Settings"));
    saveBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
    saveBtn->setMinimumHeight(40);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsPage::saveQuickConfig);
    lay->addWidget(saveBtn);

    // ── Advanced: YAML editor ──
    {
        m_yamlCard = new GlowCard(QStringLiteral("FULL CONFIGURATION (YAML)"));
        auto* yLay = m_yamlCard->contentLayout();

        m_yamlEditor = new QPlainTextEdit;
        m_yamlEditor->setObjectName(QStringLiteral("yamlEditor"));
        m_yamlEditor->setMinimumHeight(300);
        yLay->addWidget(m_yamlEditor);

        auto* yamlSaveBtn = new QPushButton(QStringLiteral("Save YAML"));
        yamlSaveBtn->setObjectName(QStringLiteral("actionBtnPrimary"));
        yamlSaveBtn->setMinimumHeight(36);
        connect(yamlSaveBtn, &QPushButton::clicked, this, &SettingsPage::saveYamlConfig);
        yLay->addWidget(yamlSaveBtn);

        m_yamlCard->setVisible(false);
        lay->addWidget(m_yamlCard);
    }

    // ── Advanced: Training internals ──
    {
        m_advancedSettingsCard = new GlowCard(QStringLiteral("TRAINING INTERNALS"));
        auto* advLay = m_advancedSettingsCard->contentLayout();
        advLay->setSpacing(10);

        auto* advNote = new QLabel(QStringLiteral(
            "Fine-tune throughput, memory behavior, and checkpoint cadence."));
        advNote->setObjectName(QStringLiteral("controlDescription"));
        advNote->setWordWrap(true);
        advLay->addWidget(advNote);

        auto* advGrid = new QGridLayout;
        advGrid->setHorizontalSpacing(14);
        advGrid->setVerticalSpacing(8);

        auto addAdvField = [advGrid](const QString& label, QLineEdit*& edit, const QString& value, int row, int col) {
            auto* lbl = new QLabel(label);
            lbl->setObjectName(QStringLiteral("settingsLabel"));
            advGrid->addWidget(lbl, row * 2, col);
            edit = new QLineEdit(value);
            edit->setMinimumHeight(34);
            advGrid->addWidget(edit, row * 2 + 1, col);
        };

        addAdvField(QStringLiteral("Micro Batch Size"), m_microBatchEdit, QStringLiteral("2"), 0, 0);
        addAdvField(QStringLiteral("Gradient Accumulation"), m_gradAccumEdit, QStringLiteral("4"), 0, 1);
        addAdvField(QStringLiteral("Warmup Steps"), m_warmupStepsEdit, QStringLiteral("500"), 1, 0);
        addAdvField(QStringLiteral("Max Grad Norm"), m_maxGradNormEdit, QStringLiteral("1.0"), 1, 1);
        addAdvField(QStringLiteral("Checkpoint Every"), m_checkpointEveryEdit, QStringLiteral("500"), 2, 0);
        addAdvField(QStringLiteral("Eval Every"), m_evalEveryEdit, QStringLiteral("500"), 2, 1);
        advLay->addLayout(advGrid);

        auto* gcLay = new QVBoxLayout;
        auto* gcLabel = new QLabel(QStringLiteral("Gradient Checkpointing"));
        gcLabel->setObjectName(QStringLiteral("settingsLabel"));
        gcLay->addWidget(gcLabel);
        m_gradientCheckpointCombo = new QComboBox;
        m_gradientCheckpointCombo->addItems({QStringLiteral("Enabled"), QStringLiteral("Disabled")});
        m_gradientCheckpointCombo->setMinimumHeight(34);
        gcLay->addWidget(m_gradientCheckpointCombo);
        advLay->addLayout(gcLay);

        m_advancedSettingsCard->setVisible(false);
        lay->addWidget(m_advancedSettingsCard);
    }

    lay->addStretch();

    auto* wrapper = buildScrollWrapper(page);
    auto* outerLay = new QVBoxLayout(this);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->addWidget(wrapper);

    connectDirtySignals();
    connect(m_api, &ApiClient::configSaved, this, [this](bool ok, const QString&) {
        m_saveInFlight = false;
        if (!ok)
            m_dirty = true;
    });
}

void SettingsPage::updateFromState(const QJsonObject& state)
{
    // Don't overwrite the user's in-progress edits
    if (m_dirty || m_saveInFlight)
        return;

    m_updating = true;
    const auto config = state[QStringLiteral("config")].toObject();
    const auto judgeConfig = config[QStringLiteral("large_judge")].toObject();
    const auto trainingConfig = config[QStringLiteral("training")].toObject();
    const auto curriculumConfig = config[QStringLiteral("curriculum")].toObject();
    const auto fallbackModels = judgeConfig[QStringLiteral("fallback_model_ids")].toArray();
    const QString judgeModelId = judgeConfig[QStringLiteral("model_id")].toString();
    const bool autoDownload = judgeConfig[QStringLiteral("auto_download_required_models")].toBool(true);

    auto setLineEdit = [](QLineEdit* edit, const QString& value) {
        if (edit && !edit->hasFocus() && edit->text() != value)
            edit->setText(value);
    };
    auto setComboIndex = [](QComboBox* combo, int index) {
        if (combo && index >= 0 && index < combo->count() && combo->currentIndex() != index)
            combo->setCurrentIndex(index);
    };

    // Populate model dropdowns from catalog
    const auto catalog = state[QStringLiteral("model_catalog")].toObject();
    const auto judgePresets = catalog[QStringLiteral("large_judge")].toArray();
    if (!m_populatedPresets && m_primaryModel && m_primaryModel->count() == 0 && !judgePresets.isEmpty()) {
        for (const auto& presetValue : judgePresets) {
            const auto preset = presetValue.toObject();
            const QString id = preset[QStringLiteral("id")].toString();
            const QString label = preset[QStringLiteral("label")].toString();
            const bool recommended = preset[QStringLiteral("recommended")].toBool();
            m_primaryModel->addItem(label + (recommended ? QStringLiteral(" \u2605") : QString()), id);
            m_fallbackModel->addItem(label, id);
        }
        m_populatedPresets = true;
    }

    bool matchedPrimaryModel = false;
    if (m_primaryModel && !judgeModelId.isEmpty()) {
        for (int i = 0; i < m_primaryModel->count(); ++i) {
            if (m_primaryModel->itemData(i).toString() == judgeModelId) {
                setComboIndex(m_primaryModel, i);
                matchedPrimaryModel = true;
                break;
            }
        }
    }
    setLineEdit(m_customModelId, matchedPrimaryModel ? QString() : judgeModelId);
    if (m_fallbackModel && !fallbackModels.isEmpty()) {
        const QString fbId = fallbackModels.first().toString();
        for (int i = 0; i < m_fallbackModel->count(); ++i) {
            if (m_fallbackModel->itemData(i).toString() == fbId) {
                setComboIndex(m_fallbackModel, i);
                break;
            }
        }
    }
    setComboIndex(m_judgeAutoDownload, autoDownload ? 0 : 1);

    // Cache hint
    const auto cache = state[QStringLiteral("model_cache")].toObject();
    const auto judgeCache = cache[QStringLiteral("large_judge")].toObject();
    QStringList cached;
    for (auto it = judgeCache.begin(); it != judgeCache.end(); ++it) {
        const auto obj = it.value().toObject();
        if (obj[QStringLiteral("cached")].toBool())
            cached.append(QStringLiteral("%1 (%2 MB)").arg(it.key()).arg(obj[QStringLiteral("size_mb")].toInt()));
    }
    if (m_modelCacheHint) {
        m_modelCacheHint->setText(cached.isEmpty()
            ? QStringLiteral("No models cached locally")
            : QStringLiteral("Cached: %1").arg(cached.join(QStringLiteral(", "))));
    }

    // Training fields
    const int batchSize = trainingConfig[QStringLiteral("batch_size")].toInt(8);
    if (m_batchSizeCombo) {
        const QString bsText = QString::number(batchSize);
        int idx = m_batchSizeCombo->findText(bsText);
        if (idx >= 0) setComboIndex(m_batchSizeCombo, idx);
    }

    const double lr = trainingConfig[QStringLiteral("learning_rate")].toDouble(2e-4);
    if (m_lrCombo && !m_lrCombo->hasFocus()) {
        const QString lrText = QString::number(lr, 'g', 6);
        int idx = m_lrCombo->findText(lrText);
        if (idx >= 0) setComboIndex(m_lrCombo, idx);
        else m_lrCombo->setCurrentText(lrText);
    }

    setLineEdit(m_vramCeilEdit, QString::number(trainingConfig[QStringLiteral("vram_ceiling_gb")].toDouble(5.5), 'g', 4));
    setComboIndex(m_cpuOffloadCombo, trainingConfig[QStringLiteral("optimizer_cpu_offload")].toBool(true) ? 0 : 1);
    setComboIndex(m_mixedPrecisionCombo, trainingConfig[QStringLiteral("mixed_precision")].toBool(true) ? 0 : 1);
    setComboIndex(m_curriculumStage, qMax(0, curriculumConfig[QStringLiteral("current_stage")].toInt(1) - 1));

    // Advanced fields
    setLineEdit(m_microBatchEdit, QString::number(trainingConfig[QStringLiteral("micro_batch_size")].toInt(2)));
    setLineEdit(m_gradAccumEdit, QString::number(trainingConfig[QStringLiteral("gradient_accumulation_steps")].toInt(4)));
    setLineEdit(m_warmupStepsEdit, QString::number(trainingConfig[QStringLiteral("warmup_steps")].toInt(500)));
    setLineEdit(m_maxGradNormEdit, QString::number(trainingConfig[QStringLiteral("max_grad_norm")].toDouble(1.0), 'g', 4));
    setLineEdit(m_checkpointEveryEdit, QString::number(trainingConfig[QStringLiteral("checkpoint_every")].toInt(500)));
    setLineEdit(m_evalEveryEdit, QString::number(trainingConfig[QStringLiteral("eval_every")].toInt(500)));
    setComboIndex(m_gradientCheckpointCombo, trainingConfig[QStringLiteral("gradient_checkpointing")].toBool(true) ? 0 : 1);

    // YAML
    const QString configText = state[QStringLiteral("config_text")].toString();
    if (m_yamlEditor && !m_yamlEditor->hasFocus() && m_yamlEditor->toPlainText() != configText && !configText.isEmpty())
        m_yamlEditor->setPlainText(configText);

    m_updating = false;
}

void SettingsPage::saveQuickConfig()
{
    QJsonObject config;

    QJsonObject judgeConfig;
    if (m_customModelId && !m_customModelId->text().trimmed().isEmpty()) {
        judgeConfig[QStringLiteral("model_id")] = m_customModelId->text().trimmed();
    } else if (m_primaryModel && m_primaryModel->currentIndex() >= 0) {
        judgeConfig[QStringLiteral("model_id")] = m_primaryModel->currentData().toString();
    }

    if (m_fallbackModel && m_fallbackModel->currentIndex() >= 0) {
        QJsonArray fallbacks;
        fallbacks.append(m_fallbackModel->currentData().toString());
        judgeConfig[QStringLiteral("fallback_model_ids")] = fallbacks;
    }

    if (m_judgeAutoDownload)
        judgeConfig[QStringLiteral("auto_download_required_models")] = (m_judgeAutoDownload->currentIndex() == 0);
    judgeConfig[QStringLiteral("enabled")] = true;
    config[QStringLiteral("large_judge")] = judgeConfig;

    QJsonObject trainingConfig;
    if (m_batchSizeCombo)
        trainingConfig[QStringLiteral("batch_size")] = m_batchSizeCombo->currentText().toInt();
    if (m_lrCombo)
        trainingConfig[QStringLiteral("learning_rate")] = m_lrCombo->currentText().toDouble();
    if (m_vramCeilEdit)
        trainingConfig[QStringLiteral("vram_ceiling_gb")] = m_vramCeilEdit->text().toDouble();
    if (m_cpuOffloadCombo)
        trainingConfig[QStringLiteral("optimizer_cpu_offload")] = (m_cpuOffloadCombo->currentIndex() == 0);
    if (m_mixedPrecisionCombo)
        trainingConfig[QStringLiteral("mixed_precision")] = (m_mixedPrecisionCombo->currentIndex() == 0);
    if (m_microBatchEdit)
        trainingConfig[QStringLiteral("micro_batch_size")] = m_microBatchEdit->text().toInt();
    if (m_gradAccumEdit)
        trainingConfig[QStringLiteral("gradient_accumulation_steps")] = m_gradAccumEdit->text().toInt();
    if (m_warmupStepsEdit)
        trainingConfig[QStringLiteral("warmup_steps")] = m_warmupStepsEdit->text().toInt();
    if (m_maxGradNormEdit)
        trainingConfig[QStringLiteral("max_grad_norm")] = m_maxGradNormEdit->text().toDouble();
    if (m_checkpointEveryEdit)
        trainingConfig[QStringLiteral("checkpoint_every")] = m_checkpointEveryEdit->text().toInt();
    if (m_evalEveryEdit)
        trainingConfig[QStringLiteral("eval_every")] = m_evalEveryEdit->text().toInt();
    if (m_gradientCheckpointCombo)
        trainingConfig[QStringLiteral("gradient_checkpointing")] = (m_gradientCheckpointCombo->currentIndex() == 0);
    config[QStringLiteral("training")] = trainingConfig;

    QJsonObject curriculumConfig;
    if (m_curriculumStage)
        curriculumConfig[QStringLiteral("current_stage")] = m_curriculumStage->currentIndex() + 1;
    config[QStringLiteral("curriculum")] = curriculumConfig;

    m_api->postConfig(config);
    m_saveInFlight = true;
    m_dirty = false;
}

void SettingsPage::saveYamlConfig()
{
    if (m_yamlEditor)
        m_api->postConfigYaml(m_yamlEditor->toPlainText());
    m_saveInFlight = true;
    m_dirty = false;
}

void SettingsPage::setAdvancedMode(bool advanced)
{
    auto animateVisibility = [](QWidget* widget, bool show) {
        if (!widget) return;
        if (show) {
            widget->setVisible(true);
            auto* effect = new QGraphicsOpacityEffect(widget);
            widget->setGraphicsEffect(effect);
            auto* anim = new QPropertyAnimation(effect, "opacity");
            anim->setDuration(250);
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
            QObject::connect(anim, &QPropertyAnimation::finished, widget, [widget]() {
                widget->setGraphicsEffect(nullptr);
            });
            anim->start(QAbstractAnimation::DeleteWhenStopped);
            return;
        }
        auto* effect = new QGraphicsOpacityEffect(widget);
        widget->setGraphicsEffect(effect);
        auto* anim = new QPropertyAnimation(effect, "opacity");
        anim->setDuration(200);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        QObject::connect(anim, &QPropertyAnimation::finished, widget, [widget]() {
            widget->setVisible(false);
            widget->setGraphicsEffect(nullptr);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    };

    animateVisibility(m_yamlCard, advanced);
    animateVisibility(m_advancedSettingsCard, advanced);
}

void SettingsPage::markDirty()
{
    if (!m_updating)
        m_dirty = true;
}

void SettingsPage::connectDirtySignals()
{
    // Combo boxes
    auto connectCombo = [this](QComboBox* combo) {
        if (combo)
            connect(combo, &QComboBox::currentIndexChanged, this, &SettingsPage::markDirty);
    };
    connectCombo(m_primaryModel);
    // Changing the dropdown clears the custom override so it doesn't win on save
    if (m_primaryModel && m_customModelId) {
        connect(m_primaryModel, &QComboBox::currentIndexChanged, this, [this]() {
            if (!m_updating)
                m_customModelId->clear();
        });
    }
    connectCombo(m_fallbackModel);
    connectCombo(m_judgeAutoDownload);
    connectCombo(m_batchSizeCombo);
    connectCombo(m_lrCombo);
    connectCombo(m_cpuOffloadCombo);
    connectCombo(m_mixedPrecisionCombo);
    connectCombo(m_curriculumStage);
    connectCombo(m_gradientCheckpointCombo);

    // Line edits
    auto connectEdit = [this](QLineEdit* edit) {
        if (edit)
            connect(edit, &QLineEdit::textEdited, this, &SettingsPage::markDirty);
    };
    connectEdit(m_customModelId);
    connectEdit(m_vramCeilEdit);
    connectEdit(m_microBatchEdit);
    connectEdit(m_gradAccumEdit);
    connectEdit(m_warmupStepsEdit);
    connectEdit(m_maxGradNormEdit);
    connectEdit(m_checkpointEveryEdit);
    connectEdit(m_evalEveryEdit);

    // YAML editor
    if (m_yamlEditor)
        connect(m_yamlEditor, &QPlainTextEdit::textChanged, this, &SettingsPage::markDirty);
}
