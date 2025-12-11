#include "WinHardwareMonitor.h"

#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

#include "../logging/Logger.h"

#include <Windows.h>
#include <Wbemidl.h>
#include <comdef.h>
#include <intrin.h>
#include <pdh.h>
#include <pdhmsg.h>

// Constants
static constexpr int SENSOR_CACHE_MS = 1000;
static constexpr int MAX_INIT_RETRIES = 3;
static constexpr float BYTES_TO_GB = 1024.0 * 1024.0 * 1024.0;
static constexpr int DDR4_TYPE_CODE = 26;
static constexpr int DDR5_TYPE_CODE = 27;
static constexpr int DDR4_MAX_STANDARD_SPEED = 2666;
static constexpr int DDR5_MAX_STANDARD_SPEED = 4800;
static constexpr int DDR5_THRESHOLD_SPEED = 4700;

// WMI namespace and paths
static const wchar_t* WMI_NAMESPACE_CIMV2 = L"ROOT\\CIMV2";

// Forward declare the unmanaged AVX detection function
void DetectAVXSupport(bool& avxSupport, bool& avx2Support);

// Helper class for WMI operations
class WMIHelper {
 private:
  IWbemServices* pSvc = nullptr;
  IWbemLocator* pLoc = nullptr;
  bool initialized = false;
  std::mutex initMutex;
  bool comInitialized = false;

 public:
  WMIHelper() = default;
  ~WMIHelper() { cleanup(); }

  bool initialize() {
    std::lock_guard<std::mutex> lock(initMutex);

    if (initialized) {
      return true;
    }

    // Don't call cleanup() here - this was causing the deadlock

    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    comInitialized =
      (SUCCEEDED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE);

    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
      LOG_ERROR << "COM initialization failed with HRESULT: 0x" << std::hex << hr << std::dec;
      return false;
    }

    hr = CoInitializeSecurity(
      nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
      RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
      LOG_ERROR << "COM security initialization failed with HRESULT: 0x" << std::hex << hr << std::dec;
      if (comInitialized) CoUninitialize();
      return false;
    }

    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, (LPVOID*)&pLoc);

    if (FAILED(hr)) {
      LOG_ERROR << "CoCreateInstance failed with HRESULT: 0x" << std::hex << hr << std::dec;
      if (comInitialized) CoUninitialize();
      return false;
    }

    hr = pLoc->ConnectServer(_bstr_t(WMI_NAMESPACE_CIMV2), nullptr, nullptr,
                             nullptr, 0, nullptr, nullptr, &pSvc);

    if (FAILED(hr)) {
      LOG_ERROR << "ConnectServer failed with HRESULT: 0x" << std::hex << hr << std::dec;
      pLoc->Release();
      pLoc = nullptr;
      if (comInitialized) CoUninitialize();
      return false;
    }

    hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                           nullptr, EOAC_NONE);

    if (FAILED(hr)) {
      LOG_ERROR << "CoSetProxyBlanket failed with HRESULT: 0x" << std::hex << hr << std::dec;
      pSvc->Release();
      pSvc = nullptr;
      pLoc->Release();
      pLoc = nullptr;
      if (comInitialized) CoUninitialize();
      return false;
    }

    initialized = true;
    return true;
  }

  void cleanup() {
    std::lock_guard<std::mutex> lock(initMutex);

    if (!initialized) {
      return;  // Don't cleanup if not initialized
    }

    if (pSvc) {
      pSvc->Release();
      pSvc = nullptr;
    }

    if (pLoc) {
      pLoc->Release();
      pLoc = nullptr;
    }

    if (comInitialized) {
      CoUninitialize();
      comInitialized = false;
    }

    initialized = false;
  }

  template <typename T>
  bool executeQuery(const std::wstring& query,
                    std::function<void(IWbemClassObject*)> callback) {
    if (!initialized) {
      if (!initialize()) {
        return false;
      }
    }

    IEnumWbemClassObject* pEnumerator = nullptr;
    HRESULT hr =
      pSvc->ExecQuery(bstr_t("WQL"), bstr_t(query.c_str()),
                      WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                      nullptr, &pEnumerator);

    if (FAILED(hr)) {
      return false;
    }

    IWbemClassObject* pclsObj = nullptr;
    ULONG uReturn = 0;
    int resultCount = 0;

    while (pEnumerator) {
      hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);

      if (uReturn == 0) break;

      resultCount++;
      callback(pclsObj);
      pclsObj->Release();
    }

    if (pEnumerator) {
      pEnumerator->Release();
    }

    return true;
  }

  bool isInitialized() const { return initialized; }
};

// Helper class for performance counters
class PDHHelper {
 private:
  PDH_HQUERY query = nullptr;
  std::map<std::string, PDH_HCOUNTER> counters;
  std::mutex pdhMutex;
  bool initialized = false;

 public:
  PDHHelper() = default;
  ~PDHHelper() { cleanup(); }

  bool initialize() {
    std::lock_guard<std::mutex> lock(pdhMutex);

    if (initialized) return true;

    if (query) {
      PdhCloseQuery(query);
      query = nullptr;
    }

    PDH_STATUS status = PdhOpenQuery(nullptr, 0, &query);
    if (status != ERROR_SUCCESS) {
      LOG_ERROR << "Error initializing PDH query: 0x" << std::hex << status << std::dec;
      return false;
    }

    initialized = true;
    return true;
  }

  bool addCounter(const std::string& name, const std::wstring& path) {
    std::lock_guard<std::mutex> lock(pdhMutex);

    if (!initialized && !initialize()) {
      return false;
    }

    PDH_HCOUNTER counter;
    if (PdhAddCounterW(query, path.c_str(), 0, &counter) != ERROR_SUCCESS) {
      return false;
    }

    counters[name] = counter;
    return true;
  }

  bool collectData() {
    std::lock_guard<std::mutex> lock(pdhMutex);

    if (!initialized) {
      return false;
    }

    return (PdhCollectQueryData(query) == ERROR_SUCCESS);
  }

  bool getCounterValue(const std::string& name, double& value) {
    std::lock_guard<std::mutex> lock(pdhMutex);

    if (!initialized || counters.find(name) == counters.end()) {
      return false;
    }

    PDH_FMT_COUNTERVALUE counterValue;
    if (PdhGetFormattedCounterValue(counters[name], PDH_FMT_DOUBLE, nullptr,
                                    &counterValue) != ERROR_SUCCESS) {
      return false;
    }

    value = counterValue.doubleValue;
    return true;
  }

  void cleanup() {
    std::lock_guard<std::mutex> lock(pdhMutex);

    if (query) {
      PdhCloseQuery(query);
      query = nullptr;
    }

    counters.clear();
    initialized = false;
  }
};

// The implementation class
class WinHardwareMonitor::Impl {
 private:
  WMIHelper wmiHelper;
  PDHHelper pdhHelper;
  std::mutex dataLock;

  // Timestamp tracking for sensor updates
  long long m_lastCpuUpdate = 0;
  long long m_lastRamUpdate = 0;
  bool countersInitialized = false;
  bool wmiInitialized = false;
  int initRetryCount = 0;
  int m_derivedMemoryType = -1;
  int m_derivedMemoryClockSpeed = 0;

  // Add these fields to the WinHardwareMonitor::Impl class private section
  bool m_frequencyMethodDetermined = false;
  bool m_useActualFrequencyMethod = true;
  bool m_usePerformanceCounterMethod = false;
  bool m_useWmiMethod = false;
  bool m_diagnosticPrinted = false;

 public:
  Impl() { initialize(); }

  bool initialize() { return true; }

  void setupCpuCounters() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;

    pdhHelper.cleanup();
    if (!pdhHelper.initialize()) {
      return;
    }

    pdhHelper.addCounter("TotalUsage",
                         L"\\Processor(_Total)\\% Processor Time");

    WCHAR computerName[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computerName, &size);
    std::wstring computerNameStr = computerName;

