#include "cpu_benchmarks.h"  // Add this to include the ColdStartResults struct

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include <windows.h>
#include <immintrin.h>

#include "logging/Logger.h"

#include "../cpu_test.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"

static double computeStdDev(const std::vector<double>& times, double mean) {
  double sumSq = 0.0;
  for (double t : times) {
    double diff = t - mean;
    sumSq += diff * diff;
  }
  return std::sqrt(sumSq / (times.size() - 1));
}

void testSIMD(double* simdScalar, double* simdAvx) {
  const int size = 1024 * 1024;
  const int numWarmupRuns = 2;
  const int numTestRuns = 5;
  std::vector<double> scalarTimings;
  std::vector<double> avxTimings;

  float* data1 = static_cast<float*>(_aligned_malloc(size * sizeof(float), 32));
  float* data2 = static_cast<float*>(_aligned_malloc(size * sizeof(float), 32));
  float* result_scalar =
    static_cast<float*>(_aligned_malloc(size * sizeof(float), 32));
  float* result_avx =
    static_cast<float*>(_aligned_malloc(size * sizeof(float), 32));

  // Initialize data
  for (int i = 0; i < size; i++) {
    data1[i] = static_cast<float>(i);
    data2[i] = static_cast<float>(i + 1);
  }

  // Warmup phase
  for (int warmup = 0; warmup < numWarmupRuns; warmup++) {
    // Scalar warmup
    for (int i = 0; i < size; i++) {
      result_scalar[i] = std::sqrt(data1[i]) * std::log(data2[i] + 1.0f);
    }

    // AVX warmup
    for (int i = 0; i < size; i += 8) {
      __m256 a = _mm256_load_ps(&data1[i]);
      __m256 b = _mm256_load_ps(&data2[i]);
      __m256 one = _mm256_set1_ps(1.0f);
      b = _mm256_add_ps(b, one);
      a = _mm256_sqrt_ps(a);
      b = _mm256_log_ps(b);
      __m256 c = _mm256_mul_ps(a, b);
      _mm256_store_ps(&result_avx[i], c);
    }
  }

  // Test phase
  for (int run = 0; run < numTestRuns; run++) {
    // Scalar operation
    auto start_scalar = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < size; i++) {
      result_scalar[i] = std::sqrt(data1[i]) * std::log(data2[i] + 1.0f);
    }
    auto end_scalar = std::chrono::high_resolution_clock::now();
    double scalar_time = std::chrono::duration_cast<std::chrono::microseconds>(
                           end_scalar - start_scalar)
                           .count();
    scalarTimings.push_back(scalar_time);

    // AVX operation
    auto start_avx = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < size; i += 8) {
      __m256 a = _mm256_load_ps(&data1[i]);
      __m256 b = _mm256_load_ps(&data2[i]);
      __m256 one = _mm256_set1_ps(1.0f);
      b = _mm256_add_ps(b, one);
      a = _mm256_sqrt_ps(a);
      b = _mm256_log_ps(b);
      __m256 c = _mm256_mul_ps(a, b);
      _mm256_store_ps(&result_avx[i], c);
    }
    auto end_avx = std::chrono::high_resolution_clock::now();
    double avx_time =
      std::chrono::duration_cast<std::chrono::microseconds>(end_avx - start_avx)
        .count();
    avxTimings.push_back(avx_time);
  }

  // Filter outliers for scalar timings
  if (scalarTimings.size() >= 4) {
    std::vector<double> sortedTimings = scalarTimings;
    std::sort(sortedTimings.begin(), sortedTimings.end());
    double median = sortedTimings[sortedTimings.size() / 2];

    std::vector<double> deviations;
    for (double t : scalarTimings) {
      deviations.push_back(std::abs(t - median));
    }
    std::sort(deviations.begin(), deviations.end());
    double mad = deviations[deviations.size() / 2];

    std::vector<double> filteredTimings;
    for (double t : scalarTimings) {
      if (std::abs(t - median) <= 2.0 * mad) {
        filteredTimings.push_back(t);
      }
    }

    if (filteredTimings.size() >= 3) {
      scalarTimings = filteredTimings;
    }
  }

  // Filter outliers for AVX timings
  if (avxTimings.size() >= 4) {
    std::vector<double> sortedTimings = avxTimings;
    std::sort(sortedTimings.begin(), sortedTimings.end());
    double median = sortedTimings[sortedTimings.size() / 2];

    std::vector<double> deviations;
    for (double t : avxTimings) {
      deviations.push_back(std::abs(t - median));
    }
    std::sort(deviations.begin(), deviations.end());
    double mad = deviations[deviations.size() / 2];

    std::vector<double> filteredTimings;
    for (double t : avxTimings) {
      if (std::abs(t - median) <= 2.0 * mad) {
        filteredTimings.push_back(t);
      }
    }

    if (filteredTimings.size() >= 3) {
      avxTimings = filteredTimings;
    }
  }

  // Calculate averages
  double totalScalarTime = 0;
  for (double t : scalarTimings) {
    totalScalarTime += t;
  }
  *simdScalar = totalScalarTime / scalarTimings.size();

  double totalAvxTime = 0;
  for (double t : avxTimings) {
    totalAvxTime += t;
  }
  *simdAvx = totalAvxTime / avxTimings.size();

  _aligned_free(data1);
  _aligned_free(data2);
  _aligned_free(result_scalar);
  _aligned_free(result_avx);
}

