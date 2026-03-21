#ifndef DATA_PAGE_H
#define DATA_PAGE_H
#include "BasePage.h"
#include <QLabel>

class QVBoxLayout;

class DataPage : public BasePage {
    Q_OBJECT
public:
    explicit DataPage(ApiClient* api, QWidget* parent = nullptr);
    void updateFromState(const QJsonObject& state) override;

private:
    QWidget* m_datasetContainer = nullptr;
    QWidget* m_depContainer = nullptr;
    QWidget* m_fileHistoryContainer = nullptr;
    QWidget* m_actionHistoryContainer = nullptr;
    QLabel* m_datasetSummary = nullptr;
    QLabel* m_depSectionSummary = nullptr;
    QLabel* m_fileHistorySummary = nullptr;
    QLabel* m_actionHistorySummary = nullptr;
    QLabel* m_totalSamples = nullptr;
    QLabel* m_cacheStatus = nullptr;
    QLabel* m_depSummary = nullptr;
};
#endif