    for (int i = 0; i < numCores; i++) {
      std::wstring freqPaths[4] = {
        L"\\Processor Information(0," + std::to_wstring(i) +
          L")\\Actual Frequency",
        L"\\\\" + computerNameStr + L"\\Processor Information(0," +
          std::to_wstring(i) + L")\\Actual Frequency",
        L"\\Processor Information(0" + std::to_wstring(i) +
          L")\\Actual Frequency",
        L"\\Processor Information(0," + std::to_wstring(i) +
          L")\\Processor Frequency"};

      bool addedCounter = false;
      for (const auto& path : freqPaths) {
        if (pdhHelper.addCounter("CoreFreq" + std::to_string(i), path)) {
          addedCounter = true;
          break;
        }
      }

      // If we couldn't add using the primary paths, try additional fallback
      // paths with specific core
      if (!addedCounter) {
        std::wstring fallbackPaths[2] = {
          L"\\Processor Information(" + std::to_wstring(i) +
            L")\\Processor Frequency",
          L"\\\\" + computerNameStr + L"\\Processor Information(" +
            std::to_wstring(i) + L")\\Processor Frequency"};

        for (const auto& path : fallbackPaths) {
          if (pdhHelper.addCounter("CoreFreq" + std::to_string(i), path)) {
            addedCounter = true;
            break;
          }
        }
      }

      std::wstring loadPaths[3] = {
        L"\\Processor Information(0," + std::to_wstring(i) +
          L")\\% Processor Time",
        L"\\\\" + computerNameStr + L"\\Processor Information(0," +
          std::to_wstring(i) + L")\\% Processor Time",
        L"\\Processor(" + std::to_wstring(i) + L")\\% Processor Time"};

      for (const auto& path : loadPaths) {
        if (pdhHelper.addCounter("CoreLoad" + std::to_string(i), path)) {
          break;
        }
      }
    }