void testStreamBandwidth() {
  const size_t arraySize = 64 * 1024 * 1024;  // 64 MB
  float* a =
    static_cast<float*>(_aligned_malloc(arraySize * sizeof(float), 64));
  float* b =
    static_cast<float*>(_aligned_malloc(arraySize * sizeof(float), 64));
  float* c =
    static_cast<float*>(_aligned_malloc(arraySize * sizeof(float), 64));

  // Initialize
  for (size_t i = 0; i < arraySize; i++) {
    a[i] = 1.0f;
    b[i] = 2.0f;
    c[i] = 0.0f;
  }

  // STREAM Triad: a[i] = b[i] + scalar * c[i]
  const float scalar = 3.0f;
  auto start = std::chrono::high_resolution_clock::now();

#pragma omp parallel for
  for (size_t i = 0; i < arraySize; i++) {
    a[i] = b[i] + scalar * c[i];
  }

  auto end = std::chrono::high_resolution_clock::now();
  double seconds = std::chrono::duration<double>(end - start).count();
  double bandwidth =
    (3 * sizeof(float) * arraySize) / (seconds * 1024 * 1024 * 1024);  // GB/s

  _aligned_free(a);
  _aligned_free(b);
  _aligned_free(c);
}

void matrixMultiplication(int N) {
  float* A = new float[N * N];
  float* B = new float[N * N];
  float* C = new float[N * N];

  // Initialize matrices
  for (int i = 0; i < N * N; ++i) {
    A[i] = 1.0f;
    B[i] = 1.0f;
    C[i] = 0.0f;
  }

  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      for (int k = 0; k < N; ++k) {
        C[i * N + j] += A[i * N + k] * B[k * N + j];
      }
    }
  }

  delete[] A;
  delete[] B;
  delete[] C;
}

