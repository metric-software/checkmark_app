# System Metrics Providers Documentation

This document provides a comprehensive overview of all metrics providers used in the Windows Benchmark application, their purposes, and the complete list of metrics each provider can collect.

## Metrics Providers Overview

### 1. **ConstantSystemInfo** 
**Purpose**: Provides static hardware and system configuration information that doesn't change during runtime.
**Usage**: System identification, hardware specifications, and configuration validation.

### 2. **PdhInterface** (Performance Data Helper)
**Purpose**: Collects real-time system performance metrics using Windows Performance Data Helper (PDH) API. Modular and reliable.
**Usage**: Live performance monitoring during benchmarks and diagnostics run, CPU/memory/disk usage tracking. The set of metrics to collect can be chosen, to reduce overhead.

### 3. **WinHardwareMonitor**
**Purpose**: Some other hardware/system metrics. but most of the system metrics are currently broken (like cpu temp, power and voltage) and they cannot be fixed for now.
**Usage**: Should mostly be used just for the constantsysteminfo during startup. (Some other metrics can be called from here. but mostly one of the actual live metrics providers should be prefered)

### 4. **CPUKernelMetricsTracker**
**Purpose**: Specialized kernel-level CPU metrics collection.
**Usage**: Low-level CPU performance analysis, interrupt tracking, context switches. (A bit unreliable for now, so PDH metrics should be used for duplicate data for now.)

### 5. **DiskPerformanceTracker**
**Purpose**: Disk I/O performance monitoring with latency analysis.
**Usage**: Storage performance benchmarking, I/O bottleneck identification. (A bit unreliable for now, so PDH metrics should be used for duplicate data for now.)

### 6. **NvidiaMetrics**
**Purpose**: NVIDIA GPU-specific metrics collection.
**Usage**: GPU performance monitoring, VRAM usage, GPU utilization tracking.

### 7. **SystemWrapper**
**Purpose**: Windows system API wrapper for miscellaneous system information.
**Usage**: Operating system details, system capabilities verification. (Should mostly be used just for the constantsysteminfo during startup.)

---

## Detailed Metrics by Provider

### ConstantSystemInfo

#### CPU Information
- `cpuName` - Processor model name
- `cpuVendor` - CPU manufacturer (Intel, AMD, etc.)
- `cpuArchitecture` - CPU architecture (x86, x64, ARM64)
- `physicalCores` - Number of physical CPU cores
- `logicalCores` - Number of logical CPU cores (including hyperthreading)
- `cpuSocket` - CPU socket type
- `baseClockMHz` - Base CPU clock speed in MHz
- `maxClockMHz` - Maximum CPU clock speed in MHz
- `l1CacheKB` - L1 cache size in KB
- `l2CacheKB` - L2 cache size in KB
- `l3CacheKB` - L3 cache size in KB
- `hyperThreadingEnabled` - Hyperthreading status
- `virtualizationEnabled` - Virtualization support status
- `avxSupport` - AVX instruction set support
- `avx2Support` - AVX2 instruction set support

#### Memory Information
- `totalPhysicalMemoryMB` - Total RAM in MB
- `memoryType` - Memory type (DDR4, DDR5, etc.)
- `memoryClockMHz` - Memory speed in MHz
- `xmpEnabled` - XMP profile status
- `memoryChannelConfig` - Memory channel configuration
- `memoryModules[]` - Array of memory module details:
  - `capacityGB` - Module capacity in GB
  - `speedMHz` - Module speed in MHz
  - `configuredSpeedMHz` - Actual configured speed
  - `manufacturer` - Module manufacturer
  - `partNumber` - Part number
  - `memoryType` - Module type
  - `deviceLocator` - Physical slot location
  - `formFactor` - Form factor (DIMM, SO-DIMM)
  - `bankLabel` - Bank label

