#include "BenchmarkFullCsvToProto.h"
#include "../../logging/Logger.h"
#include <QFile>
#include <QFileInfo>
#include <QDateTime>

// Generated headers
#include "benchmark_upload.pb.h"
#include "benchmark_public.pb.h"
#include "benchmark_full.pb.h"
#include "benchmark_common.pb.h"

using namespace checkmark::benchmarks;

static QByteArray readAllBytes(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        LOG_WARN << "BenchmarkFullCsvToProto: failed to open attachment: " << path.toStdString();
        return QByteArray();
    }
    QByteArray data = f.readAll();
    f.close();
    return data;
}

QByteArray BenchmarkFullCsvToProto::buildUploadFromCsv(const QString& csvPath,
                                                       const QString& runId,
                                                       const QString& userSystemId,
                                                       const QStringList& attachmentPaths) {
    LOG_INFO << "BenchmarkFullCsvToProto::buildUploadFromCsv: csv=" << csvPath.toStdString();

    BenchmarkUploadRequest req;

    // Envelope
    ClientEnvelope* env = req.mutable_env();
    env->set_client_version("checkmark-client");
    env->set_schema_version("1");

    // Meta
    BenchmarkRunMeta* meta = req.mutable_meta();
    if (!runId.isEmpty()) meta->set_run_id(runId.toStdString());
    meta->set_user_system_id(userSystemId.toStdString());
    meta->set_timestamp_utc(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString());

    // TODO: Parse CSV -> fill FullRun and PublicSummary/Samples when mapper ready
    // Attachments: include provided files
    for (const auto& p : attachmentPaths) {
        QFileInfo fi(p);
        if (!fi.exists() || !fi.isFile()) continue;
        Attachment* a = req.add_attachments();
        a->set_filename(fi.fileName().toStdString());
        QString mt = fi.suffix().compare("csv", Qt::CaseInsensitive) == 0 ? "text/csv" :
                     fi.suffix().compare("json", Qt::CaseInsensitive) == 0 ? "application/json" :
                     "text/plain";
        a->set_mime_type(mt.toStdString());
        QByteArray bytes = readAllBytes(p);
        if (!bytes.isEmpty()) a->set_content(bytes.constData(), bytes.size());
    }

    std::string out;
    if (!req.SerializeToString(&out)) {
        LOG_ERROR << "BenchmarkFullCsvToProto: failed to serialize BenchmarkUploadRequest";
        return QByteArray();
    }
    QByteArray ba(out.data(), static_cast<int>(out.size()));
    LOG_INFO << "BenchmarkFullCsvToProto: built protobuf payload, bytes=" << ba.size();
    return ba;
}
