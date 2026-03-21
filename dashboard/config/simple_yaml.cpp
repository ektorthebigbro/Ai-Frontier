#include "simple_yaml.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace {

struct ParsedLine {
    int indent = 0;
    QString text;
};

int mappingColonIndex(const QString& text) {
    bool inSingle = false;
    bool inDouble = false;
    bool escaped = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && inDouble) {
            escaped = true;
            continue;
        }
        if (ch == '\'' && !inDouble) {
            inSingle = !inSingle;
            continue;
        }
        if (ch == '"' && !inSingle) {
            inDouble = !inDouble;
            continue;
        }
        if (ch != ':' || inSingle || inDouble) {
            continue;
        }
        const bool endOfLine = (i + 1) >= text.size();
        const bool hasYamlSeparator = !endOfLine && text.at(i + 1).isSpace();
        if (endOfLine || hasYamlSeparator) {
            return i;
        }
    }
    return -1;
}

QString unescapeQuoted(const QString& text, QChar quote) {
    if (text.size() < 2 || !text.startsWith(quote) || !text.endsWith(quote)) {
        return text;
    }

    QString result;
    bool escaped = false;
    for (int i = 1; i < text.size() - 1; ++i) {
        const QChar ch = text.at(i);
        if (quote == '"' && escaped) {
            switch (ch.unicode()) {
            case 'n':
                result.append('\n');
                break;
            case 'r':
                result.append('\r');
                break;
            case 't':
                result.append('\t');
                break;
            case '"':
            case '\\':
                result.append(ch);
                break;
            default:
                result.append(ch);
                break;
            }
            escaped = false;
            continue;
        }
        if (quote == '"' && ch == '\\' && !escaped) {
            escaped = true;
            continue;
        }
        result.append(ch);
    }
    return result;
}

QJsonValue parseScalar(const QString& raw) {
    const QString text = raw.trimmed();
    static const QRegularExpression integerPattern(QStringLiteral("^-?(?:0|[1-9]\\d*)$"));
    static const QRegularExpression floatPattern(
        QStringLiteral("^-?(?:0|[1-9]\\d*)(?:\\.\\d+)?(?:[eE][+-]?\\d+)?$"));

    if (text.isEmpty() || text == QStringLiteral("null") || text == QStringLiteral("~")) {
        return QJsonValue();
    }
    if ((text.startsWith('"') && text.endsWith('"')) || (text.startsWith('\'') && text.endsWith('\''))) {
        return unescapeQuoted(text, text.front());
    }
    if (text == QStringLiteral("true")) {
        return true;
    }
    if (text == QStringLiteral("false")) {
        return false;
    }

    bool okInt = false;
    const qlonglong intValue = text.toLongLong(&okInt);
    if (okInt && integerPattern.match(text).hasMatch()) {
        return static_cast<double>(intValue);
    }

    bool okDouble = false;
    const double doubleValue = text.toDouble(&okDouble);
    if (okDouble
        && (text.contains(QLatin1Char('.')) || text.contains(QLatin1Char('e')) || text.contains(QLatin1Char('E')))
        && floatPattern.match(text).hasMatch()) {
        return doubleValue;
    }

    return text;
}

QString stripComment(const QString& line) {
    bool inSingle = false;
    bool inDouble = false;
    bool escaped = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (escaped) {
            escaped = false;
            continue;
        }
        if (ch == '\\' && inDouble) {
            escaped = true;
            continue;
        }
        if (ch == '\'' && !inDouble) {
            inSingle = !inSingle;
            continue;
        }
        if (ch == '"' && !inSingle) {
            inDouble = !inDouble;
            continue;
        }
        if (ch == '#' && !inSingle && !inDouble) {
            return line.left(i);
        }
    }
    return line;
}

QList<ParsedLine> preprocess(const QString& text) {
    QList<ParsedLine> lines;
    const QStringList rawLines = text.split('\n');
    for (const QString& rawLine : rawLines) {
        const QString clean = stripComment(rawLine);
        if (clean.trimmed().isEmpty()) {
            continue;
        }
        int indent = 0;
        while (indent < clean.size() && clean.at(indent) == QLatin1Char(' ')) {
            ++indent;
        }
        lines.push_back({indent, clean.mid(indent)});
    }
    return lines;
}

QJsonValue parseBlock(const QList<ParsedLine>& lines, int* index, int indent, QString* error);

QJsonObject parseInlineObject(const QString& text, QString* error) {
    QJsonObject object;
    const int colon = mappingColonIndex(text);
    if (colon < 0) {
        if (error) {
            *error = QStringLiteral("Invalid inline mapping item");
        }
        return object;
    }
    const QString key = text.left(colon).trimmed();
    if (key.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Mapping key cannot be empty");
        }
        return object;
    }
    const QString value = text.mid(colon + 1).trimmed();
    object.insert(key, parseScalar(value));
    return object;
}

