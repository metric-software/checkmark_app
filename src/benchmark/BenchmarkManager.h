#pragma once
#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <QObject>
#include <QString>

#include "BenchmarkConstants.h"
#include "BenchmarkDataPoint.h"
#include "BenchmarkResultFileManager.h"
#include "BenchmarkStateTracker.h"
#include "DemoFileManager.h"  // Add this include
#include "PresentDataExports.h"
#include "hardware/CPUKernelMetricsTracker.h"
#include "hardware/DiskPerformanceTracker.h"
#include "hardware/NvidiaMetrics.h"
#include "hardware/PdhInterface.h"


// Helper method for percentile calculation with filtered values
float calculatePercentile(const std::vector<float>& sortedValues, float percentile);

// Forward declare the callback function
extern "C" void OnMetricsUpdate(uint32_t processId, const PM_METRICS* metrics);

/**
 * @brief BenchmarkManager - Central coordinator for automated game benchmarking
 * 
 * GENERAL ARCHITECTURE AND OPERATION:
 * 
 * The benchmark system consists of several cooperating components:
 * 
 * 1. **BenchmarkManager** (this class):
 *    - Main coordinator that orchestrates the entire benchmarking process
 *    - Manages the benchmark thread that collects metrics every second
 *    - Decides when to write data to CSV files based on state tracker input
 *    - Sends real-time metrics to UI for live display
 * 
 * 2. **BenchmarkStateTracker**:
 *    - Responsible for detecting benchmark start/end automatically
 *    - Uses RustLogMonitor to detect game patterns in log files
 *    - Provides states: OFF, WAITING, RUNNING, COOLDOWN
 *    - Only during RUNNING state should CSV data be collected
 * 
 * 3. **Metrics Providers** (collect system/hardware data):
 *    - PdhInterface: Windows Performance Toolkit for CPU, memory, disk metrics
 *    - NvidiaMetricsCollector: GPU temperature, utilization, memory usage
 *    - CPUKernelMetricsTracker: Low-level CPU interrupt/DPC metrics
 *    - DiskPerformanceTracker: Disk latency and throughput metrics
 *    - PresentMon (ETW): Game frame timing, FPS, render times
 * 
 * 4. **Data Flow**:
 *    - **WAITING Phase**: 
 *      * All metrics providers run and collect data
 *      * Data is sent to UI for live display via benchmarkMetrics signal
 *      * NO data is written to CSV files
 *      * User can see live system status while waiting for benchmark
 *    
 *    - **RUNNING Phase** (when StateTracker detects game benchmark):
 *      * All metrics providers continue running
 *      * Data is sent to UI for live display
 *      * Data is also accumulated and written to CSV files
 *      * This is the "actual benchmark data" that gets saved
 *    
 *    - **COOLDOWN Phase**:
 *      * Metrics collection stops
 *      * Final CSV file is written with only RUNNING phase data
 *      * Optimization settings and system specs are exported
 * 
 * 5. **Data Storage**:
 *    - **currentData** (BenchmarkDataPoint): Latest metrics from all providers
 *    - **allData** (vector): Historical data points, only populated during RUNNING
 *    - **CSV File**: Contains only the RUNNING phase data (the actual benchmark)
 *    - **Specs File**: System hardware and software configuration
 *    - **Rust JSON**: Game's internal benchmark data (copied from game folder)
 * 
 * 6. **UI Communication**:
 *    - benchmarkMetrics signal: Real-time frame data for live display
 *    - getLatestDataPoint(): UI pulls current system metrics (CPU, memory, GPU)
 *    - benchmarkStateChanged: Updates UI with current phase (WAITING/RUNNING/etc)
 * 
 * This separation ensures that:
 * - Users get immediate feedback about system performance
 * - Only actual benchmark data is saved to files
 * - System maintains responsiveness during long benchmark runs
 * - Multiple data sources are properly synchronized
 */

class BenchmarkManager : public QObject {
  Q_OBJECT
 public:
  explicit BenchmarkManager(QObject* parent = nullptr);
  ~BenchmarkManager();
  bool startBenchmark(const QString& processName, int durationSeconds = 60);
  bool stopBenchmark();
  void emitUIMetrics();  // Consistent 1-second UI updates
  void setSaveToFile(bool save);

  // Get actual benchmark duration
  std::pair<std::chrono::steady_clock::time_point, std::chrono::steady_clock::time_point> 
  getActualBenchmarkTimes() const {
    return {getBenchmarkStartTime(), getBenchmarkEndTime()};
  }

