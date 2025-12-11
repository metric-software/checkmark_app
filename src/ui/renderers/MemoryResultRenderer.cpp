#include "MemoryResultRenderer.h"

#include <iostream>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextEdit>
#include <QVBoxLayout>

#include "DiagnosticViewComponents.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"
#include "logging/Logger.h"

namespace DiagnosticRenderers {

QWidget* MemoryResultRenderer::createMemoryMetricBox(const QString& title,
                                                     const QString& value,
                                                     bool isHighlight) {
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

  // Use CPU renderer style with value on top, label below
  QString valueColor = isHighlight ? "#FFAA00" : "#FFFFFF";
  QLabel* valueLabel = new QLabel(
    QString("<span style='font-weight: bold; color: %1;'>%2</span><br><span "
            "style='color: #888888;'>%3</span>")
      .arg(valueColor)
      .arg(value)
      .arg(title),
    box);
  valueLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(valueLabel);

  return box;
}

QWidget* MemoryResultRenderer::createPerformanceBox(const QString& title,
                                                    double value,
                                                    const QString& unit) {
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

  // Determine color based on value (for memory operations, lower is better)
  QString valueColor;
  if (unit == "ms") {
    if (value < 50)
      valueColor = "#44FF44";  // Green (excellent)
    else if (value < 100)
      valueColor = "#88FF88";  // Light green (good)
    else if (value < 200)
      valueColor = "#FFAA00";  // Orange (average)
    else
      valueColor = "#FF6666";  // Red (poor)
  } else {
    valueColor = "#0078d4";  // Default blue
  }

  QLabel* valueLabel =
    new QLabel(QString("<span style='color: %1; font-size: 18px; font-weight: "
                       "bold;'>%2 %3</span>")
                 .arg(valueColor)
                 .arg(value, 0, 'f', 1)
                 .arg(unit),
               box);
  valueLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(valueLabel);

  return box;
}

QWidget* MemoryResultRenderer::createPerformanceGauge(const QString& label,
                                                      double value,
                                                      double maxValue,
                                                      const QString& unit) {
  QWidget* container = new QWidget();
  QVBoxLayout* mainLayout = new QVBoxLayout(container);
  mainLayout->setContentsMargins(0, 1, 0, 1);  // Minimal vertical margins
  mainLayout->setSpacing(1);                   // Minimal spacing

  QHBoxLayout* layout = new QHBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);  // No internal margins
  layout->setSpacing(8);

  // Add label at the left side of the horizontal layout
  QLabel* nameLabel = new QLabel(label);
  nameLabel->setStyleSheet(
    "color: #ffffff; background: transparent; font-weight: bold;");
  nameLabel->setFixedWidth(80);               // Fixed width for alignment
  nameLabel->setContentsMargins(0, 0, 0, 0);  // No internal margins
  layout->addWidget(nameLabel);

  QWidget* barContainer = new QWidget();
  barContainer->setFixedHeight(20);
  barContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  barContainer->setStyleSheet("background-color: #333333; border-radius: 2px;");

  QHBoxLayout* barLayout = new QHBoxLayout(barContainer);
  barLayout->setContentsMargins(0, 0, 0, 0);
  barLayout->setSpacing(0);

  // Calculate percentage (0-90%) based on value / maxValue
  int percentage = int((value / maxValue) * 90);
  percentage = percentage > 90 ? 90 : percentage;

  // Color based on percentage
  QString color;
  if (percentage >= 70)
    color = "#44FF44";  // Green for excellent
  else if (percentage >= 50)
    color = "#88FF88";  // Light green for good
  else if (percentage >= 30)
    color = "#FFAA00";  // Orange for average
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
  QLabel* valueLabel =
    new QLabel(QString("%1 %2").arg(value, 0, 'f', 1).arg(unit));
  valueLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  valueLabel->setStyleSheet(
    QString("color: %1; background: transparent;").arg(color));
  layout->addWidget(valueLabel);

  mainLayout->addLayout(layout);
  return container;
}

QString MemoryResultRenderer::getMemoryPerformanceRecommendation(
  double bandwidth, double latency, bool xmpEnabled) {
  QString recommendation = "<b>Analysis:</b> ";

  if (bandwidth > 15000) {
    recommendation += "Your memory bandwidth is excellent. ";
  } else if (bandwidth > 10000) {
    recommendation += "Your memory bandwidth is good. ";
  } else if (bandwidth > 5000) {
    recommendation += "Your memory bandwidth is average. ";
  } else {
    recommendation += "Your memory bandwidth is below-average. ";
  }

  if (latency < 1.0) {
    recommendation +=
      "Memory latency is very low, which is excellent for performance. ";
  } else if (latency < 5.0) {
    recommendation += "Memory latency is good. ";
  } else {
    recommendation += "Memory latency could be improved. ";
  }

  if (!xmpEnabled) {
    recommendation += "<br><br><b>Recommendation:</b> Enable XMP in BIOS to "
                      "improve memory performance. ";
    recommendation += "Your RAM is currently not running at its rated speed.";
  } else if (bandwidth < 10000) {
    recommendation += "<br><br><b>Recommendation:</b> Consider upgrading to "
                      "faster memory for better system performance, ";
    recommendation +=
      "especially for memory-intensive tasks like gaming or content creation.";
  } else {
    recommendation += "<br><br><b>Recommendation:</b> Your memory "
                      "configuration appears optimal.";
  }

  return recommendation;
}

QWidget* MemoryResultRenderer::createRawDataWidget(const QString& result) {
  QWidget* rawDataContainer = new QWidget();
  QVBoxLayout* rawDataLayout = new QVBoxLayout(rawDataContainer);
  // Set a solid background color for the container
  rawDataContainer->setStyleSheet(
    "background-color: #252525; border-radius: 4px;");

  QPushButton* showRawDataBtn = new QPushButton("▼ Show Raw Data");
  showRawDataBtn->setStyleSheet(R"(
        QPushButton {
            color: #0078d4;
            border: none;
            text-align: left;
            padding: 4px;
            font-size: 12px;
            background-color: #252525;
        }
        QPushButton:hover {
            color: #1084d8;
            text-decoration: underline;
        }
    )");

  QTextEdit* rawDataText = new QTextEdit();
  rawDataText->setReadOnly(true);
  rawDataText->setFixedHeight(150);
  rawDataText->setText(result);
  rawDataText->setStyleSheet(
    "background-color: #1e1e1e; color: #dddddd; border: 1px solid #333333;");
  rawDataText->hide();

  // Connect with captured variables for lambda
  QObject::connect(
    showRawDataBtn, &QPushButton::clicked, [showRawDataBtn, rawDataText]() {
      bool visible = rawDataText->isVisible();
      rawDataText->setVisible(!visible);
      showRawDataBtn->setText(visible ? "▼ Show Raw Data" : "▲ Hide Raw Data");
    });

  rawDataLayout->addWidget(showRawDataBtn);
  rawDataLayout->addWidget(rawDataText);

  return rawDataContainer;
}

QWidget* MemoryResultRenderer::createMemoryResultWidget(const QString& result, const MenuData* networkMenuData, DownloadApiClient* downloadClient) {
  LOG_INFO << "MemoryResultRenderer: Creating memory result widget with network support";
  // Get memory data directly from the DiagnosticDataStore
  const auto& dataStore = DiagnosticDataStore::getInstance();
  const auto& memData = dataStore.getMemoryData();

  // Create the widget with the memory data
  QWidget* widget = processMemoryData(memData, networkMenuData, downloadClient);

  // Add raw data section at the bottom
  QWidget* rawDataWidget = createRawDataWidget(result);

  // Create a container for all content
  QWidget* containerWidget = new QWidget();
  containerWidget->setSizePolicy(QSizePolicy::Preferred,
                                 QSizePolicy::Preferred);

  QVBoxLayout* layout = new QVBoxLayout(containerWidget);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);  // Remove spacing between widgets to close the gap

  layout->addWidget(widget);
  layout->addWidget(rawDataWidget);

  return containerWidget;
}

