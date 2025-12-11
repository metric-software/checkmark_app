#include "BenchmarkResultFileManager.h"
#include "BenchmarkSpecsFileManager.h"
#include "../hardware/ConstantSystemInfo.h"
#include "../profiles/UserSystemProfile.h"
#include "BenchmarkConstants.h"
#include "../logging/Logger.h"
#include <QDir>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <windows.h>

BenchmarkResultFileManager::BenchmarkResultFileManager() {
    // Initialize with system core counts
    const auto& sysInfo = SystemMetrics::GetConstantSystemInfo();
    setCoreCount(sysInfo.logicalCores, sysInfo.physicalCores);
}

BenchmarkResultFileManager::~BenchmarkResultFileManager() {
    closeFile();
}

bool BenchmarkResultFileManager::initializeOutputFile(const QString& filename) {
    if (filename.isEmpty()) {
        logError("Cannot initialize file: filename is empty");
        return false;
    }
    
    m_outputFilename = filename;
    m_fullPath = "benchmark_results/" + filename.toStdString();
    
    return createDirectories();
}

bool BenchmarkResultFileManager::createDirectories() {
    QDir().mkpath("benchmark_results");
    QDir().mkpath("profiles");
    return true;
}

bool BenchmarkResultFileManager::openFile() {
    closeFile(); // Ensure any existing file is closed
    
    m_outputFile.open(m_fullPath, std::ios::out | std::ios::trunc);
    if (!m_outputFile.is_open()) {
        logError("Failed to open output file: " + m_fullPath);
        
        // Try alternative path
        QString absPath = QDir::currentPath() + "/benchmark_results/";
        QDir().mkpath(absPath);
        std::string altPath = absPath.toStdString() + m_outputFilename.toStdString();
        
        m_outputFile.open(altPath, std::ios::out | std::ios::trunc);
        if (!m_outputFile.is_open()) {
            // Emergency backup path
            QString emergencyPath = absPath + "emergency_backup.csv";
            m_outputFile.open(emergencyPath.toStdString(), std::ios::out | std::ios::trunc);
            
            if (!m_outputFile.is_open()) {
                logError("All file creation attempts failed");
                return false;
            }
            m_fullPath = emergencyPath.toStdString();
        } else {
            m_fullPath = altPath;
        }
    }
    
    return true;
}

void BenchmarkResultFileManager::setCoreCount(size_t logicalCores, size_t physicalCores) {
    m_finalUsageCount = logicalCores > 0 ? logicalCores : 0;
    m_finalSpeedCount = physicalCores > 0 ? physicalCores : 0;
    
    // If we still have 0 cores, try to get system count as fallback
    if (m_finalUsageCount == 0 || m_finalSpeedCount == 0) {
        SYSTEM_INFO sysInfoWin;
        GetSystemInfo(&sysInfoWin);
        
        if (m_finalUsageCount == 0)
            m_finalUsageCount = sysInfoWin.dwNumberOfProcessors;
        
        if (m_finalSpeedCount == 0)
            m_finalSpeedCount = sysInfoWin.dwNumberOfProcessors / 2;
    }
}

bool BenchmarkResultFileManager::writeHeader() {
    if (!openFile()) {
        return false;
    }
    
    writeCSVHeader();
    return true;
}

