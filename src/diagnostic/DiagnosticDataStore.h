#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QString>

// Forward declaration
class DiagnosticWorker;

class DiagnosticDataStore {
 public:
  // Define a progress callback function type
  using ProgressCallback = std::function<void(const QString&, int)>;

  // Singleton access method
  static DiagnosticDataStore& getInstance();

  // Reset all values to defaults (-1 or "no_data")
  void resetAllValues();

  // Safely reset access between runs
  void safelyResetAccess();

  // Memory data structure remains unchanged
  struct MemoryData {
    // Test results
    double bandwidth = -1.0;
    double latency = -1.0;
    double writeTime = -1.0;
    double readTime = -1.0;

    // System memory info
    double totalMemoryGB = -1.0;
    double availableMemoryGB = -1.0;
    std::string memoryType = "";

    // Module information
    struct MemoryModule {
      int slot = -1;
      std::string memoryType = "";
      int speedMHz = -1;
      int configuredSpeedMHz = -1;
      std::string manufacturer = "";
      std::string partNumber = "";
      double capacityGB = -1.0;
      std::string xmpStatus = "";
      std::string deviceLocator = "";
    };

    std::vector<MemoryModule> modules;
    std::string channelStatus = "";
    bool xmpEnabled = false;

    struct PageFileLocation {
      std::string drive;
      double currentSizeMB = 0;
      double maxSizeMB = 0;
    };

    struct PageFileInfo {
      bool exists = false;
      bool systemManaged = false;
      double totalSizeMB = 0;
      std::string primaryDrive;
      std::vector<PageFileLocation> locations;
    };

    PageFileInfo pageFile;

    // Memory stability test results
    struct StabilityTestResults {
      bool testPerformed = false;
      bool passed = false;
      int errorCount = 0;
      int completedLoops = 0;
      int completedPatterns = 0;
      size_t testedSizeMB = 0;
    };

    StabilityTestResults stabilityTest;
  };

  // Enhanced CPU data structure
  struct CPUData {
    // Basic CPU info
    std::string name = "no_data";
    int physicalCores = -1;
    int threadCount = -1;

    // Performance metrics
    double simdScalar = -1.0;
    double simdAvx = -1.0;
    double primeTime = -1.0;
    double singleCoreTime = -1.0;
    double fourThreadTime = -1.0;

    // Game simulation results
    double gameSimUPS_small = -1.0;
    double gameSimUPS_medium = -1.0;
    double gameSimUPS_large = -1.0;

    // Current CPU state
    unsigned int currentClockSpeed = 0;
    unsigned int maxClockSpeed = 0;
    double currentVoltage = -1.0;
    unsigned int loadPercentage = 0;
    unsigned int thermalStatus = 0;

    // Cache information
    struct CacheData {
      // Increase array size to 12 to ensure we can store all values
      double latencies[12] = {-1.0, -1.0, -1.0, -1.0, -1.0, -1.0,
                              -1.0, -1.0, -1.0, -1.0, -1.0, -1.0};
      int l1SizeKB = -1;
      int l2SizeKB = -1;
      int l3SizeKB = -1;
      bool hyperThreadingEnabled = false;

      // Median latencies
      double l1LatencyNs = -1.0;
      double l2LatencyNs = -1.0;
      double l3LatencyNs = -1.0;
      double ramLatencyNs = -1.0;

      std::map<size_t, double> rawLatencies;
    };

    CacheData cache;

    // C-State power management data
    struct CStateData {
      double c1TimePercent = -1.0;        // % C1 Time
      double c2TimePercent = -1.0;        // % C2 Time
      double c3TimePercent = -1.0;        // % C3 Time
      double c1TransitionsPerSec = -1.0;  // C1 Transitions/sec
      double c2TransitionsPerSec = -1.0;  // C2 Transitions/sec
      double c3TransitionsPerSec = -1.0;  // C3 Transitions/sec
      bool cStatesEnabled = false;  // Derived: true if C2/C3 usage detected
      double totalIdleTime = -1.0;  // Total time in idle states
      double powerEfficiencyScore =
        -1.0;  // 0-100 score for power management effectiveness
    };

    CStateData cStates;

    // Per-core metrics
    struct CoreMetrics {
      int coreId = -1;
      int clockMHz = -1;
      double loadPercent = -1.0;
      double temperatureC = -1.0;
      double powerW = -1.0;
    };
    std::vector<CoreMetrics> coreMetrics;

    // Boost behavior metrics
    struct BoostMetrics {
      int idleClock = -1;
      int singleLoadClock = -1;
      int allCoreClock = -1;
      double boostDeltaMHz = -1.0;
      double power = -1.0;
    };
    std::vector<BoostMetrics> boostMetrics;