QWidget* MemoryResultRenderer::processMemoryData(
  const DiagnosticDataStore::MemoryData& memData, const MenuData* networkMenuData, DownloadApiClient* downloadClient) {
  // Extract values directly from the data structure
  double bandwidth = memData.bandwidth;  // MB/s
  double latencyNs = memData.latency;    // ns
  // Use existing field names (writeTime/readTime) but treat them as GB/s values
  double writeSpeedGBs =
    memData.writeTime;  // These are now GB/s values from memory_test.cpp
  double readSpeedGBs =
    memData.readTime;  // These are now GB/s values from memory_test.cpp

  // Get constant system information as fallback
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();

  // Get memory type from modules if available
  QString memoryType = "";

  // Declare frequency variables before use
  QString frequency = "Unknown";
  int frequencyMHz = 0;

  // Check if there are memory modules available
  if (!memData.modules.empty()) {
    // Get the first module for summary display
    const auto& module = memData.modules[0];

    // Only get memory type from memData if it wasn't set earlier
    if (memoryType.isEmpty() && !module.memoryType.empty()) {
      memoryType = QString::fromStdString(module.memoryType);
    }

    // Get frequency information - prioritize configured speed
    if (module.configuredSpeedMHz > 0) {
      frequencyMHz = module.configuredSpeedMHz;
      frequency = QString("%1 MHz").arg(frequencyMHz);
      // Show base speed in parentheses only if significantly different
      if (module.speedMHz > 0 &&
          std::abs(module.speedMHz - module.configuredSpeedMHz) > 10) {
        frequency += QString(" (%1 MHz)").arg(module.speedMHz);
      }
    } else if (module.speedMHz > 0) {
      frequencyMHz = module.speedMHz;
      frequency = QString("%1 MHz").arg(frequencyMHz);
    }
  }

  // If memory type is still empty, try to get it from ConstantSystemInfo
  if (memoryType.isEmpty()) {
    // Try to use memory modules from constantInfo as second choice
    if (!constantInfo.memoryModules.empty() &&
        !constantInfo.memoryModules[0].memoryType.empty()) {
      memoryType =
        QString::fromStdString(constantInfo.memoryModules[0].memoryType);
    } else {
      // Last resort - use constantInfo.memoryType
      memoryType = QString::fromStdString(constantInfo.memoryType);
    }
  }

  // Get XMP status, first from DiagnosticDataStore, then fall back to
  // ConstantSystemInfo
  bool xmpEnabled = memData.xmpEnabled;
  if (!xmpEnabled) {
    xmpEnabled = constantInfo.xmpEnabled;
  }

  // Simplify channel status text to avoid stretching
  QString channelStatus = QString::fromStdString(memData.channelStatus);

  // If channel status is empty, try to get it from ConstantSystemInfo
  if (channelStatus.isEmpty() || channelStatus == "") {
    channelStatus = QString::fromStdString(constantInfo.memoryChannelConfig);
  }

  if (channelStatus.contains("Dual Channel", Qt::CaseInsensitive)) {
    channelStatus = "Dual Channel";
  } else if (channelStatus.contains("Single Channel", Qt::CaseInsensitive)) {
    channelStatus = "Single Channel";
  } else if (channelStatus.contains("Quad Channel", Qt::CaseInsensitive)) {
    channelStatus = "Quad Channel";
  }

  // Create container widget for memory metrics with consistent styling (match
  // CPU renderer)
  QWidget* memMetricsWidget = new QWidget();
  memMetricsWidget->setStyleSheet(
    "background-color: #252525; border-radius: 4px;");
  QVBoxLayout* mainLayout = new QVBoxLayout(memMetricsWidget);
  mainLayout->setContentsMargins(12, 4, 12, 4);  // Match CPU renderer margins
  mainLayout->setSpacing(10);

  // Create grid layout for memory metrics
  QWidget* metricsWidget = new QWidget();
  metricsWidget->setStyleSheet("background: transparent;");
  QGridLayout* memMetricsLayout = new QGridLayout(metricsWidget);
  memMetricsLayout->setContentsMargins(0, 0, 0, 0);  // No internal margins
  memMetricsLayout->setSpacing(10);

  if (!memData.modules.empty()) {
    // Get the first module for summary display
    const auto& module = memData.modules[0];

    // Only get memory type from memData if it wasn't set earlier
    if (memoryType.isEmpty() && !module.memoryType.empty()) {
      memoryType = QString::fromStdString(module.memoryType);
    }

    // Get frequency information - prioritize configured speed
    if (module.configuredSpeedMHz > 0) {
      frequencyMHz = module.configuredSpeedMHz;
      frequency = QString("%1 MHz").arg(frequencyMHz);
      // Show base speed in parentheses only if significantly different
      if (module.speedMHz > 0 &&
          std::abs(module.speedMHz - module.configuredSpeedMHz) > 10) {
        frequency += QString(" (%1 MHz)").arg(module.speedMHz);
      }
    } else if (module.speedMHz > 0) {
      frequencyMHz = module.speedMHz;
      frequency = QString("%1 MHz").arg(frequencyMHz);
    }
  }

  // Load memory comparison data (network or local)
  std::map<QString, MemoryComparisonData> comparisonData;
  if (networkMenuData && !networkMenuData->availableMemory.isEmpty()) {
    LOG_INFO << "MemoryResultRenderer: Using network menu data";
    comparisonData = createDropdownDataFromMenu(*networkMenuData);
  } else {
    LOG_INFO << "MemoryResultRenderer: Falling back to local file data";
    comparisonData = loadMemoryComparisonData();
  }

  // Create a title and dropdown section with horizontal layout
  QWidget* headerWidget = new QWidget();
  QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 0, 0, 0);

  // Create memory info section with horizontal layout (like CPU info widget)
  QWidget* memInfoWidget = new QWidget();
  memInfoWidget->setStyleSheet("background-color: #252525; padding: 8px;");
  QHBoxLayout* memInfoLayout = new QHBoxLayout(memInfoWidget);
  memInfoLayout->setContentsMargins(8, 8, 8, 8);
  memInfoLayout->setSpacing(20);

  // Determine frequency color based on value (for DDR4, higher is better)
  QString freqColor = "#FFFFFF";  // Default white
  if (memoryType.contains("DDR4", Qt::CaseInsensitive) && frequencyMHz > 0) {
    const int minFreq = 2133;  // Base frequency gets red
    const int maxFreq = 3600;  // Target frequency gets green

    // Create a continuous color scale based on the frequency
    int hue;
    if (frequencyMHz <= minFreq) {
      hue = 0;  // Red
    } else if (frequencyMHz >= maxFreq) {
      hue = 120;  // Green
    } else {
      // Scale the hue linearly between red and green
      double ratio =
        static_cast<double>(frequencyMHz - minFreq) / (maxFreq - minFreq);
      hue = static_cast<int>(120 * ratio);
    }

    QColor color = QColor::fromHsv(hue, 240, 245);
    freqColor = color.name();
  }

  // Determine channel color
  QString channelColor;
  if (channelStatus.contains("Dual", Qt::CaseInsensitive)) {
    channelColor = "#0078d4";  // Blue
  } else if (channelStatus.contains("Single", Qt::CaseInsensitive)) {
    channelColor = "#FF6666";  // Red
  } else {
    channelColor = "#FFFFFF";  // Default white
  }

  // Determine XMP profile color
  QString xmpColor;
  if (xmpEnabled) {
    xmpColor = "#44FF44";  // Green
  } else if (frequencyMHz < 2600) {
    xmpColor = "#FF6666";  // Red
  } else {
    xmpColor = "#FFAA00";  // Orange
  }

  // Create info items with consistent styling
  QLabel* typeLabel = new QLabel(
    QString(
      "<span style='font-weight: bold; color: #FFFFFF;'>%1</span><br><span "
      "style='color: #888888;'>Memory Type</span>")
      .arg(memoryType));
  typeLabel->setAlignment(Qt::AlignCenter);

  QLabel* freqLabel = new QLabel(
    QString("<span style='font-weight: bold; color: %1;'>%2</span><br><span "
            "style='color: #888888;'>Frequency</span>")
      .arg(freqColor)
      .arg(frequency));
  freqLabel->setAlignment(Qt::AlignCenter);

  QLabel* channelLabel = new QLabel(
    QString("<span style='font-weight: bold; color: %1;'>%2</span><br><span "
            "style='color: #888888;'>Channel Mode</span>")
      .arg(channelColor)
      .arg(channelStatus));
  channelLabel->setAlignment(Qt::AlignCenter);

  QLabel* xmpLabel = new QLabel(
    QString("<span style='font-weight: bold; color: %1;'>%2</span><br><span "
            "style='color: #888888;'>XMP Profile</span>")
      .arg(xmpColor)
      .arg(xmpEnabled ? "Enabled" : "Disabled"));
  xmpLabel->setAlignment(Qt::AlignCenter);

  // Add all components to the info layout
  memInfoLayout->addWidget(typeLabel);
  memInfoLayout->addWidget(freqLabel);
  memInfoLayout->addWidget(channelLabel);
  memInfoLayout->addWidget(xmpLabel);

  // Add the memory info widget to the main layout
  memMetricsLayout->addWidget(memInfoWidget, 0, 0, 1, 2);

  // Create title with dropdown section
  QWidget* titleWidget = new QWidget();
  QHBoxLayout* titleLayout = new QHBoxLayout(titleWidget);
  titleLayout->setContentsMargins(0, 10, 0, 0);

  QLabel* performanceTitle = new QLabel("<b>Memory Performance</b>");
  performanceTitle->setStyleSheet(
    "color: #ffffff; font-size: 14px; background: transparent;");
  titleLayout->addWidget(performanceTitle);

  titleLayout->addStretch(1);

  // Calculate max values for scaling
  double bandwidthGB = bandwidth / 1024.0;  // Convert from MB/s to GB/s
  double maxBandwidth = bandwidthGB;
  double maxLatency = latencyNs;
  double maxReadSpeed = readSpeedGBs;
  double maxWriteSpeed = writeSpeedGBs;

  // Compare with all values in comparison data to find global maximums
  for (const auto& [_, memCompData] : comparisonData) {
    maxBandwidth = std::max(maxBandwidth, memCompData.bandwidthMBs / 1024.0);
    maxLatency = std::max(maxLatency, memCompData.latencyNs);
    maxReadSpeed = std::max(maxReadSpeed, memCompData.readTimeGBs);
    maxWriteSpeed = std::max(maxWriteSpeed, memCompData.writeTimeGBs);
  }

  // Store value pairs (current, comparison max) for updating later
  QPair<double, double> bandwidthVals(bandwidthGB, maxBandwidth);
  QPair<double, double> latencyVals(latencyNs, maxLatency);
  QPair<double, double> readSpeedVals(readSpeedGBs, maxReadSpeed);
  QPair<double, double> writeSpeedVals(writeSpeedGBs, maxWriteSpeed);

  // Create and add the dropdown
  QComboBox* dropdown = createMemoryComparisonDropdown(
    comparisonData, memMetricsWidget, bandwidthVals, latencyVals, readSpeedVals,
    writeSpeedVals, downloadClient);

  titleLayout->addWidget(dropdown);
  memMetricsLayout->addWidget(titleWidget, 1, 0, 1, 2);

  // Create performance bars section
  QWidget* performanceBox = new QWidget();
  performanceBox->setStyleSheet("background-color: #252525;");
  QVBoxLayout* performanceLayout = new QVBoxLayout(performanceBox);
  performanceLayout->setContentsMargins(8, 12, 8, 12);
  performanceLayout->setSpacing(6);

  // Create main memory performance bars
  QWidget* bandwidthBar =
    DiagnosticViewComponents::createComparisonPerformanceBar(
      "Memory Bandwidth", bandwidthGB, 0, maxBandwidth, "GB/s", false);
  QWidget* latencyBar =
    DiagnosticViewComponents::createComparisonPerformanceBar(
      "Memory Latency", latencyNs, 0, maxLatency, "ns", true);

  // Find and set object names for the comparison bars
  QWidget* innerBandwidthBar =
    bandwidthBar->findChild<QWidget*>("comparison_bar");
  if (innerBandwidthBar)
    innerBandwidthBar->setObjectName("comparison_bar_bandwidth");

  QWidget* innerLatencyBar = latencyBar->findChild<QWidget*>("comparison_bar");
  if (innerLatencyBar) innerLatencyBar->setObjectName("comparison_bar_latency");

  performanceLayout->addWidget(bandwidthBar);
  performanceLayout->addWidget(latencyBar);

  // Add random read/write section
  QWidget* rwBox = new QWidget();
  rwBox->setStyleSheet("background-color: #252525;");
  QVBoxLayout* rwLayout = new QVBoxLayout(rwBox);
  rwLayout->setContentsMargins(8, 12, 8, 12);
  rwLayout->setSpacing(6);

  QLabel* rwTitle = new QLabel("<b>Random Access Performance</b>");
  rwTitle->setStyleSheet("color: #ffffff; font-size: 14px; background: "
                         "transparent; margin-bottom: 5px;");
  rwLayout->addWidget(rwTitle);

  // Create read/write performance bars
  QWidget* readBar = DiagnosticViewComponents::createComparisonPerformanceBar(
    "Random Read Speed", readSpeedGBs, 0, maxReadSpeed, "GB/s", false);
  QWidget* writeBar = DiagnosticViewComponents::createComparisonPerformanceBar(
    "Random Write Speed", writeSpeedGBs, 0, maxWriteSpeed, "GB/s", false);

  // Find and set object names
  QWidget* innerReadBar = readBar->findChild<QWidget*>("comparison_bar");
  if (innerReadBar) innerReadBar->setObjectName("comparison_bar_read");

  QWidget* innerWriteBar = writeBar->findChild<QWidget*>("comparison_bar");
  if (innerWriteBar) innerWriteBar->setObjectName("comparison_bar_write");

  rwLayout->addWidget(readBar);
  rwLayout->addWidget(writeBar);

  // Add recommendations and information
  QLabel* infoLabel = new QLabel(
    getMemoryPerformanceRecommendation(bandwidth, latencyNs, xmpEnabled));
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet(
    "color: #dddddd; font-style: italic; margin-top: 8px;");
  performanceLayout->addWidget(infoLabel);

  // Add performance sections to the layout
  memMetricsLayout->addWidget(performanceBox, 2, 0, 1, 2);
  memMetricsLayout->addWidget(rwBox, 3, 0, 1, 2);

  // Add memory modules table if available
  if (!memData.modules.empty()) {
    QWidget* moduleSection = createMemoryModulesTable(memData.modules);
    memMetricsLayout->addWidget(moduleSection, 4, 0, 1, 2);
  }

  // Add memory stability test results if available
  if (memData.stabilityTest.testPerformed) {
    QWidget* stabilitySection =
      createStabilityTestWidget(memData.stabilityTest);
    memMetricsLayout->addWidget(stabilitySection, 5, 0, 1, 2);
  }

  // Add all widgets to main layout
  mainLayout->addWidget(metricsWidget);

  return memMetricsWidget;
}

