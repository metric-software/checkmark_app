/**
 * @file RegistryBackupUtility.h
 * @brief Comprehensive Windows Registry Backup and Restore C++ Utility
 *
 * This class provides secure registry backup and restore functionality for the
 * Checkmark application. It offers both full and selective registry operations
 * with extensive error handling and validation, all running silently in the
 * background.
 *
 * Features:
 * - Full registry export to .reg files
 * - Selective hive export/import
 * - Pre-import backup creation
 * - Comprehensive validation
 * - Detailed logging via std::cout
 * - Safe import with rollback capability
 * - Silent operation (no console windows)
 * - Integration with existing BackupManager
 *
 * Usage Examples:
 * ```cpp
 * auto& registryBackup = RegistryBackupUtility::GetInstance();
 * registryBackup.Initialize("C:\\Checkmark\\Registry_Backups");
 *
 * // Create full registry backup
 * if (registryBackup.ExportFullRegistry("backup.reg")) {
 *     std::cout << "Full registry backup created successfully" << std::endl;
 * }
 *
 * // Import with automatic backup
 * if (registryBackup.ImportRegistryWithBackup("settings.reg")) {
 *     std::cout << "Registry import completed with safety backup" << std::endl;
 * }
 * ```
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QDateTime>
#include <QDir>
#include <QString>
#include <Windows.h>

namespace optimizations {
namespace registry {

/**
 * @brief Registry backup operation status
 */
enum class RegistryBackupStatus {
  Success,
  FileNotFound,
  InvalidFormat,
  AccessDenied,
  CorruptedBackup,
  InsufficientSpace,
  UnknownError
};

/**
 * @brief Types of registry backup operations
 */
enum class RegistryBackupType {
  Full,
  CurrentUser,
  LocalMachine,
  ClassesRoot,
  Users,
  CurrentConfig,
  Custom
};

/**
 * @brief Registry backup operation result
 */
struct RegistryBackupResult {
  RegistryBackupStatus status;
  std::string message;
  std::string backup_path;
  size_t file_size_mb = 0;

  bool IsSuccess() const { return status == RegistryBackupStatus::Success; }
};

/**
 * @brief Registry backup file information
 */
struct RegistryBackupInfo {
  std::string file_path;
  std::string file_name;
  size_t file_size_mb;
  QDateTime creation_time;
  QDateTime last_modified;
  bool is_valid;
};

/**
 * @brief Singleton utility class for Windows Registry backup and restore
 * operations
 *
 * This class provides comprehensive registry backup functionality that runs
 * silently in the background without showing console windows. It integrates
 * with the existing BackupManager and follows the application's logging
 * patterns.
 */
class RegistryBackupUtility {
 public:
  /**
   * @brief Get singleton instance
   * @return Reference to the RegistryBackupUtility instance
   */
  static RegistryBackupUtility& GetInstance();

  /**
   * @brief Initialize the registry backup utility
   * @param backup_directory Directory where registry backups will be stored
   * @param max_backup_files Maximum number of backup files to retain (default:
   * 10)
   * @return True if initialization successful
   */
  bool Initialize(const std::string& backup_directory,
                  int max_backup_files = 10);

  /**
   * @brief Check if the utility is initialized
   * @return True if initialized
   */
  bool IsInitialized() const { return initialized_; }

  // Registry Export Functions

  /**
   * @brief Export the complete Windows registry to a .reg file
   * @param output_filename Output filename (will be placed in backup directory)
   * @param include_user_hives Whether to include all user hives (can be very
   * large)
   * @return RegistryBackupResult with operation status and details
   */
  RegistryBackupResult ExportFullRegistry(const std::string& output_filename,
                                          bool include_user_hives = false);

  /**
   * @brief Export a specific registry hive to a .reg file
   * @param hive The registry hive to export
   * @param output_filename Output filename (will be placed in backup directory)
   * @return RegistryBackupResult with operation status and details
   */
  RegistryBackupResult ExportRegistryHive(RegistryBackupType hive,
                                          const std::string& output_filename);

  /**
   * @brief Export a specific registry key and its subkeys
   * @param key_path Full registry path (e.g.,
   * "HKEY_CURRENT_USER\\Software\\MyApp")
   * @param output_filename Output filename (will be placed in backup directory)
   * @return RegistryBackupResult with operation status and details
   */
  RegistryBackupResult ExportRegistryKey(const std::string& key_path,
                                         const std::string& output_filename);

  /**
   * @brief Start a registry backup operation using predefined types
   * @param backup_type Type of backup to perform
   * @param output_filename Optional custom output filename
   * @param custom_hives For custom backup type, specify which hives to include
   * @return RegistryBackupResult with operation status and details
   */
  RegistryBackupResult StartRegistryBackup(
    RegistryBackupType backup_type, const std::string& output_filename = "",
    const std::vector<RegistryBackupType>& custom_hives = {});

  // Registry Import Functions

  /**
   * @brief Import registry settings from a .reg file
   * @param file_path Path to the .reg file to import (can be relative to backup
   * directory)
   * @param create_backup Whether to create a backup before importing
   * @param validate_first Whether to validate the file before importing
   * @return RegistryBackupResult with operation status and details
   */
  RegistryBackupResult ImportRegistryFile(const std::string& file_path,
                                          bool create_backup = true,
                                          bool validate_first = true);

