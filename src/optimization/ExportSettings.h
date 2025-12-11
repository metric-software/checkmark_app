/**
 * @file ExportSettings.h
 * @brief Export system for current optimization settings
 *
 * This module provides functionality to export the current state of all
 * optimization settings to JSON format. Unlike the backup system which
 * preserves original values, this system captures the actual current values
 * from the system at the time of export.
 *
 * The exported JSON contains:
 * - Current values of all registry settings
 * - Current Rust configuration settings
 * - Current NVIDIA settings
 * - Current Visual Effects profile
 * - Current Power Plan
 * - Metadata about the export (timestamp, system info, etc.)
 *
 * Missing or inaccessible settings are included but marked appropriately.
 *
 * This system is designed to be modular and can be extended for various use
 * cases:
 * - Benchmark run optimization snapshots
 * - System state documentation
 * - Configuration sharing/comparison
 * - Troubleshooting and analysis
 */

#pragma once

#include <string>

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

// Forward declarations
namespace optimizations {
namespace settings {
class OptimizationEntity;
}
using OptimizationEntity = settings::OptimizationEntity;
}  // namespace optimizations

// Include OptimizationEntity.h to get OptimizationValue definition
#include "OptimizationEntity.h"

namespace optimizations {

/**
 * @brief Result of an export operation
 */
struct ExportResult {
  bool success = false;
  std::string error_message;
  QString exported_file_path;
  int total_settings = 0;
  int exported_settings = 0;
  int missing_settings = 0;
  int error_settings = 0;
};

/**
 * @brief Export manager for optimization settings
 */
class ExportSettings {
 public:
  /**
   * @brief Export all current optimization settings to a JSON file
   * @param file_path Path where to save the JSON file
   * @param include_metadata Whether to include system metadata in the export
   * @return Result of the export operation
   */
  static ExportResult ExportAllSettings(const std::string& file_path,
                                        bool include_metadata = true);

  /**
   * @brief Export all current optimization settings to a JSON object
   * @param include_metadata Whether to include system metadata in the export
   * @return JSON object containing all current settings
   */
  static QJsonObject ExportAllSettingsToJson(bool include_metadata = true);

  /**
   * @brief Export registry settings to JSON
   * @return JSON object containing current registry settings
   */
  static QJsonObject ExportRegistrySettings();

  /**
   * @brief Export Rust configuration settings to JSON
   * @return JSON object containing current Rust settings
   */
  static QJsonObject ExportRustSettings();

  /**
   * @brief Export NVIDIA settings to JSON
   * @return JSON object containing current NVIDIA settings
   */
  static QJsonObject ExportNvidiaSettings();

  /**
   * @brief Export Visual Effects settings to JSON
   * @return JSON object containing current Visual Effects profile
   */
  static QJsonObject ExportVisualEffectsSettings();

  /**
   * @brief Export Power Plan settings to JSON
   * @return JSON object containing current Power Plan
   */
  static QJsonObject ExportPowerPlanSettings();

  /**
   * @brief Get system metadata for inclusion in exports
   * @return JSON object containing system information
   */
  static QJsonObject GetSystemMetadata();

 private:
  /**
   * @brief Helper to safely get current value from an optimization entity
   * @param entity The optimization entity to read from
   * @return JSON value representing the current setting value
   */
  static QJsonValue SafeGetCurrentValue(
    const settings::OptimizationEntity* entity);

  /**
   * @brief Convert OptimizationValue to JSON
   * @param value The optimization value to convert
   * @return JSON representation of the value
   */
  static QJsonValue OptimizationValueToJson(const OptimizationValue& value);
};

}  // namespace optimizations
