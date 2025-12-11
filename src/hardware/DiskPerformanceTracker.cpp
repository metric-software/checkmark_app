#include "DiskPerformanceTracker.h"

#include <iomanip>
#include <iostream>
#include <numeric>

#include "benchmark/BenchmarkDataPoint.h"
#include "../logging/Logger.h"


#ifndef NTDDI_VERSION
#define NTDDI_VERSION NTDDI_WIN7
#endif
#include <krabs.hpp>
#include <krabs/perfinfo_groupmask.hpp>

// Constants
static constexpr int STATISTICS_UPDATE_INTERVAL_MS = 250;
static constexpr int METRICS_LOG_INTERVAL_SECONDS = 10;
static constexpr int METRICS_UPDATE_INTERVAL_SECONDS = 1;
static constexpr double MIN_VALID_LATENCY_MS = 0.001;
static constexpr int MAX_LOGGED_EVENTS = 50;
static constexpr int SESSION_JOIN_TIMEOUT_SECONDS = 2;
static constexpr int METRICS_RESET_INTERVAL_SECONDS = 1;

// ETW event opcodes
static constexpr BYTE DISK_IO_READ_OPCODE = 32;
static constexpr BYTE DISK_IO_WRITE_OPCODE = 33;
static constexpr BYTE DISK_IO_COMPLETION_OPCODE = 36;
static constexpr BYTE FILE_OPERATION_OPCODE = 0;
static constexpr ULONGLONG FILE_READ_EVENT_ID = 15;
static constexpr ULONGLONG FILE_WRITE_EVENT_ID = 16;
static constexpr ULONGLONG FILE_OPERATION_END_EVENT_ID = 24;

DiskPerformanceTracker::DiskPerformanceTracker()
    : m_running(false), m_threadsStopped(false), m_currentQueueSize(0),
      m_maxQueueSize(0), m_totalEventsReceived(0), m_eventsProcessed(0),
      m_eventsFiltered(0) {
  m_currentMetrics.lastUpdate = std::chrono::steady_clock::now();
  m_lastMetricsReset = m_currentMetrics.lastUpdate;
}

DiskPerformanceTracker::~DiskPerformanceTracker() { stopTracking(); }

bool DiskPerformanceTracker::startTracking() {
  if (m_running) {
    return true;
  }

  m_running = true;
  m_trackingStartTime = std::chrono::steady_clock::now();

  m_totalEventsReceived = 0;
  m_eventsProcessed = 0;
  m_eventsFiltered = 0;

  {
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    DiskMetrics newMetrics;
    newMetrics.lastUpdate = std::chrono::steady_clock::now();
    m_currentMetrics.readLatencyMs = 0;
    m_currentMetrics.writeLatencyMs = 0;
    m_currentMetrics.queueLength = 0;
    m_currentMetrics.avgQueueLength = 0;
    m_currentMetrics.maxQueueLength = 0;
    m_currentMetrics.readMB = 0;
    m_currentMetrics.writeMB = 0;

    m_currentMetrics.totalReadBytes.store(0);
    m_currentMetrics.totalWriteBytes.store(0);
    m_currentMetrics.readOperations.store(0);
    m_currentMetrics.writeOperations.store(0);
    m_currentMetrics.totalReadLatencyMs.store(0);
    m_currentMetrics.totalWriteLatencyMs.store(0);
    m_currentMetrics.lastUpdate = newMetrics.lastUpdate;
    m_currentMetrics.minReadLatencyMs = -1;
    m_currentMetrics.maxReadLatencyMs = -1;
    m_currentMetrics.minWriteLatencyMs = -1;
    m_currentMetrics.maxWriteLatencyMs = -1;
  }

  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_queueSizeSamples.clear();
    m_maxQueueSize = 0;
    m_currentQueueSize = 0;
  }

  {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    m_pendingIoOperations.clear();
  }

  m_tracingThread =
    std::thread(&DiskPerformanceTracker::tracingThreadProc, this);
  return true;
}

