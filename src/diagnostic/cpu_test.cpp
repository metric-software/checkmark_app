#include "cpu_test.h"
#include "../logging/Logger.h"

#include <chrono>
#include <iostream>

#include <windows.h>

#include "ApplicationSettings.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "diagnostic/DiagnosticWorker.h"
#include "hardware/ConstantSystemInfo.h"

// Include the new modularized test headers
#include "cpu_tests/cpu_benchmarks.h"
#include "cpu_tests/throttle_boost_tests.h"

// Add a function to emit progress updates
void emitCpuTestProgress(const QString& message, int progress) {
  // Get reference to DiagnosticDataStore
  auto& dataStore = DiagnosticDataStore::getInstance();

  // If you have a way to emit progress updates, use it here
  // Assuming there's an emitTestProgress method or similar
  if (dataStore.getEmitProgressCallback()) {
    dataStore.getEmitProgressCallback()(message, progress);
  }
}

// Forward declaration of ColdStartResults from benchmarks
struct ColdStartResults;

// Global variables to store boost test results
std::vector<CoreBoostMetrics> g_cpuBoostMetrics;
int g_bestBoostCore = 0;
int g_maxBoostDelta = 0;
double g_idleTotalPower = 0.0;
double g_allCoreTotalPower = 0.0;

// Main CPU test interface - calls benchmark functions from cpu_benchmarks.cpp
void runCpuTests() {
  LOG_INFO << "[CPU Test] Running...";

  // Save current thread priority and increase it only if enabled in settings
  HANDLE currentThread = GetCurrentThread();
  int originalPriority = GetThreadPriority(currentThread);

  bool elevatedPriorityEnabled =
    ApplicationSettings::getInstance().getElevatedPriorityEnabled();
  if (elevatedPriorityEnabled) {
    SetThreadPriority(currentThread, THREAD_PRIORITY_HIGHEST);
    LOG_INFO << "Running with elevated thread priority (enabled in settings)";
  }

  // Get reference to DiagnosticDataStore
  auto& dataStore = DiagnosticDataStore::getInstance();

  // Get CPU information
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();

  // Create a temporary CPU data object to work with
  DiagnosticDataStore::CPUData cpuData = dataStore.getCPUData();

  // Store basic CPU info
  cpuData.name = constantInfo.cpuName;
  cpuData.physicalCores = constantInfo.physicalCores;
  cpuData.threadCount = constantInfo.logicalCores;
  cpuData.currentClockSpeed = constantInfo.baseClockMHz;
  cpuData.currentVoltage = 0.0; // Voltage not available in ConstantSystemInfo
  cpuData.loadPercentage = 0; // Load percentage not available in ConstantSystemInfo

  // Emit progress and status
  emitCpuTestProgress("CPU Test: Single-Core Performance", 18);

  // Single-core test (delegates to cpu_benchmarks.cpp)
  double singleCoreTime = 0;
  singleCoreMatrixMultiplicationTest(cpuData.physicalCores, &singleCoreTime);
  cpuData.singleCoreTime = singleCoreTime;

  // Emit progress and status
  emitCpuTestProgress("CPU Test: Multi-Core Performance", 20);

  // 4-thread test (only if CPU has enough threads)
  double fourThreadTime = 0;
  fourThreadMatrixMultiplicationTest(constantInfo.logicalCores, &fourThreadTime);
  cpuData.fourThreadTime = fourThreadTime;

  // 8-thread test - DISABLED
  double eightThreadTime = -1.0;  // Set to -1.0 to indicate test was skipped
  LOG_INFO << "[8-Thread CPU Test] Skipped (disabled)";
  // Keep the implementation but don't call it
  // eightThreadMatrixMultiplicationTest(cpuInfo.logicalCores, &eightThreadTime);

  // Emit progress and status
  emitCpuTestProgress("CPU Test: SIMD Performance", 22);

  // SIMD tests (delegates to cpu_benchmarks.cpp)
  double simdScalar, simdAvx;
  testSIMD(&simdScalar, &simdAvx);
  cpuData.simdScalar = simdScalar;
  cpuData.simdAvx = simdAvx;

  // Emit progress and status
  emitCpuTestProgress("CPU Test: Prime Calculation", 23);

  // Prime calculation (delegates to cpu_benchmarks.cpp)
  cpuData.primeTime = testPrimeCalculation();
  LOG_INFO << "[CPU Test] Prime calculation result: " << cpuData.primeTime << " ms";

  // Update performance metrics before running cache test
  dataStore.updateCPUPerformanceMetrics(simdScalar, simdAvx, cpuData.primeTime,
                                        cpuData.singleCoreTime,
                                        cpuData.fourThreadTime);
  LOG_INFO << "[CPU Test] Updated CPU performance metrics with primeTime: " << cpuData.primeTime;

  // Emit progress and status
  emitCpuTestProgress("CPU Test: Cache/Memory Latency", 24);

  // Cache latency (delegates to cpu_benchmarks.cpp)
  double latencyResults[12] = {-1};
  testCacheAndMemoryLatency(latencyResults);

  // Get updated CPU data after cache test - THIS MAY OVERWRITE PRIME DATA
  double primeTimeBeforeRefresh = cpuData.primeTime;
  cpuData = dataStore.getCPUData();
  LOG_INFO << "[CPU Test] Prime time before getCPUData(): " << primeTimeBeforeRefresh;
  LOG_INFO << "[CPU Test] Prime time after getCPUData(): " << cpuData.primeTime;

  // Emit progress and status
  emitCpuTestProgress("CPU Test: Game Simulation (Small)", 25);

  // Game simulation tests (delegates to cpu_benchmarks.cpp)
  LOG_INFO << "Running game simulation tests...";

  // Small - heavy L1/L2/L3 usage but fits in cache
  cpuData.gameSimUPS_small = testGameSimulation(128 * 1024,  // 128 KB (L1/L2)
                                                2 * 1024 * 1024,  // 2 MB (L3)
                                                16 * 1024 * 1024  // 16 MB (L3)
  );

  // Emit progress and status
  emitCpuTestProgress("CPU Test: Game Simulation (Medium)", 26);

  // Medium - fills most L3, some RAM access
  cpuData.gameSimUPS_medium =
    testGameSimulation(512 * 1024,        // 512 KB (L1/L2)
                       16 * 1024 * 1024,  // 16 MB (L3)
                       48 * 1024 * 1024   // 48 MB (Overflow)
    );

  // Emit progress and status
  emitCpuTestProgress("CPU Test: Game Simulation (Large)", 27);

  // Large - forces significant RAM access
  cpuData.gameSimUPS_large =
    testGameSimulation(1 * 1024 * 1024,   // 1 MB (L1/L2)
                       64 * 1024 * 1024,  // 64 MB (L3 + RAM)
                       128 * 1024 * 1024  // 128 MB (Mostly RAM)
    );

  // Update game sim results
  dataStore.updateCPUGameSimResults(cpuData.gameSimUPS_small,
                                    cpuData.gameSimUPS_medium,
                                    cpuData.gameSimUPS_large);

  // Emit progress and status
  emitCpuTestProgress("CPU Test: Cold Start Response", 28);

  // Run the CPU cold start test (delegates to cpu_benchmarks.cpp)
  runCpuColdStartTest();

  // Restore original priority
  if (elevatedPriorityEnabled) {
    SetThreadPriority(currentThread, originalPriority);
  }

  // Final progress for CPU tests
  emitCpuTestProgress("CPU Test: Completed", 29);

  LOG_INFO << "[CPU Test] Completed.";
}

