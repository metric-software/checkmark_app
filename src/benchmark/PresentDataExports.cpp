#include "PresentDataExports.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include <Windows.h>
#include <evntrace.h>

#include "BenchmarkConstants.h"
#include "PresentMonTraceConsumer.hpp"
#include "PresentMonTraceSession.hpp"
#include "../logging/Logger.h"

static constexpr size_t MAX_FRAME_HISTORY = BenchmarkConstants::MAX_FRAME_HISTORY;
static constexpr int MAX_SHUTDOWN_RETRIES = 5;

// Keep a simplified SwapChainData that only tracks the last present event
struct SwapChainData {
  std::shared_ptr<PresentEvent> mLastPresent;
};

struct ProcessInfo {
  uint32_t pid;
  std::unordered_map<uint64_t, SwapChainData> mSwapChains;
};

struct ProcessMonitor {
  ProcessMonitor() : latestMetrics{}, running(false), updateFrequencyMs(1000), 
                     lastQueueUpdate(std::chrono::steady_clock::now()) {}

  std::unique_ptr<PMTraceConsumer> consumer;
  std::unique_ptr<PMTraceSession> session;
  std::thread traceThread;
  std::thread processingThread;

  PM_METRICS latestMetrics;
  std::mutex metricsMutex;

  uint32_t updateFrequencyMs;
  bool running;
  ProcessInfo processInfo;

  std::deque<PM_METRICS> metricsQueue;
  std::mutex queueMutex;
  static constexpr size_t MAX_QUEUE_SIZE = BenchmarkConstants::MAX_QUEUE_SIZE;

  // Per-frame data collection for accurate min/max values
  struct FrameDataCollector {
    // Frame data with timestamps
    struct FrameInfo {
      float frameTime;
      float gpuRenderTime;
      float cpuRenderTime;
      uint64_t timestamp;  // QPC timestamp when frame was captured
    };

    std::deque<FrameInfo> frameBuffer;  // Rolling buffer of frames
    uint64_t timestampFrequency = 0;     // QPC frequency for time conversion
    std::chrono::steady_clock::time_point lastUpdate =
      std::chrono::steady_clock::now();  // Add this line

    // Stats calculated from current buffer
    float minFrameTime = FLT_MAX;
    float maxFrameTime = 0.0f;
    float minGpuRenderTime = FLT_MAX;
    float maxGpuRenderTime = 0.0f;
    float minCpuRenderTime = FLT_MAX;
    float maxCpuRenderTime = 0.0f;
    float sumFrameTime = 0.0f;
    float sumGpuRenderTime = 0.0f;
    float sumCpuRenderTime = 0.0f;
    int frameCount = 0;
    bool extremaNeedRecalc = false;  // Flag to track when extrema need recalculation
    mutable std::vector<float> scratchVector;  // Reusable vector for percentile calculations

    // Initialize with the QPC frequency
    void initialize(uint64_t frequency) {
      timestampFrequency = frequency;
      lastUpdate = std::chrono::steady_clock::now();  // Initialize lastUpdate
    }

    // Add frame and maintain the rolling window
    void addFrame(float frameTime, float gpuRenderTime, float cpuRenderTime,
                  uint64_t currentTimestamp) {
      // Add new frame to buffer
      frameBuffer.push_back(
        {frameTime, gpuRenderTime, cpuRenderTime, currentTimestamp});

      // Remove frames older than 1 second
      cleanupOldFrames(currentTimestamp);

      // Update incremental sums and frame count
      updateIncrementalStats(frameTime, gpuRenderTime, cpuRenderTime, true);
      
      // Update lastUpdate timestamp
      lastUpdate = std::chrono::steady_clock::now();
    }

    // Remove frames older than 1 second
    void cleanupOldFrames(uint64_t currentTimestamp) {
      if (timestampFrequency == 0 || frameBuffer.empty()) return;

      // Calculate timestamp from 1 second ago
      uint64_t oneSecondAgo = currentTimestamp - timestampFrequency;

      // Remove frames older than one second using O(1) front removal
      while (!frameBuffer.empty() && frameBuffer.front().timestamp < oneSecondAgo) {
        const auto& removedFrame = frameBuffer.front();
        updateIncrementalStats(removedFrame.frameTime, removedFrame.gpuRenderTime, removedFrame.cpuRenderTime, false);
        frameBuffer.pop_front();
      }
    }

