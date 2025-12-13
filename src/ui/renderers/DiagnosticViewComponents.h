#pragma once

#include <functional>  // Missing include for std::function
#include <map>         // Missing include for std::map

#include <QComboBox>  // Missing include for QComboBox
#include <QGridLayout>
#include <QLabel>
#include <QString>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "diagnostic/DiagnosticDataStore.h"
#include "diagnostic/storage_analysis.h"  // Updated path to include the diagnostic directory
#include "logging/Logger.h"

namespace DiagnosticViewComponents {

// Helper function to create styled metric boxes
QWidget* createMetricBox(const QString& title, const QString& value,
                         const QString& color = "#0078d4");

// Helper for performance visualization
QWidget* createPerformanceGauge(const QString& label, double value,
                                double maxValue, const QString& unit);

// Create table for test results
QTableWidget* createResultsTable(const QStringList& headers, int rows = 0);

// Memory result helpers
QString formatMemoryResultString(
  const DiagnosticDataStore::MemoryData& memData);
QString getMemoryPerformanceRecommendation(double bandwidth, double latency,
                                           bool xmpEnabled);

// Storage helpers
QString formatStorageSize(unsigned long long bytes);
QWidget* createStorageAnalysisWidget(
  const StorageAnalysis::AnalysisResults& results);

// Create collapsible raw data section
QWidget* createRawDataWidget(const QString& result);

// Create performance bar with comparison capability
QWidget* createComparisonPerformanceBar(const QString& label, double value,
                                        double comparisonValue, double maxValue,
                                        const QString& unit,
                                        bool lowerIsBetter = true);

// New enum for aggregation type
enum class AggregationType {
  Individual,  // Original individual result
  Best,        // Best performance across all runs
  Average      // Average performance across all runs
};

// Template to create aggregated data for different component types
template <typename T> struct AggregatedComponentData {
  QString componentName;
  QString originalFullName;  // Store the original full name for API requests
  std::map<QString, T>
    individualResults;  // Key: unique identifier for the result
  T bestResult;         // Best performance metrics
  T averageResult;      // Average performance metrics

  // Helper function to get result based on aggregation type
  T& getResult(AggregationType type, const QString& individualId = "") {
    if (type == AggregationType::Best)
      return bestResult;
    else if (type == AggregationType::Average)
      return averageResult;
    else if (!individualId.isEmpty() &&
             individualResults.count(individualId) > 0)
      return individualResults[individualId];

    // Default to best if individual not found
    return bestResult;
  }
};

// Function to create comparison dropdown with aggregated data
template <typename T>
QComboBox* createAggregatedComparisonDropdown(
  const std::map<QString, AggregatedComponentData<T>>& aggregatedData,
  std::function<void(const QString&, const QString&, AggregationType, const T&)>
    onSelectionChanged) {

  QComboBox* dropdown = new QComboBox();
  dropdown->addItem("Select component for comparison...");

  const QString generalLabel = QStringLiteral("Avg for all users");
  if (aggregatedData.find(generalLabel) != aggregatedData.end()) {
    dropdown->addItem(generalLabel, QVariant("general:" + generalLabel));
  }

  // First add all "Best" entries
  for (const auto& [name, data] : aggregatedData) {
    if (name == generalLabel) continue;
    dropdown->addItem(name + " (Best)", QVariant("best:" + name));
  }

  // Then add all "Average" entries
  for (const auto& [name, data] : aggregatedData) {
    if (name == generalLabel) continue;
    dropdown->addItem(name + " (Avg)", QVariant("avg:" + name));
  }

  // Style the dropdown
  dropdown->setStyleSheet(R"(
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
    )");

  // Connect the dropdown selection change event
  QObject::connect(
    dropdown, QOverload<int>::of(&QComboBox::currentIndexChanged),
    [dropdown, aggregatedData, onSelectionChanged](int index) {
      if (index <= 0) {
        // Selected the placeholder, call with empty values
        onSelectionChanged("", "", AggregationType::Average, T());
        return;
      }

      QVariant userData = dropdown->itemData(index);
      if (!userData.isValid()) return;

      QString data = userData.toString();
      if (data.isEmpty()) return;

      // Parse the data format "type:componentName"
      QStringList parts = data.split(":");
      if (parts.size() != 2) return;

      QString typeStr = parts[0];
      QString componentName = parts[1];

      // Determine aggregation type
  AggregationType type = AggregationType::Average;  // Default
  if (typeStr == "avg") type = AggregationType::Average;
  else if (typeStr == "best") type = AggregationType::Best;
  else if (typeStr == "general") type = AggregationType::Average;

      // Find the component data
      if (aggregatedData.find(componentName) != aggregatedData.end()) {
        const auto& compData = aggregatedData.at(componentName);
        // Log the selection and the original name we will pass along
        LOG_INFO << "AggregatedDropdown: Selected component='" << componentName.toStdString()
                 << "', type='" << (type == AggregationType::Best ? "Best" : "Avg")
                 << "', originalFullName='" << compData.originalFullName.toStdString() << "'";
        onSelectionChanged(componentName, compData.originalFullName, type,
                           type == AggregationType::Best
                             ? compData.bestResult
                             : compData.averageResult);
      } else {
        LOG_WARN << "AggregatedDropdown: component not found in aggregatedData: " << componentName.toStdString();
      }
    });

  return dropdown;
}

}  // namespace DiagnosticViewComponents
