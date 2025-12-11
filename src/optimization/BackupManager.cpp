/**
 * @file BackupManager.cpp
 * @brief Implementation of the centralized backup manager for all optimization
 * settings
 */

#include "BackupManager.h"

#include <iostream>
#include "../logging/Logger.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTextStream>

#include "NvidiaOptimization.h"
#include "OptimizationEntity.h"
#include "PowerPlanManager.h"
#include "RegistryBackupUtility.h"
#include "RegistrySettings.h"
#include "Rust optimization/config_manager.h"
#include "VisualEffectsManager.h"

namespace optimizations {

BackupManager& BackupManager::GetInstance() {
  static BackupManager instance;
  return instance;
}

BackupManager::BackupManager() {
  // Initialize backup status maps
  for (int i = 0; i <= static_cast<int>(BackupType::All); i++) {
    BackupType type = static_cast<BackupType>(i);
    has_main_backup[type] = false;
    has_session_backup[type] = false;
  }
}

bool BackupManager::Initialize() {
  if (initialized) {
    return true;
  }

  // Ensure backup directory exists
  if (!EnsureBackupDirectoryExists()) {
    return false;
  }

  initialized = true;
  return true;
}

QString BackupManager::GetBackupDirectory() const {
  return QCoreApplication::applicationDirPath() + "/settings_backup";
}

bool BackupManager::EnsureBackupDirectoryExists() const {
  QString baseDir = GetBackupDirectory();

  // Create main paths
  QStringList requiredPaths = {
    baseDir,               // Main backup directory
    baseDir + "/main",     // Main backups
    baseDir + "/session",  // Session backups
    baseDir + "/archive"   // Archive for older backups
  };

  bool success = true;
  for (const QString& path : requiredPaths) {
    QDir dir(path);
    if (!dir.exists()) {
      if (!dir.mkpath(".")) {
        success = false;
      }
    }
  }

  // Verify permissions - try to create a test file to make sure we can write
  if (success) {
    QString testPath = baseDir + "/test_write_access.tmp";
    QFile testFile(testPath);
    if (testFile.open(QIODevice::WriteOnly)) {
      testFile.write("test");
      testFile.close();

      // Clean up test file
      testFile.remove();
    }
  }

  return success;
}

QString BackupManager::GetBackupFilePath(BackupType type, bool isMain) const {
  QString baseDir = GetBackupDirectory();
  QString typeStr;
  QString subfolder = isMain ? "/main" : "/session";
  QString extension = ".json";  // Default extension

  switch (type) {
    case BackupType::Registry:
      typeStr = "registry";
      break;
    case BackupType::RustConfig:
      typeStr = "rust_config";
      break;
    case BackupType::NvidiaSettings:
      typeStr = "nvidia";
      break;
    case BackupType::VisualEffects:
      typeStr = "visual_effects";
      break;
    case BackupType::PowerPlan:
      typeStr = "power_plan";
      break;
    case BackupType::FullRegistryExport:
      typeStr = "full_registry_export";
      extension = ".reg";  // Use .reg extension for registry exports
      // Full registry export goes directly in base directory, not in
      // main/session subfolders
      return baseDir + "/" + typeStr + extension;
    default:
      typeStr = "all";
      break;
  }

  // Create the subfolder if it doesn't exist (for non-FullRegistryExport types)
  QDir dir(baseDir + subfolder);
  if (!dir.exists()) {
    dir.mkpath(".");
  }

  return baseDir + subfolder + "/" + typeStr + extension;
}

bool BackupManager::FileExists(const QString& path) const {
  QFileInfo fileInfo(path);
  return fileInfo.exists() && fileInfo.isReadable();
}

BackupStatus BackupManager::CheckBackupStatus(BackupType type, bool isMain) {
  QString backupPath = GetBackupFilePath(type, isMain);

  // If backup doesn't exist in our designated backup directory, we need to
  // create it
  if (!FileExists(backupPath)) {
    return BackupStatus::NoBackupExists;
  }

  // Handle FullRegistryExport separately since it's a .reg file, not JSON
  if (type == BackupType::FullRegistryExport) {
    LOG_INFO
      << "[BackupManager::CheckBackupStatus] Checking FullRegistryExport at: "
      << backupPath.toStdString();

    // For full registry export (.reg files), check if file exists and is not
    // empty
    QFile regFile(backupPath);
    if (!regFile.exists()) {
      LOG_INFO << "[BackupManager::CheckBackupStatus] FullRegistryExport file "
                   "does not exist"
               ;
      return BackupStatus::NoBackupExists;
    }

    if (regFile.size() < 1000) {
      LOG_INFO << "[BackupManager::CheckBackupStatus] FullRegistryExport file "
                   "too small: "
                << regFile.size() << " bytes";
      return BackupStatus::PartialBackup;
    }

    // Basic validation of .reg file format
    if (regFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream stream(&regFile);
      QString firstLine = stream.readLine();
      regFile.close();

      // Check for proper registry file header
      if (!firstLine.contains("Windows Registry Editor Version")) {
        LOG_INFO << "[BackupManager::CheckBackupStatus] FullRegistryExport "
                     "file invalid header: "
                  << firstLine.toStdString();
        return BackupStatus::PartialBackup;
      }
    } else {
      LOG_INFO << "[BackupManager::CheckBackupStatus] FullRegistryExport file "
                   "cannot be opened for reading"
               ;
      return BackupStatus::BackupError;
    }

    // If we get here, the .reg file exists and looks valid
    LOG_INFO << "[BackupManager::CheckBackupStatus] FullRegistryExport file "
                 "is COMPLETE and valid ("
              << regFile.size() << " bytes)";
    return BackupStatus::CompleteBackup;
  }

  // For all other backup types, load as JSON to check metadata
  QFile file(backupPath);
  file.open(QIODevice::ReadOnly);
  if (!file.isOpen()) {
    return BackupStatus::BackupError;
  }

  QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
  file.close();

  if (doc.isNull() || !doc.isObject()) {
    return BackupStatus::BackupError;
  }

  QJsonObject obj = doc.object();

  // Check if it's a session backup and if it's from the current session
  if (!isMain) {
    if (obj.contains("timestamp")) {
      QDateTime timestamp =
        QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
      QDateTime currentTime = QDateTime::currentDateTime();

      // If timestamp is more than 8 hours old, consider it outdated
      if (timestamp.secsTo(currentTime) > 8 * 3600) {
        return BackupStatus::OutdatedSessionBackup;
      }
    } else {
      // No timestamp, consider outdated
      return BackupStatus::OutdatedSessionBackup;
    }
  }

  // Check for completeness based on type
  switch (type) {
    case BackupType::RustConfig:
      {
        // For Rust, check if all expected top-level objects are present
        if (!obj.contains("client_cfg") || !obj.contains("metadata") ||
            !obj.contains("timestamp")) {
          return BackupStatus::PartialBackup;
        }

        // Make sure client_cfg is a proper JSON object, not a string
        if (!obj["client_cfg"].isObject()) {
          return BackupStatus::PartialBackup;
        }

        // Check that client_cfg has content
        QJsonObject clientCfg = obj["client_cfg"].toObject();
        if (clientCfg.isEmpty()) {
          return BackupStatus::PartialBackup;
        }

        // Check other cfg files if they exist
        if (obj.contains("favorites_cfg") && !obj["favorites_cfg"].isObject()) {
          return BackupStatus::PartialBackup;
        }

        if (obj.contains("keys_cfg") && !obj["keys_cfg"].isObject()) {
          return BackupStatus::PartialBackup;
        }

        if (obj.contains("keys_default_cfg") &&
            !obj["keys_default_cfg"].isObject()) {
          return BackupStatus::PartialBackup;
        }

        break;
      }

    case BackupType::Registry:
      {
        // For registry, check if we have settings
        if (!obj.contains("registry_settings") ||
            !obj["registry_settings"].isArray()) {
          return BackupStatus::PartialBackup;
        }

        // Also check if the array is empty
        QJsonArray settingsArray = obj["registry_settings"].toArray();
        if (settingsArray.isEmpty()) {
          return BackupStatus::PartialBackup;
        }

        // Get the current registry optimizations to compare
        auto& optManager = OptimizationManager::GetInstance();
        optManager.Initialize();
        auto registryOpts =
          optManager.GetOptimizationsByType(OptimizationType::WindowsRegistry);

        // If we find any optimization not in the backup, consider it partial
        QSet<QString> backupIds;
        for (const auto& settingValue : settingsArray) {
          QJsonObject settingObj = settingValue.toObject();
          if (settingObj.contains("id")) {
            backupIds.insert(settingObj["id"].toString());
          }
        }

        // Check if all current registry settings are in the backup
        for (const auto& opt : registryOpts) {
          QString id = QString::fromStdString(opt->GetId());
          if (!backupIds.contains(id)) {
            return BackupStatus::PartialBackup;
          }
        }

        break;
      }

    case BackupType::NvidiaSettings:
      {
        // For NVIDIA, check if we have settings
        if (!obj.contains("nvidia_settings") ||
            !obj["nvidia_settings"].isArray()) {
          return BackupStatus::PartialBackup;
        }

        // Also check if the array is empty when we have NVIDIA optimizations
        QJsonArray settingsArray = obj["nvidia_settings"].toArray();
        auto& optManager = OptimizationManager::GetInstance();
        optManager.Initialize();
        auto nvidiaOpts =
          optManager.GetOptimizationsByType(OptimizationType::NvidiaSettings);

        if (!nvidiaOpts.empty() && settingsArray.isEmpty()) {
          return BackupStatus::PartialBackup;
        }

        break;
      }

    case BackupType::VisualEffects:
      {
        // For Visual Effects, check if we have profile
        if (!obj.contains("profile") || obj["profile"].toInt(-1) == -1) {
          return BackupStatus::PartialBackup;
        }
        break;
      }

    case BackupType::PowerPlan:
      {
        // For Power Plan, check if we have GUID
        if (!obj.contains("guid") || obj["guid"].toString().isEmpty()) {
          return BackupStatus::PartialBackup;
        }
        break;
      }

    case BackupType::FullRegistryExport:
      {
        // This case should never be reached since we handle it at the top
        // But keeping it here for safety
        return BackupStatus::CompleteBackup;
      }

    default:
      break;
  }

  return BackupStatus::CompleteBackup;
}

bool BackupManager::CreateBackup(BackupType type, bool isMain) {
  // Check if we're already in the process of creating a backup of this type
  static std::map<BackupType, bool> backupInProgress;
  if (backupInProgress[type]) {
    return true;
  }

  // Set the flag to indicate backup creation is in progress
  backupInProgress[type] = true;

  // Add debug logging for FullRegistryExport
  if (type == BackupType::FullRegistryExport) {
    LOG_INFO
      << "[BackupManager::CreateBackup] Processing FullRegistryExport, isMain="
      << (isMain ? "true" : "false");
  }

  // Check if we should create or update this backup
  BackupStatus status = CheckBackupStatus(type, isMain);

  // Add debug logging for FullRegistryExport status
  if (type == BackupType::FullRegistryExport) {
    LOG_INFO << "[BackupManager::CreateBackup] FullRegistryExport status="
              << static_cast<int>(status);
  }

  // If it's a main backup and already complete, we still want to check for new
  // settings EXCEPT for FullRegistryExport which should only be created once
  bool forceCreate = false;
  if (isMain && status == BackupStatus::CompleteBackup &&
      type != BackupType::FullRegistryExport) {
    forceCreate = true;  // Force creation to check for new settings
  }

  // Add debug logging for FullRegistryExport force creation decision
  if (type == BackupType::FullRegistryExport) {
    LOG_INFO << "[BackupManager::CreateBackup] FullRegistryExport forceCreate="
              << (forceCreate ? "true" : "false");
  }

  // For session backups, recreate if outdated
  if (!isMain && status == BackupStatus::OutdatedSessionBackup) {
    // Archive the old session backup before creating a new one
    QString backupPath = GetBackupFilePath(type, isMain);
    QFile oldBackup(backupPath);
    if (oldBackup.exists()) {
      QString backupDir = GetBackupDirectory();
      QString timestamp =
        QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
      QString backupName = backupPath.mid(backupPath.lastIndexOf('/') + 1);
      QString archivePath =
        backupDir + "/archive/" + timestamp + "_" + backupName;

      // Create archive directory if needed
      QDir archiveDir(backupDir + "/archive");
      if (!archiveDir.exists()) {
        archiveDir.mkpath(".");
      }

      // Move the old backup to archive
      if (!oldBackup.rename(archivePath)) {
        // If move fails, try to remove
        oldBackup.remove();
      }
    }
  }

  // Ensure backup directory exists first
  if (!EnsureBackupDirectoryExists()) {
    backupInProgress[type] = false;  // Reset the flag
    return false;
  }

  // Create backup based on type if:
  // 1. No backup exists (NoBackupExists)
  // 2. Backup is partial (PartialBackup)
  // 3. Session backup is outdated (OutdatedSessionBackup)
  // 4. We're forcing creation to check for new settings
  bool shouldCreate = status == BackupStatus::NoBackupExists ||
                      status == BackupStatus::PartialBackup ||
                      status == BackupStatus::OutdatedSessionBackup ||
                      forceCreate;

  // Add debug logging for FullRegistryExport shouldCreate decision
  if (type == BackupType::FullRegistryExport) {
    LOG_INFO
      << "[BackupManager::CreateBackup] FullRegistryExport shouldCreate="
      << (shouldCreate ? "true" : "false");
    LOG_INFO << "[BackupManager::CreateBackup] FullRegistryExport "
                 "shouldCreate reasons: NoBackupExists="
              << (status == BackupStatus::NoBackupExists ? "true" : "false")
              << ", PartialBackup="
              << (status == BackupStatus::PartialBackup ? "true" : "false")
              << ", OutdatedSessionBackup="
              << (status == BackupStatus::OutdatedSessionBackup ? "true"
                                                                : "false")
              << ", forceCreate=" << (forceCreate ? "true" : "false")
             ;
  }

  bool success = false;
  if (shouldCreate) {
    switch (type) {
      case BackupType::Registry:
        success = BackupRegistrySettings(isMain);
        break;
      case BackupType::RustConfig:
        success = BackupRustSettings(isMain);
        break;
      case BackupType::NvidiaSettings:
        success = BackupNvidiaSettings(isMain);
        break;
      case BackupType::VisualEffects:
        success = BackupVisualEffectsSettings(isMain);
        break;
      case BackupType::PowerPlan:
        success = BackupPowerPlanSettings(isMain);
        break;
      case BackupType::FullRegistryExport:
        success = BackupFullRegistryExport(isMain);
        break;
      case BackupType::All:
        // Backup all types including full registry export
        success =
          BackupRegistrySettings(isMain) && BackupRustSettings(isMain) &&
          BackupNvidiaSettings(isMain) && BackupVisualEffectsSettings(isMain) &&
          BackupPowerPlanSettings(isMain) && BackupFullRegistryExport(isMain);
        break;
    }
  } else {
    // Skip creation, backup is already complete
    if (type == BackupType::FullRegistryExport && isMain) {
      LOG_INFO << "[BackupManager] Skipping FullRegistryExport main backup - "
                   "already exists and complete"
               ;
    }
    success = true;
  }

  // Reset the in-progress flag before returning
  backupInProgress[type] = false;

  if (success) {
    // Update our backup status tracking
    if (isMain) {
      has_main_backup[type] = true;
      main_backup_timestamp[type] = QDateTime::currentDateTime();
    } else {
      has_session_backup[type] = true;
      session_backup_timestamp[type] = QDateTime::currentDateTime();
    }
  } else {
    // Skip creation, backup is already complete
    success = true;
  }

  return success;
}

bool BackupManager::CreateAllBackupsIfNeeded() {
  LOG_INFO << "[BackupManager] Checking and creating all backups if needed..."
           ;

  bool allSuccess = true;
  for (int i = 0; i < static_cast<int>(BackupType::All); i++) {
    BackupType type = static_cast<BackupType>(i);
    BackupStatus status = CheckBackupStatus(type, true);

    std::string typeName = "";
    switch (type) {
      case BackupType::Registry:
        typeName = "Registry";
        break;
      case BackupType::RustConfig:
        typeName = "RustConfig";
        break;
      case BackupType::NvidiaSettings:
        typeName = "NvidiaSettings";
        break;
      case BackupType::VisualEffects:
        typeName = "VisualEffects";
        break;
      case BackupType::PowerPlan:
        typeName = "PowerPlan";
        break;
      case BackupType::FullRegistryExport:
        typeName = "FullRegistryExport";
        break;
      default:
        typeName = "Unknown";
        break;
    }

    LOG_INFO << "[BackupManager] Checking " << typeName << " main backup..."
             ;

    // Always try to create main backups to make sure we have them and they're
    // up to date
    if (!CreateBackup(type, true)) {
      LOG_INFO << "[BackupManager] ERROR: Failed to create " << typeName
                << " main backup";
      allSuccess = false;
    } else {
      LOG_INFO << "[BackupManager] " << typeName
                << " main backup check completed";
    }
  }

  // Check and create session backups for all types
  for (int i = 0; i < static_cast<int>(BackupType::All); i++) {
    BackupType type = static_cast<BackupType>(i);
    BackupStatus status = CheckBackupStatus(type, false);

    // Create session backup if needed
    if (status == BackupStatus::NoBackupExists ||
        status == BackupStatus::PartialBackup ||
        status == BackupStatus::OutdatedSessionBackup) {
      if (!CreateBackup(type, false)) {
        allSuccess = false;
      }
    }
  }

  LOG_INFO << "[BackupManager] All backup creation completed. Success: "
            << (allSuccess ? "true" : "false");
  return allSuccess;
}

bool BackupManager::RestoreFromBackup(BackupType type, bool isMain) {
  // Check if backup exists
  BackupStatus status = CheckBackupStatus(type, isMain);
  if (status != BackupStatus::CompleteBackup) {
    return false;
  }

  // Restore based on type
  switch (type) {
    case BackupType::RustConfig:
      {
        // Use the RustConfigManager to restore Rust settings
        rust::RustConfigManager& rustManager =
          rust::RustConfigManager::GetInstance();
        if (!rustManager.Initialize()) {
          return false;
        }

        // Read the backup file
        QString backupPath = GetBackupFilePath(type, isMain);
        QFile file(backupPath);
        file.open(QIODevice::ReadOnly);
        if (!file.isOpen()) {
          return false;
        }

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        if (doc.isNull() || !doc.isObject()) {
          return false;
        }

        QJsonObject backupObj = doc.object();

        // Check for client_cfg object
        if (!backupObj.contains("client_cfg") ||
            !backupObj["client_cfg"].isObject()) {
          return false;
        }

        QJsonObject clientCfgObj = backupObj["client_cfg"].toObject();

        // Create a map of settings to restore
        std::map<QString, QString> settingsToRestore;
        for (auto it = clientCfgObj.begin(); it != clientCfgObj.end(); ++it) {
          if (it.value().isString()) {
            settingsToRestore[it.key()] = it.value().toString();
          }
        }

        // Apply each setting individually
        int successCount = 0;
        int totalCount = settingsToRestore.size();

        for (const auto& [key, value] : settingsToRestore) {
          if (rustManager.ApplySetting(key, value)) {
            successCount++;
          }
        }

        // Consider it successful if we restored at least some settings
        if (successCount == 0) {
          return false;
        }

        // Restore additional configuration files
        RestoreRustAdditionalFiles(backupObj,
                                   rustManager.GetRustCfgDirectory());

        return true;
      }

    case BackupType::Registry:
    case BackupType::NvidiaSettings:
    case BackupType::VisualEffects:
    case BackupType::PowerPlan:
    case BackupType::All:
      // Not implemented yet - will need custom code for each type
      return false;
  }

  return false;
}

// Helper method to restore additional Rust config files
void BackupManager::RestoreRustAdditionalFiles(const QJsonObject& backupObj,
                                               const QString& cfgDir) {
  if (cfgDir.isEmpty()) {
    return;
  }

  // Restore favorites.cfg if it exists in the backup
  if (backupObj.contains("favorites_cfg") &&
      backupObj["favorites_cfg"].isObject()) {
    QJsonObject favoritesObj = backupObj["favorites_cfg"].toObject();

    // Handle the different possible formats in the backup
    QString content;
    if (favoritesObj.contains("lines") && favoritesObj["lines"].isArray()) {
      // If stored as lines array, join them with newlines
      QJsonArray linesArray = favoritesObj["lines"].toArray();
      QStringList lines;
      for (const QJsonValue& line : linesArray) {
        lines.append(line.toString());
      }
      content = lines.join("\n");
    } else if (favoritesObj.contains("content_base64")) {
      // Handle old base64 format for backward compatibility
      QByteArray contentBytes = QByteArray::fromBase64(
        favoritesObj["content_base64"].toString().toLatin1());
      content = QString::fromUtf8(contentBytes);
    } else if (favoritesObj.contains("content")) {
      // Handle old direct content format
      content = favoritesObj["content"].toString();
    } else {
      // It's a direct JSON object, convert it back to string
      QJsonDocument favoritesDoc(favoritesObj);
      content = favoritesDoc.toJson(QJsonDocument::Indented);
    }

    if (!content.isEmpty()) {
      QFile favoritesFile(cfgDir + "/favorites.cfg");
      if (favoritesFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&favoritesFile);
        stream << content;
        favoritesFile.close();
      }
    }
  }