  bool isActivelyBenchmarking() const {
    return m_currentProcessId != 0;
  }

  // Get the latest data point (for UI display)
  BenchmarkDataPoint getLatestDataPoint() const {
    std::lock_guard<std::recursive_mutex> lock(const_cast<std::recursive_mutex&>(dataMutex));
    return currentData;
  }

  // Get cumulative frame time percentiles (for UI display)
  float getCumulativeFrameTime1Pct() const { return m_cumulativeFrameTime1pct; }
  float getCumulativeFrameTime5Pct() const { return m_cumulativeFrameTime5pct; }
  float getCumulativeFrameTime05Pct() const { return m_cumulativeFrameTime05pct; }

 signals:
  void benchmarkProgress(int percentage);
  void benchmarkMetrics(const PM_METRICS& metrics);
  void benchmarkSample(const BenchmarkDataPoint& sample);  // New coherent data signal
  void benchmarkFinished();
  void benchmarkError(const QString& error);
  void benchmarkWarning(const QString& warning);
  void benchmarkStateChanged(const QString& state);
  void benchmarkStatus(const QString& status, bool isError);

  void nvencUsageDetected(bool isActive);

 private:
  void cleanup();
  bool cleanupExistingETWSessions();
  bool cleanupSystemETW();
  uint32_t getProcessIdByName(const QString& processName);
  bool restartWithElevation();
  std::atomic<bool> m_shouldStop{false};
  std::thread benchmarkThread;
  uint32_t m_currentProcessId{0};

  // Provider caches for coherent data assembly
  struct PmCache {
    float fps = 0.0f;
    float frameTime = 0.0f;
    float gpuRenderTime = 0.0f;
    float cpuRenderTime = 0.0f;
    float highestFrameTime = 0.0f;
    float highest5PctFrameTime = 0.0f;     // Per-second highest 5% frametime for CSV export
    float highestGpuTime = 0.0f;
    float highestCpuTime = 0.0f;
    float fpsVariance = 0.0f;
    float lowFps1Percent = 0.0f;           // These are per-second percentiles from PresentMon
    float lowFps5Percent = 0.0f;
    float lowFps05Percent = 0.0f;
    uint32_t destWidth = 0;
    uint32_t destHeight = 0;
    uint32_t presentCount = 0;
    std::chrono::steady_clock::time_point lastTimestamp;
  };

  struct PdhCache {
    // CPU metrics
    double procProcessorTime = -1.0;
    double procUserTime = -1.0;
    double procPrivilegedTime = -1.0;
    double procIdleTime = -1.0;
    double procActualFreq = -1.0;
    double cpuInterruptsPerSec = -1.0;
    double cpuDpcTime = -1.0;
    double cpuInterruptTime = -1.0;
    double cpuDpcsQueuedPerSec = -1.0;
    double cpuDpcRate = -1.0;
    double cpuC1Time = -1.0;
    double cpuC2Time = -1.0;
    double cpuC3Time = -1.0;
    double cpuC1TransitionsPerSec = -1.0;
    double cpuC2TransitionsPerSec = -1.0;
    double cpuC3TransitionsPerSec = -1.0;
    
    // Memory metrics
    double availableMemoryMB = -1.0;
    double memoryLoad = -1.0;
    double memoryCommittedBytes = -1.0;
    double memoryCommitLimit = -1.0;
    double memoryFaultsPerSec = -1.0;
    double memoryPagesPerSec = -1.0;
    double memoryPoolNonPagedBytes = -1.0;
    double memoryPoolPagedBytes = -1.0;
    double memorySystemCodeBytes = -1.0;
    double memorySystemDriverBytes = -1.0;
    
    // Disk metrics
    double ioReadRateMBs = -1.0;
    double ioWriteRateMBs = -1.0;
    double diskReadsPerSec = -1.0;
    double diskWritesPerSec = -1.0;
    double diskTransfersPerSec = -1.0;
    double diskBytesPerSec = -1.0;
    double diskAvgReadQueueLength = -1.0;
    double diskAvgWriteQueueLength = -1.0;
    double diskAvgQueueLength = -1.0;
    double diskAvgReadTime = -1.0;
    double diskAvgWriteTime = -1.0;
    double diskAvgTransferTime = -1.0;
    double diskPercentTime = -1.0;
    double diskPercentReadTime = -1.0;
    double diskPercentWriteTime = -1.0;
    
