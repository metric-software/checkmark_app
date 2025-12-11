// THIS CODE WILL COLLECT ALL THE CONSTANT SYSTEM INFORMATION ONCE AT STARTUP.
// MORE STUFF CAN BE ADDED (LIKE SYSTEM SETTINGS, ETC) BUT LETS NOT ADD TOO MUCH
// STUFF IF IT STARTS TO SLOW DOWN THE STARTUP.

#include "ConstantSystemInfo.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "../logging/Logger.h"

#include <Windows.h>
#include <Powrprof.h>
#include <comdef.h>
#include <dxgi.h>
#include <intrin.h>
#include <wbemidl.h>
#include <winreg.h>

#include "hardware/NvidiaMetrics.h"
#include "hardware/SystemWrapper.h"
#include "hardware/WinHardwareMonitor.h"

namespace {
// Helper function for wstring to string conversion
std::string wstringToString(const std::wstring& wstr) {
  if (wstr.empty()) return std::string();

  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
                                        (int)wstr.size(), NULL, 0, NULL, NULL);
  std::string result(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0],
                      size_needed, NULL, NULL);
  return result;
}

// Global instance
SystemMetrics::ConstantSystemInfo g_constantSystemInfo;

// Helper for RAII-based COM initialization
class ComInitializer {
 public:
  ComInitializer() : m_initialized(false) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    m_initialized = SUCCEEDED(hr);
  }

  ~ComInitializer() {
    if (m_initialized) {
      CoUninitialize();
    }
  }

  bool isInitialized() const { return m_initialized; }

 private:
  bool m_initialized;
};

// Helper for WMI queries
class WmiHelper {
 public:
  WmiHelper() : m_pLoc(nullptr), m_pSvc(nullptr), m_initialized(false) {
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, (LPVOID*)&m_pLoc);

    if (FAILED(hr)) {
      LOG_ERROR << "Failed to create WbemLocator";
      return;
    }

    hr = m_pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, 0,
                               NULL, 0, 0, &m_pSvc);

    if (FAILED(hr)) {
      LOG_ERROR << "Failed to connect to WMI";
      m_pLoc->Release();
      m_pLoc = nullptr;
      return;
    }

    hr = CoSetProxyBlanket(m_pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                           NULL, EOAC_NONE);

    if (FAILED(hr)) {
      LOG_ERROR << "Failed to set proxy blanket";
      m_pSvc->Release();
      m_pLoc->Release();
      m_pSvc = nullptr;
      m_pLoc = nullptr;
      return;
    }

    m_initialized = true;
  }

  ~WmiHelper() {
    if (m_pSvc) m_pSvc->Release();
    if (m_pLoc) m_pLoc->Release();
  }

  IEnumWbemClassObject* executeQuery(const std::wstring& query) {
    if (!m_initialized) return nullptr;

    IEnumWbemClassObject* pEnumerator = nullptr;
    HRESULT hr =
      m_pSvc->ExecQuery(bstr_t("WQL"), bstr_t(query.c_str()),
                        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                        NULL, &pEnumerator);

    if (FAILED(hr) || !pEnumerator) {
      // Convert wstring to string before output
      std::string queryStr = wstringToString(query);
      LOG_ERROR << "Query execution failed: " << queryStr;
      return nullptr;
    }

    return pEnumerator;
  }

  bool isInitialized() const { return m_initialized; }

 private:
  IWbemLocator* m_pLoc;
  IWbemServices* m_pSvc;
  bool m_initialized;
};

// Helper function to safely execute a COM operation and release pointers
template <typename T, typename Func>
void withWmiObject(IEnumWbemClassObject* pEnumerator, Func func) {
  if (!pEnumerator) return;

  IWbemClassObject* pclsObj = nullptr;
  ULONG uReturn = 0;

  while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK) {
    func(pclsObj);
    pclsObj->Release();
  }

  pEnumerator->Release();
}

// Helper to get value from registry
std::string getRegistryString(HKEY hKey, const char* subKey,
                              const char* valueName) {
  HKEY hSubKey;
  if (RegOpenKeyExA(hKey, subKey, 0, KEY_READ, &hSubKey) != ERROR_SUCCESS) {
    return "no_data";
  }

  char buffer[512];
  DWORD bufferSize = sizeof(buffer);
  DWORD valueType;

  if (RegQueryValueExA(hSubKey, valueName, NULL, &valueType, (LPBYTE)buffer,
                       &bufferSize) != ERROR_SUCCESS) {
    RegCloseKey(hSubKey);
    return "no_data";
  }

  RegCloseKey(hSubKey);
  return std::string(buffer);
}

// Replace these functions with ones that use the comprehensive HardwareMonitor
// methods

