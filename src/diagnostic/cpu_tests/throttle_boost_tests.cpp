#include "throttle_boost_tests.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <thread>
#include <unordered_set>
#include <vector>

#include <QCoreApplication>
#include <QThread>
#include <windows.h>
#include <winevt.h>

#include "../cpu_test.h"
#include "ApplicationSettings.h"
#include "diagnostic/CoreBoostMetrics.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "diagnostic/DiagnosticWorker.h"
#include "hardware/ConstantSystemInfo.h"
#include "hardware/WinHardwareMonitor.h"
#include "hardware/PdhInterface.h"

#include "logging/Logger.h"

using namespace std::chrono;

// Simple provider wrapper for PDH metrics
class CpuMetricsProvider {
private:
    std::unique_ptr<PdhInterface> pdhInterface_;
    
public:
    CpuMetricsProvider() {
        pdhInterface_ = PdhInterface::createForCpuMonitoring();
        if (pdhInterface_) {
            pdhInterface_->start();
        }
    }
    
    ~CpuMetricsProvider() {
        if (pdhInterface_) {
            pdhInterface_->stop();
        }
    }
    
    void refresh() {
        // PDH interface refreshes automatically, but we can add a small delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::vector<double> getCoreLoads() {
        if (!pdhInterface_) return {};
        return pdhInterface_->getPerCoreCpuUsage();
    }
    
    std::vector<double> getCoreClocks() {
        if (!pdhInterface_) return {};
        
        // Try to get per-core frequency data
        std::vector<double> clocks;
        size_t coreCount = pdhInterface_->getCpuCoreCount();
        clocks.reserve(coreCount);
        
        for (size_t i = 0; i < coreCount; i++) {
            double freq = 0;
            if (pdhInterface_->getCoreMetric("cpu_actual_frequency", i, freq)) {
                clocks.push_back(freq);
            } else {
                clocks.push_back(0);
            }
        }
        
        return clocks;
    }
};

void analyzeThrottlingImpact(double peakClock, double sustainedClock) {
  double performanceRatio = sustainedClock / peakClock;
  double performanceLoss = (1.0 - performanceRatio) * 100.0;

  // Analysis results are stored but detailed output removed for cleaner logs
}