  // Restore keys.cfg if it exists in the backup
  if (backupObj.contains("keys_cfg") && backupObj["keys_cfg"].isObject()) {
    QJsonObject keysObj = backupObj["keys_cfg"].toObject();
    QString content;

    // Handle different possible formats
    if (keysObj.contains("bindings") && keysObj["bindings"].isArray()) {
      // New format - stored as array of lines
      QJsonArray bindingsArray = keysObj["bindings"].toArray();
      QStringList lines;
      for (const QJsonValue& binding : bindingsArray) {
        lines.append(binding.toString());
      }
      content = lines.join("\n");
    } else if (keysObj.contains("content_base64")) {
      // Old base64 format
      QByteArray contentBytes =
        QByteArray::fromBase64(keysObj["content_base64"].toString().toLatin1());
      content = QString::fromUtf8(contentBytes);
    } else if (keysObj.contains("content")) {
      // Old direct content format
      content = keysObj["content"].toString();
    }

    if (!content.isEmpty()) {
      QFile keysFile(cfgDir + "/keys.cfg");
      if (keysFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&keysFile);
        stream << content;
        keysFile.close();
      }
    }
  }

  // Restore keys_default.cfg if it exists in the backup
  if (backupObj.contains("keys_default_cfg") &&
      backupObj["keys_default_cfg"].isObject()) {
    QJsonObject keysDefaultObj = backupObj["keys_default_cfg"].toObject();
    QString content;

    // Handle different possible formats
    if (keysDefaultObj.contains("bindings") &&
        keysDefaultObj["bindings"].isArray()) {
      // New format - stored as array of lines
      QJsonArray bindingsArray = keysDefaultObj["bindings"].toArray();
      QStringList lines;
      for (const QJsonValue& binding : bindingsArray) {
        lines.append(binding.toString());
      }
      content = lines.join("\n");
    } else if (keysDefaultObj.contains("content_base64")) {
      // Old base64 format
      QByteArray contentBytes = QByteArray::fromBase64(
        keysDefaultObj["content_base64"].toString().toLatin1());
      content = QString::fromUtf8(contentBytes);
    } else if (keysDefaultObj.contains("content")) {
      // Old direct content format
      content = keysDefaultObj["content"].toString();
    }

    if (!content.isEmpty()) {
      QFile keysDefaultFile(cfgDir + "/keys_default.cfg");
      if (keysDefaultFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&keysDefaultFile);
        stream << content;
        keysDefaultFile.close();
      }
    }
  }
}

