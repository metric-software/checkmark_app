// THIS WILL BE THE NEW UNIFIED PROVIDER FOR CONSTANT SYSTEM INFORMATION.
// COLLECTED AT THE STARTUP OF THE APPLICATION AND THEN USED THROUGHOUT THE
// APPLICATION.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Windows.h>

namespace SystemMetrics {

// Forward declarations for GPU-related structures
struct GPUDevice {
  std::string name = "no_data";
  std::string deviceId = "no_data";
  std::string driverVersion = "no_data";
  std::string driverDate = "Unknown";
  bool hasGeForceExperience = false;
  int64_t memoryMB = -1;
  std::string vendor = "no_data";
  int pciLinkWidth = -1;
  int pcieLinkGen = -1;
  bool isPrimary = false;
};

struct MemoryModuleInfo {
  double capacityGB = -1;
  int speedMHz = -1;
  int configuredSpeedMHz = -1;
  std::string manufacturer = "no_data";
  std::string partNumber = "no_data";
  std::string memoryType = "no_data";
  std::string deviceLocator = "no_data";
  std::string formFactor = "no_data";
  std::string bankLabel = "no_data";  // Add this line
};

struct DriveInfo {
  std::string path = "no_data";
  std::string model = "no_data";
  std::string serialNumber = "no_data";
  std::string interfaceType = "no_data";
  int64_t totalSpaceGB = -1;
  int64_t freeSpaceGB = -1;
  bool isSystemDrive = false;
  bool isSSD = false;
};

struct DriverInfo {
  std::string deviceName = "no_data";
  std::string driverVersion = "no_data";
  std::string driverDate = "no_data";
  std::string providerName = "no_data";
  bool isDateValid = false;
};

struct MonitorInfo {
  std::string deviceName = "no_data";
  std::string displayName = "no_data";
  int width = -1;
  int height = -1;
  int refreshRate = -1;
  bool isPrimary = false;
};

struct ConstantSystemInfo {
  // CPU Information
  std::string cpuName = "no_data";
  std::string cpuVendor = "no_data";
  int physicalCores = -1;
  int logicalCores = -1;
  std::string cpuArchitecture = "no_data";
  std::string cpuSocket = "no_data";
  int baseClockMHz = -1;
  int maxClockMHz = -1;
  int l1CacheKB = -1;
  int l2CacheKB = -1;
  int l3CacheKB = -1;
  bool hyperThreadingSupported = false;
  bool hyperThreadingEnabled =
    false;  // New property to track if HT is actually enabled
  bool virtualizationEnabled = false;
  bool avxSupport = false;
  bool avx2Support = false;

  // Memory Information
  int64_t totalPhysicalMemoryMB = -1;
  std::string memoryType = "no_data";
  int memoryClockMHz = -1;
  bool xmpEnabled = false;
  std::string memoryChannelConfig = "no_data";
  std::vector<MemoryModuleInfo> memoryModules;

  // GPU Information
  std::vector<GPUDevice> gpuDevices;

  // Motherboard Information
  std::string motherboardManufacturer = "no_data";
  std::string motherboardModel = "no_data";
  std::string chipsetModel = "no_data";
  std::string chipsetDriverVersion = "no_data";

  // BIOS Information
  std::string biosVersion = "no_data";
  std::string biosDate = "no_data";
  std::string biosManufacturer = "no_data";

  // OS Information
  std::string osVersion = "no_data";
  std::string osBuildNumber = "no_data";
  bool isWindows11 = false;
  std::string systemName = "no_data";

  // Storage Information
  std::vector<DriveInfo> drives;

  // Monitor Information
  std::vector<MonitorInfo> monitors;

  // Power Settings
  std::string powerPlan = "no_data";
  bool powerPlanHighPerf = false;
  bool gameMode = false;

  // Page File Information
  bool pageFileExists = false;
  bool pageFileSystemManaged = false;
  double pageTotalSizeMB = 0.0;
  std::string pagePrimaryDriveLetter;
  std::vector<std::string> pageFileLocations;
  std::vector<int> pageFileCurrentSizesMB;
  std::vector<int> pageFileMaxSizesMB;

  // Driver Information
  std::vector<DriverInfo> chipsetDrivers;
  std::vector<DriverInfo> audioDrivers;
  std::vector<DriverInfo> networkDrivers;
};

// Main function to collect all system information
void CollectConstantSystemInfo();

// Function to get the collected information
const ConstantSystemInfo& GetConstantSystemInfo();

}  // namespace SystemMetrics
