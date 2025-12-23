#include "SystemWrapper.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

#include "../logging/Logger.h"

#include <Windows.h>
#include <Powrprof.h>
#include <Wbemidl.h>
#include <cfgmgr32.h>
#include <comdef.h>
#include <devguid.h>
#include <intrin.h>
#include <setupapi.h>

#ifndef SPDRP_DRIVER_DATE
#define SPDRP_DRIVER_DATE 0x0000000A
#endif

#ifndef SPDRP_INSTALL_DATE
#define SPDRP_INSTALL_DATE 0x00000020
#endif

#ifndef SPDRP_PROVIDERNAME
#define SPDRP_PROVIDERNAME 0x0000000B
#endif

static constexpr int BUFFER_SIZE = 1024;
static constexpr int MAX_PATH_LENGTH = MAX_PATH;

SystemWrapper::SystemWrapper() {}

SystemWrapper::~SystemWrapper() {}

int SystemWrapper::getL1CacheKB(int physicalCores) {
  int cpuInfo[4] = {-1};
  __cpuid(cpuInfo, 0x80000005);

  if (cpuInfo[1] > 0) {
    int l1DataCache = (cpuInfo[2] >> 24) & 0xFF;
    int l1InstructionCache = (cpuInfo[3] >> 24) & 0xFF;

    if (l1DataCache > 0 && l1InstructionCache > 0) {
      return physicalCores * (l1DataCache + l1InstructionCache);
    }
  }

  if (physicalCores > 0) {
    return 64 * physicalCores;
  }

  return -1;
}

int SystemWrapper::getL2CacheKB() { return -1; }

int SystemWrapper::getL3CacheKB() { return -1; }

std::pair<std::string, std::string> SystemWrapper::getMotherboardInfo() {
  std::string manufacturer = "no_data";
  std::string model = "no_data";

  HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
  if (SUCCEEDED(hr)) {
    IWbemLocator* pLoc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, (LPVOID*)&pLoc);

    if (SUCCEEDED(hr) && pLoc) {
      IWbemServices* pSvc = nullptr;
      hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, 0,
                               NULL, 0, 0, &pSvc);

      if (SUCCEEDED(hr) && pSvc) {
        hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                               RPC_C_AUTHN_LEVEL_CALL,
                               RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

        if (SUCCEEDED(hr)) {
          IEnumWbemClassObject* pEnumerator = nullptr;
          hr = pSvc->ExecQuery(
            bstr_t("WQL"), bstr_t("SELECT * FROM Win32_BaseBoard"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
            &pEnumerator);

          if (SUCCEEDED(hr) && pEnumerator) {
            IWbemClassObject* pclsObj = nullptr;
            ULONG uReturn = 0;

            while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) ==
                   S_OK) {
              VARIANT vtProp;

              VariantInit(&vtProp);
              if (SUCCEEDED(pclsObj->Get(L"Manufacturer", 0, &vtProp, 0, 0))) {
                if (vtProp.vt == VT_BSTR && vtProp.bstrVal != nullptr) {
                  manufacturer =
                    static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                }
                VariantClear(&vtProp);
              }

              VariantInit(&vtProp);
              if (SUCCEEDED(pclsObj->Get(L"Product", 0, &vtProp, 0, 0))) {
                if (vtProp.vt == VT_BSTR && vtProp.bstrVal != nullptr) {
                  model = static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                }
                VariantClear(&vtProp);
              }

              pclsObj->Release();
            }

            pEnumerator->Release();
          }
        }
        pSvc->Release();
      }
      pLoc->Release();
    }
    CoUninitialize();
  }

  if (manufacturer == "no_data" || model == "no_data") {
    HKEY hKey;
    char buffer[256] = {0};
    DWORD bufferSize = sizeof(buffer);
    DWORD valueType = 0;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
      if (manufacturer == "no_data") {
        bufferSize = sizeof(buffer);
        RegQueryValueExA(hKey, "BaseBoardManufacturer", NULL, &valueType,
                         (LPBYTE)buffer, &bufferSize);
        manufacturer = buffer;
      }

      if (model == "no_data") {
        bufferSize = sizeof(buffer);
        RegQueryValueExA(hKey, "BaseBoardProduct", NULL, &valueType,
                         (LPBYTE)buffer, &bufferSize);
        model = buffer;
      }

      RegCloseKey(hKey);
    }
  }

  return {manufacturer, model};
}

std::pair<bool, std::string> SystemWrapper::getChipsetDriverInfo() {
  const char* amdLocations[] = {
    "SOFTWARE\\WOW6432Node\\AMD\\AMD_Chipset_IODrivers",
    "SOFTWARE\\AMD\\AMD Chipset Software",
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{B5EBD985-555B-"
    "9D03-F77B-112A296A81F9}",
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{0ECE0C6C-ABB5-"
    "4AC1-99DE-6F11C4797AEB}",
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{0399F1BF-8603-"
    "4633-ACC9-F62589DF0B42}",
    "SOFTWARE\\WOW6432Node\\AMD\\AMD Chipset Software"};

  const char* intelLocations[] = {
    "SOFTWARE\\Intel\\IntelChipsetSoftware",
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{1CEAC85D-2590-"
    "4760-800F-8DE5E91F3700}",
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{EBB4E1C1-AD41-"
    "4160-9B46-C7FEE83BF5C1}",
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ChipsetInstall",
    "SOFTWARE\\WOW6432Node\\Intel\\IntelChipsetSoftware"};

  HKEY hKey;
  char buffer[BUFFER_SIZE] = {0};
  DWORD bufferSize = sizeof(buffer);
  DWORD valueType = 0;

  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "SOFTWARE\\WOW6432Node\\AMD\\AMD_Chipset_IODrivers", 0,
                    KEY_READ, &hKey) == ERROR_SUCCESS) {
    bufferSize = sizeof(buffer);
    if (RegQueryValueExA(hKey, "ProductVersion", NULL, &valueType,
                         (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
      RegCloseKey(hKey);
      return {true, "AMD Chipset Driver " + std::string(buffer)};
    }
    RegCloseKey(hKey);
  }

  for (const char* path : amdLocations) {
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hKey) ==
        ERROR_SUCCESS) {
      bufferSize = sizeof(buffer);

      const char* versionKeys[] = {"ProductVersion", "Version",
                                   "DisplayVersion", "VersionNumber",
                                   "DriverVersion"};
      for (const char* verKey : versionKeys) {
        if (RegQueryValueExA(hKey, verKey, NULL, &valueType, (LPBYTE)buffer,
                             &bufferSize) == ERROR_SUCCESS) {
          RegCloseKey(hKey);
          return {true, "AMD Chipset Driver " + std::string(buffer)};
        }
        bufferSize = sizeof(buffer);
      }
      RegCloseKey(hKey);
    }
  }

  for (const char* path : intelLocations) {
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &hKey) ==
        ERROR_SUCCESS) {
      bufferSize = sizeof(buffer);

      const char* versionKeys[] = {"Version", "DisplayVersion", "VersionNumber",
                                   "DriverVersion"};
      for (const char* verKey : versionKeys) {
        if (RegQueryValueExA(hKey, verKey, NULL, &valueType, (LPBYTE)buffer,
                             &bufferSize) == ERROR_SUCCESS) {
          RegCloseKey(hKey);
          return {true, "Intel Chipset Driver " + std::string(buffer)};
        }
        bufferSize = sizeof(buffer);
      }
      RegCloseKey(hKey);
    }
  }

  const char* uninstallPath =
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, uninstallPath, 0, KEY_READ, &hKey) ==
      ERROR_SUCCESS) {
    DWORD index = 0;
    char subKeyName[BUFFER_SIZE];
    DWORD subKeyNameSize = sizeof(subKeyName);

    while (RegEnumKeyExA(hKey, index++, subKeyName, &subKeyNameSize, NULL, NULL,
                         NULL, NULL) == ERROR_SUCCESS) {
      std::string fullSubKey = std::string(uninstallPath) + "\\" + subKeyName;
      HKEY subKey;

      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), 0, KEY_READ,
                        &subKey) == ERROR_SUCCESS) {
        bufferSize = sizeof(buffer);
        if (RegQueryValueExA(subKey, "DisplayName", NULL, &valueType,
                             (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
          std::string displayName = buffer;

          if (displayName.find("AMD Chipset Software") != std::string::npos ||
              displayName.find("AMD Chipset Driver") != std::string::npos ||
              displayName.find("Intel(R) Chipset Device") !=
                std::string::npos ||
              displayName.find("Intel Chipset") != std::string::npos) {

            bufferSize = sizeof(buffer);
            if (RegQueryValueExA(subKey, "DisplayVersion", NULL, &valueType,
                                 (LPBYTE)buffer,
                                 &bufferSize) == ERROR_SUCCESS) {
              std::string version = buffer;
              RegCloseKey(subKey);
              RegCloseKey(hKey);
              return {true, displayName + " " + version};
            }
          }
        }
        RegCloseKey(subKey);
      }
      subKeyNameSize = sizeof(subKeyName);
    }
    RegCloseKey(hKey);
  }

  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                    "SYSTEM\\CurrentControlSet\\Services\\amdpsp", 0, KEY_READ,
                    &hKey) == ERROR_SUCCESS) {
    RegCloseKey(hKey);
    return {true, "AMD Chipset Driver (version unavailable)"};
  } else if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                           "SYSTEM\\CurrentControlSet\\Services\\iaStor", 0,
                           KEY_READ, &hKey) == ERROR_SUCCESS) {
    RegCloseKey(hKey);
    return {true, "Intel Chipset Driver (version unavailable)"};
  } else if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                           "SOFTWARE\\NVIDIA Corporation\\Global\\nForce", 0,
                           KEY_READ, &hKey) == ERROR_SUCCESS) {
    RegCloseKey(hKey);
    return {true, "NVIDIA Chipset Driver (version unavailable)"};
  }

  return {false, ""};
}

