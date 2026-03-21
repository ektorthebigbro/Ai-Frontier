#ifndef HELPERS_H
#define HELPERS_H
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QString>

namespace Ui {

inline QLabel* makeDot(const QString& color) {
    auto* dot = new QLabel(QStringLiteral("\u2B24"));
    dot->setFixedSize(12, 12);
    dot->setAlignment(Qt::AlignCenter);
    dot->setStyleSheet(QStringLiteral("color: %1; font-size: 8px;").arg(color));
    return dot;
}

inline QLabel* makeRuntimeKey(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("runtimeKey"));
    label->setMinimumWidth(34);
    return label;
}

inline QLabel* makeRuntimeVal() {
    auto* label = new QLabel(QStringLiteral("--"));
    label->setObjectName(QStringLiteral("runtimeVal"));
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

inline QLabel* makeSectionLabel(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QStringLiteral("sectionLabel"));
    return label;
}

inline QWidget* wrapScroll(QWidget* content) {
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

}  // namespace Ui
#endif
