#pragma once
#ifndef CPU_TEST_H
#define CPU_TEST_H
#include <vector>

#include "diagnostic/CoreBoostMetrics.h"  // Include the new header

// CPU throttling test modes
enum CpuThrottlingTestMode {
  CpuThrottle_None,     // No CPU throttling tests
  CpuThrottle_Basic,    // Basic throttling tests (30 seconds)
  CpuThrottle_Extended  // Extended throttling tests (180 seconds)
};

// Forward declare the ColdStartResults struct
struct ColdStartResults;

void runCpuTests();
void printCpuInfo();
void testCacheLatency(double* latencies);
void testSIMD(double* simdScalar, double* simdAvx);
void runCpuBoostBehaviorTest();
void runCpuBoostBehaviorPerCoreTest();
void runCpuPowerThrottlingTest();
void runCombinedThrottlingTest(
  CpuThrottlingTestMode mode = CpuThrottle_Extended);
void runThreadSchedulingTest();
void runCpuColdStartTest();  // Add the new cold start test function

// Declare all global variables so they're accessible from other files
extern std::vector<CoreBoostMetrics> g_cpuBoostMetrics;
extern double g_idleTotalPower;
extern double g_allCoreTotalPower;
extern int g_bestBoostCore;
extern int g_maxBoostDelta;

#endif
