/**
 * @file ImportSettings.h
 * @brief Import system for loading optimization settings from exported JSON
 * files
 *
 * This module provides functionality to import optimization settings from JSON
 * files created by the ExportSettings system. It can load settings profiles and
 * apply them to the UI without immediately changing the system - users must
 * still click "Apply" to actually modify system settings.
 *
 * The import system handles:
 * - Loading and parsing exported JSON files
 * - Mapping imported values to current optimization entities
 * - Handling missing or incompatible settings gracefully
 * - Providing detailed import results and statistics
 * - Supporting partial imports when some settings are unavailable
 *
 * Import flow:
 * 1. Load JSON file created by ExportSettings
 * 2. Parse and validate the structure
 * 3. Map values to current OptimizationEntities by ID
 * 4. Return importable values for UI application
 * 5. UI updates widgets without applying to system
 * 6. User clicks "Apply" to actually change system settings
 */

#pragma once

#include <string>

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QStringList>
#include <QVariant>

#include "OptimizationEntity.h"  // Include to get OptimizationValue type

// Forward declarations
namespace optimizations {
namespace settings {
class OptimizationEntity;
}
using OptimizationEntity = settings::OptimizationEntity;
}  // namespace optimizations

namespace optimizations {

/**
 * @brief Information about an imported setting
 */
struct ImportedSetting {
  QString id;
  QVariant value;
  QString status;  // "imported", "missing", "error", "incompatible"
  QString errorMessage;
};

/**
 * @brief Result of an import operation
 */
struct ImportResult {
  bool success = false;
  std::string error_message;
  QString imported_file_path;

  // Statistics
  int total_settings = 0;
  int imported_settings = 0;
  int missing_settings = 0;
  int error_settings = 0;
  int incompatible_settings = 0;

  // Imported values organized by category for UI application
  QMap<QString, QList<ImportedSetting>> imported_values;

  // Lists of problematic settings for user feedback
  QStringList missing_setting_ids;
  QStringList error_setting_ids;
  QStringList incompatible_setting_ids;

  // Metadata from the imported file
  QJsonObject metadata;
};

/**
 * @brief Import manager for optimization settings
 */
class ImportSettings {
 public:
  /**
   * @brief Import settings from a JSON file created by ExportSettings
   * @param file_path Path to the JSON file to import
   * @param validate_only If true, only validate without preparing import data
   * @return Result of the import operation with detailed statistics
   */
  static ImportResult ImportSettingsFromFile(const std::string& file_path,
                                             bool validate_only = false);

  /**
   * @brief Import settings from a JSON object
   * @param json_obj JSON object containing exported settings
   * @param validate_only If true, only validate without preparing import data
   * @return Result of the import operation
   */
  static ImportResult ImportSettingsFromJson(const QJsonObject& json_obj,
                                             bool validate_only = false);

  /**
   * @brief Get a list of available profile files in a directory
   * @param directory_path Path to search for profile files
   * @return List of profile file paths found
   */
  static QStringList GetAvailableProfiles(const std::string& directory_path);

  /**
   * @brief Get metadata from a profile file without full import
   * @param file_path Path to the profile file
   * @return Metadata object or empty if failed
   */
  static QJsonObject GetProfileMetadata(const std::string& file_path);

  /**
   * @brief Validate that a file is a valid exported settings file
   * @param file_path Path to the file to validate
   * @return True if valid, false otherwise
   */
  static bool ValidateProfileFile(const std::string& file_path);

 private:
  /**
   * @brief Import registry settings from JSON
   * @param registry_obj JSON object containing registry settings
   * @param result Import result to populate
   */
  static void ImportRegistrySettings(const QJsonObject& registry_obj,
                                     ImportResult& result);

  /**
   * @brief Import Rust settings from JSON
   * @param rust_obj JSON object containing Rust settings
   * @param result Import result to populate
   */
  static void ImportRustSettings(const QJsonObject& rust_obj,
                                 ImportResult& result);

  /**
   * @brief Import NVIDIA settings from JSON
   * @param nvidia_obj JSON object containing NVIDIA settings
   * @param result Import result to populate
   */
  static void ImportNvidiaSettings(const QJsonObject& nvidia_obj,
                                   ImportResult& result);

  /**
   * @brief Import Visual Effects settings from JSON
   * @param ve_obj JSON object containing Visual Effects settings
   * @param result Import result to populate
   */
  static void ImportVisualEffectsSettings(const QJsonObject& ve_obj,
                                          ImportResult& result);

  /**
   * @brief Import Power Plan settings from JSON
   * @param pp_obj JSON object containing Power Plan settings
   * @param result Import result to populate
   */
  static void ImportPowerPlanSettings(const QJsonObject& pp_obj,
                                      ImportResult& result);

  /**
   * @brief Helper to convert JSON value to OptimizationValue
   * @param json_value JSON value to convert
   * @return Converted OptimizationValue
   */
  static OptimizationValue JsonToOptimizationValue(
    const QJsonValue& json_value);

  /**
   * @brief Helper to convert OptimizationValue to QVariant for UI
   * @param opt_value OptimizationValue to convert
   * @return QVariant for UI usage
   */
  static QVariant OptimizationValueToQVariant(
    const OptimizationValue& opt_value);

  /**
   * @brief Helper to validate JSON structure
   * @param json_obj JSON object to validate
   * @return True if structure is valid
   */
  static bool ValidateJsonStructure(const QJsonObject& json_obj);

  /**
   * @brief Helper to process a single setting import
   * @param setting_id ID of the setting
   * @param json_value Value from JSON
   * @param category Category name for organization
   * @param result Import result to update
   */
  static void ProcessSingleSetting(const QString& setting_id,
                                   const QJsonValue& json_value,
                                   const QString& category,
                                   ImportResult& result);
};

}  // namespace optimizations
