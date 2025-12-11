#include "NvidiaMetrics.h"

#include <chrono>
#include <iostream>
#include <map>

#include <Windows.h>

// Helper function to convert NVML error codes to strings for debugging
const char* nvmlErrorString(nvmlReturn_t result) {
  switch (result) {
    case NVML_SUCCESS: return "NVML_SUCCESS";
    case NVML_ERROR_UNINITIALIZED: return "NVML_ERROR_UNINITIALIZED";
    case NVML_ERROR_INVALID_ARGUMENT: return "NVML_ERROR_INVALID_ARGUMENT";
    case NVML_ERROR_NOT_SUPPORTED: return "NVML_ERROR_NOT_SUPPORTED";
    case NVML_ERROR_NO_PERMISSION: return "NVML_ERROR_NO_PERMISSION";
    case NVML_ERROR_ALREADY_INITIALIZED: return "NVML_ERROR_ALREADY_INITIALIZED";
    case NVML_ERROR_NOT_FOUND: return "NVML_ERROR_NOT_FOUND";
    case NVML_ERROR_INSUFFICIENT_SIZE: return "NVML_ERROR_INSUFFICIENT_SIZE";
    case NVML_ERROR_INSUFFICIENT_POWER: return "NVML_ERROR_INSUFFICIENT_POWER";
    case NVML_ERROR_DRIVER_NOT_LOADED: return "NVML_ERROR_DRIVER_NOT_LOADED";
    case NVML_ERROR_TIMEOUT: return "NVML_ERROR_TIMEOUT";
    case NVML_ERROR_IRQ_ISSUE: return "NVML_ERROR_IRQ_ISSUE";
    case NVML_ERROR_LIBRARY_NOT_FOUND: return "NVML_ERROR_LIBRARY_NOT_FOUND";
    case NVML_ERROR_FUNCTION_NOT_FOUND: return "NVML_ERROR_FUNCTION_NOT_FOUND";
    case NVML_ERROR_CORRUPTED_INFOROM: return "NVML_ERROR_CORRUPTED_INFOROM";
    case NVML_ERROR_GPU_IS_LOST: return "NVML_ERROR_GPU_IS_LOST";
    case NVML_ERROR_RESET_REQUIRED: return "NVML_ERROR_RESET_REQUIRED";
    case NVML_ERROR_OPERATING_SYSTEM: return "NVML_ERROR_OPERATING_SYSTEM";
    case NVML_ERROR_LIB_RM_VERSION_MISMATCH: return "NVML_ERROR_LIB_RM_VERSION_MISMATCH";
    default: return "NVML_ERROR_UNKNOWN";
  }
}

// Helper function for safe library loading with structured exception handling
HMODULE SafeLoadLibrary(const char* libraryName, BOOL& exceptionOccurred) {
  HMODULE lib = nullptr;
  __try {
    lib = LoadLibraryA(libraryName);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    exceptionOccurred = TRUE;
  }
  return lib;
}

// Forward declare function pointer globals for helper functions to use
#ifdef CAN_USE_NVML
// Function pointers need to be accessible to helper functions
static nvmlShutdown_t g_nvmlShutdown_ptr = nullptr;
static nvmlDeviceGetUtilizationRates_t g_nvmlDeviceGetUtilizationRates_ptr =
  nullptr;
static nvmlDeviceGetMemoryInfo_t g_nvmlDeviceGetMemoryInfo_ptr = nullptr;
static nvmlDeviceGetComputeRunningProcesses_t
  g_nvmlDeviceGetComputeRunningProcesses_ptr = nullptr;
static nvmlDeviceGetProcessUtilization_t g_nvmlDeviceGetProcessUtilization_ptr =
  nullptr;
#endif

// Helper function for safe NVML shutdown with structured exception handling
void SafeNvmlShutdown() {
#ifdef CAN_USE_NVML
  if (g_nvmlShutdown_ptr) {
    __try {
      g_nvmlShutdown_ptr();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      std::cout << "Exception occurred during NVML shutdown" << std::endl;
    }
  }
#endif
}

