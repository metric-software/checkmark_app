/*
 * NvidiaMetrics - NVIDIA GPU Performance Monitoring
 * 
 * WORKING METRICS PROVIDED:
 * - temperature: GPU temperature in Celsius
 * - utilization: GPU utilization percentage
 * - memoryUtilization: Memory utilization percentage
 * - powerUsage: Power usage in milliwatts
 * - totalMemory: Total GPU memory in bytes
 * - usedMemory: Used GPU memory in bytes
 * - fanSpeed: Fan speed percentage
 * - clockSpeed: GPU clock in MHz
 * - memoryClock: Memory clock in MHz
 * - name: GPU name/model
 * - throttling: Thermal throttling status
 * - deviceId: GPU device ID
 * - driverVersion: Driver version string
 * - pciLinkWidth: PCIe link width
 * - pcieLinkGen: PCIe link generation
 * - encoderUtilization: Video encoder utilization
 * - decoderUtilization: Video decoder utilization
 * - computeUtilization: Compute utilization
 * - graphicsEngineUtilization: Graphics engine utilization
 * - smUtilization: SM (streaming multiprocessor) utilization
 * - memoryBandwidthUtilization: Memory bandwidth utilization
 * - pcieRxThroughput: PCIe receive throughput
 * - pcieTxThroughput: PCIe transmit throughput
 * - nvdecUtilization: NVDEC utilization
 * - nvencUtilization: NVENC utilization
 * - driverDate: Driver date string
 * - hasGeForceExperience: Whether GeForce Experience is installed
 *
 * NOTE: Requires NVIDIA GPU and drivers. Uses NVML when available.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <thread>
#include <vector>

#include <QObject>
#include <Windows.h>

// Include NVML headers only if CUDA toolkit is available
#ifdef CAN_USE_NVML
#include <nvml.h>
// Check for GPM support in NVML header
#if defined(NVML_API_VERSION_MAJOR) && (NVML_API_VERSION_MAJOR >= 12)
#define NVML_HAS_GPM_SUPPORT
#endif

// Move typedefs to global scope
typedef nvmlReturn_t (*nvmlInit_v2_t)(void);
typedef nvmlReturn_t (*nvmlShutdown_t)(void);
typedef nvmlReturn_t (*nvmlDeviceGetCount_v2_t)(unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetHandleByIndex_v2_t)(unsigned int,
                                                        nvmlDevice_t*);
typedef nvmlReturn_t (*nvmlDeviceGetTemperature_t)(nvmlDevice_t, unsigned int,
                                                   unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetUtilizationRates_t)(nvmlDevice_t,
                                                        nvmlUtilization_t*);
typedef nvmlReturn_t (*nvmlDeviceGetPowerUsage_t)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetMemoryInfo_t)(nvmlDevice_t, nvmlMemory_t*);
typedef nvmlReturn_t (*nvmlDeviceGetFanSpeed_t)(nvmlDevice_t, unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetClockInfo_t)(nvmlDevice_t, unsigned int,
                                                 unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetName_t)(nvmlDevice_t, char*, unsigned int);
typedef nvmlReturn_t (*nvmlDeviceGetPciInfo_t)(nvmlDevice_t, nvmlPciInfo_t*);
typedef nvmlReturn_t (*nvmlSystemGetDriverVersion_t)(char*, unsigned int);
typedef nvmlReturn_t (*nvmlDeviceGetCurrPcieLinkWidth_t)(nvmlDevice_t,
                                                         unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetCurrPcieLinkGeneration_t)(nvmlDevice_t,
                                                              unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetEncoderUtilization_t)(nvmlDevice_t,
                                                          unsigned int*,
                                                          unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetDecoderUtilization_t)(nvmlDevice_t,
                                                          unsigned int*,
                                                          unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetPcieThroughput_t)(nvmlDevice_t,
                                                      unsigned int,
                                                      unsigned int*);
typedef nvmlReturn_t (*nvmlDeviceGetComputeRunningProcesses_t)(
  nvmlDevice_t, unsigned int*, nvmlProcessInfo_t*);
typedef nvmlReturn_t (*nvmlDeviceGetProcessUtilization_t)(
  nvmlDevice_t, nvmlProcessUtilizationSample_t*, unsigned int*,
  unsigned long long);
typedef const char* (*nvmlErrorString_t)(nvmlReturn_t);

#else
// Define dummy types if NVML is not available
typedef void* nvmlDevice_t;
typedef int nvmlReturn_t;
#define NVML_SUCCESS 0
#define NVML_ERROR_UNINITIALIZED 1
#define NVML_ERROR_INVALID_ARGUMENT 2
#define NVML_ERROR_NOT_SUPPORTED 3
#define NVML_ERROR_NO_PERMISSION 4
#define NVML_ERROR_ALREADY_INITIALIZED 5
#define NVML_ERROR_NOT_FOUND 6
#define NVML_ERROR_INSUFFICIENT_SIZE 7
#define NVML_ERROR_INSUFFICIENT_POWER 8
#define NVML_ERROR_DRIVER_NOT_LOADED 9
#define NVML_ERROR_TIMEOUT 10
#define NVML_ERROR_IRQ_ISSUE 11
#define NVML_ERROR_LIBRARY_NOT_FOUND 12
#define NVML_ERROR_FUNCTION_NOT_FOUND 13
#define NVML_ERROR_CORRUPTED_INFOROM 14
#define NVML_ERROR_GPU_IS_LOST 15
#define NVML_ERROR_RESET_REQUIRED 16
#define NVML_ERROR_OPERATING_SYSTEM 17
#define NVML_ERROR_LIB_RM_VERSION_MISMATCH 18
#define NVML_ERROR_UNKNOWN 999
#define NVML_TEMPERATURE_GPU 0
#define NVML_CLOCK_GRAPHICS 0
#define NVML_CLOCK_MEM 1
#define NVML_PCIE_UTIL_RX_BYTES 0
#define NVML_PCIE_UTIL_TX_BYTES 1
#define NVML_DEVICE_NAME_BUFFER_SIZE 64
#define NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE 80
#endif

struct NvidiaGPUMetrics {
  unsigned int temperature;        // GPU temperature in Celsius
  unsigned int utilization;        // GPU utilization percentage
  unsigned int memoryUtilization;  // Memory utilization percentage
  unsigned int powerUsage;         // Power usage in milliwatts
  unsigned long long totalMemory;  // Total memory in bytes
  unsigned long long usedMemory;   // Used memory in bytes
  unsigned int fanSpeed;           // Fan speed percentage
  unsigned int clockSpeed;         // GPU clock in MHz
  unsigned int memoryClock;        // Memory clock in MHz
  std::string name;                // GPU name
  bool throttling;                 // Thermal throttling status
  std::string deviceId;            // PCI device ID
  std::string driverVersion;       // GPU driver version
  unsigned int pciLinkWidth;       // PCIe link width
  unsigned int pcieLinkGen;        // PCIe link generation

  // Existing detailed GPU metrics
  unsigned int encoderUtilization;  // Video encoder utilization percentage
  unsigned int decoderUtilization;  // Video decoder utilization percentage
  unsigned int
    computeUtilization;  // Compute utilization (estimated from samples)

  // New detailed GPU metrics
  unsigned int
    graphicsEngineUtilization;  // Graphics engine activity percentage
  unsigned int smUtilization;   // Streaming Multiprocessor utilization
  unsigned int memoryBandwidthUtilization;  // Memory bandwidth utilization
  unsigned int pcieRxThroughput;            // PCIe receive throughput (MiB/sec)
  unsigned int pcieTxThroughput;  // PCIe transmit throughput (MiB/sec)
  unsigned int
    nvdecUtilization;  // NVDEC utilization (same as decoderUtilization)
  unsigned int
    nvencUtilization;  // NVENC utilization (same as encoderUtilization)

  // Add these fields to the NvidiaGPUMetrics struct
  std::string driverDate;     // NVIDIA driver installation date
  bool hasGeForceExperience;  // Whether GeForce Experience is installed
};

// Add struct for per-process GPU metrics
struct NvidiaProcessGPUMetrics {
  unsigned int pid;                 // Process ID
  std::string name;                 // Process name (if available)
  unsigned int gpuUtilization;      // GPU utilization percentage
  unsigned int memoryUtilization;   // Memory controller utilization
  unsigned int computeUtilization;  // Compute utilization
  unsigned int encoderUtilization;  // Encoder utilization
  unsigned int decoderUtilization;  // Decoder utilization
  unsigned long long memoryUsed;    // Memory used in bytes
};

// Forward declarations
struct GPUMediumFreqMetrics;

class NvidiaMetricsCollector : public QObject {
  Q_OBJECT

 public:
  explicit NvidiaMetricsCollector(QObject* parent = nullptr);
  ~NvidiaMetricsCollector();

  bool startCollecting(int updateIntervalMs = 1000);
  void stopCollecting();
  bool isRunning() const { return running; }

  // Get list of available GPUs
  std::vector<nvmlDevice_t> getAvailableGPUs();

  // Get metrics for a specific GPU (one-time snapshot)
  bool getMetricsForDevice(nvmlDevice_t device, NvidiaGPUMetrics& metrics);

  // Get detailed GPU utilization including per-process metrics if available
  bool getDetailedMetricsForDevice(
    nvmlDevice_t device, NvidiaGPUMetrics& metrics,
    std::vector<NvidiaProcessGPUMetrics>& processMetrics);

  // Add a public method to initialize NVML without starting collection
  bool ensureInitialized() { return nvmlInitialized || initializeNVML(); }

  // Optimized method for getting just utilization metrics
  bool getBasicUtilizationMetrics(nvmlDevice_t device, unsigned int& gpuUtil,
                                  unsigned int& memUtil);

  // Optimized method for getting just memory metrics
  bool getMemoryMetrics(nvmlDevice_t device, unsigned long long& totalMem,
                        unsigned long long& usedMem);

  // Optimized method for getting just power and thermal metrics
  bool getPowerAndThermalMetrics(nvmlDevice_t device, unsigned int& temperature,
                                 float& powerUsage, unsigned int& fanSpeed);

  // Optimized method for getting just clock metrics
  bool getClockMetrics(nvmlDevice_t device, unsigned int& coreClock,
                       unsigned int& memoryClock);

  // Optimized method for getting just encoder/decoder metrics
  bool getEncoderDecoderMetrics(nvmlDevice_t device, unsigned int& encoderUtil,
                                unsigned int& decoderUtil);

  // Benchmark-specific optimized metrics collection
  bool getBenchmarkGPUMetrics(nvmlDevice_t device, NvidiaGPUMetrics& metrics);

  // Static method for metrics collection (for use with function pointers)
  static bool getGPUMetricsStatic(nvmlDevice_t device,
                                  NvidiaGPUMetrics& metrics);

  // Get GPU utilization data per process
  bool getGpuProcessUtilization(
    nvmlDevice_t device, std::vector<NvidiaProcessGPUMetrics>& processMetrics);

  // Get NVIDIA driver date and check for GeForce Experience
  bool getNvidiaDriverInfo(std::string& driverDate, bool& hasGeForceExperience);

 signals:
  void metricsUpdated(const NvidiaGPUMetrics& metrics);
  void collectionError(const QString& error);

 private:
  // Helper methods for optimized collection
  bool collectHighFrequencyMetrics(nvmlDevice_t device, NvidiaGPUMetrics& metrics);
  bool collectMediumFrequencyMetrics(nvmlDevice_t device);
  void applyMediumFrequencyCache(nvmlDevice_t device, NvidiaGPUMetrics& metrics);
  bool shouldUpdateMediumFrequency(nvmlDevice_t device);
  bool initializeNVML();
  void shutdownNVML();
  void collectMetrics();
  bool getGPUMetrics(nvmlDevice_t device, NvidiaGPUMetrics& metrics);
  void initializeNoDataMetrics(NvidiaGPUMetrics& metrics);

  // Function pointers for dynamic NVML loading
#ifdef CAN_USE_NVML
  // Handle to the dynamically loaded NVML library
  HMODULE nvmlLibrary = nullptr;

  // Function pointers
  nvmlInit_v2_t nvmlInit_v2_ptr = nullptr;
  nvmlShutdown_t nvmlShutdown_ptr = nullptr;
  nvmlDeviceGetCount_v2_t nvmlDeviceGetCount_v2_ptr = nullptr;
  nvmlDeviceGetHandleByIndex_v2_t nvmlDeviceGetHandleByIndex_v2_ptr = nullptr;
  nvmlDeviceGetTemperature_t nvmlDeviceGetTemperature_ptr = nullptr;
  nvmlDeviceGetUtilizationRates_t nvmlDeviceGetUtilizationRates_ptr = nullptr;
  nvmlDeviceGetPowerUsage_t nvmlDeviceGetPowerUsage_ptr = nullptr;
  nvmlDeviceGetMemoryInfo_t nvmlDeviceGetMemoryInfo_ptr = nullptr;
  nvmlDeviceGetFanSpeed_t nvmlDeviceGetFanSpeed_ptr = nullptr;
  nvmlDeviceGetClockInfo_t nvmlDeviceGetClockInfo_ptr = nullptr;
  nvmlDeviceGetName_t nvmlDeviceGetName_ptr = nullptr;
  nvmlDeviceGetPciInfo_t nvmlDeviceGetPciInfo_ptr = nullptr;
  nvmlSystemGetDriverVersion_t nvmlSystemGetDriverVersion_ptr = nullptr;
  nvmlDeviceGetCurrPcieLinkWidth_t nvmlDeviceGetCurrPcieLinkWidth_ptr = nullptr;
  nvmlDeviceGetCurrPcieLinkGeneration_t
    nvmlDeviceGetCurrPcieLinkGeneration_ptr = nullptr;
  nvmlDeviceGetEncoderUtilization_t nvmlDeviceGetEncoderUtilization_ptr =
    nullptr;
  nvmlDeviceGetDecoderUtilization_t nvmlDeviceGetDecoderUtilization_ptr =
    nullptr;
  nvmlDeviceGetPcieThroughput_t nvmlDeviceGetPcieThroughput_ptr = nullptr;
  nvmlDeviceGetComputeRunningProcesses_t
    nvmlDeviceGetComputeRunningProcesses_ptr = nullptr;
  nvmlDeviceGetProcessUtilization_t nvmlDeviceGetProcessUtilization_ptr =
    nullptr;
  nvmlErrorString_t nvmlErrorString_ptr = nullptr;

  // Function to load the NVML library and initialize function pointers
  bool loadNvmlLibrary();

  // Function to unload the NVML library
  void unloadNvmlLibrary();
#endif

  // Cache for static GPU information
  struct GPUStaticInfo {
    // Add these fields
    std::string driverDate = "Unknown";
    bool hasGeForceExperience = false;
    bool driverDateChecked = false;

    // ...existing fields...
    std::string name;
    std::string deviceId;
    std::string driverVersion;
    unsigned int pciLinkWidth = -1;
    unsigned int pcieLinkGen = -1;
  };

  // Cache for medium-frequency metrics (updated every 3 seconds)
  struct GPUMediumFreqMetrics {
    unsigned int temperature = -1;
    float powerUsage = -1;
    unsigned int fanSpeed = -1;
    bool throttling = false;
    unsigned int smUtilization = -1;
    unsigned int memoryBandwidthUtilization = -1;
    unsigned int pcieRxThroughput = -1;
    unsigned int pcieTxThroughput = -1;
    
    // Timestamp of last update
    std::chrono::steady_clock::time_point lastUpdate;
    
    // Initialize with current time so first collection happens immediately
    GPUMediumFreqMetrics() : lastUpdate(std::chrono::steady_clock::time_point{}) {}
  };

  std::atomic<bool> running;
  std::thread collectorThread;
  int updateInterval;
  bool nvmlInitialized;
  bool staticInfoInitialized;
  std::vector<nvmlDevice_t> gpuHandles;
  std::map<nvmlDevice_t, GPUStaticInfo> staticInfoCache;
  std::map<nvmlDevice_t, GPUMediumFreqMetrics> mediumFreqCache;
};
