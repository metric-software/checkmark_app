#include "background_process_monitor.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include <Pdhmsg.h>
#include <Psapi.h>  // For GetPerformanceInfo
#include <TlHelp32.h>
#include <pdh.h>

#include "DiagnosticDataStore.h"
#include "background_process_worker.h"
#include "hardware/NvidiaMetrics.h"

#include "logging/Logger.h"

namespace BackgroundProcessMonitor {

// Add this declaration at the beginning of the namespace, with the other
// function declarations
void storeMonitoringResultsInDataStore(const MonitoringResult& result);

// Add this new helper function to check for cancellation
bool checkCancellation(BackgroundProcessWorker* worker) {
  return worker && worker->isCancelled();
}

// Forward declaration for getMemoryMetrics
void getMemoryMetrics(MonitoringResult& result, SIZE_T sumPrivateWorkingSetKB);

// Constants for monitoring parameters
static constexpr int MAX_MONITOR_SECONDS = 10;
static constexpr int SAMPLE_COUNT = 5;
static constexpr int BASELINE_WAIT_MS = 1500;
static constexpr double DPC_THRESHOLD = 1.0;
static constexpr double INTERRUPT_THRESHOLD = 0.5;

// Helper function to strip ".exe" from process names for PDH matching - moved
// to the top
std::wstring stripExeSuffix(const std::wstring& name) {
  const std::wstring exeSuffix = L".exe";
  if (name.size() > exeSuffix.size() &&
      name.compare(name.size() - exeSuffix.size(), exeSuffix.size(),
                   exeSuffix) == 0) {
    return name.substr(0, name.size() - exeSuffix.size());
  }
  return name;
}

// Helper: Enumerate valid instance names for the "Process" object and return a
// best match for a given process name.
std::wstring getValidProcessInstanceName(const std::wstring& procName) {
  DWORD counterListSize = 0;
  DWORD instanceListSize = 0;

  PDH_STATUS status =
    PdhEnumObjectItems(NULL, NULL, L"Process", NULL, &counterListSize, NULL,
                       &instanceListSize, PERF_DETAIL_WIZARD, 0);

  if (status != PDH_MORE_DATA && status != ERROR_SUCCESS) {
    return procName;
  }

  std::vector<WCHAR> instanceList(instanceListSize + 1);
  status = PdhEnumObjectItems(NULL, NULL, L"Process", NULL, &counterListSize,
                              instanceList.data(), &instanceListSize,
                              PERF_DETAIL_WIZARD, 0);

  std::set<std::wstring> validInstances;
  if (status == ERROR_SUCCESS) {
    WCHAR* instance = instanceList.data();
    while (*instance) {
      validInstances.insert(std::wstring(instance));
      instance += wcslen(instance) + 1;
    }
  }

  if (validInstances.find(procName) != validInstances.end()) {
    return procName;
  }

  std::wstring altName = procName + L"#1";
  if (validInstances.find(altName) != validInstances.end()) {
    return altName;
  }

  for (const auto& inst : validInstances) {
    if (inst.find(procName) == 0) {
      return inst;
    }
  }

  return procName;
}

// Simple utility to convert wstring to string (UTF-8)
std::string wstringToString(const std::wstring& wstr) {
  if (wstr.empty()) return "";
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
                                        (int)wstr.size(), NULL, 0, NULL, NULL);
  std::string result(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0],
                      size_needed, NULL, NULL);
  return result;
}

// Get process list using ToolHelp32 snapshot (safe and does not require direct
// process handles)
std::map<DWORD, std::wstring> getRunningProcesses() {
  std::map<DWORD, std::wstring> processes;
  HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnapshot == INVALID_HANDLE_VALUE) {
    return processes;
  }
  PROCESSENTRY32W pe32;
  pe32.dwSize = sizeof(PROCESSENTRY32W);
  if (Process32FirstW(hSnapshot, &pe32)) {
    do {
      processes[pe32.th32ProcessID] = pe32.szExeFile;
    } while (Process32NextW(hSnapshot, &pe32));
  }
  CloseHandle(hSnapshot);
  return processes;
}

