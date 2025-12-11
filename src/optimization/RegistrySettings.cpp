/**
 * @file RegistrySettings.cpp
 * @brief Implementation of registry settings management
 */

#include "RegistrySettings.h"

#include <limits>
#include <set>
#include <sstream>

#include <Windows.h>

#include "BackupManager.h"
#include "RegistryLogger.h"
#include "../logging/Logger.h"
#include "RegistrySettingsData.h"

// Forward declarations for helper utilities in the anonymous namespace
namespace optimizations {
namespace registry {
struct RegistrySettingDefinition;
}
}  // namespace optimizations

namespace {

const optimizations::registry::RegistrySettingDefinition* FindDefinitionById(
  const std::vector<optimizations::registry::RegistrySettingDefinition>&
    settings,
  const std::string& id) {
  for (const auto& setting : settings) {
    if (setting.id == id) {
      return &setting;
    }
  }
  return nullptr;
}

}  // namespace

namespace optimizations {
namespace registry {

//------------------------------------------------------------------------------
// Singleton Implementation
//------------------------------------------------------------------------------

RegistrySettings& RegistrySettings::GetInstance() {
  static RegistrySettings instance;
  return instance;
}

//------------------------------------------------------------------------------
// Core Functionality
//------------------------------------------------------------------------------

bool RegistrySettings::Initialize(const std::string& settings_file_path) {
  settings_file_path_ = settings_file_path;

  registry_settings_ = GetRegistrySettingDefinitions();
  return !registry_settings_.empty();
}

bool RegistrySettings::CheckSettingsFileExists() const {
  return !registry_settings_.empty();
}

//------------------------------------------------------------------------------
// Registry Path Utilities
//------------------------------------------------------------------------------

bool RegistrySettings::ParseFullRegistryPath(const std::string& full_path,
                                             HKEY& hive,
                                             std::string& key_path) {
  if (full_path.find("HKEY_LOCAL_MACHINE\\") == 0) {
    hive = HKEY_LOCAL_MACHINE;
    key_path = full_path.substr(19);
    return true;
  } else if (full_path.find("HKEY_CURRENT_USER\\") == 0) {
    hive = HKEY_CURRENT_USER;
    key_path = full_path.substr(18);
    return true;
  } else if (full_path.find("HKEY_CLASSES_ROOT\\") == 0) {
    hive = HKEY_CLASSES_ROOT;
    key_path = full_path.substr(18);
    return true;
  } else if (full_path.find("HKEY_USERS\\") == 0) {
    hive = HKEY_USERS;
    key_path = full_path.substr(11);
    return true;
  } else if (full_path.find("HKEY_CURRENT_CONFIG\\") == 0) {
    hive = HKEY_CURRENT_CONFIG;
    key_path = full_path.substr(20);
    return true;
  }

  // Default to HKEY_CURRENT_USER for backward compatibility
  hive = HKEY_CURRENT_USER;
  key_path = full_path;
  return true;
}

std::string RegistrySettings::GetHiveName(HKEY hive) {
  if (hive == HKEY_LOCAL_MACHINE) return "HKEY_LOCAL_MACHINE";
  if (hive == HKEY_CURRENT_USER) return "HKEY_CURRENT_USER";
  if (hive == HKEY_CLASSES_ROOT) return "HKEY_CLASSES_ROOT";
  if (hive == HKEY_USERS) return "HKEY_USERS";
  if (hive == HKEY_CURRENT_CONFIG) return "HKEY_CURRENT_CONFIG";
  return "UNKNOWN_HIVE";
}

//------------------------------------------------------------------------------
// Registry Creation Helper
//------------------------------------------------------------------------------

static bool CreateRegistryPathAndValue(HKEY hive, const std::string& key_path,
                                       const std::string& value_name,
                                       const OptimizationValue& value,
                                       const std::string& setting_id) {
  auto& logger = RegistryLogger::GetInstance();

  // Split path into components
  std::vector<std::string> path_components;
  std::string current_component;
  std::istringstream path_stream(key_path);

  while (std::getline(path_stream, current_component, '\\')) {
    if (!current_component.empty()) {
      path_components.push_back(current_component);
    }
  }

  // Create path step by step
  HKEY current_key = hive;
  std::string built_path = "";

  for (size_t i = 0; i < path_components.size(); ++i) {
    HKEY next_key;
    DWORD disposition;

    LONG result = RegCreateKeyExA(current_key, path_components[i].c_str(), 0,
                                  nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE,
                                  nullptr, &next_key, &disposition);

    if (!built_path.empty()) built_path += "\\";
    built_path += path_components[i];

    logger.LogKeyCreation(hive, built_path, result == ERROR_SUCCESS, result,
                          setting_id);

    if (result != ERROR_SUCCESS) {
      if (current_key != hive) {
        RegCloseKey(current_key);
      }
      return false;
    }

    if (current_key != hive) {
      RegCloseKey(current_key);
    }
    current_key = next_key;
  }

  // Set the value
  LONG result = ERROR_SUCCESS;

  if (std::holds_alternative<bool>(value)) {
    DWORD dword_value = std::get<bool>(value) ? 1 : 0;
    result = RegSetValueExA(current_key, value_name.c_str(), 0, REG_DWORD,
                            reinterpret_cast<const BYTE*>(&dword_value),
                            sizeof(dword_value));
  } else if (std::holds_alternative<int>(value)) {
    DWORD dword_value = static_cast<DWORD>(std::get<int>(value));
    result = RegSetValueExA(current_key, value_name.c_str(), 0, REG_DWORD,
                            reinterpret_cast<const BYTE*>(&dword_value),
                            sizeof(dword_value));
  } else if (std::holds_alternative<std::string>(value)) {
    const std::string& str_value = std::get<std::string>(value);
    result = RegSetValueExA(current_key, value_name.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(str_value.c_str()),
                            static_cast<DWORD>(str_value.length() + 1));
  }

  logger.LogValueModification(hive, key_path, value_name, value,
                              result == ERROR_SUCCESS, result, setting_id);

  if (result == ERROR_SUCCESS) {
    RegFlushKey(current_key);
  }

  RegCloseKey(current_key);
  return result == ERROR_SUCCESS;
}

//------------------------------------------------------------------------------
// Registry Value Checking
//------------------------------------------------------------------------------

static bool CheckRegistryValueExists(const std::string& registry_key,
                                     const std::string& registry_value_name) {
  HKEY hive;
  std::string key_path;
  if (!RegistrySettings::ParseFullRegistryPath(registry_key, hive, key_path)) {
    return false;
  }

  HKEY hKey;
  LONG result = RegOpenKeyExA(hive, key_path.c_str(), 0, KEY_READ, &hKey);
  if (result != ERROR_SUCCESS) {
    return false;
  }

  DWORD type, dataSize = 0;
  result = RegQueryValueExA(hKey, registry_value_name.c_str(), nullptr, &type,
                            nullptr, &dataSize);
  RegCloseKey(hKey);

  return result == ERROR_SUCCESS;
}

bool RegistrySettings::CheckCurrentValues() {
  missing_setting_ids_.clear();

  for (const auto& setting : registry_settings_) {
    const std::string& id = setting.id;
    const std::string& registry_key = setting.registry_key;
    const std::string& registry_value_name = setting.registry_value_name;

    if (registry_key.empty() || registry_value_name.empty()) {
      continue;
    }

    if (!CheckRegistryValueExists(registry_key, registry_value_name)) {
      missing_setting_ids_.insert(id);
    }
  }

  return true;
}

//------------------------------------------------------------------------------
// Entity Creation
//------------------------------------------------------------------------------

std::vector<std::unique_ptr<settings::OptimizationEntity>> RegistrySettings::
  CreateOptimizationEntities() const {
  std::vector<std::unique_ptr<settings::OptimizationEntity>> entities;

  // Skip mouse settings handled by wrapper
  std::set<std::string> mouseSettingsToSkip = {
    "win.mouse.acceleration", "win.mouse.threshold1", "win.mouse.threshold2"};

  for (const auto& setting : registry_settings_) {
    const std::string& id = setting.id;

    if (mouseSettingsToSkip.find(id) != mouseSettingsToSkip.end()) {
      continue;
    }

    try {
      auto entity = std::make_unique<optimizations::settings::ConfigurableOptimization>(
        setting);
      if (entity) {
        entities.push_back(std::move(entity));
      }
    } catch (const std::exception&) {
      // Skip invalid entities
    }
  }

  return entities;
}

//------------------------------------------------------------------------------
// Missing Settings Management
//------------------------------------------------------------------------------

bool RegistrySettings::CreateMissingRegistryPath(
  const std::string& setting_id, const OptimizationValue& value) {
  for (const auto& setting : registry_settings_) {
    if (setting.id != setting_id) continue;

    // Check security permission
    if (!setting.creation_allowed) {
      LOG_INFO
        << "[Registry Security] Setting '" << setting_id
        << "' is not whitelisted for creation. Registry creation denied."
       ;
      return false;
    }

    const std::string& registry_key = setting.registry_key;
    const std::string& registry_value_name = setting.registry_value_name;

    if (registry_key.empty() || registry_value_name.empty()) {
      return false;
    }

    // Parse path and create
    HKEY hive;
    std::string key_path;
    if (!ParseFullRegistryPath(registry_key, hive, key_path)) {
      return false;
    }

    bool success = CreateRegistryPathAndValue(
      hive, key_path, registry_value_name, value, setting_id);
    if (success) {
      missing_setting_ids_.erase(setting_id);
      Sleep(100);  // Allow registry to update
    }

    return success;
  }

  return false;
}

bool RegistrySettings::IsSettingMissing(const std::string& setting_id) const {
  return missing_setting_ids_.find(setting_id) != missing_setting_ids_.end();
}

std::string RegistrySettings::GetSettingsFilePath() const {
  return settings_file_path_;
}

//------------------------------------------------------------------------------
// Static Registry Operations
//------------------------------------------------------------------------------

bool RegistrySettings::ApplyRegistryValue(
  const std::string& registry_key, const std::string& registry_value_name,
  const OptimizationValue& value, const OptimizationValue& default_value) {
  if (registry_key.empty() || registry_value_name.empty()) {
    return false;
  }

  // Create backup before applying
  BackupManager::GetInstance().CreateBackup(BackupType::Registry, false);

  // Parse registry path
  HKEY targetHive;
  std::string actualKeyPath;
  bool useSpecificHive = false;

  if (registry_key.find("HKEY_") == 0) {
    if (ParseFullRegistryPath(registry_key, targetHive, actualKeyPath)) {
      useSpecificHive = true;
    } else {
      return false;
    }
  } else {
    actualKeyPath = registry_key;
  }

  auto applyToHive = [&](HKEY hive, const std::string& keyPath) -> bool {
    auto& logger = RegistryLogger::GetInstance();
    HKEY hKey;

    LONG result = RegOpenKeyExA(hive, keyPath.c_str(), 0, KEY_WRITE, &hKey);
    if (result != ERROR_SUCCESS) {
      return false;
    }

    bool success = false;

    // Special handling for NetworkThrottlingIndex
    if (registry_value_name == "NetworkThrottlingIndex" &&
        std::holds_alternative<int>(value) &&
        std::get<int>(value) == std::numeric_limits<int>::max()) {
      DWORD data = 0xFFFFFFFF;
      result =
        RegSetValueExA(hKey, registry_value_name.c_str(), 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&data), sizeof(DWORD));
      success = (result == ERROR_SUCCESS);
    }
    // Handle different value types
    else if (std::holds_alternative<bool>(value)) {
      DWORD data = std::get<bool>(value) ? 1 : 0;
      result =
        RegSetValueExA(hKey, registry_value_name.c_str(), 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&data), sizeof(DWORD));
      success = (result == ERROR_SUCCESS);
    } else if (std::holds_alternative<int>(value)) {
      DWORD data = static_cast<DWORD>(std::get<int>(value));
      result =
        RegSetValueExA(hKey, registry_value_name.c_str(), 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&data), sizeof(DWORD));
      success = (result == ERROR_SUCCESS);
    } else if (std::holds_alternative<double>(value)) {
      std::string strValue = std::to_string(std::get<double>(value));
      result = RegSetValueExA(hKey, registry_value_name.c_str(), 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(strValue.c_str()),
                              static_cast<DWORD>(strValue.size() + 1));
      success = (result == ERROR_SUCCESS);
    } else if (std::holds_alternative<std::string>(value)) {
      const std::string& strValue = std::get<std::string>(value);
      result = RegSetValueExA(hKey, registry_value_name.c_str(), 0, REG_SZ,
                              reinterpret_cast<const BYTE*>(strValue.c_str()),
                              static_cast<DWORD>(strValue.size() + 1));
      success = (result == ERROR_SUCCESS);
    }