    // Overall boost summary
    double idleTotalPower = -1.0;
    double singleCoreTotalPower = -1.0;
    double allCoreTotalPower = -1.0;
    int bestBoostCore = -1;
    int maxBoostDelta = -1;

    // Power throttling data
    bool throttlingDetected = false;
    double peakClock = -1.0;
    double sustainedClock = -1.0;
    double clockDropPercent = -1.0;
    int throttlingDetectedTime = -1;

    // Cold start response metrics
    struct ColdStartMetrics {
      double avgResponseTimeUs = -1.0;
      double minResponseTimeUs = -1.0;
      double maxResponseTimeUs = -1.0;
      double stdDevUs = -1.0;
      double varianceUs = -1.0;
    };
    ColdStartMetrics coldStart;
  };

  // Add GPU data structure
  struct GPUData {
    // Basic GPU info
    std::string name = "no_data";
    std::string driverVersion = "no_data";

    // Performance metrics
    float averageFPS = -1.0f;
    int totalFrames = -1;

    // Additional metrics
    float renderTimeMs = -1.0f;
  };

  // Add DriveData structure
  struct DriveData {
    struct DriveMetrics {
      std::string drivePath;
      double seqRead = -1.0;
      double seqWrite = -1.0;
      double iops4k = -1.0;
      double accessTimeMs = -1.0;
    };
    std::vector<DriveMetrics> drives;
  };

  // Add BackgroundProcessData structure
  struct BackgroundProcessData {
    double systemCpuUsage = -1.0;
    double systemGpuUsage = -1.0;
    double systemDpcTime = -1.0;
    double systemInterruptTime = -1.0;
    double peakSystemDpcTime = -1.0;        // Add peak DPC time
    double peakSystemInterruptTime = -1.0;  // Add peak interrupt time
    double peakSystemCpuUsage = -1.0;       // Add peak CPU usage
    double peakSystemGpuUsage = -1.0;       // Add peak GPU usage
    double systemDiskIO = -1.0;             // Add disk I/O usage
    double peakSystemDiskIO = -1.0;         // Add peak disk I/O usage
    bool hasDpcLatencyIssues = false;

    // Added memory metrics
    uint64_t physicalTotalKB = 0;      // Total physical RAM in KB
    uint64_t physicalAvailableKB = 0;  // Available physical RAM in KB
    uint64_t commitTotalKB = 0;        // Committed virtual memory in KB
    uint64_t commitLimitKB = 0;        // Commit limit in KB
    uint64_t kernelPagedKB = 0;        // Kernel paged pool in KB
    uint64_t kernelNonPagedKB = 0;     // Kernel non-paged pool in KB
    uint64_t systemCacheKB = 0;        // System cache resident in KB
    uint64_t userModePrivateKB =
      0;                         // Sum of process private working sets in KB
    uint64_t otherMemoryKB = 0;  // Unaccounted memory in KB

    struct ProcessInfo {
      std::string name;
      double cpuPercent = -1.0;
      double peakCpuPercent = -1.0;
      size_t memoryUsageKB = 0;
      double gpuPercent = -1.0;
      int instanceCount = 1;
    };

    std::vector<ProcessInfo> topCpuProcesses;
    std::vector<ProcessInfo> topMemoryProcesses;
    std::vector<ProcessInfo> topGpuProcesses;
  };

  // Cross-user aggregated background process metrics (from /pb/diagnostics/general).
  // Used for "typical" comparison rows in UI renderers.
  struct BackgroundProcessGeneralMetrics {
    double totalCpuUsage = -1.0;
    double totalGpuUsage = -1.0;
    double systemDpcTime = -1.0;
    double systemInterruptTime = -1.0;

    struct MemoryMetrics {
      double commitLimitMB = -1.0;
      double commitPercent = -1.0;
      double commitTotalMB = -1.0;
      double fileCacheMB = -1.0;
      double kernelNonPagedMB = -1.0;
      double kernelPagedMB = -1.0;
      double kernelTotalMB = -1.0;
      double otherMemoryMB = -1.0;
      double physicalAvailableMB = -1.0;
      double physicalTotalMB = -1.0;
      double physicalUsedMB = -1.0;
      double physicalUsedPercent = -1.0;
      double userModePrivateMB = -1.0;
    };

    MemoryMetrics memoryMetrics;

    struct MemoryMetricsByRamBin {
      double totalMemoryGB = -1.0;
      int sampleCount = 0;
      MemoryMetrics metrics;
    };

    std::vector<MemoryMetricsByRamBin> memoryMetricsByRam;
  };

  // Add NetworkData structure
  struct NetworkData {
    bool onWifi = false;
    double averageLatencyMs = -1.0;
    double averageJitterMs = -1.0;
    double averagePacketLoss = -1.0;
    double baselineLatencyMs = -1.0;
    double downloadLatencyMs = -1.0;
    double uploadLatencyMs = -1.0;
    bool hasBufferbloat = false;
    std::string networkIssues;