// Main monitoring function with improved cleanup
MonitoringResult monitorBackgroundProcesses(int durationSeconds,
                                            BackgroundProcessWorker* worker) {
  const int monitorSeconds =
    (durationSeconds < 10) ? durationSeconds : 10;  // Cap at 10 seconds
  MonitoringResult result;
  result.totalCpuUsage = 0;
  result.totalGpuUsage = 0;
  result.systemDpcTime = 0;
  result.systemInterruptTime = 0;

  auto processes = getRunningProcesses();

  // Initialize PDH queries to NULL to ensure safer cleanup
  PDH_HQUERY systemQuery = NULL;
  PDH_HQUERY processQuery = NULL;

  // Create a cleanup helper using lambda to avoid code duplication
  auto cleanupQueries = [&]() {
    if (systemQuery) {
      PdhCloseQuery(systemQuery);
      systemQuery = NULL;
    }
    if (processQuery) {
      PdhCloseQuery(processQuery);
      processQuery = NULL;
    }
  };

  // Open system query with proper error handling
  if (PdhOpenQuery(NULL, 0, &systemQuery) != ERROR_SUCCESS) {
    // No cleanup needed as queries are still NULL
    return result;
  }

  // Open process query with proper error handling
  if (PdhOpenQuery(NULL, 0, &processQuery) != ERROR_SUCCESS) {
    // Only need to clean up systemQuery
    if (systemQuery) PdhCloseQuery(systemQuery);
    return result;
  }

  // Create counters
  PDH_HCOUNTER cpuTotalCounter = NULL, dpcCounter = NULL,
               interruptCounter = NULL;
  PDH_HCOUNTER diskReadCounter = NULL,
               diskWriteCounter = NULL;  // Add disk I/O counters
  PdhAddEnglishCounter(systemQuery, L"\\Processor(_Total)\\% Processor Time", 0,
                       &cpuTotalCounter);
  PdhAddEnglishCounter(systemQuery, L"\\Processor(_Total)\\% DPC Time", 0,
                       &dpcCounter);
  PdhAddEnglishCounter(systemQuery, L"\\Processor(_Total)\\% Interrupt Time", 0,
                       &interruptCounter);

  // Add disk I/O counters for total system disk activity
  PdhAddEnglishCounter(systemQuery,
                       L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0,
                       &diskReadCounter);
  PdhAddEnglishCounter(systemQuery,
                       L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0,
                       &diskWriteCounter);

  // Check for cancellation after lengthy initialization
  if (checkCancellation(worker)) {
    cleanupQueries();
    return result;
  }

  DWORD counterListSize = 0;
  DWORD instanceListSize = 0;

  PDH_STATUS status =
    PdhEnumObjectItemsW(NULL, NULL, L"Process", NULL, &counterListSize, NULL,
                        &instanceListSize, PERF_DETAIL_WIZARD, 0);

  std::vector<WCHAR> counterList(counterListSize + 2, 0);
  std::vector<WCHAR> instanceList(instanceListSize + 2, 0);

  status = PdhEnumObjectItemsW(NULL, NULL, L"Process", counterList.data(),
                               &counterListSize, instanceList.data(),
                               &instanceListSize, PERF_DETAIL_WIZARD, 0);

  std::vector<std::wstring> validInstances;
  if (status == ERROR_SUCCESS) {
    WCHAR* instancePtr = instanceList.data();
    while (*instancePtr) {
      validInstances.push_back(instancePtr);
      instancePtr += wcslen(instancePtr) + 1;
    }
  }

  std::map<std::wstring, std::set<DWORD>> processGroups;
  for (const auto& [pid, name] : processes) {
    processGroups[name].insert(pid);
  }

  struct ProcessMetrics {
    std::wstring name;
    std::vector<PDH_HCOUNTER> cpuCounters;
    std::vector<PDH_HCOUNTER> memCounters;
    std::vector<std::wstring> instanceNames;
    double cpuPercent = 0;
    double peakCpuPercent = 0;
    SIZE_T memoryKB = 0;
    SIZE_T maxMemoryKB = 0;
    int instances = 1;
    int sampleCount = 0;
    std::set<DWORD> pids;
  };

  std::map<std::wstring, ProcessMetrics> processMetrics;

  // Build PDH counters for CPU and memory metrics (unchanged)
  for (const auto& [name, pids] : processGroups) {
    ProcessMetrics metrics;
    metrics.name = name;
    metrics.instances = static_cast<int>(pids.size());
    metrics.pids = pids;

    std::wstring nameWithoutExe = stripExeSuffix(name);
    std::wstring lowerName = nameWithoutExe;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   ::tolower);
    std::wstring lowerNameWithExe = name;
    std::transform(lowerNameWithExe.begin(), lowerNameWithExe.end(),
                   lowerNameWithExe.begin(), ::tolower);

    for (const auto& instance : validInstances) {
      bool isMatch = false;

      if (instance == nameWithoutExe || instance == name) {
        isMatch = true;
      } else if (instance.find(nameWithoutExe + L"#") == 0 ||
                 instance.find(name + L"#") == 0) {
        isMatch = true;
      } else {
        std::wstring lowerInstance = instance;
        std::transform(lowerInstance.begin(), lowerInstance.end(),
                       lowerInstance.begin(), ::tolower);

        if (lowerInstance == lowerName || lowerInstance == lowerNameWithExe ||
            lowerInstance.find(lowerName + L"#") == 0 ||
            lowerInstance.find(lowerNameWithExe + L"#") == 0) {
          isMatch = true;
        }
      }

      if (isMatch) {
        metrics.instanceNames.push_back(instance);

        std::wstring cpuPath =
          L"\\Process(" + instance + L")\\% Processor Time";
        std::wstring memPath =
          L"\\Process(" + instance + L")\\Working Set - Private";

        PDH_HCOUNTER cpuCounter = NULL;
        PDH_HCOUNTER memCounter = NULL;

        if (PdhAddEnglishCounterW(processQuery, cpuPath.c_str(), 0,
                                  &cpuCounter) == ERROR_SUCCESS) {
          metrics.cpuCounters.push_back(cpuCounter);
        }

        if (PdhAddEnglishCounterW(processQuery, memPath.c_str(), 0,
                                  &memCounter) == ERROR_SUCCESS) {
          metrics.memCounters.push_back(memCounter);
        }
      }
    }

    if (metrics.instanceNames.empty()) {
      std::wstring fallbackName = nameWithoutExe;
      metrics.instanceNames.push_back(fallbackName);

      std::wstring cpuPath =
        L"\\Process(" + fallbackName + L")\\% Processor Time";
      std::wstring memPath =
        L"\\Process(" + fallbackName + L")\\Working Set - Private";

      PDH_HCOUNTER cpuCounter = NULL;
      PDH_HCOUNTER memCounter = NULL;

      if (PdhAddEnglishCounterW(processQuery, cpuPath.c_str(), 0,
                                &cpuCounter) == ERROR_SUCCESS) {
        metrics.cpuCounters.push_back(cpuCounter);
      }

      if (PdhAddEnglishCounterW(processQuery, memPath.c_str(), 0,
                                &memCounter) == ERROR_SUCCESS) {
        metrics.memCounters.push_back(memCounter);
      }
    }

    processMetrics[name] = metrics;
  }

  // Initialize GPU metrics
  NvidiaGPUMetrics systemGpuMetrics;
  std::vector<NvidiaProcessGPUMetrics> processGpuMetrics;
  bool hasGpuMetrics = false;
  double totalGpuUsage = 0.0;  // Default to 0

  // Initialize NVIDIA metrics collection
  NvidiaMetricsCollector nvCollector;
  bool nvmlInitialized = false;

  // Only try initialization if not cancelled
  if (!checkCancellation(worker)) {
    nvmlInitialized = nvCollector.ensureInitialized();
  }

  // Get system-wide GPU metrics using NVML
  if (nvmlInitialized) {
    auto gpus = nvCollector.getAvailableGPUs();
    if (!gpus.empty()) {
      // First try getBenchmarkGPUMetrics which tends to be more reliable
      if (nvCollector.getBenchmarkGPUMetrics(gpus[0], systemGpuMetrics)) {
        totalGpuUsage = systemGpuMetrics.utilization;
        hasGpuMetrics = true;
      }
      // If that fails, fall back to detailed metrics
      else if (nvCollector.getDetailedMetricsForDevice(
                 gpus[0], systemGpuMetrics, processGpuMetrics)) {
        totalGpuUsage = systemGpuMetrics.utilization;
        hasGpuMetrics = true;
      }

      // For process metrics, use both methods to get the most complete data
      std::vector<NvidiaProcessGPUMetrics> additionalProcessMetrics;
      bool hasAdditionalMetrics =
        nvCollector.getGpuProcessUtilization(gpus[0], additionalProcessMetrics);

      // Merge process metrics
      if (hasAdditionalMetrics) {
        std::map<unsigned int, NvidiaProcessGPUMetrics> combinedMetrics;

        // Add existing metrics
        for (const auto& metric : processGpuMetrics) {
          combinedMetrics[metric.pid] = metric;
        }

        // Add/update with new metrics
        for (const auto& metric : additionalProcessMetrics) {
          if (combinedMetrics.find(metric.pid) != combinedMetrics.end()) {
            // Take the maximum values between the two methods
            auto& existing = combinedMetrics[metric.pid];
            existing.gpuUtilization =
              std::max(existing.gpuUtilization, metric.gpuUtilization);
            existing.memoryUtilization =
              std::max(existing.memoryUtilization, metric.memoryUtilization);

            // If memory shows NVIDIA's sentinel value (0xFFFFFFFF), set to 0
            if (existing.memoryUsed == 0xFFFFFFFFULL) {
              existing.memoryUsed = 0;
            }
          } else {
            // If memory shows NVIDIA's sentinel value, set to 0
            NvidiaProcessGPUMetrics newMetric = metric;
            if (newMetric.memoryUsed == 0xFFFFFFFFULL) {
              newMetric.memoryUsed = 0;
            }
            combinedMetrics[metric.pid] = newMetric;
          }
        }

        // Convert back to vector
        processGpuMetrics.clear();
        for (const auto& pair : combinedMetrics) {
          processGpuMetrics.push_back(pair.second);
        }

        hasGpuMetrics = true;
      }
    }
  }

  // Store the validated GPU usage in the result
  result.totalGpuUsage = totalGpuUsage;

  // Initial data collection
  PdhCollectQueryData(systemQuery);
  PdhCollectQueryData(processQuery);

  const int SAMPLE_INTERVAL_MS = (monitorSeconds * 1000) / (SAMPLE_COUNT + 1);

  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  DWORD numProcessors = sysInfo.dwNumberOfProcessors;

  // Initialize tracking variables for averages and peaks
  double totalDpcTime = 0, totalInterruptTime = 0;
  double peakDpcTime = 0, peakInterruptTime = 0;
  double totalCpuUsage = 0, peakCpuUsage = 0;  // Add CPU tracking
  double totalDiskRead = 0, totalDiskWrite = 0,
         peakDiskIO = 0;  // Add disk I/O tracking
  int sampleCount = 0;
  PDH_FMT_COUNTERVALUE value;

  // Main sampling loop with cancellation checks
  for (int sample = 0; sample < SAMPLE_COUNT; sample++) {
    // Check for cancellation between samples
    if (checkCancellation(worker)) {
      cleanupQueries();
      return result;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(SAMPLE_INTERVAL_MS));

    // Check for cancellation again after sleep
    if (checkCancellation(worker)) {
      cleanupQueries();
      return result;
    }

    // Collect system metrics
    PdhCollectQueryData(systemQuery);

    // CPU metrics - track both total and peak
    if (cpuTotalCounter &&
        PdhGetFormattedCounterValue(cpuTotalCounter, PDH_FMT_DOUBLE, NULL,
                                    &value) == ERROR_SUCCESS) {
      double currentCpuUsage = value.doubleValue;
      totalCpuUsage += currentCpuUsage;
      peakCpuUsage = std::max(peakCpuUsage, currentCpuUsage);
      sampleCount++;
    }

    // DPC metrics
    if (dpcCounter &&
        PdhGetFormattedCounterValue(dpcCounter, PDH_FMT_DOUBLE, NULL, &value) ==
          ERROR_SUCCESS) {
      double currentDpcTime = value.doubleValue;
      totalDpcTime += currentDpcTime;
      peakDpcTime =
        std::max(peakDpcTime, currentDpcTime);  // Track peak DPC time
    }

    // Interrupt metrics
    if (interruptCounter &&
        PdhGetFormattedCounterValue(interruptCounter, PDH_FMT_DOUBLE, NULL,
                                    &value) == ERROR_SUCCESS) {
      double currentInterruptTime = value.doubleValue;
      totalInterruptTime += currentInterruptTime;
      peakInterruptTime = std::max(
        peakInterruptTime, currentInterruptTime);  // Track peak interrupt time
    }

    // Disk I/O metrics
    double currentDiskRead = 0, currentDiskWrite = 0;
    if (diskReadCounter &&
        PdhGetFormattedCounterValue(diskReadCounter, PDH_FMT_LARGE, NULL,
                                    &value) == ERROR_SUCCESS) {
      currentDiskRead = static_cast<double>(value.largeValue) /
                        (1024 * 1024);  // Convert to MB/s
      totalDiskRead += currentDiskRead;
    }
    if (diskWriteCounter &&
        PdhGetFormattedCounterValue(diskWriteCounter, PDH_FMT_LARGE, NULL,
                                    &value) == ERROR_SUCCESS) {
      currentDiskWrite = static_cast<double>(value.largeValue) /
                         (1024 * 1024);  // Convert to MB/s
      totalDiskWrite += currentDiskWrite;
    }
    double currentTotalDiskIO = currentDiskRead + currentDiskWrite;
    peakDiskIO = std::max(peakDiskIO, currentTotalDiskIO);

    // Collect process metrics
    PdhCollectQueryData(processQuery);
    for (auto& [name, metrics] : processMetrics) {
      bool hasCpuSample = false;
      double sampleCpuTotal = 0.0;

      for (auto& cpuCounter : metrics.cpuCounters) {
        if (cpuCounter) {
          if (PdhGetFormattedCounterValue(cpuCounter, PDH_FMT_DOUBLE, NULL,
                                          &value) == ERROR_SUCCESS) {
            double cpuValue = value.doubleValue / numProcessors;
            sampleCpuTotal += cpuValue;
            hasCpuSample = true;
          }
        }
      }

      if (hasCpuSample) {
        metrics.cpuPercent += sampleCpuTotal;
        metrics.peakCpuPercent =
          std::max(sampleCpuTotal, metrics.peakCpuPercent);
        metrics.sampleCount++;
      }

      SIZE_T totalMemKB = 0;
      for (auto& memCounter : metrics.memCounters) {
        if (memCounter) {
          if (PdhGetFormattedCounterValue(memCounter, PDH_FMT_LARGE, NULL,
                                          &value) == ERROR_SUCCESS) {
            SIZE_T memKB = static_cast<SIZE_T>(value.largeValue / 1024);
            totalMemKB += memKB;
          }
        }
      }

      metrics.memoryKB = totalMemKB;
      metrics.maxMemoryKB = std::max(totalMemKB, metrics.maxMemoryKB);
    }
  }

  // Calculate results and format output
  // Store averages and peaks for system metrics
  if (sampleCount > 0) {
    result.totalCpuUsage =
      totalCpuUsage / sampleCount;       // Store average CPU usage
    result.peakCpuUsage = peakCpuUsage;  // Store peak CPU usage
    result.totalDiskIO =
      (totalDiskRead + totalDiskWrite) / sampleCount;  // Store average disk I/O
    result.peakDiskIO = peakDiskIO;                    // Store peak disk I/O
  }

  result.systemDpcTime = totalDpcTime / SAMPLE_COUNT;
  result.systemInterruptTime = totalInterruptTime / SAMPLE_COUNT;
  result.peakSystemDpcTime = peakDpcTime;  // Store peak DPC time
  result.peakSystemInterruptTime =
    peakInterruptTime;  // Store peak interrupt time

  // Track GPU peak during sampling for better accuracy
  double totalGpuUsageSum = 0.0;
  double peakGpuUsage = 0.0;
  int gpuSampleCount = 0;

  // Sample GPU multiple times during monitoring for average and peak
  for (int gpuSample = 0; gpuSample < 3; gpuSample++) {
    if (nvmlInitialized) {
      auto gpus = nvCollector.getAvailableGPUs();
      if (!gpus.empty()) {
        NvidiaGPUMetrics currentGpuMetrics;
        if (nvCollector.getBenchmarkGPUMetrics(gpus[0], currentGpuMetrics)) {
          double currentGpuUsage = currentGpuMetrics.utilization;
          if (currentGpuUsage >= 0 && currentGpuUsage <= 100.0) {
            totalGpuUsageSum += currentGpuUsage;
            peakGpuUsage = std::max(peakGpuUsage, currentGpuUsage);
            gpuSampleCount++;
          }
        }
      }
    }
    if (gpuSample < 2) {  // Don't sleep after the last sample
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }

  // Store GPU metrics
  if (gpuSampleCount > 0) {
    result.totalGpuUsage =
      totalGpuUsageSum / gpuSampleCount;  // Store average GPU usage
    result.peakGpuUsage = peakGpuUsage;   // Store peak GPU usage
  } else {
    // Fallback to original method if new sampling failed
    result.totalGpuUsage = totalGpuUsage;
    result.peakGpuUsage =
      totalGpuUsage;  // Use current value as peak if no samples
  }

  result.hasDpcLatencyIssues =
    (result.systemDpcTime > DPC_THRESHOLD ||
     result.systemInterruptTime > INTERRUPT_THRESHOLD);

  std::map<DWORD, NvidiaProcessGPUMetrics> pidToGpuMetrics;
  if (hasGpuMetrics) {
    for (const auto& procMetric : processGpuMetrics) {
      pidToGpuMetrics[procMetric.pid] = procMetric;
    }
  }

  // Calculate the total user-mode private memory in KB for all processes
  SIZE_T totalUserModePrivateKB = 0;

  std::vector<ProcessData> allProcesses;
  for (const auto& [name, metrics] : processMetrics) {
    if (metrics.sampleCount > 0) {
      double avgCpuPercent = metrics.cpuPercent / metrics.sampleCount;
      ProcessData procData;
      procData.name = name;
      procData.cpuPercent = avgCpuPercent;
      procData.peakCpuPercent = metrics.peakCpuPercent;
      procData.memoryUsageKB = std::max(metrics.memoryKB, metrics.maxMemoryKB);
      procData.instanceCount = metrics.instances;

      // Add process memory to total user-mode private memory
      totalUserModePrivateKB += procData.memoryUsageKB;

      // Apply the heuristic GPU usage only as fallback
      bool hasGpuDataForProcess = false;

      if (hasGpuMetrics) {
        for (DWORD pid : metrics.pids) {
          if (pidToGpuMetrics.count(pid) > 0) {
            const auto& gpuMetric = pidToGpuMetrics[pid];
            procData.gpuPercent =
              std::max(procData.gpuPercent,
                       static_cast<double>(gpuMetric.gpuUtilization));
            procData.gpuComputePercent =
              static_cast<double>(gpuMetric.computeUtilization);
            procData.gpuEncoderPercent =
              static_cast<double>(gpuMetric.encoderUtilization);

            // Check for NVIDIA's sentinel value before converting to MB
            if (gpuMetric.memoryUsed != 0xFFFFFFFFULL &&
                gpuMetric.memoryUsed < (32ULL * 1024 * 1024 * 1024)) {
              procData.gpuMemoryMB =
                static_cast<double>(gpuMetric.memoryUsed) / (1024 * 1024);
            } else {
              procData.gpuMemoryMB = 0.0;  // Set to 0 for invalid values
            }
            hasGpuDataForProcess = true;
          }
        }
      }

      // Only use heuristics if we don't have actual GPU data for this process
      if (!hasGpuDataForProcess) {
        // Apply heuristic-based GPU estimation for common processes
        if (name.find(L"chrome") != std::wstring::npos ||
            name.find(L"edge") != std::wstring::npos ||
            name.find(L"firefox") != std::wstring::npos) {
          procData.gpuPercent = 2.0;
        } else if (name.find(L"obs") != std::wstring::npos ||
                   name.find(L"streamlabs") != std::wstring::npos) {
          procData.gpuPercent = 4.0;
        } else if (name.find(L"nvidia") != std::wstring::npos ||
                   name.find(L"amd") != std::wstring::npos) {
          procData.gpuPercent = 3.0;
        } else if (name.find(L"game") != std::wstring::npos ||
                   name.find(L"steam") != std::wstring::npos ||
                   name.find(L"battle") != std::wstring::npos ||
                   name.find(L"epic") != std::wstring::npos) {
          procData.gpuPercent = 2.5;
        }
      }

      std::wstringstream pidList;
      pidList << L"PIDs: ";
      for (DWORD pid : metrics.pids) {
        pidList << pid << L", ";
      }
      procData.path = pidList.str();
      allProcesses.push_back(procData);
    }
  }

  // Get and store memory metrics
  getMemoryMetrics(result, totalUserModePrivateKB);

  result.processes = allProcesses;

  // Sorting and collecting top processes remains unchanged
  std::vector<ProcessData> topCpuProcesses = allProcesses;
  std::sort(topCpuProcesses.begin(), topCpuProcesses.end(),
            [](const ProcessData& a, const ProcessData& b) {
              return a.cpuPercent > b.cpuPercent;
            });
  if (topCpuProcesses.size() > 5) {
    topCpuProcesses.resize(5);
  }
  result.topCpuProcesses = topCpuProcesses;

  std::vector<ProcessData> topMemoryProcesses = allProcesses;
  std::sort(topMemoryProcesses.begin(), topMemoryProcesses.end(),
            [](const ProcessData& a, const ProcessData& b) {
              return a.memoryUsageKB > b.memoryUsageKB;
            });
  if (topMemoryProcesses.size() > 5) {
    topMemoryProcesses.resize(5);
  }
  result.topMemoryProcesses = topMemoryProcesses;

  std::vector<ProcessData> topGpuProcesses = allProcesses;
  std::sort(topGpuProcesses.begin(), topGpuProcesses.end(),
            [](const ProcessData& a, const ProcessData& b) {
              return a.gpuPercent > b.gpuPercent;
            });

  topGpuProcesses.erase(
    std::remove_if(topGpuProcesses.begin(), topGpuProcesses.end(),
                   [](const ProcessData& p) { return p.gpuPercent <= 0; }),
    topGpuProcesses.end());

  if (topGpuProcesses.size() > 5) {
    topGpuProcesses.resize(5);
  }
  result.topGpuProcesses = topGpuProcesses;

  result.formattedOutput = formatMonitoringResults(result);

  // Always clean up queries at the end
  cleanupQueries();

  // Check for cancellation one last time before expensive operations
  if (checkCancellation(worker)) {
    return result;
  }

  LOG_INFO << "==== BACKGROUND PROCESS MONITORING RESULTS ====";
  LOG_INFO << result.formattedOutput;

  // Check for cancellation before getAllProcessesDetails which is expensive
  if (!checkCancellation(worker)) {
    LOG_INFO << "==== ALL RUNNING PROCESSES DETAILS ====";
    LOG_INFO << getAllProcessesDetails();
  }

  // Store in data store only if not cancelled
  if (!checkCancellation(worker)) {
    storeMonitoringResultsInDataStore(result);
  }

  return result;
}

