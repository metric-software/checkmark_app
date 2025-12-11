#include "CPUKernelMetricsTracker.h"

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_set>

#include "../logging/Logger.h"

#include <krabs.hpp>
#include <krabs/kernel_providers.hpp>

using namespace krabs;

// Constants
static constexpr int METRICS_UPDATE_INTERVAL_MS = 1000;
static constexpr int LOG_INTERVAL_MS = 10000;
static constexpr int EVENT_STATS_LOG_INTERVAL_MS = 10000;
static constexpr uint32_t MAX_REASONABLE_WAIT_TICKS = 1000000;
static constexpr double DEFAULT_QPC_FREQUENCY = 10000000.0;

// Provider GUIDs
const GUID THREAD_PROVIDER_GUID = {0x3d6fa8d1, 0xfe05, 0x11d0, 0x9d, 0xda, 0x00,
                                   0xc0,       0x4f,   0xd7,   0xba, 0x7c};
const GUID PROCESS_PROVIDER_GUID = {
  0x3d6fa8d0, 0xfe05, 0x11d0, 0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c};
const GUID PERFINFO_PROVIDER_GUID = {
  0xce1dbfb4, 0x137e, 0x4da6, 0x87, 0xb0, 0x3f, 0x59, 0xaa, 0x10, 0x2c, 0xbc};

// Enums
enum class ThreadEventId : uint16_t {
  ThreadStart = 1,
  ThreadEnd = 2,
  DCStart = 3,
  DCEnd = 4,
  ContextSwitch = 36,
  ReadyThread = 50
};

enum class PerfInfoEventId : uint16_t {
  SampledProfile = 46,
  SysCallEnter = 51,
  SysCallExit = 52,
  ThreadedDPC = 66,
  Interrupt = 67,
  DPC = 68,
  TimerDPC = 69
};

enum class ProcessEventId : uint16_t {
  ProcessStart = 1,
  ProcessEnd = 2,
  DefunctProcess = 39,
  ProcessPerfCounters = 32,
  ProcessCounterRundown = 33
};

enum class DpcTimingMethod {
  NONE,
  EXTENDED_OFFSET,
  PRIMARY_FIELD,
  ROUTINE_DELTA
};

// Data structures
struct EventIdentifier {
  GUID providerId;
  int eventId;
  int opcode;

  bool operator<(const EventIdentifier& other) const {
    if (memcmp(&providerId, &other.providerId, sizeof(GUID)) != 0) {
      return memcmp(&providerId, &other.providerId, sizeof(GUID)) < 0;
    }
    if (eventId != other.eventId) {
      return eventId < other.eventId;
    }
    return opcode < other.opcode;
  }
};

struct EventTypeIdentifier {
  GUID providerId;
  BYTE opcode;

  bool operator<(const EventTypeIdentifier& other) const {
    if (memcmp(&providerId, &other.providerId, sizeof(GUID)) != 0) {
      return memcmp(&providerId, &other.providerId, sizeof(GUID)) < 0;
    }
    return opcode < other.opcode;
  }
};

struct DetailedEventIdentifier {
  GUID providerId;
  int eventId;
  int opcode;

  bool operator<(const DetailedEventIdentifier& other) const {
    if (memcmp(&providerId, &other.providerId, sizeof(GUID)) != 0) {
      return memcmp(&providerId, &other.providerId, sizeof(GUID)) < 0;
    }
    if (eventId != other.eventId) {
      return eventId < other.eventId;
    }
    return opcode < other.opcode;
  }
};

// Global variables
CPUKernelMetricsTracker* g_cpuKernelTracker = nullptr;
std::mutex g_dpcStatsMutex;
double g_dpcLatenciesAbove50us = 0.0;
double g_dpcLatenciesAbove100us = 0.0;
size_t g_lastValidDurations = 0;
std::map<EventTypeIdentifier, uint64_t> g_eventTypeCounts;
std::set<EventTypeIdentifier> g_loggedEventTypes;
std::chrono::steady_clock::time_point g_lastEventStatsLogTime =
  std::chrono::steady_clock::now();
std::map<EventIdentifier, uint64_t> g_eventCounts;
std::set<EventIdentifier> g_loggedEvents;
std::mutex g_eventMutex;
std::chrono::steady_clock::time_point g_lastLogTime =
  std::chrono::steady_clock::now();
std::set<DetailedEventIdentifier> g_loggedDetailedEvents;
std::mutex g_detailedEventMutex;

// Forward declarations
void calculateDpcLatencyPercentages(
  const std::map<std::string, uint64_t>& timingBins, size_t validDurations,
  double& above50us, double& above100us);

std::string guidToString(const GUID& guid) {
  char guidStr[64] = {0};
  sprintf_s(
    guidStr,
    "{%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX}",
    guid.Data1, guid.Data2, guid.Data3, guid.Data4[0], guid.Data4[1],
    guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5], guid.Data4[6],
    guid.Data4[7]);
  return guidStr;
}

class LoggingManager {
 public:
  static LoggingManager& getInstance() {
    static LoggingManager instance;
    return instance;
  }

  enum LogType {
    INITIALIZATION,
    DPC_TIMING_ANALYSIS,
    EVENT_STATISTICS,
    DPC_DISTRIBUTION,
    ERROR_ONLY
  };

