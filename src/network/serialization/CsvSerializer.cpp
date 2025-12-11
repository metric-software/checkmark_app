#include "CsvSerializer.h"
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>

SerializationFormat CsvSerializer::getFormat() const {
    return SerializationFormat::CSV;
}

QString CsvSerializer::getContentType() const {
    return "text/csv";
}

SerializationResult CsvSerializer::serialize(const QVariant& data) {
    SerializationResult result;
    
    if (data.type() == QVariant::List) {
        QVariantList list = data.toList();
        
        if (list.isEmpty()) {
            result.success = true;
            return result;
        }
        
        // Check if first item is a map (table format)
        if (list.first().type() == QVariant::Map) {
            QStringList csvLines;
            QVariantMap firstRow = list.first().toMap();
            QStringList headers = firstRow.keys();
            
            // Add header row
            QStringList headerRow;
            for (const QString& header : headers) {
                headerRow.append(escapeField(header));
            }
            csvLines.append(headerRow.join(","));
            
            // Add data rows
            for (const QVariant& item : list) {
                if (item.type() != QVariant::Map) {
                    result.error = "All list items must be maps for CSV serialization";
                    return result;
                }
                
                QVariantMap row = item.toMap();
                QStringList dataRow;
                for (const QString& header : headers) {
                    QString value = row.value(header).toString();
                    dataRow.append(escapeField(value));
                }
                csvLines.append(dataRow.join(","));
            }
            
            result.data = csvLines.join("\n").toUtf8();
            result.success = true;
        } else {
            // Simple list - single column
            QStringList csvLines;
            csvLines.append("value");  // Header
            
            for (const QVariant& item : list) {
                csvLines.append(escapeField(item.toString()));
            }
            
            result.data = csvLines.join("\n").toUtf8();
            result.success = true;
        }
    } else {
        result.error = "CSV serialization requires a list of data";
    }
    
    return result;
}

DeserializationResult CsvSerializer::deserialize(const QByteArray& data, const QString&) {
    DeserializationResult result;
    
    QString csvContent = QString::fromUtf8(data);
    QStringList lines = csvContent.split('\n', Qt::SkipEmptyParts);
    
    if (lines.isEmpty()) {
        result.data = QVariantList();
        result.success = true;
        return result;
    }
    
    // Parse header
    QStringList headers = parseCSVLine(lines.first());
    
    QVariantList resultList;
    
    // Parse data rows
    for (int i = 1; i < lines.size(); ++i) {
        QStringList values = parseCSVLine(lines[i]);
        
        if (values.size() != headers.size()) {
            result.error = QString("Row %1 has %2 values but expected %3")
                          .arg(i).arg(values.size()).arg(headers.size());
            return result;
        }
        
        QVariantMap rowMap;
        for (int j = 0; j < headers.size(); ++j) {
            rowMap[headers[j]] = values[j];
        }
        resultList.append(rowMap);
    }
    
    result.data = resultList;
    result.success = true;
    return result;
}

bool CsvSerializer::canSerialize(const QVariant& data) const {
    return data.type() == QVariant::List;
}

QString CsvSerializer::escapeField(const QString& field) const {
    if (field.contains(',') || field.contains('"') || field.contains('\n')) {
        QString escaped = field;
        escaped.replace('"', "\"\"");
        return '"' + escaped + '"';
    }
    return field;
}

QStringList CsvSerializer::parseCSVLine(const QString& line) const {
    QStringList fields;
    QString currentField;
    bool inQuotes = false;
    
    for (int i = 0; i < line.length(); ++i) {
        QChar ch = line[i];
        
        if (ch == '"') {
            if (inQuotes && i + 1 < line.length() && line[i + 1] == '"') {
                // Escaped quote
                currentField += '"';
                i++; // Skip next quote
            } else {
                inQuotes = !inQuotes;
            }
        } else if (ch == ',' && !inQuotes) {
            fields.append(currentField);
            currentField.clear();
        } else {
            currentField += ch;
        }
    }
    
    fields.append(currentField);
    return fields;
}
