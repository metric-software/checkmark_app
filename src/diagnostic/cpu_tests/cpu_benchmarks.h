#pragma once
#ifndef CPU_BENCHMARKS_H
#define CPU_BENCHMARKS_H

#include <vector>

// Structure to store cold start test results
struct ColdStartResults {
  double avgResponseTime;
  double minResponseTime;
  double maxResponseTime;
  double variance;
  double stdDev;
  std::vector<double> rawTimes;
};

// CPU benchmark function declarations
void testSIMD(double* simdScalar, double* simdAvx);
void testStreamBandwidth();
void matrixMultiplication(int N);
void singleCoreMatrixMultiplicationTest(int physicalCores, double* result);
void fourThreadMatrixMultiplicationTest(int threadCount, double* result);
void eightThreadMatrixMultiplicationTest(int threadCount, double* result);
double testPrimeCalculation();
double testGameSimulation(size_t tier1_size, size_t tier2_size,
                          size_t tier3_size);
void testCacheAndMemoryLatency(double* latencies);
ColdStartResults testCpuColdStart(int numTests = 10, int delayMinMs = 500,
                                  int delayMaxMs = 2000);

#endif  // CPU_BENCHMARKS_H