  bool shouldLog(LogType type) const {
    return enabledLogTypes.find(type) != enabledLogTypes.end();
  }

  void enableLog(LogType type) { enabledLogTypes.insert(type); }

  void disableLog(LogType type) { enabledLogTypes.erase(type); }

  void enableInitializationLogs() { enableLog(INITIALIZATION); }

  bool isInitializationLogEnabled() const { return shouldLog(INITIALIZATION); }

 private:
  LoggingManager() {
    // Only enable error logging by default
    enabledLogTypes = {ERROR_ONLY};
  }

  std::unordered_set<LogType> enabledLogTypes;
};

void ConditionalLog(LoggingManager::LogType type, const std::string& message) {
  if (LoggingManager::getInstance().shouldLog(type)) {
    LOG_INFO << message;
  }
}

// For error messages - always log these
void ErrorLog(const std::string& message) { LOG_ERROR << message; }

// Modify parseDpcTimingFromBinary to guarantee dpcTime is initialized to 0
bool parseDpcTimingFromBinary(const EVENT_RECORD& record, uint64_t& dpcTime) {
  // Initialize dpcTime to invalid value
  dpcTime = 0;

  if (record.UserDataLength < 16) {
    return false;
  }

  const BYTE* userData = reinterpret_cast<const BYTE*>(record.UserData);

  static std::vector<std::array<uint32_t, 4>> recentEvents;
  static std::mutex eventsMutex;
  static size_t totalDpcEvents = 0;
  static size_t validDurations = 0;

  static std::map<DpcTimingMethod, uint64_t> methodCounts = {
    {DpcTimingMethod::EXTENDED_OFFSET, 0},
    {DpcTimingMethod::PRIMARY_FIELD, 0},
    {DpcTimingMethod::ROUTINE_DELTA, 0}};

  static std::map<std::string, uint64_t> timingBins = {
    {"0-5μs", 0},    {"5-10μs", 0},    {"10-25μs", 0},   {"25-50μs", 0},
    {"50-100μs", 0}, {"100-250μs", 0}, {"250-500μs", 0}, {"500-1000μs", 0},
    {"1-10ms", 0},   {"10-100ms", 0}};

  std::array<uint32_t, 4> fields;
  for (int i = 0; i < 4 && i * 4 < record.UserDataLength; i++) {
    memcpy(&fields[i], &userData[i * 4], sizeof(uint32_t));
  }

  DpcTimingMethod usedMethod = DpcTimingMethod::NONE;

  {
    std::lock_guard<std::mutex> lock(eventsMutex);
    totalDpcEvents++;

    if (recentEvents.size() >= 1000) {
      recentEvents.erase(recentEvents.begin());
    }
    recentEvents.push_back(fields);
  }

  double qpcFrequency = DEFAULT_QPC_FREQUENCY;
  if (g_cpuKernelTracker) {
    qpcFrequency =
      static_cast<double>(g_cpuKernelTracker->getPerfFreq().QuadPart);
  }

  auto updateTimingBin = [&](uint32_t value, DpcTimingMethod method) {
    double microseconds =
      (static_cast<double>(value) * 1000000.0) / qpcFrequency;

    std::lock_guard<std::mutex> lock(eventsMutex);
    validDurations++;
    methodCounts[method]++;

    if (microseconds < 5)
      timingBins["0-5μs"]++;
    else if (microseconds < 10)
      timingBins["5-10μs"]++;
    else if (microseconds < 25)
      timingBins["10-25μs"]++;
    else if (microseconds < 50)
      timingBins["25-50μs"]++;
    else if (microseconds < 100)
      timingBins["50-100μs"]++;
    else if (microseconds < 250)
      timingBins["100-250μs"]++;
    else if (microseconds < 500)
      timingBins["250-500μs"]++;
    else if (microseconds < 1000)
      timingBins["500-1000μs"]++;
    else if (microseconds < 10000)
      timingBins["1-10ms"]++;
    else
      timingBins["10-100ms"]++;
  };

  if (record.UserDataLength >= 24) {
    for (size_t offset = 16; offset <= record.UserDataLength - 4; offset += 4) {
      uint32_t value = 0;
      memcpy(&value, &userData[offset], sizeof(uint32_t));

      if (value >= 5 && value <= 500) {
        dpcTime = value;
        updateTimingBin(value, DpcTimingMethod::EXTENDED_OFFSET);
        return true;
      }
    }
  }

  for (int i = 0; i < 4; i++) {
    uint32_t value = fields[i];

    if (value >= 5 && value <= 500) {
      dpcTime = value;
      updateTimingBin(value, DpcTimingMethod::PRIMARY_FIELD);
      return true;
    }
  }

  static std::map<uint32_t, uint32_t> lastTimestampByRoutine;

  uint32_t timestamp = fields[0];
  uint32_t routineId = fields[2];

  if (lastTimestampByRoutine.find(routineId) != lastTimestampByRoutine.end()) {
    uint32_t lastTimestamp = lastTimestampByRoutine[routineId];
    uint32_t delta = timestamp - lastTimestamp;

    if (delta >= 5 && delta <= 500) {
      dpcTime = delta;
      updateTimingBin(delta, DpcTimingMethod::ROUTINE_DELTA);
      lastTimestampByRoutine[routineId] = timestamp;
      return true;
    }
  }

  lastTimestampByRoutine[routineId] = timestamp;

  {
    std::lock_guard<std::mutex> lock(g_dpcStatsMutex);
    calculateDpcLatencyPercentages(timingBins, validDurations,
                                   g_dpcLatenciesAbove50us,
                                   g_dpcLatenciesAbove100us);
    g_lastValidDurations = validDurations;
  }

  return false;
}

