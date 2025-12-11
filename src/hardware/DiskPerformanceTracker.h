/*
 * DiskPerformanceTracker - ETW-based Disk I/O Performance Monitoring
 * 
 * WORKING METRICS PROVIDED:
 * - diskReadLatencyMs: Average disk read latency in milliseconds
 * - diskWriteLatencyMs: Average disk write latency in milliseconds
 * - diskQueueLength: Current disk queue length
 * - avgDiskQueueLength: Average disk queue length over collection period
 * - maxDiskQueueLength: Maximum disk queue length observed
 * - diskReadMB: Total disk read data in MB over collection period
 * - diskWriteMB: Total disk write data in MB over collection period
 * - minDiskReadLatencyMs: Minimum disk read latency in milliseconds
 * - maxDiskReadLatencyMs: Maximum disk read latency in milliseconds
 * - minDiskWriteLatencyMs: Minimum disk write latency in milliseconds
 * - maxDiskWriteLatencyMs: Maximum disk write latency in milliseconds
 *
 * NOTE: Uses ETW (Event Tracing for Windows) to monitor kernel-level disk I/O events.
 * Provides per-process and system-wide disk performance metrics.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <windows.h>

class krabs_user_trace_fwd;
struct BenchmarkDataPoint;

class DiskPerformanceTracker {
 public:
  DiskPerformanceTracker();
  ~DiskPerformanceTracker();

  bool startTracking();
  void stopTracking();
  void updateBenchmarkData(BenchmarkDataPoint& dataPoint);

  // Raw data logging
  std::string logRawData();

 private:
  struct DiskIoOperation {
    ULONGLONG timestamp;
    ULONGLONG requestDuration;
    ULONG size;
    bool isRead;
    std::wstring diskNumber;
  };

  struct DiskMetrics {
    double readLatencyMs = 0;
    double writeLatencyMs = 0;
    double queueLength = 0;
    double avgQueueLength = 0;
    double maxQueueLength = 0;
    double readMB = 0;
    double writeMB = 0;
    double minReadLatencyMs = -1;
    double maxReadLatencyMs = -1;
    double minWriteLatencyMs = -1;
    double maxWriteLatencyMs = -1;

    std::atomic<ULONGLONG> totalReadBytes{0};
    std::atomic<ULONGLONG> totalWriteBytes{0};
    std::atomic<ULONGLONG> readOperations{0};
    std::atomic<ULONGLONG> writeOperations{0};
    std::atomic<ULONGLONG> totalReadLatencyMs{0};
    std::atomic<ULONGLONG> totalWriteLatencyMs{0};

    std::chrono::steady_clock::time_point lastUpdate;
  };

  std::chrono::steady_clock::time_point m_lastMetricsReset;
  std::thread m_tracingThread;
  std::thread m_eventStatsThread;
  std::thread m_statsThread;
  std::atomic<bool> m_running{false};
  std::atomic<bool> m_threadsStopped{false};
  std::mutex m_threadControlMutex;

  std::mutex m_metricsMutex;
  DiskMetrics m_currentMetrics;

  std::atomic<int> m_currentQueueSize{0};
  int m_maxQueueSize = 0;
  std::vector<int> m_queueSizeSamples;
  std::mutex m_queueMutex;

  std::unordered_map<ULONGLONG, DiskIoOperation> m_pendingIoOperations;
  std::mutex m_ioMutex;

  std::atomic<size_t> m_totalEventsReceived{0};
  std::atomic<size_t> m_eventsProcessed{0};
  std::atomic<size_t> m_eventsFiltered{0};
  std::chrono::steady_clock::time_point m_trackingStartTime;

  void* m_activeSession = nullptr;
  std::mutex m_sessionMutex;

  void tracingThreadProc();
  void processCompletedIo(const DiskIoOperation& operation);
  void updateQueueStatistics();
  void resetPeriodicMetrics();
};
