#ifndef PUBLICEXPORTBUILDER_H
#define PUBLICEXPORTBUILDER_H

// PublicExportBuilder - builds public CSV and summary from full CSV results,
// and creates QVariant structures ready for ProtobufSerializer mapping.
// NOTE: Template header only; implement after confirming column aliases.

#include <QString>
#include <QVariant>
#include <QVector>

struct PublicFileOutputs {
    QString publicCsvPath;      // written local file path
    QString publicSummaryPath;  // written local file path
};

class PublicExportBuilder {
public:
    PublicExportBuilder() = default;

    // Parse a full CSV and compute an in-memory representation for public samples
    QVariant buildPublicSamplesVariant(const QString& csvPath) const; // -> QVariantList of samples

    // Compute summary (avg FPS, etc.) as QVariantMap
    QVariant buildPublicSummaryVariant(const QString& csvPath) const;

    // Write local artifacts
    PublicFileOutputs writePublicFiles(const QString& csvPath, const QString& outDir) const;

    // Compose a BenchmarkUploadRequest-like QVariantMap (protobuf-ready)
    QVariant buildUploadRequestVariant(const QString& csvPath,
                                       const QString& runId,
                                       const QString& userSystemId,
                                       const QStringList& attachmentPaths) const;

private:
    // Parse specs file to extract system information for public summary
    QVariantMap parseSpecsFile(const QString& csvPath) const;

    // Parse CSV and compute per-column statistics (min/max/avg + counts)
    QVariantList computeColumnStats(const QString& csvPath) const;
};

#endif // PUBLICEXPORTBUILDER_H
