#ifndef CSVSERIALIZER_H
#define CSVSERIALIZER_H

// CsvSerializer - CSV format serialization implementation
// Used by: UploadApiClient when uploading CSV benchmark data files
// Purpose: Convert QVariantList<QVariantMap> ↔ CSV with proper escaping and headers
// When to use: For tabular data exports/uploads - requires list of uniform objects
// Operations: CSV parsing with quotes/commas handling, header row management, field escaping

#include "ISerializer.h"

class CsvSerializer : public ISerializer {
public:
    SerializationFormat getFormat() const override;
    QString getContentType() const override;
    
    SerializationResult serialize(const QVariant& data) override;
    DeserializationResult deserialize(const QByteArray& data,
                                      const QString& expectedType = QString()) override;
    
    bool canSerialize(const QVariant& data) const override;

private:
    QString escapeField(const QString& field) const;
    QStringList parseCSVLine(const QString& line) const;
};

#endif // CSVSERIALIZER_H
