#ifndef BENCHMARKFULLCSVTOPROTO_H
#define BENCHMARKFULLCSVTOPROTO_H

#include <QString>
#include <QStringList>
#include <QByteArray>

namespace BenchmarkFullCsvToProto {

// Build a binary-encoded BenchmarkUploadRequest from a full CSV and attachments.
// CSV parsing into FullRun/Public is TODO; attachments always include the source CSV.
QByteArray buildUploadFromCsv(const QString& csvPath,
                              const QString& runId,
                              const QString& userSystemId,
                              const QStringList& attachmentPaths);

}

#endif // BENCHMARKFULLCSVTOPROTO_H