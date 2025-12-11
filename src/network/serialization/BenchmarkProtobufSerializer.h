#ifndef BENCHMARKPROTOBUFSERIALIZER_H
#define BENCHMARKPROTOBUFSERIALIZER_H

// BenchmarkProtobufSerializer - Protocol Buffer binary serialization for benchmark messages
// Scope: Only handles checkmark.benchmarks.* messages (upload/public/menu)

#include "ISerializer.h"

class BenchmarkProtobufSerializer : public ISerializer {
public:
    SerializationFormat getFormat() const override { return SerializationFormat::PROTOBUF; }
    QString getContentType() const override { return QStringLiteral("application/x-protobuf"); }

    SerializationResult serialize(const QVariant& data) override;
    DeserializationResult deserialize(const QByteArray& data,
                                      const QString& expectedType = QString()) override;
    bool canSerialize(const QVariant& data) const override;
};

#endif // BENCHMARKPROTOBUFSERIALIZER_H
