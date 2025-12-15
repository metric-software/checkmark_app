#include "UploadApiClient.h"
#include "BenchmarkApiClient.h"
#include "../serialization/JsonSerializer.h"
#include "../serialization/CsvSerializer.h"
#include "../serialization/ProtobufSerializer.h"
#include "../serialization/PublicExportBuilder.h"
#include "../../logging/Logger.h"
#include "../../ApplicationSettings.h"
#include "../core/FeatureToggleManager.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <limits>
#include <algorithm>
// Helper: compute a deterministic diagnostics validity hash from a few key values
static QString computeDiagnosticsValidityHash(const QVariantMap& root) {
    auto fmt = [](double v){ return QString::number(v, 'f', 3); };
    QStringList parts;
    // CPU results
    QVariantMap cpu = root.value("cpu").toMap();
    QVariantMap results = cpu.value("results").toMap();
    const QStringList keys = {
        QStringLiteral("single_core"),
        QStringLiteral("multi_core"),
        QStringLiteral("four_thread"),
        QStringLiteral("simd_scalar"),
        QStringLiteral("avx"),
        QStringLiteral("prime_time"),
        QStringLiteral("game_sim_small"),
        QStringLiteral("game_sim_medium"),
        QStringLiteral("game_sim_large")
    };
    for (const QString& k : keys) {
        if (results.contains(k)) {
            parts << (k + QLatin1Char(':') + fmt(results.value(k).toDouble()));
        }
    }
    // include a coarse timestamp component if available (from metadata)
    QVariantMap metadata = root.value("metadata").toMap();
    QString ts = metadata.value("timestamp").toString();
    if (!ts.isEmpty()) {
        // Reduce precision to minute to avoid accidental uniqueness
        QString tsMinute = ts.left(16); // e.g., 2025-09-05T12:34
        parts << (QStringLiteral("timestamp:") + tsMinute);
    }
    QString canonical = parts.join('|');
    QByteArray hash = QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash.left(16));
}

// Helper: scrub PII from diagnostics metadata and inject validity hash
static QVariant sanitizeDiagnosticsPayload(const QVariant& data) {
    if (data.type() != QVariant::Map) return data;
    QVariantMap root = data.toMap();
    QVariantMap metadata = root.value("metadata").toMap();
    // Remove PII fields
    metadata.remove("user_id");
    metadata.remove("combined_identifier");
    // Remove nested system_id entirely
    metadata.remove("system_id");
    // Compute validity hash and store it in system_hash field (repurposed)
    QString vhash = computeDiagnosticsValidityHash(root);
    metadata.insert("system_hash", vhash);
    // Write back
    root.insert("metadata", metadata);
    return QVariant(root);
}

UploadApiClient::UploadApiClient(QObject* parent)
    : BaseApiClient(parent), m_uploading(false) {
    
    // Set protobuf serializer for binary protobuf communication
    setSerializer(std::make_shared<ProtobufSerializer>());
    
    connect(this, &BaseApiClient::requestProgress, 
            this, &UploadApiClient::onRequestProgress);
}

void UploadApiClient::pingServer(PingCallback callback) {
    get("/pb/ping", [this, callback](const ApiResponse& response) {
        bool success = response.success;
        emit pingCompleted(success);
        callback(success, response.error);
    }, false); // Don't use cache for ping
}

void UploadApiClient::uploadFiles(const QStringList& filePaths, UploadCallback callback) {
    LOG_INFO << "UploadApiClient::uploadFiles called with " << filePaths.size() << " files";

    LOG_INFO << "UploadApiClient: refreshing remote flags before upload gate check";
    {
        FeatureToggleManager featureToggleManager;
        featureToggleManager.fetchAndApplyRemoteFlags();
    }

    if (!ApplicationSettings::getInstance().getEffectiveAutomaticDataUploadEnabled()) {
        QString error = ApplicationSettings::getInstance().isOfflineModeEnabled()
            ? QStringLiteral("Offline mode is enabled")
            : QStringLiteral("Data collection/upload is disabled");
        LOG_INFO << "Upload blocked: " << error.toStdString();
        emit uploadError(error);
        if (callback) callback(false, error);
        return;
    }

    if (m_uploading) {
        QString error = "Upload already in progress";
        LOG_ERROR << "Upload rejected: " << error.toStdString();
        emit uploadError(error);
        if (callback) callback(false, error);
        return;
    }
    
    if (filePaths.isEmpty()) {
        QString error = "No files to upload";
        LOG_ERROR << "Upload rejected: " << error.toStdString();
        emit uploadError(error);
        if (callback) callback(false, error);
        return;
    }
    
    m_uploading = true;
    m_uploadQueue = filePaths;
    m_batchCallback = callback;
    m_totalFilesInBatch = m_uploadQueue.size();
    m_completedInBatch = 0;
    m_successCount = 0;
    m_failureCount = 0;
    m_firstError.clear();

    LOG_INFO << "Enqueued " << m_totalFilesInBatch << " files for sequential upload";
    emit uploadBatchStarted(m_totalFilesInBatch);
    
    uploadNextInQueue();
}