#### GPU Information
- `gpuDevices[]` - Array of GPU devices:
  - `name` - GPU model name
  - `deviceId` - Device ID
  - `memoryMB` - VRAM in MB
  - `driverVersion` - Driver version
  - `driverDate` - Driver date
  - `hasGeForceExperience` - GeForce Experience status
  - `vendor` - GPU vendor
  - `pciLinkWidth` - PCIe link width
  - `pcieLinkGen` - PCIe generation
  - `isPrimary` - Primary GPU flag

#### Motherboard Information
- `motherboardManufacturer` - Motherboard manufacturer
- `motherboardModel` - Motherboard model
- `chipsetModel` - Chipset model
- `chipsetDriverVersion` - Chipset driver version
- `biosVersion` - BIOS version
- `biosDate` - BIOS date
- `biosManufacturer` - BIOS manufacturer

#### Storage Information
- `drives[]` - Array of storage drives:
  - `path` - Drive path
  - `model` - Drive model
  - `serialNumber` - Serial number
  - `interfaceType` - Interface type (SATA, NVMe, etc.)
  - `totalSpaceGB` - Total capacity in GB
  - `freeSpaceGB` - Free space in GB
  - `isSystemDrive` - System drive flag
  - `isSSD` - SSD flag

#### Power & System Settings
- `powerPlan` - Current power plan
- `powerPlanHighPerf` - High performance power plan status
- `gameMode` - Windows Game Mode status
- `pageFileExists` - Page file existence
- `pageFileSystemManaged` - System managed page file
- `pageTotalSizeMB` - Total page file size in MB
- `pagePrimaryDriveLetter` - Primary page file drive
- `pageFileLocations[]` - Page file locations
- `pageFileCurrentSizesMB[]` - Current page file sizes
- `pageFileMaxSizesMB[]` - Maximum page file sizes

#### Operating System Information
- `osVersion` - OS version string
- `osBuildNumber` - OS build number
- `isWindows11` - Windows 11 flag
- `systemName` - Computer name

#### Monitor Information
- `monitors[]` - Array of connected monitors:
  - `deviceName` - Device name
  - `displayName` - Display name
  - `width` - Resolution width
  - `height` - Resolution height
  - `refreshRate` - Refresh rate in Hz
  - `isPrimary` - Primary monitor flag

#### Driver Information
- `chipsetDrivers[]` - Chipset drivers
- `audioDrivers[]` - Audio drivers
- `networkDrivers[]` - Network drivers
  Each driver entry contains:
  - `deviceName` - Device name
  - `driverVersion` - Driver version
  - `driverDate` - Driver date
  - `providerName` - Provider name

### PdhInterface (Performance Data Helper)

#### CPU Metrics
- `cpu_total_usage` - Total CPU usage percentage
- `cpu_user_time` - User time percentage
- `cpu_privileged_time` - Privileged/kernel time percentage
- `cpu_idle_time` - CPU idle time percentage
- `cpu_per_core_usage` - Per-core CPU usage percentages
- `cpu_actual_frequency` - CPU actual frequency in MHz
- `cpu_per_core_actual_freq` - Per-core actual frequency (when available)
- `cpu_per_core_actual_freq_comma` - Per-core actual frequency (comma format, for advanced use)
- `cpu_interrupts_per_sec` - Interrupts per second
- `cpu_dpc_time` - DPC time percentage
- `cpu_interrupt_time` - Interrupt time percentage
- `cpu_dpcs_queued_per_sec` - DPCs queued per second
- `cpu_dpc_rate` - DPC rate
- `cpu_c1_time` - C1 state time percentage
- `cpu_c2_time` - C2 state time percentage
- `cpu_c3_time` - C3 state time percentage
- `cpu_c1_transitions_per_sec` - C1 state transitions per second
- `cpu_c2_transitions_per_sec` - C2 state transitions per second
- `cpu_c3_transitions_per_sec` - C3 state transitions per second