    // Update incremental statistics (add=true to add, add=false to remove)
    void updateIncrementalStats(float frameTime, float gpuRenderTime, float cpuRenderTime, bool add) {
      if (add) {
        // Adding frame
        sumFrameTime += frameTime;
        sumGpuRenderTime += gpuRenderTime;
        sumCpuRenderTime += cpuRenderTime;
        frameCount++;
        
        // Update extrema only if necessary
        if (frameTime < minFrameTime) minFrameTime = frameTime;
        if (frameTime > maxFrameTime) maxFrameTime = frameTime;
        if (gpuRenderTime < minGpuRenderTime) minGpuRenderTime = gpuRenderTime;
        if (gpuRenderTime > maxGpuRenderTime) maxGpuRenderTime = gpuRenderTime;
        if (cpuRenderTime < minCpuRenderTime) minCpuRenderTime = cpuRenderTime;
        if (cpuRenderTime > maxCpuRenderTime) maxCpuRenderTime = cpuRenderTime;
      } else {
        // Removing frame
        sumFrameTime -= frameTime;
        sumGpuRenderTime -= gpuRenderTime;
        sumCpuRenderTime -= cpuRenderTime;
        frameCount--;
        
        // Note: min/max values are NOT decremented here for performance
        // They will be recalculated only when needed (on snapshot requests)
        extremaNeedRecalc = true;
      }
    }
    
    // Force recalculate extrema when needed (only called on snapshots)
    void recalculateExtrema() {
      if (!extremaNeedRecalc || frameBuffer.empty()) return;
      
      minFrameTime = FLT_MAX;
      maxFrameTime = 0.0f;
      minGpuRenderTime = FLT_MAX;
      maxGpuRenderTime = 0.0f;
      minCpuRenderTime = FLT_MAX;
      maxCpuRenderTime = 0.0f;
      
      for (const auto& frame : frameBuffer) {
        if (frame.frameTime < minFrameTime) minFrameTime = frame.frameTime;
        if (frame.frameTime > maxFrameTime) maxFrameTime = frame.frameTime;
        if (frame.gpuRenderTime < minGpuRenderTime) minGpuRenderTime = frame.gpuRenderTime;
        if (frame.gpuRenderTime > maxGpuRenderTime) maxGpuRenderTime = frame.gpuRenderTime;
        if (frame.cpuRenderTime < minCpuRenderTime) minCpuRenderTime = frame.cpuRenderTime;
        if (frame.cpuRenderTime > maxCpuRenderTime) maxCpuRenderTime = frame.cpuRenderTime;
      }
      
      extremaNeedRecalc = false;
    }

    // Calculate percentile from current buffer using reusable scratch vector
    float calculatePercentile(float percentile) const {
      // Recalculate extrema if needed before percentile calculation
      const_cast<FrameDataCollector*>(this)->recalculateExtrema();
      
      if (frameBuffer.empty()) return 0.0f;
      if (frameBuffer.size() == 1) return frameBuffer[0].frameTime;

      // Reuse scratch vector to avoid allocations
      scratchVector.clear();
      scratchVector.reserve(frameBuffer.size());
      for (const auto& frame : frameBuffer) {
        scratchVector.push_back(frame.frameTime);
      }

      std::sort(scratchVector.begin(), scratchVector.end());

      float fraction = percentile / 100.0f;
      size_t index = static_cast<size_t>(scratchVector.size() * fraction);
      index = std::min(index, scratchVector.size() - 1);

      return scratchVector[index];
    }