void DiskPerformanceTracker::stopTracking() {
  LOG_INFO << "DiskPerformanceTracker: Beginning shutdown sequence...";

  // First, check if already stopped to avoid double-stopping
  bool wasRunning = true;
  if (!m_running.compare_exchange_strong(wasRunning, false)) {
    LOG_INFO << "DiskPerformanceTracker: Already stopped or stopping";
    return;  // Already stopped
  }

  LOG_INFO << "DiskPerformanceTracker: Set running flag to false";

  // Use a safeguard to ensure cleanup happens even if something fails
  auto cleanup = [this]() {
    LOG_INFO << "DiskPerformanceTracker: Final cleanup";

    // Reset all state values regardless of thread join success
    {
      std::lock_guard<std::mutex> lock(m_metricsMutex);
      m_currentMetrics.totalReadBytes.store(0);
      m_currentMetrics.totalWriteBytes.store(0);
      m_currentMetrics.readOperations.store(0);
      m_currentMetrics.writeOperations.store(0);
    }

    {
      std::lock_guard<std::mutex> lock(m_queueMutex);
      m_queueSizeSamples.clear();
      m_currentQueueSize.store(0);
      m_maxQueueSize = 0;
    }

    {
      std::lock_guard<std::mutex> lock(m_ioMutex);
      m_pendingIoOperations.clear();
    }

    // Finally reset the session pointer
    {
      std::lock_guard<std::mutex> lock(m_sessionMutex);
      m_activeSession = nullptr;
    }

    m_threadsStopped = true;
    LOG_INFO << "DiskPerformanceTracker: Shutdown complete";
  };

  try {
    // Step 1: Stop the ETW session first to unblock the trace thread
    LOG_INFO << "DiskPerformanceTracker: Stopping ETW session...";
    bool sessionStopped = false;
    {
      std::lock_guard<std::mutex> lock(m_sessionMutex);
      if (m_activeSession) {
        try {
          krabs::user_trace* session =
            static_cast<krabs::user_trace*>(m_activeSession);
          session->stop();
          sessionStopped = true;
          LOG_INFO << "DiskPerformanceTracker: ETW session stopped successfully";
        } catch (const std::exception& e) {
          LOG_ERROR << "DiskPerformanceTracker: Error stopping ETW session: " << e.what();
        }
      } else {
        LOG_INFO << "DiskPerformanceTracker: No active ETW session to stop";
      }
    }

    // Force ETW session stop using ControlTrace if our approach failed
    if (!sessionStopped) {
      LOG_WARN << "DiskPerformanceTracker: Forcing ETW session cleanup with ControlTrace";
      try {
        EVENT_TRACE_PROPERTIES props = {};
        props.Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES);
        ULONG status = ControlTraceW(0, L"DiskPerformanceTracker", &props,
                                     EVENT_TRACE_CONTROL_STOP);
        LOG_INFO << "DiskPerformanceTracker: Forced ETW session cleanup returned status: " << status;
      } catch (...) {
        LOG_ERROR << "DiskPerformanceTracker: Error during forced ETW cleanup";
      }
    }

    // Step 2: Wait a bit to allow the tracing thread to detect the session stop
    LOG_INFO << "DiskPerformanceTracker: Waiting for threads to react to ETW session stop...";
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Step 3: Join each thread with a timeout and detailed error reporting
    auto joinThreadWithTimeout = [](std::thread& t, const char* threadName,
                                    int timeoutMs) -> bool {
      if (!t.joinable()) {
        LOG_INFO << "DiskPerformanceTracker: Thread " << threadName
                  << " not joinable";
        return true;
      }

      LOG_INFO << "DiskPerformanceTracker: Joining " << threadName << " with "
                << timeoutMs << "ms timeout...";

      std::mutex m;
      std::condition_variable cv;
      bool joined = false;

      std::thread joiner([&]() {
        LOG_DEBUG << "DiskPerformanceTracker: Joiner thread for " << threadName
                  << " started";
        t.join();
        {
          std::lock_guard<std::mutex> lock(m);
          joined = true;
        }
        cv.notify_one();
        LOG_INFO << "DiskPerformanceTracker: " << threadName
                  << " joined successfully";
      });

      {
        std::unique_lock<std::mutex> lock(m);
        if (!cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                         [&] { return joined; })) {
          LOG_WARN << "DiskPerformanceTracker: " << threadName
                    << " join timed out after " << timeoutMs << "ms";
          joiner.detach();
          return false;
        }
      }

      if (joiner.joinable()) {
        joiner.join();
      }
      return true;
    };

    // Give more time for threads to exit when more processors are available
    int numProcessors = std::thread::hardware_concurrency();
    int baseTimeout = 1000;  // Base timeout in milliseconds
    int timeoutMultiplier =
      (numProcessors > 8) ? 3 : 1;  // Increase timeout for systems with SMT
    int timeout = baseTimeout * timeoutMultiplier;

    LOG_INFO << "DiskPerformanceTracker: Using " << timeout
              << "ms timeout for thread joining (CPUs: " << numProcessors << ")";

    // Join threads in reverse order of creation
    bool allThreadsJoined = true;

    if (m_statsThread.joinable()) {
      allThreadsJoined &=
        joinThreadWithTimeout(m_statsThread, "statistics thread", timeout);
    }

    if (m_eventStatsThread.joinable()) {
      allThreadsJoined &= joinThreadWithTimeout(
        m_eventStatsThread, "event statistics thread", timeout);
    }

    // Main tracing thread gets a longer timeout
    if (m_tracingThread.joinable()) {
      allThreadsJoined &=
        joinThreadWithTimeout(m_tracingThread, "tracing thread", timeout * 2);
    }

    if (!allThreadsJoined) {
      LOG_ERROR << "DiskPerformanceTracker: One or more threads failed to join within timeout";

      // More aggressive cleanup approach - try to force close any ETW session
      // again
      LOG_WARN << "DiskPerformanceTracker: Attempting additional ETW cleanup...";
      try {
        // Try multiple named patterns for the ETW session
        const std::wstring sessionNames[] = {L"DiskPerformanceTracker",
                                             L"PresentMon_*",
                                             L"Microsoft-Windows-DiskIO*"};

        for (const auto& name : sessionNames) {
          EVENT_TRACE_PROPERTIES props = {};
          props.Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES);
          ControlTraceW(0, name.c_str(), &props, EVENT_TRACE_CONTROL_STOP);
        }
      } catch (...) {
        LOG_ERROR << "DiskPerformanceTracker: Error during additional ETW cleanup";
      }
    }

    // Run the cleanup regardless of thread join status
    cleanup();
  } catch (const std::exception& e) {
    LOG_ERROR << "DiskPerformanceTracker: Exception during stopTracking: " << e.what();
    // Ensure cleanup happens even after an exception
    cleanup();
  } catch (...) {
    LOG_ERROR << "DiskPerformanceTracker: Unknown exception during stopTracking";
    // Ensure cleanup happens even after an exception
    cleanup();
  }
}

