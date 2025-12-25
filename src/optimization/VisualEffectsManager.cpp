#include "VisualEffectsManager.h"

#include "logging/Logger.h"

#include <Windows.h>
#include <dwmapi.h>

#include "BackupManager.h"
#include "RegistryLogger.h"

namespace optimizations {
namespace visual_effects {

// Static definitions for profile names and descriptions
std::string VisualEffectsManager::GetProfileName(VisualEffectsProfile profile) {
  switch (profile) {
    case VisualEffectsProfile::LetWindowsDecide:
      return "Let Windows decide";
    case VisualEffectsProfile::BestAppearance:
      return "Best appearance";
    case VisualEffectsProfile::BestPerformance:
      return "Best performance";
    case VisualEffectsProfile::Recommended:
      return "Recommended";
    case VisualEffectsProfile::Custom:
      return "Custom";
    default:
      return "Unknown";
  }
}

std::string VisualEffectsManager::GetProfileDescription(
  VisualEffectsProfile profile) {
  switch (profile) {
    case VisualEffectsProfile::LetWindowsDecide:
      return "Let Windows choose what's best for your computer";
    case VisualEffectsProfile::BestAppearance:
      return "Adjust for best appearance";
    case VisualEffectsProfile::BestPerformance:
      return "Adjust for best performance";
    case VisualEffectsProfile::Recommended:
      return "Recommended performance settings - disables animations while "
             "keeping important visual features";
    case VisualEffectsProfile::Custom:
      return "Custom settings";
    default:
      return "Unknown profile";
  }
}

// VisualEffectsManager implementation
VisualEffectsManager::VisualEffectsManager() {
  // Private constructor
}

VisualEffectsManager& VisualEffectsManager::GetInstance() {
  static VisualEffectsManager instance;
  return instance;
}

bool VisualEffectsManager::Initialize() {
  if (is_initialized_) {
    return true;
  }

  // Check if we're on Windows 10/11
  RTL_OSVERSIONINFOW osvi = {0};
  osvi.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);

  // Define the function pointer type for RtlGetVersion
  typedef LONG(WINAPI * pfnRtlGetVersion)(RTL_OSVERSIONINFOW*);

  // Get the function from ntdll.dll
  HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
  if (!hNtdll) {
    return false;
  }

  pfnRtlGetVersion fnRtlGetVersion =
    (pfnRtlGetVersion)GetProcAddress(hNtdll, "RtlGetVersion");
  if (!fnRtlGetVersion) {
    return false;
  }

  osvi.dwOSVersionInfoSize = sizeof(osvi);
  if (fnRtlGetVersion(&osvi) != 0) {
    return false;
  }

  // Check if Windows 10 or higher
  if (osvi.dwMajorVersion < 10) {
    // We'll still continue, just warn the user
  }