bool cleanupExistingSession(const std::wstring& sessionName) {
  EVENT_TRACE_PROPERTIES* props = nullptr;
  ULONG bufferSize = sizeof(EVENT_TRACE_PROPERTIES) +
                     (sessionName.length() + 1) * sizeof(wchar_t);

  try {
    props = reinterpret_cast<EVENT_TRACE_PROPERTIES*>(new char[bufferSize]);
    ZeroMemory(props, bufferSize);
    props->Wnode.BufferSize = bufferSize;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    ULONG status =
      ControlTraceW(0, sessionName.c_str(), props, EVENT_TRACE_CONTROL_STOP);

    bool success =
      (status == ERROR_SUCCESS || status == ERROR_WMI_INSTANCE_NOT_FOUND ||
       status == ERROR_FILE_NOT_FOUND);

    delete[] reinterpret_cast<char*>(props);
    return success;
  } catch (const std::exception&) {
    if (props) {
      delete[] reinterpret_cast<char*>(props);
    }
    return false;
  }
}

bool EnableEtwPrivileges() {
  HANDLE hToken;
  TOKEN_PRIVILEGES tp;
  LUID luid;

  if (!OpenProcessToken(GetCurrentProcess(),
                        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
    return false;
  }

  if (!LookupPrivilegeValue(nullptr, SE_SYSTEM_PROFILE_NAME, &luid)) {
    CloseHandle(hToken);
    return false;
  }

  tp.PrivilegeCount = 1;
  tp.Privileges[0].Luid = luid;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

  if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES),
                             nullptr, nullptr)) {
    CloseHandle(hToken);
    return false;
  }

  if (LookupPrivilegeValue(nullptr, SE_DEBUG_NAME, &luid)) {
    tp.Privileges[0].Luid = luid;
    AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr,
                          nullptr);
  }

  CloseHandle(hToken);
  return true;
}

CPUKernelMetricsTracker::CPUKernelMetricsTracker() {
  QueryPerformanceFrequency(&m_perfFreq);
  m_lastUpdateTime = std::chrono::steady_clock::now();

  m_threadMetrics.voluntaryContextSwitches.store(0);
  m_threadMetrics.involuntaryContextSwitches.store(0);
  m_threadMetrics.highPriorityInterruptions.store(0);
  m_threadMetrics.priorityInversions.store(0);
  m_threadMetrics.mutexWaits.store(0);
  m_threadMetrics.resourceWaits.store(0);
  m_threadMetrics.ioWaits.store(0);
  m_threadMetrics.totalThreadWaitTimeMs.store(0);
  m_threadMetrics.waitCount.store(0);

  g_cpuKernelTracker = this;
}

CPUKernelMetricsTracker::~CPUKernelMetricsTracker() {
  stopTracking();

  if (g_cpuKernelTracker == this) {
    g_cpuKernelTracker = nullptr;
  }
}

bool CPUKernelMetricsTracker::startTracking() {
  if (m_running) {
    return true;
  }

  m_running = true;
  m_traceStartedSuccessfully = false;

  m_contextSwitches = 0;
  m_interrupts = 0;
  m_dpcCount = 0;
  m_totalDpcLatencyTicks = 0;

  m_lastUpdateTime = std::chrono::steady_clock::now();

  // Start tracing thread
  m_traceThread =
    std::thread(&CPUKernelMetricsTracker::combinedThreadProc, this);

  return true;
}

// Helper function to safely join a thread with timeout
bool joinThreadWithTimeout(std::thread& thread, const char* threadName,
                           int timeoutMs) {
  if (!thread.joinable()) return true;

  std::mutex mtx;
  std::condition_variable cv;
  bool joined = false;

  std::thread joiner([&thread, &mtx, &cv, &joined]() {
    try {
      thread.join();
      {
        std::lock_guard<std::mutex> lock(mtx);
        joined = true;
      }
      cv.notify_one();
    } catch (const std::exception& ex) {
      LOG_ERROR << "Exception during thread join: " << ex.what();
    } catch (...) {
      LOG_ERROR << "Unknown exception during thread join";
    }
  });

  {
    std::unique_lock<std::mutex> lock(mtx);
    if (!cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                     [&joined] { return joined; })) {
      LOG_WARN << "Thread join timeout for " << threadName << " after "
                << timeoutMs << "ms";

      // We can't safely terminate the joiner thread, so we detach it
      // This is better than potentially leaking the main thread resources
      joiner.detach();
      return false;
    }
  }

  // If we got here, the thread was joined successfully - clean up the joiner
  // thread
  if (joiner.joinable()) {
    try {
      joiner.join();
    } catch (...) {
      // At this point the main thread is already joined, so ignore any issues
      // with the joiner
    }
  }

  return true;
}