void UploadApiClient::uploadNextInQueue() {
    if (m_uploadQueue.isEmpty()) {
        bool overallSuccess = (m_failureCount == 0);
        LOG_INFO << "Upload batch finished - success: " << overallSuccess 
                 << ", completed=" << m_completedInBatch 
                 << ", successCount=" << m_successCount 
                 << ", failureCount=" << m_failureCount;
        
        m_uploading = false;
        emit uploadBatchFinished(m_successCount, m_failureCount);
        emit uploadCompleted(overallSuccess);
        if (m_batchCallback) {
            m_batchCallback(overallSuccess, overallSuccess ? QString() : m_firstError);
        }
        m_batchCallback = nullptr;
        m_uploadQueue.clear();
        return;
    }

    QString currentFile = m_uploadQueue.takeFirst();
    QFileInfo fileInfo(currentFile);
    LOG_INFO << "Processing file " << (m_completedInBatch + 1) << "/" << m_totalFilesInBatch 
             << ": " << currentFile.toStdString();
    emit uploadFileStarted(currentFile);

    QString error;

    if (currentFile.endsWith(".json", Qt::CaseInsensitive)) {
        // Diagnostics JSON submission (enhanced): attach optimization settings JSON and PDH CSV if present in same folder
        LOG_INFO << "Detected JSON file -> treating as Diagnostics submission to /pb/submit (+attachments if found)";
        QVariant diagData = loadJsonFile(currentFile, error);
        if (!error.isEmpty()) {
            LOG_ERROR << "Diagnostics JSON load failed: " << error.toStdString();
            finalizeSingleFile(currentFile, false, error);
            return;
        }

        // Ensure protobuf serializer and submit to diagnostics endpoint
        setSerializer(std::make_shared<ProtobufSerializer>());
        // Scrub PII and add validity hash (no GDPR data version)
        QVariant sanitized = sanitizeDiagnosticsPayload(diagData);

        // Augment with optimization settings JSON and PDH CSV if available
        QVariantMap payload = sanitized.toMap();
        QFileInfo fi(currentFile);
        QDir dir = fi.dir();

        // Extract diagnostic timestamp from filename (diagnostics_YYYYMMDD_HHMMSS[_hash].json)
        auto parseTs = [](const QString& date, const QString& time) -> QDateTime {
            return QDateTime::fromString(date + time, "yyyyMMddHHmmss");
        };
        QDateTime diagTs;
        QString runToken;
        {
            QString base = fi.baseName();
            QStringList parts = base.split('_');
            if (parts.size() >= 3) {
                diagTs = parseTs(parts[1], parts[2]);
                if (parts.size() >= 4 && !parts[3].isEmpty()) {
                    runToken = QString("%1_%2_%3").arg(parts[1], parts[2], parts[3]);
                } else {
                    runToken = QString("%1_%2").arg(parts[1], parts[2]);
                }
            }
        }

        // Helper to pick the closest file (<= diagTs preferred) from a list of timestamped files
        auto pickClosestByTs = [&](const QFileInfoList& list, int dateIndex, int timeIndex) -> QFileInfo {
            if (list.isEmpty()) return QFileInfo();
            if (!diagTs.isValid()) return list.first(); // fall back to most recent
            QFileInfo best;
            qint64 bestDiff = std::numeric_limits<qint64>::max();
            bool foundEarlier = false;
            for (const QFileInfo& f : list) {
                QStringList p = f.baseName().split('_');
                if (p.size() <= std::max(dateIndex, timeIndex)) continue;
                QDateTime ts = parseTs(p.at(dateIndex), p.at(timeIndex));
                if (!ts.isValid()) continue;
                qint64 diff = ts.secsTo(diagTs);
                if (diff > 0 && diff < bestDiff) { best = f; bestDiff = diff; foundEarlier = true; }
            }
            if (!foundEarlier) return list.first();
            return best;
        };

        auto pickByRunToken = [&](const QFileInfoList& list) -> QFileInfo {
            if (runToken.isEmpty()) return QFileInfo();
            for (const QFileInfo& f : list) {
                if (f.baseName().contains(runToken)) return f;
            }
            return QFileInfo();
        };

        // Find optimization settings file in same directory
        // Prefer timestamped: optimization_settings_YYYYMMDD_HHMMSS.json (sorted by time), else fallback to optimizationsettings.json
        QString optPath;
        QFileInfoList optList = dir.entryInfoList(QStringList() << "optimization_settings_*.json", QDir::Files, QDir::Time);
        if (!optList.isEmpty()) {
            QFileInfo picked = pickByRunToken(optList);
            if (!picked.exists()) {
                picked = pickClosestByTs(optList, /*dateIndex=*/2, /*timeIndex=*/3);
            }
            optPath = picked.absoluteFilePath();
        } else {
            QString fallback = dir.absoluteFilePath("optimizationsettings.json");
            if (QFileInfo::exists(fallback)) optPath = fallback;
        }
        if (!optPath.isEmpty()) {
            LOG_INFO << "Including optimization settings: " << optPath.toStdString();
            QFile f(optPath);
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray raw = f.readAll(); f.close();
                payload.insert("optimization_settings_json", QString::fromUtf8(raw));
            } else {
                LOG_WARN << "Failed to read optimization settings JSON: " << optPath.toStdString();
            }
        } else {
            LOG_INFO << "No optimization settings file found next to diagnostics";
        }

        // Find PDH CSV - accept both legacy and new naming; choose closest <= diagnostic time when possible
        QString pdhFile;
        QFileInfoList pdhList = dir.entryInfoList(QStringList() << "pdh_metrics_*.csv" << "processor_metrics_*.csv", QDir::Files, QDir::Time);
        if (!pdhList.isEmpty()) {
            // For processor_metrics_YYYYMMDD_HHMMSS.csv -> dateIndex=2, timeIndex=3
            // For pdh_metrics_YYYYMMDD_HHMMSS.csv -> dateIndex=2, timeIndex=3 as well
            QFileInfo picked = pickByRunToken(pdhList);
            if (!picked.exists()) {
                picked = pickClosestByTs(pdhList, /*dateIndex=*/2, /*timeIndex=*/3);
            }
            pdhFile = picked.absoluteFilePath();
        }
        if (!pdhFile.isEmpty()) {
            QFile f(pdhFile);
            if (f.open(QIODevice::ReadOnly)) {
                QByteArray csv = f.readAll();
                f.close();
                LOG_INFO << "Including PDH CSV metrics: " << QFileInfo(pdhFile).fileName().toStdString() << ", bytes=" << csv.size();
                payload.insert("pdh_metrics_csv", csv);
                payload.insert("pdh_metrics_filename", QFileInfo(pdhFile).fileName());
            } else {
                LOG_WARN << "Failed to read PDH CSV file: " << pdhFile.toStdString();
            }
        } else {
            LOG_INFO << "No PDH CSV metrics file found next to diagnostics";
        }

        post("/pb/submit", payload, [this, currentFile](const ApiResponse& response) {
            finalizeSingleFile(currentFile, response.success, response.error);
        }, QStringLiteral("UploadResponse"));
        return;
    }

    if (currentFile.endsWith(".csv", Qt::CaseInsensitive)) {
        // Benchmark CSV -> build BenchmarkUploadRequest via PublicExportBuilder and send through BenchmarkApiClient
        LOG_INFO << "Detected CSV file -> building BenchmarkUploadRequest (protobuf)";

        PublicExportBuilder builder;
        // Attach at minimum the original CSV; specs/optimization JSON can be added by caller later
        QStringList attachments{ currentFile };
        QVariant uploadPayload = builder.buildUploadRequestVariant(currentFile,
                                                                   /*runId*/ QString(),
                                                                   /*userSystemId*/ QString(),
                                                                   attachments);
        if (!uploadPayload.isValid()) {
            QString err = QStringLiteral("Failed to build benchmark upload payload from CSV");
            LOG_ERROR << err.toStdString();
            finalizeSingleFile(currentFile, false, err);
            return;
        }

        LOG_INFO << "Calling BenchmarkApiClient with binary protobuf payload...";
        auto* benchApi = new BenchmarkApiClient(this);
        benchApi->uploadBenchmark(uploadPayload, [this, currentFile, benchApi](bool success, const QString& err, QString runId){
            LOG_INFO << "BenchmarkApiClient upload completed for " << currentFile.toStdString()
                     << " - success: " << success << ", runId=" << runId.toStdString();
            if (!success) LOG_ERROR << "Upload error: " << err.toStdString();
            benchApi->deleteLater();
            finalizeSingleFile(currentFile, success, err);
        });
        return;
    }

    // Unsupported
    error = QString("Unsupported file format: %1").arg(fileInfo.suffix());
    LOG_ERROR << "Unsupported file format: " << fileInfo.suffix().toStdString();
    finalizeSingleFile(currentFile, false, error);
}

