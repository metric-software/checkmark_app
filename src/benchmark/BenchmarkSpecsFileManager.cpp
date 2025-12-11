#include "BenchmarkSpecsFileManager.h"

#include <fstream>
#include <iostream>
#include <iomanip>

#include <QCryptographicHash>
#include <QDateTime>
#include <QRandomGenerator>
#include <Windows.h>
#include <Powrprof.h>

#include "hardware/ConstantSystemInfo.h"
#include "profiles/UserSystemProfile.h"
#include "hardware/RustConfigFinder.h"
#include "../logging/Logger.h"

void BenchmarkSpecsFileManager::saveSystemSpecsToFile(const QString& benchmarkFileName, bool isUnfinished) {
    QString specsFileName = benchmarkFileName;
    specsFileName.replace(".csv", "_specs.txt");

    std::ofstream file(specsFileName.toStdString(), std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR << "Failed to write specs file";
        return;
    }

    if (isUnfinished) {
        file << "STATUS: INCOMPLETE - Benchmark did not finish properly\n\n";
    } else {
        file << "STATUS: COMPLETE - Valid benchmark\n\n";
    }

    // Get user system profile for the ID
    auto& userProfile = SystemMetrics::UserSystemProfile::getInstance();
    if (!userProfile.isInitialized()) {
        userProfile.initialize();
    }
    std::string systemIdentifier = userProfile.getCombinedIdentifier();

    // Get constant system info
    const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();

    // Generate hash for this benchmark
    QString hash = generateNewBenchmarkHash();

    // Write benchmark info section
    file << "Benchmark Information:\n"
         << "  Hash: " << hash.toStdString() << "\n"
         << "  Timestamp: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString() << "\n"
         << "  User System ID: " << systemIdentifier << "\n\n";

    // CPU Information
    file << "CPU Information:\n"
         << "  Model: " << constantInfo.cpuName << "\n"
         << "  Vendor: " << constantInfo.cpuVendor << "\n"
         << "  Architecture: " << constantInfo.cpuArchitecture << "\n"
         << "  Physical Cores: " << constantInfo.physicalCores << "\n"
         << "  Logical Cores: " << constantInfo.logicalCores << "\n"
         << "  Socket: " << constantInfo.cpuSocket << "\n"
         << "  Base Clock: " << constantInfo.baseClockMHz << " MHz\n"
         << "  Max Clock: " << constantInfo.maxClockMHz << " MHz\n"
         << "  L1 Cache: " << constantInfo.l1CacheKB << " KB\n"
         << "  L2 Cache: " << constantInfo.l2CacheKB << " KB\n"
         << "  L3 Cache: " << constantInfo.l3CacheKB << " KB\n"
         << "  Hyperthreading: " << (constantInfo.hyperThreadingEnabled ? "Enabled" : "Disabled") << "\n"
         << "  Virtualization: " << (constantInfo.virtualizationEnabled ? "Enabled" : "Disabled") << "\n"
         << "  AVX Support: " << (constantInfo.avxSupport ? "Yes" : "No") << "\n"
         << "  AVX2 Support: " << (constantInfo.avx2Support ? "Yes" : "No") << "\n\n";

    // Memory Information
    float ramInGB = constantInfo.totalPhysicalMemoryMB / 1024.0f;
    file << "Memory Information:\n"
         << "  Total Physical: " << std::fixed << std::setprecision(4) << ramInGB << " GB\n"
         << "  Total Physical (MB): " << constantInfo.totalPhysicalMemoryMB << " MB\n"
         << "  Type: " << constantInfo.memoryType << "\n"
         << "  Clock: " << constantInfo.memoryClockMHz << " MHz\n"
         << "  XMP Enabled: " << (constantInfo.xmpEnabled ? "Yes" : "No") << "\n"
         << "  Channel Configuration: " << constantInfo.memoryChannelConfig << "\n\n";

    // Memory Modules
    file << "Memory Modules (" << constantInfo.memoryModules.size() << "):\n";
    for (size_t i = 0; i < constantInfo.memoryModules.size(); i++) {
        const auto& module = constantInfo.memoryModules[i];
        file << "  Module " << (i + 1) << ":\n"
             << "    Capacity: " << module.capacityGB << " GB\n"
             << "    Speed: " << module.speedMHz << " MHz\n"
             << "    Configured Speed: " << module.configuredSpeedMHz << " MHz\n"
             << "    Manufacturer: " << module.manufacturer << "\n"
             << "    Part Number: " << module.partNumber << "\n"
             << "    Type: " << module.memoryType << "\n"
             << "    Location: " << module.deviceLocator << "\n"
             << "    Form Factor: " << module.formFactor << "\n";

        if (!module.bankLabel.empty() && module.bankLabel != "no_data") {
            file << "    Bank Label: " << module.bankLabel << "\n";
        }
    }
    file << "\n";

    // GPU Information
    file << "GPU Devices (" << constantInfo.gpuDevices.size() << "):\n";
    for (size_t i = 0; i < constantInfo.gpuDevices.size(); i++) {
        const auto& gpu = constantInfo.gpuDevices[i];
        file << "  GPU " << (i + 1) << (gpu.isPrimary ? " (Primary)" : "") << ":\n"
             << "    Model: " << gpu.name << "\n"
             << "    Device ID: " << gpu.deviceId << "\n"
             << "    Memory: " << (gpu.memoryMB / 1024) << " GB\n"
             << "    Memory (MB): " << gpu.memoryMB << " MB\n"
             << "    Driver: " << gpu.driverVersion << "\n"
             << "    Driver Date: " << gpu.driverDate << "\n"
             << "    Has GeForce Experience: " << (gpu.hasGeForceExperience ? "Yes" : "No") << "\n"
             << "    Vendor: " << gpu.vendor << "\n"
             << "    PCIe Width: " << gpu.pciLinkWidth << "\n"
             << "    PCIe Generation: " << gpu.pcieLinkGen << "\n"
             << "    Primary: " << (gpu.isPrimary ? "Yes" : "No") << "\n";
    }
    file << "\n";

    // Motherboard Information
    file << "Motherboard Information:\n"
         << "  Manufacturer: " << constantInfo.motherboardManufacturer << "\n"
         << "  Model: " << constantInfo.motherboardModel << "\n"
         << "  Chipset: " << constantInfo.chipsetModel << "\n"
         << "  Chipset Driver Version: " << constantInfo.chipsetDriverVersion << "\n"
         << "  BIOS Version: " << constantInfo.biosVersion << "\n"
         << "  BIOS Date: " << constantInfo.biosDate << "\n"
         << "  BIOS Manufacturer: " << constantInfo.biosManufacturer << "\n\n";

    // Storage Information
    file << "Storage Drives (" << constantInfo.drives.size() << "):\n";
    for (size_t i = 0; i < constantInfo.drives.size(); i++) {
        const auto& drive = constantInfo.drives[i];
    file << "  Drive " << (i + 1) << (drive.isSystemDrive ? " (System Drive)" : " (Data Drive)") << ":\n"
         << "    Path: " << drive.path << "\n"
         << "    Model: " << drive.model << "\n"
         /* Serial Number removed for privacy */
         << "    Interface: " << drive.interfaceType << "\n"
         << "    Capacity: " << drive.totalSpaceGB << " GB\n"
         << "    Free Space: " << drive.freeSpaceGB << " GB\n"
         << "    System Drive: " << (drive.isSystemDrive ? "Yes" : "No") << "\n"
         << "    SSD: " << (drive.isSSD ? "Yes" : "No") << "\n";
    }
    file << "\n";

    // Power Settings
    file << "Power Settings:\n"
         << "  Power Plan: " << constantInfo.powerPlan << "\n"
         << "  High Performance Power Plan: " << (constantInfo.powerPlanHighPerf ? "Yes" : "No") << "\n"
         << "  Game Mode: " << (constantInfo.gameMode ? "Enabled" : "Disabled") << "\n\n";

    // Page File Information
    file << "Page File Information:\n"
         << "  Exists: " << (constantInfo.pageFileExists ? "Yes" : "No") << "\n";

    if (constantInfo.pageFileExists) {
        file << "  System Managed: " << (constantInfo.pageFileSystemManaged ? "Yes" : "No") << "\n"
             << "  Total Size: " << constantInfo.pageTotalSizeMB << " MB\n"
             << "  Primary Drive: " << constantInfo.pagePrimaryDriveLetter << "\n"
             << "  Locations:\n";

        for (size_t i = 0; i < constantInfo.pageFileLocations.size(); i++) {
            file << "    " << constantInfo.pageFileLocations[i];

            if (i < constantInfo.pageFileCurrentSizesMB.size() && i < constantInfo.pageFileMaxSizesMB.size()) {
                file << " (Current: " << constantInfo.pageFileCurrentSizesMB[i] << " MB";

                if (constantInfo.pageFileMaxSizesMB[i] > 0) {
                    file << ", Max: " << constantInfo.pageFileMaxSizesMB[i] << " MB";
                }

                file << ")";
            }

            file << "\n";
        }
    }
    file << "\n";

    // System Settings
    file << "OS Information:\n"
         << "  OS Version: " << constantInfo.osVersion << "\n"
         << "  Build: " << constantInfo.osBuildNumber << "\n"
         << "  Windows 11: " << (constantInfo.isWindows11 ? "Yes" : "No") << "\n";
         /* System Name removed for privacy */
    

    // Monitor Information
    file << "Monitor Information (" << constantInfo.monitors.size() << "):\n";
    for (size_t i = 0; i < constantInfo.monitors.size(); i++) {
        const auto& monitor = constantInfo.monitors[i];
        file << "  Monitor " << (i + 1) << (monitor.isPrimary ? " (Primary)" : " (Secondary)") << ":\n"
             << "    Device Name: " << monitor.deviceName << "\n"
             << "    Display Name: " << monitor.displayName << "\n"
             << "    Resolution: " << monitor.width << " x " << monitor.height << "\n"
             << "    Refresh Rate: " << monitor.refreshRate << " Hz\n";
    }
    file << "\n";

    // Chipset Drivers
    file << "Chipset Drivers (" << constantInfo.chipsetDrivers.size() << "):\n";
    for (size_t i = 0; i < constantInfo.chipsetDrivers.size(); i++) {
        const auto& driver = constantInfo.chipsetDrivers[i];
        file << "  Driver #" << (i + 1) << ": " << driver.deviceName << "\n"
             << "    Version: " << driver.driverVersion << "\n"
             << "    Date: " << driver.driverDate << "\n"
             << "    Provider: " << driver.providerName << "\n";
    }
    file << "\n";

    // Audio Drivers
    file << "Audio Drivers (" << constantInfo.audioDrivers.size() << "):\n";
    for (size_t i = 0; i < constantInfo.audioDrivers.size(); i++) {
        const auto& driver = constantInfo.audioDrivers[i];
        file << "  Driver #" << (i + 1) << ": " << driver.deviceName << "\n"
             << "    Version: " << driver.driverVersion << "\n"
             << "    Date: " << driver.driverDate << "\n"
             << "    Provider: " << driver.providerName << "\n";
    }
    file << "\n";

    // Network Drivers
    file << "Network Drivers (" << constantInfo.networkDrivers.size() << "):\n";
    for (size_t i = 0; i < constantInfo.networkDrivers.size(); i++) {
        const auto& driver = constantInfo.networkDrivers[i];
        file << "  Driver #" << (i + 1) << ": " << driver.deviceName << "\n"
             << "    Version: " << driver.driverVersion << "\n"
             << "    Date: " << driver.driverDate << "\n"
             << "    Provider: " << driver.providerName << "\n";
    }
    file << "\n";

    // Add Rust config
    file << "Rust Configuration:\n";
    QString configPath = RustConfigFinder::findConfigFile();
    if (!configPath.isEmpty()) {
        auto config = RustConfigFinder::parseConfig(configPath);
        for (const auto& pair : config) {
            file << "  " << pair.first.toStdString() << " = " << pair.second.toStdString() << "\n";
        }
    } else {
        file << "  Config file not found\n";
    }

    file.close();
    LOG_INFO << "Saved system specs to: " << specsFileName.toStdString();
}