bool BackupManager::BackupRustSettings(bool isMain) {
  // Get backup path for Rust settings
  QString backupPath = GetBackupFilePath(BackupType::RustConfig, isMain);

  // Ensure the directory exists
  QFileInfo fileInfo(backupPath);
  QDir().mkpath(fileInfo.path());

  // Use RustConfigManager to get current settings
  auto& rustManager = rust::RustConfigManager::GetInstance();

  // Only initialize if not already initialized to prevent loops
  static bool backupInProgress = false;
  if (backupInProgress) {
    return false;
  }

  backupInProgress = true;

  // If rustManager fails to initialize, don't crash, just report
  if (!rustManager.Initialize()) {
    LOG_ERROR << "ERROR: Failed to initialize Rust config manager";
    backupInProgress = false;
    return false;
  }

  // Read current settings from Rust config files
  QString clientCfgContent = rustManager.GetRawConfigContent();
  if (clientCfgContent.isEmpty()) {
    LOG_ERROR << "ERROR: No Rust config content found";
    backupInProgress = false;
    return false;
  }

  // Create base backup object
  QJsonObject backupObj;
  QJsonObject metadataObj;

  // Add metadata
  metadataObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  metadataObj["backup_type"] = isMain ? "main" : "session";
  metadataObj["version"] = "1.0";
  backupObj["metadata"] = metadataObj;

  // Add timestamp at root level for backup status detection
  backupObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

  // Parse all settings from the config file into client_cfg object for
  // programmatic access
  QJsonObject clientCfgSettings;
  QStringList lines = clientCfgContent.split('\n');

  // Parse individual settings for easier access and modifications
  int parsedSettings = 0;
  for (const QString& line : lines) {
    QString trimmedLine = line.trimmed();
    if (!trimmedLine.isEmpty() && !trimmedLine.startsWith("//")) {
      QString key, value;
      int equalPos = trimmedLine.indexOf('=');
      if (equalPos > 0) {
        key = trimmedLine.left(equalPos).trimmed();
        value = trimmedLine.mid(equalPos + 1).trimmed();
      } else {
        int spacePos = trimmedLine.indexOf(' ');
        if (spacePos > 0) {
          key = trimmedLine.left(spacePos).trimmed();
          value = trimmedLine.mid(spacePos + 1).trimmed();
          if (value.startsWith("\"") && value.endsWith("\"")) {
            value = value.mid(1, value.length() - 2);
          }
        }
      }

      // Important: Include ALL settings, even with empty values
      if (!key.isEmpty()) {
        clientCfgSettings[key] = value;  // Store even if value is empty
        parsedSettings++;
      }
    }
  }

  // Get the directory where Rust cfg files are stored
  QString cfgDir = rustManager.GetRustCfgDirectory();

  // Handle existing backup if it exists
  QJsonObject existingBackupObj;
  bool hasExistingBackup = false;

  // Only look in our designated backup file
  if (QFile::exists(backupPath)) {
    QFile existingFile(backupPath);
    if (existingFile.open(QIODevice::ReadOnly)) {
      QJsonDocument existingDoc =
        QJsonDocument::fromJson(existingFile.readAll());
      existingFile.close();

      if (!existingDoc.isNull() && existingDoc.isObject()) {
        existingBackupObj = existingDoc.object();
        hasExistingBackup = true;

        // For main backups, merge with existing backup
        if (isMain) {
          // Preserve existing client_cfg object if it exists
          if (existingBackupObj.contains("client_cfg") &&
              existingBackupObj["client_cfg"].isObject()) {
            QJsonObject existingClientCfg =
              existingBackupObj["client_cfg"].toObject();

            // Add only new settings to the existing client_cfg
            int newSettingsAdded = 0;
            for (auto it = clientCfgSettings.begin();
                 it != clientCfgSettings.end(); ++it) {
              if (!existingClientCfg.contains(it.key())) {
                existingClientCfg[it.key()] = it.value();
                newSettingsAdded++;
              }
            }

            // Use updated client_cfg
            clientCfgSettings = existingClientCfg;
          }

          // Keep existing metadata in place
          if (existingBackupObj.contains("metadata") &&
              existingBackupObj["metadata"].isObject()) {
            metadataObj = existingBackupObj["metadata"].toObject();
            // Update last modified time
            metadataObj["last_updated"] =
              QDateTime::currentDateTime().toString(Qt::ISODate);
            backupObj["metadata"] = metadataObj;
          }
        }
      }
    }
  }

  // Add client_cfg to the backup
  backupObj["client_cfg"] = clientCfgSettings;

  // Read and parse additional config files if the directory exists
  if (!cfgDir.isEmpty()) {
    // Read favorites.cfg - This is already in JSON format
    QFile favoritesFile(cfgDir + "/favorites.cfg");
    QJsonObject favoritesCfgObj;

    if (favoritesFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QByteArray content = favoritesFile.readAll();
      favoritesFile.close();

      // Parse the JSON content
      QJsonDocument favoriteDoc = QJsonDocument::fromJson(content);
      if (!favoriteDoc.isNull()) {
        // If valid JSON, store the parsed object directly
        favoritesCfgObj = favoriteDoc.object();
      } else {
        // If not valid JSON, store as lines array for readability
        QJsonArray linesArray;
        QStringList contentLines = QString::fromUtf8(content).split('\n');
        for (const QString& line : contentLines) {
          if (!line.trimmed().isEmpty()) {
            linesArray.append(line);
          }
        }
        favoritesCfgObj["lines"] = linesArray;
      }
    } else if (hasExistingBackup &&
               existingBackupObj.contains("favorites_cfg")) {
      // If can't read current file but we have it in backup, preserve it
      favoritesCfgObj = existingBackupObj["favorites_cfg"].toObject();
    }
    backupObj["favorites_cfg"] = favoritesCfgObj;

    // Read keys.cfg - Store each line as an array entry for readability
    QFile keysFile(cfgDir + "/keys.cfg");
    QJsonObject keysCfgObj;

    if (keysFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QJsonArray linesArray;
      QTextStream in(&keysFile);
      while (!in.atEnd()) {
        QString line = in.readLine();
        if (!line.trimmed().isEmpty()) {
          linesArray.append(line);
        }
      }
      keysCfgObj["bindings"] = linesArray;
      keysFile.close();
    } else if (hasExistingBackup && existingBackupObj.contains("keys_cfg")) {
      keysCfgObj = existingBackupObj["keys_cfg"].toObject();
    }
    backupObj["keys_cfg"] = keysCfgObj;

    // Read keys_default.cfg - Store each line as an array entry for readability
    QFile keysDefaultFile(cfgDir + "/keys_default.cfg");
    QJsonObject keysDefaultCfgObj;

    if (keysDefaultFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QJsonArray linesArray;
      QTextStream in(&keysDefaultFile);
      while (!in.atEnd()) {
        QString line = in.readLine();
        if (!line.trimmed().isEmpty()) {
          linesArray.append(line);
        }
      }
      keysDefaultCfgObj["bindings"] = linesArray;
      keysDefaultFile.close();
    } else if (hasExistingBackup &&
               existingBackupObj.contains("keys_default_cfg")) {
      keysDefaultCfgObj = existingBackupObj["keys_default_cfg"].toObject();
    }
    backupObj["keys_default_cfg"] = keysDefaultCfgObj;
  } else if (hasExistingBackup) {
    // If directory doesn't exist but we have existing backup, preserve those
    // sections
    if (existingBackupObj.contains("favorites_cfg")) {
      backupObj["favorites_cfg"] = existingBackupObj["favorites_cfg"];
    }
    if (existingBackupObj.contains("keys_cfg")) {
      backupObj["keys_cfg"] = existingBackupObj["keys_cfg"];
    }
    if (existingBackupObj.contains("keys_default_cfg")) {
      backupObj["keys_default_cfg"] = existingBackupObj["keys_default_cfg"];
    }
  }

  // Write to the backup file
  QFile backupFile(backupPath);
  backupFile.open(QIODevice::WriteOnly);
  if (!backupFile.isOpen()) {
    LOG_ERROR << "ERROR: Could not open backup file for writing";
    backupInProgress = false;
    return false;
  }

  QJsonDocument doc(backupObj);
  backupFile.write(doc.toJson(QJsonDocument::Indented));
  backupFile.close();

  backupInProgress = false;
  return true;
}

