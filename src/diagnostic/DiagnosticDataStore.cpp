#include "DiagnosticDataStore.h"
#include "../logging/Logger.h"

#include <iostream>

DiagnosticDataStore& DiagnosticDataStore::getInstance() {
  static DiagnosticDataStore instance;
  return instance;
}

DiagnosticDataStore::DiagnosticDataStore() { resetAllValues(); }

void DiagnosticDataStore::resetAllValues() {
  std::lock_guard<std::mutex> lock(dataMutex);

  // Reset memory data
  memoryData = MemoryData();

  // Reset CPU data
  cpuData = CPUData();

  // Reset GPU data
  gpuData = GPUData();

  // Reset cross-user background comparison metrics
  backgroundGeneralMetrics = BackgroundProcessGeneralMetrics();

  // Reset background process data
  backgroundData = BackgroundProcessData();

  // Reset network data
  networkData = NetworkData();
  networkData.regionalLatencies.clear();
  networkData.serverResults.clear();

  // Explicitly set any non-default values that need special handling
  for (int i = 0; i < 11; i++) {
    cpuData.cache.latencies[i] = -1.0;
  }

  // Clear collections
  cpuData.coreMetrics.clear();
  cpuData.boostMetrics.clear();
  cpuData.cache.rawLatencies.clear();
  memoryData.modules.clear();
  memoryData.pageFile.locations.clear();

  LOG_INFO << "DiagnosticDataStore reset - all values initialized to defaults";

  // Reset the reset flag - we've just reset everything
  needsReset = false;
}

// Add a new method to safely reset the access state between runs
void DiagnosticDataStore::safelyResetAccess() {
  std::lock_guard<std::mutex> lock(dataMutex);

  // Set the reset flag to indicate data is in an invalid state
  // This will cause getters to return minimal placeholder data until
  // resetAllValues() is called
  needsReset = true;

  LOG_INFO << "DiagnosticDataStore marked for reset - access will be limited until resetAllValues() is called";
}

void DiagnosticDataStore::updateMemoryPerformanceMetrics(double bandwidth,
                                                         double latency,
                                                         double writeBandwidth,
                                                         double readBandwidth) {
  std::lock_guard<std::mutex> lock(dataMutex);

  // Only log at a high level
  LOG_INFO << "Updating memory performance metrics: " << bandwidth << " MB/s, " << latency << " ns";

  // Store the existing modules and other info
  auto existingModules = memoryData.modules;
  std::string existingMemoryType = memoryData.memoryType;
  std::string existingChannelStatus = memoryData.channelStatus;
  bool existingXmpEnabled = memoryData.xmpEnabled;

  // Update the performance metrics only, don't overwrite the module data
  memoryData.bandwidth = bandwidth;
  memoryData.latency = latency;
  memoryData.writeTime = writeBandwidth;
  memoryData.readTime = readBandwidth;

  // Restore all existing memory configuration data
  memoryData.modules = existingModules;
  memoryData.memoryType = existingMemoryType;
  memoryData.channelStatus = existingChannelStatus;
  memoryData.xmpEnabled = existingXmpEnabled;
}

// Update the updateFromCPUMetrics implementation to handle the new data
// structure

void DiagnosticDataStore::updateFromCPUMetrics(
  const double simdScalar, const double simdAvx, const double primeTime,
  const double singleCoreTime, const double multiCoreTime,
  const double gameSimSmall, const double gameSimMedium,
  const double gameSimLarge) {

  std::lock_guard<std::mutex> lock(dataMutex);

  cpuData.simdScalar = simdScalar;
  cpuData.simdAvx = simdAvx;
  cpuData.primeTime = primeTime;
  cpuData.singleCoreTime = singleCoreTime;
  // Maintain backward compatibility but don't update anything
  cpuData.gameSimUPS_small = gameSimSmall;
  cpuData.gameSimUPS_medium = gameSimMedium;
  cpuData.gameSimUPS_large = gameSimLarge;
}