void collectCpuInfo() {
  // Initialize with "no data" values
  g_constantSystemInfo.cpuName = "no_data";
  g_constantSystemInfo.cpuVendor = "no_data";
  g_constantSystemInfo.physicalCores = -1;
  g_constantSystemInfo.logicalCores = -1;
  g_constantSystemInfo.baseClockMHz = -1;
  g_constantSystemInfo.maxClockMHz = -1;
  g_constantSystemInfo.avxSupport = false;
  g_constantSystemInfo.avx2Support = false;
  g_constantSystemInfo.hyperThreadingEnabled = false;
  g_constantSystemInfo.cpuArchitecture = "no_data";
  g_constantSystemInfo.cpuSocket = "no_data";
  g_constantSystemInfo.virtualizationEnabled = false;
  g_constantSystemInfo.l1CacheKB = -1;
  g_constantSystemInfo.l2CacheKB = -1;
  g_constantSystemInfo.l3CacheKB = -1;

  // Create a single WinHardwareMonitor instance
  WinHardwareMonitor hardwareMonitor;

  // Get all CPU information in a single call
  CPUInfo cpuInfo = hardwareMonitor.getCPUInfo();

  // Create SystemWrapper for additional metrics
  SystemWrapper sysWrapper;

  // Map the returned info to the constant system info structure
  g_constantSystemInfo.cpuName = cpuInfo.name;
  g_constantSystemInfo.cpuVendor = cpuInfo.vendor;
  g_constantSystemInfo.physicalCores = cpuInfo.physicalCores;
  g_constantSystemInfo.logicalCores = cpuInfo.logicalCores;
  g_constantSystemInfo.baseClockMHz = cpuInfo.baseClockSpeed;
  g_constantSystemInfo.maxClockMHz = cpuInfo.maxClockSpeed;
  g_constantSystemInfo.avxSupport = cpuInfo.avxSupport;
  g_constantSystemInfo.avx2Support = cpuInfo.avx2Support;
  g_constantSystemInfo.hyperThreadingEnabled = cpuInfo.smtActive;
  g_constantSystemInfo.cpuSocket = cpuInfo.socket;
  g_constantSystemInfo.virtualizationEnabled = cpuInfo.virtualizationEnabled;

  // AMD architecture correction: If socket is AM5, it's definitely Zen4
  if (cpuInfo.socket == "AM5") {
    g_constantSystemInfo.cpuArchitecture = "Zen4";
  } else {
    g_constantSystemInfo.cpuArchitecture = cpuInfo.architecture;
  }

  // Get L1 cache size using SystemWrapper
  g_constantSystemInfo.l1CacheKB =
    sysWrapper.getL1CacheKB(cpuInfo.physicalCores);

  // Parse cache sizes if available for L2 and L3
  if (!cpuInfo.cacheSizes.empty()) {
    size_t l2Pos = cpuInfo.cacheSizes.find("L2: ");
    size_t l3Pos = cpuInfo.cacheSizes.find("L3: ");

    if (l2Pos != std::string::npos) {
      size_t l2End = cpuInfo.cacheSizes.find(" KB", l2Pos);
      if (l2End != std::string::npos) {
        std::string l2Str =
          cpuInfo.cacheSizes.substr(l2Pos + 4, l2End - (l2Pos + 4));
        try {
          g_constantSystemInfo.l2CacheKB = std::stoi(l2Str);
        } catch (...) {
          // Parsing failed
        }
      }
    }

    if (l3Pos != std::string::npos) {
      size_t l3End = cpuInfo.cacheSizes.find(" KB", l3Pos);
      if (l3End != std::string::npos) {
        std::string l3Str =
          cpuInfo.cacheSizes.substr(l3Pos + 4, l3End - (l3Pos + 4));
        try {
          g_constantSystemInfo.l3CacheKB = std::stoi(l3Str);
        } catch (...) {
          // Parsing failed
        }
      }
    }
  }

  // Force sensor update before printing detailed CPU info
}

// Update collectGpuInfo function to include driver date and GeForce Experience
// status
void collectGpuInfo() {
  // First, try to collect GPU info using NvidiaMetricsCollector
  bool nvmlSuccess = false;

#ifndef NO_NVML_SUPPORT
  try {
    NvidiaMetricsCollector nvCollector;

    // Connect to error signal with lambda to capture errors
    std::string nvmlError;
    QObject::connect(
      &nvCollector, &NvidiaMetricsCollector::collectionError,
      [&nvmlError](const QString& error) { nvmlError = error.toStdString(); });

    auto gpuHandles = nvCollector.getAvailableGPUs();
    if (!gpuHandles
           .empty()) {  // This checks if NVML initialization was successful
      for (size_t i = 0; i < gpuHandles.size(); i++) {
        NvidiaGPUMetrics metrics;
        if (nvCollector.getMetricsForDevice(gpuHandles[i], metrics)) {
          // Check if we already have this GPU in our list
          bool found = false;
          for (auto& gpu : g_constantSystemInfo.gpuDevices) {
            if (gpu.name.find(metrics.name) != std::string::npos ||
                metrics.name.find(gpu.name) != std::string::npos) {
              // Update with more detailed information from NvidiaMetrics
              gpu.memoryMB =
                static_cast<int64_t>(metrics.totalMemory / (1024 * 1024));
              gpu.deviceId = metrics.deviceId;
              gpu.driverVersion = metrics.driverVersion;
              gpu.driverDate = metrics.driverDate;
              gpu.hasGeForceExperience = metrics.hasGeForceExperience;
              gpu.pciLinkWidth = metrics.pciLinkWidth;
              gpu.pcieLinkGen = metrics.pcieLinkGen;
              gpu.vendor = "NVIDIA";
              found = true;
              break;
            }
          }

          // Add GPU if it wasn't found and is valid
          if (!found &&
              metrics.name.find("Microsoft Basic") == std::string::npos) {
            SystemMetrics::GPUDevice gpu;
            gpu.name = metrics.name;
            gpu.memoryMB =
              static_cast<int64_t>(metrics.totalMemory / (1024 * 1024));
            gpu.deviceId = metrics.deviceId;
            gpu.driverVersion = metrics.driverVersion;
            gpu.driverDate = metrics.driverDate;
            gpu.hasGeForceExperience = metrics.hasGeForceExperience;
            gpu.pciLinkWidth = metrics.pciLinkWidth;
            gpu.pcieLinkGen = metrics.pcieLinkGen;
            gpu.vendor = "NVIDIA";
            gpu.isPrimary = (i == 0);  // Assume first GPU is primary

            g_constantSystemInfo.gpuDevices.push_back(gpu);
          }

          nvmlSuccess = true;
        }
      }
    }
  } catch (...) {
    // Silently handle exceptions from NVML
  }
#endif

  // Fall back to WinHardwareMonitor if NVML failed or is not available
  if (!nvmlSuccess) {
    // Create a single WinHardwareMonitor instance
    WinHardwareMonitor hardwareMonitor;

    // Get all GPU information in a single call
    GPUInfo gpuInfo = hardwareMonitor.getGPUInfo();

    // Check if we already have this GPU in our list or if we need to add it
    if (!gpuInfo.name.empty()) {
      bool found = false;
      for (auto& gpu : g_constantSystemInfo.gpuDevices) {
        if (gpu.name.find(gpuInfo.name) != std::string::npos ||
            gpuInfo.name.find(gpu.name) != std::string::npos) {
          // Update with more detailed information from HardwareMonitor
          gpu.memoryMB = static_cast<int64_t>(gpuInfo.memoryTotal *
                                              1024);  // Convert GB to MB
          gpu.pciLinkWidth = gpuInfo.pcieLinkWidth;
          gpu.pcieLinkGen = gpuInfo.pcieLinkGen;
          found = true;
          break;
        }
      }

      // Add GPU if it wasn't found and is valid
      if (!found && gpuInfo.name.find("Microsoft Basic") == std::string::npos) {
        SystemMetrics::GPUDevice gpu;
        gpu.name = gpuInfo.name;
        gpu.memoryMB = static_cast<int64_t>(gpuInfo.memoryTotal * 1024);
        gpu.pciLinkWidth = gpuInfo.pcieLinkWidth;
        gpu.pcieLinkGen = gpuInfo.pcieLinkGen;
        gpu.isPrimary = true;  // Assume first GPU is primary

        g_constantSystemInfo.gpuDevices.push_back(gpu);
      }
    }
  }
}