  /**
   * @brief Import registry file with automatic backup and rollback capability
   * @param import_path Path to the .reg file to import
   * @param custom_backup_path Optional custom path for the safety backup
   * @return RegistryBackupResult with operation status and details
   */
  RegistryBackupResult ImportRegistryWithBackup(
    const std::string& import_path, const std::string& custom_backup_path = "");

  /**
   * @brief Restore registry from a backup file with additional safety checks
   * @param backup_path Path to the backup file to restore from
   * @param create_safety_backup Whether to create an additional backup before
   * restoration
   * @return RegistryBackupResult with operation status and details
   */
  RegistryBackupResult RestoreRegistryFromBackup(
    const std::string& backup_path, bool create_safety_backup = true);

  // Utility Functions

  /**
   * @brief Get information about existing registry backup files
   * @return Vector of RegistryBackupInfo for all backup files
   */
  std::vector<RegistryBackupInfo> GetRegistryBackupInfo() const;

  /**
   * @brief Test the integrity of a registry backup file
   * @param backup_path Path to the backup file to test
   * @return True if the backup file is valid and not corrupted
   */
  bool TestRegistryBackupIntegrity(const std::string& backup_path) const;

  /**
   * @brief Validate a registry file format and basic structure
   * @param file_path Path to the registry file to validate
   * @return True if the file is a valid registry file
   */
  bool ValidateRegistryFile(const std::string& file_path) const;

  /**
   * @brief Clean up old backup files, keeping only the most recent ones
   * @return Number of files removed
   */
  int CleanupOldBackups();

  /**
   * @brief Get the full path to the backup directory
   * @return Backup directory path
   */
  std::string GetBackupDirectory() const { return backup_directory_; }

  /**
   * @brief Convert a RegistryBackupType to its corresponding HKEY string
   * @param type The backup type
   * @return HKEY string (e.g., "HKEY_CURRENT_USER")
   */
  static std::string BackupTypeToHiveString(RegistryBackupType type);

  /**
   * @brief Generate a timestamped backup filename
   * @param base_name Base name for the file
   * @param extension File extension (default: ".reg")
   * @return Timestamped filename
   */
  static std::string GenerateBackupFileName(
    const std::string& base_name, const std::string& extension = ".reg");

 private:
  RegistryBackupUtility();
  ~RegistryBackupUtility() = default;

  // Delete copy constructor and assignment operator
  RegistryBackupUtility(const RegistryBackupUtility&) = delete;
  RegistryBackupUtility& operator=(const RegistryBackupUtility&) = delete;

  // Internal helper functions

  /**
   * @brief Execute a registry command silently using reg.exe
   * @param command Registry command (export, import)
   * @param arguments Command arguments
   * @param output_path Optional output path for logging
   * @return True if command executed successfully
   */
  bool ExecuteRegistryCommand(const std::string& command,
                              const std::vector<std::string>& arguments,
                              const std::string& output_path = "") const;

  /**
   * @brief Execute registry export using PowerShell and regedit.exe
   * @param output_path Full path where the registry export should be saved
   * @return True if export completed successfully
   */
  bool ExecuteRegistryExportViaPowerShell(const std::string& output_path) const;

  /**
   * @brief Execute registry import using PowerShell and regedit.exe
   * @param file_path Full path to the registry file to import
   * @return True if import completed successfully
   */
  bool ExecuteRegistryImportViaPowerShell(const std::string& file_path) const;

  /**
   * @brief Ensure the backup directory exists and is writable
   * @return True if directory is ready for use
   */
  bool EnsureBackupDirectoryExists() const;

  /**
   * @brief Get the full path for a backup file
   * @param filename Filename (can be relative or absolute)
   * @return Full path to the backup file
   */
  std::string GetFullBackupPath(const std::string& filename) const;

  /**
   * @brief Check if we have administrator privileges
   * @return True if running as administrator
   */
  bool HasAdministratorPrivileges() const;

  /**
   * @brief Log a registry operation message
   * @param message Message to log
   * @param is_error Whether this is an error message
   */
  void LogRegistryOperation(const std::string& message,
                            bool is_error = false) const;

  /**
   * @brief Get file size in megabytes
   * @param file_path Path to the file
   * @return File size in MB, or 0 if file doesn't exist
   */
  size_t GetFileSizeMB(const std::string& file_path) const;

  /**
   * @brief Create a RegistryBackupResult for an error condition
   * @param status Error status
   * @param message Error message
   * @return RegistryBackupResult with error information
   */
  RegistryBackupResult CreateErrorResult(RegistryBackupStatus status,
                                         const std::string& message) const;

  /**
   * @brief Create a RegistryBackupResult for a successful operation
   * @param message Success message
   * @param backup_path Path to the created backup
   * @return RegistryBackupResult with success information
   */
  RegistryBackupResult CreateSuccessResult(
    const std::string& message, const std::string& backup_path) const;

  // Member variables
  bool initialized_ = false;
  std::string backup_directory_;
  int max_backup_files_ = 10;

  // Registry hive mappings
  static const std::map<RegistryBackupType, std::string> hive_strings_;
};

}  // namespace registry
}  // namespace optimizations
