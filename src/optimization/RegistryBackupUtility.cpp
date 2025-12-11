/**
 * @file RegistryBackupUtility.cpp
 * @brief Implementation of the comprehensive Windows Registry Backup and
 * Restore C++ Utility
 */

#include "RegistryBackupUtility.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

namespace fs = std::filesystem;

namespace optimizations {
namespace registry {

//------------------------------------------------------------------------------
// Static member initialization
//------------------------------------------------------------------------------

const std::map<RegistryBackupType, std::string>
  RegistryBackupUtility::hive_strings_ = {
    {RegistryBackupType::CurrentUser, "HKEY_CURRENT_USER"},
    {RegistryBackupType::LocalMachine, "HKEY_LOCAL_MACHINE"},
    {RegistryBackupType::ClassesRoot, "HKEY_CLASSES_ROOT"},
    {RegistryBackupType::Users, "HKEY_USERS"},
    {RegistryBackupType::CurrentConfig, "HKEY_CURRENT_CONFIG"}};

//------------------------------------------------------------------------------
// Singleton Implementation
//------------------------------------------------------------------------------

RegistryBackupUtility& RegistryBackupUtility::GetInstance() {
  static RegistryBackupUtility instance;
  return instance;
}

RegistryBackupUtility::RegistryBackupUtility() {
  // Constructor is private for singleton pattern
}

//------------------------------------------------------------------------------
// Public Interface
//------------------------------------------------------------------------------

bool RegistryBackupUtility::Initialize(const std::string& backup_directory,
                                       int max_backup_files) {
  if (initialized_) {
    LogRegistryOperation("Registry backup utility already initialized");
    return true;
  }

  backup_directory_ = backup_directory;
  max_backup_files_ = max_backup_files;

  // Check administrator privileges
  if (!HasAdministratorPrivileges()) {
    LogRegistryOperation(
      "ERROR: Administrator privileges required for registry operations", true);
    return false;
  }

  // Ensure backup directory exists
  if (!EnsureBackupDirectoryExists()) {
    LogRegistryOperation(
      "ERROR: Failed to create or access backup directory: " +
        backup_directory_,
      true);
    return false;
  }

  initialized_ = true;
  LogRegistryOperation("Registry backup utility initialized successfully");
  LogRegistryOperation("Backup directory: " + backup_directory_);
  LogRegistryOperation("Max backup files: " +
                       std::to_string(max_backup_files_));

  return true;
}

RegistryBackupResult RegistryBackupUtility::ExportFullRegistry(
  const std::string& output_filename, bool include_user_hives) {
  if (!initialized_) {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Registry backup utility not initialized");
  }

  std::string output_file = output_filename.empty()
                              ? GenerateBackupFileName("registry_full")
                              : output_filename;
  std::string full_output_path = GetFullBackupPath(output_file);

  LogRegistryOperation("Starting full registry export to: " + full_output_path);
  LogRegistryOperation(
    "Using Registry Editor built-in export functionality (regedit /e)");

  try {
    // Use PowerShell to call regedit.exe with export parameter
    // This replicates the "File > Export > All" functionality from Registry
    // Editor
    bool success = ExecuteRegistryExportViaPowerShell(full_output_path);

    if (success) {
      size_t file_size = GetFileSizeMB(full_output_path);
      LogRegistryOperation("Full registry export completed successfully");
      LogRegistryOperation("Export size: " + std::to_string(file_size) + " MB");

      CleanupOldBackups();
      return CreateSuccessResult("Full registry export completed successfully",
                                 full_output_path);
    } else {
      return CreateErrorResult(RegistryBackupStatus::UnknownError,
                               "Full registry export failed");
    }

  } catch (const std::exception& e) {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Critical error during full registry export: " +
                               std::string(e.what()));
  }
}

RegistryBackupResult RegistryBackupUtility::ExportRegistryHive(
  RegistryBackupType hive, const std::string& output_filename) {
  if (!initialized_) {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Registry backup utility not initialized");
  }

  if (hive == RegistryBackupType::Full || hive == RegistryBackupType::Custom) {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Invalid hive type for single hive export");
  }

  std::string hive_string = BackupTypeToHiveString(hive);
  std::string output_file =
    output_filename.empty() ? GenerateBackupFileName("registry_" + hive_string)
                            : output_filename;
  std::string full_output_path = GetFullBackupPath(output_file);

  LogRegistryOperation("Starting export of registry hive: " + hive_string);

  std::vector<std::string> args = {hive_string, full_output_path, "/y"};
  if (ExecuteRegistryCommand("export", args, full_output_path)) {
    size_t file_size = GetFileSizeMB(full_output_path);
    LogRegistryOperation("Successfully exported " + hive_string + " to " +
                         full_output_path);
    LogRegistryOperation("Export size: " + std::to_string(file_size) + " MB");

    CleanupOldBackups();
    return CreateSuccessResult("Successfully exported " + hive_string,
                               full_output_path);
  } else {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Failed to export " + hive_string);
  }
}

RegistryBackupResult RegistryBackupUtility::ExportRegistryKey(
  const std::string& key_path, const std::string& output_filename) {
  if (!initialized_) {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Registry backup utility not initialized");
  }

  std::string output_file = output_filename.empty()
                              ? GenerateBackupFileName("registry_key")
                              : output_filename;
  std::string full_output_path = GetFullBackupPath(output_file);

  LogRegistryOperation("Starting export of registry key: " + key_path);

  std::vector<std::string> args = {key_path, full_output_path, "/y"};
  if (ExecuteRegistryCommand("export", args, full_output_path)) {
    LogRegistryOperation("Successfully exported " + key_path + " to " +
                         full_output_path);
    CleanupOldBackups();
    return CreateSuccessResult("Successfully exported registry key",
                               full_output_path);
  } else {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Failed to export " + key_path);
  }
}

RegistryBackupResult RegistryBackupUtility::StartRegistryBackup(
  RegistryBackupType backup_type, const std::string& output_filename,
  const std::vector<RegistryBackupType>& custom_hives) {
  if (!initialized_) {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Registry backup utility not initialized");
  }

  LogRegistryOperation("Starting registry backup operation: " +
                       std::to_string(static_cast<int>(backup_type)));

  std::string output_file = output_filename;
  if (output_file.empty()) {
    std::string backup_name;
    switch (backup_type) {
      case RegistryBackupType::Full:
        backup_name = "registry_full";
        break;
      case RegistryBackupType::CurrentUser:
        backup_name = "registry_currentuser";
        break;
      case RegistryBackupType::LocalMachine:
        backup_name = "registry_localmachine";
        break;
      case RegistryBackupType::Custom:
        backup_name = "registry_custom";
        break;
      default:
        backup_name = "registry_" + BackupTypeToHiveString(backup_type);
        break;
    }
    output_file = GenerateBackupFileName(backup_name);
  }

  RegistryBackupResult result;

  switch (backup_type) {
    case RegistryBackupType::Full:
      result = ExportFullRegistry(output_file);
      break;

    case RegistryBackupType::Custom:
      if (custom_hives.empty()) {
        return CreateErrorResult(RegistryBackupStatus::UnknownError,
                                 "No custom hives specified for custom backup");
      }

      // Export each hive to a separate file
      {
        bool all_success = true;
        std::vector<std::string> created_files;

        for (const auto& hive : custom_hives) {
          std::string hive_output = output_file;
          // Modify filename to include hive name
          size_t dot_pos = hive_output.find_last_of('.');
          if (dot_pos != std::string::npos) {
            hive_output = hive_output.substr(0, dot_pos) + "_" +
                          BackupTypeToHiveString(hive) +
                          hive_output.substr(dot_pos);
          } else {
            hive_output += "_" + BackupTypeToHiveString(hive);
          }

          auto hive_result = ExportRegistryHive(hive, hive_output);
          if (hive_result.IsSuccess()) {
            created_files.push_back(hive_result.backup_path);
          } else {
            all_success = false;
          }
        }

        if (all_success) {
          result =
            CreateSuccessResult("Custom registry backup completed successfully",
                                created_files.empty() ? "" : created_files[0]);
        } else {
          result =
            CreateErrorResult(RegistryBackupStatus::UnknownError,
                              "Custom registry backup completed with errors");
        }
      }
      break;

    default:
      result = ExportRegistryHive(backup_type, output_file);
      break;
  }

  if (result.IsSuccess()) {
    LogRegistryOperation("Registry backup completed successfully");
  }

  return result;
}

RegistryBackupResult RegistryBackupUtility::ImportRegistryFile(
  const std::string& file_path, bool create_backup, bool validate_first) {
  if (!initialized_) {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Registry backup utility not initialized");
  }

  std::string full_file_path = GetFullBackupPath(file_path);

  LogRegistryOperation("Starting registry import from: " + full_file_path);

  // Check if file exists
  if (!fs::exists(full_file_path)) {
    return CreateErrorResult(RegistryBackupStatus::FileNotFound,
                             "Registry file not found: " + full_file_path);
  }

  // Validate file if requested
  if (validate_first) {
    if (!ValidateRegistryFile(full_file_path)) {
      return CreateErrorResult(
        RegistryBackupStatus::InvalidFormat,
        "Registry file validation failed, aborting import");
    }
  }

  // Create backup if requested
  if (create_backup) {
    std::string backup_filename = GenerateBackupFileName("pre_import_backup");
    LogRegistryOperation("Creating pre-import backup: " + backup_filename);

    auto backup_result = ExportFullRegistry(backup_filename);
    if (!backup_result.IsSuccess()) {
      return CreateErrorResult(
        RegistryBackupStatus::UnknownError,
        "Failed to create pre-import backup, aborting import");
    }
  }

  // Perform the import using PowerShell + regedit.exe
  LogRegistryOperation("Importing registry file using regedit.exe...");
  bool success = ExecuteRegistryImportViaPowerShell(full_file_path);

  if (success) {
    LogRegistryOperation("Registry import completed successfully");
    return CreateSuccessResult("Registry import completed successfully",
                               full_file_path);
  } else {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Registry import failed");
  }
}

RegistryBackupResult RegistryBackupUtility::ImportRegistryWithBackup(
  const std::string& import_path, const std::string& custom_backup_path) {
  if (!initialized_) {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Registry backup utility not initialized");
  }

  LogRegistryOperation("Starting safe registry import with backup");

  std::string backup_path = custom_backup_path;
  if (backup_path.empty()) {
    backup_path = GenerateBackupFileName("safe_import_backup");
  }

  LogRegistryOperation("Creating safety backup before import...");

  // Create full registry backup
  auto backup_result = ExportFullRegistry(backup_path);
  if (!backup_result.IsSuccess()) {
    return CreateErrorResult(
      RegistryBackupStatus::UnknownError,
      "Failed to create safety backup, import aborted for security");
  }

  LogRegistryOperation("Safety backup created: " + backup_result.backup_path);

  // Validate import file
  std::string full_import_path = GetFullBackupPath(import_path);
  if (!ValidateRegistryFile(full_import_path)) {
    return CreateErrorResult(RegistryBackupStatus::InvalidFormat,
                             "Import file validation failed");
  }

  // Perform import
  auto import_result = ImportRegistryFile(import_path, false, false);

  if (import_result.IsSuccess()) {
    LogRegistryOperation("Registry import completed successfully");
    LogRegistryOperation("Rollback file available at: " +
                         backup_result.backup_path);
    CleanupOldBackups();
    return CreateSuccessResult(
      "Registry import completed successfully with safety backup",
      import_result.backup_path);
  } else {
    LogRegistryOperation("Registry import failed, rollback file available: " +
                           backup_result.backup_path,
                         true);
    return import_result;
  }
}

RegistryBackupResult RegistryBackupUtility::RestoreRegistryFromBackup(
  const std::string& backup_path, bool create_safety_backup) {
  if (!initialized_) {
    return CreateErrorResult(RegistryBackupStatus::UnknownError,
                             "Registry backup utility not initialized");
  }

  std::string full_backup_path = GetFullBackupPath(backup_path);
  LogRegistryOperation("Starting registry restoration from backup: " +
                       full_backup_path);

  // Test backup integrity first
  if (!TestRegistryBackupIntegrity(full_backup_path)) {
    return CreateErrorResult(
      RegistryBackupStatus::CorruptedBackup,
      "Backup integrity check failed, restoration aborted");
  }

  // Create safety backup if requested
  if (create_safety_backup) {
    std::string safety_backup_path =
      GenerateBackupFileName("pre_restore_backup");
    LogRegistryOperation("Creating safety backup before restoration...");

    auto safety_result = ExportFullRegistry(safety_backup_path);
    if (!safety_result.IsSuccess()) {
      return CreateErrorResult(
        RegistryBackupStatus::UnknownError,
        "Failed to create safety backup, restoration aborted");
    }

    LogRegistryOperation("Safety backup created: " + safety_result.backup_path);
  }

  // Perform restoration
  return ImportRegistryFile(backup_path, false, false);
}

std::vector<RegistryBackupInfo> RegistryBackupUtility::GetRegistryBackupInfo()
  const {
  std::vector<RegistryBackupInfo> backup_info;

  if (!initialized_ || backup_directory_.empty()) {
    LogRegistryOperation(
      "No backup directory configured or directory does not exist", true);
    return backup_info;
  }

  try {
    QDir backup_dir(QString::fromStdString(backup_directory_));
    if (!backup_dir.exists()) {
      return backup_info;
    }

    QStringList filters;
    filters << "*.reg";
    backup_dir.setNameFilters(filters);

    QFileInfoList backup_files =
      backup_dir.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);

    LogRegistryOperation("Found " + std::to_string(backup_files.size()) +
                         " registry backup files:");

    for (const QFileInfo& file_info : backup_files) {
      RegistryBackupInfo info;
      info.file_path = file_info.absoluteFilePath().toStdString();
      info.file_name = file_info.fileName().toStdString();
      info.file_size_mb = static_cast<size_t>(file_info.size() / (1024 * 1024));
      info.creation_time = file_info.birthTime();
      info.last_modified = file_info.lastModified();
      info.is_valid = ValidateRegistryFile(info.file_path);

      backup_info.push_back(info);

      LogRegistryOperation("  " + info.file_name + " - " +
                           std::to_string(info.file_size_mb) + " MB - " +
                           info.last_modified.toString().toStdString());
    }

  } catch (const std::exception& e) {
    LogRegistryOperation("Error getting backup info: " + std::string(e.what()),
                         true);
  }

