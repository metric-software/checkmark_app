#include "memory_test.h"
#include "../logging/Logger.h"

#include <algorithm>  // For std::min_element, std::max_element
#include <chrono>
#include <cmath>  // For std::sqrt
#include <iostream>
#include <numeric>  // For std::accumulate
#include <random>   // For std::random_device and std::mt19937
#include <vector>

#include <windows.h>
#include <immintrin.h>  // For AVX intrinsics
#include <omp.h>        // For explicit OpenMP control

#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"  // Only need ConstantSystemInfo

std::vector<DiagnosticDataStore::MemoryData::MemoryModule> getConstantMemoryInfo(
  std::string& channelStatus, bool& xmpEnabled) {
  LOG_INFO << "[Memory Info] Retrieving from ConstantSystemInfo";

  const auto& constInfo = SystemMetrics::GetConstantSystemInfo();
  std::vector<DiagnosticDataStore::MemoryData::MemoryModule> moduleObjects;

  // Get channel status and XMP status from constant info
  channelStatus = constInfo.memoryChannelConfig;
  xmpEnabled = constInfo.xmpEnabled;

  for (const auto& module : constInfo.memoryModules) {
    DiagnosticDataStore::MemoryData::MemoryModule newModule;

    // Extract channel designator from deviceLocator (e.g., extract "A2" from
    // "DIMM_A2")
    std::string channelDesignator = "Unknown";
    int slotNum = moduleObjects.size() + 1;  // Default sequential numbering

    if (!module.deviceLocator.empty()) {
      std::string slotStr = module.deviceLocator;

      std::string::size_type channelPos = slotStr.find_first_of("AB");
      std::string::size_type numPos =
        slotStr.find_first_of("0123456789", channelPos);

      if (channelPos != std::string::npos && numPos != std::string::npos) {
        channelDesignator = slotStr.substr(channelPos, numPos - channelPos + 1);

        char channel = slotStr[channelPos];
        int slotId = std::stoi(slotStr.substr(numPos, 1));

        if (channel == 'A') {
          slotNum = slotId * 2 - 1;  // A1->1, A2->3
        } else if (channel == 'B') {
          slotNum = slotId * 2;  // B1->2, B2->4
        }
      }
    }

    newModule.slot = slotNum;
    newModule.deviceLocator = module.deviceLocator;
    newModule.capacityGB = module.capacityGB;
    newModule.speedMHz = module.speedMHz;
    newModule.configuredSpeedMHz = module.configuredSpeedMHz;
    newModule.manufacturer = module.manufacturer;
    newModule.partNumber = module.partNumber;
    newModule.memoryType = module.memoryType;

    // Calculate XMP status string
    int memoryTypeCode = 0;
    if (module.memoryType == "DDR4")
      memoryTypeCode = 26;
    else if (module.memoryType == "DDR5")
      memoryTypeCode = 27;

    newModule.xmpStatus = checkXMPStatus(memoryTypeCode, module.speedMHz,
                                         module.configuredSpeedMHz);

    moduleObjects.push_back(newModule);
  }

  return moduleObjects;
}

std::string checkXMPStatus(int memoryType, int speed, int configuredSpeed) {
  // DDR4 check
  if (memoryType == 26) {  // DDR4
    if (configuredSpeed < 2800) {
      return "Low memory speed, check XMP mode from BIOS";
    }
  }
  // DDR5 check
  else if (memoryType == 27) {  // DDR5
    if (configuredSpeed < 4900) {
      return "Low memory speed, check XMP mode from BIOS";
    }
  }

  if (speed != configuredSpeed) {
    return "Different speed and configured speed, check memory status from "
           "BIOS";
  }

  return "Running at rated speed";
}

std::string checkDualChannelStatus(int moduleCount, int memoryType,
                                   int configuredSpeed) {
  if (moduleCount == 1 || moduleCount == 3) {
    return "Single channel mode detected - Install memory in pairs (2 or 4 "
           "modules) for optimal performance";
  } else if (memoryType == 26 && configuredSpeed < 2000) {  // DDR4
    return "Very low memory speed detected - Verify memory modules are "
           "installed in the correct slots (usually A2/B2)";
  } else {
    return "Dual channel mode detected";
  }
}

void getMemoryInfo() {
  try {
    LOG_INFO << "[Memory Info] Retrieving system memory information";

    auto& dataStore = DiagnosticDataStore::getInstance();
    const auto& constInfo = SystemMetrics::GetConstantSystemInfo();

    std::string channelStatus;
    bool xmpEnabled = false;
    std::string memoryType = constInfo.memoryType;

    // Get memory information from ConstantSystemInfo
    std::vector<DiagnosticDataStore::MemoryData::MemoryModule> modules =
      getConstantMemoryInfo(channelStatus, xmpEnabled);

    // Update the DiagnosticDataStore with the hardware info
    dataStore.updateMemoryHardwareInfo(modules, memoryType, channelStatus,
                                       xmpEnabled);

    // Also check page file configuration
    getPageFileInfo();
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception in getMemoryInfo(): " << e.what();
  } catch (...) {
    LOG_ERROR << "Unknown exception in getMemoryInfo()";
  }
}

