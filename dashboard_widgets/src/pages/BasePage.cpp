#include "BasePage.h"
#include <QScrollArea>
#include <QFrame>

BasePage::BasePage(ApiClient* api, QWidget* parent)
    : QWidget(parent), m_api(api) {}

QWidget* BasePage::buildScrollWrapper(QWidget* content) {
    auto* scroll = new QScrollArea;
    content->setObjectName(QStringLiteral("pageCanvas"));
    content->setAttribute(Qt::WA_StyledBackground, true);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setObjectName(QStringLiteral("pageViewport"));
    scroll->viewport()->setAutoFillBackground(false);
    return scroll;
}