// Update helper functions to use function pointers
bool SafeGetBasicUtilizationMetrics(nvmlDevice_t device, unsigned int& gpuUtil,
                                    unsigned int& memUtil) {
#ifdef CAN_USE_NVML
  if (!g_nvmlDeviceGetUtilizationRates_ptr) {
    gpuUtil = -1;
    memUtil = -1;
    return false;
  }

  __try {
    nvmlUtilization_t utilization = {};
    nvmlReturn_t result =
      g_nvmlDeviceGetUtilizationRates_ptr(device, &utilization);
    if (result == NVML_SUCCESS) {
      gpuUtil = utilization.gpu;
      memUtil = utilization.memory;
      return true;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // Ignore exception and return default values
  }
  return false;
#else
  gpuUtil = -1;
  memUtil = -1;
  return false;
#endif
}

// Update helper function for memory metrics
bool SafeGetMemoryMetrics(nvmlDevice_t device, unsigned long long& totalMem,
                          unsigned long long& usedMem) {
#ifdef CAN_USE_NVML
  if (!g_nvmlDeviceGetMemoryInfo_ptr) {
    totalMem = 0;
    usedMem = 0;
    return false;
  }

  __try {
    nvmlMemory_t memory = {};
    nvmlReturn_t result = g_nvmlDeviceGetMemoryInfo_ptr(device, &memory);
    if (result == NVML_SUCCESS) {
      totalMem = memory.total;
      usedMem = memory.used;
      return true;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // Ignore exception and return default values
  }
  return false;
#else
  totalMem = 0;
  usedMem = 0;
  return false;
#endif
}

// Update SafeGetDetailedDeviceMetrics to use function pointers
bool SafeGetDetailedDeviceMetrics(
  nvmlDevice_t device, NvidiaGPUMetrics& metrics,
  std::vector<NvidiaProcessGPUMetrics>& processMetrics,
  bool (*getGPUMetricsFunc)(nvmlDevice_t, NvidiaGPUMetrics&)) {
#ifdef CAN_USE_NVML
  if (!g_nvmlDeviceGetComputeRunningProcesses_ptr ||
      !g_nvmlDeviceGetProcessUtilization_ptr) {
    return false;
  }

  bool success = false;

  try {
    // First get basic device metrics
    success = getGPUMetricsFunc(device, metrics);
    if (!success) {
      return false;
    }

    // Get a list of processes using this GPU
    unsigned int procCount = 64;
    nvmlProcessInfo_t procInfos[64];
    nvmlReturn_t result =
      g_nvmlDeviceGetComputeRunningProcesses_ptr(device, &procCount, procInfos);
    if (result != NVML_SUCCESS) {
      // Unable to get process list, but we still have basic metrics
      return true;
    }

    // Get process utilization samples
    unsigned int sampleCount = 64;
    nvmlProcessUtilizationSample_t samples[64];
    result = g_nvmlDeviceGetProcessUtilization_ptr(
      device, samples, &sampleCount, 1000000);  // 1 second sampling

    if (result != NVML_SUCCESS) {
      // Unable to get process utilization, but we have basic metrics
      return true;
    }

    // Process the samples and build process metrics
    // ...existing code for processing samples...

    for (unsigned int i = 0; i < sampleCount; i++) {
      // ...existing process sample code...
    }
  } catch (...) {
    std::cout << "Exception during detailed metrics collection" << std::endl;
    return false;
  }

  return success;
#else
  return false;
#endif
}

// Constructor - initialize function pointer globals to nullptr
NvidiaMetricsCollector::NvidiaMetricsCollector(QObject* parent)
    : QObject(parent), running(false), updateInterval(1000),
      nvmlInitialized(false), staticInfoInitialized(false) {
  // Set all function pointers to nullptr in constructor
#ifdef CAN_USE_NVML
  nvmlLibrary = nullptr;
  nvmlInit_v2_ptr = nullptr;
  nvmlShutdown_ptr = nullptr;
  nvmlDeviceGetCount_v2_ptr = nullptr;
  nvmlDeviceGetHandleByIndex_v2_ptr = nullptr;
  nvmlDeviceGetTemperature_ptr = nullptr;
  nvmlDeviceGetUtilizationRates_ptr = nullptr;
  nvmlDeviceGetPowerUsage_ptr = nullptr;
  nvmlDeviceGetMemoryInfo_ptr = nullptr;
  nvmlDeviceGetFanSpeed_ptr = nullptr;
  nvmlDeviceGetClockInfo_ptr = nullptr;
  nvmlDeviceGetName_ptr = nullptr;
  nvmlDeviceGetPciInfo_ptr = nullptr;
  nvmlSystemGetDriverVersion_ptr = nullptr;
  nvmlDeviceGetCurrPcieLinkWidth_ptr = nullptr;
  nvmlDeviceGetCurrPcieLinkGeneration_ptr = nullptr;
  nvmlDeviceGetEncoderUtilization_ptr = nullptr;
  nvmlDeviceGetDecoderUtilization_ptr = nullptr;
  nvmlDeviceGetPcieThroughput_ptr = nullptr;
  nvmlDeviceGetComputeRunningProcesses_ptr = nullptr;
  nvmlDeviceGetProcessUtilization_ptr = nullptr;
  nvmlErrorString_ptr = nullptr;
#endif
}

NvidiaMetricsCollector::~NvidiaMetricsCollector() {
  stopCollecting();
  shutdownNVML();
}

#ifdef CAN_USE_NVML
bool NvidiaMetricsCollector::loadNvmlLibrary() {
  // First try to load nvml.dll directly
  BOOL exceptionOccurred = FALSE;
  nvmlLibrary = SafeLoadLibrary("nvml.dll", exceptionOccurred);

  // If that fails, try the path in the NVIDIA driver directory
  if (!nvmlLibrary && !exceptionOccurred) {
    char systemPath[MAX_PATH] = {0};
    if (GetSystemDirectoryA(systemPath, MAX_PATH)) {
      std::string nvmlPath =
        std::string(systemPath) + "\\drivers\\nvidia\\nvml\\nvml.dll";
      nvmlLibrary = SafeLoadLibrary(nvmlPath.c_str(), exceptionOccurred);
    }
  }

  if (!nvmlLibrary) {
    std::cout << "Failed to load NVML library" << std::endl;
    return false;
  }

  // Load all function pointers
  nvmlInit_v2_ptr = (nvmlInit_v2_t)GetProcAddress(nvmlLibrary, "nvmlInit_v2");
  nvmlShutdown_ptr =
    (nvmlShutdown_t)GetProcAddress(nvmlLibrary, "nvmlShutdown");
  nvmlDeviceGetCount_v2_ptr = (nvmlDeviceGetCount_v2_t)GetProcAddress(
    nvmlLibrary, "nvmlDeviceGetCount_v2");
  nvmlDeviceGetHandleByIndex_v2_ptr =
    (nvmlDeviceGetHandleByIndex_v2_t)GetProcAddress(
      nvmlLibrary, "nvmlDeviceGetHandleByIndex_v2");
  nvmlDeviceGetTemperature_ptr = (nvmlDeviceGetTemperature_t)GetProcAddress(
    nvmlLibrary, "nvmlDeviceGetTemperature");
  nvmlDeviceGetUtilizationRates_ptr =
    (nvmlDeviceGetUtilizationRates_t)GetProcAddress(
      nvmlLibrary, "nvmlDeviceGetUtilizationRates");
  nvmlDeviceGetPowerUsage_ptr = (nvmlDeviceGetPowerUsage_t)GetProcAddress(
    nvmlLibrary, "nvmlDeviceGetPowerUsage");
  nvmlDeviceGetMemoryInfo_ptr = (nvmlDeviceGetMemoryInfo_t)GetProcAddress(
    nvmlLibrary, "nvmlDeviceGetMemoryInfo");
  nvmlDeviceGetFanSpeed_ptr = (nvmlDeviceGetFanSpeed_t)GetProcAddress(
    nvmlLibrary, "nvmlDeviceGetFanSpeed");
  nvmlDeviceGetClockInfo_ptr = (nvmlDeviceGetClockInfo_t)GetProcAddress(
    nvmlLibrary, "nvmlDeviceGetClockInfo");
  nvmlDeviceGetName_ptr =
    (nvmlDeviceGetName_t)GetProcAddress(nvmlLibrary, "nvmlDeviceGetName");
  nvmlDeviceGetPciInfo_ptr =
    (nvmlDeviceGetPciInfo_t)GetProcAddress(nvmlLibrary, "nvmlDeviceGetPciInfo");
  nvmlSystemGetDriverVersion_ptr = (nvmlSystemGetDriverVersion_t)GetProcAddress(
    nvmlLibrary, "nvmlSystemGetDriverVersion");
  nvmlDeviceGetCurrPcieLinkWidth_ptr =
    (nvmlDeviceGetCurrPcieLinkWidth_t)GetProcAddress(
      nvmlLibrary, "nvmlDeviceGetCurrPcieLinkWidth");
  nvmlDeviceGetCurrPcieLinkGeneration_ptr =
    (nvmlDeviceGetCurrPcieLinkGeneration_t)GetProcAddress(
      nvmlLibrary, "nvmlDeviceGetCurrPcieLinkGeneration");
  nvmlDeviceGetEncoderUtilization_ptr =
    (nvmlDeviceGetEncoderUtilization_t)GetProcAddress(
      nvmlLibrary, "nvmlDeviceGetEncoderUtilization");
  nvmlDeviceGetDecoderUtilization_ptr =
    (nvmlDeviceGetDecoderUtilization_t)GetProcAddress(
      nvmlLibrary, "nvmlDeviceGetDecoderUtilization");
  nvmlDeviceGetPcieThroughput_ptr =
    (nvmlDeviceGetPcieThroughput_t)GetProcAddress(
      nvmlLibrary, "nvmlDeviceGetPcieThroughput");
  nvmlDeviceGetComputeRunningProcesses_ptr =
    (nvmlDeviceGetComputeRunningProcesses_t)GetProcAddress(
      nvmlLibrary, "nvmlDeviceGetComputeRunningProcesses");
  nvmlDeviceGetProcessUtilization_ptr =
    (nvmlDeviceGetProcessUtilization_t)GetProcAddress(
      nvmlLibrary, "nvmlDeviceGetProcessUtilization");
  nvmlErrorString_ptr =
    (nvmlErrorString_t)GetProcAddress(nvmlLibrary, "nvmlErrorString");

  // Also set global function pointers for helper functions
  g_nvmlShutdown_ptr = nvmlShutdown_ptr;
  g_nvmlDeviceGetUtilizationRates_ptr = nvmlDeviceGetUtilizationRates_ptr;
  g_nvmlDeviceGetMemoryInfo_ptr = nvmlDeviceGetMemoryInfo_ptr;
  g_nvmlDeviceGetComputeRunningProcesses_ptr =
    nvmlDeviceGetComputeRunningProcesses_ptr;
  g_nvmlDeviceGetProcessUtilization_ptr = nvmlDeviceGetProcessUtilization_ptr;

  // Check if critical functions were loaded
  if (!nvmlInit_v2_ptr || !nvmlShutdown_ptr || !nvmlDeviceGetCount_v2_ptr ||
      !nvmlDeviceGetHandleByIndex_v2_ptr || !nvmlErrorString_ptr) {
    std::cout << "Failed to load critical NVML functions" << std::endl;
    unloadNvmlLibrary();
    return false;
  }

  return true;
}

void NvidiaMetricsCollector::unloadNvmlLibrary() {
  if (nvmlLibrary) {
    FreeLibrary(nvmlLibrary);
    nvmlLibrary = nullptr;
  }

  // Reset all function pointers
  nvmlInit_v2_ptr = nullptr;
  nvmlShutdown_ptr = nullptr;
  nvmlDeviceGetCount_v2_ptr = nullptr;
  nvmlDeviceGetHandleByIndex_v2_ptr = nullptr;
  nvmlDeviceGetTemperature_ptr = nullptr;
  nvmlDeviceGetUtilizationRates_ptr = nullptr;
  nvmlDeviceGetPowerUsage_ptr = nullptr;
  nvmlDeviceGetMemoryInfo_ptr = nullptr;
  nvmlDeviceGetFanSpeed_ptr = nullptr;
  nvmlDeviceGetClockInfo_ptr = nullptr;
  nvmlDeviceGetName_ptr = nullptr;
  nvmlDeviceGetPciInfo_ptr = nullptr;
  nvmlSystemGetDriverVersion_ptr = nullptr;
  nvmlDeviceGetCurrPcieLinkWidth_ptr = nullptr;
  nvmlDeviceGetCurrPcieLinkGeneration_ptr = nullptr;
  nvmlDeviceGetEncoderUtilization_ptr = nullptr;
  nvmlDeviceGetDecoderUtilization_ptr = nullptr;
  nvmlDeviceGetPcieThroughput_ptr = nullptr;
  nvmlDeviceGetComputeRunningProcesses_ptr = nullptr;
  nvmlDeviceGetProcessUtilization_ptr = nullptr;
  nvmlErrorString_ptr = nullptr;

  // Reset global function pointers
  g_nvmlShutdown_ptr = nullptr;
  g_nvmlDeviceGetUtilizationRates_ptr = nullptr;
  g_nvmlDeviceGetMemoryInfo_ptr = nullptr;
  g_nvmlDeviceGetComputeRunningProcesses_ptr = nullptr;
  g_nvmlDeviceGetProcessUtilization_ptr = nullptr;
}
#endif

bool NvidiaMetricsCollector::initializeNVML() {
#ifdef CAN_USE_NVML
  if (nvmlInitialized) return true;

  // Load the NVML library and function pointers
  if (!loadNvmlLibrary()) {
    std::cout
      << "NVIDIA monitoring disabled - driver not installed or compatible"
      << std::endl;
    emit collectionError(
      "NVIDIA monitoring disabled - driver not installed or compatible");
    return false;
  }

  // Use standard try/catch for the rest of the function
  try {
    nvmlReturn_t result = nvmlInit_v2_ptr();
    if (result != NVML_SUCCESS) {
      std::cout << "Failed to initialize NVML: " << nvmlErrorString_ptr(result) << std::endl;
      emit collectionError(QString("Failed to initialize NVML: %1")
                             .arg(nvmlErrorString_ptr(result)));
      unloadNvmlLibrary();
      return false;
    }

    // Get available GPU devices
    unsigned int deviceCount = 0;
    result = nvmlDeviceGetCount_v2_ptr(&deviceCount);
    if (result != NVML_SUCCESS) {
      std::cout << "Failed to get device count: " << nvmlErrorString_ptr(result) << std::endl;
      emit collectionError(QString("Failed to get device count: %1")
                             .arg(nvmlErrorString_ptr(result)));
      nvmlShutdown_ptr();
      unloadNvmlLibrary();
      return false;
    }

    if (deviceCount == 0) {
      std::cout << "No NVIDIA GPUs found" << std::endl;
      emit collectionError("No NVIDIA GPUs found");
      nvmlShutdown_ptr();
      unloadNvmlLibrary();
      return false;
    }

    std::cout << "Found " << deviceCount << " NVIDIA GPU(s)" << std::endl;

    // Get handles for all GPUs
    gpuHandles.clear();
    for (unsigned int i = 0; i < deviceCount; i++) {
      nvmlDevice_t device;
      if (nvmlDeviceGetHandleByIndex_v2_ptr(i, &device) == NVML_SUCCESS) {
        gpuHandles.push_back(device);
      }
    }

    nvmlInitialized = true;
    return true;
  } catch (...) {
    std::cout << "Exception occurred during NVML initialization" << std::endl;
    emit collectionError("Exception occurred during NVML initialization");
    if (nvmlLibrary) {
      unloadNvmlLibrary();
    }
    return false;
  }
#else
  std::cout << "NVML support not compiled in" << std::endl;
  emit collectionError("NVML support not compiled in");
  return false;
#endif
}

void NvidiaMetricsCollector::shutdownNVML() {
#ifdef CAN_USE_NVML
  if (nvmlInitialized && nvmlShutdown_ptr) {
    try {
      nvmlShutdown_ptr();
    } catch (...) {
      std::cout << "Exception occurred during NVML shutdown" << std::endl;
    }
    unloadNvmlLibrary();
    nvmlInitialized = false;
  }
#endif
}

bool NvidiaMetricsCollector::startCollecting(int updateIntervalMs) {
  if (running) return false;

  updateInterval = updateIntervalMs;

  // Create dummy metrics up front
  NvidiaGPUMetrics noDataMetrics;
  initializeNoDataMetrics(noDataMetrics);

  // Try to initialize NVML, but don't return early if it fails
  // We'll still emit metrics with -1 values
  bool nvmlSuccess = initializeNVML();

  // Always emit initial metrics, which will be valid if NVML initialized,
  // or contain -1 values if it didn't
  emit metricsUpdated(noDataMetrics);

  // Only start the collection thread if NVML initialized successfully
  if (nvmlSuccess) {
    running = true;
    collectorThread =
      std::thread(&NvidiaMetricsCollector::collectMetrics, this);
  }

  // Always return true as the function completed its task
  // (either starting collection or providing fallback metrics)
  return true;
}

void NvidiaMetricsCollector::stopCollecting() {
  running = false;
  if (collectorThread.joinable()) {
    collectorThread.join();
  }
}

// Initialize all metrics to proper "no data" values (always -1, never backup values)
void NvidiaMetricsCollector::initializeNoDataMetrics(
  NvidiaGPUMetrics& metrics) {
  metrics.temperature = -1;
  metrics.utilization = -1;
  metrics.memoryUtilization = -1;
  metrics.powerUsage = -1;
  metrics.fanSpeed = -1;
  metrics.clockSpeed = -1;
  metrics.memoryClock = -1;
  metrics.pciLinkWidth = -1;
  metrics.pcieLinkGen = -1;
  metrics.totalMemory = 0;
  metrics.usedMemory = 0;
  metrics.throttling = false;
  metrics.name = "No NVIDIA GPU";
  metrics.deviceId = "N/A";
  metrics.driverVersion = "N/A";

  // Initialize all advanced metrics to -1 (no backup/estimated values)
  metrics.encoderUtilization = -1;
  metrics.decoderUtilization = -1;
  metrics.computeUtilization = -1;
  metrics.graphicsEngineUtilization = -1;
  metrics.smUtilization = -1;
  metrics.memoryBandwidthUtilization = -1;
  metrics.pcieRxThroughput = -1;
  metrics.pcieTxThroughput = -1;
  metrics.nvdecUtilization = -1;
  metrics.nvencUtilization = -1;

  // Driver info - use "Unknown" for missing string data
  metrics.driverDate = "Unknown";
  metrics.hasGeForceExperience = false;
}

// Optimized collectMetrics - improved error handling and efficiency
void NvidiaMetricsCollector::collectMetrics() {
  // Create a single metrics instance to reuse
  NvidiaGPUMetrics metrics;
  int consecutiveFailures = 0;
  const int MAX_CONSECUTIVE_FAILURES = 5; // Increased tolerance
  auto lastRecoveryAttempt = std::chrono::steady_clock::now();
  const auto RECOVERY_COOLDOWN = std::chrono::seconds(5); // Faster recovery for benchmarks

  while (running) {
    bool metricsValid = false;
    
    // Fast path - try to collect metrics without exception handling first
    if (nvmlInitialized && !gpuHandles.empty()) {
      metricsValid = getBenchmarkGPUMetrics(gpuHandles[0], metrics);
    }

    if (metricsValid) {
      consecutiveFailures = 0;  // Reset failure counter on success
      emit metricsUpdated(metrics);
    } else {
      consecutiveFailures++;
      
      // Always emit -1 values for missing data (never backup values)
      initializeNoDataMetrics(metrics);
      emit metricsUpdated(metrics);

      // Only attempt recovery after many failures and cooldown period
      auto now = std::chrono::steady_clock::now();
      if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES && 
          (now - lastRecoveryAttempt) > RECOVERY_COOLDOWN) {
        
        std::cout << "GPU metrics collection unstable (" << consecutiveFailures 
                  << " failures), attempting recovery..." << std::endl;
        
        lastRecoveryAttempt = now;
        shutdownNVML();
        
        // Brief delay before reinitializing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (initializeNVML()) {
          std::cout << "GPU metrics recovery successful" << std::endl;
          consecutiveFailures = 0;
        } else {
          std::cout << "GPU metrics recovery failed, will retry later" << std::endl;
          // Don't reset consecutiveFailures - let it build up for longer recovery intervals
        }
      }
    }

    // Always maintain consistent timing
    std::this_thread::sleep_for(std::chrono::milliseconds(updateInterval));
  }
}

