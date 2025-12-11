#pragma once
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "DataPreviewWindow.h"
#include "SilentNotificationBanner.h"
#include "../network/api/UploadApiClient.h"

class DiagnosticUploadDialog : public QDialog {
  Q_OBJECT

 public:
  explicit DiagnosticUploadDialog(QWidget* parent = nullptr);

 private slots:
  void onUploadClicked();
  void onUploadProgress(int percentage);
  void onUploadCompleted(bool success);
  void onUploadError(const QString& errorMessage);
  void onBatchStarted(int totalFiles);
  void onBatchProgress(int completedFiles, int totalFiles);
  void onBatchFinished(int successCount, int failureCount);
  void onFileFinished(const QString& filePath, bool success, const QString& errorMessage);

 private:
  void loadDiagnosticRuns();
  QStringList getSelectedFilePaths() const;
  void performUpload(const QStringList& filePaths);

  // Helper method to find the closest processor metrics file before the
  // diagnostic time
  QString findClosestProcessorMetricsFile(
    const QFileInfoList& files, const QDateTime& diagnosticTime) const;

  QListWidget* diagnosticList;
  QPushButton* uploadButton;
  QPushButton* cancelButton;
  QCheckBox* previewDataCheckbox;
  QCheckBox* includeDebugDataCheckbox;
  
  SilentNotificationBanner* notificationBanner;
  UploadApiClient* uploadApiClient;
  int m_totalUploads = 0;
  int m_completedUploads = 0;
  int m_successfulUploads = 0;
  int m_failedUploads = 0;
};