#### Memory Metrics
- `memory_available_mbytes` - Available physical memory in MB
- `memory_committed_bytes` - Total committed memory in bytes
- `memory_commit_limit` - Memory commit limit in bytes
- `memory_page_faults_per_sec` - Page faults per second
- `memory_pages_per_sec` - Pages per second
- `memory_pool_nonpaged_bytes` - Non-paged pool memory in bytes
- `memory_pool_paged_bytes` - Paged pool memory in bytes
- `memory_system_code_bytes` - System code memory in bytes
- `memory_system_driver_bytes` - System driver memory in bytes


#### Disk I/O Metrics
- `disk_read_bytes_per_sec` - Disk read rate in bytes/second
- `disk_write_bytes_per_sec` - Disk write rate in bytes/second
- `disk_reads_per_sec` - Disk reads per second
- `disk_writes_per_sec` - Disk writes per second
- `disk_transfers_per_sec` - Total disk transfers per second
- `disk_bytes_per_sec` - Total disk bytes per second
- `disk_avg_read_queue_length` - Average disk read queue length
- `disk_avg_write_queue_length` - Average disk write queue length
- `disk_avg_queue_length` - Average disk queue length
- `disk_avg_read_time` - Average disk read time in seconds
- `disk_avg_write_time` - Average disk write time in seconds
- `disk_avg_transfer_time` - Average disk transfer time in seconds
- `disk_percent_time` - Disk busy time percentage
- `disk_percent_read_time` - Disk read time percentage
- `disk_percent_write_time` - Disk write time percentage
- `disk_logical_percent_time` - Per-drive disk time percentages
- `disk_logical_percent_read_time` - Per-drive read time percentages
- `disk_logical_percent_write_time` - Per-drive write time percentages
- `disk_logical_percent_idle_time` - Per-drive idle time percentages


#### System Kernel Metrics
- `system_context_switches_per_sec` - Context switches per second
- `system_system_calls_per_sec` - System calls per second
- `system_processor_queue_length` - Processor queue length
- `system_processes` - Number of processes
- `system_threads` - Number of threads


### WinHardwareMonitor


#### CPU Sensors
**Available/implemented:**
- `coreLoads[]` – Per-core load percentages
- `clockSpeeds[]` – Per-core clock speeds
- `baseClockSpeed` – Base CPU clock speed in MHz
- `currentClockSpeed` – Current CPU clock speed in MHz
- `maxClockSpeed` – Maximum CPU clock speed in MHz
- `performancePercentage` – CPU performance percentage
- `smtActive` – Hyperthreading/SMT status
- `powerPlan` – Current power plan
- `architecture` – CPU architecture string
- `socket` – CPU socket type
- `cacheSizes` – CPU cache sizes (L1/L2/L3)
- `avxSupport` / `avx2Support` – AVX/AVX2 support
- `virtualizationEnabled` – Virtualization support

**Not implemented / cannot be implemented for now:**
- `temperature` – CPU temperature in Celsius (**cannot be implemented for now**)
- `coreTemperatures[]` – Per-core temperatures (**cannot be implemented for now**)
- `packagePower` – CPU package power consumption (**cannot be implemented for now**)
- `corePowers[]` – Per-core power consumption (**cannot be implemented for now**)
- `voltage` – CPU voltage (**cannot be implemented for now**)
- `coreVoltages[]` – Per-core voltages (**cannot be implemented for now**)
- `tjMax` – Maximum safe CPU temperature (**cannot be implemented for now**)


#### Memory Sensors
**Available/implemented:**
- `used` – Used memory in MB
- `available` – Available memory in MB
- `memoryType` – Memory type (DDR4, DDR5, etc.)
- `clockSpeed` – Memory clock speed in MHz
- `channels` – Number of memory channels
- `slotClockSpeeds[]` – Per-slot clock speeds