  return backup_info;
}

bool RegistryBackupUtility::TestRegistryBackupIntegrity(
  const std::string& backup_path) const {
  std::string full_path = GetFullBackupPath(backup_path);
  LogRegistryOperation("Testing integrity of backup file: " + full_path);

  try {
    if (!fs::exists(full_path)) {
      LogRegistryOperation("Backup file not found: " + full_path, true);
      return false;
    }

    // Basic file validation
    if (!ValidateRegistryFile(full_path)) {
      return false;
    }

    // Check file size (should not be empty)
    auto file_size = fs::file_size(full_path);
    if (file_size <
        1000) {  // Less than 1KB is suspicious for a registry backup
      LogRegistryOperation("Backup file seems too small (" +
                             std::to_string(file_size) +
                             " bytes), may be corrupted",
                           true);
      return false;
    }

    // Check for common registry structure
    std::ifstream file(full_path);
    if (!file.is_open()) {
      LogRegistryOperation("Cannot open backup file for reading", true);
      return false;
    }

    std::string line;
    bool has_registry_keys = false;
    int line_count = 0;

    while (std::getline(file, line) && line_count < 50) {
      line_count++;
      if (line.find("[HKEY_") == 0) {
        has_registry_keys = true;
        break;
      }
    }

    if (!has_registry_keys) {
      LogRegistryOperation(
        "No registry keys found in backup file, may be corrupted", true);
      return false;
    }

    LogRegistryOperation("Backup file integrity check passed");
    return true;

  } catch (const std::exception& e) {
    LogRegistryOperation(
      "Error during integrity check: " + std::string(e.what()), true);
    return false;
  }
}

