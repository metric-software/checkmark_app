#pragma once

#include <string>
#include <vector>

// SystemWrapper - provides access to system metrics not available in other
// wrappers
class SystemWrapper {
 public:
  // Drive information struct
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

  // Page file information struct
  struct PageFileInfo {
    bool exists = false;
    bool systemManaged = false;
    double totalSizeMB = 0;
    std::string primaryDriveLetter;
    std::vector<std::string> locations;
    std::vector<int> currentSizesMB;
    std::vector<int> maxSizesMB;
  };

  // Monitor information struct
  struct MonitorInfo {
    std::string deviceName;
    std::string displayName;
    int width = -1;
    int height = -1;
    int refreshRate = -1;
    bool isPrimary = false;
  };

  // Driver information struct
  struct DriverInfo {
    std::string deviceName;
    std::string driverVersion;
    std::string driverDate;
    std::string providerName;
    bool isDateValid = false;
  };

  SystemWrapper();
  ~SystemWrapper();

  // CPU cache information
  int getL1CacheKB(int physicalCores = -1);
  int getL2CacheKB();
  int getL3CacheKB();

  // Motherboard & chipset information
  std::pair<std::string, std::string> getMotherboardInfo();
  std::pair<bool, std::string> getChipsetDriverInfo();
  std::string getChipsetModel();

  // BIOS information
  std::tuple<std::string, std::string, std::string> getBiosInfo();

  // Drive information
  std::vector<DriveInfo> getDriveInfo();
  PageFileInfo getPageFileInfo();

  // Power settings information
  std::string getPowerPlan();
  bool isHighPerformancePowerPlan();
  bool isGameModeEnabled();

  // Monitor information
  std::vector<MonitorInfo> getMonitorInfo();

  // Driver information
  std::vector<DriverInfo> getDriverInfo(const std::string& deviceClass = "");
  std::vector<DriverInfo> getChipsetDriverDetails();
  std::vector<DriverInfo> getAudioDriverDetails();
  std::vector<DriverInfo> getNetworkDriverDetails();

  std::string logRawData();

 private:
  // Helper methods for raw data logging
  std::string logRawWmiData();
  std::string logRawRegistryData();
  std::string logRawAPIData();
  std::string logRawDriverData();
};