  is_initialized_ = true;
  return true;
}

bool VisualEffectsManager::ApplyProfile(VisualEffectsProfile profile) {
  // Initialize if needed
  if (!Initialize()) {
    return false;
  }

  // Create both main and session backups if they don't exist
  auto& backupManager = BackupManager::GetInstance();

  // First check if we have a main backup
  if (backupManager.CheckBackupStatus(BackupType::VisualEffects, true) !=
      BackupStatus::CompleteBackup) {
    // Create a main backup of original settings
    backupManager.CreateBackup(BackupType::VisualEffects, true);
  }

  // Then create a session backup
  backupManager.CreateBackup(BackupType::VisualEffects, false);

  // Handle the Recommended profile separately
  if (profile == VisualEffectsProfile::Recommended) {
    return ApplyRecommendedSettings();
  }

  // For built-in Windows profiles, we just need to set the mode
  if (profile == VisualEffectsProfile::LetWindowsDecide ||
      profile == VisualEffectsProfile::BestAppearance ||
      profile == VisualEffectsProfile::BestPerformance) {

    // Set the VisualFXSetting registry value
    if (!SetRegistryDWORD("Software\\Microsoft\\Windows\\CurrentVersion\\Explor"
                          "er\\VisualEffects",
                          "VisualFXSetting", static_cast<DWORD>(profile))) {
      return false;
    }

    // Notify the system of the change
    return NotifySettingsChange();
  }

  // For Custom profile, we'll need to set specific registry values
  return ApplyRegistrySettings(profile);
}

VisualEffectsProfile VisualEffectsManager::GetCurrentProfile() {
  DWORD value = 0;
  if (!GetRegistryDWORD(
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects",
        "VisualFXSetting", value)) {
    return VisualEffectsProfile::Custom;  // Default to Custom if we can't read
  }

  // Check if the value maps to one of our predefined profiles
  if (value <= 3) {  // Valid profiles are 0, 1, 2, 3.
    VisualEffectsProfile currentProfile =
      static_cast<VisualEffectsProfile>(value);
    return currentProfile;
  }

  return VisualEffectsProfile::Custom;
}

bool VisualEffectsManager::ApplyRegistrySettings(VisualEffectsProfile profile) {
  // Only Custom and Recommended should use this method
  if (profile != VisualEffectsProfile::Custom &&
      profile != VisualEffectsProfile::Recommended) {
    return false;
  }

  // Set to Custom mode first
  if (!SetRegistryDWORD(
        "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects",
        "VisualFXSetting",
        3)) {  // 3 = Custom
    return false;
  }

  bool success = true;

  // Apply settings based on the profile
  if (profile == VisualEffectsProfile::Recommended) {
    // Apply our recommended performance settings
    // The actual implementation is in ApplyRecommendedSettings()
    success = ApplyRecommendedSettings();
  } else {
    // For a true custom profile, the caller should set individual settings
    // This typically means individual settings are managed elsewhere or this
    // path shouldn't be hit for 'Custom' if it implies user choice via UI
    // elements. For now, we assume 'Custom' means applying a set of predefined
    // "custom" settings if any, or it's a placeholder. Let's consider it a
    // success if VisualFXSetting was set to 3 (Custom).
    success = true;
  }

  // Notify the system of the changes
  if (success) {
    success = NotifySettingsChange();
  }

  return success;
}

bool VisualEffectsManager::ApplyRecommendedSettings() {

  bool success = true;

  // 1. Set to Custom mode
  success &= SetRegistryDWORD(
    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects",
    "VisualFXSetting",
    3);  // 3 = Custom

  // 2. Set UserPreferencesMask to disable most animations but keep font
  // smoothing The mask below disables animations, fades, shadows, and smooth
  // scrolling but keeps font smoothing enabled
  BYTE mask[] = {0x90, 0x12, 0x03, 0x80, 0x10, 0x00, 0x00, 0x00};
  success &= SetUserPreferencesMask(mask, sizeof(mask));

  // 3. Disable various visual effects in Explorer
  success &= SetRegistryDWORD(
    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "TaskbarAnimations", 0);

  success &= SetRegistryDWORD(
    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "ListviewShadow", 0);

  LOG_DEBUG << "[VisualEffectsManager.cpp::ApplyRecommendedSettings] Setting ListviewAlphaSelect to 1.";
  // Enable show translucent selection rectangle
  success &= SetRegistryDWORD(
    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "ListviewAlphaSelect",
    1);  // Changed from 0 to 1

  // 4. Disable window animation
  success &= SetRegistryString("Control Panel\\Desktop\\WindowMetrics",
                               "MinAnimate", "0");

  // 5. Enable window contents while dragging
  success &= SetRegistryString("Control Panel\\Desktop", "DragFullWindows",
                               "1");  // Changed from "0" to "1"

  // 6. Disable transparency
  success &= SetRegistryDWORD(
    "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
    "EnableTransparency", 0);

  // 7. DWM-specific effects
  // Enable peek
  success &=
    SetRegistryDWORD("Software\\Microsoft\\Windows\\DWM", "EnableAeroPeek",
                     1);  // Changed from 0 to 1

  success &= SetRegistryDWORD("Software\\Microsoft\\Windows\\DWM",
                              "AlwaysHibernateThumbnails", 0);

  // 8. Keep thumbnails instead of icons (useful feature)
  success &= SetRegistryDWORD(
    "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
    "IconsOnly", 0);

  // 9. Notify the system of the changes
  if (success) {
    success = NotifySettingsChange();
  }

  return success;
}

bool VisualEffectsManager::SetUserPreferencesMask(const BYTE* maskBytes,
                                                  DWORD maskSize) {
  HKEY hKey;
  LONG result = RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Desktop", 0,
                              KEY_WRITE, &hKey);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  result = RegSetValueExA(hKey, "UserPreferencesMask", 0, REG_BINARY, maskBytes,
                          maskSize);

  // Log the registry modification
  auto& logger = registry::RegistryLogger::GetInstance();
  logger.LogValueModification(HKEY_CURRENT_USER, "Control Panel\\Desktop",
                              "UserPreferencesMask", std::string("BINARY_DATA"),
                              result == ERROR_SUCCESS, result,
                              "visual_effects");

  RegCloseKey(hKey);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  return true;
}