void collectMemoryInfo() {
  // Initialize with "no data" values
  g_constantSystemInfo.totalPhysicalMemoryMB = -1;
  g_constantSystemInfo.memoryType = "no_data";
  g_constantSystemInfo.memoryClockMHz = -1;
  g_constantSystemInfo.xmpEnabled = false;
  g_constantSystemInfo.memoryChannelConfig = "no_data";
  g_constantSystemInfo.memoryModules.clear();

  // Create a single WinHardwareMonitor instance
  WinHardwareMonitor hardwareMonitor;

  // Get all RAM information in a single call
  RAMInfo ramInfo = hardwareMonitor.getRAMInfo();

  // Get detailed memory module information
  std::vector<MemoryModuleInfo> moduleInfo;
  std::string channelStatus;
  bool xmpEnabled = false;

  hardwareMonitor.getDetailedMemoryInfo(moduleInfo, channelStatus, xmpEnabled);

  // Map the returned info to the constant system info structure
  g_constantSystemInfo.totalPhysicalMemoryMB =
    static_cast<int64_t>(ramInfo.total * 1024);

  // MODIFIED APPROACH: First try to determine memory type from modules
  // as they are more reliable for DDR5 detection
  bool memoryTypeFound = false;
  if (!moduleInfo.empty()) {
    // Use the type from the first module that has a valid memory type
    for (const auto& module : moduleInfo) {
      if (!module.memoryType.empty() && module.memoryType != "-1") {
        g_constantSystemInfo.memoryType = module.memoryType;
        memoryTypeFound = true;
        break;
      }
    }
  }

  // Only fall back to ramInfo if we couldn't determine from modules
  if (!memoryTypeFound) {
    if (ramInfo.memoryType == 26) {
      g_constantSystemInfo.memoryType = "DDR4";
    } else if (ramInfo.memoryType == 27) {
      g_constantSystemInfo.memoryType = "DDR5";
    } else if (ramInfo.memoryType > 0) {
      g_constantSystemInfo.memoryType =
        "DDR" + std::to_string(ramInfo.memoryType);
    }
  }

  // Similarly for clock speed, use module info if main RAM info is missing
  if (ramInfo.clockSpeed <= 0 && !moduleInfo.empty()) {
    // Find the highest configured clock speed among modules
    int maxClockSpeed = 0;
    for (const auto& module : moduleInfo) {
      if (module.configuredSpeedMHz > maxClockSpeed) {
        maxClockSpeed = module.configuredSpeedMHz;
      }
    }

    if (maxClockSpeed > 0) {
      g_constantSystemInfo.memoryClockMHz = maxClockSpeed;
    } else {
      g_constantSystemInfo.memoryClockMHz = ramInfo.clockSpeed;
    }
  } else {
    g_constantSystemInfo.memoryClockMHz = ramInfo.clockSpeed;
  }

  g_constantSystemInfo.xmpEnabled = xmpEnabled;
  g_constantSystemInfo.memoryChannelConfig = channelStatus;

  // Convert memory modules
  g_constantSystemInfo.memoryModules.clear();
  for (const auto& module : moduleInfo) {
    SystemMetrics::MemoryModuleInfo sysMemModule;

    sysMemModule.capacityGB = module.capacityGB;
    sysMemModule.speedMHz = module.speedMHz;
    sysMemModule.configuredSpeedMHz = module.configuredSpeedMHz;
    sysMemModule.manufacturer = module.manufacturer;
    sysMemModule.partNumber = module.partNumber;
    sysMemModule.memoryType = module.memoryType;
    sysMemModule.deviceLocator = module.deviceLocator;
    sysMemModule.formFactor =
      std::to_string(module.formFactor);        // Convert int to string
    sysMemModule.bankLabel = module.bankLabel;  // Copy the bank label

    g_constantSystemInfo.memoryModules.push_back(sysMemModule);
  }
}

// For collectMotherboardInfo function

void collectMotherboardInfo() {
  // Initialize with "no data" values
  g_constantSystemInfo.motherboardManufacturer = "no_data";
  g_constantSystemInfo.motherboardModel = "no_data";
  g_constantSystemInfo.chipsetDriverVersion = "no_data";
  g_constantSystemInfo.chipsetModel = "no_data";

  // Create SystemWrapper to get motherboard information
  SystemWrapper sysWrapper;

  // Get motherboard manufacturer and model
  auto [manufacturer, model] = sysWrapper.getMotherboardInfo();
  g_constantSystemInfo.motherboardManufacturer = manufacturer;
  g_constantSystemInfo.motherboardModel = model;

  // Get chipset information
  auto [chipsetDriverInstalled, chipsetDriverVersion] =
    sysWrapper.getChipsetDriverInfo();

  if (chipsetDriverInstalled) {
    g_constantSystemInfo.chipsetDriverVersion = chipsetDriverVersion;
    g_constantSystemInfo.chipsetModel = sysWrapper.getChipsetModel();
  }
}

void collectBiosInfo() {
  // Initialize with "no data" values
  g_constantSystemInfo.biosVersion = "no_data";
  g_constantSystemInfo.biosDate = "no_data";
  g_constantSystemInfo.biosManufacturer = "no_data";

  // Use SystemWrapper to get BIOS information
  SystemWrapper sysWrapper;

  // Get BIOS version, date and manufacturer
  auto [version, date, manufacturer] = sysWrapper.getBiosInfo();
  g_constantSystemInfo.biosVersion = version;
  g_constantSystemInfo.biosDate = date;
  g_constantSystemInfo.biosManufacturer = manufacturer;
}