void CPUKernelMetricsTracker::stopTracking() {
  if (!m_running) {
    return;
  }

  // Signal all threads to stop by setting running flag to false
  m_running = false;

  const std::wstring sessionName = L"CPUMetricsTraceSession";

  // First attempt to stop the ETW session properly - this should unblock the
  // start() call
  if (m_traceSession) {
    try {
      LOG_DEBUG << "Stopping ETW trace session...";
      m_traceSession->stop();
      LOG_DEBUG << "ETW trace session stopped successfully";
    } catch (const std::exception& ex) {
      LOG_ERROR << "Error stopping trace session: " << ex.what();
      // Continue with cleanup despite error
    }
    m_traceSession.reset();
  }

  // Secondary cleanup to ensure all ETW sessions are properly closed
  try {
    LOG_DEBUG << "Cleaning up existing ETW sessions...";
    cleanupExistingSession(sessionName);

    // Additional cleanup with direct API call to ensure no sessions remain
    for (int attempt = 0; attempt < 2; attempt++) {
      EVENT_TRACE_PROPERTIES props = {};
      props.Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES);
      props.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
      ULONG status1 = ControlTraceW(0, NULL, &props, EVENT_TRACE_CONTROL_STOP);
      ULONG status2 =
        ControlTraceW(0, sessionName.c_str(), &props, EVENT_TRACE_CONTROL_STOP);

      LOG_DEBUG << "ETW session cleanup attempt " << (attempt + 1)
                << " status: " << status1 << ", " << status2;

      // Give some time for OS to release resources
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } catch (const std::exception& ex) {
    LOG_ERROR << "Exception during session cleanup: " << ex.what();
  }

  // Join the tracing thread with a reasonable timeout
  if (m_traceThread.joinable()) {
    LOG_DEBUG << "Waiting for trace thread to finish...";

    // Using helper function to join with timeout
    if (!joinThreadWithTimeout(m_traceThread, "ETW trace thread", 3000)) {
      LOG_WARN
        << "WARNING: ETW trace thread did not exit within timeout period"
       ;
      LOG_WARN
        << "This may indicate that ETW resources won't be properly cleaned up"
       ;

      // Last resort ETW cleanup
      try {
        LOG_WARN << "Performing emergency ETW session cleanup...";
        EVENT_TRACE_PROPERTIES props = {};
        props.Wnode.BufferSize = sizeof(EVENT_TRACE_PROPERTIES);
        props.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
        ControlTraceW(0, NULL, &props, EVENT_TRACE_CONTROL_STOP);
        ControlTraceW(0, sessionName.c_str(), &props, EVENT_TRACE_CONTROL_STOP);
        // Force kill sessions with a unique name that might have been created
        ControlTraceW(0, L"CPUMetricsTraceSession*", &props,
                      EVENT_TRACE_CONTROL_STOP);
      } catch (...) {
        LOG_ERROR << "Error during emergency ETW cleanup";
      }

      // As a last resort, we still need to free the thread resources
      // This is not ideal, but better than a memory leak
      LOG_WARN << "WARNING: Detaching trace thread as last resort"
               ;
      m_traceThread.detach();
    } else {
      LOG_DEBUG << "Trace thread joined successfully";
    }
  }

  // Reset global tracker pointer if this instance is currently set
  if (g_cpuKernelTracker == this) {
    g_cpuKernelTracker = nullptr;
  }

  LOG_INFO << "CPU kernel metrics tracker stopped";
}