void DiskPerformanceTracker::updateBenchmarkData(BenchmarkDataPoint& dataPoint) {
  std::lock_guard<std::mutex> lock(m_metricsMutex);

  dataPoint.diskReadLatencyMs = m_currentMetrics.readLatencyMs;
  dataPoint.diskWriteLatencyMs = m_currentMetrics.writeLatencyMs;
  dataPoint.diskQueueLength = m_currentMetrics.queueLength;
  dataPoint.avgDiskQueueLength = m_currentMetrics.avgQueueLength;
  dataPoint.maxDiskQueueLength = m_currentMetrics.maxQueueLength;
  dataPoint.ioReadMB = m_currentMetrics.readMB;
  dataPoint.ioWriteMB = m_currentMetrics.writeMB;

  dataPoint.minDiskReadLatencyMs = m_currentMetrics.minReadLatencyMs;
  dataPoint.maxDiskReadLatencyMs = m_currentMetrics.maxReadLatencyMs;
  dataPoint.minDiskWriteLatencyMs = m_currentMetrics.minWriteLatencyMs;
  dataPoint.maxDiskWriteLatencyMs = m_currentMetrics.maxWriteLatencyMs;
}

void DiskPerformanceTracker::tracingThreadProc() {
  try {
    krabs::user_trace session(L"DiskPerformanceTracker");

    krabs::provider<> diskio_provider(
      krabs::guid(L"{945186BF-3DD6-4F3F-9C8E-9EDD3FC9D558}"));
    krabs::provider<> diskio_completion_provider(
      krabs::guid(L"{CF13BBC7-A730-484A-83B0-34DA8729F1DC}"));
    krabs::provider<> kernel_provider(
      krabs::guid(L"{9E814AAD-3204-11D2-9A82-006008A86939}"));
    krabs::provider<> file_provider(
      krabs::guid(L"{EDD08927-9CC4-4E65-B970-C2560FB5C289}"));

    diskio_provider.any(0xFFFFFFFF);
    diskio_completion_provider.any(0xFFFFFFFF);
    kernel_provider.any(0xFFFFFFFF);
    file_provider.any(0xFFFFFFFF);

    std::map<std::wstring, std::map<BYTE, size_t>> providerOpcodeCounts;
    std::map<ULONGLONG, size_t> eventIdCounts;
    std::set<ULONGLONG> loggedEventIds;

    const std::map<ULONGLONG, std::wstring> fileEventNames = {
      {10, L"NameCreate"},
      {11, L"NameDelete"},
      {12, L"Create"},
      {13, L"Cleanup"},
      {14, L"Close"},
      {15, L"Read"},
      {16, L"Write"},
      {17, L"SetInformation"},
      {18, L"SetDelete"},
      {19, L"Rename"},
      {20, L"DirEnum"},
      {21, L"Flush"},
      {22, L"QueryInformation"},
      {23, L"FSCTL"},
      {24, L"OperationEnd"},
      {25, L"DirNotify"},
      {26, L"DeletePath"},
      {27, L"RenamePath"},
      {28, L"SetLinkPath"},
      {29, L"Rename29"},
      {30, L"CreateNewFile"},
      {31, L"SetSecurity"},
      {32, L"QuerySecurity"},
      {33, L"SetEA"},
      {34, L"QueryEA"}};

    auto setupEventCallback =
      [this, &providerOpcodeCounts, &eventIdCounts, &loggedEventIds,
       &fileEventNames](krabs::provider<>& provider, const std::wstring& name) {
        provider.add_on_event_callback(
          [this, name, &providerOpcodeCounts, &eventIdCounts, &loggedEventIds,
           &fileEventNames](const EVENT_RECORD& record,
                            const krabs::trace_context& trace_context) {
            try {
              m_totalEventsReceived++;
              providerOpcodeCounts[name]
                                  [record.EventHeader.EventDescriptor.Opcode]++;

              try {
                if (name == L"Kernel-File" &&
                    record.EventHeader.EventDescriptor.Opcode ==
                      FILE_OPERATION_OPCODE) {
                  krabs::schema schema(record, trace_context.schema_locator);
                  krabs::parser parser(schema);

                  ULONGLONG eventId = schema.event_id();
                  eventIdCounts[eventId]++;

                  if (eventId == FILE_READ_EVENT_ID ||
                      eventId == FILE_WRITE_EVENT_ID) {
                    bool isRead = (eventId == FILE_READ_EVENT_ID);
                    ULONG ioSize = 0;
                    bool extracted = false;

                    try {
                      ioSize = parser.parse<ULONG>(L"IOSize");
                      extracted = true;
                    } catch (...) {
                      try {
                        ioSize = parser.parse<ULONG>(L"Length");
                        extracted = true;
                      } catch (...) {
                        try {
                          ioSize = parser.parse<ULONG>(L"Size");
                          extracted = true;
                        } catch (...) {
                        }
                      }
                    }

                    if (extracted) {
                      ULONGLONG timestamp = static_cast<ULONGLONG>(
                        record.EventHeader.TimeStamp.QuadPart);

                      ULONGLONG irpPtr = 0;
                      try {
                        irpPtr = parser.parse<ULONGLONG>(L"Irp");
                      } catch (...) {
                      }

                      int newQueueSize = ++m_currentQueueSize;
                      {
                        std::lock_guard<std::mutex> lock(m_queueMutex);
                        if (newQueueSize > m_maxQueueSize) {
                          m_maxQueueSize = newQueueSize;
                        }
                        m_queueSizeSamples.push_back(newQueueSize);
                      }

                      if (irpPtr != 0) {
                        std::lock_guard<std::mutex> lock(m_ioMutex);
                        m_pendingIoOperations[irpPtr] = {timestamp, 0, ioSize,
                                                         isRead, L""};
                      } else {
                        DiskIoOperation operation = {timestamp, 0, ioSize,
                                                     isRead, L""};

                        processCompletedIo(operation);
                      }

                      m_eventsProcessed++;
                    }
                  } else if (eventId == FILE_OPERATION_END_EVENT_ID) {
                    ULONGLONG irpPtr = 0;
                    try {
                      irpPtr = parser.parse<ULONGLONG>(L"Irp");
                      if (irpPtr != 0) {
                        ULONGLONG endTimestamp = static_cast<ULONGLONG>(
                          record.EventHeader.TimeStamp.QuadPart);

                        std::lock_guard<std::mutex> lock(m_ioMutex);
                        auto it = m_pendingIoOperations.find(irpPtr);
                        if (it != m_pendingIoOperations.end()) {
                          DiskIoOperation operation = it->second;
                          operation.requestDuration =
                            endTimestamp - operation.timestamp;

                          processCompletedIo(operation);

                          m_pendingIoOperations.erase(it);
                          --m_currentQueueSize;
                          m_eventsProcessed++;
                        }
                      }
                    } catch (...) {
                    }
                  }
                } else if (name == L"Kernel-Trace" &&
                           (record.EventHeader.EventDescriptor.Opcode ==
                              DISK_IO_READ_OPCODE ||
                            record.EventHeader.EventDescriptor.Opcode ==
                              DISK_IO_WRITE_OPCODE ||
                            record.EventHeader.EventDescriptor.Opcode ==
                              DISK_IO_COMPLETION_OPCODE)) {
                  try {
                    krabs::schema schema(record, trace_context.schema_locator);
                    krabs::parser parser(schema);

                    try {
                      ULONGLONG irpPtr = 0;
                      ULONG transferSize = 0;
                      bool extracted = false;

                      try {
                        irpPtr = parser.parse<ULONGLONG>(L"Irp");
                        transferSize = parser.parse<ULONG>(L"TransferSize");
                        extracted = true;
                      } catch (...) {
                        try {
                          irpPtr = parser.parse<ULONGLONG>(L"IrpPtr");
                          transferSize = parser.parse<ULONG>(L"Size");
                          extracted = true;
                        } catch (...) {
                        }
                      }

                      if (extracted) {
                        bool isRead =
                          (record.EventHeader.EventDescriptor.Opcode ==
                           DISK_IO_READ_OPCODE);

                        int newQueueSize = ++m_currentQueueSize;
                        {
                          std::lock_guard<std::mutex> lock(m_queueMutex);
                          if (newQueueSize > m_maxQueueSize) {
                            m_maxQueueSize = newQueueSize;
                          }
                          m_queueSizeSamples.push_back(newQueueSize);
                        }

                        {
                          std::lock_guard<std::mutex> lock(m_ioMutex);
                          m_pendingIoOperations[irpPtr] = {
                            static_cast<ULONGLONG>(
                              record.EventHeader.TimeStamp.QuadPart),
                            0, transferSize, isRead, L""};
                        }

                        m_eventsProcessed++;
                      }
                    } catch (...) {
                    }
                  } catch (const krabs::could_not_find_schema&) {
                    // Silently ignore schema not found errors
                  } catch (...) {
                  }
                } else if (name == L"IoCompletionCallback" ||
                           (name == L"Kernel-Trace" &&
                            record.EventHeader.EventDescriptor.Opcode ==
                              DISK_IO_COMPLETION_OPCODE)) {
                  try {
                    krabs::schema schema(record, trace_context.schema_locator);
                    krabs::parser parser(schema);

                    try {
                      ULONGLONG irpPtr = 0;
                      bool extracted = false;

                      try {
                        irpPtr = parser.parse<ULONGLONG>(L"Irp");
                        extracted = true;
                      } catch (...) {
                        try {
                          irpPtr = parser.parse<ULONGLONG>(L"IrpPtr");
                          extracted = true;
                        } catch (...) {
                        }
                      }

                      if (extracted) {
                        std::lock_guard<std::mutex> lock(m_ioMutex);
                        auto it = m_pendingIoOperations.find(irpPtr);
                        if (it != m_pendingIoOperations.end()) {
                          DiskIoOperation operation = it->second;
                          operation.requestDuration =
                            record.EventHeader.TimeStamp.QuadPart -
                            operation.timestamp;

                          processCompletedIo(operation);

                          m_pendingIoOperations.erase(it);
                          --m_currentQueueSize;
                          m_eventsProcessed++;
                        }
                      }
                    } catch (...) {
                    }
                  } catch (const krabs::could_not_find_schema&) {
                    // Silently ignore schema not found errors
                  } catch (...) {
                  }
                } else {
                  if (name == L"Kernel-Trace" || name == L"Kernel-Disk" ||
                      name == L"IoCompletionCallback") {
                    m_eventsFiltered++;
                  }
                }
              } catch (const krabs::could_not_find_schema&) {
                // Specifically catch and handle schema not found errors
              } catch (...) {
              }
            } catch (...) {
            }
          });
      };

    setupEventCallback(diskio_provider, L"Kernel-Disk");
    setupEventCallback(diskio_completion_provider, L"IoCompletionCallback");
    setupEventCallback(kernel_provider, L"Kernel-Trace");
    setupEventCallback(file_provider, L"Kernel-File");

    m_eventStatsThread = std::thread([this]() {
      while (m_running) {
        std::this_thread::sleep_for(
          std::chrono::seconds(METRICS_UPDATE_INTERVAL_SECONDS));

        if (!m_running) break;

        {
          std::lock_guard<std::mutex> lock(m_metricsMutex);
          auto now = std::chrono::steady_clock::now();
          auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           now - m_currentMetrics.lastUpdate)
                           .count();
          if (elapsed > 0) {
            uint64_t totalReadBytes = m_currentMetrics.totalReadBytes.load();
            uint64_t totalWriteBytes = m_currentMetrics.totalWriteBytes.load();

            double readMB = totalReadBytes / (1024.0 * 1024.0);
            double writeMB = totalWriteBytes / (1024.0 * 1024.0);

            uint64_t readOps = m_currentMetrics.readOperations.load();
            uint64_t writeOps = m_currentMetrics.writeOperations.load();

            uint64_t readLatencyOps = 0;
            double readLatencyMs = 0;
            if (m_currentMetrics.totalReadLatencyMs.load() > 0) {
              readLatencyOps = readOps;
              if (readLatencyOps > 0) {
                readLatencyMs = static_cast<double>(
                                  m_currentMetrics.totalReadLatencyMs.load()) /
                                readLatencyOps;
              }
            }

            uint64_t writeLatencyOps = 0;
            double writeLatencyMs = 0;
            if (m_currentMetrics.totalWriteLatencyMs.load() > 0) {
              writeLatencyOps = writeOps;
              if (writeLatencyOps > 0) {
                writeLatencyMs =
                  static_cast<double>(
                    m_currentMetrics.totalWriteLatencyMs.load()) /
                  writeLatencyOps;
              }
            }

            m_currentMetrics.readLatencyMs = readLatencyMs;
            m_currentMetrics.writeLatencyMs = writeLatencyMs;
            m_currentMetrics.readMB = readMB;
            m_currentMetrics.writeMB = writeMB;
          }

          // Periodically reset min/max values to ensure per-second metrics
          auto resetElapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                now - m_lastMetricsReset)
                                .count();
          if (resetElapsed >= METRICS_RESET_INTERVAL_SECONDS) {
            resetPeriodicMetrics();
            m_lastMetricsReset = now;
          }
        }
      }
    });

    m_statsThread = std::thread([this]() {
      while (m_running) {
        updateQueueStatistics();

        for (int i = 0; i < 5 && m_running; i++) {
          std::this_thread::sleep_for(
            std::chrono::milliseconds(STATISTICS_UPDATE_INTERVAL_MS / 5));
          if (!m_running) break;
        }
      }
    });

    session.enable(diskio_provider);
    session.enable(diskio_completion_provider);
    session.enable(kernel_provider);
    session.enable(file_provider);

    {
      std::lock_guard<std::mutex> lock(m_sessionMutex);
      m_activeSession = &session;
    }

    try {
      std::atomic<bool> stopRequested{false};

      std::thread stopMonitorThread([this, &session, &stopRequested]() {
        while (m_running && !stopRequested) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!stopRequested) {
          stopRequested = true;
          try {
            session.stop();
          } catch (const std::exception& e) {
            LOG_ERROR << "Error stopping ETW session from monitor: " << e.what();
          }
        }
      });

      session.start();

      stopRequested = true;
      if (stopMonitorThread.joinable()) {
        stopMonitorThread.join();
      }
    } catch (const std::exception& e) {
      LOG_ERROR << "Failed to start ETW trace session: " << e.what();
    }

    {
      std::lock_guard<std::mutex> lock(m_sessionMutex);
      m_activeSession = nullptr;
    }

    while (m_running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "Error in DiskPerformanceTracker: " << e.what();
  }
}