bool BackupManager::BackupRegistrySettings(bool isMain) {
  QString backupPath = GetBackupFilePath(BackupType::Registry, isMain);

  // Use OptimizationManager to get current registry settings
  auto& optManager = OptimizationManager::GetInstance();
  optManager.Initialize();  // Just call Initialize without checking return

  // Get registry optimizations
  auto registryOpts =
    optManager.GetOptimizationsByType(OptimizationType::WindowsRegistry);
  if (registryOpts.empty()) {
    return false;
  }

  // Create JSON object for the settings export
  QJsonObject backupObj;
  backupObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  backupObj["backup_type"] = isMain ? "main" : "session";
  backupObj["version"] = 1;

  // Create array for registry settings
  QJsonArray registrySettings;

  for (const auto& opt : registryOpts) {
    // Create a JSON object for this setting
    QJsonObject settingObj;
    settingObj["id"] = QString::fromStdString(opt->GetId());
    settingObj["name"] = QString::fromStdString(opt->GetName());

    // Get registry key and value name if possible
    auto regOpt = dynamic_cast<settings::RegistryOptimization*>(opt);
    if (regOpt) {
      settingObj["registry_key"] =
        QString::fromStdString(regOpt->GetRegistryKey());
      settingObj["registry_value_name"] =
        QString::fromStdString(regOpt->GetRegistryValueName());
    }

    // Get the current value
    OptimizationValue currentValue = opt->GetCurrentValue();

    // Handle various value types
    if (std::holds_alternative<bool>(currentValue)) {
      settingObj["current_value"] = std::get<bool>(currentValue);
    } else if (std::holds_alternative<int>(currentValue)) {
      settingObj["current_value"] = std::get<int>(currentValue);
    } else if (std::holds_alternative<double>(currentValue)) {
      settingObj["current_value"] = std::get<double>(currentValue);
    } else if (std::holds_alternative<std::string>(currentValue)) {
      std::string strValue = std::get<std::string>(currentValue);
      settingObj["current_value"] = QString::fromStdString(strValue);
    }

    registrySettings.append(settingObj);
  }

  // Get existing backup only from our backup directory
  QJsonObject existingObj;
  bool hasExistingBackup = false;

  if (QFile::exists(backupPath)) {
    QFile existingFile(backupPath);
    if (existingFile.open(QIODevice::ReadOnly)) {
      QJsonDocument existingDoc =
        QJsonDocument::fromJson(existingFile.readAll());
      existingFile.close();

      if (!existingDoc.isNull() && existingDoc.isObject()) {
        existingObj = existingDoc.object();
        hasExistingBackup = true;
      }
    }
  }

  // For main backups, we want to merge new settings with existing ones
  if (isMain && hasExistingBackup) {
    // Get existing settings array
    QJsonArray existingSettingsArray =
      existingObj["registry_settings"].toArray();
    QMap<QString, QJsonObject> mergedSettingsMap;

    // First, add all settings from the existing backup to ensure their values
    // are preserved
    for (const QJsonValue& val : existingSettingsArray) {
      if (val.isObject()) {
        QJsonObject settingObj = val.toObject();
        if (settingObj.contains("id")) {
          mergedSettingsMap[settingObj["id"].toString()] = settingObj;
        }
      }
    }

    // Now, iterate through the current settings. If a setting ID is not in our
    // merged map, it means it's a new setting that wasn't in the original
    // backup, so add it. If it already exists, its original backed-up value is
    // preserved from the step above.
    int newSettingsAdded = 0;
    for (const QJsonValue& val :
         registrySettings) {  // registrySettings is QJsonArray of current values
      if (val.isObject()) {
        QJsonObject currentSettingObj = val.toObject();
        QString id = currentSettingObj["id"].toString();
        if (!mergedSettingsMap.contains(id)) {
          mergedSettingsMap[id] = currentSettingObj;  // Add new setting

          newSettingsAdded++;
        }
      }
    }

    // Reconstruct the registrySettings array from the merged map
    QJsonArray finalRegistrySettingsArray;
    for (const QJsonObject& settingObj : mergedSettingsMap.values()) {
      finalRegistrySettingsArray.append(settingObj);
    }

    // Update registry settings in the existing backup object before assigning
    // to backupObj
    existingObj["registry_settings"] = finalRegistrySettingsArray;
    existingObj["last_updated"] =
      QDateTime::currentDateTime().toString(Qt::ISODate);

    // Use the updated existing backup
    backupObj = existingObj;
  } else if (!isMain || !hasExistingBackup) {
    // For session backups, or if main backup doesn't exist, use current
    // settings directly
    backupObj["registry_settings"] = registrySettings;
  }

  // Ensure the directory exists
  QFileInfo fileInfo(backupPath);
  QDir().mkpath(fileInfo.path());

  // Write to the backup file
  QFile backupFile(backupPath);
  backupFile.open(QIODevice::WriteOnly);
  if (!backupFile.isOpen()) {
    LOG_ERROR << "ERROR: Could not open backup file for writing";
    return false;
  }

  QJsonDocument doc(backupObj);
  backupFile.write(doc.toJson(QJsonDocument::Indented));
  backupFile.close();

  return true;
}