void UploadApiClient::finalizeSingleFile(const QString& filePath, bool success, const QString& error) {
    if (!m_uploading) {
        LOG_WARN << "finalizeSingleFile called while no batch is active";
        return;
    }

    if (success) {
        ++m_successCount;
    } else {
        ++m_failureCount;
        if (m_firstError.isEmpty()) {
            m_firstError = error;
        }
        emit uploadError(error);
    }

    ++m_completedInBatch;
    emit uploadFileFinished(filePath, success, error);
    emit uploadBatchProgress(m_completedInBatch, m_totalFilesInBatch);

    uploadNextInQueue();
}

void UploadApiClient::uploadData(const QVariant& data, UploadCallback callback) {
    if (m_uploading) {
        QString error = "Upload already in progress";
        LOG_ERROR << "Upload rejected: " << error.toStdString();
        emit uploadError(error);
        callback(false, error);
        return;
    }
    
    m_uploading = true;
    LOG_INFO << "Upload state set to true (uploadData)";
    
    uploadDataWithFormat(data, SerializationFormat::PROTOBUF, [this, callback](bool success, const QString& error) {
        m_uploading = false;
        callback(success, error);
    });
}

void UploadApiClient::uploadDataWithFormat(const QVariant& data, SerializationFormat format, 
                                          UploadCallback callback) {
    // Note: Don't check m_uploading here since uploadFiles() already handles this check and sets the flag
    
    // Always use protobuf for server communication - format parameter ignored
    // Local files remain JSON but server communication is protobuf
    setSerializer(std::make_shared<ProtobufSerializer>());
    
    post("/pb/submit", data, [this, callback](const ApiResponse& response) {
        m_uploading = false;
        handleUploadResponse(response, callback);
    }, QStringLiteral("UploadResponse"));
}