void collectOsInfo() {
  // Initialize with "no data" values
  g_constantSystemInfo.osVersion = "no_data";
  g_constantSystemInfo.osBuildNumber = "no_data";
  g_constantSystemInfo.isWindows11 = false;
  g_constantSystemInfo.systemName = "no_data";
  g_constantSystemInfo.gameMode = false;
  g_constantSystemInfo.powerPlan = "no_data";
  g_constantSystemInfo.powerPlanHighPerf = false;

  // Get Windows version using RtlGetVersion (more reliable than GetVersionEx)
  typedef LONG NTSTATUS;
  typedef struct _RTL_OSVERSIONINFOW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
  } RTL_OSVERSIONINFOW, *PRTL_OSVERSIONINFOW;

  typedef NTSTATUS(WINAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);

  RTL_OSVERSIONINFOW osvi = {sizeof(osvi)};
  HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");

  if (hNtDll) {
    auto pRtlGetVersion = reinterpret_cast<RtlGetVersionPtr>(
      GetProcAddress(hNtDll, "RtlGetVersion"));
    if (pRtlGetVersion) {
      if (pRtlGetVersion(&osvi) == 0) {
        g_constantSystemInfo.osBuildNumber = std::to_string(osvi.dwBuildNumber);

        // Determine OS version string and Windows 11 status
        if (osvi.dwBuildNumber >= 22000) {
          g_constantSystemInfo.osVersion = "Windows 11";
          g_constantSystemInfo.isWindows11 = true;
        } else if (osvi.dwMajorVersion == 10) {
          g_constantSystemInfo.osVersion = "Windows 10";
        } else if (osvi.dwMajorVersion == 6) {
          switch (osvi.dwMinorVersion) {
            case 3:
              g_constantSystemInfo.osVersion = "Windows 8.1";
              break;
            case 2:
              g_constantSystemInfo.osVersion = "Windows 8";
              break;
            case 1:
              g_constantSystemInfo.osVersion = "Windows 7";
              break;
            default:
              g_constantSystemInfo.osVersion = "Windows 6.x";
          }
        } else {
          g_constantSystemInfo.osVersion =
            "Windows " + std::to_string(osvi.dwMajorVersion) + "." +
            std::to_string(osvi.dwMinorVersion);
        }
      }
    }
  }

  // Get computer name
  wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD nameSize = MAX_COMPUTERNAME_LENGTH + 1;
  if (GetComputerNameW(computerName, &nameSize)) {
    int len = WideCharToMultiByte(CP_UTF8, 0, computerName, -1, nullptr, 0,
                                  nullptr, nullptr);
    if (len > 0) {
      std::string name(len, 0);
      WideCharToMultiByte(CP_UTF8, 0, computerName, -1, &name[0], len, nullptr,
                          nullptr);
      g_constantSystemInfo.systemName = name.c_str();  // Remove trailing null
    }
  }

  // Check if Game Mode is enabled
  HKEY hKey;
  DWORD gameMode = 0;
  DWORD dataSize = sizeof(DWORD);

  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", 0,
                    KEY_READ, &hKey) == ERROR_SUCCESS) {
    if (RegQueryValueExW(hKey, L"AutoGameMode", 0, NULL, (LPBYTE)&gameMode,
                         &dataSize) == ERROR_SUCCESS) {
      g_constantSystemInfo.gameMode = (gameMode == 1);
    }
    RegCloseKey(hKey);
  }

  // Check power plan
  GUID* pActiveScheme = NULL;
  if (PowerGetActiveScheme(NULL, &pActiveScheme) == ERROR_SUCCESS) {
    // Check if it's the high performance plan
    GUID highPerfGuid = {0x8c5e7fda,
                         0xe8bf,
                         0x4a96,
                         {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x6b, 0x83, 0x1e}};
    g_constantSystemInfo.powerPlanHighPerf =
      (memcmp(pActiveScheme, &highPerfGuid, sizeof(GUID)) == 0);

    // Get plan name
    DWORD nameSize = 0;
    PowerReadFriendlyName(NULL, pActiveScheme, NULL, NULL, NULL, &nameSize);
    if (nameSize > 0) {
      std::vector<wchar_t> nameBuf(nameSize / sizeof(wchar_t));
      if (PowerReadFriendlyName(NULL, pActiveScheme, NULL, NULL,
                                (UCHAR*)nameBuf.data(),
                                &nameSize) == ERROR_SUCCESS) {
        int len = WideCharToMultiByte(CP_UTF8, 0, nameBuf.data(), -1, nullptr,
                                      0, nullptr, nullptr);
        if (len > 0) {
          std::string name(len, 0);
          WideCharToMultiByte(CP_UTF8, 0, nameBuf.data(), -1, &name[0], len,
                              nullptr, nullptr);
          g_constantSystemInfo.powerPlan =
            name.c_str();  // Remove trailing null
        }
      }
    }
    LocalFree(pActiveScheme);
  }
}

void collectDriveInfo() {
  SystemWrapper sysWrapper;

  // Get drive information
  auto sysWrapperDrives = sysWrapper.getDriveInfo();

  // Convert from SystemWrapper::DriveInfo to SystemMetrics::DriveInfo
  g_constantSystemInfo.drives.clear();
  for (const auto& drive : sysWrapperDrives) {
    SystemMetrics::DriveInfo driveInfo;
    driveInfo.path = drive.path;
    driveInfo.model = drive.model;
    driveInfo.serialNumber = drive.serialNumber;
    driveInfo.interfaceType = drive.interfaceType;
    driveInfo.totalSpaceGB = drive.totalSpaceGB;
    driveInfo.freeSpaceGB = drive.freeSpaceGB;
    driveInfo.isSystemDrive = drive.isSystemDrive;
    driveInfo.isSSD = drive.isSSD;

    g_constantSystemInfo.drives.push_back(driveInfo);
  }
}

void collectPowerInfo() {
  SystemWrapper sysWrapper;

  // Get power plan
  g_constantSystemInfo.powerPlan = sysWrapper.getPowerPlan();

  // Check if high performance plan is active
  g_constantSystemInfo.powerPlanHighPerf =
    sysWrapper.isHighPerformancePowerPlan();

  // Check if game mode is enabled
  g_constantSystemInfo.gameMode = sysWrapper.isGameModeEnabled();
}

// Add this function after collectPowerInfo()