bool BackupManager::BackupNvidiaSettings(bool isMain) {
  QString backupPath = GetBackupFilePath(BackupType::NvidiaSettings, isMain);

  // Use OptimizationManager to get current NVIDIA settings
  auto& optManager = OptimizationManager::GetInstance();
  optManager.Initialize();  // Just call Initialize without checking return

  // Get NVIDIA optimizations
  auto nvidiaOpts =
    optManager.GetOptimizationsByType(OptimizationType::NvidiaSettings);
  if (nvidiaOpts.empty()) {
    // No NVIDIA optimizations found - this is OK, might not have an NVIDIA GPU
    return true;
  }

  // Create JSON object for the settings export
  QJsonObject backupObj;
  backupObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  backupObj["backup_type"] = isMain ? "main" : "session";
  backupObj["version"] = 1;

  // Create array for NVIDIA settings
  QJsonArray nvidiaSettings;

  for (const auto& opt : nvidiaOpts) {
    // Create a JSON object for this setting
    QJsonObject settingObj;
    settingObj["id"] = QString::fromStdString(opt->GetId());
    settingObj["name"] = QString::fromStdString(opt->GetName());

    // Get the current value
    OptimizationValue currentValue = opt->GetCurrentValue();

    // Handle various value types
    if (std::holds_alternative<bool>(currentValue)) {
      settingObj["current_value"] = std::get<bool>(currentValue);
    } else if (std::holds_alternative<int>(currentValue)) {
      settingObj["current_value"] = std::get<int>(currentValue);
    } else if (std::holds_alternative<double>(currentValue)) {
      settingObj["current_value"] = std::get<double>(currentValue);
    } else if (std::holds_alternative<std::string>(currentValue)) {
      std::string strValue = std::get<std::string>(currentValue);
      settingObj["current_value"] = QString::fromStdString(strValue);
    }

    nvidiaSettings.append(settingObj);
  }

  backupObj["nvidia_settings"] = nvidiaSettings;

  // Check if it's a main backup and already exists
  if (isMain && QFile::exists(backupPath)) {
    // For main backups, load the existing backup and update it
    QFile existingFile(backupPath);
    if (!existingFile.open(QIODevice::ReadOnly)) {
    } else {
      QJsonDocument existingDoc =
        QJsonDocument::fromJson(existingFile.readAll());
      existingFile.close();

      if (!existingDoc.isNull() && existingDoc.isObject()) {
        QJsonObject existingObj = existingDoc.object();
        QJsonArray existingSettingsArray =
          existingObj.contains("nvidia_settings") &&
              existingObj["nvidia_settings"].isArray()
            ? existingObj["nvidia_settings"].toArray()
            : QJsonArray();

        QMap<QString, QJsonObject> mergedSettingsMap;

        // Preserve existing backed-up values
        for (const QJsonValue& val : existingSettingsArray) {
          if (val.isObject()) {
            QJsonObject settingObj = val.toObject();
            if (settingObj.contains("id")) {
              mergedSettingsMap[settingObj["id"].toString()] = settingObj;
            }
          }
        }

        // Add new settings not present in the original backup
        int newSettingsAdded = 0;
        for (const QJsonValue& val :
             nvidiaSettings) {  // nvidiaSettings is QJsonArray of current values
          if (val.isObject()) {
            QJsonObject currentSettingObj = val.toObject();
            QString id = currentSettingObj["id"].toString();
            if (!mergedSettingsMap.contains(id)) {
              mergedSettingsMap[id] = currentSettingObj;

              newSettingsAdded++;
            }
          }
        }

        QJsonArray finalNvidiaSettingsArray;
        for (const QJsonObject& settingObj : mergedSettingsMap.values()) {
          finalNvidiaSettingsArray.append(settingObj);
        }

        existingObj["nvidia_settings"] = finalNvidiaSettingsArray;
        existingObj["last_updated"] =
          QDateTime::currentDateTime().toString(Qt::ISODate);
        backupObj = existingObj;  // Use the merged object
      }
    }
  }

  // Write to the backup file
  QFile backupFile(backupPath);
  backupFile.open(QIODevice::WriteOnly);
  if (!backupFile.isOpen()) {
    LOG_ERROR << "ERROR: Could not open backup file for writing";
    return false;
  }

  QJsonDocument doc(backupObj);
  backupFile.write(doc.toJson(QJsonDocument::Indented));
  backupFile.close();

  return true;
}

bool BackupManager::BackupVisualEffectsSettings(bool isMain) {
  QString backupPath = GetBackupFilePath(BackupType::VisualEffects, isMain);

  // Use VisualEffectsManager to get current settings
  visual_effects::VisualEffectsManager& visualManager =
    visual_effects::VisualEffectsManager::GetInstance();
  if (!visualManager.Initialize()) {
    LOG_ERROR << "ERROR: Failed to initialize Visual Effects manager"
             ;
    return false;
  }

  // Get current profile
  visual_effects::VisualEffectsProfile currentProfile =
    visualManager.GetCurrentProfile();

  // Create JSON object for the settings export
  QJsonObject backupObj;
  backupObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  backupObj["backup_type"] = isMain ? "main" : "session";
  backupObj["version"] = 1;
  backupObj["profile"] = static_cast<int>(currentProfile);
  backupObj["profile_name"] =
    QString::fromStdString(visualManager.GetProfileName(currentProfile));

  // For main backup, never overwrite if it exists
  if (isMain && QFile::exists(backupPath)) {
    return true;
  }

  // Write to the backup file
  QFile backupFile(backupPath);
  backupFile.open(QIODevice::WriteOnly);
  if (!backupFile.isOpen()) {
    LOG_ERROR << "ERROR: Could not open backup file for writing";
    return false;
  }

  QJsonDocument doc(backupObj);
  backupFile.write(doc.toJson(QJsonDocument::Indented));
  backupFile.close();

  return true;
}

bool BackupManager::BackupPowerPlanSettings(bool isMain) {
  QString backupPath = GetBackupFilePath(BackupType::PowerPlan, isMain);

  // Use PowerPlanManager to get current settings
  power::PowerPlanManager& powerManager =
    power::PowerPlanManager::GetInstance();
  if (!powerManager.Initialize()) {
    LOG_ERROR << "ERROR: Failed to initialize Power Plan manager";
    return false;
  }

  // Get current power plan GUID
  std::wstring wCurrentPlan = powerManager.GetCurrentPowerPlan();
  std::string currentPlan(wCurrentPlan.begin(), wCurrentPlan.end());

  // Create JSON object for the settings export
  QJsonObject backupObj;
  backupObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  backupObj["backup_type"] = isMain ? "main" : "session";
  backupObj["version"] = 1;
  backupObj["guid"] = QString::fromStdString(currentPlan);

  // Get available power plans to find the name
  std::vector<power::PowerPlan> availablePlans =
    powerManager.GetAvailablePowerPlans();
  for (const auto& plan : availablePlans) {
    if (plan.guid == wCurrentPlan) {
      backupObj["name"] = QString::fromStdString(plan.name);

      break;
    }
  }

  // For main backup, never overwrite if it exists
  if (isMain && QFile::exists(backupPath)) {
    return true;
  }

  // Write to the backup file
  QFile backupFile(backupPath);
  backupFile.open(QIODevice::WriteOnly);
  if (!backupFile.isOpen()) {
    LOG_ERROR << "ERROR: Could not open backup file for writing";
    return false;
  }

  QJsonDocument doc(backupObj);
  backupFile.write(doc.toJson(QJsonDocument::Indented));
  backupFile.close();

  return true;
}

