#ifndef UPLOADAPICLIENT_H
#define UPLOADAPICLIENT_H

// UploadApiClient - File and data upload API client
// Used by: UI components (DiagnosticUploadDialog, BenchmarkUploadDialog)
// Purpose: Upload benchmark JSON/CSV files and data to server with progress tracking
// When to use: For uploading benchmark results - call directly from UI components
// Operations: File uploads, data serialization, server ping, progress tracking, format conversion

#include "BaseApiClient.h"
#include <QStringList>
#include <functional>

using PingCallback = std::function<void(bool success, const QString& error)>;
using UploadCallback = std::function<void(bool success, const QString& error)>;

class UploadApiClient : public BaseApiClient {
    Q_OBJECT

public:
    explicit UploadApiClient(QObject* parent = nullptr);
    
    void pingServer(PingCallback callback);
    void uploadFiles(const QStringList& filePaths, UploadCallback callback);
    void uploadData(const QVariant& data, UploadCallback callback);
    void uploadDataWithFormat(const QVariant& data, SerializationFormat format, UploadCallback callback);
    
    bool isUploading() const;
    void resetUploadState();

signals:
    void pingCompleted(bool success);
    void uploadProgress(int percentage);
    void uploadCompleted(bool success);
    void uploadError(const QString& errorMessage);
    void uploadBatchStarted(int totalFiles);
    void uploadBatchProgress(int completedFiles, int totalFiles);
    void uploadBatchFinished(int successCount, int failureCount);
    void uploadFileStarted(const QString& filePath);
    void uploadFileFinished(const QString& filePath, bool success, const QString& errorMessage);

private:
    bool m_uploading;
    QStringList m_uploadQueue;
    UploadCallback m_batchCallback;
    int m_totalFilesInBatch = 0;
    int m_completedInBatch = 0;
    int m_successCount = 0;
    int m_failureCount = 0;
    QString m_firstError;
    
    QVariant loadJsonFile(const QString& filePath, QString& error) const;
    QVariant loadCsvFile(const QString& filePath, QString& error) const;
    void handleUploadResponse(const ApiResponse& response, UploadCallback callback);
    void uploadNextInQueue();
    void finalizeSingleFile(const QString& filePath, bool success, const QString& error);
    
private slots:
    void onRequestProgress(qint64 bytesSent, qint64 bytesTotal);
};

#endif // UPLOADAPICLIENT_H
