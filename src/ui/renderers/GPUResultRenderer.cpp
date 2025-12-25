#include "GPUResultRenderer.h"

#include <algorithm>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QStyle>

#include "DiagnosticViewComponents.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"
#include "logging/Logger.h"
#include "network/api/DownloadApiClient.h"

namespace DiagnosticRenderers {

QWidget* GPUResultRenderer::createGPUResultWidget(const QString& result, const MenuData* networkMenuData, DownloadApiClient* downloadClient) {
  // Get data from DiagnosticDataStore
  auto& dataStore = DiagnosticDataStore::getInstance();
  const auto& gpuData = dataStore.getGPUData();

  // Get constant system information
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();

  // Initialize values with data from DiagnosticDataStore
  float averageFPS = gpuData.averageFPS;
  int totalFrames = gpuData.totalFrames;
  QString driverVersion = QString::fromStdString(gpuData.driverVersion);

  // Get GPU name from constant info if available
  QString gpuName = "";
  if (!constantInfo.gpuDevices.empty()) {
    gpuName = QString::fromStdString(constantInfo.gpuDevices[0].name);
    // If driver version is not set in DiagnosticDataStore, get it from
    // constantInfo
    if (driverVersion.isEmpty() || driverVersion == "no_data") {
      driverVersion =
        QString::fromStdString(constantInfo.gpuDevices[0].driverVersion);
    }
  }

  // If critical data is missing from DataStore, fall back to parsing the text
  // result
  if (averageFPS <= 0.0f || totalFrames <= 0 || driverVersion.isEmpty()) {
    QStringList lines = result.split('\n');

    // Parse the raw data
    for (const QString& line : lines) {
      if (line.contains("FPS", Qt::CaseInsensitive) && averageFPS <= 0.0f)
        averageFPS = line.split(":").last().trimmed().toFloat();
      else if (line.contains("Frames", Qt::CaseInsensitive) && totalFrames <= 0)
        totalFrames = line.split(":").last().trimmed().toInt();
      else if (line.contains("Driver", Qt::CaseInsensitive) &&
               (driverVersion.isEmpty() || driverVersion == "no_data"))
        driverVersion = line.split(":").last().trimmed();
    }
  }

  // Load comparison data (network-based if available, otherwise from files)
  std::map<QString, GPUComparisonData> allComparisonData;
  MenuData cachedMenu;
  const MenuData* menuData = networkMenuData;
  if (!menuData && downloadClient && downloadClient->isMenuCached()) {
    cachedMenu = downloadClient->getCachedMenu();
    menuData = &cachedMenu;
  }

  if (menuData && !menuData->availableGpus.isEmpty()) {
    LOG_INFO << "GPUResultRenderer: Using network menu data for comparison dropdowns";
    allComparisonData = createDropdownDataFromMenu(*menuData);
  } else {
    LOG_INFO << "GPUResultRenderer: Falling back to local file comparison data";
    allComparisonData = loadGPUComparisonData();
  }

  if (downloadClient) {
    GPUComparisonData general{};
    general.model = DownloadApiClient::generalAverageLabel();
    general.fullModel = DownloadApiClient::generalAverageLabel();
    general.vendor = "";
    general.vramMB = 0;
    general.driverVersion = "";
    general.pcieGen = 0;
    general.pciLinkWidth = 0;
    general.fps = 0;
    general.frames = 0;
    allComparisonData[DownloadApiClient::generalAverageLabel()] = general;
  }

  // Find maximum FPS value across all GPUs (both user and comparison)
  double maxFPS = averageFPS;

  // Check comparison GPUs for higher FPS values
  for (const auto& [_, gpuCompData] : allComparisonData) {
    maxFPS = std::max(maxFPS, gpuCompData.fps);
  }

  // Use a consistent scaling factor - 80% instead of 90% to match the
  // comparison bars
  double scaledMaxFPS =
    (maxFPS > 0.1) ? (maxFPS * 1.25) : 150.0;  // 1/0.8 = 1.25

  // Create the main container widget with consistent styling
  QWidget* containerWidget = new QWidget();
  containerWidget->setSizePolicy(QSizePolicy::Preferred,
                                 QSizePolicy::Preferred);

  QVBoxLayout* mainLayout = new QVBoxLayout(containerWidget);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(
    0);  // Remove spacing between widgets to match other renderers

  // Create GPU metrics widget with consistent styling
  QWidget* gpuMetricsWidget = new QWidget();
  gpuMetricsWidget->setStyleSheet(
    "background-color: #252525; border-radius: 4px;");
  QVBoxLayout* metricsContainerLayout = new QVBoxLayout(gpuMetricsWidget);
  metricsContainerLayout->setContentsMargins(
    12, 4, 12, 4);  // Match CPU/Memory renderer margins
  metricsContainerLayout->setSpacing(10);

  // Create a title for the GPU section with consistent styling
  QLabel* gpuTitle = new QLabel("<b>GPU Performance Analysis</b>");
  gpuTitle->setStyleSheet("color: #ffffff; font-size: 14px; background: "
                          "transparent; margin-bottom: 5px;");
  gpuTitle->setContentsMargins(0, 0, 0, 0);

  // Add title to the layout
  metricsContainerLayout->addWidget(gpuTitle);

  // Create grid layout for GPU metrics
  QWidget* metricsWidget = new QWidget();
  metricsWidget->setStyleSheet("background: transparent;");
  QGridLayout* gpuMetricsLayout = new QGridLayout(metricsWidget);
  gpuMetricsLayout->setContentsMargins(0, 0, 0, 0);
  gpuMetricsLayout->setSpacing(10);

  // Determine FPS color based on performance - consistent color scheme
  QString fpsColor = "#FFFFFF";  // Default white
  if (averageFPS >= 3500)
    fpsColor = "#44FF44";  // Excellent (green)
  else if (averageFPS >= 2000)
    fpsColor = "#88FF88";  // Good (light green)
  else if (averageFPS >= 1000)
    fpsColor = "#FFEE44";  // Normal (yellow)
  else if (averageFPS >= 300)
    fpsColor = "#FFAA00";  // Below average (orange)
  else
    fpsColor = "#FF6666";  // Potential issue (red)

  // Add GPU metrics with consistent styling
  QWidget* gpuInfoWidget = new QWidget();
  gpuInfoWidget->setStyleSheet(
    "background-color: #252525; border-radius: 4px; padding: 8px;");
  QHBoxLayout* gpuInfoLayout = new QHBoxLayout(gpuInfoWidget);
  gpuInfoLayout->setContentsMargins(8, 8, 8, 8);
  gpuInfoLayout->setSpacing(20);

  // Create metrics with consistent styling (same as CPU/Memory info items)
  QLabel* fpsLabel = new QLabel(
    QString("<span style='font-weight: bold; color: %1;'>%2</span><br><span "
            "style='color: #888888;'>Average FPS</span>")
      .arg(fpsColor)
      .arg(averageFPS, 0, 'f', 1));
  fpsLabel->setAlignment(Qt::AlignCenter);

  QLabel* framesLabel = new QLabel(
    QString(
      "<span style='font-weight: bold; color: #FFFFFF;'>%1</span><br><span "
      "style='color: #888888;'>Total Frames</span>")
      .arg(totalFrames));
  framesLabel->setAlignment(Qt::AlignCenter);

  QLabel* driverLabel = new QLabel(
    QString(
      "<span style='font-weight: bold; color: #FFFFFF;'>%1</span><br><span "
      "style='color: #888888;'>Driver Version</span>")
      .arg(driverVersion));
  driverLabel->setAlignment(Qt::AlignCenter);

  // Add info components to the layout
  gpuInfoLayout->addWidget(fpsLabel);
  gpuInfoLayout->addWidget(framesLabel);
  gpuInfoLayout->addWidget(driverLabel);

  // Add GPU name if available
  if (!gpuName.isEmpty() && gpuName != "no_data") {
    QLabel* nameLabel = new QLabel(
      QString(
        "<span style='font-weight: bold; color: #FFFFFF;'>%1</span><br><span "
        "style='color: #888888;'>GPU Model</span>")
        .arg(gpuName));
    nameLabel->setAlignment(Qt::AlignCenter);
    gpuInfoLayout->addWidget(nameLabel);
  }

  // Add GPU info widget to metrics layout
  gpuMetricsLayout->addWidget(gpuInfoWidget, 0, 0, 1, 3);

  // Create title with dropdown section for comparison
  QWidget* titleWidget = new QWidget();
  QHBoxLayout* titleLayout = new QHBoxLayout(titleWidget);
  titleLayout->setContentsMargins(0, 10, 0, 0);

  QLabel* performanceTitle = new QLabel("<b>GPU Performance</b>");
  performanceTitle->setStyleSheet(
    "color: #ffffff; font-size: 14px; background: transparent;");
  titleLayout->addWidget(performanceTitle);

  titleLayout->addStretch(1);

  // Store value pair (current, max) for updating later
  QPair<double, double> fpsVals(averageFPS, maxFPS);

  // Create and add the dropdown
  QComboBox* dropdown =
    createGPUComparisonDropdown(allComparisonData, gpuMetricsWidget, fpsVals, downloadClient);
  dropdown->setObjectName("gpu_comparison_dropdown");

  titleLayout->addWidget(dropdown);
  gpuMetricsLayout->addWidget(titleWidget, 1, 0, 1, 3);

  // Create performance visualization box with consistent styling
  QWidget* performanceBox = new QWidget();
  performanceBox->setStyleSheet("background-color: #252525;");
  QVBoxLayout* performanceLayout = new QVBoxLayout(performanceBox);
  performanceLayout->setContentsMargins(8, 12, 8, 12);
  performanceLayout->setSpacing(6);

  // Use the comparison performance bar instead of the custom FPS gauge
  QString gpuDisplayName =
    (!gpuName.isEmpty() && gpuName != "no_data") ? gpuName : "Your GPU";
  QWidget* fpsBar = DiagnosticViewComponents::createComparisonPerformanceBar(
    "Frames Per Second", averageFPS, 0, maxFPS, "FPS", false);

  // Find and set object name for the comparison bar
  QWidget* innerFpsBar = fpsBar->findChild<QWidget*>("comparison_bar");
  if (innerFpsBar) innerFpsBar->setObjectName("comparison_bar_fps");

  // Find and update the user name label to show GPU information
  QLabel* fpsUserNameLabel = fpsBar->findChild<QLabel*>("userNameLabel");
  if (fpsUserNameLabel) fpsUserNameLabel->setText(gpuDisplayName);

  performanceLayout->addWidget(fpsBar);

  if (downloadClient) {
    const int idx = dropdown->findText(DownloadApiClient::generalAverageLabel());
    if (idx > 0) dropdown->setCurrentIndex(idx);
  }

  // Add the performance box to the metrics layout
  gpuMetricsLayout->addWidget(performanceBox, 2, 0, 1, 3);

  // Add the metrics widget to main layout
  metricsContainerLayout->addWidget(metricsWidget);

  // Add GPU metrics widget to container
  mainLayout->addWidget(gpuMetricsWidget);

  return containerWidget;
}

QWidget* GPUResultRenderer::createGpuMetricBox(const QString& title,
                                               const QString& value,
                                               const QString& color) {
  QWidget* box = new QWidget();
  box->setStyleSheet(
    "background-color: #252525; border-radius: 4px;");  // Match CPU/Memory
                                                        // renderer style

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

QWidget* GPUResultRenderer::createFpsGauge(float fps) {
  QWidget* container = new QWidget();
  QVBoxLayout* mainLayout = new QVBoxLayout(container);
  mainLayout->setContentsMargins(0, 1, 0, 1);
  mainLayout->setSpacing(1);

  QHBoxLayout* layout = new QHBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  // Add label at the left side of the horizontal layout
  QLabel* nameLabel = new QLabel("FPS Rating");
  nameLabel->setStyleSheet(
    "color: #ffffff; background: transparent; font-weight: bold;");
  nameLabel->setFixedWidth(80);
  nameLabel->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(nameLabel);

  QWidget* barContainer = new QWidget();
  barContainer->setFixedHeight(20);
  barContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  barContainer->setStyleSheet("background-color: #333333; border-radius: 2px;");

  QHBoxLayout* barLayout = new QHBoxLayout(barContainer);
  barLayout->setContentsMargins(0, 0, 0, 0);
  barLayout->setSpacing(0);

  // Calculate percentage based on FPS benchmarks
  double maxFPS =
    150.0;  // Reference maximum (changed from 6000.0 for better scaling)

  // Cap at 90% like other renderers
  float cappedFPS = (fps < (float)maxFPS) ? fps : (float)maxFPS;
  int percentage = int((cappedFPS / maxFPS) * 90);

  // Color based on FPS thresholds - consistent with other renderers
  QString color;
  if (fps >= 120)
    color = "#44FF44";  // Green for excellent
  else if (fps >= 60)
    color = "#88FF88";  // Light green for good
  else if (fps >= 30)
    color = "#FFAA00";  // Orange for acceptable
  else
    color = "#FF6666";  // Red for poor

  QWidget* bar = new QWidget();
  bar->setFixedHeight(20);
  bar->setStyleSheet(
    QString("background-color: %1; border-radius: 2px;").arg(color));

  QWidget* spacer = new QWidget();
  spacer->setStyleSheet("background-color: transparent;");

  // Use stretch factors for correct proportion
  barLayout->addWidget(bar, percentage);
  barLayout->addWidget(spacer, 100 - percentage);

  layout->addWidget(barContainer);

  // Show the actual value with the same color
  QLabel* valueLabel = new QLabel(QString("%1 FPS").arg(fps, 0, 'f', 1));
  valueLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  valueLabel->setStyleSheet(
    QString("color: %1; background: transparent;").arg(color));
  layout->addWidget(valueLabel);

  // Add typical value reference like other renderers
  QLabel* typicalLabel = new QLabel("(typical: 60.0 FPS)");
  typicalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  typicalLabel->setStyleSheet(
    "color: #888888; font-size: 10px; background: transparent;");
  layout->addWidget(typicalLabel);

  mainLayout->addLayout(layout);
  return container;
}

std::map<QString, GPUComparisonData> GPUResultRenderer::loadGPUComparisonData() {
  std::map<QString, GPUComparisonData> comparisonData;

  // Find the comparison_data folder
  QString appDir = QApplication::applicationDirPath();
  QDir dataDir(appDir + "/comparison_data");

  if (!dataDir.exists()) {
    return comparisonData;
  }

  // Look for GPU benchmark JSON files
  QStringList filters;
  filters << "gpu_benchmark_*.json";
  dataDir.setNameFilters(filters);

  QStringList gpuFiles = dataDir.entryList(QDir::Files);

  for (const QString& fileName : gpuFiles) {
    QFile file(dataDir.absoluteFilePath(fileName));

    if (file.open(QIODevice::ReadOnly)) {
      QByteArray jsonData = file.readAll();
      QJsonDocument doc = QJsonDocument::fromJson(jsonData);

      if (doc.isObject()) {
        QJsonObject rootObj = doc.object();

        GPUComparisonData gpu;
        gpu.model = rootObj["model"].toString();
        gpu.fullModel = rootObj["full_model"].toString();
        gpu.vendor = rootObj["vendor"].toString();
        gpu.vramMB = rootObj["vram_mb"].toInt();
        gpu.driverVersion = rootObj["driver_version"].toString();
        gpu.pcieGen = rootObj["pcie_gen"].toInt();
        gpu.pciLinkWidth = rootObj["pci_link_width"].toInt();

        // Get benchmark results
        if (rootObj.contains("benchmark_results") &&
            rootObj["benchmark_results"].isObject()) {
          QJsonObject resultsObj = rootObj["benchmark_results"].toObject();
          gpu.fps = resultsObj["fps"].toDouble();
          gpu.frames = resultsObj["frames"].toInt();
        }

        // Use model name as the display name
        QString displayName = gpu.model;
        if (displayName.isEmpty()) {
          // If model is missing, use system_id
          displayName = rootObj["system_id"].toString();
        }

        comparisonData[displayName] = gpu;
      }

      file.close();
    }
  }

  return comparisonData;
}

// Network-based method to convert ComponentData to GPUComparisonData
GPUComparisonData GPUResultRenderer::convertNetworkDataToGPU(const ComponentData& networkData) {
  GPUComparisonData gpu;
  
  LOG_DEBUG << "GPUResultRenderer: Converting network data for: " << networkData.componentName.toStdString();
  
  // Parse the testData which contains the full JSON structure
  QJsonObject rootData = networkData.testData;
  
  // Extract performance metrics from nested benchmark_results (protobuf structure)
  if (rootData.contains("benchmark_results") && rootData["benchmark_results"].isObject()) {
    QJsonObject results = rootData["benchmark_results"].toObject();
    gpu.fps = results.value("fps").toDouble();
    if (gpu.fps <= 0) {
      gpu.fps = results.value("avg_fps").toDouble();
    }
    if (gpu.fps <= 0) {
      gpu.fps = results.value("average_fps").toDouble();
    }
    gpu.frames = results.value("frames").toInt();
    if (gpu.frames <= 0) {
      gpu.frames = results.value("frame_count").toInt();
    }
  } else if (rootData.contains("benchmarkResults") && rootData["benchmarkResults"].isObject()) {
    // Fallback for camelCase container name
    QJsonObject results = rootData["benchmarkResults"].toObject();
    gpu.fps = results.value("fps").toDouble();
    if (gpu.fps <= 0) {
      gpu.fps = results.value("avgFps").toDouble();
    }
    if (gpu.fps <= 0) {
      gpu.fps = results.value("averageFps").toDouble();
    }
    gpu.frames = results.value("frames").toInt();
    if (gpu.frames <= 0) {
      gpu.frames = results.value("frameCount").toInt();
    }
  } else if (rootData.contains("results") && rootData["results"].isObject()) {
    // Fallback for GPUData-style payloads
    QJsonObject results = rootData["results"].toObject();
    gpu.fps = results.value("fps").toDouble();
    gpu.frames = results.value("frames").toInt();
  } else {
    // Fallbacks if server ever sends flattened fields
    gpu.fps = rootData.value("fps").toDouble();
    if (gpu.fps <= 0) {
      gpu.fps = rootData.value("avg_fps").toDouble();
    }
    if (gpu.fps <= 0) {
      gpu.fps = rootData.value("average_fps").toDouble();
    }
    gpu.frames = rootData.value("frames").toInt();
    if (gpu.frames <= 0) {
      gpu.frames = rootData.value("frame_count").toInt();
    }
  }
  
  LOG_INFO << "GPUResultRenderer: Performance data - fps=" << gpu.fps 
           << ", frames=" << gpu.frames;
  
  // For network data, set reasonable defaults since component details are not available
  gpu.model = ""; // Component name not available in direct format
  gpu.fullModel = ""; // Not available
  gpu.vendor = ""; // Unknown from direct data
  gpu.vramMB = 0; // Unknown from direct data
  gpu.driverVersion = ""; // Unknown from direct data
  gpu.pcieGen = 0; // Unknown from direct data
  gpu.pciLinkWidth = 0; // Unknown from direct data
  
  LOG_INFO << "GPUResultRenderer: Conversion complete";
  return gpu;
}

// Create dropdown data structure from menu (names only, no performance data yet)
std::map<QString, GPUComparisonData> GPUResultRenderer::createDropdownDataFromMenu(const MenuData& menuData) {
  std::map<QString, GPUComparisonData> dropdownData;
  
  // Create placeholder entries for each GPU in the menu
  for (const QString& gpuName : menuData.availableGpus) {
    GPUComparisonData gpu;
    gpu.model = gpuName; // Only the name is known at this point
    // All other fields remain at default/zero values
    dropdownData[gpuName] = gpu;
  }
  
  LOG_INFO << "GPUResultRenderer: Created dropdown data for " << dropdownData.size() << " GPUs from menu";
  return dropdownData;
}

// Add this function implementation after loadGPUComparisonData
std::map<QString, DiagnosticViewComponents::AggregatedComponentData<GPUComparisonData>> GPUResultRenderer::
  generateAggregatedGPUData(
    const std::map<QString, GPUComparisonData>& individualData) {
  std::map<QString,
           DiagnosticViewComponents::AggregatedComponentData<GPUComparisonData>>
    result;

  // Group GPUs by model number (like "3070", "3070Ti", "5060", etc.)
  std::map<QString, std::vector<std::pair<QString, GPUComparisonData>>>
    groupedData;

  // Regular expression to match common GPU model numbers
  QRegularExpression gpuModelRegex(
    "(?:RTX|GTX)?\\s*(\\d{3,4}\\s*(?:Ti|XT|SUPER)?)",
    QRegularExpression::CaseInsensitiveOption);

  for (const auto& [id, data] : individualData) {
    // Try to extract the GPU model number from the model name
    QString gpuModel;

    // First check the model field
    QRegularExpressionMatch match = gpuModelRegex.match(data.model);
    if (match.hasMatch()) {
      gpuModel = match.captured(1).trimmed();
    } else {
      // If not found in model, try the full model field
      match = gpuModelRegex.match(data.fullModel);
      if (match.hasMatch()) {
        gpuModel = match.captured(1).trimmed();
      } else {
        // If still not found, use the full model name
        gpuModel = data.model;
      }
    }

    // Add vendor prefix if available (like "NVIDIA" or "AMD")
    if (!data.vendor.isEmpty()) {
      gpuModel = data.vendor + " " + gpuModel;
    }

    // Add to the corresponding group
    groupedData[gpuModel].push_back({id, data});
  }

  // Create aggregated data for each GPU model
  for (const auto& [gpuModel, dataList] : groupedData) {
    DiagnosticViewComponents::AggregatedComponentData<GPUComparisonData>
      aggregated;
    aggregated.componentName = gpuModel;
    
    // Store the original full identifier from the first entry (for API requests)
    if (!dataList.empty()) {
      aggregated.originalFullName = dataList[0].first; // use map key (menu name)
      LOG_INFO << "GPUResultRenderer: Aggregated '" << gpuModel.toStdString()
               << "' originalFullName='" << aggregated.originalFullName.toStdString() << "'";
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

      // For GPU benchmarks, higher FPS is always better
      double maxFPS = firstData.fps;

      // Initialize sums for averages
      double sumFPS = firstData.fps;
      int sumFrames = firstData.frames;

      // Process all entries after the first
      for (size_t i = 1; i < dataList.size(); i++) {
        const auto& data = dataList[i].second;

        // Update maximum FPS
        if (data.fps > 0) {
          maxFPS = std::max(maxFPS, data.fps);
          sumFPS += data.fps;
        }

        // Sum frames too
        if (data.frames > 0) {
          sumFrames += data.frames;
        }
      }

      // Set the best result values
      aggregated.bestResult.fps = maxFPS;

      // Calculate averages
      size_t count = dataList.size();
      aggregated.averageResult.fps = sumFPS / count;
      aggregated.averageResult.frames = sumFrames / count;

      // Copy the vendor from the first result
      aggregated.bestResult.vendor = firstData.vendor;
      aggregated.averageResult.vendor = firstData.vendor;
    }

    // Add to the result map
    result[gpuModel] = aggregated;
  }

  return result;
}

// Replace the existing createGPUComparisonDropdown function with this updated
// version
QComboBox* GPUResultRenderer::createGPUComparisonDropdown(
  const std::map<QString, GPUComparisonData>& comparisonData,
  QWidget* containerWidget, const QPair<double, double>& fpsVals,
  DownloadApiClient* downloadClient) {

  // Generate aggregated data from individual results
  auto aggregatedData = generateAggregatedGPUData(comparisonData);

  const QPair<double, double> fpsValsCopy = fpsVals;

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
                            bool hasSelection) {
    if (!hasSelection) {
      return QString("Select GPU to compare");
    }

    return (componentName == DownloadApiClient::generalAverageLabel())
             ? componentName
             : componentName + " (" +
                 (type == DiagnosticViewComponents::AggregationType::Best
                    ? "Best)"
                    : "Avg)");
  };

  auto updateGpuBars =
    [containerWidget, fpsValsCopy, updateUserBarLayout](
      const GPUComparisonData* compData, const QString& displayName,
      bool hasSelection) {
      std::vector<TestMetric> tests = {
        {"comparison_bar_fps", fpsValsCopy.first,
         compData ? compData->fps : 0.0, "FPS", false}};

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
            valueLabel->setText(
              QString("%1 %2").arg(test.compValue, 0, 'f', 1).arg(test.unit));
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
          const bool isBetter = percentChange > 0;
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
  auto selectionCallback = [downloadClient, makeDisplayName, updateGpuBars](
                             const QString& componentName,
                             const QString& originalFullName,
                             DiagnosticViewComponents::AggregationType type,
                             const GPUComparisonData& gpuData) {
    LOG_INFO << "GPUResultRenderer: selectionCallback invoked: component='"
             << componentName.toStdString() << "', originalFullName='"
             << originalFullName.toStdString() << "', aggType='"
             << (type == DiagnosticViewComponents::AggregationType::Best ? "Best" : "Avg")
             << "', havePerfData=" << (gpuData.fps > 0);

    if (downloadClient && !componentName.isEmpty() && gpuData.fps <= 0) {
      LOG_INFO << "GPUResultRenderer: Fetching network data for GPU: "
               << componentName.toStdString() << " using original name: "
               << originalFullName.toStdString();

      downloadClient->fetchComponentData(
        "gpu", originalFullName,
        [componentName, type, makeDisplayName, updateGpuBars]
        (bool success, const ComponentData& networkData, const QString& error) {
          if (success) {
            LOG_INFO << "GPUResultRenderer: Successfully fetched GPU data for "
                     << componentName.toStdString();

            GPUComparisonData fetchedGpuData =
              convertNetworkDataToGPU(networkData);
            const QString displayName =
              makeDisplayName(componentName, type, true);
            updateGpuBars(&fetchedGpuData, displayName, true);
          } else {
            LOG_ERROR << "GPUResultRenderer: Failed to fetch GPU data for "
                      << componentName.toStdString() << ": "
                      << error.toStdString();
          }
        });

      return;
    }

    const bool hasSelection = !componentName.isEmpty();
    const QString displayName =
      makeDisplayName(componentName, type, hasSelection);
    updateGpuBars(hasSelection ? &gpuData : nullptr, displayName,
                  hasSelection);
  };

  // Use the shared helper to create the dropdown
  return DiagnosticViewComponents::createAggregatedComparisonDropdown<
    GPUComparisonData>(aggregatedData, selectionCallback);
}

}  // namespace DiagnosticRenderers