QWidget* MemoryResultRenderer::createMemoryModulesTable(
  const std::vector<DiagnosticDataStore::MemoryData::MemoryModule>& modules) {
  QWidget* moduleSection = new QWidget();
  QVBoxLayout* moduleSectionLayout = new QVBoxLayout(moduleSection);
  moduleSectionLayout->setContentsMargins(0, 10, 0, 0);

  // Create a table widget to display all memory modules
  QTableWidget* modulesTable = new QTableWidget(modules.size(), 5);
  modulesTable->setHorizontalHeaderLabels(
    {"Slot", "Capacity", "Speed", "Manufacturer", "Part Number"});
  modulesTable->setStyleSheet(
    "background-color: #252525; color: #ffffff; border: none;");

  // Set sizing policy to adapt to container width
  modulesTable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  modulesTable->horizontalHeader()->setSectionResizeMode(
    0, QHeaderView::ResizeToContents);  // Slot
  modulesTable->horizontalHeader()->setSectionResizeMode(
    1, QHeaderView::ResizeToContents);  // Capacity
  modulesTable->horizontalHeader()->setSectionResizeMode(
    2, QHeaderView::ResizeToContents);  // Speed
  modulesTable->horizontalHeader()->setSectionResizeMode(
    3, QHeaderView::Stretch);  // Manufacturer
  modulesTable->horizontalHeader()->setSectionResizeMode(
    4, QHeaderView::Stretch);  // Part Number
  modulesTable->verticalHeader()->setVisible(false);

  // Populate the table with module data
  for (size_t i = 0; i < modules.size(); i++) {
    const auto& mod = modules[i];

    // Extract the channel designator from deviceLocator if available
    std::string deviceLocator = "Unknown";
    if (!mod.deviceLocator.empty()) {
      std::string slotStr = mod.deviceLocator;
      std::string::size_type channelPos = slotStr.find_first_of("AB");
      std::string::size_type numPos =
        slotStr.find_first_of("0123456789", channelPos);

      if (channelPos != std::string::npos && numPos != std::string::npos) {
        deviceLocator = slotStr.substr(channelPos, numPos - channelPos + 1);
      }
    }

    // Table items (same as before but optimizing alignment)
    QTableWidgetItem* slotItem =
      !deviceLocator.empty() && deviceLocator != "Unknown"
        ? new QTableWidgetItem(QString::fromStdString(deviceLocator))
        : new QTableWidgetItem(QString::number(mod.slot));
    slotItem->setTextAlignment(Qt::AlignCenter);
    modulesTable->setItem(i, 0, slotItem);

    QTableWidgetItem* capacityItem = new QTableWidgetItem(QString("%1 GB").arg(
      mod.capacityGB, 0, 'f', 0));  // Remove decimal places for cleaner display
    capacityItem->setTextAlignment(Qt::AlignCenter);
    modulesTable->setItem(i, 1, capacityItem);

    // Prioritize configured speed when available
    QString speedText;
    if (mod.configuredSpeedMHz > 0) {
      // Always use configured speed as primary if available
      speedText = QString("%1 MHz").arg(mod.configuredSpeedMHz);
      // Only show base speed in parentheses if significantly different from
      // configured speed
      if (mod.speedMHz > 0 &&
          std::abs(mod.speedMHz - mod.configuredSpeedMHz) > 10) {
        speedText += QString(" (%1)").arg(mod.speedMHz);
      }
    } else if (mod.speedMHz > 0) {
      speedText = QString("%1 MHz").arg(mod.speedMHz);
    } else {
      speedText = "Unknown";
    }

    QTableWidgetItem* speedItem = new QTableWidgetItem(speedText);
    speedItem->setTextAlignment(Qt::AlignCenter);
    modulesTable->setItem(i, 2, speedItem);

    QTableWidgetItem* mfgItem =
      new QTableWidgetItem(QString::fromStdString(mod.manufacturer));
    mfgItem->setTextAlignment(Qt::AlignCenter);
    modulesTable->setItem(i, 3, mfgItem);

    QTableWidgetItem* partItem =
      new QTableWidgetItem(QString::fromStdString(mod.partNumber));
    partItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    modulesTable->setItem(i, 4, partItem);
  }

  // Set the height based on content but with a reasonable maximum
  modulesTable->setFixedHeight(
    std::min(static_cast<int>(modules.size() * 30 + 30), 200));
  moduleSectionLayout->addWidget(modulesTable);

  return moduleSection;
}