void BenchmarkResultFileManager::writeCSVHeader() {
    // =====================================================================================
    // === CSV HEADER ORGANIZED BY PROVIDER ===
    // =====================================================================================
    
    m_outputFile
        // === PRESENTMON (ETW) METRICS - Keep FPS first as requested ===
        << "Time,FPS,Frame Time,Highest Frame Time,5% Highest Frame Time (Per-Second),"
        << "GPU Render Time,CPU Render Time,Highest GPU Time,Highest CPU Time,Frame Time Variance,"
        << "1% Low FPS (Cumulative),5% Low FPS (Cumulative),0.5% Low FPS (Cumulative),"
        << "Display Width,Display Height,"
        
        // === NVIDIA GPU METRICS ===
        << "GPU Temp,GPU Usage,GPU Power,GPU Clock,GPU Mem Clock,GPU Fan,"
        << "GPU Mem Used,GPU Mem Total,GPU SM Util,GPU Mem Bandwidth Util,"
        << "GPU PCIe Rx,GPU PCIe Tx,GPU NVDEC Util,GPU NVENC Util,"
        
        // === PDH CPU METRICS ===
        << "PDH_CPU_Usage(%),PDH_CPU_User_Time(%),PDH_CPU_Privileged_Time(%),PDH_CPU_Idle_Time(%),"
        << "PDH_CPU_Freq(MHz),"
        << "PDH_CPU_Interrupts/sec,PDH_CPU_DPC_Time(%),PDH_CPU_Interrupt_Time(%),"
        << "PDH_CPU_DPCs_Queued/sec,PDH_CPU_DPC_Rate,"
        << "PDH_CPU_C1_Time(%),PDH_CPU_C2_Time(%),PDH_CPU_C3_Time(%),"
        << "PDH_CPU_C1_Transitions/sec,PDH_CPU_C2_Transitions/sec,PDH_CPU_C3_Transitions/sec,"
        
        // === PDH MEMORY METRICS ===
        << "PDH_Memory_Available(MB),PDH_Memory_Load(%),PDH_Memory_Committed(bytes),"
        << "PDH_Memory_Commit_Limit(bytes),PDH_Memory_Page_Faults/sec,PDH_Memory_Pages/sec,"
        << "PDH_Memory_Pool_NonPaged(bytes),PDH_Memory_Pool_Paged(bytes),"
        << "PDH_Memory_System_Code(bytes),PDH_Memory_System_Driver(bytes),"
        
        // === PDH DISK METRICS ===
        << "PDH_Disk_Read_Rate(MB/s),PDH_Disk_Write_Rate(MB/s),"
        << "PDH_Disk_Reads/sec,PDH_Disk_Writes/sec,PDH_Disk_Transfers/sec,PDH_Disk_Bytes/sec,"
        << "PDH_Disk_Avg_Read_Queue,PDH_Disk_Avg_Write_Queue,PDH_Disk_Avg_Queue,"
        << "PDH_Disk_Avg_Read_Time(sec),PDH_Disk_Avg_Write_Time(sec),PDH_Disk_Avg_Transfer_Time(sec),"
        << "PDH_Disk_Percent_Time(%),PDH_Disk_Percent_Read_Time(%),PDH_Disk_Percent_Write_Time(%),"
        
        // === PDH SYSTEM METRICS ===
        << "PDH_Context_Switches/sec,PDH_System_Processor_Queue,PDH_System_Processes,"
        << "PDH_System_Threads,PDH_System_Calls/sec";

    // === PDH PER-CORE METRICS ===
    for (size_t i = 0; i < m_finalUsageCount; i++) {
        m_outputFile << ",PDH_Core " << i << " CPU (%)";
    }
    for (size_t i = 0; i < m_finalSpeedCount; i++) {
        m_outputFile << ",PDH_Core " << i << " Freq (MHz)";
    }
    
    // === CPU KERNEL TRACKER (ETW) METRICS ===
    m_outputFile
        << ",ETW_Interrupts/sec,ETW_DPCs/sec,ETW_Avg_DPC_Latency(μs),"
        << "ETW_DPC_Latencies_>50μs(%),ETW_DPC_Latencies_>100μs(%),"
        
        // === DISK PERFORMANCE TRACKER METRICS ===
        << "Disk_Read_Latency(ms),Disk_Write_Latency(ms),Disk_Queue_Length,"
        << "Disk_Avg_Queue_Length,Disk_Max_Queue_Length,"
        << "Disk_Min_Read_Latency(ms),Disk_Max_Read_Latency(ms),"
        << "Disk_Min_Write_Latency(ms),Disk_Max_Write_Latency(ms),"
        << "Disk_IO_Read_Total(MB),Disk_IO_Write_Total(MB)";
}