void DiskPerformanceTracker::processCompletedIo(
  const DiskIoOperation& operation) {
  double latencyMs = operation.requestDuration / 10000.0;

  std::lock_guard<std::mutex> lock(m_metricsMutex);

  if (operation.isRead) {
    m_currentMetrics.totalReadBytes += operation.size;
    m_currentMetrics.readOperations++;

    if (latencyMs > MIN_VALID_LATENCY_MS) {
      m_currentMetrics.totalReadLatencyMs += latencyMs;

      if (m_currentMetrics.minReadLatencyMs < 0 ||
          latencyMs < m_currentMetrics.minReadLatencyMs) {
        m_currentMetrics.minReadLatencyMs = latencyMs;
      }
      if (latencyMs > m_currentMetrics.maxReadLatencyMs) {
        m_currentMetrics.maxReadLatencyMs = latencyMs;
      }
    }
  } else {
    m_currentMetrics.totalWriteBytes += operation.size;
    m_currentMetrics.writeOperations++;

    if (latencyMs > MIN_VALID_LATENCY_MS) {
      m_currentMetrics.totalWriteLatencyMs += latencyMs;

      if (m_currentMetrics.minWriteLatencyMs < 0 ||
          latencyMs < m_currentMetrics.minWriteLatencyMs) {
        m_currentMetrics.minWriteLatencyMs = latencyMs;
      }
      if (latencyMs > m_currentMetrics.maxWriteLatencyMs) {
        m_currentMetrics.maxWriteLatencyMs = latencyMs;
      }
    }
  }
}