void testThreadScheduling(int testDurationSeconds) {
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
  const int logicalCores = constantInfo.logicalCores;
  const int physicalCores = constantInfo.physicalCores;
  
  CpuMetricsProvider provider;

  // Determine if SMT/Hyperthreading is active
  const bool smtActive = (logicalCores > physicalCores);

  // Data structures to store topology information
  struct CoreInfo {
    int logicalCoreId;
    int physicalCoreId;
    int coreType;  // 0 = unknown, 1 = P-core, 2 = E-core
    bool isEfficiencyCore;
    std::string coreDescription;
    std::vector<int>
      siblingThreads;  // Other logical cores sharing this physical core
  };
  std::vector<CoreInfo> coreTopology(logicalCores);
  bool isHybridCpu = false;

  // Windows API: Get detailed CPU topology using
  // GetLogicalProcessorInformationEx
  DWORD returnLength = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr,
                                   &returnLength);

  if (returnLength > 0) {
    std::vector<BYTE> buffer(returnLength);
    if (GetLogicalProcessorInformationEx(
          RelationProcessorCore,
          reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
            buffer.data()),
          &returnLength)) {

      PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info =
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
          buffer.data());
      DWORD offset = 0;

      int physicalCoreId = 0;
      while (offset < returnLength) {
        info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(
          buffer.data() + offset);
        if (info->Relationship == RelationProcessorCore) {
          // Get processor mask which shows which logical processors belong to
          // this core
          GROUP_AFFINITY* groupAffinities = info->Processor.GroupMask;
          for (WORD groupIdx = 0; groupIdx < info->Processor.GroupCount;
               groupIdx++) {
            KAFFINITY mask = groupAffinities[groupIdx].Mask;
            WORD groupNumber = groupAffinities[groupIdx].Group;

            std::vector<int> logicalIds;
            // Walk through the processor mask to find logical cores
            for (int bit = 0; bit < sizeof(KAFFINITY) * 8; bit++) {
              if ((mask >> bit) & 1) {
                int logicalId = bit + (groupNumber * sizeof(KAFFINITY) * 8);
                if (logicalId < logicalCores) {
                  logicalIds.push_back(logicalId);

                  // Check for efficiency core flag (Windows 11 only)
                  bool isEfficiency = false;
#ifdef PROCESSOR_INFORMATION_INTEL_EFFICIENCY_CLASS
                  if (info->Processor.Flags &
                      PROCESSOR_INFORMATION_INTEL_EFFICIENCY_CLASS) {
                    isEfficiency = true;
                    isHybridCpu = true;
                  }
#endif

                  // Store core info
                  coreTopology[logicalId].logicalCoreId = logicalId;
                  coreTopology[logicalId].physicalCoreId = physicalCoreId;
                  coreTopology[logicalId].isEfficiencyCore = isEfficiency;
                  coreTopology[logicalId].coreType = isEfficiency ? 2 : 1;
                  coreTopology[logicalId].coreDescription =
                    isEfficiency ? "E-core" : "P-core";
                }
              }
            }

            // Set sibling information for each logical core in this physical
            // core
            for (int logicalId : logicalIds) {
              for (int siblingId : logicalIds) {
                if (logicalId != siblingId) {
                  coreTopology[logicalId].siblingThreads.push_back(siblingId);
                }
              }
            }
          }
          physicalCoreId++;
        }
        offset += info->Size;
      }
    }
  }

  // If hybrid flag wasn't detected but we still have performance data,
  // try a heuristic approach based on baseline clock speeds
  if (!isHybridCpu) {
    // Get base clock speeds for each core
    auto coreClocks = provider.getCoreClocks();

    if (!coreClocks.empty()) {
      // Find min and max clock speeds
      int minClock = INT_MAX;
      int maxClock = 0;
      for (int clock : coreClocks) {
        if (clock > 0) {
          minClock = std::min(minClock, clock);
          maxClock = std::max(maxClock, clock);
        }
      }

      // If significant difference between min and max clocks (more than 20%),
      // this might indicate hybrid cores
      if (maxClock > 0 && minClock < (maxClock * 0.8)) {
        isHybridCpu = true;
        LOG_INFO << "Potential hybrid CPU detected based on clock speed variance";

        // Mark cores with lower clocks as potential E-cores
        for (size_t i = 0; i < coreClocks.size() && i < coreTopology.size();
             i++) {
          if (coreClocks[i] > 0 && coreClocks[i] < (maxClock * 0.8)) {
            coreTopology[i].isEfficiencyCore = true;
            coreTopology[i].coreType = 2;
            coreTopology[i].coreDescription = "Potential E-core";
          }
        }
      }
    }
  }

  // Step 2: Set up thread monitoring
  // For storing CPU usage data
  struct CoreUsageSnapshot {
    int timestamp;
    std::vector<double> usage;
  };

  std::vector<CoreUsageSnapshot> usageHistory;

  // Get baseline CPU usage
  provider.refresh();
  auto initialLoads = provider.getCoreLoads();

  // Setup simple workload to test thread scheduling
  // Create threads with different priorities and CPU affinities
  const int NUM_TEST_THREADS =
    std::min(physicalCores * 2, 16);  // Don't create too many threads

  // Thread info structure
  struct ThreadInfo {
    int id;
    int priority;
    int preferredCore;  // Core we want this thread to run on
    std::atomic<int>
      actualCore;  // Core it actually runs on (updated during execution)
    std::atomic<double> cpuTime;  // CPU time consumed
  };

  std::vector<ThreadInfo> threadInfo(NUM_TEST_THREADS);
  std::vector<std::thread> threads;
  std::atomic<bool> shouldRun(true);

  // Create thread workload function
  auto threadWorkload = [&shouldRun](ThreadInfo* info) {
    // Set thread name for easier identification
    std::string threadName = "TestThread-" + std::to_string(info->id);

#ifdef _WIN32
    // Get thread handle
    HANDLE hThread = GetCurrentThread();

    // Set thread priority
    int winPriority = THREAD_PRIORITY_NORMAL;
    switch (info->priority) {
      case 1:
        winPriority = THREAD_PRIORITY_LOWEST;
        break;
      case 2:
        winPriority = THREAD_PRIORITY_BELOW_NORMAL;
        break;
      case 3:
        winPriority = THREAD_PRIORITY_NORMAL;
        break;
      case 4:
        winPriority = THREAD_PRIORITY_ABOVE_NORMAL;
        break;
      case 5:
        winPriority = THREAD_PRIORITY_HIGHEST;
        break;
    }

    SetThreadPriority(hThread, winPriority);

    // Set thread affinity if a preferred core is specified
    if (info->preferredCore >= 0) {
      SetThreadAffinityMask(hThread, (DWORD_PTR)(1ULL << info->preferredCore));
    }
#endif

    // CPU-intensive work
    volatile double result = 0.0;

    while (shouldRun) {
// Get current processor
#ifdef _WIN32
      DWORD currentProcessor = GetCurrentProcessorNumber();
      info->actualCore = static_cast<int>(currentProcessor);
#endif

      // Do some floating-point work
      for (int i = 0; i < 10000; i++) {
        result += std::sin(result) * std::cos(result);
        result = std::fmod(result, 1.0);
      }

      // Brief sleep to allow thread switches
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Store result to prevent optimization
    info->cpuTime = result;
  };

  // Initialize thread info
  for (int i = 0; i < NUM_TEST_THREADS; i++) {
    threadInfo[i].id = i;
    threadInfo[i].priority = (i % 5) + 1;  // Priority from 1-5

    // For some threads, set preferred cores
    if (i < physicalCores) {
      // For half the threads, try to target specific cores
      // For hybrid CPUs, target E-cores for low-priority, P-cores for
      // high-priority
      if (isHybridCpu && i < NUM_TEST_THREADS / 2) {
        // Find a P-core for high-priority thread
        for (size_t j = 0; j < coreTopology.size(); j++) {
          if (!coreTopology[j].isEfficiencyCore) {
            threadInfo[i].preferredCore = j;
            break;
          }
        }
      } else if (isHybridCpu) {
        // Find an E-core for low-priority thread
        for (size_t j = 0; j < coreTopology.size(); j++) {
          if (coreTopology[j].isEfficiencyCore) {
            threadInfo[i].preferredCore = j;
            break;
          }
        }
      } else {
        // For non-hybrid CPUs, distribute across physical cores
        threadInfo[i].preferredCore = i % physicalCores;
      }
    } else {
      threadInfo[i].preferredCore = -1;  // Let OS decide
    }

    threadInfo[i].actualCore = -1;
    threadInfo[i].cpuTime = 0.0;
  }

  // Start the threads
  for (int i = 0; i < NUM_TEST_THREADS; i++) {
    threads.emplace_back(threadWorkload, &threadInfo[i]);
  }

  // Monitor CPU usage over time
  for (int t = 0; t < testDurationSeconds; t++) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    provider.refresh();
    auto currentLoads = provider.getCoreLoads();

    CoreUsageSnapshot snapshot;
    snapshot.timestamp = t;
    snapshot.usage = currentLoads;
    usageHistory.push_back(snapshot);

    // Print current usage
    LOG_INFO << "Second " << t + 1 << " load: ";
    for (size_t i = 0; i < std::min(currentLoads.size(), size_t(16)); i++) {
    }
  }

  // Stop all threads
  shouldRun = false;
  for (auto& thread : threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  // Step 3: Analyze results

  bool foundSchedulingIssue = false;

  for (int i = 0; i < NUM_TEST_THREADS; i++) {
    const auto& thread = threadInfo[i];
    int actualCore = thread.actualCore;

    // Determine if this placement was suboptimal
    bool isSuboptimal = false;
    std::string issueDesc = "";

    if (isHybridCpu && actualCore >= 0 && actualCore < coreTopology.size()) {
      // Check if high-priority thread ran on E-core
      if (thread.priority >= 4 && coreTopology[actualCore].isEfficiencyCore) {
        isSuboptimal = true;
        issueDesc = " (HIGH PRIORITY ON E-CORE!)";
        foundSchedulingIssue = true;
      }
      // Check if preferred core was ignored significantly
      else if (thread.preferredCore >= 0 &&
               actualCore != thread.preferredCore) {
        // Only flag if core types differ
        if (actualCore < coreTopology.size() &&
            thread.preferredCore < coreTopology.size() &&
            coreTopology[actualCore].coreType !=
              coreTopology[thread.preferredCore].coreType) {
          isSuboptimal = true;
          issueDesc = " (WRONG CORE TYPE)";
          foundSchedulingIssue = true;
        }
      }
    }

    // Analysis is performed but detailed logging removed for cleaner output
  }
}