void CPUKernelMetricsTracker::combinedThreadProc() {
  try {
    EnableEtwPrivileges();

    const std::wstring sessionName = L"CPUMetricsTraceSession";
    cleanupExistingSession(sessionName);
    cleanupExistingSession(L"CPUMetricsTraceSession*");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::unique_ptr<krabs::kernel_trace> traceSession;

    try {
      traceSession = std::make_unique<krabs::kernel_trace>(sessionName);

      const ULONG CORE_FLAGS = EVENT_TRACE_FLAG_CSWITCH | EVENT_TRACE_FLAG_DPC |
                               EVENT_TRACE_FLAG_INTERRUPT |
                               EVENT_TRACE_FLAG_PROFILE;

      auto systemTraceGuid =
        krabs::guid(L"{9e814aad-3204-11d2-9a82-006008a86939}");
      krabs::kernel_provider systemProvider(CORE_FLAGS, systemTraceGuid);

      auto processGuid = krabs::guid(L"{3d6fa8d0-fe05-11d0-9dda-00c04fd7ba7c}");
      krabs::kernel_provider processProvider(EVENT_TRACE_FLAG_PROCESS,
                                             processGuid);

      auto threadGuid = krabs::guid(L"{3d6fa8d1-fe05-11d0-9dda-00c04fd7ba7c}");
      krabs::kernel_provider threadProvider(EVENT_TRACE_FLAG_THREAD,
                                            threadGuid);

      auto perfInfoGuid =
        krabs::guid(L"{ce1dbfb4-137e-4da6-87b0-3f59aa102cbc}");
      krabs::kernel_provider perfInfoProvider(0, perfInfoGuid);

      auto setupCallback = [this](auto& provider, const std::wstring&) {
        provider.add_on_event_callback(
          [this](const EVENT_RECORD& record,
                 const krabs::trace_context& trace_context) {
            processEvent(record, trace_context, m_contextSwitches, m_interrupts,
                         m_dpcCount, m_totalDpcLatencyTicks);
          });
      };

      setupCallback(systemProvider, L"system_provider");
      setupCallback(processProvider, L"process_provider");
      setupCallback(threadProvider, L"thread_provider");
      setupCallback(perfInfoProvider, L"perfinfo_provider");

      try {
        ULONG bufferSize = 256;
        ULONG minBuffers = 32;
        ULONG maxBuffers = 256;

        EVENT_TRACE_PROPERTIES* props =
          (EVENT_TRACE_PROPERTIES*)malloc(sizeof(EVENT_TRACE_PROPERTIES));
        if (props) {
          ZeroMemory(props, sizeof(EVENT_TRACE_PROPERTIES));
          props->BufferSize = bufferSize;
          props->MinimumBuffers = minBuffers;
          props->MaximumBuffers = maxBuffers;
          props->FlushTimer = 1;

          traceSession->set_trace_properties(props);
          free(props);
        }
      } catch (...) {
      }

      try {
        traceSession->enable(systemProvider);
      } catch (...) {
      }
      try {
        traceSession->enable(processProvider);
      } catch (...) {
      }
      try {
        traceSession->enable(threadProvider);
      } catch (...) {
      }
      try {
        traceSession->enable(perfInfoProvider);
      } catch (...) {
      }

      std::atomic<bool> sessionStartAttempted{false};
      std::promise<bool> sessionStartPromise;
      std::future<bool> sessionStartFuture = sessionStartPromise.get_future();

      std::thread sessionThread(
        [&traceSession, &sessionStartPromise, &sessionStartAttempted, this]() {
          try {
            sessionStartAttempted = true;
            m_traceStartedSuccessfully = true;

            try {
              sessionStartPromise.set_value(true);
            } catch (...) {
            }

            traceSession->start();
          } catch (...) {
            m_traceStartedSuccessfully = false;
            try {
              if (!sessionStartAttempted) {
                sessionStartPromise.set_value(false);
              }
            } catch (...) {
            }
          }
        });

      try {
        sessionStartFuture.wait_for(std::chrono::seconds(5));
      } catch (...) {
      }

      auto nextMetricsTime =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(METRICS_UPDATE_INTERVAL_MS);

      while (m_running) {
        auto now = std::chrono::steady_clock::now();

        if (now >= nextMetricsTime) {
          updateMetrics();
          nextMetricsTime =
            now + std::chrono::milliseconds(METRICS_UPDATE_INTERVAL_MS);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        if (!m_running) break;
      }

      if (m_traceStartedSuccessfully) {
        try {
          traceSession->stop();
        } catch (const std::exception& ex) {
          LOG_WARN << "Error stopping session: " << ex.what();
          try {
            cleanupExistingSession(sessionName);
          } catch (...) {
            LOG_WARN << "Cleanup failed";
          }
        }
      }

      if (sessionThread.joinable()) {
        try {
          std::mutex mtx;
          std::condition_variable cv;
          bool threadJoined = false;

          std::thread tmpJoinThread(
            [&sessionThread, &cv, &mtx, &threadJoined]() {
              sessionThread.join();
              std::lock_guard<std::mutex> lock(mtx);
              threadJoined = true;
              cv.notify_one();
            });

          {
            std::unique_lock<std::mutex> lock(mtx);
            if (!cv.wait_for(lock, std::chrono::seconds(2),
                             [&threadJoined] { return threadJoined; })) {
              tmpJoinThread.detach();
              sessionThread.detach();
            } else {
              tmpJoinThread.join();
            }
          }
        } catch (...) {
          sessionThread.detach();
        }
      }

      cleanupExistingSession(sessionName);
      traceSession.reset();
    } catch (...) {
    }

    traceSession.reset();
  } catch (...) {
  }
}

void CPUKernelMetricsTracker::updateMetrics() {
  auto now = std::chrono::steady_clock::now();
  auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - m_lastUpdateTime)
                     .count();

  if (elapsedMs <= 0) return;

  double intervalSeconds = elapsedMs / 1000.0;

  uint64_t cSwitches = m_contextSwitches.exchange(0);
  uint64_t ints = m_interrupts.exchange(0);
  uint64_t dpcs = m_dpcCount.exchange(0);
  uint64_t dpcLatencyTicks = m_totalDpcLatencyTicks.exchange(0);

  uint64_t volCS = m_threadMetrics.voluntaryContextSwitches.exchange(0);
  uint64_t involCS = m_threadMetrics.involuntaryContextSwitches.exchange(0);
  uint64_t highPriorityInts =
    m_threadMetrics.highPriorityInterruptions.exchange(0);
  uint64_t prioInversions = m_threadMetrics.priorityInversions.exchange(0);
  uint64_t mutexWaits = m_threadMetrics.mutexWaits.exchange(0);
  uint64_t resourceWaits = m_threadMetrics.resourceWaits.exchange(0);
  uint64_t ioWaits = m_threadMetrics.ioWaits.exchange(0);
  double totalWaitTimeMs = m_threadMetrics.totalThreadWaitTimeMs.exchange(0.0);
  uint64_t waitCount = m_threadMetrics.waitCount.exchange(0);

  uint64_t cSwitchesPerSec = static_cast<uint64_t>(cSwitches / intervalSeconds);
  uint64_t intsPerSec = static_cast<uint64_t>(ints / intervalSeconds);
  uint64_t dpcsPerSec = static_cast<uint64_t>(dpcs / intervalSeconds);

  uint64_t volCSPerSec = static_cast<uint64_t>(volCS / intervalSeconds);
  uint64_t involCSPerSec = static_cast<uint64_t>(involCS / intervalSeconds);
  uint64_t highPriorityIntsPerSec =
    static_cast<uint64_t>(highPriorityInts / intervalSeconds);
  uint64_t prioInversionsPerSec =
    static_cast<uint64_t>(prioInversions / intervalSeconds);

  double avgDpcLatencyUs = 0.0;
  if (dpcs > 0 && dpcLatencyTicks > 0) {
    double qpcFrequency = static_cast<double>(getPerfFreq().QuadPart);
    if (qpcFrequency > 0) {
      double totalDpcLatencyUs =
        (static_cast<double>(dpcLatencyTicks) * 1000000.0) / qpcFrequency;
      avgDpcLatencyUs = totalDpcLatencyUs / dpcs;
    }
  }

  double avgWaitTimeMs = 0.0;
  if (waitCount > 0 && totalWaitTimeMs > 0) {
    avgWaitTimeMs = totalWaitTimeMs / waitCount;
  }

  double dpcLatenciesAbove50us = 0.0;
  double dpcLatenciesAbove100us = 0.0;

  {
    std::lock_guard<std::mutex> lock(g_dpcStatsMutex);
    dpcLatenciesAbove50us = g_dpcLatenciesAbove50us;
    dpcLatenciesAbove100us = g_dpcLatenciesAbove100us;
  }

  {
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    m_latestMetrics.contextSwitchesPerSec = cSwitchesPerSec;
    m_latestMetrics.interruptsPerSec = intsPerSec;
    m_latestMetrics.dpcCountPerSec = dpcsPerSec;
    m_latestMetrics.avgDpcLatencyUs = avgDpcLatencyUs;
    m_latestMetrics.dpcLatenciesAbove50us = dpcLatenciesAbove50us;
    m_latestMetrics.dpcLatenciesAbove100us = dpcLatenciesAbove100us;

    m_latestMetrics.voluntaryContextSwitchesPerSec = volCSPerSec;
    m_latestMetrics.involuntaryContextSwitchesPerSec = involCSPerSec;
    m_latestMetrics.highPriorityInterruptionsPerSec = highPriorityIntsPerSec;
    m_latestMetrics.priorityInversionsPerSec = prioInversionsPerSec;
    m_latestMetrics.avgThreadWaitTimeMs = avgWaitTimeMs;
  }

  m_lastUpdateTime = now;
}