QWidget* MemoryResultRenderer::createStabilityTestWidget(
  const DiagnosticDataStore::MemoryData::StabilityTestResults& stabilityTest) {
  QWidget* stabilityWidget = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(stabilityWidget);
  layout->setContentsMargins(0, 10, 0, 0);

  QLabel* titleLabel = new QLabel("<b>Memory Stability Test</b>");
  titleLabel->setStyleSheet("color: #ffffff; font-size: 14px; background: "
                            "transparent; margin-bottom: 5px;");
  layout->addWidget(titleLabel);

  QWidget* contentBox = new QWidget();
  contentBox->setStyleSheet("background-color: #252525;");
  QVBoxLayout* contentLayout = new QVBoxLayout(contentBox);
  contentLayout->setContentsMargins(8, 12, 8, 12);
  contentLayout->setSpacing(6);

  if (!stabilityTest.testPerformed) {
    QLabel* noTestLabel =
      new QLabel("Memory stability test was not performed.");
    noTestLabel->setStyleSheet("color: #AAAAAA; font-style: italic;");
    contentLayout->addWidget(noTestLabel);
  } else {
    // Create a simple status indicator
    QString statusColor = stabilityTest.passed ? "#44FF44" : "#FF6666";
    QString statusText =
      stabilityTest.passed
        ? "PASSED"
        : QString("FAILED with %1 errors").arg(stabilityTest.errorCount);

    QLabel* statusLabel =
      new QLabel(QString("<span style='color: %1;'>%2</span>")
                   .arg(statusColor)
                   .arg(statusText));
    contentLayout->addWidget(statusLabel);

    // Show only basic test information
    QLabel* testInfoLabel =
      new QLabel(QString("Tested %1 MB of memory with %2 loops.")
                   .arg(stabilityTest.testedSizeMB)
                   .arg(stabilityTest.completedLoops));
    testInfoLabel->setStyleSheet("color: #DDDDDD;");
    contentLayout->addWidget(testInfoLabel);

    // Add recommendation if test failed, but keep it minimal
    if (!stabilityTest.passed) {
      QLabel* recommendationLabel = new QLabel(
        "<span style='color: #FFAA00;'>Memory errors detected. Please check "
        "for hardware issues or incorrect memory timings.</span>");
      recommendationLabel->setWordWrap(true);
      contentLayout->addWidget(recommendationLabel);
    }
  }

  layout->addWidget(contentBox);
  return stabilityWidget;
}

QWidget* MemoryResultRenderer::createTimeBar(const QString& label, double value,
                                             const QString& unit) {
  QWidget* container = new QWidget();
  QVBoxLayout* mainLayout = new QVBoxLayout(container);
  mainLayout->setContentsMargins(0, 1, 0, 1);  // Minimal vertical margins
  mainLayout->setSpacing(1);                   // Minimal spacing

  QHBoxLayout* layout = new QHBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);  // No internal margins
  layout->setSpacing(8);

  // Add label at the left side of the horizontal layout
  QLabel* nameLabel = new QLabel(label);
  nameLabel->setStyleSheet(
    "color: #ffffff; background: transparent; font-weight: bold;");
  nameLabel->setFixedWidth(80);               // Fixed width for alignment
  nameLabel->setContentsMargins(0, 0, 0, 0);  // No internal margins
  layout->addWidget(nameLabel);

  QWidget* barContainer = new QWidget();
  barContainer->setFixedHeight(20);
  barContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  barContainer->setStyleSheet("background-color: #333333; border-radius: 2px;");

  QHBoxLayout* barLayout = new QHBoxLayout(barContainer);
  barLayout->setContentsMargins(0, 0, 0, 0);
  barLayout->setSpacing(0);

  // For read/write times - lower is better
  // Define reference values
  double typicalValue = 50.0;  // 50ms is a typical good value
  double maxValue = 200.0;     // Cap at 200ms for visualization

  // Calculate percentage (0-90%) for bar width
  int percentage = int((std::min(value, maxValue) / maxValue) * 90);

  // Create color based on performance (for time, lower is better)
  QString barColor;
  double ratio = value / typicalValue;

  // Create a continuous color scale based on the performance ratio
  int hue, sat = 240, val = 245;  // Base HSV values

  if (ratio <= 0.7) {
    // Excellent performance - pure green (ratio 0.7 or lower)
    hue = 120;
  } else if (ratio >= 1.3) {
    // Poor performance - pure red (ratio 1.3 or higher)
    hue = 0;
  } else {
    // Scale the hue linearly between green and red
    // Map ratio from [0.7, 1.3] to [120, 0]
    double normalizedRatio = (ratio - 0.7) / 0.6;  // 1.3 - 0.7 = 0.6
    hue = static_cast<int>(120 * (1.0 - normalizedRatio));
  }

  // Create the color using HSV
  QColor hsv = QColor::fromHsv(hue, sat, val);
  barColor = hsv.name();

  QWidget* bar = new QWidget();
  bar->setFixedHeight(20);
  bar->setStyleSheet(
    QString("background-color: %1; border-radius: 2px;").arg(barColor));

  QWidget* spacer = new QWidget();
  spacer->setStyleSheet("background-color: transparent;");

  // Use stretch factors for correct proportion
  barLayout->addWidget(bar, percentage);
  barLayout->addWidget(spacer, 100 - percentage);

  layout->addWidget(barContainer);

  // Show the actual value with the same color
  QLabel* valueLabel =
    new QLabel(QString("%1 %2").arg(value, 0, 'f', 1).arg(unit));
  valueLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  valueLabel->setStyleSheet(
    QString("color: %1; background: transparent;").arg(barColor));
  layout->addWidget(valueLabel);

  // Add typical value reference
  QLabel* typicalLabel =
    new QLabel(QString("(typical: %1 ms)").arg(typicalValue, 0, 'f', 1));
  typicalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  typicalLabel->setStyleSheet(
    "color: #888888; font-size: 10px; background: transparent;");
  layout->addWidget(typicalLabel);

  mainLayout->addLayout(layout);
  return container;
}