    // System metrics
    double contextSwitchesPerSec = -1.0;
    double systemProcessorQueueLength = -1.0;
    double systemProcesses = -1.0;
    double systemThreads = -1.0;
    double pdhInterruptsPerSec = -1.0;
    
    // Per-core metrics (vectors)
    std::vector<double> perCoreCpuUsage;
    std::vector<double> perCoreActualFreq;
    
    // Timestamp
    std::chrono::steady_clock::time_point lastTimestamp;
  };

  struct NvCache {
    float gpuTemperature = 0.0f;
    float gpuPowerUsage = 0.0f;
    float gpuMemoryUsage = 0.0f;
    float gpuCoreUtilization = 0.0f;
    float gpuMemoryUtilization = 0.0f;
    unsigned long long gpuMemUsed = 0;     // Raw used memory in bytes
    unsigned long long gpuMemTotal = 0;    // Raw total memory in bytes
    
    // Additional GPU metrics that were missing
    unsigned int gpuClock = 0;             // GPU core clock in MHz
    unsigned int gpuMemClock = 0;          // GPU memory clock in MHz
    unsigned int gpuFanSpeed = 0;          // Fan speed percentage
    unsigned int gpuSmUtilization = 0;     // SM utilization percentage
    unsigned int gpuMemBandwidthUtil = 0;  // Memory bandwidth utilization
    unsigned int gpuPcieRxThroughput = 0;  // PCIe receive throughput
    unsigned int gpuPcieTxThroughput = 0;  // PCIe transmit throughput
    unsigned int gpuNvdecUtil = 0;         // NVDEC utilization
    unsigned int gpuNvencUtil = 0;         // NVENC utilization
    bool gpuThrottling = false;            // Thermal throttling status
    
    std::chrono::steady_clock::time_point lastTimestamp;
  };

  PmCache pmCache;
  PdhCache pdhCache;
  NvCache nvCache;
  std::mutex pmMutex;
  std::mutex pdhMutex;
  std::mutex nvMutex;

  BenchmarkDataPoint currentData;  // Now read-only lastCommittedSample
  BenchmarkDataPoint lastCommittedSample;
  std::vector<BenchmarkDataPoint> allData;
  std::chrono::steady_clock::time_point startTime;
  std::chrono::steady_clock::time_point benchmarkStartTime;
  bool saveToFile = false;
  std::recursive_mutex dataMutex;

  void accumulateMetrics(const PM_METRICS& metrics);
  
  static constexpr int BATCH_SIZE_SECONDS = 5;  // Write every 5 seconds

  void updateBenchmarkState(const PM_METRICS& metrics);

  friend void ::OnMetricsUpdate(uint32_t processId, const PM_METRICS* metrics);

  // SystemInfoManager removed - using ConstantSystemInfo instead
  std::unique_ptr<BenchmarkStateTracker> m_stateTracker;
  BenchmarkStateTracker::State currentBenchmarkState =
    BenchmarkStateTracker::State::OFF;

  std::chrono::steady_clock::time_point getBenchmarkStartTime() const;
  std::chrono::steady_clock::time_point getBenchmarkEndTime() const;

  void copyRustBenchmarkFiles();

  std::unique_ptr<NvidiaMetricsCollector> m_gpuMetrics;

  // File output members - now managed by BenchmarkResultFileManager
  std::unique_ptr<BenchmarkResultFileManager> m_resultFileManager;
  QString m_outputFilename;
  
  QString m_benchmarkHash;
  bool m_finalWriteDone = false;
  bool m_firstWriteNeeded = true;
  std::atomic<bool> m_cleanupDone = false;  // Prevent double cleanup
  std::atomic<bool> m_stopBenchmarkCalled = false;  // Prevent calling stopBenchmark multiple times
  std::atomic<bool> m_benchmarkEndDetected = false;  // Signal from StateTracker that benchmark ended
  std::atomic<bool> m_nvencUsageActive{false};  // Tracks NVENC-based capture detection
  
  // Simple timing for data collection (since we now detect start AND end)
  std::chrono::steady_clock::time_point m_benchmarkStartTime;
  std::chrono::steady_clock::time_point m_benchmarkEndTime;

  std::unique_ptr<DiskPerformanceTracker> m_diskTracker;


  struct FrameTimePoint {
    float fps;
    float frameTime;
    float timestamp;  // seconds since start of benchmark
  };
  std::vector<FrameTimePoint> allFrameTimePoints;  // Store all frame times
  std::mutex frameTimesMutex;  // Mutex for protecting frame time data