bool RegistryBackupUtility::ValidateRegistryFile(
  const std::string& file_path) const {
  std::string full_path = GetFullBackupPath(file_path);

  try {
    if (!fs::exists(full_path)) {
      LogRegistryOperation("Registry file not found: " + full_path, true);
      return false;
    }

    // Check file extension
    if (full_path.substr(full_path.find_last_of('.')) != ".reg") {
      LogRegistryOperation("File does not have .reg extension: " + full_path,
                           true);
    }

    // Read first few lines to check format
    std::ifstream file(full_path);
    if (!file.is_open()) {
      LogRegistryOperation("Cannot open file for validation: " + full_path,
                           true);
      return false;
    }

    std::string first_line;
    if (std::getline(file, first_line)) {
      if (first_line.find("Windows Registry Editor Version") ==
          std::string::npos) {
        LogRegistryOperation("Invalid registry file header: " + full_path,
                             true);
        return false;
      }
    } else {
      LogRegistryOperation("Empty registry file: " + full_path, true);
      return false;
    }

    LogRegistryOperation("Registry file validation passed: " + full_path);
    return true;

  } catch (const std::exception& e) {
    LogRegistryOperation(
      "Error validating registry file: " + std::string(e.what()), true);
    return false;
  }
}

int RegistryBackupUtility::CleanupOldBackups() {
  if (!initialized_) {
    return 0;
  }

  try {
    QDir backup_dir(QString::fromStdString(backup_directory_));
    if (!backup_dir.exists()) {
      return 0;
    }

    QStringList filters;
    filters << "*.reg";
    backup_dir.setNameFilters(filters);

    QFileInfoList backup_files =
      backup_dir.entryInfoList(QDir::Files, QDir::Time | QDir::Reversed);

    int files_removed = 0;
    if (backup_files.size() > max_backup_files_) {
      for (int i = max_backup_files_; i < backup_files.size(); ++i) {
        if (backup_dir.remove(backup_files[i].fileName())) {
          LogRegistryOperation("Removed old backup: " +
                               backup_files[i].fileName().toStdString());
          files_removed++;
        }
      }
    }

    return files_removed;

  } catch (const std::exception& e) {
    LogRegistryOperation(
      "Error cleaning up old backups: " + std::string(e.what()), true);
    return 0;
  }
}

