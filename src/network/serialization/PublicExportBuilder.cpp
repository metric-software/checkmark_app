#include "PublicExportBuilder.h"
#include "CsvSerializer.h"
#include "../../logging/Logger.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <limits>
#include <unordered_map>
#include <vector>

// Generated from split proto files
#include "benchmark_upload.pb.h"
#include "benchmark_public.pb.h"

using checkmark::benchmarks::BenchmarkUploadRequest;
using checkmark::benchmarks::ClientEnvelope;
using checkmark::benchmarks::BenchmarkRunMeta;
using checkmark::benchmarks::PublicSummary;
using checkmark::benchmarks::PublicSample;
using checkmark::benchmarks::CoreUsage;
using checkmark::benchmarks::Attachment;
using checkmark::benchmarks::ColumnStat;

static QByteArray readAllBytes(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        LOG_WARN << "PublicExportBuilder: failed to open attachment: " << path.toStdString();
        return QByteArray();
    }
    QByteArray data = f.readAll();
    f.close();
    return data;
}

namespace {
// Minimal CSV parser (quotes/commas) to preserve alignment with CsvSerializer
QStringList parseCsvLine(const QString& line) {
    QStringList fields;
    QString currentField;
    bool inQuotes = false;

    for (int i = 0; i < line.length(); ++i) {
        QChar ch = line[i];

        if (ch == '"') {
            if (inQuotes && i + 1 < line.length() && line[i + 1] == '"') {
                currentField += '"';
                ++i;
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

struct StatAccumulator {
    double sum = 0.0;
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();
    uint32_t validCount = 0;
    uint32_t totalCount = 0;

    void addSample(const QString& valueStr) {
        totalCount++;
        bool ok = false;
        double v = valueStr.toDouble(&ok);
        if (!ok || v == -1.0) {
            return; // treat -1 or non-numeric as invalid/missing
        }

        sum += v;
        validCount++;
        if (v < min) min = v;
        if (v > max) max = v;
    }

    QVariantMap toVariant(const QString& column) const {
        QVariantMap m;
        double avg = validCount > 0 ? sum / static_cast<double>(validCount) : 0.0;
        double minOut = validCount > 0 ? min : 0.0;
        double maxOut = validCount > 0 ? max : 0.0;

        m.insert(QStringLiteral("column"), column);
        m.insert(QStringLiteral("avg"), avg);
        m.insert(QStringLiteral("min"), minOut);
        m.insert(QStringLiteral("max"), maxOut);
        m.insert(QStringLiteral("valid_count"), static_cast<int>(validCount));
        m.insert(QStringLiteral("total_count"), static_cast<int>(totalCount));
        return m;
    }
};
} // namespace

QVariant PublicExportBuilder::buildPublicSamplesVariant(const QString& csvPath) const {
    LOG_INFO << "PublicExportBuilder::buildPublicSamplesVariant parsing: " << csvPath.toStdString();
    
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR << "Failed to open CSV file: " << csvPath.toStdString();
        return QVariantList{};
    }
    
    QByteArray csvData = file.readAll();
    file.close();
    
    CsvSerializer csvSerializer;
    DeserializationResult result = csvSerializer.deserialize(csvData);
    
    if (!result.success) {
        LOG_ERROR << "CSV parsing failed: " << result.error.toStdString();
        return QVariantList{};
    }
    
    QVariantList fullData = result.data.toList();
    if (fullData.isEmpty()) {
        LOG_WARN << "CSV file is empty";
        return QVariantList{};
    }
    
    // Define the columns we want to include in public data (matching specification in benchmark_backend_data.md)
    QStringList publicColumns = {
        "Time",                    // Time
        "FPS",                     // FPS
        "1% High Frame Time",      // 1% High Frame Time
        "5% High Frame Time",      // 5% High Frame Time
        "GPU Usage",               // GPU Usage (there is no "GPU Utilization" column, using "GPU Usage")
        "PDH_Memory_Load(%)",       // Memory Load
        "GPU Mem Used",            // GPU Mem Used
        "GPU Mem Total",           // GPU Mem Total  
        "Frame Time Variance",     // Frame Time Variance
        "Highest Frame Time",      // Highest Frame Time
        "Frame Time",              // Frame Time
        "PDH_Memory_Available(MB)" // Memory Usage (MB) - using Available as proxy
        // Note: Core <N> (%) will be added dynamically below
    };
    
    // Add all CPU core columns (PDH_Core N CPU (%))
    for (int core = 0; core <= 7; ++core) {
        publicColumns.append(QString("PDH_Core %1 CPU (%)").arg(core));
    }
    
    QVariantList publicSamples;
    
    for (const QVariant& rowVariant : fullData) {
        QVariantMap fullRow = rowVariant.toMap();
        QVariantMap publicRow;
        
        // Extract only the public columns
        for (const QString& column : publicColumns) {
            if (fullRow.contains(column)) {
                publicRow[column] = fullRow[column];
            }
        }
        
        if (!publicRow.isEmpty()) {
            publicSamples.append(publicRow);
        }
    }
    
    LOG_INFO << "Built " << publicSamples.size() << " public samples from " << fullData.size() << " total samples";
    return publicSamples;
}

QVariant PublicExportBuilder::buildPublicSummaryVariant(const QString& csvPath) const {
    LOG_INFO << "PublicExportBuilder::buildPublicSummaryVariant computing from: " << csvPath.toStdString();

    QVariantList columnStats = computeColumnStats(csvPath);
    if (columnStats.isEmpty()) {
        LOG_WARN << "Column stats empty, returning default summary";
    }

    // Build a lookup for convenience
    std::unordered_map<std::string, QVariantMap> statsLookup;
    for (const QVariant& v : columnStats) {
        QVariantMap m = v.toMap();
        QString colName = m.value(QStringLiteral("column")).toString();
        statsLookup[colName.toStdString()] = m;
    }

    auto getAvg = [&](const QString& name) -> double {
        auto it = statsLookup.find(name.toStdString());
        if (it == statsLookup.end()) return 0.0;
        return it->second.value(QStringLiteral("avg")).toDouble();
    };
    auto getMax = [&](const QString& name) -> double {
        auto it = statsLookup.find(name.toStdString());
        if (it == statsLookup.end()) return 0.0;
        return it->second.value(QStringLiteral("max")).toDouble();
    };

    double avgFps = getAvg(QStringLiteral("FPS"));
    double avgFrameTime = getAvg(QStringLiteral("Frame Time"));
    double avgGpuUsage = getAvg(QStringLiteral("GPU Usage"));
    double avgMemoryLoad = getAvg(QStringLiteral("PDH_Memory_Load(%)"));
    double highestFrameTime = getMax(QStringLiteral("Highest Frame Time"));

    // Use cumulative columns if present for lows; otherwise fallback to FPS stats
    double p1LowFps = getAvg(QStringLiteral("1% Low FPS (Cumulative)"));
    double p5LowFps = getAvg(QStringLiteral("5% Low FPS (Cumulative)"));

    LOG_INFO << "Calculated metrics - avg FPS: " << avgFps
             << ", avg Frame Time: " << avgFrameTime
             << ", avg GPU Usage: " << avgGpuUsage
             << ", avg Memory Load: " << avgMemoryLoad
             << ", highest Frame Time: " << highestFrameTime
             << ", 1% low FPS: " << p1LowFps
             << ", 5% low FPS: " << p5LowFps;

    QVariantMap summary;
    summary.insert(QStringLiteral("avg_fps"), avgFps);
    summary.insert(QStringLiteral("avg_frame_time_ms"), avgFrameTime);
    summary.insert(QStringLiteral("avg_gpu_usage_pct"), avgGpuUsage);
    summary.insert(QStringLiteral("avg_memory_load_pct"), avgMemoryLoad);
    summary.insert(QStringLiteral("p1_low_fps_cumulative"), p1LowFps);
    summary.insert(QStringLiteral("p5_low_fps_cumulative"), p5LowFps);
    summary.insert(QStringLiteral("highest_frame_time_ms"), highestFrameTime);
    summary.insert(QStringLiteral("column_stats"), columnStats);

    // Add system specs from specs file
    QVariantMap specsData = parseSpecsFile(csvPath);
    for (auto it = specsData.begin(); it != specsData.end(); ++it) {
        summary.insert(it.key(), it.value());
    }

    return summary;
}

PublicFileOutputs PublicExportBuilder::writePublicFiles(const QString& csvPath, const QString& outDir) const {
    // TODO: write <timestamp>_<hash>_public.csv and _public_summary.json
    Q_UNUSED(csvPath);
    Q_UNUSED(outDir);
    LOG_INFO << "PublicExportBuilder::writePublicFiles (stub)";
    return PublicFileOutputs{};
}

QVariant PublicExportBuilder::buildUploadRequestVariant(const QString& csvPath,
                                       const QString& runId,
                                       const QString& userSystemId,
                                       const QStringList& attachmentPaths) const {
    LOG_INFO << "PublicExportBuilder::buildUploadRequestVariant: csv=" << csvPath.toStdString();

    BenchmarkUploadRequest req;

    // Envelope
    ClientEnvelope* env = req.mutable_env();
    env->set_client_version("checkmark-client");
    env->set_schema_version("1");

    // Meta
    BenchmarkRunMeta* meta = req.mutable_meta();
    // For GDPR-neutral uploads, userSystemId should be empty at call-site
    meta->set_user_system_id(userSystemId.toStdString());
    const QString timestampIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    meta->set_timestamp_utc(timestampIso.toStdString());

    // Public summary - compute from CSV
    QVariant summaryVariant = buildPublicSummaryVariant(csvPath);
    QVariantMap summaryMap = summaryVariant.toMap();
    
    PublicSummary* summary = req.mutable_public_summary();
    
    // Set all computed metrics
    double avgFps = summaryMap.value("avg_fps").toDouble();
    double avgFrameTime = summaryMap.value("avg_frame_time_ms").toDouble();
    double avgGpuUsage = summaryMap.value("avg_gpu_usage_pct").toDouble();
    double avgMemoryLoad = summaryMap.value("avg_memory_load_pct").toDouble();
    double p1LowFps = summaryMap.value("p1_low_fps_cumulative").toDouble();
    double p5LowFps = summaryMap.value("p5_low_fps_cumulative").toDouble();
    double highestFrameTime = summaryMap.value("highest_frame_time_ms").toDouble();
    
    summary->set_avg_fps(avgFps);
    summary->set_avg_frame_time_ms(avgFrameTime);
    summary->set_avg_gpu_usage_pct(avgGpuUsage);
    summary->set_avg_memory_load_pct(avgMemoryLoad);
    summary->set_p1_low_fps_cumulative(p1LowFps);
    summary->set_p5_low_fps_cumulative(p5LowFps);
    summary->set_highest_frame_time_ms(highestFrameTime);

    // Column-level stats
    QVariantList colStats = summaryMap.value(QStringLiteral("column_stats")).toList();
    for (const QVariant& v : colStats) {
        QVariantMap m = v.toMap();
        ColumnStat* cs = summary->add_column_stats();
        cs->set_column(m.value(QStringLiteral("column")).toString().toStdString());
        cs->set_avg(m.value(QStringLiteral("avg")).toDouble());
        cs->set_min(m.value(QStringLiteral("min")).toDouble());
        cs->set_max(m.value(QStringLiteral("max")).toDouble());
        cs->set_valid_samples(m.value(QStringLiteral("valid_count")).toInt());
        cs->set_total_samples(m.value(QStringLiteral("total_count")).toInt());
    }
    
    LOG_INFO << "Setting public summary metrics - FPS: " << avgFps
             << ", Frame Time: " << avgFrameTime
             << ", GPU Usage: " << avgGpuUsage
             << ", Memory Load: " << avgMemoryLoad
             << ", 1% Low FPS: " << p1LowFps
             << ", 5% Low FPS: " << p5LowFps
             << ", Highest Frame Time: " << highestFrameTime;

    // Compute deterministic validity hash (run_id) from public summary + timestamp
    // Use fixed precision to ensure repeatability
    auto fmt = [](double v){ return QString::number(v, 'f', 3); };
    QStringList parts;
    parts << QStringLiteral("avg_fps:") + fmt(avgFps)
          << QStringLiteral("avg_frame_time_ms:") + fmt(avgFrameTime)
          << QStringLiteral("avg_gpu_usage_pct:") + fmt(avgGpuUsage)
          << QStringLiteral("avg_memory_load_pct:") + fmt(avgMemoryLoad)
          << QStringLiteral("p1_low_fps_cumulative:") + fmt(p1LowFps)
          << QStringLiteral("p5_low_fps_cumulative:") + fmt(p5LowFps)
          << QStringLiteral("highest_frame_time_ms:") + fmt(highestFrameTime)
          << QStringLiteral("timestamp_utc:") + timestampIso;
    QString canonical = parts.join('|');
    QByteArray hash = QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex();
    QString runIdDet = hash.left(16); // short, readable
    if (!runId.isEmpty()) {
        // If caller provided runId, prefer it; else use our computed one
        meta->set_run_id(runId.toStdString());
    } else {
        meta->set_run_id(runIdDet.toStdString());
    }
    
    // Set system specs fields
    if (summaryMap.contains("cpu_model")) {
        summary->set_cpu_model(summaryMap.value("cpu_model").toString().toStdString());
    }
    if (summaryMap.contains("memory_total_physical")) {
        summary->set_memory_total_physical(summaryMap.value("memory_total_physical").toString().toStdString());
    }
    if (summaryMap.contains("memory_clock")) {
        summary->set_memory_clock(summaryMap.value("memory_clock").toString().toStdString());
    }
    if (summaryMap.contains("gpu_primary_model")) {
        summary->set_gpu_primary_model(summaryMap.value("gpu_primary_model").toString().toStdString());
    }
    if (summaryMap.contains("graphics_resolution")) {
        summary->set_graphics_resolution(summaryMap.value("graphics_resolution").toString().toStdString());
    }

    // Public samples intentionally omitted for now (full CSV still uploaded as attachment)

    // Attachments: include all provided files
    for (const auto& p : attachmentPaths) {
        QFileInfo fi(p);
        if (!fi.exists() || !fi.isFile()) continue;
        Attachment* a = req.add_attachments();
        a->set_filename(fi.fileName().toStdString());
        // crude mime type guess
        QString mt = fi.suffix().compare("csv", Qt::CaseInsensitive) == 0 ? "text/csv" :
                     fi.suffix().compare("json", Qt::CaseInsensitive) == 0 ? "application/json" :
                     "text/plain";
        a->set_mime_type(mt.toStdString());
        QByteArray bytes = readAllBytes(p);
        if (!bytes.isEmpty()) a->set_content(bytes.constData(), bytes.size());
    }

    std::string out;
    if (!req.SerializeToString(&out)) {
        LOG_ERROR << "PublicExportBuilder: failed to serialize BenchmarkUploadRequest";
        return QVariant();
    }
    QByteArray ba(out.data(), static_cast<int>(out.size()));
    LOG_INFO << "PublicExportBuilder: built protobuf payload, bytes=" << ba.size();
    return ba; // Will be sent with BinarySerializer
}

QVariantMap PublicExportBuilder::parseSpecsFile(const QString& csvPath) const {
    QVariantMap specsData;
    
    // Derive specs file path from CSV path
    QString specsPath = csvPath;
    specsPath.replace(".csv", "_specs.txt");
    
    QFile specsFile(specsPath);
    if (!specsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_WARN << "Failed to open specs file: " << specsPath.toStdString();
        return specsData; // Return empty map if file not found
    }
    
    QTextStream in(&specsFile);
    QString content = in.readAll();
    specsFile.close();
    
    // Parse the specs file content
    QStringList lines = content.split('\n', Qt::KeepEmptyParts);
    
    QString cpuModel;
    QString memoryTotalPhysical;
    QString memoryClock;
    QString gpuPrimaryModel;
    QString graphicsResolution;
    
    QString currentSection;
    bool inRustConfig = false;
    
    for (const QString& line : lines) {
        QString trimmedLine = line.trimmed();
        
        if (trimmedLine.startsWith("CPU Information:")) {
            currentSection = "CPU";
            continue;
        } else if (trimmedLine.startsWith("Memory Information:")) {
            currentSection = "Memory";
            continue;
        } else if (trimmedLine.startsWith("GPU Devices")) {
            currentSection = "GPU";
            continue;
        } else if (trimmedLine.startsWith("Rust Configuration:")) {
            currentSection = "Rust";
            inRustConfig = true;
            continue;
        } else if (trimmedLine.isEmpty() && inRustConfig) {
            inRustConfig = false;
        }
        
        // Parse CPU Model
        if (currentSection == "CPU" && trimmedLine.startsWith("Model:")) {
            cpuModel = trimmedLine.mid(7).trimmed(); // Remove "Model: "
        }
        // Parse Memory Total Physical
        else if (currentSection == "Memory" && trimmedLine.startsWith("Total Physical:")) {
            memoryTotalPhysical = trimmedLine.mid(16).trimmed(); // Remove "Total Physical: "
        }
        // Parse Memory Clock
        else if (currentSection == "Memory" && trimmedLine.startsWith("Clock:")) {
            memoryClock = trimmedLine.mid(7).trimmed(); // Remove "Clock: "
        }
        // Parse GPU Primary Model (look for "GPU 1 (Primary):" section)
        else if (currentSection == "GPU" && trimmedLine.contains("GPU 1 (Primary)")) {
            // Next line should contain the model
            continue;
        }
        else if (currentSection == "GPU" && trimmedLine.startsWith("Model:") && gpuPrimaryModel.isEmpty()) {
            gpuPrimaryModel = trimmedLine.mid(7).trimmed(); // Remove "Model: "
        }
        // Parse Rust Configuration graphics.resolution
        else if (inRustConfig && trimmedLine.startsWith("graphics.resolution")) {
            QStringList parts = trimmedLine.split('=');
            if (parts.size() == 2) {
                graphicsResolution = parts[1].trimmed();
            }
        }
    }
    
    // Add parsed data to the map
    if (!cpuModel.isEmpty()) {
        specsData.insert("cpu_model", cpuModel);
    }
    if (!memoryTotalPhysical.isEmpty()) {
        specsData.insert("memory_total_physical", memoryTotalPhysical);
    }
    if (!memoryClock.isEmpty()) {
        specsData.insert("memory_clock", memoryClock);
    }
    if (!gpuPrimaryModel.isEmpty()) {
        specsData.insert("gpu_primary_model", gpuPrimaryModel);
    }
    if (!graphicsResolution.isEmpty()) {
        specsData.insert("graphics_resolution", graphicsResolution);
    }

    LOG_INFO << "Parsed specs file - CPU: " << cpuModel.toStdString() 
             << ", Memory: " << memoryTotalPhysical.toStdString() 
             << ", Clock: " << memoryClock.toStdString()
             << ", GPU: " << gpuPrimaryModel.toStdString()
             << ", Resolution: " << graphicsResolution.toStdString();
    
    return specsData;
}

QVariantList PublicExportBuilder::computeColumnStats(const QString& csvPath) const {
    QFile file(csvPath);
    QVariantList out;

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_ERROR << "computeColumnStats: failed to open CSV: " << csvPath.toStdString();
        return out;
    }

    QTextStream in(&file);
    if (in.atEnd()) {
        LOG_WARN << "computeColumnStats: empty CSV: " << csvPath.toStdString();
        file.close();
        return out;
    }

    QString headerLine = in.readLine();
    QStringList headers = parseCsvLine(headerLine);
    if (headers.isEmpty()) {
        LOG_ERROR << "computeColumnStats: header parse failed for: " << csvPath.toStdString();
        file.close();
        return out;
    }

    std::vector<StatAccumulator> stats(headers.size());

    int rowIndex = 0;
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.isEmpty())
            continue;

        QStringList fields = parseCsvLine(line);
        if (fields.size() != headers.size()) {
            LOG_WARN << "computeColumnStats: row " << rowIndex
                     << " has " << fields.size()
                     << " fields, expected " << headers.size();
            rowIndex++;
            continue;
        }

        for (int i = 0; i < fields.size(); ++i) {
            stats[i].addSample(fields[i]);
        }

        rowIndex++;
    }

    file.close();

    for (int i = 0; i < headers.size(); ++i) {
        out.append(stats[i].toVariant(headers[i]));
    }

    return out;
}