QJsonArray parseArray(const QList<ParsedLine>& lines, int* index, int indent, QString* error) {
    QJsonArray array;
    while (*index < lines.size()) {
        const ParsedLine& line = lines.at(*index);
        if (line.indent != indent || !line.text.startsWith(QStringLiteral("- "))) {
            break;
        }

        const QString itemText = line.text.mid(2).trimmed();
        if (itemText.isEmpty()) {
            ++(*index);
            if (*index < lines.size() && lines.at(*index).indent > indent) {
                array.append(parseBlock(lines, index, lines.at(*index).indent, error));
                if (error && !error->isEmpty()) {
                    return {};
                }
            } else {
                array.append(QJsonValue());
            }
            continue;
        }

        const int colon = mappingColonIndex(itemText);
        if (colon >= 0 && !itemText.startsWith('{')) {
            QJsonObject object = parseInlineObject(itemText, error);
            if (error && !error->isEmpty()) {
                return {};
            }
            ++(*index);
            if (*index < lines.size() && lines.at(*index).indent > indent) {
                const QJsonValue child = parseBlock(lines, index, lines.at(*index).indent, error);
                if (error && !error->isEmpty()) {
                    return {};
                }
                if (!child.isObject()) {
                    if (error) {
                        *error = QStringLiteral("Inline mapping items can only contain nested mappings");
                    }
                    return {};
                }
                const QJsonObject childObject = child.toObject();
                for (auto it = childObject.begin(); it != childObject.end(); ++it) {
                    object.insert(it.key(), *it);
                }
            }
            array.append(object);
            continue;
        }

        array.append(parseScalar(itemText));
        ++(*index);
    }
    return array;
}

QJsonObject parseObject(const QList<ParsedLine>& lines, int* index, int indent, QString* error) {
    QJsonObject object;
    while (*index < lines.size()) {
        const ParsedLine& line = lines.at(*index);
        if (line.indent != indent || line.text.startsWith(QStringLiteral("- "))) {
            break;
        }

        const int colon = mappingColonIndex(line.text);
        if (colon < 0) {
            if (error) {
                *error = QStringLiteral("Invalid mapping entry: %1").arg(line.text.trimmed());
            }
            return {};
        }

        const QString key = line.text.left(colon).trimmed();
        if (key.isEmpty()) {
            if (error) {
                *error = QStringLiteral("Mapping key cannot be empty");
            }
            return {};
        }

        const QString valueText = line.text.mid(colon + 1).trimmed();
        ++(*index);

        if (!valueText.isEmpty()) {
            object.insert(key, parseScalar(valueText));
            continue;
        }

        if (*index < lines.size()) {
            const ParsedLine& nextLine = lines.at(*index);
            if (nextLine.indent > indent) {
                object.insert(key, parseBlock(lines, index, nextLine.indent, error));
                if (error && !error->isEmpty()) {
                    return {};
                }
                continue;
            }
        }
        object.insert(key, QJsonValue());
    }
    return object;
}

QJsonValue parseBlock(const QList<ParsedLine>& lines, int* index, int indent, QString* error) {
    if (*index >= lines.size()) {
        return QJsonObject{};
    }
    if (lines.at(*index).text.startsWith(QStringLiteral("- "))) {
        return parseArray(lines, index, indent, error);
    }
    return parseObject(lines, index, indent, error);
}

}  // namespace

QJsonValue SimpleYaml::parse(const QString& text, QString* error) {
    const auto jsonDoc = QJsonDocument::fromJson(text.toUtf8());
    if (!jsonDoc.isNull()) {
        if (jsonDoc.isObject()) {
            if (error) {
                *error = QString();
            }
            return jsonDoc.object();
        }
        if (jsonDoc.isArray()) {
            if (error) {
                *error = QString();
            }
            return jsonDoc.array();
        }
    }

    const QList<ParsedLine> lines = preprocess(text);
    if (lines.isEmpty()) {
        if (error) {
            *error = QStringLiteral("YAML input is empty or contains only comments");
        }
        return QJsonObject{};
    }

    int index = 0;
    QString parseError;
    const QJsonValue value = parseBlock(lines, &index, lines.first().indent, &parseError);
    if (parseError.isEmpty() && index != lines.size()) {
        parseError = QStringLiteral("Unsupported or malformed YAML near: %1").arg(lines.at(index).text.trimmed());
    }
    if (error) {
        *error = parseError;
    }
    if (!parseError.isEmpty()) {
        return QJsonObject{};
    }
    return value;
}

QString SimpleYaml::dump(const QJsonValue& value, int indent) {
    Q_UNUSED(indent);
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Indented));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Indented));
    }
    QJsonArray tempArray;
    tempArray.append(value);
    QByteArray bytes = QJsonDocument(tempArray).toJson(QJsonDocument::Compact);
    bytes.remove(0, 1);
    bytes.chop(1);
    return QString::fromUtf8(bytes) + QLatin1Char('\n');
}
