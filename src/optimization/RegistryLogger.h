#pragma once

#include <fstream>
#include <mutex>
#include <string>

#include <Windows.h>

#include "OptimizationEntity.h"

namespace optimizations {
namespace registry {

/**
 * @brief Singleton class for logging all registry modifications
 *
 * This class ensures that every registry modification is logged with timestamps
 * to provide a complete audit trail of all changes made by the application.
 * The log file is saved to settings_backup/registry_log.txt and is persistent
 * across application sessions.
 */
class RegistryLogger {
 public:
  /**
   * @brief Get the singleton instance
   */
  static RegistryLogger& GetInstance();

  /**
   * @brief Initialize the logger with the application data directory
   * @param app_data_dir Path to the application data directory
   */
  void Initialize(const std::string& app_data_dir);

  /**
   * @brief Log a registry key creation attempt
   * @param hive Registry hive (e.g., HKEY_LOCAL_MACHINE)
   * @param key_path Full key path
   * @param success Whether the operation succeeded
   * @param error_code Windows error code if failed
   * @param setting_id Optional setting ID for context
   */
  void LogKeyCreation(HKEY hive, const std::string& key_path, bool success,
                      LONG error_code = 0, const std::string& setting_id = "");

  /**
   * @brief Log a registry value modification attempt
   * @param hive Registry hive
   * @param key_path Key path
   * @param value_name Value name
   * @param value New value being set
   * @param success Whether the operation succeeded
   * @param error_code Windows error code if failed
   * @param setting_id Optional setting ID for context
   */
  void LogValueModification(HKEY hive, const std::string& key_path,
                            const std::string& value_name,
                            const OptimizationValue& value, bool success,
                            LONG error_code = 0,
                            const std::string& setting_id = "");

  /**
   * @brief Log a registry value deletion attempt
   * @param hive Registry hive
   * @param key_path Key path
   * @param value_name Value name
   * @param success Whether the operation succeeded
   * @param error_code Windows error code if failed
   * @param setting_id Optional setting ID for context
   */
  void LogValueDeletion(HKEY hive, const std::string& key_path,
                        const std::string& value_name, bool success,
                        LONG error_code = 0,
                        const std::string& setting_id = "");

 private:
  RegistryLogger() = default;
  ~RegistryLogger();

  // Prevent copying
  RegistryLogger(const RegistryLogger&) = delete;
  RegistryLogger& operator=(const RegistryLogger&) = delete;

  /**
   * @brief Get current timestamp as formatted string
   */
  std::string GetTimestamp() const;

  /**
   * @brief Convert OptimizationValue to string for logging
   */
  std::string ValueToString(const OptimizationValue& value) const;

  /**
   * @brief Get hive name as string
   */
  std::string GetHiveName(HKEY hive) const;

  /**
   * @brief Write a log entry (thread-safe)
   */
  void WriteLogEntry(const std::string& entry);

  std::string log_file_path_;
  std::ofstream log_file_;
  std::mutex log_mutex_;
  bool initialized_ = false;
};

}  // namespace registry
}  // namespace optimizations
