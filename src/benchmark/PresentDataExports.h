/*
 * PresentDataExports - ETW-based Frame Timing and Presentation Metrics
 * 
 * WORKING METRICS PROVIDED:
 * - frametime: Frame time in milliseconds (from display timestamps)
 * - fps: Frames per second (1000/frametime)
 * - gpuRenderTime: GPU duration (instant), in ms
 * - gpuVideoTime: GPU video processing time (instant), in ms  
 * - cpuRenderTime: CPU render time (instant), in ms
 * - appRenderTime: Application render time (instant), in ms
 * - appSleepTime: Time app spent sleeping, in ms
 * - destWidth: Destination surface width
 * - destHeight: Destination surface height
 * - supportsTearing: Whether tearing is supported
 * - syncInterval: VSync interval
 * - frameId: Frame sequence number
 * - presentFlags: Present flags from DXGI/D3D
 * - runtime: Runtime (DXGI, D3D9, etc)
 * - presentMode: Present mode (Flip, BitBlt, etc)
 * - minFrameTime: Minimum frame time in collection interval
 * - maxFrameTime: Maximum frame time in collection interval
 * - minGpuRenderTime: Minimum GPU render time
 * - maxGpuRenderTime: Maximum GPU render time
 * - minCpuRenderTime: Minimum CPU render time
 * - maxCpuRenderTime: Maximum CPU render time
 * - frameTimeVariance: Standard deviation of frame times (note: historically named 'variance')
 * - frameTime99Percentile: 99th percentile frame time (1% low)
 * - frameTime95Percentile: 95th percentile frame time (5% low)
 * - frameTime995Percentile: 99.5th percentile frame time (0.5% low)
 * - frameCount: Number of frames in this collection interval
 */

#pragma once
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

// Remove DLL export/import macros
#define PRESENT_DATA_API

enum class PM_STATUS {
  PM_SUCCESS = 0,
  PM_ERROR_INVALID_PARAMETER,
  PM_ERROR_ALREADY_RUNNING,
  PM_ERROR_NOT_RUNNING,
  PM_ERROR_START_FAILED,
  PM_ERROR_STOP_FAILED
};

// Runtime values matching the implementation
enum class PM_RUNTIME : uint32_t { OTHER = 0, DXGI = 1, D3D9 = 2 };

// PresentMode values matching the implementation
enum class PM_PRESENT_MODE : uint32_t {
  UNKNOWN = 0,
  HARDWARE_LEGACY_FLIP = 1,
  HARDWARE_LEGACY_COPY_TO_FRONT_BUFFER = 2,
  HARDWARE_INDEPENDENT_FLIP = 3,
  COMPOSED_FLIP = 4,
  COMPOSED_COPY_GPU_GDI = 5,
  COMPOSED_COPY_CPU_GDI = 6,
  HARDWARE_COMPOSED_INDEPENDENT_FLIP = 8
};

struct PM_METRICS {
  // Core timing metrics
  float frametime;      // Frame time in milliseconds (from display timestamps)
  float fps;            // Frames per second (1000/frametime)
  float gpuRenderTime;  // GPU duration (instant), in ms
  float gpuVideoTime;   // GPU video processing time (instant), in ms
  float cpuRenderTime;  // CPU render time (instant), in ms
  float appRenderTime;  // Application render time (instant), in ms
  float appSleepTime;   // Time app spent sleeping, in ms

  // Display metrics
  uint32_t destWidth;    // Destination surface width
  uint32_t destHeight;   // Destination surface height
  bool supportsTearing;  // Whether tearing is supported
  int32_t syncInterval;  // VSync interval

  // Frame metadata
  uint32_t frameId;       // Frame sequence number
  uint32_t presentFlags;  // Present flags from DXGI/D3D
  uint32_t runtime;       // Runtime (DXGI, D3D9, etc)
  uint32_t presentMode;   // Present mode (Flip, BitBlt, etc)

  // Min/max metrics over collection interval
  float minFrameTime;      // Minimum frame time in collection interval
  float maxFrameTime;      // Maximum frame time in collection interval
  float minGpuRenderTime;  // Minimum GPU render time
  float maxGpuRenderTime;  // Maximum GPU render time
  float minCpuRenderTime;  // Minimum CPU render time
  float maxCpuRenderTime;  // Maximum CPU render time

  // Statistical metrics over collection interval
  float frameTimeVariance;       // Standard deviation of frame times (note: historically named 'variance')
  float frameTime99Percentile;   // 99th percentile frame time (1% low)
  float frameTime95Percentile;   // 95th percentile frame time (5% low)
  float frameTime995Percentile;  // 99.5th percentile frame time (0.5% low)

  // Collection stats
  int frameCount;  // Number of frames in this collection interval
};

// Forward declarations
struct ProcessInfo;
struct PresentEvent;

// Callback type for receiving metrics updates
typedef void (*PresentMetricsCallback)(uint32_t processId,
                                       const PM_METRICS* metrics);

extern "C" {
// Core API Functions
PRESENT_DATA_API PM_STATUS PM_Initialize();
PRESENT_DATA_API PM_STATUS PM_StartMonitoring(uint32_t processId,
                                              uint32_t updateFrequencyMs);
PRESENT_DATA_API PM_STATUS PM_StopMonitoring(uint32_t processId);
PRESENT_DATA_API PM_STATUS
PM_GetMetrics(uint32_t processId, PM_METRICS* metrics,
              std::vector<PM_METRICS>* allMetricsSinceLastCall = nullptr);
PRESENT_DATA_API void PM_SetMetricsCallback(PresentMetricsCallback callback);
PRESENT_DATA_API void PM_Shutdown();
}