void collectPageFileInfo() {
  SystemWrapper sysWrapper;

  // Get page file information using SystemWrapper
  auto pageFileInfo = sysWrapper.getPageFileInfo();

  // Map the returned info to the constant system info structure
  g_constantSystemInfo.pageFileExists = pageFileInfo.exists;
  g_constantSystemInfo.pageFileSystemManaged = pageFileInfo.systemManaged;
  g_constantSystemInfo.pageTotalSizeMB = pageFileInfo.totalSizeMB;
  g_constantSystemInfo.pagePrimaryDriveLetter = pageFileInfo.primaryDriveLetter;
  g_constantSystemInfo.pageFileLocations = pageFileInfo.locations;
  g_constantSystemInfo.pageFileCurrentSizesMB = pageFileInfo.currentSizesMB;
  g_constantSystemInfo.pageFileMaxSizesMB = pageFileInfo.maxSizesMB;
}

// Modify the collectDriverInfo function to handle type conversion properly

void collectDriverInfo() {
  SystemWrapper sysWrapper;

  // Helper function to convert SystemWrapper::DriverInfo to
  // SystemMetrics::DriverInfo
  auto convertDriverInfo =
    [](const std::vector<SystemWrapper::DriverInfo>& wrapperDrivers) {
      std::vector<SystemMetrics::DriverInfo> result;
      for (const auto& driver : wrapperDrivers) {
        SystemMetrics::DriverInfo newDriver;
        newDriver.deviceName = driver.deviceName;
        newDriver.driverVersion = driver.driverVersion;
        newDriver.driverDate = driver.driverDate;
        newDriver.providerName = driver.providerName;
        newDriver.isDateValid = driver.isDateValid;
        result.push_back(newDriver);
      }
      return result;
    };

  // Collect chipset drivers
  LOG_DEBUG << "Collecting chipset driver information...";
  auto chipsetDrivers = sysWrapper.getChipsetDriverDetails();
  g_constantSystemInfo.chipsetDrivers = convertDriverInfo(chipsetDrivers);
  LOG_INFO << "Found " << g_constantSystemInfo.chipsetDrivers.size()
            << " chipset drivers";

  // Collect audio drivers
  LOG_DEBUG << "Collecting audio driver information...";
  auto audioDrivers = sysWrapper.getAudioDriverDetails();
  g_constantSystemInfo.audioDrivers = convertDriverInfo(audioDrivers);
  LOG_INFO << "Found " << g_constantSystemInfo.audioDrivers.size()
            << " audio drivers";

  // Collect network drivers
  LOG_DEBUG << "Collecting network driver information...";
  auto networkDrivers = sysWrapper.getNetworkDriverDetails();
  g_constantSystemInfo.networkDrivers = convertDriverInfo(networkDrivers);
  LOG_INFO << "Found " << g_constantSystemInfo.networkDrivers.size()
            << " network drivers";

  // Log detailed information about collected drivers
  for (const auto& driver : g_constantSystemInfo.chipsetDrivers) {
    LOG_INFO << "Chipset driver: " << driver.deviceName;
    LOG_INFO << "  Version: " << driver.driverVersion;
    LOG_INFO << "  Date: "
              << (driver.isDateValid ? driver.driverDate : "Unknown")
             ;
    LOG_INFO << "  Provider: " << driver.providerName;
  }

  for (const auto& driver : g_constantSystemInfo.audioDrivers) {
    LOG_INFO << "Audio driver: " << driver.deviceName;
    LOG_INFO << "  Version: " << driver.driverVersion;
    LOG_INFO << "  Date: "
              << (driver.isDateValid ? driver.driverDate : "Unknown")
             ;
    LOG_INFO << "  Provider: " << driver.providerName;
  }

  for (const auto& driver : g_constantSystemInfo.networkDrivers) {
    LOG_INFO << "Network driver: " << driver.deviceName;
    LOG_INFO << "  Version: " << driver.driverVersion;
    LOG_INFO << "  Date: "
              << (driver.isDateValid ? driver.driverDate : "Unknown")
             ;
    LOG_INFO << "  Provider: " << driver.providerName;
  }
}

