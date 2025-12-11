#pragma once

#include <map>  // Add this include for std::map

#include <QComboBox>  // Add this include for dropdown
#include <QGridLayout>
#include <QLabel>
#include <QMap>
#include <QString>
#include <QWidget>

#include "DiagnosticViewComponents.h"  // Add this include for DiagnosticViewComponents
#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"

// Include for network types - need full definition for method signatures
#include "network/api/DownloadApiClient.h"

namespace DiagnosticRenderers {

struct DriveComparisonData {
  QString model;
  QString driveType;  // SSD, HDD, etc.
  double readSpeedMBs;
  double writeSpeedMBs;
  double iops4k;
  double accessTimeMs;
};

class DriveResultRenderer {
 public:
  // Create drive performance metrics display
  static QWidget* createDriveResultWidget(const QString& result, const MenuData* networkMenuData = nullptr, DownloadApiClient* downloadClient = nullptr);
  
  // Network-based methods
  static DriveComparisonData convertNetworkDataToDrive(const ComponentData& networkData);
  static std::map<QString, DriveComparisonData> createDropdownDataFromMenu(const MenuData& menuData);

 private:
  // Helper methods
  static QWidget* createDriveMetricBox(const QString& title,
                                       const QString& value,
                                       const QString& color = "#0078d4");
  static QWidget* createPerformanceBar(const QString& label, double value,
                                       double maxValue, const QString& unit,
                                       bool higherIsBetter = true);
  static QWidget* createRawDataWidget(const QString& result);
  static QString getColorForSpeed(double value, double typicalValue,
                                  bool higherIsBetter);

  // Add new methods for comparison
  static std::map<QString, DriveComparisonData> loadDriveComparisonData();
  static QComboBox* createDriveComparisonDropdown(
    const std::map<QString, DriveComparisonData>& comparisonData,
    QWidget* containerWidget, const QPair<double, double>& readSpeedVals,
    const QPair<double, double>& writeSpeedVals,
    const QPair<double, double>& iopsVals,
    const QPair<double, double>& accessTimeVals,
    DownloadApiClient* downloadClient);

  // Updated method signature
  static QWidget* processDriveData(
    const DiagnosticDataStore::DriveData::DriveMetrics& drive,
    const SystemMetrics::ConstantSystemInfo& constantInfo, double maxReadSpeed,
    double maxWriteSpeed, double maxIops, double maxAccessTime,
    const std::map<QString, DriveComparisonData>& comparisonData,
    DownloadApiClient* downloadClient);

  // Add this method declaration to the DriveResultRenderer class
  static std::map<QString, DiagnosticViewComponents::AggregatedComponentData<DriveComparisonData>> generateAggregatedDriveData(
    const std::map<QString, DriveComparisonData>& individualData);
};

}  // namespace DiagnosticRenderers