/**
 * @brief Create initial backups for all categories to facilitate testing
 * @return True if successful
 */
bool BackupManager::CreateInitialBackups() {
  if (!initialized && !Initialize()) {
    return false;
  }

  bool success = true;

  // Create main backup for each type if it doesn't exist
  for (int i = 0; i < static_cast<int>(BackupType::All); i++) {
    BackupType type = static_cast<BackupType>(i);

    if (CheckBackupStatus(type, true) != BackupStatus::CompleteBackup) {
      if (!CreateBackup(type, true)) {
        success = false;
      }
    }
  }

  // Create session backup for each type
  for (int i = 0; i < static_cast<int>(BackupType::All); i++) {
    BackupType type = static_cast<BackupType>(i);

    if (!CreateBackup(type, false)) {
      success = false;
    }
  }

  return success;
}

bool BackupManager::SaveUserPreferences() {
  // Ensure backup directory exists
  if (!EnsureBackupDirectoryExists()) {
    return false;
  }

  try {
    // Create a JSON object to store preferences
    QJsonObject preferencesObj;

    // Get all optimizations from the OptimizationManager
    auto& optManager = optimizations::OptimizationManager::GetInstance();
    for (auto& type :
         {OptimizationType::WindowsRegistry, OptimizationType::NvidiaSettings,
          OptimizationType::VisualEffects, OptimizationType::PowerPlan}) {
      auto optimizations = optManager.GetOptimizationsByType(type);
      for (auto* opt : optimizations) {
        if (opt) {
          preferencesObj[QString::fromStdString(opt->GetId())] =
            opt->DontEdit();
        }
      }
    }

    // Save to file
    QJsonDocument doc(preferencesObj);
    QFile file(GetUserPreferencesFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
      return false;
    }

    file.write(doc.toJson());
    file.close();

    return true;
  } catch (const std::exception& e) {
    return false;
  } catch (...) {
    return false;
  }
}

bool BackupManager::LoadUserPreferences() {
  QFile file(GetUserPreferencesFilePath());
  if (!file.exists()) {
    // File doesn't exist yet, not an error
    return false;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  try {
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
      return false;
    }

    // Successfully loaded the file
    return true;
  } catch (const std::exception& e) {
    return false;
  } catch (...) {
    return false;
  }
}

QString BackupManager::GetUserPreferencesFilePath() const {
  QString backupDir = GetBackupDirectory();
  return backupDir + "/user_preferences.json";
}

QString BackupManager::GetUnknownValuesFilePath() const {
  QString backupDir = GetBackupDirectory();
  return backupDir + "/unknown_values.json";
}

bool BackupManager::SetDontEditFlag(const std::string& optimization_id,
                                    bool dont_edit) {
  // This method would set the flag in memory and then save to file
  // For now, we'll just locate the optimization and set the flag
  auto& optManager = optimizations::OptimizationManager::GetInstance();
  auto* opt = optManager.FindOptimizationById(optimization_id);
  if (opt) {
    opt->SetDontEdit(dont_edit);
    return SaveUserPreferences();  // Save changes to file
  }
  return false;
}

bool BackupManager::GetDontEditFlag(const std::string& optimization_id,
                                    bool default_value) const {
  // Check in user preferences first
  QFile file(GetUserPreferencesFilePath());
  if (file.exists() && file.open(QIODevice::ReadOnly)) {
    QByteArray data = file.readAll();
    file.close();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isNull() && doc.isObject()) {
      QJsonObject obj = doc.object();
      if (obj.contains("settings_preferences") &&
          obj["settings_preferences"].isObject()) {
        QJsonObject prefs = obj["settings_preferences"].toObject();
        if (prefs.contains(QString::fromStdString(optimization_id))) {
          QJsonValue val = prefs[QString::fromStdString(optimization_id)];
          if (val.isObject() && val.toObject().contains("dont_edit")) {
            return val.toObject()["dont_edit"].toBool(default_value);
          }
        }
      }
    }
  }
  return default_value;
}

QVariant BackupManager::GetOriginalValueFromBackup(
  const std::string& optimization_id) const {
  // First, identify the backup type based on the optimization ID
  BackupType type = BackupType::Registry;  // Default to registry

  // Parse optimization ID to determine type
  QString optId = QString::fromStdString(optimization_id);
  if (optId.startsWith("nvidia_")) {
    type = BackupType::NvidiaSettings;
  } else if (optId.startsWith(
               "visual_effects_")) {  // Assuming a prefix for visual effects if
                                      // they were itemized
    type = BackupType::VisualEffects;
  } else if (optId.startsWith("power_plan_")) {  // Assuming a prefix for power
                                                 // plan if it were itemized
    type = BackupType::PowerPlan;
  } else if (optimization_id ==
             "visual_effects_profile") {  // Specific ID for the single visual
                                          // effect setting
    type = BackupType::VisualEffects;
  } else if (optimization_id ==
             "power.plan") {  // Specific ID for the single power plan setting
    type = BackupType::PowerPlan;
  }

  QString backupPath = GetBackupFilePath(type, true);  // true for main backup

  if (!FileExists(backupPath)) {
    return QVariant();
  }

  QFile file(backupPath);
  if (!file.open(QIODevice::ReadOnly)) {
    return QVariant();
  }

  QByteArray data = file.readAll();
  file.close();

  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (doc.isNull() || !doc.isObject()) {
    return QVariant();
  }

  QJsonObject obj = doc.object();
  QJsonValue valueToConvert;

  switch (type) {
    case BackupType::Registry:
      {
        if (!obj.contains("registry_settings") ||
            !obj["registry_settings"].isArray()) {
          return QVariant();
        }
        QJsonArray settingsArray = obj["registry_settings"].toArray();
        for (const QJsonValue& settingValue : settingsArray) {
          QJsonObject settingObj = settingValue.toObject();
          if (settingObj.contains("id") &&
              settingObj["id"].toString() == optId) {
            // For main backups, "current_value" contains the original value at
            // time of first backup
            if (settingObj.contains("current_value")) {
              valueToConvert = settingObj["current_value"];
              break;
            }
          }
        }
        break;
      }

    case BackupType::NvidiaSettings:
      {
        if (!obj.contains("nvidia_settings") ||
            !obj["nvidia_settings"].isArray()) {
          return QVariant();
        }
        QJsonArray settingsArray = obj["nvidia_settings"].toArray();
        for (const QJsonValue& settingValue : settingsArray) {
          QJsonObject settingObj = settingValue.toObject();
          if (settingObj.contains("id") &&
              settingObj["id"].toString() == optId) {
            // For main backups, "current_value" contains the original value at
            // time of first backup
            if (settingObj.contains("current_value")) {
              valueToConvert = settingObj["current_value"];
              break;
            }
          }
        }
        break;
      }

    case BackupType::VisualEffects:
      {
        if (optimization_id == "visual_effects_profile" &&
            obj.contains("profile")) {
          valueToConvert = obj["profile"];
        }
        break;
      }

    case BackupType::PowerPlan:
      {
        if (optimization_id == "power.plan" && obj.contains("guid")) {
          valueToConvert = obj["guid"];
        }
        break;
      }

    case BackupType::RustConfig:
      {
        if (!obj.contains("client_cfg") || !obj["client_cfg"].isObject()) {
          return QVariant();
        }
        QString settingName = optId.mid(5);  // Remove "rust_" prefix
        QJsonObject clientCfg = obj["client_cfg"].toObject();
        if (clientCfg.contains(settingName)) {
          valueToConvert = clientCfg[settingName];
        }
        break;
      }

    default:
      return QVariant();
  }

  if (valueToConvert.isUndefined() || valueToConvert.isNull()) {
    return QVariant();
  } else if (valueToConvert.isBool() || valueToConvert.isDouble() ||
             valueToConvert.isString() || valueToConvert.isObject() ||
             valueToConvert.isArray()) {
    // Allow objects and arrays for Rust config. The conversion to QVariant will
    // handle them. For simple types (bool, double, string), it's
    // straightforward. For objects/arrays, toVariant() will create a
    // QVariantMap or QVariantList.
    QVariant result = valueToConvert.toVariant();

    // Apply type normalization for consistency
    if (result.type() == QVariant::Double) {
      double doubleVal = result.toDouble();
      // If the value fits in a regular int, convert it to int for consistency
      if (doubleVal == static_cast<int>(doubleVal) && doubleVal >= INT_MIN &&
          doubleVal <= INT_MAX) {
        result = QVariant(static_cast<int>(doubleVal));
      }
    } else if (result.type() == QVariant::LongLong ||
               result.type() == QVariant::ULongLong) {
      // Handle qlonglong types that come from JSON parsing
      qlonglong longValue = result.toLongLong();
      // If the value fits in a regular int, convert it to int for consistency
      if (longValue >= INT_MIN && longValue <= INT_MAX) {
        result = QVariant(static_cast<int>(longValue));
      }
    }

    // Normalize string representations of booleans and numbers
    if (result.type() == QVariant::String) {
      QString strValue = result.toString().toLower().trimmed();

      // Remove quotes if present (common in config files)
      if (strValue.startsWith('"') && strValue.endsWith('"')) {
        strValue = strValue.mid(1, strValue.length() - 2);
      }

      // Try boolean conversion first
      if (strValue == "true" || strValue == "1") {
        result = QVariant(true);
      } else if (strValue == "false" || strValue == "0") {
        result = QVariant(false);
      } else {
        // Try integer conversion
        bool okInt;
        int intValue = strValue.toInt(&okInt);
        if (okInt) {
          result = QVariant(intValue);
        } else {
          // Try double conversion (for values like "0.74")
          bool okDouble;
          double doubleValue = strValue.toDouble(&okDouble);
          if (okDouble) {
            result = QVariant(doubleValue);
          }
          // Keep as string if neither boolean, integer, nor double
        }
      }
    }

    return result;
  }

  return QVariant();  // Return null QVariant if not found or unsupported type
}