std::string SystemWrapper::getChipsetModel() {
  auto [driverInstalled, driverInfo] = getChipsetDriverInfo();

  if (driverInstalled) {
    if (driverInfo.find("AMD") != std::string::npos) {
      HKEY hKey;
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                        "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ,
                        &hKey) == ERROR_SUCCESS) {
        char buffer[256] = {0};
        DWORD bufferSize = sizeof(buffer);
        DWORD valueType = 0;

        if (RegQueryValueExA(hKey, "BaseBoardProduct", NULL, &valueType,
                             (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
          std::string product = buffer;
          RegCloseKey(hKey);

          // Newer AMD chipsets
          if (product.find("X670E") != std::string::npos) return "AMD X670E";
          if (product.find("X670") != std::string::npos) return "AMD X670";
          if (product.find("B650E") != std::string::npos) return "AMD B650E";
          if (product.find("B650") != std::string::npos) return "AMD B650";
          if (product.find("A620") != std::string::npos) return "AMD A620";

          // Existing and older chipsets
          if (product.find("X570S") != std::string::npos) return "AMD X570S";
          if (product.find("X570") != std::string::npos) return "AMD X570";
          if (product.find("X470") != std::string::npos) return "AMD X470";
          if (product.find("X370") != std::string::npos) return "AMD X370";
          if (product.find("B550") != std::string::npos) return "AMD B550";
          if (product.find("B450") != std::string::npos) return "AMD B450";
          if (product.find("B350") != std::string::npos) return "AMD B350";
          if (product.find("A520") != std::string::npos) return "AMD A520";
          if (product.find("A320") != std::string::npos) return "AMD A320";

          // Threadripper chipsets
          if (product.find("TRX50") != std::string::npos) return "AMD TRX50";
          if (product.find("TRX40") != std::string::npos) return "AMD TRX40";
          if (product.find("X399") != std::string::npos) return "AMD X399";

          // APU chipsets
          if (product.find("X3D") != std::string::npos) return "AMD X3D";

          // Fallback to baseboard product if it contains chipset-like naming
          if (product.find("AMD") != std::string::npos) return product;

          // Generic AMD fallback
          return "AMD " + product;
        }
      }
      return "AMD";
    } else if (driverInfo.find("Intel") != std::string::npos) {
      HKEY hKey;
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                        "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ,
                        &hKey) == ERROR_SUCCESS) {
        char buffer[256] = {0};
        DWORD bufferSize = sizeof(buffer);
        DWORD valueType = 0;

        if (RegQueryValueExA(hKey, "BaseBoardProduct", NULL, &valueType,
                             (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
          std::string product = buffer;
          RegCloseKey(hKey);

          // Newer Intel chipsets (1xx0 series)
          if (product.find("Z890") != std::string::npos) return "Intel Z890";
          if (product.find("H810") != std::string::npos) return "Intel H810";
          if (product.find("B860") != std::string::npos) return "Intel B860";

          // 700 series
          if (product.find("Z790") != std::string::npos) return "Intel Z790";
          if (product.find("H770") != std::string::npos) return "Intel H770";
          if (product.find("B760") != std::string::npos) return "Intel B760";
          if (product.find("H710") != std::string::npos) return "Intel H710";

          // 600 series
          if (product.find("Z690") != std::string::npos) return "Intel Z690";
          if (product.find("H670") != std::string::npos) return "Intel H670";
          if (product.find("B660") != std::string::npos) return "Intel B660";
          if (product.find("H610") != std::string::npos) return "Intel H610";

          // 500 series
          if (product.find("Z590") != std::string::npos) return "Intel Z590";
          if (product.find("B560") != std::string::npos) return "Intel B560";
          if (product.find("H570") != std::string::npos) return "Intel H570";
          if (product.find("H510") != std::string::npos) return "Intel H510";

          // 400 series
          if (product.find("Z490") != std::string::npos) return "Intel Z490";
          if (product.find("B460") != std::string::npos) return "Intel B460";
          if (product.find("H470") != std::string::npos) return "Intel H470";
          if (product.find("H410") != std::string::npos) return "Intel H410";

          // 300 series
          if (product.find("Z390") != std::string::npos) return "Intel Z390";
          if (product.find("Z370") != std::string::npos) return "Intel Z370";
          if (product.find("H370") != std::string::npos) return "Intel H370";
          if (product.find("B365") != std::string::npos) return "Intel B365";
          if (product.find("B360") != std::string::npos) return "Intel B360";

          // HEDT chipsets
          if (product.find("X299") != std::string::npos) return "Intel X299";
          if (product.find("X399") != std::string::npos) return "Intel X399";

          // Fallback to baseboard product if it contains chipset-like naming
          if (product.find("Intel") != std::string::npos) return product;

          // Generic Intel fallback
          return "Intel " + product;
        }
      }
      return "Intel";
    } else if (driverInfo.find("NVIDIA") != std::string::npos) {
      HKEY hKey;
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                        "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ,
                        &hKey) == ERROR_SUCCESS) {
        char buffer[256] = {0};
        DWORD bufferSize = sizeof(buffer);
        DWORD valueType = 0;

        if (RegQueryValueExA(hKey, "BaseBoardProduct", NULL, &valueType,
                             (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
          std::string product = buffer;
          RegCloseKey(hKey);

          // NVIDIA chipsets
          if (product.find("nForce") != std::string::npos)
            return "NVIDIA " + product;

          // Generic NVIDIA fallback
          return "NVIDIA " + product;
        }
      }
      return "NVIDIA";
    }
  }

  // Final fallback - try to get BaseBoardProduct directly
  HKEY hKey;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS",
                    0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    char buffer[256] = {0};
    DWORD bufferSize = sizeof(buffer);
    DWORD valueType = 0;

    if (RegQueryValueExA(hKey, "BaseBoardProduct", NULL, &valueType,
                         (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
      std::string product = buffer;
      RegCloseKey(hKey);
      return product;
    }
    RegCloseKey(hKey);
  }

  return "";
}

std::tuple<std::string, std::string, std::string> SystemWrapper::getBiosInfo() {
  std::string version = "no_data";
  std::string date = "no_data";
  std::string manufacturer = "no_data";

  // First check registry directly
  HKEY hKey;
  char buffer[BUFFER_SIZE] = {0};
  DWORD bufferSize = sizeof(buffer);
  DWORD valueType = 0;

  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS",
                    0, KEY_READ, &hKey) == ERROR_SUCCESS) {
    // Get BIOS Version
    bufferSize = sizeof(buffer);
    if (RegQueryValueExA(hKey, "BIOSVersion", NULL, &valueType, (LPBYTE)buffer,
                         &bufferSize) == ERROR_SUCCESS) {
      version = buffer;
    }

    // Get BIOS Release Date
    bufferSize = sizeof(buffer);
    if (RegQueryValueExA(hKey, "BIOSReleaseDate", NULL, &valueType,
                         (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
      date = buffer;
    }

    // Get Manufacturer
    bufferSize = sizeof(buffer);
    if (RegQueryValueExA(hKey, "SystemManufacturer", NULL, &valueType,
                         (LPBYTE)buffer, &bufferSize) == ERROR_SUCCESS) {
      manufacturer = buffer;
    }

    RegCloseKey(hKey);
  }

  // Fall back to WMI if any values are still missing
  if (version == "no_data" || date == "no_data" || manufacturer == "no_data") {
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
      IWbemLocator* pLoc = nullptr;
      hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                            IID_IWbemLocator, (LPVOID*)&pLoc);

      if (SUCCEEDED(hr) && pLoc) {
        IWbemServices* pSvc = nullptr;
        hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, 0,
                                 NULL, 0, 0, &pSvc);

        if (SUCCEEDED(hr) && pSvc) {
          hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                                 NULL, RPC_C_AUTHN_LEVEL_CALL,
                                 RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

          if (SUCCEEDED(hr)) {
            IEnumWbemClassObject* pEnumerator = nullptr;
            hr = pSvc->ExecQuery(
              bstr_t("WQL"), bstr_t("SELECT * FROM Win32_BIOS"),
              WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
              &pEnumerator);

            if (SUCCEEDED(hr) && pEnumerator) {
              IWbemClassObject* pclsObj = nullptr;
              ULONG uReturn = 0;

              while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) ==
                     S_OK) {
                VARIANT vtProp;

                if (version == "no_data") {
                  VariantInit(&vtProp);
                  if (SUCCEEDED(
                        pclsObj->Get(L"SMBIOSBIOSVersion", 0, &vtProp, 0, 0))) {
                    if (vtProp.vt == VT_BSTR && vtProp.bstrVal != nullptr) {
                      version =
                        static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                    }
                    VariantClear(&vtProp);
                  }
                }

                if (date == "no_data") {
                  VariantInit(&vtProp);
                  if (SUCCEEDED(
                        pclsObj->Get(L"ReleaseDate", 0, &vtProp, 0, 0))) {
                    if (vtProp.vt == VT_BSTR && vtProp.bstrVal != nullptr) {
                      std::string dateString =
                        static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                      if (dateString.length() >= 8) {
                        std::string year = dateString.substr(0, 4);
                        std::string month = dateString.substr(4, 2);
                        std::string day = dateString.substr(6, 2);
                        date = month + "/" + day + "/" + year;
                      } else {
                        date = dateString;
                      }
                    }
                    VariantClear(&vtProp);
                  }
                }

                if (manufacturer == "no_data") {
                  VariantInit(&vtProp);
                  if (SUCCEEDED(
                        pclsObj->Get(L"Manufacturer", 0, &vtProp, 0, 0))) {
                    if (vtProp.vt == VT_BSTR && vtProp.bstrVal != nullptr) {
                      manufacturer =
                        static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                    }
                    VariantClear(&vtProp);
                  }
                }

                pclsObj->Release();
              }

              pEnumerator->Release();
            }
          }
          pSvc->Release();
        }
        pLoc->Release();
      }
      CoUninitialize();
    }
  }

  return {version, date, manufacturer};
}