// Modify testCombinedThrottling to run more efficiently with less console
// blocking
void testCombinedThrottling(int testDuration) {
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
  const int numCores = constantInfo.logicalCores;
  
  CpuMetricsProvider provider;

  // Data structures to store comprehensive metrics
  struct FrequencyMetrics {
    int timestamp;
    double avgClock;
    double maxClock;
    int highestClockCore;
    std::vector<double> coreClocks;  // Changed from int to double to match getCoreClocks()
  };

  std::vector<FrequencyMetrics> metricsHistory;

  // Get baseline idle metrics
  provider.refresh();
  auto idleClocks = provider.getCoreClocks();

  // Calculate average idle values
  double avgIdleClock = 0;
  int nonZeroClocks = 0;

  for (size_t i = 0; i < idleClocks.size(); i++) {
    if (idleClocks[i] > 0) {
      avgIdleClock += idleClocks[i];
      nonZeroClocks++;
    }
  }

  if (nonZeroClocks > 0) avgIdleClock /= nonZeroClocks;

  // Create a lambda for heavy multi-core computation
  std::atomic<bool> running(true);
  auto heavyTask = [&running]() {
    volatile double result = 0.0;
    while (running) {
      for (int i = 0; i < 10000; i++) {
        result += std::sin(result) * std::cos(result) /
                  (std::sqrt(std::abs(result) + 1.0) + 1.0);
      }
    }
    return result;
  };

  // Start workload on all cores - explicitly set thread affinity to distribute
  // load evenly
  std::vector<std::future<double>> results;
  for (int i = 0; i < numCores; i++) {
    results.push_back(
      std::async(std::launch::async, [i, &heavyTask, &running]() {
        // Set thread affinity to distribute load evenly
        HANDLE currentThread = GetCurrentThread();
        SetThreadAffinityMask(currentThread, (DWORD_PTR)1 << i);
        return heavyTask();
      }));
  }

  LOG_INFO << "\nTime |  AvgMHz  |  MaxMHz  | Core#\n";
  LOG_INFO << "--------------------------------\n";

  // Variables to track peak values
  double peakAvgClock = 0;
  double peakMaxClock = 0;

  // Reduce console output frequency
  const int outputInterval = 5;  // Print every 5 seconds

  for (int t = 0; t <= testDuration; t++) {
    // Sleep for one second
    if (t > 0) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Get fresh data first, before any console output
    provider.refresh();

    // Capture all metrics
    FrequencyMetrics metrics;
    metrics.timestamp = t;
    metrics.coreClocks = provider.getCoreClocks();

    // Calculate derived metrics
    double totalClock = 0;
    int maxClock = 0;
    int highestClockCore = 0;
    int nonZeroClockCount = 0;

    for (size_t i = 0; i < metrics.coreClocks.size(); i++) {
      if (metrics.coreClocks[i] > 0) {
        totalClock += metrics.coreClocks[i];
        nonZeroClockCount++;
        if (metrics.coreClocks[i] > maxClock) {
          maxClock = metrics.coreClocks[i];
          highestClockCore = i;
        }
      }
    }

    // Calculate averages
    metrics.avgClock =
      nonZeroClockCount > 0 ? totalClock / nonZeroClockCount : 0;
    metrics.maxClock = maxClock;
    metrics.highestClockCore = highestClockCore;

    // Track peak values
    if (metrics.avgClock > peakAvgClock) peakAvgClock = metrics.avgClock;
    if (metrics.maxClock > peakMaxClock) peakMaxClock = metrics.maxClock;

    // Store metrics history
    metricsHistory.push_back(metrics);
  }

  // Stop the workload
  running = false;
  for (auto& result : results) {
    result.wait();
  }

  // Analyze frequency stability (detailed output removed for cleaner logs)
  double lastAvgClock = metricsHistory.back().avgClock;
  double clockDropPercent = 0;
  if (peakAvgClock > 0) {
    clockDropPercent = 100.0 * (peakAvgClock - lastAvgClock) / peakAvgClock;
  }

  // Calculate when throttling occurred (if it did)
  int throttlingDetectedTime = -1;
  if (clockDropPercent > 10) {
    // Find when the clock first dropped below 90% of peak
    for (size_t i = 0; i < metricsHistory.size(); i++) {
      if (metricsHistory[i].avgClock > 0 &&
          metricsHistory[i].avgClock < (peakAvgClock * 0.9)) {
        throttlingDetectedTime = metricsHistory[i].timestamp;
        break;
      }
    }

    // Store throttling data in DiagnosticDataStore but remove console output
    auto& dataStore = DiagnosticDataStore::getInstance();
    dataStore.updateCPUThrottlingInfo(true, peakAvgClock, lastAvgClock,
                                      clockDropPercent, throttlingDetectedTime);

    analyzeThrottlingImpact(peakAvgClock, lastAvgClock);
  } else {
    // Store non-throttling data in DiagnosticDataStore
    auto& dataStore = DiagnosticDataStore::getInstance();
    dataStore.updateCPUThrottlingInfo(false, peakAvgClock, lastAvgClock,
                                      clockDropPercent, -1);
  }
}