    // Calculate standard deviation of frame times (expensive - only call at snapshot time)
    float calculateStdDev() const {
      const_cast<FrameDataCollector*>(this)->recalculateExtrema();
      
      if (frameBuffer.empty() || frameCount == 0) return 0.0f;

      float mean = sumFrameTime / frameCount;
      float sumSquaredDiff = 0.0f;

      for (const auto& frame : frameBuffer) {
        float diff = frame.frameTime - mean;
        sumSquaredDiff += diff * diff;
      }

      return std::sqrt(sumSquaredDiff / frameBuffer.size());
    }
    
    // Calculate all expensive statistics at snapshot time only
    void calculateSnapshotStats(PM_METRICS& metrics) {
      // Recalculate extrema if needed
      recalculateExtrema();
      
      // Calculate expensive percentiles only at snapshot time
      metrics.frameTime95Percentile = calculatePercentile(95.0f);
      metrics.frameTime99Percentile = calculatePercentile(99.0f);
      metrics.frameTime995Percentile = calculatePercentile(99.5f);
      metrics.frameTimeVariance = calculateStdDev();  // Note: actually stddev, not variance
      
      // Update the basic aggregated metrics
      metrics.frameCount = frameCount;
      metrics.minFrameTime = minFrameTime;
      metrics.maxFrameTime = maxFrameTime;
      metrics.minGpuRenderTime = minGpuRenderTime;
      metrics.maxGpuRenderTime = maxGpuRenderTime;
      metrics.minCpuRenderTime = minCpuRenderTime;
      metrics.maxCpuRenderTime = maxCpuRenderTime;
      
      if (frameCount > 0) {
        metrics.frametime = sumFrameTime / frameCount;
        metrics.fps = 1000.0f / metrics.frametime;
        metrics.gpuRenderTime = sumGpuRenderTime / frameCount;
        metrics.cpuRenderTime = sumCpuRenderTime / frameCount;
      }
    }
  };

  FrameDataCollector frameCollector;
  std::chrono::steady_clock::time_point lastQueueUpdate;  // Per-monitor queue timing
};

namespace {
std::mutex g_monitorsMutex;
std::unordered_map<uint32_t, std::unique_ptr<ProcessMonitor>> g_monitors;
bool g_initialized = false;
PresentMetricsCallback g_metricsCallback = nullptr;
}  // namespace

void StopExistingSession(const wchar_t* sessionName) {
  for (int attempt = 0; attempt < MAX_SHUTDOWN_RETRIES; ++attempt) {
    ULONG status = StopNamedTraceSession(sessionName);
    if (status == ERROR_SUCCESS || status == ERROR_WMI_INSTANCE_NOT_FOUND) {
      break;
    }
    Sleep(500);
  }
  Sleep(500);
}

std::wstring GetSessionNameForProcess(uint32_t processId) {
  return std::wstring(L"PresentMon_Session_") + std::to_wstring(processId);
}

PRESENT_DATA_API PM_STATUS PM_Initialize() {
  std::lock_guard<std::mutex> lock(g_monitorsMutex);
  if (g_initialized) {
    return PM_STATUS::PM_SUCCESS;
  }
  g_initialized = true;
  return PM_STATUS::PM_SUCCESS;
}