/**
 * @brief Load the unknown values from the backup file
 * @param unknownValues Reference to a map where the values will be stored
 * @return True if the load was successful
 */
bool BackupManager::LoadUnknownValues(
  QMap<QString, QList<QVariant>>& unknownValues) const {
  QString filePath = GetUnknownValuesFilePath();
  QFile file(filePath);

  // Check if file exists
  if (!file.exists()) {
    // No unknown values file yet, which is okay
    return true;
  }

  // Open file
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  // Read file
  QByteArray data = file.readAll();
  file.close();

  // Parse JSON
  QJsonDocument doc = QJsonDocument::fromJson(data);
  if (doc.isNull() || !doc.isObject()) {
    return false;
  }

  // Clear existing unknown values
  unknownValues.clear();

  // Process JSON
  QJsonObject rootObj = doc.object();
  for (auto it = rootObj.begin(); it != rootObj.end(); ++it) {
    QString settingId = it.key();
    QJsonValue value = it.value();

    if (!value.isArray()) {
      continue;
    }

    QJsonArray valuesArray = value.toArray();
    QList<QVariant> valuesList;

    for (const QJsonValue& valueItem : valuesArray) {
      if (!valueItem.isObject()) {
        continue;
      }

      QJsonObject valueObj = valueItem.toObject();
      if (!valueObj.contains("type") || !valueObj.contains("value")) {
        continue;
      }

      QString type = valueObj["type"].toString();
      QJsonValue jsonValue = valueObj["value"];

      QVariant variantValue;
      if (type == "bool") {
        variantValue = jsonValue.toBool();
      } else if (type == "int") {
        variantValue = jsonValue.toInt();
      } else if (type == "double") {
        variantValue = jsonValue.toDouble();
      } else if (type == "QString") {
        variantValue = jsonValue.toString();
      } else {
        // Skip unsupported types
        continue;
      }

      valuesList.append(variantValue);
    }

    unknownValues[settingId] = valuesList;
  }

  return true;
}

/**
 * @brief Save the unknown values to the backup file
 * @param unknownValues Map of unknown values to save
 * @return True if the save was successful
 */
bool BackupManager::SaveUnknownValues(
  const QMap<QString, QList<QVariant>>& unknownValues) {
  // Create JSON document to store unknown values
  QJsonObject rootObj;

  // Convert our map of unknown values to JSON
  for (auto it = unknownValues.begin(); it != unknownValues.end(); ++it) {
    QString settingId = it.key();
    const QList<QVariant>& values = it.value();

    if (values.isEmpty()) {
      continue;
    }

    QJsonArray valuesArray;
    for (const QVariant& value : values) {
      QJsonObject valueObj;

      // Store the value type and actual value
      valueObj["type"] = value.typeName();

      // Store value based on type
      if (value.type() == QVariant::Bool) {
        valueObj["value"] = value.toBool();
      } else if (value.type() == QVariant::Int) {
        valueObj["value"] = value.toInt();
      } else if (value.type() == QVariant::Double) {
        valueObj["value"] = value.toDouble();
      } else if (value.type() == QVariant::String) {
        valueObj["value"] = value.toString();
      } else {
        // Skip unsupported types
        continue;
      }

      valuesArray.append(valueObj);
    }

    rootObj[settingId] = valuesArray;
  }

  // Convert to JSON document
  QJsonDocument doc(rootObj);

  // Ensure backup directory exists
  if (!EnsureBackupDirectoryExists()) {
    return false;
  }

  // Get file path
  QString filePath = GetUnknownValuesFilePath();

  // Save to file
  QFile file(filePath);
  if (file.open(QIODevice::WriteOnly)) {
    file.write(doc.toJson());
    file.close();
    return true;
  } else {
    return false;
  }
}

bool BackupManager::AddMissingSettingToMainBackup(
  const std::string& optimization_id, const QVariant& current_value) {
  // Determine the backup type based on the optimization ID
  BackupType type = BackupType::Registry;  // Default to registry

  QString optId = QString::fromStdString(optimization_id);
  if (optId.startsWith("nvidia_")) {
    type = BackupType::NvidiaSettings;
  } else if (optimization_id == "visual_effects_profile") {
    type = BackupType::VisualEffects;
  } else if (optimization_id == "power.plan") {
    type = BackupType::PowerPlan;
  } else if (optId.startsWith("rust_")) {
    type = BackupType::RustConfig;
  }

  // Get the main backup file path
  QString backupPath = GetBackupFilePath(type, true);  // true for main backup

  // Ensure backup directory exists
  if (!EnsureBackupDirectoryExists()) {
    LOG_ERROR << "ERROR: Could not create backup directory";
    return false;
  }

  LOG_INFO
    << "[BackupManager::AddMissingSettingToMainBackup] Adding missing setting: "
    << optimization_id
    << " with value: " << current_value.toString().toStdString();

  // Declare backupObj at function level so it's accessible throughout
  QJsonObject backupObj;

  // Handle different backup types
  switch (type) {
    case BackupType::Registry:
    case BackupType::NvidiaSettings:
      {
        // Load existing backup if it exists
        bool hasExistingBackup = false;

        if (FileExists(backupPath)) {
          QFile existingFile(backupPath);
          if (existingFile.open(QIODevice::ReadOnly)) {
            QJsonDocument existingDoc =
              QJsonDocument::fromJson(existingFile.readAll());
            existingFile.close();

            if (!existingDoc.isNull() && existingDoc.isObject()) {
              backupObj = existingDoc.object();
              hasExistingBackup = true;
            }
          }
        }

        // If no existing backup, create a new one
        if (!hasExistingBackup) {
          backupObj["timestamp"] =
            QDateTime::currentDateTime().toString(Qt::ISODate);
          backupObj["backup_type"] = "main";
          backupObj["version"] = 1;
        }

        // Get the settings array name
        QString settingsArrayName = (type == BackupType::Registry)
                                      ? "registry_settings"
                                      : "nvidia_settings";

        // Get existing settings array or create new one
        QJsonArray settingsArray;
        if (backupObj.contains(settingsArrayName) &&
            backupObj[settingsArrayName].isArray()) {
          settingsArray = backupObj[settingsArrayName].toArray();
        }

        // Check if this setting already exists (safety check)
        bool settingExists = false;
        for (const QJsonValue& val : settingsArray) {
          if (val.isObject()) {
            QJsonObject settingObj = val.toObject();
            if (settingObj.contains("id") &&
                settingObj["id"].toString() == optId) {
              settingExists = true;
              LOG_INFO
                << "    Setting already exists in backup, not overriding"
               ;
              break;
            }
          }
        }

        // Only add if it doesn't already exist
        if (!settingExists) {
          // Get the optimization entity to get additional info
          auto& optManager = OptimizationManager::GetInstance();
          auto* opt = optManager.FindOptimizationById(optimization_id);

          // Create the new setting object
          QJsonObject newSettingObj;
          newSettingObj["id"] = optId;
          if (opt) {
            newSettingObj["name"] = QString::fromStdString(opt->GetName());

            // Add registry-specific info if available
            if (type == BackupType::Registry) {
              auto regOpt = dynamic_cast<settings::RegistryOptimization*>(opt);
              if (regOpt) {
                newSettingObj["registry_key"] =
                  QString::fromStdString(regOpt->GetRegistryKey());
                newSettingObj["registry_value_name"] =
                  QString::fromStdString(regOpt->GetRegistryValueName());
              }
            }
          } else {
            newSettingObj["name"] =
              optId;  // Fallback to ID if optimization not found
          }

          // Store the current value as the "original" value
          if (current_value.type() == QVariant::Bool) {
            newSettingObj["current_value"] = current_value.toBool();
          } else if (current_value.type() == QVariant::Int) {
            newSettingObj["current_value"] = current_value.toInt();
          } else if (current_value.type() == QVariant::Double) {
            newSettingObj["current_value"] = current_value.toDouble();
          } else {
            newSettingObj["current_value"] = current_value.toString();
          }

          // Add to settings array
          settingsArray.append(newSettingObj);
          backupObj[settingsArrayName] = settingsArray;
          backupObj["last_updated"] =
            QDateTime::currentDateTime().toString(Qt::ISODate);

          LOG_INFO << "    Added new setting to "
                    << settingsArrayName.toStdString()
                    << " array in main backup";
        }

        break;
      }

    case BackupType::RustConfig:
      {
        // For Rust settings, we need to handle the client_cfg object
        QString settingName = optId.mid(5);  // Remove "rust_" prefix

        // Load existing backup if it exists
        bool hasExistingBackup = false;

        if (FileExists(backupPath)) {
          QFile existingFile(backupPath);
          if (existingFile.open(QIODevice::ReadOnly)) {
            QJsonDocument existingDoc =
              QJsonDocument::fromJson(existingFile.readAll());
            existingFile.close();

            if (!existingDoc.isNull() && existingDoc.isObject()) {
              backupObj = existingDoc.object();
              hasExistingBackup = true;
            }
          }
        }

        // If no existing backup, create a new one
        if (!hasExistingBackup) {
          backupObj["timestamp"] =
            QDateTime::currentDateTime().toString(Qt::ISODate);
          backupObj["backup_type"] = "main";
          QJsonObject metadataObj;
          metadataObj["version"] = "1.0";
          metadataObj["timestamp"] =
            QDateTime::currentDateTime().toString(Qt::ISODate);
          backupObj["metadata"] = metadataObj;
        }

        // Get existing client_cfg object or create new one
        QJsonObject clientCfgObj;
        if (backupObj.contains("client_cfg") &&
            backupObj["client_cfg"].isObject()) {
          clientCfgObj = backupObj["client_cfg"].toObject();
        }

        // Check if this setting already exists (safety check)
        if (!clientCfgObj.contains(settingName)) {
          // Add the missing setting with its current value
          clientCfgObj[settingName] = current_value.toString();
          backupObj["client_cfg"] = clientCfgObj;
          backupObj["last_updated"] =
            QDateTime::currentDateTime().toString(Qt::ISODate);

          LOG_INFO << "    Added new Rust setting '"
                    << settingName.toStdString()
                    << "' to client_cfg in main backup";
        } else {
          LOG_INFO
            << "    Rust setting already exists in backup, not overriding"
           ;
        }

        break;
      }

    case BackupType::VisualEffects:
    case BackupType::PowerPlan:
      // These are single-value backups, usually created once
      // If the backup doesn't exist, create it with the current value
      if (!FileExists(backupPath)) {
        backupObj["timestamp"] =
          QDateTime::currentDateTime().toString(Qt::ISODate);
        backupObj["backup_type"] = "main";
        backupObj["version"] = 1;

        if (type == BackupType::VisualEffects) {
          backupObj["profile"] = current_value.toInt();
          backupObj["profile_name"] = "Unknown";  // We don't have the name
        } else if (type == BackupType::PowerPlan) {
          backupObj["guid"] = current_value.toString();
          backupObj["name"] = "Unknown";  // We don't have the name
        }

        LOG_INFO << "    Created new "
                  << (type == BackupType::VisualEffects ? "visual effects"
                                                        : "power plan")
                  << " main backup";
      } else {
        LOG_INFO << "    "
                  << (type == BackupType::VisualEffects ? "Visual effects"
                                                        : "Power plan")
                  << " backup already exists, not overriding";
        return true;  // Not an error, just already exists
      }
      break;

    default:
      LOG_ERROR << "ERROR: Unsupported backup type for adding missing setting"
               ;
      return false;
  }

  // Write the updated backup to file
  QFile backupFile(backupPath);
  if (!backupFile.open(QIODevice::WriteOnly)) {
    LOG_ERROR << "ERROR: Could not open backup file for writing: "
              << backupPath.toStdString();
    return false;
  }

  QJsonDocument doc(backupObj);
  backupFile.write(doc.toJson(QJsonDocument::Indented));
  backupFile.close();

  LOG_INFO << "    Successfully added missing setting to main backup file: "
            << backupPath.toStdString();
  return true;
}

