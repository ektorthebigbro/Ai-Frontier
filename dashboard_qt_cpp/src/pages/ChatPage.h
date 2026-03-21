#ifndef CHAT_PAGE_H
#define CHAT_PAGE_H
#include "BasePage.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>

class ChatPage : public BasePage {
    Q_OBJECT
public:
    explicit ChatPage(ApiClient* api, QWidget* parent = nullptr);
    void updateFromState(const QJsonObject& state) override;

private:
    void sendMessage();
    void appendPreset(const QString& prompt);
    void refreshCheckpointSelector(const QJsonObject& checkpoint);

    QTextEdit* m_chatMessages = nullptr;
    QLineEdit* m_chatInput = nullptr;
    QLineEdit* m_chatTemperature = nullptr;
    QLineEdit* m_chatMaxTokens = nullptr;
    QLineEdit* m_chatTopK = nullptr;
    QLineEdit* m_chatTopP = nullptr;
    QComboBox* m_chatMode = nullptr;
    QComboBox* m_chatCheckpointSelect = nullptr;
    QLabel* m_chatCheckpointChip = nullptr;
    QLabel* m_chatTrainingChip = nullptr;
    QLabel* m_chatTurnCount = nullptr;
    QLabel* m_chatSendState = nullptr;
    int m_chatTurns = 0;
    QString m_selectedCheckpointPath;
};
#endif