PRESENT_DATA_API PM_STATUS PM_StartMonitoring(uint32_t processId,
                                              uint32_t updateFrequencyMs) {
  if (!g_initialized) {
    LOG_ERROR << "[ERROR] Library not initialized.";
    return PM_STATUS::PM_ERROR_NOT_RUNNING;
  }

  std::lock_guard<std::mutex> lock(g_monitorsMutex);
  if (g_monitors.find(processId) != g_monitors.end()) {
    LOG_ERROR << "[ERROR] Already monitoring process: " << processId
             ;
    return PM_STATUS::PM_ERROR_ALREADY_RUNNING;
  }

  auto monitor = std::make_unique<ProcessMonitor>();
  monitor->updateFrequencyMs = updateFrequencyMs;
  monitor->running = true;
  monitor->processInfo.pid = processId;

  try {
    monitor->consumer = std::make_unique<PMTraceConsumer>();
    // Enable ETW filtering for better performance
    monitor->consumer->mFilteredProcessIds = true;  // Enable process filtering
    monitor->consumer->AddTrackedProcessForFiltering(processId);
    monitor->consumer->mTrackDisplay = true;
    monitor->consumer->mTrackGPU = true;
    monitor->consumer->mTrackGPUVideo = false;  // Disable if not used to reduce overhead
    monitor->consumer->mTrackInput = false;     // Disable input tracking for performance
    monitor->consumer->mTrackFrameType = false; // Disable frame type tracking for performance
    monitor->consumer->mTrackPMMeasurements = true;
    monitor->consumer->mFilteredEvents = true;  // Enable event filtering
  } catch (const std::exception& e) {
    LOG_ERROR << "[ERROR] Failed to initialize consumer: " << e.what()
             ;
    return PM_STATUS::PM_ERROR_START_FAILED;
  }

  try {
    monitor->session = std::make_unique<PMTraceSession>();
    monitor->session->mPMConsumer = monitor->consumer.get();
  } catch (const std::exception& e) {
    LOG_ERROR << "[ERROR] Failed to initialize session: " << e.what()
             ;
    return PM_STATUS::PM_ERROR_START_FAILED;
  }

  std::wstring sessionName = GetSessionNameForProcess(processId);
  StopExistingSession(sessionName.c_str());

  ULONG status = monitor->session->Start(nullptr, sessionName.c_str());
  if (status != ERROR_SUCCESS) {
    LOG_ERROR << "[ERROR] Failed to start session with error code: " << status
             ;
    return PM_STATUS::PM_ERROR_START_FAILED;
  }

  monitor->frameCollector.initialize(
    monitor->session->mTimestampFrequency.QuadPart);

  monitor->traceThread = std::thread([monitor = monitor.get()]() {
    ULONG processStatus =
      ProcessTrace(&monitor->session->mTraceHandle, 1, nullptr, nullptr);
    if (processStatus != ERROR_SUCCESS) {
      LOG_ERROR << "[ERROR] ProcessTrace() ended with error status "
                << processStatus;
      monitor->running = false;
    }
  });

  monitor->processingThread = std::thread([monitor = monitor.get()]() {
    while (monitor->running) {
      std::vector<std::shared_ptr<PresentEvent>> events;
      monitor->consumer->DequeuePresentEvents(events);

      if (!events.empty()) {
        for (const auto& pe : events) {
          if (pe->IsLost || pe->ProcessId != monitor->processInfo.pid) {
            continue;
          }

          // Process resolution and display metrics immediately
          if (pe->DestWidth > 0 && pe->DestHeight > 0) {
            std::lock_guard<std::mutex> lock(monitor->metricsMutex);
            monitor->latestMetrics.destWidth = pe->DestWidth;
            monitor->latestMetrics.destHeight = pe->DestHeight;
            monitor->latestMetrics.syncInterval = pe->SyncInterval;
            monitor->latestMetrics.supportsTearing = pe->SupportsTearing;
          }

          auto& chain = monitor->processInfo.mSwapChains[pe->SwapChainAddress];

          if (pe->FinalState == PresentResult::Presented &&
              chain.mLastPresent != nullptr) {
            double freq = static_cast<double>(
              monitor->session->mTimestampFrequency.QuadPart);

            double frameTime_ms = 0.0;
            if (pe->PresentStartTime > chain.mLastPresent->PresentStartTime) {
              double qpcDelta = static_cast<double>(
                pe->PresentStartTime - chain.mLastPresent->PresentStartTime);
              frameTime_ms = (qpcDelta * 1000.0) / freq;
            }

            double gpuRenderTime_ms = 0.0;
            if (pe->GPUDuration > 0) {
              gpuRenderTime_ms = (pe->GPUDuration * 1000.0) / freq;
            }

            double cpuTime_ms = frameTime_ms - gpuRenderTime_ms;
            if (cpuTime_ms < 0) cpuTime_ms = 0.0;

            // Add frame to the rolling window
            if (frameTime_ms > 0) {
              std::lock_guard<std::mutex> lock(monitor->metricsMutex);

              // Add frame to collector
              monitor->frameCollector.addFrame(
                static_cast<float>(frameTime_ms),
                static_cast<float>(gpuRenderTime_ms),
                static_cast<float>(cpuTime_ms), pe->PresentStartTime);

              // Update basic metrics for current frame
              monitor->latestMetrics.frameId = pe->AppFrameId;
              monitor->latestMetrics.frametime =
                static_cast<float>(frameTime_ms);
              monitor->latestMetrics.fps =
                frameTime_ms > 0 ? 1000.0f / frameTime_ms : 0;
              monitor->latestMetrics.gpuRenderTime =
                static_cast<float>(gpuRenderTime_ms);
              monitor->latestMetrics.gpuVideoTime =
                static_cast<float>(pe->GPUVideoDuration > 0
                                     ? (pe->GPUVideoDuration * 1000.0) / freq
                                     : 0);
              monitor->latestMetrics.cpuRenderTime =
                static_cast<float>(cpuTime_ms);

              // Update aggregated metrics
              monitor->latestMetrics.frameCount =
                monitor->frameCollector.frameCount;
              monitor->latestMetrics.minFrameTime =
                monitor->frameCollector.minFrameTime;
              monitor->latestMetrics.maxFrameTime =
                monitor->frameCollector.maxFrameTime;
              monitor->latestMetrics.minGpuRenderTime =
                monitor->frameCollector.minGpuRenderTime;
              monitor->latestMetrics.maxGpuRenderTime =
                monitor->frameCollector.maxGpuRenderTime;
              monitor->latestMetrics.minCpuRenderTime =
                monitor->frameCollector.minCpuRenderTime;
              monitor->latestMetrics.maxCpuRenderTime =
                monitor->frameCollector.maxCpuRenderTime;

              // Calculate average values
              if (monitor->frameCollector.frameCount > 0) {
                monitor->latestMetrics.frametime =
                  monitor->frameCollector.sumFrameTime /
                  monitor->frameCollector.frameCount;
                monitor->latestMetrics.fps =
                  1000.0f / monitor->latestMetrics.frametime;
                monitor->latestMetrics.gpuRenderTime =
                  monitor->frameCollector.sumGpuRenderTime /
                  monitor->frameCollector.frameCount;
                monitor->latestMetrics.cpuRenderTime =
                  monitor->frameCollector.sumCpuRenderTime /
                  monitor->frameCollector.frameCount;
              }

              // Note: Percentiles and variance are expensive - only calculated at snapshot time
              // Basic metrics are updated here for immediate use
              
              // Store a copy in the queue periodically based on updateFrequencyMs
              auto now = std::chrono::steady_clock::now();
              auto elapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - monitor->lastQueueUpdate)
                  .count();

              // Push metrics to queue at configured frequency
              if (elapsed >= monitor->updateFrequencyMs) {
                // Calculate expensive stats only at snapshot time
                PM_METRICS snapshotMetrics = monitor->latestMetrics;
                monitor->frameCollector.calculateSnapshotStats(snapshotMetrics);
                
                std::lock_guard<std::mutex> queueLock(monitor->queueMutex);
                monitor->metricsQueue.push_back(snapshotMetrics);
                if (monitor->metricsQueue.size() >
                    ProcessMonitor::MAX_QUEUE_SIZE) {
                  monitor->metricsQueue.pop_front();
                }

                // Notify callback if registered
                if (g_metricsCallback) {
                  g_metricsCallback(monitor->processInfo.pid, &snapshotMetrics);
                }

                monitor->lastQueueUpdate = now;
              }
            }
          }
          chain.mLastPresent = pe;
        }
      }

      // Check if we need to force an update due to timeout
      {
        std::lock_guard<std::mutex> lock(monitor->metricsMutex);
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - monitor->frameCollector.lastUpdate)
                         .count();

        // If no frames arrived but a full second has passed, emit the last
        // metrics
        if (elapsed >= 1000 && monitor->frameCollector.frameCount == 0 &&
            g_metricsCallback) {
          // Force a metrics update with current values
          monitor->latestMetrics.frameCount = 0;
          g_metricsCallback(monitor->processInfo.pid, &monitor->latestMetrics);
          monitor->frameCollector.lastUpdate = now;

          // Store a copy in the queue
          std::lock_guard<std::mutex> queueLock(monitor->queueMutex);
          monitor->metricsQueue.push_back(monitor->latestMetrics);
          if (monitor->metricsQueue.size() > ProcessMonitor::MAX_QUEUE_SIZE) {
            monitor->metricsQueue.pop_front();
          }
        }
      }

      // Sleep based on update frequency to reduce CPU usage
      uint32_t sleepMs = std::min(monitor->updateFrequencyMs / 4, 250u);  // Quarter of update frequency, max 250ms
      std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
  });

  g_monitors[processId] = std::move(monitor);
  return PM_STATUS::PM_SUCCESS;
}