// Modify testPowerThrottling to run more efficiently with less console blocking
void testPowerThrottling() {
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
  const int numCores = constantInfo.logicalCores;
  
  CpuMetricsProvider provider;
  const int testDuration = 60;  // Duration in seconds

  // Data structures to store metrics at each measurement point
  struct FrequencyMetrics {
    int timestamp;
    double avgClockSpeed;
    double maxClockSpeed;
    bool throttlingDetected;
  };
  std::vector<FrequencyMetrics> frequencyHistory;

  // First get baseline idle metrics
  provider.refresh();
  auto idleClocks = provider.getCoreClocks();
  double avgIdleClock = 0;
  int nonZeroClocks = 0;

  for (size_t i = 0; i < idleClocks.size(); i++) {
    if (idleClocks[i] > 0) {
      avgIdleClock += idleClocks[i];
      nonZeroClocks++;
    }
  }

  if (nonZeroClocks > 0) {
    avgIdleClock /= nonZeroClocks;
  }

  LOG_INFO << "Baseline measurements:";
  LOG_INFO << "  Avg. idle clock: " << avgIdleClock << " MHz";
  LOG_INFO << "Starting heavy CPU load...";

  // Create a lambda for heavy multi-core computation
  std::atomic<bool> running(true);
  auto heavyTask = [&running]() {
    volatile double result = 0.0;
    while (running) {
      for (int i = 0; i < 10000; i++) {
        result += std::sin(result) * std::cos(result) /
                  (std::sqrt(std::abs(result) + 1.0) + 1.0);
      }
    }
    return result;  // Prevent optimization
  };

  // Start workload on all cores
  std::vector<std::future<double>> results;
  for (int i = 0; i < numCores; i++) {
    results.push_back(std::async(std::launch::async, heavyTask));
  }

  // Header for metrics output
  LOG_INFO << "\nTime | Avg Clock | Max Clock | Status\n";
  LOG_INFO << "-------------------------------------\n";

  // Track maximum values observed
  double maxAvgClock = 0;
  double maxClock = 0;
  int maxAvgClockTime = 0;

  // Variables to detect throttling
  bool throttlingDetected = false;
  double peakClock = 0;
  double clockDropPercent = 0;
  int significantDropTime = -1;

  // Reduce console output frequency to prevent blocking
  const int outputInterval = 5;  // Only print every 5 seconds

  for (int t = 0; t <= testDuration; t++) {
    // Sleep for one second
    if (t > 0) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Refresh data and get metrics first, before any console output
    provider.refresh();
    auto clockSpeeds = provider.getCoreClocks();
    double avgClock = 0;
    double maxCurrentClock = 0;
    int activeCores = 0;

    for (size_t i = 0; i < clockSpeeds.size(); i++) {
      if (clockSpeeds[i] > 0) {
        avgClock += clockSpeeds[i];
        maxCurrentClock = std::max(maxCurrentClock, (double)clockSpeeds[i]);
        activeCores++;
      }
    }

    if (activeCores > 0) {
      avgClock /= activeCores;
    }

    // Track maximum values
    if (avgClock > maxAvgClock) {
      maxAvgClock = avgClock;
      maxAvgClockTime = t;
    }

    if (maxCurrentClock > maxClock) {
      maxClock = maxCurrentClock;
    }

    if (avgClock > peakClock) {
      peakClock = avgClock;
    }

    // Check for significant clock speed drop (potential throttling)
    bool currentThrottling = false;
    if (t > 5 && peakClock > 0 && avgClock < peakClock * 0.9 &&
        significantDropTime == -1) {
      significantDropTime = t;
      clockDropPercent = 100.0 * (peakClock - avgClock) / peakClock;
      throttlingDetected = true;
      currentThrottling = true;
    }

    // Store metrics for this time point
    FrequencyMetrics metrics;
    metrics.timestamp = t;
    metrics.avgClockSpeed = avgClock;
    metrics.maxClockSpeed = maxCurrentClock;
    metrics.throttlingDetected = currentThrottling;
    frequencyHistory.push_back(metrics);

    // Output current metrics less frequently to reduce console blocking
    if (t % outputInterval == 0 || t == testDuration || currentThrottling) {
      std::string status =
        currentThrottling ? "THROTTLING DETECTED" : "Normal operation";
      LOG_INFO << std::setw(4) << t << " | " << std::setw(9) << std::fixed
                << std::setprecision(0) << avgClock << " | " << std::setw(9)
                << std::fixed << std::setprecision(0) << maxCurrentClock
                << " | " << status;
    }
  }

  // Stop the load test
  running = false;
  for (auto& result : results) {
    result.wait();
  }

  // Print summary
  LOG_INFO << "\n===== Frequency Throttling Test Summary =====\n";

  if (throttlingDetected) {
    LOG_INFO << "THROTTLING DETECTED: CPU frequency dropped by " << std::fixed
              << std::setprecision(1) << clockDropPercent << "% after "
              << significantDropTime << " seconds of load";

    LOG_INFO << "Peak clock speed: " << std::fixed << std::setprecision(0)
              << peakClock << " MHz";

    double sustainedClock = peakClock * (1 - clockDropPercent / 100);
    LOG_INFO << "Post-throttle clock: " << std::fixed << std::setprecision(0)
              << sustainedClock << " MHz";

    // Store the throttling data in DiagnosticDataStore
    auto& dataStore = DiagnosticDataStore::getInstance();
    dataStore.updateCPUThrottlingInfo(throttlingDetected, peakClock,
                                      sustainedClock, clockDropPercent,
                                      significantDropTime);

    analyzeThrottlingImpact(peakClock, sustainedClock);
  } else {
    LOG_INFO << "No significant throttling detected during the test period.";

    auto& dataStore = DiagnosticDataStore::getInstance();
    dataStore.updateCPUThrottlingInfo(false, peakClock, peakClock, 0.0, -1);
  }

  LOG_INFO << "\nFrequency statistics:\n";
  LOG_INFO << "  Maximum average clock: " << std::fixed << std::setprecision(0)
           << maxAvgClock << " MHz at " << maxAvgClockTime << " seconds";
  LOG_INFO << "  Maximum single core clock: " << std::fixed
           << std::setprecision(0) << maxClock << " MHz";

  LOG_INFO << "\nTest completed.";
}