    Sleep(500);
    pdhHelper.collectData();
    countersInitialized = true;
  }

  CPUInfo GetCPUInfo() {
    std::lock_guard<std::mutex> lock(dataLock);
    CPUInfo info;

    if (!countersInitialized && initRetryCount < MAX_INIT_RETRIES) {
      setupCpuCounters();
      initRetryCount++;
    }

    long long currentTime = GetTickCount64();
    if (currentTime - m_lastCpuUpdate > SENSOR_CACHE_MS) {
      updateCpuInfo(info);
      m_lastCpuUpdate = currentTime;
    }

    return info;
  }

  void updateCpuInfo(CPUInfo& info) {
    getCpuBasicInfo(info);
    getCpuCacheInfo(info);
    getCpuPerformanceInfo(info);
    getCpuTemperatureInfo(info);
    detectAvxSupport(info);
    checkVirtualizationStatus(info);

    if (info.baseClockSpeed > 0 && info.currentClockSpeed > 0) {
      info.performancePercentage =
        static_cast<int>(100.0 * info.currentClockSpeed / info.baseClockSpeed);
    }

    info.smtActive = (info.logicalCores > info.physicalCores);
    getPowerPlanInfo(info);
  }

  void getCpuCacheInfo(CPUInfo& info) {
    std::string cacheInfo;
    int l1DataCache = -1, l1InstCache = -1, l2Cache = -1, l3Cache = -1;
    int totalL1Cache = 0, totalL2Cache = 0;

    DWORD len = 0;
    if (GetLogicalProcessorInformationEx(RelationCache, NULL, &len) == FALSE &&
        GetLastError() == ERROR_INSUFFICIENT_BUFFER) {

      std::vector<BYTE> buffer(len);
      auto* ptr = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
        buffer.data());

      if (GetLogicalProcessorInformationEx(RelationCache, ptr, &len)) {
        auto* curr = ptr;
        DWORD offset = 0;

        std::map<int, int> perCoreCaches;
        std::map<int, int> sharedCaches;

        while (offset < len) {
          if (curr->Relationship == RelationCache) {
            CACHE_RELATIONSHIP cache = curr->Cache;
            int cacheSize = cache.CacheSize / 1024;

            if (cache.Type == CacheData || cache.Type == CacheUnified) {
              if (cache.Level == 1) {
                l1DataCache = cacheSize;
                totalL1Cache += cacheSize;
              } else if (cache.Level == 2) {
                if (cache.Type == CacheUnified) {
                  totalL2Cache += cacheSize;
                }
                if (l2Cache < cacheSize) {
                  l2Cache = cacheSize;
                }
              } else if (cache.Level == 3) {
                if (l3Cache < cacheSize) {
                  l3Cache = cacheSize;
                }
              }
            } else if (cache.Type == CacheInstruction && cache.Level == 1) {
              l1InstCache = cacheSize;
              totalL1Cache += cacheSize;
            }
          }
          offset += curr->Size;
          curr = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
            reinterpret_cast<BYTE*>(curr) + curr->Size);
        }
      }
    }

    if (l1DataCache == -1 && l2Cache == -1 && l3Cache == -1) {
      DWORD len = 0;
      if (GetLogicalProcessorInformation(NULL, &len) == FALSE &&
          GetLastError() == ERROR_INSUFFICIENT_BUFFER) {

        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(
          len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));

        if (GetLogicalProcessorInformation(buffer.data(), &len)) {
          totalL1Cache = 0;
          totalL2Cache = 0;

          for (size_t i = 0; i < buffer.size(); i++) {
            if (buffer[i].Relationship == RelationCache) {
              CACHE_DESCRIPTOR cache = buffer[i].Cache;
              int cacheSize = cache.Size / 1024;

              if (cache.Type == CacheData || cache.Type == CacheUnified) {
                if (cache.Level == 1) {
                  l1DataCache = cacheSize;
                  totalL1Cache += cacheSize;
                } else if (cache.Level == 2) {
                  totalL2Cache += cacheSize;
                  if (l2Cache < cacheSize) {
                    l2Cache = cacheSize;
                  }
                } else if (cache.Level == 3) {
                  if (l3Cache < cacheSize) {
                    l3Cache = cacheSize;
                  }
                }
              } else if (cache.Type == CacheInstruction && cache.Level == 1) {
                l1InstCache = cacheSize;
                totalL1Cache += cacheSize;
              }
            }
          }
        }
      }
    }

    if (l2Cache == -1 || l3Cache == -1) {
      wmiHelper.executeQuery<std::string>(
        L"SELECT L2CacheSize, L3CacheSize FROM Win32_Processor",
        [&l2Cache, &l3Cache, &totalL2Cache](IWbemClassObject* pObj) {
          VARIANT vtL2Cache, vtL3Cache;
          VariantInit(&vtL2Cache);
          VariantInit(&vtL3Cache);

          if (SUCCEEDED(pObj->Get(L"L2CacheSize", 0, &vtL2Cache, 0, 0)) &&
              vtL2Cache.vt == VT_I4) {
            if (vtL2Cache.lVal > 0) {
              l2Cache = vtL2Cache.lVal;
              if (totalL2Cache == 0) {
                totalL2Cache = l2Cache;
              }
            }
          }

          if (SUCCEEDED(pObj->Get(L"L3CacheSize", 0, &vtL3Cache, 0, 0)) &&
              vtL3Cache.vt == VT_I4) {
            if (vtL3Cache.lVal > 0) {
              l3Cache = vtL3Cache.lVal;
            }
          }

          VariantClear(&vtL2Cache);
          VariantClear(&vtL3Cache);
        });
    }

    if (info.vendor.find("AMD") != std::string::npos &&
        info.physicalCores > 0) {
      if (l2Cache > 0 && totalL2Cache == 0 && info.physicalCores > 1) {
        totalL2Cache = l2Cache * info.physicalCores;
      }
    }

    int finalL1 = (totalL1Cache > 0)
                    ? totalL1Cache
                    : ((l1DataCache > 0 && l1InstCache > 0)
                         ? (l1DataCache + l1InstCache)
                         : ((l1DataCache > 0) ? l1DataCache : l1InstCache));
    int finalL2 = (totalL2Cache > 0) ? totalL2Cache : l2Cache;

    if (finalL1 > 0) {
      cacheInfo = "L1: " + std::to_string(finalL1) + " KB";
    }

    if (finalL2 > 0) {
      if (!cacheInfo.empty()) cacheInfo += ", ";
      cacheInfo += "L2: " + std::to_string(finalL2) + " KB";
    }

    if (l3Cache > 0) {
      if (!cacheInfo.empty()) cacheInfo += ", ";
      cacheInfo += "L3: " + std::to_string(l3Cache) + " KB";
    }

    info.cacheSizes = cacheInfo;
  }

  void getCpuBasicInfo(CPUInfo& info) {
    // Initialize with "no data" values
    info.name = "no_data";
    info.vendor = "no_data";
    info.physicalCores = -1;
    info.logicalCores = -1;
    info.baseClockSpeed = -1;
    info.currentClockSpeed = -1;
    info.architecture = "no_data";
    info.socket = "no_data";
    info.powerPlan = "no_data";
    info.performancePercentage = -1;
    info.temperature = -1;
    info.packagePower = -1;
    info.socketPower = -1;
    info.coreClocks.clear();
    info.coreVoltages.clear();
    info.coreTemperatures.clear();
    info.coreLoads.clear();
    info.cacheSizes = "no_data";

    // Continue with the existing code
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    info.logicalCores = sysInfo.dwNumberOfProcessors;

    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0);
    char vendorId[13] = {0};
    memcpy(vendorId, &cpuInfo[1], 4);
    memcpy(vendorId + 4, &cpuInfo[3], 4);
    memcpy(vendorId + 8, &cpuInfo[2], 4);
    vendorId[12] = '\0';
    info.vendor = vendorId;

    __cpuid(cpuInfo, 0x80000000);
    if (info.vendor.find("AMD") != std::string::npos &&
        cpuInfo[0] >= 0x8000001E) {
      __cpuid(cpuInfo, 0x8000001E);
      int threadsPerCore = ((cpuInfo[1] >> 8) & 0xFF) + 1;

      __cpuid(cpuInfo, 0x80000008);
      int totalCores = ((cpuInfo[2] & 0xFF) + 1);

      if (threadsPerCore > 0) {
        info.physicalCores = totalCores / threadsPerCore;
        if (info.physicalCores == 0) info.physicalCores = totalCores;
      } else {
        info.physicalCores = totalCores;
      }
    } else {
      __cpuid(cpuInfo, 1);

      if (info.vendor.find("Intel") != std::string::npos) {
        bool htt = (cpuInfo[3] & (1 << 28)) != 0;

        int logicalPerCore = ((cpuInfo[1] >> 16) & 0xFF);

        if (htt && logicalPerCore > 1) {
          int i = 0, coresPerPackage = 0;
          do {
            __cpuidex(cpuInfo, 4, i);
            if (i == 0) {
              coresPerPackage = ((cpuInfo[0] >> 26) & 0x3F) + 1;
            }
            i++;
          } while ((cpuInfo[0] & 0x1F) != 0);

          if (coresPerPackage > 0) {
            info.physicalCores = coresPerPackage;
          }
        } else {
          info.physicalCores = info.logicalCores;
        }
      } else {
        __cpuid(cpuInfo, 0x80000000);
        if (cpuInfo[0] >= 0x80000008) {
          __cpuid(cpuInfo, 0x80000008);
          info.physicalCores = ((cpuInfo[2] & 0xFF) + 1);
        }
      }
    }

    __cpuid(cpuInfo, 1);
    int family = ((cpuInfo[0] >> 8) & 0xF);
    int extFamily = ((cpuInfo[0] >> 20) & 0xFF);
    int model = ((cpuInfo[0] >> 4) & 0xF);
    int extModel = ((cpuInfo[0] >> 12) & 0xF);

    if (family == 0xF) {
      family += extFamily;
    }

    if (family == 0x6 || family == 0xF) {
      model += (extModel << 4);
    }

    if (info.vendor.find("AMD") != std::string::npos) {
      if (family == 0x17) {
        info.architecture = "Zen/Zen+/Zen2";
      } else if (family == 0x19) {
        info.architecture = "Zen3/Zen3+";
      } else if (family >= 0x1A) {
        info.architecture = "Zen4+";
      }
    } else if (info.vendor.find("Intel") != std::string::npos) {
      if (family == 6) {
        if (model >= 0x8E && model <= 0x8F) {
          info.architecture = "Core (9th-10th gen)";
        } else if (model >= 0x97 && model <= 0x9F) {
          info.architecture = "Core (11th-12th gen)";
        } else if (model >= 0xA5 && model <= 0xAF) {
          info.architecture = "Core (13th+ gen)";
        }
      }
    }

    wmiHelper.executeQuery<std::string>(
      L"SELECT SocketDesignation FROM Win32_Processor",
      [&info](IWbemClassObject* pObj) {
        VARIANT vtProp;
        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"SocketDesignation", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_BSTR) {
          info.socket = _bstr_t(vtProp.bstrVal);
        }
        VariantClear(&vtProp);
      });

    wmiHelper.executeQuery<CPUInfo>(
      L"SELECT Name, Manufacturer, NumberOfCores, NumberOfLogicalProcessors, "
      L"MaxClockSpeed, CurrentClockSpeed FROM Win32_Processor",
      [&info](IWbemClassObject* pObj) {
        VARIANT vtProp;

        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"Name", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_BSTR) {
          info.name = _bstr_t(vtProp.bstrVal);
        }
        VariantClear(&vtProp);

        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"Manufacturer", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_BSTR) {
          info.vendor = _bstr_t(vtProp.bstrVal);
        }
        VariantClear(&vtProp);

        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"NumberOfCores", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_I4) {
          if (info.physicalCores <= 0) {
            info.physicalCores = vtProp.lVal;
          } else if (vtProp.lVal > info.physicalCores * 2 ||
                     vtProp.lVal < info.physicalCores / 2) {
            info.physicalCores = vtProp.lVal;
          }
        }
        VariantClear(&vtProp);

        VariantInit(&vtProp);
        if (SUCCEEDED(
              pObj->Get(L"NumberOfLogicalProcessors", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_I4) {
          if (vtProp.lVal > 0) {
            info.logicalCores = vtProp.lVal;
          }
        }
        VariantClear(&vtProp);

        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"MaxClockSpeed", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_I4) {
          if (vtProp.lVal > 0) {
            info.baseClockSpeed = vtProp.lVal;
          }
        }
        VariantClear(&vtProp);

        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"CurrentClockSpeed", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_I4) {
          if (vtProp.lVal > 0) {
            info.currentClockSpeed = vtProp.lVal;
          }
        }
        VariantClear(&vtProp);
      });

    if (info.name.empty()) {
      HKEY hKey;
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                        KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[256] = {0};
        DWORD size = sizeof(buffer);
        if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL,
                             (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
          info.name = buffer;
        }
        RegCloseKey(hKey);
      }
    }

    if (info.baseClockSpeed <= 0) {
      HKEY hKey;
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                        KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD mhz = 0;
        DWORD size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "~MHz", NULL, NULL, (LPBYTE)&mhz, &size) ==
            ERROR_SUCCESS) {
          info.baseClockSpeed = mhz;
        }
        RegCloseKey(hKey);
      }
    }

    info.coreClocks.resize(info.logicalCores, 0);
    info.coreVoltages.resize(info.logicalCores, 0.0);
    info.coreTemperatures.resize(info.logicalCores, 0.0);
    info.coreLoads.resize(info.logicalCores, 0.0);

    if (info.logicalCores > 0 && info.physicalCores > 0) {
      info.smtActive = (info.logicalCores > info.physicalCores);
    }
  }

  int tryProcessorPerformanceCounter(CPUInfo& info) {
    // Get max clock speed (should be available in info.baseClockSpeed)
    int maxClockSpeed = info.baseClockSpeed;

    if (maxClockSpeed <= 0) {
      // Try to get maxClockSpeed as a fallback
      HKEY hKey;
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                        KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD mhz = 0;
        DWORD size = sizeof(DWORD);
        if (RegQueryValueExA(hKey, "~MHz", NULL, NULL, (LPBYTE)&mhz, &size) ==
            ERROR_SUCCESS) {
          maxClockSpeed = mhz;
        }
        RegCloseKey(hKey);
      }

      if (maxClockSpeed <= 0) {
        return -1;  // Failed to get max clock speed
      }
    }

    // Initialize PDH query for processor performance counter
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    PDH_STATUS status;

    status = PdhOpenQuery(NULL, 0, &query);
    if (status != ERROR_SUCCESS) {
      return -1;
    }

    // Add processor performance counter
    status = PdhAddCounterW(
      query, L"\\Processor Information(_Total)\\% Processor Performance", 0,
      &counter);
    if (status != ERROR_SUCCESS) {
      PdhCloseQuery(query);
      return -1;
    }

    // Collect data
    status = PdhCollectQueryData(query);
    if (status != ERROR_SUCCESS) {
      PdhCloseQuery(query);
      return -1;
    }

    // Wait a moment for more accurate measurements
    Sleep(100);

    // Collect data again
    status = PdhCollectQueryData(query);
    if (status != ERROR_SUCCESS) {
      PdhCloseQuery(query);
      return -1;
    }

    // Get counter value
    PDH_FMT_COUNTERVALUE value;
    status = PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, &value);

    // Close query
    PdhCloseQuery(query);

    if (status != ERROR_SUCCESS) {
      return -1;
    }

    // Calculate current clock speed based on performance percentage
    double performancePercentage = value.doubleValue;

    // Some systems report values over 100%, likely due to Turbo Boost/Precision
    // Boost Cap it at a reasonable maximum to avoid unrealistic values
    if (performancePercentage > 200.0) {
      performancePercentage = 200.0;
    }

    int currentClockSpeed =
      static_cast<int>(maxClockSpeed * (performancePercentage / 100.0));
    return currentClockSpeed;
  }

  void getCpuPerformanceInfo(CPUInfo& info) {
    int actualFrequencyResult = -1;
    int performanceCounterResult = -1;

    // Determine which method works if we haven't already
    if (!m_frequencyMethodDetermined) {
      // Try the Actual Frequency method first (preferred method)
      if (pdhHelper.initialize()) {
        bool addedCounter = false;
        PDH_STATUS status = ERROR_SUCCESS;

        // Test if we can add the Actual Frequency counter
        PDH_HQUERY testQuery = nullptr;
        PDH_HCOUNTER testCounter = nullptr;

        if (PdhOpenQuery(NULL, 0, &testQuery) == ERROR_SUCCESS) {
          status = PdhAddCounterW(
            testQuery, L"\\Processor Information(_Total)\\Actual Frequency", 0,
            &testCounter);
          m_useActualFrequencyMethod = (status == ERROR_SUCCESS);

          if (testQuery) {
            PdhCloseQuery(testQuery);
          }
        }

        // If we can use the Actual Frequency method, initialize and collect data
        if (m_useActualFrequencyMethod) {
          // Set up counters and collect data to verify it works
          for (int retries = 0; retries < 2; retries++) {
            if (pdhHelper.collectData()) {
              double totalUsage;
              if (pdhHelper.getCounterValue("TotalUsage", totalUsage)) {
                info.loadPercentage = totalUsage;
              }

              double highestClock = 0;
              for (int i = 0; i < info.logicalCores; i++) {
                double frequency;
                if (pdhHelper.getCounterValue("CoreFreq" + std::to_string(i),
                                              frequency) &&
                    frequency > 0) {
                  info.coreClocks[i] = static_cast<int>(frequency);
                  if (frequency > highestClock) {
                    highestClock = frequency;
                  }
                }
              }

              if (highestClock > 0) {
                info.currentClockSpeed = static_cast<int>(highestClock);
                actualFrequencyResult = info.currentClockSpeed;
                break;
              } else if (retries == 0) {
                Sleep(100);  // Wait and try again
              }
            }
          }

          // Update flag based on result
          m_useActualFrequencyMethod = (actualFrequencyResult > 0);
        }
      } else {
        m_useActualFrequencyMethod = false;
      }

      // If Actual Frequency method failed, try performance counter method
      if (!m_useActualFrequencyMethod) {
        performanceCounterResult = tryProcessorPerformanceCounter(info);
        m_usePerformanceCounterMethod = (performanceCounterResult > 0);

        if (m_usePerformanceCounterMethod) {
          info.currentClockSpeed = performanceCounterResult;
        }
      }

      m_frequencyMethodDetermined = true;
    }

    // Now use the appropriate method based on what we determined works
    if (m_useActualFrequencyMethod) {
      // Use the Actual Frequency method
      if (pdhHelper.collectData()) {
        double totalUsage;
        if (pdhHelper.getCounterValue("TotalUsage", totalUsage)) {
          info.loadPercentage = totalUsage;
        } else {
          info.loadPercentage = -1;
        }

        double highestClock = 0;
        for (int i = 0; i < info.logicalCores; i++) {
          double frequency;
          if (pdhHelper.getCounterValue("CoreFreq" + std::to_string(i),
                                        frequency)) {
            info.coreClocks[i] = static_cast<int>(frequency);
            if (frequency > highestClock) {
              highestClock = frequency;
            }
          } else {
            info.coreClocks[i] = 0;
          }

          double load;
          if (pdhHelper.getCounterValue("CoreLoad" + std::to_string(i), load)) {
            info.coreLoads[i] = load;
          } else {
            info.coreLoads[i] = -1.0;
          }
        }

        if (highestClock > 0) {
          info.currentClockSpeed = static_cast<int>(highestClock);
        } else {
          // Fall back to performance counter if Actual Frequency suddenly stops
          // working
          info.currentClockSpeed = tryProcessorPerformanceCounter(info);
        }
      } else {
        // Handle failure of Actual Frequency method
        info.currentClockSpeed = tryProcessorPerformanceCounter(info);
      }
    } else if (m_usePerformanceCounterMethod) {
      // Use the Performance Counter method
      info.currentClockSpeed = tryProcessorPerformanceCounter(info);

      // Also get core load information
      if (pdhHelper.collectData()) {
        double totalUsage;
        if (pdhHelper.getCounterValue("TotalUsage", totalUsage)) {
          info.loadPercentage = totalUsage;
        } else {
          info.loadPercentage = -1;
        }

        // Apply the CPU frequency uniformly to all cores
        for (int i = 0; i < info.logicalCores; i++) {
          info.coreClocks[i] = info.currentClockSpeed;

          double load;
          if (pdhHelper.getCounterValue("CoreLoad" + std::to_string(i), load)) {
            info.coreLoads[i] = load;
          } else {
            info.coreLoads[i] = -1.0;
          }
        }
      }
    } else {
      // All methods failed
      info.currentClockSpeed = -1;
      info.loadPercentage = -1;
    }
  }

  void tryWmiForCpuFrequency(CPUInfo& info) {
    wmiHelper.executeQuery<int>(
      L"SELECT * FROM Win32_Processor", [&info](IWbemClassObject* pObj) {
        VARIANT vtProp;
        VariantInit(&vtProp);

        if (SUCCEEDED(pObj->Get(L"CurrentClockSpeed", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_I4) {
          info.currentClockSpeed = vtProp.lVal;
        } else {
          info.currentClockSpeed = -1;
        }

        VariantClear(&vtProp);
      });
  }

  void getCpuTemperatureInfo(CPUInfo& info) {
    wmiHelper.executeQuery<double>(
      L"SELECT * FROM Win32_TemperatureProbe", [&info](IWbemClassObject* pObj) {
        VARIANT vtProp;
        VariantInit(&vtProp);

        if (SUCCEEDED(pObj->Get(L"CurrentReading", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_I4) {
          double temp = static_cast<double>(vtProp.lVal) / 10.0;

          if (temp > 0 && temp < 150) {
            info.temperature = temp;
          }
        }

        VariantClear(&vtProp);
      });

    if (info.temperature <= 0) {
      wmiHelper.executeQuery<double>(
        L"SELECT * FROM MSAcpi_ThermalZoneTemperature",
        [&info](IWbemClassObject* pObj) {
          VARIANT vtProp;
          VariantInit(&vtProp);

          if (SUCCEEDED(pObj->Get(L"CurrentTemperature", 0, &vtProp, 0, 0)) &&
              vtProp.vt == VT_I4) {
            double tempKelvin = static_cast<double>(vtProp.lVal) / 10.0;
            double tempCelsius = tempKelvin - 273.15;

            if (tempCelsius > 0 && tempCelsius < 150) {
              info.temperature = tempCelsius;
            }
          }

          VariantClear(&vtProp);
        });
    }
  }

  void detectAvxSupport(CPUInfo& info) {
    int cpuInfo[4] = {0};

    __cpuid(cpuInfo, 1);
    info.avxSupport = (cpuInfo[2] & (1 << 28)) != 0;

    __cpuid(cpuInfo, 7);
    info.avx2Support = (cpuInfo[1] & (1 << 5)) != 0;
  }

  void getPowerPlanInfo(CPUInfo& info) {
    wmiHelper.executeQuery<std::string>(
      L"SELECT * FROM Win32_PowerPlan WHERE IsActive=True",
      [&info](IWbemClassObject* pObj) {
        VARIANT vtProp;
        VariantInit(&vtProp);

        if (SUCCEEDED(pObj->Get(L"ElementName", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_BSTR) {
          info.powerPlan = _bstr_t(vtProp.bstrVal);
        }

        VariantClear(&vtProp);
      });
  }

  GPUInfo GetGPUInfo() { return GPUInfo{}; }

  RAMInfo GetRAMInfo() {
    std::lock_guard<std::mutex> lock(dataLock);
    RAMInfo info;

    long long currentTime = GetTickCount64();
    if (currentTime - m_lastRamUpdate > SENSOR_CACHE_MS ||
        m_lastRamUpdate == 0) {
      updateRamInfo(info);
      m_lastRamUpdate = currentTime;
    }

    return info;
  }

  void updateRamInfo(RAMInfo& info) {
    // Initialize with "no data" values
    info.total = -1;
    info.available = -1;
    info.used = -1;
    info.memoryType = -1;
    info.clockSpeed = -1;

    // Continue with existing code
    MEMORYSTATUSEX memStatus = {sizeof(MEMORYSTATUSEX)};
    if (GlobalMemoryStatusEx(&memStatus)) {
      info.total = static_cast<double>(memStatus.ullTotalPhys) / BYTES_TO_GB;
      info.available =
        static_cast<double>(memStatus.ullAvailPhys) / BYTES_TO_GB;
      info.used = info.total - info.available;
    }

    if (m_derivedMemoryType > 0) {
      info.memoryType = m_derivedMemoryType;
    }

    if (m_derivedMemoryClockSpeed > 0) {
      info.clockSpeed = m_derivedMemoryClockSpeed;
    }

    if (info.memoryType <= 0 || info.clockSpeed <= 0) {
      wmiHelper.executeQuery<RAMInfo>(
        L"SELECT * FROM Win32_PhysicalMemory", [&info](IWbemClassObject* pObj) {
          VARIANT vtMemType, vtSpeed;
          VariantInit(&vtMemType);
          VariantInit(&vtSpeed);

          if (info.memoryType <= 0 &&
              SUCCEEDED(pObj->Get(L"SMBIOSMemoryType", 0, &vtMemType, 0, 0)) &&
              vtMemType.vt == VT_I4) {
            info.memoryType = vtMemType.lVal;
          }

          if (info.clockSpeed <= 0 &&
              SUCCEEDED(
                pObj->Get(L"ConfiguredClockSpeed", 0, &vtSpeed, 0, 0)) &&
              vtSpeed.vt == VT_I4) {
            info.clockSpeed = vtSpeed.lVal;
          }

          VariantClear(&vtMemType);
          VariantClear(&vtSpeed);
        });
    }
  }

  void getDetailedMemoryInfo(std::vector<MemoryModuleInfo>& modules,
                             std::string& channelStatus, bool& xmpEnabled) {
    modules.clear();
    channelStatus = "no_data";
    xmpEnabled = false;

    LOG_DEBUG << "Starting memory information retrieval";

    // Use existing WMI connection instead of creating a new one
    if (!wmiHelper.initialize()) {
      LOG_ERROR << "Failed to initialize WMI for memory information";
      return;
    }

    int moduleCount = 0;

    // Query memory information using existing WMI connection - ADD FormFactor
    // and BankLabel to the query
    wmiHelper.executeQuery<MemoryModuleInfo>(
      L"SELECT DeviceLocator, Manufacturer, PartNumber, Capacity, "
      L"ConfiguredClockSpeed, Speed, SMBIOSMemoryType, FormFactor, BankLabel "
      L"FROM Win32_PhysicalMemory",
      [&modules, &moduleCount, &xmpEnabled, this](IWbemClassObject* pObj) {
        MemoryModuleInfo module;
        VARIANT vtProp;

        // DeviceLocator
        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"DeviceLocator", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_BSTR) {
          module.deviceLocator = _bstr_t(vtProp.bstrVal);
        }
        VariantClear(&vtProp);

        // Manufacturer
        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"Manufacturer", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_BSTR) {
          module.manufacturer = _bstr_t(vtProp.bstrVal);
        }
        VariantClear(&vtProp);

        // PartNumber
        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"PartNumber", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_BSTR) {
          module.partNumber = _bstr_t(vtProp.bstrVal);
          module.partNumber.erase(
            module.partNumber.find_last_not_of(" \t\r\n") + 1);
        }
        VariantClear(&vtProp);

        // Capacity
        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"Capacity", 0, &vtProp, 0, 0))) {
          uint64_t capacity = 0;

          if (vtProp.vt == VT_BSTR) {
            _bstr_t bstr(vtProp.bstrVal);
            capacity = _atoi64(static_cast<const char*>(bstr));
          } else if (vtProp.vt == VT_I8 || vtProp.vt == VT_UI8) {
            capacity = (vtProp.vt == VT_I8) ? vtProp.llVal : vtProp.ullVal;
          } else if (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4) {
            capacity = (vtProp.vt == VT_I4) ? vtProp.lVal : vtProp.ulVal;
          }

          if (capacity > 0) {
            module.capacityGB = static_cast<double>(capacity) / BYTES_TO_GB;
          }
        }
        VariantClear(&vtProp);

        // ConfiguredClockSpeed
        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"ConfiguredClockSpeed", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_I4) {
          module.configuredSpeedMHz = vtProp.lVal;
        }
        VariantClear(&vtProp);

        // Speed
        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"Speed", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_I4) {
          module.speedMHz = vtProp.lVal;
        }
        VariantClear(&vtProp);

        // Memory type
        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"SMBIOSMemoryType", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_I4) {
          int memType = vtProp.lVal;
          module.memoryType = (memType == DDR4_TYPE_CODE)   ? "DDR4"
                              : (memType == DDR5_TYPE_CODE) ? "DDR5"
                              : (memType == 24)             ? "DDR3"
                              : (memType == 34)             ? "DDR5"
                              : (memType > 0)
                                ? ("Type-" + std::to_string(memType))
                                : "Unknown";
        }
        VariantClear(&vtProp);

        // Form Factor - ADD THIS CODE
        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"FormFactor", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_I4) {
          module.formFactor = vtProp.lVal;
        }
        VariantClear(&vtProp);

        // Bank Label - ADD THIS CODE
        VariantInit(&vtProp);
        if (SUCCEEDED(pObj->Get(L"BankLabel", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_BSTR) {
          module.bankLabel = _bstr_t(vtProp.bstrVal);
        }
        VariantClear(&vtProp);

        if (module.capacityGB > 0 && !module.deviceLocator.empty()) {
          modules.push_back(module);
          moduleCount++;
        }
      });

    // Determine XMP/DOCP status
    for (auto& module : modules) {
      if (module.speedMHz > 0 && module.configuredSpeedMHz > 0) {
        bool isDDR5 = module.memoryType == "DDR5" ||
                      module.configuredSpeedMHz > DDR5_THRESHOLD_SPEED;

        if ((module.memoryType == "DDR4" &&
             module.configuredSpeedMHz > DDR4_MAX_STANDARD_SPEED) ||
            (isDDR5 && module.configuredSpeedMHz > DDR5_MAX_STANDARD_SPEED)) {
          module.xmpStatus = "Running at rated speed";
          xmpEnabled = true;
        } else if (module.speedMHz == module.configuredSpeedMHz) {
          module.xmpStatus = "Running at default speed";
        } else if (module.configuredSpeedMHz > module.speedMHz) {
          module.xmpStatus = "Overclocked";
          xmpEnabled = true;
        } else {
          module.xmpStatus = "Speed mismatch - check BIOS settings";
        }
      }
    }

    // Determine channel configuration
    if (moduleCount > 0) {
      // First check if we have SODIMMs
      bool hasSodimm = false;
      int sodimmCount = 0;
      bool hasDimm = false;

      for (const auto& module : modules) {
        if (module.formFactor == 12) {  // 12 = SODIMM
          hasSodimm = true;
          sodimmCount++;
        } else if (module.formFactor == 8) {  // 8 = DIMM
          hasDimm = true;
        }
      }

      // Handle laptop/SODIMM memory differently - be more cautious with channel
      // detection
      if (hasSodimm && !hasDimm) {
        if (sodimmCount == 1) {
          channelStatus = "Single Channel Mode (SODIMM)";
        } else if (sodimmCount == 2) {
          // For laptops with 2 SODIMMs, it's usually dual channel but not
          // guaranteed
          channelStatus = "Likely Dual Channel Mode (SODIMM)";
        } else if (sodimmCount > 2) {
          channelStatus = "Multi-Channel Mode (SODIMM)";
        }
      } else {
        // Handle desktop/DIMM modules with more detailed analysis
        std::set<std::string> channelIdentifiers;
        std::map<std::string, int> locationCount;

        // Count duplicated device locations
        for (const auto& module : modules) {
          locationCount[module.deviceLocator]++;
        }

        // Check if we have duplicate locations that need disambiguation
        bool hasDuplicateLocations = false;
        for (const auto& loc : locationCount) {
          if (loc.second > 1) {
            hasDuplicateLocations = true;
            break;
          }
        }

        // First pass - see if bank labels contain explicit channel information
        bool hasChannelInfo = false;
        for (const auto& module : modules) {
          if (!module.bankLabel.empty()) {
            size_t channelPos = module.bankLabel.find("CHANNEL");
            if (channelPos != std::string::npos) {
              hasChannelInfo = true;
              break;
            }
          }
        }

        // Second pass - collect unique channel identifiers
        for (const auto& module : modules) {
          std::string channelId;

          if (hasChannelInfo && !module.bankLabel.empty()) {
            // Use bank label channel info when available (most accurate)
            size_t channelPos = module.bankLabel.find("CHANNEL");
            if (channelPos != std::string::npos) {
              channelId = module.bankLabel.substr(channelPos);
            }
          }

          // If no channel info in bank label but we have duplicate locations
          if (channelId.empty() && hasDuplicateLocations &&
              locationCount[module.deviceLocator] > 1) {
            // Create a composite identifier using both location and bank label
            if (!module.bankLabel.empty()) {
              channelId = module.deviceLocator + " " + module.bankLabel;
            } else {
              channelId =
                module.deviceLocator + "_" +
                std::to_string(&module -
                               &modules[0]);  // Use address as unique suffix
            }
          }

          // Otherwise use the device location which is already unique
          if (channelId.empty()) {
            channelId = module.deviceLocator;
          }

          if (!channelId.empty()) {
            channelIdentifiers.insert(channelId);
          }
        }

        // Determine channel mode based on unique identifiers
        if (channelIdentifiers.size() > 1) {
          if (channelIdentifiers.size() == 2) {
            channelStatus = "Dual Channel Mode";
          } else if (channelIdentifiers.size() >= 4) {
            channelStatus = "Quad Channel Mode";
          } else if (channelIdentifiers.size() == 3) {
            channelStatus = "Triple Channel Mode";
          } else {
            channelStatus = "Multi-Channel Mode";
          }
        } else if (moduleCount == 1) {
          channelStatus = "Single Channel Mode";
        } else {
          // For multiple modules where we couldn't determine channels from
          // identifiers, make an educated guess based on module count
          if (moduleCount == 2 || moduleCount == 4) {
            channelStatus = "Dual Channel Mode (assumed)";
          } else if (moduleCount == 3) {
            channelStatus = "Triple Channel Mode (assumed)";
          } else if (moduleCount >= 6) {
            channelStatus = "Multi-Channel Mode (assumed)";
          } else {
            channelStatus = "Unknown Channel Mode";
          }
        }
      }
    }

    // Update derived memory info for later use
    bool isDDR5 = false;
    int clockSpeed = 0;

    for (const auto& module : modules) {
      if (module.memoryType == "DDR5" ||
          module.configuredSpeedMHz > DDR5_THRESHOLD_SPEED) {
        isDDR5 = true;
      }

      if (module.configuredSpeedMHz > clockSpeed) {
        clockSpeed = module.configuredSpeedMHz;
      }
    }

    // Update derived memory type and clock speed
    if (!modules.empty()) {
      m_derivedMemoryType = isDDR5 ? DDR5_TYPE_CODE : DDR4_TYPE_CODE;
      m_derivedMemoryClockSpeed = clockSpeed;
      m_lastRamUpdate = 0;  // Force RAM info cache refresh
    }
  }

  void UpdateSensors() {
    m_lastCpuUpdate = 0;
    m_lastRamUpdate = 0;
  }

  std::string printAllCPUInfo() {
    CPUInfo info = GetCPUInfo();

    std::stringstream ss;
    ss << "CPU Information Summary\n";
    ss << "----------------------\n\n";

    ss << "CPU: " << info.name << "\n";
    ss << "Vendor: " << info.vendor << "\n";
    ss << "Architecture: " << info.architecture << "\n";
    ss << "Socket: " << info.socket << "\n";
    ss << "Cores: " << info.physicalCores << " physical, " << info.logicalCores
       << " logical\n";
    ss << "Cache: " << info.cacheSizes << "\n";
    ss << "Base Speed: " << info.baseClockSpeed << " MHz\n";
    ss << "Current Speed: " << info.currentClockSpeed << " MHz\n";
    ss << "Performance: " << info.performancePercentage << "%\n";
    ss << "CPU Load: " << std::fixed << std::setprecision(1)
       << info.loadPercentage << "%\n";

    if (info.temperature > 0) {
      ss << "Temperature: " << std::fixed << std::setprecision(1)
         << info.temperature << "°C\n";
    }

    ss << "Power Plan: " << info.powerPlan << "\n";
    ss << "Virtualization: "
       << (info.virtualizationEnabled ? "Enabled" : "Disabled") << "\n";
    ss << "SMT/Hyper-Threading: " << (info.smtActive ? "Active" : "Inactive")
       << "\n";
    ss << "AVX Support: " << (info.avxSupport ? "Yes" : "No") << "\n";
    ss << "AVX2 Support: " << (info.avx2Support ? "Yes" : "No") << "\n";

    ss << "\nPer-Core Clock Speeds:\n";
    for (int i = 0; i < info.coreClocks.size(); i++) {
      ss << "Core " << i << ": " << info.coreClocks[i] << " MHz";
      if (i < info.coreLoads.size()) {
        ss << " (Load: " << std::fixed << std::setprecision(1)
           << info.coreLoads[i] << "%)";
      }
      ss << "\n";
    }

    return ss.str();
  }

  std::string logRawWmiData() {
    std::stringstream ss;
    try {
      if (!wmiHelper.initialize()) {
        return "ERROR: Failed to initialize WMI connection\n";
      }

      // Log CPU WMI data
      ss << "=== RAW WMI CPU Data ===\n";
      int cpuCount = 0;
      wmiHelper.executeQuery<std::string>(
        L"SELECT * FROM Win32_Processor",
        [&ss, &cpuCount](IWbemClassObject* pObj) {
          cpuCount++;
          ss << "CPU #" << cpuCount << " Properties:\n";

          // Get property names
          SAFEARRAY* pNames = nullptr;
          if (SUCCEEDED(pObj->GetNames(
                nullptr, WBEM_FLAG_ALWAYS | WBEM_FLAG_NONSYSTEM_ONLY, nullptr,
                &pNames))) {
            long lLower, lUpper;
            SafeArrayGetLBound(pNames, 1, &lLower);
            SafeArrayGetUBound(pNames, 1, &lUpper);

            for (long i = lLower; i <= lUpper; i++) {
              BSTR bstrName;
              SafeArrayGetElement(pNames, &i, &bstrName);

              VARIANT vtProp;
              VariantInit(&vtProp);
              if (SUCCEEDED(
                    pObj->Get(bstrName, 0, &vtProp, nullptr, nullptr))) {
                // Determine property name as std::string for checks
                std::string propName = static_cast<const char*>(_bstr_t(bstrName));
                ss << "  " << propName << ": ";

                // Redact sensitive computer/system properties to avoid
                // leaking PII (computer name, user name, DNS/Domain, etc.)
                bool redact = (propName == "Name" || propName == "DNSHostName" ||
                               propName == "Domain" || propName == "UserName" ||
                               propName == "Workgroup" || propName == "SystemName");

                if (redact) {
                  ss << "[hidden for data privacy reasons]";
                } else {
                  // Convert variant to string based on type
                  if (vtProp.vt == VT_BSTR) {
                    ss << static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                  } else if (vtProp.vt == VT_I4) {
                    ss << vtProp.lVal;
                  } else if (vtProp.vt == VT_I8) {
                    ss << vtProp.llVal;
                  } else if (vtProp.vt == VT_BOOL) {
                    ss << (vtProp.boolVal ? "True" : "False");
                  } else if (vtProp.vt == VT_NULL) {
                    ss << "NULL";
                  } else {
                    ss << "[Type: " << vtProp.vt << "]";
                  }
                }
                ss << "\n";
              }
              VariantClear(&vtProp);
              SysFreeString(bstrName);
            }
            SafeArrayDestroy(pNames);
          }
          ss << "\n";
        });

      // Log RAM WMI data
      ss << "\n=== RAW WMI Memory Data ===\n";
      int memModuleCount = 0;
      wmiHelper.executeQuery<std::string>(
        L"SELECT * FROM Win32_PhysicalMemory",
        [&ss, &memModuleCount](IWbemClassObject* pObj) {
          memModuleCount++;
          ss << "Memory Module #" << memModuleCount << " Properties:\n";

          // Get property names
          SAFEARRAY* pNames = nullptr;
          if (SUCCEEDED(pObj->GetNames(
                nullptr, WBEM_FLAG_ALWAYS | WBEM_FLAG_NONSYSTEM_ONLY, nullptr,
                &pNames))) {
            long lLower, lUpper;
            SafeArrayGetLBound(pNames, 1, &lLower);
            SafeArrayGetUBound(pNames, 1, &lUpper);

            for (long i = lLower; i <= lUpper; i++) {
              BSTR bstrName;
              SafeArrayGetElement(pNames, &i, &bstrName);

              VARIANT vtProp;
              VariantInit(&vtProp);
              if (SUCCEEDED(
                    pObj->Get(bstrName, 0, &vtProp, nullptr, nullptr))) {
                // Determine property name as std::string for checks
                std::string propName = static_cast<const char*>(_bstr_t(bstrName));
                ss << "  " << propName << ": ";

                // Redact sensitive processor properties (e.g., ProcessorId)
                bool redact = (propName == "ProcessorId" || propName == "UniqueId");

                if (redact) {
                  ss << "[hidden for data privacy reasons]";
                } else {
                  // Convert variant to string based on type
                  if (vtProp.vt == VT_BSTR) {
                    ss << static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                  } else if (vtProp.vt == VT_I4) {
                    ss << vtProp.lVal;
                  } else if (vtProp.vt == VT_I8) {
                    ss << vtProp.llVal;
                  } else if (vtProp.vt == VT_BOOL) {
                    ss << (vtProp.boolVal ? "True" : "False");
                  } else if (vtProp.vt == VT_NULL) {
                    ss << "NULL";
                  } else {
                    ss << "[Type: " << vtProp.vt << "]";
                  }
                }
                ss << "\n";
              }
              VariantClear(&vtProp);
              SysFreeString(bstrName);
            }
            SafeArrayDestroy(pNames);
          }
          ss << "\n";
        });

      // Log computer system info
      ss << "\n=== RAW WMI Computer System Data ===\n";
      wmiHelper.executeQuery<std::string>(
        L"SELECT * FROM Win32_ComputerSystem", [&ss](IWbemClassObject* pObj) {
          // Get property names
          SAFEARRAY* pNames = nullptr;
          if (SUCCEEDED(pObj->GetNames(
                nullptr, WBEM_FLAG_ALWAYS | WBEM_FLAG_NONSYSTEM_ONLY, nullptr,
                &pNames))) {
            long lLower, lUpper;
            SafeArrayGetLBound(pNames, 1, &lLower);
            SafeArrayGetUBound(pNames, 1, &lUpper);

            for (long i = lLower; i <= lUpper; i++) {
              BSTR bstrName;
              SafeArrayGetElement(pNames, &i, &bstrName);

              VARIANT vtProp;
              VariantInit(&vtProp);
              if (SUCCEEDED(
                    pObj->Get(bstrName, 0, &vtProp, nullptr, nullptr))) {
                ss << "  " << static_cast<const char*>(_bstr_t(bstrName))
                   << ": ";

                // Determine property name as std::string for checks
                std::string propName = static_cast<const char*>(_bstr_t(bstrName));

                // Redact potentially sensitive memory properties like
                // SerialNumber
                if (propName == "SerialNumber" || propName == "PartNumber") {
                  ss << "[hidden for data privacy reasons]";
                } else {
                  // Convert variant to string based on type
                  if (vtProp.vt == VT_BSTR) {
                    ss << static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                  } else if (vtProp.vt == VT_I4) {
                    ss << vtProp.lVal;
                  } else if (vtProp.vt == VT_I8) {
                    ss << vtProp.llVal;
                  } else if (vtProp.vt == VT_BOOL) {
                    ss << (vtProp.boolVal ? "True" : "False");
                  } else if (vtProp.vt == VT_NULL) {
                    ss << "NULL";
                  } else {
                    ss << "[Type: " << vtProp.vt << "]";
                  }
                }
                ss << "\n";
              }
              VariantClear(&vtProp);
              SysFreeString(bstrName);
            }
            SafeArrayDestroy(pNames);
          }
          ss << "\n";
        });

      // Log power plan info
      ss << "\n=== RAW WMI Power Plan Data ===\n";
      wmiHelper.executeQuery<std::string>(
        L"SELECT * FROM Win32_PowerPlan", [&ss](IWbemClassObject* pObj) {
          VARIANT vtPlan, vtActive;
          VariantInit(&vtPlan);
          VariantInit(&vtActive);

          bool isActive = false;
          std::string planName = "Unknown";

          if (SUCCEEDED(pObj->Get(L"ElementName", 0, &vtPlan, 0, 0)) &&
              vtPlan.vt == VT_BSTR) {
            planName = static_cast<const char*>(_bstr_t(vtPlan.bstrVal));
          }

          if (SUCCEEDED(pObj->Get(L"IsActive", 0, &vtActive, 0, 0)) &&
              vtActive.vt == VT_BOOL) {
            isActive = vtActive.boolVal != 0;
          }

          ss << "  Plan: " << planName
             << " (Active: " << (isActive ? "Yes" : "No") << ")\n";

          VariantClear(&vtPlan);
          VariantClear(&vtActive);
        });
    } catch (const std::exception& e) {
      ss << "ERROR: Exception in WMI logging: " << e.what() << "\n";
    }
    return ss.str();
  }

  std::string logRawPdhData() {
    std::stringstream ss;
    try {
      ss << "=== RAW PDH Counter Data ===\n";

      if (!pdhHelper.initialize()) {
        ss << "ERROR: Failed to initialize PDH\n";
        return ss.str();
      }

      // Collect data from PDH counters
      if (!pdhHelper.collectData()) {
        ss << "ERROR: Failed to collect PDH data\n";
        return ss.str();
      }

      // Log CPU usage counters
      double totalUsage;
      if (pdhHelper.getCounterValue("TotalUsage", totalUsage)) {
        ss << "TotalUsage: " << totalUsage << "\n";
      } else {
        ss << "TotalUsage: Failed to retrieve\n";
      }

      // Get available counters
      SYSTEM_INFO sysInfo;
      GetSystemInfo(&sysInfo);
      int numCores = sysInfo.dwNumberOfProcessors;

      // Log core-specific counters
      for (int i = 0; i < numCores; i++) {
        ss << "Core " << i << " Metrics:\n";

        double frequency;
        if (pdhHelper.getCounterValue("CoreFreq" + std::to_string(i),
                                      frequency)) {
          ss << "  Frequency: " << frequency << " MHz\n";
        } else {
          ss << "  Frequency: Failed to retrieve\n";
        }

        double load;
        if (pdhHelper.getCounterValue("CoreLoad" + std::to_string(i), load)) {
          ss << "  Load: " << load << "%\n";
        } else {
          ss << "  Load: Failed to retrieve\n";
        }
      }

      // Try to query available PDH counters
      ss << "\nAvailable PDH Counters:\n";
      PDH_STATUS status;
      DWORD counterListSize = 0;
      DWORD instanceListSize = 0;

      // First call to get buffer sizes
      status = PdhEnumObjectItemsW(NULL, NULL, L"Processor Information", NULL,
                                   &counterListSize, NULL, &instanceListSize,
                                   PERF_DETAIL_WIZARD, 0);

      if (status == PDH_MORE_DATA) {
        std::vector<WCHAR> counterList(counterListSize);
        std::vector<WCHAR> instanceList(instanceListSize);

        status = PdhEnumObjectItemsW(NULL, NULL, L"Processor Information",
                                     counterList.data(), &counterListSize,
                                     instanceList.data(), &instanceListSize,
                                     PERF_DETAIL_WIZARD, 0);

        if (status == ERROR_SUCCESS) {
          // List counter names
          WCHAR* counter = counterList.data();
          while (*counter) {
            ss << "  Counter: " << static_cast<const char*>(_bstr_t(counter))
               << "\n";
            counter += wcslen(counter) + 1;
          }

          // List instance names
          WCHAR* instance = instanceList.data();
          while (*instance) {
            ss << "  Instance: " << static_cast<const char*>(_bstr_t(instance))
               << "\n";
            instance += wcslen(instance) + 1;
          }
        } else {
          ss << "  Failed to enumerate counters: 0x" << std::hex << status
             << std::dec << "\n";
        }
      } else {
        ss << "  Failed to get counter list size: 0x" << std::hex << status
           << std::dec << "\n";
      }
    } catch (const std::exception& e) {
      ss << "ERROR: Exception in PDH logging: " << e.what() << "\n";
    }
    return ss.str();
  }

  std::string logRawSystemInfo() {
    std::stringstream ss;
    try {
      ss << "=== RAW System Information ===\n";

      // Get basic system info
      SYSTEM_INFO sysInfo;
      GetSystemInfo(&sysInfo);

      ss << "System Information:\n";
      ss << "  Processor Architecture: " << sysInfo.wProcessorArchitecture
         << "\n";
      ss << "  Number of Processors: " << sysInfo.dwNumberOfProcessors << "\n";
      ss << "  Page Size: " << sysInfo.dwPageSize << " bytes\n";
      ss << "  Processor Type: " << sysInfo.dwProcessorType << "\n";
      ss << "  Active Processor Mask: 0x" << std::hex
         << sysInfo.dwActiveProcessorMask << std::dec << "\n";

      // Get system memory information
      MEMORYSTATUSEX memStatus = {sizeof(MEMORYSTATUSEX)};
      if (GlobalMemoryStatusEx(&memStatus)) {
        ss << "\nMemory Information:\n";
        ss << "  Memory Load: " << memStatus.dwMemoryLoad << "%\n";
        ss << "  Total Physical Memory: "
           << memStatus.ullTotalPhys / (1024 * 1024) << " MB\n";
        ss << "  Available Physical Memory: "
           << memStatus.ullAvailPhys / (1024 * 1024) << " MB\n";
        ss << "  Total Virtual Memory: "
           << memStatus.ullTotalVirtual / (1024 * 1024) << " MB\n";
        ss << "  Available Virtual Memory: "
           << memStatus.ullAvailVirtual / (1024 * 1024) << " MB\n";
        ss << "  Total Page File: "
           << memStatus.ullTotalPageFile / (1024 * 1024) << " MB\n";
        ss << "  Available Page File: "
           << memStatus.ullAvailPageFile / (1024 * 1024) << " MB\n";
      } else {
        ss << "\nFailed to get memory information\n";
      }

      // Get processor cache information
      ss << "\nProcessor Cache Information:\n";
      DWORD bufferSize = 0;
      if (GetLogicalProcessorInformation(NULL, &bufferSize) == FALSE &&
          GetLastError() == ERROR_INSUFFICIENT_BUFFER) {

        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(
          bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));

        if (GetLogicalProcessorInformation(buffer.data(), &bufferSize)) {
          for (size_t i = 0; i < buffer.size(); i++) {
            if (buffer[i].Relationship == RelationCache) {
              CACHE_DESCRIPTOR cache = buffer[i].Cache;

              ss << "  Cache Entry:\n";
              ss << "    Level: " << static_cast<int>(cache.Level) << "\n";
              ss << "    Type: ";
              switch (cache.Type) {
                case CacheUnified:
                  ss << "Unified\n";
                  break;
                case CacheInstruction:
                  ss << "Instruction\n";
                  break;
                case CacheData:
                  ss << "Data\n";
                  break;
                case CacheTrace:
                  ss << "Trace\n";
                  break;
                default:
                  ss << "Unknown (" << cache.Type << ")\n";
              }
              ss << "    Size: " << cache.Size / 1024 << " KB\n";
              ss << "    Line Size: " << cache.LineSize << " bytes\n";
              ss << "    Associativity: "
                 << static_cast<int>(cache.Associativity) << "\n";
            }
          }
        } else {
          ss << "  Failed to get cache information\n";
        }
      } else {
        ss << "  Failed to get cache information buffer size\n";
      }

      // Get raw CPUID information
      ss << "\nCPUID Raw Information:\n";
      int cpuInfo[4] = {0};

      // Get vendor ID
      __cpuid(cpuInfo, 0);
      char vendorId[13] = {0};
      memcpy(vendorId, &cpuInfo[1], 4);
      memcpy(vendorId + 4, &cpuInfo[3], 4);
      memcpy(vendorId + 8, &cpuInfo[2], 4);
      vendorId[12] = '\0';

      ss << "  Vendor ID: " << vendorId << "\n";
      ss << "  Max Standard Function: 0x" << std::hex << cpuInfo[0] << std::dec
         << "\n";

      // Get basic feature flags (if supported)
      if (cpuInfo[0] >= 1) {
        __cpuid(cpuInfo, 1);
        ss << "  Family: " << ((cpuInfo[0] >> 8) & 0xF) << "\n";
        ss << "  Model: " << ((cpuInfo[0] >> 4) & 0xF) << "\n";
        ss << "  Stepping: " << (cpuInfo[0] & 0xF) << "\n";
        ss << "  Feature Flags (EDX): 0x" << std::hex << cpuInfo[3] << std::dec
           << "\n";
        ss << "  Feature Flags (ECX): 0x" << std::hex << cpuInfo[2] << std::dec
           << "\n";

        // Intel or AMD specific interpretations
        ss << "  SSE: " << ((cpuInfo[3] & (1 << 25)) ? "Yes" : "No") << "\n";
        ss << "  SSE2: " << ((cpuInfo[3] & (1 << 26)) ? "Yes" : "No") << "\n";
        ss << "  SSE3: " << ((cpuInfo[2] & (1 << 0)) ? "Yes" : "No") << "\n";
        ss << "  AVX: " << ((cpuInfo[2] & (1 << 28)) ? "Yes" : "No") << "\n";
        ss << "  Hyper-Threading: " << ((cpuInfo[3] & (1 << 28)) ? "Yes" : "No")
           << "\n";
      }

      // Get extended CPUID info
      __cpuid(cpuInfo, 0x80000000);
      ss << "  Max Extended Function: 0x" << std::hex << cpuInfo[0] << std::dec
         << "\n";

      // Check for extended processor info
      if (cpuInfo[0] >= 0x80000001) {
        __cpuid(cpuInfo, 0x80000001);
        ss << "  Extended Feature Flags (EDX): 0x" << std::hex << cpuInfo[3]
           << std::dec << "\n";
        ss << "  Extended Feature Flags (ECX): 0x" << std::hex << cpuInfo[2]
           << std::dec << "\n";
      }

      // Get processor brand string
      if (cpuInfo[0] >= 0x80000004) {
        char brand[49] = {0};
        __cpuid(cpuInfo, 0x80000002);
        memcpy(brand, cpuInfo, 16);

        __cpuid(cpuInfo, 0x80000003);
        memcpy(brand + 16, cpuInfo, 16);

        __cpuid(cpuInfo, 0x80000004);
        memcpy(brand + 32, cpuInfo, 16);

        ss << "  Processor Brand: " << brand << "\n";
      }
    } catch (const std::exception& e) {
      ss << "ERROR: Exception in system info logging: " << e.what() << "\n";
    }
    return ss.str();
  }

  // Modify the logRawData method in the Impl class
  std::string logRawData() {
    std::stringstream ss;

    ss << "===================================================\n";
    ss << "=== WinHardwareMonitor Raw Data Collection Log ===\n";
    ss << "===================================================\n\n";

    // Call each raw data collection method with try/catch blocks for each
    // section
    try {
      ss << "--- System Information Section ---\n";
      ss << logRawSystemInfo();
      ss << "\n\n";
    } catch (const std::exception& e) {
      ss << "ERROR collecting system info: " << e.what() << "\n\n";
    }

    try {
      ss << "--- WMI Data Section ---\n";
      ss << logRawWmiData();
      ss << "\n\n";
    } catch (const std::exception& e) {
      ss << "ERROR collecting WMI data: " << e.what() << "\n\n";
    }

    try {
      ss << "--- PDH Data Section ---\n";
      ss << logRawPdhData();
      ss << "\n\n";
    } catch (const std::exception& e) {
      ss << "ERROR collecting PDH data: " << e.what() << "\n\n";
    }

    return ss.str();
  }

  void checkVirtualizationStatus(CPUInfo& info) {
    info.virtualizationEnabled = false;  // Default to false

    wmiHelper.executeQuery<bool>(
      L"SELECT HypervisorPresent FROM Win32_ComputerSystem",
      [&info](IWbemClassObject* pObj) {
        VARIANT vtProp;
        VariantInit(&vtProp);

        if (SUCCEEDED(pObj->Get(L"HypervisorPresent", 0, &vtProp, 0, 0)) &&
            vtProp.vt == VT_BOOL) {
          info.virtualizationEnabled = (vtProp.boolVal != VARIANT_FALSE);
        }

        VariantClear(&vtProp);
      });
  }
};