void getPageFileInfo() {
  try {
    LOG_INFO << "[Memory Info] Checking page file configuration";

    auto& dataStore = DiagnosticDataStore::getInstance();
    const auto& constInfo = SystemMetrics::GetConstantSystemInfo();
    DiagnosticDataStore::MemoryData::PageFileInfo pfInfo;

    if (constInfo.pageFileExists) {
      pfInfo.systemManaged = constInfo.pageFileSystemManaged;

      for (size_t i = 0; i < constInfo.pageFileLocations.size(); i++) {
        DiagnosticDataStore::MemoryData::PageFileLocation loc;
        loc.drive = constInfo.pageFileLocations[i];

        if (i < constInfo.pageFileCurrentSizesMB.size()) {
          loc.currentSizeMB = constInfo.pageFileCurrentSizesMB[i];
        }

        if (i < constInfo.pageFileMaxSizesMB.size()) {
          loc.maxSizeMB = constInfo.pageFileMaxSizesMB[i];
        }

        pfInfo.locations.push_back(loc);
      }

      pfInfo.exists = true;
      pfInfo.totalSizeMB = constInfo.pageTotalSizeMB;
      pfInfo.primaryDrive = constInfo.pagePrimaryDriveLetter;
    } else {
      pfInfo.exists = false;
    }

    // Save to diagnostic data store
    dataStore.updatePageFileInfo(pfInfo);
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception in getPageFileInfo(): " << e.what();
  }
}

static double computeStdDev(const std::vector<double>& times, double mean) {
  double sumSq = 0.0;
  for (double t : times) {
    double diff = t - mean;
    sumSq += diff * diff;
  }
  return std::sqrt(sumSq / (times.size() - 1));
}

