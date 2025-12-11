#pragma once

// Include WinSock2.h before Windows.h to prevent conflicts

#include <fstream>  // Add this include for std::ofstream
#include <future>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QProcess>
#include <QDateTime>
#include <QThread>
#include <Windows.h>
#include <shellapi.h>
#include <iostream>

#include "DevToolsChecker.h"
#include "background_process_monitor.h"
#include "diagnostic/CoreBoostMetrics.h"     // Include the new header
#include "diagnostic/DiagnosticDataStore.h"  // Add this include
#include "hardware/PdhInterface.h"           // Add PDH interface include
#include "memory_test.h"
#include "storage_analysis.h"

#include "logging/Logger.h"

// Add forward declaration here
class GPUTest;

// Remove the CoreBoostMetrics struct definition - now using the one from
// CoreBoostMetrics.h

class DiagnosticWorker : public QObject {
  Q_OBJECT
 public:
  explicit DiagnosticWorker(QObject* parent = nullptr);
  ~DiagnosticWorker();

  // Make all getters thread-safe
  const std::vector<CoreBoostMetrics>& getCpuBoostMetrics() const {
    QMutexLocker locker(&mutex);  // Add a mutex lock when accessing data
    return cpuBoostMetrics;
  }
  int getBestBoostCore() const { return bestBoostCore; }
  int getMaxBoostDelta() const { return maxBoostDelta; }
  double getIdleTotalPower() const { return idleTotalPower; }
  double getAllCoreTotalPower() const { return allCoreTotalPower; }
  bool isRunningAsAdmin() const;
  bool restartAsAdmin();

  // Add a method to safely cancel ongoing operations before destruction
  void prepareForDestruction() {
    // Cancel any pending operations
    if (memoryTestFuture.valid()) {
      try {
        // Attempt to get result (which will wait for completion)
        memoryTestFuture.get();
      } catch (const std::exception& e) {
        // Just log any exceptions, don't let them propagate
        LOG_ERROR << "Exception during memory test cleanup: " << e.what();
      }
    }

    // Log that we're cleaning up
    LOG_INFO << "DiagnosticWorker preparing for destruction";
  }

  // Add this new public method
  void cancelPendingOperations();

  // Add these new methods
  void setSystemDriveOnlyMode(bool systemOnly) {
    systemDriveOnlyMode = systemOnly;
  }
  void setExtendedNetworkTests(bool extended) {
    extendedNetworkTests = extended;
  }
  void setExtendedCpuThrottlingTests(bool extended) {
    extendedCpuThrottlingTests = extended;
  }

 public slots:
  // This method now just starts the thread
  void runDiagnostics();
  // Move this method to public slots so it can be called via signal/slot
  void runDiagnosticsInternal();

  void runBackgroundProcessTest();

  void setSkipDriveTests(bool skip) { skipDriveTests = skip; }
  void setSkipGpuTests(bool skip) { skipGpuTests = skip; }
  void setDeveloperMode(bool enabled) { developerMode = enabled; }
  void setRunStorageAnalysis(bool run) { runStorageAnalysis = run; }
  void setSaveResults(bool save) { saveResults = save; }
  void setComparisonMode(bool enabled) { compareMode = enabled; }
  QJsonObject resultsToJson() const;
  void setSkipCpuThrottlingTests(bool skip) { skipCpuThrottlingTests = skip; }
  void setRunCpuBoostTests(bool run) { runCpuBoostTests = run; }
  void setRunNetworkTests(bool run);
  
  // New test mode setters
  void setDriveTestMode(int mode) { driveTestMode = mode; }
  void setNetworkTestMode(int mode) { networkTestMode = mode; }
  void setCpuThrottlingTestMode(int mode) { cpuThrottlingTestMode = mode; }
  void setRunMemoryTests(bool run) { runMemoryTests = run; }
  void setRunBackgroundTests(bool run) { runBackgroundTests = run; }
  void setUseRecommendedSettings(bool use) { useRecommendedSettings = use; }

