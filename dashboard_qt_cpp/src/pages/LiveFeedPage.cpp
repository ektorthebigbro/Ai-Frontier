#include "LiveFeedPage.h"
#include "../app/ApiClient.h"
#include "../util/Formatters.h"
#include "../widgets/GlowCard.h"
#include <QDateTime>
#include <QJsonArray>
#include <QLabel>
#include <QScrollBar>
#include <QVBoxLayout>

LiveFeedPage::LiveFeedPage(ApiClient* api, QWidget* parent)
    : BasePage(api, parent)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(28, 26, 28, 24);
    lay->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Live Feed"));
    title->setObjectName(QStringLiteral("pageHeroTitle"));
    lay->addWidget(title);

    auto* subtitle = new QLabel(
        QStringLiteral("Watch job events, alerts, and backend process activity stream in real time."));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    subtitle->setWordWrap(true);
    lay->addWidget(subtitle);

    auto* card = new GlowCard(QStringLiteral("Event Stream"));
    auto* cardLay = card->contentLayout();
    cardLay->setSpacing(10);

    m_feedArea = new QPlainTextEdit;
    m_feedArea->setReadOnly(true);
    m_feedArea->setObjectName(QStringLiteral("feedArea"));
    m_feedArea->setMaximumBlockCount(1000);
    m_feedArea->setMinimumHeight(300);
    cardLay->addWidget(m_feedArea, 1);

    lay->addWidget(card, 1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(buildScrollWrapper(page));
}

void LiveFeedPage::updateFromState(const QJsonObject& state)
{
    const auto feed = state[QStringLiteral("feed")].toArray();
    if (feed.isEmpty() || !m_feedArea) {
        return;
    }

    QStringList lines;
    for (const auto& item : feed) {
        const auto obj = item.toObject();
        const double ts = obj[QStringLiteral("ts")].toDouble();
        const QDateTime dt = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(ts));
        lines.append(QStringLiteral("[%1] %2 %3 %4")
            .arg(dt.toString(QStringLiteral("HH:mm:ss")))
            .arg(obj[QStringLiteral("job")].toString(obj[QStringLiteral("type")].toString()))
            .arg(obj[QStringLiteral("stage")].toString())
            .arg(obj[QStringLiteral("message")].toString()));
    }
    const QString newText = lines.join(QStringLiteral("\n"));
    if (m_feedArea->toPlainText() != newText) {
        const bool atBottom = m_feedArea->verticalScrollBar()->value()
            >= m_feedArea->verticalScrollBar()->maximum() - 10;
        m_feedArea->setPlainText(newText);
        if (atBottom) {
            m_feedArea->verticalScrollBar()->setValue(
                m_feedArea->verticalScrollBar()->maximum());
        }
    }
}