// Modified storeMonitoringResultsInDataStore function
void storeMonitoringResultsInDataStore(const MonitoringResult& result) {
  auto& dataStore = DiagnosticDataStore::getInstance();

  auto convertProcessData = [](const ProcessData& src)
    -> DiagnosticDataStore::BackgroundProcessData::ProcessInfo {
    DiagnosticDataStore::BackgroundProcessData::ProcessInfo dest;
    dest.name = wstringToString(src.name);
    dest.cpuPercent = src.cpuPercent;
    dest.peakCpuPercent = src.peakCpuPercent;
    dest.memoryUsageKB = src.memoryUsageKB;
    dest.gpuPercent = src.gpuPercent;
    dest.instanceCount = src.instanceCount;
    return dest;
  };

  std::vector<DiagnosticDataStore::BackgroundProcessData::ProcessInfo> topCpu;
  std::vector<DiagnosticDataStore::BackgroundProcessData::ProcessInfo> topMemory;
  std::vector<DiagnosticDataStore::BackgroundProcessData::ProcessInfo> topGpu;

  for (const auto& proc : result.topCpuProcesses) {
    topCpu.push_back(convertProcessData(proc));
  }

  for (const auto& proc : result.topMemoryProcesses) {
    topMemory.push_back(convertProcessData(proc));
  }

  for (const auto& proc : result.topGpuProcesses) {
    topGpu.push_back(convertProcessData(proc));
  }

  dataStore.updateBackgroundProcessData(
    result.totalCpuUsage, result.totalGpuUsage, result.systemDpcTime,
    result.systemInterruptTime, result.hasDpcLatencyIssues, topCpu, topMemory,
    topGpu, result.physicalTotalKB, result.physicalAvailableKB,
    result.commitTotalKB, result.commitLimitKB, result.kernelPagedKB,
    result.kernelNonPagedKB, result.systemCacheKB, result.userModePrivateKB,
    result.otherMemoryKB,
    result.peakSystemDpcTime,        // Pass peak DPC time
    result.peakSystemInterruptTime,  // Pass peak interrupt time
    result.peakCpuUsage,             // Pass peak CPU usage
    result.peakGpuUsage,             // Pass peak GPU usage
    result.totalDiskIO,              // Pass disk I/O usage
    result.peakDiskIO                // Pass peak disk I/O usage
  );
}