void DiagnosticDataStore::setMemoryModules(
  const std::vector<std::map<std::string, std::string>>& modules) {
  memoryData.modules.clear();

  for (const auto& moduleMap : modules) {
    MemoryData::MemoryModule module;

    // Helper function to safely get values
    auto getValue = [&moduleMap](const std::string& key) -> std::string {
      auto it = moduleMap.find(key);
      return (it != moduleMap.end()) ? it->second : "";
    };

    try {
      // Parse slot number
      std::string slotStr = getValue("slot");
      if (!slotStr.empty()) {
        module.slot = std::stoi(slotStr);
      }

      // Get memory type
      module.memoryType = getValue("memory_type");

      // Parse speeds
      std::string speedStr = getValue("speed_mhz");
      if (!speedStr.empty()) {
        module.speedMHz = std::stoi(speedStr);
      }

      std::string configSpeedStr = getValue("configured_clock_speed_mhz");
      if (!configSpeedStr.empty()) {
        module.configuredSpeedMHz = std::stoi(configSpeedStr);
      }

      // Get manufacturer and part number
      module.manufacturer = getValue("manufacturer");
      module.partNumber = getValue("part_number");

      // Parse capacity
      std::string capacityStr = getValue("capacity_gb");
      if (!capacityStr.empty()) {
        module.capacityGB = std::stod(capacityStr);
      }

      // Get XMP status
      module.xmpStatus = getValue("xmp_status");

      // Update global XMP status
      if (module.xmpStatus.find("Running at rated speed") !=
          std::string::npos) {
        memoryData.xmpEnabled = true;
      }

      // Debug output
      LOG_DEBUG << "Adding memory module to store:\n"
                << "  Slot: " << module.slot << "\n"
                << "  Type: " << module.memoryType << "\n"
                << "  Speed: " << module.speedMHz << "\n"
                << "  Configured: " << module.configuredSpeedMHz << "\n"
                << "  Manufacturer: " << module.manufacturer << "\n"
                << "  Part Number: " << module.partNumber << "\n"
                << "  Capacity: " << module.capacityGB << "\n"
                << "  XMP: " << module.xmpStatus;

      // Also set the memory type for the overall data
      if (memoryData.memoryType.empty() && !module.memoryType.empty()) {
        memoryData.memoryType = module.memoryType;
      }

      memoryData.modules.push_back(module);
    } catch (const std::exception& e) {
      LOG_ERROR << "Error parsing memory module data: " << e.what();
      continue;
    }
  }
}

void DiagnosticDataStore::setChannelStatus(const std::string& status) {
  memoryData.channelStatus = status;
}

void DiagnosticDataStore::updateMemoryHardwareInfo(
  const std::vector<MemoryData::MemoryModule>& modules,
  const std::string& memoryType, const std::string& channelStatus,
  bool xmpEnabled) {
  // Save performance metrics before updating hardware info
  double originalBandwidth = memoryData.bandwidth;
  double originalLatency = memoryData.latency;
  double originalWriteTime = memoryData.writeTime;
  double originalReadTime = memoryData.readTime;

  // Update hardware details
  memoryData.modules = modules;
  memoryData.memoryType = memoryType;
  memoryData.channelStatus = channelStatus;
  memoryData.xmpEnabled = xmpEnabled;

  // Restore performance metrics
  memoryData.bandwidth = originalBandwidth;
  memoryData.latency = originalLatency;
  memoryData.writeTime = originalWriteTime;
  memoryData.readTime = originalReadTime;

  // Log the update
  LOG_INFO << "Updated memory hardware info in DiagnosticDataStore:\n"
           << "  Modules count: " << modules.size() << "\n"
           << "  Memory Type: " << memoryType << "\n"
           << "  Channel Status: " << channelStatus << "\n"
           << "  XMP Enabled: " << (xmpEnabled ? "true" : "false") << "\n"
           << "  (Performance metrics preserved)";
}

void DiagnosticDataStore::updateCPUBasicInfo(const std::string& name,
                                             int physicalCores,
                                             int threadCount) {
  std::lock_guard<std::mutex> lock(dataMutex);
  cpuData.name = name;
  cpuData.physicalCores = physicalCores;
  cpuData.threadCount = threadCount;
  cpuData.cache.hyperThreadingEnabled = (threadCount > physicalCores);
}

// Update the updateCPUPerformanceMetrics implementation to remove the
// eightThreadTime parameter
void DiagnosticDataStore::updateCPUPerformanceMetrics(double simdScalar,
                                                      double simdAvx,
                                                      double primeTime,
                                                      double singleCoreTime,
                                                      double fourThreadTime) {
  std::lock_guard<std::mutex> lock(dataMutex);
  cpuData.simdScalar = simdScalar;
  cpuData.simdAvx = simdAvx;
  cpuData.primeTime = primeTime;
  cpuData.singleCoreTime = singleCoreTime;
  cpuData.fourThreadTime = fourThreadTime;
  
  LOG_INFO << "[DataStore] Updated CPU performance metrics - primeTime: " << primeTime 
           << ", simdScalar: " << simdScalar << ", simdAvx: " << simdAvx;
  
  // Remove the eightThreadTime assignment
}

