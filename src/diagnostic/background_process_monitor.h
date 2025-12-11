#pragma once

#include <map>
#include <string>
#include <vector>

#include <Windows.h>
#include <dxgi.h>
#include <pdh.h>
#include <psapi.h>

// Forward declaration of BackgroundProcessWorker for the cancellation check
class BackgroundProcessWorker;

namespace BackgroundProcessMonitor {

struct ProcessData {
  std::wstring name;
  std::wstring path;
  double cpuPercent = 0;      // Average CPU usage
  double peakCpuPercent = 0;  // Peak CPU usage during monitoring
  SIZE_T memoryUsageKB = 0;
  double diskIOBytesPerSec = 0;
  double gpuPercent = 0;
  double gpuComputePercent = 0;  // GPU compute cores usage
  double gpuMemoryMB = 0;        // Dedicated GPU memory usage in MB
  double gpuEncoderPercent = 0;  // NVENC/encoder usage if available
  bool isSystem = false;
  bool isPotentialIssue = false;
  bool isDpcSource = false;        // Potential DPC latency source
  bool isInterruptSource = false;  // Potential interrupt source
  int cpuSpikeCount = 0;           // Number of CPU usage spikes
  std::vector<double>
    cpuSamples;  // Individual CPU measurements for spike detection
  bool exceedsTwoPctCpu = false;  // Process exceeded 2% CPU in any sample
  bool isHighMemory = false;      // Process uses more than 500MB memory
  int instanceCount = 1;          // For grouping similar processes
};

struct MonitoringResult {
  bool hasHighCpuProcesses = false;
  bool hasHighGpuProcesses = false;
  bool hasHighMemoryProcesses = false;
  bool hasHighDiskIOProcesses = false;
  bool hasDpcLatencyIssues = false;
  double systemDpcTime = 0.0;
  double systemInterruptTime = 0.0;
  double peakSystemDpcTime = 0.0;        // Add peak DPC time tracking
  double peakSystemInterruptTime = 0.0;  // Add peak interrupt time tracking
  double totalCpuUsage = 0.0;
  double peakCpuUsage = 0.0;  // Add peak CPU usage tracking
  double totalGpuUsage = 0.0;
  double peakGpuUsage = 0.0;  // Add peak GPU usage tracking
  double totalDiskIO = 0.0;
  double peakDiskIO = 0.0;  // Add peak disk I/O tracking

  // Added memory metrics
  uint64_t physicalTotalKB = 0;      // Total physical RAM in KB
  uint64_t physicalAvailableKB = 0;  // Available physical RAM in KB
  uint64_t commitTotalKB = 0;        // Committed virtual memory in KB
  uint64_t commitLimitKB = 0;        // Commit limit in KB
  uint64_t kernelPagedKB = 0;        // Kernel paged pool in KB
  uint64_t kernelNonPagedKB = 0;     // Kernel non-paged pool in KB
  uint64_t systemCacheKB = 0;        // System cache resident in KB
  uint64_t userModePrivateKB = 0;  // Sum of process private working sets in KB
  uint64_t otherMemoryKB = 0;      // Unaccounted memory in KB

  std::vector<ProcessData> processes;
  std::vector<ProcessData> systemProcesses;
  std::vector<ProcessData> interruptingProcesses;  // DPC/interrupt sources
  std::vector<ProcessData>
    twoPctCpuProcesses;  // Processes that sometimes use >2% CPU
  std::vector<ProcessData>
    highMemoryProcesses;  // Processes using >500MB memory
  std::vector<ProcessData>
    dpcInterruptProcesses;  // Grouped DPC/interrupt processes
  std::string formattedOutput;
  std::vector<ProcessData> topCpuProcesses;     // Top 5 CPU consumers
  std::vector<ProcessData> topMemoryProcesses;  // Top 5 memory consumers
  std::vector<ProcessData> topGpuProcesses;     // Top 5 GPU consumers
};

// Main function to monitor background processes
MonitoringResult monitorBackgroundProcesses(
  int durationSeconds = 15, BackgroundProcessWorker* worker = nullptr);

// Helper functions
std::vector<DWORD> getRunningProcessIds();
ProcessData collectProcessData(DWORD processId);
bool isKnownGameImpactingProcess(const std::wstring& processName);
double calculateProcessCpuUsage(HANDLE hProcess, ULARGE_INTEGER& lastCPU,
                                ULARGE_INTEGER& lastSysCPU,
                                ULARGE_INTEGER& lastUserCPU);
double getProcessGpuUsage(DWORD processId);
double getSystemDpcTime();
double getSystemInterruptTime();
std::wstring getProcessNameFromId(DWORD processId);
std::wstring getProcessPathFromId(DWORD processId);
std::string getAllProcessesDetails();

// Format results as a string for display
std::string formatMonitoringResults(const MonitoringResult& results);

// New function to check for cancellation
bool checkCancellation(BackgroundProcessWorker* worker);

// Just declare this variable, it's defined in the cpp file
extern int durationSeconds;
}  // namespace BackgroundProcessMonitor