void DiskPerformanceTracker::updateQueueStatistics() {
  std::lock_guard<std::mutex> lock(m_queueMutex);

  if (m_queueSizeSamples.empty()) {
    return;
  }

  double avgQueueLength =
    std::accumulate(m_queueSizeSamples.begin(), m_queueSizeSamples.end(), 0.0) /
    m_queueSizeSamples.size();

  {
    std::lock_guard<std::mutex> metricsLock(m_metricsMutex);
    m_currentMetrics.queueLength = m_currentQueueSize;
    m_currentMetrics.avgQueueLength = avgQueueLength;
    m_currentMetrics.maxQueueLength = m_maxQueueSize;
  }

  m_maxQueueSize = m_currentQueueSize;
  m_queueSizeSamples.clear();
}

void DiskPerformanceTracker::resetPeriodicMetrics() {
  uint64_t currentReadBytes = m_currentMetrics.totalReadBytes.load();
  uint64_t currentWriteBytes = m_currentMetrics.totalWriteBytes.load();

  static uint64_t previousReadBytes = 0;
  static uint64_t previousWriteBytes = 0;

  uint64_t readBytesDelta = currentReadBytes - previousReadBytes;
  uint64_t writeBytesDelta = currentWriteBytes - previousWriteBytes;

  previousReadBytes = currentReadBytes;
  previousWriteBytes = currentWriteBytes;

  double readMB = readBytesDelta / (1024.0 * 1024.0);
  double writeMB = writeBytesDelta / (1024.0 * 1024.0);

  uint64_t readOps = m_currentMetrics.readOperations.load();
  uint64_t writeOps = m_currentMetrics.writeOperations.load();

  double readLatencyMs = 0.0;
  double writeLatencyMs = 0.0;

  uint64_t totalReadLatencyMs = m_currentMetrics.totalReadLatencyMs.load();
  uint64_t totalWriteLatencyMs = m_currentMetrics.totalWriteLatencyMs.load();

  if (totalReadLatencyMs > 0 && readOps > 0) {
    readLatencyMs = static_cast<double>(totalReadLatencyMs) / readOps;
  }

  if (totalWriteLatencyMs > 0 && writeOps > 0) {
    writeLatencyMs = static_cast<double>(totalWriteLatencyMs) / writeOps;
  }

  double queueLength = m_currentMetrics.queueLength;
  double avgQueueLength = m_currentMetrics.avgQueueLength;
  double maxQueueLength = m_currentMetrics.maxQueueLength;

  // Save the current values before resetting
  m_currentMetrics.readLatencyMs = readLatencyMs;
  m_currentMetrics.writeLatencyMs = writeLatencyMs;
  m_currentMetrics.queueLength = queueLength;
  m_currentMetrics.avgQueueLength = avgQueueLength;
  m_currentMetrics.maxQueueLength = maxQueueLength;
  m_currentMetrics.readMB = readMB;
  m_currentMetrics.writeMB = writeMB;

  // Properly reset min/max values for the next time period
  m_currentMetrics.minReadLatencyMs = -1;
  m_currentMetrics.maxReadLatencyMs = -1;
  m_currentMetrics.minWriteLatencyMs = -1;
  m_currentMetrics.maxWriteLatencyMs = -1;

  m_currentMetrics.totalReadLatencyMs = 0;
  m_currentMetrics.totalWriteLatencyMs = 0;
  m_currentMetrics.readOperations = 0;
  m_currentMetrics.writeOperations = 0;

  m_currentMetrics.lastUpdate = std::chrono::steady_clock::now();
}

