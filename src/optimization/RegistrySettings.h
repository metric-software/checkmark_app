/**
 * @file RegistrySettings.h
 * @brief Manages Windows registry optimization settings from hardcoded
 * definitions.
 *
 * This class handles loading registry optimization settings from
 * RegistrySettingsData (compiled into the binary), checking their current
 * values in the Windows registry, and creating OptimizationEntity objects
 * based on these settings.
 *
 * **Missing Registry Settings System:**
 *
 * Registry settings defined in code may not exist on the user's system. This
 * class supports a "user-directed creation" approach for missing settings with
 * enhanced security controls:
 *
 * - Missing registry settings are detected during CheckCurrentValues() and
 * stored internally
 * - CreateOptimizationEntities() creates entities for ALL settings (both
 * existing and missing)
 * - Missing settings are marked with a special flag so the UI can display them
 * differently
 * - By default, missing settings are shown in the UI as greyed-out and
 * non-interactive
 * - A blue "Add Setting" button is provided for each missing setting
 * - **SECURITY**: Only settings with "creation_allowed": true can be created by
 * users
 * - When the user clicks "Add Setting", CreateMissingRegistryPath() first
 * checks creation_allowed
 * - Settings with "creation_allowed": false will show an error and deny
 * registry creation
 * - The backup system records "NON_EXISTENT" as the original value for created
 * settings
 * - This allows proper restoration - settings created by the user can be
 * removed during restore
 *
 * This approach ensures the application never automatically modifies the
 * registry without explicit user consent and only allows creation of
 * pre-approved safe registry settings.
 *
 * **Setting Level System:**
 *
 * Registry settings are categorized by a "level" property that indicates their
 * importance and risk level. This allows for progressive disclosure and safety
 * controls:
 *
 * - Level 0 (Normal): Standard optimization settings that are well-tested and
 * safe
 *   * Treated exactly as before - no special handling
 *   * Default level if not specified in definitions
 *
 * - Level 1 (User Preference/Optional): Settings that are primarily user
 * preferences
 *   * Safe to modify but more about personal preference than performance
 *
 * - Level 2 (Experimental): Advanced settings that may have system-wide effects
 *   * Require extra caution. Not ment for all users.
 *
 * - Level 3 (Reserved): Currently unused.
 *
 * The level system is extensible - additional levels can be added in the future
 * without breaking existing functionality. Unknown levels default to 0
 * (Normal).
 */

#pragma once

#include <set>
#include <string>
#include <vector>

#include <Windows.h>

#include "OptimizationEntity.h"
#include "RegistrySettingsData.h"

namespace optimizations {
namespace registry {

/**
 * @brief Singleton class that loads and manages registry settings from static
 * definitions
 */
class RegistrySettings {
 public:
  static RegistrySettings& GetInstance();

  // Core functionality
  bool Initialize(const std::string& settings_file_path);
  bool CheckSettingsFileExists() const;
  bool CheckCurrentValues();
  std::vector<std::unique_ptr<settings::OptimizationEntity>> CreateOptimizationEntities()
    const;

  // Missing settings management
  bool CreateMissingRegistryPath(const std::string& setting_id,
                                 const OptimizationValue& value);
  bool IsSettingMissing(const std::string& setting_id) const;

  // Path utilities
  std::string GetSettingsFilePath() const;

  // Static registry operations (used by OptimizationEntity)
  static bool ParseFullRegistryPath(const std::string& full_path, HKEY& hive,
                                    std::string& key_path);
  static std::string GetHiveName(HKEY hive);
  static bool ApplyRegistryValue(const std::string& registry_key,
                                 const std::string& registry_value_name,
                                 const OptimizationValue& value,
                                 const OptimizationValue& default_value);
  static bool ApplyRegistryValue(const std::string& registry_key,
                                 const std::string& registry_value_name,
                                 const OptimizationValue& value,
                                 const OptimizationValue& default_value,
                                 const std::string& setting_id);
  static OptimizationValue GetRegistryValue(
    const std::string& registry_key, const std::string& registry_value_name,
    const OptimizationValue& default_value);
  static bool RequiresSystemRefresh(const std::string& setting_id);
  static void RefreshWallpaperSettings();

 private:
  RegistrySettings() = default;
  ~RegistrySettings() = default;
  RegistrySettings(const RegistrySettings&) = delete;
  RegistrySettings& operator=(const RegistrySettings&) = delete;

  // Internal data
  std::vector<RegistrySettingDefinition> registry_settings_;
  std::string settings_file_path_;
  std::set<std::string> missing_setting_ids_;
};

}  // namespace registry
}  // namespace optimizations
