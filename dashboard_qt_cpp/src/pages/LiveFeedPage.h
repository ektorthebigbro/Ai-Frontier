#ifndef LIVE_FEED_PAGE_H
#define LIVE_FEED_PAGE_H
#include "BasePage.h"
#include <QPlainTextEdit>

class LiveFeedPage : public BasePage {
    Q_OBJECT
public:
    explicit LiveFeedPage(ApiClient* api, QWidget* parent = nullptr);
    void updateFromState(const QJsonObject& state) override;
private:
    QPlainTextEdit* m_feedArea = nullptr;
};
#endif