// Add a new function for bandwidth bars (higher is better)
QWidget* MemoryResultRenderer::createBandwidthBar(const QString& label,
                                                  double value,
                                                  const QString& unit,
                                                  double typicalValue) {
  QWidget* container = new QWidget();
  QVBoxLayout* mainLayout = new QVBoxLayout(container);
  mainLayout->setContentsMargins(0, 1, 0, 1);  // Minimal vertical margins
  mainLayout->setSpacing(1);                   // Minimal spacing

  QHBoxLayout* layout = new QHBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);  // No internal margins
  layout->setSpacing(8);

  // Add label at the left side of the horizontal layout
  QLabel* nameLabel = new QLabel(label);
  nameLabel->setStyleSheet(
    "color: #ffffff; background: transparent; font-weight: bold;");
  nameLabel->setFixedWidth(
    130);  // Wider fixed width for "Random Read/Write Speed" labels
  nameLabel->setContentsMargins(0, 0, 0, 0);  // No internal margins
  layout->addWidget(nameLabel);

  QWidget* barContainer = new QWidget();
  barContainer->setFixedHeight(20);
  barContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  barContainer->setStyleSheet("background-color: #333333; border-radius: 2px;");

  QHBoxLayout* barLayout = new QHBoxLayout(barContainer);
  barLayout->setContentsMargins(0, 0, 0, 0);
  barLayout->setSpacing(0);

  // For bandwidth - higher is better
  // Define reference values for GB/s
  double maxValue = 10.0;  // Cap at 10 GB/s for visualization
  // typicalValue is now a parameter

  // Calculate percentage (0-90%) for bar width
  int percentage = int((std::min(value, maxValue) / maxValue) * 90);

  // Create color based on performance (for bandwidth, higher is better)
  QString barColor;
  double ratio = value / typicalValue;

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
  barColor = hsv.name();

  QWidget* bar = new QWidget();
  bar->setFixedHeight(20);
  bar->setStyleSheet(
    QString("background-color: %1; border-radius: 2px;").arg(barColor));

  QWidget* spacer = new QWidget();
  spacer->setStyleSheet("background-color: transparent;");

  // Use stretch factors for correct proportion
  barLayout->addWidget(bar, percentage);
  barLayout->addWidget(spacer, 100 - percentage);

  layout->addWidget(barContainer);

  // Show the actual value with the same color
  QLabel* valueLabel =
    new QLabel(QString("%1 %2").arg(value, 0, 'f', 1).arg(unit));
  valueLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  valueLabel->setStyleSheet(
    QString("color: %1; background: transparent;").arg(barColor));
  layout->addWidget(valueLabel);

  // Add typical value reference
  QLabel* typicalLabel =
    new QLabel(QString("(typical: %1 GB/s)").arg(typicalValue, 0, 'f', 1));
  typicalLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  typicalLabel->setStyleSheet(
    "color: #888888; font-size: 10px; background: transparent;");
  layout->addWidget(typicalLabel);

  mainLayout->addLayout(layout);
  return container;
}

// Add these implementations after the existing methods

std::map<QString, MemoryComparisonData> MemoryResultRenderer::
  loadMemoryComparisonData() {
  std::map<QString, MemoryComparisonData> comparisonData;

  // Find the comparison_data folder
  QString appDir = QApplication::applicationDirPath();
  QDir dataDir(appDir + "/comparison_data");

  if (!dataDir.exists()) {
    return comparisonData;
  }

  // Look for RAM benchmark JSON files - use correct pattern
  // "ram_benchmark_*.json"
  QStringList filters;
  filters << "ram_benchmark_*.json";
  dataDir.setNameFilters(filters);

  QStringList memFiles = dataDir.entryList(QDir::Files);

  for (const QString& fileName : memFiles) {
    QFile file(dataDir.absoluteFilePath(fileName));

    if (file.open(QIODevice::ReadOnly)) {
      QByteArray jsonData = file.readAll();
      QJsonDocument doc = QJsonDocument::fromJson(jsonData);

      if (doc.isObject()) {
        QJsonObject rootObj = doc.object();

        MemoryComparisonData mem;
        mem.type = rootObj["type"].toString();
        mem.totalMemoryGB = rootObj["total_memory_gb"].toDouble();
        mem.frequencyMHz = 0;

        // Prioritize configured speed from modules if available
        bool configuredSpeedFound = false;

        // First check modules for configured_clock_speed_mhz
        if (rootObj.contains("modules") && rootObj["modules"].isArray()) {
          QJsonArray modulesArray = rootObj["modules"].toArray();
          if (modulesArray.size() > 0 && modulesArray[0].isObject()) {
            QJsonObject firstModule = modulesArray[0].toObject();
            // Try configured speed first
            if (firstModule.contains("configured_clock_speed_mhz") &&
                firstModule["configured_clock_speed_mhz"].toInt() > 0) {
              mem.frequencyMHz =
                firstModule["configured_clock_speed_mhz"].toInt();
              configuredSpeedFound = true;
            }
          }
        }

        // If configured speed not found, try other sources
        if (!configuredSpeedFound) {
          // Check frequency from primary source
          if (rootObj["frequency_mhz"].isDouble()) {
            mem.frequencyMHz = rootObj["frequency_mhz"].toInt();
          } else if (rootObj["frequency_mhz"].isString() &&
                     rootObj["frequency_mhz"].toString() != "N/A") {
            bool ok;
            int freq = rootObj["frequency_mhz"].toString().toInt(&ok);
            if (ok && freq > 0) {
              mem.frequencyMHz = freq;
            }
          }

          // If still not found, check modules again for base speed
          if (mem.frequencyMHz <= 0 && rootObj.contains("modules") &&
              rootObj["modules"].isArray()) {
            QJsonArray modulesArray = rootObj["modules"].toArray();
            if (modulesArray.size() > 0 && modulesArray[0].isObject()) {
              QJsonObject firstModule = modulesArray[0].toObject();
              // Fall back to base speed
              if (firstModule.contains("speed_mhz") &&
                  firstModule["speed_mhz"].toInt() > 0) {
                mem.frequencyMHz = firstModule["speed_mhz"].toInt();
              }
            }
          }
        }

        // Last resort: Try to extract from system_id if it contains MHz
        if (mem.frequencyMHz <= 0 && rootObj.contains("system_id")) {
          QString systemId = rootObj["system_id"].toString();
          QRegularExpression regex("(\\d+)\\s*MHz");
          QRegularExpressionMatch match = regex.match(systemId);
          if (match.hasMatch()) {
            mem.frequencyMHz = match.captured(1).toInt();
          }
        }

        mem.channelStatus = rootObj["channel_status"].toString();
        mem.xmpEnabled = rootObj["xmp_enabled"].toBool();

        // Get benchmark results
        if (rootObj.contains("benchmark_results") &&
            rootObj["benchmark_results"].isObject()) {
          QJsonObject resultsObj = rootObj["benchmark_results"].toObject();
          mem.bandwidthMBs = resultsObj["bandwidth_mb_s"].toDouble();
          mem.latencyNs = resultsObj["latency_ns"].toDouble();
          mem.readTimeGBs = resultsObj["read_time_gb_s"].toDouble();
          mem.writeTimeGBs = resultsObj["write_time_gb_s"].toDouble();
        }

        // Get module info
        mem.moduleCount = 0;
        mem.moduleCapacityGB = 0;

        if (rootObj.contains("modules") && rootObj["modules"].isArray()) {
          QJsonArray modulesArray = rootObj["modules"].toArray();
          mem.moduleCount = modulesArray.size();

          // Get capacity of first module
          if (mem.moduleCount > 0 && modulesArray[0].isObject()) {
            mem.moduleCapacityGB =
              modulesArray[0].toObject()["capacity_gb"].toDouble();
          }
        }

        // Create a more descriptive display name - no longer include memory size
        QString displayName;
        // Include only type and frequency
        displayName = QString("%1").arg(mem.type);

        // Add frequency if we have it
        if (mem.frequencyMHz > 0) {
          displayName += QString(" %1MHz").arg(mem.frequencyMHz);
        } else {
          displayName += " (Unknown MHz)";
        }

        // Add channel status to make the name more descriptive
        if (!mem.channelStatus.isEmpty()) {
          // Simplify channel status for the display
          if (mem.channelStatus.contains("Dual Channel", Qt::CaseInsensitive)) {
            displayName += " Dual Channel";
          } else if (mem.channelStatus.contains("Single Channel",
                                                Qt::CaseInsensitive)) {
            displayName += " Single Channel";
          } else if (mem.channelStatus.contains("Quad Channel",
                                                Qt::CaseInsensitive)) {
            displayName += " Quad Channel";
          } else {
            displayName += " " + mem.channelStatus;
          }
        }

        // Add XMP status
        displayName += mem.xmpEnabled ? " (XMP)" : "";

        comparisonData[displayName] = mem;
      }

      file.close();
    }
  }

  return comparisonData;
}