//------------------------------------------------------------------------------
// Static Helper Functions
//------------------------------------------------------------------------------

std::string RegistryBackupUtility::BackupTypeToHiveString(
  RegistryBackupType type) {
  auto it = hive_strings_.find(type);
  if (it != hive_strings_.end()) {
    return it->second;
  }
  return "HKEY_CURRENT_USER";  // Default fallback
}

std::string RegistryBackupUtility::GenerateBackupFileName(
  const std::string& base_name, const std::string& extension) {
  QDateTime current_time = QDateTime::currentDateTime();
  std::string timestamp =
    current_time.toString("yyyyMMdd_hhmmss").toStdString();
  return base_name + "_" + timestamp + extension;
}

//------------------------------------------------------------------------------
// Private Helper Functions
//------------------------------------------------------------------------------

bool RegistryBackupUtility::ExecuteRegistryCommand(
  const std::string& command, const std::vector<std::string>& arguments,
  const std::string& output_path) const {
  try {
    QProcess process;

    // Configure process to run silently
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
      });

    // Build command line arguments
    QStringList qargs;
    qargs << QString::fromStdString(command);
    for (const auto& arg : arguments) {
      qargs << QString::fromStdString(arg);
    }

    LogRegistryOperation("Executing: reg.exe " + command + " with " +
                         std::to_string(arguments.size()) + " arguments");

    // Start the process
    process.start("reg.exe", qargs);

    // Wait for completion (registry operations can take time)
    bool finished = process.waitForFinished(300000);  // 5 minutes timeout

    if (!finished) {
      LogRegistryOperation("Registry command timed out", true);
      process.kill();
      return false;
    }

    int exit_code = process.exitCode();
    if (exit_code == 0) {
      if (!output_path.empty()) {
        LogRegistryOperation(
          "Registry command completed successfully, output: " + output_path);
      } else {
        LogRegistryOperation("Registry command completed successfully");
      }
      return true;
    } else {
      LogRegistryOperation("Registry command failed with exit code: " +
                             std::to_string(exit_code),
                           true);
      return false;
    }

  } catch (const std::exception& e) {
    LogRegistryOperation(
      "Error executing registry command: " + std::string(e.what()), true);
    return false;
  }
}

