#pragma once

/**
 * @file RendererCommon.h
 * @brief Common utilities, styles, and helper functions for diagnostic result renderers.
 *
 * This file provides shared constants and utility functions used by all result renderers
 * (CPU, GPU, Memory, Drive, Network) to ensure consistent styling and reduce code duplication.
 */

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <map>

#include "logging/Logger.h"

namespace RendererCommon {

// =============================================================================
// Style Constants
// =============================================================================

namespace Styles {
  // Container backgrounds
  constexpr const char* ContainerBackground = "background-color: #252525; border-radius: 4px;";
  constexpr const char* TransparentBackground = "background: transparent;";
  constexpr const char* BarBackground = "background-color: #333333; border-radius: 2px;";

  // Text colors
  constexpr const char* TextWhite = "#ffffff";
  constexpr const char* TextGray = "#888888";
  constexpr const char* TextBlue = "#0078d4";

  // Performance colors
  constexpr const char* ColorExcellent = "#44FF44";  // Green
  constexpr const char* ColorGood = "#88FF88";       // Light green
  constexpr const char* ColorAverage = "#FFEE44";    // Yellow
  constexpr const char* ColorBelowAverage = "#FFAA00"; // Orange
  constexpr const char* ColorPoor = "#FF6666";       // Red

  // User vs Comparison bar colors
  constexpr const char* UserBarColor = "#0078d4";    // Blue for user results
  constexpr const char* ComparisonBarColor = "#FF4444"; // Red for comparison

  // Title styling
  inline QString titleStyle() {
    return "color: #ffffff; font-size: 14px; background: transparent; margin-bottom: 5px;";
  }

  // Label styling
  inline QString labelStyle() {
    return "color: #ffffff; background: transparent; font-weight: bold;";
  }

  // Value label styling
  inline QString valueStyle(const QString& color) {
    return QString("color: %1; background: transparent;").arg(color);
  }

  // Dropdown styling
  inline QString dropdownStyle() {
    return R"(
      QComboBox {
        background-color: #333333;
        color: #FFFFFF;
        border: 1px solid #444444;
        border-radius: 4px;
        padding: 2px 8px;
        min-width: 200px;
      }
      QComboBox::drop-down {
        subcontrol-origin: padding;
        subcontrol-position: right center;
        width: 20px;
        border-left: 1px solid #444444;
      }
      QComboBox QAbstractItemView {
        background-color: #333333;
        color: #FFFFFF;
        selection-background-color: #0078d4;
        selection-color: #FFFFFF;
      }
    )";
  }
}

// =============================================================================
// Performance Color Utilities
// =============================================================================

/**
 * @brief Get performance color based on value thresholds (higher is better).
 *
 * @param value The current value
 * @param excellent Threshold for excellent (green)
 * @param good Threshold for good (light green)
 * @param average Threshold for average (yellow)
 * @param belowAverage Threshold for below average (orange)
 * @return QString Color hex code
 */
inline QString getPerformanceColorHigherBetter(double value, double excellent,
                                                double good, double average,
                                                double belowAverage) {
  if (value >= excellent) return Styles::ColorExcellent;
  if (value >= good) return Styles::ColorGood;
  if (value >= average) return Styles::ColorAverage;
  if (value >= belowAverage) return Styles::ColorBelowAverage;
  return Styles::ColorPoor;
}

/**
 * @brief Get performance color based on value thresholds (lower is better).
 *
 * @param value The current value
 * @param excellent Threshold for excellent (green) - lower values
 * @param good Threshold for good (light green)
 * @param average Threshold for average (yellow)
 * @param belowAverage Threshold for below average (orange)
 * @return QString Color hex code
 */
inline QString getPerformanceColorLowerBetter(double value, double excellent,
                                               double good, double average,
                                               double belowAverage) {
  if (value <= excellent) return Styles::ColorExcellent;
  if (value <= good) return Styles::ColorGood;
  if (value <= average) return Styles::ColorAverage;
  if (value <= belowAverage) return Styles::ColorBelowAverage;
  return Styles::ColorPoor;
}

// =============================================================================
// Widget Factory Functions
// =============================================================================

