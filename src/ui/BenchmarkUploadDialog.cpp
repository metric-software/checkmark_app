#include "BenchmarkUploadDialog.h"

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
#include "../network/api/BenchmarkApiClient.h"
#include "../network/serialization/PublicExportBuilder.h"
#include "../logging/Logger.h"
#include "../ApplicationSettings.h"

BenchmarkUploadDialog::BenchmarkUploadDialog(QWidget* parent)
    : QDialog(parent), m_uploadInProgress(false) {
  setWindowTitle("Upload Benchmark Data");
  setMinimumWidth(400);
  setMinimumHeight(300);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Add notification banner at the top
  notificationBanner = new SilentNotificationBanner(this);
  mainLayout->addWidget(notificationBanner);

  // Add list widget
  benchmarkList = new QListWidget(this);
  benchmarkList->setSelectionMode(QAbstractItemView::NoSelection);
  mainLayout->addWidget(benchmarkList);

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
  selectButton = new QPushButton("Select", this);
  cancelButton = new QPushButton("Cancel", this);

  // Style buttons
  selectButton->setStyleSheet(R"(
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
  controlsLayout->addWidget(selectButton);

  mainLayout->addLayout(controlsLayout);

  // Connect signals
  connect(selectButton, &QPushButton::clicked, this,
          &BenchmarkUploadDialog::onSelectClicked);
  connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

  // Load benchmark runs
  loadBenchmarkRuns();

  if (ApplicationSettings::getInstance().isOfflineModeEnabled()) {
    notificationBanner->showNotification("Offline Mode is enabled. Uploading is disabled.",
                                         SilentNotificationBanner::Error);
    selectButton->setEnabled(false);
  } else if (!ApplicationSettings::getInstance().getAllowDataCollection()) {
    notificationBanner->showNotification("Allow data collection is disabled. Uploading is disabled.",
                                         SilentNotificationBanner::Error);
    selectButton->setEnabled(false);
  }
  
  LOG_INFO << "BenchmarkUploadDialog initialized";
}

void BenchmarkUploadDialog::loadBenchmarkRuns() {
  QString resultsPath = qApp->applicationDirPath() + "/benchmark_results";
  LOG_INFO << "Looking for benchmark results in: " << resultsPath.toStdString();
  QDir dir(resultsPath);

  if (!dir.exists()) {
    LOG_WARN << "Benchmark results directory does not exist: " << resultsPath.toStdString();
    QListWidgetItem* noFilesItem =
      new QListWidgetItem("No benchmark results found.");
    noFilesItem->setFlags(Qt::ItemIsEnabled);
    benchmarkList->addItem(noFilesItem);
    selectButton->setEnabled(false);
    return;
  }

  QStringList filters;
  filters << "*.csv";
  QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

  if (files.isEmpty()) {
    LOG_WARN << "No CSV files found in benchmark results directory";
    QListWidgetItem* noFilesItem =
      new QListWidgetItem("No benchmark results found.");
    noFilesItem->setFlags(Qt::ItemIsEnabled);
    benchmarkList->addItem(noFilesItem);
    selectButton->setEnabled(false);
    return;
  }
  
  LOG_INFO << "Found " << files.size() << " benchmark CSV files";

  for (const QFileInfo& file : files) {
    QString timestamp = file.baseName().section('_', 0, 2);

    QListWidgetItem* item =
      new QListWidgetItem(QString("Benchmark run - %1").arg(timestamp));
    item->setData(Qt::UserRole,
                  file.absoluteFilePath());  // Store the full file path
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    benchmarkList->addItem(item);
  }
}

QStringList BenchmarkUploadDialog::getSelectedFilePaths() const {
  QStringList selectedFiles;
  const bool includeDebugData = includeDebugDataCheckbox->isChecked();
  QVector<RunUploadBundle> runs = collectSelectedRuns(includeDebugData);
  for (const auto& run : runs) {
    for (const auto& path : run.attachments) {
      if (!selectedFiles.contains(path)) {
        selectedFiles.append(path);
      }
    }
  }
  return selectedFiles;
}

QVector<BenchmarkUploadDialog::RunUploadBundle> BenchmarkUploadDialog::collectSelectedRuns(bool includeDebugData) const {
  QVector<RunUploadBundle> runs;
  QString resultsPath = qApp->applicationDirPath() + "/benchmark_results";
  QDir resultsDir(resultsPath);

  if (!resultsDir.exists()) {
    LOG_WARN << "Benchmark results directory missing while collecting uploads: " << resultsPath.toStdString();
    return runs;
  }

  // Preload optimization settings and debug file lists for better matching
  QFileInfoList optFiles = resultsDir.entryInfoList(QStringList() << "optimization_settings_*.json",
                                                    QDir::Files, QDir::Time);
  QString optFallback = resultsDir.absoluteFilePath("optimizationsettings.json");

  QString debugLogPath = qApp->applicationDirPath() + "/debug logging";
  QDir debugLogDir(debugLogPath);
  QFileInfoList metricsFiles;
  QFileInfoList logFiles;
  if (includeDebugData && debugLogDir.exists()) {
    metricsFiles = debugLogDir.entryInfoList(QStringList() << "raw_metrics_*.txt", QDir::Files, QDir::Time);
    logFiles = debugLogDir.entryInfoList(QStringList() << "log_*.txt", QDir::Files, QDir::Time);
  }

  auto parseTs = [](const QString& date, const QString& time) -> QDateTime {
    return QDateTime::fromString(date + time, "yyyyMMddHHmmss");
  };

  auto pickClosestByTs = [&](const QFileInfoList& list, const QDateTime& target, int dateIndex, int timeIndex) -> QString {
    if (list.isEmpty()) return QString();
    if (!target.isValid()) return list.first().absoluteFilePath();
    QString bestPath;
    qint64 bestDiff = std::numeric_limits<qint64>::max();
    bool foundEarlier = false;
    for (const QFileInfo& f : list) {
      QStringList parts = f.baseName().split('_');
      if (parts.size() <= std::max(dateIndex, timeIndex)) continue;
      QDateTime ts = parseTs(parts.at(dateIndex), parts.at(timeIndex));
      if (!ts.isValid()) continue;
      qint64 diff = ts.secsTo(target);
      if (diff > 0 && diff < bestDiff) {
        bestDiff = diff;
        bestPath = f.absoluteFilePath();
        foundEarlier = true;
      }
    }
    return foundEarlier ? bestPath : list.first().absoluteFilePath();
  };

  for (int i = 0; i < benchmarkList->count(); ++i) {
    QListWidgetItem* item = benchmarkList->item(i);
    if (item->checkState() != Qt::Checked) continue;

    QString csvPath = item->data(Qt::UserRole).toString();
    QFileInfo csvInfo(csvPath);
    if (!csvInfo.exists()) {
      LOG_WARN << "Selected CSV does not exist: " << csvPath.toStdString();
      continue;
    }

    RunUploadBundle run;
    run.csvPath = csvPath;
    run.timestamp = QDateTime();

    QStringList parts = csvInfo.baseName().split('_');
    if (parts.size() >= 3) {
      run.timestamp = parseTs(parts.at(1), parts.at(2));
    }

    run.attachments.append(csvPath);

    QString uniqueId;
    if (parts.size() >= 4) {
      uniqueId = parts.at(3);
    } else if (parts.size() >= 3) {
      uniqueId = parts.at(2);
    }

    if (!uniqueId.isEmpty()) {
      // Run-specific JSON
      QStringList jsonFilters;
      jsonFilters << QString("*_%1.json").arg(uniqueId);
      QFileInfoList jsonFiles = resultsDir.entryInfoList(jsonFilters, QDir::Files);
      if (!jsonFiles.isEmpty()) {
        QString path = jsonFiles.first().absoluteFilePath();
        if (!run.attachments.contains(path)) run.attachments.append(path);
      } else {
        LOG_INFO << "No matching JSON found for run id " << uniqueId.toStdString();
      }

      // Run-specific specs
      QStringList specFilters;
      specFilters << QString("*_%1_specs.txt").arg(uniqueId)
                  << QString("*_%1_specs.json").arg(uniqueId);
      QFileInfoList specFiles = resultsDir.entryInfoList(specFilters, QDir::Files);
      if (!specFiles.isEmpty()) {
        QString path = specFiles.first().absoluteFilePath();
        if (!run.attachments.contains(path)) run.attachments.append(path);
      } else {
        LOG_INFO << "No specs file found for run id " << uniqueId.toStdString();
      }
    }

    // Optimization settings (shared)
    QString optPath;
    if (!optFiles.isEmpty()) {
      optPath = pickClosestByTs(optFiles, run.timestamp, /*dateIndex=*/2, /*timeIndex=*/3);
    } else if (QFileInfo::exists(optFallback)) {
      optPath = optFallback;
    }
    if (!optPath.isEmpty() && !run.attachments.contains(optPath)) {
      run.attachments.append(optPath);
    }

    // Debug files (closest to run timestamp)
    if (includeDebugData) {
      QString metricsPath = findClosestDebugFile(metricsFiles, run.timestamp);
      if (!metricsPath.isEmpty() && !run.attachments.contains(metricsPath)) {
        run.attachments.append(metricsPath);
      }

      QString logPath = findClosestDebugFile(logFiles, run.timestamp);
      if (!logPath.isEmpty() && !run.attachments.contains(logPath)) {
        run.attachments.append(logPath);
      }
    }

    runs.append(run);
  }

  return runs;
}

QString BenchmarkUploadDialog::findClosestDebugFile(
  const QFileInfoList& files, const QDateTime& benchmarkTime) const {
  if (files.isEmpty()) {
    return QString();
  }

  if (!benchmarkTime.isValid()) {
    return files.first().absoluteFilePath();
  }

  QString closest;
  qint64 bestDiff = std::numeric_limits<qint64>::max();
  bool foundEarlier = false;

  for (const QFileInfo& file : files) {
    QStringList parts = file.baseName().split('_');
    if (parts.size() < 3) continue;

    QString dateStr = parts.at(parts.size() - 2);
    QString timeStr = parts.at(parts.size() - 1);
    QDateTime ts = QDateTime::fromString(dateStr + timeStr, "yyyyMMddHHmmss");
    if (!ts.isValid()) continue;

    qint64 diff = ts.secsTo(benchmarkTime);
    if (diff >= 0 && diff < bestDiff) {
      bestDiff = diff;
      closest = file.absoluteFilePath();
      foundEarlier = true;
    }
  }

  if (foundEarlier && !closest.isEmpty()) {
    return closest;
  }

  return files.first().absoluteFilePath();
}

void BenchmarkUploadDialog::onSelectClicked() {
  LOG_INFO << "BenchmarkUploadDialog::onSelectClicked() - user clicked Select button";
  
  if (m_uploadInProgress) {
    LOG_WARN << "Upload already in progress, ignoring select click";
    notificationBanner->showNotification("An upload is already in progress.", SilentNotificationBanner::Info);
    return;
  }
  
  QStringList selectedFiles = getSelectedFilePaths();
  LOG_INFO << "Selected " << selectedFiles.size() << " files for upload";
  
  for (const QString& file : selectedFiles) {
    LOG_INFO << "Selected file: " << file.toStdString();
  }

  if (selectedFiles.isEmpty()) {
    LOG_WARN << "No files selected for upload";
    notificationBanner->showNotification("Please select at least one benchmark to upload.", SilentNotificationBanner::Warning);
    return;
  }

  if (previewDataCheckbox->isChecked()) {
    LOG_INFO << "Preview mode enabled - showing DataPreviewWindow";
    // Show preview window
    DataPreviewWindow previewWindow(this);

    // Add selected files for preview
    for (const QString& filePath : selectedFiles) {
      previewWindow.addFile(filePath);
    }

    // Show the preview window
    if (previewWindow.exec() == QDialog::Accepted) {
      LOG_INFO << "User accepted preview window - performing upload";
      performUpload(selectedFiles);
    } else {
      LOG_INFO << "User cancelled preview window";
    }
  } else {
    LOG_INFO << "Preview disabled - performing direct upload";
    performUpload(selectedFiles);
  }
}

void BenchmarkUploadDialog::performUpload(const QStringList& selectedFiles) {
  LOG_INFO << "BenchmarkUploadDialog::performUpload starting with " << selectedFiles.size() << " files";
  
  if (m_uploadInProgress) {
    LOG_WARN << "Upload already in progress";
    notificationBanner->showNotification("An upload is already in progress.", SilentNotificationBanner::Info);
    return;
  }
  
  try {
    QVector<RunUploadBundle> runs = collectSelectedRuns(includeDebugDataCheckbox->isChecked());
    if (runs.isEmpty()) {
      LOG_ERROR << "No benchmark runs to upload after filtering selection";
      notificationBanner->showNotification("No benchmark runs found to upload.", SilentNotificationBanner::Warning);
      return;
    }
    
    m_uploadInProgress = true;
    m_pendingRuns.clear();
    for (const auto& run : runs) {
      m_pendingRuns.append(run);
    }
    m_totalRuns = m_pendingRuns.size();
    m_completedRuns = 0;
    m_successfulRuns = 0;
    m_failedRuns = 0;
    
    selectButton->setEnabled(false);
    selectButton->setText(QString("Uploading... 0/%1").arg(m_totalRuns));
    
    LOG_INFO << "Queueing " << m_totalRuns << " benchmark run(s) for upload";
    uploadNextRun();
    
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception during upload flow: " << e.what();
    resetUploadState();
    notificationBanner->showNotification(QString("Exception: %1").arg(e.what()), SilentNotificationBanner::Error);
  }
}

void BenchmarkUploadDialog::uploadNextRun() {
  if (m_pendingRuns.isEmpty()) {
    int total = m_totalRuns;
    int successes = m_successfulRuns;
    int failures = m_failedRuns;

    resetUploadState();

    if (total == 0) {
      return;
    }

    if (failures == 0) {
      notificationBanner->showNotification(QString("%1 benchmark run(s) uploaded successfully.")
                                           .arg(successes),
                                         SilentNotificationBanner::Success);
      QTimer::singleShot(2000, this, &QDialog::accept);
    } else {
      notificationBanner->showNotification(QString("Uploaded %1/%2 benchmark runs. %3 failed.")
                                           .arg(successes)
                                           .arg(total)
                                           .arg(failures),
                                         SilentNotificationBanner::Warning);
    }
    return;
  }

  RunUploadBundle run = m_pendingRuns.takeFirst();
  int currentIndex = std::min(m_completedRuns + 1, m_totalRuns);
  selectButton->setText(QString("Uploading... %1/%2").arg(currentIndex).arg(m_totalRuns));

  LOG_INFO << "Building upload request for run " << currentIndex << "/" << m_totalRuns
           << ": " << run.csvPath.toStdString();

  PublicExportBuilder builder;
  QVariant uploadReq = builder.buildUploadRequestVariant(run.csvPath, QString(), QString(), run.attachments);

  if (!uploadReq.isValid()) {
    QString err = QStringLiteral("Failed to build upload request");
    LOG_ERROR << err.toStdString();
    handleRunFinished(run, false, err, QString());
    return;
  }

  auto* api = new BenchmarkApiClient(this);
  connect(api, &BaseApiClient::requestStarted, this, &BenchmarkUploadDialog::onUploadStarted);
  connect(api, &BaseApiClient::requestCompleted, this, &BenchmarkUploadDialog::onUploadCompleted);
  connect(api, &BaseApiClient::requestProgress, this, &BenchmarkUploadDialog::onUploadProgress);

  api->uploadBenchmark(uploadReq, [this, api, run](bool success, const QString& err, QString runId){
    LOG_INFO << "Upload callback received - success: " << success << ", runId: " << runId.toStdString();
    api->deleteLater();
    handleRunFinished(run, success, err, runId);
  });
}

void BenchmarkUploadDialog::handleRunFinished(const RunUploadBundle& run, bool success, const QString& error, const QString& runId) {
  if (success) {
    ++m_successfulRuns;
    LOG_INFO << "Upload succeeded for " << run.csvPath.toStdString() << ", runId=" << runId.toStdString();
  } else {
    ++m_failedRuns;
    QString errMsg = error.isEmpty() ? QStringLiteral("Unknown error") : error;
    LOG_ERROR << "Upload failed for " << run.csvPath.toStdString() << " : " << errMsg.toStdString();
    notificationBanner->showNotification(QString("Upload failed for %1: %2")
                                         .arg(QFileInfo(run.csvPath).fileName())
                                         .arg(errMsg),
                                       SilentNotificationBanner::Warning);
  }

  ++m_completedRuns;
  uploadNextRun();
}

void BenchmarkUploadDialog::resetUploadState() {
  LOG_INFO << "Resetting upload state";
  m_uploadInProgress = false;
  m_pendingRuns.clear();
  m_totalRuns = 0;
  m_completedRuns = 0;
  m_successfulRuns = 0;
  m_failedRuns = 0;
  selectButton->setEnabled(true);
  selectButton->setText("Select");
}

void BenchmarkUploadDialog::onUploadStarted(const QString& path) {
  LOG_INFO << "Upload request started to path: " << path.toStdString();
}

void BenchmarkUploadDialog::onUploadProgress(qint64 bytesSent, qint64 bytesTotal) {
  if (bytesTotal > 0) {
    int percentage = static_cast<int>((bytesSent * 100) / bytesTotal);
    int currentIndex = (m_totalRuns > 0) ? std::min(m_completedRuns + 1, m_totalRuns) : 1;
    LOG_INFO << "Upload progress: " << bytesSent << "/" << bytesTotal << " (" << percentage << "%)";
    if (m_totalRuns > 0) {
      selectButton->setText(QString("Uploading %1/%2... %3%").arg(currentIndex).arg(m_totalRuns).arg(percentage));
    } else {
      selectButton->setText(QString("Uploading... %1%").arg(percentage));
    }
  } else {
    LOG_INFO << "Upload progress: " << bytesSent << " bytes sent (total unknown)";
    if (m_totalRuns > 0) {
      selectButton->setText(QString("Uploading %1/%2...").arg(std::min(m_completedRuns + 1, m_totalRuns)).arg(m_totalRuns));
    } else {
      selectButton->setText("Uploading...");
    }
  }
}

void BenchmarkUploadDialog::onUploadCompleted(const QString& path, bool success) {
  LOG_INFO << "Upload request completed to path: " << path.toStdString() << ", success: " << success;
}