// Add optimized methods for specific metric collection
bool NvidiaMetricsCollector::getBasicUtilizationMetrics(nvmlDevice_t device,
                                                        unsigned int& gpuUtil,
                                                        unsigned int& memUtil) {
#ifdef NO_NVML_SUPPORT
  gpuUtil = -1;
  memUtil = -1;
  return false;
#else
  if (!nvmlInitialized && !initializeNVML()) {
    gpuUtil = -1;
    memUtil = -1;
    return false;
  }

  if (SafeGetBasicUtilizationMetrics(device, gpuUtil, memUtil)) {
    std::cout << "Basic GPU Utilization - GPU: " << gpuUtil
              << "%, Memory: " << memUtil << "%" << std::endl;
    return true;
  }

  gpuUtil = -1;
  memUtil = -1;
  return false;
#endif
}

// Replace getMemoryMetrics implementation to use the helper function
bool NvidiaMetricsCollector::getMemoryMetrics(nvmlDevice_t device,
                                              unsigned long long& totalMem,
                                              unsigned long long& usedMem) {
#ifdef NO_NVML_SUPPORT
  totalMem = 0;
  usedMem = 0;
  return false;
#else
  if (!nvmlInitialized && !initializeNVML()) {
    totalMem = 0;
    usedMem = 0;
    return false;
  }

  if (SafeGetMemoryMetrics(device, totalMem, usedMem)) {
    std::cout << "GPU Memory - Used: " << (usedMem / (1024 * 1024))
              << "MB, Total: " << (totalMem / (1024 * 1024)) << "MB ("
              << (usedMem * 100 / totalMem) << "%)" << std::endl;
    return true;
  }

  totalMem = 0;
  usedMem = 0;
  return false;
#endif
}