// New test function to examine CPU boost behavior under load
void testCPUBoostBehavior() {
  LOG_INFO << "\n===== CPU Boost Behavior Test =====";
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
  const int numCores = constantInfo.logicalCores;
  
  CpuMetricsProvider provider;
  const int physicalCores = constantInfo.physicalCores;

  // Add explicit sleep to prevent UI thread blocking
  QThread::msleep(10);

  // Data structures to store metrics at different load levels
  struct CoreMetrics {
    int clock;
    double load;
  };

  std::vector<CoreMetrics> idleMetrics(numCores);
  std::vector<CoreMetrics> singleCoreLoadMetrics(numCores);
  std::vector<CoreMetrics> allCoreLoadMetrics(numCores);

  double idleTotalPower = 0.0;
  double singleCoreTotalPower = 0.0;
  double allCoreTotalPower = 0.0;

  // Helper function to capture and store metrics
  auto captureMetrics = [&](std::vector<CoreMetrics>& metrics,
                            const std::string& loadType) {
    // Take multiple samples to ensure stable readings
    const int numSamples = 5;
    std::vector<int> avgClocks(numCores, 0);
    std::vector<double> avgLoads(numCores, 0.0);

    for (int sample = 0; sample < numSamples; sample++) {
      // Ensure we get fresh data
      provider.refresh();

      // Get current metrics
      auto clocks = provider.getCoreClocks();
      auto loads = provider.getCoreLoads();

      // Accumulate values
      for (int i = 0; i < numCores; i++) {
        if (i < clocks.size()) avgClocks[i] += clocks[i];
        if (i < loads.size()) avgLoads[i] += loads[i];
      }

      // Wait between samples to allow hardware sensors to update
      std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // Calculate averages and store in metrics vector
    for (int i = 0; i < numCores; i++) {
      metrics[i].clock = avgClocks[i] / numSamples;
      metrics[i].load = avgLoads[i] / numSamples;
    }

    // Print current metrics summary
    LOG_INFO << "\n--- " << loadType << " Metrics ---";

    // Find max clock and its core
    int maxClock = 0;
    int maxClockCore = 0;
    for (int i = 0; i < numCores; i++) {
      if (metrics[i].clock > maxClock) {
        maxClock = metrics[i].clock;
        maxClockCore = i;
      }
    }

    LOG_INFO << "Max Clock: " << maxClock << " MHz on Core #" << maxClockCore;
    LOG_INFO << "Average Core Metrics (first " << std::min(8, numCores)
             << " cores):";

    // Print header
    LOG_INFO << "Core   | Clock (MHz) | Load (%)";
    LOG_INFO << "--------------------------------";

    // Print metrics for first few cores (to avoid too much output)
    for (int i = 0; i < std::min(8, numCores); i++) {
      LOG_INFO << std::setw(6) << i << " | " << std::setw(11)
               << metrics[i].clock << " | " << std::setw(8) << std::fixed
               << std::setprecision(1) << metrics[i].load;
    }
  };

  // 1. First get baseline (idle) metrics
  LOG_INFO << "Measuring idle metrics...";
  // Wait a bit to ensure system is relatively idle
  std::this_thread::sleep_for(std::chrono::seconds(2));
  captureMetrics(idleMetrics, "Idle");

  // 2. Single-core load test using a controlled workload
  LOG_INFO << "\nStarting single-core load test...";

  // Create a lambda for heavy single-core computation
  std::atomic<bool> running(true);
  auto heavyTask = [&running]() {
    volatile double result = 0.0;
    while (running) {
      for (int i = 0; i < 10000; i++) {
        // Complex math operations to max out a single core
        result += std::sin(result) * std::cos(result) /
                  (std::sqrt(std::abs(result) + 1.0) + 1.0);
      }
    }
    return result;  // To prevent optimization
  };

  // Start single-core workload in a separate thread
  std::future<double> singleCoreResult =
    std::async(std::launch::async, heavyTask);

  // Wait for load to stabilize
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Measure metrics under single-core load
  captureMetrics(singleCoreLoadMetrics, "Single-Core Load");

  // Stop the single-core workload
  running = false;
  singleCoreResult.wait();

  // Reset the flag for multi-core test
  running = true;

  // 3. Multi-core load test
  LOG_INFO << "\nStarting multi-core load test...";

  // Create a vector of futures to hold all the thread results
  std::vector<std::future<double>> multiCoreResults;

  // Start workload on all cores
  for (int i = 0; i < physicalCores; i++) {
    multiCoreResults.push_back(std::async(std::launch::async, heavyTask));
  }

  // Wait for load to stabilize
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Measure metrics under multi-core load
  captureMetrics(allCoreLoadMetrics, "All-Core Load");

  // Stop all workloads
  running = false;
  for (auto& result : multiCoreResults) {
    result.wait();
  }

  // 4. Print comparison summary
  LOG_INFO << "\n===== Boost Behavior Summary =====";

  LOG_INFO << "CPU Clock Behavior:";
  // Find highest boosting cores
  int highestSingleCoreBoost = 0;
  int highestSingleCoreIndex = 0;
  int highestAllCoreBoost = 0;
  int highestAllCoreIndex = 0;
  double averageSingleCoreBoost = 0;
  double averageAllCoreBoost = 0;

  for (int i = 0; i < numCores; i++) {
    int singleCoreDelta = singleCoreLoadMetrics[i].clock - idleMetrics[i].clock;
    int allCoreDelta = allCoreLoadMetrics[i].clock - idleMetrics[i].clock;

    averageSingleCoreBoost += singleCoreDelta;
    averageAllCoreBoost += allCoreDelta;

    if (singleCoreDelta > highestSingleCoreBoost) {
      highestSingleCoreBoost = singleCoreDelta;
      highestSingleCoreIndex = i;
    }

    if (allCoreDelta > highestAllCoreBoost) {
      highestAllCoreBoost = allCoreDelta;
      highestAllCoreIndex = i;
    }
  }

  averageSingleCoreBoost /= numCores;
  averageAllCoreBoost /= numCores;

  LOG_INFO << "  Highest Single-Core Boost: +" << highestSingleCoreBoost
           << " MHz on Core #" << highestSingleCoreIndex;
  LOG_INFO << "  Highest All-Core Boost: +" << highestAllCoreBoost
           << " MHz on Core #" << highestAllCoreIndex;
  LOG_INFO << "  Average Single-Core Boost: +" << std::fixed
           << std::setprecision(1) << averageSingleCoreBoost << " MHz";
  LOG_INFO << "  Average All-Core Boost: +" << std::fixed
           << std::setprecision(1) << averageAllCoreBoost << " MHz";

  LOG_INFO << "\nTest completed. This information can be used to assess CPU "
               "boost behavior.";

  // Add periodic QApplication::processEvents() calls if running in UI thread
  if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
    QCoreApplication::processEvents();
  }

  // At the end of each major task
  QThread::msleep(10);  // Small pause to allow event processing
}

