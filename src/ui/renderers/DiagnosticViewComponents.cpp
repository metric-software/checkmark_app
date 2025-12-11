#include "DiagnosticViewComponents.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextEdit>

#include "hardware/ConstantSystemInfo.h"  // Add this include

namespace DiagnosticViewComponents {

QWidget* createMetricBox(const QString& title, const QString& value,
                         const QString& color) {
  QWidget* box = new QWidget();
  box->setStyleSheet(R"(
        QWidget {
            background-color: #252525;
            border: 1px solid #383838;
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

QWidget* createPerformanceGauge(const QString& label, double value,
                                double maxValue, const QString& unit) {
  QWidget* container = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 4, 0, 4);
  layout->setSpacing(8);

  QLabel* nameLabel = new QLabel(label);
  nameLabel->setFixedWidth(60);
  nameLabel->setStyleSheet("color: #ffffff; background: transparent;");
  layout->addWidget(nameLabel);

  QWidget* gaugeContainer = new QWidget();
  gaugeContainer->setFixedHeight(20);
  gaugeContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  gaugeContainer->setStyleSheet(
    "background-color: #333333; border-radius: 2px;");

  QHBoxLayout* gaugeLayout = new QHBoxLayout(gaugeContainer);
  gaugeLayout->setContentsMargins(0, 0, 0, 0);
  gaugeLayout->setSpacing(0);

  QWidget* bar = new QWidget();
  int percentage = int((value / maxValue) * 100);
  int width = (percentage < 100) ? percentage : 100;
  bar->setFixedWidth(width * gaugeContainer->width() / 100);

  // Color based on percentage
  QString color;
  if (percentage >= 80)
    color = "#44FF44";  // Green for excellent
  else if (percentage >= 60)
    color = "#88FF88";  // Light green for good
  else if (percentage >= 40)
    color = "#FFAA00";  // Orange for average
  else
    color = "#FF6666";  // Red for poor

  bar->setStyleSheet(
    QString("background-color: %1; border-radius: 2px;").arg(color));
  gaugeLayout->addWidget(bar);
  gaugeLayout->addStretch();

  layout->addWidget(gaugeContainer);

  QLabel* valueLabel =
    new QLabel(QString("%1 %2").arg(value, 0, 'f', 1).arg(unit));
  valueLabel->setFixedWidth(100);
  valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  valueLabel->setStyleSheet(
    QString("color: %1; background: transparent;").arg(color));
  layout->addWidget(valueLabel);

  return container;
}

QTableWidget* createResultsTable(const QStringList& headers, int rows) {
  QTableWidget* table = new QTableWidget(rows, headers.size());
  table->setHorizontalHeaderLabels(headers);
  table->setStyleSheet(
    "background-color: #252525; color: #ffffff; border: 1px solid #383838;");
  table->horizontalHeader()->setStretchLastSection(true);
  table->verticalHeader()->setVisible(false);
  return table;
}

QString formatMemoryResultString(
  const DiagnosticDataStore::MemoryData& memData) {
  QString result;

  // Memory type and configuration info
  result += "Memory Type: " + QString::fromStdString(memData.memoryType) + "\n";
  result +=
    "Channel Configuration: " + QString::fromStdString(memData.channelStatus) +
    "\n";
  result +=
    "XMP Profile: " + QString(memData.xmpEnabled ? "Enabled" : "Disabled") +
    "\n\n";

  // Performance metrics
  result += QString("Memory Performance:\n");
  result += QString("Bandwidth: %1 MB/s\n").arg(memData.bandwidth, 0, 'f', 2);
  result += QString("Latency: %1 ns\n").arg(memData.latency, 0, 'f', 2);
  result += QString("Read Time: %1 ms\n").arg(memData.readTime, 0, 'f', 2);
  result += QString("Write Time: %1 ms\n\n").arg(memData.writeTime, 0, 'f', 2);

  // Module information
  result += QString("Memory Modules (%1):\n").arg(memData.modules.size());
  for (const auto& module : memData.modules) {
    result += QString("Slot %1: %2 GB %3 MHz %4 %5\n")
                .arg(module.slot)
                .arg(module.capacityGB, 0, 'f', 2)
                .arg(module.speedMHz)
                .arg(QString::fromStdString(module.manufacturer))
                .arg(QString::fromStdString(module.partNumber));
  }

  return result;
}

QString formatStorageSize(unsigned long long bytes) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  int unitIndex = 0;
  double size = bytes;

  while (size >= 1024 && unitIndex < 4) {
    size /= 1024;
    unitIndex++;
  }

  return QString("%1 %2").arg(size, 0, 'f', 2).arg(units[unitIndex]);
}

QWidget* createRawDataWidget(const QString& result) {
  QWidget* rawDataContainer = new QWidget();
  QVBoxLayout* rawDataLayout = new QVBoxLayout(rawDataContainer);

  QPushButton* showRawDataBtn = new QPushButton("▼ Show Raw Data");
  showRawDataBtn->setStyleSheet(R"(
        QPushButton {
            color: #0078d4;
            border: none;
            text-align: left;
            padding: 4px;
            font-size: 12px;
            background: transparent;
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

QWidget* createStorageAnalysisWidget(
  const StorageAnalysis::AnalysisResults& results) {
  QWidget* widget = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(widget);

  QString html;

  // Add summary statistics at the top
  html += "<h3>Analysis Summary:</h3>";
  html += QString("<p><b>Scanned:</b> %1 files, %2 folders<br>")
            .arg(results.totalFilesScanned)
            .arg(results.totalFoldersScanned);

  // Convert duration to seconds for display
  double durationSeconds = results.actualDuration.count() / 1000.0;
  html +=
    QString("<b>Duration:</b> %1 seconds").arg(durationSeconds, 0, 'f', 1);

  if (results.timedOut) {
    html +=
      " <span style='color: #ffaa00;'>(Timed out - partial results)</span>";
  }
  html += "</p><br>";

  // Show folders
  html += "<h3>Largest Folders:</h3><br>";
  const auto numFoldersToShow =
    std::min<size_t>(30, results.largestFolders.size());
  for (size_t i = 0; i < numFoldersToShow; i++) {
    const auto& result = results.largestFolders[i];
    QString path = QString::fromStdWString(result.first);
    QString size = formatStorageSize(result.second);
    html += QString("%1. <a href=\"file:///%2\">%2</a> - %3<br>")
              .arg(i + 1)
              .arg(path)
              .arg(size);
  }

  // Show files with links to containing directory
  html += "<br><h3>Largest Files:</h3><br>";
  const auto numFilesToShow = std::min<size_t>(30, results.largestFiles.size());
  for (size_t i = 0; i < numFilesToShow; i++) {
    const auto& result = results.largestFiles[i];
    QString filePath = QString::fromStdWString(result.first);
    QString dirPath = QFileInfo(filePath).absolutePath();
    QString size = formatStorageSize(result.second);

    // Show file name but link to directory
    html +=
      QString("%1. %2 <a href=\"file:///%3\">(Open Location)</a> - %4<br>")
        .arg(i + 1)
        .arg(filePath)
        .arg(dirPath)
        .arg(size);
  }

  QLabel* resultsLabel = new QLabel(html);
  resultsLabel->setTextFormat(Qt::RichText);
  resultsLabel->setWordWrap(true);
  resultsLabel->setOpenExternalLinks(true);
  layout->addWidget(resultsLabel);

  return widget;
}

QWidget* createComparisonPerformanceBar(const QString& label, double value,
                                        double comparisonValue, double maxValue,
                                        const QString& unit,
                                        bool lowerIsBetter) {

  // Use a generic name that will be updated by specific renderers (CPU, Drive,
  // Memory, etc.)
  QString userItemName = "User Result";

  // Scale maxValue so highest value fills 80% of the bar
  double scaledMaxValue = maxValue * 1.25;  // 1/0.8 = 1.25

  QWidget* container = new QWidget();
  QVBoxLayout* mainLayout = new QVBoxLayout(container);
  mainLayout->setContentsMargins(0, 8, 0, 1);
  mainLayout->setSpacing(4);  // Spacing between user result and comparison

  // Create title label at the top with color-coded lower/higher is better text
  QString titleText = label;
  QString betterText =
    lowerIsBetter ? "(lower is better)" : "(higher is better)";
  QString betterColor =
    lowerIsBetter
      ? "#FF6666"
      : "#44FF44";  // Red for lower-is-better, green for higher-is-better

  titleText +=
    QString(" <span style='color: %1; font-style: italic;'>%2</span>")
      .arg(betterColor)
      .arg(betterText);

  QLabel* titleLabel = new QLabel(titleText);
  titleLabel->setTextFormat(Qt::RichText);
  titleLabel->setStyleSheet(
    "color: #ffffff; background: transparent; font-weight: bold;");
  titleLabel->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(titleLabel);

  // Create the user's result bar
  QHBoxLayout* userLayout = new QHBoxLayout();
  userLayout->setContentsMargins(0, 0, 0, 0);
  userLayout->setSpacing(8);

  // Add CPU name label on the left with actual CPU name
  QLabel* userNameLabel = new QLabel(userItemName);
  userNameLabel->setObjectName(
    "userNameLabel");  // Make sure the object name is set for finding later
  userNameLabel->setStyleSheet(
    "color: #ffffff; background: transparent; font-weight: bold;");
  userNameLabel->setFixedWidth(
    150);  // Make wider to accommodate longer CPU names
  userNameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  userLayout->addWidget(userNameLabel);

  // Create bar container for user's result - wider and expandable
  QWidget* userBarContainer = new QWidget();
  userBarContainer->setFixedHeight(20);
  userBarContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  userBarContainer->setStyleSheet(
    "background-color: #333333; border-radius: 2px;");

  QHBoxLayout* userBarLayout = new QHBoxLayout(userBarContainer);
  userBarLayout->setContentsMargins(0, 0, 0, 0);
  userBarLayout->setSpacing(0);

  // Calculate percentage (0-100%) based on value / scaledMaxValue
  int userPercentage =
    value <= 0
      ? 0
      : static_cast<int>(std::min(100.0, (value / scaledMaxValue) * 100.0));

  // Use blue for user's result
  QString userBarColor = "#0078d4";  // Blue for user's result

  QWidget* userBar = new QWidget();
  userBar->setFixedHeight(20);
  userBar->setStyleSheet(
    QString("background-color: %1; border-radius: 2px;").arg(userBarColor));

  // Add percentage difference label inside the bar
  QLabel* percentageLabel = nullptr;
  if (comparisonValue > 0) {
    double percentChange = 0;
    if (lowerIsBetter) {
      // For lower-is-better metrics, negative percent means user is better
      percentChange = ((value / comparisonValue) - 1.0) * 100.0;
    } else {
      // For higher-is-better metrics, positive percent means user is better
      percentChange = ((value / comparisonValue) - 1.0) * 100.0;
    }

    QString percentText;
    QString percentColor;

    // Determine if user result is better or worse
    bool isBetter = (lowerIsBetter && percentChange < 0) ||
                    (!lowerIsBetter && percentChange > 0);
    bool isApproxEqual = qAbs(percentChange) < 1.0;

    if (isApproxEqual) {
      percentText = "≈";
      percentColor = "#FFAA00";  // Yellow for equal
    } else {
      percentText =
        QString("%1%2%")
          .arg(isBetter ? "+" : "")  // Add + prefix for better results
          .arg(percentChange, 0, 'f', 1);
      percentColor =
        isBetter ? "#44FF44" : "#FF4444";  // Green for better, red for worse
    }

    // Create the label and add it to the bar
    percentageLabel = new QLabel(percentText);
    percentageLabel->setStyleSheet(
      QString("color: %1; background: transparent; font-weight: bold;")
        .arg(percentColor));
    percentageLabel->setAlignment(Qt::AlignCenter);
  }

  QWidget* userSpacer = new QWidget();
  userSpacer->setStyleSheet("background-color: transparent;");

  userBarLayout->addWidget(userBar, userPercentage);
  userBarLayout->addWidget(userSpacer, 100 - userPercentage);

  // Add the percentage label on top of the bar layout if it exists
  if (percentageLabel) {
    // Remove margins first to position the label properly
    userBarLayout->setContentsMargins(0, 0, 0, 0);

    // Create an overlay layout
    QHBoxLayout* overlayLayout = new QHBoxLayout(userBarContainer);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->addWidget(percentageLabel);
  }

  userLayout->addWidget(userBarContainer);

  // Show the actual value with the same color - fixed width for alignment
  QString resultText = QString("%1 %2").arg(value, 0, 'f', 1).arg(unit);

  QLabel* userValueLabel = new QLabel(resultText);
  userValueLabel->setTextFormat(Qt::RichText);
  userValueLabel->setFixedWidth(150);  // Wider to accommodate percentage info
  userValueLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  userValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  userValueLabel->setStyleSheet(
    QString("color: %1; background: transparent;").arg(userBarColor));
  userLayout->addWidget(userValueLabel);

  mainLayout->addLayout(userLayout);

  // Create the comparison bar
  QHBoxLayout* compLayout = new QHBoxLayout();
  compLayout->setContentsMargins(0, 0, 0, 0);
  compLayout->setSpacing(8);

  // Add comparison CPU name or placeholder
  QLabel* compNameLabel = new QLabel("Select CPU to compare");
  compNameLabel->setObjectName("comp_name_label");
  compNameLabel->setStyleSheet(
    "color: #888888; font-style: italic; background: transparent;");
  compNameLabel->setFixedWidth(150);  // Same width as user name label
  compNameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  compLayout->addWidget(compNameLabel);

  // Create bar container for comparison - same policy as user bar
  QWidget* compBarContainer = new QWidget();
  compBarContainer->setObjectName("comparison_bar");  // Generic name

  // Set specific object name based on test type for easier identification
  if (label == "Single-core")
    compBarContainer->setObjectName("comparison_bar_single_core");
  else if (label == "Multi-core")
    compBarContainer->setObjectName("comparison_bar_multi_core");
  else if (label == "Scalar ops")
    compBarContainer->setObjectName("comparison_bar_scalar");
  else if (label == "AVX ops")
    compBarContainer->setObjectName("comparison_bar_avx");
  else if (label == "Prime calculation")
    compBarContainer->setObjectName("comparison_bar_prime");
  else if (label == "Small (L3)")
    compBarContainer->setObjectName("comparison_bar_small");
  else if (label == "Medium")
    compBarContainer->setObjectName("comparison_bar_medium");
  else if (label == "Large (RAM)")
    compBarContainer->setObjectName("comparison_bar_large");
  // Add cache latency object names
  else if (label.contains("KB") || label.contains("MB")) {
    QString objName =
      "comparison_bar_cache_" + label.simplified().toLower().replace(" ", "_");
    compBarContainer->setObjectName(objName);
  }

  compBarContainer->setFixedHeight(16);  // Thinner height
  compBarContainer->setSizePolicy(
    QSizePolicy::Expanding,
    QSizePolicy::Fixed);  // Expandable to match user bar
  compBarContainer->setStyleSheet(
    "background-color: #333333; border-radius: 2px;");

  QHBoxLayout* compBarLayout = new QHBoxLayout(compBarContainer);
  compBarLayout->setContentsMargins(0, 0, 0, 0);
  compBarLayout->setSpacing(0);

  if (comparisonValue > 0) {
    // Calculate percentage for comparison bar
    int compPercentage = static_cast<int>(
      std::min(100.0, (comparisonValue / scaledMaxValue) * 100.0));

    QWidget* compBar = new QWidget();
    compBar->setFixedHeight(16);
    compBar->setStyleSheet(
      "background-color: #FF4444; border-radius: 2px;");  // Red for comparison

    QWidget* compSpacer = new QWidget();
    compSpacer->setStyleSheet("background-color: transparent;");

    compBarLayout->addWidget(compBar, compPercentage);
    compBarLayout->addWidget(compSpacer, 100 - compPercentage);
  } else {
    // Empty bar for placeholder
    QWidget* emptyBar = new QWidget();
    emptyBar->setStyleSheet("background-color: transparent;");
    compBarLayout->addWidget(emptyBar);
  }

  compLayout->addWidget(compBarContainer);

  // Value label with same fixed width for alignment
  QLabel* compValueLabel =
    new QLabel(comparisonValue > 0
                 ? QString("%1 %2").arg(comparisonValue, 0, 'f', 1).arg(unit)
                 : "-");
  compValueLabel->setObjectName("value_label");  // For finding it later
  compValueLabel->setFixedWidth(150);  // Same width as user value label
  compValueLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
  compValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  compValueLabel->setStyleSheet(
    comparisonValue > 0 ? "color: #FF4444; background: transparent;"
                        :  // Red for comparison
      "color: #888888; font-style: italic; background: transparent;");
  compLayout->addWidget(compValueLabel);

  mainLayout->addLayout(compLayout);

  return container;
}

}  // end namespace DiagnosticViewComponents