    logger.LogValueModification(hive, keyPath, registry_value_name, value,
                                success, result);
    RegCloseKey(hKey);
    return success;
  };

  if (useSpecificHive) {
    return applyToHive(targetHive, actualKeyPath);
  } else {
    // Try HKEY_CURRENT_USER first, then HKEY_LOCAL_MACHINE
    const HKEY rootKeys[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    for (HKEY rootKey : rootKeys) {
      if (applyToHive(rootKey, actualKeyPath)) {
        return true;
      }
    }
    return false;
  }
}

// Overloaded version with setting_id for wallpaper refresh support
bool RegistrySettings::ApplyRegistryValue(
  const std::string& registry_key, const std::string& registry_value_name,
  const OptimizationValue& value, const OptimizationValue& default_value,
  const std::string& setting_id) {
  LOG_INFO << "[RegistrySettings] Applying setting: " << setting_id << " to "
            << registry_key << "\\" << registry_value_name;

  // Print the value being set
  if (std::holds_alternative<bool>(value)) {
    LOG_INFO << " = " << (std::get<bool>(value) ? "true" : "false");
  } else if (std::holds_alternative<int>(value)) {
    LOG_INFO << " = " << std::get<int>(value);
  } else if (std::holds_alternative<std::string>(value)) {
    LOG_INFO << " = \"" << std::get<std::string>(value) << "\"";
  }
  LOG_INFO;

  // Call the original ApplyRegistryValue
  bool result =
    ApplyRegistryValue(registry_key, registry_value_name, value, default_value);

  if (result) {
    LOG_INFO << "[RegistrySettings] Successfully applied setting: "
              << setting_id;
  } else {
    LOG_INFO << "[RegistrySettings] Failed to apply setting: " << setting_id
             ;
  }

  // If successful and this setting requires system refresh, call the refresh
  // function
  if (result && RequiresSystemRefresh(setting_id)) {
    LOG_INFO << "[RegistrySettings] Triggering wallpaper refresh for: "
              << setting_id;
    RefreshWallpaperSettings();
  }

  return result;
}