std::vector<SystemWrapper::DriveInfo> SystemWrapper::getDriveInfo() {
  std::vector<DriveInfo> drives;
  std::map<std::string, DriveInfo> physicalDrives;
  std::map<std::string, std::string> driveToPartitionMap;
  std::map<std::string, std::string> logicalToPartitionMap;

  HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    hr = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
      hr = CoInitialize(NULL);
      if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LOG_ERROR << "Failed to initialize COM: 0x" << std::hex << hr
                  << std::dec;
        return drives;
      }
    }
  }

  bool comInitialized = (hr != RPC_E_CHANGED_MODE);

  IWbemLocator* pLoc = nullptr;
  hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                        IID_IWbemLocator, (LPVOID*)&pLoc);

  if (SUCCEEDED(hr) && pLoc) {
    IWbemServices* pSvc = nullptr;
    hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, 0, NULL,
                             0, 0, &pSvc);

    if (SUCCEEDED(hr) && pSvc) {
      hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                             RPC_C_AUTHN_LEVEL_CALL,
                             RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

      if (SUCCEEDED(hr)) {
        // Get physical disk drives
        IEnumWbemClassObject* pEnumerator = nullptr;
        hr = pSvc->ExecQuery(
          bstr_t("WQL"), bstr_t("SELECT * FROM Win32_DiskDrive"),
          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
          &pEnumerator);

        if (SUCCEEDED(hr) && pEnumerator) {
          IWbemClassObject* pclsObj = nullptr;
          ULONG uReturn = 0;

          while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) ==
                 S_OK) {
            VARIANT vtProp;
            std::string deviceID;
            DriveInfo driveInfo;

            VariantInit(&vtProp);
            if (SUCCEEDED(pclsObj->Get(L"DeviceID", 0, &vtProp, 0, 0))) {
              if (vtProp.vt == VT_BSTR) {
                deviceID = static_cast<const char*>(_bstr_t(vtProp.bstrVal));
              }
              VariantClear(&vtProp);
            }

            VariantInit(&vtProp);
            if (SUCCEEDED(pclsObj->Get(L"Model", 0, &vtProp, 0, 0))) {
              if (vtProp.vt == VT_BSTR) {
                driveInfo.model =
                  static_cast<const char*>(_bstr_t(vtProp.bstrVal));
              }
              VariantClear(&vtProp);
            }

            VariantInit(&vtProp);
            if (SUCCEEDED(pclsObj->Get(L"SerialNumber", 0, &vtProp, 0, 0))) {
              if (vtProp.vt == VT_BSTR) {
                driveInfo.serialNumber =
                  static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                driveInfo.serialNumber.erase(
                  0, driveInfo.serialNumber.find_first_not_of(" \t\n\r\f\v"));
                driveInfo.serialNumber.erase(
                  driveInfo.serialNumber.find_last_not_of(" \t\n\r\f\v") + 1);
              }
              VariantClear(&vtProp);
            }

            VariantInit(&vtProp);
            if (SUCCEEDED(pclsObj->Get(L"InterfaceType", 0, &vtProp, 0, 0))) {
              if (vtProp.vt == VT_BSTR) {
                driveInfo.interfaceType =
                  static_cast<const char*>(_bstr_t(vtProp.bstrVal));
              }
              VariantClear(&vtProp);
            }

            VariantInit(&vtProp);
            if (SUCCEEDED(pclsObj->Get(L"MediaType", 0, &vtProp, 0, 0))) {
              if (vtProp.vt == VT_BSTR) {
                std::string mediaType =
                  static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                driveInfo.isSSD =
                  (mediaType.find("SSD") != std::string::npos ||
                   driveInfo.model.find("SSD") != std::string::npos);

                if (driveInfo.model.find("NVMe") != std::string::npos ||
                    driveInfo.interfaceType.find("NVMe") != std::string::npos) {
                  driveInfo.isSSD = true;
                }
              }
              VariantClear(&vtProp);
            }

            if (!deviceID.empty()) {
              physicalDrives[deviceID] = driveInfo;
            }

            pclsObj->Release();
          }

          pEnumerator->Release();
        } else {
          LOG_ERROR << "Failed to execute physical disk query: 0x" << std::hex
                    << hr << std::dec;
        }

        // Get disk drive to partition mapping
        pEnumerator = nullptr;
        hr = pSvc->ExecQuery(
          bstr_t("WQL"), bstr_t("SELECT * FROM Win32_DiskDriveToDiskPartition"),
          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
          &pEnumerator);

        if (SUCCEEDED(hr) && pEnumerator) {
          IWbemClassObject* pclsObj = nullptr;
          ULONG uReturn = 0;

          while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) ==
                 S_OK) {
            VARIANT vtAntecedent, vtDependent;

            VariantInit(&vtAntecedent);
            VariantInit(&vtDependent);

            if (SUCCEEDED(
                  pclsObj->Get(L"Antecedent", 0, &vtAntecedent, 0, 0)) &&
                SUCCEEDED(pclsObj->Get(L"Dependent", 0, &vtDependent, 0, 0))) {

              if (vtAntecedent.vt == VT_BSTR && vtDependent.vt == VT_BSTR) {
                std::string driveRef =
                  static_cast<const char*>(_bstr_t(vtAntecedent.bstrVal));
                std::string partitionRef =
                  static_cast<const char*>(_bstr_t(vtDependent.bstrVal));

                size_t driveStart = driveRef.find("DeviceID=\"");
                size_t driveEnd = driveRef.rfind("\"");
                if (driveStart != std::string::npos &&
                    driveEnd != std::string::npos && driveStart < driveEnd) {
                  driveStart += 10;
                  std::string driveID =
                    driveRef.substr(driveStart, driveEnd - driveStart);

                  size_t partStart = partitionRef.find("DeviceID=\"");
                  size_t partEnd = partitionRef.rfind("\"");
                  if (partStart != std::string::npos &&
                      partEnd != std::string::npos && partStart < partEnd) {
                    partStart += 10;
                    std::string partID =
                      partitionRef.substr(partStart, partEnd - partStart);

                    size_t pos = 0;
                    while ((pos = driveID.find("\\\\", pos)) !=
                           std::string::npos) {
                      driveID.replace(pos, 2, "\\");
                      pos += 1;
                    }

                    pos = 0;
                    while ((pos = partID.find("\\\\", pos)) !=
                           std::string::npos) {
                      partID.replace(pos, 2, "\\");
                      pos += 1;
                    }

                    driveToPartitionMap[partID] = driveID;
                  }
                }
              }
            }

            VariantClear(&vtAntecedent);
            VariantClear(&vtDependent);
            pclsObj->Release();
          }

          pEnumerator->Release();
        } else {
          LOG_ERROR << "Failed to execute DiskDriveToDiskPartition query: 0x"
                    << std::hex << hr << std::dec;
        }

        // Get partition to logical disk mapping
        pEnumerator = nullptr;
        hr = pSvc->ExecQuery(
          bstr_t("WQL"), bstr_t("SELECT * FROM Win32_LogicalDiskToPartition"),
          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
          &pEnumerator);

        if (SUCCEEDED(hr) && pEnumerator) {
          IWbemClassObject* pclsObj = nullptr;
          ULONG uReturn = 0;

          while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) ==
                 S_OK) {
            VARIANT vtAntecedent, vtDependent;

            VariantInit(&vtAntecedent);
            VariantInit(&vtDependent);

            if (SUCCEEDED(
                  pclsObj->Get(L"Antecedent", 0, &vtAntecedent, 0, 0)) &&
                SUCCEEDED(pclsObj->Get(L"Dependent", 0, &vtDependent, 0, 0))) {

              if (vtAntecedent.vt == VT_BSTR && vtDependent.vt == VT_BSTR) {
                std::string partitionRef =
                  static_cast<const char*>(_bstr_t(vtAntecedent.bstrVal));
                std::string logicalRef =
                  static_cast<const char*>(_bstr_t(vtDependent.bstrVal));

                size_t partStart = partitionRef.find("DeviceID=\"");
                size_t partEnd = partitionRef.rfind("\"");
                if (partStart != std::string::npos &&
                    partEnd != std::string::npos && partStart < partEnd) {
                  partStart += 10;
                  std::string partID =
                    partitionRef.substr(partStart, partEnd - partStart);

                  size_t logStart = logicalRef.find("DeviceID=\"");
                  size_t logEnd = logicalRef.rfind("\"");
                  if (logStart != std::string::npos &&
                      logEnd != std::string::npos && logStart < logEnd) {
                    logStart += 10;
                    std::string logID =
                      logicalRef.substr(logStart, logEnd - logStart);

                    size_t pos = 0;
                    while ((pos = partID.find("\\\\", pos)) !=
                           std::string::npos) {
                      partID.replace(pos, 2, "\\");
                      pos += 1;
                    }

                    logicalToPartitionMap[logID] = partID;
                  }
                }
              }
            }

            VariantClear(&vtAntecedent);
            VariantClear(&vtDependent);
            pclsObj->Release();
          }

          pEnumerator->Release();
        } else {
          LOG_ERROR << "Failed to execute LogicalDiskToPartition query: 0x"
                    << std::hex << hr << std::dec;
        }

        // Get logical drives directly
        IEnumWbemClassObject* pLogicalEnum = nullptr;
        hr = pSvc->ExecQuery(
          bstr_t("WQL"),
          bstr_t("SELECT * FROM Win32_LogicalDisk WHERE DriveType=3"),
          WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
          &pLogicalEnum);

        if (SUCCEEDED(hr) && pLogicalEnum) {
          IWbemClassObject* pLogicalObj = nullptr;
          ULONG uReturn = 0;

          while (pLogicalEnum->Next(WBEM_INFINITE, 1, &pLogicalObj, &uReturn) ==
                 S_OK) {
            VARIANT vtProp;
            DriveInfo driveInfo;
            std::string deviceID;

            VariantInit(&vtProp);
            if (SUCCEEDED(pLogicalObj->Get(L"DeviceID", 0, &vtProp, 0, 0))) {
              if (vtProp.vt == VT_BSTR) {
                deviceID = static_cast<const char*>(_bstr_t(vtProp.bstrVal));
                driveInfo.path = deviceID;
              }
              VariantClear(&vtProp);
            }

            VariantInit(&vtProp);
            if (SUCCEEDED(pLogicalObj->Get(L"FreeSpace", 0, &vtProp, 0, 0))) {
              if (vtProp.vt == VT_BSTR) {
                try {
                  ULONGLONG freeSpace = _wtoi64((wchar_t*)vtProp.bstrVal);
                  driveInfo.freeSpaceGB =
                    static_cast<int64_t>(freeSpace / (1024 * 1024 * 1024));
                } catch (...) {
                  driveInfo.freeSpaceGB = -1;
                }
              }
              VariantClear(&vtProp);
            }

            VariantInit(&vtProp);
            if (SUCCEEDED(pLogicalObj->Get(L"Size", 0, &vtProp, 0, 0))) {
              if (vtProp.vt == VT_BSTR) {
                try {
                  ULONGLONG totalSpace = _wtoi64((wchar_t*)vtProp.bstrVal);
                  driveInfo.totalSpaceGB =
                    static_cast<int64_t>(totalSpace / (1024 * 1024 * 1024));
                } catch (...) {
                  driveInfo.totalSpaceGB = -1;
                }
              }
              VariantClear(&vtProp);
            }

            char systemDir[MAX_PATH];
            GetSystemDirectoryA(systemDir, MAX_PATH);
            driveInfo.isSystemDrive =
              (toupper(systemDir[0]) == toupper(driveInfo.path[0]));

            bool foundPhysicalDrive = false;

            if (logicalToPartitionMap.find(deviceID) !=
                logicalToPartitionMap.end()) {
              std::string partID = logicalToPartitionMap[deviceID];

              if (driveToPartitionMap.find(partID) !=
                  driveToPartitionMap.end()) {
                std::string driveID = driveToPartitionMap[partID];

                if (physicalDrives.find(driveID) != physicalDrives.end()) {
                  DriveInfo& physDrive = physicalDrives[driveID];

                  driveInfo.model = physDrive.model;
                  driveInfo.serialNumber = physDrive.serialNumber;
                  driveInfo.interfaceType = physDrive.interfaceType;
                  driveInfo.isSSD = physDrive.isSSD;

                  foundPhysicalDrive = true;
                }
              }
            }

            if (!foundPhysicalDrive && !physicalDrives.empty()) {
              auto firstDrive = physicalDrives.begin();
              driveInfo.model = firstDrive->second.model;
              driveInfo.serialNumber = firstDrive->second.serialNumber;
              driveInfo.interfaceType = firstDrive->second.interfaceType;
              driveInfo.isSSD = firstDrive->second.isSSD;
            }

            drives.push_back(driveInfo);
            pLogicalObj->Release();
          }

          pLogicalEnum->Release();
        } else {
          LOG_ERROR << "Failed to execute logical disk query: 0x" << std::hex
                    << hr << std::dec;
        }
      } else {
        LOG_ERROR << "Failed to set proxy blanket: 0x" << std::hex << hr
                  << std::dec;
      }
      pSvc->Release();
    } else {
      LOG_ERROR << "Failed to connect to WMI service: 0x" << std::hex << hr
                << std::dec;
    }
    pLoc->Release();
  } else {
    LOG_ERROR << "Failed to create WbemLocator: 0x" << std::hex << hr
              << std::dec;
  }

  if (comInitialized) {
    CoUninitialize();
  }

  return drives;
}

std::string SystemWrapper::getPowerPlan() {
  // Force decimal output
  // No longer needed with LOG_* macros
  std::string powerPlan = "unknown";
  LOG_DEBUG << "SystemWrapper: Getting power plan...";

  GUID* pActivePolicy = nullptr;
  if (PowerGetActiveScheme(NULL, &pActivePolicy) == ERROR_SUCCESS) {
    // Get the friendly name of the power plan
    DWORD nameSize = 0;
    PowerReadFriendlyName(NULL, pActivePolicy, NULL, NULL, NULL, &nameSize);

    if (nameSize > 0) {
      std::vector<wchar_t> nameBuf(nameSize / sizeof(wchar_t));
      if (PowerReadFriendlyName(NULL, pActivePolicy, NULL, NULL,
                                (UCHAR*)nameBuf.data(),
                                &nameSize) == ERROR_SUCCESS) {
        int len = WideCharToMultiByte(CP_UTF8, 0, nameBuf.data(), -1, nullptr,
                                      0, nullptr, nullptr);
        if (len > 0) {
          std::string name(len, 0);
          WideCharToMultiByte(CP_UTF8, 0, nameBuf.data(), -1, &name[0], len,
                              nullptr, nullptr);
          powerPlan =
            name.c_str();  // Convert to string and remove null terminator
          LOG_DEBUG << "  Found power plan: [power plan name hidden for privacy]";
        }
      }
    }
    LocalFree(pActivePolicy);
  }

  return powerPlan;
}