bool RegistryBackupUtility::EnsureBackupDirectoryExists() const {
  try {
    QDir dir(QString::fromStdString(backup_directory_));
    if (!dir.exists()) {
      if (dir.mkpath(".")) {
        LogRegistryOperation("Created backup directory: " + backup_directory_);
      } else {
        LogRegistryOperation(
          "Failed to create backup directory: " + backup_directory_, true);
        return false;
      }
    }

    // Test write permissions by creating a temporary file
    QString test_file_path = dir.absoluteFilePath("test_write_access.tmp");
    QFile test_file(test_file_path);
    if (test_file.open(QIODevice::WriteOnly)) {
      test_file.write("test");
      test_file.close();
      test_file.remove();
      return true;
    } else {
      LogRegistryOperation(
        "No write access to backup directory: " + backup_directory_, true);
      return false;
    }

  } catch (const std::exception& e) {
    LogRegistryOperation(
      "Error ensuring backup directory exists: " + std::string(e.what()), true);
    return false;
  }
}

std::string RegistryBackupUtility::GetFullBackupPath(
  const std::string& filename) const {
  // If filename is already an absolute path, return as-is
  if (fs::path(filename).is_absolute()) {
    return filename;
  }

  // Otherwise, combine with backup directory
  return (fs::path(backup_directory_) / filename).string();
}