void BenchmarkSpecsFileManager::updateSpecsFileStatus(const QString& specsFilePath, bool isUnfinished) {
    // Read the current file
    std::ifstream inFile(specsFilePath.toStdString());
    if (!inFile.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    // Replace status line
    std::string oldStatus = isUnfinished ? "STATUS: COMPLETE - Valid benchmark"
                                        : "STATUS: INCOMPLETE - Benchmark did not finish properly";
    std::string newStatus = isUnfinished ? "STATUS: INCOMPLETE - Benchmark did not finish properly"
                                        : "STATUS: COMPLETE - Valid benchmark";

    // Replace status or add it if not found
    size_t pos = content.find(oldStatus);
    if (pos != std::string::npos) {
        content.replace(pos, oldStatus.length(), newStatus);
    } else {
        // If status line not found, add it at the beginning
        content = newStatus + "\n\n" + content;
    }

    // Write updated content
    std::ofstream outFile(specsFilePath.toStdString());
    if (outFile.is_open()) {
        outFile << content;
        outFile.close();
    }
}

QString BenchmarkSpecsFileManager::generateNewBenchmarkHash() {
    QString timeStamp = QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
    QString hashInput = timeStamp + QString::number(QRandomGenerator::global()->generate64());
    return QCryptographicHash::hash(hashInput.toUtf8(), QCryptographicHash::Md5).toHex().left(8);
}

QString BenchmarkSpecsFileManager::getSystemWarnings() {
    const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
    QString warnings;

    if (constantInfo.virtualizationEnabled) {
        warnings += "WARNING: Virtualization is enabled. Benchmark results may be inaccurate.\n";
    }

    if (!constantInfo.powerPlanHighPerf) {
        warnings += "NOTE: System is not using High Performance power plan.\n";
    }

    if (!constantInfo.gameMode) {
        warnings += "NOTE: Windows Game Mode is disabled.\n";
    }

    return warnings;
}

bool BenchmarkSpecsFileManager::checkVirtualization() {
    const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
    return constantInfo.virtualizationEnabled;
}

bool BenchmarkSpecsFileManager::checkHyperV() {
    // Check for Hyper-V services
    bool hyperVPresent = false;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Virtualization", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        hyperVPresent = true;
        RegCloseKey(hKey);
    }

    return hyperVPresent;
}

void BenchmarkSpecsFileManager::checkGameModeStatus() {
    // This information is available in ConstantSystemInfo
    // No need to implement separately
}

void BenchmarkSpecsFileManager::checkPowerPlan() {
    // This information is available in ConstantSystemInfo
    // No need to implement separately
}