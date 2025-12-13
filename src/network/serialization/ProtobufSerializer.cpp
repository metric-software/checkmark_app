#include "ProtobufSerializer.h"
#include "../../logging/Logger.h"
#include "diagnostic.pb.h"
#include <QVariantMap>
#include <QVariantList>
#include <QDateTime>
#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/time_util.h>
#include <google/protobuf/message.h>
#include <google/protobuf/timestamp.pb.h>  // Added for Timestamp handling
#include <google/protobuf/struct.pb.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <optional>

// Avoid Windows macro collision with google::protobuf::Reflection::GetMessage
#ifdef GetMessage
#undef GetMessage
#endif

using namespace diagnostic;

ProtobufSerializer::ProtobufSerializer() {
    LOG_WARN << "ProtobufSerializer initialized";
}

ProtobufSerializer::~ProtobufSerializer() = default;

SerializationFormat ProtobufSerializer::getFormat() const {
    return SerializationFormat::PROTOBUF;
}

QString ProtobufSerializer::getContentType() const {
    return "application/x-protobuf";
}

SerializationResult ProtobufSerializer::serialize(const QVariant& data) {
    SerializationResult result;
    
    try {
        if (!canSerialize(data)) {
            result.error = "Data structure not supported for protobuf serialization";
            return result;
        }
        
        QVariantMap dataMap = data.toMap();
        QString messageType = detectMessageType(dataMap);
        
        if (messageType.isEmpty()) {
            result.error = "Unable to determine protobuf message type from data structure";
            return result;
        }
        
        LOG_INFO << "Serializing as message type: " << messageType.toStdString();
        
        auto message = createMessageFromVariant(data, messageType);
        if (!message) {
            result.error = "Failed to create protobuf message from data";
            return result;
        }
        
        // Serialize to binary protobuf
        std::string binaryData;
        if (!message->SerializeToString(&binaryData)) {
            result.error = "Failed to serialize protobuf message to binary format";
            return result;
        }
        
        result.data = QByteArray(binaryData.data(), static_cast<int>(binaryData.size()));
        result.success = true;
        
        LOG_INFO << "Protobuf serialization successful, " << result.data.size() << " bytes";
        
    } catch (const std::exception& e) {
        result.error = QString("Protobuf serialization failed: %1").arg(e.what());
        LOG_ERROR << "Protobuf serialization exception: " << e.what();
    }
    
    return result;
}

