#ifndef SETTINGS_PAGE_H
#define SETTINGS_PAGE_H
#include "BasePage.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>

class GlowCard;

class SettingsPage : public BasePage {
    Q_OBJECT
public:
    explicit SettingsPage(ApiClient* api, QWidget* parent = nullptr);
    void updateFromState(const QJsonObject& state) override;
    bool hasAdvancedPanels() const override { return true; }
    void setAdvancedMode(bool advanced) override;

private:
    void saveQuickConfig();
    void saveYamlConfig();

    // Model settings
    QComboBox* m_primaryModel = nullptr;
    QComboBox* m_fallbackModel = nullptr;
    QComboBox* m_judgeAutoDownload = nullptr;
    QLineEdit* m_customModelId = nullptr;
    QLabel* m_modelCacheHint = nullptr;

    // Training settings
    QComboBox* m_batchSizeCombo = nullptr;
    QComboBox* m_lrCombo = nullptr;
    QLineEdit* m_vramCeilEdit = nullptr;
    QComboBox* m_cpuOffloadCombo = nullptr;
    QComboBox* m_mixedPrecisionCombo = nullptr;
    QComboBox* m_curriculumStage = nullptr;

    // Advanced settings
    QLineEdit* m_microBatchEdit = nullptr;
    QLineEdit* m_gradAccumEdit = nullptr;
    QLineEdit* m_warmupStepsEdit = nullptr;
    QLineEdit* m_maxGradNormEdit = nullptr;
    QLineEdit* m_checkpointEveryEdit = nullptr;
    QLineEdit* m_evalEveryEdit = nullptr;
    QComboBox* m_gradientCheckpointCombo = nullptr;

    // YAML
    QPlainTextEdit* m_yamlEditor = nullptr;
    GlowCard* m_yamlCard = nullptr;
    GlowCard* m_advancedSettingsCard = nullptr;

    bool m_populatedPresets = false;
    bool m_dirty = false;
    bool m_saveInFlight = false;
    bool m_updating = false;

    void markDirty();
    void connectDirtySignals();
};
#endif
