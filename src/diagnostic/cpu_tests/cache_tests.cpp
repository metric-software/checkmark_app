#include "cache_tests.h"  // Add this to include the ColdStartResults struct

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

#include <windows.h>
#include <immintrin.h>

#include "../cpu_test.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"

#include "logging/Logger.h"

void testCacheAndMemoryLatency(double* latencies) {
  // Pin to a single core for consistent results
  DWORD_PTR originalAffinity = SetThreadAffinityMask(GetCurrentThread(), 1);

  LOG_INFO << "\n===== Enhanced Cache and Memory Latency Test =====\n";
  std::random_device rd;
  std::mt19937 gen(rd());

  // Get reference to DiagnosticDataStore
  DiagnosticDataStore& dataStore = DiagnosticDataStore::getInstance();
  DiagnosticDataStore::CPUData cpuData = dataStore.getCPUData();
  
  LOG_INFO << "[Cache Test] Starting cache test - existing primeTime: " << cpuData.primeTime;

  // Define buffer sizes to test - from small L1 sizes to large memory sizes
  std::vector<size_t> bufferSizes = {
    4 * 1024,           // 4 KB - L1 cache
    8 * 1024,           // 8 KB - L1 cache
    16 * 1024,          // 16 KB - L1 cache
    32 * 1024,          // 32 KB - L1 cache
    64 * 1024,          // 64 KB - L1/L2 cache boundary
    96 * 1024,          // 96 KB - L2 cache
    128 * 1024,         // 128 KB - L2 cache
    192 * 1024,         // 192 KB - L2 cache
    256 * 1024,         // 256 KB - L2 cache
    384 * 1024,         // 384 KB - L2 cache
    512 * 1024,         // 512 KB - L2 cache
    768 * 1024,         // 768 KB - L2/L3 boundary
    1 * 1024 * 1024,    // 1 MB - L3 cache
    1 * 1024 * 1024,    // 1 MB duplicate (for repeated tests)
    2 * 1024 * 1024,    // 2 MB - L3 cache
    3 * 1024 * 1024,    // 3 MB - L3 cache
    4 * 1024 * 1024,    // 4 MB - L3 cache
    6 * 1024 * 1024,    // 6 MB - L3 cache
    8 * 1024 * 1024,    // 8 MB - L3 cache
    12 * 1024 * 1024,   // 12 MB - L3 cache
    16 * 1024 * 1024,   // 16 MB - L3 cache
    24 * 1024 * 1024,   // 24 MB - L3 cache
    32 * 1024 * 1024,   // 32 MB - L3 cache
    48 * 1024 * 1024,   // 48 MB - Main memory
    64 * 1024 * 1024,   // 64 MB - Main memory
    128 * 1024 * 1024,  // 128 MB - Main memory
    256 * 1024 * 1024,  // 256 MB - Main memory
  };

  // Store detected cache sizes
  int l1CacheKB = -1, l2CacheKB = -1, l3CacheKB = -1;

  // Get cache sizes from ConstantSystemInfo instead of SystemInfoProvider
  const auto& constInfo = SystemMetrics::GetConstantSystemInfo();
  l1CacheKB = constInfo.l1CacheKB;
  l2CacheKB = constInfo.l2CacheKB;
  l3CacheKB = constInfo.l3CacheKB;

  // Print detected cache sizes
  if (l1CacheKB > 0) LOG_INFO << "Detected L1 Cache: " << l1CacheKB << " KB";
  if (l2CacheKB > 0) LOG_INFO << "Detected L2 Cache: " << l2CacheKB << " KB";
  if (l3CacheKB > 0) LOG_INFO << "Detected L3 Cache: " << l3CacheKB << " KB";

  // Test various packet sizes (data access sizes)
  std::vector<size_t> packetSizes = {8,
                                     64};  // 8B (pointer) and 64B (cache line)

  LOG_INFO << "\nTesting memory latency with different packet sizes...\n";
  LOG_INFO << "Buffer Size | 8-byte Latency | 64-byte Latency | Memory Level\n";
  LOG_INFO << "------------------------------------------------------------\n";

  // Store all the raw latency measurements
  std::vector<double> allLatencies(bufferSizes.size());

  // For each buffer size
  for (size_t i = 0; i < bufferSizes.size(); i++) {
    size_t bufferSize = bufferSizes[i];
    LOG_INFO << std::setw(10)
             << (bufferSize < 1024 * 1024 ? bufferSize / 1024
                                          : bufferSize / (1024 * 1024))
             << (bufferSize < 1024 * 1024 ? " KB" : " MB") << " | ";

    std::vector<double> packetLatencies;

    // For each packet size
    for (size_t packetSize : packetSizes) {
      try {
        // Allocate buffer and create random access pattern
        size_t elementCount = bufferSize / packetSize;
        if (elementCount < 2) elementCount = 2;  // Ensure at least 2 elements

        // Use aligned allocation for better performance
        void* buffer = _aligned_malloc(elementCount * packetSize, 64);
        if (!buffer) throw std::bad_alloc();

        // Create random pointer-chasing pattern
        size_t* indices = new size_t[elementCount];
        for (size_t j = 0; j < elementCount; j++) {
          indices[j] = j;
        }
        std::shuffle(indices, indices + elementCount, gen);

        // Setup pointer chasing pattern
        if (packetSize == 8) {
          // 8-byte (64-bit) pointer chasing
          uint64_t* ptrs = static_cast<uint64_t*>(buffer);
          for (size_t j = 0; j < elementCount - 1; j++) {
            ptrs[indices[j]] =
              reinterpret_cast<uint64_t>(&ptrs[indices[j + 1]]);
          }
          ptrs[indices[elementCount - 1]] =
            reinterpret_cast<uint64_t>(&ptrs[indices[0]]);

          // Flush cache
          for (size_t j = 0; j < elementCount; j += 8) {
            _mm_clflush(&ptrs[j]);
          }
          _mm_mfence();

          // Warm up
          volatile uint64_t* p = &ptrs[0];
          for (int w = 0; w < 1000; w++) {
            p = reinterpret_cast<uint64_t*>(*p);
          }

          // Measure latency
          const int numIterations =
            std::min(10'000'000, static_cast<int>(elementCount * 100));
          auto start = std::chrono::high_resolution_clock::now();
          p = &ptrs[0];
          for (int iter = 0; iter < numIterations; iter++) {
            p = reinterpret_cast<uint64_t*>(*p);  // Follow pointer chain
          }
          auto end = std::chrono::high_resolution_clock::now();

          double nanoseconds =
            std::chrono::duration<double, std::nano>(end - start).count();
          double latencyNs = nanoseconds / numIterations;
          packetLatencies.push_back(latencyNs);

          LOG_INFO << std::fixed << std::setw(14) << std::setprecision(2)
                    << latencyNs << " ns | ";

          // Anti-optimization (use the result)
          volatile uint64_t dummy = reinterpret_cast<uint64_t>(p);
        } else if (packetSize == 64) {
          // 64-byte (cache line) access
          char* blocks = static_cast<char*>(buffer);

          // Create pointer chasing pattern with 64-byte alignment
          for (size_t j = 0; j < elementCount - 1; j++) {
            *reinterpret_cast<char**>(blocks + indices[j] * packetSize) =
              blocks + indices[j + 1] * packetSize;
          }
          *reinterpret_cast<char**>(blocks +
                                    indices[elementCount - 1] * packetSize) =
            blocks + indices[0] * packetSize;

          // Flush cache
          for (size_t j = 0; j < elementCount; j++) {
            _mm_clflush(blocks + j * packetSize);
          }
          _mm_mfence();

          // Warm up
          volatile char* p = blocks;
          for (int w = 0; w < 1000; w++) {
            p = *reinterpret_cast<char* volatile*>(const_cast<char*>(p));
          }

          // Measure latency
          const int numIterations =
            std::min(5'000'000, static_cast<int>(elementCount * 50));
          auto start = std::chrono::high_resolution_clock::now();
          p = blocks;
          for (int iter = 0; iter < numIterations; iter++) {
            p = *reinterpret_cast<char* volatile*>(
              const_cast<char*>(p));  // Follow pointer chain
          }
          auto end = std::chrono::high_resolution_clock::now();

          double nanoseconds =
            std::chrono::duration<double, std::nano>(end - start).count();
          double latencyNs = nanoseconds / numIterations;
          packetLatencies.push_back(latencyNs);

          LOG_INFO << std::fixed << std::setw(16) << std::setprecision(2)
                    << latencyNs << " ns | ";

          // Anti-optimization (use the result)
          volatile uint64_t dummy = reinterpret_cast<uint64_t>(p);
        }

        // Cleanup
        if (packetSize == 8) delete[] indices;
        _aligned_free(buffer);
      } catch (const std::exception& e) {
        LOG_ERROR << "Error: " << e.what() << " | ";
        packetLatencies.push_back(0.0);
      }
    }

    // Store best (lowest) valid latency for this buffer size
    double bestLatency = 0.0;
    for (double lat : packetLatencies) {
      if (lat > 0 && (bestLatency == 0.0 || lat < bestLatency)) {
        bestLatency = lat;
      }
    }
    allLatencies[i] = bestLatency;

    // Determine memory level based on actual detected cache sizes
    std::string memoryLevel = "Unknown";
    if (bufferSize <= l1CacheKB * 1024) {
      memoryLevel = "L1 Cache";
    } else if (bufferSize <= l2CacheKB * 1024) {
      memoryLevel = "L2 Cache";
    } else if (bufferSize <= l3CacheKB * 1024) {
      memoryLevel = "L3 Cache";
    } else {
      memoryLevel = "Main Memory";
    }

    LOG_INFO << memoryLevel;
  }

  // Group latencies by cache level
  std::vector<double> l1Latencies, l2Latencies, l3Latencies, ramLatencies;

  for (size_t i = 0; i < bufferSizes.size(); i++) {
    double latencyValue = allLatencies[i];
    size_t bufferSize = bufferSizes[i];

    if (latencyValue <= 0) continue;  // Skip invalid measurements

    // Categorize by size
    if (bufferSize <= l1CacheKB * 1024) {
      l1Latencies.push_back(latencyValue);
    } else if (bufferSize <= l2CacheKB * 1024) {
      l2Latencies.push_back(latencyValue);
    } else if (bufferSize <= l3CacheKB * 1024) {
      l3Latencies.push_back(latencyValue);
    } else {
      ramLatencies.push_back(latencyValue);
    }
  }

  // Calculate median latency for each level
  auto calculateMedian = [](std::vector<double>& values) -> double {
    if (values.empty()) return 0.0;

    std::sort(values.begin(), values.end());
    if (values.size() % 2 == 0) {
      return (values[values.size() / 2 - 1] + values[values.size() / 2]) / 2.0;
    } else {
      return values[values.size() / 2];
    }
  };

  double medianL1Latency = calculateMedian(l1Latencies);
  double medianL2Latency = calculateMedian(l2Latencies);
  double medianL3Latency = calculateMedian(l3Latencies);
  double medianRamLatency = calculateMedian(ramLatencies);

  // Print median latencies
  LOG_INFO << "\n===== Cache Latency Summary =====\n";
  LOG_INFO << "Cache Level | Median Latency | Sample Count\n";
  LOG_INFO << "-----------------------------------------\n";
  LOG_INFO << "L1 Cache    | " << std::fixed << std::setprecision(2)
            << medianL1Latency << " ns | " << l1Latencies.size() << "\n";
  LOG_INFO << "L2 Cache    | " << std::fixed << std::setprecision(2)
            << medianL2Latency << " ns | " << l2Latencies.size() << "\n";
  LOG_INFO << "L3 Cache    | " << std::fixed << std::setprecision(2)
            << medianL3Latency << " ns | " << l3Latencies.size() << "\n";
  LOG_INFO << "RAM Memory  | " << std::fixed << std::setprecision(2)
            << medianRamLatency << " ns | " << ramLatencies.size() << "\n";

  // Print full raw latency results
  LOG_INFO << "\n===== All Raw Latency Measurements =====\n";
  LOG_INFO << "Size | Latency (ns)\n";
  LOG_INFO << "------------------\n";
  for (size_t i = 0; i < bufferSizes.size(); i++) {
    size_t bufferSize = bufferSizes[i];
    LOG_INFO << std::setw(5)
              << (bufferSize < 1024 * 1024 ? bufferSize / 1024
                                           : bufferSize / (1024 * 1024))
              << (bufferSize < 1024 * 1024 ? " KB" : " MB") << " | "
              << std::fixed << std::setprecision(3) << allLatencies[i]
              << " ns\n";
  }

  // Store all raw measurements in the DiagnosticDataStore
  // dataStore = DiagnosticDataStore::getInstance();
  cpuData = dataStore.getCPUData();

  // Create a map of all buffer sizes to their latencies for easy lookup
  std::map<size_t, double> allLatenciesMap;
  for (size_t i = 0; i < bufferSizes.size(); i++) {
    size_t bufferSize = bufferSizes[i];
    size_t sizeKB = bufferSize / 1024;  // Convert to KB
    allLatenciesMap[sizeKB] = allLatencies[i];
  }

  // Update the cpuData with all the raw measurements
  cpuData.cache.rawLatencies = allLatenciesMap;

  // Also update the median latencies
  cpuData.cache.l1LatencyNs = medianL1Latency;
  cpuData.cache.l2LatencyNs = medianL2Latency;
  cpuData.cache.l3LatencyNs = medianL3Latency;
  cpuData.cache.ramLatencyNs = medianRamLatency;

  // Update cache sizes in DiagnosticDataStore
  cpuData.cache.l1SizeKB = l1CacheKB;
  cpuData.cache.l2SizeKB = l2CacheKB;
  cpuData.cache.l3SizeKB = l3CacheKB;

  // Fill the latencies array for DiagnosticDataStore
  if (latencies) {
    // Initialize all slots to -1 (update to 12)
    for (int i = 0; i < 12; i++) {
      cpuData.cache.latencies[i] = -1.0;
      latencies[i] = -1.0;
    }

    // Fill in the values for the UI-required sizes
    const size_t uiSizes[] = {32, 128, 1024, 8192, 32768};
    for (int i = 0; i < 5; i++) {
      size_t sizeKB = uiSizes[i];
      // Find the closest size we have a measurement for
      auto it = allLatenciesMap.find(sizeKB);
      if (it != allLatenciesMap.end()) {
        cpuData.cache.latencies[i] = it->second;
        latencies[i] = it->second;
      }
    }

    // Fill remaining slots with other interesting measurements if available
    if (allLatenciesMap.find(64) != allLatenciesMap.end()) {
      cpuData.cache.latencies[5] = allLatenciesMap[64];
      latencies[5] = allLatenciesMap[64];
    }
    if (allLatenciesMap.find(256) != allLatenciesMap.end()) {
      cpuData.cache.latencies[6] = allLatenciesMap[256];
      latencies[6] = allLatenciesMap[256];
    }
    if (allLatenciesMap.find(2048) != allLatenciesMap.end()) {
      cpuData.cache.latencies[7] = allLatenciesMap[2048];
      latencies[7] = allLatenciesMap[2048];
    }
    if (allLatenciesMap.find(16384) != allLatenciesMap.end()) {
      cpuData.cache.latencies[8] = allLatenciesMap[16384];
      latencies[8] = allLatenciesMap[16384];
    }
    if (allLatenciesMap.find(65536) != allLatenciesMap.end()) {
      cpuData.cache.latencies[9] = allLatenciesMap[65536];
      latencies[9] = allLatenciesMap[65536];
      // Log the value to verify it's being stored
      LOG_INFO << "64MB latency value stored: " << allLatenciesMap[65536] << " ns";
    }
    if (allLatenciesMap.find(262144) != allLatenciesMap.end()) {
      cpuData.cache.latencies[10] = allLatenciesMap[262144];
      latencies[10] = allLatenciesMap[262144];
    }
    // Add additional size if needed
    if (allLatenciesMap.find(524288) != allLatenciesMap.end()) {
      cpuData.cache.latencies[11] = allLatenciesMap[524288];
      latencies[11] = allLatenciesMap[524288];
    }
  }

  // Update the data store with the combined info
  LOG_INFO << "[Cache Test] Before setCPUData - primeTime: " << cpuData.primeTime;
  dataStore.setCPUData(cpuData);
  LOG_INFO << "[Cache Test] Cache test completed - data saved";

  // Restore original affinity
  SetThreadAffinityMask(GetCurrentThread(), originalAffinity);
}