// Improved getAllProcessesDetails() with proper cleanup
std::string getAllProcessesDetails() {
  std::stringstream ss;
  ss << "===== All Running Processes =====\n\n";

  auto processes = getRunningProcesses();

  std::map<std::wstring, std::set<DWORD>> processGroups;
  for (const auto& [pid, name] : processes) {
    processGroups[name].insert(pid);
  }

  // Initialize NVIDIA metrics with proper cleanup
  NvidiaMetricsCollector nvCollector;
  std::map<DWORD, NvidiaProcessGPUMetrics> gpuProcessMetrics;
  bool nvmlInitialized = nvCollector.ensureInitialized();

  // Get GPU metrics if NVML initialized successfully
  if (nvmlInitialized) {
    auto gpus = nvCollector.getAvailableGPUs();
    if (!gpus.empty()) {
      std::vector<NvidiaProcessGPUMetrics> processMetrics;
      bool hasGpuMetrics =
        nvCollector.getGpuProcessUtilization(gpus[0], processMetrics);

      if (hasGpuMetrics) {
        for (const auto& metric : processMetrics) {
          gpuProcessMetrics[metric.pid] = metric;
        }
      }
    }
  }

  // PDH query for process metrics
  PDH_HQUERY query = NULL;
  PDH_STATUS status = PdhOpenQueryW(NULL, 0, &query);
  if (status != ERROR_SUCCESS) {
    return ss.str();  // Return early but no cleanup needed yet
  }

  // Use a lambda for cleanup to avoid code duplication
  auto cleanupQuery = [&]() {
    if (query) {
      PdhCloseQuery(query);
      query = NULL;
    }
  };

  // Rest of existing code with cleanup on early returns
  DWORD counterListSize = 0;
  DWORD instanceListSize = 0;

  status = PdhEnumObjectItemsW(NULL, NULL, L"Process", NULL, &counterListSize,
                               NULL, &instanceListSize, PERF_DETAIL_WIZARD, 0);

  if (status != PDH_MORE_DATA && status != ERROR_SUCCESS) {
    cleanupQuery();
    return ss.str();
  }

  std::vector<WCHAR> counterList(counterListSize + 2, 0);
  std::vector<WCHAR> instanceList(instanceListSize + 2, 0);

  status = PdhEnumObjectItemsW(NULL, NULL, L"Process", counterList.data(),
                               &counterListSize, instanceList.data(),
                               &instanceListSize, PERF_DETAIL_WIZARD, 0);

  if (status != ERROR_SUCCESS) {
    cleanupQuery();
    return ss.str();
  }

  std::vector<std::wstring> validInstances;
  WCHAR* instancePtr = instanceList.data();
  while (*instancePtr) {
    validInstances.push_back(instancePtr);
    instancePtr += wcslen(instancePtr) + 1;
  }

  struct ProcessData {
    std::wstring name;
    std::vector<PDH_HCOUNTER> cpuCounters;
    std::vector<PDH_HCOUNTER> memCounters;
    std::vector<std::wstring> instanceNames;
    double cpuPercent = 0.0;
    SIZE_T memoryKB = 0;
    int instanceCount = 0;
    std::set<DWORD> pids;
  };

  std::map<std::wstring, ProcessData> processDataMap;

  for (const auto& [name, pids] : processGroups) {
    ProcessData data;
    data.name = name;
    data.instanceCount = static_cast<int>(pids.size());
    data.pids = pids;

    std::wstring nameWithoutExe = stripExeSuffix(name);
    std::wstring lowerName = nameWithoutExe;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   ::tolower);
    std::wstring lowerNameWithExe = name;
    std::transform(lowerNameWithExe.begin(), lowerNameWithExe.end(),
                   lowerNameWithExe.begin(), ::tolower);

    for (const auto& instance : validInstances) {
      bool isMatch = false;

      if (instance == nameWithoutExe || instance == name) {
        isMatch = true;
      } else if (instance.find(nameWithoutExe + L"#") == 0 ||
                 instance.find(name + L"#") == 0) {
        isMatch = true;
      } else {
        std::wstring lowerInstance = instance;
        std::transform(lowerInstance.begin(), lowerInstance.end(),
                       lowerInstance.begin(), ::tolower);

        if (lowerInstance == lowerName || lowerInstance == lowerNameWithExe ||
            lowerInstance.find(lowerName + L"#") == 0 ||
            lowerInstance.find(lowerNameWithExe + L"#") == 0) {
          isMatch = true;
        }
      }

      if (isMatch) {
        data.instanceNames.push_back(instance);

        std::wstring cpuPath =
          L"\\Process(" + instance + L")\\% Processor Time";
        std::wstring memPath =
          L"\\Process(" + instance + L")\\Working Set - Private";

        PDH_HCOUNTER cpuCounter = NULL;
        PDH_HCOUNTER memCounter = NULL;

        if (PdhAddEnglishCounterW(query, cpuPath.c_str(), 0, &cpuCounter) ==
            ERROR_SUCCESS) {
          data.cpuCounters.push_back(cpuCounter);
        }

        if (PdhAddEnglishCounterW(query, memPath.c_str(), 0, &memCounter) ==
            ERROR_SUCCESS) {
          data.memCounters.push_back(memCounter);
        }
      }
    }

    if (data.instanceNames.empty()) {
      std::wstring fallbackName = nameWithoutExe;
      data.instanceNames.push_back(fallbackName);

      std::wstring cpuPath =
        L"\\Process(" + fallbackName + L")\\% Processor Time";
      std::wstring memPath =
        L"\\Process(" + fallbackName + L")\\Working Set - Private";

      PDH_HCOUNTER cpuCounter = NULL;
      PDH_HCOUNTER memCounter = NULL;

      if (PdhAddEnglishCounterW(query, cpuPath.c_str(), 0, &cpuCounter) ==
          ERROR_SUCCESS) {
        data.cpuCounters.push_back(cpuCounter);
      }

      if (PdhAddEnglishCounterW(query, memPath.c_str(), 0, &memCounter) ==
          ERROR_SUCCESS) {
        data.memCounters.push_back(memCounter);
      }
    }

    processDataMap[name] = data;
  }

  status = PdhCollectQueryData(query);
  if (status != ERROR_SUCCESS) {
    cleanupQuery();
    return ss.str();
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(BASELINE_WAIT_MS));

  status = PdhCollectQueryData(query);
  if (status != ERROR_SUCCESS) {
    cleanupQuery();
    return ss.str();
  }

  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  DWORD numProcessors = sysInfo.dwNumberOfProcessors;

  for (auto& [name, data] : processDataMap) {
    for (auto& cpuCounter : data.cpuCounters) {
      if (cpuCounter) {
        PDH_FMT_COUNTERVALUE value;
        DWORD valueType;
        status = PdhGetFormattedCounterValue(cpuCounter, PDH_FMT_DOUBLE,
                                             &valueType, &value);
        if (status == ERROR_SUCCESS) {
          data.cpuPercent += (value.doubleValue / numProcessors);
        }
      }
    }

    for (auto& memCounter : data.memCounters) {
      if (memCounter) {
        PDH_FMT_COUNTERVALUE value;
        DWORD valueType;
        status = PdhGetFormattedCounterValue(memCounter, PDH_FMT_LARGE,
                                             &valueType, &value);
        if (status == ERROR_SUCCESS) {
          data.memoryKB += static_cast<SIZE_T>(value.largeValue / 1024);
        }
      }
    }
  }

  std::vector<std::pair<std::wstring, ProcessData>> sortedProcesses;
  for (const auto& item : processDataMap) {
    sortedProcesses.push_back(item);
  }

  std::sort(sortedProcesses.begin(), sortedProcesses.end(),
            [](const auto& a, const auto& b) {
              return a.second.cpuPercent > b.second.cpuPercent;
            });

  // When outputting process info, include the GPU metrics if available
  for (const auto& [name, data] : sortedProcesses) {
    ss << "  • " << wstringToString(data.name);
    if (data.instanceCount > 1) {
      ss << " (" << data.instanceCount << " instances)";
    }
    ss << "\n    CPU: " << std::fixed << std::setprecision(2) << data.cpuPercent
       << "% | ";
    ss << "Memory: " << (data.memoryKB / 1024) << " MB";

    // Check if any of the PIDs have GPU metrics
    bool hasGpuInfoForProcess = false;
    double gpuPercent = 0.0;
    double gpuCompute = 0.0;
    double gpuEncode = 0.0;
    double gpuMemoryMB = 0.0;

    if (nvmlInitialized) {
      for (DWORD pid : data.pids) {
        if (gpuProcessMetrics.count(pid) > 0) {
          const auto& metric = gpuProcessMetrics[pid];
          gpuPercent =
            std::max(gpuPercent, static_cast<double>(metric.gpuUtilization));
          gpuCompute = std::max(gpuCompute,
                                static_cast<double>(metric.computeUtilization));
          gpuEncode =
            std::max(gpuEncode, static_cast<double>(metric.encoderUtilization));
          gpuMemoryMB =
            std::max(gpuMemoryMB,
                     static_cast<double>(metric.memoryUsed) / (1024 * 1024));
          hasGpuInfoForProcess = true;
        }
      }
    }

    // Display GPU metrics if available from NVML
    if (hasGpuInfoForProcess) {
      if (gpuPercent > 0) {
        ss << " | GPU: " << std::fixed << std::setprecision(1) << gpuPercent
           << "%";
      }
      if (gpuCompute > 0) {
        ss << " | GPU Compute: " << std::fixed << std::setprecision(1)
           << gpuCompute << "%";
      }
      if (gpuEncode > 0) {
        ss << " | GPU Encode: " << std::fixed << std::setprecision(1)
           << gpuEncode << "%";
      }
      if (gpuMemoryMB > 0) {
        ss << " | GPU Memory: " << std::fixed << std::setprecision(1)
           << gpuMemoryMB << " MB";
      }
    }
    // Fall back to heuristics if no NVML data
    else {
      double estimatedGpuPercent = 0.0;
      if (name.find(L"chrome") != std::wstring::npos ||
          name.find(L"edge") != std::wstring::npos ||
          name.find(L"firefox") != std::wstring::npos) {
        estimatedGpuPercent = 2.0;
      } else if (name.find(L"obs") != std::wstring::npos ||
                 name.find(L"streamlabs") != std::wstring::npos) {
        estimatedGpuPercent = 4.0;
      } else if (name.find(L"nvidia") != std::wstring::npos ||
                 name.find(L"amd") != std::wstring::npos) {
        estimatedGpuPercent = 3.0;
      } else if (name.find(L"game") != std::wstring::npos ||
                 name.find(L"steam") != std::wstring::npos ||
                 name.find(L"battle") != std::wstring::npos ||
                 name.find(L"epic") != std::wstring::npos) {
        estimatedGpuPercent = 2.5;
      }

      if (estimatedGpuPercent > 0) {
        ss << " | GPU: " << std::fixed << std::setprecision(1)
           << estimatedGpuPercent << "% (estimated)";
      }
    }

    ss << "\n    PIDs: ";
    bool first = true;
    for (DWORD pid : data.pids) {
      if (!first) ss << ", ";
      ss << pid;
      first = false;
    }
    ss << "\n";
  }

  // Always clean up before returning
  cleanupQuery();
  return ss.str();
}