bool RegistryBackupUtility::HasAdministratorPrivileges() const {
  BOOL is_admin = FALSE;
  SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
  PSID admin_group = nullptr;

  if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                               DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                               &admin_group)) {
    CheckTokenMembership(nullptr, admin_group, &is_admin);
    FreeSid(admin_group);
  }

  return is_admin == TRUE;
}

void RegistryBackupUtility::LogRegistryOperation(const std::string& message,
                                                 bool is_error) const {
  QDateTime current_time = QDateTime::currentDateTime();
  std::string timestamp =
    current_time.toString("yyyy-MM-dd hh:mm:ss").toStdString();
  std::string level = is_error ? "ERROR" : "INFO";
  std::string log_message =
    "[" + timestamp + "] [" + level + "] [RegistryBackup] " + message;

  std::cout << log_message << std::endl;
}

size_t RegistryBackupUtility::GetFileSizeMB(
  const std::string& file_path) const {
  try {
    if (fs::exists(file_path)) {
      return static_cast<size_t>(fs::file_size(file_path) / (1024 * 1024));
    }
  } catch (const std::exception&) {
    // File doesn't exist or cannot access
  }
  return 0;
}

RegistryBackupResult RegistryBackupUtility::CreateErrorResult(
  RegistryBackupStatus status, const std::string& message) const {
  LogRegistryOperation(message, true);
  return {status, message, "", 0};
}

RegistryBackupResult RegistryBackupUtility::CreateSuccessResult(
  const std::string& message, const std::string& backup_path) const {
  size_t file_size = GetFileSizeMB(backup_path);
  LogRegistryOperation(message);
  return {RegistryBackupStatus::Success, message, backup_path, file_size};
}