bool SystemWrapper::isHighPerformancePowerPlan() {
  std::string currentPlan = getPowerPlan();

  // Check if the current plan is high performance - case insensitive comparison
  bool isHighPerf = false;

  // Convert to lowercase for case-insensitive comparison
  std::string lowerPlan = currentPlan;
  for (char& c : lowerPlan) {
    c = std::tolower(c);
  }

  if (lowerPlan.find("high performance") != std::string::npos ||
      lowerPlan.find("ultimate performance") != std::string::npos ||
      lowerPlan.find("performance") != std::string::npos) {
    isHighPerf = true;
  }

  // Also check GUID directly
  GUID* pActivePolicy = nullptr;
  if (PowerGetActiveScheme(NULL, &pActivePolicy) == ERROR_SUCCESS) {
    // High Performance GUID {8c5e7fda-e8bf-4a96-9a85-a6e23a6b831e}
    GUID highPerfGuid = {0x8c5e7fda,
                         0xe8bf,
                         0x4a96,
                         {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x6b, 0x83, 0x1e}};
    // Ultimate Performance GUID {e9a42b02-d5df-448d-aa00-03f14749eb61}
    GUID ultimatePerfGuid = {0xe9a42b02,
                             0xd5df,
                             0x448d,
                             {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};

    if (memcmp(pActivePolicy, &highPerfGuid, sizeof(GUID)) == 0 ||
        memcmp(pActivePolicy, &ultimatePerfGuid, sizeof(GUID)) == 0) {
      isHighPerf = true;
    }

    LocalFree(pActivePolicy);
  }

  LOG_INFO << "  Is high performance power plan: "
            << (isHighPerf ? "Yes" : "No");
  return isHighPerf;
}

bool SystemWrapper::isGameModeEnabled() {
  // Force decimal output
  // No longer needed with LOG_* macros
  bool gameMode = false;
  LOG_DEBUG << "SystemWrapper: Checking game mode status...";

  HKEY hKey;
  DWORD gameModeValue = 0;
  DWORD dataSize = sizeof(DWORD);
  bool valueRead = false;

  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\GameBar", 0,
                    KEY_READ, &hKey) == ERROR_SUCCESS) {
    if (RegQueryValueExW(hKey, L"AutoGameModeEnabled", 0, NULL,
                         (LPBYTE)&gameModeValue,
                         &dataSize) == ERROR_SUCCESS) {
      valueRead = true;
    } else if (RegQueryValueExW(hKey, L"AutoGameMode", 0, NULL,
                                (LPBYTE)&gameModeValue,
                                &dataSize) == ERROR_SUCCESS) {
      valueRead = true;
    }

    if (valueRead) {
      gameMode = (gameModeValue == 1);
      LOG_INFO << "  Game mode is " << (gameMode ? "enabled" : "disabled")
               ;
    } else {
      LOG_WARN << "  Failed to read AutoGameMode value, assuming disabled"
               ;
    }
    RegCloseKey(hKey);
  } else {
    LOG_WARN
      << "  Failed to open GameBar registry key, assuming game mode is disabled";
  }

  return gameMode;
}

SystemWrapper::PageFileInfo SystemWrapper::getPageFileInfo() {
  PageFileInfo info;
  LOG_DEBUG << "SystemWrapper: Getting page file info...";
  bool systemManagedKnown = false;

  try {
    // First check if page file exists based on memory status
    MEMORYSTATUSEX memoryStatus = {sizeof(MEMORYSTATUSEX)};
    if (GlobalMemoryStatusEx(&memoryStatus)) {
      if (memoryStatus.ullTotalPageFile > memoryStatus.ullTotalPhys) {
        info.exists = true;
        info.totalSizeMB =
          (memoryStatus.ullTotalPageFile - memoryStatus.ullTotalPhys) /
          (1024.0 * 1024.0);
        LOG_INFO << "  Page file exists, total size: " << info.totalSizeMB
                  << " MB";
      }
    }

    // Check for AutomaticManagedPagefile in Win32_ComputerSystem
    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    bool comInitialized = SUCCEEDED(hr);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
      IWbemLocator* pLoc = nullptr;
      hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                            IID_IWbemLocator, (LPVOID*)&pLoc);

      if (SUCCEEDED(hr) && pLoc) {
        IWbemServices* pSvc = nullptr;
        hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL,
                                 0, 0, &pSvc);

        if (SUCCEEDED(hr) && pSvc) {
          hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                                 NULL, RPC_C_AUTHN_LEVEL_CALL,
                                 RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

          if (SUCCEEDED(hr)) {
            // Query for AutomaticManagedPagefile
            IEnumWbemClassObject* pEnumerator = nullptr;
            hr = pSvc->ExecQuery(
              bstr_t("WQL"),
              bstr_t(
                "SELECT AutomaticManagedPagefile FROM Win32_ComputerSystem"),
              WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
              &pEnumerator);

            if (SUCCEEDED(hr) && pEnumerator) {
              IWbemClassObject* pclsObj = nullptr;
              ULONG uReturn = 0;

              if (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) ==
                  S_OK) {
                VARIANT vtProp;
                VariantInit(&vtProp);

                if (SUCCEEDED(pclsObj->Get(L"AutomaticManagedPagefile", 0,
                                           &vtProp, 0, 0))) {
                  if (vtProp.vt == VT_BOOL) {
                    info.systemManaged = (vtProp.boolVal == VARIANT_TRUE);
                    systemManagedKnown = true;
                    LOG_DEBUG
                      << "  Page file is "
                      << (info.systemManaged ? "system-managed (from WMI)"
                                             : "manually configured (from WMI)")
                     ;
                  }
                  VariantClear(&vtProp);
                }

                pclsObj->Release();
              }

              pEnumerator->Release();
            }
          }

          pSvc->Release();
        }

        pLoc->Release();
      }

      if (comInitialized) {
        CoUninitialize();
      }
    }

    // Use WMI to get detailed page file information (keep existing code)
    hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    comInitialized = SUCCEEDED(hr);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
      IWbemLocator* pLoc = nullptr;
      hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                            IID_IWbemLocator, (LPVOID*)&pLoc);

      if (SUCCEEDED(hr) && pLoc) {
        IWbemServices* pSvc = nullptr;
        hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL,
                                 0, 0, &pSvc);

        if (SUCCEEDED(hr) && pSvc) {
          hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                                 NULL, RPC_C_AUTHN_LEVEL_CALL,
                                 RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

          if (SUCCEEDED(hr)) {
            // Query page file usage
            IEnumWbemClassObject* pEnumerator = nullptr;
            hr = pSvc->ExecQuery(
              bstr_t("WQL"), bstr_t("SELECT * FROM Win32_PageFileUsage"),
              WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
              &pEnumerator);

            if (SUCCEEDED(hr) && pEnumerator) {
              IWbemClassObject* pclsObj = nullptr;
              ULONG uReturn = 0;

              while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) ==
                     S_OK) {
                info.exists = true;
                VARIANT vtProp;

                // Get the page file name
                VariantInit(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"Name", 0, &vtProp, 0, 0))) {
                  if (vtProp.vt == VT_BSTR && vtProp.bstrVal != nullptr) {
                    // Convert BSTR to std::string properly
                    char name[MAX_PATH] = {0};
                    WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, name,
                                        MAX_PATH, NULL, NULL);

                    // Find the colon position safely
                    char* colonPtr = strchr(name, ':');
                    if (colonPtr != nullptr) {
                      size_t colonPos = colonPtr - name;
                      if (colonPos > 0) {
                        std::string driveLetter =
                          std::string(1, name[colonPos - 1]) + ":";
                        info.locations.push_back(driveLetter);

                        // Store primary drive letter if it's the first one
                        if (info.primaryDriveLetter.empty()) {
                          info.primaryDriveLetter = driveLetter;
                        }
                        LOG_DEBUG << "  Page file location: [drive letter hidden for privacy]"
                                 ;
                      } else {
                        info.locations.push_back(name);
                        LOG_DEBUG << "  Page file location: [path hidden for privacy]"
                                 ;
                      }
                    } else {
                      info.locations.push_back(name);
                      LOG_DEBUG << "  Page file location: [path hidden for privacy]"
                               ;
                    }
                  }
                  VariantClear(&vtProp);
                }

                // Get current size
                VariantInit(&vtProp);
                if (SUCCEEDED(
                      pclsObj->Get(L"CurrentUsage", 0, &vtProp, 0, 0))) {
                  info.currentSizesMB.push_back(vtProp.intVal);
                  LOG_DEBUG << "  Current usage: " << vtProp.intVal << " MB"
                           ;
                  VariantClear(&vtProp);
                } else {
                  info.currentSizesMB.push_back(0);
                }

                // Get peak size
                VariantInit(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"PeakUsage", 0, &vtProp, 0, 0))) {
                  info.maxSizesMB.push_back(vtProp.intVal);
                  LOG_DEBUG << "  Peak usage: " << vtProp.intVal << " MB"
                           ;
                  VariantClear(&vtProp);
                } else {
                  info.maxSizesMB.push_back(0);
                }

                pclsObj->Release();
              }

              pEnumerator->Release();
            }

            // Query for page file settings
            pEnumerator = nullptr;
            hr = pSvc->ExecQuery(
              bstr_t("WQL"), bstr_t("SELECT * FROM Win32_PageFileSetting"),
              WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL,
              &pEnumerator);

            if (SUCCEEDED(hr) && pEnumerator) {
              IWbemClassObject* pclsObj = nullptr;
              ULONG uReturn = 0;

              if (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) !=
                  S_OK) {
                // No specific settings found, which can indicate system-managed
                if (!systemManagedKnown) {
                  info.systemManaged = true;
                }
                LOG_DEBUG << "  Page file is system-managed (no specific "
                             "settings found)"
                         ;
              } else {
                if (!systemManagedKnown) {
                  info.systemManaged = false;
                }
                LOG_DEBUG << "  Page file has custom configuration"
                         ;

                info.exists = true;

                // Try to get name from this object as well
                VARIANT vtProp;
                VariantInit(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"Name", 0, &vtProp, 0, 0))) {
                  if (vtProp.vt == VT_BSTR && vtProp.bstrVal != nullptr) {
                    // Convert BSTR to std::string properly
                    char name[MAX_PATH] = {0};
                    WideCharToMultiByte(CP_UTF8, 0, vtProp.bstrVal, -1, name,
                                        MAX_PATH, NULL, NULL);

                    // Extract drive letter if not already in locations
                    char* colonPtr = strchr(name, ':');
                    if (colonPtr != nullptr) {
                      size_t colonPos = colonPtr - name;
                      if (colonPos > 0) {
                        std::string driveLetter =
                          std::string(1, name[colonPos - 1]) + ":";

                        // Check if this location is already in our list
                        auto it = std::find(info.locations.begin(),
                                            info.locations.end(), driveLetter);
                        if (it == info.locations.end()) {
                          info.locations.push_back(driveLetter);
                          LOG_DEBUG << "  Additional page file location: [drive letter hidden for privacy]"
                                   ;

                          // Set primary drive letter if it's still empty
                          if (info.primaryDriveLetter.empty()) {
                            info.primaryDriveLetter = driveLetter;
                          }
                        }
                      }
                    }
                  }
                  VariantClear(&vtProp);
                }

                pclsObj->Release();
              }

              pEnumerator->Release();
            }
          }

          pSvc->Release();
        }

        pLoc->Release();
      }

      if (comInitialized) {
        CoUninitialize();
      }
    }

    // As a fallback, try to detect using registry
    if (info.locations.empty()) {
      // Try to get pagefile location from registry
      HKEY hKey;
      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                        "SYSTEM\\CurrentControlSet\\Control\\Session "
                        "Manager\\Memory Management",
                        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD bufferSize = 0;
        DWORD type = 0;

        if (RegQueryValueExA(hKey, "PagingFiles", NULL, &type, nullptr,
                             &bufferSize) == ERROR_SUCCESS &&
            bufferSize > 0) {
          std::vector<char> buffer(bufferSize + 2, 0);
          if (RegQueryValueExA(hKey, "PagingFiles", NULL, &type,
                               reinterpret_cast<LPBYTE>(buffer.data()),
                               &bufferSize) == ERROR_SUCCESS) {
            const char* ptr = buffer.data();
            while (*ptr) {
              std::string entry = ptr;
              size_t colonPos = entry.find(':');
              if (colonPos != std::string::npos && colonPos > 0) {
                std::string driveLetter = entry.substr(colonPos - 1, 2);
                auto it =
                  std::find(info.locations.begin(), info.locations.end(),
                            driveLetter);
                if (it == info.locations.end()) {
                  info.locations.push_back(driveLetter);
                  info.exists = true;
                  if (info.primaryDriveLetter.empty()) {
                    info.primaryDriveLetter = driveLetter;
                  }
                  LOG_DEBUG << "  Registry page file location: [drive letter hidden for privacy]"
                           ;
                }
              }

              ptr += entry.size() + 1;
            }
          }
        }
        RegCloseKey(hKey);
      }
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception in getPageFileInfo(): " << e.what();
  }

  return info;
}

// Add this implementation after the existing methods