**Not implemented / cannot be implemented for now:**
- `load` – Memory load percentage (**not implemented**)
- `timingCL` – CAS latency (**not implemented, cannot be implemented for now**)
- `timingRCD` – RCD timing (**not implemented, cannot be implemented for now**)
- `timingRP` – RP timing (**not implemented, cannot be implemented for now**)
- `timingRAS` – RAS timing (**not implemented, cannot be implemented for now**)
- `formFactor` – RAM form factor (**not implemented, cannot be implemented for now**)
- `slotLoads[]` – Per-slot memory loads (**not implemented, struct field only**)


#### GPU Sensors
**Not implemented (struct fields only, always default/empty):**
- All GPU metrics below are **not implemented**. The struct fields exist, but the implementation always returns default/empty values:
  - `gpuTemperature`, `gpuLoad`, `memoryLoad`, `fanSpeed`, `power`, `coreClock`, `memoryClock`, `driver`, `hotSpotTemp`, `memoryTemp`, `vrm1Temp`, `fanSpeeds[]`, `memoryControllerLoad`, `videoEngineLoad`, `busInterface`, `powerLimit`
  - These will not be implemented without a driver or third-party library.


#### Memory Module Info
**Available/implemented:**
- `capacityGB` – Module capacity in GB
- `speedMHz` – Module speed in MHz
- `configuredSpeedMHz` – Actual configured speed
- `manufacturer` – Module manufacturer
- `partNumber` – Part number
- `memoryType` – Module type
- `xmpStatus` – XMP profile status
- `deviceLocator` – Physical slot location
- `formFactor` – Form factor (from WMI; DIMM, SO-DIMM, if available)
- `bankLabel` – Bank label (from WMI, if available)

### CPUKernelMetricsTracker

#### Kernel Performance Metrics
- `contextSwitchesPerSec` - Context switches per second
- `interruptsPerSec` - Interrupts per second
- `dpcCountPerSec` - DPC (Deferred Procedure Call) count per second
- `avgDpcLatencyUs` - Average DPC latency in microseconds
- `dpcLatenciesAbove50us` - Percentage of DPCs with latency > 50μs
- `dpcLatenciesAbove100us` - Percentage of DPCs with latency > 100μs
- `voluntaryContextSwitchesPerSec` - Voluntary context switches per second
- `involuntaryContextSwitchesPerSec` - Involuntary context switches per second
- `highPriorityInterruptionsPerSec` - High priority interruptions per second
- `priorityInversionsPerSec` - Priority inversions per second
- `avgThreadWaitTimeMs` - Average thread wait time in milliseconds


### DiskPerformanceTracker

#### Disk Performance Metrics
- `diskReadLatencyMs` - Average disk read latency in milliseconds
- `diskWriteLatencyMs` - Average disk write latency in milliseconds
- `diskQueueLength` - Current disk queue length
- `avgDiskQueueLength` - Average disk queue length over collection period
- `maxDiskQueueLength` - Maximum disk queue length observed
- `diskReadMB` - Total disk read data in MB over collection period
- `diskWriteMB` - Total disk write data in MB over collection period
- `minDiskReadLatencyMs` - Minimum disk read latency in milliseconds
- `maxDiskReadLatencyMs` - Maximum disk read latency in milliseconds
- `minDiskWriteLatencyMs` - Minimum disk write latency in milliseconds
- `maxDiskWriteLatencyMs` - Maximum disk write latency in milliseconds


### NvidiaMetrics

