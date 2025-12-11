#pragma once
#include <cstdint>
#include <map>
#include <vector>

struct BenchmarkDataPoint {
  // =====================================================================================
  // === PRESENTMON (ETW) METRICS - Frame Timing and Presentation ===
  // =====================================================================================
  
  // Real-time frame metrics (instant values from PresentMon)
  float fps = 0.0f;                      // Frames per second (1000/frametime) - current average
  float frameTime = 0.0f;                // Frame time in milliseconds - current average
  float maxFrameTime = 0.0f;             // [DEPRECATED] Use highestFrameTime instead
  float gpuRenderTime = 0.0f;            // GPU render time in ms - current average
  float cpuRenderTime = 0.0f;            // CPU render time in ms - current average  
  float appRenderTime = 0.0f;            // Application render time in ms - current average
  
  // Display resolution metrics (from PresentMon)
  unsigned int destWidth = 0;            // Display/render target width in pixels
  unsigned int destHeight = 0;           // Display/render target height in pixels
  
  // PresentMon statistical metrics (calculated from rolling 1-second window)
  float lowFps1Percent = 0.0f;           // 1% low FPS: FPS value that 99% of frames exceed (worst 1% performance)
  float lowFps5Percent = 0.0f;           // 5% low FPS: FPS value that 95% of frames exceed (worst 5% performance)  
  float lowFps05Percent = 0.0f;          // 0.5% low FPS: FPS value that 99.5% of frames exceed (worst 0.5% performance)
  float highestFrameTime = 0.0f;         // Highest single frametime recorded in rolling window (worst frame spike)
  float highest5PctFrameTime = 0.0f;     // Highest 5% frametime for this second (95th percentile per-second metric)
  float highestGpuTime = 0.0f;           // Highest GPU render time recorded in rolling window (worst GPU spike)
  float highestCpuTime = 0.0f;           // Highest CPU render time recorded in rolling window (worst CPU spike)
  float fpsVariance = 0.0f;              // Standard deviation of frame times (frame consistency measure)

  // =====================================================================================
  // === NVIDIA GPU METRICS - Temperature, Utilization, Memory ===
  // =====================================================================================
  unsigned int gpuTemp = 0;              // GPU temperature in Celsius
  unsigned int gpuUtilization = 0;       // GPU utilization percentage
  unsigned int gpuMemUtilization = 0;    // Memory utilization percentage
  unsigned int gpuPower = 0;             // Power usage in milliwatts
  unsigned int gpuClock = 0;             // GPU clock in MHz
  unsigned int gpuMemClock = 0;          // Memory clock in MHz
  unsigned int gpuFanSpeed = 0;          // Fan speed percentage
  bool gpuThrottling = false;            // Thermal throttling status
  unsigned long long gpuMemTotal = 0;    // Total GPU memory in bytes
  unsigned long long gpuMemUsed = 0;     // Used GPU memory in bytes
  unsigned int gpuSmUtilization = 0;     // SM utilization percentage
  unsigned int gpuMemBandwidthUtil = 0;  // Memory bandwidth utilization
  unsigned int gpuPcieRxThroughput = 0;  // PCIe receive throughput
  unsigned int gpuPcieTxThroughput = 0;  // PCIe transmit throughput
  unsigned int gpuNvdecUtil = 0;         // NVDEC utilization
  unsigned int gpuNvencUtil = 0;         // NVENC utilization

  // =====================================================================================
  // === PDH INTERFACE METRICS - Windows Performance Counters ===
  // =====================================================================================
  
  // --- PDH CPU Usage Metrics ---
  double procProcessorTime = -1.0;       // % Processor Time (total)
  double procUserTime = -1.0;            // % User Time (total)
  double procPrivilegedTime = -1.0;      // % Privileged Time (total)
  double procIdleTime = -1.0;            // % Idle Time (total)
  std::vector<double> perCoreCpuUsagePdh; // Per-core CPU usage from PDH
  
  // --- PDH CPU Frequency Metrics ---
  double procActualFreq = -1.0;          // Actual Frequency in MHz (total)
  std::vector<double> perCoreActualFreq; // Per-core actual frequencies from PDH
  
  // --- PDH CPU Interrupt Metrics ---
  double cpuInterruptsPerSec = -1.0;     // Interrupts/sec from CPU counter
  double cpuDpcTime = -1.0;              // % DPC Time
  double cpuInterruptTime = -1.0;        // % Interrupt Time  
  double cpuDpcsQueuedPerSec = -1.0;     // DPCs Queued/sec
  double cpuDpcRate = -1.0;              // DPC Rate
  
  // --- PDH CPU Power State Metrics ---
  double cpuC1Time = -1.0;               // % C1 Time
  double cpuC2Time = -1.0;               // % C2 Time
  double cpuC3Time = -1.0;               // % C3 Time
  double cpuC1TransitionsPerSec = -1.0;  // C1 Transitions/sec
  double cpuC2TransitionsPerSec = -1.0;  // C2 Transitions/sec
  double cpuC3TransitionsPerSec = -1.0;  // C3 Transitions/sec
  
