#ifndef JSONSERIALIZER_H
#define JSONSERIALIZER_H

// JsonSerializer - JSON format serialization implementation
// Used by: BaseApiClient (default serializer for REST API communication)
// Purpose: Convert QVariant ↔ JSON using QJsonDocument with robust type handling
// When to use: Default choice for API communication - handles objects, arrays, primitives
// Operations: QVariant to JSON conversion, JSON parsing, type validation, error handling

#include "ISerializer.h"
#include <QJsonDocument>
#include <QJsonObject>

class JsonSerializer : public ISerializer {
public:
    SerializationFormat getFormat() const override;
    QString getContentType() const override;
    
    SerializationResult serialize(const QVariant& data) override;
    DeserializationResult deserialize(const QByteArray& data,
                                      const QString& expectedType = QString()) override;
    
    bool canSerialize(const QVariant& data) const override;

private:
    QJsonValue variantToJsonValue(const QVariant& variant) const;
    QVariant jsonValueToVariant(const QJsonValue& jsonValue) const;
};

#endif // JSONSERIALIZER_H