    struct RegionalLatency {
      std::string region;
      double latencyMs = -1.0;
    };

    struct ServerResult {
      std::string hostname;
      std::string ipAddress;
      std::string region;
      double minLatencyMs = -1.0;
      double maxLatencyMs = -1.0;
      double avgLatencyMs = -1.0;
      double jitterMs = -1.0;
      double packetLossPercent = -1.0;
      int sentPackets = 0;
      int receivedPackets = 0;
    };

    std::vector<RegionalLatency> regionalLatencies;
    std::vector<ServerResult> serverResults;
  };

  // Getters for data structures - update all getters to check the reset flag
  const MemoryData& getMemoryData() const {
    if (needsReset) {
      static MemoryData emptyData;
      return emptyData;
    }
    return memoryData;
  }
  void setMemoryData(const MemoryData& data) { memoryData = data; }

  const CPUData& getCPUData() const {
    if (needsReset) {
      static CPUData emptyData;
      return emptyData;
    }
    return cpuData;
  }
  void setCPUData(const CPUData& data) { cpuData = data; }

  // GPU data getters/setters
  const GPUData& getGPUData() const {
    if (needsReset) {
      static GPUData emptyData;
      return emptyData;
    }
    return gpuData;
  }
  void setGPUData(const GPUData& data) { gpuData = data; }

  // Drive data getters/setters
  const DriveData& getDriveData() const {
    if (needsReset) {
      static DriveData emptyData;
      return emptyData;
    }
    return driveData;
  }
  void setDriveData(const DriveData& data) { driveData = data; }

  // Background process data getters/setters
  const BackgroundProcessData& getBackgroundProcessData() const {
    if (needsReset) {
      static BackgroundProcessData emptyData;
      return emptyData;
    }
    return backgroundData;
  }
  void setBackgroundProcessData(const BackgroundProcessData& data) {
    backgroundData = data;
  }

  // Background process comparison data (cross-user aggregate)
  const BackgroundProcessGeneralMetrics& getGeneralBackgroundProcessMetrics() const {
    if (needsReset) {
      static BackgroundProcessGeneralMetrics emptyData;
      return emptyData;
    }
    return backgroundGeneralMetrics;
  }
  void setGeneralBackgroundProcessMetrics(const BackgroundProcessGeneralMetrics& data) {
    backgroundGeneralMetrics = data;
  }

  // Network data getters/setters
  const NetworkData& getNetworkData() const {
    if (needsReset) {
      static NetworkData emptyData;
      return emptyData;
    }
    return networkData;
  }
  void setNetworkData(const NetworkData& data) { networkData = data; }

  void updateMemoryPerformanceMetrics(double bandwidth, double latency,
                                      double writeBandwidth,
                                      double readBandwidth);

  // Keep existing method for backward compatibility but mark as deprecated
  void updateFromCPUMetrics(const double simdScalar, const double simdAvx,
                            const double primeTime, const double singleCoreTime,
                            const double multiCoreTime,
                            const double gameSimSmall,
                            const double gameSimMedium,
                            const double gameSimLarge);

  // New direct update methods
  void updateCPUBasicInfo(const std::string& name, int physicalCores,
                          int threadCount);
  void updateCPUPerformanceMetrics(double simdScalar, double simdAvx,
                                   double primeTime, double singleCoreTime,
                                   double fourThreadTime = -1.0);
  void updateCPUGameSimResults(double smallUPS, double mediumUPS,
                               double largeUPS);
  void updateCPUCacheLatencies(const double* latencies, int l1SizeKB,
                               int l2SizeKB, int l3SizeKB);
  void updateCPUCoreMetrics(const std::vector<CPUData::CoreMetrics>& metrics);
  void updateCPUBoostMetrics(const std::vector<CPUData::BoostMetrics>& metrics,
                             double idlePower, double singleCorePower,
                             double allCorePower, int bestCore, int maxDelta);
  void updateCPUThrottlingInfo(bool detected, double peakClock,
                               double sustainedClock, double dropPercent,
                               int detectedTime);

  // Add method to update C-state information
  void updateCPUCStateData(double c1Time, double c2Time, double c3Time,
                           double c1Transitions, double c2Transitions,
                           double c3Transitions);

  // Add method to update GPU metrics
  void updateGPUMetrics(float averageFPS, int totalFrames,
                        float renderTimeMs = -1.0f) {
    std::lock_guard<std::mutex> lock(dataMutex);
    gpuData.averageFPS = averageFPS;
    gpuData.totalFrames = totalFrames;
    gpuData.renderTimeMs = renderTimeMs;
  }

