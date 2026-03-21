#include "ReportsPage.h"
#include "../app/ApiClient.h"
#include "../util/Formatters.h"
#include "../widgets/GlowCard.h"
#include <QLabel>
#include <QVBoxLayout>

ReportsPage::ReportsPage(ApiClient* api, QWidget* parent)
    : BasePage(api, parent)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(28, 26, 28, 24);
    lay->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("Reports"));
    title->setObjectName(QStringLiteral("pageHeroTitle"));
    lay->addWidget(title);

    auto* subtitle = new QLabel(QStringLiteral("Review evaluation results and benchmark scores."));
    subtitle->setObjectName(QStringLiteral("pageSubtitle"));
    subtitle->setWordWrap(true);
    lay->addWidget(subtitle);

    auto* card = new GlowCard(QStringLiteral("Evaluation Report"));
    auto* cardLay = card->contentLayout();
    cardLay->setSpacing(10);

    m_reportArea = new QPlainTextEdit;
    m_reportArea->setReadOnly(true);
    m_reportArea->setObjectName(QStringLiteral("reportArea"));
    m_reportArea->setPlainText(QStringLiteral("No reports yet."));
    m_reportArea->setMinimumHeight(300);
    cardLay->addWidget(m_reportArea, 1);

    lay->addWidget(card, 1);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(buildScrollWrapper(page));
}

void ReportsPage::updateFromState(const QJsonObject& state)
{
    const QString reportText = state[QStringLiteral("report")].toString();
    if (m_reportArea) {
        m_reportArea->setPlainText(reportText.isEmpty()
            ? QStringLiteral("No reports yet.")
            : reportText);
    }
}
