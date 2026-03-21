#ifndef REPORTS_PAGE_H
#define REPORTS_PAGE_H
#include "BasePage.h"
#include <QPlainTextEdit>

class ReportsPage : public BasePage {
    Q_OBJECT
public:
    explicit ReportsPage(ApiClient* api, QWidget* parent = nullptr);
    void updateFromState(const QJsonObject& state) override;
private:
    QPlainTextEdit* m_reportArea = nullptr;
};
#endif