bool VisualEffectsManager::GetUserPreferencesMask(BYTE* maskBytes,
                                                  DWORD& maskSize) {
  HKEY hKey;
  LONG result = RegOpenKeyExA(HKEY_CURRENT_USER, "Control Panel\\Desktop", 0,
                              KEY_READ, &hKey);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  DWORD type = REG_BINARY;
  result = RegQueryValueExA(hKey, "UserPreferencesMask", NULL, &type, maskBytes,
                            &maskSize);

  RegCloseKey(hKey);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  return true;
}

bool VisualEffectsManager::SetRegistryDWORD(const std::string& keyPath,
                                            const std::string& valueName,
                                            DWORD value) {
  HKEY hKey;
  LONG result =
    RegOpenKeyExA(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_WRITE, &hKey);

  if (result != ERROR_SUCCESS) {
    // Try to create the key if it doesn't exist
    result =
      RegCreateKeyExA(HKEY_CURRENT_USER, keyPath.c_str(), 0, NULL,
                      REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);

    // Log the key creation attempt
    auto& logger = registry::RegistryLogger::GetInstance();
    logger.LogKeyCreation(HKEY_CURRENT_USER, keyPath, result == ERROR_SUCCESS,
                          result, "visual_effects");

    if (result != ERROR_SUCCESS) {
      return false;
    }
  }

  result = RegSetValueExA(hKey, valueName.c_str(), 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value), sizeof(DWORD));

  // Log the registry modification
  auto& logger = registry::RegistryLogger::GetInstance();
  logger.LogValueModification(HKEY_CURRENT_USER, keyPath, valueName,
                              static_cast<int>(value), result == ERROR_SUCCESS,
                              result, "visual_effects");

  RegCloseKey(hKey);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  return true;
}

bool VisualEffectsManager::SetRegistryString(const std::string& keyPath,
                                             const std::string& valueName,
                                             const std::string& value) {
  HKEY hKey;
  LONG result =
    RegOpenKeyExA(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_WRITE, &hKey);

  if (result != ERROR_SUCCESS) {
    // Try to create the key if it doesn't exist
    result =
      RegCreateKeyExA(HKEY_CURRENT_USER, keyPath.c_str(), 0, NULL,
                      REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);

    // Log the key creation attempt
    auto& logger = registry::RegistryLogger::GetInstance();
    logger.LogKeyCreation(HKEY_CURRENT_USER, keyPath, result == ERROR_SUCCESS,
                          result, "visual_effects");

    if (result != ERROR_SUCCESS) {
      return false;
    }
  }

  result = RegSetValueExA(
    hKey, valueName.c_str(), 0, REG_SZ,
    reinterpret_cast<const BYTE*>(value.c_str()),
    static_cast<DWORD>(value.length() + 1)  // Include null terminator
  );

  // Log the registry modification
  auto& logger = registry::RegistryLogger::GetInstance();
  logger.LogValueModification(HKEY_CURRENT_USER, keyPath, valueName, value,
                              result == ERROR_SUCCESS, result,
                              "visual_effects");

  RegCloseKey(hKey);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  return true;
}

bool VisualEffectsManager::GetRegistryDWORD(const std::string& keyPath,
                                            const std::string& valueName,
                                            DWORD& value) {
  HKEY hKey;
  LONG result =
    RegOpenKeyExA(HKEY_CURRENT_USER, keyPath.c_str(), 0, KEY_READ, &hKey);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  DWORD dataSize = sizeof(DWORD);
  DWORD type = REG_DWORD;

  result = RegQueryValueExA(hKey, valueName.c_str(), NULL, &type,
                            reinterpret_cast<BYTE*>(&value), &dataSize);

  RegCloseKey(hKey);

  if (result != ERROR_SUCCESS) {
    return false;
  }

  return true;
}

bool VisualEffectsManager::NotifySettingsChange() {
  // Broadcast WM_SETTINGCHANGE to notify all apps
  DWORD_PTR result;
  BOOL success =
    SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                       (LPARAM) "Environment", SMTO_ABORTIFHUNG, 2000, &result);

  // Also refresh the shell
  SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM) "Windows",
                     SMTO_ABORTIFHUNG, 2000, &result);

  // Specifically for visual effect changes
  SendMessageTimeout(HWND_BROADCAST, WM_SETTINGCHANGE, SPI_SETUIEFFECTS, 0,
                     SMTO_ABORTIFHUNG, 2000, &result);

  return success != FALSE;
}

}  // namespace visual_effects
}  // namespace optimizations