// Interface to CPU cold start test
void runCpuColdStartTest() {
  LOG_INFO << "[CPU Cold Start Response Test] Running...";

  // Run the cold start test (delegates to cpu_benchmarks.cpp)
  ColdStartResults results = testCpuColdStart();

  // Store results in the DiagnosticDataStore
  auto& dataStore = DiagnosticDataStore::getInstance();
  DiagnosticDataStore::CPUData cpuData = dataStore.getCPUData();

  // Create and populate the cold start metrics
  DiagnosticDataStore::CPUData::ColdStartMetrics coldStartMetrics;
  coldStartMetrics.avgResponseTimeUs = results.avgResponseTime;
  coldStartMetrics.minResponseTimeUs = results.minResponseTime;
  coldStartMetrics.maxResponseTimeUs = results.maxResponseTime;
  coldStartMetrics.stdDevUs = results.stdDev;
  coldStartMetrics.varianceUs = results.variance;

  // Update the CPU data
  cpuData.coldStart = coldStartMetrics;
  dataStore.setCPUData(cpuData);

  LOG_INFO << "[CPU Cold Start Response Test] Completed.";
}

// Interface to CPU boost behavior test
void runCpuBoostBehaviorTest() {
  LOG_INFO << "[CPU Boost Behavior Test] Running...";
  testCPUBoostBehavior();  // Delegates to throttle_boost_tests.cpp
  LOG_INFO << "[CPU Boost Behavior Test] Completed.";
}

// Interface to per-core CPU boost behavior test
void runCpuBoostBehaviorPerCoreTest() {
  LOG_INFO << "[CPU Per-Core Boost Behavior Test] Running...";
  testCPUBoostBehaviorPerCore();  // Delegates to throttle_boost_tests.cpp
  LOG_INFO << "[CPU Per-Core Boost Behavior Test] Completed.";
}

// Interface to CPU power throttling test
void runCpuPowerThrottlingTest() {
  LOG_INFO << "[CPU Power Throttling Test] Running...";
  testPowerThrottling();  // Delegates to throttle_boost_tests.cpp
  LOG_INFO << "[CPU Power Throttling Test] Completed.";
}

// Interface to combined throttling test
void runCombinedThrottlingTest(CpuThrottlingTestMode mode) {
  if (mode == CpuThrottle_None) {
    LOG_INFO << "[CPU Throttling Test] Skipped.";

    auto& dataStore = DiagnosticDataStore::getInstance();
    dataStore.updateCPUThrottlingInfo(false, -1.0, -1.0, 0.0, -1);
    return;
  }

  LOG_INFO << "[CPU Combined Power and Thermal Throttling Test] Running...";

  // Reduce durations: 15 sec for basic mode, 45 sec for extended mode
  int duration = (mode == CpuThrottle_Basic) ? 15 : 45;

  testCombinedThrottling(duration);  // Delegates to throttle_boost_tests.cpp
  LOG_INFO << "[CPU Combined Power and Thermal Throttling Test] Completed.";
}

// Interface to thread scheduling test
void runThreadSchedulingTest() {
  LOG_INFO << "[CPU Thread Scheduling Test] Running...";
  testThreadScheduling(15);  // Delegates to throttle_boost_tests.cpp
  LOG_INFO << "[CPU Thread Scheduling Test] Completed.";
}
