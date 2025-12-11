#pragma once
#ifndef THROTTLE_BOOST_TESTS_H
#define THROTTLE_BOOST_TESTS_H

#include <vector>

#include "diagnostic/CoreBoostMetrics.h"

// Forward declare enum from cpu_test.h to avoid duplication
enum CpuThrottlingTestMode;

// Throttling and boost test function declarations - ONLY implementation
// functions
void analyzeThrottlingImpact(double peakClock, double sustainedClock);
void testCPUBoostBehavior();
void testCPUBoostBehaviorPerCore();
void testPowerThrottling();
void testCombinedThrottling(int testDuration = 180);
void testThreadScheduling(int testDurationSeconds = 10);

// External references to global variables defined in cpu_test.cpp
extern std::vector<CoreBoostMetrics> g_cpuBoostMetrics;
extern double g_idleTotalPower;
extern double g_allCoreTotalPower;
extern int g_bestBoostCore;
extern int g_maxBoostDelta;

#endif  // THROTTLE_BOOST_TESTS_H