bool UploadApiClient::isUploading() const {
    return m_uploading;
}

void UploadApiClient::resetUploadState() {
    m_uploading = false;
    m_uploadQueue.clear();
    m_totalFilesInBatch = 0;
    m_completedInBatch = 0;
    m_successCount = 0;
    m_failureCount = 0;
    m_firstError.clear();
    m_batchCallback = nullptr;
}

QVariant UploadApiClient::loadJsonFile(const QString& filePath, QString& error) const {
    QFile file(filePath);
    
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("Failed to open file: %1").arg(filePath);
        return QVariant();
    }
    
    QByteArray jsonData = file.readAll();
    file.close();
    
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        error = QString("JSON parse error in file %1: %2").arg(filePath, parseError.errorString());
        return QVariant();
    }
    
    if (doc.isObject()) {
        return doc.object().toVariantMap();
    } else if (doc.isArray()) {
        return doc.array().toVariantList();
    } else {
        error = QString("Invalid JSON structure in file: %1").arg(filePath);
        return QVariant();
    }
}

QVariant UploadApiClient::loadCsvFile(const QString& filePath, QString& error) const {
    QFile file(filePath);
    
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("Failed to open file: %1").arg(filePath);
        return QVariant();
    }
    
    QByteArray csvData = file.readAll();
    file.close();
    
    CsvSerializer csvSerializer;
    DeserializationResult result = csvSerializer.deserialize(csvData);
    
    if (!result.success) {
        error = QString("CSV parse error in file %1: %2").arg(filePath, result.error);
        return QVariant();
    }
    
    return result.data;
}

void UploadApiClient::handleUploadResponse(const ApiResponse& response, UploadCallback callback) {
    if (response.success) {
        emit uploadCompleted(true);
        callback(true, QString());
    } else {
        emit uploadError(response.error);
        emit uploadCompleted(false);
        callback(false, response.error);
    }
}

void UploadApiClient::onRequestProgress(qint64 bytesSent, qint64 bytesTotal) {
    if (bytesTotal > 0) {
        int percentage = static_cast<int>((bytesSent * 100) / bytesTotal);
        emit uploadProgress(percentage);
    }
}