void DiagnosticDataStore::updateCPUGameSimResults(double smallUPS,
                                                  double mediumUPS,
                                                  double largeUPS) {
  std::lock_guard<std::mutex> lock(dataMutex);
  cpuData.gameSimUPS_small = smallUPS;
  cpuData.gameSimUPS_medium = mediumUPS;
  cpuData.gameSimUPS_large = largeUPS;
}

void DiagnosticDataStore::updateCPUCacheLatencies(const double* latencies,
                                                  int l1SizeKB, int l2SizeKB,
                                                  int l3SizeKB) {
  std::lock_guard<std::mutex> lock(dataMutex);
  if (latencies) {
    // Update to 12 elements
    for (int i = 0; i < 12; i++) {
      cpuData.cache.latencies[i] = latencies[i];
    }
  }

  if (l1SizeKB > 0) cpuData.cache.l1SizeKB = l1SizeKB;
  if (l2SizeKB > 0) cpuData.cache.l2SizeKB = l2SizeKB;
  if (l3SizeKB > 0) cpuData.cache.l3SizeKB = l3SizeKB;
}

void DiagnosticDataStore::updateCPUCoreMetrics(
  const std::vector<CPUData::CoreMetrics>& metrics) {
  std::lock_guard<std::mutex> lock(dataMutex);
  cpuData.coreMetrics = metrics;
}

void DiagnosticDataStore::updateCPUBoostMetrics(
  const std::vector<CPUData::BoostMetrics>& metrics, double idlePower,
  double singleCorePower, double allCorePower, int bestCore, int maxDelta) {
  std::lock_guard<std::mutex> lock(dataMutex);
  cpuData.boostMetrics = metrics;
  cpuData.idleTotalPower = idlePower;
  cpuData.singleCoreTotalPower = singleCorePower;
  cpuData.allCoreTotalPower = allCorePower;
  cpuData.bestBoostCore = bestCore;
  cpuData.maxBoostDelta = maxDelta;
}

void DiagnosticDataStore::updateCPUThrottlingInfo(bool detected,
                                                  double peakClock,
                                                  double sustainedClock,
                                                  double dropPercent,
                                                  int detectedTime) {
  std::lock_guard<std::mutex> lock(dataMutex);
  cpuData.throttlingDetected = detected;
  cpuData.peakClock = peakClock;
  cpuData.sustainedClock = sustainedClock;
  cpuData.clockDropPercent = dropPercent;
  cpuData.throttlingDetectedTime = detectedTime;
}

void DiagnosticDataStore::updateCPUCStateData(double c1Time, double c2Time,
                                              double c3Time,
                                              double c1Transitions,
                                              double c2Transitions,
                                              double c3Transitions) {
  std::lock_guard<std::mutex> lock(dataMutex);

  // Store raw C-state data
  cpuData.cStates.c1TimePercent = c1Time;
  cpuData.cStates.c2TimePercent = c2Time;
  cpuData.cStates.c3TimePercent = c3Time;
  cpuData.cStates.c1TransitionsPerSec = c1Transitions;
  cpuData.cStates.c2TransitionsPerSec = c2Transitions;
  cpuData.cStates.c3TransitionsPerSec = c3Transitions;

  // Calculate total idle time (sum of all C-states)
  if (c1Time >= 0 && c2Time >= 0 && c3Time >= 0) {
    cpuData.cStates.totalIdleTime = c1Time + c2Time + c3Time;
  }

  // Determine if C-states are enabled
  // C-states are considered enabled if we see significant usage of C2 or C3
  // states Thresholds: C2 > 1% or C3 > 0.5% indicates C-states are working
  cpuData.cStates.cStatesEnabled = (c2Time > 1.0 || c3Time > 0.5);

  // Calculate power efficiency score (0-100)
  // This score considers:
  // 1. Use of deeper C-states (C2, C3 are better than just C1)
  // 2. Transition frequency (too many transitions can be inefficient)
  // 3. Overall idle time utilization

  if (c1Time >= 0 && c2Time >= 0 && c3Time >= 0) {
    double score = 0.0;

    // Base score from C-state usage (40 points max)
    if (cpuData.cStates.totalIdleTime > 0) {
      // Prefer deeper C-states - C3 is most efficient, then C2, then C1
      double c3Weight = 3.0;
      double c2Weight = 2.0;
      double c1Weight = 1.0;

      double weightedUsage =
        (c3Time * c3Weight + c2Time * c2Weight + c1Time * c1Weight);
      double maxPossibleWeight = cpuData.cStates.totalIdleTime * c3Weight;

      if (maxPossibleWeight > 0) {
        score += (weightedUsage / maxPossibleWeight) * 40.0;
      }
    }

    // Bonus for having C-states enabled (30 points)
    if (cpuData.cStates.cStatesEnabled) {
      score += 30.0;
    }

    // Transition efficiency score (30 points max)
    // Moderate transition rates are good (not too low, not too high)
    double totalTransitions = c1Transitions + c2Transitions + c3Transitions;
    if (totalTransitions >= 0) {
      // Optimal transition range: 10-100 transitions per second
      if (totalTransitions >= 10.0 && totalTransitions <= 100.0) {
        score += 30.0;
      } else if (totalTransitions >= 5.0 && totalTransitions <= 200.0) {
        score += 20.0;  // Acceptable range
      } else if (totalTransitions >= 1.0 && totalTransitions <= 500.0) {
        score += 10.0;  // Suboptimal but working
      }
      // Very low (<1/sec) or very high (>500/sec) transitions get 0 points
    }

    // Cap the score at 100
    cpuData.cStates.powerEfficiencyScore =
      std::min(100.0, std::max(0.0, score));
  } else {
    cpuData.cStates.powerEfficiencyScore = 0.0;  // No valid data
  }
}

