#pragma once
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QList>
#include <QVector>
#include <QPushButton>
#include <QVBoxLayout>

#include "DataPreviewWindow.h"
#include "SilentNotificationBanner.h"

class BenchmarkUploadDialog : public QDialog {
  Q_OBJECT

 public:
  explicit BenchmarkUploadDialog(QWidget* parent = nullptr);

 private slots:
  void onSelectClicked();
  void onUploadProgress(qint64 bytesSent, qint64 bytesTotal);
  void onUploadCompleted(const QString& path, bool success);
  void onUploadStarted(const QString& path);

 private:
  struct RunUploadBundle {
    QString csvPath;
    QStringList attachments;
    QDateTime timestamp;
  };

  void loadBenchmarkRuns();
  QStringList getSelectedFilePaths() const;
  void performUpload(const QStringList& filePaths);
  QVector<RunUploadBundle> collectSelectedRuns(bool includeDebugData) const;
  void uploadNextRun();
  void handleRunFinished(const RunUploadBundle& run, bool success, const QString& error, const QString& runId);
  void resetUploadState();

  // Helper method to find the closest debug file before the benchmark time
  QString findClosestDebugFile(const QFileInfoList& files,
                               const QDateTime& benchmarkTime) const;

  QListWidget* benchmarkList;
  QPushButton* selectButton;
  QPushButton* cancelButton;
  QCheckBox* previewDataCheckbox;
  QCheckBox* includeDebugDataCheckbox;
  
  SilentNotificationBanner* notificationBanner;
  
  // Track upload state
  bool m_uploadInProgress;
  QList<RunUploadBundle> m_pendingRuns;
  int m_totalRuns = 0;
  int m_completedRuns = 0;
  int m_successfulRuns = 0;
  int m_failedRuns = 0;
};