// Format the monitoring results with added memory metrics.
std::string formatMonitoringResults(const MonitoringResult& results) {
  std::stringstream ss;
  ss << "===== Background Process Monitor Results =====\n\n";
  ss << "System Resource Usage:\n";
  ss << "  CPU Usage: " << std::fixed << std::setprecision(2)
     << results.totalCpuUsage << "% (avg)";
  if (results.peakCpuUsage > 0) {
    ss << ", Peak: " << std::fixed << std::setprecision(2)
       << results.peakCpuUsage << "%";
  }
  ss << "\n";
  ss << "  DPC Time: " << std::fixed << std::setprecision(2)
     << results.systemDpcTime << "% (avg)";
  if (results.peakSystemDpcTime > 0) {
    ss << ", Peak: " << std::fixed << std::setprecision(2)
       << results.peakSystemDpcTime << "%";
  }
  ss << "\n";
  ss << "  Interrupt Time: " << std::fixed << std::setprecision(2)
     << results.systemInterruptTime << "% (avg)";
  if (results.peakSystemInterruptTime > 0) {
    ss << ", Peak: " << std::fixed << std::setprecision(2)
       << results.peakSystemInterruptTime << "%";
  }
  ss << "\n";

  // Only include GPU info if we have valid data
  if (results.totalGpuUsage > 0 && results.totalGpuUsage <= 100.0) {
    ss << "  GPU Usage: " << std::fixed << std::setprecision(2)
       << results.totalGpuUsage << "% (avg)";
    if (results.peakGpuUsage > 0) {
      ss << ", Peak: " << std::fixed << std::setprecision(2)
         << results.peakGpuUsage << "%";
    }
    ss << "\n";
  }

  // Add disk I/O information
  if (results.totalDiskIO >= 0) {
    ss << "  Disk I/O: " << std::fixed << std::setprecision(2)
       << results.totalDiskIO << " MB/s (avg)";
    if (results.peakDiskIO > 0) {
      ss << ", Peak: " << std::fixed << std::setprecision(2)
         << results.peakDiskIO << " MB/s";
    }
    ss << "\n";
  }

  // Add memory metrics if available
  if (results.physicalTotalKB > 0) {
    // Physical memory
    double physicalTotalGB = results.physicalTotalKB / (1024.0 * 1024.0);
    double physicalAvailableGB =
      results.physicalAvailableKB / (1024.0 * 1024.0);
    double physicalUsedGB = physicalTotalGB - physicalAvailableGB;
    double physicalUsedPercent = (physicalUsedGB / physicalTotalGB) * 100.0;

    ss << "\nMemory Usage:\n";
    ss << "  RAM: " << std::fixed << std::setprecision(1) << physicalUsedGB
       << " GB / " << std::fixed << std::setprecision(1) << physicalTotalGB
       << " GB (" << std::fixed << std::setprecision(1) << physicalUsedPercent
       << "%)\n";

    // Committed memory
    if (results.commitTotalKB > 0 && results.commitLimitKB > 0) {
      double commitTotalGB = results.commitTotalKB / (1024.0 * 1024.0);
      double commitLimitGB = results.commitLimitKB / (1024.0 * 1024.0);
      double commitPercent = (commitTotalGB / commitLimitGB) * 100.0;

      ss << "  Committed: " << std::fixed << std::setprecision(1)
         << commitTotalGB << " GB / " << std::fixed << std::setprecision(1)
         << commitLimitGB << " GB (" << std::fixed << std::setprecision(1)
         << commitPercent << "%)\n";
    }

    // Kernel memory
    if (results.kernelPagedKB > 0 || results.kernelNonPagedKB > 0) {
      double kernelPagedMB = results.kernelPagedKB / 1024.0;
      double kernelNonPagedMB = results.kernelNonPagedKB / 1024.0;
      double kernelTotalMB = kernelPagedMB + kernelNonPagedMB;

      ss << "  Kernel / Driver: " << std::fixed << std::setprecision(1)
         << kernelTotalMB << " MB (" << std::fixed << std::setprecision(1)
         << kernelPagedMB << " MB paged, " << std::fixed << std::setprecision(1)
         << kernelNonPagedMB << " MB non-paged)\n";
    }

    // File cache
    if (results.systemCacheKB > 0) {
      double systemCacheMB = results.systemCacheKB / 1024.0;
      ss << "  File Cache: " << std::fixed << std::setprecision(1)
         << systemCacheMB << " MB\n";
    }

    // User mode private + other memory
    if (results.userModePrivateKB > 0) {
      double userModePrivateMB = results.userModePrivateKB / 1024.0;
      ss << "  User-mode Private: " << std::fixed << std::setprecision(1)
         << userModePrivateMB << " MB\n";

      if (results.otherMemoryKB > 0) {
        double otherMemoryMB = results.otherMemoryKB / 1024.0;
        ss << "  Other: " << std::fixed << std::setprecision(1) << otherMemoryMB
           << " MB (driver DMA, firmware, HW reservations, etc.)\n";
      }
    }
  }
  ss << "\n";

  if (results.hasDpcLatencyIssues) {
    ss << "⚠️ HIGH DPC/INTERRUPT LATENCY DETECTED!\n";
    ss << "   This may indicate driver issues causing stuttering.\n\n";
  }

  ss << "All Detected Processes:\n";
  if (!results.processes.empty()) {
    std::vector<ProcessData> sortedProcesses = results.processes;
    std::sort(sortedProcesses.begin(), sortedProcesses.end(),
              [](const ProcessData& a, const ProcessData& b) {
                return a.cpuPercent > b.cpuPercent;
              });

    for (const auto& proc : sortedProcesses) {
      ss << "  • " << wstringToString(proc.name);
      if (proc.instanceCount > 1) {
        ss << " (" << proc.instanceCount << " instances)";
      }
      ss << "\n    CPU: " << std::fixed << std::setprecision(2)
         << proc.cpuPercent << "% ";
      if (proc.peakCpuPercent > proc.cpuPercent * 1.2) {
        ss << "(Peak: " << std::fixed << std::setprecision(2)
           << proc.peakCpuPercent << "%) ";
      }
      ss << "| Memory: " << (proc.memoryUsageKB / 1024) << " MB";
      if (proc.gpuPercent > 0) {
        ss << " | GPU: " << std::fixed << std::setprecision(1)
           << proc.gpuPercent << "%";
      }
      ss << "\n";
    }
  } else {
    ss << "  No processes detected\n";
  }
  ss << "\n";

  // Add sections for top consumers (memory, CPU, GPU)
  // Memory consumers
  ss << "Top 5 Memory Consumers:\n";
  if (!results.topMemoryProcesses.empty()) {
    for (const auto& proc : results.topMemoryProcesses) {
      ss << "  • " << wstringToString(proc.name);
      if (proc.instanceCount > 1) {
        ss << " (" << proc.instanceCount << " instances)";
      }
      ss << "\n    Memory: " << (proc.memoryUsageKB / 1024) << " MB | ";
      ss << "CPU: " << std::fixed << std::setprecision(2) << proc.cpuPercent
         << "% | ";
      ss << "GPU: " << std::fixed << std::setprecision(1) << proc.gpuPercent
         << "%\n";
    }
  } else {
    ss << "  No memory consuming processes detected\n";
  }
  ss << "\n";

  // CPU consumers
  ss << "Top 5 CPU Consumers:\n";
  if (!results.topCpuProcesses.empty()) {
    for (const auto& proc : results.topCpuProcesses) {
      ss << "  • " << wstringToString(proc.name);
      if (proc.instanceCount > 1) {
        ss << " (" << proc.instanceCount << " instances)";
      }
      ss << "\n    CPU: " << std::fixed << std::setprecision(2)
         << proc.cpuPercent << "% ";
      if (proc.peakCpuPercent > proc.cpuPercent * 1.2) {
        ss << "(Peak: " << std::fixed << std::setprecision(2)
           << proc.peakCpuPercent << "%) ";
      }
      ss << "\n    Memory: " << (proc.memoryUsageKB / 1024) << " MB\n";
    }
  } else {
    ss << "  No CPU consuming processes detected\n";
  }
  ss << "\n";

  // GPU consumers
  ss << "Top 5 Estimated GPU Consumers:\n";
  if (!results.topGpuProcesses.empty()) {
    for (const auto& proc : results.topGpuProcesses) {
      ss << "  • " << wstringToString(proc.name);
      if (proc.instanceCount > 1) {
        ss << " (" << proc.instanceCount << " instances)";
      }
      ss << "\n    ";

      if (proc.gpuComputePercent > 0) {
        ss << "GPU Compute: " << std::fixed << std::setprecision(1)
           << proc.gpuComputePercent << "% | ";
      } else if (proc.gpuPercent > 0) {
        ss << "GPU: " << std::fixed << std::setprecision(1) << proc.gpuPercent
           << "% | ";
      }

      if (proc.gpuEncoderPercent > 0) {
        ss << "GPU Encoder: " << std::fixed << std::setprecision(1)
           << proc.gpuEncoderPercent << "% | ";
      }

      // Only show GPU memory if it's a reasonable value (less than 32GB)
      if (proc.gpuMemoryMB > 0 && proc.gpuMemoryMB < 32768) {
        ss << "GPU Memory: " << std::fixed << std::setprecision(1)
           << proc.gpuMemoryMB << " MB | ";
      }

      ss << "CPU: " << std::fixed << std::setprecision(2) << proc.cpuPercent
         << "% | ";
      ss << "Memory: " << (proc.memoryUsageKB / 1024) << " MB\n";
    }
  } else {
    ss << "  No GPU consuming processes detected\n";
  }
  ss << "\n";
  return ss.str();
}