bool RegistrySettings::RequiresSystemRefresh(const std::string& setting_id) {
  auto& instance = RegistrySettings::GetInstance();
  const auto* def = FindDefinitionById(instance.registry_settings_, setting_id);
  if (def && def->requires_system_refresh) {
    LOG_INFO << "[RegistrySettings] Setting " << setting_id
              << " requires system refresh";
    return true;
  }
  return false;
}

void RegistrySettings::RefreshWallpaperSettings() {
  LOG_INFO << "[RegistrySettings] Calling SystemParametersInfo to refresh "
               "wallpaper settings"
           ;

  // Multiple approaches to refresh wallpaper settings for better compatibility

  // Method 1: Refresh desktop wallpaper using current registry settings
  BOOL result1 = SystemParametersInfoW(
    SPI_SETDESKWALLPAPER,  // Action: Set desktop wallpaper
    0,                     // uiParam: Not used for this action
    nullptr,  // pvParam: nullptr means use current registry settings
    SPIF_UPDATEINIFILE |
      SPIF_SENDCHANGE  // Flags: Update INI file and broadcast change
  );

  // Method 2: Force refresh of desktop - this helps with some Windows 11 issues
  BOOL result2 = SystemParametersInfoW(
    SPI_SETDESKPATTERN,  // Action: Set desktop pattern (forces refresh)
    0,                   // uiParam: Not used
    nullptr,             // pvParam: nullptr for default
    SPIF_SENDCHANGE      // Flags: Broadcast change
  );

  // Method 3: Refresh shell settings
  BOOL result3 = SystemParametersInfoW(
    SPI_SETDESKWALLPAPER,                 // Action: Set desktop wallpaper
    0,                                    // uiParam: Not used
    const_cast<LPWSTR>(L""),              // pvParam: Empty string to reset
    SPIF_UPDATEINIFILE | SPIF_SENDCHANGE  // Flags: Update and broadcast
  );

  // Method 4: Final wallpaper refresh
  BOOL result4 = SystemParametersInfoW(
    SPI_SETDESKWALLPAPER,                 // Action: Set desktop wallpaper
    0,                                    // uiParam: Not used
    nullptr,                              // pvParam: nullptr to use registry
    SPIF_UPDATEINIFILE | SPIF_SENDCHANGE  // Flags: Update and broadcast
  );

  if (result1 || result2 || result3 || result4) {
    LOG_INFO << "[RegistrySettings] Successfully refreshed wallpaper settings "
                 "(methods: "
              << result1 << "," << result2 << "," << result3 << "," << result4
              << ")";

    // Additional sleep to allow Windows to process the changes
    Sleep(500);

    // Force explorer refresh as a final step for Windows 11 compatibility
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, nullptr, SPIF_SENDCHANGE);
  } else {
    DWORD error = GetLastError();
    LOG_INFO
      << "[RegistrySettings] All wallpaper refresh methods failed. Last Error: "
      << error;
  }
}