bool BackupManager::RecordNonExistentSetting(
  const std::string& optimization_id) {
  QString backupPath =
    GetBackupFilePath(BackupType::Registry, true);  // Main backup

  QJsonObject backupObj;
  QJsonArray registrySettings;

  // Load existing backup if it exists
  QFile existingFile(backupPath);
  if (existingFile.exists() && existingFile.open(QIODevice::ReadOnly)) {
    QJsonDocument existingDoc = QJsonDocument::fromJson(existingFile.readAll());
    existingFile.close();

    if (existingDoc.isObject()) {
      backupObj = existingDoc.object();
      if (backupObj.contains("registry_settings") &&
          backupObj["registry_settings"].isArray()) {
        registrySettings = backupObj["registry_settings"].toArray();
      }
    }
  }

  // Check if this setting already exists in the backup
  bool settingExists = false;
  for (const auto& settingValue : registrySettings) {
    QJsonObject settingObj = settingValue.toObject();
    if (settingObj.contains("id") &&
        settingObj["id"].toString().toStdString() == optimization_id) {
      settingExists = true;
      break;
    }
  }

  // Only add if not already present
  if (!settingExists) {
    QJsonObject newSetting;
    newSetting["id"] = QString::fromStdString(optimization_id);
    newSetting["current_value"] = "NON_EXISTENT";
    newSetting["name"] =
      QString::fromStdString(optimization_id);  // Fallback name

    registrySettings.append(newSetting);

    // Update backup object
    backupObj["backup_type"] = "main";
    backupObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    backupObj["last_updated"] =
      QDateTime::currentDateTime().toString(Qt::ISODate);
    backupObj["version"] = 1;
    backupObj["registry_settings"] = registrySettings;

    // Save the updated backup
    QFile backupFile(backupPath);
    if (!backupFile.open(QIODevice::WriteOnly)) {
      return false;
    }

    QJsonDocument doc(backupObj);
    backupFile.write(doc.toJson(QJsonDocument::Indented));
    backupFile.close();
  }

  return true;
}

bool BackupManager::BackupFullRegistryExport(bool isMain) {
  // Full registry exports are created only once - there are no separate
  // main/session versions The file goes directly in the base backup directory

  QString backupPath =
    GetBackupFilePath(BackupType::FullRegistryExport, isMain);

  LOG_INFO << "[BackupManager::BackupFullRegistryExport] Creating full "
               "registry export backup"
           ;
  LOG_INFO << "[BackupManager::BackupFullRegistryExport] Target path: "
            << backupPath.toStdString();
  LOG_INFO << "[BackupManager::BackupFullRegistryExport] Note: Full registry "
               "export is created only once (no main/session versions)"
           ;

  // Never overwrite if the file already exists and is valid
  if (FileExists(backupPath)) {
    LOG_INFO << "[BackupManager::BackupFullRegistryExport] Full registry "
                 "export backup already exists, skipping creation"
             ;
    return true;
  }

  // Initialize the RegistryBackupUtility
  auto& registryBackup =
    optimizations::registry::RegistryBackupUtility::GetInstance();

  // Initialize with our backup directory base (not the main/session
  // subdirectory)
  QString backupDirBase = GetBackupDirectory();
  if (!registryBackup.Initialize(backupDirBase.toStdString())) {
    LOG_INFO << "[BackupManager::BackupFullRegistryExport] ERROR: Failed to "
                 "initialize RegistryBackupUtility"
             ;
    return false;
  }
  LOG_INFO << "[BackupManager::BackupFullRegistryExport] "
               "RegistryBackupUtility initialized successfully"
           ;

  // Full registry export uses just the filename since it goes directly in the
  // base backup directory
  QString filename = "full_registry_export.reg";
  LOG_INFO << "[BackupManager::BackupFullRegistryExport] Using filename: "
            << filename.toStdString();

  try {
    LOG_INFO
      << "[BackupManager::BackupFullRegistryExport] Starting registry export..."
     ;
    LOG_INFO << "[BackupManager::BackupFullRegistryExport] This may take "
                 "several minutes for large registries..."
             ;

    // Create full registry export using just the filename
    auto result = registryBackup.ExportFullRegistry(filename.toStdString());

    if (result.IsSuccess()) {
      LOG_INFO << "[BackupManager::BackupFullRegistryExport] SUCCESS: "
                   "Registry export completed"
               ;
      LOG_INFO << "[BackupManager::BackupFullRegistryExport] Backup size: "
                << result.file_size_mb << " MB";
      LOG_INFO << "[BackupManager::BackupFullRegistryExport] Backup path: "
                << result.backup_path;
      return true;
    } else {
      LOG_INFO << "[BackupManager::BackupFullRegistryExport] ERROR: Registry "
                   "export failed - "
                << result.message;
      return false;
    }

  } catch (const std::exception& e) {
    LOG_INFO << "[BackupManager::BackupFullRegistryExport] ERROR: Exception "
                 "during registry export: "
              << e.what();
    return false;
  }
}

}  // namespace optimizations