std::string DiskPerformanceTracker::logRawData() {
  std::stringstream ss;
  ss << "=== Disk Performance Tracker Raw Data Collection ===\n";

  // Performance metrics snapshot
  {
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    auto now = std::chrono::steady_clock::now();
    auto runningTime = std::chrono::duration_cast<std::chrono::seconds>(
                         now - m_trackingStartTime)
                         .count();

    ss << "\nETW Session Information:\n";
    ss << "  Running: " << (m_running ? "Yes" : "No") << "\n";
    ss << "  Running time: " << runningTime << " seconds\n";
    ss << "  Session active: " << (m_activeSession != nullptr ? "Yes" : "No")
       << "\n";
    ss << "  Total events received: " << m_totalEventsReceived.load() << "\n";
    ss << "  Events processed: " << m_eventsProcessed.load() << "\n";
    ss << "  Events filtered: " << m_eventsFiltered.load() << "\n";
    ss << "  Processing ratio: "
       << (m_totalEventsReceived.load() > 0
             ? (double)m_eventsProcessed.load() / m_totalEventsReceived.load() *
                 100.0
             : 0.0)
       << "%\n";

    ss << "\nRaw Metrics Values:\n";
    ss << "  Total read bytes: " << m_currentMetrics.totalReadBytes.load()
       << " bytes ("
       << (m_currentMetrics.totalReadBytes.load() / (1024.0 * 1024.0))
       << " MB)\n";
    ss << "  Total write bytes: " << m_currentMetrics.totalWriteBytes.load()
       << " bytes ("
       << (m_currentMetrics.totalWriteBytes.load() / (1024.0 * 1024.0))
       << " MB)\n";
    ss << "  Read operations: " << m_currentMetrics.readOperations.load()
       << "\n";
    ss << "  Write operations: " << m_currentMetrics.writeOperations.load()
       << "\n";
    ss << "  Total read latency: " << m_currentMetrics.totalReadLatencyMs.load()
       << " ms\n";
    ss << "  Total write latency: "
       << m_currentMetrics.totalWriteLatencyMs.load() << " ms\n";

    ss << "\nCalculated Metrics:\n";
    ss << "  Avg read latency: " << m_currentMetrics.readLatencyMs << " ms\n";
    ss << "  Avg write latency: " << m_currentMetrics.writeLatencyMs << " ms\n";
    ss << "  Min read latency: " << m_currentMetrics.minReadLatencyMs
       << " ms\n";
    ss << "  Max read latency: " << m_currentMetrics.maxReadLatencyMs
       << " ms\n";
    ss << "  Min write latency: " << m_currentMetrics.minWriteLatencyMs
       << " ms\n";
    ss << "  Max write latency: " << m_currentMetrics.maxWriteLatencyMs
       << " ms\n";
    ss << "  Current read rate: " << m_currentMetrics.readMB << " MB/s\n";
    ss << "  Current write rate: " << m_currentMetrics.writeMB << " MB/s\n";

    auto lastUpdateElapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               now - m_currentMetrics.lastUpdate)
                               .count();
    ss << "  Time since last metrics update: " << lastUpdateElapsed
       << " seconds\n";
  }

  // Queue information
  {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    ss << "\nI/O Queue Information:\n";
    ss << "  Current queue size: " << m_currentQueueSize.load() << "\n";
    ss << "  Maximum queue size: " << m_maxQueueSize << "\n";
    ss << "  Queue sample count: " << m_queueSizeSamples.size() << "\n";

    if (!m_queueSizeSamples.empty()) {
      double avg = std::accumulate(m_queueSizeSamples.begin(),
                                   m_queueSizeSamples.end(), 0.0) /
                   m_queueSizeSamples.size();
      ss << "  Average queue size (current samples): " << avg << "\n";

      // Log a few sample values if available
      ss << "  Sample queue sizes: ";
      size_t sampleCount = std::min<size_t>(10, m_queueSizeSamples.size());
      for (size_t i = 0; i < sampleCount; i++) {
        ss << m_queueSizeSamples[i];
        if (i < sampleCount - 1) ss << ", ";
      }
      ss << "\n";
    }
  }

  // Pending I/O operations
  {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    ss << "\nPending I/O Operations: " << m_pendingIoOperations.size() << "\n";

    // Log a few sample pending operations if available
    int sampleCount = 0;
    for (const auto& [irpPtr, operation] : m_pendingIoOperations) {
      if (sampleCount++ >= 10) break;  // Limit to 10 samples

      ss << "  IRP: 0x" << std::hex << irpPtr << std::dec << ", ";
      ss << "Type: " << (operation.isRead ? "Read" : "Write") << ", ";
      ss << "Size: " << operation.size << " bytes, ";
      ss << "Started: " << operation.timestamp << "\n";
    }
  }

  // ETW Provider information
  ss << "\nETW Provider Information:\n";
  ss << "  Disk I/O Provider GUID: {945186BF-3DD6-4F3F-9C8E-9EDD3FC9D558}\n";
  ss << "  I/O Completion Provider GUID: "
        "{CF13BBC7-A730-484A-83B0-34DA8729F1DC}\n";
  ss << "  Kernel Provider GUID: {9E814AAD-3204-11D2-9A82-006008A86939}\n";
  ss << "  File Provider GUID: {EDD08927-9CC4-4E65-B970-C2560FB5C289}\n";

  // ETW Event opcode info
  ss << "\nETW Event Opcodes Used:\n";
  ss << "  Disk I/O Read: 32\n";
  ss << "  Disk I/O Write: 33\n";
  ss << "  Disk I/O Completion: 36\n";
  ss << "  File Operation: 0\n";

  // ETW Event ID info
  ss << "\nETW File Operation Event IDs:\n";
  ss << "  File Read: 15\n";
  ss << "  File Write: 16\n";
  ss << "  File Operation End: 24\n";

  return ss.str();
}