bool BenchmarkResultFileManager::writeDataPoints(
    const std::vector<BenchmarkDataPoint>& dataPoints,
    const std::set<std::string>& diskNames) {
    
    if (!m_outputFile.is_open()) {
        logError("Cannot write data points: file is not open");
        return false;
    }

    // Add headers for per-disk throughput metrics if we have disk data
    if (!diskNames.empty()) {
        logCritical("Found per-disk data for " + std::to_string(diskNames.size()) + " drives");
        for (const auto& diskName : diskNames) {
            m_outputFile << ",Disk_" << diskName << "_Read(MB/s),Disk_" << diskName << "_Write(MB/s)";
        }
    }
    
    m_outputFile << "\n";

    int pointsWritten = 0;
    for (const auto& data : dataPoints) {
        if (data.presentCount > 0) {
            writeDataPoint(data, diskNames);
            pointsWritten++;
        }
    }

    m_outputFile.flush();
    
    // Calculate and log average values for all metrics
    if (!dataPoints.empty()) {
        try {
            logBenchmarkAverages(dataPoints, diskNames);
        } catch (const std::exception& e) {
            logError("Benchmark averages error: " + std::string(e.what()));
        } catch (...) {
            logError("Unknown error in benchmark averages");
        }
    }
    
    logCritical("Wrote " + std::to_string(pointsWritten) + " data points to CSV");
    return true;
}

