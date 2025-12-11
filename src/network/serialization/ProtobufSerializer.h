#ifndef PROTOBUFSERIALIZER_H
#define PROTOBUFSERIALIZER_H

// ProtobufSerializer - Protocol Buffer binary serialization implementation
// Used by: BaseApiClient for binary protobuf communication with server
// Purpose: Convert QVariant ↔ binary protobuf using Google Protocol Buffers
// When to use: For communication with /pb/ endpoints requiring binary protobuf format
// Operations: QVariant to protobuf conversion, binary parsing, schema validation

#include "ISerializer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <google/protobuf/message.h>
#include <memory>

class ProtobufSerializer : public ISerializer {
public:
    ProtobufSerializer();
    ~ProtobufSerializer();
    
    SerializationFormat getFormat() const override;
    QString getContentType() const override;
    
    SerializationResult serialize(const QVariant& data) override;
    DeserializationResult deserialize(const QByteArray& data,
                                      const QString& expectedType = QString()) override;
    
    bool canSerialize(const QVariant& data) const override;

private:
    // Helper methods for converting QVariant to protobuf messages
    std::unique_ptr<google::protobuf::Message> createMessageFromVariant(
        const QVariant& data, const QString& messageType) const;
    
    QVariant convertMessageToVariant(const google::protobuf::Message& message) const;
    
    // Specific message type converters
    std::unique_ptr<google::protobuf::Message> createDiagnosticSubmission(const QVariantMap& data) const;
    std::unique_ptr<google::protobuf::Message> createMenuResponse(const QVariantMap& data) const;
    std::unique_ptr<google::protobuf::Message> createComponentComparison(const QVariantMap& data) const;
    
    // Helper methods for populating protobuf messages from QVariant data
    void populateCPUData(google::protobuf::Message* cpuDataMsg, const QVariantMap& data) const;
    void populateGPUData(google::protobuf::Message* gpuDataMsg, const QVariantMap& data) const;
    void populateMemoryData(google::protobuf::Message* memoryDataMsg, const QVariantMap& data) const;
    void populateDriveData(google::protobuf::Message* driveDataMsg, const QVariantMap& data) const;
    void populateNetworkData(google::protobuf::Message* networkDataMsg, const QVariantMap& data) const;
    void populateSystemData(google::protobuf::Message* systemDataMsg, const QVariantMap& data) const;
    void populateMetadata(google::protobuf::Message* metadataMsg, const QVariantMap& data) const;
    
    // Helper methods for converting protobuf messages to QVariant
    QVariantMap convertCPUDataToVariant(const google::protobuf::Message& cpuData) const;
    QVariantMap convertGPUDataToVariant(const google::protobuf::Message& gpuData) const;
    QVariantMap convertMemoryDataToVariant(const google::protobuf::Message& memoryData) const;
    QVariantMap convertDriveCollectionToVariant(const google::protobuf::Message& driveCollection) const;
    
    // Message type detection from QVariant structure
    QString detectMessageType(const QVariantMap& data) const;
    
    // Validation helpers
    bool isValidDiagnosticSubmission(const QVariantMap& data) const;
    bool isValidMenuResponse(const QVariantMap& data) const;
    bool isValidComponentComparison(const QVariantMap& data) const;
};

#endif // PROTOBUFSERIALIZER_H
