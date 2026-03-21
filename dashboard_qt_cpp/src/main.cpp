#include "app/DashboardWindow.h"
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AI Frontier"));
    app.setApplicationVersion(QStringLiteral("3.0.0"));

    DashboardWindow window;
    window.show();

    return app.exec();
}