DeserializationResult ProtobufSerializer::deserialize(const QByteArray& data,
                                                      const QString& expectedType) {
    DeserializationResult result;
    
    try {
        if (!expectedType.isEmpty()) {
            LOG_WARN << "Protobuf deserialize: expected=" << expectedType.toStdString()
                     << " bytes=" << data.size();
        }
        
        // Validate input data
        if (data.isEmpty()) {
            result.error = "Cannot deserialize empty protobuf data";
            LOG_WARN << "Received empty protobuf data for deserialization";
            return result;
        }
        
        if (data.size() > 100 * 1024 * 1024) { // 100MB limit
            result.error = "Protobuf data too large (>100MB)";
            LOG_ERROR << "Protobuf data size exceeds 100MB limit: " << data.size() << " bytes";
            return result;
        }
        
        // Try to deserialize as different message types
        // If caller provided expectedType, try that FIRST and fail fast if it doesn't parse.

        auto parseAsType = [&](const QString& typeName) -> std::optional<QVariant> {
            try {
                if (typeName == QLatin1String("DiagnosticSubmission")) {
                    DiagnosticSubmission msg;
                    if (msg.ParseFromArray(data.constData(), data.size()) && msg.IsInitialized()) {
                        LOG_WARN << "Protobuf deserializer (forced): DiagnosticSubmission";
                        return convertMessageToVariant(msg);
                    }
                } else if (typeName == QLatin1String("MenuResponse")) {
                    MenuResponse msg;
                    if (msg.ParseFromArray(data.constData(), data.size()) && msg.IsInitialized()) {
                        LOG_WARN << "Protobuf deserializer (forced): MenuResponse";
                        return convertMessageToVariant(msg);
                    }
                } else if (typeName == QLatin1String("ComponentComparison")) {
                    ComponentComparison msg;
                    if (msg.ParseFromArray(data.constData(), data.size()) && msg.IsInitialized()) {
                        LOG_WARN << "Protobuf deserializer (forced): ComponentComparison";
                        return convertMessageToVariant(msg);
                    }
                } else if (typeName == QLatin1String("Struct")) {
                    google::protobuf::Struct msg;
                    if (msg.ParseFromArray(data.constData(), data.size()) && msg.IsInitialized()) {
                        LOG_WARN << "Protobuf deserializer (forced): google.protobuf.Struct";
                        return convertMessageToVariant(msg);
                    }
                } else if (typeName == QLatin1String("UploadResponse")) {
                    UploadResponse msg;
                    if (msg.ParseFromArray(data.constData(), data.size()) && msg.IsInitialized()) {
                        LOG_WARN << "Protobuf deserializer (forced): UploadResponse";
                        return convertMessageToVariant(msg);
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN << "Exception forcing protobuf type " << typeName.toStdString() << ": " << e.what();
            }
            return std::nullopt;
        };

        if (!expectedType.isEmpty()) {
            QString normalized = expectedType;
            if (normalized.contains(QLatin1Char('.'))) {
                normalized = normalized.section(QLatin1Char('.'), -1); // strip package if present
            }
            if (auto forced = parseAsType(normalized)) {
                result.data = *forced;
                result.success = true;
                return result;
            }
            result.error = QString("Failed to parse protobuf as expected type: %1").arg(expectedType);
            LOG_ERROR << result.error.toStdString();
            return result;
        }

        // Fallback heuristic path (legacy)
        // Prioritize DiagnosticSubmission first so uploads parse correctly and aren't mistaken
        // for MenuResponse (proto3 messages parse empty/default successfully).

        // Try DiagnosticSubmission first
        {
            try {
                DiagnosticSubmission diagnosticSubmission;
                if (diagnosticSubmission.ParseFromArray(data.constData(), data.size())) {
                    if (diagnosticSubmission.IsInitialized()) {
                        LOG_INFO << "Protobuf deserializer: Parsed type DiagnosticSubmission";
                        result.data = convertMessageToVariant(diagnosticSubmission);
                        result.success = true;
                        LOG_INFO << "Successfully deserialized as DiagnosticSubmission";
                        return result;
                    } else {
                        LOG_WARN << "DiagnosticSubmission parsed but not properly initialized";
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN << "Exception parsing DiagnosticSubmission: " << e.what();
            }
        }

        // Next, try common response types

        // Try MenuResponse first
        {
            try {
                MenuResponse menuResponse;
                if (menuResponse.ParseFromArray(data.constData(), data.size())) {
                    if (menuResponse.IsInitialized()) {
                        LOG_INFO << "Protobuf deserializer: Parsed type MenuResponse";
                        result.data = convertMessageToVariant(menuResponse);
                        // Log compact JSON preview
                        try {
                            QVariantMap vm = result.data.toMap();
                            QJsonObject jo = QJsonObject::fromVariantMap(vm);
                            QByteArray preview = QJsonDocument(jo).toJson(QJsonDocument::Compact);
                            QString out = QString::fromUtf8(preview);
                            if (out.size() > 1500) out = out.left(1500) + QStringLiteral("...");
                            LOG_INFO << "MenuResponse JSON preview: " << out.toStdString();
                        } catch (...) {}
                        result.success = true;
                        LOG_INFO << "Successfully deserialized as MenuResponse";
                        return result;
                    } else {
                        LOG_WARN << "MenuResponse parsed but not properly initialized";
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN << "Exception parsing MenuResponse: " << e.what();
            }
        }
        
        // Try ComponentComparison
        {
            try {
                ComponentComparison componentComparison;
                if (componentComparison.ParseFromArray(data.constData(), data.size())) {
                    if (componentComparison.IsInitialized()) {
                        LOG_INFO << "Protobuf deserializer: Parsed type ComponentComparison";
                        // Log which oneof is set
                        try {
                            if (componentComparison.has_cpu()) {
                                LOG_INFO << "ComponentComparison contains: cpu";
                            } else if (componentComparison.has_gpu()) {
                                LOG_INFO << "ComponentComparison contains: gpu";
                            } else if (componentComparison.has_memory()) {
                                LOG_INFO << "ComponentComparison contains: memory";
                            } else if (componentComparison.has_drive()) {
                                LOG_INFO << "ComponentComparison contains: drive";
                            } else {
                                LOG_WARN << "ComponentComparison 'component' oneof not set";
                            }
                        } catch (...) {}

                        result.data = convertMessageToVariant(componentComparison);
                        // Log compact JSON preview
                        try {
                            QVariantMap vm = result.data.toMap();
                            QJsonObject jo = QJsonObject::fromVariantMap(vm);
                            QByteArray preview = QJsonDocument(jo).toJson(QJsonDocument::Compact);
                            QString out = QString::fromUtf8(preview);
                            if (out.size() > 1500) out = out.left(1500) + QStringLiteral("...");
                            LOG_INFO << "ComponentComparison JSON preview: " << out.toStdString();
                        } catch (...) {}
                        result.success = true;
                        LOG_INFO << "Successfully deserialized as ComponentComparison";
                        return result;
                    } else {
                        LOG_WARN << "ComponentComparison parsed but not properly initialized";
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN << "Exception parsing ComponentComparison: " << e.what();
            }
        }
        
        // Try UploadResponse
        {
            try {
        UploadResponse uploadResponse;
                if (uploadResponse.ParseFromArray(data.constData(), data.size())) {
                    if (uploadResponse.IsInitialized()) {
            LOG_INFO << "Protobuf deserializer: Parsed type UploadResponse";
            result.data = convertMessageToVariant(uploadResponse);
                        result.success = true;
                        LOG_INFO << "Successfully deserialized as UploadResponse";
                        return result;
                    } else {
                        LOG_WARN << "UploadResponse parsed but not properly initialized";
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN << "Exception parsing UploadResponse: " << e.what();
            }
        }
        
        // Log first few bytes for debugging
        QString hexData;
    int maxBytes = (data.size() < 32) ? data.size() : 32;
        for (int i = 0; i < maxBytes; ++i) {
            hexData += QString("%1 ").arg(static_cast<unsigned char>(data[i]), 2, 16, QChar('0'));
        }
        
        result.error = QString("Unable to parse protobuf data as any known message type (size: %1 bytes, start: %2)")
                      .arg(data.size()).arg(hexData.trimmed());
        LOG_ERROR << "Protobuf deserialization failed: " << result.error.toStdString();
        
    } catch (const std::exception& e) {
        result.error = QString("Protobuf deserialization failed: %1").arg(e.what());
        LOG_ERROR << "Protobuf deserialization exception: " << e.what();
    } catch (...) {
        result.error = "Unknown exception during protobuf deserialization";
        LOG_ERROR << "Unknown exception during protobuf deserialization";
    }
    
    return result;
}

bool ProtobufSerializer::canSerialize(const QVariant& data) const {
    if (data.type() != QVariant::Map) {
        return false;
    }
    
    QVariantMap dataMap = data.toMap();
    return isValidDiagnosticSubmission(dataMap) ||
           isValidMenuResponse(dataMap) ||
           isValidComponentComparison(dataMap);
}

std::unique_ptr<google::protobuf::Message> ProtobufSerializer::createMessageFromVariant(
    const QVariant& data, const QString& messageType) const {
    
    QVariantMap dataMap = data.toMap();
    
    if (messageType == "DiagnosticSubmission") {
        return createDiagnosticSubmission(dataMap);
    } else if (messageType == "MenuResponse") {
        return createMenuResponse(dataMap);
    } else if (messageType == "ComponentComparison") {
        return createComponentComparison(dataMap);
    }
    
    return nullptr;
}

QVariant ProtobufSerializer::convertMessageToVariant(const google::protobuf::Message& message) const {
    QVariantMap result;
    
    try {
        // Use reflection to convert protobuf message to QVariant
        const google::protobuf::Descriptor* descriptor = message.GetDescriptor();
        const google::protobuf::Reflection* reflection = message.GetReflection();
        
        if (!descriptor || !reflection) {
            LOG_ERROR << "Invalid protobuf message descriptor or reflection";
            return result;
        }
        // Keep conversion logging minimal; raw/parsed payloads are dumped by BaseApiClient.
        
        // Special handling for well-known Struct/Value types used by /pb/diagnostics/general.
        // Reflection-based conversion doesn't understand map semantics for Struct fields.
        if (descriptor->full_name() == "google.protobuf.Struct" ||
            descriptor->full_name() == "google.protobuf.Value" ||
            descriptor->full_name() == "google.protobuf.ListValue") {
            std::string jsonOut;
            google::protobuf::util::JsonPrintOptions opts;
            opts.preserve_proto_field_names = true;
            const auto status = google::protobuf::util::MessageToJsonString(message, &jsonOut, opts);
            if (status.ok()) {
                QJsonParseError err;
                const QByteArray bytes = QByteArray::fromStdString(jsonOut);
                QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
                if (err.error == QJsonParseError::NoError) {
                    return doc.toVariant();
                }
            }
        }
        
        for (int i = 0; i < descriptor->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* field = descriptor->field(i);
            if (!field) {
                LOG_WARN << "Invalid field descriptor at index " << i;
                continue;
            }
            
            const std::string& fieldName = field->name();
            
            try {
                // Handle repeated fields differently
                if (field->is_repeated()) {
                    int count = reflection->FieldSize(message, field);
                    if (count == 0) {
                        continue; // Skip empty repeated fields
                    }

                    // Map fields are encoded as repeated key/value entry messages.
                    if (field->is_map()) {
                        QVariantMap mapOut;
                        for (int j = 0; j < count; ++j) {
                            const google::protobuf::Message& entry = reflection->GetRepeatedMessage(message, field, j);
                            const google::protobuf::Descriptor* ed = entry.GetDescriptor();
                            const google::protobuf::Reflection* er = entry.GetReflection();
                            if (!ed || !er) continue;

                            const auto* keyField = ed->FindFieldByName("key");
                            const auto* valueField = ed->FindFieldByName("value");
                            if (!keyField || !valueField) continue;

                            QString key;
                            if (keyField->type() == google::protobuf::FieldDescriptor::TYPE_STRING) {
                                key = QString::fromStdString(er->GetString(entry, keyField));
                            } else if (keyField->type() == google::protobuf::FieldDescriptor::TYPE_INT32) {
                                key = QString::number(er->GetInt32(entry, keyField));
                            } else {
                                continue;
                            }

                            QVariant value;
                            switch (valueField->type()) {
                                case google::protobuf::FieldDescriptor::TYPE_STRING:
                                    value = QString::fromStdString(er->GetString(entry, valueField));
                                    break;
                                case google::protobuf::FieldDescriptor::TYPE_INT32:
                                    value = er->GetInt32(entry, valueField);
                                    break;
                                case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
                                    value = er->GetDouble(entry, valueField);
                                    break;
                                case google::protobuf::FieldDescriptor::TYPE_BOOL:
                                    value = er->GetBool(entry, valueField);
                                    break;
                                case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
                                    value = convertMessageToVariant(er->GetMessage(entry, valueField));
                                    break;
                                default:
                                    continue;
                            }

                            mapOut.insert(key, value);
                        }

                        if (!mapOut.isEmpty()) {
                            result[QString::fromStdString(fieldName)] = mapOut;
                        }
                        continue;
                    }
                    
                    QVariantList list;
                    for (int j = 0; j < count; ++j) {
                        try {
                            QVariant itemValue;
                            
                            switch (field->type()) {
                                case google::protobuf::FieldDescriptor::TYPE_STRING:
                                    itemValue = QString::fromStdString(reflection->GetRepeatedString(message, field, j));
                                    break;
                                case google::protobuf::FieldDescriptor::TYPE_BYTES: {
                                    const std::string& bytes = reflection->GetRepeatedString(message, field, j);
                                    itemValue = QByteArray(bytes.data(), static_cast<int>(bytes.size()));
                                } break;
                                case google::protobuf::FieldDescriptor::TYPE_INT32:
                                    itemValue = reflection->GetRepeatedInt32(message, field, j);
                                    break;
                                case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
                                    itemValue = reflection->GetRepeatedDouble(message, field, j);
                                    break;
                                case google::protobuf::FieldDescriptor::TYPE_BOOL:
                                    itemValue = reflection->GetRepeatedBool(message, field, j);
                                    break;
                                case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
                                    {
                                        const google::protobuf::Message& subMessage = reflection->GetRepeatedMessage(message, field, j);
                                        itemValue = convertMessageToVariant(subMessage);
                                    }
                                    break;
                                default:
                                    LOG_WARN << "Unsupported repeated field type: " << field->type();
                                    continue;
                            }
                            
                            list.append(itemValue);
                        } catch (const std::exception& e) {
                            LOG_WARN << "Exception converting repeated field " << fieldName << "[" << j << "]: " << e.what();
                        }
                    }
                    
                    if (!list.isEmpty()) {
                        result[QString::fromStdString(fieldName)] = list;
                    }
                } else {
                    // Singular field
                    // Only log if set
                    // Handle singular fields
                    if (!reflection->HasField(message, field)) {
                        continue;
                    }
                    
                    QVariant fieldValue;
                    
                    switch (field->type()) {
                        case google::protobuf::FieldDescriptor::TYPE_STRING:
                            fieldValue = QString::fromStdString(reflection->GetString(message, field));
                            break;
                        case google::protobuf::FieldDescriptor::TYPE_BYTES: {
                            const std::string& bytes = reflection->GetString(message, field);
                            fieldValue = QByteArray(bytes.data(), static_cast<int>(bytes.size()));
                        } break;
                        case google::protobuf::FieldDescriptor::TYPE_INT32:
                            fieldValue = reflection->GetInt32(message, field);
                            break;
                        case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
                            {
                                double value = reflection->GetDouble(message, field);
                                if (std::isnan(value) || std::isinf(value)) {
                                    LOG_WARN << "Invalid double value in field " << fieldName << ": " << value;
                                    fieldValue = 0.0; // Use safe default
                                } else {
                                    fieldValue = value;
                                }
                            }
                            break;
                        case google::protobuf::FieldDescriptor::TYPE_BOOL:
                            fieldValue = reflection->GetBool(message, field);
                            break;
                        case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
                            {
                                const google::protobuf::Message& subMessage = reflection->GetMessage(message, field);
                                fieldValue = convertMessageToVariant(subMessage);
                            }
                            break;
                        default:
                            LOG_WARN << "Unsupported protobuf field type: " << field->type() << " for field " << fieldName;
                            continue;
                    }
                    
                    result[QString::fromStdString(fieldName)] = fieldValue;
                }
            } catch (const std::exception& e) {
                LOG_WARN << "Exception converting field " << fieldName << ": " << e.what();
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR << "Exception during message conversion: " << e.what();
    }
    
    return result;
}

std::unique_ptr<google::protobuf::Message> ProtobufSerializer::createDiagnosticSubmission(const QVariantMap& data) const {
    auto submission = std::make_unique<DiagnosticSubmission>();
    
    // Populate CPU data
    if (data.contains("cpu")) {
        QVariantMap cpuMap = data["cpu"].toMap();
        CPUData* cpuData = submission->mutable_cpu();
        populateCPUData(cpuData, cpuMap);
    }
    
    // Populate GPU data
    if (data.contains("gpu")) {
        QVariantMap gpuMap = data["gpu"].toMap();
        GPUData* gpuData = submission->mutable_gpu();
        populateGPUData(gpuData, gpuMap);
    }
    
    // Populate Memory data
    if (data.contains("memory")) {
        QVariantMap memoryMap = data["memory"].toMap();
        MemoryData* memoryData = submission->mutable_memory();
        populateMemoryData(memoryData, memoryMap);
    }
    
    // Populate Drive data (updated to use DriveData instead of DriveCollection)
    if (data.contains("drives")) {
        QVariantMap drivesMap = data["drives"].toMap();
        DriveData* driveData = submission->mutable_drives();
        populateDriveData(driveData, drivesMap);
    }
    
    // Populate Network data
    if (data.contains("network")) {
        QVariantMap networkMap = data["network"].toMap();
        NetworkData* networkData = submission->mutable_network();
        populateNetworkData(networkData, networkMap);
    }
    
    // Populate System data
    if (data.contains("system")) {
        QVariantMap systemMap = data["system"].toMap();
        SystemData* systemData = submission->mutable_system();
        populateSystemData(systemData, systemMap);
    }
    
    // Populate Metadata
    if (data.contains("metadata")) {
        QVariantMap metadataMap = data["metadata"].toMap();
        MetadataInfo* metadata = submission->mutable_metadata();
        populateMetadata(metadata, metadataMap);
    }

    // Additional artifacts: optimization settings JSON and PDH metrics CSV
    if (data.contains("optimization_settings_json")) {
        submission->set_optimization_settings_json(data.value("optimization_settings_json").toString().toStdString());
    }
    if (data.contains("pdh_metrics_csv")) {
        QByteArray bytes = data.value("pdh_metrics_csv").toByteArray();
        if (!bytes.isEmpty()) {
            submission->set_pdh_metrics_csv(std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
        }
    }
    if (data.contains("pdh_metrics_filename")) {
        submission->set_pdh_metrics_filename(data.value("pdh_metrics_filename").toString().toStdString());
    }
    
    return std::unique_ptr<google::protobuf::Message>(submission.release());
}

std::unique_ptr<google::protobuf::Message> ProtobufSerializer::createMenuResponse(const QVariantMap& data) const {
    auto menu = std::make_unique<MenuResponse>();
    
    // Available CPUs
    if (data.contains("available_cpus") || data.contains("availableCpus")) {
        QVariantList cpuList = data.contains("available_cpus") ? 
            data["available_cpus"].toList() : data["availableCpus"].toList();
        for (const QVariant& cpu : cpuList) {
            menu->add_available_cpus(cpu.toString().toStdString());
        }
    }
    
    // Available GPUs
    if (data.contains("available_gpus") || data.contains("availableGpus")) {
        QVariantList gpuList = data.contains("available_gpus") ? 
            data["available_gpus"].toList() : data["availableGpus"].toList();
        for (const QVariant& gpu : gpuList) {
            menu->add_available_gpus(gpu.toString().toStdString());
        }
    }
    
    // Available Memory
    if (data.contains("available_memory") || data.contains("availableMemory")) {
        QVariantList memoryList = data.contains("available_memory") ? 
            data["available_memory"].toList() : data["availableMemory"].toList();
        for (const QVariant& memory : memoryList) {
            menu->add_available_memory(memory.toString().toStdString());
        }
    }
    
    // Available Drives
    if (data.contains("available_drives") || data.contains("availableDrives")) {
        QVariantList driveList = data.contains("available_drives") ? 
            data["available_drives"].toList() : data["availableDrives"].toList();
        for (const QVariant& drive : driveList) {
            menu->add_available_drives(drive.toString().toStdString());
        }
    }
    
    // Endpoints
    if (data.contains("endpoints")) {
        QVariantMap endpoints = data["endpoints"].toMap();
        auto* endpointMap = menu->mutable_endpoints();
        for (auto it = endpoints.begin(); it != endpoints.end(); ++it) {
            (*endpointMap)[it.key().toStdString()] = it.value().toString().toStdString();
        }
    }
    
    return std::unique_ptr<google::protobuf::Message>(menu.release());
}

std::unique_ptr<google::protobuf::Message> ProtobufSerializer::createComponentComparison(const QVariantMap& data) const {
    auto comparison = std::make_unique<ComponentComparison>();
    
    // Determine component type and populate accordingly
    if (data.contains("cpu")) {
        // Handle CPU comparison
        QVariantMap cpuMap = data["cpu"].toMap();
        CPUComparison* cpuComparison = comparison->mutable_cpu();
        
        if (cpuMap.contains("model")) {
            cpuComparison->set_model(cpuMap["model"].toString().toStdString());
        }
        if (cpuMap.contains("date")) {
            cpuComparison->set_date(cpuMap["date"].toString().toStdString());
        }
        // Add more CPU comparison fields as needed...
    }
    // Handle other component types similarly...
    
    return std::unique_ptr<google::protobuf::Message>(comparison.release());
}

void ProtobufSerializer::populateCPUData(google::protobuf::Message* cpuDataMsg, const QVariantMap& data) const {
    CPUData* cpuData = dynamic_cast<CPUData*>(cpuDataMsg);
    if (!cpuData) return;
    
    // Removed set_tested() as CPUData no longer has a 'tested' field in the proto
    
    // Populate CPU info
    if (data.contains("info")) {
        QVariantMap infoMap = data["info"].toMap();
        CPUInfo* info = cpuData->mutable_info();
        
        if (infoMap.contains("model")) {
            info->set_model(infoMap["model"].toString().toStdString());
        }
        if (infoMap.contains("architecture")) {
            info->set_architecture(infoMap["architecture"].toString().toStdString());
        }
        if (infoMap.contains("cores")) {
            info->set_cores(infoMap["cores"].toInt());
        }
        if (infoMap.contains("threads")) {
            info->set_threads(infoMap["threads"].toInt());
        }
        if (infoMap.contains("base_clock_mhz")) {
            info->set_base_clock_mhz(infoMap["base_clock_mhz"].toInt());
        }
        if (infoMap.contains("max_clock_mhz")) {
            info->set_max_clock_mhz(infoMap["max_clock_mhz"].toInt());
        }
        if (infoMap.contains("smt")) {
            info->set_smt(infoMap["smt"].toString().toStdString());
        }
        if (infoMap.contains("socket")) {
            info->set_socket(infoMap["socket"].toString().toStdString());
        }
        if (infoMap.contains("vendor")) {
            info->set_vendor(infoMap["vendor"].toString().toStdString());
        }
        if (infoMap.contains("virtualization")) {
            info->set_virtualization(infoMap["virtualization"].toString().toStdString());
        }

        if (infoMap.contains("boost_summary")) {
            QVariantMap boostMap = infoMap["boost_summary"].toMap();
            CPUBoostSummary* boost = info->mutable_boost_summary();
            if (boostMap.contains("all_core_power_w")) boost->set_all_core_power_w(boostMap["all_core_power_w"].toDouble());
            if (boostMap.contains("best_boosting_core")) boost->set_best_boosting_core(boostMap["best_boosting_core"].toInt());
            if (boostMap.contains("idle_power_w")) boost->set_idle_power_w(boostMap["idle_power_w"].toDouble());
            if (boostMap.contains("max_boost_delta_mhz")) boost->set_max_boost_delta_mhz(boostMap["max_boost_delta_mhz"].toInt());
            if (boostMap.contains("single_core_power_w")) boost->set_single_core_power_w(boostMap["single_core_power_w"].toDouble());
        }

        if (infoMap.contains("cache_info")) {
            QVariantMap cacheMap = infoMap["cache_info"].toMap();
            CPUCacheInfo* cache = info->mutable_cache_info();
            if (cacheMap.contains("l1_kb")) cache->set_l1_kb(cacheMap["l1_kb"].toInt());
            if (cacheMap.contains("l2_kb")) cache->set_l2_kb(cacheMap["l2_kb"].toInt());
            if (cacheMap.contains("l3_kb")) cache->set_l3_kb(cacheMap["l3_kb"].toInt());
        }

        if (infoMap.contains("cold_start")) {
            QVariantMap coldMap = infoMap["cold_start"].toMap();
            CPUColdStart* cold = info->mutable_cold_start();
            if (coldMap.contains("avg_response_time_us")) cold->set_avg_response_time_us(coldMap["avg_response_time_us"].toDouble());
            if (coldMap.contains("max_response_time_us")) cold->set_max_response_time_us(coldMap["max_response_time_us"].toDouble());
            if (coldMap.contains("min_response_time_us")) cold->set_min_response_time_us(coldMap["min_response_time_us"].toDouble());
            if (coldMap.contains("std_dev_us")) cold->set_std_dev_us(coldMap["std_dev_us"].toDouble());
            if (coldMap.contains("variance_us")) cold->set_variance_us(coldMap["variance_us"].toDouble());
        }

        if (infoMap.contains("cores_detail")) {
            QVariantList coresList = infoMap["cores_detail"].toList();
            for (const QVariant& c : coresList) {
                QVariantMap cMap = c.toMap();
                CPUCoreDetail* core = info->add_cores_detail();
                if (cMap.contains("clock_mhz")) core->set_clock_mhz(cMap["clock_mhz"].toInt());
                if (cMap.contains("core_number")) core->set_core_number(cMap["core_number"].toInt());
                if (cMap.contains("load_percent")) core->set_load_percent(cMap["load_percent"].toInt());
                if (cMap.contains("boost_metrics")) {
                    QVariantMap bm = cMap["boost_metrics"].toMap();
                    CPUBoostMetrics* bmProto = core->mutable_boost_metrics();
                    if (bm.contains("all_core_clock_mhz")) bmProto->set_all_core_clock_mhz(bm["all_core_clock_mhz"].toInt());
                    if (bm.contains("boost_delta_mhz")) bmProto->set_boost_delta_mhz(bm["boost_delta_mhz"].toInt());
                    if (bm.contains("idle_clock_mhz")) bmProto->set_idle_clock_mhz(bm["idle_clock_mhz"].toInt());
                    if (bm.contains("single_load_clock_mhz")) bmProto->set_single_load_clock_mhz(bm["single_load_clock_mhz"].toInt());
                }
            }
        }

        if (infoMap.contains("throttling")) {
            QVariantMap tMap = infoMap["throttling"].toMap();
            CPUThrottling* t = info->mutable_throttling();
            if (tMap.contains("clock_drop_percent")) t->set_clock_drop_percent(tMap["clock_drop_percent"].toInt());
            if (tMap.contains("detected")) t->set_detected(tMap["detected"].toBool());
            if (tMap.contains("detected_time_seconds")) t->set_detected_time_seconds(tMap["detected_time_seconds"].toInt());
            if (tMap.contains("peak_clock")) t->set_peak_clock(tMap["peak_clock"].toInt());
            if (tMap.contains("sustained_clock")) t->set_sustained_clock(tMap["sustained_clock"].toInt());
        }
    }
    
    // Populate CPU results
    if (data.contains("results")) {
        QVariantMap resultsMap = data["results"].toMap();
        CPUResults* results = cpuData->mutable_results();
        
        if (resultsMap.contains("single_core")) {
            results->set_single_core(resultsMap["single_core"].toDouble());
        }
        if (resultsMap.contains("four_thread")) {
            results->set_four_thread(resultsMap["four_thread"].toDouble());
        }
        if (resultsMap.contains("avx")) {
            results->set_avx(resultsMap["avx"].toDouble());
        }
        if (resultsMap.contains("game_sim_large")) results->set_game_sim_large(resultsMap["game_sim_large"].toDouble());
        if (resultsMap.contains("game_sim_medium")) results->set_game_sim_medium(resultsMap["game_sim_medium"].toDouble());
        if (resultsMap.contains("game_sim_small")) results->set_game_sim_small(resultsMap["game_sim_small"].toDouble());
        if (resultsMap.contains("multi_core")) results->set_multi_core(resultsMap["multi_core"].toDouble());
        if (resultsMap.contains("prime_time")) results->set_prime_time(resultsMap["prime_time"].toDouble());
        if (resultsMap.contains("simd_scalar")) results->set_simd_scalar(resultsMap["simd_scalar"].toDouble());
        if (resultsMap.contains("raw_cache_latencies")) {
            QVariantList latList = resultsMap["raw_cache_latencies"].toList();
            for (const QVariant& lv : latList) {
                QVariantMap lm = lv.toMap();
                CacheLatency* cl = results->add_raw_cache_latencies();
                if (lm.contains("latency")) cl->set_latency(lm["latency"].toDouble());
                if (lm.contains("size_kb")) cl->set_size_kb(lm["size_kb"].toInt());
            }
        }
        if (resultsMap.contains("specific_cache_latencies")) {
            QVariantMap scl = resultsMap["specific_cache_latencies"].toMap();
            CPUSpecificCacheLatencies* sc = results->mutable_specific_cache_latencies();
            if (scl.contains("l1_ns")) sc->set_l1_ns(scl["l1_ns"].toDouble());
            if (scl.contains("l2_ns")) sc->set_l2_ns(scl["l2_ns"].toDouble());
            if (scl.contains("l3_ns")) sc->set_l3_ns(scl["l3_ns"].toDouble());
            if (scl.contains("ram_ns")) sc->set_ram_ns(scl["ram_ns"].toDouble());
        }
    }
}

void ProtobufSerializer::populateGPUData(google::protobuf::Message* gpuDataMsg, const QVariantMap& data) const {
    GPUData* gpuData = dynamic_cast<GPUData*>(gpuDataMsg);
    if (!gpuData) return;
    
    if (data.contains("tested")) {
        gpuData->set_tested(data["tested"].toBool());
    }
    
    // Populate GPU info
    if (data.contains("info")) {
        QVariantMap infoMap = data["info"].toMap();
        GPUInfo* info = gpuData->mutable_info();
        
        if (infoMap.contains("model")) {
            info->set_model(infoMap["model"].toString().toStdString());
        }
        if (infoMap.contains("driver")) {
            info->set_driver(infoMap["driver"].toString().toStdString());
        }
        if (infoMap.contains("memory_mb")) {
            info->set_memory_mb(infoMap["memory_mb"].toInt());
        }
        // devices
        if (infoMap.contains("devices")) {
            QVariantList devs = infoMap["devices"].toList();
            for (const QVariant& dv : devs) {
                QVariantMap dmap = dv.toMap();
                GPUDevice* dev = info->add_devices();
                if (dmap.contains("device_id")) dev->set_device_id(dmap["device_id"].toString().toStdString());
                if (dmap.contains("driver_date")) dev->set_driver_date(dmap["driver_date"].toString().toStdString());
                if (dmap.contains("driver_version")) dev->set_driver_version(dmap["driver_version"].toString().toStdString());
                if (dmap.contains("has_geforce_experience")) dev->set_has_geforce_experience(dmap["has_geforce_experience"].toBool());
                if (dmap.contains("is_primary")) dev->set_is_primary(dmap["is_primary"].toBool());
                if (dmap.contains("memory_mb")) dev->set_memory_mb(dmap["memory_mb"].toInt());
                if (dmap.contains("name")) dev->set_name(dmap["name"].toString().toStdString());
                if (dmap.contains("pci_link_width")) dev->set_pci_link_width(dmap["pci_link_width"].toInt());
                if (dmap.contains("pcie_link_gen")) dev->set_pcie_link_gen(dmap["pcie_link_gen"].toInt());
                if (dmap.contains("vendor")) dev->set_vendor(dmap["vendor"].toString().toStdString());
            }
        }
    }
    
    // Populate GPU results
    if (data.contains("results")) {
        QVariantMap resultsMap = data["results"].toMap();
        GPUResults* results = gpuData->mutable_results();
        
        if (resultsMap.contains("fps")) {
            results->set_fps(resultsMap["fps"].toDouble());
        }
        if (resultsMap.contains("frames")) {
            results->set_frames(resultsMap["frames"].toInt());
        }
    if (resultsMap.contains("render_time_ms")) results->set_render_time_ms(resultsMap["render_time_ms"].toDouble());
    }
}

void ProtobufSerializer::populateMemoryData(google::protobuf::Message* memoryDataMsg, const QVariantMap& data) const {
    MemoryData* memoryData = dynamic_cast<MemoryData*>(memoryDataMsg);
    if (!memoryData) return;

    if (data.contains("info")) {
        QVariantMap infoMap = data["info"].toMap();
        MemoryInfo* info = memoryData->mutable_info();
        if (infoMap.contains("available_memory_gb")) info->set_available_memory_gb(infoMap["available_memory_gb"].toDouble());
        if (infoMap.contains("channel_status")) info->set_channel_status(infoMap["channel_status"].toString().toStdString());
        if (infoMap.contains("clock_speed_mhz")) info->set_clock_speed_mhz(infoMap["clock_speed_mhz"].toInt());
        if (infoMap.contains("total_memory_gb")) info->set_total_memory_gb(infoMap["total_memory_gb"].toDouble());
        if (infoMap.contains("type")) info->set_type(infoMap["type"].toString().toStdString());
        if (infoMap.contains("xmp_enabled")) info->set_xmp_enabled(infoMap["xmp_enabled"].toBool());

        if (infoMap.contains("modules")) {
            QVariantList modules = infoMap["modules"].toList();
            for (const QVariant& m : modules) {
                QVariantMap mm = m.toMap();
                MemoryModule* mod = info->add_modules();
                if (mm.contains("capacity_gb")) mod->set_capacity_gb(mm["capacity_gb"].toDouble());
                if (mm.contains("configured_clock_speed_mhz")) mod->set_configured_clock_speed_mhz(mm["configured_clock_speed_mhz"].toInt());
                if (mm.contains("device_locator")) mod->set_device_locator(mm["device_locator"].toString().toStdString());
                if (mm.contains("manufacturer")) mod->set_manufacturer(mm["manufacturer"].toString().toStdString());
                if (mm.contains("memory_type")) mod->set_memory_type(mm["memory_type"].toString().toStdString());
                if (mm.contains("part_number")) mod->set_part_number(mm["part_number"].toString().toStdString());
                if (mm.contains("slot")) mod->set_slot(mm["slot"].toInt());
                if (mm.contains("speed_mhz")) mod->set_speed_mhz(mm["speed_mhz"].toInt());
                if (mm.contains("xmp_status")) mod->set_xmp_status(mm["xmp_status"].toString().toStdString());
            }
        }

        if (infoMap.contains("page_file")) {
            QVariantMap pf = infoMap["page_file"].toMap();
            MemoryPageFile* page = info->mutable_page_file();
            if (pf.contains("exists")) page->set_exists(pf["exists"].toBool());
            if (pf.contains("primary_drive")) page->set_primary_drive(pf["primary_drive"].toString().toStdString());
            if (pf.contains("system_managed")) page->set_system_managed(pf["system_managed"].toBool());
            if (pf.contains("total_size_mb")) page->set_total_size_mb(pf["total_size_mb"].toInt());
            if (pf.contains("locations")) {
                QVariantList locs = pf["locations"].toList();
                for (const QVariant& l : locs) {
                    QVariantMap lm = l.toMap();
                    MemoryPageFileLocation* loc = page->add_locations();
                    if (lm.contains("path")) loc->set_path(lm["path"].toString().toStdString());
                }
            }
        }
    }

    if (data.contains("results")) {
        QVariantMap r = data["results"].toMap();
        MemoryResults* results = memoryData->mutable_results();
        if (r.contains("bandwidth")) results->set_bandwidth(r["bandwidth"].toDouble());
        if (r.contains("latency")) results->set_latency(r["latency"].toDouble());
        if (r.contains("read_time")) results->set_read_time(r["read_time"].toDouble());
        if (r.contains("write_time")) results->set_write_time(r["write_time"].toDouble());
        if (r.contains("stability_test")) {
            QVariantMap st = r["stability_test"].toMap();
            MemoryStabilityTest* mt = results->mutable_stability_test();
            if (st.contains("completed_loops")) mt->set_completed_loops(st["completed_loops"].toInt());
            if (st.contains("completed_patterns")) mt->set_completed_patterns(st["completed_patterns"].toInt());
            if (st.contains("error_count")) mt->set_error_count(st["error_count"].toInt());
            if (st.contains("passed")) mt->set_passed(st["passed"].toBool());
            if (st.contains("test_performed")) mt->set_test_performed(st["test_performed"].toBool());
            if (st.contains("tested_size_mb")) mt->set_tested_size_mb(st["tested_size_mb"].toInt());
        }
    }
}

void ProtobufSerializer::populateDriveData(google::protobuf::Message* driveDataMsg, const QVariantMap& data) const {
    DriveData* driveData = dynamic_cast<DriveData*>(driveDataMsg);
    if (!driveData) return;
    
    if (data.contains("tested")) {
        driveData->set_tested(data["tested"].toBool());
    }
    
    if (data.contains("items")) {
        QVariantList items = data["items"].toList();
        for (const QVariant& item : items) {
            QVariantMap itemMap = item.toMap();
            DriveItem* driveItem = driveData->add_items();
            
            if (itemMap.contains("info")) {
                QVariantMap infoMap = itemMap["info"].toMap();
                DriveInfo* info = driveItem->mutable_info();
                
                if (infoMap.contains("model")) {
                    info->set_model(infoMap["model"].toString().toStdString());
                }
                if (infoMap.contains("path")) {
                    info->set_path(infoMap["path"].toString().toStdString());
                }
                if (infoMap.contains("free_space_gb")) info->set_free_space_gb(infoMap["free_space_gb"].toInt());
                if (infoMap.contains("interface_type")) info->set_interface_type(infoMap["interface_type"].toString().toStdString());
                if (infoMap.contains("is_ssd")) info->set_is_ssd(infoMap["is_ssd"].toBool());
                if (infoMap.contains("is_system_drive")) info->set_is_system_drive(infoMap["is_system_drive"].toBool());
                if (infoMap.contains("size_gb")) info->set_size_gb(infoMap["size_gb"].toInt());
                // Add more drive info fields...
            }
            
            if (itemMap.contains("results")) {
                QVariantMap resultsMap = itemMap["results"].toMap();
                DriveResults* results = driveItem->mutable_results();
                
                if (resultsMap.contains("read_speed")) {
                    results->set_read_speed(resultsMap["read_speed"].toDouble());
                }
                if (resultsMap.contains("write_speed")) {
                    results->set_write_speed(resultsMap["write_speed"].toDouble());
                }
                if (resultsMap.contains("access_time")) results->set_access_time(resultsMap["access_time"].toDouble());
                if (resultsMap.contains("iops_4k")) results->set_iops_4k(resultsMap["iops_4k"].toDouble());
                // Add more drive result fields...
            }
        }
    }
}

void ProtobufSerializer::populateMetadata(google::protobuf::Message* metadataMsg, const QVariantMap& data) const {
    MetadataInfo* metadata = dynamic_cast<MetadataInfo*>(metadataMsg);
    if (!metadata) return;
    
    if (data.contains("user_id")) {
        metadata->set_user_id(data["user_id"].toString().toStdString());
    }
    if (data.contains("version")) {
        metadata->set_version(data["version"].toString().toStdString());
    }
    if (data.contains("timestamp")) {
        // Handle timestamp as google.protobuf.Timestamp
        google::protobuf::Timestamp* ts = metadata->mutable_timestamp();
        // Try protobuf util first, fall back to QDateTime parsing
        std::string tsStr = data["timestamp"].toString().toStdString();
        bool parsed = false;
        if (!tsStr.empty()) {
            bool status = google::protobuf::util::TimeUtil::FromString(tsStr, ts);
            if (status) {
                parsed = true;
            } else {
                // Fallback: QDateTime (accepts many formats)
                QDateTime dt = QDateTime::fromString(QString::fromStdString(tsStr), Qt::ISODate);
                if (!dt.isValid()) dt = QDateTime::fromString(QString::fromStdString(tsStr), Qt::ISODateWithMs);
                if (!dt.isValid()) dt = QDateTime::fromString(QString::fromStdString(tsStr));
                if (dt.isValid()) {
                    int64_t secs = dt.toSecsSinceEpoch();
                    ts->set_seconds(secs);
                    ts->set_nanos(dt.time().msec() * 1000000);
                    parsed = true;
                }
            }
        }
        if (!parsed) {
            // leave timestamp unset (avoid leaving default zero if input was present but unparseable)
            metadata->clear_timestamp();
        }
    }
    if (data.contains("combined_identifier")) {
        metadata->set_combined_identifier(data["combined_identifier"].toString().toStdString());
    }
    if (data.contains("profile_last_updated")) {
        metadata->set_profile_last_updated(data["profile_last_updated"].toString().toStdString());
    }
    if (data.contains("run_as_admin")) {
        metadata->set_run_as_admin(data["run_as_admin"].toBool());
    }
    if (data.contains("system_hash")) {
        metadata->set_system_hash(data["system_hash"].toString().toStdString());
    }
    if (data.contains("system_id")) {
        QVariantMap systemIdMap = data["system_id"].toMap();
        auto* systemId = metadata->mutable_system_id();
        if (systemIdMap.contains("fingerprint")) systemId->set_fingerprint(systemIdMap["fingerprint"].toString().toStdString());
        if (systemIdMap.contains("motherboard")) systemId->set_motherboard(systemIdMap["motherboard"].toString().toStdString());
        if (systemIdMap.contains("cpu")) systemId->set_cpu(systemIdMap["cpu"].toString().toStdString());
        if (systemIdMap.contains("gpu")) systemId->set_gpu(systemIdMap["gpu"].toString().toStdString());
    }
}

void ProtobufSerializer::populateNetworkData(google::protobuf::Message* networkDataMsg, const QVariantMap& data) const {
    NetworkData* networkData = dynamic_cast<NetworkData*>(networkDataMsg);
    if (!networkData) return;
    
    if (data.contains("tested")) {
        networkData->set_tested(data["tested"].toBool());
    }
    
    if (data.contains("results")) {
        QVariantMap resultsMap = data["results"].toMap();
        NetworkResults* results = networkData->mutable_results();
        
        if (resultsMap.contains("average_jitter_ms")) results->set_average_jitter_ms(resultsMap["average_jitter_ms"].toDouble());
        if (resultsMap.contains("average_latency_ms")) results->set_average_latency_ms(resultsMap["average_latency_ms"].toDouble());
        if (resultsMap.contains("baseline_latency_ms")) results->set_baseline_latency_ms(resultsMap["baseline_latency_ms"].toDouble());
        if (resultsMap.contains("download_latency_ms")) results->set_download_latency_ms(resultsMap["download_latency_ms"].toDouble());
        if (resultsMap.contains("has_bufferbloat")) results->set_has_bufferbloat(resultsMap["has_bufferbloat"].toBool());
        if (resultsMap.contains("issues")) results->set_issues(resultsMap["issues"].toString().toStdString());
        if (resultsMap.contains("packet_loss_percent")) results->set_packet_loss_percent(resultsMap["packet_loss_percent"].toDouble());
        if (resultsMap.contains("upload_latency_ms")) results->set_upload_latency_ms(resultsMap["upload_latency_ms"].toDouble());
        
        if (resultsMap.contains("regional_latencies")) {
            QVariantList regionalList = resultsMap["regional_latencies"].toList();
            for (const QVariant& regVariant : regionalList) {
                QVariantMap regMap = regVariant.toMap();
                RegionalLatency* regional = results->add_regional_latencies();
                if (regMap.contains("latency_ms")) regional->set_latency_ms(regMap["latency_ms"].toDouble());
                if (regMap.contains("region")) regional->set_region(regMap["region"].toString().toStdString());
            }
        }
        
        if (resultsMap.contains("server_results")) {
            QVariantList serverList = resultsMap["server_results"].toList();
            for (const QVariant& serverVariant : serverList) {
                QVariantMap serverMap = serverVariant.toMap();
                ServerResult* server = results->add_server_results();
                if (serverMap.contains("avg_latency_ms")) server->set_avg_latency_ms(serverMap["avg_latency_ms"].toDouble());
                if (serverMap.contains("hostname")) server->set_hostname(serverMap["hostname"].toString().toStdString());
                if (serverMap.contains("ip_address")) server->set_ip_address(serverMap["ip_address"].toString().toStdString());
                if (serverMap.contains("jitter_ms")) server->set_jitter_ms(serverMap["jitter_ms"].toDouble());
                if (serverMap.contains("max_latency_ms")) server->set_max_latency_ms(serverMap["max_latency_ms"].toDouble());
                if (serverMap.contains("min_latency_ms")) server->set_min_latency_ms(serverMap["min_latency_ms"].toDouble());
                if (serverMap.contains("packet_loss_percent")) server->set_packet_loss_percent(serverMap["packet_loss_percent"].toDouble());
                if (serverMap.contains("received_packets")) server->set_received_packets(serverMap["received_packets"].toInt());
                if (serverMap.contains("region")) server->set_region(serverMap["region"].toString().toStdString());
                if (serverMap.contains("sent_packets")) server->set_sent_packets(serverMap["sent_packets"].toInt());
            }
        }
    }
}

void ProtobufSerializer::populateSystemData(google::protobuf::Message* systemDataMsg, const QVariantMap& data) const {
    SystemData* systemData = dynamic_cast<SystemData*>(systemDataMsg);
    if (!systemData) return;
    
    if (data.contains("info")) {
        QVariantMap infoMap = data["info"].toMap();
        SystemInfo* info = systemData->mutable_info();
        
        if (infoMap.contains("audio_drivers")) {
            QVariantList audioList = infoMap["audio_drivers"].toList();
            for (const QVariant& audioVariant : audioList) {
                QVariantMap audioMap = audioVariant.toMap();
                DriverInfo* audio = info->add_audio_drivers();
                if (audioMap.contains("device_name")) audio->set_device_name(audioMap["device_name"].toString().toStdString());
                if (audioMap.contains("driver_date")) audio->set_driver_date(audioMap["driver_date"].toString().toStdString());
                if (audioMap.contains("driver_version")) audio->set_driver_version(audioMap["driver_version"].toString().toStdString());
                if (audioMap.contains("is_date_valid")) audio->set_is_date_valid(audioMap["is_date_valid"].toBool());
                if (audioMap.contains("provider_name")) audio->set_provider_name(audioMap["provider_name"].toString().toStdString());
            }
        }
        
        if (infoMap.contains("background")) {
            QVariantMap bgMap = infoMap["background"].toMap();
            BackgroundActivity* bg = info->mutable_background();
            
            if (bgMap.contains("cpu_percentages")) {
                QVariantList cpuPercList = bgMap["cpu_percentages"].toList();
                for (const QVariant& cpuPerc : cpuPercList) {
                    bg->add_cpu_percentages(cpuPerc.toDouble());
                }
            }
            if (bgMap.contains("gpu_percentages")) {
                QVariantList gpuPercList = bgMap["gpu_percentages"].toList();
                for (const QVariant& gpuPerc : gpuPercList) {
                    bg->add_gpu_percentages(gpuPerc.toDouble());
                }
            }
            if (bgMap.contains("has_dpc_latency_issues")) bg->set_has_dpc_latency_issues(bgMap["has_dpc_latency_issues"].toBool());
            if (bgMap.contains("has_high_cpu_processes")) bg->set_has_high_cpu_processes(bgMap["has_high_cpu_processes"].toBool());
            if (bgMap.contains("has_high_gpu_processes")) bg->set_has_high_gpu_processes(bgMap["has_high_gpu_processes"].toBool());
            if (bgMap.contains("has_high_memory_processes")) bg->set_has_high_memory_processes(bgMap["has_high_memory_processes"].toBool());
            if (bgMap.contains("max_process_cpu")) bg->set_max_process_cpu(bgMap["max_process_cpu"].toDouble());
            if (bgMap.contains("max_process_memory_mb")) bg->set_max_process_memory_mb(bgMap["max_process_memory_mb"].toDouble());
            if (bgMap.contains("memory_usages_mb")) {
                QVariantList memUsageList = bgMap["memory_usages_mb"].toList();
                for (const QVariant& memUsage : memUsageList) {
                    bg->add_memory_usages_mb(memUsage.toDouble());
                }
            }
            if (bgMap.contains("system_dpc_time")) bg->set_system_dpc_time(bgMap["system_dpc_time"].toDouble());
            if (bgMap.contains("system_interrupt_time")) bg->set_system_interrupt_time(bgMap["system_interrupt_time"].toDouble());
            if (bgMap.contains("total_cpu_usage")) bg->set_total_cpu_usage(bgMap["total_cpu_usage"].toDouble());
            if (bgMap.contains("total_gpu_usage")) bg->set_total_gpu_usage(bgMap["total_gpu_usage"].toDouble());
            
            if (bgMap.contains("memory_metrics")) {
                QVariantMap mmMap = bgMap["memory_metrics"].toMap();
                MemoryMetrics* mm = bg->mutable_memory_metrics();
                if (mmMap.contains("commit_limit_mb")) mm->set_commit_limit_mb(mmMap["commit_limit_mb"].toDouble());
                if (mmMap.contains("commit_percent")) mm->set_commit_percent(mmMap["commit_percent"].toDouble());
                if (mmMap.contains("commit_total_mb")) mm->set_commit_total_mb(mmMap["commit_total_mb"].toDouble());
                if (mmMap.contains("file_cache_mb")) mm->set_file_cache_mb(mmMap["file_cache_mb"].toDouble());
                if (mmMap.contains("kernel_nonpaged_mb")) mm->set_kernel_nonpaged_mb(mmMap["kernel_nonpaged_mb"].toDouble());
                if (mmMap.contains("kernel_paged_mb")) mm->set_kernel_paged_mb(mmMap["kernel_paged_mb"].toDouble());
                if (mmMap.contains("kernel_total_mb")) mm->set_kernel_total_mb(mmMap["kernel_total_mb"].toDouble());
                if (mmMap.contains("other_memory_mb")) mm->set_other_memory_mb(mmMap["other_memory_mb"].toDouble());
                if (mmMap.contains("physical_available_mb")) mm->set_physical_available_mb(mmMap["physical_available_mb"].toDouble());
                if (mmMap.contains("physical_total_mb")) mm->set_physical_total_mb(mmMap["physical_total_mb"].toDouble());
                if (mmMap.contains("physical_used_mb")) mm->set_physical_used_mb(mmMap["physical_used_mb"].toDouble());
                if (mmMap.contains("physical_used_percent")) mm->set_physical_used_percent(mmMap["physical_used_percent"].toDouble());
                if (mmMap.contains("user_mode_private_mb")) mm->set_user_mode_private_mb(mmMap["user_mode_private_mb"].toDouble());
            }
            
            if (bgMap.contains("summary")) {
                QVariantMap summaryMap = bgMap["summary"].toMap();
                BackgroundSummary* summary = bg->mutable_summary();
                if (summaryMap.contains("has_background_issues")) summary->set_has_background_issues(summaryMap["has_background_issues"].toBool());
                if (summaryMap.contains("high_interrupt_activity")) summary->set_high_interrupt_activity(summaryMap["high_interrupt_activity"].toBool());
                if (summaryMap.contains("overall_impact")) summary->set_overall_impact(summaryMap["overall_impact"].toString().toStdString());
            }
        }
        
        if (infoMap.contains("bios")) {
            QVariantMap biosMap = infoMap["bios"].toMap();
            BIOSInfo* bios = info->mutable_bios();
            if (biosMap.contains("date")) bios->set_date(biosMap["date"].toString().toStdString());
            if (biosMap.contains("manufacturer")) bios->set_manufacturer(biosMap["manufacturer"].toString().toStdString());
            if (biosMap.contains("version")) bios->set_version(biosMap["version"].toString().toStdString());
        }
        
        if (infoMap.contains("chipset_drivers")) {
            QVariantList chipsetList = infoMap["chipset_drivers"].toList();
            for (const QVariant& chipsetVariant : chipsetList) {
                QVariantMap chipsetMap = chipsetVariant.toMap();
                DriverInfo* chipset = info->add_chipset_drivers();
                if (chipsetMap.contains("device_name")) chipset->set_device_name(chipsetMap["device_name"].toString().toStdString());
                if (chipsetMap.contains("driver_date")) chipset->set_driver_date(chipsetMap["driver_date"].toString().toStdString());
                if (chipsetMap.contains("driver_version")) chipset->set_driver_version(chipsetMap["driver_version"].toString().toStdString());
                if (chipsetMap.contains("is_date_valid")) chipset->set_is_date_valid(chipsetMap["is_date_valid"].toBool());
                if (chipsetMap.contains("provider_name")) chipset->set_provider_name(chipsetMap["provider_name"].toString().toStdString());
            }
        }
        
        if (infoMap.contains("kernel_memory")) {
            QVariantMap kernelMap = infoMap["kernel_memory"].toMap();
            KernelMemoryInfo* kernel = info->mutable_kernel_memory();
            if (kernelMap.contains("note")) kernel->set_note(kernelMap["note"].toString().toStdString());
        }
        
        if (infoMap.contains("monitors")) {
            QVariantList monitorList = infoMap["monitors"].toList();
            for (const QVariant& monitorVariant : monitorList) {
                QVariantMap monitorMap = monitorVariant.toMap();
                MonitorInfo* monitor = info->add_monitors();
                if (monitorMap.contains("device_name")) monitor->set_device_name(monitorMap["device_name"].toString().toStdString());
                if (monitorMap.contains("display_name")) monitor->set_display_name(monitorMap["display_name"].toString().toStdString());
                if (monitorMap.contains("height")) monitor->set_height(monitorMap["height"].toInt());
                if (monitorMap.contains("is_primary")) monitor->set_is_primary(monitorMap["is_primary"].toBool());
                if (monitorMap.contains("refresh_rate")) monitor->set_refresh_rate(monitorMap["refresh_rate"].toInt());
                if (monitorMap.contains("width")) monitor->set_width(monitorMap["width"].toInt());
            }
        }
        
        if (infoMap.contains("motherboard")) {
            QVariantMap mbMap = infoMap["motherboard"].toMap();
            MotherboardInfo* mb = info->mutable_motherboard();
            if (mbMap.contains("chipset")) mb->set_chipset(mbMap["chipset"].toString().toStdString());
            if (mbMap.contains("chipset_driver")) mb->set_chipset_driver(mbMap["chipset_driver"].toString().toStdString());
            if (mbMap.contains("manufacturer")) mb->set_manufacturer(mbMap["manufacturer"].toString().toStdString());
            if (mbMap.contains("model")) mb->set_model(mbMap["model"].toString().toStdString());
        }
        
        if (infoMap.contains("network_drivers")) {
            QVariantList networkList = infoMap["network_drivers"].toList();
            for (const QVariant& networkVariant : networkList) {
                QVariantMap networkMap = networkVariant.toMap();
                DriverInfo* network = info->add_network_drivers();
                if (networkMap.contains("device_name")) network->set_device_name(networkMap["device_name"].toString().toStdString());
                if (networkMap.contains("driver_date")) network->set_driver_date(networkMap["driver_date"].toString().toStdString());
                if (networkMap.contains("driver_version")) network->set_driver_version(networkMap["driver_version"].toString().toStdString());
                if (networkMap.contains("is_date_valid")) network->set_is_date_valid(networkMap["is_date_valid"].toBool());
                if (networkMap.contains("provider_name")) network->set_provider_name(networkMap["provider_name"].toString().toStdString());
            }
        }
        
        if (infoMap.contains("os")) {
            QVariantMap osMap = infoMap["os"].toMap();
            OSInfo* os = info->mutable_os();
            if (osMap.contains("build")) os->set_build(osMap["build"].toString().toStdString());
            if (osMap.contains("is_windows11")) os->set_is_windows11(osMap["is_windows11"].toBool());
            if (osMap.contains("version")) os->set_version(osMap["version"].toString().toStdString());
        }
        
        if (infoMap.contains("power")) {
            QVariantMap powerMap = infoMap["power"].toMap();
            PowerInfo* power = info->mutable_power();
            if (powerMap.contains("game_mode")) power->set_game_mode(powerMap["game_mode"].toBool());
            if (powerMap.contains("high_performance")) power->set_high_performance(powerMap["high_performance"].toBool());
            if (powerMap.contains("plan")) power->set_plan(powerMap["plan"].toString().toStdString());
        }
        
        if (infoMap.contains("virtualization")) {
            info->set_virtualization(infoMap["virtualization"].toBool());
        }
    }
}

QString ProtobufSerializer::detectMessageType(const QVariantMap& data) const {
    if (isValidDiagnosticSubmission(data)) {
        return "DiagnosticSubmission";
    } else if (isValidMenuResponse(data)) {
        return "MenuResponse";
    } else if (isValidComponentComparison(data)) {
        return "ComponentComparison";
    }
    
    return QString();
}

bool ProtobufSerializer::isValidDiagnosticSubmission(const QVariantMap& data) const {
    // Check for diagnostic submission structure (has cpu, gpu, memory, drives, network, system, or metadata)
    return data.contains("cpu") || data.contains("gpu") || 
           data.contains("memory") || data.contains("drives") || 
           data.contains("network") || data.contains("system") || 
           data.contains("metadata") ||
           data.contains("optimization_settings_json") || data.contains("pdh_metrics_csv");
}

bool ProtobufSerializer::isValidMenuResponse(const QVariantMap& data) const {
    // Check for menu response structure
    return data.contains("available_cpus") || data.contains("availableCpus") ||
           data.contains("available_gpus") || data.contains("availableGpus") ||
           data.contains("endpoints");
}

bool ProtobufSerializer::isValidComponentComparison(const QVariantMap& data) const {
    // Check for component comparison structure
    return (data.size() == 1) && 
           (data.contains("cpu") || data.contains("gpu") || 
            data.contains("memory") || data.contains("drive"));
}