WinHardwareMonitor::WinHardwareMonitor() : pImpl(new Impl()) {}

WinHardwareMonitor::~WinHardwareMonitor() {}

CPUInfo WinHardwareMonitor::getCPUInfo() { return pImpl->GetCPUInfo(); }

GPUInfo WinHardwareMonitor::getGPUInfo() { return pImpl->GetGPUInfo(); }

RAMInfo WinHardwareMonitor::getRAMInfo() { return pImpl->GetRAMInfo(); }

void WinHardwareMonitor::updateSensors() { pImpl->UpdateSensors(); }

std::string WinHardwareMonitor::printAllCPUInfo() {
  return pImpl->printAllCPUInfo();
}

void WinHardwareMonitor::getDetailedMemoryInfo(
  std::vector<MemoryModuleInfo>& modules, std::string& channelStatus,
  bool& xmpEnabled) {
  // Create local variables to avoid reference issues across threads
  std::vector<MemoryModuleInfo> localModules;
  std::string localChannelStatus;
  bool localXmpEnabled = false;

  // Call the implementation with local variables
  pImpl->getDetailedMemoryInfo(localModules, localChannelStatus,
                               localXmpEnabled);

  // Copy the results back to the output parameters
  modules = std::move(localModules);
  channelStatus = std::move(localChannelStatus);
  xmpEnabled = localXmpEnabled;
}

std::string WinHardwareMonitor::logRawData() {
  try {
    return pImpl->logRawData();
  } catch (const std::exception& e) {
    return "ERROR: Exception in WinHardwareMonitor::logRawData: " +
           std::string(e.what());
  }
}
