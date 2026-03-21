#pragma once

#include <QJsonValue>
#include <QString>

namespace SimpleYaml {

QJsonValue parse(const QString& text, QString* error = nullptr);
QString dump(const QJsonValue& value, int indent = 0);

}  // namespace SimpleYaml
