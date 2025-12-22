#pragma once
#include <algorithm>
#include <vector>

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "CustomWidgetWithTitle.h"
#include "SettingsDropdown.h"  // Add this include
#include "diagnostic/DiagnosticWorker.h"
#include "diagnostic/storage_analysis.h"
#include "renderers/AnalysisSummaryRenderer.h"
#include "renderers/CPUResultRenderer.h"  // Add this include for CPUComparisonData
#include "network/MenuManager.h"  // Add centralized menu management

class DiagnosticView : public QWidget {
  Q_OBJECT
 public:
  explicit DiagnosticView(QWidget* parent = nullptr);

  DiagnosticWorker* getWorker() { return worker; }
  ~DiagnosticView();

 public slots:
  void setRunDriveTests(bool run);
  void setRunGpuTests(bool run);
  void setRunCpuBoostTests(bool run);
  void setRunNetworkTests(bool run);
  void setRunCpuThrottlingTests(bool run);
  void updateExperimentalFeaturesVisibility();  // Add this line

  // Add new slot methods for combo boxes
  void setDriveTestMode(int mode);
  void setNetworkTestMode(int mode);
  void setCpuThrottlingTestMode(int mode);
  void setUseRecommendedSettings(bool useRecommended);
  
  void updateRunButtonState();  // Helper to update button state

 private slots:
  void onRunDiagnostics();
  void updateCPUResults(const QString& result);
  void updateCacheResults(const QString& result);
  void updateMemoryResults(const QString& result);
  void updateGPUResults(const QString& result);
  void updateDriveResults(const QString& result);
  void updateSystemInfo(const QString& result);
  void diagnosticsFinished();
  void updateStorageResults(const StorageAnalysis::AnalysisResults& results);
  void updateBackgroundProcessResults(const QString& result);
  void updateTestStatus(const QString& testName);
  void updateProgress(int progress);
  void handleAdminElevation();  // Make sure this declaration is properly saved
  void updateNetworkResults(const QString& result);

 private:
  void setupLayout();
  QString formatResultValue(const QString& label, const QString& value);
  QString getMemoryPerformanceRecommendation(double bandwidth, double latency,
                                             bool xmpEnabled);
  void updateEstimatedTime();  // Add this method declaration
  void clearAllResults();      // Add new method declaration

  // Add missing method declarations
  void disconnectAllSignals();
  void cleanUpWorkerAndThread();
  void connectWorkerSignals();

  DiagnosticWorker* worker;  // Make sure this declaration is properly saved

  // Add worker thread as class member
  QThread* workerThread = nullptr;

  // System info removed - using ConstantSystemInfo directly when needed

  // UI Elements
  QPushButton* runButton;
  QProgressBar* diagnosticProgress;

  // Track last progress value to prevent decreasing
  int lastProgressValue = 0;

  // Info & Performance Labels
  QLabel* cpuInfoLabel;
  QLabel* cpuPerfLabel;
  QLabel* cachePerfLabel;
  QLabel* memoryInfoLabel;
  QLabel* memoryPerfLabel;
  QLabel* gpuInfoLabel;
  QLabel* gpuPerfLabel;
  QLabel* systemInfoLabel;
  QVector<QLabel*> driveInfoLabels;
  QVector<QLabel*> drivePerfLabels;

  // Updated checkbox declarations (lines 95-105)
  QCheckBox* runGpuTestsCheckbox;
  QCheckBox* runCpuBoostTestsCheckbox;
  QCheckBox* storageAnalysisCheckbox;
  QCheckBox* useRecommendedCheckbox;

  // Remove from private members:
  bool runStorageAnalysis = false;

  QLabel* storageAnalysisLabel;
  QString formatStorageSize(unsigned long long bytes);

  // Add these member variables
  CustomWidgetWithTitle* cpuWidget;
  CustomWidgetWithTitle* cacheWidget;
  CustomWidgetWithTitle* memoryWidget;
  CustomWidgetWithTitle* gpuWidget;
  CustomWidgetWithTitle* sysWidget;
  CustomWidgetWithTitle* driveWidget;
  CustomWidgetWithTitle* storageAnalysisGroup;
  CustomWidgetWithTitle* backgroundProcessWidget;
  QLabel* backgroundProcessLabel;

  // Add to the private members section:
  CustomWidgetWithTitle* summaryWidget;

  // Add to private methods:
  QWidget* createSystemMetricBox(const QString& title, QLabel* contentLabel);
  QWidget* createMetricBox(const QString& title);
  QWidget* createPerformanceBox(const QString& title, double value,
                                const QString& unit);

  bool runCpuBoostTests = true;  // Default to true

  // Add layout member variables
  QVBoxLayout* mainLayout;
  QWidget* headerWidget;

  // Add this helper function for determining colors
  QString getColorForPerformance(double value, const QString& unit);

  // Add to private member variables:
  QCheckBox* runDriveTestsCheckbox;

  // Change boolean flags to match new names
  bool runDriveTests = true;          // Default to true
  bool runGpuTests = true;            // Default to true
  bool runCpuThrottlingTests = true;  // Default to true
  bool runNetworkTests = true;        // Default to true

  // Add to private members:
  CustomWidgetWithTitle* networkWidget;

  QWidget* storageContainerWidget;
  QGridLayout* storageLayout;

  // Add these enum definitions near the top of the class
 public:
  // Enum for drive test modes
  enum DriveTestMode {
    DriveTest_None,        // No drive tests
    DriveTest_SystemOnly,  // Test only C: drive
    DriveTest_AllDrives    // Test all drives
  };

  // Enum for network test modes
  enum NetworkTestMode {
    NetworkTest_None,     // No network tests
    NetworkTest_Basic,    // Basic network tests (short)
    NetworkTest_Extended  // Extended network tests (long)
  };

  // Enum for CPU throttling test modes
  enum CpuThrottlingTestMode {
    CpuThrottle_None,     // No CPU throttling tests
    CpuThrottle_Basic,    // Basic throttling tests (short)
    CpuThrottle_Extended  // Extended throttling tests (long)
  };

  // Add to public section:
 public:
  void cancelOperations();

 private:
  // Replace QComboBox with SettingsDropdown
  SettingsDropdown* driveTestModeCombo;
  SettingsDropdown* networkTestModeCombo;
  SettingsDropdown* cpuThrottlingTestModeCombo;

  // Keep these as checkboxes - but REMOVE these duplicate declarations

  // Variables to store current modes
  DriveTestMode driveTestMode = DriveTest_SystemOnly;  // Default: C: drive only
  NetworkTestMode networkTestMode =
    NetworkTest_Basic;  // Default: Basic network test
  CpuThrottlingTestMode cpuThrottlingTestMode =
    CpuThrottle_None;  // Default: No throttling test

  // Add this member variable to the DiagnosticView class
  std::map<QString, DiagnosticRenderers::CPUComparisonData> cpuComparisonData;

  QLabel* statusLabel;

  // Add these declarations
  QList<QProcess*> activeProcesses;
  bool isRunning = false;
  BackgroundProcessWorker* backgroundProcessWorker = nullptr;  // Add this line

  QLabel* estimatedTimeLabel;  // Add this member variable

  // Add guard for diagnosticsFinished
  bool m_isCurrentlyExecuting = false;

  // Network client for comparison data
  DownloadApiClient* downloadClient = nullptr;
  MenuData cachedMenuData;
  bool menuDataLoaded = false;
};
