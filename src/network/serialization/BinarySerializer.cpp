#include "BinarySerializer.h"
#include "../../logging/Logger.h"

SerializationResult BinarySerializer::serialize(const QVariant& data) {
    SerializationResult r;
    if (data.canConvert<QByteArray>()) {
        r.data = data.toByteArray();
        r.success = true;
        LOG_INFO << "BinarySerializer::serialize bytes=" << r.data.size();
    } else {
        r.error = QStringLiteral("BinarySerializer expects QByteArray");
        LOG_ERROR << r.error.toStdString();
    }
    return r;
}

DeserializationResult BinarySerializer::deserialize(const QByteArray& data, const QString&) {
    DeserializationResult r;
    r.success = true;
    r.data = data; // keep as raw bytes; caller decides how to parse
    LOG_INFO << "BinarySerializer::deserialize bytes=" << data.size();
    return r;
}

bool BinarySerializer::canSerialize(const QVariant& data) const {
    return data.canConvert<QByteArray>();
}