bool RegistryBackupUtility::ExecuteRegistryExportViaPowerShell(
  const std::string& output_path) const {
  try {
    // Create PowerShell script content that uses regedit.exe /e
    // This replicates the "File > Export > All" functionality from Registry
    // Editor
    std::string ps_script = R"POWERSHELL(
# PowerShell script to export full Windows Registry using regedit.exe
# This is equivalent to Registry Editor "File > Export > All"

param([string]$OutputPath)

try {
    # Ensure the output directory exists
    $outputDir = Split-Path -Parent $OutputPath
    if (!(Test-Path -Path $outputDir)) {
        New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    }
    
    # Remove existing file if it exists
    if (Test-Path -Path $OutputPath) {
        Remove-Item -Path $OutputPath -Force
    }
    
    Write-Host "[INFO] Starting full registry export using regedit.exe..."
    Write-Host "[INFO] Output file: $OutputPath"
    
    # Use regedit.exe with /e parameter to export entire registry
    $process = Start-Process -FilePath "regedit.exe" -ArgumentList "/e", "`"$OutputPath`"" -WindowStyle Hidden -Wait -PassThru
    
    # Check if the process completed successfully
    if ($process.ExitCode -eq 0) {
        # Verify the output file was created and has content
        if ((Test-Path -Path $OutputPath) -and ((Get-Item $OutputPath).Length -gt 1000)) {
            $fileSizeMB = [math]::Round((Get-Item $OutputPath).Length / 1MB, 2)
            Write-Host "[INFO] Registry export completed successfully"
            Write-Host "[INFO] File size: $fileSizeMB MB"
            Exit 0
        } else {
            Write-Host "[ERROR] Registry export file was not created or is too small"
            Exit 1
        }
    } else {
        Write-Host "[ERROR] Registry export failed with exit code: $($process.ExitCode)"
        Exit 1
    }
} catch {
    Write-Host "[ERROR] PowerShell script error: $($_.Exception.Message)"
    Exit 1
}
)POWERSHELL";

    // Create temporary PowerShell script file
    std::string temp_script_path = QDir::tempPath().toStdString() +
                                   "/checkmark_registry_export_" +
                                   std::to_string(GetTickCount()) + ".ps1";

    // Write PowerShell script to temporary file
    std::ofstream script_file(temp_script_path);
    if (!script_file.is_open()) {
      LogRegistryOperation("Failed to create temporary PowerShell script",
                           true);
      return false;
    }
    script_file << ps_script;
    script_file.close();

    LogRegistryOperation("Created PowerShell script: " + temp_script_path);

    // Execute PowerShell script
    QProcess process;

    // Configure process to run silently
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
      });

    // Build PowerShell command arguments
    QStringList ps_args;
    ps_args << "-ExecutionPolicy" << "Bypass"  // Allow script execution
            << "-WindowStyle" << "Hidden"      // Hide PowerShell window
            << "-NonInteractive"               // No user interaction
            << "-File"
            << QString::fromStdString(temp_script_path)  // Script file
            << "-OutputPath"
            << QString::fromStdString(output_path);  // Output parameter

    LogRegistryOperation("Executing PowerShell registry export script...");

    // Start PowerShell process
    process.start("powershell.exe", ps_args);

    // Wait for completion (registry export can take several minutes)
    // Using a shorter timeout and checking periodically to keep the application
    // responsive
    bool finished = false;
    int totalWaitTime = 0;
    const int checkInterval = 5000;  // Check every 5 seconds
    const int maxWaitTime = 300000;  // Maximum 5 minutes

    LogRegistryOperation(
      "Registry export started, checking progress every 5 seconds...");

    while (totalWaitTime < maxWaitTime && !finished) {
      finished = process.waitForFinished(checkInterval);
      totalWaitTime += checkInterval;

      if (!finished) {
        LogRegistryOperation("Registry export still in progress... (" +
                             std::to_string(totalWaitTime / 1000) +
                             "s elapsed)");
      }
    }

    // Clean up temporary script file
    if (fs::exists(temp_script_path)) {
      fs::remove(temp_script_path);
    }

    if (!finished) {
      LogRegistryOperation(
        "PowerShell registry export timed out after 5 minutes", true);
      process.kill();
      return false;
    }

    int exit_code = process.exitCode();

    // Read PowerShell output for logging
    QByteArray output = process.readAllStandardOutput();
    if (!output.isEmpty()) {
      std::string output_str = output.toStdString();
      std::istringstream output_stream(output_str);
      std::string line;
      while (std::getline(output_stream, line)) {
        if (line.find("[INFO]") != std::string::npos) {
          LogRegistryOperation(line.substr(6));  // Remove [INFO] prefix
        } else if (line.find("[ERROR]") != std::string::npos) {
          LogRegistryOperation(line.substr(7), true);  // Remove [ERROR] prefix
        }
      }
    }

    if (exit_code == 0) {
      // Double-check that the file was created and is not empty
      if (fs::exists(output_path) && fs::file_size(output_path) > 1000) {
        LogRegistryOperation(
          "PowerShell registry export completed successfully");
        return true;
      } else {
        LogRegistryOperation("PowerShell script reported success but output "
                             "file is missing or empty",
                             true);
        return false;
      }
    } else {
      LogRegistryOperation(
        "PowerShell registry export failed with exit code: " +
          std::to_string(exit_code),
        true);
      return false;
    }

  } catch (const std::exception& e) {
    LogRegistryOperation("Error executing PowerShell registry export: " +
                           std::string(e.what()),
                         true);
    return false;
  }
}

bool RegistryBackupUtility::ExecuteRegistryImportViaPowerShell(
  const std::string& file_path) const {
  try {
    // Create PowerShell script content that uses regedit.exe /s for import
    // This replicates silent registry import functionality
    std::string ps_script = R"POWERSHELL(
# PowerShell script to import Windows Registry using regedit.exe
# This performs a silent registry import

param([string]$InputPath)

try {
    # Ensure the input file exists
    if (!(Test-Path -Path $InputPath)) {
        Write-Host "[ERROR] Registry file not found: $InputPath"
        Exit 1
    }
    
    Write-Host "[INFO] Starting registry import using regedit.exe..."
    Write-Host "[INFO] Input file: $InputPath"
    
    # Use regedit.exe with /s parameter to import registry silently
    $process = Start-Process -FilePath "regedit.exe" -ArgumentList "/s", "`"$InputPath`"" -WindowStyle Hidden -Wait -PassThru
    
    # Check if the process completed successfully
    if ($process.ExitCode -eq 0) {
        Write-Host "[INFO] Registry import completed successfully"
        Exit 0
    } else {
        Write-Host "[ERROR] Registry import failed with exit code: $($process.ExitCode)"
        Exit 1
    }
} catch {
    Write-Host "[ERROR] PowerShell script error: $($_.Exception.Message)"
    Exit 1
}
)POWERSHELL";

    // Create temporary PowerShell script file
    std::string temp_script_path = QDir::tempPath().toStdString() +
                                   "/checkmark_registry_import_" +
                                   std::to_string(GetTickCount()) + ".ps1";

    // Write PowerShell script to temporary file
    std::ofstream script_file(temp_script_path);
    if (!script_file.is_open()) {
      LogRegistryOperation("Failed to create temporary PowerShell script",
                           true);
      return false;
    }
    script_file << ps_script;
    script_file.close();

    LogRegistryOperation("Created PowerShell script: " + temp_script_path);

    // Execute PowerShell script
    QProcess process;

    // Configure process to run silently
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setCreateProcessArgumentsModifier(
      [](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
      });

    // Build PowerShell command arguments
    QStringList ps_args;
    ps_args << "-ExecutionPolicy" << "Bypass"  // Allow script execution
            << "-WindowStyle" << "Hidden"      // Hide PowerShell window
            << "-NonInteractive"               // No user interaction
            << "-File"
            << QString::fromStdString(temp_script_path)  // Script file
            << "-InputPath"
            << QString::fromStdString(file_path);  // Input parameter

    LogRegistryOperation("Executing PowerShell registry import script...");

    // Start PowerShell process
    process.start("powershell.exe", ps_args);

    // Wait for completion (registry import can take several minutes)
    // Using a shorter timeout and checking periodically to keep the application
    // responsive
    bool finished = false;
    int totalWaitTime = 0;
    const int checkInterval = 5000;  // Check every 5 seconds
    const int maxWaitTime = 300000;  // Maximum 5 minutes

    LogRegistryOperation(
      "Registry import started, checking progress every 5 seconds...");

    while (totalWaitTime < maxWaitTime && !finished) {
      finished = process.waitForFinished(checkInterval);
      totalWaitTime += checkInterval;

      if (!finished) {
        LogRegistryOperation("Registry import still in progress... (" +
                             std::to_string(totalWaitTime / 1000) +
                             "s elapsed)");
      }
    }

    // Clean up temporary script file
    if (fs::exists(temp_script_path)) {
      fs::remove(temp_script_path);
    }

    if (!finished) {
      LogRegistryOperation(
        "PowerShell registry import timed out after 5 minutes", true);
      process.kill();
      return false;
    }

    int exit_code = process.exitCode();

    // Read PowerShell output for logging
    QByteArray output = process.readAllStandardOutput();
    if (!output.isEmpty()) {
      std::string output_str = output.toStdString();
      std::istringstream output_stream(output_str);
      std::string line;
      while (std::getline(output_stream, line)) {
        if (line.find("[INFO]") != std::string::npos) {
          LogRegistryOperation(line.substr(6));  // Remove [INFO] prefix
        } else if (line.find("[ERROR]") != std::string::npos) {
          LogRegistryOperation(line.substr(7), true);  // Remove [ERROR] prefix
        }
      }
    }

    if (exit_code == 0) {
      LogRegistryOperation("PowerShell registry import completed successfully");
      return true;
    } else {
      LogRegistryOperation(
        "PowerShell registry import failed with exit code: " +
          std::to_string(exit_code),
        true);
      return false;
    }

  } catch (const std::exception& e) {
    LogRegistryOperation("Error executing PowerShell registry import: " +
                           std::string(e.what()),
                         true);
    return false;
  }
}

}  // namespace registry
}  // namespace optimizations