#### GPU Performance Metrics
- `temperature` - GPU temperature in Celsius
- `utilization` - GPU utilization percentage
- `memoryUtilization` - Memory utilization percentage
- `powerUsage` - Power usage in watts
- `totalMemory` - Total GPU memory in bytes
- `usedMemory` - Used GPU memory in bytes
- `fanSpeed` - Fan speed percentage
- `clockSpeed` - GPU clock in MHz
- `memoryClock` - Memory clock in MHz
- `name` - GPU name/model
- `throttling` - Thermal throttling status
- `deviceId` - GPU device ID
- `driverVersion` - Driver version string
- `pciLinkWidth` - PCIe link width
- `pcieLinkGen` - PCIe link generation
- `encoderUtilization` - Video encoder utilization
- `decoderUtilization` - Video decoder utilization
- `computeUtilization` - Compute utilization
- `graphicsEngineUtilization` - Graphics engine utilization
- `smUtilization` - SM (streaming multiprocessor) utilization
- `memoryBandwidthUtilization` - Memory bandwidth utilization
- `pcieRxThroughput` - PCIe receive throughput
- `pcieTxThroughput` - PCIe transmit throughput
- `nvdecUtilization` - NVDEC utilization
- `nvencUtilization` - NVENC utilization
- `driverDate` - Driver date string
- `hasGeForceExperience` - Whether GeForce Experience is installed

#### Per-process GPU Metrics (if available)
- `pid` - Process ID
- `name` - Process name
- `gpuUtilization` - GPU utilization percentage
- `memoryUtilization` - Memory controller utilization
- `computeUtilization` - Compute utilization
- `encoderUtilization` - Encoder utilization
- `decoderUtilization` - Decoder utilization
- `memoryUsed` - Memory used in bytes


### SystemWrapper

#### System Information
- `osVersion` - Operating system version
- `kernelVersion` - Kernel version
- `systemUptime` - System uptime
- `processCount` - Number of running processes
- `threadCount` - Number of threads
- `handleCount` - Number of handles
- `pageFileInfo` - Page file information (exists, systemManaged, totalSizeMB, primaryDriveLetter, locations, currentSizesMB, maxSizesMB)
- `monitorInfo[]` - Array of monitor information (deviceName, displayName, width, height, refreshRate, isPrimary)
- `driverInfo[]` - Array of driver information (deviceName, driverVersion, driverDate, providerName, isDateValid)
- `chipsetDriverDetails[]` - Chipset driver details
- `audioDriverDetails[]` - Audio driver details
- `networkDriverDetails[]` - Network driver details

### PresentDataExports

**Purpose**: ETW-based frame timing and presentation metrics (graphics/DirectX).
**Usage**: Frame time, FPS, GPU/CPU render time, present mode, and display stats for graphics benchmarking.

#### Metrics
- `frametime` - Frame time in milliseconds (from display timestamps)
- `fps` - Frames per second (1000/frametime)
- `gpuRenderTime` - GPU duration (ms)
- `gpuVideoTime` - GPU video processing time (ms)
- `cpuRenderTime` - CPU render time (ms)
- `appRenderTime` - Application render time (ms)
- `appSleepTime` - Time app spent sleeping (ms)
- `destWidth` - Destination surface width
- `destHeight` - Destination surface height
- `supportsTearing` - Whether tearing is supported
- `syncInterval` - VSync interval
- `frameId` - Frame sequence number
- `presentFlags` - Present flags from DXGI/D3D
- `runtime` - Runtime (DXGI, D3D9, etc)
- `presentMode` - Present mode (Flip, BitBlt, etc)
- `minFrameTime` - Minimum frame time in collection interval
- `maxFrameTime` - Maximum frame time in collection interval
- `minGpuRenderTime` - Minimum GPU render time
- `maxGpuRenderTime` - Maximum GPU render time
- `minCpuRenderTime` - Minimum CPU render time
- `maxCpuRenderTime` - Maximum CPU render time
- `frameTimeVariance` - Variance in frame times
- `frameTime99Percentile` - 99th percentile frame time (1% low)
- `frameTime95Percentile` - 95th percentile frame time (5% low)
- `frameTime995Percentile` - 99.5th percentile frame time (0.5% low)
- `frameCount` - Number of frames in this collection interval

---

## Metric Collection Best Practices

1. **Choose appropriate collection intervals** based on metric type
2. **Use batch collection** when possible to reduce overhead
3. **Handle missing metrics gracefully** for cross-platform compatibility.

---