std::vector<SystemWrapper::MonitorInfo> SystemWrapper::getMonitorInfo() {
  std::vector<MonitorInfo> monitors;

  DISPLAY_DEVICEA displayDevice;
  DEVMODEA deviceMode;

  ZeroMemory(&displayDevice, sizeof(displayDevice));
  displayDevice.cb = sizeof(displayDevice);

  ZeroMemory(&deviceMode, sizeof(deviceMode));
  deviceMode.dmSize = sizeof(deviceMode);

  DWORD deviceIndex = 0;
  while (EnumDisplayDevicesA(NULL, deviceIndex, &displayDevice, 0)) {
    MonitorInfo monitor;
    monitor.deviceName = displayDevice.DeviceName;
    monitor.displayName = displayDevice.DeviceString;
    monitor.isPrimary =
      (displayDevice.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;

    if (EnumDisplaySettingsA(displayDevice.DeviceName, ENUM_CURRENT_SETTINGS,
                             &deviceMode)) {
      monitor.width = deviceMode.dmPelsWidth;
      monitor.height = deviceMode.dmPelsHeight;
      monitor.refreshRate = deviceMode.dmDisplayFrequency;
    }

    monitors.push_back(monitor);
    deviceIndex++;

    ZeroMemory(&displayDevice, sizeof(displayDevice));
    displayDevice.cb = sizeof(displayDevice);
  }

  if (monitors.empty()) {
    MonitorInfo monitor;
    monitor.deviceName = "Primary Display";
    monitor.isPrimary = true;

    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &deviceMode)) {
      monitor.width = deviceMode.dmPelsWidth;
      monitor.height = deviceMode.dmPelsHeight;
      monitor.refreshRate = deviceMode.dmDisplayFrequency;

      monitors.push_back(monitor);
    }
  }

  return monitors;
}

static bool isValidDriverDate(const std::string& dateStr) {
  if (dateStr == "6-21-2006" || dateStr.length() < 8) {
    return false;
  }

  return dateStr.find('-') != std::string::npos ||
         dateStr.find('/') != std::string::npos;
}

std::vector<SystemWrapper::DriverInfo> SystemWrapper::getDriverInfo(
  const std::string& deviceClass) {
  std::vector<DriverInfo> drivers;

  const GUID* guidDevClass = NULL;

  if (!deviceClass.empty()) {
    if (deviceClass == "System") {
      guidDevClass = &GUID_DEVCLASS_SYSTEM;
    } else if (deviceClass == "Sound") {
      guidDevClass = &GUID_DEVCLASS_MEDIA;
    } else if (deviceClass == "Net") {
      guidDevClass = &GUID_DEVCLASS_NET;
    }
  }

  HDEVINFO hDevInfo =
    SetupDiGetClassDevsA(guidDevClass, NULL, NULL,
                         DIGCF_PRESENT | (guidDevClass ? 0 : DIGCF_ALLCLASSES));

  if (hDevInfo == INVALID_HANDLE_VALUE) {
    LOG_ERROR << "Failed to get device information set. Error code: "
              << GetLastError();
    return drivers;
  }

  SP_DEVINFO_DATA devInfoData;
  devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

  for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
    DriverInfo driver;

    // Get device friendly name
    char deviceName[256] = {0};
    BOOL hasName = SetupDiGetDeviceRegistryPropertyA(
      hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)deviceName,
      sizeof(deviceName), NULL);

    if (!hasName) {
      hasName = SetupDiGetDeviceRegistryPropertyA(
        hDevInfo, &devInfoData, SPDRP_DEVICEDESC, NULL, (PBYTE)deviceName,
        sizeof(deviceName), NULL);
    }

    if (hasName) {
      driver.deviceName = deviceName;
    } else {
      continue;  // Skip devices without names
    }

    // Get device class if we're not filtering already
    if (guidDevClass == NULL && !deviceClass.empty()) {
      char deviceClassStr[256] = {0};
      if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_CLASS,
                                            NULL, (PBYTE)deviceClassStr,
                                            sizeof(deviceClassStr), NULL)) {

        // If we're looking for a specific class and this isn't it, skip
        if (std::string(deviceClassStr) != deviceClass) {
          continue;
        }
      }
    }

    // Get driver provider name
    char providerName[256] = {0};
    if (SetupDiGetDeviceRegistryPropertyA(
          hDevInfo, &devInfoData, SPDRP_PROVIDERNAME, NULL, (PBYTE)providerName,
          sizeof(providerName), NULL)) {
      driver.providerName = providerName;
    }

    // Get driver version from driver key
    HKEY hDeviceKey = SetupDiOpenDevRegKey(
      hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);

    if (hDeviceKey != INVALID_HANDLE_VALUE) {
      char driverVersion[256] = {0};
      DWORD size = sizeof(driverVersion);
      DWORD type = 0;

      if (RegQueryValueExA(hDeviceKey, "DriverVersion", NULL, &type,
                           (LPBYTE)driverVersion, &size) == ERROR_SUCCESS) {
        driver.driverVersion = driverVersion;
      }

      RegCloseKey(hDeviceKey);
    }

    // Try DRIVER_DATE property first
    DWORD dataType = 0;
    DWORD dataSize = 0;
    BOOL gotDate = FALSE;

    // First query to get the size and type
    SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_DRIVER_DATE,
                                      &dataType, NULL, 0, &dataSize);

    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && dataSize > 0) {
      std::vector<BYTE> dataBuf(dataSize);
      if (SetupDiGetDeviceRegistryPropertyA(
            hDevInfo, &devInfoData, SPDRP_DRIVER_DATE, &dataType,
            dataBuf.data(), dataSize, &dataSize)) {

        if (dataType == REG_SZ || dataType == REG_MULTI_SZ) {
          // Handle string date format (MM-DD-YYYY)
          char dateStr[64] = {0};
          strncpy_s(
            dateStr, sizeof(dateStr), (const char*)dataBuf.data(),
            std::min(sizeof(dateStr) - 1, static_cast<size_t>(dataSize)));

          if (strlen(dateStr) > 0) {
            driver.driverDate = dateStr;
            driver.isDateValid = true;
            gotDate = TRUE;
          }
        } else if (dataType == REG_BINARY && dataSize >= sizeof(FILETIME)) {
          // Handle binary FILETIME
          FILETIME fileTime;
          SYSTEMTIME systemTime;
          memcpy(&fileTime, dataBuf.data(), sizeof(FILETIME));
          if (FileTimeToSystemTime(&fileTime, &systemTime)) {
            char dateStr[64];
            sprintf_s(dateStr, "%02d-%02d-%04d", systemTime.wMonth,
                      systemTime.wDay, systemTime.wYear);
            driver.driverDate = dateStr;
            driver.isDateValid = true;
            gotDate = TRUE;
          }
        }
      }
    }

    // Fall back to registry lookup if needed
    if (!gotDate || !driver.isDateValid) {
      char deviceInstanceId[MAX_PATH] = {0};
      if (CM_Get_Device_IDA(devInfoData.DevInst, deviceInstanceId, MAX_PATH,
                            0) == CR_SUCCESS) {
        // Try DriverDatabase first
        std::string deviceIdStr(deviceInstanceId);
        size_t lastBackslash = deviceIdStr.find_last_of('\\');
        if (lastBackslash != std::string::npos) {
          std::string driverId = deviceIdStr.substr(lastBackslash + 1);
          std::string fullKeyPath =
            "SYSTEM\\CurrentControlSet\\DriverDatabase\\DriverInformation\\" +
            driverId;

          HKEY hDriverKey;
          if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, fullKeyPath.c_str(), 0,
                            KEY_READ, &hDriverKey) == ERROR_SUCCESS) {
            char dateStr[64] = {0};
            DWORD dateSize = sizeof(dateStr);
            DWORD type = 0;

            if (RegQueryValueExA(hDriverKey, "DriverDate", NULL, &type,
                                 (LPBYTE)dateStr, &dateSize) == ERROR_SUCCESS) {
              driver.driverDate = dateStr;
              driver.isDateValid = true;
              gotDate = TRUE;
            }
            RegCloseKey(hDriverKey);
          }
        }

        // If still no date, try class registry path
        if (!gotDate) {
          char classGuid[64] = {0};
          if (SetupDiGetDeviceRegistryPropertyA(
                hDevInfo, &devInfoData, SPDRP_CLASSGUID, NULL, (PBYTE)classGuid,
                sizeof(classGuid), NULL)) {

            std::string controlClassPath =
              "SYSTEM\\CurrentControlSet\\Control\\Class\\" +
              std::string(classGuid);

            // Try driver key
            HKEY hDriverKey = SetupDiOpenDevRegKey(
              hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
            if (hDriverKey != INVALID_HANDLE_VALUE) {
              char dateStr[64] = {0};
              DWORD dateSize = sizeof(dateStr);
              DWORD type = 0;

              if (RegQueryValueExA(hDriverKey, "DriverDate", NULL, &type,
                                   (LPBYTE)dateStr,
                                   &dateSize) == ERROR_SUCCESS) {
                driver.driverDate = dateStr;
                driver.isDateValid = true;
                gotDate = TRUE;
              } else {
                dateSize = sizeof(dateStr);
                if (RegQueryValueExA(hDriverKey, "InstallDate", NULL, &type,
                                     (LPBYTE)dateStr,
                                     &dateSize) == ERROR_SUCCESS) {
                  driver.driverDate = dateStr;
                  driver.isDateValid = true;
                  gotDate = TRUE;
                }
              }
              RegCloseKey(hDriverKey);
            }

            // Try class registry if still no date
            if (!gotDate) {
              HKEY hClassKey;
              if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, controlClassPath.c_str(), 0,
                                KEY_READ, &hClassKey) == ERROR_SUCCESS) {
                char subKeyName[256];
                DWORD subKeyIndex = 0;
                DWORD subKeySize = sizeof(subKeyName);

                while (RegEnumKeyExA(hClassKey, subKeyIndex++, subKeyName,
                                     &subKeySize, NULL, NULL, NULL,
                                     NULL) == ERROR_SUCCESS) {
                  HKEY hDriverSubKey;
                  std::string subKeyPath = controlClassPath + "\\" + subKeyName;

                  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKeyPath.c_str(), 0,
                                    KEY_READ,
                                    &hDriverSubKey) == ERROR_SUCCESS) {
                    char dateStr[64] = {0};
                    DWORD dateSize = sizeof(dateStr);
                    DWORD type = 0;

                    if (RegQueryValueExA(hDriverSubKey, "DriverDate", NULL,
                                         &type, (LPBYTE)dateStr,
                                         &dateSize) == ERROR_SUCCESS) {
                      if (isValidDriverDate(dateStr)) {
                        driver.driverDate = dateStr;
                        driver.isDateValid = true;
                        gotDate = TRUE;
                        RegCloseKey(hDriverSubKey);
                        break;
                      }
                    }
                    RegCloseKey(hDriverSubKey);
                  }
                  subKeySize = sizeof(subKeyName);
                }
                RegCloseKey(hClassKey);
              }
            }
          }
        }
      }
    }

    drivers.push_back(driver);
  }

  SetupDiDestroyDeviceInfoList(hDevInfo);

  return drivers;
}

