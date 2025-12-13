#include "diagnostic/DiagnosticWorker.h"
#include "../logging/Logger.h"
#include "../ApplicationSettings.h"
#include "../network/api/UploadApiClient.h"

#include <iostream>
#include <sstream>

#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QUuid>

#include "DiagnosticDataStore.h"
#include "background_process_monitor.h"
#include "background_process_worker.h"
#include "cpu_test.h"
#include "drive_test.h"
#include "gpu_test.h"
#include "hardware/ConstantSystemInfo.h"
#include "hardware/WinHardwareMonitor.h"
#include "hardware/PdhInterface.h"
#include "memory_test.h"
#include "network_test_interface.h"  // Only includes the interface, not the Windows headers
#include "optimization/OptimizationEntity.h"  // Include optimization settings export functionality
#include "profiles/UserSystemProfile.h"  // Add this include for UserSystemProfile
#include "storage_analysis.h"


extern std::vector<CoreBoostMetrics> g_cpuBoostMetrics;
extern double g_idleTotalPower;
extern double g_allCoreTotalPower;
extern int g_bestBoostCore;
extern int g_maxBoostDelta;

std::string wstringToString(const std::wstring& wstr) {
  if (wstr.empty()) return std::string();
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(),
                                        NULL, 0, NULL, NULL);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0],
                      size_needed, NULL, NULL);
  return strTo;
}

DiagnosticWorker::DiagnosticWorker(QObject* parent)
    : QObject(parent),
      devToolsChecker(new DevToolsChecker(this)), skipDriveTests(false),
      skipGpuTests(false), developerMode(false), runStorageAnalysis(false),
      saveResults(false),
      skipCpuThrottlingTests(true)  // Default to true (skip tests)
      ,
      extendedCpuThrottlingTests(false), runCpuBoostTests(true),
      skipNetworkTests(false), runNetworkTests(true),
      extendedNetworkTests(false) {
  // Connect signals from DevToolsChecker
  connect(devToolsChecker, &DevToolsChecker::logMessage, this,
          &DiagnosticWorker::log);
  connect(devToolsChecker, &DevToolsChecker::toolCheckCompleted, this,
          &DiagnosticWorker::devToolsResultsReady);

  // Note: We no longer create a thread here - it's managed by DiagnosticView

  // Connect testStarted signal to update current test name
  connect(this, &DiagnosticWorker::testStarted, this,
          [this](const QString& testName) {
            std::lock_guard<std::mutex> lock(m_testNameMutex);
            m_currentTestName = testName;
          });

  // Register progress callback with DiagnosticDataStore
  auto& dataStore = DiagnosticDataStore::getInstance();
  dataStore.setEmitProgressCallback(
    [this](const QString& message, int progress) {
      // Only emit the test name, not the progress - we're handling progress
      // differently now
      emit testStarted(message);
    });
}

DiagnosticWorker::~DiagnosticWorker() {
  try {
    LOG_DEBUG << "DiagnosticWorker destructor called";

    // First properly cancel all operations
    try {
      LOG_DEBUG << "Canceling pending operations in DiagnosticWorker destructor";
      cancelPendingOperations();
    } catch (const std::exception& e) {
      LOG_ERROR << "Exception during operation cancellation in DiagnosticWorker destructor: " << e.what();
    } catch (...) {
      LOG_ERROR << "Unknown exception during operation cancellation in DiagnosticWorker destructor";
    }

    // Clean up memory test resources
    if (memoryTestFuture.valid()) {
      try {
        LOG_DEBUG << "Waiting for memory test to complete in DiagnosticWorker destructor";
        auto status = memoryTestFuture.wait_for(std::chrono::milliseconds(500));
        if (status == std::future_status::timeout) {
          LOG_WARN << "Memory test taking too long to complete, abandoning wait";
        } else {
          memoryTestFuture.get();
          LOG_DEBUG << "Memory test completed successfully";
        }
      } catch (const std::exception& e) {
        LOG_ERROR << "Exception during memory test cleanup in destructor: " << e.what();
      } catch (...) {
        LOG_ERROR << "Unknown exception during memory test cleanup in destructor";
      }
    }

    // Clean up GPU test resources
    if (activeGpuTest) {
      try {
        LOG_DEBUG << "Cleaning up active GPU test in DiagnosticWorker destructor";
        delete activeGpuTest;
        activeGpuTest = nullptr;
      } catch (const std::exception& e) {
        LOG_ERROR << "Exception during GPU test cleanup in DiagnosticWorker destructor: " << e.what();
      } catch (...) {
        LOG_ERROR << "Unknown exception during GPU test cleanup in DiagnosticWorker destructor";
      }
    }

    // Stop PDH metrics collection
    try {
      LOG_DEBUG << "Stopping PDH metrics collection in DiagnosticWorker destructor";
      stopPdhMetricsCollection();
    } catch (const std::exception& e) {
      LOG_ERROR << "Exception during metrics collection stop in DiagnosticWorker destructor: " << e.what();
    } catch (...) {
      LOG_ERROR << "Unknown exception during metrics collection stop in DiagnosticWorker destructor";
    }

    LOG_DEBUG << "DiagnosticWorker destroyed successfully";
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception in DiagnosticWorker destructor: " << e.what();
  } catch (...) {
    LOG_ERROR << "Unknown exception in DiagnosticWorker destructor";
  }
}

bool DiagnosticWorker::isRunningAsAdmin() const {
  BOOL isAdmin = FALSE;
  PSID adminGroup;
  SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

  // Create a SID for the Administrators group
  if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                               &adminGroup)) {
    CheckTokenMembership(NULL, adminGroup, &isAdmin);
    FreeSid(adminGroup);
  }

  return isAdmin;
}

bool DiagnosticWorker::restartAsAdmin() {
  QString app = QCoreApplication::applicationFilePath();

  // Start the same application with elevated privileges
  SHELLEXECUTEINFO sei = {sizeof(sei)};
  sei.lpVerb = L"runas";
  sei.lpFile = reinterpret_cast<LPCWSTR>(app.utf16());
  sei.hwnd = NULL;
  sei.nShow = SW_NORMAL;

  if (!ShellExecuteEx(&sei)) {
    DWORD error = GetLastError();
    if (error == ERROR_CANCELLED) {
      // User clicked "No" on UAC prompt
      return false;
    }
  }
  return true;
}

void DiagnosticWorker::runDiagnostics() {
  // This is now just a gateway to runDiagnosticsInternal
  // The actual work will happen when the thread is started
  // and the QThread::started signal triggers runDiagnosticsInternal

  // We can run directly if we're already in the worker thread
  if (QThread::currentThread() == thread()) {
    runDiagnosticsInternal();
  } else {
    // Use queued connection to safely invoke the method in the worker thread
    QMetaObject::invokeMethod(this, "runDiagnosticsInternal",
                              Qt::QueuedConnection);
  }
}

// Add this method to the DiagnosticWorker class implementation

void DiagnosticWorker::runDiagnosticsInternal() {
  // Set thread priority based on user settings
  HANDLE currentThread = GetCurrentThread();
  int originalPriority = GetThreadPriority(currentThread);

  // Generate a shared token for this diagnostic run to align all output files
  m_currentRunTimestamp = QDateTime::currentDateTime();
  m_currentRunToken = QString("%1_%2")
                        .arg(m_currentRunTimestamp.toString("yyyyMMdd_hhmmss"))
                        .arg(QUuid::createUuid()
                               .toString(QUuid::WithoutBraces)
                               .left(8));

  // Use the elevated priority setting from ApplicationSettings
  if (ApplicationSettings::getInstance().getElevatedPriorityEnabled()) {
    SetThreadPriority(currentThread, THREAD_PRIORITY_ABOVE_NORMAL);
    LOG_INFO << "Diagnostic thread priority set to ABOVE_NORMAL based on settings";
  } else {
    SetThreadPriority(currentThread, THREAD_PRIORITY_NORMAL);
    LOG_INFO << "Diagnostic thread priority set to NORMAL based on settings";
  }

  // Reset the diagnostic data store at the start of each run
  DiagnosticDataStore::getInstance().resetAllValues();

  // Start PDH metrics collection in the background
  startPdhMetricsCollection();

  // Check for admin privileges at the start
  if (!isRunningAsAdmin()) {
    QDialog dialog;
    dialog.setWindowTitle("Limited Diagnostics Mode");
    dialog.setFixedWidth(400);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    QLabel* iconLabel = new QLabel;
    iconLabel->setPixmap(QApplication::style()
                           ->standardIcon(QStyle::SP_MessageBoxWarning)
                           .pixmap(32, 32));

    QLabel* msgLabel = new QLabel(
      "Some tests require administrator privileges for accurate results.");
    msgLabel->setWordWrap(true);

    QLabel* infoLabel = new QLabel(
      "Running without administrator privileges may result in limited "
      "or inaccurate diagnostics for system components, drives, and hardware "
      "access.");
    infoLabel->setWordWrap(true);

    QDialogButtonBox* buttonBox =
      new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No);
    buttonBox->button(QDialogButtonBox::Yes)
      ->setText("Restart as Administrator");
    buttonBox->button(QDialogButtonBox::No)->setText("Continue Limited");
    buttonBox->button(QDialogButtonBox::No)->setDefault(true);

    QHBoxLayout* topLayout = new QHBoxLayout;
    topLayout->addWidget(iconLabel);
    topLayout->addWidget(msgLabel, 1);

    layout->addLayout(topLayout);
    layout->addWidget(infoLabel);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
      if (restartAsAdmin()) {
        QCoreApplication::quit();
        return;
      }
    }
    this->log("Running with limited diagnostics (no administrator privileges)");
  }

  log("Starting diagnostics...");
  emit progressUpdated(0);

  // Collect initial memory metrics using PDH
  try {
    // Note: Kernel memory tracking removed - using ConstantSystemInfo for static info
    // Dynamic memory metrics would use PdhInterface if needed
    log("Initial system state captured");
  } catch (const std::exception& e) {
    log(QString("Failed to capture initial system state: %1")
          .arg(e.what()));
  }

  // Add a helper function to ensure a clear break between tests
  auto ensureTestBreak = [this]() {
    QCoreApplication::processEvents();
    QThread::msleep(200);  // Consistent 200ms break between tests
  };

  // Define progress ranges for each test - simplified to be more consistent
  const int PROGRESS_TOTAL = 100;
  int currentProgress = 0;

  // Assign progress based on relative complexity/duration of each test
  // Background process test (0-10%)
  const int BG_PROCESS_WEIGHT = 10;
  const int MEMORY_INFO_WEIGHT = 5;
  const int CPU_TEST_WEIGHT = 20;
  const int MEMORY_TEST_WEIGHT = 15;
  const int GPU_TEST_WEIGHT = 15;
  const int DRIVE_TEST_WEIGHT = 15;
  const int NETWORK_TEST_WEIGHT = 10;
  const int FINAL_WEIGHT = 5;

  // Run background process analysis first
  emit testStarted("Background Process Analysis");
  emit progressUpdated(currentProgress);
  runBackgroundProcessTest();
  currentProgress += BG_PROCESS_WEIGHT;
  emit progressUpdated(currentProgress);
  ensureTestBreak();

  // Add memory info first
  emit testStarted("Memory Information");
  emit progressUpdated(currentProgress);
  getMemoryInfo();
  currentProgress += MEMORY_INFO_WEIGHT;
  emit progressUpdated(currentProgress);
  ensureTestBreak();

  // Run CPU tests
  emit testStarted("CPU Tests");
  emit progressUpdated(currentProgress);
  runCPUTest();
  currentProgress += CPU_TEST_WEIGHT;
  emit progressUpdated(currentProgress);
  ensureTestBreak();

  // Run Memory tests
  emit testStarted("Memory Tests");
  emit progressUpdated(currentProgress);
  runMemoryTest();
  currentProgress += MEMORY_TEST_WEIGHT;
  emit progressUpdated(currentProgress);
  ensureTestBreak();

  // Check skipGpuTests flag before running GPU test
  if (!skipGpuTests) {
    emit testStarted("GPU Tests");
    emit progressUpdated(currentProgress);
    log("Running GPU tests...");
    runGPUTest();
    currentProgress += GPU_TEST_WEIGHT;
    emit progressUpdated(currentProgress);
    ensureTestBreak();
  } else {
    log("GPU tests skipped.");
    currentProgress += GPU_TEST_WEIGHT;
    emit progressUpdated(currentProgress);
    ensureTestBreak();
  }

  // Check skipDriveTests flag before running drive test
  if (!skipDriveTests) {
    emit testStarted("Drive Tests");
    emit progressUpdated(currentProgress);
    log("Running drive tests...");
    runDriveTest();
    currentProgress += DRIVE_TEST_WEIGHT;
    emit progressUpdated(currentProgress);
    ensureTestBreak();
  } else {
    log("Drive tests skipped.");
    currentProgress += DRIVE_TEST_WEIGHT;
    emit progressUpdated(currentProgress);
    ensureTestBreak();
  }

  // Run Network tests if not skipped
  if (!skipNetworkTests) {
    emit testStarted("Network Tests");
    emit progressUpdated(currentProgress);
    log("Running network tests...");
    runNetworkTest();
    currentProgress += NETWORK_TEST_WEIGHT;
    emit progressUpdated(currentProgress);
    ensureTestBreak();
  } else {
    log("Network tests skipped.");
    currentProgress += NETWORK_TEST_WEIGHT;
    emit progressUpdated(currentProgress);
    ensureTestBreak();
  }

  // Developer tools tests
  if (developerMode) {
    emit testStarted("Developer Tools Analysis");
    emit progressUpdated(currentProgress);
    runDeveloperToolsTest();
    ensureTestBreak();
  }

  // Convert current results to JSON
  emit testStarted("Finalizing Results");
  emit progressUpdated(currentProgress);
  QJsonObject currentResults = resultsToJson();

  // Handle comparison if enabled
  if (compareMode) {
    emit testStarted("Comparing Results");
    QJsonArray previousResults = loadPreviousResults();
    emit comparisonReady(currentResults, previousResults);
    ensureTestBreak();
  }

  // Run storage analysis if requested
  if (runStorageAnalysis) {
    emit testStarted("Storage Analysis");
    performStorageAnalysis();
    ensureTestBreak();
  }

  // Always save diagnostic results locally to ensure uploads are optional/fail-safe
  emit testStarted("Saving Results");
  saveTestResults();
  ensureTestBreak();

  // Log final system state
  try {
    // Note: Kernel memory tracking removed - using ConstantSystemInfo for static info
    // Dynamic memory metrics would use PdhInterface if needed
    log("Final system state captured");
  } catch (const std::exception& e) {
    log(QString("Failed to capture final system state: %1")
          .arg(e.what()));
  }

  // Stop PDH metrics collection
  stopPdhMetricsCollection();

  // Export optimization settings to JSON
  try {
    auto& optManager = optimizations::OptimizationManager::GetInstance();

    QString appDir = QCoreApplication::applicationDirPath();
    QString resultsDir = appDir + "/diagnostic_results";
    QString optimizationSettingsFile =
      resultsDir + "/optimization_settings_" + getRunTokenForOutput() + ".json";

    // Create directory if needed
    QDir().mkpath(resultsDir);

    // Export settings to JSON
    if (optManager.ExportSettingsToJson(
          optimizationSettingsFile.toStdString())) {
      log("Optimization settings exported to: " + optimizationSettingsFile);
    } else {
      log("Failed to export optimization settings");
    }
  } catch (const std::exception& e) {
    log(QString("Error exporting optimization settings: %1").arg(e.what()));
  }

  // Make sure we get to 100% on the progress bar
  try {
    // Explicitly ensure 100% progress regardless of previous steps
    currentProgress = PROGRESS_TOTAL;
    emit progressUpdated(currentProgress);
    QThread::msleep(100);               // Let the progress update propagate
    QCoreApplication::processEvents();  // Process any pending events
  } catch (const std::exception& e) {
    log(QString("Error updating final progress: %1").arg(e.what()));
  }

  log("All diagnostics completed.");

  // Add a delay before signaling completion to ensure UI updates
  QThread::msleep(200);
  emit diagnosticsFinished();
  
  // Perform automatic upload if enabled
  performAutomaticUpload();

  // At the end of the method, restore original priority
  SetThreadPriority(currentThread, originalPriority);
}

