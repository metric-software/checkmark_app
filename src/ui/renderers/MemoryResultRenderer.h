#pragma once

#include <map>

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QTableWidget>
#include <QWidget>

#include "DiagnosticViewComponents.h"  // Add this include for DiagnosticViewComponents
#include "diagnostic/DiagnosticDataStore.h"

// Include for network types - need full definition for method signatures
#include "network/api/DownloadApiClient.h"

namespace DiagnosticRenderers {

// Move struct definition before the class that uses it
struct MemoryComparisonData {
  QString type;
  double totalMemoryGB;
  int frequencyMHz;
  QString channelStatus;
  bool xmpEnabled;

  // Performance metrics
  double bandwidthMBs;
  double latencyNs;
  double readTimeGBs;
  double writeTimeGBs;

  // Modules info - simplified for comparison
  int moduleCount;
  double moduleCapacityGB;
};

class MemoryResultRenderer {
 public:
  // Create memory results widget
  static QWidget* createMemoryResultWidget(const QString& result, const MenuData* networkMenuData = nullptr, DownloadApiClient* downloadClient = nullptr);
  
  // Network-based methods
  static MemoryComparisonData convertNetworkDataToMemory(const ComponentData& networkData);
  static std::map<QString, MemoryComparisonData> createDropdownDataFromMenu(const MenuData& menuData);

  // Process memory data and create display widget
  static QWidget* processMemoryData(
    const DiagnosticDataStore::MemoryData& memData, const MenuData* networkMenuData = nullptr, DownloadApiClient* downloadClient = nullptr);

  // Add this new method declaration - change it to static
  static QWidget* createBandwidthBar(const QString& label, double value,
                                     const QString& unit, double typicalValue);

  // Add these methods with proper template arguments
  static std::map<QString, MemoryComparisonData> loadMemoryComparisonData();
  static QComboBox* createMemoryComparisonDropdown(
    const std::map<QString, MemoryComparisonData>& comparisonData,
    QWidget* containerWidget, const QPair<double, double>& bandwidthVals,
    const QPair<double, double>& latencyVals,
    const QPair<double, double>& readTimeVals,
    const QPair<double, double>& writeTimeVals, DownloadApiClient* downloadClient = nullptr);

  // Fix the declaration - properly define the return type
  static std::map<QString, DiagnosticViewComponents::AggregatedComponentData<MemoryComparisonData>> generateAggregatedMemoryData(
    const std::map<QString, MemoryComparisonData>& individualData);

 private:
  // Helper methods
  static QWidget* createMemoryMetricBox(const QString& title,
                                        const QString& value,
                                        bool isHighlight = false);
  static QWidget* createPerformanceBox(const QString& title, double value,
                                       const QString& unit);
  static QWidget* createPerformanceGauge(const QString& label, double value,
                                         double maxValue, const QString& unit);
  static QWidget* createTimeBar(const QString& label, double value,
                                const QString& unit);
  static QString getMemoryPerformanceRecommendation(double bandwidth,
                                                    double latency,
                                                    bool xmpEnabled);
  static QWidget* createRawDataWidget(const QString& result);
  static QWidget* createMemoryModulesTable(
    const std::vector<DiagnosticDataStore::MemoryData::MemoryModule>& modules);
  static QWidget* createStabilityTestWidget(
    const DiagnosticDataStore::MemoryData::StabilityTestResults& stabilityTest);
};

}  // namespace DiagnosticRenderers
