#ifndef BASE_PAGE_H
#define BASE_PAGE_H

#include <QJsonObject>
#include <QWidget>

class ApiClient;

class BasePage : public QWidget {
    Q_OBJECT
public:
    explicit BasePage(ApiClient* api, QWidget* parent = nullptr);
    virtual ~BasePage() = default;

    virtual void updateFromState(const QJsonObject& state) = 0;
    virtual void updateFromDiagnostics(const QJsonObject& /*diag*/) {}

    // Whether this page uses advanced-mode-only panels
    virtual bool hasAdvancedPanels() const { return false; }
    virtual void setAdvancedMode(bool /*advanced*/) {}

protected:
    ApiClient* m_api;
    QWidget* buildScrollWrapper(QWidget* content);
};

#endif