void processEvent(const EVENT_RECORD& record,
                  const krabs::trace_context& trace_context,
                  std::atomic<uint64_t>& contextSwitches,
                  std::atomic<uint64_t>& interrupts,
                  std::atomic<uint64_t>& dpcCount,
                  std::atomic<uint64_t>& totalDpcLatencyTicks) {
  if (!g_cpuKernelTracker) return;

  try {
    int eventId = record.EventHeader.EventDescriptor.Id;
    BYTE opcode = record.EventHeader.EventDescriptor.Opcode;

    if (opcode == static_cast<BYTE>(ThreadEventId::ContextSwitch)) {
      contextSwitches++;

      if (!g_cpuKernelTracker) return;

      try {
        krabs::schema schema(record, trace_context.schema_locator);
        krabs::parser parser(schema);

        uint8_t waitReason = parser.parse<uint8_t>(L"WaitReason");
        uint32_t waitTime = parser.parse<uint32_t>(L"WaitTime");
        bool isVoluntary = parser.parse<uint8_t>(L"IsVoluntary") != 0;
        uint8_t oldThreadPriority = parser.parse<uint8_t>(L"OldThreadPriority");
        uint8_t newThreadPriority = parser.parse<uint8_t>(L"NewThreadPriority");

        ThreadWaitReason waitReasonEnum =
          static_cast<ThreadWaitReason>(waitReason);

        if (!g_cpuKernelTracker) return;

        if (oldThreadPriority > newThreadPriority + 5) {
          g_cpuKernelTracker->getThreadMetrics().priorityInversions++;
        }

        if (!g_cpuKernelTracker) return;

        if (newThreadPriority > oldThreadPriority && !isVoluntary) {
          g_cpuKernelTracker->getThreadMetrics().highPriorityInterruptions++;
        }

        if (!g_cpuKernelTracker) return;

        if (isVoluntary) {
          g_cpuKernelTracker->getThreadMetrics().voluntaryContextSwitches++;
        } else {
          g_cpuKernelTracker->getThreadMetrics().involuntaryContextSwitches++;
        }

        if (!g_cpuKernelTracker) return;

        {
          if (!g_cpuKernelTracker) return;
          std::lock_guard<std::mutex> lock(
            g_cpuKernelTracker->getThreadMetrics().waitReasonMutex);
          g_cpuKernelTracker->getThreadMetrics()
            .waitReasonCounts[waitReasonEnum]++;
        }

        if (waitTime > 0 && g_cpuKernelTracker) {
          double qpcFrequency =
            static_cast<double>(g_cpuKernelTracker->getPerfFreq().QuadPart);
          if (qpcFrequency > 0) {
            if (waitTime < MAX_REASONABLE_WAIT_TICKS) {
              double waitTimeMs =
                (static_cast<double>(waitTime) * 1000.0) / qpcFrequency;

              if (waitTimeMs >= 0 && waitTimeMs < 100.0) {
                if (!g_cpuKernelTracker) return;
                g_cpuKernelTracker->getThreadMetrics().totalThreadWaitTimeMs +=
                  waitTimeMs;
                g_cpuKernelTracker->getThreadMetrics().waitCount++;
              }
            }
          }
        }

      } catch (...) {
        // Silent failure
      }
    } else if (memcmp(&record.EventHeader.ProviderId, &PERFINFO_PROVIDER_GUID,
                      sizeof(GUID)) == 0) {
      if (opcode == static_cast<BYTE>(PerfInfoEventId::Interrupt)) {
        interrupts++;
      }
      // Modify the code in processEvent function where it processes DPC events
      else if (opcode == static_cast<BYTE>(PerfInfoEventId::DPC) ||
               opcode == static_cast<BYTE>(PerfInfoEventId::TimerDPC) ||
               opcode == static_cast<BYTE>(PerfInfoEventId::ThreadedDPC)) {
        dpcCount++;

        if (!g_cpuKernelTracker) return;

        uint64_t binaryDpcTime = 0;
        if (parseDpcTimingFromBinary(record, binaryDpcTime) &&
            binaryDpcTime > 0) {
          if (g_cpuKernelTracker && binaryDpcTime < 1000000) {
            totalDpcLatencyTicks += binaryDpcTime;
          }
        }
      }
    }
  } catch (...) {
    // Silent error handling
  }
}