void BenchmarkResultFileManager::writeDataPoint(
    const BenchmarkDataPoint& data, 
    const std::set<std::string>& diskNames) {
    
    // =====================================================================================
    // === DATA ORGANIZED BY PROVIDER (matching headers) ===
    // =====================================================================================
    
    m_outputFile << std::fixed << std::setprecision(2)
        // === PRESENTMON (ETW) METRICS ===
        << data.timestamp
        << "," << (data.fps > 0 ? data.fps : -1)
        << "," << (data.frameTime > 0 ? data.frameTime : -1)
        << "," << (data.highestFrameTime > 0 ? data.highestFrameTime : -1)
        << "," << (data.highest5PctFrameTime > 0 ? data.highest5PctFrameTime : -1)
        << "," << (data.gpuRenderTime > 0 ? data.gpuRenderTime : -1)
        << "," << (data.cpuRenderTime > 0 ? data.cpuRenderTime : -1)
        << "," << (data.highestGpuTime > 0 ? data.highestGpuTime : -1)
        << "," << (data.highestCpuTime > 0 ? data.highestCpuTime : -1)
        << "," << data.fpsVariance
        << "," << data.lowFps1Percent
        << "," << data.lowFps5Percent
        << "," << data.lowFps05Percent
        << "," << data.destWidth
        << "," << data.destHeight
        
        // === NVIDIA GPU METRICS ===
        << "," << data.gpuTemp
        << "," << data.gpuUtilization
        << "," << data.gpuPower
        << "," << data.gpuClock
        << "," << data.gpuMemClock
        << "," << data.gpuFanSpeed
        << "," << std::setprecision(4) << (data.gpuMemUsed / (1024.0 * 1024.0))
        << "," << (data.gpuMemTotal / (1024.0 * 1024.0))
        << "," << data.gpuSmUtilization
        << "," << data.gpuMemBandwidthUtil
        << "," << data.gpuPcieRxThroughput
        << "," << data.gpuPcieTxThroughput
        << "," << data.gpuNvdecUtil
        << "," << data.gpuNvencUtil
        
        // === PDH CPU METRICS ===
        << "," << std::setprecision(2) << (data.procProcessorTime >= 0 ? data.procProcessorTime : -1)
        << "," << (data.procUserTime >= 0 ? data.procUserTime : -1)
        << "," << (data.procPrivilegedTime >= 0 ? data.procPrivilegedTime : -1)
        << "," << (data.procIdleTime >= 0 ? data.procIdleTime : -1)
        << "," << (data.procActualFreq >= 0 ? data.procActualFreq : -1)
        << "," << (data.cpuInterruptsPerSec >= 0 ? data.cpuInterruptsPerSec : -1)
        << "," << (data.cpuDpcTime >= 0 ? data.cpuDpcTime : -1)
        << "," << (data.cpuInterruptTime >= 0 ? data.cpuInterruptTime : -1)
        << "," << (data.cpuDpcsQueuedPerSec >= 0 ? data.cpuDpcsQueuedPerSec : -1)
        << "," << (data.cpuDpcRate >= 0 ? data.cpuDpcRate : -1)
        << "," << (data.cpuC1Time >= 0 ? data.cpuC1Time : -1)
        << "," << (data.cpuC2Time >= 0 ? data.cpuC2Time : -1)
        << "," << (data.cpuC3Time >= 0 ? data.cpuC3Time : -1)
        << "," << (data.cpuC1TransitionsPerSec >= 0 ? data.cpuC1TransitionsPerSec : -1)
        << "," << (data.cpuC2TransitionsPerSec >= 0 ? data.cpuC2TransitionsPerSec : -1)
        << "," << (data.cpuC3TransitionsPerSec >= 0 ? data.cpuC3TransitionsPerSec : -1)
        
        // === PDH MEMORY METRICS ===
        << "," << (data.availableMemoryMB > 0 ? data.availableMemoryMB : -1)
        << "," << (data.memoryLoad > 0 ? data.memoryLoad : -1)
        << "," << (data.memoryCommittedBytes >= 0 ? data.memoryCommittedBytes : -1)
        << "," << (data.memoryCommitLimit >= 0 ? data.memoryCommitLimit : -1)
        << "," << (data.memoryFaultsPerSec >= 0 ? data.memoryFaultsPerSec : -1)
        << "," << (data.memoryPagesPerSec >= 0 ? data.memoryPagesPerSec : -1)
        << "," << (data.memoryPoolNonPagedBytes >= 0 ? data.memoryPoolNonPagedBytes : -1)
        << "," << (data.memoryPoolPagedBytes >= 0 ? data.memoryPoolPagedBytes : -1)
        << "," << (data.memorySystemCodeBytes >= 0 ? data.memorySystemCodeBytes : -1)
        << "," << (data.memorySystemDriverBytes >= 0 ? data.memorySystemDriverBytes : -1)
        
        // === PDH DISK METRICS ===
        << "," << (data.ioReadRateMBs >= 0 ? data.ioReadRateMBs : -1)
        << "," << (data.ioWriteRateMBs >= 0 ? data.ioWriteRateMBs : -1)
        << "," << (data.diskReadsPerSec >= 0 ? data.diskReadsPerSec : -1)
        << "," << (data.diskWritesPerSec >= 0 ? data.diskWritesPerSec : -1)
        << "," << (data.diskTransfersPerSec >= 0 ? data.diskTransfersPerSec : -1)
        << "," << (data.diskBytesPerSec >= 0 ? data.diskBytesPerSec : -1)
        << "," << (data.diskAvgReadQueueLength >= 0 ? data.diskAvgReadQueueLength : -1)
        << "," << (data.diskAvgWriteQueueLength >= 0 ? data.diskAvgWriteQueueLength : -1)
        << "," << (data.diskAvgQueueLength >= 0 ? data.diskAvgQueueLength : -1)
        << "," << (data.diskAvgReadTime >= 0 ? data.diskAvgReadTime : -1)
        << "," << (data.diskAvgWriteTime >= 0 ? data.diskAvgWriteTime : -1)
        << "," << (data.diskAvgTransferTime >= 0 ? data.diskAvgTransferTime : -1)
        << "," << (data.diskPercentTime >= 0 ? data.diskPercentTime : -1)
        << "," << (data.diskPercentReadTime >= 0 ? data.diskPercentReadTime : -1)
        << "," << (data.diskPercentWriteTime >= 0 ? data.diskPercentWriteTime : -1)
        
        // === PDH SYSTEM METRICS ===
        << "," << (data.contextSwitchesPerSec >= 0 ? data.contextSwitchesPerSec : -1)
        << "," << (data.systemProcessorQueueLength >= 0 ? data.systemProcessorQueueLength : -1)
        << "," << (data.systemProcesses >= 0 ? data.systemProcesses : -1)
        << "," << (data.systemThreads >= 0 ? data.systemThreads : -1)
        << "," << (data.pdhInterruptsPerSec >= 0 ? data.pdhInterruptsPerSec : -1);

    // === PDH PER-CORE CPU USAGE ===
    for (size_t i = 0; i < m_finalUsageCount; i++) {
        m_outputFile << ",";
        if (i < data.perCoreCpuUsagePdh.size() && data.perCoreCpuUsagePdh[i] >= 0) {
            m_outputFile << std::setprecision(2) << data.perCoreCpuUsagePdh[i];
        } else {
            m_outputFile << -1;
        }
    }
    
    // === PDH PER-CORE ACTUAL FREQUENCY ===
    for (size_t i = 0; i < m_finalSpeedCount; i++) {
        m_outputFile << ",";
        if (i < data.perCoreActualFreq.size() && data.perCoreActualFreq[i] > 0) {
            m_outputFile << std::setprecision(0) << data.perCoreActualFreq[i];
        } else {
            m_outputFile << -1;
        }
    }
    
    // === CPU KERNEL TRACKER (ETW) METRICS ===
    m_outputFile 
        << "," << (data.interruptsPerSec >= 0 ? data.interruptsPerSec : -1)
        << "," << (data.dpcCountPerSec >= 0 ? data.dpcCountPerSec : -1)
        << "," << std::setprecision(3) << data.avgDpcLatencyUs
        << "," << std::setprecision(2) << data.dpcLatenciesAbove50us
        << "," << data.dpcLatenciesAbove100us;

    // === DISK PERFORMANCE TRACKER METRICS ===
    m_outputFile << std::setprecision(4)
        << "," << (data.diskReadLatencyMs >= 0 ? data.diskReadLatencyMs : -1)
        << "," << (data.diskWriteLatencyMs >= 0 ? data.diskWriteLatencyMs : -1)
        << "," << (data.diskQueueLength >= 0 ? data.diskQueueLength : -1)
        << "," << (data.avgDiskQueueLength >= 0 ? data.avgDiskQueueLength : -1)
        << "," << (data.maxDiskQueueLength >= 0 ? data.maxDiskQueueLength : -1)
        << "," << (data.minDiskReadLatencyMs >= 0 ? data.minDiskReadLatencyMs : -1)
        << "," << (data.maxDiskReadLatencyMs >= 0 ? data.maxDiskReadLatencyMs : -1)
        << "," << (data.minDiskWriteLatencyMs >= 0 ? data.minDiskWriteLatencyMs : -1)
        << "," << (data.maxDiskWriteLatencyMs >= 0 ? data.maxDiskWriteLatencyMs : -1)
        << "," << std::setprecision(2) << (data.ioReadMB >= 0 ? data.ioReadMB : -1)
        << "," << (data.ioWriteMB >= 0 ? data.ioWriteMB : -1);

    // === PER-DISK THROUGHPUT (from DiskPerformanceTracker) ===
    for (const auto& diskName : diskNames) {
        m_outputFile << ",";
        auto readIt = data.perDiskReadRates.find(diskName);
        if (readIt != data.perDiskReadRates.end()) {
            m_outputFile << std::setprecision(4) << readIt->second;
        } else {
            m_outputFile << -1;
        }

        m_outputFile << ",";
        auto writeIt = data.perDiskWriteRates.find(diskName);
        if (writeIt != data.perDiskWriteRates.end()) {
            m_outputFile << std::setprecision(4) << writeIt->second;
        } else {
            m_outputFile << -1;
        }
    }

    m_outputFile << "\n";
}