PRESENT_DATA_API PM_STATUS PM_StopMonitoring(uint32_t processId) {
  std::lock_guard<std::mutex> lock(g_monitorsMutex);
  auto it = g_monitors.find(processId);
  if (it == g_monitors.end()) {
    LOG_ERROR << "[ERROR] No active monitor found for process " << processId
             ;
    return PM_STATUS::PM_ERROR_NOT_RUNNING;
  }

  it->second->running = false;

  if (it->second->processingThread.joinable()) {
    try {
      it->second->processingThread.join();
    } catch (const std::exception& e) {
      LOG_ERROR << "[ERROR] Error joining processing thread: " << e.what()
               ;
    }
  }

  try {
    it->second->session->Stop();
  } catch (const std::exception& e) {
    LOG_ERROR << "[ERROR] Error stopping ETW session: " << e.what()
             ;
  }

  if (it->second->traceThread.joinable()) {
    try {
      it->second->traceThread.join();
    } catch (const std::exception& e) {
      LOG_ERROR << "[ERROR] Error joining trace thread: " << e.what()
               ;
    }
  }

  g_monitors.erase(it);
  return PM_STATUS::PM_SUCCESS;
}

PRESENT_DATA_API PM_STATUS
PM_GetMetrics(uint32_t processId, PM_METRICS* metrics,
              std::vector<PM_METRICS>* allMetricsSinceLastCall) {
  if (!metrics) {
    return PM_STATUS::PM_ERROR_INVALID_PARAMETER;
  }

  std::lock_guard<std::mutex> lock(g_monitorsMutex);
  auto it = g_monitors.find(processId);
  if (it == g_monitors.end()) {
    return PM_STATUS::PM_ERROR_NOT_RUNNING;
  }

  std::lock_guard<std::mutex> metricsLock(it->second->metricsMutex);
  *metrics = it->second->latestMetrics;

  // If caller wants all metrics since last call
  if (allMetricsSinceLastCall) {
    std::lock_guard<std::mutex> queueLock(it->second->queueMutex);
    allMetricsSinceLastCall->clear();
    // Reserve space to avoid reallocations during copy
    allMetricsSinceLastCall->reserve(it->second->metricsQueue.size());
    allMetricsSinceLastCall->insert(allMetricsSinceLastCall->end(),
                                    it->second->metricsQueue.begin(),
                                    it->second->metricsQueue.end());
    it->second->metricsQueue.clear();
  }

  return PM_STATUS::PM_SUCCESS;
}

PRESENT_DATA_API void PM_Shutdown() {
  std::lock_guard<std::mutex> lock(g_monitorsMutex);

  for (auto& pair : g_monitors) {
    auto& monitor = pair.second;
    monitor->running = false;

    if (monitor->processingThread.joinable()) {
      monitor->processingThread.join();
    }
    if (monitor->traceThread.joinable()) {
      monitor->traceThread.join();
    }
    monitor->session->Stop();
  }

  g_monitors.clear();
  g_initialized = false;
}

PRESENT_DATA_API void PM_SetMetricsCallback(PresentMetricsCallback callback) {
  g_metricsCallback = callback;
}