// Add this function to print all metrics
void printCollectedSystemInfo() {
  // Ensure all output is in decimal format
  LOG_INFO << std::dec;

  LOG_INFO << "\n===== CONSTANT SYSTEM INFORMATION =====\n";

  // CPU Information
  LOG_INFO << "\n----- CPU Information -----\n";
  LOG_INFO << "CPU Name: " << g_constantSystemInfo.cpuName << "\n";
  LOG_INFO << "CPU Vendor: " << g_constantSystemInfo.cpuVendor << "\n";
  LOG_INFO << "Physical Cores: " << g_constantSystemInfo.physicalCores << "\n";
  LOG_INFO << "Logical Cores: " << g_constantSystemInfo.logicalCores << "\n";
  LOG_INFO << "CPU Architecture: " << g_constantSystemInfo.cpuArchitecture
            << "\n";
  LOG_INFO << "CPU Socket: " << g_constantSystemInfo.cpuSocket << "\n";
  LOG_INFO << "Base Clock (MHz): " << g_constantSystemInfo.baseClockMHz
            << "\n";
  LOG_INFO << "L1 Cache (KB): " << g_constantSystemInfo.l1CacheKB << "\n";
  LOG_INFO << "L2 Cache (KB): " << g_constantSystemInfo.l2CacheKB << "\n";
  LOG_INFO << "L3 Cache (KB): " << g_constantSystemInfo.l3CacheKB << "\n";
  LOG_INFO << "Hyper-Threading: "
            << (g_constantSystemInfo.hyperThreadingEnabled ? "Enabled"
                                                           : "Disabled")
            << "\n";
  LOG_INFO << "Virtualization Enabled: "
            << (g_constantSystemInfo.virtualizationEnabled ? "Yes" : "No")
            << "\n";
  LOG_INFO << "AVX Support: "
            << (g_constantSystemInfo.avxSupport ? "Yes" : "No") << "\n";
  LOG_INFO << "AVX2 Support: "
            << (g_constantSystemInfo.avx2Support ? "Yes" : "No") << "\n";

  // Memory Information
  LOG_INFO << "\n----- Memory Information -----\n";
  LOG_INFO << "Total Physical Memory (MB): "
            << g_constantSystemInfo.totalPhysicalMemoryMB << "\n";
  LOG_INFO << "Memory Type: " << g_constantSystemInfo.memoryType << "\n";
  LOG_INFO << "Memory Clock (MHz): " << g_constantSystemInfo.memoryClockMHz
            << "\n";
  LOG_INFO << "XMP Enabled: "
            << (g_constantSystemInfo.xmpEnabled ? "Yes" : "No") << "\n";
  LOG_INFO << "Memory Channel Config: "
            << g_constantSystemInfo.memoryChannelConfig << "\n";

  // Print memory modules
  LOG_INFO << "\n----- Memory Modules ("
            << g_constantSystemInfo.memoryModules.size() << ") -----\n";
  for (size_t i = 0; i < g_constantSystemInfo.memoryModules.size(); i++) {
    const auto& module = g_constantSystemInfo.memoryModules[i];
    LOG_INFO << "Module #" << (i + 1) << ":\n";
    LOG_INFO << "  Capacity (GB): " << module.capacityGB << "\n";
    LOG_INFO << "  Speed (MHz): " << module.speedMHz << "\n";
    LOG_INFO << "  Configured Speed (MHz): " << module.configuredSpeedMHz
              << "\n";
    LOG_INFO << "  Manufacturer: " << module.manufacturer << "\n";
    LOG_INFO << "  Part Number: " << module.partNumber << "\n";
    LOG_INFO << "  Memory Type: " << module.memoryType << "\n";
    LOG_INFO << "  Device Locator: " << module.deviceLocator << "\n";
    LOG_INFO << "  Form Factor: " << module.formFactor << "\n";
  }

  // GPU Information
  LOG_INFO << "\n----- GPU Devices (" << g_constantSystemInfo.gpuDevices.size()
            << ") -----\n";
  for (size_t i = 0; i < g_constantSystemInfo.gpuDevices.size(); i++) {
    const auto& gpu = g_constantSystemInfo.gpuDevices[i];
    LOG_INFO << "GPU #" << (i + 1) << " ("
              << (gpu.isPrimary ? "Primary" : "Secondary") << "):\n";
    LOG_INFO << "  Name: " << gpu.name << "\n";
    LOG_INFO << "  Device ID: " << gpu.deviceId << "\n";
    LOG_INFO << "  Driver Version: " << gpu.driverVersion << "\n";
    LOG_INFO << "  Driver Date: " << gpu.driverDate << "\n";
    LOG_INFO << "  Has GeForce Experience: "
              << (gpu.hasGeForceExperience ? "Yes" : "No") << "\n";
    LOG_INFO << "  Memory (MB): " << gpu.memoryMB << "\n";
    LOG_INFO << "  Vendor: " << gpu.vendor << "\n";
    LOG_INFO << "  PCI Link Width: " << gpu.pciLinkWidth << "\n";
    LOG_INFO << "  PCIe Link Gen: " << gpu.pcieLinkGen << "\n";
  }

  // Motherboard Information
  LOG_INFO << "\n----- Motherboard Information -----\n";
  LOG_INFO << "Manufacturer: " << g_constantSystemInfo.motherboardManufacturer
            << "\n";
  LOG_INFO << "Model: " << g_constantSystemInfo.motherboardModel << "\n";
  LOG_INFO << "Chipset Model: " << g_constantSystemInfo.chipsetModel << "\n";
  LOG_INFO << "Chipset Driver Version: "
            << g_constantSystemInfo.chipsetDriverVersion << "\n";

  // BIOS Information
  LOG_INFO << "\n----- BIOS Information -----\n";
  LOG_INFO << "BIOS Version: " << g_constantSystemInfo.biosVersion << "\n";
  LOG_INFO << "BIOS Date: " << g_constantSystemInfo.biosDate << "\n";
  LOG_INFO << "BIOS Manufacturer: " << g_constantSystemInfo.biosManufacturer
            << "\n";

  // OS Information
  LOG_INFO << "\n----- OS Information -----\n";
  LOG_INFO << "OS Version: " << g_constantSystemInfo.osVersion << "\n";
  LOG_INFO << "OS Build Number: " << g_constantSystemInfo.osBuildNumber
            << "\n";
  LOG_INFO << "Is Windows 11: "
            << (g_constantSystemInfo.isWindows11 ? "Yes" : "No") << "\n";
  LOG_INFO << "System Name: [system name hidden for privacy]\n";

  // Storage Information
  LOG_INFO << "\n----- Storage Drives (" << std::dec
            << g_constantSystemInfo.drives.size() << ") -----\n";
  for (size_t i = 0; i < g_constantSystemInfo.drives.size(); i++) {
    const auto& drive = g_constantSystemInfo.drives[i];
    LOG_INFO << std::dec;
    LOG_INFO << "Drive #" << (i + 1) << " ("
              << (drive.isSystemDrive ? "System Drive" : "Data Drive")
              << "):\n";
    LOG_INFO << "  Path: [drive path hidden for privacy]\n";
    LOG_INFO << "  Model: " << drive.model << "\n";
    LOG_INFO << "  Serial Number: [serial number hidden for privacy]\n";
    LOG_INFO << "  Interface Type: " << drive.interfaceType << "\n";
    LOG_INFO << "  Total Space (GB): " << std::dec << drive.totalSpaceGB
              << "\n";
    LOG_INFO << "  Free Space (GB): " << std::dec << drive.freeSpaceGB << "\n";
    LOG_INFO << "  SSD: " << (drive.isSSD ? "Yes" : "No") << "\n";
  }

  // Power Settings
  LOG_INFO << "\n----- Power Settings -----\n";
  LOG_INFO << "Power Plan: " << g_constantSystemInfo.powerPlan << "\n";
  LOG_INFO << "High Performance Power Plan: "
            << (g_constantSystemInfo.powerPlanHighPerf ? "Yes" : "No") << "\n";
  LOG_INFO << "Game Mode: "
            << (g_constantSystemInfo.gameMode ? "Enabled" : "Disabled") << "\n";

  // Page File Information
  LOG_INFO << "\n----- Page File Information -----\n";
  LOG_INFO << "Page File Exists: "
            << (g_constantSystemInfo.pageFileExists ? "Yes" : "No") << "\n";

  if (g_constantSystemInfo.pageFileExists) {
    LOG_INFO << "System Managed: "
              << (g_constantSystemInfo.pageFileSystemManaged ? "Yes" : "No")
              << "\n";
    LOG_INFO << "Total Size (MB): " << g_constantSystemInfo.pageTotalSizeMB
              << "\n";
    LOG_INFO << "Primary Drive Letter: [drive letter hidden for privacy]\n";

    LOG_INFO << "Locations: ";
    if (g_constantSystemInfo.pageFileLocations.empty()) {
      LOG_INFO << "None";
    } else {
      LOG_INFO << "[page file locations hidden for privacy]";
    }
    LOG_INFO << "\n";

    // Print current and max sizes if available
    if (!g_constantSystemInfo.pageFileCurrentSizesMB.empty() &&
        !g_constantSystemInfo.pageFileMaxSizesMB.empty() &&
        g_constantSystemInfo.pageFileCurrentSizesMB.size() ==
          g_constantSystemInfo.pageFileLocations.size()) {

      for (size_t i = 0; i < g_constantSystemInfo.pageFileLocations.size();
           i++) {
        LOG_INFO << "  [page file location hidden for privacy]: ";
        LOG_INFO << g_constantSystemInfo.pageFileCurrentSizesMB[i]
                  << " MB current";

        if (i < g_constantSystemInfo.pageFileMaxSizesMB.size()) {
          LOG_INFO << ", " << g_constantSystemInfo.pageFileMaxSizesMB[i]
                    << " MB peak";
        }
        LOG_INFO << "\n";
      }
    }
  }

  // Driver Information
  LOG_INFO << "\n----- Chipset Drivers ("
            << g_constantSystemInfo.chipsetDrivers.size() << ") -----\n";
  for (size_t i = 0; i < g_constantSystemInfo.chipsetDrivers.size(); i++) {
    const auto& driver = g_constantSystemInfo.chipsetDrivers[i];
    LOG_INFO << "Driver #" << (i + 1) << ": " << driver.deviceName << "\n";
    LOG_INFO << "  Version: " << driver.driverVersion << "\n";
    LOG_INFO << "  Date: "
              << (driver.isDateValid ? driver.driverDate : "Unknown") << "\n";
    LOG_INFO << "  Provider: " << driver.providerName << "\n";
  }

  LOG_INFO << "\n----- Audio Drivers ("
            << g_constantSystemInfo.audioDrivers.size() << ") -----\n";
  for (size_t i = 0; i < g_constantSystemInfo.audioDrivers.size(); i++) {
    const auto& driver = g_constantSystemInfo.audioDrivers[i];
    LOG_INFO << "Driver #" << (i + 1) << ": " << driver.deviceName << "\n";
    LOG_INFO << "  Version: " << driver.driverVersion << "\n";
    LOG_INFO << "  Date: "
              << (driver.isDateValid ? driver.driverDate : "Unknown") << "\n";
    LOG_INFO << "  Provider: " << driver.providerName << "\n";
  }

  LOG_INFO << "\n----- Network Drivers ("
            << g_constantSystemInfo.networkDrivers.size() << ") -----\n";
  for (size_t i = 0; i < g_constantSystemInfo.networkDrivers.size(); i++) {
    const auto& driver = g_constantSystemInfo.networkDrivers[i];
    LOG_INFO << "Driver #" << (i + 1) << ": " << driver.deviceName << "\n";
    LOG_INFO << "  Version: " << driver.driverVersion << "\n";
    LOG_INFO << "  Date: "
              << (driver.isDateValid ? driver.driverDate : "Unknown") << "\n";
    LOG_INFO << "  Provider: " << driver.providerName << "\n";
  }

  // Add this section after the driver information section
  LOG_INFO << "\n----- Monitor Information ("
            << g_constantSystemInfo.monitors.size() << ") -----\n";
  for (size_t i = 0; i < g_constantSystemInfo.monitors.size(); i++) {
    const auto& monitor = g_constantSystemInfo.monitors[i];
    LOG_INFO << "Monitor #" << (i + 1) << " ("
              << (monitor.isPrimary ? "Primary" : "Secondary") << "):\n";
    LOG_INFO << "  Device Name: " << monitor.deviceName << "\n";
    LOG_INFO << "  Display Name: " << monitor.displayName << "\n";
    LOG_INFO << "  Resolution: " << monitor.width << " x " << monitor.height
              << "\n";
    LOG_INFO << "  Refresh Rate: " << monitor.refreshRate << " Hz\n";
  }

  LOG_INFO << "\n===== END OF CONSTANT SYSTEM INFORMATION =====\n"
           ;
}