QComboBox* MemoryResultRenderer::createMemoryComparisonDropdown(
  const std::map<QString, MemoryComparisonData>& comparisonData,
  QWidget* containerWidget, const QPair<double, double>& bandwidthVals,
  const QPair<double, double>& latencyVals,
  const QPair<double, double>& readTimeVals,
  const QPair<double, double>& writeTimeVals, DownloadApiClient* downloadClient) {

  // Generate aggregated data from individual results
  auto aggregatedData = generateAggregatedMemoryData(comparisonData);

  // Create a callback function to handle selection changes
  auto selectionCallback = [containerWidget, bandwidthVals, latencyVals,
                            readTimeVals, writeTimeVals, downloadClient](
                             const QString& componentName,
                             const QString& originalFullName,
                             DiagnosticViewComponents::AggregationType type,
                             const MemoryComparisonData& memData) {
  LOG_INFO << "MemoryResultRenderer: selectionCallback invoked: component='"
       << componentName.toStdString() << "', originalFullName='"
       << originalFullName.toStdString() << "', aggType='"
       << (type == DiagnosticViewComponents::AggregationType::Best ? "Best" : "Avg")
       << "', havePerfData=" << (memData.bandwidthMBs > 0);
    
    // If downloadClient is available and memData has no performance data (only name), 
    // fetch the actual data from the server
  if (downloadClient && !componentName.isEmpty() && memData.bandwidthMBs <= 0) {
      LOG_INFO << "MemoryResultRenderer: Fetching network data for Memory: " << componentName.toStdString() << " using original name: " << originalFullName.toStdString();
      
      downloadClient->fetchComponentData("memory", originalFullName, 
        [containerWidget, bandwidthVals, latencyVals, readTimeVals, writeTimeVals, componentName, type]
        (bool success, const ComponentData& networkData, const QString& error) {
          
          if (success) {
            LOG_INFO << "MemoryResultRenderer: Successfully fetched Memory data for " << componentName.toStdString();
            
            // Convert network data to MemoryComparisonData
            MemoryComparisonData fetchedMemData = convertNetworkDataToMemory(networkData);
            
            // Find all comparison bars
            QList<QWidget*> allBars = containerWidget->findChildren<QWidget*>(
              QRegularExpression("^comparison_bar_"));
            
            // Create display name
            QString displayName = componentName + " (" +
              (type == DiagnosticViewComponents::AggregationType::Best ? "Best)" : "Avg)");
            
            LOG_INFO << "MemoryResultRenderer: Updating comparison bars with fetched data";
            
            // Build test data with the fetched values
            struct TestData {
              QString objectName;
              double value;
              QString unit;
            };
            
            std::vector<TestData> tests = {
              {"comparison_bar_bandwidth", fetchedMemData.bandwidthMBs / 1000.0, "GB/s"},
              {"comparison_bar_latency", fetchedMemData.latencyNs, "ns"},
              {"comparison_bar_read", fetchedMemData.readTimeGBs, "GB/s"},
              {"comparison_bar_write", fetchedMemData.writeTimeGBs, "GB/s"}
            };
            
            // Update each comparison bar with fetched data
            for (QWidget* bar : allBars) {
              QWidget* parentContainer = bar->parentWidget();
              if (parentContainer) {
                QLabel* nameLabel = parentContainer->findChild<QLabel*>("comp_name_label");
                if (nameLabel) {
                  nameLabel->setText(displayName);
                  nameLabel->setStyleSheet("color: #ffffff; background: transparent;");
                }
                
                // Update the value label and bar based on bar type
                for (const auto& test : tests) {
                  if (bar->objectName() == test.objectName && test.value > 0) {
                    LOG_INFO << "MemoryResultRenderer: Updating bar " << test.objectName.toStdString() 
                             << " with value " << test.value;
                    
                    QLabel* valueLabel = bar->parentWidget()->findChild<QLabel*>("value_label");
                    if (valueLabel) {
                      valueLabel->setText(QString("%1 %2").arg(test.value, 0, 'f', 1).arg(test.unit));
                      valueLabel->setStyleSheet("color: #FF4444; background: transparent;");
                    }
                    
                    // Also update the bar visual (simplified approach) 
                    QLayout* layout = bar->layout();
                    if (layout) {
                      // Remove existing items
                      QLayoutItem* child;
                      while ((child = layout->takeAt(0)) != nullptr) {
                        delete child->widget();
                        delete child;
                      }
                      
                      // Calculate percentage based on appropriate max value
                      double maxValue = 1.0;
                      if (test.objectName == "comparison_bar_bandwidth") {
                        maxValue = bandwidthVals.second * 1.25;
                      } else if (test.objectName == "comparison_bar_latency") {
                        maxValue = latencyVals.second * 1.25;
                      } else if (test.objectName == "comparison_bar_read") {
                        maxValue = readTimeVals.second * 1.25;
                      } else if (test.objectName == "comparison_bar_write") {
                        maxValue = writeTimeVals.second * 1.25;
                      }
                      
                      int percentage = test.value <= 0 ? 0 : 
                        static_cast<int>(std::min(100.0, (test.value / maxValue) * 100.0));
                      
                      // Create a comparison bar
                      QWidget* barWidget = new QWidget();
                      barWidget->setFixedHeight(16);
                      barWidget->setStyleSheet("background-color: #FF4444; border-radius: 2px;");
                      
                      QWidget* spacer = new QWidget();
                      spacer->setStyleSheet("background-color: transparent;");
                      
                      QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
                      if (newLayout) {
                        newLayout->addWidget(barWidget, percentage);
                        newLayout->addWidget(spacer, 100 - percentage);
                      }
                    }
                    break;
                  }
                }
              }
            }
            
          } else {
            LOG_ERROR << "MemoryResultRenderer: Failed to fetch Memory data for " << componentName.toStdString() 
                      << ": " << error.toStdString();
            // Continue with empty/placeholder data
          }
        });
      
      return; // Exit early - the network callback will handle the UI update
    }
    // Find all comparison bars
    QList<QWidget*> allBars = containerWidget->findChildren<QWidget*>(
      QRegularExpression("^comparison_bar_"));

    if (componentName.isEmpty()) {
      LOG_WARN << "MemoryResultRenderer: Empty component selection; resetting bars.";
      // Reset all bars if user selects the placeholder option
      for (QWidget* bar : allBars) {
        QLabel* valueLabel = bar->findChild<QLabel*>("value_label");
        QLabel* nameLabel =
          bar->parentWidget()->findChild<QLabel*>("comp_name_label");

        if (valueLabel) {
          valueLabel->setText("-");
          valueLabel->setStyleSheet(
            "color: #888888; font-style: italic; background: transparent;");
        }

        if (nameLabel) {
          nameLabel->setText("Select memory kit to compare");
          nameLabel->setStyleSheet(
            "color: #888888; font-style: italic; background: transparent;");
        }

        QLayout* layout = bar->layout();
        if (layout) {
          // Clear existing layout
          QLayoutItem* child;
          while ((child = layout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
          }

          // Add empty placeholder
          QWidget* emptyBar = new QWidget();
          emptyBar->setStyleSheet("background-color: transparent;");

          QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
          if (newLayout) {
            newLayout->addWidget(emptyBar);
          }
        }
      }
      return;
    }

    // Structure to hold test data for updating bars
    struct TestData {
      QString objectName;
      double value;
      double maxValue;
      QString unit;
      bool lowerIsBetter;
    };

    // Create display name with aggregation type
    QString displayName =
      componentName + " (" +
      (type == DiagnosticViewComponents::AggregationType::Best ? "Best)"
                                                               : "Avg)");

    std::vector<TestData> tests = {
      {"comparison_bar_bandwidth", memData.bandwidthMBs / 1024.0,
       bandwidthVals.second, "GB/s", false},
      {"comparison_bar_latency", memData.latencyNs, latencyVals.second, "ns",
       true},
      {"comparison_bar_read", memData.readTimeGBs, readTimeVals.second, "GB/s",
       false},
      {"comparison_bar_write", memData.writeTimeGBs, writeTimeVals.second,
       "GB/s", false}};

    // Update all comparison bars
    for (QWidget* bar : allBars) {
      QWidget* parentContainer = bar->parentWidget();
      if (parentContainer) {
        QLabel* nameLabel =
          parentContainer->findChild<QLabel*>("comp_name_label");
        if (nameLabel) {
          nameLabel->setText(displayName);
          nameLabel->setStyleSheet("color: #ffffff; background: transparent;");
        }

        for (const auto& test : tests) {
          if (bar->objectName() == test.objectName) {
            // Find the value label and update it
            QLabel* valueLabel =
              bar->parentWidget()->findChild<QLabel*>("value_label");
            if (valueLabel) {
              valueLabel->setText(
                QString("%1 %2").arg(test.value, 0, 'f', 1).arg(test.unit));
              valueLabel->setStyleSheet(
                "color: #FF4444; background: transparent;");
            }

            // Update the bar width with scaled value
            QLayout* layout = bar->layout();
            if (layout) {
              // Remove existing items
              QLayoutItem* child;
              while ((child = layout->takeAt(0)) != nullptr) {
                delete child->widget();
                delete child;
              }

              // Calculate scaled percentage (0-100%)
              double scaledMaxValue =
                test.maxValue *
                1.25;  // Use same scaling as in createComparisonPerformanceBar
              int percentage =
                test.value <= 0
                  ? 0
                  : static_cast<int>(
                      std::min(100.0, (test.value / scaledMaxValue) * 100.0));

              // Create bar and spacer
              QWidget* barWidget = new QWidget();
              barWidget->setFixedHeight(16);
              barWidget->setStyleSheet(
                "background-color: #FF4444; border-radius: 2px;");

              QWidget* spacer = new QWidget();
              spacer->setStyleSheet("background-color: transparent;");

              QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
              if (newLayout) {
                newLayout->addWidget(barWidget, percentage);
                newLayout->addWidget(spacer, 100 - percentage);
              }
            }

            // Also update percentage difference with user's result
            QWidget* userBar =
              parentContainer->findChild<QWidget*>("userBarContainer");
            if (userBar) {
              // Find if there's an existing percentage label to remove
              QLabel* existingLabel =
                userBar->findChild<QLabel*>("percentageLabel");
              if (existingLabel) {
                delete existingLabel;
              }

              // Get the matching user value to calculate percentage
              double userValue = 0;
              if (test.objectName == "comparison_bar_bandwidth")
                userValue = bandwidthVals.first;
              else if (test.objectName == "comparison_bar_latency")
                userValue = latencyVals.first;
              else if (test.objectName == "comparison_bar_read")
                userValue = readTimeVals.first;
              else if (test.objectName == "comparison_bar_write")
                userValue = writeTimeVals.first;

              // Only add percentage if we have valid values
              if (userValue > 0 && test.value > 0) {
                // Calculate percentage difference
                double percentChange = 0;
                if (test.lowerIsBetter) {
                  // For lower-is-better metrics, negative percent means user is
                  // better
                  percentChange = ((userValue / test.value) - 1.0) * 100.0;
                } else {
                  // For higher-is-better metrics, positive percent means user
                  // is better
                  percentChange = ((userValue / test.value) - 1.0) * 100.0;
                }

                QString percentText;
                QString percentColor;

                // Determine if user result is better or worse
                bool isBetter = (test.lowerIsBetter && percentChange < 0) ||
                                (!test.lowerIsBetter && percentChange > 0);
                bool isApproxEqual = qAbs(percentChange) < 1.0;

                if (isApproxEqual) {
                  percentText = "≈";
                  percentColor = "#FFAA00";  // Yellow for equal
                } else {
                  percentText =
                    QString("%1%2%")
                      .arg(isBetter ? "+"
                                    : "")  // Add + prefix for better results
                      .arg(percentChange, 0, 'f', 1);
                  percentColor =
                    isBetter ? "#44FF44"
                             : "#FF4444";  // Green for better, red for worse
                }

                // Create an overlay layout if it doesn't exist
                QHBoxLayout* overlayLayout =
                  userBar->findChild<QHBoxLayout*>("overlayLayout");
                if (!overlayLayout) {
                  overlayLayout = new QHBoxLayout(userBar);
                  overlayLayout->setObjectName("overlayLayout");
                  overlayLayout->setContentsMargins(0, 0, 0, 0);
                }

                // Create and add percentage label
                QLabel* percentageLabel = new QLabel(percentText);
                percentageLabel->setObjectName("percentageLabel");
                percentageLabel->setStyleSheet(
                  QString(
                    "color: %1; background: transparent; font-weight: bold;")
                    .arg(percentColor));
                percentageLabel->setAlignment(Qt::AlignCenter);
                overlayLayout->addWidget(percentageLabel);
              }
            }

            break;
          }
        }
      }
    }
  };

  // Use the shared helper to create the dropdown
  return DiagnosticViewComponents::createAggregatedComparisonDropdown<
    MemoryComparisonData>(aggregatedData, selectionCallback);
}

// Add this function after the loadMemoryComparisonData function
std::map<QString, DiagnosticViewComponents::AggregatedComponentData<MemoryComparisonData>> MemoryResultRenderer::
  generateAggregatedMemoryData(
    const std::map<QString, MemoryComparisonData>& individualData) {
  std::map<QString, DiagnosticViewComponents::AggregatedComponentData<
                      MemoryComparisonData>>
    result;

  // Group results by memory type and frequency only, ignoring total capacity
  std::map<QString, std::vector<std::pair<QString, MemoryComparisonData>>>
    groupedData;

  for (const auto& [id, data] : individualData) {
    // Create a key that uniquely identifies this type of memory kit
    // Only use type and frequency - removed total memory size from key
    QString kitKey = QString("%1 %2MHz").arg(data.type).arg(data.frequencyMHz);

    // Add to the corresponding group
    groupedData[kitKey].push_back({id, data});
  }

  // Create aggregated data for each memory kit type
  for (const auto& [kitName, dataList] : groupedData) {
    DiagnosticViewComponents::AggregatedComponentData<MemoryComparisonData>
      aggregated;
    aggregated.componentName = kitName;
    if (!dataList.empty()) {
      // Preserve the original full identifier from the first entry (menu key)
      aggregated.originalFullName = dataList[0].first;
      LOG_INFO << "MemoryResultRenderer: Aggregated '" << kitName.toStdString()
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

      // For lower-is-better metrics, find minimum values
      double minLatencyNs = firstData.latencyNs;

      // For higher-is-better metrics, find maximum values
      double maxBandwidthMBs = firstData.bandwidthMBs;
      double maxReadTimeGBs = firstData.readTimeGBs;
      double maxWriteTimeGBs = firstData.writeTimeGBs;

      // Initialize sums for averages
      double sumLatencyNs = firstData.latencyNs;
      double sumBandwidthMBs = firstData.bandwidthMBs;
      double sumReadTimeGBs = firstData.readTimeGBs;
      double sumWriteTimeGBs = firstData.writeTimeGBs;

      // Process all entries after the first
      for (size_t i = 1; i < dataList.size(); i++) {
        const auto& data = dataList[i].second;

        // Update minimums for lower-is-better metrics
        if (data.latencyNs > 0) {
          minLatencyNs = std::min(minLatencyNs, data.latencyNs);
          sumLatencyNs += data.latencyNs;
        }

        // Update maximums for higher-is-better metrics
        if (data.bandwidthMBs > 0) {
          maxBandwidthMBs = std::max(maxBandwidthMBs, data.bandwidthMBs);
          sumBandwidthMBs += data.bandwidthMBs;
        }

        if (data.readTimeGBs > 0) {
          maxReadTimeGBs = std::max(maxReadTimeGBs, data.readTimeGBs);
          sumReadTimeGBs += data.readTimeGBs;
        }

        if (data.writeTimeGBs > 0) {
          maxWriteTimeGBs = std::max(maxWriteTimeGBs, data.writeTimeGBs);
          sumWriteTimeGBs += data.writeTimeGBs;
        }
      }

      // Set the best result values
      aggregated.bestResult.latencyNs = minLatencyNs;
      aggregated.bestResult.bandwidthMBs = maxBandwidthMBs;
      aggregated.bestResult.readTimeGBs = maxReadTimeGBs;
      aggregated.bestResult.writeTimeGBs = maxWriteTimeGBs;

      // Calculate averages
      size_t count = dataList.size();
      aggregated.averageResult.latencyNs = sumLatencyNs / count;
      aggregated.averageResult.bandwidthMBs = sumBandwidthMBs / count;
      aggregated.averageResult.readTimeGBs = sumReadTimeGBs / count;
      aggregated.averageResult.writeTimeGBs = sumWriteTimeGBs / count;

      // Copy memory kit info from first result (these should be the same for
      // all runs of the same kit type)
      aggregated.bestResult.type = kitName.split(' ').at(0);
      aggregated.bestResult.frequencyMHz = firstData.frequencyMHz;

      // Same for average result
      aggregated.averageResult.type = kitName.split(' ').at(0);
      aggregated.averageResult.frequencyMHz = firstData.frequencyMHz;

      // For channel status and XMP, use the most common configuration
      std::map<QString, int> channelCounts;
      std::map<bool, int> xmpCounts;

      for (const auto& [_, data] : dataList) {
        channelCounts[data.channelStatus]++;
        xmpCounts[data.xmpEnabled]++;
      }

      // Find most common channel status
      QString mostCommonChannel = firstData.channelStatus;
      int maxChannelCount = 0;
      for (const auto& [channel, count] : channelCounts) {
        if (count > maxChannelCount) {
          maxChannelCount = count;
          mostCommonChannel = channel;
        }
      }

      // Find most common XMP setting
      bool mostCommonXmp = firstData.xmpEnabled;
      int xmpEnabledCount = xmpCounts[true];
      int xmpDisabledCount = xmpCounts[false];
      if (xmpDisabledCount > xmpEnabledCount) {
        mostCommonXmp = false;
      }

      // Set the channel and XMP values
      aggregated.bestResult.channelStatus = mostCommonChannel;
      aggregated.bestResult.xmpEnabled = mostCommonXmp;
      aggregated.averageResult.channelStatus = mostCommonChannel;
      aggregated.averageResult.xmpEnabled = mostCommonXmp;
    }

    // Add to the result map
    result[kitName] = aggregated;
  }

  return result;
}

// Network-based method to convert ComponentData to MemoryComparisonData
MemoryComparisonData MemoryResultRenderer::convertNetworkDataToMemory(const ComponentData& networkData) {
  MemoryComparisonData mem;
  
  LOG_INFO << "MemoryResultRenderer: Converting network data to memory comparison data";
  
  // Log the full JSON as plain text for debugging
  QJsonDocument doc(networkData.testData);
  QString jsonString = doc.toJson(QJsonDocument::Indented);
  LOG_INFO << "MemoryResultRenderer: Received JSON data (plain text):\n" << jsonString.toStdString();
  
  // Parse the testData which contains the full JSON structure
  QJsonObject rootData = networkData.testData;
  
  // Extract performance metrics from nested benchmark_results (protobuf structure)
  if (rootData.contains("benchmark_results") && rootData["benchmark_results"].isObject()) {
    QJsonObject results = rootData["benchmark_results"].toObject();
    mem.bandwidthMBs = results.value("bandwidth_mb_s").toDouble();
    if (mem.bandwidthMBs <= 0) mem.bandwidthMBs = results.value("bandwidthMbS").toDouble();
    mem.latencyNs = results.value("latency_ns").toDouble();
    if (mem.latencyNs <= 0) mem.latencyNs = results.value("latencyNs").toDouble();
    mem.readTimeGBs = results.value("read_time_gb_s").toDouble();
    if (mem.readTimeGBs <= 0) mem.readTimeGBs = results.value("readTimeGbS").toDouble();
    mem.writeTimeGBs = results.value("write_time_gb_s").toDouble();
    if (mem.writeTimeGBs <= 0) mem.writeTimeGBs = results.value("writeTimeGbS").toDouble();
  } else if (rootData.contains("benchmarkResults") && rootData["benchmarkResults"].isObject()) {
    // Fallback for camelCase container
    QJsonObject results = rootData["benchmarkResults"].toObject();
    mem.bandwidthMBs = results.value("bandwidth_mb_s").toDouble();
    if (mem.bandwidthMBs <= 0) mem.bandwidthMBs = results.value("bandwidthMbS").toDouble();
    mem.latencyNs = results.value("latency_ns").toDouble();
    if (mem.latencyNs <= 0) mem.latencyNs = results.value("latencyNs").toDouble();
    mem.readTimeGBs = results.value("read_time_gb_s").toDouble();
    if (mem.readTimeGBs <= 0) mem.readTimeGBs = results.value("readTimeGbS").toDouble();
    mem.writeTimeGBs = results.value("write_time_gb_s").toDouble();
    if (mem.writeTimeGBs <= 0) mem.writeTimeGBs = results.value("writeTimeGbS").toDouble();
  } else {
    // Fallbacks if server ever sends flattened fields
    mem.bandwidthMBs = rootData.value("bandwidth_mb_s").toDouble();
    if (mem.bandwidthMBs <= 0) mem.bandwidthMBs = rootData.value("bandwidthMbS").toDouble();
    mem.latencyNs = rootData.value("latency_ns").toDouble();
    if (mem.latencyNs <= 0) mem.latencyNs = rootData.value("latencyNs").toDouble();
    mem.readTimeGBs = rootData.value("read_time_gb_s").toDouble();
    if (mem.readTimeGBs <= 0) mem.readTimeGBs = rootData.value("readTimeGbS").toDouble();
    mem.writeTimeGBs = rootData.value("write_time_gb_s").toDouble();
    if (mem.writeTimeGBs <= 0) mem.writeTimeGBs = rootData.value("writeTimeGbS").toDouble();
  }
  
  LOG_INFO << "MemoryResultRenderer: Performance data - bandwidth=" << mem.bandwidthMBs
           << "MB/s, latency=" << mem.latencyNs << "ns, read=" << mem.readTimeGBs 
           << "GB/s, write=" << mem.writeTimeGBs << "GB/s";
  
  // For network data, set reasonable defaults since component name is not available
  mem.type = "DDR4"; // Default assumption
  mem.frequencyMHz = 0; // Unknown from direct data
  mem.totalMemoryGB = 0; // Unknown from direct data 
  mem.channelStatus = ""; // Unknown for network data
  mem.xmpEnabled = false; // Unknown for network data
  mem.moduleCount = 0; // Unknown for network data
  mem.moduleCapacityGB = 0; // Unknown for network data
  
  LOG_INFO << "MemoryResultRenderer: Conversion complete";
  return mem;
}

// Create dropdown data structure from menu (names only, no performance data yet)
std::map<QString, MemoryComparisonData> MemoryResultRenderer::createDropdownDataFromMenu(const MenuData& menuData) {
  std::map<QString, MemoryComparisonData> dropdownData;
  
  LOG_INFO << "MemoryResultRenderer: Creating dropdown data from menu with " 
           << menuData.availableMemory.size() << " memory options";
  
  // Create placeholder entries for each memory type in the menu
  for (const QString& memoryName : menuData.availableMemory) {
    MemoryComparisonData placeholder;
    placeholder.type = memoryName; // Use the full name as provided
    placeholder.totalMemoryGB = 0;
    placeholder.frequencyMHz = 0;
    placeholder.channelStatus = "";
    placeholder.xmpEnabled = false;
    
    // Performance metrics are 0 initially (will be loaded on demand)
    placeholder.bandwidthMBs = 0;
    placeholder.latencyNs = 0;
    placeholder.readTimeGBs = 0;
    placeholder.writeTimeGBs = 0;
    
    placeholder.moduleCount = 0;
    placeholder.moduleCapacityGB = 0;
    
    dropdownData[memoryName] = placeholder;
    
    LOG_INFO << "MemoryResultRenderer: Added memory option: " << memoryName.toStdString();
  }
  
  return dropdownData;
}

}  // namespace DiagnosticRenderers