std::vector<BenchmarkDataPoint> BenchmarkResultFileManager::extractBenchmarkData(
    const std::vector<BenchmarkDataPoint>& allData) {
    
    std::vector<BenchmarkDataPoint> trimmedData;
    
    if (allData.empty()) {
        logError("No benchmark data to extract");
        return trimmedData;
    }

    trimmedData.reserve(allData.size());

    // Get the last timestamp as the end of the reference data
    float rawEndTime = allData.back().timestamp;

    // Adjust end time to exclude the buffer
    float endTime = rawEndTime - static_cast<float>(BenchmarkConstants::BENCHMARK_END_BUFFER);

    // Calculate start time based on target duration
    float targetStart = endTime - static_cast<float>(BenchmarkConstants::TARGET_BENCHMARK_DURATION);
    float startTime = (targetStart > 0.0f) ? targetStart : 0.0f;

    logCritical("Extracting data: " + std::to_string(static_cast<int>(startTime)) +
               "s to " + std::to_string(static_cast<int>(endTime)) + "s");

    int pointsInRange = 0;
    for (const auto& data : allData) {
        if (data.timestamp >= startTime && data.timestamp <= endTime) {
            trimmedData.push_back(data);
            pointsInRange++;
        }
    }

    logCritical("Selected " + std::to_string(pointsInRange) + " data points");
    return trimmedData;
}

