#ifndef JOBS_PAGE_H
#define JOBS_PAGE_H

#include "BasePage.h"
#include <QHash>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

class GlowCard;
class GradientProgressBar;
class QBoxLayout;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QResizeEvent;
class QVBoxLayout;
class QWidget;

class JobsPage : public BasePage {
    Q_OBJECT
public:
    explicit JobsPage(ApiClient* api, QWidget* parent = nullptr);
    void updateFromState(const QJsonObject& state) override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    struct JobCardUi {
        GlowCard* card = nullptr;
        QLabel* statusLabel = nullptr;
        QLabel* stageLabel = nullptr;
        QLabel* pctLabel = nullptr;
        GradientProgressBar* progressBar = nullptr;
        QLabel* messageLabel = nullptr;
        QLabel* etaLabel = nullptr;
        QLabel* noteLabel = nullptr;
        QLabel* logLabel = nullptr;
        QHBoxLayout* actionRow = nullptr;
    };

    void ensureCards();
    void rebuildGrid();
    void rebuildSummaryGrid();
    void updateResponsiveLayout();
    void updateActivityCard(const QJsonObject& state);
    void updateJobCard(const QString& name,
                       const QJsonObject& job,
                       const QJsonObject& process,
                       const QJsonObject& state);
    void updateFocusCard(const QJsonObject& state);
    int contentWidth() const;

    QWidget* m_pageRoot = nullptr;
    QBoxLayout* m_headerLayout = nullptr;
    QWidget* m_headerPrimaryColumn = nullptr;
    QWidget* m_focusCard = nullptr;
    QLabel* m_focusStatus = nullptr;
    QLabel* m_focusName = nullptr;
    QLabel* m_focusSummary = nullptr;
    QLabel* m_focusMeta = nullptr;
    QLabel* m_focusNextValue = nullptr;
    QLabel* m_focusRecoveryValue = nullptr;
    QPushButton* m_focusActionBtn = nullptr;
    QBoxLayout* m_focusFooterLayout = nullptr;
    QWidget* m_summaryContainer = nullptr;
    QGridLayout* m_summaryGrid = nullptr;
    GlowCard* m_activityCard = nullptr;
    QLabel* m_activitySummary = nullptr;
    QWidget* m_activityListHost = nullptr;
    QVBoxLayout* m_activityList = nullptr;
    QWidget* m_jobContainer = nullptr;
    QGridLayout* m_jobGrid = nullptr;
    QLabel* m_activeCount = nullptr;
    QLabel* m_readyCount = nullptr;
    QLabel* m_resumableCount = nullptr;
    QLabel* m_attentionCount = nullptr;
    QVector<GlowCard*> m_summaryCards;
    QHash<QString, JobCardUi> m_jobCards;
    QJsonObject m_lastState;
    QStringList m_jobOrder;
    int m_columnCount = 0;
    int m_summaryColumnCount = 0;
    bool m_headerCompact = false;
};

#endif