// Fix getPowerAndThermalMetrics function
bool NvidiaMetricsCollector::getPowerAndThermalMetrics(
  nvmlDevice_t device, unsigned int& temperature, float& powerUsage,
  unsigned int& fanSpeed) {
#ifdef NO_NVML_SUPPORT
  temperature = -1;
  powerUsage = -1;
  fanSpeed = -1;
  return false;
#else
  if (!nvmlInitialized && !initializeNVML()) {
    temperature = -1;
    powerUsage = -1;
    fanSpeed = -1;
    return false;
  }

  bool anySuccess = false;
  temperature = -1;
  powerUsage = -1;
  fanSpeed = -1;

  __try {
    // Temperature
    if (nvmlDeviceGetTemperature_ptr &&
        nvmlDeviceGetTemperature_ptr(device, NVML_TEMPERATURE_GPU,
                                     &temperature) == NVML_SUCCESS) {
      anySuccess = true;
    }

    // Power usage
    unsigned int powerMw = 0;
    if (nvmlDeviceGetPowerUsage_ptr &&
        nvmlDeviceGetPowerUsage_ptr(device, &powerMw) == NVML_SUCCESS) {
      powerUsage = powerMw / 1000.0f;  // Convert to watts as float
      anySuccess = true;
    }

    // Fan speed
    if (nvmlDeviceGetFanSpeed_ptr &&
        nvmlDeviceGetFanSpeed_ptr(device, &fanSpeed) == NVML_SUCCESS) {
      anySuccess = true;
    }

    if (anySuccess) {
      std::cout << "GPU Thermal metrics - Temp: " << temperature
                << "°C, Power: " << powerUsage << "W, Fan: " << fanSpeed << "%"
                << std::endl;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // Ignore exception and return default values
  }

  return anySuccess;
#endif
}

// Modified version of getGPUMetrics that handles GPM features conditionally
bool NvidiaMetricsCollector::getGPUMetrics(nvmlDevice_t device,
                                           NvidiaGPUMetrics& metrics) {
#ifdef NO_NVML_SUPPORT
  initializeNoDataMetrics(metrics);
  return false;
#else
  // Initialize all metrics to -1
  initializeNoDataMetrics(metrics);

  if (!nvmlInitialized) {
    return false;
  }

  bool anyMetricsCollected = false;
  nvmlReturn_t result;

  try {
    // Check if we need to initialize static info
    if (!staticInfoCache.count(device)) {
      GPUStaticInfo staticInfo;

      // Get GPU name
      char name[NVML_DEVICE_NAME_BUFFER_SIZE];
      if (nvmlDeviceGetName_ptr &&
          nvmlDeviceGetName_ptr(device, name, NVML_DEVICE_NAME_BUFFER_SIZE) ==
            NVML_SUCCESS) {
        staticInfo.name = name;
        anyMetricsCollected = true;
      }

      // Get PCI device ID
      nvmlPciInfo_t pciInfo;
      if (nvmlDeviceGetPciInfo_ptr &&
          nvmlDeviceGetPciInfo_ptr(device, &pciInfo) == NVML_SUCCESS) {
        char deviceIdStr[16];
        snprintf(deviceIdStr, sizeof(deviceIdStr), "%04X", pciInfo.device);
        staticInfo.deviceId = deviceIdStr;
        anyMetricsCollected = true;
      }

      // Get driver version (global, not per-device)
      if (!staticInfoInitialized && nvmlSystemGetDriverVersion_ptr) {
        char driverVersion[NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE];
        if (nvmlSystemGetDriverVersion_ptr(
              driverVersion, NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE) ==
            NVML_SUCCESS) {
          staticInfo.driverVersion = driverVersion;
          staticInfoInitialized = true;
          anyMetricsCollected = true;
        }
      }

      // Get PCI link information (semi-static)
      unsigned int currLinkWidth = 0;
      if (nvmlDeviceGetCurrPcieLinkWidth_ptr &&
          nvmlDeviceGetCurrPcieLinkWidth_ptr(device, &currLinkWidth) ==
            NVML_SUCCESS) {
        staticInfo.pciLinkWidth = currLinkWidth;
        anyMetricsCollected = true;
      }

      unsigned int currLinkGen = 0;
      if (nvmlDeviceGetCurrPcieLinkGeneration_ptr &&
          nvmlDeviceGetCurrPcieLinkGeneration_ptr(device, &currLinkGen) ==
            NVML_SUCCESS) {
        staticInfo.pcieLinkGen = currLinkGen;
        anyMetricsCollected = true;
      }

      // Store in cache
      staticInfoCache[device] = staticInfo;

      if (anyMetricsCollected) {
        std::cout << "GPU Static Info - Name: " << staticInfo.name
                  << ", Device ID: " << staticInfo.deviceId
                  << ", Driver: " << staticInfo.driverVersion << ", PCIe: Gen"
                  << staticInfo.pcieLinkGen << " x" << staticInfo.pciLinkWidth
                  << std::endl;
      }
    }

    // Apply cached static info if available
    if (staticInfoCache.count(device)) {
      metrics.name = staticInfoCache[device].name;
      metrics.deviceId = staticInfoCache[device].deviceId;
      metrics.driverVersion = staticInfoCache[device].driverVersion;
      metrics.pciLinkWidth = staticInfoCache[device].pciLinkWidth;
      metrics.pcieLinkGen = staticInfoCache[device].pcieLinkGen;
      anyMetricsCollected = true;
    }

    // Get driver date and GeForce Experience status
    if (!staticInfoCache[device].driverDateChecked) {
      std::string driverDate;
      bool hasGeForceExperience;
      if (getNvidiaDriverInfo(driverDate, hasGeForceExperience)) {
        staticInfoCache[device].driverDate = driverDate;
        staticInfoCache[device].hasGeForceExperience = hasGeForceExperience;
      }
      staticInfoCache[device].driverDateChecked = true;
    }

    // Apply driver date info to metrics
    metrics.driverDate = staticInfoCache[device].driverDate;
    metrics.hasGeForceExperience = staticInfoCache[device].hasGeForceExperience;

    // Get dynamic metrics

    // Temperature
    if (nvmlDeviceGetTemperature_ptr &&
        nvmlDeviceGetTemperature_ptr(device, NVML_TEMPERATURE_GPU,
                                     &metrics.temperature) == NVML_SUCCESS) {
      anyMetricsCollected = true;
    }

    // Basic Utilization
    nvmlUtilization_t utilization = {};
    if (nvmlDeviceGetUtilizationRates_ptr) {
      result = nvmlDeviceGetUtilizationRates_ptr(device, &utilization);
      if (result == NVML_SUCCESS) {
        metrics.utilization = utilization.gpu;
        metrics.memoryUtilization = utilization.memory;
        // Use raw GPU utilization value for SM utilization
        metrics.smUtilization = utilization.gpu;
        anyMetricsCollected = true;
      }
    }

    // Power usage
    unsigned int powerMw = 0;
    if (nvmlDeviceGetPowerUsage_ptr) {
      result = nvmlDeviceGetPowerUsage_ptr(device, &powerMw);
      if (result == NVML_SUCCESS) {
        metrics.powerUsage = powerMw / 1000.0f;
        anyMetricsCollected = true;
      }
    }

    // Memory info
    nvmlMemory_t memory = {};
    if (nvmlDeviceGetMemoryInfo_ptr) {
      result = nvmlDeviceGetMemoryInfo_ptr(device, &memory);
      if (result == NVML_SUCCESS) {
        metrics.totalMemory = memory.total;
        metrics.usedMemory = memory.used;
        anyMetricsCollected = true;
      }
    }

    // Fan speed
    if (nvmlDeviceGetFanSpeed_ptr &&
        nvmlDeviceGetFanSpeed_ptr(device, &metrics.fanSpeed) == NVML_SUCCESS) {
      anyMetricsCollected = true;
    }

    // Clock speeds
    if (nvmlDeviceGetClockInfo_ptr) {
      if (nvmlDeviceGetClockInfo_ptr(device, NVML_CLOCK_GRAPHICS,
                                     &metrics.clockSpeed) == NVML_SUCCESS) {
        anyMetricsCollected = true;
      }

      if (nvmlDeviceGetClockInfo_ptr(device, NVML_CLOCK_MEM,
                                     &metrics.memoryClock) == NVML_SUCCESS) {
        anyMetricsCollected = true;
      }
    }

    // Get encoder utilization
    unsigned int encoderValue = 0;
    unsigned int samplingPeriod = 0;
    if (nvmlDeviceGetEncoderUtilization_ptr) {
      result = nvmlDeviceGetEncoderUtilization_ptr(device, &encoderValue,
                                                   &samplingPeriod);
      if (result == NVML_SUCCESS) {
        metrics.encoderUtilization = encoderValue;
        metrics.nvencUtilization =
          encoderValue;  // Duplicate for new field name
        anyMetricsCollected = true;
      }
    }

    // Get decoder utilization
    unsigned int decoderValue = 0;
    if (nvmlDeviceGetDecoderUtilization_ptr) {
      result = nvmlDeviceGetDecoderUtilization_ptr(device, &decoderValue,
                                                   &samplingPeriod);
      if (result == NVML_SUCCESS) {
        metrics.decoderUtilization = decoderValue;
        metrics.nvdecUtilization =
          decoderValue;  // Duplicate for new field name
        anyMetricsCollected = true;
      }
    }

    // Try to get detailed performance metrics if available (requires newer NVML
    // versions) Only attempt this if defined in NVML headers
#ifdef NVML_HAS_GPM_SUPPORT
    // Check if GPU Performance Monitoring is supported on this device
    nvmlGpmMetricsSupport_t gpmSupport;
    if (nvmlDeviceGetGpmMetricsSupport &&
        nvmlDeviceGetGpmMetricsSupport(device, &gpmSupport) == NVML_SUCCESS &&
        gpmSupport.isSupported) {

      // Graphics Engine Activity (SM Utilization)
      nvmlGpmMetricGet_t graphicsUtilMetric;
      graphicsUtilMetric.metricId = NVML_GPM_METRIC_GRAPHICS_UTIL;
      graphicsUtilMetric.gpuInstanceId = 0xffffffff;
      graphicsUtilMetric.subProcessorIndex = 0xffffffff;

      nvmlGpmMetricValue_t graphicsUtilValue;

      if (nvmlDeviceGetGpmMetrics(device, &graphicsUtilMetric, 1,
                                  &graphicsUtilValue) == NVML_SUCCESS) {
        metrics.graphicsEngineUtilization =
          static_cast<unsigned int>(graphicsUtilValue.value.dVal);
        anyMetricsCollected = true;
      }

      // SM Utilization and other GPM metrics would be handled similarly
      // ...
    }
#else
    // DO NOT create backup/estimated values - missing data should always be -1
    // computeUtilization and other advanced metrics remain -1 if not available
#endif

    if (anyMetricsCollected) {
      std::cout << "GPU Metrics - GPU: " << metrics.utilization
                << "%, Memory: " << metrics.memoryUtilization
                << "%, Compute: " << metrics.computeUtilization
                << "%, Temp: " << metrics.temperature
                << "°C, Core: " << metrics.clockSpeed
                << "MHz, Memory: " << metrics.memoryClock
                << "MHz, VRAM: " << (metrics.usedMemory / (1024 * 1024)) << "/"
                << (metrics.totalMemory / (1024 * 1024)) << "MB" << std::endl;
    }
  } catch (...) {
    std::cout << "Exception during metrics collection" << std::endl;
    initializeNoDataMetrics(metrics);
    return false;
  }

  return anyMetricsCollected;
#endif
}

bool NvidiaMetricsCollector::getMetricsForDevice(nvmlDevice_t device,
                                                 NvidiaGPUMetrics& metrics) {
  // Always initialize metrics with -1 values first
  initializeNoDataMetrics(metrics);

  if (!nvmlInitialized) {
    if (!initializeNVML()) {
      return false;
    }
  }

  return getGPUMetrics(device, metrics);
}

std::vector<nvmlDevice_t> NvidiaMetricsCollector::getAvailableGPUs() {
  if (!nvmlInitialized) {
    if (!initializeNVML()) {
      return {};
    }
  }
  return gpuHandles;
}

// Add this function to bridge between member function and free function
bool GetGPUMetricsStatic(nvmlDevice_t device, NvidiaGPUMetrics& metrics) {
#ifndef NO_NVML_SUPPORT
  return NvidiaMetricsCollector::getGPUMetricsStatic(device, metrics);
#else
  return false;
#endif
}

// Add the static implementation
bool NvidiaMetricsCollector::getGPUMetricsStatic(nvmlDevice_t device,
                                                 NvidiaGPUMetrics& metrics) {
  // Same implementation as getGPUMetrics but static
  // This is a temporary bridge to work around the member function pointer issue
  NvidiaMetricsCollector collector;
  return collector.getGPUMetrics(device, metrics);
}

// Fix the getDetailedMetricsForDevice function to use the static bridge
bool NvidiaMetricsCollector::getDetailedMetricsForDevice(
  nvmlDevice_t device, NvidiaGPUMetrics& metrics,
  std::vector<NvidiaProcessGPUMetrics>& processMetrics) {
#ifdef NO_NVML_SUPPORT
  initializeNoDataMetrics(metrics);
  processMetrics.clear();
  return false;
#else
  // Always initialize with no data first
  initializeNoDataMetrics(metrics);
  processMetrics.clear();

  if (!nvmlInitialized && !initializeNVML()) {
    return false;
  }

  bool success = SafeGetDetailedDeviceMetrics(device, metrics, processMetrics,
                                              &GetGPUMetricsStatic);

  if (!success) {
    initializeNoDataMetrics(metrics);
    processMetrics.clear();
  }

  return success;
#endif
}

// Add these optimized methods for targeted metric collection
bool NvidiaMetricsCollector::getClockMetrics(nvmlDevice_t device,
                                             unsigned int& coreClock,
                                             unsigned int& memoryClock) {
#ifdef NO_NVML_SUPPORT
  coreClock = -1;
  memoryClock = -1;
  return false;
#else
  if (!nvmlInitialized && !initializeNVML()) {
    coreClock = -1;
    memoryClock = -1;
    return false;
  }

  if (!nvmlDeviceGetClockInfo_ptr) {
    coreClock = -1;
    memoryClock = -1;
    return false;
  }

  coreClock = -1;
  memoryClock = -1;
  bool anySuccess = false;

  __try {
    if (nvmlDeviceGetClockInfo_ptr(device, NVML_CLOCK_GRAPHICS, &coreClock) ==
        NVML_SUCCESS) {
      anySuccess = true;
    }

    if (nvmlDeviceGetClockInfo_ptr(device, NVML_CLOCK_MEM, &memoryClock) ==
        NVML_SUCCESS) {
      anySuccess = true;
    }

    if (anySuccess) {
      std::cout << "GPU Clock Speeds - Core: " << coreClock
                << "MHz, Memory: " << memoryClock << "MHz" << std::endl;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    coreClock = -1;
    memoryClock = -1;
    return false;
  }

  return anySuccess;
#endif
}

// Fix getEncoderDecoderMetrics function
bool NvidiaMetricsCollector::getEncoderDecoderMetrics(
  nvmlDevice_t device, unsigned int& encoderUtil, unsigned int& decoderUtil) {
#ifdef NO_NVML_SUPPORT
  encoderUtil = -1;
  decoderUtil = -1;
  return false;
#else
  if (!nvmlInitialized && !initializeNVML()) {
    encoderUtil = -1;
    decoderUtil = -1;
    return false;
  }

  if (!nvmlDeviceGetEncoderUtilization_ptr ||
      !nvmlDeviceGetDecoderUtilization_ptr) {
    encoderUtil = -1;
    decoderUtil = -1;
    return false;
  }

  encoderUtil = -1;
  decoderUtil = -1;
  bool anySuccess = false;
  unsigned int samplingPeriod = 0;

  __try {
    if (nvmlDeviceGetEncoderUtilization_ptr(device, &encoderUtil,
                                            &samplingPeriod) == NVML_SUCCESS) {
      anySuccess = true;
    }

    if (nvmlDeviceGetDecoderUtilization_ptr(device, &decoderUtil,
                                            &samplingPeriod) == NVML_SUCCESS) {
      anySuccess = true;
    }

    if (anySuccess) {
      std::cout << "GPU Video Engine - NVENC: " << encoderUtil
                << "%, NVDEC: " << decoderUtil << "%" << std::endl;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    encoderUtil = -1;
    decoderUtil = -1;
    return false;
  }

  return anySuccess;
#endif
}

// Add this function implementation after getGPUMetrics

bool NvidiaMetricsCollector::getBenchmarkGPUMetrics(nvmlDevice_t device,
                                                    NvidiaGPUMetrics& metrics) {
#ifdef CAN_USE_NVML
  // Initialize all metrics to -1 (no backup values, missing data = -1)
  initializeNoDataMetrics(metrics);

  // Caller should ensure NVML is initialized - don't try to initialize here
  // as it can cause delays in the collection loop
  if (!nvmlInitialized) {
    return false;
  }

  bool anyMetricsCollected = false;

  __try {
    // Apply static info if available (collected once)
    if (staticInfoCache.count(device)) {
      metrics.name = staticInfoCache[device].name;
      metrics.deviceId = staticInfoCache[device].deviceId;
      metrics.driverVersion = staticInfoCache[device].driverVersion;
      metrics.pciLinkWidth = staticInfoCache[device].pciLinkWidth;
      metrics.pcieLinkGen = staticInfoCache[device].pcieLinkGen;
      metrics.driverDate = staticInfoCache[device].driverDate;
      metrics.hasGeForceExperience = staticInfoCache[device].hasGeForceExperience;
      anyMetricsCollected = true;
    } else {
      // If we don't have static info yet, trigger collection in getGPUMetrics
      std::cout << "[DEBUG] No static cache found, calling getGPUMetrics to populate it" << std::endl;
      if (getGPUMetrics(device, metrics)) {
        anyMetricsCollected = true;
        // Don't return early, continue with high frequency collection
      }
    }

    // Collect high-frequency metrics (every second)
    if (collectHighFrequencyMetrics(device, metrics)) {
      anyMetricsCollected = true;
    }

    // Check if medium-frequency metrics need updating (every 3 seconds)
    if (shouldUpdateMediumFrequency(device)) {
      if (collectMediumFrequencyMetrics(device)) {
        mediumFreqCache[device].lastUpdate = std::chrono::steady_clock::now();
      }
    }

    // Apply cached medium-frequency metrics to current metrics
    applyMediumFrequencyCache(device, metrics);

    // Get total memory from static cache or collect once
    if (staticInfoCache.count(device) && staticInfoCache[device].name != "No NVIDIA GPU") {
      // Try to get total memory if not cached
      if (metrics.totalMemory == 0 && nvmlDeviceGetMemoryInfo_ptr) {
        nvmlMemory_t memory = {};
        if (nvmlDeviceGetMemoryInfo_ptr(device, &memory) == NVML_SUCCESS) {
          metrics.totalMemory = memory.total;
        }
      }
    }

  } __except (EXCEPTION_EXECUTE_HANDLER) {
    std::cout << "Exception during optimized benchmark metrics collection" << std::endl;
    // Re-initialize to ensure all values are -1 on exception
    initializeNoDataMetrics(metrics);
    return false;
  }

  return anyMetricsCollected;
#else
  initializeNoDataMetrics(metrics);
  return false;
#endif
}

bool NvidiaMetricsCollector::getGpuProcessUtilization(
  nvmlDevice_t device, std::vector<NvidiaProcessGPUMetrics>& processMetrics) {
#ifdef CAN_USE_NVML
  if (!nvmlInitialized && !initializeNVML()) {
    return false;
  }

  // Check if we have the required function pointers
  if (!nvmlDeviceGetComputeRunningProcesses_ptr ||
      !nvmlDeviceGetProcessUtilization_ptr) {
    return false;
  }

  processMetrics.clear();
  bool success = false;

  try {
    // First get a list of processes using this GPU
    unsigned int procCount = 0;

    // First call to get the count
    nvmlReturn_t result =
      nvmlDeviceGetComputeRunningProcesses_ptr(device, &procCount, nullptr);
    if (result != NVML_SUCCESS && result != NVML_ERROR_INSUFFICIENT_SIZE) {
      std::cout << "Failed to get GPU process count: "
                << nvmlErrorString_ptr(result) << std::endl;
      return false;
    }

    if (procCount == 0) {
      // No processes are using the GPU
      return true;  // Success but empty list
    }

    // Allocate memory for the process info
    std::vector<nvmlProcessInfo_t> procInfos(procCount);

    // Second call to get the actual data
    result = nvmlDeviceGetComputeRunningProcesses_ptr(device, &procCount,
                                                      procInfos.data());

    if (result != NVML_SUCCESS) {
      std::cout << "Failed to get GPU process list: "
                << nvmlErrorString_ptr(result) << std::endl;
      return false;
    }

    // Get timestamp for samples (milliseconds since 1970)
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();

    unsigned long long startTime = timestamp - 1000;  // 1 second ago

    // Get process utilization samples
    unsigned int sampleCount = 0;

    // First call to get the count
    result = nvmlDeviceGetProcessUtilization_ptr(device, nullptr, &sampleCount,
                                                 startTime);
    if (result != NVML_SUCCESS && result != NVML_ERROR_INSUFFICIENT_SIZE) {
      std::cout << "Failed to get GPU process utilization count: "
                << nvmlErrorString_ptr(result) << std::endl;
      return false;
    }

    if (sampleCount == 0) {
      // No samples available
      return true;
    }

    // Allocate memory for the samples
    std::vector<nvmlProcessUtilizationSample_t> samples(sampleCount);

    // Second call to get the actual data
    result = nvmlDeviceGetProcessUtilization_ptr(device, samples.data(),
                                                 &sampleCount, startTime);

    if (result != NVML_SUCCESS) {
      std::cout << "Failed to get GPU process utilization: "
                << nvmlErrorString_ptr(result) << std::endl;
      return false;
    }

    // Create a map to match PIDs with process info
    std::map<unsigned int, nvmlProcessInfo_t> pidToInfo;
    for (unsigned int i = 0; i < procCount; i++) {
      pidToInfo[procInfos[i].pid] = procInfos[i];
    }

    // Process the utilization samples
    for (unsigned int i = 0; i < sampleCount; i++) {
      const auto& sample = samples[i];

      // Only process if this PID matches one of our running processes
      if (pidToInfo.find(sample.pid) != pidToInfo.end()) {
        NvidiaProcessGPUMetrics procMetric;
        procMetric.pid = sample.pid;

        // Try to get process name from Windows API
        HANDLE hProcess = nullptr;
        try {
          hProcess =
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, sample.pid);
          if (hProcess) {
            char processName[MAX_PATH] = {0};
            DWORD size = sizeof(processName);
            if (QueryFullProcessImageNameA(hProcess, 0, processName, &size)) {
              // Extract just the filename part
              const char* lastSlash = strrchr(processName, '\\');
              if (lastSlash) {
                procMetric.name = lastSlash + 1;
              } else {
                procMetric.name = processName;
              }
            }
          }
        } catch (...) {
          // Ignore any exceptions from process querying
        }

        if (hProcess) {
          CloseHandle(hProcess);
        }

        // If we couldn't get the name, use the PID as a string
        if (procMetric.name.empty()) {
          procMetric.name = "PID_" + std::to_string(sample.pid);
        }

        // Get the GPU utilization metrics
        procMetric.gpuUtilization = sample.smUtil;  // SM utilization
        procMetric.memoryUtilization =
          sample.memUtil;  // Memory controller utilization
        procMetric.encoderUtilization = sample.encUtil;  // Encoder utilization
        procMetric.decoderUtilization = sample.decUtil;  // Decoder utilization

        // Calculate compute utilization (SM - encoder - decoder)
        // Ensuring we don't go negative
        if (sample.smUtil > (sample.encUtil + sample.decUtil)) {
          procMetric.computeUtilization =
            sample.smUtil - sample.encUtil - sample.decUtil;
        } else {
          procMetric.computeUtilization = 0;
        }

        // Get memory used
        if (pidToInfo.find(sample.pid) != pidToInfo.end()) {
          procMetric.memoryUsed = pidToInfo[sample.pid].usedGpuMemory;
        }

        processMetrics.push_back(procMetric);
      }
    }

    success = true;

#if defined(NVML_HAS_GPM_SUPPORT) && defined(NVML_PROCESS_DETAIL_SUPPORTED)
    // Try to get additional process detail if available in newer versions
    // This requires NVML 12.0+ and the corresponding header definitions
    if (nvmlDeviceGetProcessDetailList) {
      nvmlProcessDetailList_v1_t procDetailList;
      procDetailList.version = nvmlProcessDetailList_v1;
      procDetailList.mode = NVML_PROCESS_DETAIL_MODE_ALL;
      procDetailList.numProcArrayEntries = 0;
      procDetailList.procArray = nullptr;

      // First call to get the count
      result = nvmlDeviceGetProcessDetailList(device, &procDetailList);

      if (result == NVML_SUCCESS && procDetailList.numProcArrayEntries > 0) {
        // Allocate memory for process details
        std::vector<nvmlProcessDetail_v1_t> procDetails(
          procDetailList.numProcArrayEntries);
        procDetailList.procArray = procDetails.data();

        // Second call to get the actual data
        result = nvmlDeviceGetProcessDetailList(device, &procDetailList);

        if (result == NVML_SUCCESS) {
          // Map existing process metrics by PID for updating
          std::map<unsigned int, size_t> pidToIndex;
          for (size_t i = 0; i < processMetrics.size(); i++) {
            pidToIndex[processMetrics[i].pid] = i;
          }

          // Update or add process metrics
          for (unsigned int i = 0; i < procDetailList.numProcArrayEntries;
               i++) {
            const auto& detail = procDetails[i];
            unsigned int pid = detail.pid;

            // Check if we already have metrics for this PID
            if (pidToIndex.find(pid) != pidToIndex.end()) {
              // Update existing metrics with more detailed info
              auto& procMetric = processMetrics[pidToIndex[pid]];
              // Add any additional metrics here
            } else {
              // Add new process metrics
              NvidiaProcessGPUMetrics procMetric;
              procMetric.pid = pid;

              // Fill in available metrics
              // Additional details would be populated here

              processMetrics.push_back(procMetric);
            }
          }
        }
      }
    }
#endif

    std::cout << "Retrieved GPU process metrics for " << processMetrics.size()
              << " processes" << std::endl;
  } catch (const std::exception& e) {
    std::cout << "Exception during GPU process metrics collection: " << e.what()
              << std::endl;
    return false;
  } catch (...) {
    std::cout << "Unknown exception during GPU process metrics collection"
              << std::endl;
    return false;
  }

  return success;
#else
  processMetrics.clear();
  return false;
#endif
}

// Add this method to check NVIDIA driver date and GeForce Experience
bool NvidiaMetricsCollector::getNvidiaDriverInfo(std::string& driverDate,
                                                 bool& hasGeForceExperience) {
  driverDate = "Unknown";
  hasGeForceExperience = false;

  // Check for driver date in registry
  HKEY hKey;
  bool foundDate = false;
  const char* regPaths[] = {
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"};

  for (const char* basePath : regPaths) {
    if (foundDate) break;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, basePath, 0, KEY_READ, &hKey) ==
        ERROR_SUCCESS) {
      DWORD index = 0;
      char subKeyName[256];
      DWORD subKeySize = sizeof(subKeyName);

      while (RegEnumKeyExA(hKey, index++, subKeyName, &subKeySize, NULL, NULL,
                           NULL, NULL) == ERROR_SUCCESS) {
        std::string fullSubKey = std::string(basePath) + "\\" + subKeyName;
        HKEY subKey;

        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), 0, KEY_READ,
                          &subKey) == ERROR_SUCCESS) {
          char displayName[512] = {0};
          DWORD nameSize = sizeof(displayName);

          if (RegQueryValueExA(subKey, "DisplayName", NULL, NULL,
                               (LPBYTE)displayName,
                               &nameSize) == ERROR_SUCCESS) {
            std::string name = displayName;
            bool isNvidiaDriver =
              name.find("NVIDIA Graphics Driver") != std::string::npos;

            if (isNvidiaDriver) {
              char installDate[20] = {0};
              DWORD dateSize = sizeof(installDate);

              if (RegQueryValueExA(subKey, "InstallDate", NULL, NULL,
                                   (LPBYTE)installDate,
                                   &dateSize) == ERROR_SUCCESS) {
                std::string dateStr = installDate;
                if (dateStr.length() == 8) {
                  // Format from YYYYMMDD to MM/DD/YYYY
                  std::string year = dateStr.substr(0, 4);
                  std::string month = dateStr.substr(4, 2);
                  std::string day = dateStr.substr(6, 2);
                  driverDate = month + "/" + day + "/" + year;
                  foundDate = true;
                  std::cout
                    << "  Found NVIDIA driver install date: " << driverDate
                    << std::endl;
                  break;
                }
              }
            }
          }
          RegCloseKey(subKey);
        }
        subKeySize = sizeof(subKeyName);
      }
      RegCloseKey(hKey);
    }
  }

  // Check for GeForce Experience
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "SOFTWARE\\NVIDIA Corporation\\Global\\GFExperience", 0,
                    KEY_READ, &hKey) == ERROR_SUCCESS) {
    hasGeForceExperience = true;
    RegCloseKey(hKey);
    std::cout << "  GeForce Experience is installed" << std::endl;
  } else if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                           "SOFTWARE\\WOW6432Node\\NVIDIA "
                           "Corporation\\Global\\GFExperience",
                           0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    hasGeForceExperience = true;
    RegCloseKey(hKey);
    std::cout << "  GeForce Experience is installed" << std::endl;
  }

  return foundDate || hasGeForceExperience;
}

