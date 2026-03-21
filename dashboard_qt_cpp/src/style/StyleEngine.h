#ifndef STYLE_ENGINE_H
#define STYLE_ENGINE_H
#include <QString>

namespace StyleEngine {
    QString globalStyle();
    QString chromeBarStyle();
    QString sidebarStyle();
    QString cardStyle();
    QString inputStyle();
    QString buttonStyle();
    QString progressBarStyle();
    QString chartStyle();
    QString alertStyle();
    QString scrollbarStyle();
    QString statusBarStyle();
    QString pageStyle();

    // Concatenates all sub-styles
    QString fullStyleSheet();
}
#endif
