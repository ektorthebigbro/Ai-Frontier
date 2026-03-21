#ifndef DIAGNOSTICS_PAGE_H
#define DIAGNOSTICS_PAGE_H
#include "BasePage.h"
#include <QComboBox>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>

class DiagnosticsPage : public BasePage {
    Q_OBJECT
public:
    explicit DiagnosticsPage(ApiClient* api, QWidget* parent = nullptr);
    void updateFromState(const QJsonObject& state) override;
    void updateFromDiagnostics(const QJsonObject& diag) override;

private:
    void runHealthChecks();
    void reloadModule();
    void clearAllIssues();
    void runSelfHeal();
    void showIssueDeepDive(const QJsonObject& issue);

    QWidget* m_healthList = nullptr;
    QWidget* m_issueList = nullptr;
    QLabel* m_issueCount = nullptr;
    QLabel* m_logSummary = nullptr;
    QComboBox* m_moduleSelect = nullptr;
    QLabel* m_moduleGuide = nullptr;
    QLabel* m_reloadResult = nullptr;
    QPlainTextEdit* m_fixLog = nullptr;
    QPlainTextEdit* m_backendLog = nullptr;
    QJsonObject m_lastDiagnostics;
};
#endif
