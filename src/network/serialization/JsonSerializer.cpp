#include "JsonSerializer.h"
#include <QJsonArray>
#include <QJsonParseError>
#include <QVariantMap>
#include <QVariantList>

SerializationFormat JsonSerializer::getFormat() const {
    return SerializationFormat::JSON;
}

QString JsonSerializer::getContentType() const {
    return "application/json";
}

SerializationResult JsonSerializer::serialize(const QVariant& data) {
    SerializationResult result;
    
    try {
        QJsonValue jsonValue = variantToJsonValue(data);
        QJsonDocument doc;
        
        if (jsonValue.isObject()) {
            doc = QJsonDocument(jsonValue.toObject());
        } else if (jsonValue.isArray()) {
            doc = QJsonDocument(jsonValue.toArray());
        } else {
            result.error = "Data must be an object or array";
            return result;
        }
        
        result.data = doc.toJson(QJsonDocument::Compact);
        result.success = true;
        
    } catch (const std::exception& e) {
        result.error = QString("Serialization failed: %1").arg(e.what());
    }
    
    return result;
}

DeserializationResult JsonSerializer::deserialize(const QByteArray& data, const QString&) {
    DeserializationResult result;
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        result.error = QString("JSON parse error: %1").arg(parseError.errorString());
        return result;
    }
    
    if (doc.isObject()) {
        result.data = jsonValueToVariant(doc.object());
    } else if (doc.isArray()) {
        result.data = jsonValueToVariant(doc.array());
    } else {
        result.error = "Invalid JSON document structure";
        return result;
    }
    
    result.success = true;
    return result;
}

bool JsonSerializer::canSerialize(const QVariant& data) const {
    switch (data.type()) {
        case QVariant::Map:
        case QVariant::Hash:
        case QVariant::List:
        case QVariant::StringList:
            return true;
        default:
            return false;
    }
}

QJsonValue JsonSerializer::variantToJsonValue(const QVariant& variant) const {
    switch (variant.type()) {
        case QVariant::Bool:
            return QJsonValue(variant.toBool());
        case QVariant::Int:
        case QVariant::LongLong:
            return QJsonValue(variant.toLongLong());
        case QVariant::UInt:
        case QVariant::ULongLong:
            return QJsonValue(static_cast<qint64>(variant.toULongLong()));
        case QVariant::Double:
            return QJsonValue(variant.toDouble());
        case QVariant::String:
            return QJsonValue(variant.toString());
        case QVariant::List: {
            QJsonArray array;
            const QVariantList list = variant.toList();
            for (const QVariant& item : list) {
                array.append(variantToJsonValue(item));
            }
            return array;
        }
        case QVariant::Map: {
            QJsonObject object;
            const QVariantMap map = variant.toMap();
            for (auto it = map.begin(); it != map.end(); ++it) {
                object.insert(it.key(), variantToJsonValue(it.value()));
            }
            return object;
        }
        case QVariant::Hash: {
            QJsonObject object;
            const QVariantHash hash = variant.toHash();
            for (auto it = hash.begin(); it != hash.end(); ++it) {
                object.insert(it.key(), variantToJsonValue(it.value()));
            }
            return object;
        }
        default:
            return QJsonValue();
    }
}

QVariant JsonSerializer::jsonValueToVariant(const QJsonValue& jsonValue) const {
    switch (jsonValue.type()) {
        case QJsonValue::Bool:
            return QVariant(jsonValue.toBool());
        case QJsonValue::Double:
            return QVariant(jsonValue.toDouble());
        case QJsonValue::String:
            return QVariant(jsonValue.toString());
        case QJsonValue::Array: {
            QVariantList list;
            const QJsonArray array = jsonValue.toArray();
            for (const QJsonValue& item : array) {
                list.append(jsonValueToVariant(item));
            }
            return QVariant(list);
        }
        case QJsonValue::Object: {
            QVariantMap map;
            const QJsonObject object = jsonValue.toObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                map.insert(it.key(), jsonValueToVariant(it.value()));
            }
            return QVariant(map);
        }
        default:
            return QVariant();
    }
}
