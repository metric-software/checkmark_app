#pragma once

#include <functional>
#include <memory>
#include <set>  // Add missing header for std::set
#include <string>
#include <vector>

struct CPUInfo {
  std::string name;
  std::string vendor;
  int physicalCores = 0;
  int logicalCores = 0;
  bool virtualizationEnabled = false;
  bool avxSupport = false;
  bool avx2Support = false;
  double voltage = 0.0;
  int baseClockSpeed = 0;
  int currentClockSpeed = 0;
  int maxClockSpeed = 0;
  int performancePercentage = 0;
  double loadPercentage = 0.0;
  double temperature = 0.0;
  bool smtActive = false;
  std::string powerPlan;

  // Additional CPU metrics
  std::vector<double> coreVoltages;
  std::vector<int> coreClocks;
  std::vector<double> coreTemperatures;
  std::vector<double> coreLoads;
  double packagePower = 0.0;
  double socketPower = 0.0;
  std::string cacheSizes;
  std::string architecture;
  std::string socket;
  int tjMax = 0;
  std::vector<double> corePowers;
};

struct GPUInfo {
  std::string name;
  double temperature = 0.0;
  double load = 0.0;
  double memoryUsed = 0.0;
  double memoryTotal = 0.0;
  int coreClock = 0;
  int memoryClock = 0;
  double powerUsage = 0.0;
  double fanSpeed = 0.0;

  std::string driver;
  double hotSpotTemp = 0.0;
  double memoryTemp = 0.0;
  double vrm1Temp = 0.0;
  int pcieLinkWidth = 0;
  int pcieLinkGen = 0;
  std::vector<double> fanSpeeds;
  double memoryControllerLoad = 0.0;
  double videoEngineLoad = 0.0;
  double busInterface = 0.0;
  double powerLimit = 0.0;
};

struct RAMInfo {
  double used = 0.0;
  double total = 0.0;
  double available = 0.0;
  int memoryType = 0;
  int clockSpeed = 0;

  double timingCL = 0.0;
  double timingRCD = 0.0;
  double timingRP = 0.0;
  double timingRAS = 0.0;
  std::string formFactor;
  int channels = 0;
  std::vector<int> slotClockSpeeds;
  std::vector<double> slotLoads;
};

struct MemoryModuleInfo {
  double capacityGB = 0.0;
  int speedMHz = 0;
  int configuredSpeedMHz = 0;
  std::string manufacturer;
  std::string partNumber;
  std::string memoryType;
  std::string xmpStatus;
  std::string deviceLocator;
  int formFactor = 0;     // Add missing formFactor member
  std::string bankLabel;  // Add missing bankLabel member
};

class WinHardwareMonitor {
 public:
  WinHardwareMonitor();
  ~WinHardwareMonitor();

  // Same interface as HardwareMonitor
  CPUInfo getCPUInfo();
  GPUInfo getGPUInfo();
  RAMInfo getRAMInfo();

  void updateSensors();
  std::string printAllCPUInfo();

  void getDetailedMemoryInfo(std::vector<MemoryModuleInfo>& modules,
                             std::string& channelStatus, bool& xmpEnabled);

  std::string logRawData();

 private:
  // Implementation will be different but keep same interface
  class Impl;
  std::unique_ptr<Impl> pImpl;
};