  // --- PDH Memory System Metrics ---
  double availableMemoryMB = 0.0;        // Available physical memory in MB
  double memoryLoad = 0.0;               // System memory usage percentage (calculated)
  double memoryCommittedBytes = -1.0;    // Committed Bytes
  double memoryCommitLimit = -1.0;       // Commit Limit
  double memoryFaultsPerSec = 0.0;       // Page faults per second
  double memoryPagesPerSec = -1.0;       // Pages/sec
  double memoryPoolNonPagedBytes = -1.0; // Pool Nonpaged Bytes
  double memoryPoolPagedBytes = -1.0;    // Pool Paged Bytes
  double memorySystemCodeBytes = -1.0;   // System Code Total Bytes
  double memorySystemDriverBytes = -1.0; // System Driver Total Bytes
  
  // --- PDH Disk I/O Metrics ---
  double ioReadRateMBs = 0.0;            // Current IO read rate in MB/s (from PDH)
  double ioWriteRateMBs = 0.0;           // Current IO write rate in MB/s (from PDH)
  double diskReadsPerSec = -1.0;         // Disk Reads/sec
  double diskWritesPerSec = -1.0;        // Disk Writes/sec
  double diskTransfersPerSec = -1.0;     // Disk Transfers/sec
  double diskBytesPerSec = -1.0;         // Disk Bytes/sec
  double diskAvgReadQueueLength = -1.0;  // Avg. Disk Read Queue Length
  double diskAvgWriteQueueLength = -1.0; // Avg. Disk Write Queue Length
  double diskAvgQueueLength = -1.0;      // Avg. Disk Queue Length
  double diskAvgReadTime = -1.0;         // Avg. Disk sec/Read
  double diskAvgWriteTime = -1.0;        // Avg. Disk sec/Write
  double diskAvgTransferTime = -1.0;     // Avg. Disk sec/Transfer
  double diskPercentTime = -1.0;         // % Disk Time
  double diskPercentReadTime = -1.0;     // % Disk Read Time
  double diskPercentWriteTime = -1.0;    // % Disk Write Time
  std::map<std::string, double> perDiskPercentTime;     // % Disk Time per drive
  std::map<std::string, double> perDiskPercentReadTime; // % Disk Read Time per drive
  std::map<std::string, double> perDiskPercentWriteTime;// % Disk Write Time per drive
  std::map<std::string, double> perDiskPercentIdleTime; // % Idle Time per drive
  
  // --- PDH System Kernel Metrics ---
  double contextSwitchesPerSec = -1.0;   // Context switches per second
  double systemProcessorQueueLength = -1.0; // Processor Queue Length
  double systemProcesses = -1.0;         // Processes
  double systemThreads = -1.0;           // Threads
  double pdhInterruptsPerSec = -1.0;     // System calls/sec (legacy compatibility)

  // =====================================================================================
  // === CPU KERNEL TRACKER (ETW) METRICS - Low-level CPU Activity ===
  // =====================================================================================
  double interruptsPerSec = -1.0;        // Interrupts/sec from ETW
  double dpcCountPerSec = -1.0;          // DPCs/sec from ETW
  double avgDpcLatencyUs = 0.0;          // Average DPC latency in microseconds
  double dpcLatenciesAbove50us = 0.0;    // Percentage of DPCs with latency > 50μs
  double dpcLatenciesAbove100us = 0.0;   // Percentage of DPCs with latency > 100μs
  double voluntaryContextSwitchesPerSec = -1.0;   // Voluntary context switches/sec
  double involuntaryContextSwitchesPerSec = -1.0; // Involuntary context switches/sec
  double highPriorityInterruptionsPerSec = -1.0;  // High priority interruptions/sec
  double priorityInversionsPerSec = -1.0;         // Priority inversions/sec
  double avgThreadWaitTimeMs = 0.0;               // Average thread wait time in ms

  // =====================================================================================
  // === DISK PERFORMANCE TRACKER METRICS - Latency and Throughput ===
  // =====================================================================================
  double ioReadMB = 0.0;                 // Total IO read in MB
  double ioWriteMB = 0.0;                // Total IO write in MB
  double ioReadDeltaMB = 0.0;            // IO read since last update in MB
  double ioWriteDeltaMB = 0.0;           // IO write since last update in MB
  double diskReadLatencyMs = 0.0;        // Disk read latency in ms
  double diskWriteLatencyMs = 0.0;       // Disk write latency in ms
  double diskQueueLength = 0.0;          // Current disk queue length
  double avgDiskQueueLength = 0.0;       // Average disk queue length
  double maxDiskQueueLength = 0.0;       // Maximum disk queue length
  double minDiskReadLatencyMs = -1.0;    // Minimum disk read latency
  double maxDiskReadLatencyMs = -1.0;    // Maximum disk read latency
  double minDiskWriteLatencyMs = -1.0;   // Minimum disk write latency
  double maxDiskWriteLatencyMs = -1.0;   // Maximum disk write latency
  std::map<std::string, double> perDiskReadRates;  // Per-disk read rates
  std::map<std::string, double> perDiskWriteRates; // Per-disk write rates

  // =====================================================================================
  // === METADATA ===
  // =====================================================================================
  int64_t timestamp = 0;                 // Timestamp in seconds
  int presentCount = 0;                  // Number of presentation samples
  int processCount = 0;                  // Number of process samples

};