std::vector<SystemWrapper::DriverInfo> SystemWrapper::getChipsetDriverDetails() {
  std::vector<DriverInfo> chipsetDrivers = getDriverInfo("System");
  std::vector<DriverInfo> filteredDrivers;

  // Check registry directly for AMD/Intel chipset driver info
  auto [driverInstalled, driverVersionStr] = getChipsetDriverInfo();
  if (driverInstalled) {
    // Create a driver entry based on registry information
    DriverInfo registryDriver;

    if (driverVersionStr.find("AMD") != std::string::npos) {
      registryDriver.deviceName = "AMD Chipset Driver";
      registryDriver.providerName = "Advanced Micro Devices, Inc.";
    } else if (driverVersionStr.find("Intel") != std::string::npos) {
      registryDriver.deviceName = "Intel Chipset Driver";
      registryDriver.providerName = "Intel Corporation";
    } else if (driverVersionStr.find("NVIDIA") != std::string::npos) {
      registryDriver.deviceName = "NVIDIA Chipset Driver";
      registryDriver.providerName = "NVIDIA Corporation";
    } else {
      registryDriver.deviceName = "Chipset Driver";
      registryDriver.providerName = "Unknown";
    }

    // Extract version from string
    size_t versionPos = driverVersionStr.find_last_of(" ");
    if (versionPos != std::string::npos &&
        versionPos < driverVersionStr.length() - 1) {
      registryDriver.driverVersion = driverVersionStr.substr(versionPos + 1);
    } else {
      registryDriver.driverVersion = driverVersionStr;
    }

    // Look for driver date in registry
    HKEY hKey;
    bool foundDate = false;
    const char* regPaths[] = {
      "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
      "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"};

    for (const char* basePath : regPaths) {
      if (foundDate) break;

      if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, basePath, 0, KEY_READ, &hKey) ==
          ERROR_SUCCESS) {
        DWORD index = 0;
        char subKeyName[256];
        DWORD subKeySize = sizeof(subKeyName);

        while (RegEnumKeyExA(hKey, index++, subKeyName, &subKeySize, NULL, NULL,
                             NULL, NULL) == ERROR_SUCCESS) {
          std::string fullSubKey = std::string(basePath) + "\\" + subKeyName;
          HKEY subKey;

          if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, fullSubKey.c_str(), 0, KEY_READ,
                            &subKey) == ERROR_SUCCESS) {
            char displayName[512] = {0};
            DWORD nameSize = sizeof(displayName);

            if (RegQueryValueExA(subKey, "DisplayName", NULL, NULL,
                                 (LPBYTE)displayName,
                                 &nameSize) == ERROR_SUCCESS) {
              std::string name = displayName;
              bool isChipsetEntry =
                (name.find("AMD Chipset") != std::string::npos) ||
                (name.find("Intel(R) Chipset") != std::string::npos) ||
                (name.find("NVIDIA nForce") != std::string::npos);

              if (isChipsetEntry) {
                char installDate[20] = {0};
                DWORD dateSize = sizeof(installDate);

                if (RegQueryValueExA(subKey, "InstallDate", NULL, NULL,
                                     (LPBYTE)installDate,
                                     &dateSize) == ERROR_SUCCESS) {
                  std::string dateStr = installDate;
                  if (dateStr.length() == 8) {
                    std::string year = dateStr.substr(0, 4);
                    std::string month = dateStr.substr(4, 2);
                    std::string day = dateStr.substr(6, 2);
                    registryDriver.driverDate = month + "/" + day + "/" + year;
                    registryDriver.isDateValid = true;
                    foundDate = true;
                  }
                }
              }
            }
            RegCloseKey(subKey);
          }

          if (foundDate) break;
          subKeySize = sizeof(subKeyName);
        }
        RegCloseKey(hKey);
      }
    }

    filteredDrivers.push_back(registryDriver);
  }

  // AMD chipset component drivers to look for specifically
  const std::vector<std::string> amdChipsetComponents = {
    "AMD I2C", "AMD GPIO", "AMD SMBus", "AMD PSP", "AMD Ryzen Power Plan",
    "AMD PCI"};

  // Intel chipset component drivers to look for specifically
  const std::vector<std::string> intelChipsetComponents = {
    // LPSS / Serial IO
    "LPSS: I2C Controller", "LPSS: SPI", "LPSS: UART", "Serial IO I2C",

    // PCH bus controllers
    "SMBus Controller", "SPI (flash) Controller", "LPC Controller", "P2SB",
    "PMC", "PCI Express Root Port", "Shared SRAM",

    // Management & security
    "Management Engine Interface", "Platform Trust Technology",

    // Power & thermal
    "Dynamic Platform and Thermal Framework",

    // Storage / RAID
    "RST Premium Controller", "RST VMD Controller"};

  // Filter drivers from device manager with more stringent criteria
  std::vector<DriverInfo> amdComponentDrivers;
  std::vector<DriverInfo> intelComponentDrivers;

  // First check system devices
  for (const auto& driver : chipsetDrivers) {
    bool isChipsetDriver = false;

    // Skip generic Microsoft/Windows system drivers
    if (driver.providerName == "(Standard system devices)" ||
        driver.providerName.find("Microsoft") != std::string::npos) {
      continue;
    }

    // Look for specific chipset keywords
    if (driver.deviceName.find("Chipset") != std::string::npos ||
        driver.deviceName.find("Platform Controller Hub") !=
          std::string::npos ||
        driver.deviceName.find("PCH") != std::string::npos ||
        driver.deviceName.find("Root Complex") != std::string::npos ||
        driver.deviceName.find("Management Engine") != std::string::npos) {
      isChipsetDriver = true;
    }

    // Check for specific AMD chipset component drivers
    for (const auto& component : amdChipsetComponents) {
      if (driver.deviceName.find(component) != std::string::npos) {
        isChipsetDriver = true;

        // Store AMD component drivers separately
        if (driver.isDateValid) {
          amdComponentDrivers.push_back(driver);
        }
        break;
      }
    }

    // Check for specific Intel chipset component drivers
    for (const auto& component : intelChipsetComponents) {
      if (driver.deviceName.find(component) != std::string::npos) {
        isChipsetDriver = true;

        // Store Intel component drivers separately
        if (driver.isDateValid) {
          intelComponentDrivers.push_back(driver);
        }
        break;
      }
    }

    if (isChipsetDriver) {
      filteredDrivers.push_back(driver);
    }
  }

  // Check for AMD PSP specifically (might be in Security device class)
  std::vector<DriverInfo> allDrivers =
    getDriverInfo("");  // Get all device classes

  for (const auto& driver : allDrivers) {
    if (driver.deviceName.find("AMD PSP") != std::string::npos ||
        driver.deviceName.find("AMD Platform Security Processor") !=
          std::string::npos) {

      // Check if we already have this driver (avoid duplicates)
      bool isDuplicate = false;
      for (const auto& existingDriver : filteredDrivers) {
        if (existingDriver.deviceName == driver.deviceName) {
          isDuplicate = true;
          break;
        }
      }

      if (!isDuplicate) {
        if (driver.isDateValid) {
          amdComponentDrivers.push_back(driver);
        }
        filteredDrivers.push_back(driver);
      }
    }
  }

  // Use AMD component driver dates to update the main chipset driver date if
  // available
  if (!filteredDrivers.empty() && !amdComponentDrivers.empty() &&
      filteredDrivers[0].deviceName.find("AMD") != std::string::npos) {

    // Find the most recent component driver date
    std::string mostRecentDate;
    for (const auto& compDriver : amdComponentDrivers) {
      if (mostRecentDate.empty() || compDriver.driverDate > mostRecentDate) {
        mostRecentDate = compDriver.driverDate;
      }
    }

    if (!mostRecentDate.empty()) {
      filteredDrivers[0].driverDate = mostRecentDate;
      filteredDrivers[0].isDateValid = true;
    }
  }

  // Similarly, use Intel component driver dates to update main Intel chipset
  // driver date
  if (!filteredDrivers.empty() && !intelComponentDrivers.empty() &&
      filteredDrivers[0].deviceName.find("Intel") != std::string::npos) {

    // Find the most recent component driver date
    std::string mostRecentDate;
    for (const auto& compDriver : intelComponentDrivers) {
      if (mostRecentDate.empty() || compDriver.driverDate > mostRecentDate) {
        mostRecentDate = compDriver.driverDate;
      }
    }

    if (!mostRecentDate.empty()) {
      filteredDrivers[0].driverDate = mostRecentDate;
      filteredDrivers[0].isDateValid = true;
    }
  }

  return filteredDrivers;
}

std::vector<SystemWrapper::DriverInfo> SystemWrapper::getAudioDriverDetails() {
  std::vector<DriverInfo> audioDrivers = getDriverInfo("Sound");
  std::vector<DriverInfo> filteredDrivers;

  // Known audio hardware manufacturers to include
  const std::vector<std::string> knownAudioManufacturers = {
    "Realtek",
    "Creative",
    "Yamaha",
    "Steinberg",
    "Sound Blaster",
    "ASUS",
    "Focusrite",
    "PreSonus",
    "MOTU",
    "Roland",
    "Universal Audio",
    "Behringer",
    "Native Instruments",
    "Logitech",
    "Razer",
    "Corsair",
    "Turtle Beach",
    "HyperX",
    "SteelSeries",
    "Audiotrak",
    "Asus Xonar",
    "AudioQuest",
    "M-Audio",
    "Antlion",
    "Sennheiser",
    "Blue Microphones"};

  // Audio device keywords to include
  const std::vector<std::string> includeKeywords = {
    "High Definition Audio", "HD Audio",        "Audio Device", "Sound Card",
    "Sound Device",          "Audio Controller"};

  // Devices to exclude
  const std::vector<std::string> excludeKeywords = {
    "Microsoft Streaming Service",
    "NVIDIA High Definition Audio",
    "NVIDIA Virtual Audio",
    "NVIDIA Broadcast",
    "HDMI Audio",
    "Remote Audio",
    "Bluetooth Audio",
    "DisplayPort Audio",
    "Intel Smart Sound"};

  for (const auto& driver : audioDrivers) {
    bool shouldInclude = false;
    bool shouldExclude = false;

    // Check if device is from a known audio manufacturer
    for (const auto& manufacturer : knownAudioManufacturers) {
      if (driver.deviceName.find(manufacturer) != std::string::npos ||
          driver.providerName.find(manufacturer) != std::string::npos) {
        shouldInclude = true;
        break;
      }
    }

    // Check for include keywords if not already included
    if (!shouldInclude) {
      for (const auto& keyword : includeKeywords) {
        if (driver.deviceName.find(keyword) != std::string::npos) {
          shouldInclude = true;
          break;
        }
      }
    }

    // Check for exclude keywords
    for (const auto& keyword : excludeKeywords) {
      if (driver.deviceName.find(keyword) != std::string::npos) {
        shouldExclude = true;
        break;
      }
    }

    // Special case: include Microsoft HD Audio only if provider is Microsoft
    // and name contains "High Definition Audio"
    if (driver.providerName.find("Microsoft") != std::string::npos &&
        driver.deviceName.find("High Definition Audio") != std::string::npos) {
      shouldInclude = true;
      shouldExclude = false;
    }

    // Exclude Microsoft drivers except for High Definition Audio
    if (driver.providerName.find("Microsoft") != std::string::npos &&
        !shouldInclude) {
      shouldExclude = true;
    }

    if (shouldInclude && !shouldExclude) {
      filteredDrivers.push_back(driver);
    }
  }

  return filteredDrivers;
}

std::vector<SystemWrapper::DriverInfo> SystemWrapper::getNetworkDriverDetails() {
  std::vector<DriverInfo> networkDrivers = getDriverInfo("Net");
  std::vector<DriverInfo> filteredDrivers;

  // Known network hardware manufacturers to include
  const std::vector<std::string> knownNetworkManufacturers = {
    "Intel",          "Realtek",  "Killer", "Broadcom", "Marvell",  "Atheros",
    "Rivet Networks", "Qualcomm", "Ralink", "Aquantia", "MediaTek", "TP-Link",
    "D-Link",         "Netgear",  "ASUS",   "MSI",      "Gigabyte", "ASRock",
    "EDUP",           "Mellanox", "Cisco",  "3Com",     "AMD"};

  // Network device keywords to include
  const std::vector<std::string> includeKeywords = {
    "Ethernet",       "Network Connection",
    "Gigabit",        "LAN",
    "Wireless",       "Wi-Fi",
    "WiFi",           "802.11",
    "Network Adapter"};

  // Devices to exclude
  const std::vector<std::string> excludeKeywords = {
    "WAN Miniport", "Virtual",    "VPN",       "Tunnel",       "TAP",
    "TUN",          "Bluetooth",  "Debug",     "Kernel Debug", "Monitor",
    "Teredo",       "ISATAP",     "RAS",       "NDIS",         "PPPOE",
    "PPTP",         "L2TP",       "SSTP",      "IKEv2",        "NordVPN",
    "OpenVPN",      "ExpressVPN", "SurfShark", "NordLynx"};

  for (const auto& driver : networkDrivers) {
    bool shouldInclude = false;
    bool shouldExclude = false;

    // Check if device is from a known network manufacturer
    for (const auto& manufacturer : knownNetworkManufacturers) {
      if (driver.deviceName.find(manufacturer) != std::string::npos ||
          driver.providerName.find(manufacturer) != std::string::npos) {
        shouldInclude = true;
        break;
      }
    }

    // Check for include keywords if not already included
    if (!shouldInclude) {
      for (const auto& keyword : includeKeywords) {
        if (driver.deviceName.find(keyword) != std::string::npos) {
          shouldInclude = true;
          break;
        }
      }
    }

    // Check for exclude keywords
    for (const auto& keyword : excludeKeywords) {
      if (driver.deviceName.find(keyword) != std::string::npos) {
        shouldExclude = true;
        break;
      }
    }

    // Exclude Microsoft drivers
    if (driver.providerName.find("Microsoft") != std::string::npos) {
      shouldExclude = true;
    }

    if (shouldInclude && !shouldExclude) {
      filteredDrivers.push_back(driver);
    }
  }

  return filteredDrivers;
}