/**
 * @brief Create a styled container widget with standard layout.
 *
 * @param margins Layout margins (default: 0, 0, 0, 0)
 * @param spacing Layout spacing (default: 0)
 * @return QWidget* The container widget with a QVBoxLayout
 */
inline QWidget* createContainer(const QMargins& margins = QMargins(0, 0, 0, 0),
                                 int spacing = 0) {
  QWidget* container = new QWidget();
  container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

  QVBoxLayout* layout = new QVBoxLayout(container);
  layout->setContentsMargins(margins);
  layout->setSpacing(spacing);

  return container;
}

/**
 * @brief Create a styled metrics container with background.
 *
 * @param margins Layout margins (default: 12, 4, 12, 4)
 * @param spacing Layout spacing (default: 10)
 * @return QWidget* The metrics container widget
 */
inline QWidget* createMetricsContainer(const QMargins& margins = QMargins(12, 4, 12, 4),
                                        int spacing = 10) {
  QWidget* widget = new QWidget();
  widget->setStyleSheet(Styles::ContainerBackground);

  QVBoxLayout* layout = new QVBoxLayout(widget);
  layout->setContentsMargins(margins);
  layout->setSpacing(spacing);

  return widget;
}

/**
 * @brief Create a section title label with standard styling.
 *
 * @param text The title text (can include HTML)
 * @return QLabel* The styled title label
 */
inline QLabel* createSectionTitle(const QString& text) {
  QLabel* title = new QLabel(text);
  title->setStyleSheet(Styles::titleStyle());
  title->setContentsMargins(0, 0, 0, 0);
  return title;
}

/**
 * @brief Create a title row with a label and optional dropdown.
 *
 * @param titleText The title text
 * @param dropdown Optional dropdown to add (nullptr for no dropdown)
 * @return QWidget* The title row widget
 */
inline QWidget* createTitleRow(const QString& titleText, QComboBox* dropdown = nullptr) {
  QWidget* titleWidget = new QWidget();
  QHBoxLayout* titleLayout = new QHBoxLayout(titleWidget);
  titleLayout->setContentsMargins(0, 10, 0, 0);

  QLabel* title = new QLabel(QString("<b>%1</b>").arg(titleText));
  title->setStyleSheet("color: #ffffff; font-size: 14px; background: transparent;");
  titleLayout->addWidget(title);

  titleLayout->addStretch(1);

  if (dropdown) {
    titleLayout->addWidget(dropdown);
  }

  return titleWidget;
}

/**
 * @brief Create a metric info item (value with label below).
 *
 * @param value The display value
 * @param label The label text
 * @param valueColor Color for the value (default: white)
 * @return QLabel* The metric label
 */
inline QLabel* createMetricInfoItem(const QString& value, const QString& label,
                                     const QString& valueColor = Styles::TextWhite) {
  QLabel* metricLabel = new QLabel(
    QString("<span style='font-weight: bold; color: %1;'>%2</span><br>"
            "<span style='color: #888888;'>%3</span>")
      .arg(valueColor)
      .arg(value)
      .arg(label));
  metricLabel->setAlignment(Qt::AlignCenter);
  return metricLabel;
}

/**
 * @brief Create a horizontal info widget with multiple metric items.
 *
 * @return QWidget* The info widget with horizontal layout
 */
inline QWidget* createInfoWidget() {
  QWidget* infoWidget = new QWidget();
  infoWidget->setStyleSheet("background-color: #252525; border-radius: 4px; padding: 8px;");

  QHBoxLayout* layout = new QHBoxLayout(infoWidget);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(20);

  return infoWidget;
}

// =============================================================================
// File Loading Utilities
// =============================================================================

/**
 * @brief Load JSON comparison data files from the comparison_data directory.
 *
 * @tparam T The data structure type to populate
 * @param filePattern The file pattern to match (e.g., "cpu_benchmark_*.json")
 * @param parseFunction Function to parse a QJsonObject into type T
 * @param getKeyFunction Function to get the map key from type T
 * @return std::map<QString, T> Map of parsed comparison data
 */