// Add a helper function to time operations
template <typename Func>
long long timeOperation(const std::string& operationName, Func&& func) {
  auto startTime = std::chrono::high_resolution_clock::now();
  func();
  auto endTime = std::chrono::high_resolution_clock::now();
  auto duration =
    std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
      .count();
  return duration;
}

// Add this function to validate collected information
void validateCollectedInfo() {
  // Define counters
  size_t totalValues = 0;
  size_t validValues = 0;
  std::vector<std::string> missingFields;

  // Helper lambda to check string fields
  auto checkString = [&](const std::string& value,
                         const std::string& fieldName) {
    totalValues++;
    if (value != "no_data" && !value.empty()) {
      validValues++;
    } else {
      missingFields.push_back(fieldName);
    }
  };

  // Helper lambda to check numeric fields
  auto checkNumeric = [&](int value, const std::string& fieldName) {
    totalValues++;
    if (value != -1) {
      validValues++;
    } else {
      missingFields.push_back(fieldName);
    }
  };

  // Helper lambda to check int64 fields
  auto checkInt64 = [&](int64_t value, const std::string& fieldName) {
    totalValues++;
    if (value != -1) {
      validValues++;
    } else {
      missingFields.push_back(fieldName);
    }
  };

  // Check CPU information
  checkString(g_constantSystemInfo.cpuName, "cpuName");
  checkString(g_constantSystemInfo.cpuVendor, "cpuVendor");
  checkNumeric(g_constantSystemInfo.physicalCores, "physicalCores");
  checkNumeric(g_constantSystemInfo.logicalCores, "logicalCores");
  checkString(g_constantSystemInfo.cpuArchitecture, "cpuArchitecture");
  checkString(g_constantSystemInfo.cpuSocket,
              "cpuSocket");  // This is likely the missing field
  checkNumeric(g_constantSystemInfo.baseClockMHz, "baseClockMHz");
  checkNumeric(g_constantSystemInfo.maxClockMHz, "maxClockMHz");
  checkNumeric(g_constantSystemInfo.l1CacheKB, "l1CacheKB");
  checkNumeric(g_constantSystemInfo.l2CacheKB, "l2CacheKB");
  checkNumeric(g_constantSystemInfo.l3CacheKB, "l3CacheKB");

  // Check Memory information
  checkInt64(g_constantSystemInfo.totalPhysicalMemoryMB,
             "totalPhysicalMemoryMB");
  checkString(g_constantSystemInfo.memoryType, "memoryType");
  checkNumeric(g_constantSystemInfo.memoryClockMHz, "memoryClockMHz");
  checkString(g_constantSystemInfo.memoryChannelConfig, "memoryChannelConfig");

  // Check GPU information - count if we have at least one valid GPU
  totalValues++;
  if (!g_constantSystemInfo.gpuDevices.empty() &&
      g_constantSystemInfo.gpuDevices[0].name != "no_data") {
    validValues++;
  } else {
    missingFields.push_back("gpuDevices");
  }

  // Check Motherboard information
  checkString(g_constantSystemInfo.motherboardManufacturer,
              "motherboardManufacturer");
  checkString(g_constantSystemInfo.motherboardModel, "motherboardModel");
  checkString(g_constantSystemInfo.chipsetModel, "chipsetModel");
  checkString(g_constantSystemInfo.chipsetDriverVersion,
              "chipsetDriverVersion");

  // Check BIOS information
  checkString(g_constantSystemInfo.biosVersion, "biosVersion");
  checkString(g_constantSystemInfo.biosDate, "biosDate");
  checkString(g_constantSystemInfo.biosManufacturer, "biosManufacturer");

  // Check OS information
  checkString(g_constantSystemInfo.osVersion, "osVersion");
  checkString(g_constantSystemInfo.osBuildNumber, "osBuildNumber");
  checkString(g_constantSystemInfo.systemName, "systemName");

  // Check Drive information - count if we have at least one valid drive
  totalValues++;
  if (!g_constantSystemInfo.drives.empty() &&
      g_constantSystemInfo.drives[0].path != "no_data") {
    validValues++;
  } else {
    missingFields.push_back("drives");
  }

  // Check Power settings
  checkString(g_constantSystemInfo.powerPlan, "powerPlan");

  // Add this check after the "drives" check
  // Check Monitor information - count if we have at least one valid monitor
  totalValues++;
  if (!g_constantSystemInfo.monitors.empty() &&
      g_constantSystemInfo.monitors[0].width > 0) {
    validValues++;
  } else {
    missingFields.push_back("monitors");
  }

  // Calculate percentage
  double successPercentage = (validValues * 100.0) / totalValues;

  // Print summary
  LOG_INFO << "\n===== SYSTEM INFO COLLECTION SUMMARY =====\n";
  LOG_INFO << "Successfully collected: " << validValues << " / " << totalValues
            << " values";
  LOG_INFO << " (" << std::fixed << std::setprecision(1) << successPercentage
            << "%)";

  // Print missing fields if any
  if (!missingFields.empty()) {
    LOG_INFO << "Missing fields: ";
    for (size_t i = 0; i < missingFields.size(); i++) {
      LOG_INFO << missingFields[i];
      if (i < missingFields.size() - 1) {
        LOG_INFO << ", ";
      }
    }
    LOG_INFO;
  }

  LOG_INFO << "==========================================";
}