// Add these methods to the SystemWrapper implementation

std::string SystemWrapper::logRawWmiData() {
  std::stringstream ss;
  ss << "=== RAW WMI Data Collection ===\n";

  // Helper function to execute WMI query and log all properties
  auto logWmiClass = [&ss](const wchar_t* query, const char* className) {
    ss << "\n--- " << className << " ---\n";

    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
      ss << "COM initialization failed: 0x" << std::hex << hr << std::dec
         << "\n";
      return;
    }

    bool comInitialized = (hr != RPC_E_CHANGED_MODE);

    IWbemLocator* pLoc = nullptr;
    hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, (LPVOID*)&pLoc);

    if (SUCCEEDED(hr) && pLoc) {
      IWbemServices* pSvc = nullptr;
      hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), nullptr, nullptr, 0,
                               NULL, 0, 0, &pSvc);

      if (SUCCEEDED(hr) && pSvc) {
        hr = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                               RPC_C_AUTHN_LEVEL_CALL,
                               RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

        if (SUCCEEDED(hr)) {
          IEnumWbemClassObject* pEnumerator = nullptr;
          hr = pSvc->ExecQuery(bstr_t("WQL"), bstr_t(query),
                               WBEM_FLAG_FORWARD_ONLY |
                                 WBEM_FLAG_RETURN_IMMEDIATELY,
                               NULL, &pEnumerator);

          if (SUCCEEDED(hr) && pEnumerator) {
            int objectCount = 0;
            IWbemClassObject* pclsObj = nullptr;
            ULONG uReturn = 0;

            while (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) ==
                   S_OK) {
              objectCount++;
              ss << "Object #" << objectCount << ":\n";

              // Get all properties
              SAFEARRAY* pNames = nullptr;
              if (SUCCEEDED(pclsObj->GetNames(
                    nullptr, WBEM_FLAG_ALWAYS | WBEM_FLAG_NONSYSTEM_ONLY,
                    nullptr, &pNames))) {
                long lLower, lUpper;
                SafeArrayGetLBound(pNames, 1, &lLower);
                SafeArrayGetUBound(pNames, 1, &lUpper);

                for (long i = lLower; i <= lUpper; i++) {
                  BSTR bstrName;
                  SafeArrayGetElement(pNames, &i, &bstrName);

                  VARIANT vtProp;
                  VariantInit(&vtProp);
                  if (SUCCEEDED(
                        pclsObj->Get(bstrName, 0, &vtProp, nullptr, nullptr))) {
                    ss << "  " << static_cast<const char*>(_bstr_t(bstrName))
                       << ": ";

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
                    ss << "\n";
                  }
                  VariantClear(&vtProp);
                  SysFreeString(bstrName);
                }
                SafeArrayDestroy(pNames);
              }

              pclsObj->Release();
            }

            ss << "Total objects: " << objectCount << "\n";
            pEnumerator->Release();
          } else {
            ss << "Failed to execute query: 0x" << std::hex << hr << std::dec
               << "\n";
          }
        }
        pSvc->Release();
      }
      pLoc->Release();
    }

    if (comInitialized) {
      CoUninitialize();
    }
  };

  // Log motherboard & BIOS information
  logWmiClass(L"SELECT * FROM Win32_BaseBoard", "Win32_BaseBoard");
  logWmiClass(L"SELECT * FROM Win32_BIOS", "Win32_BIOS");
  logWmiClass(L"SELECT * FROM Win32_ComputerSystem", "Win32_ComputerSystem");

  // Log drive information
  logWmiClass(L"SELECT * FROM Win32_DiskDrive", "Win32_DiskDrive");
  logWmiClass(L"SELECT * FROM Win32_LogicalDisk WHERE DriveType=3",
              "Win32_LogicalDisk");
  logWmiClass(L"SELECT * FROM Win32_DiskDriveToDiskPartition",
              "Win32_DiskDriveToDiskPartition");
  logWmiClass(L"SELECT * FROM Win32_LogicalDiskToPartition",
              "Win32_LogicalDiskToPartition");

  // Log page file information
  logWmiClass(L"SELECT * FROM Win32_PageFileUsage", "Win32_PageFileUsage");
  logWmiClass(L"SELECT * FROM Win32_PageFileSetting", "Win32_PageFileSetting");

  // Log power plan information
  logWmiClass(L"SELECT * FROM Win32_PowerPlan", "Win32_PowerPlan");

  return ss.str();
}

std::string SystemWrapper::logRawRegistryData() {
  std::stringstream ss;
  ss << "=== RAW Registry Data Collection ===\n";

  // Helper function to log registry key values
  auto logRegistryKey = [&ss](const char* keyPath, const char* keyName) {
    ss << "\n--- Registry Key: " << keyPath << " ---\n";

    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, keyPath, 0, KEY_READ, &hKey) ==
        ERROR_SUCCESS) {
      ss << "Key opened successfully\n";

      // If no specific value name is provided, enumerate all values
      if (!keyName || strcmp(keyName, "") == 0) {
        DWORD valueCount;
        DWORD maxValueNameLen;
        DWORD maxValueLen;

        if (RegQueryInfoKeyA(hKey, NULL, NULL, NULL, NULL, NULL, NULL,
                             &valueCount, &maxValueNameLen, &maxValueLen, NULL,
                             NULL) == ERROR_SUCCESS) {

          ss << "Value count: " << valueCount << "\n";

          // Allocate buffer for value name and data
          std::vector<char> valueName(maxValueNameLen + 1);
          std::vector<BYTE> valueData(maxValueLen + 1);

          for (DWORD i = 0; i < valueCount; i++) {
            DWORD valueNameSize = maxValueNameLen + 1;
            DWORD valueDataSize = maxValueLen + 1;
            DWORD valueType;

            if (RegEnumValueA(hKey, i, valueName.data(), &valueNameSize, NULL,
                              &valueType, valueData.data(),
                              &valueDataSize) == ERROR_SUCCESS) {

              ss << "  " << valueName.data() << " (Type: " << valueType
                 << "): ";

              // Format based on type
              switch (valueType) {
                case REG_SZ:
                case REG_EXPAND_SZ:
                  ss << static_cast<char*>(
                    static_cast<void*>(valueData.data()));
                  break;
                case REG_DWORD:
                  ss << *reinterpret_cast<DWORD*>(valueData.data());
                  break;
                case REG_QWORD:
                  ss << *reinterpret_cast<ULONGLONG*>(valueData.data());
                  break;
                case REG_BINARY:
                  ss << "[Binary data, " << valueDataSize << " bytes]";
                  break;
                case REG_MULTI_SZ:
                  {
                    char* ptr = reinterpret_cast<char*>(valueData.data());
                    ss << "[Multi-string: ";
                    while (*ptr) {
                      ss << "\"" << ptr << "\" ";
                      ptr += strlen(ptr) + 1;
                    }
                    ss << "]";
                    break;
                  }
                default:
                  ss << "[Data type " << valueType << ", " << valueDataSize
                     << " bytes]";
              }

              ss << "\n";
            }
          }
        }
      } else {
        // Query specific value
        DWORD valueType;
        DWORD valueSize = 0;

        // First get the size
        if (RegQueryValueExA(hKey, keyName, NULL, &valueType, NULL,
                             &valueSize) == ERROR_SUCCESS) {
          std::vector<BYTE> valueData(valueSize + 1, 0);

          if (RegQueryValueExA(hKey, keyName, NULL, &valueType,
                               valueData.data(), &valueSize) == ERROR_SUCCESS) {
            ss << "  " << keyName << " (Type: " << valueType << "): ";

            // Format based on type
            switch (valueType) {
              case REG_SZ:
              case REG_EXPAND_SZ:
                ss << static_cast<char*>(static_cast<void*>(valueData.data()));
                break;
              case REG_DWORD:
                ss << *reinterpret_cast<DWORD*>(valueData.data());
                break;
              case REG_QWORD:
                ss << *reinterpret_cast<ULONGLONG*>(valueData.data());
                break;
              case REG_BINARY:
                ss << "[Binary data, " << valueSize << " bytes]";
                break;
              case REG_MULTI_SZ:
                {
                  char* ptr = reinterpret_cast<char*>(valueData.data());
                  ss << "[Multi-string: ";
                  while (*ptr) {
                    ss << "\"" << ptr << "\" ";
                    ptr += strlen(ptr) + 1;
                  }
                  ss << "]";
                  break;
                }
              default:
                ss << "[Data type " << valueType << ", " << valueSize
                   << " bytes]";
            }

            ss << "\n";
          } else {
            ss << "  Failed to query value data for " << keyName << "\n";
          }
        } else {
          ss << "  Value not found: " << keyName << "\n";
        }
      }

      RegCloseKey(hKey);
    } else {
      ss << "Failed to open key\n";
    }
  };

  // Log BIOS and motherboard information
  logRegistryKey("HARDWARE\\DESCRIPTION\\System\\BIOS", "");

  // Log chipset driver information
  logRegistryKey("SOFTWARE\\WOW6432Node\\AMD\\AMD_Chipset_IODrivers", "");
  logRegistryKey("SOFTWARE\\AMD\\AMD Chipset Software", "");
  logRegistryKey("SOFTWARE\\Intel\\IntelChipsetSoftware", "");

  // Log power management information
  logRegistryKey("SYSTEM\\CurrentControlSet\\Control\\Power", "");
  logRegistryKey("SOFTWARE\\Microsoft\\GameBar", "AutoGameMode");

  // Log pagefile settings
  logRegistryKey(
    "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
    "PagingFiles");

  return ss.str();
}