// New function to gather memory metrics
void getMemoryMetrics(MonitoringResult& result, SIZE_T sumPrivateWorkingSetKB) {
  PERFORMANCE_INFORMATION perfInfo = {0};
  perfInfo.cb = sizeof(PERFORMANCE_INFORMATION);

  if (GetPerformanceInfo(&perfInfo, sizeof(PERFORMANCE_INFORMATION))) {
    DWORD pageSize = perfInfo.PageSize;
    SIZE_T pageKB = pageSize / 1024;

    // Calculate available physical memory
    MEMORYSTATUSEX memStatus = {0};
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memStatus);
    uint64_t availPhysKB = memStatus.ullAvailPhys / 1024;

    // Store the metrics in result
    result.physicalTotalKB = perfInfo.PhysicalTotal * pageKB;
    result.physicalAvailableKB = availPhysKB;
    result.commitTotalKB = perfInfo.CommitTotal * pageKB;
    result.commitLimitKB = perfInfo.CommitLimit * pageKB;
    result.kernelPagedKB = perfInfo.KernelPaged * pageKB;
    result.kernelNonPagedKB = perfInfo.KernelNonpaged * pageKB;
    result.systemCacheKB = perfInfo.SystemCache * pageKB;
    result.userModePrivateKB = sumPrivateWorkingSetKB;

    // Calculate unaccounted/other memory - MODIFIED: exclude the cache from
    // accounting
    uint64_t physUsedKB = perfInfo.PhysicalTotal * pageKB - availPhysKB;
    uint64_t userKB = sumPrivateWorkingSetKB;
    uint64_t kernelKB =
      (perfInfo.KernelPaged + perfInfo.KernelNonpaged) * pageKB;
    // Don't subtract cacheKB when calculating unknownKB
    uint64_t unknownKB =
      physUsedKB > (userKB + kernelKB) ? (physUsedKB - (userKB + kernelKB)) : 0;

    result.otherMemoryKB = unknownKB;

    // Debug output
    LOG_INFO << "Memory metrics:";
    LOG_INFO << "  Physical Total: " << (result.physicalTotalKB / 1024) << " MB";
    LOG_INFO << "  Physical Available: " << (result.physicalAvailableKB / 1024) << " MB";
    LOG_INFO << "  Commit Total: " << (result.commitTotalKB / 1024) << " MB";
    LOG_INFO << "  Commit Limit: " << (result.commitLimitKB / 1024) << " MB";
    LOG_INFO << "  Kernel Paged: " << (result.kernelPagedKB / 1024) << " MB";
    LOG_INFO << "  Kernel Non-Paged: " << (result.kernelNonPagedKB / 1024) << " MB";
    LOG_INFO << "  System Cache: " << (result.systemCacheKB / 1024) << " MB";
    LOG_INFO << "  User-mode Private: " << (result.userModePrivateKB / 1024) << " MB";
    LOG_INFO << "  Other Memory: " << (result.otherMemoryKB / 1024) << " MB";
  } else {
    LOG_ERROR << "Error getting performance information: " << GetLastError();
  }
}
}  // namespace BackgroundProcessMonitor