  // Add method to update Drive metrics
  void updateDriveMetrics(const std::string& drivePath, double seqRead,
                          double seqWrite, double iops4k, double accessTimeMs) {
    std::lock_guard<std::mutex> lock(dataMutex);

    // Look for existing drive entry
    for (auto& drive : driveData.drives) {
      if (drive.drivePath == drivePath) {
        // Update existing entry
        drive.seqRead = seqRead;
        drive.seqWrite = seqWrite;
        drive.iops4k = iops4k;
        drive.accessTimeMs = accessTimeMs;
        return;
      }
    }

    // Add new entry if not found
    DriveData::DriveMetrics newDrive;
    newDrive.drivePath = drivePath;
    newDrive.seqRead = seqRead;
    newDrive.seqWrite = seqWrite;
    newDrive.iops4k = iops4k;
    newDrive.accessTimeMs = accessTimeMs;
    driveData.drives.push_back(newDrive);
  }

  // Add method to update BackgroundProcessData
  void updateBackgroundProcessData(
    double cpuUsage, double gpuUsage, double dpcTime, double interruptTime,
    bool hasLatencyIssues,
    const std::vector<BackgroundProcessData::ProcessInfo>& topCpu,
    const std::vector<BackgroundProcessData::ProcessInfo>& topMemory,
    const std::vector<BackgroundProcessData::ProcessInfo>& topGpu,
    uint64_t physicalTotalKB = 0, uint64_t physicalAvailableKB = 0,
    uint64_t commitTotalKB = 0, uint64_t commitLimitKB = 0,
    uint64_t kernelPagedKB = 0, uint64_t kernelNonPagedKB = 0,
    uint64_t systemCacheKB = 0, uint64_t userModePrivateKB = 0,
    uint64_t otherMemoryKB = 0,
    double peakDpcTime = -1.0,        // Add peak DPC time parameter
    double peakInterruptTime = -1.0,  // Add peak interrupt time parameter
    double peakCpuUsage = -1.0,       // Add peak CPU usage parameter
    double peakGpuUsage = -1.0,       // Add peak GPU usage parameter
    double diskIO = -1.0,             // Add disk I/O parameter
    double peakDiskIO = -1.0);        // Add peak disk I/O parameter

  // Add method to update network metrics
  void updateNetworkData(const NetworkData& data) {
    std::lock_guard<std::mutex> lock(dataMutex);
    networkData = data;
  }

  // Memory module updates remain unchanged
  void setMemoryModules(
    const std::vector<std::map<std::string, std::string>>& modules);
  void setChannelStatus(const std::string& status);

  void updateMemoryHardwareInfo(
    const std::vector<MemoryData::MemoryModule>& modules,
    const std::string& memoryType, const std::string& channelStatus,
    bool xmpEnabled);

  void updatePageFileInfo(const MemoryData::PageFileInfo& pageFileInfo) {
    std::lock_guard<std::mutex> lock(dataMutex);
    memoryData.pageFile = pageFileInfo;
  }

  // Add this declaration to the public section of the DiagnosticDataStore class
  void updateFromMemoryMetrics(const MemoryData& metrics);

  // Add this to the public section of the DiagnosticDataStore class
  void updateMemoryStabilityResults(
    const MemoryData::StabilityTestResults& results) {
    std::lock_guard<std::mutex> lock(dataMutex);
    memoryData.stabilityTest = results;
  }

  // Add this getter for our mutex to allow safe updates
  std::mutex& getDataMutex() { return dataMutex; }

  // Set and get progress callback
  void setEmitProgressCallback(ProgressCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_progressCallback = callback;
  }

  ProgressCallback getEmitProgressCallback() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_progressCallback;
  }

 private:
  // Private constructor for singleton
  DiagnosticDataStore();

  // Delete copy/move constructors and assignment operators
  DiagnosticDataStore(const DiagnosticDataStore&) = delete;
  DiagnosticDataStore& operator=(const DiagnosticDataStore&) = delete;
  DiagnosticDataStore(DiagnosticDataStore&&) = delete;
  DiagnosticDataStore& operator=(DiagnosticDataStore&&) = delete;

  // Data members
  MemoryData memoryData;
  CPUData cpuData;

  // Add GPUData member
  GPUData gpuData;

  // Add DriveData member
  DriveData driveData;

  // Add BackgroundProcessData member
  BackgroundProcessData backgroundData;

  // Background process comparison data (cross-user aggregate)
  BackgroundProcessGeneralMetrics backgroundGeneralMetrics;

  // Add NetworkData member
  NetworkData networkData;

  // Add mutex for thread safety
  std::mutex dataMutex;

  // Progress callback
  ProgressCallback m_progressCallback;

  // Thread safety
  mutable std::mutex m_mutex;

  // Flag to indicate data needs to be reset before access
  bool needsReset = false;
};