void calculateDpcLatencyPercentages(
  const std::map<std::string, uint64_t>& timingBins, size_t validDurations,
  double& above50us, double& above100us) {
  above50us = 0.0;
  above100us = 0.0;

  if (validDurations == 0) return;

  uint64_t count50us = 0;
  auto it50 = timingBins.find("50-100μs");
  if (it50 != timingBins.end()) count50us += it50->second;

  auto it100 = timingBins.find("100-250μs");
  if (it100 != timingBins.end()) {
    count50us += it100->second;
    above100us += it100->second;
  }

  auto it250 = timingBins.find("250-500μs");
  if (it250 != timingBins.end()) {
    count50us += it250->second;
    above100us += it250->second;
  }

  auto it500 = timingBins.find("500-1000μs");
  if (it500 != timingBins.end()) {
    count50us += it500->second;
    above100us += it500->second;
  }

  auto it1ms = timingBins.find("1-10ms");
  if (it1ms != timingBins.end()) {
    count50us += it1ms->second;
    above100us += it1ms->second;
  }

  auto it10ms = timingBins.find("10-100ms");
  if (it10ms != timingBins.end()) {
    count50us += it10ms->second;
    above100us += it10ms->second;
  }

  above50us = (count50us * 100.0) / validDurations;
  above100us = (above100us * 100.0) / validDurations;
}

void CPUKernelMetricsTracker::updateBenchmarkData(BenchmarkDataPoint& dataPoint) {
  std::lock_guard<std::mutex> lock(m_metricsMutex);
  dataPoint.contextSwitchesPerSec = m_latestMetrics.contextSwitchesPerSec;
  dataPoint.interruptsPerSec = m_latestMetrics.interruptsPerSec;
  dataPoint.dpcCountPerSec = m_latestMetrics.dpcCountPerSec;
  dataPoint.avgDpcLatencyUs = m_latestMetrics.avgDpcLatencyUs;
  dataPoint.dpcLatenciesAbove50us = m_latestMetrics.dpcLatenciesAbove50us;
  dataPoint.dpcLatenciesAbove100us = m_latestMetrics.dpcLatenciesAbove100us;

  dataPoint.voluntaryContextSwitchesPerSec =
    m_latestMetrics.voluntaryContextSwitchesPerSec;
  dataPoint.involuntaryContextSwitchesPerSec =
    m_latestMetrics.involuntaryContextSwitchesPerSec;
  dataPoint.highPriorityInterruptionsPerSec =
    m_latestMetrics.highPriorityInterruptionsPerSec;
  dataPoint.priorityInversionsPerSec = m_latestMetrics.priorityInversionsPerSec;
  dataPoint.avgThreadWaitTimeMs = m_latestMetrics.avgThreadWaitTimeMs;
}

