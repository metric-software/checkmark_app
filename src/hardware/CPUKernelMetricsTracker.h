/*
 * CPUKernelMetricsTracker - ETW-based CPU Kernel Activity Monitoring
 * 
 * WORKING METRICS PROVIDED:
 * - contextSwitchesPerSec: Context switches per second
 * - interruptsPerSec: Interrupts per second  
 * - dpcCountPerSec: DPC (Deferred Procedure Call) count per second
 * - avgDpcLatencyUs: Average DPC latency in microseconds
 * - dpcLatenciesAbove50us: Percentage of DPCs with latency > 50μs
 * - dpcLatenciesAbove100us: Percentage of DPCs with latency > 100μs
 * - voluntaryContextSwitchesPerSec: Voluntary context switches per second
 * - involuntaryContextSwitchesPerSec: Involuntary context switches per second
 * - highPriorityInterruptionsPerSec: High priority interruptions per second
 * - priorityInversionsPerSec: Priority inversions per second
 * - avgThreadWaitTimeMs: Average thread wait time in milliseconds
 *
 * NOTE: Uses ETW (Event Tracing for Windows) to monitor kernel-level CPU activity.
 * Requires elevated privileges to access kernel provider events.
 */

#pragma once
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

#include <Windows.h>
#include <krabs.hpp>
#include <krabs/kernel_providers.hpp>

#include "../benchmark/BenchmarkDataPoint.h"

enum class ThreadWaitReason : uint16_t {
  Executive = 0,
  FreePage = 1,
  PageIn = 2,
  PoolAllocation = 3,
  DelayExecution = 4,
  Suspended = 5,
  UserRequest = 6,
  WrExecutive = 7,
  WrFreePage = 8,
  WrPageIn = 9,
  WrPoolAllocation = 10,
  WrDelayExecution = 11,
  WrSuspended = 12,
  WrUserRequest = 13,
  WrEventPair = 14,
  WrQueue = 15,
  WrLpcReceive = 16,
  WrLpcReply = 17,
  WrVirtualMemory = 18,
  WrPageOut = 19,
  WrRendezvous = 20,
  WrKeyedEvent = 21,
  WrTerminated = 22,
  WrProcessInSwap = 23,
  WrCpuRateControl = 24,
  WrCalloutStack = 25,
  WrKernel = 26,
  WrResource = 27,
  WrPushLock = 28,
  WrMutex = 29,
  WrQuantumEnd = 30,
  WrDispatchInt = 31,
  WrPreempted = 32,
  WrYieldExecution = 33,
  WrFastMutex = 34,
  WrGuardedMutex = 35,
  WrRundown = 36,
  WrAlertByThreadId = 37,
  WrDeferredPreempt = 38,
  MaximumWaitReason = 39
};

struct ThreadMetrics {
  std::atomic<uint64_t> voluntaryContextSwitches{0};
  std::atomic<uint64_t> involuntaryContextSwitches{0};
  std::atomic<uint64_t> highPriorityInterruptions{0};
  std::atomic<uint64_t> priorityInversions{0};
  std::atomic<uint64_t> mutexWaits{0};
  std::atomic<uint64_t> resourceWaits{0};
  std::atomic<uint64_t> ioWaits{0};
  std::atomic<double> totalThreadWaitTimeMs{0.0};
  std::atomic<uint64_t> waitCount{0};

  std::map<ThreadWaitReason, uint64_t> waitReasonCounts;
  std::mutex waitReasonMutex;
};

void processEvent(const EVENT_RECORD& record,
                  const krabs::trace_context& trace_context,
                  std::atomic<uint64_t>& contextSwitches,
                  std::atomic<uint64_t>& interrupts,
                  std::atomic<uint64_t>& dpcCount,
                  std::atomic<uint64_t>& totalDpcLatencyTicks);

class CPUKernelMetricsTracker {
 public:
  CPUKernelMetricsTracker();
  ~CPUKernelMetricsTracker();

  bool startTracking();
  void stopTracking();
  void updateBenchmarkData(BenchmarkDataPoint& dataPoint);

  friend void processEvent(const EVENT_RECORD& record,
                           const krabs::trace_context& trace_context,
                           std::atomic<uint64_t>& contextSwitches,
                           std::atomic<uint64_t>& interrupts,
                           std::atomic<uint64_t>& dpcCount,
                           std::atomic<uint64_t>& totalDpcLatencyTicks);

  ThreadMetrics& getThreadMetrics() { return m_threadMetrics; }
  LARGE_INTEGER getPerfFreq() const { return m_perfFreq; }
  friend bool cleanupExistingSession(const std::wstring& sessionName);

  // Raw data logging
  std::string logRawData();

 private:
  void combinedThreadProc();
  void updateMetrics();

  std::atomic<bool> m_running{false};
  std::atomic<bool> m_traceStartedSuccessfully{false};
  std::thread m_traceThread;
  std::unique_ptr<krabs::kernel_trace> m_traceSession;

  LARGE_INTEGER m_perfFreq;
  std::chrono::steady_clock::time_point m_lastUpdateTime;

  std::atomic<uint64_t> m_contextSwitches{0};
  std::atomic<uint64_t> m_interrupts{0};
  std::atomic<uint64_t> m_dpcCount{0};
  std::atomic<uint64_t> m_totalDpcLatencyTicks{0};

  ThreadMetrics m_threadMetrics;

  struct KernelMetrics {
    uint64_t contextSwitchesPerSec = 0;
    uint64_t interruptsPerSec = 0;
    uint64_t dpcCountPerSec = 0;
    double avgDpcLatencyUs = 0.0;
    double dpcLatenciesAbove50us = 0.0;
    double dpcLatenciesAbove100us = 0.0;
    uint64_t voluntaryContextSwitchesPerSec = 0;
    uint64_t involuntaryContextSwitchesPerSec = 0;
    uint64_t highPriorityInterruptionsPerSec = 0;
    uint64_t priorityInversionsPerSec = 0;
    double avgThreadWaitTimeMs = 0.0;
  };

  std::mutex m_metricsMutex;
  KernelMetrics m_latestMetrics;
};