// Helper method for collecting high-frequency metrics (every 1 second)
bool NvidiaMetricsCollector::collectHighFrequencyMetrics(nvmlDevice_t device, NvidiaGPUMetrics& metrics) {
#ifdef CAN_USE_NVML
  bool anyMetricsCollected = false;
  nvmlReturn_t result;

  // High-frequency metrics: utilization, clocks, memory usage, encoder/decoder
  
  // Basic Utilization - core GPU metrics (most important)
  if (nvmlDeviceGetUtilizationRates_ptr) {
    nvmlUtilization_t utilization = {};
    result = nvmlDeviceGetUtilizationRates_ptr(device, &utilization);
    if (result == NVML_SUCCESS) {
      metrics.utilization = utilization.gpu;
      metrics.memoryUtilization = utilization.memory;
      anyMetricsCollected = true;
    }
  }

  // Clock speeds - important for performance analysis
  if (nvmlDeviceGetClockInfo_ptr) {
    unsigned int tempClockSpeed = 0;
    nvmlReturn_t graphicsResult = nvmlDeviceGetClockInfo_ptr(device, NVML_CLOCK_GRAPHICS, &tempClockSpeed);
    if (graphicsResult == NVML_SUCCESS) {
      metrics.clockSpeed = tempClockSpeed;
      anyMetricsCollected = true;
      //std::cout << "[CLOCK-DEBUG] GPU Clock: " << tempClockSpeed << "MHz" << std::endl;
    } else {
      metrics.clockSpeed = -1;
      std::cout << "GPU graphics clock query failed with error: " << nvmlErrorString(graphicsResult) << " (" << graphicsResult << ")" << std::endl;
    }

    unsigned int tempMemoryClock = 0;
    nvmlReturn_t memoryResult = nvmlDeviceGetClockInfo_ptr(device, NVML_CLOCK_MEM, &tempMemoryClock);
    if (memoryResult == NVML_SUCCESS) {
      metrics.memoryClock = tempMemoryClock;
      anyMetricsCollected = true;
      //std::cout << "[CLOCK-DEBUG] GPU Memory Clock: " << tempMemoryClock << "MHz" << std::endl;
    } else {
      metrics.memoryClock = -1;
      std::cout << "GPU memory clock query failed with error: " << nvmlErrorString(memoryResult) << " (" << memoryResult << ")" << std::endl;
    }
  } else {
    metrics.clockSpeed = -1;
    metrics.memoryClock = -1;
    std::cout << "[CLOCK-DEBUG] nvmlDeviceGetClockInfo_ptr is NULL" << std::endl;
  }

  // Memory usage (not total, just used - changes frequently)
  if (nvmlDeviceGetMemoryInfo_ptr) {
    nvmlMemory_t memory = {};
    result = nvmlDeviceGetMemoryInfo_ptr(device, &memory);
    if (result == NVML_SUCCESS) {
      metrics.usedMemory = memory.used;
      // Only set total memory if we don't have it cached yet
      if (metrics.totalMemory == 0) {
        metrics.totalMemory = memory.total;
      }
      anyMetricsCollected = true;
    }
  }

  // Encoder utilization - important for streaming/recording detection
  if (nvmlDeviceGetEncoderUtilization_ptr) {
    unsigned int encoderValue = 0;
    unsigned int samplingPeriod = 0;
    result = nvmlDeviceGetEncoderUtilization_ptr(device, &encoderValue,
                                                 &samplingPeriod);
    if (result == NVML_SUCCESS) {
      metrics.encoderUtilization = encoderValue;
      metrics.nvencUtilization = encoderValue;    // Both fields should be populated
      anyMetricsCollected = true;
    } else {
      // Explicitly set to -1 on failure for clear debugging
      metrics.encoderUtilization = -1;
      metrics.nvencUtilization = -1;
      if (result != NVML_ERROR_NOT_SUPPORTED) {
        std::cout << "NVENC utilization query failed: " << result << std::endl;
      }
    }
  } else {
    metrics.encoderUtilization = -1;
    metrics.nvencUtilization = -1;
  }

  // Decoder utilization
  if (nvmlDeviceGetDecoderUtilization_ptr) {
    unsigned int decoderValue = 0;
    unsigned int samplingPeriod = 0;
    result = nvmlDeviceGetDecoderUtilization_ptr(device, &decoderValue,
                                                 &samplingPeriod);
    if (result == NVML_SUCCESS) {
      metrics.decoderUtilization = decoderValue;
      metrics.nvdecUtilization = decoderValue;    // Both fields should be populated
      anyMetricsCollected = true;
    } else {
      // Explicitly set to -1 on failure for clear debugging
      metrics.decoderUtilization = -1;
      metrics.nvdecUtilization = -1;
      if (result != NVML_ERROR_NOT_SUPPORTED) {
        std::cout << "NVDEC utilization query failed: " << result << std::endl;
      }
    }
  } else {
    metrics.decoderUtilization = -1;
    metrics.nvdecUtilization = -1;
  }
  
  // Fan speed - move to high frequency to ensure it's always collected
  if (nvmlDeviceGetFanSpeed_ptr) {
    unsigned int fanSpeed = 0;
    nvmlReturn_t fanResult = nvmlDeviceGetFanSpeed_ptr(device, &fanSpeed);
    if (fanResult == NVML_SUCCESS) {
      metrics.fanSpeed = fanSpeed;
      anyMetricsCollected = true;
      //std::cout << "[FAN-DEBUG] GPU Fan Speed: " << fanSpeed << "%" << std::endl;
    } else {
      metrics.fanSpeed = -1; // Set to -1 on failure
      std::cout << "GPU fan speed query failed with error: " << nvmlErrorString(fanResult) << " (" << fanResult << ")" << std::endl;
    }
  } else {
    metrics.fanSpeed = -1;
    std::cout << "[FAN-DEBUG] nvmlDeviceGetFanSpeed_ptr is NULL" << std::endl;
  }

  // Debug output to track which metrics are successfully collected
  if (anyMetricsCollected) {
    static std::chrono::steady_clock::time_point lastDebugOutput;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastDebugOutput);
    
    // Output debug info every 10 seconds to avoid spam
    if (elapsed.count() >= 10) {
      std::cout << "GPU High-Freq Metrics Status - "
                << "Util: " << metrics.utilization << ", "
                << "Clock: " << metrics.clockSpeed << "MHz, "
                << "MemClock: " << metrics.memoryClock << "MHz, "
                << "Fan: " << metrics.fanSpeed << "%, "
                << "NVENC: " << metrics.nvencUtilization << "%, "
                << "NVDEC: " << metrics.nvdecUtilization << "%"
                << std::endl;
      lastDebugOutput = now;
    }
  }

  return anyMetricsCollected;
