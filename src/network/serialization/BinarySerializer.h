#ifndef BINARYSERIALIZER_H
#define BINARYSERIALIZER_H

#include "ISerializer.h"

// BinarySerializer - passthrough serializer for already-encoded payloads
// Use: provide a QByteArray as QVariant; it will be sent as-is with protobuf content-type
class BinarySerializer : public ISerializer {
public:
    SerializationFormat getFormat() const override { return SerializationFormat::PROTOBUF; }
    QString getContentType() const override { return QStringLiteral("application/x-protobuf"); }

    SerializationResult serialize(const QVariant& data) override;
    DeserializationResult deserialize(const QByteArray& data,
                                      const QString& expectedType = QString()) override;
    bool canSerialize(const QVariant& data) const override;
};

#endif // BINARYSERIALIZER_H