void singleCoreMatrixMultiplicationTest(int physicalCores, double* result) {
  const int N = 512;             // Matrix size
  const int numWarmupRuns = 20;  // More warmup runs
  const int numTestRuns = 25;    // More test runs for better statistics
  std::vector<double> timings;

  // Use a fixed core for consistent results between benchmark runs
  // Core 0 is typically one of the highest performing cores on most systems
  const int targetCore = 0;

  // Initial system-wide warmup to bring the CPU to a more stable state
  {
    std::vector<std::thread> warmupThreads;
    std::atomic<bool> keepRunning(true);

    // Start a short workload on all cores to stabilize system power state
    for (int i = 0; i < physicalCores; ++i) {
      warmupThreads.emplace_back([&keepRunning]() {
        volatile double result = 0.0;
        for (int j = 0; j < 5'000'000 && keepRunning; j++) {
          result += std::sin(result) * std::cos(result);
        }
      });
    }

    // Let the system run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    keepRunning = false;

    // Wait for all threads to finish
    for (auto& t : warmupThreads) {
      if (t.joinable()) t.join();
    }

    // Brief cooldown
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
  }

  // Get current thread and set affinity to target core
  HANDLE currentThread = GetCurrentThread();
  DWORD_PTR originalAffinity =
    SetThreadAffinityMask(currentThread, (DWORD_PTR)1 << targetCore);

  // Increase thread priority temporarily
  int originalPriority = GetThreadPriority(currentThread);
  SetThreadPriority(currentThread, THREAD_PRIORITY_HIGHEST);

  // Pre-allocate matrices to avoid allocation during tests
  // Use aligned memory for better cache behavior
  float* A = static_cast<float*>(_aligned_malloc(N * N * sizeof(float), 64));
  float* B = static_cast<float*>(_aligned_malloc(N * N * sizeof(float), 64));
  float* C = static_cast<float*>(_aligned_malloc(N * N * sizeof(float), 64));

  if (!A || !B || !C) {
    LOG_INFO << "ERROR: Failed to allocate aligned memory for matrices";
    if (A) _aligned_free(A);
    if (B) _aligned_free(B);
    if (C) _aligned_free(C);
    *result = -1;
    return;
  }

  // Initialize matrices with deterministic pattern for consistent cache behavior
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      // Use the same exact values for every benchmark run
      A[i * N + j] = static_cast<float>((i * j) % 8) * 0.01f + 0.5f;
      B[i * N + j] = static_cast<float>((i + j) % 16) * 0.01f + 1.0f;
      C[i * N + j] = 0.0f;
    }
  }

  // Define the computation function with cache clearing
  auto runComputation = [N](float* A, float* B, float* C) {
    // Memory fence to ensure timing accuracy
    _mm_mfence();

    // Explicitly flush cache to create consistent starting conditions
    for (int i = 0; i < N * N; i += 16) {
      _mm_clflush(&A[i]);
      _mm_clflush(&B[i]);
      _mm_clflush(&C[i]);
    }
    _mm_mfence();

    // Do matrix multiplication with cache-friendly blocking
    constexpr int BLOCK_SIZE = 32;  // Choose block size to fit in L1 cache

    for (int i0 = 0; i0 < N; i0 += BLOCK_SIZE) {
      for (int j0 = 0; j0 < N; j0 += BLOCK_SIZE) {
        for (int k0 = 0; k0 < N; k0 += BLOCK_SIZE) {
          // Process a block
          for (int i = i0; i < std::min(i0 + BLOCK_SIZE, N); i++) {
            for (int j = j0; j < std::min(j0 + BLOCK_SIZE, N); j++) {
              float sum = 0.0f;
              for (int k = k0; k < std::min(k0 + BLOCK_SIZE, N); k++) {
                sum += A[i * N + k] * B[k * N + j];
              }
              C[i * N + j] += sum;
            }
          }
        }
      }
    }

    // Final fence to ensure all computation is complete
    _mm_mfence();

    // Prevent optimization by using the result
    volatile float checksum = 0.0f;
    for (int i = 0; i < N * N; i += 64) {
      checksum += C[i];
    }
  };

  // More aggressive warmup phase to stabilize CPU frequency

  // First do some continuous work to get the CPU into a higher frequency state
  {
    auto start = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::milliseconds(0);

    // Run continuous workload for at least 2 seconds to ensure stable frequency
    while (elapsed < std::chrono::milliseconds(2000)) {
      runComputation(A, B, C);
      elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    }
  }

  // Clear the matrices for actual warmup runs
  for (int i = 0; i < N * N; ++i) {
    C[i] = 0.0f;
  }

  // Now do normal warmup cycles
  for (int i = 0; i < numWarmupRuns; i++) {
    runComputation(A, B, C);

    // Add a small sleep between warmup runs to mimic the timing pattern of
    // actual test runs
    if (i % 5 == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  // Ensure CPU governor is in high-performance state after warmup
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Clear the matrices for the actual test
  for (int i = 0; i < N * N; ++i) {
    C[i] = 0.0f;
  }

  // Test phase with more runs for better statistics

  std::vector<double> allTimings;
  allTimings.reserve(numTestRuns);

  for (int run = 0; run < numTestRuns; run++) {
    // Ensure consistent starting state
    _mm_mfence();

    auto start = std::chrono::high_resolution_clock::now();

    // Perform actual test computation
    runComputation(A, B, C);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start)
        .count() /
      1000.0;
    allTimings.push_back(duration);

    // Add fixed delay between tests to allow stable timing pattern
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Free allocated matrices
  _aligned_free(A);
  _aligned_free(B);
  _aligned_free(C);

  // Restore original thread priority and affinity
  SetThreadPriority(currentThread, originalPriority);
  SetThreadAffinityMask(currentThread, originalAffinity);

  // Analyze the results using robust statistics
  if (allTimings.size() >= 5) {
    // Calculate median (more robust than mean against outliers)
    std::vector<double> sortedTimings = allTimings;
    std::sort(sortedTimings.begin(), sortedTimings.end());
    double median = sortedTimings[sortedTimings.size() / 2];

    // Calculate median absolute deviation (MAD)
    std::vector<double> deviations;
    for (double t : allTimings) {
      deviations.push_back(std::abs(t - median));
    }
    std::sort(deviations.begin(), deviations.end());
    double mad = deviations[deviations.size() / 2];

    // Filter outliers (more than 3 * MAD from median)
    std::vector<double> filteredTimings;
    for (double t : allTimings) {
      if (std::abs(t - median) <= 3.0 * mad) {
        filteredTimings.push_back(t);
      }
    }

    // Recalculate median with filtered values
    if (filteredTimings.size() >= 5) {
      std::sort(filteredTimings.begin(), filteredTimings.end());
      *result = filteredTimings[filteredTimings.size() / 2];
    } else {
      // If we don't have enough filtered values, use the original median
      *result = median;
    }

    // Calculate and display statistics
    double sum = 0.0;
    for (double t : filteredTimings) {
      sum += t;
    }
    double mean = sum / filteredTimings.size();

    double variance = 0.0;
    for (double t : filteredTimings) {
      double diff = t - mean;
      variance += diff * diff;
    }
    variance /= filteredTimings.size();
    double stdDev = std::sqrt(variance);
    double cv = (stdDev / mean) * 100.0;  // Coefficient of variation

    double min = filteredTimings.front();
    double max = filteredTimings.back();
    double range = max - min;
    double rangePercent = (range / *result) * 100.0;
  } else {
    // Fallback if we don't have enough samples
    std::vector<double> sortedTimings = allTimings;
    std::sort(sortedTimings.begin(), sortedTimings.end());
    *result = sortedTimings[sortedTimings.size() / 2];
  }
}

double testPrimeCalculation() {
  const int limit = 1000000;
  const int numWarmupRuns = 2;
  const int numTestRuns = 5;
  std::vector<double> timings;

  // Warmup phase
  for (int i = 0; i < numWarmupRuns; i++) {
    int count = 0;
    for (int n = 2; n < limit; n++) {
      bool isPrime = true;
      for (int j = 2; j * j <= n; j++) {  // Fixed: changed 'i' to 'j' to avoid shadow variable
        if (n % j == 0) {
          isPrime = false;
          break;
        }
      }
      if (isPrime) count++;
    }
    // Prevent compiler optimization by using the count
    volatile int preventOptimization = count;
  }

  // Test phase
  for (int run = 0; run < numTestRuns; run++) {
    int count = 0;
    auto start = std::chrono::high_resolution_clock::now();

    for (int n = 2; n < limit; n++) {
      bool isPrime = true;
      for (int j = 2; j * j <= n; j++) {  // Fixed: changed 'i' to 'j' to avoid shadow variable
        if (n % j == 0) {
          isPrime = false;
          break;
        }
      }
      if (isPrime) count++;
    }

    auto end = std::chrono::high_resolution_clock::now();
    
    // Use microseconds for better precision, then convert to milliseconds
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double duration_ms = duration_us / 1000.0;
    
    LOG_INFO << "[Prime Test] Run " << (run + 1) << ": Found " << count << " primes, took " 
             << duration_us << " microseconds (" << duration_ms << " ms)";
    
    timings.push_back(duration_ms);
    
    // Prevent compiler optimization by using the count
    volatile int preventOptimization = count;
  }

  // Filter outliers
  if (timings.size() >= 4) {
    std::vector<double> sortedTimings = timings;
    std::sort(sortedTimings.begin(), sortedTimings.end());
    double median = sortedTimings[sortedTimings.size() / 2];

    std::vector<double> deviations;
    for (double t : timings) {
      deviations.push_back(std::abs(t - median));
    }
    std::sort(deviations.begin(), deviations.end());
    double mad = deviations[deviations.size() / 2];

    std::vector<double> filteredTimings;
    for (double t : timings) {
      if (std::abs(t - median) <= 2.0 * mad) {
        filteredTimings.push_back(t);
      }
    }

    if (filteredTimings.size() >= 3) {
      timings = filteredTimings;
    }
  }

  // Calculate average
  double totalTime = 0;
  for (double t : timings) {
    totalTime += t;
  }
  double averageTime = totalTime / timings.size();
  
  LOG_INFO << "[Prime Test] Completed with " << timings.size() << " samples, average: " << averageTime << " ms";
  
  return averageTime;
}

void singleCoreMatrixMultiplicationTest(int physicalCores) {
  const int N = 512;  // Matrix size
  auto start = std::chrono::high_resolution_clock::now();
  for (int repeat = 0; repeat < physicalCores;
       ++repeat) {  // Perform 8 calculations per core
    matrixMultiplication(N);
  }
  auto end = std::chrono::high_resolution_clock::now();
  auto duration =
    std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

double testGameSimulation(size_t tier1_size, size_t tier2_size,
                          size_t tier3_size) {
  // Fixed parameters
  constexpr size_t PLAYER_COUNT = 64;
  constexpr size_t ITERATIONS = 5'000'000;
  constexpr size_t HEALTH_UPDATE_FREQ = 100;

  // Increase memory access frequency significantly
  constexpr double TIER1_PROB = 0.80;  // 80% chance - L1 cache testing
  constexpr double TIER2_PROB = 0.60;  // 60% chance - L2/L3 cache testing
  constexpr double TIER3_PROB = 0.40;  // 40% chance - RAM testing

  // More cache-focused sizes for tiers
  const size_t TIER1_COUNT =
    tier1_size / sizeof(int);  // Target L1 cache (~32KB-64KB)
  const size_t TIER2_COUNT =
    tier2_size / sizeof(int);  // Target L2/L3 cache (~256KB-6MB)
  const size_t TIER3_COUNT = tier3_size / sizeof(int);  // Exceed cache (~32MB+)

  // Cache line size for alignment
  constexpr size_t CACHE_LINE = 64;

  // Align struct to cache line
  struct alignas(CACHE_LINE) PlayerState {
    float x, y, z;
    float velocity[3];
    int health;
    int team;
    char padding[CACHE_LINE - (sizeof(float) * 6 + sizeof(int) * 2)];
  };

  volatile int sink = 0;

  // Initialize aligned memory for players
  std::vector<PlayerState> players(PLAYER_COUNT);

  // Initialize all memory tiers with alignment
  std::vector<int> tier1_data(TIER1_COUNT);
  std::vector<int> tier2_data(TIER2_COUNT);
  std::vector<int> tier3_data(TIER3_COUNT);

  // Initialize indices for random access
  std::vector<size_t> indices1(TIER1_COUNT), indices2(TIER2_COUNT),
    indices3(TIER3_COUNT);
  std::iota(indices1.begin(), indices1.end(), 0);
  std::iota(indices2.begin(), indices2.end(), 0);
  std::iota(indices3.begin(), indices3.end(), 0);

  std::mt19937 rng(42);
  std::shuffle(indices1.begin(), indices1.end(), rng);
  std::shuffle(indices2.begin(), indices2.end(), rng);
  std::shuffle(indices3.begin(), indices3.end(), rng);

  // Create scattered memory access patterns
  size_t ptr1 = 0, ptr2 = 0, ptr3 = 0;
  std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
  std::uniform_int_distribution<size_t> jump_dist(1, 16);

  auto start_time = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < ITERATIONS; i++) {
    // Hot path - always accessed
    size_t playerIndex = i % PLAYER_COUNT;
    PlayerState& p = players[playerIndex];
    p.x += p.velocity[0] * 0.016f;
    p.y += p.velocity[1] * 0.016f;
    p.z += p.velocity[2] * 0.016f;

    if ((i % HEALTH_UPDATE_FREQ) == 0) {
      p.health -= 10;
      sink += p.health;
    }

    // Intense cache testing with random jumps
    if (prob_dist(rng) < TIER1_PROB) {
      ptr1 = (ptr1 + jump_dist(rng)) % TIER1_COUNT;
      volatile int val1 = tier1_data[indices1[ptr1]];
      sink += val1;
      tier1_data[indices1[ptr1]] = val1 + 1;
    }

    if (prob_dist(rng) < TIER2_PROB) {
      ptr2 = (ptr2 + jump_dist(rng)) % TIER2_COUNT;
      volatile int val2 = tier2_data[indices2[ptr2]];
      sink += val2;
      tier2_data[indices2[ptr2]] = val2 + 1;
    }

    if (prob_dist(rng) < TIER3_PROB) {
      ptr3 = (ptr3 + jump_dist(rng)) % TIER3_COUNT;
      volatile int val3 = tier3_data[indices3[ptr3]];
      sink += val3;
      tier3_data[indices3[ptr3]] = val3 + 1;
    }
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  double duration =
    std::chrono::duration<double>(end_time - start_time).count();
  double updatesPerSecond = static_cast<double>(ITERATIONS) / duration;

  return updatesPerSecond;
}

// Function to test CPU cold start response time
ColdStartResults testCpuColdStart(int numTests, int delayMinMs,
                                  int delayMaxMs) {
  std::vector<double> responseTimes;
  responseTimes.reserve(numTests);

  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> delayDist(delayMinMs, delayMaxMs);

  // Define a small, intense CPU workload
  auto shortWorkload = []() {
    const int DATA_SIZE = 10000;
    std::vector<double> data(DATA_SIZE);

    // Fill with random but deterministic values
    for (int i = 0; i < DATA_SIZE; i++) {
      data[i] = std::sin(static_cast<double>(i));
    }

    auto start = std::chrono::high_resolution_clock::now();

    // Perform mixed operations: sort, math, and memory access
    std::sort(data.begin(), data.end());

    double sum = 0.0;
    for (int i = 0; i < DATA_SIZE; i++) {
      sum += std::sqrt(std::abs(data[i])) * std::log(1.0 + std::abs(data[i]));
      // Random memory access to surprise the cache
      int idx = (i * 97) % DATA_SIZE;
      sum += data[idx];
    }

    auto end = std::chrono::high_resolution_clock::now();
    double microseconds =
      std::chrono::duration<double, std::micro>(end - start).count();

    // Prevent optimization
    volatile double result = sum;

    return microseconds;
  };

  for (int test = 0; test < numTests; test++) {
    // Sleep for a random amount of time to allow CPU to enter lower power state
    int sleepTime = delayDist(gen);
    std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));

    // Run the workload and measure response time
    double responseTime = shortWorkload();
    responseTimes.push_back(responseTime);
  }

  // Calculate statistics
  double totalTime = 0.0;
  double minTime = responseTimes[0];
  double maxTime = responseTimes[0];

  for (double time : responseTimes) {
    totalTime += time;
    minTime = std::min(minTime, time);
    maxTime = std::max(maxTime, time);
  }

  double avgTime = totalTime / numTests;
  double variance = 0.0;

  for (double time : responseTimes) {
    variance += (time - avgTime) * (time - avgTime);
  }
  variance /= numTests;
  double stdDev = std::sqrt(variance);

  // Calculate coefficient of variation (CV) - standardized measure of dispersion
  double cv = (stdDev / avgTime) * 100.0;

  // Return the results
  ColdStartResults results;
  results.avgResponseTime = avgTime;
  results.minResponseTime = minTime;
  results.maxResponseTime = maxTime;
  results.variance = variance;
  results.stdDev = stdDev;
  results.rawTimes = responseTimes;

  // Store results in DiagnosticDataStore
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

  return results;
}