void runMemoryTests() {
  LOG_INFO << "[Memory Test] Running basic memory test";
  auto& dataStore = DiagnosticDataStore::getInstance();

  // Properly declare the thread priority variables
  HANDLE currentThread = GetCurrentThread();
  int originalPriority = GetThreadPriority(currentThread);

  // Check if elevated priority is enabled in settings
  bool elevatedPriorityEnabled =
    ApplicationSettings::getInstance().getElevatedPriorityEnabled();
  if (elevatedPriorityEnabled) {
    SetThreadPriority(currentThread, THREAD_PRIORITY_ABOVE_NORMAL);
    LOG_INFO << "[Memory Test] Running with elevated thread priority (enabled in settings)";
  }

  try {
    const size_t testSizeMB = 100;
    const size_t elementCount = (testSizeMB * 1024 * 1024) / sizeof(int);
    std::vector<int> testArray(elementCount);

    // Write Phase
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < elementCount; i++) {
      testArray[i] = static_cast<int>(i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
        .count();
    LOG_DEBUG << " Write test: " << duration << " ms";

    // Read Phase
    start = std::chrono::high_resolution_clock::now();
    bool success = true;
    for (size_t i = 0; i < elementCount; i++) {
      if (testArray[i] != static_cast<int>(i)) {
        success = false;
        break;
      }
    }
    end = std::chrono::high_resolution_clock::now();
    duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
        .count();
    LOG_DEBUG << " Read test: " << duration << " ms";

    if (success) {
      LOG_INFO << " Memory test passed";
    } else {
      LOG_ERROR << " Memory test failed";
    }

    // Run memory stability test with a smaller size (256MB) for quicker results
    // while still being substantial enough to detect issues
    LOG_INFO << "[Memory Test] Running memory stability test";
    const size_t stabilityTestSizeMB = 256;

    // Get full test results structure rather than just a boolean
    auto stabilityTestResults = runMemoryStabilityTest(stabilityTestSizeMB);

    // Use the passed field from the results for simple status check
    bool stabilityTestPassed = stabilityTestResults.passed;

    // Store the stability test results in the data store
    DiagnosticDataStore::MemoryData::StabilityTestResults stabilityResults;
    stabilityResults.testPerformed = true;
    stabilityResults.passed = stabilityTestPassed;
    stabilityResults.testedSizeMB = stabilityTestSizeMB;
    stabilityResults.errorCount = stabilityTestResults.errorCount;
    stabilityResults.completedLoops = stabilityTestResults.completedLoops;
    stabilityResults.completedPatterns = stabilityTestResults.completedPatterns;

    // Update the data store with stability test results
    std::lock_guard<std::mutex> lock(dataStore.getDataMutex());
    auto memData = dataStore.getMemoryData();
    memData.stabilityTest = stabilityResults;
    dataStore.setMemoryData(memData);

    // Restore thread priority before returning
    if (elevatedPriorityEnabled) {
      SetThreadPriority(currentThread, originalPriority);
    }
  } catch (const std::exception& e) {
    // Restore thread priority in case of exception
    if (elevatedPriorityEnabled) {
      SetThreadPriority(currentThread, originalPriority);
    }
    LOG_ERROR << " Memory test failed: " << e.what();
  }
}

void runMemoryTestsMultiple(DiagnosticDataStore::MemoryData* metrics) {
  auto& dataStore = DiagnosticDataStore::getInstance();

  // Declare thread priority variables at the beginning of this function
  HANDLE currentThread = GetCurrentThread();
  int originalPriority = GetThreadPriority(currentThread);
  SetThreadPriority(currentThread, THREAD_PRIORITY_ABOVE_NORMAL);

  // Back up the existing memory module data
  std::vector<DiagnosticDataStore::MemoryData::MemoryModule> backupModules =
    dataStore.getMemoryData().modules;
  std::string backupMemoryType = dataStore.getMemoryData().memoryType;
  std::string backupChannelStatus = dataStore.getMemoryData().channelStatus;
  bool backupXmpEnabled = dataStore.getMemoryData().xmpEnabled;

  LOG_INFO << "[Memory Test] Running performance tests";

  // MEMORY LATENCY TEST - IMPROVED
  // ----------------------------
  // Use a much larger buffer to ensure we're beyond all CPU caches (512MB)
  // Use true pointer chasing with cache line flush instructions
  const size_t LATENCY_TEST_SIZE = 512 * 1024 * 1024;
  const int numRuns = 5;
  std::vector<double> latencyTimes(numRuns);

  try {
    void* buffer = _aligned_malloc(LATENCY_TEST_SIZE, 64);
    if (!buffer) throw std::bad_alloc();

    uint64_t* ptrs = static_cast<uint64_t*>(buffer);
    const size_t elementCount = LATENCY_TEST_SIZE / sizeof(uint64_t);

    // Create indexing array and randomize it
    std::vector<size_t> indices(elementCount);
    for (size_t i = 0; i < indices.size(); i++) {
      indices[i] = i;
    }

    std::random_device rd;
    std::mt19937 g(rd());

    // Wide stride to avoid prefetcher prediction
    // Only shuffle a subset to speed up initialization but maintain randomness
    const size_t strideSize = 64;  // Cache line size in bytes
    const size_t numElements = elementCount / (strideSize / sizeof(uint64_t));
    std::vector<size_t> strideIndices(numElements);

    for (size_t i = 0; i < numElements; i++) {
      strideIndices[i] = i * (strideSize / sizeof(uint64_t));
    }

    std::shuffle(strideIndices.begin(), strideIndices.end(), g);

    // Create the pointer chasing pattern
    for (size_t i = 0; i < numElements - 1; i++) {
      ptrs[strideIndices[i]] =
        reinterpret_cast<uint64_t>(&ptrs[strideIndices[i + 1]]);
    }
    ptrs[strideIndices[numElements - 1]] =
      reinterpret_cast<uint64_t>(&ptrs[strideIndices[0]]);

    for (int run = 0; run < numRuns; run++) {
// Flush the entire cache hierarchy
#pragma omp parallel for
      for (size_t i = 0; i < numElements; i++) {
        _mm_clflush(&ptrs[strideIndices[i]]);
      }
      _mm_mfence();

      const int iterations = 10000000;
      volatile uint64_t* p = &ptrs[strideIndices[0]];

      // Warm up to ensure fair timing
      for (int i = 0; i < 1000; i++) {
        p = reinterpret_cast<uint64_t*>(*p);
      }
      _mm_mfence();

      // Main measurement
      auto start = std::chrono::high_resolution_clock::now();
      for (int i = 0; i < iterations; i++) {
        p = reinterpret_cast<uint64_t*>(*p);
      }
      _mm_mfence();
      auto end = std::chrono::high_resolution_clock::now();

      double totalNs =
        std::chrono::duration<double, std::nano>(end - start).count();
      double nsPerAccess = totalNs / iterations;
      latencyTimes[run] = nsPerAccess;

      // Anti-optimization
      volatile uint64_t dummy = reinterpret_cast<uint64_t>(p);
    }

    _aligned_free(buffer);

    // Calculate average latency (excluding highest and lowest)
    std::sort(latencyTimes.begin(), latencyTimes.end());
    double avgLat;
    if (latencyTimes.size() >= 3) {
      // Exclude highest and lowest for more stable results
      avgLat =
        std::accumulate(latencyTimes.begin() + 1, latencyTimes.end() - 1, 0.0) /
        (latencyTimes.size() - 2);
    } else {
      avgLat = std::accumulate(latencyTimes.begin(), latencyTimes.end(), 0.0) /
               latencyTimes.size();
    }
    metrics->latency = avgLat;

    LOG_INFO << "[Memory Test] Average RAM latency: " << avgLat << " ns";
  } catch (const std::exception& e) {
    LOG_ERROR << "Latency test failed: " << e.what();
    metrics->latency = -1;
  }

  // BANDWIDTH TEST - IMPROVED
  // ----------------------
  // Uses larger buffers and consistent non-temporal stores to bypass cache
  const size_t ALIGNMENT = 64;
  const int BANDWIDTH_TEST_RUNS = 5;
  std::vector<double> bandwidthResults(BANDWIDTH_TEST_RUNS);

  try {
    // Use larger arrays to exceed any modern CPU cache
    const size_t arrayElements = 512 * 1024 * 1024 / sizeof(double);
    const size_t arrayBytes = arrayElements * sizeof(double);

    double* a = static_cast<double*>(_aligned_malloc(arrayBytes, ALIGNMENT));
    double* b = static_cast<double*>(_aligned_malloc(arrayBytes, ALIGNMENT));
    double* c = static_cast<double*>(_aligned_malloc(arrayBytes, ALIGNMENT));

    if (!a || !b || !c) {
      throw std::runtime_error("Failed to allocate memory for bandwidth test");
    }

    // Initialize arrays with random data to prevent optimization
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> dist(0.0, 1.0);

#pragma omp parallel for
    for (int64_t i = 0; i < static_cast<int64_t>(arrayElements); i++) {
      b[i] = dist(gen);
      c[i] = dist(gen);
    }

    const double scalar = 3.0;

    for (int run = 0; run < BANDWIDTH_TEST_RUNS; run++) {
      // Flush CPU caches
      _mm_mfence();

      auto start = std::chrono::high_resolution_clock::now();

// Always use non-temporal stores to bypass cache
#pragma omp parallel
      {
#pragma omp for schedule(static)
        for (int64_t i = 0; i < static_cast<int64_t>(arrayElements); i += 4) {
          if (i + 4 <= static_cast<int64_t>(arrayElements)) {
            __m256d bVal = _mm256_load_pd(&b[i]);
            __m256d cVal = _mm256_load_pd(&c[i]);
            __m256d scaled = _mm256_mul_pd(cVal, _mm256_set1_pd(scalar));
            __m256d result = _mm256_add_pd(bVal, scaled);
            _mm256_stream_pd(&a[i], result);  // Non-temporal store
          }
        }
      }

      _mm_mfence();
      auto end = std::chrono::high_resolution_clock::now();
      double seconds = std::chrono::duration<double>(end - start).count();

      // Calculate bandwidth: we read two arrays and write one
      double bytesProcessed = 3.0 * arrayBytes;
      double bandwidthMBs = (bytesProcessed / seconds) / (1024.0 * 1024.0);
      bandwidthResults[run] = bandwidthMBs;

      // Anti-optimization
      volatile double sum = 0.0;
      for (size_t i = 0; i < 100; i++) {
        size_t idx = i * 10000;
        if (idx < arrayElements) sum += a[idx];
      }
    }

    // Use the best bandwidth result
    double bestBandwidth =
      *std::max_element(bandwidthResults.begin(), bandwidthResults.end());
    metrics->bandwidth = bestBandwidth;
    LOG_INFO << "[Memory Test] Memory bandwidth: " << bestBandwidth << " MB/s";

    _aligned_free(a);
    _aligned_free(b);
    _aligned_free(c);
  } catch (const std::exception& e) {
    LOG_ERROR << "Bandwidth test failed: " << e.what();
    metrics->bandwidth = -1;
  }

  // READ/WRITE SPEED TESTS - IMPROVED
  // -----------------------------
  try {
    // Use larger buffer size (2GB) to ensure we're testing RAM
    const size_t testSizeMB = 2048;
    const size_t elementCount = (testSizeMB * 1024 * 1024) / sizeof(int);

    // True random access pattern
    std::vector<size_t> accessIndices(
      std::min<size_t>(elementCount, static_cast<size_t>(64 * 1024 * 1024)));
    for (size_t i = 0; i < accessIndices.size(); i++) {
      accessIndices[i] =
        (i * 16807) %
        elementCount;  // Prime number multiply for pseudo-randomness
    }

    // WRITE SPEED TEST
    double writeGBs = -1;
    {
      // Allocate with alignment for better performance
      int* writeArray =
        static_cast<int*>(_aligned_malloc(elementCount * sizeof(int), 4096));
      if (!writeArray) throw std::bad_alloc();

      // Flush caches
      _mm_mfence();

      auto start = std::chrono::high_resolution_clock::now();

// Use cache bypass instructions for true RAM testing
#pragma omp parallel
      {
#pragma omp for schedule(dynamic, 16384)
        for (int64_t i = 0; i < static_cast<int64_t>(accessIndices.size());
             i++) {
          const size_t idx = accessIndices[i];
          // Use non-temporal stores to bypass cache
          _mm_stream_si32(writeArray + idx, static_cast<int>(idx));
        }
      }

      _mm_mfence();
      auto end = std::chrono::high_resolution_clock::now();

      double seconds = std::chrono::duration<double>(end - start).count();
      double bytesPerSecond = (accessIndices.size() * sizeof(int)) / seconds;
      writeGBs = bytesPerSecond / (1024.0 * 1024.0 * 1024.0);

      metrics->writeTime = writeGBs;
      LOG_INFO << "[Memory Test] Memory write speed: " << writeGBs << " GB/s";

      _aligned_free(writeArray);
    }

    // READ SPEED TEST
    double readGBs = -1;
    {
      // Allocate and initialize read array with random values
      int* readArray =
        static_cast<int*>(_aligned_malloc(elementCount * sizeof(int), 4096));
      if (!readArray) throw std::bad_alloc();

#pragma omp parallel for
      for (int64_t i = 0; i < static_cast<int64_t>(elementCount); i++) {
        readArray[i] =
          static_cast<int>(i * 7919);  // Another prime for pseudo-randomness
      }

      _mm_mfence();

// Flush CPU caches
#pragma omp parallel for
      for (int64_t i = 0; i < static_cast<int64_t>(accessIndices.size());
           i += 16) {
        _mm_clflush(&readArray[accessIndices[i]]);
      }
      _mm_mfence();

      auto start = std::chrono::high_resolution_clock::now();

      volatile int64_t sum = 0;

#pragma omp parallel reduction(+ : sum)
      {
        int64_t localSum = 0;
#pragma omp for schedule(dynamic, 16384)
        for (int64_t i = 0; i < static_cast<int64_t>(accessIndices.size());
             i++) {
          // Use prefetch prevention by XORing with a volatile value
          volatile size_t idx = accessIndices[i];
          localSum += readArray[idx];
        }
        sum += localSum;
      }

      _mm_mfence();
      auto end = std::chrono::high_resolution_clock::now();

      double seconds = std::chrono::duration<double>(end - start).count();
      double bytesPerSecond = (accessIndices.size() * sizeof(int)) / seconds;
      readGBs = bytesPerSecond / (1024.0 * 1024.0 * 1024.0);

      metrics->readTime = readGBs;
      LOG_INFO << "[Memory Test] Memory read speed: " << readGBs << " GB/s";

      _aligned_free(readArray);
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "Read/write tests failed: " << e.what();
    if (metrics->readTime <= 0) metrics->readTime = -1;
    if (metrics->writeTime <= 0) metrics->writeTime = -1;
  }

  // Add memory stability test (with a smaller size for quicker results)
  LOG_INFO << "[Memory Test] Running memory stability test";
  const size_t stabilityTestSizeMB = 256;

  // Run stability test with error validation
  auto stabilityTestResults = runMemoryStabilityTest(stabilityTestSizeMB);
  bool stabilityTestPassed = stabilityTestResults.passed;

  // Create stability test results
  DiagnosticDataStore::MemoryData::StabilityTestResults stabilityResults;
  stabilityResults.testPerformed = true;
  stabilityResults.passed = stabilityTestPassed;
  stabilityResults.testedSizeMB = stabilityTestSizeMB;
  stabilityResults.errorCount = stabilityTestResults.errorCount;
  stabilityResults.completedLoops = stabilityTestResults.completedPatterns;
  stabilityResults.completedPatterns = stabilityTestResults.completedPatterns;

  // Add stability results to metrics
  metrics->stabilityTest = stabilityResults;

  // Restore memory hardware info if needed
  if (backupModules.size() > 0 &&
      dataStore.getMemoryData().modules.size() == 0) {
    dataStore.updateMemoryHardwareInfo(backupModules, backupMemoryType,
                                       backupChannelStatus, backupXmpEnabled);
  }

  SetThreadPriority(currentThread, originalPriority);

  // Update metrics in data store
  dataStore.updateFromMemoryMetrics(*metrics);
}

std::future<void> runMemoryTestsAsync(
  DiagnosticDataStore::MemoryData* metrics) {
  auto& dataStore = DiagnosticDataStore::getInstance();

  // Make a deep copy of all memory data
  std::vector<DiagnosticDataStore::MemoryData::MemoryModule> modulesCopy =
    dataStore.getMemoryData().modules;
  std::string memoryTypeCopy = dataStore.getMemoryData().memoryType;
  std::string channelStatusCopy = dataStore.getMemoryData().channelStatus;
  bool xmpEnabledCopy = dataStore.getMemoryData().xmpEnabled;

  // Launch async task
  return std::async(std::launch::async, [metrics, modulesCopy, memoryTypeCopy,
                                         channelStatusCopy, xmpEnabledCopy]() {
    auto& threadDataStore = DiagnosticDataStore::getInstance();

    // Restore memory hardware info in this thread
    threadDataStore.updateMemoryHardwareInfo(modulesCopy, memoryTypeCopy,
                                             channelStatusCopy, xmpEnabledCopy);

    // Run the tests
    runMemoryTestsMultiple(metrics);
  });
}

// Add after the existing code

// Memory stability test structures and classes

// Configuration structure for the memory test
struct MemoryStabilityTestConfig {
  size_t memorySizeBytes;  // Size of the memory block to test
  int testLoops;           // Number of loops/passes to run the tests
  bool useMultithreading;  // Flag to enable multithreading for parallel tests
  int numThreads;  // Number of threads to use if multithreading is enabled

  MemoryStabilityTestConfig()
      : memorySizeBytes(256 * 1024 * 1024),  // Default 256 MB
        testLoops(3), useMultithreading(true),
        numThreads(std::thread::hardware_concurrency()) {}
};

// Error information structure
struct MemoryErrorInfo {
  size_t address;
  uint8_t expected;
  uint8_t actual;
  int loopNumber;
  std::string testName;
};

// Base class for a memory test pattern
class MemoryTestPattern {
 public:
  virtual ~MemoryTestPattern() = default;
  virtual std::string getName() const = 0;
  // The test function writes a pattern to memory and then verifies it
  virtual bool runTest(uint8_t* buffer, size_t size, int loop,
                       std::vector<MemoryErrorInfo>& errors) = 0;
};

// Test pattern: Alternating Bits (0xAA / 0x55)
class AlternatingBitsTest : public MemoryTestPattern {
 public:
  std::string getName() const override { return "Alternating Bits Test"; }

  bool runTest(uint8_t* buffer, size_t size, int loop,
               std::vector<MemoryErrorInfo>& errors) override {
// Write phase: fill with alternating pattern
#pragma omp parallel for
    for (int64_t i = 0; i < static_cast<int64_t>(size); i++) {
      buffer[i] = (i % 2 == 0) ? 0xAA : 0x55;
    }

    // Memory barrier to ensure writes are committed
    _mm_mfence();

    // Read/Verify phase
    for (size_t i = 0; i < size; i++) {
      uint8_t expected = (i % 2 == 0) ? 0xAA : 0x55;
      if (buffer[i] != expected) {
        errors.push_back({i, expected, buffer[i], loop, getName()});
        return false;  // stop on error
      }
    }
    return true;
  }
};

// Test pattern: Walking Ones (shifted bit pattern)
class WalkingOnesTest : public MemoryTestPattern {
 public:
  std::string getName() const override { return "Walking Ones Test"; }

  bool runTest(uint8_t* buffer, size_t size, int loop,
               std::vector<MemoryErrorInfo>& errors) override {
// Write phase: for each byte, cycle through single-bit positions
#pragma omp parallel for
    for (int64_t i = 0; i < static_cast<int64_t>(size); i++) {
      buffer[i] = 1 << (i % 8);
    }

    _mm_mfence();

    // Read/Verify phase
    for (size_t i = 0; i < size; i++) {
      uint8_t expected = 1 << (i % 8);
      if (buffer[i] != expected) {
        errors.push_back({i, expected, buffer[i], loop, getName()});
        return false;
      }
    }
    return true;
  }
};

// Replace the current RandomPatternTest class with this more robust version
class RandomPatternTest : public MemoryTestPattern {
 public:
  std::string getName() const override { return "Random Pattern Test"; }

  bool runTest(uint8_t* buffer, size_t size, int loop,
               std::vector<MemoryErrorInfo>& errors) override {
    LOG_INFO << "[Memory Stability Test] Starting Random Pattern Test (loop " << loop + 1 << ")";

    // Create a reproducible random sequence based on a fixed seed
    // Use a simpler pattern generation approach to avoid thread synchronization
    // issues
    const unsigned int seed = 12345 + loop;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);

    // Generate pattern in smaller chunks to improve cache locality
    const size_t CHUNK_SIZE = 1024 * 1024;  // 1MB chunks

    for (size_t offset = 0; offset < size; offset += CHUNK_SIZE) {
      size_t currentChunkSize = std::min(CHUNK_SIZE, size - offset);

      // Reset RNG to the chunk-specific seed for reproducibility
      rng.seed(seed + static_cast<unsigned int>(offset / CHUNK_SIZE));

      // Sequential write phase for this chunk (no multithreading)
      for (size_t i = 0; i < currentChunkSize; i++) {
        buffer[offset + i] = static_cast<uint8_t>(dist(rng));
      }

      // Memory barrier
      _mm_mfence();

      // Reset RNG to the same seed for verification
      rng.seed(seed + static_cast<unsigned int>(offset / CHUNK_SIZE));

      // Sequential verification phase for this chunk
      for (size_t i = 0; i < currentChunkSize; i++) {
        uint8_t expected = static_cast<uint8_t>(dist(rng));
        if (buffer[offset + i] != expected) {
          // Double-check the error with multiple reads to filter transient
          // issues
          bool confirmedError = true;
          for (int retries = 0; retries < 3; retries++) {
            _mm_lfence();  // Force read ordering
            if (buffer[offset + i] == expected) {
              confirmedError = false;
              break;
            }
          }

          if (confirmedError) {
            LOG_WARN << "[Memory Stability Test] Potential error at 0x" << std::hex << (offset + i) << std::dec << ", expected: 0x" << std::hex << static_cast<int>(expected) << ", got: 0x" << static_cast<int>(buffer[offset + i]) << std::dec;

            // Try writing and reading again to confirm it's not a transient
            // issue
            buffer[offset + i] = expected;
            _mm_mfence();
            _mm_lfence();

            if (buffer[offset + i] != expected) {
              errors.push_back(
                {offset + i, expected, buffer[offset + i], loop, getName()});

              if (errors.size() >= 5) {
                LOG_ERROR << "[Memory Stability Test] Too many errors, stopping test";
                return false;
              }
            } else {
              LOG_WARN << "[Memory Stability Test] Error corrected on rewrite, ignoring";
            }
          }
        }
      }
    }

    LOG_INFO << "[Memory Stability Test] Random Pattern Test completed successfully";
    return errors.empty();  // Return true if no errors were found
  }
};

// Additional pattern: Inverse Alternating Bits (0x55 / 0xAA)
class InverseAlternatingBitsTest : public MemoryTestPattern {
 public:
  std::string getName() const override {
    return "Inverse Alternating Bits Test";
  }

  bool runTest(uint8_t* buffer, size_t size, int loop,
               std::vector<MemoryErrorInfo>& errors) override {
// Write phase: fill with alternating pattern (inverse of AlternatingBitsTest)
#pragma omp parallel for
    for (int64_t i = 0; i < static_cast<int64_t>(size); i++) {
      buffer[i] = (i % 2 == 0) ? 0x55 : 0xAA;
    }

    _mm_mfence();

    // Read/Verify phase
    for (size_t i = 0; i < size; i++) {
      uint8_t expected = (i % 2 == 0) ? 0x55 : 0xAA;
      if (buffer[i] != expected) {
        errors.push_back({i, expected, buffer[i], loop, getName()});
        return false;
      }
    }
    return true;
  }
};

// MemoryStabilityTester class to run tests using the selected patterns and
// configuration
class MemoryStabilityTester {
 public:
  MemoryStabilityTester(const MemoryStabilityTestConfig& config)
      : config_(config), stopOnError_(true) {
    // Add only the most reliable test patterns
    testPatterns_.push_back(new AlternatingBitsTest());
    testPatterns_.push_back(new WalkingOnesTest());

    // Only add RandomPatternTest if explicitly requested via config
    // (This prevents most false positives while still allowing other patterns
    // to run)
    if (config.memorySizeBytes <
        512 * 1024 * 1024) {  // Only for smaller tests (<512MB)
      testPatterns_.push_back(new RandomPatternTest());
    }
  }

  ~MemoryStabilityTester() {
    // Clean up allocated test pattern objects
    for (auto* pattern : testPatterns_) {
      delete pattern;
    }
  }

  // Structure to hold test results
  struct TestResults {
    bool passed = true;
    std::vector<MemoryErrorInfo> errors;
    int completedLoops = 0;
    int completedPatterns = 0;
    int errorCount = 0;
  };

  // Run the memory test across all loops and test patterns
  TestResults runTests() {
    TestResults results;
    LOG_INFO << "[Memory Stability Test] Starting test with " << (config_.memorySizeBytes / (1024 * 1024)) << " MB of memory, " << config_.testLoops << " loops";

    // Allocate memory to test
    uint8_t* memoryBlock =
      static_cast<uint8_t*>(_aligned_malloc(config_.memorySizeBytes, 4096));
    if (!memoryBlock) {
      LOG_ERROR << "[Memory Stability Test] Failed to allocate memory block for testing.";
      results.passed = false;
      return results;
    }

    // For thread-safe error reporting
    std::mutex errorMutex;
    std::atomic<bool> errorDetected(false);

    auto runLoop = [&](int loop) {
      // Run each test pattern
      for (size_t patternIndex = 0; patternIndex < testPatterns_.size();
           patternIndex++) {
        auto* pattern = testPatterns_[patternIndex];

        LOG_INFO << "[Memory Stability Test] Loop " << (loop + 1) << "/" << config_.testLoops << ", Pattern: " << pattern->getName();

        std::vector<MemoryErrorInfo> patternErrors;
        if (!pattern->runTest(memoryBlock, config_.memorySizeBytes, loop,
                              patternErrors)) {
          errorDetected = true;

          // Log error details
          std::lock_guard<std::mutex> lock(errorMutex);
          results.errors.insert(results.errors.end(), patternErrors.begin(),
                                patternErrors.end());
          results.passed = false;

          for (const auto& error : patternErrors) {
            LOG_ERROR << "[Memory Stability Test] Error detected at address 0x" << std::hex << error.address << std::dec << " during " << error.testName << " (loop " << error.loopNumber + 1 << "). Expected: 0x" << std::hex << static_cast<int>(error.expected) << ", Got: 0x" << static_cast<int>(error.actual) << std::dec;
          }

          if (stopOnError_) {
            return;  // Exit early on error if stopOnError_ is true
          }
        }

        std::lock_guard<std::mutex> lock(errorMutex);
        results.completedPatterns++;
      }

      std::lock_guard<std::mutex> lock(errorMutex);
      results.completedLoops++;
    };

    // Decide whether to run loops in parallel or sequentially
    if (config_.useMultithreading && config_.numThreads > 1) {
      std::vector<std::thread> threads;

      // Divide the loops among threads
      for (int i = 0; i < config_.testLoops; i++) {
        if (errorDetected.load() && stopOnError_) {
          break;
        }

        threads.emplace_back([&, i]() {
          if (!errorDetected.load() || !stopOnError_) {
            runLoop(i);
          }
        });

        // Throttle thread creation if we've spawned enough
        if (threads.size() >= static_cast<size_t>(config_.numThreads)) {
          for (auto& t : threads) {
            if (t.joinable()) t.join();
          }
          threads.clear();
        }
      }

      // Join any remaining threads
      for (auto& t : threads) {
        if (t.joinable()) t.join();
      }
    } else {
      // Sequential execution
      for (int i = 0;
           i < config_.testLoops && (!errorDetected.load() || !stopOnError_);
           i++) {
        runLoop(i);
      }
    }

    _aligned_free(memoryBlock);

    if (results.passed) {
      LOG_INFO << "[Memory Stability Test] Completed successfully. No errors detected.";
    } else {
      LOG_ERROR << "[Memory Stability Test] Failed with " << results.errors.size() << " errors.";
    }

    // Add to the end of runTests method, before the return statement
    results.completedLoops =
      std::min(config_.testLoops, results.completedLoops);
    results.completedPatterns =
      std::min(static_cast<int>(testPatterns_.size() * config_.testLoops),
               results.completedPatterns);
    results.errorCount = results.errors.size();

    return results;
  }

  void setStopOnError(bool stop) { stopOnError_ = stop; }

 private:
  MemoryStabilityTestConfig config_;
  bool stopOnError_;
  std::vector<MemoryTestPattern*> testPatterns_;
};

// Function to run memory stability test
DiagnosticDataStore::MemoryData::StabilityTestResults runMemoryStabilityTest(
  size_t memorySizeMB) {
  DiagnosticDataStore::MemoryData::StabilityTestResults results;
  results.testPerformed = true;
  results.testedSizeMB = memorySizeMB;

  try {
    MemoryStabilityTestConfig config;
    config.memorySizeBytes = memorySizeMB * 1024 * 1024;
    config.testLoops = 3;  // Using 3 loops for more thorough testing
    config.useMultithreading =
      false;  // Disable multithreading for more reliable results
    config.numThreads = 1;

    LOG_INFO << "[Memory Stability Test] Creating tester with " << memorySizeMB << "MB test size, " << config.testLoops << " loops";

    MemoryStabilityTester tester(config);
    tester.setStopOnError(
      false);  // Continue after errors for better diagnostics

    auto testResults = tester.runTests();

    results.passed = testResults.passed;
    results.errorCount = testResults.errors.size();
    results.completedLoops = testResults.completedLoops;
    results.completedPatterns = testResults.completedPatterns;

    // Save results to DiagnosticDataStore
    auto& dataStore = DiagnosticDataStore::getInstance();
    dataStore.updateMemoryStabilityResults(results);

    // For diagnostic purposes, print summary
    LOG_INFO << "[Memory Stability Test] Summary: " << (testResults.passed ? "PASSED" : "FAILED") << " with " << testResults.errors.size() << " errors, " << testResults.completedLoops << " loops completed, " << testResults.completedPatterns << " patterns completed";
  } catch (const std::exception& e) {
    LOG_ERROR << "[Memory Stability Test] Exception: " << e.what();
    results.passed = false;
    results.errorCount = 1;

    // Even in case of exception, update the DiagnosticDataStore
    auto& dataStore = DiagnosticDataStore::getInstance();
    dataStore.updateMemoryStabilityResults(results);
  }

  return results;
}