  void calculateCumulativeFrameTimePercentiles();

  // Optimized histogram for frame time percentile calculation
  struct FrameTimeHistogram {
    static constexpr float MIN_FRAME_TIME = 1.0f;    // 1ms (1000 FPS)
    static constexpr float MAX_FRAME_TIME = 200.0f;  // 200ms (5 FPS)
    static constexpr float BUCKET_SIZE = 0.5f;       // 0.5ms bucket granularity
    static constexpr size_t BUCKET_COUNT = static_cast<size_t>((MAX_FRAME_TIME - MIN_FRAME_TIME) / BUCKET_SIZE) + 1;
    
    std::array<uint32_t, BUCKET_COUNT> buckets{};
    uint32_t totalSamples = 0;
    uint32_t underflowCount = 0;  // samples < MIN_FRAME_TIME
    uint32_t overflowCount = 0;   // samples > MAX_FRAME_TIME
    
    void addFrameTime(float frameTime) {
      if (frameTime < MIN_FRAME_TIME) {
        underflowCount++;
      } else if (frameTime > MAX_FRAME_TIME) {
        overflowCount++;
      } else {
        size_t bucketIndex = static_cast<size_t>((frameTime - MIN_FRAME_TIME) / BUCKET_SIZE);
        bucketIndex = std::min(bucketIndex, BUCKET_COUNT - 1);
        buckets[bucketIndex]++;
      }
      totalSamples++;
    }
    
    // Batch add frame times to avoid per-frame loops
    void addFrameTime(float frameTime, uint32_t count) {
      if (frameTime < MIN_FRAME_TIME) {
        underflowCount += count;
      } else if (frameTime > MAX_FRAME_TIME) {
        overflowCount += count;
      } else {
        size_t bucketIndex = static_cast<size_t>((frameTime - MIN_FRAME_TIME) / BUCKET_SIZE);
        bucketIndex = std::min(bucketIndex, BUCKET_COUNT - 1);
        buckets[bucketIndex] += count;
      }
      totalSamples += count;
    }
    
    float calculatePercentile(float percentile) const {
      if (totalSamples == 0) return -1.0f;
      
      uint32_t targetSample = static_cast<uint32_t>(totalSamples * (percentile / 100.0f));
      targetSample = std::min(targetSample, totalSamples - 1);
      
      uint32_t cumulativeCount = underflowCount;
      
      // Check if target is in underflow
      if (targetSample < cumulativeCount) {
        return MIN_FRAME_TIME / 2.0f; // Return estimate for underflow
      }
      
      // Search through histogram buckets
      for (size_t i = 0; i < BUCKET_COUNT; ++i) {
        cumulativeCount += buckets[i];
        if (targetSample < cumulativeCount) {
          return MIN_FRAME_TIME + (i * BUCKET_SIZE) + (BUCKET_SIZE / 2.0f);
        }
      }
      
      // Must be in overflow
      return MAX_FRAME_TIME * 1.5f; // Return estimate for overflow
    }
    
    void clear() {
      buckets.fill(0);
      totalSamples = 0;
      underflowCount = 0;
      overflowCount = 0;
    }
  };
  
  FrameTimeHistogram frameTimeHistogram;

  // Cumulative frame time percentiles (for UI display)
  float m_cumulativeFrameTime1pct = -1.0f;
  float m_cumulativeFrameTime5pct = -1.0f;
  float m_cumulativeFrameTime05pct = -1.0f;

  std::unique_ptr<CPUKernelMetricsTracker> m_cpuKernelTracker;

  QString m_userSystemId;

  // FPS data tracking (still needed for some calculations)
  struct FpsDataPoint {
    float fps;
    float timestamp;  // Store timestamp with each FPS sample
  };
  std::vector<FpsDataPoint> allFpsSamples;
  std::mutex fpsValuesMutex;

  // Replace ProcessorMetrics with PdhInterface
  std::unique_ptr<PdhInterface> m_pdhInterface;
  std::chrono::steady_clock::time_point m_lastPdhMetricsLog;

  // Helper for PDH metrics accumulation
  void accumulatePdhMetrics();
  
  // Benchmark lifecycle handlers
  void handleBenchmarkStart();
  void handleBenchmarkEnd();

  // Demo file management
  std::unique_ptr<DemoFileManager> m_demoManager;

  // Automatic upload functionality
  void performAutomaticUpload();
};