// Add this forward declaration before collectAllSystemInfo
void collectMonitorInfo();

// Update collectAllSystemInfo to call validation at the end
void collectAllSystemInfo() {
  auto totalStartTime = std::chrono::high_resolution_clock::now();

  timeOperation("CPU info", collectCpuInfo);
  timeOperation("Memory info", collectMemoryInfo);
  timeOperation("GPU info", collectGpuInfo);
  timeOperation("Motherboard info", collectMotherboardInfo);
  timeOperation("BIOS info", collectBiosInfo);
  timeOperation("OS info", collectOsInfo);
  timeOperation("Drive info", collectDriveInfo);
  timeOperation("Page file info", collectPageFileInfo);
  timeOperation("Driver info", collectDriverInfo);
  timeOperation("Monitor info", collectMonitorInfo);

  auto totalEndTime = std::chrono::high_resolution_clock::now();
  auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                         totalEndTime - totalStartTime)
                         .count();

  // Print all collected information
  printCollectedSystemInfo();

  // Validate and print summary of collection results
  validateCollectedInfo();
}

void collectMonitorInfo() {
  // Initialize with empty monitor list
  g_constantSystemInfo.monitors.clear();

  // Create SystemWrapper to get monitor information
  SystemWrapper sysWrapper;

  // Get monitor information
  auto sysWrapperMonitors = sysWrapper.getMonitorInfo();

  // Convert from SystemWrapper::MonitorInfo to SystemMetrics::MonitorInfo
  // Filter out monitors with invalid data (negative width, height, or refresh
  // rate)
  for (const auto& monitor : sysWrapperMonitors) {
    // Skip monitors with invalid resolution or refresh rate
    if (monitor.width <= 0 || monitor.height <= 0 || monitor.refreshRate <= 0) {
      continue;
    }

    SystemMetrics::MonitorInfo monitorInfo;
    monitorInfo.deviceName = monitor.deviceName;
    monitorInfo.displayName = monitor.displayName;
    monitorInfo.width = monitor.width;
    monitorInfo.height = monitor.height;
    monitorInfo.refreshRate = monitor.refreshRate;
    monitorInfo.isPrimary = monitor.isPrimary;

    g_constantSystemInfo.monitors.push_back(monitorInfo);
  }
}

}  // namespace

// Public interface implementation
namespace SystemMetrics {

void CollectConstantSystemInfo() { collectAllSystemInfo(); }

const ConstantSystemInfo& GetConstantSystemInfo() {
  return g_constantSystemInfo;
}

}  // namespace SystemMetrics