void matrixMultiplicationWithThreads(int N, int numThreads, double* result) {
  const int numWarmupRuns =
    20;  // Increased from 5 to 20 (like single-core test)
  const int numTestRuns =
    25;  // Increased from 10 to 25 (like single-core test)
  std::vector<double> timings;

  // Check system load before starting the test
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
  int numCores = constantInfo.logicalCores;
  
  double avgSystemLoad = 0.0;
  // For CPU benchmarks, we'll proceed regardless of current system load

  // Initial system-wide warmup to bring the CPU to a more stable state
  {
    std::vector<std::thread> warmupThreads;
    std::atomic<bool> keepRunning(true);

    // Start a short workload on all cores to stabilize system power state
    for (int i = 0; i < numCores; ++i) {
      warmupThreads.emplace_back([&keepRunning]() {
        volatile double result = 0.0;
        for (int j = 0; j < 5'000'000 && keepRunning; j++) {
          result += std::sin(result) * std::cos(result);
        }
      });
    }

    // Let the system run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    keepRunning = false;

    // Wait for all threads to finish
    for (auto& t : warmupThreads) {
      if (t.joinable()) t.join();
    }

    // Brief cooldown
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
  }

  // Define the thread work function with more robustness features
  auto threadTask = [N](int coreId) {
    // Pin this thread to a specific core
    HANDLE currentThread = GetCurrentThread();
    SetThreadAffinityMask(currentThread, (DWORD_PTR)1 << coreId);

    // Increase thread priority for more consistent timing
    int originalPriority = GetThreadPriority(currentThread);
    SetThreadPriority(currentThread, THREAD_PRIORITY_HIGHEST);

    // Allocate aligned memory for matrices
    float* A = static_cast<float*>(_aligned_malloc(N * N * sizeof(float), 64));
    float* B = static_cast<float*>(_aligned_malloc(N * N * sizeof(float), 64));
    float* C = static_cast<float*>(_aligned_malloc(N * N * sizeof(float), 64));

    // Initialize matrices with deterministic pattern for consistent cache
    // behavior
    for (int i = 0; i < N; ++i) {
      for (int j = 0; j < N; ++j) {
        // Use the same exact values for every benchmark run
        A[i * N + j] = static_cast<float>((i * j) % 8) * 0.01f + 0.5f;
        B[i * N + j] = static_cast<float>((i + j) % 16) * 0.01f + 1.0f;
        C[i * N + j] = 0.0f;
      }
    }

    // Memory fence to ensure starting state consistency
    _mm_mfence();

    // Explicitly flush cache to create more consistent behavior
    for (int i = 0; i < N * N; i += 16) {
      _mm_clflush(&A[i]);
      _mm_clflush(&B[i]);
      _mm_clflush(&C[i]);
    }
    _mm_mfence();

    // Perform matrix multiplication with cache-friendly blocking
    constexpr int BLOCK_SIZE = 32;  // Choose block size to fit in L1 cache

    for (int i0 = 0; i0 < N; i0 += BLOCK_SIZE) {
      for (int j0 = 0; j0 < N; j0 += BLOCK_SIZE) {
        for (int k0 = 0; k0 < N; k0 += BLOCK_SIZE) {
          // Process a block
          for (int i = i0; i < std::min(i0 + BLOCK_SIZE, N); i++) {
            for (int j = j0; j < std::min(j0 + BLOCK_SIZE, N); j++) {
              float sum = 0.0f;
              for (int k = k0; k < std::min(k0 + BLOCK_SIZE, N); k++) {
                sum += A[i * N + k] * B[k * N + j];
              }
              C[i * N + j] += sum;
            }
          }
        }
      }
    }

    // Final fence to ensure all computation is complete
    _mm_mfence();

    // Anti-optimization check
    volatile float checksum = 0.0f;
    for (int i = 0; i < N; i++) {
      checksum += C[i * N + i];
    }

    // Restore original thread priority
    SetThreadPriority(currentThread, originalPriority);

    // Free memory
    _aligned_free(A);
    _aligned_free(B);
    _aligned_free(C);
  };

  // Continuous workload warmup phase (like single-core test)
  {
    auto start = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::milliseconds(0);
    std::vector<std::thread> threads;

    // Run continuous workload for 2 seconds to ensure stable frequency
    while (elapsed < std::chrono::milliseconds(2000)) {
      // Start threads
      threads.clear();
      for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(threadTask, i % numCores);
      }

      // Wait for all threads
      for (auto& t : threads) {
        t.join();
      }

      elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::high_resolution_clock::now() - start);
    }
  }

  // Regular warmup phase
  for (int i = 0; i < numWarmupRuns; i++) {
    std::vector<std::thread> threads;

    // Create threads and distribute them across cores
    for (int j = 0; j < numThreads; ++j) {
      // Use consistent core mapping strategy
      int coreId = j % numCores;
      threads.emplace_back(threadTask, coreId);
    }

    for (auto& t : threads) {
      t.join();
    }

    // Brief cooldown between warmup runs with consistent pattern
    if (i % 5 == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  // Ensure CPU governor is in high-performance state after warmup
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Test phase
  for (int run = 0; run < numTestRuns; run++) {
    std::vector<std::thread> threads;

    // Memory fence before timing
    _mm_mfence();

    auto start = std::chrono::high_resolution_clock::now();

    // Create thread for each core with explicit affinity
    for (int i = 0; i < numThreads; ++i) {
      // Use same core mapping strategy as in warmup
      int coreId = i % numCores;
      threads.emplace_back(threadTask, coreId);
    }

    for (auto& t : threads) {
      t.join();
    }

    // Memory fence after timing to ensure all work is done
    _mm_mfence();

    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
        .count();
    timings.push_back(duration);

    // Add fixed delay between tests to allow stable timing pattern
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  // Statistical analysis - use robust statistics like in single-core test
  if (timings.size() >= 5) {
    std::vector<double> sortedTimings = timings;
    std::sort(sortedTimings.begin(), sortedTimings.end());

    // Calculate median and median absolute deviation (MAD)
    double median = sortedTimings[sortedTimings.size() / 2];

    std::vector<double> deviations;
    for (double t : timings) {
      deviations.push_back(std::abs(t - median));
    }
    std::sort(deviations.begin(), deviations.end());
    double mad = deviations[deviations.size() / 2];

    // Filter extreme outliers (more than 3 MADs from median)
    std::vector<double> filteredTimings;
    for (double t : timings) {
      if (std::abs(t - median) <= 3.0 * mad) {
        filteredTimings.push_back(t);
      }
    }

    // Use the median of filtered values for final result
    if (filteredTimings.size() >= 5) {
      std::sort(filteredTimings.begin(), filteredTimings.end());
      *result = filteredTimings[filteredTimings.size() / 2];

      // Calculate and display statistics
      double sum = 0.0;
      for (double t : filteredTimings) {
        sum += t;
      }
      double mean = sum / filteredTimings.size();

      double variance = 0.0;
      for (double t : filteredTimings) {
        double diff = t - mean;
        variance += diff * diff;
      }
      variance /= filteredTimings.size();
      double stdDev = std::sqrt(variance);
      double cv = (stdDev / mean) * 100.0;  // Coefficient of variation

      double min = filteredTimings.front();
      double max = filteredTimings.back();
      double range = max - min;
      double rangePercent = (range / *result) * 100.0;
    } else {
      // If we don't have enough filtered values, use the original median
      *result = median;
    }
  } else {
    // For very few samples, just use median
    std::vector<double> sortedTimings = timings;
    std::sort(sortedTimings.begin(), sortedTimings.end());
    *result = sortedTimings[sortedTimings.size() / 2];
  }
}

void fourThreadMatrixMultiplicationTest(int threadCount, double* result) {
  if (threadCount < 4) {
    *result = -1;
    return;
  }

  const int N = 512;  // Matrix size
  matrixMultiplicationWithThreads(N, 4, result);
}

void eightThreadMatrixMultiplicationTest(int threadCount, double* result) {
  if (threadCount < 8) {
    *result = -1;
    return;
  }

  const int N = 512;  // Matrix size
  matrixMultiplicationWithThreads(N, 8, result);
}