template <typename T>
std::map<QString, T> loadComparisonDataFromFiles(
    const QString& filePattern,
    std::function<T(const QJsonObject&)> parseFunction,
    std::function<QString(const T&)> getKeyFunction) {

  std::map<QString, T> comparisonData;

  QString appDir = QApplication::applicationDirPath();
  QDir dataDir(appDir + "/comparison_data");

  if (!dataDir.exists()) {
    LOG_ERROR << "Comparison data folder not found: " << dataDir.absolutePath().toStdString();
    return comparisonData;
  }

  QStringList filters;
  filters << filePattern;
  dataDir.setNameFilters(filters);

  QStringList files = dataDir.entryList(QDir::Files);
  for (const QString& fileName : files) {
    QFile file(dataDir.absoluteFilePath(fileName));

    if (file.open(QIODevice::ReadOnly)) {
      QByteArray jsonData = file.readAll();
      QJsonDocument doc = QJsonDocument::fromJson(jsonData);

      if (doc.isObject()) {
        QJsonObject rootObj = doc.object();
        T data = parseFunction(rootObj);
        QString key = getKeyFunction(data);
        if (!key.isEmpty()) {
          comparisonData[key] = data;
        }
      }

      file.close();
    }
  }

  LOG_INFO << "Loaded " << comparisonData.size() << " items from " << filePattern.toStdString();
  return comparisonData;
}

/**
 * @brief Log component data for debugging purposes.
 * Replaces std::cout debug blocks with proper logging.
 *
 * @param componentType Type of component (e.g., "CPU", "GPU")
 * @param testData The test data QJsonObject
 * @param metaData The metadata QJsonObject
 */
inline void logComponentData(const QString& componentType,
                              const QJsonObject& testData,
                              const QJsonObject& metaData) {
  LOG_DEBUG << componentType.toStdString() << "ResultRenderer: Converting network data";

  // Log testData summary
  QJsonDocument testDataDoc(testData);
  QString testDataString = testDataDoc.toJson(QJsonDocument::Compact);
  LOG_DEBUG << "testData: " << testDataString.left(500).toStdString()
            << (testDataString.length() > 500 ? "..." : "");

  // Log metaData summary
  QJsonDocument metaDataDoc(metaData);
  QString metaDataString = metaDataDoc.toJson(QJsonDocument::Compact);
  LOG_DEBUG << "metaData: " << metaDataString.left(500).toStdString()
            << (metaDataString.length() > 500 ? "..." : "");
}

// =============================================================================
// Scaling Utilities
// =============================================================================

/**
 * @brief Calculate scaled maximum value for bar charts.
 * Uses 80% fill factor (max value fills 80% of bar).
 *
 * @param maxValue The maximum value to scale
 * @param fallback Fallback value if maxValue is too small
 * @return double Scaled maximum value
 */
inline double calculateScaledMax(double maxValue, double fallback = 100.0) {
  return (maxValue > 0.1) ? (maxValue * 1.25) : fallback;  // 1/0.8 = 1.25
}

/**
 * @brief Calculate bar percentage for a given value.
 *
 * @param value The value to calculate percentage for
 * @param scaledMax The scaled maximum value
 * @return int Percentage (0-100)
 */
inline int calculateBarPercentage(double value, double scaledMax) {
  if (value <= 0 || scaledMax <= 0) return 0;
  return static_cast<int>(std::min(100.0, (value / scaledMax) * 100.0));
}

/**
 * @brief Calculate percentage difference between user and comparison values.
 *
 * @param userValue The user's value
 * @param comparisonValue The comparison value
 * @param lowerIsBetter Whether lower values are better
 * @param percentChange Output: the percentage change
 * @param isBetter Output: whether user result is better
 * @param isApproxEqual Output: whether values are approximately equal
 */
inline void calculatePercentageDiff(double userValue, double comparisonValue,
                                     bool lowerIsBetter,
                                     double& percentChange,
                                     bool& isBetter,
                                     bool& isApproxEqual) {
  if (comparisonValue <= 0) {
    percentChange = 0;
    isBetter = false;
    isApproxEqual = true;
    return;
  }

  percentChange = ((userValue / comparisonValue) - 1.0) * 100.0;
  isApproxEqual = std::abs(percentChange) < 1.0;
  isBetter = (lowerIsBetter && percentChange < 0) ||
             (!lowerIsBetter && percentChange > 0);
}

}  // namespace RendererCommon