std::string CPUKernelMetricsTracker::logRawData() {
  std::stringstream ss;
  ss << "=== CPU Kernel Metrics Tracker Raw Data Collection ===\n";

  // ETW Provider information
  ss << "\nETW Provider Information:\n";
  ss << "  Thread Provider GUID: " << guidToString(THREAD_PROVIDER_GUID)
     << "\n";
  ss << "  Process Provider GUID: " << guidToString(PROCESS_PROVIDER_GUID)
     << "\n";
  ss << "  PerfInfo Provider GUID: " << guidToString(PERFINFO_PROVIDER_GUID)
     << "\n";

  // Event types being tracked
  ss << "\nTracked Event Types:\n";
  ss << "  Thread Context Switch (ID: "
     << static_cast<int>(ThreadEventId::ContextSwitch) << ")\n";
  ss << "  Interrupt (ID: " << static_cast<int>(PerfInfoEventId::Interrupt)
     << ")\n";
  ss << "  DPC (ID: " << static_cast<int>(PerfInfoEventId::DPC) << ")\n";
  ss << "  Timer DPC (ID: " << static_cast<int>(PerfInfoEventId::TimerDPC)
     << ")\n";
  ss << "  Threaded DPC (ID: " << static_cast<int>(PerfInfoEventId::ThreadedDPC)
     << ")\n";

  // Current counter state
  ss << "\nRaw Counter Values (Since Last Reset):\n";
  ss << "  Context Switches: " << m_contextSwitches.load() << "\n";
  ss << "  Interrupts: " << m_interrupts.load() << "\n";
  ss << "  DPC Count: " << m_dpcCount.load() << "\n";
  ss << "  Total DPC Latency Ticks: " << m_totalDpcLatencyTicks.load() << "\n";
  ss << "  QPC Frequency: " << m_perfFreq.QuadPart << " ticks/second\n";

  // Thread metrics
  ss << "\nThread Wait Metrics:\n";
  ss << "  Voluntary Context Switches: "
     << m_threadMetrics.voluntaryContextSwitches.load() << "\n";
  ss << "  Involuntary Context Switches: "
     << m_threadMetrics.involuntaryContextSwitches.load() << "\n";
  ss << "  High Priority Interruptions: "
     << m_threadMetrics.highPriorityInterruptions.load() << "\n";
  ss << "  Priority Inversions: " << m_threadMetrics.priorityInversions.load()
     << "\n";
  ss << "  Mutex Waits: " << m_threadMetrics.mutexWaits.load() << "\n";
  ss << "  Resource Waits: " << m_threadMetrics.resourceWaits.load() << "\n";
  ss << "  IO Waits: " << m_threadMetrics.ioWaits.load() << "\n";
  ss << "  Total Thread Wait Time (ms): "
     << m_threadMetrics.totalThreadWaitTimeMs.load() << "\n";
  ss << "  Wait Count: " << m_threadMetrics.waitCount.load() << "\n";

  // Sample of wait reasons
  ss << "\nWait Reason Distribution:\n";
  {
    std::lock_guard<std::mutex> lock(m_threadMetrics.waitReasonMutex);
    for (const auto& [reason, count] : m_threadMetrics.waitReasonCounts) {
      ss << "  " << static_cast<int>(reason) << " (";

      // Map common wait reasons to names
      switch (reason) {
        case ThreadWaitReason::Executive:
          ss << "Executive";
          break;
        case ThreadWaitReason::FreePage:
          ss << "FreePage";
          break;
        case ThreadWaitReason::PageIn:
          ss << "PageIn";
          break;
        case ThreadWaitReason::WrMutex:
          ss << "Mutex";
          break;
        case ThreadWaitReason::WrResource:
          ss << "Resource";
          break;
        case ThreadWaitReason::DelayExecution:
          ss << "DelayExecution";
          break;
        case ThreadWaitReason::Suspended:
          ss << "Suspended";
          break;
        case ThreadWaitReason::UserRequest:
          ss << "UserRequest";
          break;
        case ThreadWaitReason::WrVirtualMemory:
          ss << "VirtualMemory";
          break;
        case ThreadWaitReason::WrQueue:
          ss << "Queue";
          break;
        default:
          ss << "Other";
      }

      ss << "): " << count << "\n";
    }
  }

  // DPC latency information
  {
    std::lock_guard<std::mutex> lock(g_dpcStatsMutex);
    ss << "\nDPC Latency Statistics:\n";
    ss << "  DPC Latencies Above 50μs: " << g_dpcLatenciesAbove50us << "%\n";
    ss << "  DPC Latencies Above 100μs: " << g_dpcLatenciesAbove100us << "%\n";
    ss << "  Last Valid Duration Count: " << g_lastValidDurations << "\n";
  }

  // Trace session state
  ss << "\nTracing Session State:\n";
  ss << "  Running: " << (m_running ? "Yes" : "No") << "\n";
  ss << "  Trace Started Successfully: "
     << (m_traceStartedSuccessfully ? "Yes" : "No") << "\n";

  // Latest calculated metrics
  {
    std::lock_guard<std::mutex> lock(m_metricsMutex);
    ss << "\nCalculated Metrics (Per Second):\n";
    ss << "  Context Switches/sec: " << m_latestMetrics.contextSwitchesPerSec
       << "\n";
    ss << "  Interrupts/sec: " << m_latestMetrics.interruptsPerSec << "\n";
    ss << "  DPC Count/sec: " << m_latestMetrics.dpcCountPerSec << "\n";
    ss << "  Avg DPC Latency (μs): " << m_latestMetrics.avgDpcLatencyUs << "\n";
    ss << "  Voluntary Context Switches/sec: "
       << m_latestMetrics.voluntaryContextSwitchesPerSec << "\n";
    ss << "  Involuntary Context Switches/sec: "
       << m_latestMetrics.involuntaryContextSwitchesPerSec << "\n";
    ss << "  High Priority Interruptions/sec: "
       << m_latestMetrics.highPriorityInterruptionsPerSec << "\n";
    ss << "  Priority Inversions/sec: "
       << m_latestMetrics.priorityInversionsPerSec << "\n";
    ss << "  Avg Thread Wait Time (ms): " << m_latestMetrics.avgThreadWaitTimeMs
       << "\n";
  }

  return ss.str();
}
