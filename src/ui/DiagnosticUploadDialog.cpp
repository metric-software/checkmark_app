#include "DiagnosticUploadDialog.h"

#include <algorithm>
#include <iostream>
#include <limits>
#include "../logging/Logger.h"

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QTimer>

#include "DataPreviewWindow.h"
#include "SilentNotificationBanner.h"
#include "../network/api/UploadApiClient.h"

DiagnosticUploadDialog::DiagnosticUploadDialog(QWidget* parent)
    : QDialog(parent), uploadApiClient(nullptr) {
  setWindowTitle("Upload Diagnostic Data");
  setMinimumWidth(500);
  setMinimumHeight(350);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Add notification banner at the top
  notificationBanner = new SilentNotificationBanner(this);
  mainLayout->addWidget(notificationBanner);

  // Add header label
  QLabel* headerLabel =
    new QLabel("Select diagnostic results to upload:", this);
  headerLabel->setStyleSheet("font-size: 14px; margin-bottom: 10px;");
  mainLayout->addWidget(headerLabel);

  // Add list widget
  diagnosticList = new QListWidget(this);
  diagnosticList->setSelectionMode(QAbstractItemView::NoSelection);
  mainLayout->addWidget(diagnosticList);

  // Add description text
  QLabel* descriptionLabel =
    new QLabel("Uploaded diagnostic data helps us improve the application and "
               "provide better recommendations. "
               "All personal information is anonymized before upload.",
               this);
  descriptionLabel->setWordWrap(true);
  descriptionLabel->setStyleSheet(
    "color: #888888; font-style: italic; margin-top: 5px;");
  mainLayout->addWidget(descriptionLabel);

  // Create bottom controls layout (checkboxes + button on the same line)
  QHBoxLayout* controlsLayout = new QHBoxLayout();

  // Add checkboxes with standard style
  previewDataCheckbox = new QCheckBox("Preview data", this);
  includeDebugDataCheckbox = new QCheckBox("Include debug data", this);

  // Set default states
  previewDataCheckbox->setChecked(true);
  includeDebugDataCheckbox->setChecked(false);

  // Apply standard checkbox style
  QString checkboxStyle = R"(
        QCheckBox {
            color: #ffffff;
            spacing: 3px;
            padding: 2px 4px;
            background: transparent;
            margin-right: 3px;
            border-radius: 3px;
            font-size: 12px;
        }
        QCheckBox::indicator {
            width: 10px;
            height: 10px;
        }
        QCheckBox::indicator:unchecked {
            border: 1px solid #666666;
            background: #1e1e1e;
        }
        QCheckBox::indicator:checked {
            border: 1px solid #0078d4;
            background: #0078d4;
        }
    )";

  previewDataCheckbox->setStyleSheet(checkboxStyle);
  includeDebugDataCheckbox->setStyleSheet(checkboxStyle);

  // Create buttons
  uploadButton = new QPushButton("Upload Selected", this);
  cancelButton = new QPushButton("Cancel", this);

  // Style buttons
  uploadButton->setStyleSheet(R"(
        QPushButton {
            background-color: #0078d4;
            color: white;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
        }
        QPushButton:hover { background-color: #1084d8; }
        QPushButton:pressed { background-color: #006cc1; }
    )");

  cancelButton->setStyleSheet(R"(
        QPushButton {
            background-color: #333333;
            color: white;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
        }
        QPushButton:hover { background-color: #404040; }
        QPushButton:pressed { background-color: #292929; }
    )");

  // Add controls to the layout (checkboxes and buttons on the same line)
  controlsLayout->addWidget(previewDataCheckbox);
  controlsLayout->addWidget(includeDebugDataCheckbox);
  controlsLayout->addStretch();
  controlsLayout->addWidget(cancelButton);
  controlsLayout->addWidget(uploadButton);

  mainLayout->addLayout(controlsLayout);

  // Connect signals
  connect(uploadButton, &QPushButton::clicked, this,
          &DiagnosticUploadDialog::onUploadClicked);
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

  // Initialize upload API client
  uploadApiClient = new UploadApiClient(this);
  connect(uploadApiClient, &UploadApiClient::uploadProgress, this, &DiagnosticUploadDialog::onUploadProgress);
  connect(uploadApiClient, &UploadApiClient::uploadCompleted, this, &DiagnosticUploadDialog::onUploadCompleted);
  connect(uploadApiClient, &UploadApiClient::uploadError, this, &DiagnosticUploadDialog::onUploadError);
  connect(uploadApiClient, &UploadApiClient::uploadBatchStarted, this, &DiagnosticUploadDialog::onBatchStarted);
  connect(uploadApiClient, &UploadApiClient::uploadBatchProgress, this, &DiagnosticUploadDialog::onBatchProgress);
  connect(uploadApiClient, &UploadApiClient::uploadBatchFinished, this, &DiagnosticUploadDialog::onBatchFinished);
  connect(uploadApiClient, &UploadApiClient::uploadFileFinished, this, &DiagnosticUploadDialog::onFileFinished);

  // Load diagnostic runs
  loadDiagnosticRuns();
}

void DiagnosticUploadDialog::loadDiagnosticRuns() {
  QString resultsPath = qApp->applicationDirPath() + "/diagnostic_results";
  QDir dir(resultsPath);

  if (!dir.exists()) {
    QListWidgetItem* noFilesItem =
      new QListWidgetItem("No diagnostic results found.");
    noFilesItem->setFlags(Qt::ItemIsEnabled);
    diagnosticList->addItem(noFilesItem);
    uploadButton->setEnabled(false);
    return;
  }

  // We build combined entries per run: JSON + optimization settings + PDH CSV
  QStringList filters;
  filters << "diagnostics_*.json";
  QFileInfoList jsonFiles = dir.entryInfoList(filters, QDir::Files, QDir::Time);

  if (jsonFiles.isEmpty()) {
    QListWidgetItem* noFilesItem =
      new QListWidgetItem("No diagnostic results found.");
    noFilesItem->setFlags(Qt::ItemIsEnabled);
    diagnosticList->addItem(noFilesItem);
    uploadButton->setEnabled(false);
    return;
  }

  // Helper for parsing timestamp
  auto parseTs = [](const QString& date, const QString& time) -> QDateTime {
    return QDateTime::fromString(date + time, "yyyyMMddHHmmss");
  };

  // Collect PDH metrics and optimization files once to speed matching
  QFileInfoList pdhFiles = dir.entryInfoList(QStringList() << "pdh_metrics_*.csv" << "processor_metrics_*.csv",
                                             QDir::Files, QDir::Time);
  QFileInfoList optFiles = dir.entryInfoList(QStringList() << "optimization_settings_*.json",
                                             QDir::Files, QDir::Time);
  QString optFallback = dir.absoluteFilePath("optimizationsettings.json");

  auto findByRunToken = [](const QFileInfoList& list, const QString& token) -> QFileInfo {
    if (token.isEmpty()) return QFileInfo();
    for (const QFileInfo& f : list) {
      if (f.baseName().contains(token)) return f;
    }
    return QFileInfo();
  };

  auto pickClosestByTs = [&](const QFileInfoList& list, const QDateTime& target, int dateIndex, int timeIndex) -> QFileInfo {
    if (list.isEmpty()) return QFileInfo();
    if (!target.isValid()) return list.first();
    QFileInfo best; qint64 bestDiff = std::numeric_limits<qint64>::max(); bool foundEarlier=false;
    for (const QFileInfo& f : list) {
      QStringList p = f.baseName().split('_');
      if (p.size() <= std::max(dateIndex, timeIndex)) continue;
      QDateTime ts = parseTs(p.at(dateIndex), p.at(timeIndex));
      if (!ts.isValid()) continue;
      qint64 diff = ts.secsTo(target);
      if (diff > 0 && diff < bestDiff) { best=f; bestDiff=diff; foundEarlier=true; }
    }
    return foundEarlier ? best : list.first();
  };

  int added = 0;
  for (const QFileInfo& file : jsonFiles) {
    // Parse timestamp from json
    QStringList parts = file.baseName().split('_');
    if (parts.size() < 3) continue;
    QDateTime diagTs = parseTs(parts[1], parts[2]);
    QString runToken =
      (parts.size() >= 4 && !parts[3].isEmpty())
        ? QString("%1_%2_%3").arg(parts[1], parts[2], parts[3])
        : QString("%1_%2").arg(parts[1], parts[2]);

    // Resolve optimization settings
    QString optPath;
    if (!optFiles.isEmpty()) {
      QFileInfo of = findByRunToken(optFiles, runToken);
      if (!of.exists()) {
        of = pickClosestByTs(optFiles, diagTs, /*dateIndex=*/2, /*timeIndex=*/3);
      }
      optPath = of.absoluteFilePath();
    } else if (QFileInfo::exists(optFallback)) {
      optPath = optFallback;
    }

    // Resolve PDH CSV
    QString pdhPath;
    if (!pdhFiles.isEmpty()) {
      QFileInfo pf = findByRunToken(pdhFiles, runToken);
      if (!pf.exists()) {
        pf = pickClosestByTs(pdhFiles, diagTs, /*dateIndex=*/2, /*timeIndex=*/3);
      }
      pdhPath = pf.absoluteFilePath();
    }

    // Only add entries that have all 3 components
    if (optPath.isEmpty() || pdhPath.isEmpty()) {
      LOG_WARN << "Skipping diagnostic JSON without full attachments: " << file.fileName().toStdString();
      continue;
    }

    QString displayToken = runToken.isEmpty() ? QString("%1_%2").arg(parts[1], parts[2]) : runToken;
    QString displayName = QString("Diagnostic run - %1 (3 files)").arg(displayToken);

    QListWidgetItem* item = new QListWidgetItem(displayName);
    // Store the primary JSON path in UserRole for upload, and a map for potential previews
    QVariantMap bundle; bundle.insert("json", file.absoluteFilePath());
    bundle.insert("opt", optPath); bundle.insert("pdh", pdhPath);
    item->setData(Qt::UserRole, bundle);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    diagnosticList->addItem(item);
    ++added;
  }

  if (added == 0) {
    QListWidgetItem* noFilesItem =
      new QListWidgetItem("No complete diagnostic runs found.");
    noFilesItem->setFlags(Qt::ItemIsEnabled);
    diagnosticList->addItem(noFilesItem);
    uploadButton->setEnabled(false);
    return;
  }

  // Select the most recent file by default
  if (diagnosticList->count() > 0) {
    diagnosticList->item(0)->setCheckState(Qt::Checked);
  }
}

QStringList DiagnosticUploadDialog::getSelectedFilePaths() const {
  QStringList selectedFiles;

  // Get debug logging directory
  QString debugLogPath = qApp->applicationDirPath() + "/debug logging";
  QDir debugLogDir(debugLogPath);
  bool includeDebugData = includeDebugDataCheckbox->isChecked();

  // Collect primary JSON files for checked bundles; attachments will be auto-included by UploadApiClient
  for (int i = 0; i < diagnosticList->count(); ++i) {
    QListWidgetItem* item = diagnosticList->item(i);
    if (item->checkState() == Qt::Checked) {
      QVariantMap bundle = item->data(Qt::UserRole).toMap();
      QString jsonPath = bundle.value("json").toString();
      if (!jsonPath.isEmpty()) selectedFiles.append(jsonPath);
    }
  }

  if (selectedFiles.isEmpty()) {
    return selectedFiles;
  }

  // Now handle debug files (just one of each) if requested
  if (includeDebugData && debugLogDir.exists()) {
    QString selectedMetricsFile;
    QString selectedLogFile;

    // Get the most recent log file
    QFileInfoList logFiles = debugLogDir.entryInfoList(
      QStringList() << "log_*.txt", QDir::Files, QDir::Time);

    if (!logFiles.isEmpty()) {
      selectedLogFile = logFiles.first().absoluteFilePath();
      LOG_INFO << "Selected log file: "
                << logFiles.first().fileName().toStdString();
    } else {
      LOG_WARN << "No log files found";
    }

    // Get the most recent metrics file
    QFileInfoList metricsFiles = debugLogDir.entryInfoList(
      QStringList() << "raw_metrics_*.txt", QDir::Files, QDir::Time);

    if (!metricsFiles.isEmpty()) {
      selectedMetricsFile = metricsFiles.first().absoluteFilePath();
      LOG_INFO << "Selected metrics file: "
                << metricsFiles.first().fileName().toStdString();
    } else {
      LOG_WARN << "No metrics files found";
    }

    // Add the selected debug files to our list
    if (!selectedMetricsFile.isEmpty()) {
      selectedFiles.append(selectedMetricsFile);
      LOG_INFO << "Added metrics file to upload list";
    }

    if (!selectedLogFile.isEmpty()) {
      selectedFiles.append(selectedLogFile);
      LOG_INFO << "Added log file to upload list";
    }
  }

  return selectedFiles;
}

QString DiagnosticUploadDialog::findClosestProcessorMetricsFile(
  const QFileInfoList& files, const QDateTime& diagnosticTime) const {
  if (files.isEmpty()) {
    return QString();
  }

  // Find the closest file before the diagnostic time, or any file if none match
  QString closestFile;
  qint64 closestTimeDiff = std::numeric_limits<qint64>::max();
  bool foundEarlierFile = false;

  for (const QFileInfo& file : files) {
    // Extract timestamp from filename (format:
    // processor_metrics_20250422_114130.csv)
    QString filename = file.fileName();
    QStringList parts = filename.split('_');

    if (parts.size() >= 3) {
      QString dateStr = parts[2];  // 20250422
      QString timeStr = parts[3];  // 114130.csv - need to remove .csv
      timeStr = timeStr.split('.').first();

      // Create datetime object from the extracted parts
      QDateTime fileTime =
        QDateTime::fromString(dateStr + timeStr, "yyyyMMddHHmmss");

      if (fileTime.isValid()) {
        // Calculate time difference in seconds
        qint64 timeDiff = fileTime.secsTo(diagnosticTime);

        // Check if file is before diagnostic and closer than any found so far
        if (timeDiff > 0 && timeDiff < closestTimeDiff) {
          closestTimeDiff = timeDiff;
          closestFile = file.absoluteFilePath();
          foundEarlierFile = true;
        }
      }
    }
  }

  // If no files before the diagnostic were found, use any file as backup
  if (!foundEarlierFile && !files.isEmpty()) {
    closestFile = files.first().absoluteFilePath();
    LOG_WARN << "No processor metrics files found before diagnostic time, "
                 "using most recent: "
              << files.first().fileName().toStdString();
  }

  return closestFile;
}

void DiagnosticUploadDialog::onUploadClicked() {
  QStringList selectedFiles = getSelectedFilePaths();

  if (selectedFiles.isEmpty()) {
    notificationBanner->showNotification(
      "Please select at least one diagnostic result to upload.", 
      SilentNotificationBanner::Warning);
    return;
  }

  if (previewDataCheckbox->isChecked()) {
    // Show preview window
    DataPreviewWindow previewWindow(this);

    // Add selected files for preview
    for (const QString& filePath : selectedFiles) {
      previewWindow.addFile(filePath);
    }

    // Show the preview window
    if (previewWindow.exec() == QDialog::Accepted) {
      // User confirmed upload in preview window, proceed with actual upload
      performUpload(selectedFiles);
    }
  } else {
    // No preview requested, upload directly
    performUpload(selectedFiles);
  }
}

void DiagnosticUploadDialog::performUpload(const QStringList& filePaths) {
  if (uploadApiClient->isUploading()) {
    notificationBanner->showNotification("An upload is already in progress.", 
                                       SilentNotificationBanner::Info);
    return;
  }

  // Additional guard: check if button is already disabled (prevents double-click)
  if (!uploadButton->isEnabled()) {
    LOG_WARN << "Upload already in progress (button disabled)";
    return;
  }

  // Filter files to only include diagnostic JSON files (exclude CSV processor metrics)
  QStringList jsonDiagnosticFiles;
  for (const QString& file : filePaths) {
    if (!file.endsWith(".json", Qt::CaseInsensitive)) {
      LOG_INFO << "Excluding from upload (not JSON): " << file.toStdString();
      continue;
    }
    QString baseName = QFileInfo(file).baseName().toLower();
    if (baseName.startsWith("diagnostics_") || baseName.startsWith("benchmark_")) {
      jsonDiagnosticFiles.append(file);
      LOG_INFO << "Including for upload: " << file.toStdString();
    } else {
      LOG_INFO << "Excluding from upload (unrecognized prefix): " << file.toStdString();
    }
  }
  
  if (jsonDiagnosticFiles.isEmpty()) {
    notificationBanner->showNotification("No diagnostic JSON files found to upload.", 
                                       SilentNotificationBanner::Warning);
    return;
  }

  // Disable upload button and update text
  uploadButton->setEnabled(false);
  uploadButton->setText("Uploading...");
  
  LOG_INFO << "Starting upload of " << jsonDiagnosticFiles.size() << " diagnostic JSON files";
  
  // Start upload directly (skip ping since menu request already confirmed server is up)
  uploadApiClient->uploadFiles(jsonDiagnosticFiles, [](bool success, const QString& error) {
    // Upload callback is handled by the connected signals
  });
}


void DiagnosticUploadDialog::onUploadProgress(int percentage) {
  int currentIndex = (m_totalUploads > 0) ? std::min(m_completedUploads + 1, m_totalUploads) : 1;
  if (m_totalUploads > 0) {
    uploadButton->setText(QString("Uploading %1/%2... %3%")
                          .arg(currentIndex)
                          .arg(m_totalUploads)
                          .arg(percentage));
  } else {
    uploadButton->setText(QString("Uploading... %1%").arg(percentage));
  }
}

void DiagnosticUploadDialog::onUploadCompleted(bool success) {
  uploadButton->setEnabled(true);
  uploadButton->setText("Upload Selected");
  
  if (m_totalUploads > 0) {
    // Batch flow handles notifications in onBatchFinished; just reset counters here.
    m_totalUploads = 0;
    m_completedUploads = 0;
    m_successfulUploads = 0;
    m_failedUploads = 0;
    return;
  }
  
  // Legacy single-upload flow
  if (success) {
    notificationBanner->showNotification("Diagnostic data has been uploaded successfully.", 
                                       SilentNotificationBanner::Success);
    LOG_INFO << "Diagnostic upload completed successfully";
    QTimer::singleShot(2000, this, &QDialog::accept);
  } else {
    notificationBanner->showNotification("Failed to upload diagnostic data. Please try again.", 
                                       SilentNotificationBanner::Error);
    LOG_ERROR << "Diagnostic upload failed";
  }
}

void DiagnosticUploadDialog::onUploadError(const QString& errorMessage) {
  notificationBanner->showNotification(QString("Upload failed: %1").arg(errorMessage), 
                                     SilentNotificationBanner::Error);
  LOG_ERROR << "Diagnostic upload error: " << errorMessage.toStdString();
}

void DiagnosticUploadDialog::onBatchStarted(int totalFiles) {
  m_totalUploads = totalFiles;
  m_completedUploads = 0;
  m_successfulUploads = 0;
  m_failedUploads = 0;
  
  uploadButton->setEnabled(false);
  if (totalFiles > 1) {
    uploadButton->setText(QString("Uploading... 0/%1").arg(totalFiles));
  } else {
    uploadButton->setText("Uploading...");
  }
  
  notificationBanner->showNotification(QString("Uploading %1 diagnostic file(s)...").arg(totalFiles), 
                                     SilentNotificationBanner::Info);
}

void DiagnosticUploadDialog::onBatchProgress(int completedFiles, int totalFiles) {
  m_completedUploads = completedFiles;
  m_totalUploads = totalFiles;
  uploadButton->setText(QString("Uploading... %1/%2").arg(completedFiles).arg(totalFiles));
}

void DiagnosticUploadDialog::onBatchFinished(int successCount, int failureCount) {
  m_successfulUploads = successCount;
  m_failedUploads = failureCount;
  int total = successCount + failureCount;
  
  uploadButton->setEnabled(true);
  uploadButton->setText("Upload Selected");
  
  if (failureCount == 0) {
    notificationBanner->showNotification(QString("%1 diagnostic file(s) uploaded successfully.")
                                         .arg(successCount),
                                       SilentNotificationBanner::Success);
    LOG_INFO << "Diagnostic upload batch completed successfully (" << successCount << " files)";
    QTimer::singleShot(2000, this, &QDialog::accept);
  } else {
    notificationBanner->showNotification(QString("Uploaded %1/%2 diagnostic files. %3 failed.")
                                         .arg(successCount)
                                         .arg(total)
                                         .arg(failureCount),
                                       SilentNotificationBanner::Warning);
    LOG_WARN << "Diagnostic upload batch completed with failures. success=" << successCount 
             << ", failure=" << failureCount;
  }
}

void DiagnosticUploadDialog::onFileFinished(const QString& filePath, bool success, const QString& errorMessage) {
  if (success) {
    ++m_successfulUploads;
    LOG_INFO << "Finished uploading diagnostic file: " << filePath.toStdString();
  } else {
    ++m_failedUploads;
    LOG_ERROR << "Diagnostic upload failed for " << filePath.toStdString() 
              << " : " << errorMessage.toStdString();
  }
}