OptimizationValue RegistrySettings::GetRegistryValue(
  const std::string& registry_key, const std::string& registry_value_name,
  const OptimizationValue& default_value) {
  if (registry_key.empty() || registry_value_name.empty()) {
    return default_value;
  }

  // Parse registry path
  HKEY targetHive;
  std::string actualKeyPath;
  bool useSpecificHive = false;

  if (registry_key.find("HKEY_") == 0) {
    if (ParseFullRegistryPath(registry_key, targetHive, actualKeyPath)) {
      useSpecificHive = true;
    } else {
      return default_value;
    }
  } else {
    actualKeyPath = registry_key;
  }

  auto readFromHive = [&](HKEY hive,
                          const std::string& keyPath) -> OptimizationValue {
    HKEY hKey;
    LONG result = RegOpenKeyExA(hive, keyPath.c_str(), 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
      return std::string("__KEY_NOT_FOUND__");
    }

    DWORD type, dataSize = 0;
    result = RegQueryValueExA(hKey, registry_value_name.c_str(), nullptr, &type,
                              nullptr, &dataSize);
    if (result != ERROR_SUCCESS) {
      RegCloseKey(hKey);
      return std::string("__KEY_NOT_FOUND__");
    }

    switch (type) {
      case REG_DWORD:
        {
          DWORD data = 0;
          DWORD actualSize = sizeof(data);
          result =
            RegQueryValueExA(hKey, registry_value_name.c_str(), nullptr, &type,
                             reinterpret_cast<BYTE*>(&data), &actualSize);
          RegCloseKey(hKey);

          if (result != ERROR_SUCCESS) {
            return default_value;
          }

          // Special handling for NetworkThrottlingIndex
          if (registry_value_name == "NetworkThrottlingIndex" &&
              data == 0xFFFFFFFF) {
            return std::numeric_limits<int>::max();
          }

          // Convert based on default value type
          if (std::holds_alternative<bool>(default_value)) {
            return data != 0;
          } else if (std::holds_alternative<int>(default_value)) {
            if (data > static_cast<DWORD>(std::numeric_limits<int>::max())) {
              return std::numeric_limits<int>::max();
            }
            return static_cast<int>(data);
          } else if (std::holds_alternative<double>(default_value)) {
            return static_cast<double>(data);
          } else {
            return std::to_string(data);
          }
        }

      case REG_SZ:
        {
          std::unique_ptr<char[]> buffer(new char[dataSize]);
          result =
            RegQueryValueExA(hKey, registry_value_name.c_str(), nullptr, &type,
                             reinterpret_cast<BYTE*>(buffer.get()), &dataSize);
          RegCloseKey(hKey);

          if (result != ERROR_SUCCESS) {
            return default_value;
          }

          std::string strValue(buffer.get());

          // Convert based on default value type
          if (std::holds_alternative<bool>(default_value)) {
            return (strValue == "true" || strValue == "1" || strValue == "yes");
          } else if (std::holds_alternative<int>(default_value)) {
            try {
              return std::stoi(strValue);
            } catch (...) {
              return default_value;
            }
          } else if (std::holds_alternative<double>(default_value)) {
            try {
              return std::stod(strValue);
            } catch (...) {
              return default_value;
            }
          } else {
            return strValue;
          }
        }

      default:
        RegCloseKey(hKey);
        return default_value;
    }
  };

  if (useSpecificHive) {
    return readFromHive(targetHive, actualKeyPath);
  } else {
    // Try different registry roots
    const HKEY rootKeys[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, HKEY_USERS};
    for (HKEY rootKey : rootKeys) {
      OptimizationValue result = readFromHive(rootKey, actualKeyPath);

      if (std::holds_alternative<std::string>(result)) {
        if (std::get<std::string>(result) != "__KEY_NOT_FOUND__") {
          return result;
        }
      } else {
        return result;
      }
    }

    return std::string("__KEY_NOT_FOUND__");
  }
}

}  // namespace registry
}  // namespace optimizations