std::string SystemWrapper::logRawAPIData() {
  std::stringstream ss;
  ss << "=== RAW Windows API Data Collection ===\n";

  // Log monitor information
  ss << "\n--- Display Information ---\n";

  DISPLAY_DEVICEA displayDevice;
  DEVMODEA deviceMode;

  ZeroMemory(&displayDevice, sizeof(displayDevice));
  displayDevice.cb = sizeof(displayDevice);

  ZeroMemory(&deviceMode, sizeof(deviceMode));
  deviceMode.dmSize = sizeof(deviceMode);

  DWORD deviceIndex = 0;
  ss << "Enumerating display devices:\n";

  while (EnumDisplayDevicesA(NULL, deviceIndex, &displayDevice, 0)) {
    ss << "Device #" << deviceIndex << ":\n";
    ss << "  Device Name: " << displayDevice.DeviceName << "\n";
    ss << "  Device String: " << displayDevice.DeviceString << "\n";
    ss << "  State Flags: 0x" << std::hex << displayDevice.StateFlags
       << std::dec << "\n";
    ss << "  Device ID: " << displayDevice.DeviceID << "\n";
    ss << "  Device Key: " << displayDevice.DeviceKey << "\n";

    if (EnumDisplaySettingsA(displayDevice.DeviceName, ENUM_CURRENT_SETTINGS,
                             &deviceMode)) {
      ss << "  Current Settings:\n";
      ss << "    Width: " << deviceMode.dmPelsWidth << "\n";
      ss << "    Height: " << deviceMode.dmPelsHeight << "\n";
      ss << "    Bits Per Pixel: " << deviceMode.dmBitsPerPel << "\n";
      ss << "    Refresh Rate: " << deviceMode.dmDisplayFrequency << " Hz\n";
      ss << "    Display Flags: 0x" << std::hex << deviceMode.dmDisplayFlags
         << std::dec << "\n";
      ss << "    Display Orientation: " << deviceMode.dmDisplayOrientation
         << "\n";
    }

    deviceIndex++;
    ZeroMemory(&displayDevice, sizeof(displayDevice));
    displayDevice.cb = sizeof(displayDevice);
  }

  // Log power plan information
  ss << "\n--- Power Plan Information ---\n";

  GUID* pActivePolicy = nullptr;
  if (PowerGetActiveScheme(NULL, &pActivePolicy) == ERROR_SUCCESS) {
    ss << "Active Power Plan GUID: ";
    ss << std::hex << pActivePolicy->Data1 << "-" << pActivePolicy->Data2 << "-"
       << pActivePolicy->Data3 << "-";

    for (int i = 0; i < 8; i++) {
      ss << std::hex << static_cast<int>(pActivePolicy->Data4[i]);
      if (i == 1) ss << "-";
    }
    ss << std::dec << "\n";

    // Get the friendly name
    DWORD nameSize = 0;
    PowerReadFriendlyName(NULL, pActivePolicy, NULL, NULL, NULL, &nameSize);

    if (nameSize > 0) {
      std::vector<wchar_t> nameBuf(nameSize / sizeof(wchar_t));
      if (PowerReadFriendlyName(NULL, pActivePolicy, NULL, NULL,
                                (UCHAR*)nameBuf.data(),
                                &nameSize) == ERROR_SUCCESS) {
        // Convert wide string to narrow string safely
        int len = WideCharToMultiByte(CP_UTF8, 0, nameBuf.data(), -1, nullptr,
                                      0, nullptr, nullptr);
        if (len > 0) {
          std::vector<char> buf(len);
          WideCharToMultiByte(CP_UTF8, 0, nameBuf.data(), -1, buf.data(), len,
                              nullptr, nullptr);
          ss << "Power Plan Name: " << buf.data() << "\n";
        }
      }
    }

    // Enumerate some power settings
    GUID subgroups[] = {// GUID_SLEEP_SUBGROUP
                        {0x238C9FA8,
                         0x0AAD,
                         0x41ED,
                         {0x83, 0xF4, 0x97, 0xBE, 0x24, 0x2C, 0x8F, 0x20}},
                        // GUID_PROCESSOR_SETTINGS_SUBGROUP
                        {0x54533251,
                         0x82BE,
                         0x4824,
                         {0x96, 0xC1, 0x47, 0xB6, 0x0B, 0x74, 0x0D, 0x00}}};

    for (const auto& subgroup : subgroups) {
      ss << "Subgroup: ";
      ss << std::hex << subgroup.Data1 << "-" << subgroup.Data2 << "-"
         << subgroup.Data3 << "-";

      for (int i = 0; i < 8; i++) {
        ss << std::hex << static_cast<int>(subgroup.Data4[i]);
        if (i == 1) ss << "-";
      }
      ss << std::dec << "\n";

      // Get subgroup friendly name
      DWORD sgNameSize = 0;
      PowerReadFriendlyName(NULL, &subgroup, NULL, NULL, NULL, &sgNameSize);

      if (sgNameSize > 0) {
        std::vector<wchar_t> sgNameBuf(sgNameSize / sizeof(wchar_t));
        if (PowerReadFriendlyName(NULL, &subgroup, NULL, NULL,
                                  (UCHAR*)sgNameBuf.data(),
                                  &sgNameSize) == ERROR_SUCCESS) {
          int len = WideCharToMultiByte(CP_UTF8, 0, sgNameBuf.data(), -1,
                                        nullptr, 0, nullptr, nullptr);
          if (len > 0) {
            std::vector<char> buf(len);
            WideCharToMultiByte(CP_UTF8, 0, sgNameBuf.data(), -1, buf.data(),
                                len, nullptr, nullptr);
            ss << "  Subgroup Name: " << buf.data() << "\n";
          }
        }
      }
    }

    LocalFree(pActivePolicy);
  } else {
    ss << "Failed to get active power scheme\n";
  }

  // Log system memory information
  ss << "\n--- Memory Information ---\n";

  MEMORYSTATUSEX memStatus = {sizeof(MEMORYSTATUSEX)};
  if (GlobalMemoryStatusEx(&memStatus)) {
    ss << "Memory Load: " << memStatus.dwMemoryLoad << "%\n";
    ss << "Total Physical Memory: " << memStatus.ullTotalPhys << " bytes ("
       << (memStatus.ullTotalPhys / (1024 * 1024 * 1024)) << " GB)\n";
    ss << "Available Physical Memory: " << memStatus.ullAvailPhys << " bytes ("
       << (memStatus.ullAvailPhys / (1024 * 1024 * 1024)) << " GB)\n";
    ss << "Total Page File: " << memStatus.ullTotalPageFile << " bytes ("
       << (memStatus.ullTotalPageFile / (1024 * 1024 * 1024)) << " GB)\n";
    ss << "Available Page File: " << memStatus.ullAvailPageFile << " bytes ("
       << (memStatus.ullAvailPageFile / (1024 * 1024 * 1024)) << " GB)\n";
    ss << "Total Virtual Memory: " << memStatus.ullTotalVirtual << " bytes ("
       << (memStatus.ullTotalVirtual / (1024 * 1024 * 1024)) << " GB)\n";
    ss << "Available Virtual Memory: " << memStatus.ullAvailVirtual
       << " bytes (" << (memStatus.ullAvailVirtual / (1024 * 1024 * 1024))
       << " GB)\n";
  } else {
    ss << "Failed to get memory status\n";
  }

  return ss.str();
}

std::string SystemWrapper::logRawDriverData() {
  std::stringstream ss;
  ss << "=== RAW Driver Information ===\n";

  // Log driver information using SetupAPI
  auto logDeviceClass = [&ss](const GUID* pClassGuid, const char* className) {
    ss << "\n--- Device Class: " << className << " ---\n";

    HDEVINFO hDevInfo =
      SetupDiGetClassDevsA(pClassGuid, NULL, NULL,
                           DIGCF_PRESENT | (pClassGuid ? 0 : DIGCF_ALLCLASSES));

    if (hDevInfo == INVALID_HANDLE_VALUE) {
      ss << "Failed to get device information set. Error code: "
         << GetLastError() << "\n";
      return;
    }

    SP_DEVINFO_DATA devInfoData;
    devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    int deviceCount = 0;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devInfoData); i++) {
      deviceCount++;
      ss << "Device #" << deviceCount << ":\n";

      // Get device property names and values
      char buffer[512];
      DWORD propType;
      DWORD requiredSize;

      // Device instance ID
      if (CM_Get_Device_IDA(devInfoData.DevInst, buffer, sizeof(buffer), 0) ==
          CR_SUCCESS) {
        ss << "  Device Instance ID: " << buffer << "\n";
      }

      // Device description
      if (SetupDiGetDeviceRegistryPropertyA(
            hDevInfo, &devInfoData, SPDRP_DEVICEDESC, &propType, (PBYTE)buffer,
            sizeof(buffer), &requiredSize)) {
        ss << "  Description: " << buffer << "\n";
      }

      // Friendly name
      if (SetupDiGetDeviceRegistryPropertyA(
            hDevInfo, &devInfoData, SPDRP_FRIENDLYNAME, &propType,
            (PBYTE)buffer, sizeof(buffer), &requiredSize)) {
        ss << "  Friendly Name: " << buffer << "\n";
      }

      // Driver provider
      if (SetupDiGetDeviceRegistryPropertyA(
            hDevInfo, &devInfoData, SPDRP_PROVIDERNAME, &propType,
            (PBYTE)buffer, sizeof(buffer), &requiredSize)) {
        ss << "  Provider: " << buffer << "\n";
      }

      // Manufacturer
      if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &devInfoData, SPDRP_MFG,
                                            &propType, (PBYTE)buffer,
                                            sizeof(buffer), &requiredSize)) {
        ss << "  Manufacturer: " << buffer << "\n";
      }

      // Device driver key for additional properties
      HKEY hDriverKey = SetupDiOpenDevRegKey(
        hDevInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);

      if (hDriverKey != INVALID_HANDLE_VALUE) {
        DWORD valueType;
        DWORD valueSize = sizeof(buffer);

        if (RegQueryValueExA(hDriverKey, "DriverVersion", NULL, &valueType,
                             (LPBYTE)buffer, &valueSize) == ERROR_SUCCESS) {
          ss << "  Driver Version: " << buffer << "\n";
        }

        valueSize = sizeof(buffer);
        if (RegQueryValueExA(hDriverKey, "DriverDate", NULL, &valueType,
                             (LPBYTE)buffer, &valueSize) == ERROR_SUCCESS) {
          ss << "  Driver Date: " << buffer << "\n";
        }

        RegCloseKey(hDriverKey);
      }

      // Hardware IDs
      if (SetupDiGetDeviceRegistryPropertyA(
            hDevInfo, &devInfoData, SPDRP_HARDWAREID, &propType, (PBYTE)buffer,
            sizeof(buffer), &requiredSize)) {

        if (propType == REG_MULTI_SZ) {
          ss << "  Hardware IDs:\n";
          char* ptr = buffer;
          while (*ptr) {
            ss << "    " << ptr << "\n";
            ptr += strlen(ptr) + 1;
          }
        }
      }

      // Log device registry key raw content
      HKEY hDeviceKey;
      if (CM_Open_DevNode_Key(devInfoData.DevInst, KEY_READ, 0,
                              RegDisposition_OpenExisting, &hDeviceKey,
                              CM_REGISTRY_HARDWARE) == CR_SUCCESS) {
        ss << "  Device Registry Properties:\n";

        DWORD valueCount = 0;
        DWORD maxValueNameLen = 0;
        DWORD maxValueLen = 0;

        if (RegQueryInfoKeyA(hDeviceKey, NULL, NULL, NULL, NULL, NULL, NULL,
                             &valueCount, &maxValueNameLen, &maxValueLen, NULL,
                             NULL) == ERROR_SUCCESS) {

          if (valueCount > 0) {
            std::vector<char> valueName(maxValueNameLen + 1);
            std::vector<BYTE> valueData(maxValueLen + 1);

            for (DWORD j = 0; j < valueCount; j++) {
              DWORD valueNameSize = maxValueNameLen + 1;
              DWORD valueDataSize = maxValueLen + 1;
              DWORD valueType;

              if (RegEnumValueA(hDeviceKey, j, valueName.data(), &valueNameSize,
                                NULL, &valueType, valueData.data(),
                                &valueDataSize) == ERROR_SUCCESS) {

                ss << "    " << valueName.data() << ": ";

                if (valueType == REG_SZ || valueType == REG_EXPAND_SZ) {
                  ss << static_cast<char*>(
                    static_cast<void*>(valueData.data()));
                } else {
                  ss << "[Data type " << valueType << ", " << valueDataSize
                     << " bytes]";
                }

                ss << "\n";
              }
            }
          } else {
            ss << "    No values found\n";
          }
        }

        RegCloseKey(hDeviceKey);
      }
    }

    if (deviceCount == 0) {
      ss << "No devices found in this class\n";
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
  };

  // Log different device classes
  logDeviceClass(&GUID_DEVCLASS_SYSTEM, "System");
  logDeviceClass(&GUID_DEVCLASS_MEDIA, "Media (Sound)");
  logDeviceClass(&GUID_DEVCLASS_NET, "Network");

  return ss.str();
}

std::string SystemWrapper::logRawData() {
  std::stringstream ss;

  ss << "===================================================\n";
  ss << "=== SystemWrapper Raw Data Collection Log ===\n";
  ss << "===================================================\n\n";

  try {
    // Call each raw data collection method and append to the stream
    ss << logRawWmiData();
    ss << "\n\n";
    ss << logRawRegistryData();
    ss << "\n\n";
    ss << logRawAPIData();
    ss << "\n\n";
    ss << logRawDriverData();
  } catch (const std::exception& e) {
    ss << "ERROR: Exception during raw data collection: " << e.what() << "\n";
  }

  return ss.str();
}