void DiagnosticWorker::runCPUTest() {
  try {
    emit testStarted("CPU Test");
    log("Running CPU test...");

    // Get reference to the WinHardwareMonitor for live metrics
    WinHardwareMonitor monitor;

    try {
      // Update sensors to get fresh readings
      monitor.updateSensors();


      // Take several samples to get an average reading
      int numSamples = 5;
      int numCores = SystemMetrics::GetConstantSystemInfo().logicalCores;
      if (numCores <= 0) numCores = 1; // Fallback to 1 if not detected
      std::vector<double> avgCoreLoads(numCores, 0);
      std::vector<int> avgCoreClocks(numCores, 0);
      std::vector<double> avgCoreTemps(numCores, 0);
      std::vector<double> avgCorePowers(numCores, 0);

      log("Sampling CPU sensor data...");
      emit testStarted("CPU Test: Collecting Sensor Data");

      for (int i = 0; i < numSamples; i++) {
        try {
          // Update sensors to get fresh readings
          monitor.updateSensors();

          // Get current measurements from WinHardwareMonitor
          auto cpuInfo = monitor.getCPUInfo();
          std::vector<double> coreLoads = cpuInfo.coreLoads;
          std::vector<int> coreClocks = cpuInfo.coreClocks;
          std::vector<double> coreTemps = cpuInfo.coreTemperatures;
          std::vector<double> corePowers = cpuInfo.corePowers;

          // Add to running total
          for (size_t i = 0; i < avgCoreLoads.size(); i++) {
            if (i < coreLoads.size()) avgCoreLoads[i] += coreLoads[i];
            if (i < coreClocks.size()) avgCoreClocks[i] += coreClocks[i];
            if (i < coreTemps.size()) avgCoreTemps[i] += coreTemps[i];
            if (i < corePowers.size()) avgCorePowers[i] += corePowers[i];
          }
        } catch (const std::exception& e) {
          log(QString("Error during CPU sensor reading [sample %1]: %2")
                .arg(i + 1)
                .arg(e.what()));
          // Continue with next sample
        }

        // Increase delay between measurements to allow sensors to update
        // 250ms is a more reasonable time for hardware sensors
        QThread::msleep(250);
      }

      // Calculate averages
      for (size_t i = 0; i < avgCoreLoads.size(); i++) {
        avgCoreLoads[i] /= numSamples;
        avgCoreClocks[i] /= numSamples;
        avgCoreTemps[i] /= numSamples;
        avgCorePowers[i] /= numSamples;
      }
    } catch (const std::exception& e) {
      log(QString("Error collecting CPU sensor data: %1").arg(e.what()));
      // Continue with test - we can still run performance tests without sensor
      // data
    }

    // Run the actual CPU tests
    try {
      emit testStarted("CPU Test: Basic Performance Tests");
      log("Running CPU performance tests...");
      runCpuTests();
    } catch (const std::exception& e) {
      log(QString("Error during CPU performance tests: %1").arg(e.what()));
      // Continue with next tests
    }

    // Add CPU boost behavior test - now conditional
    if (runCpuBoostTests) {
      try {
        log("Running CPU boost behavior test...");
        emit testStarted("CPU Test: Boost Behavior");
        runCpuBoostBehaviorTest();
        log("CPU boost behavior test completed.");
      } catch (const std::exception& e) {
        log(QString("Error during CPU boost behavior test: %1").arg(e.what()));
        // Continue with next tests
      }
    } else {
      log("CPU boost behavior test skipped.");
    }

    // Add condition for CPU throttling tests with different modes
    if (!skipCpuThrottlingTests) {
      try {
        if (extendedCpuThrottlingTests) {
          log("Running extended CPU throttling tests...");
          emit testStarted("CPU Test: Extended Throttling Test");
          runCombinedThrottlingTest(
            CpuThrottle_Extended);  // Run the extended combined test (180s)
          log("Extended CPU throttling tests completed.");
        } else {
          log("Running basic CPU throttling test...");
          emit testStarted("CPU Test: Basic Throttling Test");
          runCombinedThrottlingTest(
            CpuThrottle_Basic);  // Run the basic combined test (30s)
          log("Basic CPU throttling test completed.");
        }
      } catch (const std::exception& e) {
        log(QString("Error during CPU throttling test: %1").arg(e.what()));
        // Continue with next tests
      }
    } else {
      log("CPU throttling tests skipped.");
    }

    try {
      // Initialize metrics storage based on number of cores
      int numCores = SystemMetrics::GetConstantSystemInfo().logicalCores;
      cpuBoostMetrics.resize(numCores);

      // Add per-core boost behavior test - now conditional
      if (runCpuBoostTests) {
        log("Running per-core CPU boost behavior test...");
        emit testStarted("CPU Test: Per-Core Boost Analysis");

        try {
          // We'll need to hook into the test to capture its results
          runCpuBoostBehaviorPerCoreTest();

          // Explicitly copy global metrics data to ensure the latest values are
          // used
          for (size_t i = 0; i < numCores && i < g_cpuBoostMetrics.size();
               i++) {
            cpuBoostMetrics[i] = g_cpuBoostMetrics[i];
          }

          // Also capture the global metrics values
          idleTotalPower = g_idleTotalPower;
          allCoreTotalPower = g_allCoreTotalPower;
          bestBoostCore = g_bestBoostCore;
          maxBoostDelta = g_maxBoostDelta;
        } catch (const std::exception& e) {
          log(QString("Error during per-core CPU boost behavior test: %1")
                .arg(e.what()));
          // Fill with empty data to avoid crashes
          for (size_t i = 0; i < numCores; i++) {
            cpuBoostMetrics[i] = CoreBoostMetrics{};  // Empty/default metrics
          }
        }

        log("Per-core CPU boost behavior test completed.");
      } else {
        log("Per-core CPU boost behavior test skipped.");
      }
    } catch (const std::exception& e) {
      log(QString("Error during CPU boost metrics initialization: %1")
            .arg(e.what()));
      // Continue to format results with whatever data we have
    }

    // Get CPU data from DiagnosticDataStore
    auto& dataStore = DiagnosticDataStore::getInstance();
    auto& cpuData = dataStore.getCPUData();

    // Format the result string
    QString cpuResult =
      QString("Model: %1\n"
              "Cores: %2, Threads: %3\n"
              "SIMD Scalar: %4 us\n"
              "AVX: %5 us\n"
              "Prime: %6 ms\n"
              "Single: %7 ms\n"
              "Multi: %8 ms\n"
              "Game Sim Small: %9 ups\n"
              "Game Sim Medium: %10 ups\n"
              "Game Sim Large: %11 ups\n\n"
              "Per-Core Metrics (averaged over %12 samples):")
        .arg(QString::fromStdString(cpuData.name))
        .arg(cpuData.physicalCores)
        .arg(cpuData.threadCount)
        .arg(cpuData.simdScalar)
        .arg(cpuData.simdAvx)
        .arg(cpuData.primeTime)
        .arg(cpuData.singleCoreTime)
        .arg(cpuData.fourThreadTime > 0 ? cpuData.fourThreadTime : -1.0)
        .arg(QString::number(cpuData.gameSimUPS_small, 'f', 0))
        .arg(QString::number(cpuData.gameSimUPS_medium, 'f', 0))
        .arg(QString::number(cpuData.gameSimUPS_large, 'f', 0))
        .arg(5);  // Use fixed value since we don't have the actual numSamples

    // Try to safely add per-core metrics to the result string
    try {
      WinHardwareMonitor monitor;
      monitor.updateSensors();
      auto cpuInfo = monitor.getCPUInfo();
      
      std::vector<double> coreLoads = cpuInfo.coreLoads;
      std::vector<int> coreClocks = cpuInfo.coreClocks;
      std::vector<double> coreTemps = cpuInfo.coreTemperatures;
      std::vector<double> corePowers = cpuInfo.corePowers;

      // Get logical core count from ConstantSystemInfo
      const SystemMetrics::ConstantSystemInfo& constantInfo = SystemMetrics::GetConstantSystemInfo();
      int logicalCores = constantInfo.logicalCores;

      for (int i = 0; i < logicalCores; i++) {
        QString coreInfo =
          QString("\nCore #%1: %2 MHz (Load: %3%, ")
            .arg(i)
            .arg(i < coreClocks.size() ? coreClocks[i] : 0)
            .arg(i < coreLoads.size() ? coreLoads[i] : 0.0, 0, 'f', 1);

        if (i < coreTemps.size() && coreTemps[i] > 0)
          coreInfo += QString("Temp: %1°C, ").arg(coreTemps[i], 0, 'f', 1);

        if (i < corePowers.size() && corePowers[i] > 0)
          coreInfo += QString("Power: %1W").arg(corePowers[i], 0, 'f', 2);
        else
          coreInfo.chop(2);  // Remove trailing comma and space if no power data

        coreInfo += ")";
        cpuResult += coreInfo;
      }
    } catch (const std::exception& e) {
      log(QString("Error formatting per-core metrics: %1").arg(e.what()));
      cpuResult += "\nError retrieving per-core metrics";
    }

    // Cache metrics
    QString cacheResult;
    try {
      cacheResult = QString("Cache Latencies:\n");
      // Safely access cache latencies - it's a C-style array with 12 elements
      const size_t MAX_EXPECTED_LATENCIES =
        11;  // We only want to display up to 11 latencies

      // No need to check size() since it's a fixed array - just iterate safely
      for (size_t i = 0; i < MAX_EXPECTED_LATENCIES; i++) {
        int size = (32 << i);  // 32KB to 32MB

        // Check if the value is valid (not the default -1.0)
        if (cpuData.cache.latencies[i] > 0) {
          if (size < 1024) {
            cacheResult += QString("%1 KB: %2 ns\n")
                             .arg(size)
                             .arg(cpuData.cache.latencies[i]);
          } else {
            cacheResult += QString("%1 MB: %2 ns\n")
                             .arg(size / 1024)
                             .arg(cpuData.cache.latencies[i]);
          }
        }
      }

      // Check if we have valid latency data (no need to check array size)
      bool hasValidLatencyData = false;
      for (size_t i = 0; i < MAX_EXPECTED_LATENCIES; i++) {
        if (cpuData.cache.latencies[i] > 0) {
          hasValidLatencyData = true;
          break;
        }
      }

      if (!hasValidLatencyData) {
        cacheResult +=
          QString("Note: No valid cache latency data collected.\n");
      }
    } catch (const std::exception& e) {
      log(QString("Error formatting cache latencies: %1").arg(e.what()));
      cacheResult = "Cache Latencies: Error retrieving data";
    }

    // Finalizing CPU test
    emit testStarted("CPU Test: Finalizing");

    // Emit CPU test completed first
    emit cpuTestCompleted(cpuResult);

    // Use QTimer to delay the cache test signal
    QTimer::singleShot(100, this, [this, cacheResult]() {
      // Ensure cache data is fully available in data store before emitting
      // signal
      emit cacheTestCompleted(cacheResult);
    });

    emit testCompleted("CPU Test");
  } catch (const std::exception& e) {
    log(QString("Unhandled exception in CPU test: %1").arg(e.what()));

    // Make sure we send back some results even on total failure
    emit cpuTestCompleted("CPU Test Error: " + QString(e.what()));

    // Send empty cache result to prevent UI waiting for it
    QTimer::singleShot(100, this, [this]() {
      emit cacheTestCompleted("Cache Latencies: Error during test");
    });

    emit testCompleted("CPU Test");
  } catch (...) {
    log("Unknown exception in CPU test");

    // Make sure we send back some results even on total failure
    emit cpuTestCompleted("CPU Test Error: Unknown exception occurred");

    // Send empty cache result to prevent UI waiting for it
    QTimer::singleShot(100, this, [this]() {
      emit cacheTestCompleted("Cache Latencies: Error during test");
    });

    emit testCompleted("CPU Test");
  }
}

