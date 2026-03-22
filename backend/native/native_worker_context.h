#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct NativeWorkerContext {
    QString jobName;
    QString projectRoot;
    QString configPath;
    QString modulePath;
    QStringList arguments;
    QJsonObject config;
};