// Add this implementation at an appropriate location in the file
void DiagnosticDataStore::updateFromMemoryMetrics(const MemoryData& metrics) {
  // Call the existing method that handles updating memory metrics
  updateMemoryPerformanceMetrics(metrics.bandwidth, metrics.latency,
                                 metrics.writeTime, metrics.readTime);
}

void DiagnosticDataStore::updateBackgroundProcessData(
  double cpuUsage, double gpuUsage, double dpcTime, double interruptTime,
  bool hasLatencyIssues,
  const std::vector<BackgroundProcessData::ProcessInfo>& topCpu,
  const std::vector<BackgroundProcessData::ProcessInfo>& topMemory,
  const std::vector<BackgroundProcessData::ProcessInfo>& topGpu,
  uint64_t physicalTotalKB, uint64_t physicalAvailableKB,
  uint64_t commitTotalKB, uint64_t commitLimitKB, uint64_t kernelPagedKB,
  uint64_t kernelNonPagedKB, uint64_t systemCacheKB, uint64_t userModePrivateKB,
  uint64_t otherMemoryKB, double peakDpcTime, double peakInterruptTime,
  double peakCpuUsage, double peakGpuUsage, double diskIO, double peakDiskIO) {

  std::lock_guard<std::mutex> lock(dataMutex);
  backgroundData.systemCpuUsage = cpuUsage;
  backgroundData.systemGpuUsage = gpuUsage;
  backgroundData.systemDpcTime = dpcTime;
  backgroundData.systemInterruptTime = interruptTime;
  backgroundData.peakSystemDpcTime = peakDpcTime;  // Store peak DPC time
  backgroundData.peakSystemInterruptTime =
    peakInterruptTime;  // Store peak interrupt time
  backgroundData.peakSystemCpuUsage = peakCpuUsage;  // Store peak CPU usage
  backgroundData.peakSystemGpuUsage = peakGpuUsage;  // Store peak GPU usage
  backgroundData.systemDiskIO = diskIO;              // Store disk I/O usage
  backgroundData.peakSystemDiskIO = peakDiskIO;  // Store peak disk I/O usage
  backgroundData.hasDpcLatencyIssues = hasLatencyIssues;
  backgroundData.topCpuProcesses = topCpu;
  backgroundData.topMemoryProcesses = topMemory;
  backgroundData.topGpuProcesses = topGpu;

  // Set the new memory metrics
  backgroundData.physicalTotalKB = physicalTotalKB;
  backgroundData.physicalAvailableKB = physicalAvailableKB;
  backgroundData.commitTotalKB = commitTotalKB;
  backgroundData.commitLimitKB = commitLimitKB;
  backgroundData.kernelPagedKB = kernelPagedKB;
  backgroundData.kernelNonPagedKB = kernelNonPagedKB;
  backgroundData.systemCacheKB = systemCacheKB;
  backgroundData.userModePrivateKB = userModePrivateKB;
  backgroundData.otherMemoryKB = otherMemoryKB;
}
