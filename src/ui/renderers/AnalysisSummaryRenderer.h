#pragma once

#include <QLabel>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include "DiagnosticViewComponents.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"

namespace DiagnosticRenderers {

class AnalysisSummaryRenderer {
 public:
  static QWidget* createAnalysisSummaryWidget();

 private:
  // Helper methods for analysis categories
  static void analyzeCPU(const DiagnosticDataStore::CPUData& cpuData,
                         const SystemMetrics::ConstantSystemInfo& constantInfo,
                         QStringList& criticalIssues, QStringList& issues,
                         QStringList& recommendations,
                         QStringList& performanceSummary);

  static void analyzeMemory(const DiagnosticDataStore::MemoryData& memData,
                            QStringList& criticalIssues, QStringList& issues,
                            QStringList& recommendations,
                            QStringList& performanceSummary);

  static void analyzePageFile(const DiagnosticDataStore::MemoryData& memData,
                              const DiagnosticDataStore::DriveData& driveData,
                              QStringList& criticalIssues, QStringList& issues,
                              QStringList& performanceSummary);

  static void analyzeDriveSpace(
    const SystemMetrics::ConstantSystemInfo& constantInfo,
    QStringList& criticalIssues, QStringList& issues,
    QStringList& performanceSummary);

  static void analyzeDrivers(
    const SystemMetrics::ConstantSystemInfo& constantInfo, QStringList& issues,
    QStringList& recommendations, QStringList& performanceSummary);

  static void analyzeGPU(const DiagnosticDataStore::GPUData& gpuData,
                         QStringList& performanceSummary);

  static void analyzeBackgroundProcesses(
    const DiagnosticDataStore::BackgroundProcessData& bgData,
    QStringList& issues, QStringList& recommendations,
    QStringList& performanceSummary);

  static void analyzeNetwork(
    const DiagnosticDataStore::NetworkData& networkData, QStringList& issues,
    QStringList& recommendations, QStringList& performanceSummary);

  // Helper method to parse driver dates in various formats
  static QDate parseDriverDate(const std::string& dateStr);
};

}  // namespace DiagnosticRenderers