// Fix for runMemoryTestsAsync - it requires a parameter
void DiagnosticWorker::runMemoryTest() {
  emit testStarted("Memory Test");
  log("Running Memory test...");

  // Get reference to our data store
  auto& dataStore = DiagnosticDataStore::getInstance();

  try {
    // First, just call getMemoryInfo() normally without capturing its output
    emit testStarted("Memory Test: Collecting System Memory Information");
    getMemoryInfo();

    // Now the data store has the correct module information

    // Create a local memory data object to pass to the async function
    DiagnosticDataStore::MemoryData memoryMetrics;

    // Then run memory tests - always run synchronously
    log("Starting memory benchmarks...");
    emit testStarted("Memory Test: Running Memory Bandwidth Test");
    memoryTestFuture = runMemoryTestsAsync(&memoryMetrics);

    try {
      // Block until complete - no async here
      memoryTestFuture.get();
      log("Memory benchmarks completed");

      // Build and emit memory result string
      QString memoryResult =
        formatMemoryResultString(dataStore.getMemoryData());
      emit testStarted("Memory Test: Finalizing");
      emit memoryTestCompleted(memoryResult);
    } catch (const std::exception& e) {
      log(QString("Memory test failed: %1").arg(e.what()));
      emit memoryTestCompleted("Memory test failed");
    }
  } catch (const std::exception& e) {
    log(QString("Memory info retrieval failed: %1").arg(e.what()));
    emit memoryTestCompleted("Memory test failed");
  }
}