 signals:
  void cpuTestCompleted(const QString& result);
  void cacheTestCompleted(const QString& result);
  void memoryTestCompleted(const QString& result);
  void gpuTestCompleted(const QString& result);
  void driveTestCompleted(const QString& result);
  void diagnosticsFinished();
  void devToolsResultsReady(const QString& result);
  void additionalToolsResultsReady(const QString& result);
  void storageAnalysisReady(const StorageAnalysis::AnalysisResults& results);
  void comparisonReady(const QJsonObject& currentResults,
                       const QJsonArray& previousResults);
  void backgroundProcessTestCompleted(const QString& result);
  void networkTestCompleted(const QString& result);
  void testStarted(const QString& testName);
  void progressUpdated(int progress);
  void requestAdminElevation();
  void testCompleted(const QString& testName);

 private:
  void addResult(const QString& tool, bool found, const QString& version = "");

  bool skipDriveTests = false;
  bool skipGpuTests = false;
  bool developerMode = false;
  bool runStorageAnalysis = false;
  bool skipCpuThrottlingTests = false;
  bool saveResults = true;  // Always persist diagnostics locally
  bool compareMode = false;
  QString devToolsResults;
  std::future<void> memoryTestFuture;
  DevToolsChecker* devToolsChecker;
  QVector<QMap<QString, QString>> memoryModules;
  QString memoryChannelStatus;
  GPUTest* activeGpuTest = nullptr;

  // Add storage for CPU boost test results
  std::vector<CoreBoostMetrics> cpuBoostMetrics;
  double idleTotalPower = 0.0;
  double singleCoreTotalPower = 0.0;
  double allCoreTotalPower = 0.0;
  int bestBoostCore = 0;
  int maxBoostDelta = 0;

  bool skipNetworkTests = false;
  bool extendedNetworkTests = false;  // New flag for longer network tests
  bool runCpuBoostTests = true;
  bool runNetworkTests = true;

  // Test mode settings
  int driveTestMode = 1;  // 0=None, 1=SystemOnly, 2=AllDrives
  int networkTestMode = 1;  // 0=None, 1=Basic, 2=Extended  
  int cpuThrottlingTestMode = 0;  // 0=None, 1=Basic, 2=Extended
  bool runMemoryTests = true;
  bool runBackgroundTests = true;
  bool useRecommendedSettings = true;

  mutable QMutex mutex;  // Add this mutex for thread safety

  void runCPUTest();
  void runMemoryTest();
  void runGPUTest();
  void runDriveTest();
  void runDeveloperToolsTest();
  void runNetworkTest();

  void performStorageAnalysis();
  void saveTestResults();
  QString generateResultsFilename() const;
  void log(const QString& message) const;
  QJsonArray loadPreviousResults() const;
  QString getRunTokenForOutput() const;
  QString getComparisonFolder() const { return "benchmark_results"; }
  void processBackgroundMonitorResults(
    const BackgroundProcessMonitor::MonitoringResult& result);

  // Other methods...
  QString formatMemoryResultString(
    const DiagnosticDataStore::MemoryData& memData) const;

  // Add these new member variables
  bool systemDriveOnlyMode = true;          // Default to system drive only
  bool extendedCpuThrottlingTests = false;  // Default to basic throttling tests

  // Kernel memory metrics removed - using ConstantSystemInfo instead
  // SystemInfoProvider::KernelMemoryInfo m_startKernelMemory;
  // SystemInfoProvider::KernelMemoryInfo m_endKernelMemory;

  // PDH metrics collection for diagnostic runs
  std::atomic<bool> m_pdhMetricsRunning{false};
  std::thread m_pdhMetricsThread;
  QString m_currentTestName;
  std::ofstream m_pdhMetricsFile;
  std::mutex m_testNameMutex;
  std::unique_ptr<PdhInterface> m_pdhInterface;

  // PDH metrics collection methods
  void startPdhMetricsCollection();
  void stopPdhMetricsCollection();
  void pdhMetricsCollectionThread();

  // Automatic upload functionality
  void performAutomaticUpload();

  // Track current diagnostic run id so JSON/CSV/optimization files stay grouped
  QDateTime m_currentRunTimestamp;
  QString m_currentRunToken;
};
