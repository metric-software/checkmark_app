#pragma once

#include <map>
#include <vector>

#include <QComboBox>
#include <QDir>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QString>
#include <QWidget>

#include "DiagnosticViewComponents.h"  // Add this import
#include "diagnostic/CoreBoostMetrics.h"

// Include for network types - need full definition for method signatures
#include "network/api/DownloadApiClient.h"


namespace DiagnosticRenderers {

// Make the actual struct definition
struct CPUComparisonData {
  QString model;
  QString fullModel;
  int cores = 0;
  int threads = 0;
  int baseClock = 0;
  int boostClock = 0;
  QString architecture;
  int l1CacheKB = 0;
  int l2CacheKB = 0;
  int l3CacheKB = 0;

  // Performance metrics
  double singleCoreTime = 0.0;
  double fourThreadTime = 0.0;
  double simdScalar = 0.0;
  double simdAvx = 0.0;
  double primeTime = 0.0;
  double gameSimSmall = 0.0;
  double gameSimMedium = 0.0;
  double gameSimLarge = 0.0;

  // Cold start metrics
  double coldStartAvg = 0.0;
  double coldStartMin = 0.0;
  double coldStartMax = 0.0;
  double coldStartStdDev = 0.0;

  // Cache latencies
  QMap<int, double> cacheLatencies;

  // Boost metrics data
  std::vector<CoreBoostMetrics> boostMetrics;
};

class CPUResultRenderer {
 public:
  // Create CPU performance metrics display
  static QWidget* createCPUResultWidget(
    const QString& result, const std::vector<CoreBoostMetrics>& boostMetrics, 
    const MenuData* networkMenuData = nullptr, DownloadApiClient* downloadClient = nullptr);
  static QWidget* createCacheResultWidget(
    const QString& result,
    const std::map<QString, CPUComparisonData>& comparisonData,
    const MenuData* networkMenuData = nullptr, DownloadApiClient* downloadClient = nullptr);

  // Move loadCPUComparisonData to public section (legacy local file support)
  static std::map<QString, CPUComparisonData> loadCPUComparisonData();

  // New network-based methods
  static CPUComparisonData convertNetworkDataToCPU(const ComponentData& networkData);
  static std::map<QString, CPUComparisonData> createDropdownDataFromMenu(const MenuData& menuData);

  // Add this new method to generate aggregated data
  static std::map<QString, DiagnosticViewComponents::AggregatedComponentData<CPUComparisonData>> generateAggregatedCPUData(
    const std::map<QString, CPUComparisonData>& individualData);

 private:
  // Helper to create metric boxes with the same styling
  static QWidget* createMetricBox(const QString& title);
  static QWidget* createLatencyBox(const QString& title, double latency,
                                   const QString& color);
  static QWidget* createLatencyBar(const QString& label, double value,
                                   const QString& color);

  // New helpers for comparison functionality
  static QWidget* createComparisonPerformanceBar(
    const QString& label, double value, double comparisonValue, double maxValue,
    const QString& unit, bool lowerIsBetter = true);
  static QWidget* createComparisonPerformanceBar(
    const QString& label, double value, double comparisonValue, double maxValue,
    const QString& unit, const char* description, bool lowerIsBetter = true);
  static QWidget* createComparisonPerformanceBar(
    const QString& label, double value, double comparisonValue, double maxValue,
    const QString& unit, const QString& description, bool lowerIsBetter = true);
  // Update method signature to remove eightThreadVals and add network support
  static QComboBox* createCPUComparisonDropdown(
    const std::map<QString, CPUComparisonData>& comparisonData,
    QWidget* containerWidget, QWidget* cpuTestsBox, QWidget* gameSimBox,
    const QPair<double, double>& singleCoreVals,
    const QPair<double, double>& fourThreadVals,
    // Remove eightThreadVals parameter
    const QPair<double, double>& simdScalarVals,
    const QPair<double, double>& simdAvxVals,
    const QPair<double, double>& primeTimeVals,
    const QPair<double, double>& gameSimSmallVals,
    const QPair<double, double>& gameSimMediumVals,
    const QPair<double, double>& gameSimLargeVals,
    const QPair<double, double>& coldStartVals,
    DownloadApiClient* downloadClient = nullptr);
};

}  // namespace DiagnosticRenderers