void DiagnosticWorker::runGPUTest() {
  try {
    emit testStarted("GPU Test");
    log("Running GPU test...");

    // Create GPU test instance and store pointer for cancellation
    GPUTest* gpuTest = nullptr;

    try {
      gpuTest = new GPUTest();
      activeGpuTest = gpuTest;

      // Create a proper window for D3D rendering
      emit testStarted("GPU Test: Initializing DirectX");

      WNDCLASSEXW wc = {};
      wc.cbSize = sizeof(WNDCLASSEXW);
      wc.lpfnWndProc = DefWindowProc;
      wc.hInstance = GetModuleHandle(nullptr);
      wc.lpszClassName = L"GPUTestWorkerClass";

      if (!RegisterClassExW(&wc)) {
        throw std::runtime_error(
          "Failed to register window class for GPU test");
      }

      HWND hwnd =
        CreateWindowW(L"GPUTestWorkerClass", L"GPU Test Worker",
                      WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800,
                      600, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

      if (!hwnd) {
        UnregisterClassW(L"GPUTestWorkerClass", GetModuleHandle(nullptr));
        throw std::runtime_error("Failed to create window for GPU test");
      }

      // Show the window so it's visible during the GPU test
      ShowWindow(hwnd, SW_SHOW);

      bool initSuccess = false;
      try {
        // Try to initialize the GPU test with a valid window
        initSuccess = gpuTest->Initialize(hwnd);
        if (!initSuccess) {
          log("GPU test initialization failed - your system may not support "
              "the required DirectX features");
        } else {
          // Only run the test if initialization succeeded
          emit testStarted("GPU Test: Rendering Test");

          // Run the actual GPU test
          gpuTest->RunTest();

          emit testStarted("GPU Test: Finalizing");
        }
      } catch (const std::exception& e) {
        log(QString("GPU test exception during initialization/run: %1")
              .arg(e.what()));
        // Don't rethrow - continue with cleanup
      }

      // Cleanup window
      DestroyWindow(hwnd);
      UnregisterClassW(L"GPUTestWorkerClass", GetModuleHandle(nullptr));
    } catch (const std::exception& e) {
      log(QString("GPU test exception during resource creation: %1")
            .arg(e.what()));
    }

    // Clean up GPU test resources
    try {
      if (gpuTest) {
        delete gpuTest;
      }
    } catch (const std::exception& e) {
      log(QString("Exception during GPU test cleanup: %1").arg(e.what()));
    }

    // Always reset the active pointer, no matter what happened
    activeGpuTest = nullptr;

    // Get GPU data from DiagnosticDataStore
    auto& dataStore = DiagnosticDataStore::getInstance();
    auto& gpuData = dataStore.getGPUData();

    QString gpuResult = QString("Driver: %1\n"
                                "Avg FPS: %2\n"
                                "Total Frames: %3")
                          .arg(QString::fromStdString(gpuData.driverVersion))
                          .arg(gpuData.averageFPS)
                          .arg(gpuData.totalFrames);

    if (gpuData.averageFPS <= 0) {
      gpuResult += "\n\nGPU test failed: DirectX initialization error. Your "
                   "system may not support the required DirectX features.";
    }

    emit gpuTestCompleted(gpuResult);
  } catch (const std::exception& e) {
    log(QString("Unhandled GPU test exception: %1").arg(e.what()));

    // Ensure we always emit a result even on exception
    QString errorResult = "GPU Test Error: " + QString(e.what());
    emit gpuTestCompleted(errorResult);

    // Make sure activeGpuTest is nulled
    activeGpuTest = nullptr;
  } catch (...) {
    log("Unknown exception in GPU test");
    emit gpuTestCompleted("GPU Test Error: Unknown exception occurred");
    activeGpuTest = nullptr;
  }
}

// Fix for DriveData access - access via drives collection
void DiagnosticWorker::runDriveTest() {
  emit testStarted("Drive Test");
  log("Running Drive test...");

  auto& dataStore = DiagnosticDataStore::getInstance();

  if (systemDriveOnlyMode) {
    log("Testing system drive only mode");
    emit testStarted("Drive Test: Examining System Drive");

    // Get system drive path (usually C:\)
    std::string systemDrivePath = "C:\\";  // Default
    char windowsDir[MAX_PATH];
    if (GetWindowsDirectoryA(windowsDir, MAX_PATH) > 0) {
      systemDrivePath = std::string(windowsDir, 3);  // Extract "C:\"
    }

    // Test only the system drive
    try {
      emit testStarted(QString("Drive Test: Testing %1")
                         .arg(QString::fromStdString(systemDrivePath)));

      auto results = testDrivePerformance(systemDrivePath);

      // Create drive metrics and store directly in DiagnosticDataStore
      DiagnosticDataStore::DriveData::DriveMetrics driveMetrics;
      driveMetrics.drivePath = systemDrivePath;
      driveMetrics.seqWrite = results.sequentialWriteMBps;
      driveMetrics.seqRead = results.sequentialReadMBps;
      driveMetrics.iops4k = results.iops4k;
      driveMetrics.accessTimeMs = results.accessTimeMs;

      dataStore.updateDriveMetrics(driveMetrics.drivePath, driveMetrics.seqRead,
                                   driveMetrics.seqWrite, driveMetrics.iops4k,
                                   driveMetrics.accessTimeMs);

      emit testStarted("Drive Test: Finalizing Results");
    } catch (const std::exception& e) {
      log(QString("Drive test failed for system drive: %1").arg(e.what()));
    }
  } else {
    // Run the normal test on all drives
    emit testStarted("Drive Test: Detecting Drives");

    // Get drive information first
    char driveStrings[256];
    if (GetLogicalDriveStringsA(sizeof(driveStrings), driveStrings) == 0) {
      log("Failed to retrieve drives.");
      return;
    }

    // Count drives to distribute progress updates
    int driveCount = 0;
    const char* drive = driveStrings;
    while (*drive) {
      driveCount++;
      drive += strlen(drive) + 1;
    }

    // Reset drive pointer
    drive = driveStrings;

    // Calculate progress increment per drive
    int currentDrive = 0;

    log(QString("Found %1 drive(s) to test").arg(driveCount));

    while (*drive) {
      currentDrive++;

      emit testStarted(QString("Drive Test: Testing Drive %1 of %2 (%3)")
                         .arg(currentDrive)
                         .arg(driveCount)
                         .arg(QString::fromUtf8(drive)));

      log(QString("\nTesting Drive: %1").arg(QString::fromUtf8(drive)));

      try {
        auto results = testDrivePerformance(drive);

        // Update DriveMetrics in DiagnosticDataStore
        dataStore.updateDriveMetrics(drive, results.sequentialReadMBps,
                                     results.sequentialWriteMBps,
                                     results.iops4k, results.accessTimeMs);
      } catch (const std::exception& e) {
        log(QString("Drive test failed for %1: %2")
              .arg(QString::fromUtf8(drive))
              .arg(e.what()));
      }

      drive += strlen(drive) + 1;
    }

    emit testStarted("Drive Test: Finalizing Results");
  }

  // Format and emit results
  QString driveResult;
  const auto& driveDataList = dataStore.getDriveData().drives;

  // Format results for tested drives
  for (const auto& drive : driveDataList) {
    driveResult += QString("PATH=%1\n"  // Add PATH marker to identify drive
                           "Read: %2 MB/s\n"
                           "Write: %3 MB/s\n"
                           "4K IOPS: %4\n"
                           "Access Time: %5 ms\n\n")
                     .arg(QString::fromStdString(drive.drivePath))
                     .arg(drive.seqRead)
                     .arg(drive.seqWrite)
                     .arg(drive.iops4k)
                     .arg(drive.accessTimeMs);
  }

  emit driveTestCompleted(driveResult);
}

void DiagnosticWorker::runBackgroundProcessTest() {
  emit testStarted("Background Process Test");
  log("Running background process analysis...");

  // Monitor for 15 seconds to get a good snapshot of system activity
  const int monitorDuration = 15;
  log(QString("Monitoring background processes for %1 seconds...")
        .arg(monitorDuration));
  emit testStarted("Background Process: Initializing Monitors");

  // Create a sync object for waiting
  QEventLoop waitLoop;
  bool testCompleted = false;

  // Create worker thread
  QThread* bgThread = new QThread();
  bgThread->setObjectName("BackgroundProcessThread");

  // Create worker
  BackgroundProcessWorker* worker = new BackgroundProcessWorker();
  worker->moveToThread(bgThread);

  // Connect signals
  connect(bgThread, &QThread::started, worker, [worker, monitorDuration]() {
    worker->startMonitoring(monitorDuration);
  });

  // Set up a timer to provide status updates during monitoring
  QTimer* statusTimer = new QTimer();
  int elapsedTime = 0;

  connect(
    statusTimer, &QTimer::timeout, [this, &elapsedTime, monitorDuration]() {
      elapsedTime++;

      // Update status every few seconds
      if (elapsedTime % 3 == 0 || elapsedTime == 1) {
        emit testStarted(QString("Background Process: Monitoring (%1/%2 sec)")
                           .arg(elapsedTime)
                           .arg(monitorDuration));
      }
    });

  connect(
    worker, &BackgroundProcessWorker::monitoringFinished, this,
    [this, bgThread, worker, &waitLoop, &testCompleted,
     statusTimer](const BackgroundProcessMonitor::MonitoringResult& result) {
      // Stop the status timer
      statusTimer->stop();
      statusTimer->deleteLater();

      // Processing phase
      emit testStarted("Background Process: Analyzing Results");

      // Process results
      processBackgroundMonitorResults(result);

      // Signal completion
      testCompleted = true;
      waitLoop.quit();

      // Clean up
      bgThread->quit();
      bgThread->wait();
      worker->deleteLater();
      bgThread->deleteLater();
    },
    Qt::QueuedConnection);

  // Start the thread
  bgThread->start();

  // Start the status timer
  statusTimer->start(1000);  // Update every second

  // Wait with timeout (monitorDuration + 5 seconds for processing)
  QTimer timer;
  connect(&timer, &QTimer::timeout, &waitLoop, &QEventLoop::quit);
  timer.start((monitorDuration + 5) * 1000);

  waitLoop.exec();

  // If not completed, force cleanup
  if (!testCompleted) {
    log("Background process monitoring timed out");

    // Stop and delete the status timer if it's still running
    if (statusTimer) {
      statusTimer->stop();
      statusTimer->deleteLater();
    }

    bgThread->quit();
    bgThread->wait(1000);
    if (bgThread->isRunning()) {
      bgThread->terminate();
      bgThread->wait();
    }
    worker->deleteLater();
    bgThread->deleteLater();
  }
}

// Process the background monitoring results and update DiagnosticDataStore
void DiagnosticWorker::processBackgroundMonitorResults(
  const BackgroundProcessMonitor::MonitoringResult& result) {
  auto& dataStore = DiagnosticDataStore::getInstance();
  auto bgData = dataStore.getBackgroundProcessData();

  // Update background process data in DiagnosticDataStore
  bgData.systemCpuUsage = result.totalCpuUsage;
  bgData.systemDpcTime = result.systemDpcTime;
  bgData.systemInterruptTime = result.systemInterruptTime;
  bgData.hasDpcLatencyIssues = result.hasDpcLatencyIssues;

  // Process results for UI
  QString backgroundResult = "Background Process Analysis Results:\n\n";
  backgroundResult += "System Resource Usage:\n";
  backgroundResult +=
    QString("  CPU Usage: %1%\n").arg(result.totalCpuUsage, 0, 'f', 2);
  backgroundResult +=
    QString("  GPU Usage: %1%\n").arg(result.totalGpuUsage, 0, 'f', 2);
  backgroundResult += QString("  Disk I/O: %1 MB/s\n")
                        .arg(result.totalDiskIO / (1024 * 1024), 0, 'f', 2);
  backgroundResult +=
    QString("  DPC Time: %1%\n").arg(result.systemDpcTime, 0, 'f', 2);
  backgroundResult += QString("  Interrupt Time: %1%\n\n")
                        .arg(result.systemInterruptTime, 0, 'f', 2);

  // Add more detailed formatting as needed
  if (result.hasDpcLatencyIssues) {
    backgroundResult += "⚠️ HIGH DPC/INTERRUPT LATENCY DETECTED!\n";
    backgroundResult +=
      "   This may indicate driver issues causing stuttering.\n\n";
  }

  if (result.hasHighCpuProcesses) {
    backgroundResult += "⚠️ High CPU usage background processes detected\n\n";
  }

  if (result.hasHighGpuProcesses) {
    backgroundResult += "⚠️ High GPU usage background processes detected\n\n";
  }

  // Add detailed process list if available
  if (!result.processes.empty()) {
    backgroundResult += "High Resource Usage Applications:\n";
    for (const auto& proc : result.processes) {
      if (proc.cpuPercent > 1.0 || proc.gpuPercent > 1.0 ||
          proc.memoryUsageKB > 100000) {
        backgroundResult +=
          QString("• %1 (CPU: %2%, GPU: %3%, Memory: %4 MB)\n")
            .arg(QString::fromStdWString(proc.name))
            .arg(proc.cpuPercent, 0, 'f', 1)
            .arg(proc.gpuPercent, 0, 'f', 1)
            .arg(proc.memoryUsageKB / 1024);
      }
    }
    backgroundResult += "\n";
  }

  // Add system processes if available
  if (!result.systemProcesses.empty()) {
    backgroundResult += "System Processes:\n";
    for (const auto& proc : result.systemProcesses) {
      if (proc.cpuPercent > 0.5) {
        backgroundResult += QString("• %1 (CPU: %2%)\n")
                              .arg(QString::fromStdWString(proc.name))
                              .arg(proc.cpuPercent, 0, 'f', 1);

        // Add to DiagnosticDataStore
        DiagnosticDataStore::BackgroundProcessData::ProcessInfo processInfo;
        processInfo.name = wstringToString(proc.name);
        processInfo.cpuPercent = proc.cpuPercent;
        processInfo.peakCpuPercent = proc.peakCpuPercent;
        processInfo.memoryUsageKB = proc.memoryUsageKB;
        processInfo.gpuPercent = proc.gpuPercent;

        bgData.topCpuProcesses.push_back(processInfo);
      }
    }
  }

  // Update the DiagnosticDataStore with the processed data
  dataStore.setBackgroundProcessData(bgData);

  // Emit the result
  emit backgroundProcessTestCompleted(backgroundResult);
}

void DiagnosticWorker::addResult(const QString& tool, bool found,
                                 const QString& version) {
  log(QString("[%1] Status: %2").arg(tool).arg(found ? "Found" : "Not Found"));
  if (!version.isEmpty()) {
    log(QString("[%1] Version: %2").arg(tool, version));
  }
  log("-----------------------------------------------");

  devToolsResults += QString("%1:\t<span style='color: %2;'>%3</span><br>")
                       .arg(tool)
                       .arg(found ? "#0078d4" : "#ff4444")
                       .arg(found ? version : QString("Not Found"));
}

void DiagnosticWorker::runDeveloperToolsTest() {
  log("\n===============================================");
  log("Starting Developer Tools Check");
  log("===============================================\n");

  devToolsChecker->checkAllTools();
}

void DiagnosticWorker::log(const QString& message) const {  // Add const here
  LOG_INFO << message.toStdString();
}

void DiagnosticWorker::performStorageAnalysis() {
  log("Starting comprehensive storage analysis...");

  try {
    // Set up progress reporting callback
    auto progressCallback = [this](const std::wstring& message, int progress) {
      // Convert wstring to QString for Qt signals
      QString qMessage = QString::fromStdWString(message);

      // Emit progress updates to the UI
      emit testStarted(qMessage);
      emit progressUpdated(progress);

      // Also log to console for debugging
      LOG_DEBUG << "Storage Analysis: " << qMessage.toStdString() << " (" << progress << "%)";
    };

    // Use 2-minute timeout and progress reporting
    std::chrono::seconds timeout(120);  // 2 minutes

    // Use C: drive as default root path, but could be configurable
    std::wstring rootPath = L"C:\\";

    log(QString("Starting analysis of %1 with %2 second timeout...")
          .arg(QString::fromStdWString(rootPath))
          .arg(timeout.count()));

    auto results =
      StorageAnalysis::analyzeStorageUsage(rootPath, timeout, progressCallback);

    // Log final statistics
    log(
      QString(
        "Storage analysis completed - Scanned %1 files and %2 folders in %3ms")
        .arg(results.totalFilesScanned)
        .arg(results.totalFoldersScanned)
        .arg(results.actualDuration.count()));

    if (results.timedOut) {
      log("Storage analysis timed out - showing partial results");
    }

    emit storageAnalysisReady(results);
  } catch (const std::exception& e) {
    log(QString("Storage analysis error: %1").arg(e.what()));

    // Create empty results to indicate error
    StorageAnalysis::AnalysisResults emptyResults;
    emptyResults.timedOut = true;  // Mark as timed out to indicate issue
    emit storageAnalysisReady(emptyResults);
  }

  log("Storage analysis completed.");
}

QJsonObject DiagnosticWorker::resultsToJson() const {
  QJsonObject results;
  auto& dataStore = DiagnosticDataStore::getInstance();
  const SystemMetrics::ConstantSystemInfo& constantInfo = SystemMetrics::GetConstantSystemInfo();

  // Get data from DiagnosticDataStore
  const auto& cpuData = dataStore.getCPUData();
  const auto& memoryData = dataStore.getMemoryData();
  const auto& gpuData = dataStore.getGPUData();
  const auto& driveData = dataStore.getDriveData();
  const auto& backgroundData = dataStore.getBackgroundProcessData();
  const auto& networkData = dataStore.getNetworkData();

  // CPU section
  QJsonObject cpu;

  // Enhanced CPU info with data from DiagnosticDataStore and ConstantSystemInfo
  QJsonObject cpuInfo;
  cpuInfo["model"] = QString::fromStdString(constantInfo.cpuName);
  cpuInfo["vendor"] = QString::fromStdString(constantInfo.cpuVendor);
  cpuInfo["cores"] = cpuData.physicalCores;
  cpuInfo["threads"] = cpuData.threadCount;
  cpuInfo["architecture"] =
    QString::fromStdString(constantInfo.cpuArchitecture);
  cpuInfo["socket"] = QString::fromStdString(constantInfo.cpuSocket);

  // Add virtualization status from constant info
  cpuInfo["virtualization"] =
    constantInfo.virtualizationEnabled ? "Enabled" : "Disabled";

  // Add HyperThreading/SMT status based on core vs thread count
  cpuInfo["smt"] = constantInfo.hyperThreadingEnabled ? "Enabled" : "Disabled";

  // Add feature support
  cpuInfo["avx_support"] = constantInfo.avxSupport;
  cpuInfo["avx2_support"] = constantInfo.avx2Support;

  // Get base and max clock speeds from ConstantSystemInfo
  cpuInfo["base_clock_mhz"] = constantInfo.baseClockMHz;
  cpuInfo["max_clock_mhz"] = constantInfo.maxClockMHz;

  // Add cache info from constant info
  if (constantInfo.l1CacheKB > 0 || constantInfo.l2CacheKB > 0 ||
      constantInfo.l3CacheKB > 0) {
    QJsonObject cacheInfo;
    if (constantInfo.l1CacheKB > 0) cacheInfo["l1_kb"] = constantInfo.l1CacheKB;
    if (constantInfo.l2CacheKB > 0) cacheInfo["l2_kb"] = constantInfo.l2CacheKB;
    if (constantInfo.l3CacheKB > 0) cacheInfo["l3_kb"] = constantInfo.l3CacheKB;
    cpuInfo["cache_info"] = cacheInfo;
  }

  // Add per-core metrics
  QJsonArray coreDetails;
  
  // Get live metrics from WinHardwareMonitor
  WinHardwareMonitor monitor;
  monitor.updateSensors();
  auto cpuInfo_hw = monitor.getCPUInfo();
  
  std::vector<double> coreLoads = cpuInfo_hw.coreLoads;
  std::vector<int> coreClocks = cpuInfo_hw.coreClocks;
  std::vector<double> coreTemps = cpuInfo_hw.coreTemperatures;
  std::vector<double> corePowers = cpuInfo_hw.corePowers;

  for (int i = 0; i < constantInfo.logicalCores; i++) {
    QJsonObject core;
    core["core_number"] = static_cast<int>(i);

    if (i < coreClocks.size()) {
      core["clock_mhz"] = coreClocks[i];
    }

    if (i < coreLoads.size()) {
      core["load_percent"] = coreLoads[i];
    }

    if (i < coreTemps.size() && coreTemps[i] > 0) {
      core["temperature_c"] = coreTemps[i];
    }

    if (i < corePowers.size() && corePowers[i] > 0) {
      core["power_w"] = corePowers[i];
    }

    // Add boost behavior metrics if available
    if (i < cpuBoostMetrics.size()) {
      QJsonObject boostMetrics;
      boostMetrics["idle_clock_mhz"] = cpuBoostMetrics[i].idleClock;
      boostMetrics["single_load_clock_mhz"] =
        cpuBoostMetrics[i].singleLoadClock;
      boostMetrics["all_core_clock_mhz"] = cpuBoostMetrics[i].allCoreClock;
      boostMetrics["boost_delta_mhz"] =
        cpuBoostMetrics[i].singleLoadClock - cpuBoostMetrics[i].idleClock;
      core["boost_metrics"] = boostMetrics;
    }

    coreDetails.append(core);
  }
  cpuInfo["cores_detail"] = coreDetails;

  // Add overall boost behavior summary
  QJsonObject boostSummary;
  boostSummary["idle_power_w"] = idleTotalPower;
  boostSummary["single_core_power_w"] = singleCoreTotalPower;
  boostSummary["all_core_power_w"] = allCoreTotalPower;
  boostSummary["best_boosting_core"] = bestBoostCore;
  boostSummary["max_boost_delta_mhz"] = maxBoostDelta;
  cpuInfo["boost_summary"] = boostSummary;

  // Always add throttling information (even if not detected)
  QJsonObject throttlingInfo;
  throttlingInfo["detected"] = cpuData.throttlingDetected;
  throttlingInfo["peak_clock"] = cpuData.peakClock;
  throttlingInfo["sustained_clock"] = cpuData.sustainedClock;
  throttlingInfo["clock_drop_percent"] = cpuData.clockDropPercent;
  throttlingInfo["detected_time_seconds"] = cpuData.throttlingDetectedTime;
  cpuInfo["throttling"] = throttlingInfo;

  // Add CPU cold start response metrics if available
  if (cpuData.coldStart.avgResponseTimeUs > 0) {
    QJsonObject coldStartInfo;
    coldStartInfo["avg_response_time_us"] = cpuData.coldStart.avgResponseTimeUs;
    coldStartInfo["min_response_time_us"] = cpuData.coldStart.minResponseTimeUs;
    coldStartInfo["max_response_time_us"] = cpuData.coldStart.maxResponseTimeUs;
    coldStartInfo["std_dev_us"] = cpuData.coldStart.stdDevUs;
    coldStartInfo["variance_us"] = cpuData.coldStart.varianceUs;
    cpuInfo["cold_start"] = coldStartInfo;
  }

  // Add C-state power management data
  if (cpuData.cStates.c1TimePercent >= 0 ||
      cpuData.cStates.c2TimePercent >= 0 ||
      cpuData.cStates.c3TimePercent >= 0) {
    QJsonObject cStateInfo;
    cStateInfo["c1_time_percent"] = cpuData.cStates.c1TimePercent;
    cStateInfo["c2_time_percent"] = cpuData.cStates.c2TimePercent;
    cStateInfo["c3_time_percent"] = cpuData.cStates.c3TimePercent;
    cStateInfo["c1_transitions_per_sec"] = cpuData.cStates.c1TransitionsPerSec;
    cStateInfo["c2_transitions_per_sec"] = cpuData.cStates.c2TransitionsPerSec;
    cStateInfo["c3_transitions_per_sec"] = cpuData.cStates.c3TransitionsPerSec;
    cStateInfo["cstates_enabled"] = cpuData.cStates.cStatesEnabled;
    cStateInfo["total_idle_time"] = cpuData.cStates.totalIdleTime;
    cStateInfo["power_efficiency_score"] = cpuData.cStates.powerEfficiencyScore;

    // Add interpretation of the power efficiency score
    QString efficiencyLevel;
    if (cpuData.cStates.powerEfficiencyScore >= 80) {
      efficiencyLevel = "excellent";
    } else if (cpuData.cStates.powerEfficiencyScore >= 60) {
      efficiencyLevel = "good";
    } else if (cpuData.cStates.powerEfficiencyScore >= 40) {
      efficiencyLevel = "adequate";
    } else if (cpuData.cStates.powerEfficiencyScore > 0) {
      efficiencyLevel = "poor";
    } else {
      efficiencyLevel = "unknown";
    }
    cStateInfo["efficiency_level"] = efficiencyLevel;

    cpuInfo["power_states"] = cStateInfo;
  }

  cpu["info"] = cpuInfo;

  // CPU performance results
  QJsonObject cpuResults;
  cpuResults["simd_scalar"] = cpuData.simdScalar;
  cpuResults["avx"] = cpuData.simdAvx;
  cpuResults["prime_time"] = cpuData.primeTime;
  cpuResults["single_core"] = cpuData.singleCoreTime;
  cpuResults["four_thread"] = cpuData.fourThreadTime;
  cpuResults["multi_core"] =
    cpuData.fourThreadTime > 0 ? cpuData.fourThreadTime : -1.0;
  cpuResults["game_sim_small"] = cpuData.gameSimUPS_small;
  cpuResults["game_sim_medium"] = cpuData.gameSimUPS_medium;
  cpuResults["game_sim_large"] = cpuData.gameSimUPS_large;

  // Remove the duplicate cache latencies array and keep only the raw
  // measurements for more detailed and accurate analysis
  QJsonArray rawLatencies;
  for (const auto& pair : cpuData.cache.rawLatencies) {
    QJsonObject rawLevel;
    rawLevel["size_kb"] = static_cast<int>(pair.first);
    rawLevel["latency"] = pair.second;
    rawLatencies.append(rawLevel);
  }

  // Add raw measurements as the primary cache latency data
  if (!rawLatencies.isEmpty()) {
    cpuResults["raw_cache_latencies"] = rawLatencies;
  }

  // In the CPU cache section:
  QJsonObject specificLatencies;
  if (cpuData.cache.l1LatencyNs > 0)
    specificLatencies["l1_ns"] = cpuData.cache.l1LatencyNs;
  if (cpuData.cache.l2LatencyNs > 0)
    specificLatencies["l2_ns"] = cpuData.cache.l2LatencyNs;
  if (cpuData.cache.l3LatencyNs > 0)
    specificLatencies["l3_ns"] = cpuData.cache.l3LatencyNs;
  if (cpuData.cache.ramLatencyNs > 0)
    specificLatencies["ram_ns"] = cpuData.cache.ramLatencyNs;

  if (!specificLatencies.isEmpty()) {
    cpuResults["specific_cache_latencies"] = specificLatencies;
  }

  cpu["results"] = cpuResults;
  results["cpu"] = cpu;

  // Memory section
  QJsonObject memory;

  // Basic memory info
  QJsonObject memInfo;
  memInfo["total_memory_gb"] =
    static_cast<double>(constantInfo.totalPhysicalMemoryMB) / 1024.0;
  
  // Get available memory from system API
  MEMORYSTATUSEX memStatus;
  memStatus.dwLength = sizeof(MEMORYSTATUSEX);
  if (GlobalMemoryStatusEx(&memStatus)) {
    memInfo["available_memory_gb"] =
      static_cast<double>(memStatus.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
  }

  // Get memory info from constant info
  memInfo["type"] = QString::fromStdString(constantInfo.memoryType);

  if (constantInfo.memoryClockMHz > 0) {
    memInfo["clock_speed_mhz"] = constantInfo.memoryClockMHz;
  }

  // Get channel config
  if (!constantInfo.memoryChannelConfig.empty()) {
    memInfo["channel_status"] =
      QString::fromStdString(constantInfo.memoryChannelConfig);
  } else if (!memoryData.channelStatus.empty()) {
    memInfo["channel_status"] =
      QString::fromStdString(memoryData.channelStatus);
  }

  // Add XMP status
  memInfo["xmp_enabled"] = constantInfo.xmpEnabled || memoryData.xmpEnabled;

  // Use memory modules from DiagnosticDataStore
  QJsonArray memoryModulesJson;
  for (const auto& module : memoryData.modules) {
    QJsonObject moduleJson;
    moduleJson["slot"] = module.slot;
    moduleJson["speed_mhz"] = module.speedMHz;
    moduleJson["configured_clock_speed_mhz"] = module.configuredSpeedMHz;
    moduleJson["capacity_gb"] = module.capacityGB;
    moduleJson["manufacturer"] = QString::fromStdString(module.manufacturer);
    moduleJson["part_number"] = QString::fromStdString(module.partNumber);
    moduleJson["memory_type"] = QString::fromStdString(module.memoryType);
    moduleJson["device_locator"] = QString::fromStdString(module.deviceLocator);
    moduleJson["xmp_status"] = QString::fromStdString(module.xmpStatus);

    memoryModulesJson.append(moduleJson);
  }

  // If no modules in DiagnosticDataStore, use modules from ConstantSystemInfo
  if (memoryModulesJson.isEmpty() && !constantInfo.memoryModules.empty()) {
    for (const auto& module : constantInfo.memoryModules) {
      QJsonObject moduleJson;
      moduleJson["capacity_gb"] = module.capacityGB;
      moduleJson["speed_mhz"] = module.speedMHz;
      moduleJson["configured_clock_speed_mhz"] = module.configuredSpeedMHz;
      moduleJson["manufacturer"] = QString::fromStdString(module.manufacturer);
      moduleJson["part_number"] = QString::fromStdString(module.partNumber);
      moduleJson["memory_type"] = QString::fromStdString(module.memoryType);
      moduleJson["device_locator"] =
        QString::fromStdString(module.deviceLocator);
      moduleJson["form_factor"] = QString::fromStdString(module.formFactor);
      moduleJson["bank_label"] = QString::fromStdString(module.bankLabel);

      memoryModulesJson.append(moduleJson);
    }
  }

  if (!memoryModulesJson.isEmpty()) {
    memInfo["modules"] = memoryModulesJson;
  }

  // Add page file information
  if (constantInfo.pageFileExists) {
    QJsonObject pageFileJson;
    pageFileJson["exists"] = constantInfo.pageFileExists;
    pageFileJson["system_managed"] = constantInfo.pageFileSystemManaged;
    pageFileJson["total_size_mb"] = constantInfo.pageTotalSizeMB;
    pageFileJson["primary_drive"] =
      QString::fromStdString(constantInfo.pagePrimaryDriveLetter);

    // Add locations array with more details
    QJsonArray locationsArray;
    for (size_t i = 0; i < constantInfo.pageFileLocations.size(); i++) {
      QJsonObject locationObj;
      locationObj["path"] =
        QString::fromStdString(constantInfo.pageFileLocations[i]);

      // Add current and max sizes if available
      if (i < constantInfo.pageFileCurrentSizesMB.size() &&
          i < constantInfo.pageFileMaxSizesMB.size()) {
        locationObj["current_size_mb"] = constantInfo.pageFileCurrentSizesMB[i];

        if (constantInfo.pageFileMaxSizesMB[i] > 0) {
          locationObj["max_size_mb"] = constantInfo.pageFileMaxSizesMB[i];
        }
      }

      locationsArray.append(locationObj);
    }

    if (!locationsArray.isEmpty()) {
      pageFileJson["locations"] = locationsArray;
    }

    memInfo["page_file"] = pageFileJson;
  } else if (memoryData.pageFile.exists) {
    // Fall back to diagnostic data if available
    QJsonObject pageFileJson;
    pageFileJson["exists"] = memoryData.pageFile.exists;
    pageFileJson["system_managed"] = memoryData.pageFile.systemManaged;
    pageFileJson["total_size_mb"] = memoryData.pageFile.totalSizeMB;

    // Simplify by only storing drive letters in an array
    QJsonArray driveLetters;
    for (const auto& location : memoryData.pageFile.locations) {
      driveLetters.append(QString::fromStdString(location.drive));
    }

    if (!driveLetters.isEmpty()) {
      pageFileJson["drive_letters"] = driveLetters;
      // Keep primary drive as it's useful reference data
      pageFileJson["primary_drive"] =
        QString::fromStdString(memoryData.pageFile.primaryDrive);
    }

    memInfo["page_file"] = pageFileJson;
  }

  memory["info"] = memInfo;

  // Memory results
  QJsonObject memResults;
  memResults["bandwidth"] = memoryData.bandwidth;
  memResults["latency"] = memoryData.latency;
  memResults["write_time"] = memoryData.writeTime;
  memResults["read_time"] = memoryData.readTime;

  // Add memory stability test results
  QJsonObject stabilityTest;
  stabilityTest["test_performed"] = memoryData.stabilityTest.testPerformed;
  stabilityTest["passed"] = memoryData.stabilityTest.passed;
  stabilityTest["error_count"] = memoryData.stabilityTest.errorCount;
  stabilityTest["completed_loops"] = memoryData.stabilityTest.completedLoops;
  stabilityTest["completed_patterns"] =
    memoryData.stabilityTest.completedPatterns;
  stabilityTest["tested_size_mb"] =
    static_cast<int>(memoryData.stabilityTest.testedSizeMB);
  memResults["stability_test"] = stabilityTest;

  memory["results"] = memResults;
  results["memory"] = memory;

  // GPU Section
  QJsonObject gpu;
  gpu["tested"] = !skipGpuTests;

  if (!skipGpuTests) {
    QJsonObject gpuInfo;

    // Try to get GPU info from DiagnosticDataStore first, then fall back to
    // constant info
    if (!gpuData.name.empty() && gpuData.name != "no_data") {
      gpuInfo["model"] = QString::fromStdString(gpuData.name);
    } else if (!constantInfo.gpuDevices.empty()) {
      gpuInfo["model"] =
        QString::fromStdString(constantInfo.gpuDevices[0].name);
      gpuInfo["memory_mb"] = constantInfo.gpuDevices[0].memoryMB;
    }

    // Add driver version
    if (!gpuData.driverVersion.empty() && gpuData.driverVersion != "no_data") {
      gpuInfo["driver"] = QString::fromStdString(gpuData.driverVersion);
    } else if (!constantInfo.gpuDevices.empty()) {
      gpuInfo["driver"] =
        QString::fromStdString(constantInfo.gpuDevices[0].driverVersion);
    }

    // Add detailed GPU info from constantInfo for all GPUs
    QJsonArray gpuDevicesArray;
    for (const auto& gpuDevice : constantInfo.gpuDevices) {
      QJsonObject deviceInfo;
      deviceInfo["name"] = QString::fromStdString(gpuDevice.name);
      deviceInfo["device_id"] = QString::fromStdString(gpuDevice.deviceId);
      deviceInfo["memory_mb"] = gpuDevice.memoryMB;
      deviceInfo["driver_version"] =
        QString::fromStdString(gpuDevice.driverVersion);
      deviceInfo["driver_date"] = QString::fromStdString(gpuDevice.driverDate);
      deviceInfo["has_geforce_experience"] = gpuDevice.hasGeForceExperience;
      deviceInfo["vendor"] = QString::fromStdString(gpuDevice.vendor);
      deviceInfo["pci_link_width"] = gpuDevice.pciLinkWidth;
      deviceInfo["pcie_link_gen"] = gpuDevice.pcieLinkGen;
      deviceInfo["is_primary"] = gpuDevice.isPrimary;

      gpuDevicesArray.append(deviceInfo);
    }

    if (!gpuDevicesArray.isEmpty()) {
      gpuInfo["devices"] = gpuDevicesArray;
    }

    gpu["info"] = gpuInfo;

    QJsonObject gpuResults;
    gpuResults["fps"] = gpuData.averageFPS;
    gpuResults["frames"] = gpuData.totalFrames;
    gpuResults["render_time_ms"] =
      gpuData
        .renderTimeMs;  // Always include, -1 is the default for unavailable

    gpu["results"] = gpuResults;
  } else {
    // Even if we skip testing, include the GPU device information
    QJsonObject gpuInfo;
    QJsonArray gpuDevicesArray;

    for (const auto& gpuDevice : constantInfo.gpuDevices) {
      QJsonObject deviceInfo;
      deviceInfo["name"] = QString::fromStdString(gpuDevice.name);
      deviceInfo["device_id"] = QString::fromStdString(gpuDevice.deviceId);
      deviceInfo["memory_mb"] = gpuDevice.memoryMB;
      deviceInfo["driver_version"] =
        QString::fromStdString(gpuDevice.driverVersion);
      deviceInfo["driver_date"] = QString::fromStdString(gpuDevice.driverDate);
      deviceInfo["has_geforce_experience"] = gpuDevice.hasGeForceExperience;
      deviceInfo["vendor"] = QString::fromStdString(gpuDevice.vendor);
      deviceInfo["pci_link_width"] = gpuDevice.pciLinkWidth;
      deviceInfo["pcie_link_gen"] = gpuDevice.pcieLinkGen;
      deviceInfo["is_primary"] = gpuDevice.isPrimary;

      gpuDevicesArray.append(deviceInfo);
    }

    if (!gpuDevicesArray.isEmpty()) {
      gpuInfo["devices"] = gpuDevicesArray;
    }

    gpu["info"] = gpuInfo;
    gpu["results"] = QJsonObject();
  }
  results["gpu"] = gpu;

  // Drive Section
  QJsonObject drives;
  drives["tested"] = !skipDriveTests;

  if (!skipDriveTests) {
    QJsonArray driveItems;
    for (const auto& drive : driveData.drives) {
      QJsonObject driveItem;

      QJsonObject driveInfo;
      driveInfo["path"] = QString::fromStdString(drive.drivePath);

      // Try to match with drive info from constant info - more robust path
      // comparison
      bool matched = false;
      for (const auto& constDrive : constantInfo.drives) {
        // Normalize paths for comparison (remove trailing backslash if present)
        std::string normalizedTestPath = drive.drivePath;
        std::string normalizedConstPath = constDrive.path;

        // Remove trailing backslash if present
        if (!normalizedTestPath.empty() && normalizedTestPath.back() == '\\') {
          normalizedTestPath.pop_back();
        }
        if (!normalizedConstPath.empty() &&
            normalizedConstPath.back() == '\\') {
          normalizedConstPath.pop_back();
        }

        // Compare drive letter only (for Windows paths like "C:" vs "C:\")
        if ((normalizedTestPath.size() >= 2 &&
             normalizedConstPath.size() >= 2) &&
            (normalizedTestPath[0] == normalizedConstPath[0] &&
             normalizedTestPath[1] == ':' && normalizedConstPath[1] == ':')) {

          matched = true;
          driveInfo["model"] = QString::fromStdString(constDrive.model);
          driveInfo["size_gb"] = static_cast<int>(constDrive.totalSpaceGB);
          driveInfo["free_space_gb"] = static_cast<int>(constDrive.freeSpaceGB);
          driveInfo["is_ssd"] = constDrive.isSSD;
          driveInfo["is_system_drive"] = constDrive.isSystemDrive;
          driveInfo["interface_type"] = QString::fromStdString(constDrive.interfaceType);
          // driveInfo["serial_number"] = QString::fromStdString(constDrive.serialNumber); // Removed for privacy
          break;
        }
      }

      // If we didn't find a match but have constant drive data, include disk
      // info for this drive letter
      if (!matched && !constantInfo.drives.empty()) {
        // Extract drive letter from path (like "C:" from "C:\" or "C:\path")
        std::string driveLetter;
        if (drive.drivePath.size() >= 2 && drive.drivePath[1] == ':') {
          driveLetter = drive.drivePath.substr(0, 2);

          // Look for a drive with matching drive letter
          for (const auto& constDrive : constantInfo.drives) {
            if (constDrive.path.size() >= 2 &&
                constDrive.path[0] == drive.drivePath[0] &&
                constDrive.path[1] == ':') {

              driveInfo["model"] = QString::fromStdString(constDrive.model);
              driveInfo["size_gb"] = static_cast<int>(constDrive.totalSpaceGB);
              driveInfo["free_space_gb"] =
                static_cast<int>(constDrive.freeSpaceGB);
              driveInfo["is_ssd"] = constDrive.isSSD;
              driveInfo["is_system_drive"] = constDrive.isSystemDrive;
              driveInfo["interface_type"] =
                QString::fromStdString(constDrive.interfaceType);
              driveInfo["serial_number"] =
                QString::fromStdString(constDrive.serialNumber);
              break;
            }
          }
        }
      }

      driveItem["info"] = driveInfo;

      QJsonObject driveResults;
      driveResults["read_speed"] = drive.seqRead;
      driveResults["write_speed"] = drive.seqWrite;
      driveResults["iops_4k"] = drive.iops4k;
      driveResults["access_time"] = drive.accessTimeMs;
      driveItem["results"] = driveResults;

      driveItems.append(driveItem);
    }
    drives["items"] = driveItems;

    // Also include all drives from constantInfo even if not tested
    // to provide complete system drive information
    if (driveItems.isEmpty()) {
      for (const auto& constDrive : constantInfo.drives) {
        QJsonObject driveItem;
        QJsonObject driveInfo;

  driveInfo["path"] = QString::fromStdString(constDrive.path);
  driveInfo["model"] = QString::fromStdString(constDrive.model);
  driveInfo["size_gb"] = static_cast<int>(constDrive.totalSpaceGB);
  driveInfo["free_space_gb"] = static_cast<int>(constDrive.freeSpaceGB);
  driveInfo["is_ssd"] = constDrive.isSSD;
  driveInfo["is_system_drive"] = constDrive.isSystemDrive;
  driveInfo["interface_type"] = QString::fromStdString(constDrive.interfaceType);
  // driveInfo["serial_number"] = QString::fromStdString(constDrive.serialNumber); // Removed for privacy

        driveItem["info"] = driveInfo;
        driveItem["results"] = QJsonObject();  // No test results

        driveItems.append(driveItem);
      }
      drives["items"] = driveItems;
    }
  } else {
    // Even if we skipped drive tests, include drive information from
    // constantInfo
    QJsonArray driveItems;
    for (const auto& constDrive : constantInfo.drives) {
      QJsonObject driveItem;
      QJsonObject driveInfo;

  driveInfo["path"] = QString::fromStdString(constDrive.path);
  driveInfo["model"] = QString::fromStdString(constDrive.model);
  driveInfo["size_gb"] = static_cast<int>(constDrive.totalSpaceGB);
  driveInfo["free_space_gb"] = static_cast<int>(constDrive.freeSpaceGB);
  driveInfo["is_ssd"] = constDrive.isSSD;
  driveInfo["is_system_drive"] = constDrive.isSystemDrive;
  driveInfo["interface_type"] = QString::fromStdString(constDrive.interfaceType);
  // driveInfo["serial_number"] = QString::fromStdString(constDrive.serialNumber); // Removed for privacy

      driveItem["info"] = driveInfo;
      driveItem["results"] = QJsonObject();  // No test results

      driveItems.append(driveItem);
    }
    drives["items"] = driveItems;
  }
  results["drives"] = drives;

  // Network section
  QJsonObject network;
  network["tested"] = !skipNetworkTests;

  if (!skipNetworkTests) {
    QJsonObject networkInfo;
    networkInfo["wifi"] = networkData.onWifi;

    QJsonObject networkResults;
    networkResults["average_latency_ms"] = networkData.averageLatencyMs;
    networkResults["average_jitter_ms"] = networkData.averageJitterMs;
    networkResults["packet_loss_percent"] = networkData.averagePacketLoss;
    networkResults["baseline_latency_ms"] = networkData.baselineLatencyMs;
    networkResults["download_latency_ms"] = networkData.downloadLatencyMs;
    networkResults["upload_latency_ms"] = networkData.uploadLatencyMs;
    networkResults["has_bufferbloat"] = networkData.hasBufferbloat;

    if (!networkData.networkIssues.empty()) {
      networkResults["issues"] =
        QString::fromStdString(networkData.networkIssues);
    }

    // Add regional latency data if available
    if (!networkData.regionalLatencies.empty()) {
      QJsonArray regionalData;
      for (const auto& region : networkData.regionalLatencies) {
        QJsonObject regionObj;
        regionObj["region"] = QString::fromStdString(region.region);
        regionObj["latency_ms"] = region.latencyMs;
        regionalData.append(regionObj);
      }
      networkResults["regional_latencies"] = regionalData;
    }

    // Add detailed server results if available
    if (!networkData.serverResults.empty()) {
      QJsonArray serverData;
      for (const auto& server : networkData.serverResults) {
        QJsonObject serverObj;
        serverObj["hostname"] = QString::fromStdString(server.hostname);
        serverObj["ip_address"] = QString::fromStdString(server.ipAddress);
        serverObj["region"] = QString::fromStdString(server.region);
        serverObj["min_latency_ms"] = server.minLatencyMs;
        serverObj["max_latency_ms"] = server.maxLatencyMs;
        serverObj["avg_latency_ms"] = server.avgLatencyMs;
        serverObj["jitter_ms"] = server.jitterMs;
        serverObj["packet_loss_percent"] = server.packetLossPercent;
        serverObj["sent_packets"] = server.sentPackets;
        serverObj["received_packets"] = server.receivedPackets;
        serverData.append(serverObj);
      }
      networkResults["server_results"] = serverData;
    }

    network["results"] = networkResults;
  }
  results["network"] = network;

  // System info section
  QJsonObject system;
  QJsonObject sysInfo;

  // Motherboard information
  QJsonObject motherboardInfo;
  motherboardInfo["manufacturer"] =
    QString::fromStdString(constantInfo.motherboardManufacturer);
  motherboardInfo["model"] =
    QString::fromStdString(constantInfo.motherboardModel);
  motherboardInfo["chipset"] =
    QString::fromStdString(constantInfo.chipsetModel);
  motherboardInfo["chipset_driver"] =
    QString::fromStdString(constantInfo.chipsetDriverVersion);
  sysInfo["motherboard"] = motherboardInfo;

  // BIOS information
  QJsonObject biosInfo;
  biosInfo["version"] = QString::fromStdString(constantInfo.biosVersion);
  biosInfo["date"] = QString::fromStdString(constantInfo.biosDate);
  biosInfo["manufacturer"] =
    QString::fromStdString(constantInfo.biosManufacturer);
  sysInfo["bios"] = biosInfo;

  // OS information
  QJsonObject osInfo;
  osInfo["version"] = QString::fromStdString(constantInfo.osVersion);
  osInfo["build"] = QString::fromStdString(constantInfo.osBuildNumber);
  osInfo["is_windows11"] = constantInfo.isWindows11;
  // osInfo["system_name"] = QString::fromStdString(constantInfo.systemName); // Removed for privacy
  sysInfo["os"] = osInfo;

  // Power settings
  QJsonObject powerInfo;
  powerInfo["plan"] = QString::fromStdString(constantInfo.powerPlan);
  powerInfo["high_performance"] = constantInfo.powerPlanHighPerf;
  powerInfo["game_mode"] = constantInfo.gameMode;
  sysInfo["power"] = powerInfo;

  // Hardware virtualization
  sysInfo["virtualization"] = constantInfo.virtualizationEnabled;

  // Add monitor information
  QJsonArray monitorsArray;
  for (const auto& monitor : constantInfo.monitors) {
    QJsonObject monitorObj;
    monitorObj["device_name"] = QString::fromStdString(monitor.deviceName);
    monitorObj["display_name"] = QString::fromStdString(monitor.displayName);
    monitorObj["width"] = monitor.width;
    monitorObj["height"] = monitor.height;
    monitorObj["refresh_rate"] = monitor.refreshRate;
    monitorObj["is_primary"] = monitor.isPrimary;
    monitorsArray.append(monitorObj);
  }

  if (!monitorsArray.isEmpty()) {
    sysInfo["monitors"] = monitorsArray;
  }

  // Add driver information
  // Chipset drivers
  QJsonArray chipsetDriversArray;
  for (const auto& driver : constantInfo.chipsetDrivers) {
    QJsonObject driverObj;
    driverObj["device_name"] = QString::fromStdString(driver.deviceName);
    driverObj["driver_version"] = QString::fromStdString(driver.driverVersion);
    driverObj["driver_date"] = QString::fromStdString(driver.driverDate);
    driverObj["provider_name"] = QString::fromStdString(driver.providerName);
    driverObj["is_date_valid"] = driver.isDateValid;
    chipsetDriversArray.append(driverObj);
  }

  if (!chipsetDriversArray.isEmpty()) {
    sysInfo["chipset_drivers"] = chipsetDriversArray;
  }

  // Audio drivers
  QJsonArray audioDriversArray;
  for (const auto& driver : constantInfo.audioDrivers) {
    QJsonObject driverObj;
    driverObj["device_name"] = QString::fromStdString(driver.deviceName);
    driverObj["driver_version"] = QString::fromStdString(driver.driverVersion);
    driverObj["driver_date"] = QString::fromStdString(driver.driverDate);
    driverObj["provider_name"] = QString::fromStdString(driver.providerName);
    driverObj["is_date_valid"] = driver.isDateValid;
    audioDriversArray.append(driverObj);
  }

  if (!audioDriversArray.isEmpty()) {
    sysInfo["audio_drivers"] = audioDriversArray;
  }

  // Network drivers
  QJsonArray networkDriversArray;
  for (const auto& driver : constantInfo.networkDrivers) {
    QJsonObject driverObj;
    driverObj["device_name"] = QString::fromStdString(driver.deviceName);
    driverObj["driver_version"] = QString::fromStdString(driver.driverVersion);
    driverObj["driver_date"] = QString::fromStdString(driver.driverDate);
    driverObj["provider_name"] = QString::fromStdString(driver.providerName);
    driverObj["is_date_valid"] = driver.isDateValid;
    networkDriversArray.append(driverObj);
  }

  if (!networkDriversArray.isEmpty()) {
    sysInfo["network_drivers"] = networkDriversArray;
  }

  // Add background processes info from DiagnosticDataStore
  QJsonObject backgroundInfo;

  // Determine if there are high CPU/GPU/memory processes
  bool hasHighCpuProcesses = false;
  bool hasHighGpuProcesses = false;
  bool hasHighMemoryProcesses = false;

  for (const auto& proc : backgroundData.topCpuProcesses) {
    if (proc.cpuPercent > 10.0) {
      hasHighCpuProcesses = true;
      break;
    }
  }

  for (const auto& proc : backgroundData.topGpuProcesses) {
    if (proc.gpuPercent > 5.0) {
      hasHighGpuProcesses = true;
      break;
    }
  }

  for (const auto& proc : backgroundData.topMemoryProcesses) {
    if (proc.memoryUsageKB > 500 * 1024) {  // 500 MB
      hasHighMemoryProcesses = true;
      break;
    }
  }

  backgroundInfo["has_high_cpu_processes"] = hasHighCpuProcesses;
  backgroundInfo["has_high_gpu_processes"] = hasHighGpuProcesses;
  backgroundInfo["has_high_memory_processes"] = hasHighMemoryProcesses;
  backgroundInfo["has_dpc_latency_issues"] = backgroundData.hasDpcLatencyIssues;
  backgroundInfo["total_cpu_usage"] = backgroundData.systemCpuUsage;
  backgroundInfo["total_gpu_usage"] =
    backgroundData.systemGpuUsage;  // Use actual value instead of -1.0
  backgroundInfo["system_dpc_time"] = backgroundData.systemDpcTime;
  backgroundInfo["system_interrupt_time"] = backgroundData.systemInterruptTime;

  // Add the detailed memory metrics from background process monitoring
  if (backgroundData.physicalTotalKB > 0) {
    QJsonObject memoryMetrics;

    // Convert KB values to MB for better readability in the JSON
    double physicalTotalMB = backgroundData.physicalTotalKB / 1024.0;
    double physicalAvailableMB = backgroundData.physicalAvailableKB / 1024.0;
    double physicalUsedMB = physicalTotalMB - physicalAvailableMB;
    double physicalUsedPercent = (physicalUsedMB / physicalTotalMB) * 100.0;

    // RAM usage
    memoryMetrics["physical_total_mb"] = physicalTotalMB;
    memoryMetrics["physical_available_mb"] = physicalAvailableMB;
    memoryMetrics["physical_used_mb"] = physicalUsedMB;
    memoryMetrics["physical_used_percent"] = physicalUsedPercent;

    // Committed memory
    if (backgroundData.commitTotalKB > 0 && backgroundData.commitLimitKB > 0) {
      double commitTotalMB = backgroundData.commitTotalKB / 1024.0;
      double commitLimitMB = backgroundData.commitLimitKB / 1024.0;
      double commitPercent = (commitTotalMB / commitLimitMB) * 100.0;

      memoryMetrics["commit_total_mb"] = commitTotalMB;
      memoryMetrics["commit_limit_mb"] = commitLimitMB;
      memoryMetrics["commit_percent"] = commitPercent;
    }

    // Kernel memory
    if (backgroundData.kernelPagedKB > 0 ||
        backgroundData.kernelNonPagedKB > 0) {
      double kernelPagedMB = backgroundData.kernelPagedKB / 1024.0;
      double kernelNonPagedMB = backgroundData.kernelNonPagedKB / 1024.0;
      double kernelTotalMB = kernelPagedMB + kernelNonPagedMB;

      memoryMetrics["kernel_paged_mb"] = kernelPagedMB;
      memoryMetrics["kernel_nonpaged_mb"] = kernelNonPagedMB;
      memoryMetrics["kernel_total_mb"] = kernelTotalMB;
    }

    // File cache
    if (backgroundData.systemCacheKB > 0) {
      memoryMetrics["file_cache_mb"] = backgroundData.systemCacheKB / 1024.0;
    }

    // User mode private + other memory
    if (backgroundData.userModePrivateKB > 0) {
      memoryMetrics["user_mode_private_mb"] =
        backgroundData.userModePrivateKB / 1024.0;
    }

    if (backgroundData.otherMemoryKB > 0) {
      memoryMetrics["other_memory_mb"] = backgroundData.otherMemoryKB / 1024.0;
    }

    backgroundInfo["memory_metrics"] = memoryMetrics;
  }

  // Add a summary section for quick reference
  QJsonObject summary;
  bool hasIssues = hasHighCpuProcesses || hasHighGpuProcesses ||
                   hasHighMemoryProcesses || backgroundData.hasDpcLatencyIssues;
  summary["has_background_issues"] = hasIssues;
  summary["high_interrupt_activity"] =
    backgroundData.systemInterruptTime > 0.5 ||
    backgroundData.systemDpcTime > 1.0;
  summary["overall_impact"] = hasIssues ? "significant" : "minimal";
  backgroundInfo["summary"] = summary;

  // Instead of detailed process objects, just create separate arrays for each
  // metric
  QJsonArray cpuPercentages;
  QJsonArray memoryUsages;
  QJsonArray gpuPercentages;

  // Fill the CPU percentages array
  for (const auto& proc : backgroundData.topCpuProcesses) {
    if (proc.cpuPercent > 0.1) {  // Only include non-trivial values
      cpuPercentages.append(proc.cpuPercent);
    }
  }

  // Fill the memory usages array (convert to MB)
  for (const auto& proc : backgroundData.topMemoryProcesses) {
    if (proc.memoryUsageKB > 10 * 1024) {  // Only include >10MB processes
      memoryUsages.append(proc.memoryUsageKB / 1024.0);
    }
  }

  // Fill the GPU percentages array
  for (const auto& proc : backgroundData.topGpuProcesses) {
    if (proc.gpuPercent > 0.1) {  // Only include non-trivial values
      gpuPercentages.append(proc.gpuPercent);
    }
  }

  // Add arrays to the background info
  backgroundInfo["cpu_percentages"] = cpuPercentages;
  backgroundInfo["memory_usages_mb"] = memoryUsages;
  backgroundInfo["gpu_percentages"] = gpuPercentages;

  // Add some basic statistical info that would be useful for analysis
  if (!cpuPercentages.isEmpty()) {
    // Calculate max CPU usage
    double maxCpu = 0;
    for (const auto& val : backgroundData.topCpuProcesses) {
      maxCpu = std::max(maxCpu, val.cpuPercent);
    }
    backgroundInfo["max_process_cpu"] = maxCpu;
  }

  if (!memoryUsages.isEmpty()) {
    // Calculate max memory usage
    double maxMemMB = 0;
    for (const auto& val : backgroundData.topMemoryProcesses) {
      maxMemMB = std::max(maxMemMB, val.memoryUsageKB / 1024.0);
    }
    backgroundInfo["max_process_memory_mb"] = maxMemMB;
  }

  sysInfo["background"] = backgroundInfo;

  // Kernel memory tracking removed - using ConstantSystemInfo for static data
  // Dynamic memory metrics would use PdhInterface if needed
  QJsonObject kernelMemory;
  kernelMemory["note"] = "Kernel memory tracking removed - using ConstantSystemInfo for static memory data";

  sysInfo["kernel_memory"] = kernelMemory;

  system["info"] = sysInfo;
  results["system"] = system;

  // Add metadata with system identification
  QJsonObject metadata;
  metadata["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  metadata["version"] = "1.0";
  metadata["run_as_admin"] = isRunningAsAdmin();

  // Get user system profile information
  auto& userProfile = SystemMetrics::UserSystemProfile::getInstance();

  // Initialize the profile if needed
  if (!userProfile.isInitialized()) {
    userProfile.initialize();
  }

  // Add user system identifiers to metadata (personal info removed for privacy)
  metadata["user_id"] = QString::fromStdString(userProfile.getUserId());
  metadata["profile_last_updated"] =
    QString::fromStdString(userProfile.getLastUpdateTimestamp());

  // Add test settings used for this diagnostic run
  QJsonObject testSettings;
  testSettings["drive_test_mode"] = driveTestMode;  // 0=None, 1=SystemOnly, 2=AllDrives
  testSettings["network_test_mode"] = networkTestMode;  // 0=None, 1=Basic, 2=Extended
  testSettings["cpu_throttling_test_mode"] = cpuThrottlingTestMode;  // 0=None, 1=Basic, 2=Extended
  testSettings["run_gpu_tests"] = !skipGpuTests;
  testSettings["run_cpu_boost_tests"] = runCpuBoostTests;
  testSettings["run_memory_tests"] = runMemoryTests;
  testSettings["run_background_tests"] = runBackgroundTests;
  testSettings["developer_mode"] = developerMode;
  testSettings["run_storage_analysis"] = runStorageAnalysis;
  testSettings["use_recommended_settings"] = useRecommendedSettings;
  metadata["test_settings"] = testSettings;

  // Personal information removed: system_hash, combined_identifier, system_id
  results["metadata"] = metadata;

  return results;
}

QJsonArray DiagnosticWorker::loadPreviousResults() const {
  QJsonArray previousResults;
  QString resultsPath = getComparisonFolder();
  QDir dir(resultsPath);

  if (!dir.exists()) {
    log("No previous results found");
    return previousResults;
  }

  QStringList filters;
  filters << "diagnostics_*.json";
  QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);

  // Load last 5 results (or fewer if less exist)
  for (int i = 0; i < qMin(5, files.size()); i++) {
    QFile file(files[i].absoluteFilePath());
    if (file.open(QIODevice::ReadOnly)) {
      QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
      if (!doc.isNull() && doc.isObject()) {
        previousResults.append(doc.object());
      }
    }
  }

  return previousResults;
}

QString DiagnosticWorker::generateResultsFilename() const {
  return QString("diagnostics_%1.json").arg(getRunTokenForOutput());
}

QString DiagnosticWorker::getRunTokenForOutput() const {
  if (!m_currentRunToken.isEmpty()) return m_currentRunToken;
  // Fallback for legacy flows; ensures file creation still succeeds
  return QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
}

void DiagnosticWorker::saveTestResults() {
  // Create path to diagnostic_results folder in application directory
  QString appDir = QCoreApplication::applicationDirPath();
  QString resultsDir = appDir + "/diagnostic_results";

  // Create the directory if it doesn't exist
  QDir().mkpath(resultsDir);

  // Generate filename
  QString filename = resultsDir + "/" + generateResultsFilename();

  // Save the file
  QJsonDocument doc = QJsonDocument(resultsToJson());
  QFile file(filename);
  if (!file.open(QIODevice::WriteOnly)) {
    log("Error: Could not save results file to " + filename);
    return;
  }

  file.write(doc.toJson(QJsonDocument::Indented));
  file.close();

  log("Results saved to " + filename);
}

// Modify the runNetworkTest method to handle extended network tests
void DiagnosticWorker::runNetworkTest() {
  emit testStarted("Network Test");
  log("Running network diagnostic tests...");

  try {
    // Allow for variable test length configuration
    int pingCount = extendedNetworkTests ? 30 : 15;
    int bufferbloatDuration = extendedNetworkTests ? 10 : 5;

    log(QString("Testing network with %1 pings per target and %2 seconds "
                "bufferbloat test...")
          .arg(pingCount)
          .arg(bufferbloatDuration));

    // Initialize network test
    emit testStarted("Network Test: Detecting Connection Type");

    // Detection phase
    emit testStarted("Network Test: Testing Basic Connectivity");

    // First phase: ping tests
    emit testStarted("Network Test: Running Latency Tests");

    // Use the interface function with adjusted parameters
    NetworkTest::NetworkTestResult result = NetworkTest::RunNetworkDiagnostics(
      pingCount,           // Number of pings
      800,                 // Timeout in ms
      true,                // Include bufferbloat test
      bufferbloatDuration  // Bufferbloat test duration in seconds
    );

    // Second phase: bufferbloat test (if enabled)
    emit testStarted("Network Test: Running Bufferbloat Test");

    // Bufferbloat test runs automatically as part of RunNetworkDiagnostics

    // Convert the result to QString
    QString networkResult = QString::fromStdString(result.formattedOutput);

    // Finalizing phase
    emit testStarted("Network Test: Analyzing Results");

    // Log a summary of the results
    log("\n---- Network Test Results ----");
    log(QString("Connection Type: %1")
          .arg(result.isWiFi ? "WiFi" : "Wired Ethernet"));

    // Log the full report details
    log("\nDetailed Network Report:");
    QStringList lines = networkResult.split('\n');
    for (const QString& line : lines) {
      // Only log non-empty lines
      if (!line.trimmed().isEmpty()) {
        log(line);
      }
    }
    log("----------------------------------\n");

    // Emit the signal with the formatted results
    emit networkTestCompleted(networkResult);
    log("Network diagnostics completed.");
  } catch (const std::exception& e) {
    log(QString("Network test failed: %1").arg(e.what()));
    emit networkTestCompleted("Network test failed: " + QString(e.what()));
  }
}

QString DiagnosticWorker::formatMemoryResultString(
  const DiagnosticDataStore::MemoryData& memData) const {
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
  result +=
    QString("Random Read Speed: %1 GB/s\n").arg(memData.readTime, 0, 'f', 2);
  result += QString("Random Write Speed: %1 GB/s\n\n")
              .arg(memData.writeTime, 0, 'f', 2);

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

void DiagnosticWorker::cancelPendingOperations() {
  // Handle the memory test future if it's still active
  if (memoryTestFuture.valid()) {
    try {
      // Check if it's ready with a short timeout
      auto status = memoryTestFuture.wait_for(std::chrono::milliseconds(100));
      if (status == std::future_status::timeout) {
        // It's still running, log this issue
        LOG_WARN << "Warning: Memory test is still running during cleanup - this may cause issues";
        // We can't directly cancel a std::future in C++11, but we can reset our
        // handle to it This is not ideal but prevents waiting on it in the
        // destructor
        memoryTestFuture =
          std::async(std::launch::deferred, []() -> void { /* do nothing */ });
      } else {
        // It's done, get the result to clear the future
        memoryTestFuture.get();
        LOG_DEBUG << "Previous memory test future successfully resolved";
      }
    } catch (const std::exception& e) {
      LOG_ERROR << "Exception during memory test cleanup: " << e.what();
      // Reset the future to prevent issues in the destructor
      memoryTestFuture =
        std::async(std::launch::deferred, []() -> void { /* do nothing */ });
    }
  }

  // Clean up GPU test resources if active
  if (activeGpuTest) {
    try {
      LOG_DEBUG << "Cleaning up active GPU test during cancellation";
      delete activeGpuTest;
    } catch (const std::exception& e) {
      LOG_ERROR << "Exception during GPU test cleanup in cancellation: " << e.what();
    }
    activeGpuTest = nullptr;
  }

  // Cancel network tests if running
  if (!skipNetworkTests) {
    NetworkTest::cancelNetworkTests();
  }

  // Stop PDH metrics collection
  stopPdhMetricsCollection();
}

void DiagnosticWorker::setRunNetworkTests(bool run) {
  runNetworkTests = run;
  skipNetworkTests =
    !run;  // Update the flag that's actually checked in runDiagnosticsInternal
}

// Add a field to DiagnosticWorker class

void DiagnosticWorker::startPdhMetricsCollection() {
  // Generate the CSV filename based on the shared run token
  QString appDir = QCoreApplication::applicationDirPath();
  QString resultsDir = appDir + "/diagnostic_results";
  QString csvFilename = resultsDir + "/pdh_metrics_" +
                        getRunTokenForOutput() + ".csv";

  // Create the directory if it doesn't exist
  QDir().mkpath(resultsDir);

  // Open the CSV file
  m_pdhMetricsFile.open(csvFilename.toStdString());
  if (!m_pdhMetricsFile.is_open()) {
    log("Error: Could not open PDH metrics CSV file: " + csvFilename);
    return;
  }

  // Create PDH interface for diagnostic run (same approach as BenchmarkManager)
  m_pdhInterface = PdhInterface::createOptimizedForBenchmarking(std::chrono::milliseconds(1000));
  if (!m_pdhInterface) {
    log("Warning: Failed to create optimized PDH interface, trying minimal interface");
    m_pdhInterface = PdhInterface::createMinimal(std::chrono::milliseconds(1000));
    if (!m_pdhInterface) {
      log("Error: Failed to create any PDH interface for diagnostic metrics collection");
      m_pdhMetricsFile.close();
      return;
    } else {
      log("Successfully created minimal PDH interface for diagnostics");
    }
  } else {
    log("Successfully created optimized PDH interface for diagnostics");
  }

  // Write comprehensive CSV header with ALL essential metrics
  m_pdhMetricsFile
    << "Timestamp,TestName,"
    // CPU Metrics - Total
    << "CPU_Total_Usage(%),CPU_User_Time(%),CPU_Privileged_Time(%),CPU_Idle_Time(%),"
    << "CPU_Actual_Frequency(MHz),CPU_Interrupts_Per_Sec,CPU_DPC_Time(%),CPU_Interrupt_Time(%),"
    << "CPU_DPCs_Queued_Per_Sec,CPU_DPC_Rate,CPU_C1_Time(%),CPU_C2_Time(%),CPU_C3_Time(%),"
    << "CPU_C1_Transitions_Per_Sec,CPU_C2_Transitions_Per_Sec,CPU_C3_Transitions_Per_Sec,"
    // CPU Metrics - Per Core (first 8 cores, using getPerCoreMetric)
    << "CPU_Core0_Usage(%),CPU_Core1_Usage(%),CPU_Core2_Usage(%),CPU_Core3_Usage(%),"
    << "CPU_Core4_Usage(%),CPU_Core5_Usage(%),CPU_Core6_Usage(%),CPU_Core7_Usage(%),"
    << "CPU_Core0_Freq(MHz),CPU_Core1_Freq(MHz),CPU_Core2_Freq(MHz),CPU_Core3_Freq(MHz),"
    << "CPU_Core4_Freq(MHz),CPU_Core5_Freq(MHz),CPU_Core6_Freq(MHz),CPU_Core7_Freq(MHz),"
    // Memory Metrics
    << "Memory_Available_MB,Memory_Committed_Bytes,Memory_Commit_Limit,Memory_Page_Faults_Per_Sec,"
    << "Memory_Pages_Per_Sec,Memory_Pool_Nonpaged_Bytes,Memory_Pool_Paged_Bytes,"
    << "Memory_System_Code_Bytes,Memory_System_Driver_Bytes,"
    // Disk Metrics - Physical
    << "Disk_Read_Bytes_Per_Sec,Disk_Write_Bytes_Per_Sec,Disk_Reads_Per_Sec,Disk_Writes_Per_Sec,"
    << "Disk_Transfers_Per_Sec,Disk_Bytes_Per_Sec,Disk_Avg_Read_Queue_Length,Disk_Avg_Write_Queue_Length,"
    << "Disk_Avg_Queue_Length,Disk_Avg_Read_Time(s),Disk_Avg_Write_Time(s),Disk_Avg_Transfer_Time(s),"
    << "Disk_Percent_Time(%),Disk_Percent_Read_Time(%),Disk_Percent_Write_Time(%),"
    // System Metrics
    << "System_Context_Switches_Per_Sec,System_System_Calls_Per_Sec,System_Processor_Queue_Length,"
    << "System_Processes,System_Threads\n";

  // Start PDH interface (like BenchmarkManager does)
  if (!m_pdhInterface->start()) {
    log("Error: Failed to start PDH interface for diagnostic metrics collection");
    m_pdhMetricsFile.close();
    m_pdhInterface.reset();
    return;
  }

  // Start the collection thread
  m_pdhMetricsRunning = true;
  m_pdhMetricsThread =
    std::thread(&DiagnosticWorker::pdhMetricsCollectionThread, this);

  log("Started comprehensive PDH metrics collection");
}

void DiagnosticWorker::pdhMetricsCollectionThread() {
  // PDH interface should already be started in startPdhMetricsCollection
  if (!m_pdhInterface || !m_pdhInterface->isRunning()) {
    LOG_WARN << "PDH interface not running in collection thread";
    return;
  }

  auto startTime = std::chrono::steady_clock::now();

  while (m_pdhMetricsRunning) {
    // Collect metrics
    if (m_pdhInterface->isRunning()) {
      auto now = std::chrono::steady_clock::now();
      auto elapsedSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(now - startTime)
          .count();

      // Get current test name
      std::string testName;
      {
        std::lock_guard<std::mutex> lock(m_testNameMutex);
        testName = m_currentTestName.toStdString();
      }

      // Helper function to safely get metric value
      auto getMetricValue = [this](const std::string& metricName) -> double {
        double value = -1.0;
        m_pdhInterface->getMetric(metricName, value);
        return value;
      };

      // Helper function to get per-core metric value by index
      auto getPerCoreValue = [this](const std::string& metricName, int coreIndex) -> double {
        std::vector<double> perCoreValues;
        if (m_pdhInterface->getPerCoreMetric(metricName, perCoreValues) && coreIndex < perCoreValues.size()) {
          return perCoreValues[coreIndex];
        }
        return -1.0;
      };

      // Write comprehensive data to CSV with ALL essential metrics
      if (m_pdhMetricsFile.is_open()) {
        m_pdhMetricsFile
          << elapsedSeconds << ","
          << "\"" << testName << "\","
          // CPU Metrics - Total
          << getMetricValue("cpu_total_usage") << ","
          << getMetricValue("cpu_user_time") << ","
          << getMetricValue("cpu_privileged_time") << ","
          << getMetricValue("cpu_idle_time") << ","
          << getMetricValue("cpu_actual_frequency") << ","
          << getMetricValue("cpu_interrupts_per_sec") << ","
          << getMetricValue("cpu_dpc_time") << ","
          << getMetricValue("cpu_interrupt_time") << ","
          << getMetricValue("cpu_dpcs_queued_per_sec") << ","
          << getMetricValue("cpu_dpc_rate") << ","
          << getMetricValue("cpu_c1_time") << ","
          << getMetricValue("cpu_c2_time") << ","
          << getMetricValue("cpu_c3_time") << ","
          << getMetricValue("cpu_c1_transitions_per_sec") << ","
          << getMetricValue("cpu_c2_transitions_per_sec") << ","
          << getMetricValue("cpu_c3_transitions_per_sec") << ","
          // CPU Metrics - Per Core Usage (using getPerCoreMetric like BenchmarkManager)
          << getPerCoreValue("cpu_per_core_usage", 0) << ","
          << getPerCoreValue("cpu_per_core_usage", 1) << ","
          << getPerCoreValue("cpu_per_core_usage", 2) << ","
          << getPerCoreValue("cpu_per_core_usage", 3) << ","
          << getPerCoreValue("cpu_per_core_usage", 4) << ","
          << getPerCoreValue("cpu_per_core_usage", 5) << ","
          << getPerCoreValue("cpu_per_core_usage", 6) << ","
          << getPerCoreValue("cpu_per_core_usage", 7) << ","
          // CPU Metrics - Per Core Frequency (try comma format first, fallback to regular)
          << getPerCoreValue("cpu_per_core_actual_freq_comma", 0) << ","
          << getPerCoreValue("cpu_per_core_actual_freq_comma", 1) << ","
          << getPerCoreValue("cpu_per_core_actual_freq_comma", 2) << ","
          << getPerCoreValue("cpu_per_core_actual_freq_comma", 3) << ","
          << getPerCoreValue("cpu_per_core_actual_freq_comma", 4) << ","
          << getPerCoreValue("cpu_per_core_actual_freq_comma", 5) << ","
          << getPerCoreValue("cpu_per_core_actual_freq_comma", 6) << ","
          << getPerCoreValue("cpu_per_core_actual_freq_comma", 7) << ","
          // Memory Metrics
          << getMetricValue("memory_available_mbytes") << ","
          << getMetricValue("memory_committed_bytes") << ","
          << getMetricValue("memory_commit_limit") << ","
          << getMetricValue("memory_page_faults_per_sec") << ","
          << getMetricValue("memory_pages_per_sec") << ","
          << getMetricValue("memory_pool_nonpaged_bytes") << ","
          << getMetricValue("memory_pool_paged_bytes") << ","
          << getMetricValue("memory_system_code_bytes") << ","
          << getMetricValue("memory_system_driver_bytes") << ","
          // Disk Metrics - Physical
          << getMetricValue("disk_read_bytes_per_sec") << ","
          << getMetricValue("disk_write_bytes_per_sec") << ","
          << getMetricValue("disk_reads_per_sec") << ","
          << getMetricValue("disk_writes_per_sec") << ","
          << getMetricValue("disk_transfers_per_sec") << ","
          << getMetricValue("disk_bytes_per_sec") << ","
          << getMetricValue("disk_avg_read_queue_length") << ","
          << getMetricValue("disk_avg_write_queue_length") << ","
          << getMetricValue("disk_avg_queue_length") << ","
          << getMetricValue("disk_avg_read_time") << ","
          << getMetricValue("disk_avg_write_time") << ","
          << getMetricValue("disk_avg_transfer_time") << ","
          << getMetricValue("disk_percent_time") << ","
          << getMetricValue("disk_percent_read_time") << ","
          << getMetricValue("disk_percent_write_time") << ","
          // System Metrics
          << getMetricValue("system_context_switches_per_sec") << ","
          << getMetricValue("system_system_calls_per_sec") << ","
          << getMetricValue("system_processor_queue_length") << ","
          << getMetricValue("system_processes") << ","
          << getMetricValue("system_threads") << "\n";

        m_pdhMetricsFile.flush();
      }
    }

    // Sleep for one second
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Clean up
  if (m_pdhInterface) {
    m_pdhInterface->stop();
  }
}

void DiagnosticWorker::stopPdhMetricsCollection() {
  if (m_pdhMetricsRunning) {
    m_pdhMetricsRunning = false;

    if (m_pdhMetricsThread.joinable()) {
      m_pdhMetricsThread.join();
    }

    if (m_pdhMetricsFile.is_open()) {
      m_pdhMetricsFile.close();
    }

    // Clean up PDH interface
    if (m_pdhInterface) {
      m_pdhInterface->stop();
      m_pdhInterface.reset();
    }

    log("Comprehensive PDH metrics collection stopped");
  }
}

  void DiagnosticWorker::performAutomaticUpload() {
    try {
      ApplicationSettings& settings = ApplicationSettings::getInstance();
    if (settings.isOfflineModeEnabled()) {
      LOG_INFO << "Offline mode enabled, skipping automatic diagnostic upload";
      return;
    }

    // Check if data collection is allowed (prerequisite)
    if (!settings.getAllowDataCollection()) {
      LOG_INFO << "Data collection is disabled, skipping automatic upload";
      return;
    }

    // Check if automatic upload is enabled
    if (!settings.getEffectiveAutomaticDataUploadEnabled()) {
      LOG_INFO << "Automatic data upload is disabled";
      return;
    }

    LOG_INFO << "Starting automatic diagnostic data upload...";

    // Find the most recent diagnostic JSON file
    QString appDir = QCoreApplication::applicationDirPath();
    QString resultsPath = appDir + "/diagnostic_results";
    QDir dir(resultsPath);
    
    if (!dir.exists()) {
      LOG_WARN << "Diagnostic results directory does not exist: " << resultsPath.toStdString();
      return;
    }

    // Look for diagnostics JSON files (the ones that contain "diagnostics_" like the dialog does)
    QStringList filters;
    filters << "diagnostics_*.json";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Time);
    
    if (files.isEmpty()) {
      LOG_WARN << "No diagnostic JSON files found for automatic upload";
      return;
    }

    // Get the most recent file (first in the time-sorted list)
    QFileInfo mostRecentFile = files.first();
    QString jsonPath = mostRecentFile.absoluteFilePath();
    
    LOG_INFO << "Found most recent diagnostic file: " << jsonPath.toStdString();
    
  // Create list with just the JSON file; UploadApiClient will auto-attach optimization settings and PDH CSV
    QStringList filesToUpload;
    filesToUpload << jsonPath;
    
    LOG_INFO << "Creating UploadApiClient for automatic diagnostic upload...";
    auto* uploadClient = new UploadApiClient(this);
    
    LOG_INFO << "Starting automatic diagnostic upload via UploadApiClient...";
    
    // Use the same approach as DiagnosticUploadDialog
    uploadClient->uploadFiles(filesToUpload, [this, uploadClient](bool success, const QString& error) {
      LOG_INFO << "Automatic diagnostic upload callback received - success: " << success;
      
      // Clean up client
      uploadClient->deleteLater();
      
      if (success) {
        LOG_INFO << "Automatic diagnostic upload succeeded";
      } else {
        LOG_ERROR << "Automatic diagnostic upload failed: " << error.toStdString();
      }
    });
    
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception during automatic diagnostic upload: " << e.what();
  } catch (...) {
    LOG_ERROR << "Unknown exception during automatic diagnostic upload";
  }
}