#else
  return false;
#endif
}

// Helper method for collecting medium-frequency metrics (every 3 seconds)
bool NvidiaMetricsCollector::collectMediumFrequencyMetrics(nvmlDevice_t device) {
#ifdef CAN_USE_NVML
  if (!nvmlInitialized) {
    return false;
  }

  bool anyMetricsCollected = false;
  nvmlReturn_t result;
  GPUMediumFreqMetrics& mediumMetrics = mediumFreqCache[device];

  // Medium-frequency metrics: temperature, power, fan, throttling, etc.

  // Temperature - changes slowly
  if (nvmlDeviceGetTemperature_ptr) {
    if (nvmlDeviceGetTemperature_ptr(device, NVML_TEMPERATURE_GPU,
                                     &mediumMetrics.temperature) == NVML_SUCCESS) {
      anyMetricsCollected = true;
    }
  }

  // Power usage - changes slowly
  if (nvmlDeviceGetPowerUsage_ptr) {
    unsigned int powerMw = 0;
    result = nvmlDeviceGetPowerUsage_ptr(device, &powerMw);
    if (result == NVML_SUCCESS) {
      mediumMetrics.powerUsage = powerMw / 1000.0f;
      anyMetricsCollected = true;
    }
  }

  // Fan speed - moved to high frequency collection for better availability

  // SM utilization - try to get more accurate SM utilization if available
  // For now, still use GPU utilization but make it clear this is an approximation
  if (nvmlDeviceGetUtilizationRates_ptr) {
    nvmlUtilization_t utilization = {};
    result = nvmlDeviceGetUtilizationRates_ptr(device, &utilization);
    if (result == NVML_SUCCESS) {
      mediumMetrics.smUtilization = utilization.gpu; // Basic approximation - true SM util requires GPM
      anyMetricsCollected = true;
    }
  }

  // PCIe throughput metrics - improved error handling and debug
  if (nvmlDeviceGetPcieThroughput_ptr) {
    unsigned int rxBytes = 0, txBytes = 0;
    
    // Try to get RX throughput
    nvmlReturn_t rxResult = nvmlDeviceGetPcieThroughput_ptr(device, NVML_PCIE_UTIL_RX_BYTES, &rxBytes);
    if (rxResult == NVML_SUCCESS) {
      mediumMetrics.pcieRxThroughput = rxBytes;
      anyMetricsCollected = true;
    } else {
      mediumMetrics.pcieRxThroughput = -1; // Explicitly set to -1 on failure
      if (rxResult != NVML_ERROR_NOT_SUPPORTED) {
        std::cout << "PCIe RX throughput query failed: " << rxResult << std::endl;
      }
    }

    // Try to get TX throughput
    nvmlReturn_t txResult = nvmlDeviceGetPcieThroughput_ptr(device, NVML_PCIE_UTIL_TX_BYTES, &txBytes);
    if (txResult == NVML_SUCCESS) {
      mediumMetrics.pcieTxThroughput = txBytes;
      anyMetricsCollected = true;
    } else {
      mediumMetrics.pcieTxThroughput = -1; // Explicitly set to -1 on failure
      if (txResult != NVML_ERROR_NOT_SUPPORTED) {
        std::cout << "PCIe TX throughput query failed: " << txResult << std::endl;
      }
    }
  }

  // Memory bandwidth utilization - use memory utilization as approximation
  // True memory bandwidth utilization requires advanced NVML/GPM features
  if (nvmlDeviceGetUtilizationRates_ptr) {
    nvmlUtilization_t utilization = {};
    result = nvmlDeviceGetUtilizationRates_ptr(device, &utilization);
    if (result == NVML_SUCCESS) {
      // Use memory controller utilization as approximation for memory bandwidth
      mediumMetrics.memoryBandwidthUtilization = utilization.memory;
      anyMetricsCollected = true;
    } else {
      mediumMetrics.memoryBandwidthUtilization = -1;
    }
  } else {
    mediumMetrics.memoryBandwidthUtilization = -1;
  }
  
  // Throttling detection would require additional API calls not readily available
  mediumMetrics.throttling = false;

  // Debug output for medium frequency metrics
  if (anyMetricsCollected) {
    static std::chrono::steady_clock::time_point lastMediumDebugOutput;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastMediumDebugOutput);
    
    // Output debug info every 15 seconds to avoid spam
    if (elapsed.count() >= 15) {
      std::cout << "GPU Medium-Freq Metrics Status - "
                << "Temp: " << mediumMetrics.temperature << "C, "
                << "Power: " << mediumMetrics.powerUsage << "W, "
                << "SM: " << mediumMetrics.smUtilization << "%, "
                << "MemBW: " << mediumMetrics.memoryBandwidthUtilization << "%, "
                << "PCIe_RX: " << mediumMetrics.pcieRxThroughput << ", "
                << "PCIe_TX: " << mediumMetrics.pcieTxThroughput
                << std::endl;
      lastMediumDebugOutput = now;
    }
  }

  return anyMetricsCollected;