// Global variables to store per-core boost metrics
extern std::vector<CoreBoostMetrics> g_cpuBoostMetrics;
extern double g_idleTotalPower;
extern double g_allCoreTotalPower;
extern int g_bestBoostCore;
extern int g_maxBoostDelta;

// New test function that examines each CPU core individually
void testCPUBoostBehaviorPerCore() {
  LOG_INFO << "\n===== CPU Per-Core Boost Behavior Test =====";

  // Use WinHardwareMonitor directly to get CPU information
  WinHardwareMonitor hwMonitor;
  hwMonitor.updateSensors();
  auto cpuInfo = hwMonitor.getCPUInfo();
  const int numCores = cpuInfo.logicalCores;

  // Resize the global metrics storage if needed
  if (g_cpuBoostMetrics.size() < numCores) {
    g_cpuBoostMetrics.resize(numCores);
  }

  // Data structures to store metrics for each state
  struct CoreMetrics {
    int clock;
    double load;
  };

  // Get idle metrics first
  LOG_INFO << "Measuring idle metrics...";
  std::vector<CoreMetrics> idleMetrics(numCores);
  double idleTotalPower = 0.0;

  // Take multiple samples to ensure stable readings
  const int numSamples = 5;
  std::vector<int> avgClocks(numCores, 0);
  std::vector<double> avgLoads(numCores, 0.0);

  for (int sample = 0; sample < numSamples; sample++) {
    hwMonitor.updateSensors();
    auto info = hwMonitor.getCPUInfo();
    auto clocks = info.coreClocks;
    auto loads = info.coreLoads;

    for (int i = 0; i < numCores; i++) {
      if (i < clocks.size()) avgClocks[i] += clocks[i];
      if (i < loads.size()) avgLoads[i] += loads[i];
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }

  for (int i = 0; i < numCores; i++) {
    idleMetrics[i].clock = avgClocks[i] / numSamples;
    idleMetrics[i].load = avgLoads[i] / numSamples;

    // Store in global metrics
    g_cpuBoostMetrics[i].idleClock = idleMetrics[i].clock;
  }
  idleTotalPower = 0.0;
  g_idleTotalPower = 0.0;

  // Results for each core under load
  std::vector<CoreMetrics> perCoreLoadMetrics(numCores);
  std::vector<double> perCorePower(numCores);

  // Create a lambda for heavy computation
  auto heavyTask = [](volatile bool* shouldRun) {
    volatile double result = 0.0;
    while (*shouldRun) {
      for (int i = 0; i < 10000; i++) {
        // Complex math operations to max out the core
        result += std::sin(result) * std::cos(result) /
                  (std::sqrt(std::abs(result) + 1.0) + 1.0);
      }
    }
  };

  // Test each core individually
  for (int coreToTest = 0; coreToTest < numCores; coreToTest++) {
    LOG_INFO << "\nTesting Core #" << coreToTest << "...";

    // Create a thread with affinity to specific core
    volatile bool shouldRun = true;
    std::thread testThread([&]() {
      // Set affinity mask to target only the specific core
      SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << coreToTest);
      heavyTask(&shouldRun);
    });

    // Give the core time to boost and stabilize
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Capture metrics for this core under load
    hwMonitor.updateSensors();
    auto info = hwMonitor.getCPUInfo();
    auto clocks = info.coreClocks;
    auto loads = info.coreLoads;

    // Store metrics for this core
    if (coreToTest < clocks.size()) {
      perCoreLoadMetrics[coreToTest].clock = clocks[coreToTest];
      // Store in global metrics
      g_cpuBoostMetrics[coreToTest].singleLoadClock = clocks[coreToTest];
    }
    if (coreToTest < loads.size())
      perCoreLoadMetrics[coreToTest].load = loads[coreToTest];

    // Print current metrics
    LOG_INFO << "  Clock: " << perCoreLoadMetrics[coreToTest].clock << " MHz";
    LOG_INFO << ", Load: " << std::fixed << std::setprecision(1)
              << perCoreLoadMetrics[coreToTest].load << "%";

    // Stop the test for this core
    shouldRun = false;
    testThread.join();

    // Allow the system to cool down and return to idle
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Now do an all-core test
  LOG_INFO << "\nRunning all-core test for comparison...";
  std::vector<CoreMetrics> allCoreMetrics(numCores);
  double allCoreTotalPower = 0.0;

  // Create threads for all cores
  std::vector<std::thread> threads;
  volatile bool allCoresShouldRun = true;

  for (int i = 0; i < numCores; i++) {
    threads.emplace_back([i, &allCoresShouldRun, &heavyTask]() {
      SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)1 << i);
      heavyTask(&allCoresShouldRun);
    });
  }

  // Let the system stabilize under full load
  std::this_thread::sleep_for(std::chrono::seconds(3));

  // Take measurements under full load
  hwMonitor.updateSensors();
  auto info = hwMonitor.getCPUInfo();
  auto clocks = info.coreClocks;
  auto loads = info.coreLoads;
  auto temps = info.coreTemperatures;
  auto powers = info.corePowers;
  allCoreTotalPower = 0.0;
  g_allCoreTotalPower = 0.0;

  // Store all-core metrics
  for (int i = 0; i < numCores; i++) {
    if (i < clocks.size()) {
      allCoreMetrics[i].clock = clocks[i];
      g_cpuBoostMetrics[i].allCoreClock = clocks[i];
    }
    if (i < loads.size()) allCoreMetrics[i].load = loads[i];
  }

  // Stop all threads
  allCoresShouldRun = false;
  for (auto& t : threads) {
    t.join();
  }

  // Print summary of results
  LOG_INFO << "\n===== Per-Core Boost Summary =====\n";
  LOG_INFO << "Idle Package Power: " << std::fixed << std::setprecision(2)
           << idleTotalPower << " W\n";
  LOG_INFO << "All-Core Package Power: " << allCoreTotalPower << " W\n\n";

  LOG_INFO << "Core  | Idle Clock | Single Load | Boost Delta | All-Core |\n";
  LOG_INFO << "--------------------------------------------------------\n";

  // Calculate maximum boost for each core
  int maxBoostCore = 0;
  int maxBoostClock = 0;

  for (int i = 0; i < numCores; i++) {
    int boostDelta = perCoreLoadMetrics[i].clock - idleMetrics[i].clock;

    LOG_INFO << std::setw(5) << i << " | " << std::setw(10)
             << idleMetrics[i].clock << " | " << std::setw(11)
             << perCoreLoadMetrics[i].clock << " | " << std::setw(11)
             << boostDelta << " | " << std::setw(8) << allCoreMetrics[i].clock
             << "\n";

    if (boostDelta > maxBoostClock) {
      maxBoostClock = boostDelta;
      maxBoostCore = i;
    }
  }

  // Store best boosting core information
  g_bestBoostCore = maxBoostCore;
  g_maxBoostDelta = maxBoostClock;

  LOG_INFO << "\nBest boosting core: Core #" << maxBoostCore << " with +"
           << maxBoostClock << " MHz\n";

  LOG_INFO << "\nTest completed. This data shows how each individual core "
               "boosts under load.\n";
}