bool BenchmarkResultFileManager::finalizeBenchmark(
    const std::vector<BenchmarkDataPoint>& allData,
    const QString& userSystemId) {
    
    // Extract benchmark data points
    std::vector<BenchmarkDataPoint> trimmedData = extractBenchmarkData(allData);
    
    // Find all disk names for headers
    std::set<std::string> allDiskNames;
    for (const auto& data : trimmedData) {
        for (const auto& [diskName, _] : data.perDiskReadRates) {
            allDiskNames.insert(diskName);
        }
    }

    // Write header and data
    if (!writeHeader()) {
        return false;
    }
    
    if (!writeDataPoints(trimmedData, allDiskNames)) {
        return false;
    }

    // Write system specs and benchmark results
    writeSystemSpecs(userSystemId);
    writeFinalBenchmarkResults();

    closeFile();
    return true;
}

bool BenchmarkResultFileManager::writeSystemSpecs(const QString& userSystemId) {
    QString specsPath = QString::fromStdString(m_fullPath);
    specsPath.replace(".csv", "_specs.txt");

    // Save system specs including user system profile ID
    BenchmarkSpecsFileManager::saveSystemSpecsToFile(specsPath);

    // Append user system profile ID to the specs file
    std::ofstream specsFile(specsPath.toStdString(), std::ios::app);
    if (specsFile.is_open()) {
        specsFile << "\n\n=== USER SYSTEM PROFILE ===\n";
        specsFile << "User System ID: " << userSystemId.toStdString() << "\n";

        // Get the profile file path for reference
        auto& userProfile = SystemMetrics::UserSystemProfile::getInstance();
        std::string profilePath = "profiles/system_profile.json";
        specsFile << "Profile Location: " << profilePath << "\n";

        // Make sure the profile is saved
        QDir().mkpath("profiles");
        userProfile.saveToFile(profilePath);
        
        specsFile.close();
        return true;
    }
    
    return false;
}

bool BenchmarkResultFileManager::writeFinalBenchmarkResults() {
    // This would need access to frame time data from BenchmarkManager
    // For now, we'll leave this as a placeholder
    // The actual implementation would need the frame time calculation logic
    return true;
}

void BenchmarkResultFileManager::closeFile() {
    if (m_outputFile.is_open()) {
        m_outputFile.flush();
        m_outputFile.close();
    }
}

bool BenchmarkResultFileManager::isFileOpen() const {
    return m_outputFile.is_open();
}

std::string BenchmarkResultFileManager::getFilePath() const {
    return m_fullPath;
}

void BenchmarkResultFileManager::logError(const std::string& message) {
    LOG_ERROR << "[ERROR] " << message;
}

void BenchmarkResultFileManager::logCritical(const std::string& message) {
    LOG_ERROR << "[CRITICAL] " << message;
}

void BenchmarkResultFileManager::logBenchmarkAverages(const std::vector<BenchmarkDataPoint>& dataPoints,
                                                     const std::set<std::string>& diskNames) {
    try {
        if (dataPoints.empty()) {
            logError("No data points for averages calculation");
            return;
        }
        
        size_t validSamples = 0;
        double sumFps = 0.0, sumFrameTime = 0.0, sumCpuUsage = 0.0;
        double sumGpuTemp = 0.0, sumGpuUtil = 0.0;
        
        // Simple accumulation with bounds checking using PDH metrics
        for (const auto& data : dataPoints) {
            if (data.presentCount > 0) {
                validSamples++;
                if (data.fps > 0 && data.fps < 1000) sumFps += data.fps;
                if (data.frameTime > 0 && data.frameTime < 1000) sumFrameTime += data.frameTime;
                if (data.procProcessorTime >= 0 && data.procProcessorTime <= 100) sumCpuUsage += data.procProcessorTime;
                if (data.gpuTemp > 0 && data.gpuTemp < 200) sumGpuTemp += data.gpuTemp;
                if (data.gpuUtilization >= 0 && data.gpuUtilization <= 100) sumGpuUtil += data.gpuUtilization;
            }
        }
        
        if (validSamples == 0) {
            logError("No valid samples found");
            return;
        }
        
        logCritical("BENCHMARK AVERAGES (" + std::to_string(validSamples) + " samples)");
        logCritical("Avg FPS: " + std::to_string(static_cast<int>(sumFps / validSamples)) +
                   " CPU: " + std::to_string(static_cast<int>(sumCpuUsage / validSamples)) + "%" +
                   " GPU: " + std::to_string(static_cast<int>(sumGpuUtil / validSamples)) + "%");
        
    } catch (const std::exception& e) {
        logError("Error calculating averages: " + std::string(e.what()));
    } catch (...) {
        logError("Unknown error calculating averages");
    }
}
