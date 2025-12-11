#ifndef ISERIALIZER_H
#define ISERIALIZER_H

// ISerializer - Abstract data serialization interface
// Used by: BaseApiClient for request/response body conversion
// Purpose: Convert QVariant data to/from wire format (JSON, CSV, protobuf)
// When to use: Implement for new formats - injected into API clients for automatic conversion
// Operations: Bidirectional data transformation, content-type management, format validation

#include <QByteArray>
#include <QVariant>
#include <QString>

enum class SerializationFormat {
    JSON,
    CSV,
    PROTOBUF
};

struct SerializationResult {
    bool success = false;
    QByteArray data;
    QString error;
};

struct DeserializationResult {
    bool success = false;
    QVariant data;
    QString error;
};

class ISerializer {
public:
    virtual ~ISerializer() = default;
    
    virtual SerializationFormat getFormat() const = 0;
    virtual QString getContentType() const = 0;
    
    virtual SerializationResult serialize(const QVariant& data) = 0;
    // expectedType: optional hint for wire formats that support multiple schemas (e.g., protobuf).
    virtual DeserializationResult deserialize(const QByteArray& data,
                                              const QString& expectedType = QString()) = 0;
    
    virtual bool canSerialize(const QVariant& data) const = 0;
};

#endif // ISERIALIZER_H
