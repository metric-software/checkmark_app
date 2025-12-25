#include "DriveResultRenderer.h"

#include <algorithm>  // For std::min
#include <iostream>

#include <QApplication>  // Add for applicationDirPath
#include <QDir>          // Add for directory operations
#include <QFile>         // Add for file operations
#include <QHeaderView>
#include <QJsonArray>     // Add for JSON arrays
#include <QJsonDocument>  // Add for JSON parsing
#include <QJsonObject>    // Add for JSON objects
#include <QPushButton>
#include <QRegularExpression>  // Add for regex
#include <QVBoxLayout>

#include "DiagnosticViewComponents.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"
#include "logging/Logger.h"

namespace DiagnosticRenderers {

QWidget* DriveResultRenderer::createDriveResultWidget(const QString& /*result*/, const MenuData* networkMenuData, DownloadApiClient* downloadClient) {
  LOG_INFO << "DriveResultRenderer: Creating drive result widget with network support";
  
  // Get data from DiagnosticDataStore
  auto& dataStore = DiagnosticDataStore::getInstance();
  const auto& driveData = dataStore.getDriveData();

  // Get constant system information
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();

  // Load all comparison data first to determine global max values across all drives
  std::map<QString, DriveComparisonData> allComparisonData;
  if (networkMenuData && !networkMenuData->availableDrives.isEmpty()) {
    LOG_INFO << "DriveResultRenderer: Using network menu data";
    allComparisonData = createDropdownDataFromMenu(*networkMenuData);
  } else {
    LOG_INFO << "DriveResultRenderer: Falling back to local file data";
    allComparisonData = loadDriveComparisonData();
  }

  if (downloadClient) {
    DriveComparisonData general{};
    general.model = DownloadApiClient::generalAverageLabel();
    general.driveType = "";
    general.readSpeedMBs = 0;
    general.writeSpeedMBs = 0;
    general.iops4k = 0;
    general.accessTimeMs = 0;
    allComparisonData[DownloadApiClient::generalAverageLabel()] = general;
  }

  // Variables to track maximum values - include both user drives and comparison
  // drives
  double maxReadSpeed = 0.0;
  double maxWriteSpeed = 0.0;
  double maxIops = 0.0;
  double maxAccessTime = 0.0;

  // Find maximum values across all user drives
  for (const auto& drive : driveData.drives) {
    maxReadSpeed = qMax(maxReadSpeed, drive.seqRead);
    maxWriteSpeed = qMax(maxWriteSpeed, drive.seqWrite);
    maxIops = qMax(maxIops, drive.iops4k);
    maxAccessTime = qMax(maxAccessTime, drive.accessTimeMs);
  }

  // Compare with all comparison drives to ensure global maximums
  for (const auto& [_, driveCompData] : allComparisonData) {
    maxReadSpeed = std::max(maxReadSpeed, driveCompData.readSpeedMBs);
    maxWriteSpeed = std::max(maxWriteSpeed, driveCompData.writeSpeedMBs);
    maxIops = std::max(maxIops, driveCompData.iops4k);
    maxAccessTime = std::max(maxAccessTime, driveCompData.accessTimeMs);
  }

  // Use a consistent scaling factor - 80% instead of 90% to match the
  // comparison bars
  double scaledMaxReadSpeed =
    (maxReadSpeed > 0.1) ? (maxReadSpeed * 1.25) : 100.0;  // 1/0.8 = 1.25
  double scaledMaxWriteSpeed =
    (maxWriteSpeed > 0.1) ? (maxWriteSpeed * 1.25) : 100.0;
  double scaledMaxIops = (maxIops > 0.1) ? (maxIops * 1.25) : 1000.0;
  double scaledMaxAccessTime = (maxAccessTime > 0.1)
                                 ? (maxAccessTime * 1.25)
                                 : 0.1;  // Reduced from 1.0 to 0.1

  // Create the main container widget
  QWidget* containerWidget = new QWidget();
  containerWidget->setSizePolicy(QSizePolicy::Preferred,
                                 QSizePolicy::Preferred);

  QVBoxLayout* mainLayout = new QVBoxLayout(containerWidget);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(10);

  // Create widgets for each drive using the same global maximum values
  for (const auto& drive : driveData.drives) {
    QWidget* driveWidget = processDriveData(
      drive, constantInfo, scaledMaxReadSpeed, scaledMaxWriteSpeed,
      scaledMaxIops, scaledMaxAccessTime, allComparisonData, downloadClient);
    mainLayout->addWidget(driveWidget);
  }

  return containerWidget;
}

QWidget* DriveResultRenderer::processDriveData(
  const DiagnosticDataStore::DriveData::DriveMetrics& drive,
  const SystemMetrics::ConstantSystemInfo& constantInfo, double maxReadSpeed,
  double maxWriteSpeed, double maxIops, double maxAccessTime,
  const std::map<QString, DriveComparisonData>& comparisonData,
  DownloadApiClient* downloadClient) {

  // Create a widget for this drive with metrics and performance bars
  QWidget* driveMetricsWidget = new QWidget();
  driveMetricsWidget->setStyleSheet(
    "background-color: #252525; border-radius: 4px;");
  QVBoxLayout* mainLayout = new QVBoxLayout(driveMetricsWidget);
  mainLayout->setContentsMargins(12, 4, 12, 4);  // Match CPU renderer margins
  mainLayout->setSpacing(10);

  // Get additional drive information from constant info if available
  QString driveModel = "Unknown";
  bool isSSD = false;
  bool isSystemDrive = false;

  for (const auto& constDrive : constantInfo.drives) {
    if (QString::fromStdString(constDrive.path) ==
        QString::fromStdString(drive.drivePath)) {
      driveModel = QString::fromStdString(constDrive.model);
      isSSD = constDrive.isSSD;
      isSystemDrive = constDrive.isSystemDrive;
      break;
    }
  }

  // Create title for this drive with model info if available
  QString titleText =
    "<b>Drive: " + QString::fromStdString(drive.drivePath) + "</b>";
  if (driveModel != "Unknown") {
    titleText += " - " + driveModel;
  }
  if (isSystemDrive) {
    titleText += " (System Drive)";
  }

  QLabel* driveTitle = new QLabel(titleText);
  driveTitle->setStyleSheet("color: #ffffff; font-size: 14px; background: "
                            "transparent; margin-bottom: 5px;");
  driveTitle->setContentsMargins(0, 0, 0, 0);
  mainLayout->addWidget(driveTitle);

  // Create title with dropdown section
  QWidget* titleWidget = new QWidget();
  QHBoxLayout* titleLayout = new QHBoxLayout(titleWidget);
  titleLayout->setContentsMargins(0, 10, 0, 0);

  QLabel* performanceTitle = new QLabel("<b>Drive Performance</b>");
  performanceTitle->setStyleSheet(
    "color: #ffffff; font-size: 14px; background: transparent;");
  titleLayout->addWidget(performanceTitle);

  titleLayout->addStretch(1);

  // Store value pairs (current, max) for updating later - use the global
  // maximum values
  QPair<double, double> readSpeedVals(drive.seqRead, maxReadSpeed);
  QPair<double, double> writeSpeedVals(drive.seqWrite, maxWriteSpeed);
  QPair<double, double> iopsVals(drive.iops4k, maxIops);
  QPair<double, double> accessTimeVals(drive.accessTimeMs, maxAccessTime);

  // Create and add the dropdown
  QComboBox* dropdown = createDriveComparisonDropdown(
    comparisonData, driveMetricsWidget, readSpeedVals, writeSpeedVals, iopsVals,
    accessTimeVals, downloadClient);
  dropdown->setObjectName("drive_comparison_dropdown");
  if (downloadClient) {
    const int idx = dropdown->findText(DownloadApiClient::generalAverageLabel());
    if (idx > 0) dropdown->setCurrentIndex(idx);
  }

  titleLayout->addWidget(dropdown);
  mainLayout->addWidget(titleWidget);

  // Create performance metric box
  QWidget* performanceBox = new QWidget();
  performanceBox->setStyleSheet("background-color: #252525;");
  QVBoxLayout* performanceLayout = new QVBoxLayout(performanceBox);
  performanceLayout->setContentsMargins(8, 12, 8, 12);
  performanceLayout->setSpacing(6);

  // Create custom labels for each test with the drive name
  QString driveName = driveModel;
  if (driveName == "Unknown") {
    driveName = QString::fromStdString(drive.drivePath);
  } else {
    driveName += " (" + QString::fromStdString(drive.drivePath) + ")";
  }

  // Use the unscaled max values - createComparisonPerformanceBar will apply the
  // same 1.25 scaling factor we've already applied to maxReadSpeed etc.
  QWidget* readBar = DiagnosticViewComponents::createComparisonPerformanceBar(
    "Read Speed", drive.seqRead, 0, maxReadSpeed, "MB/s", false);
  QWidget* writeBar = DiagnosticViewComponents::createComparisonPerformanceBar(
    "Write Speed", drive.seqWrite, 0, maxWriteSpeed, "MB/s", false);
  QWidget* iopsBar = DiagnosticViewComponents::createComparisonPerformanceBar(
    "4K IOPS", drive.iops4k, 0, maxIops, "", false);

  // Find and set object names for the comparison bars
  QWidget* innerReadBar = readBar->findChild<QWidget*>("comparison_bar");
  if (innerReadBar) innerReadBar->setObjectName("comparison_bar_read");

  QWidget* innerWriteBar = writeBar->findChild<QWidget*>("comparison_bar");
  if (innerWriteBar) innerWriteBar->setObjectName("comparison_bar_write");

  QWidget* innerIopsBar = iopsBar->findChild<QWidget*>("comparison_bar");
  if (innerIopsBar) innerIopsBar->setObjectName("comparison_bar_iops");

  // Find and update the user name labels to show drive information instead of
  // CPU
  QLabel* readUserNameLabel = readBar->findChild<QLabel*>("userNameLabel");
  if (readUserNameLabel) readUserNameLabel->setText(driveName);

  QLabel* writeUserNameLabel = writeBar->findChild<QLabel*>("userNameLabel");
  if (writeUserNameLabel) writeUserNameLabel->setText(driveName);

  QLabel* iopsUserNameLabel = iopsBar->findChild<QLabel*>("userNameLabel");
  if (iopsUserNameLabel) iopsUserNameLabel->setText(driveName);

  performanceLayout->addWidget(readBar);
  performanceLayout->addWidget(writeBar);
  performanceLayout->addWidget(iopsBar);

  // Add access time - for access time, lower is better
  if (drive.accessTimeMs > 0.0) {
    QWidget* accessBar =
      DiagnosticViewComponents::createComparisonPerformanceBar(
        "Access Time", drive.accessTimeMs, 0, maxAccessTime, "ms", true);

    QWidget* innerAccessBar = accessBar->findChild<QWidget*>("comparison_bar");
    if (innerAccessBar) innerAccessBar->setObjectName("comparison_bar_access");

    // Update label for access time too
    QLabel* accessUserNameLabel =
      accessBar->findChild<QLabel*>("userNameLabel");
    if (accessUserNameLabel) accessUserNameLabel->setText(driveName);

    performanceLayout->addWidget(accessBar);
  }

  // Add the performance box to main layout
  mainLayout->addWidget(performanceBox);

  return driveMetricsWidget;
}

QWidget* DriveResultRenderer::createDriveMetricBox(const QString& title,
                                                   const QString& value,
                                                   const QString& color) {
  QWidget* box = new QWidget();
  box->setStyleSheet(R"(
        QWidget {
            background-color: #252525;
            border-radius: 4px;
        }
    )");

  QVBoxLayout* layout = new QVBoxLayout(box);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(4);

  QLabel* titleLabel = new QLabel(title, box);
  titleLabel->setStyleSheet("color: #0078d4; font-size: 12px; font-weight: "
                            "bold; background: transparent;");
  layout->addWidget(titleLabel);

  QLabel* valueLabel = new QLabel(
    QString(
      "<span style='color: %1; font-size: 18px; font-weight: bold;'>%2</span>")
      .arg(color)
      .arg(value),
    box);
  valueLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(valueLabel);

  return box;
}

QWidget* DriveResultRenderer::createPerformanceBar(const QString& label,
                                                   double value,
                                                   double maxValue,
                                                   const QString& unit,
                                                   bool higherIsBetter) {
  QWidget* container = new QWidget();
  QVBoxLayout* mainLayout = new QVBoxLayout(container);
  mainLayout->setContentsMargins(0, 1, 0, 1);
  mainLayout->setSpacing(1);

  QHBoxLayout* layout = new QHBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  // Add label at the left side of the horizontal layout
  QLabel* nameLabel = new QLabel(label);
  nameLabel->setStyleSheet(
    "color: #ffffff; background: transparent; font-weight: bold;");
  nameLabel->setFixedWidth(90);
  nameLabel->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(nameLabel);

  QWidget* barContainer = new QWidget();
  barContainer->setFixedHeight(20);
  barContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  barContainer->setStyleSheet("background-color: #333333; border-radius: 2px;");

  QHBoxLayout* barLayout = new QHBoxLayout(barContainer);
  barLayout->setContentsMargins(0, 0, 0, 0);
  barLayout->setSpacing(0);

  // Calculate percentage for bar width
  double percentage;
  if (higherIsBetter) {
    // For metrics where higher is better (read/write/IOPS)
    double limitedValue = (std::min)(value, maxValue);
    percentage = (limitedValue / maxValue) * 90;
  } else {
    // For metrics where lower is better (access time)
    // Invert the scale - 0 is good (0%), max is bad (90%)
    double limitedValue = (std::min)(value, maxValue);
    percentage = (limitedValue / maxValue) * 90;
  }

  // Determine typical values based on metric type
  double typicalValue = 0.0;

  if (label.contains("Read Speed", Qt::CaseInsensitive)) {
    typicalValue = 500.0;  // Updated typical SSD read speed in MB/s
  } else if (label.contains("Write Speed", Qt::CaseInsensitive)) {
    typicalValue = 250.0;  // Updated typical SSD write speed in MB/s
  } else if (label.contains("IOPS", Qt::CaseInsensitive)) {
    typicalValue = 10000.0;  // Updated typical SSD 4K IOPS
  } else if (label.contains("Access Time", Qt::CaseInsensitive)) {
    typicalValue = 0.1;  // Updated typical SSD access time in ms
  }

  // Get appropriate color based on performance relative to typical value
  QString barColor = getColorForSpeed(value, typicalValue, higherIsBetter);

  QWidget* bar = new QWidget();
  bar->setFixedHeight(20);
  bar->setStyleSheet(
    QString("background-color: %1; border-radius: 2px;").arg(barColor));

  QWidget* spacer = new QWidget();
  spacer->setStyleSheet("background-color: transparent;");

  // Use stretch factors for correct proportion
  barLayout->addWidget(bar, static_cast<int>(percentage));
  barLayout->addWidget(spacer, 100 - static_cast<int>(percentage));

  layout->addWidget(barContainer);

  // Show the actual value with the same color and add units if provided
  QString displayValue;

  // Use higher precision for access time (4 decimal places instead of 2)
  if (label.contains("Access Time", Qt::CaseInsensitive)) {
    displayValue = QString("%1").arg(value, 0, 'f', 4);
  } else {
    displayValue = QString("%1").arg(value, 0, 'f', 1);
  }

  if (!unit.isEmpty()) {
    displayValue += " " + unit;
  }

  QLabel* valueLabel = new QLabel(displayValue);
  valueLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  valueLabel->setStyleSheet(
    QString("color: %1; background: transparent;").arg(barColor));
  layout->addWidget(valueLabel);

  // Add typical value reference based on the metric
  QString typicalValueStr;
  if (label.contains("Read Speed", Qt::CaseInsensitive)) {
    typicalValueStr = "500 MB/s";  // Updated typical SSD read
  } else if (label.contains("Write Speed", Qt::CaseInsensitive)) {
    typicalValueStr = "250 MB/s";  // Updated typical SSD write
  } else if (label.contains("IOPS", Qt::CaseInsensitive)) {
    typicalValueStr = "10000";  // Updated typical SSD 4K IOPS
  } else if (label.contains("Access Time", Qt::CaseInsensitive)) {
    typicalValueStr = "0.10 ms";  // Updated typical SSD access time
  }

  QLabel* typicalLabel =
    new QLabel(QString("(typical: %1)").arg(typicalValueStr));
  typicalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  typicalLabel->setStyleSheet(
    "color: #888888; font-size: 10px; background: transparent;");
  layout->addWidget(typicalLabel);

  mainLayout->addLayout(layout);
  return container;
}

QString DriveResultRenderer::getColorForSpeed(double value, double typicalValue,
                                              bool higherIsBetter) {
  // If no typical value provided, return standard blue
  if (typicalValue <= 0.0) {
    return "#0078d4";  // Standard blue color
  }

  // Calculate ratio of value to typical value
  double ratio = value / typicalValue;

  // For metrics where lower is better, invert the ratio
  if (!higherIsBetter) {
    ratio = 1.0 / ratio;
  }

  // Create a continuous color scale based on the performance ratio
  int hue, sat = 240, val = 245;  // Base HSV values

  if (ratio >= 1.3) {
    // Excellent performance - pure green (ratio 1.3 or higher)
    hue = 120;
  } else if (ratio <= 0.7) {
    // Poor performance - pure red (ratio 0.7 or lower)
    hue = 0;
  } else {
    // Scale the hue linearly between red and green
    // Map ratio from [0.7, 1.3] to [0, 120]
    double normalizedRatio = (ratio - 0.7) / 0.6;  // 1.3 - 0.7 = 0.6
    hue = static_cast<int>(120 * normalizedRatio);
  }

  // Create the color using HSV
  QColor hsv = QColor::fromHsv(hue, sat, val);
  return hsv.name();
}

std::map<QString, DriveComparisonData> DriveResultRenderer::
  loadDriveComparisonData() {
  std::map<QString, DriveComparisonData> comparisonData;

  // Find the comparison_data folder
  QString appDir = QApplication::applicationDirPath();
  QDir dataDir(appDir + "/comparison_data");

  if (!dataDir.exists()) {
    return comparisonData;
  }

  // Look for drive benchmark JSON files
  QStringList filters;
  filters << "drive_benchmark_*.json";
  dataDir.setNameFilters(filters);

  QStringList driveFiles = dataDir.entryList(QDir::Files);

  for (const QString& fileName : driveFiles) {
    QFile file(dataDir.absoluteFilePath(fileName));

    if (file.open(QIODevice::ReadOnly)) {
      QByteArray jsonData = file.readAll();
      QJsonDocument doc = QJsonDocument::fromJson(jsonData);

      if (doc.isObject()) {
        QJsonObject rootObj = doc.object();

        DriveComparisonData drive;
        drive.model = rootObj["model"].toString();
        drive.driveType = rootObj["type"].toString();

        // Get benchmark results
        if (rootObj.contains("benchmark_results") &&
            rootObj["benchmark_results"].isObject()) {
          QJsonObject resultsObj = rootObj["benchmark_results"].toObject();
          drive.readSpeedMBs = resultsObj["read_speed_mb_s"].toDouble();
          drive.writeSpeedMBs = resultsObj["write_speed_mb_s"].toDouble();
          drive.iops4k = resultsObj["iops_4k"].toDouble();
          drive.accessTimeMs = resultsObj["access_time_ms"].toDouble();
        }

        // Use model as the display name as specified
        QString displayName = drive.model;
        if (displayName.isEmpty()) {
          // If model is missing, use system_id or name
          displayName = rootObj["system_id"].toString();
          if (displayName.isEmpty()) {
            displayName = rootObj["name"].toString();
          }
        }

        // Add drive type if available
        if (!drive.driveType.isEmpty() && drive.driveType != "Unknown") {
          displayName += " (" + drive.driveType + ")";
        }

        comparisonData[displayName] = drive;
      }

      file.close();
    }
  }

  return comparisonData;
}

// Add this function implementation after loadDriveComparisonData
std::map<QString, DiagnosticViewComponents::AggregatedComponentData<DriveComparisonData>> DriveResultRenderer::
  generateAggregatedDriveData(
    const std::map<QString, DriveComparisonData>& individualData) {
  std::map<QString, DiagnosticViewComponents::AggregatedComponentData<
                      DriveComparisonData>>
    result;

  // Group results by drive model, removing any extra descriptors
  std::map<QString, std::vector<std::pair<QString, DriveComparisonData>>>
    groupedData;

  for (const auto& [id, data] : individualData) {
    // Extract the primary model name (before "with" or other qualifiers)
    QString baseModel = data.model;

    // Remove any text after "with" if present
    int withPos = baseModel.indexOf(" with ");
    if (withPos > 0) {
      baseModel = baseModel.left(withPos);
    }

    // Remove any text in parentheses
    QRegularExpression parensRegex("\\s*\\([^)]*\\)");
    baseModel = baseModel.replace(parensRegex, "");

    // Trim any extra whitespace
    baseModel = baseModel.trimmed();

    // Add to the corresponding group
    groupedData[baseModel].push_back({id, data});
  }

  // Create aggregated data for each drive model
  for (const auto& [modelName, dataList] : groupedData) {
    DiagnosticViewComponents::AggregatedComponentData<DriveComparisonData>
      aggregated;
    aggregated.componentName = modelName;
    
    // Store the original full name from the first entry (for API requests)
    if (!dataList.empty()) {
      aggregated.originalFullName = dataList[0].second.model;
    }

    // Start with the first entry as both best and average
    if (!dataList.empty()) {
      const auto& firstData = dataList[0].second;
      aggregated.bestResult = firstData;
      aggregated.averageResult = firstData;

      // Add all individual results
      for (const auto& [id, data] : dataList) {
        aggregated.individualResults[id] = data;
      }

      // For higher-is-better metrics, find maximum values
      double maxReadSpeed = firstData.readSpeedMBs;
      double maxWriteSpeed = firstData.writeSpeedMBs;
      double maxIops = firstData.iops4k;

      // For lower-is-better metrics, find minimum values
      double minAccessTime = firstData.accessTimeMs;

      // Initialize sums for averages
      double sumReadSpeed = firstData.readSpeedMBs;
      double sumWriteSpeed = firstData.writeSpeedMBs;
      double sumIops = firstData.iops4k;
      double sumAccessTime = firstData.accessTimeMs;

      // Process all entries after the first
      for (size_t i = 1; i < dataList.size(); i++) {
        const auto& data = dataList[i].second;

        // Update maximums for higher-is-better metrics
        if (data.readSpeedMBs > 0) {
          maxReadSpeed = std::max(maxReadSpeed, data.readSpeedMBs);
          sumReadSpeed += data.readSpeedMBs;
        }

        if (data.writeSpeedMBs > 0) {
          maxWriteSpeed = std::max(maxWriteSpeed, data.writeSpeedMBs);
          sumWriteSpeed += data.writeSpeedMBs;
        }

        if (data.iops4k > 0) {
          maxIops = std::max(maxIops, data.iops4k);
          sumIops += data.iops4k;
        }

        // Update minimums for lower-is-better metrics
        if (data.accessTimeMs > 0) {
          minAccessTime = std::min(minAccessTime, data.accessTimeMs);
          sumAccessTime += data.accessTimeMs;
        }
      }

      // Set the best result values
      aggregated.bestResult.readSpeedMBs = maxReadSpeed;
      aggregated.bestResult.writeSpeedMBs = maxWriteSpeed;
      aggregated.bestResult.iops4k = maxIops;
      aggregated.bestResult.accessTimeMs = minAccessTime;

      // Calculate averages
      size_t count = dataList.size();
      aggregated.averageResult.readSpeedMBs = sumReadSpeed / count;
      aggregated.averageResult.writeSpeedMBs = sumWriteSpeed / count;
      aggregated.averageResult.iops4k = sumIops / count;
      aggregated.averageResult.accessTimeMs = sumAccessTime / count;

      // Copy model and drive type from first result (these should be the same
      // for all runs)
      aggregated.bestResult.model = modelName;
      aggregated.bestResult.driveType = firstData.driveType;

      aggregated.averageResult.model = modelName;
      aggregated.averageResult.driveType = firstData.driveType;
    }

    // Add to the result map
    result[modelName] = aggregated;
  }

  return result;
}

// Replace the existing createDriveComparisonDropdown function with this updated
// version
QComboBox* DriveResultRenderer::createDriveComparisonDropdown(
  const std::map<QString, DriveComparisonData>& comparisonData,
  QWidget* containerWidget, const QPair<double, double>& readSpeedVals,
  const QPair<double, double>& writeSpeedVals,
  const QPair<double, double>& iopsVals,
  const QPair<double, double>& accessTimeVals,
  DownloadApiClient* downloadClient) {

  // Generate aggregated data from individual results
  auto aggregatedData = generateAggregatedDriveData(comparisonData);

  const QPair<double, double> readSpeedValsCopy = readSpeedVals;
  const QPair<double, double> writeSpeedValsCopy = writeSpeedVals;
  const QPair<double, double> iopsValsCopy = iopsVals;
  const QPair<double, double> accessTimeValsCopy = accessTimeVals;

  struct TestMetric {
    QString objectName;
    double userValue;
    double compValue;
    QString unit;
    bool lowerIsBetter;
  };

  auto updateUserBarLayout = [](QWidget* parentContainer, int percentage) {
    QWidget* userBarContainer =
      parentContainer ? parentContainer->findChild<QWidget*>("userBarContainer") : nullptr;
    if (!userBarContainer) {
      return;
    }

    QHBoxLayout* userBarLayout =
      userBarContainer->findChild<QHBoxLayout*>("user_bar_layout");
    if (!userBarLayout) {
      return;
    }

    QWidget* userBar = userBarContainer->findChild<QWidget*>("user_bar_fill");
    QWidget* userSpacer =
      userBarContainer->findChild<QWidget*>("user_bar_spacer");
    if (!userBar || !userSpacer) {
      return;
    }

    const int barIndex = userBarLayout->indexOf(userBar);
    const int spacerIndex = userBarLayout->indexOf(userSpacer);
    if (barIndex >= 0) {
      userBarLayout->setStretch(barIndex, percentage);
    }
    if (spacerIndex >= 0) {
      userBarLayout->setStretch(spacerIndex, 100 - percentage);
    }
  };

  auto makeDisplayName = [](const QString& componentName,
                            DiagnosticViewComponents::AggregationType type,
                            const QString& driveType, bool hasSelection) {
    if (!hasSelection) {
      return QString("Select drive to compare");
    }

    QString name = (componentName == DownloadApiClient::generalAverageLabel())
                     ? componentName
                     : componentName + " (" +
                         (type == DiagnosticViewComponents::AggregationType::Best
                            ? "Best)"
                            : "Avg)");

    if (!driveType.isEmpty() && driveType != "Unknown") {
      name += " (" + driveType + ")";
    }

    return name;
  };

  auto updateDriveBars =
    [containerWidget, readSpeedValsCopy, writeSpeedValsCopy, iopsValsCopy,
     accessTimeValsCopy, updateUserBarLayout](
      const DriveComparisonData* compData, const QString& displayName,
      bool hasSelection) {
      std::vector<TestMetric> tests = {
        {"comparison_bar_read", readSpeedValsCopy.first,
         compData ? compData->readSpeedMBs : 0.0, "MB/s", false},
        {"comparison_bar_write", writeSpeedValsCopy.first,
         compData ? compData->writeSpeedMBs : 0.0, "MB/s", false},
        {"comparison_bar_iops", iopsValsCopy.first,
         compData ? compData->iops4k : 0.0, "IOPS", false},
        {"comparison_bar_access", accessTimeValsCopy.first,
         compData ? compData->accessTimeMs : 0.0, "ms", true}};

      QHash<QString, TestMetric> testMap;
      for (const auto& test : tests) {
        testMap.insert(test.objectName, test);
      }

      QList<QWidget*> allBars = containerWidget->findChildren<QWidget*>(
        QRegularExpression("^comparison_bar_"));

      for (QWidget* bar : allBars) {
        auto it = testMap.find(bar->objectName());
        if (it == testMap.end()) {
          continue;
        }

        const TestMetric test = it.value();
        const double maxValue =
          std::max(test.userValue, test.compValue);
        const double scaledMax = maxValue > 0 ? maxValue * 1.25 : 0.0;
        const int userPercentage =
          (test.userValue > 0 && scaledMax > 0)
            ? static_cast<int>(
                std::min(100.0, (test.userValue / scaledMax) * 100.0))
            : 0;

        QWidget* parentContainer = bar->parentWidget();
        if (!parentContainer) {
          continue;
        }

        QLabel* nameLabel =
          parentContainer->findChild<QLabel*>("comp_name_label");
        if (nameLabel) {
          nameLabel->setText(displayName);
          nameLabel->setStyleSheet(
            hasSelection
              ? "color: #ffffff; background: transparent;"
              : "color: #888888; font-style: italic; background: transparent;");
        }

        updateUserBarLayout(parentContainer, userPercentage);

        QLabel* valueLabel = parentContainer->findChild<QLabel*>("value_label");
        QLayout* layout = bar->layout();
        if (layout) {
          QLayoutItem* child;
          while ((child = layout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
          }

          if (!hasSelection || test.compValue <= 0) {
            QWidget* emptyBar = new QWidget();
            emptyBar->setStyleSheet("background-color: transparent;");
            QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
            if (newLayout) {
              newLayout->addWidget(emptyBar);
            }
          } else {
            const int compPercentage =
              (scaledMax > 0)
                ? static_cast<int>(std::min(
                    100.0, (test.compValue / scaledMax) * 100.0))
                : 0;

            QWidget* barWidget = new QWidget();
            barWidget->setFixedHeight(16);
            barWidget->setStyleSheet(
              "background-color: #FF4444; border-radius: 2px;");

            QWidget* spacer = new QWidget();
            spacer->setStyleSheet("background-color: transparent;");

            QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
            if (newLayout) {
              newLayout->addWidget(barWidget, compPercentage);
              newLayout->addWidget(spacer, 100 - compPercentage);
            }
          }
        }

        if (valueLabel) {
          if (!hasSelection || test.compValue <= 0) {
            valueLabel->setText("-");
            valueLabel->setStyleSheet(
              "color: #888888; font-style: italic; background: transparent;");
          } else {
            const int decimals =
              test.objectName == "comparison_bar_access" ? 4 : 1;
            valueLabel->setText(
              QString("%1 %2").arg(test.compValue, 0, 'f', decimals).arg(test.unit));
            valueLabel->setStyleSheet(
              "color: #FF4444; background: transparent;");
          }
        }

        QWidget* containerWithBars = parentContainer;
        QWidget* userBarContainer =
          containerWithBars ? containerWithBars->findChild<QWidget*>("userBarContainer") : nullptr;
        QLabel* percentageLabel =
          containerWithBars ? containerWithBars->findChild<QLabel*>("percentageLabel") : nullptr;
        if (!userBarContainer || !percentageLabel) {
          continue;
        }

        if (!hasSelection || test.compValue <= 0 || test.userValue <= 0) {
          percentageLabel->setText("-");
          percentageLabel->setStyleSheet(
            "color: #888888; font-style: italic; background: transparent;");
        } else {
          const double percentChange =
            ((test.userValue / test.compValue) - 1.0) * 100.0;

          QString percentText =
            QString("%1%2%")
              .arg(percentChange > 0 ? "+" : "")
              .arg(percentChange, 0, 'f', 1);
          const bool isBetter =
            (test.lowerIsBetter && percentChange < 0) ||
            (!test.lowerIsBetter && percentChange > 0);
          QString percentColor = isBetter ? "#44FF44" : "#FF4444";

          percentageLabel->setText(percentText);
          percentageLabel->setStyleSheet(
            QString(
              "color: %1; background: transparent; font-weight: bold;")
              .arg(percentColor));
        }
      }
    };

  // Create a callback function to handle selection changes
  auto selectionCallback = [downloadClient, makeDisplayName, updateDriveBars](
                             const QString& componentName,
                             const QString& originalFullName,
                             DiagnosticViewComponents::AggregationType type,
                             const DriveComparisonData& driveData) {
    // If downloadClient is available and driveData has no performance data (only name), 
    // fetch the actual data from the server
    if (downloadClient && !componentName.isEmpty() && driveData.readSpeedMBs <= 0) {
      LOG_INFO << "DriveResultRenderer: Fetching network data for Drive: " << componentName.toStdString() << " using original name: " << originalFullName.toStdString();
      
      downloadClient->fetchComponentData("drive", originalFullName, 
        [componentName, type, makeDisplayName, updateDriveBars]
        (bool success, const ComponentData& networkData, const QString& error) {
          
          if (success) {
            LOG_INFO << "DriveResultRenderer: Successfully fetched Drive data for " << componentName.toStdString();
            
            // Convert network data to DriveComparisonData
            DriveComparisonData fetchedDriveData = convertNetworkDataToDrive(networkData);
            
            const QString displayName = makeDisplayName(
              componentName, type, fetchedDriveData.driveType, true);
            updateDriveBars(&fetchedDriveData, displayName, true);
          } else {
            LOG_ERROR << "DriveResultRenderer: Failed to fetch Drive data for " << componentName.toStdString() 
                      << ": " << error.toStdString();
            // Continue with empty/placeholder data
          }
        });
      
      return; // Exit early - the network callback will handle the UI update
    }

    const bool hasSelection = !componentName.isEmpty();
    const QString displayName = makeDisplayName(
      componentName, type, hasSelection ? driveData.driveType : QString(),
      hasSelection);

    updateDriveBars(hasSelection ? &driveData : nullptr, displayName,
                    hasSelection);
  };

  // Use the shared helper to create the dropdown
  return DiagnosticViewComponents::createAggregatedComparisonDropdown<
    DriveComparisonData>(aggregatedData, selectionCallback);
}

// Network-based method to convert ComponentData to DriveComparisonData
DriveComparisonData DriveResultRenderer::convertNetworkDataToDrive(const ComponentData& networkData) {
  DriveComparisonData drive;
  
  LOG_INFO << "DriveResultRenderer: Converting network data to drive comparison data";
  
  // Log the full JSON as plain text for debugging
  QJsonDocument doc(networkData.testData);
  QString jsonString = doc.toJson(QJsonDocument::Indented);
  LOG_INFO << "DriveResultRenderer: Received JSON data (plain text):\n" << jsonString.toStdString();
  
  // Parse the testData which contains the full JSON structure
  QJsonObject rootData = networkData.testData;
  
  // Extract performance metrics from nested benchmark_results (protobuf structure)
  if (rootData.contains("benchmark_results") && rootData["benchmark_results"].isObject()) {
    QJsonObject results = rootData["benchmark_results"].toObject();
    // Server JSON uses camelCase names in the user's example; file-based uses snake_case
    drive.readSpeedMBs = results.value("read_speed_mb_s").toDouble();
    if (drive.readSpeedMBs <= 0) drive.readSpeedMBs = results.value("readSpeedMbS").toDouble();
    drive.writeSpeedMBs = results.value("write_speed_mb_s").toDouble();
    if (drive.writeSpeedMBs <= 0) drive.writeSpeedMBs = results.value("writeSpeedMbS").toDouble();
    drive.iops4k = results.value("iops_4k").toDouble();
    if (drive.iops4k <= 0) drive.iops4k = results.value("iops4k").toDouble();
    drive.accessTimeMs = results.value("access_time_ms").toDouble();
    if (drive.accessTimeMs <= 0) drive.accessTimeMs = results.value("accessTimeMs").toDouble();
  } else {
    // Fallbacks if server ever sends flattened fields
    drive.readSpeedMBs = rootData.value("read_speed_mb_s").toDouble();
    if (drive.readSpeedMBs <= 0) drive.readSpeedMBs = rootData.value("readSpeedMbS").toDouble();
    drive.writeSpeedMBs = rootData.value("write_speed_mb_s").toDouble();
    if (drive.writeSpeedMBs <= 0) drive.writeSpeedMBs = rootData.value("writeSpeedMbS").toDouble();
    drive.iops4k = rootData.value("iops_4k").toDouble();
    if (drive.iops4k <= 0) drive.iops4k = rootData.value("iops4k").toDouble();
    drive.accessTimeMs = rootData.value("access_time_ms").toDouble();
    if (drive.accessTimeMs <= 0) drive.accessTimeMs = rootData.value("accessTimeMs").toDouble();
  }
  
  LOG_INFO << "DriveResultRenderer: Performance data - read=" << drive.readSpeedMBs
           << "MB/s, write=" << drive.writeSpeedMBs << "MB/s, IOPS=" << drive.iops4k 
           << ", access_time=" << drive.accessTimeMs << "ms";
  
  // For network data, set reasonable defaults since component name is not available
  drive.model = ""; // Component name not available in direct format
  drive.driveType = "SSD"; // Default assumption
  
  LOG_INFO << "DriveResultRenderer: Conversion complete";
  return drive;
}

// Create dropdown data structure from menu (names only, no performance data yet)
std::map<QString, DriveComparisonData> DriveResultRenderer::createDropdownDataFromMenu(const MenuData& menuData) {
  std::map<QString, DriveComparisonData> dropdownData;
  
  LOG_INFO << "DriveResultRenderer: Creating dropdown data from menu with " 
           << menuData.availableDrives.size() << " drive options";
  
  // Create placeholder entries for each drive type in the menu
  for (const QString& driveName : menuData.availableDrives) {
    const QString trimmed = driveName.trimmed();
    if (trimmed.isEmpty()) {
      continue;
    }

    // Filter out bogus "drive letter" pseudo-models like "D:\".
    static const QRegularExpression driveLetterOnly(R"(^[A-Za-z]:\\?$)");
    if (driveLetterOnly.match(trimmed).hasMatch() || trimmed.size() < 6) {
      LOG_INFO << "DriveResultRenderer: Skipping invalid drive name from menu: " << trimmed.toStdString();
      continue;
    }

    DriveComparisonData placeholder;
    placeholder.model = trimmed; // Use the full name as provided
    placeholder.driveType = "";
    
    // Performance metrics are 0 initially (will be loaded on demand)
    placeholder.readSpeedMBs = 0;
    placeholder.writeSpeedMBs = 0;
    placeholder.iops4k = 0;
    placeholder.accessTimeMs = 0;
    
    dropdownData[placeholder.model] = placeholder;
    
    LOG_INFO << "DriveResultRenderer: Added drive option: " << placeholder.model.toStdString();
  }
  
  return dropdownData;
}

}  // namespace DiagnosticRenderers