#else
  return false;
#endif
}

// Helper method to apply cached medium-frequency metrics to current metrics
void NvidiaMetricsCollector::applyMediumFrequencyCache(nvmlDevice_t device, NvidiaGPUMetrics& metrics) {
  if (mediumFreqCache.count(device)) {
    const auto& cache = mediumFreqCache[device];
    
    // Check if cache is stale (older than 6 seconds - double the update interval)
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - cache.lastUpdate);
    
    if (elapsed.count() < 6000) { // Cache is still valid
      metrics.temperature = cache.temperature;
      metrics.powerUsage = cache.powerUsage;
      // fanSpeed is now collected in high frequency, don't override
      metrics.throttling = cache.throttling;
      metrics.smUtilization = cache.smUtilization;
      metrics.memoryBandwidthUtilization = cache.memoryBandwidthUtilization;
      metrics.pcieRxThroughput = cache.pcieRxThroughput;
      metrics.pcieTxThroughput = cache.pcieTxThroughput;
    } else {
      // Cache is stale, set to -1 as per user requirement
      metrics.temperature = -1;
      metrics.powerUsage = -1;
      // fanSpeed is now collected in high frequency, don't override
      metrics.throttling = false;
      metrics.smUtilization = -1;
      metrics.memoryBandwidthUtilization = -1;
      metrics.pcieRxThroughput = -1;
      metrics.pcieTxThroughput = -1;
    }
  } else {
    // If no cache exists, set medium-frequency metrics to -1 values
    metrics.temperature = -1;
    metrics.powerUsage = -1;
    // fanSpeed is now collected in high frequency, don't override
    metrics.throttling = false;
    metrics.smUtilization = -1;
    metrics.memoryBandwidthUtilization = -1;
    metrics.pcieRxThroughput = -1;
    metrics.pcieTxThroughput = -1;
  }
}

// Helper method to check if medium-frequency metrics should be updated
bool NvidiaMetricsCollector::shouldUpdateMediumFrequency(nvmlDevice_t device) {
  if (!mediumFreqCache.count(device)) {
    return true; // First collection
  }
  
  auto now = std::chrono::steady_clock::now();
  auto lastUpdate = mediumFreqCache[device].lastUpdate;
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate);
  
  return elapsed.count() >= 3000; // 3 seconds
}
