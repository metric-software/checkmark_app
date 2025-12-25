#include "config_manager.h"

#include <fstream>
#include <iostream>
#include <set>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStorageInfo>
#include <QThread>

#include "../BackupManager.h"  // Include our new BackupManager

#include "logging/Logger.h"

#include "../../core/AppNotificationBus.h"

namespace {

void notifyRustConfigError(const QString& message) {
  AppNotificationBus::post(message, AppNotificationBus::Type::Error, 8000);
}

void notifyRustConfigWarning(const QString& message) {
  AppNotificationBus::post(message, AppNotificationBus::Type::Warning, 8000);
}

bool ensureOriginalBackupExists(const QString& targetFilePath,
                               QString* outErrorMessage) {
  if (!QFile::exists(targetFilePath)) {
    return true;  // Nothing to back up.
  }

  const QString originalPath = targetFilePath + ".original";
  if (QFile::exists(originalPath)) {
    return true;  // Preserve first known-good original.
  }

  if (!QFile::copy(targetFilePath, originalPath)) {
    if (outErrorMessage) {
      *outErrorMessage = QStringLiteral("could not create .original backup");
    }
    return false;
  }

  return true;
}

bool createTimestampedOldBackup(const QString& targetFilePath,
                               const QString& tag,
                               QString* outErrorMessage) {
  if (!QFile::exists(targetFilePath)) {
    return true;
  }

  const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
  const QString oldPath = targetFilePath + QStringLiteral(".%1_%2").arg(tag, ts);
  if (!QFile::copy(targetFilePath, oldPath)) {
    if (outErrorMessage) {
      *outErrorMessage = QStringLiteral("could not create %1 backup").arg(tag);
    }
    return false;
  }
  return true;
}

}  // namespace

namespace optimizations {
namespace rust {

RustConfigManager& RustConfigManager::GetInstance() {
  static RustConfigManager instance;
  return instance;
}

RustConfigManager::RustConfigManager() {
  // Private constructor to enforce singleton pattern
  InitializeExpectedValues();
  InitializeFocusedSettings();
}

bool RustConfigManager::Initialize() {
  if (initialized) {
    return true;
  }

  // Prevent recursive initialization
  static bool initializationInProgress = false;
  if (initializationInProgress) {
    // Already being initialized in another call stack
    LOG_WARN << "RustConfigManager initialization already in progress, "
              << "skipping recursive call";
    return false;
  }

  initializationInProgress = true;

  // Find the config file
  configFilePath = FindConfigFile();
  if (configFilePath.isEmpty()) {
    LOG_ERROR << "Rust config file not found.";
    initializationInProgress = false;
    return false;
  }

  LOG_INFO << "Found Rust config file at: [path hidden for privacy]";

  // Read current settings
  if (!ReadCurrentSettings()) {
    LOG_ERROR << "Failed to read current Rust settings.";
    initializationInProgress = false;
    return false;
  }

  // Initialize BackupManager to ensure it exists but don't create any backups
  // yet
  BackupManager::GetInstance().Initialize();

  initialized = true;
  initializationInProgress = false;
  return true;
}

QString RustConfigManager::FindConfigFile() {
  // Return cached path if we already found it
  static QString cachedPath;
  if (!cachedPath.isEmpty() && QFile::exists(cachedPath)) {
    return cachedPath;
  }

  QStringList possiblePaths;

  // Check Steam registry first (similar to RustConfigFinder)
  QSettings steamRegistry(
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam",
    QSettings::NativeFormat);
  QString steamPath = steamRegistry.value("InstallPath").toString();
  if (!steamPath.isEmpty()) {
    possiblePaths << steamPath + "/steamapps/common/Rust";
  }

  // Add common Steam paths
  possiblePaths << "C:/Program Files (x86)/Steam/steamapps/common/Rust"
                << "C:/Program Files/Steam/steamapps/common/Rust";

  // Add all drives
  for (const QStorageInfo& drive : QStorageInfo::mountedVolumes()) {
    if (drive.isValid() && drive.isReady()) {
      possiblePaths << drive.rootPath() + "SteamLibrary/steamapps/common/Rust";
    }
  }

  // Find first valid Rust installation by checking for RustClient.exe
  for (const QString& path : possiblePaths) {
    QFileInfo exeFile(path + "/RustClient.exe");
    if (exeFile.exists() && exeFile.isFile()) {
      QString configPath = path + "/cfg/client.cfg";
      if (QFile::exists(configPath)) {
        cachedPath = configPath;
        return configPath;
      }
    }
  }

  return QString();
}

bool RustConfigManager::ReadCurrentSettings() {
  if (configFilePath.isEmpty()) {
    configFilePath = FindConfigFile();
    if (configFilePath.isEmpty()) {
      LOG_ERROR << "No config file path provided or found.";
      return false;
    }
  }

  QFile file(configFilePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open config file: [path hidden for privacy]";
    return false;
  }

  std::map<QString, QString> currentConfig;
  QTextStream in(&file);
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    // Keep only actual config lines (not comments or empty lines)
    if (!line.isEmpty() && !line.startsWith("//")) {
      // Handle both = and space-separated values
      QString key, value;
      int equalPos = line.indexOf('=');
      if (equalPos > 0) {
        key = line.left(equalPos).trimmed();
        value = line.mid(equalPos + 1).trimmed();
      } else {
        // Handle Rust's space-separated format: "key value"
        int spacePos = line.indexOf(' ');
        if (spacePos > 0) {
          key = line.left(spacePos).trimmed();
          value = line.mid(spacePos + 1).trimmed();
          // Remove quotes if present
          if (value.startsWith("\"") && value.endsWith("\"")) {
            value = value.mid(1, value.length() - 2);
          }
        }
      }
      // Important change: Keep settings with empty values too!
      if (!key.isEmpty()) {
        currentConfig[key] = value;  // Even if value is empty
      }
    }
  }
  file.close();

  // Update our settings with current values
  for (auto& [key, setting] : settings) {
    auto it = currentConfig.find(key);
    if (it != currentConfig.end()) {
      setting.currentValue = it->second;
    } else {
      setting.currentValue = "missing";
    }

    // Check if different from optimal
    setting.isDifferent = (setting.currentValue != setting.optimalValue);
  }

  // Update different settings list
  differentSettings.clear();
  for (const auto& [key, setting] : settings) {
    if (setting.isDifferent) {
      differentSettings.push_back(setting);
    }
  }

  // If we have a backup, validate that it contains all the same settings
  // as the current config file, and update it if needed
  if (HasBackup()) {
    ValidateAndUpdateBackup();
  }

  return true;
}

bool RustConfigManager::ValidatePath(const QString& path) const {
  if (path.isEmpty()) {
    return false;
  }

  QFileInfo fileInfo(path);
  return fileInfo.exists() && fileInfo.isFile() && fileInfo.isReadable();
}

int RustConfigManager::CheckSettings() {
  if (!Initialize()) {
    LOG_ERROR << "Failed to initialize Rust config manager.";
    return -1;
  }

  // Always get fresh values
  if (!ReadCurrentSettings()) {
    return -1;
  }

  // Create backups if needed
  auto& backupManager = BackupManager::GetInstance();
  BackupStatus mainStatus =
    backupManager.CheckBackupStatus(BackupType::RustConfig, true);
  if (mainStatus != BackupStatus::CompleteBackup) {
    LOG_INFO << "Creating main backup for Rust settings...";
    backupManager.CreateBackup(BackupType::RustConfig, true);
  }

  BackupStatus sessionStatus =
    backupManager.CheckBackupStatus(BackupType::RustConfig, false);
  if (sessionStatus != BackupStatus::CompleteBackup) {
    LOG_INFO << "Creating session backup for Rust settings...";
    backupManager.CreateBackup(BackupType::RustConfig, false);
  }

  // Create old-style backups for compatibility
  if (!HasBackup()) {
    if (!CreateBackupUsingOldSystem()) {
      LOG_WARN << "Warning: Failed to create legacy backup of Rust settings.";
    } else {
      LOG_INFO << "Created legacy backup of Rust settings.";
    }
  }

  int differentCount = 0;

  LOG_INFO << "\n=== RUST CONFIGURATION CHECK ===";
  LOG_INFO << "Checking " << settings.size()
           << " targeted Rust configuration settings.";

  // Log differences
  for (const auto& setting : differentSettings) {
    LOG_INFO << "DIFFERENT: " << setting.key.toStdString()
             << " (Current: " << setting.currentValue.toStdString()
             << ", Expected: " << setting.optimalValue.toStdString() << ")";
    differentCount++;
  }

  LOG_INFO << "Found " << differentCount
           << " different or missing settings out of " << settings.size()
           << " settings.";
  LOG_INFO << "=== END OF RUST CONFIGURATION CHECK ===";

  return differentCount;
}

const std::vector<RustSetting>& RustConfigManager::GetDifferentSettings()
  const {
  return differentSettings;
}

const std::map<QString, RustSetting>& RustConfigManager::GetAllSettings()
  const {
  return settings;
}

bool RustConfigManager::ApplyOptimalSettings() {
  if (!Initialize()) {
    LOG_ERROR << "Failed to initialize Rust config manager.";
    return false;
  }

  // Create backup before applying changes
  auto& backupManager = BackupManager::GetInstance();

  // Ensure we have both main and session backups
  BackupStatus mainStatus =
    backupManager.CheckBackupStatus(BackupType::RustConfig, true);
  if (mainStatus != BackupStatus::CompleteBackup) {
    LOG_INFO << "Creating main backup before applying Rust settings...";
    backupManager.CreateBackup(BackupType::RustConfig, true);
  }

  BackupStatus sessionStatus =
    backupManager.CheckBackupStatus(BackupType::RustConfig, false);
  if (sessionStatus != BackupStatus::CompleteBackup) {
    LOG_INFO << "Creating session backup before applying Rust settings...";
    backupManager.CreateBackup(BackupType::RustConfig, false);
  }

  // Create legacy backup before applying changes if it doesn't exist
  if (!HasBackup()) {
    if (!CreateBackupUsingOldSystem()) {
      LOG_WARN << "Warning: Failed to create legacy backup before applying settings.";
    } else {
      LOG_INFO << "Created legacy backup of Rust settings.";
    }
  }

  // Create a map with only the settings we want to update with optimal values
  std::map<QString, QString> optimalSettingsToApply;
  for (const auto& [key, setting] : settings) {
    // Only include settings that actually need to be changed
    if (setting.currentValue != setting.optimalValue) {
      optimalSettingsToApply[key] = setting.optimalValue;
    }
  }

  // If no settings need to be changed, we're done
  if (optimalSettingsToApply.empty()) {
    LOG_INFO << "All settings are already at their optimal values.";
    return true;
  }

  // Write the modified config back to the file - this will preserve all other
  // settings
  if (!WriteConfigFile(optimalSettingsToApply)) {
    LOG_ERROR << "Failed to write Rust configuration file.";
    return false;
  }

  // Re-read settings to ensure changes are reflected
  ReadCurrentSettings();

  LOG_INFO << "Applied " << optimalSettingsToApply.size()
           << " optimal settings to Rust configuration.";
  return true;
}

bool RustConfigManager::ApplySetting(const QString& key, const QString& value) {
  if (!Initialize()) {
    LOG_ERROR << "Failed to initialize Rust config manager before applying setting.";
    return false;
  }

  // Create a copy of all settings
  std::map<QString, QString> currentSettings;

  // Read the current config file content
  QFile origFile(configFilePath);
  if (!origFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open config file for reading before applying setting.";
    return false;
  }

  // Parse the file line by line to extract settings
  QTextStream in(&origFile);
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (!line.isEmpty() && !line.startsWith("//")) {
      int equalPos = line.indexOf('=');
      int spacePos = line.indexOf(' ');

      QString lineKey, lineValue;

      if (equalPos > 0) {
        lineKey = line.left(equalPos).trimmed();
        lineValue = line.mid(equalPos + 1).trimmed();
      } else if (spacePos > 0) {
        lineKey = line.left(spacePos).trimmed();
        lineValue = line.mid(spacePos + 1).trimmed();

        // Remove quotes if present
        if (lineValue.startsWith("\"") && lineValue.endsWith("\"")) {
          lineValue = lineValue.mid(1, lineValue.length() - 2);
        }
      }

      if (!lineKey.isEmpty()) {
        currentSettings[lineKey] = lineValue;
      }
    }
  }
  origFile.close();

  // Update our setting
  QString normalized_value = value;

  // For numeric settings, make sure we handle conversion properly
  bool isNumeric = false;
  bool ok;
  value.toInt(&ok);
  if (ok) {
    isNumeric = true;
  }

  // Boolean settings should be properly capitalized
  if (value.toLower() == "true") {
    normalized_value = "True";
  } else if (value.toLower() == "false") {
    normalized_value = "False";
  }

  // Update our internal tracking
  if (settings.find(key) != settings.end()) {
    settings[key].currentValue = normalized_value;
    // Update isDifferent flag
    settings[key].isDifferent =
      (normalized_value != settings[key].optimalValue);
  }

  // Update the current settings copy
  currentSettings[key] = normalized_value;

  // Write the updated config back to file
  return WriteConfigFile(currentSettings);
}

bool RustConfigManager::WriteConfigFile(
  const std::map<QString, QString>& settings) {
  if (configFilePath.isEmpty()) {
    LOG_ERROR << "No config file path provided.";
    return false;
  }

  // Preserve an initial copy of the config before we start modifying it.
  {
    QString backupError;
    if (!ensureOriginalBackupExists(configFilePath, &backupError)) {
      LOG_ERROR << "Failed to create Rust config backup copy: "
                << backupError.toStdString();
      notifyRustConfigError(
        QStringLiteral("Rust settings update failed: %1").arg(backupError));
      return false;
    }
  }

  // First, read the entire file content including comments and empty lines
  QStringList originalLines;
  QFile originalFile(configFilePath);
  std::map<QString, QString> existingSettings;

  if (originalFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&originalFile);
    while (!in.atEnd()) {
      QString line = in.readLine();
      originalLines.append(line);

      // Also parse settings to know which ones we need to update
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
        if (!key.isEmpty()) {
          existingSettings[key] = value;  // Include empty values
        }
      }
    }
    originalFile.close();
  } else {
    LOG_WARN << "Warning: Could not read existing config file. Will create a new one.";
  }

  // Open the file for atomic writing (do not delete/overwrite until commit).
  QSaveFile file(configFilePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open Rust config file for writing: [path hidden for privacy]";
    notifyRustConfigError(
      QStringLiteral("Rust settings update failed: could not open config file for writing"));
    return false;
  }

  QTextStream out(&file);

  // Track which settings we've written
  std::set<QString> writtenSettings;

  // First, go through the original file and update settings that already exist
  for (const QString& line : originalLines) {
    QString trimmedLine = line.trimmed();

    // Keep comments and empty lines unchanged
    if (trimmedLine.isEmpty() || trimmedLine.startsWith("//")) {
      out << line << Qt::endl;
      continue;
    }

    // Parse the setting line
    QString key, value;
    int equalPos = trimmedLine.indexOf('=');
    if (equalPos > 0) {
      key = trimmedLine.left(equalPos).trimmed();
    } else {
      int spacePos = trimmedLine.indexOf(' ');
      if (spacePos > 0) {
        key = trimmedLine.left(spacePos).trimmed();
      }
    }

    // Check if this setting needs to be updated
    if (!key.isEmpty() && settings.find(key) != settings.end()) {
      // Get the new value
      QString newValue = settings.at(key);

      // Check if the value is numeric
      bool isNumeric = false;
      newValue.toInt(&isNumeric);

      // Format differently based on whether it's numeric or not
      if (newValue.isEmpty()) {
        out << key << " \"\"" << Qt::endl;  // Use empty quotes for empty values
      } else if (isNumeric) {
        out << key << " " << newValue
            << Qt::endl;  // No quotes for numeric values
      } else if (newValue.toLower() == "true" ||
                 newValue.toLower() == "false") {
        // For boolean values, use proper capitalization without quotes
        out << key << " " << (newValue.toLower() == "true" ? "True" : "False")
            << Qt::endl;
      } else {
        out << key << " \"" << newValue << "\""
            << Qt::endl;  // Quotes for string values
      }
      writtenSettings.insert(key);
    } else {
      // Keep the line as is
      out << line << Qt::endl;
    }
  }

  // Add any new settings that weren't in the original file
  for (const auto& [key, value] : settings) {
    if (writtenSettings.find(key) == writtenSettings.end()) {
      // Check if the value is numeric
      bool isNumeric = false;
      value.toInt(&isNumeric);

      // Format differently based on whether it's numeric or not
      if (value.isEmpty()) {
        out << key << " \"\"" << Qt::endl;  // Use empty quotes for empty values
      } else if (isNumeric) {
        out << key << " " << value << Qt::endl;  // No quotes for numeric values
      } else if (value.toLower() == "true" || value.toLower() == "false") {
        // For boolean values, use proper capitalization without quotes
        out << key << " " << (value.toLower() == "true" ? "True" : "False")
            << Qt::endl;
      } else {
        out << key << " \"" << value << "\""
            << Qt::endl;  // Quotes for string values
      }
      writtenSettings.insert(key);
    }
  }

  if (!file.commit()) {
    LOG_ERROR << "Failed to commit Rust config file write: [path hidden for privacy]";
    notifyRustConfigError(
      QStringLiteral("Rust settings update failed: could not commit config file changes"));
    return false;
  }
  LOG_INFO << "Successfully updated Rust configuration file with "
            << settings.size() << " settings.";

  return true;
}

bool RustConfigManager::CreateBackup() {
  if (!Initialize()) {
    return false;
  }

  // Use the BackupManager to create backups in the centralized location
  auto& backupManager = BackupManager::GetInstance();
  return backupManager.CreateBackup(BackupType::RustConfig,
                                    true) &&  // Main backup
         backupManager.CreateBackup(BackupType::RustConfig,
                                    false);  // Session backup
}

bool RustConfigManager::CreateBackupUsingOldSystem() {
  // Determine if we should create a new versioned backup
  bool createVersioned = ShouldCreateNewVersionedBackup();
  QString backupDir;

  if (createVersioned) {
    backupDir = GetVersionedBackupDir();
    LOG_INFO << "Creating new versioned backup in: [path hidden for privacy]";

    // Ensure the backup directory exists
    QDir dir(backupDir);
    if (!dir.exists()) {
      dir.mkpath(".");
    }
  }

  // Read the entire config file directly
  QFile configFile(configFilePath);
  if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open config file for backup.";
    return false;
  }

  // Store the entire file content for a complete backup
  QByteArray rawContent = configFile.readAll();
  QString entireContent = QString::fromUtf8(rawContent);

  // Reset file position to start for parsing
  configFile.seek(0);

  // Parse all settings from the config file
  std::map<QString, QString> allCurrentSettings;
  QTextStream in(&configFile);
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    // Keep only actual config lines (not comments or empty lines)
    if (!line.isEmpty() && !line.startsWith("//")) {
      // Handle both = and space-separated values
      QString key, value;
      int equalPos = line.indexOf('=');
      if (equalPos > 0) {
        key = line.left(equalPos).trimmed();
        value = line.mid(equalPos + 1).trimmed();
      } else {
        // Handle Rust's space-separated format: "key value"
        int spacePos = line.indexOf(' ');
        if (spacePos > 0) {
          key = line.left(spacePos).trimmed();
          value = line.mid(spacePos + 1).trimmed();
          // Remove quotes if present
          if (value.startsWith("\"") && value.endsWith("\"")) {
            value = value.mid(1, value.length() - 2);
          }
        }
      }
      if (!key.isEmpty()) {
        // Important: Include settings with empty values
        allCurrentSettings[key] = value;
      }
    }
  }
  configFile.close();

  // Create a JSON object to store the settings
  QJsonObject backupObj;

  // Add ALL settings to the backup
  for (const auto& [key, value] : allCurrentSettings) {
    backupObj[key] = value;
  }

  // Add metadata
  QJsonObject metaObj;
  metaObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  metaObj["config_path"] = configFilePath;
  metaObj["backup_version"] = "2.0";  // Version to track backup format changes
  metaObj["total_settings"] = static_cast<int>(allCurrentSettings.size());
  metaObj["raw_format"] = "json_key_value";  // Format descriptor

  // Add the raw content to ensure we have everything
  backupObj["raw_content"] = entireContent;

  // Add our tracked settings info for reference
  QJsonObject trackedSettingsObj;
  for (const auto& [key, setting] : settings) {
    QJsonObject settingObj;
    settingObj["optimal_value"] = setting.optimalValue;
    settingObj["is_bool"] = setting.isBool;
    trackedSettingsObj[key] = settingObj;
  }
  metaObj["tracked_settings"] = trackedSettingsObj;

  backupObj["metadata"] = metaObj;

  // Write to both the standard backup location and versioned backup if needed
  QJsonDocument doc(backupObj);

  // Always update the main backup file
  QFile file(GetBackupFilePath());
  if (!file.open(QIODevice::WriteOnly)) {
    LOG_ERROR << "Failed to open backup file for writing.";
    return false;
  }
  file.write(doc.toJson(QJsonDocument::Indented));
  file.close();
  LOG_INFO << "Successfully created full backup of Rust settings.";

  // If creating a versioned backup, write to the versioned location too
  if (createVersioned) {
    QFile versionedFile(backupDir + "/client.cfg.json");
    if (!versionedFile.open(QIODevice::WriteOnly)) {
      LOG_ERROR << "Failed to open versioned backup file for writing.";
    } else {
      versionedFile.write(doc.toJson(QJsonDocument::Indented));
      versionedFile.close();
      LOG_INFO << "Successfully created versioned backup of Rust settings.";
    }

    // Also create a raw text copy of the config file
    QFile rawCopy(backupDir + "/client.cfg.txt");
    if (rawCopy.open(QIODevice::WriteOnly | QIODevice::Text)) {
      rawCopy.write(rawContent);
      rawCopy.close();
      LOG_INFO << "Created raw text copy of client.cfg in backup directory";
    }
  }

  // Backup additional configuration files
  bool additionalBackupSuccess = true;
  QStringList filesToBackup = {"favorites.cfg", "keys.cfg", "keys_default.cfg"};

  // Check for existing backup file to migrate old format data
  QJsonObject existingBackupObj;
  bool hasExistingBackup = false;
  QString existingBackupPath = GetBackupFilePath();

  if (QFile::exists(existingBackupPath)) {
    QFile existingFile(existingBackupPath);
    if (existingFile.open(QIODevice::ReadOnly)) {
      QJsonDocument existingDoc =
        QJsonDocument::fromJson(existingFile.readAll());
      existingFile.close();

      if (!existingDoc.isNull() && existingDoc.isObject()) {
        existingBackupObj = existingDoc.object();
        hasExistingBackup = true;
      }
    }
  }

  for (const QString& filename : filesToBackup) {
    // Always backup to standard location
    if (!BackupConfigFile(filename)) {
      additionalBackupSuccess = false;
    }

    // If creating versioned backup, also backup to versioned location
    if (createVersioned) {
      if (!BackupConfigFileToDir(filename, backupDir)) {
        additionalBackupSuccess = false;
      }

      // Additionally, create human-readable JSON backups
      QString cfgDir = GetRustCfgDirectory();
      QString sourceFilePath = cfgDir + "/" + filename;
      QFile sourceFile(sourceFilePath);

      // If can read from source file, create new human-readable backup
      if (sourceFile.exists() &&
          sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Create a JSON object to store the file content
        QJsonObject fileObj;

        if (filename == "favorites.cfg") {
          // For favorites.cfg which is already JSON, parse it
          QByteArray content = sourceFile.readAll();
          sourceFile.close();

          QJsonDocument favoriteDoc = QJsonDocument::fromJson(content);
          if (!favoriteDoc.isNull()) {
            // Store the parsed JSON directly
            fileObj = favoriteDoc.object();
          } else {
            // If parsing fails, store as lines array
            QJsonArray linesArray;
            QStringList contentLines = QString::fromUtf8(content).split('\n');
            for (const QString& line : contentLines) {
              if (!line.trimmed().isEmpty()) {
                linesArray.append(line);
              }
            }
            fileObj["lines"] = linesArray;
          }
        } else {
          // For keys files, store as array of lines
          QJsonArray linesArray;
          QTextStream in(&sourceFile);
          while (!in.atEnd()) {
            QString line = in.readLine();
            if (!line.trimmed().isEmpty()) {
              linesArray.append(line);
            }
          }
          fileObj["bindings"] = linesArray;
          sourceFile.close();
        }

        // Add timestamp for reference
        fileObj["timestamp"] =
          QDateTime::currentDateTime().toString(Qt::ISODate);

        // Write the JSON backup
        QString jsonBackupPath = backupDir + "/" + filename + ".json";
        QFile jsonBackupFile(jsonBackupPath);

        if (jsonBackupFile.open(QIODevice::WriteOnly)) {
          QJsonDocument fileDoc(fileObj);
          jsonBackupFile.write(fileDoc.toJson(QJsonDocument::Indented));
          jsonBackupFile.close();
          LOG_INFO << "Created human-readable JSON backup of "
                    << filename.toStdString();
        } else {
          LOG_ERROR << "Failed to create JSON backup of "
                    << filename.toStdString();
          additionalBackupSuccess = false;
        }
      }
      // If source file not available but have existing backup with old format,
      // try to convert it
      else if (hasExistingBackup) {
        // Convert filename (e.g., "favorites.cfg") to JSON key (e.g.,
        // "favorites_cfg")
        QString jsonKey = filename;
        jsonKey.replace(".cfg", "_cfg");

        if (existingBackupObj.contains(jsonKey)) {
          QJsonObject oldBackupObj = existingBackupObj[jsonKey].toObject();
          QJsonObject newFileObj;

          // Convert from various old formats to new human-readable format
          if (filename == "favorites.cfg") {
            // For favorites.cfg, try to parse the content as JSON
            QString oldContent;

            if (oldBackupObj.contains("content_base64")) {
              QByteArray contentBytes = QByteArray::fromBase64(
                oldBackupObj["content_base64"].toString().toLatin1());
              oldContent = QString::fromUtf8(contentBytes);
            } else if (oldBackupObj.contains("content")) {
              oldContent = oldBackupObj["content"].toString();
            }

            if (!oldContent.isEmpty()) {
              QJsonDocument favoriteDoc =
                QJsonDocument::fromJson(oldContent.toUtf8());
              if (!favoriteDoc.isNull()) {
                // Store the parsed JSON directly
                newFileObj = favoriteDoc.object();
              } else {
                // If parsing fails, store as lines array
                QJsonArray linesArray;
                QStringList contentLines = oldContent.split('\n');
                for (const QString& line : contentLines) {
                  if (!line.trimmed().isEmpty()) {
                    linesArray.append(line);
                  }
                }
                newFileObj["lines"] = linesArray;
              }
            }
          } else {
            // For keys files, convert to an array of lines
            QString oldContent;

            if (oldBackupObj.contains("content_base64")) {
              QByteArray contentBytes = QByteArray::fromBase64(
                oldBackupObj["content_base64"].toString().toLatin1());
              oldContent = QString::fromUtf8(contentBytes);
            } else if (oldBackupObj.contains("content")) {
              oldContent = oldBackupObj["content"].toString();
            }

            if (!oldContent.isEmpty()) {
              QJsonArray linesArray;
              QStringList contentLines = oldContent.split('\n');
              for (const QString& line : contentLines) {
                if (!line.trimmed().isEmpty()) {
                  linesArray.append(line);
                }
              }
              newFileObj["bindings"] = linesArray;
            }
          }

          // Add timestamp
          newFileObj["timestamp"] =
            QDateTime::currentDateTime().toString(Qt::ISODate);
          newFileObj["migrated_from_old_format"] = true;

          // Write the JSON backup if we have content
          if (!newFileObj.isEmpty()) {
            QString jsonBackupPath = backupDir + "/" + filename + ".json";
            QFile jsonBackupFile(jsonBackupPath);

            if (jsonBackupFile.open(QIODevice::WriteOnly)) {
              QJsonDocument fileDoc(newFileObj);
              jsonBackupFile.write(fileDoc.toJson(QJsonDocument::Indented));
              jsonBackupFile.close();
              LOG_INFO << "Migrated " << filename.toStdString()
                        << " backup to human-readable format";
            } else {
              LOG_ERROR << "Failed to write migrated " << filename.toStdString()
                        << " backup";
              additionalBackupSuccess = false;
            }
          }
        }
      }
    }
  }

  if (!additionalBackupSuccess) {
    LOG_WARN << "Warning: Some additional configuration files could not be backed up.";
  }

  return true;
}

bool RustConfigManager::HasBackup() const {
  // Check if backup exists using BackupManager's path
  auto& backupManager = BackupManager::GetInstance();
  return backupManager.CheckBackupStatus(BackupType::RustConfig, false) ==
           BackupStatus::CompleteBackup ||
         backupManager.CheckBackupStatus(BackupType::RustConfig, true) ==
           BackupStatus::CompleteBackup;
}

bool RustConfigManager::RestoreFromBackup() {
  // Check if backup exists using BackupManager
  auto& backupManager = BackupManager::GetInstance();
  if (backupManager.CheckBackupStatus(BackupType::RustConfig, false) !=
        BackupStatus::CompleteBackup &&
      backupManager.CheckBackupStatus(BackupType::RustConfig, true) !=
        BackupStatus::CompleteBackup) {
    LOG_ERROR << "No Rust config backup found in settings_backup directory.";
    return false;
  }

  // Prioritize main backup over session backup if available
  bool useMainBackup =
    backupManager.CheckBackupStatus(BackupType::RustConfig, true) ==
    BackupStatus::CompleteBackup;
  QString backupPath =
    backupManager.GetBackupFilePath(BackupType::RustConfig, !useMainBackup);

  if (!Initialize()) {
    LOG_ERROR << "Failed to initialize Rust config manager.";
    return false;
  }

  // Read the backup file
  QFile backupFile(backupPath);
  if (!backupFile.open(QIODevice::ReadOnly)) {
    LOG_ERROR << "Failed to open Rust backup file for reading: [path hidden for privacy]";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: could not open backup file"));
    return false;
  }

  QJsonDocument doc = QJsonDocument::fromJson(backupFile.readAll());
  backupFile.close();

  if (doc.isNull() || !doc.isObject()) {
    LOG_ERROR << "Backup file is not valid JSON.";
    return false;
  }

  QJsonObject backupObj = doc.object();

  // First, check if we have raw content to restore directly
  if (backupObj.contains("client_cfg_raw_content") ||
      backupObj.contains("raw_content")) {
    // Get the raw content from the backup
    QString rawContent;
    if (backupObj.contains("client_cfg_raw_content")) {
      rawContent = backupObj["client_cfg_raw_content"].toString();
    } else {
      rawContent = backupObj["raw_content"].toString();
    }

    if (!rawContent.isEmpty()) {
      {
        // Preserve existing file before overwriting.
        QString backupError;
        if (!ensureOriginalBackupExists(configFilePath, &backupError)) {
          LOG_ERROR << "Rust restore failed: " << backupError.toStdString();
          notifyRustConfigError(
            QStringLiteral("Rust settings restore failed: %1").arg(backupError));
          return false;
        }
        if (!createTimestampedOldBackup(configFilePath, "old", &backupError)) {
          LOG_ERROR << "Rust restore failed: " << backupError.toStdString();
          notifyRustConfigError(
            QStringLiteral("Rust settings restore failed: %1").arg(backupError));
          return false;
        }
      }

      // Write the raw content atomically to the config file.
      QSaveFile configFile(configFilePath);
      if (configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&configFile);
        out << rawContent;
        if (!configFile.commit()) {
          LOG_ERROR << "Rust restore failed: could not commit config file changes";
          notifyRustConfigError(
            QStringLiteral(
              "Rust settings restore failed: could not commit config file changes"));
          return false;
        }

        LOG_INFO << "Restored client.cfg using raw content from backup.";

        // Re-read settings to update our tracked settings
        ReadCurrentSettings();

        // Restore additional configuration files
        bool additionalRestoreSuccess = RestoreAdditionalConfigFiles();
        if (!additionalRestoreSuccess) {
          LOG_WARN << "Warning: Some additional configuration files could not "
                    "be restored.";
        }

        return true;
      } else {
        LOG_ERROR << "Rust restore failed: could not open config file for writing raw content";
        notifyRustConfigError(
          QStringLiteral("Rust settings restore failed: could not open config file for writing"));
        // Continue with normal restoration process as fallback
      }
    }
  }

  // If raw content restoration failed, fall back to individual settings
  // First, read the current configuration to get all current settings
  QFile configFile(configFilePath);
  if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open current config file for reading.";
    return false;
  }

  // Parse all current settings from the config file
  std::map<QString, QString> currentSettings;
  QTextStream in(&configFile);
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    // Keep only actual config lines (not comments or empty lines)
    if (!line.isEmpty() && !line.startsWith("//")) {
      // Handle both = and space-separated values
      QString key, value;
      int equalPos = line.indexOf('=');
      if (equalPos > 0) {
        key = line.left(equalPos).trimmed();
        value = line.mid(equalPos + 1).trimmed();
      } else {
        // Handle Rust's space-separated format: "key value"
        int spacePos = line.indexOf(' ');
        if (spacePos > 0) {
          key = line.left(spacePos).trimmed();
          value = line.mid(spacePos + 1).trimmed();
          // Remove quotes if present
          if (value.startsWith("\"") && value.endsWith("\"")) {
            value = value.mid(1, value.length() - 2);
          }
        }
      }
      if (!key.isEmpty()) {
        currentSettings[key] = value;  // Include empty values
      }
    }
  }
  configFile.close();

  // Start with the current settings to preserve any new settings that weren't
  // in the backup
  std::map<QString, QString> settingsToRestore = currentSettings;
  int restoredCount = 0;

  // Extract settings from backup (excluding metadata) and apply them
  if (backupObj.contains("client_cfg") && backupObj["client_cfg"].isObject()) {
    QJsonObject clientCfgObj = backupObj["client_cfg"].toObject();

    // Update the setting, whether it exists in current config or not
    for (auto it = clientCfgObj.begin(); it != clientCfgObj.end(); ++it) {
      if (it.key() != "metadata") {
        settingsToRestore[it.key()] = it.value().toString();
        restoredCount++;
      }
    }
  } else {
    // Check for old format where settings were at the root
    for (auto it = backupObj.begin(); it != backupObj.end(); ++it) {
      if (it.key() != "metadata" && it.key() != "raw_content" &&
          it.key() != "client_cfg_raw_content" &&
          it.key() != "client_cfg_lines" && it.key() != "client_cfg_metadata" &&
          !it.key().startsWith("favorites_cfg") &&
          !it.key().startsWith("keys_")) {
        settingsToRestore[it.key()] = it.value().toString();
        restoredCount++;
      }
    }
  }

  // Write the settings to the config file - this will now preserve all existing
  // settings while updating those from the backup
  {
    QString backupError;
    if (!createTimestampedOldBackup(configFilePath, "old", &backupError)) {
      LOG_ERROR << "Rust restore failed: " << backupError.toStdString();
      notifyRustConfigError(
        QStringLiteral("Rust settings restore failed: %1").arg(backupError));
      return false;
    }
  }
  if (!WriteConfigFile(settingsToRestore)) {
    LOG_ERROR << "Failed to write Rust configuration file after restore.";
    return false;
  }

  // Re-read settings to update our tracked settings
  ReadCurrentSettings();

  LOG_INFO << "Restored " << restoredCount << " settings from backup.";

  // Restore additional configuration files
  bool additionalRestoreSuccess = RestoreAdditionalConfigFiles();
  if (!additionalRestoreSuccess) {
    LOG_WARN << "Warning: Some additional configuration files could not be restored.";
  }

  return true;
}

QString RustConfigManager::GetBackupFilePath() const {
  // Get the path from the BackupManager
  auto& backupManager = BackupManager::GetInstance();
  return backupManager.GetBackupFilePath(
    BackupType::RustConfig, false);  // Use session backup as default
}

QString RustConfigManager::GetRawConfigContent() const {
  if (configFilePath.isEmpty()) {
    return QString();
  }

  QFile file(configFilePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open config file for reading: [path hidden for privacy]";
    notifyRustConfigError(
      QStringLiteral("Rust settings read failed: could not open config file"));
    return QString();
  }

  QTextStream in(&file);
  QString content = in.readAll();
  file.close();

  return content;
}

void RustConfigManager::InitializeFocusedSettings() {
  // We'll now define settings in the settings map directly
  // This replaces the old focusedSettings list

  // Initialize boolean settings (true/false toggles)
  QStringList boolSettings = {"graphics.contactshadows",
                              "graphics.dof",
                              "graphics.grassshadows",
                              "graphicssettings.billboardsfacecameraposition",
                              "graphicssettings.softparticles",
                              "effects.ao",
                              "effects.bloom",
                              "effects.lensdirt",
                              "effects.motionblur",
                              "effects.shafts",
                              "effects.sharpen",
                              "effects.vignet",
                              "grass.displacement",
                              "system.auto_cpu_affinity"};

  // Create possible values for boolean settings
  QList<QVariant> boolValues = {QVariant("True"), QVariant("False")};

  // Initialize each setting from the expectedValues map
  std::map<QString, QString> tempExpectedValues = GetExpectedValues();
  for (const auto& [key, value] : tempExpectedValues) {
    RustSetting setting;
    setting.key = key;
    setting.optimalValue = value;
    setting.currentValue =
      "unknown";                 // Will be populated during ReadCurrentSettings
    setting.isDifferent = true;  // Assume different until checked

    // Determine if this is a boolean setting
    setting.isBool = boolSettings.contains(key);

    // Set possible values based on setting type
    if (setting.isBool) {
      setting.possibleValues = boolValues;
    } else {
      // For non-boolean settings, we need to determine appropriate ranges
      if (key == "graphics.af") {
        setting.possibleValues = {QVariant("1"), QVariant("2"), QVariant("4"),
                                  QVariant("8"), QVariant("16")};
      } else if (key == "graphics.maxqueuedframes") {
        setting.possibleValues = {QVariant("1"), QVariant("2"), QVariant("3")};
      } else if (key == "graphics.drawdistance") {
        // Common draw distance values
        setting.possibleValues = {QVariant("500"), QVariant("1000"),
                                  QVariant("1500"), QVariant("2000"),
                                  QVariant("2500")};
      } else if (key == "graphics.lodbias") {
        setting.possibleValues = {QVariant("1"), QVariant("5")};
      } else if (key == "graphics.parallax") {
        setting.possibleValues = {QVariant("0"), QVariant("1")};
      } else if (key == "graphics.reflexmode") {
        setting.possibleValues = {QVariant("0"), QVariant("1"), QVariant("2")};
      } else if (key == "graphics.shaderlod") {
        // Newer Rust builds use lower shader LOD values (e.g., 1)
        setting.possibleValues = {QVariant("1"), QVariant("2"), QVariant("3")};
      } else if (key == "graphics.shadowlights") {
        setting.possibleValues = {QVariant("0"), QVariant("3")};
      } else if (key == "graphics.shadowmode") {
        setting.possibleValues = {QVariant("0"), QVariant("1")};
      } else if (key == "graphics.shadowquality") {
        setting.possibleValues = {QVariant("0"), QVariant("1")};
      } else if (key == "graphicssettings.anisotropicfiltering") {
        setting.possibleValues = {QVariant("0"), QVariant("1"), QVariant("2")};
      } else if (key == "graphicssettings.globaltexturemipmaplimit") {
        setting.possibleValues = {QVariant("0"), QVariant("1"), QVariant("2"),
                                  QVariant("3")};
      } else if (key == "graphicssettings.particleraycastbudget") {
        setting.possibleValues = {QVariant("4"), QVariant("256"),
                                  QVariant("1024")};
      } else if (key == "graphicssettings.pixellightcount") {
        setting.possibleValues = {QVariant("0"), QVariant("4"), QVariant("8"),
                                  QVariant("16")};
      } else if (key == "graphicssettings.shadowcascades") {
        setting.possibleValues = {QVariant("1"), QVariant("2"), QVariant("4")};
      } else if (key == "graphicssettings.shadowdistancepercent") {
        setting.possibleValues = {QVariant("0"), QVariant("50"),
                                  QVariant("100")};
      } else if (key == "graphicssettings.shadowmaskmode") {
        setting.possibleValues = {QVariant("0"), QVariant("1")};
      } else if (key == "graphicssettings.shadowresolution") {
        setting.possibleValues = {QVariant("0"), QVariant("1"), QVariant("2"),
                                  QVariant("3")};
      } else if (key == "effects.antialiasing") {
        setting.possibleValues = {QVariant("0"), QVariant("2")};
      } else if (key == "global.asyncloadingpreset") {
        setting.possibleValues = {QVariant("0"), QVariant("1")};
      } else if (key == "grass.quality") {
        setting.possibleValues = {QVariant("0"), QVariant("50"),
                                  QVariant("100")};
      } else if (key == "mesh.quality") {
        setting.possibleValues = {QVariant("0"), QVariant("100")};
      } else if (key == "particle.quality") {
        setting.possibleValues = {QVariant("0"), QVariant("100")};
      } else if (key == "render.instanced_rendering") {
        setting.possibleValues = {QVariant("0"), QVariant("1")};
      } else if (key == "terrain.quality") {
        setting.possibleValues = {QVariant("0"), QVariant("100")};
      } else if (key == "tree.meshes") {
        setting.possibleValues = {QVariant("0"), QVariant("50"),
                                  QVariant("100")};
      } else if (key == "tree.quality") {
        setting.possibleValues = {QVariant("0"), QVariant("100"),
                                  QVariant("200")};
      } else if (key == "water.quality") {
        setting.possibleValues = {QVariant("0"), QVariant("2")};
      } else if (key == "water.reflections") {
        setting.possibleValues = {QVariant("0"), QVariant("2")};
      } else {
        // Default to the current expected value as the only option
        setting.possibleValues = {QVariant(value)};
      }
    }

    settings[key] = setting;
  }
}

// Add this helper method to return the expected values as a QString map
std::map<QString, QString> RustConfigManager::GetExpectedValues() const {
  // Return a map of expected optimal values
  return {// Graphics
          {"graphics.af", "1"},
          {"graphics.contactshadows", "False"},
          {"graphics.dof", "False"},
          {"graphics.drawdistance", "500"},
          {"graphics.grassshadows", "False"},
          {"graphics.lodbias", "5"},
          {"graphics.maxqueuedframes", "2"},
          {"graphics.parallax", "0"},
          {"graphics.reflexmode", "2"},
          {"graphics.shaderlod", "1"},
          {"graphics.shadowlights", "0"},
          {"graphics.shadowmode", "1"},
          {"graphics.shadowquality", "0"},

          // Graphics Settings
          {"graphicssettings.anisotropicfiltering", "0"},
          {"graphicssettings.billboardsfacecameraposition", "False"},
          {"graphicssettings.globaltexturemipmaplimit", "2"},
          {"graphicssettings.particleraycastbudget", "4"},
          {"graphicssettings.pixellightcount", "0"},
          {"graphicssettings.shadowcascades", "1"},
          {"graphicssettings.shadowdistancepercent", "0"},
          {"graphicssettings.shadowmaskmode", "0"},
          {"graphicssettings.shadowresolution", "0"},
          {"graphicssettings.softparticles", "False"},

          // Effects
          {"effects.antialiasing", "0"},
          {"effects.ao", "False"},
          {"effects.bloom", "False"},
          {"effects.lensdirt", "False"},
          {"effects.motionblur", "False"},
          {"effects.shafts", "False"},
          {"effects.sharpen", "True"},
          {"effects.vignet", "False"},

          // Other Categories
          {"global.asyncloadingpreset", "1"},
          {"grass.displacement", "False"},
          {"grass.quality", "0"},
          {"mesh.quality", "0"},
          {"particle.quality", "0"},
          {"render.instanced_rendering", "0"},
          {"system.auto_cpu_affinity", "True"},
          {"terrain.quality", "0"},
          {"tree.meshes", "100"},
          {"tree.quality", "0"},
          {"water.quality", "0"},
          {"water.reflections", "0"}};
}

void RustConfigManager::InitializeExpectedValues() {
  // This function is now empty since we get the expected values from
  // GetExpectedValues() We'll initialize our settings in
  // InitializeFocusedSettings() directly
}

QString RustConfigManager::GetRustCfgDirectory() const {
  if (configFilePath.isEmpty()) {
    return QString();
  }

  QFileInfo fileInfo(configFilePath);
  return fileInfo.absolutePath();
}

QString RustConfigManager::GetConfigBackupPath(const QString& filename) const {
  QString appDir = QCoreApplication::applicationDirPath();
  return appDir + "/profiles/rust_" + filename + "_backup";
}

bool RustConfigManager::BackupConfigFile(const QString& filename) {
  QString cfgDir = GetRustCfgDirectory();
  if (cfgDir.isEmpty()) {
    LOG_ERROR << "Failed to determine Rust cfg directory.";
    return false;
  }

  QString sourcePath = cfgDir + "/" + filename;
  QString backupPath = GetConfigBackupPath(filename);

  // If source file doesn't exist, we consider it a success (nothing to back up)
  if (!QFile::exists(sourcePath)) {
    LOG_INFO << "Config file not found, skipping backup: "
             << filename.toStdString();
    return true;
  }

  // Ensure backup directory exists
  QFileInfo backupFileInfo(backupPath);
  QDir backupDir = backupFileInfo.absoluteDir();
  if (!backupDir.exists()) {
    backupDir.mkpath(".");
  }

  // Avoid deleting: rotate any existing backup out of the way.
  if (QFile::exists(backupPath)) {
    const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    const QString rotated = backupPath + QStringLiteral(".old_") + ts;
    if (!QFile::rename(backupPath, rotated)) {
      LOG_WARN << "Warning: Failed to rotate existing Rust backup file.";
      notifyRustConfigWarning(
        QStringLiteral("Rust backup warning: could not rotate an existing backup file"));
    }
  }

  // Create the regular file backup
  bool success = true;
  if (QFile::copy(sourcePath, backupPath)) {
    LOG_INFO << "Successfully backed up " << filename.toStdString();
  } else {
    LOG_ERROR << "Failed to backup " << filename.toStdString();
    success = false;
  }

  // Also create a JSON backup with base64 encoding
  if (!CreateJsonBackup(sourcePath, backupPath + ".json")) {
    LOG_ERROR << "Failed to create JSON backup for " << filename.toStdString();
    success = false;
  }

  return success;
}

bool RustConfigManager::CreateJsonBackup(const QString& sourcePath,
                                         const QString& jsonBackupPath) {
  QFile sourceFile(sourcePath);
  if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open source file for JSON backup: [path hidden for privacy]";
    notifyRustConfigWarning(
      QStringLiteral("Rust backup warning: could not read a config file for backup"));
    return false;
  }

  // Create a JSON object to store the file content
  QJsonObject fileObj;

  // Get the filename from the source path
  QString filename = QFileInfo(sourcePath).fileName();

  if (filename == "favorites.cfg") {
    // For favorites.cfg which is already JSON, parse it
    QByteArray content = sourceFile.readAll();
    sourceFile.close();

    QJsonDocument favoriteDoc = QJsonDocument::fromJson(content);
    if (!favoriteDoc.isNull()) {
      // Store the parsed JSON directly
      fileObj = favoriteDoc.object();
    } else {
      // If parsing fails, store as lines array
      QJsonArray linesArray;
      QStringList contentLines = QString::fromUtf8(content).split('\n');
      for (const QString& line : contentLines) {
        if (!line.trimmed().isEmpty()) {
          linesArray.append(line);
        }
      }
      fileObj["lines"] = linesArray;
    }
  } else {
    // For keys files and other config files, store as array of lines
    QJsonArray linesArray;
    QTextStream in(&sourceFile);
    while (!in.atEnd()) {
      QString line = in.readLine();
      if (!line.trimmed().isEmpty()) {
        linesArray.append(line);
      }
    }
    fileObj["bindings"] = linesArray;
    sourceFile.close();
  }

  // Add timestamp for reference
  fileObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

  // Write the JSON backup
  QFile jsonBackupFile(jsonBackupPath);

  // Ensure directory exists
  QFileInfo fileInfo(jsonBackupPath);
  QDir().mkpath(fileInfo.path());

  if (jsonBackupFile.open(QIODevice::WriteOnly)) {
    QJsonDocument fileDoc(fileObj);
    jsonBackupFile.write(fileDoc.toJson(QJsonDocument::Indented));
    jsonBackupFile.close();
    LOG_INFO << "Created human-readable JSON backup.";
    return true;
  } else {
    LOG_ERROR << "Failed to create JSON backup.";
    return false;
  }
}

bool RustConfigManager::RestoreConfigFile(const QString& filename) {
  QString cfgDir = GetRustCfgDirectory();
  if (cfgDir.isEmpty()) {
    LOG_ERROR << "Failed to determine Rust cfg directory.";
    return false;
  }

  QString targetPath = cfgDir + "/" + filename;
  QString backupPath = GetConfigBackupPath(filename);

  // First check if we have a JSON backup with human-readable content
  QString jsonBackupPath = backupPath + ".json";
  if (QFile::exists(jsonBackupPath)) {
    QFile jsonFile(jsonBackupPath);
    if (jsonFile.open(QIODevice::ReadOnly)) {
      QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonFile.readAll());
      jsonFile.close();

      if (!jsonDoc.isNull() && jsonDoc.isObject()) {
        QJsonObject jsonObj = jsonDoc.object();
        QString content;

        if (filename == "favorites.cfg") {
          // Handle favorites.cfg which is already in JSON format
          if (jsonObj.contains("lines") && jsonObj["lines"].isArray()) {
            // If stored as lines array, join them with newlines
            QJsonArray linesArray = jsonObj["lines"].toArray();
            QStringList lines;
            for (const QJsonValue& line : linesArray) {
              lines.append(line.toString());
            }
            content = lines.join("\n");
          } else if (jsonObj.contains("content_base64")) {
            // Handle old base64 format for backward compatibility
            QByteArray contentBytes = QByteArray::fromBase64(
              jsonObj["content_base64"].toString().toLatin1());
            content = QString::fromUtf8(contentBytes);
          } else if (jsonObj.contains("content")) {
            // Handle old direct content format
            content = jsonObj["content"].toString();
          } else {
            // It's a direct JSON object, convert it back to string
            // Remove our timestamp field first
            QJsonObject favoritesObj = jsonObj;
            favoritesObj.remove("timestamp");
            favoritesObj.remove("migrated_from_old_format");

            QJsonDocument favoritesDoc(favoritesObj);
            content = favoritesDoc.toJson(QJsonDocument::Indented);
          }
        } else {
          // Handle keys.cfg and keys_default.cfg
          if (jsonObj.contains("bindings") && jsonObj["bindings"].isArray()) {
            // New format - stored as array of binding lines
            QJsonArray bindingsArray = jsonObj["bindings"].toArray();
            QStringList lines;
            for (const QJsonValue& binding : bindingsArray) {
              lines.append(binding.toString());
            }
            content = lines.join("\n");
          } else if (jsonObj.contains("content_base64")) {
            // Old base64 format
            QByteArray contentBytes = QByteArray::fromBase64(
              jsonObj["content_base64"].toString().toLatin1());
            content = QString::fromUtf8(contentBytes);
          } else if (jsonObj.contains("content")) {
            // Old direct content format
            content = jsonObj["content"].toString();
          }
        }

        if (!content.isEmpty()) {
          // Preserve current file (avoid delete) so we can restore safely.
          if (QFile::exists(targetPath)) {
            const QString ts =
              QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
            const QString oldPath = targetPath + QStringLiteral(".old_") + ts;
            if (!QFile::rename(targetPath, oldPath)) {
              LOG_ERROR << "Failed to preserve existing config file before restore.";
              notifyRustConfigError(
                QStringLiteral("Rust settings restore failed: could not preserve existing %1")
                  .arg(filename));
              return false;
            }
          }

          // Write the content atomically to the target file.
          QSaveFile targetFile(targetPath);
          if (targetFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&targetFile);
            out << content;
            if (!targetFile.commit()) {
              LOG_ERROR << "Failed to commit restored config file.";
              notifyRustConfigError(
                QStringLiteral("Rust settings restore failed: could not commit %1")
                  .arg(filename));
              return false;
            }
            LOG_INFO << "Successfully restored " << filename.toStdString()
                     << " from JSON backup";
            return true;
          } else {
            LOG_ERROR << "Failed to open target file for restore.";
            notifyRustConfigError(
              QStringLiteral("Rust settings restore failed: could not write %1")
                .arg(filename));
            return false;
          }
        }
      }
    }
  }

  // If JSON backup doesn't exist or failed, fall back to regular file backup
  if (!QFile::exists(backupPath)) {
    LOG_INFO << "Backup not found, skipping restore: "
             << filename.toStdString();
    return true;
  }

  // Preserve current file (avoid delete) so we can restore safely.
  if (QFile::exists(targetPath)) {
    const QString ts =
      QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    const QString oldPath = targetPath + QStringLiteral(".old_") + ts;
    if (!QFile::rename(targetPath, oldPath)) {
      LOG_ERROR << "Failed to preserve existing config file before restore.";
      notifyRustConfigError(
        QStringLiteral("Rust settings restore failed: could not preserve existing %1")
          .arg(filename));
      return false;
    }
  }

  // Restore from backup atomically.
  QFile backupFile(backupPath);
  if (!backupFile.open(QIODevice::ReadOnly)) {
    LOG_ERROR << "Failed to open backup file for restore: [path hidden for privacy]";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: could not read backup for %1")
        .arg(filename));
    return false;
  }
  const QByteArray backupBytes = backupFile.readAll();
  backupFile.close();

  QSaveFile targetFile(targetPath);
  if (!targetFile.open(QIODevice::WriteOnly)) {
    LOG_ERROR << "Failed to open target file for restore.";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: could not write %1")
        .arg(filename));
    return false;
  }
  if (targetFile.write(backupBytes) != backupBytes.size()) {
    LOG_ERROR << "Failed to write restored data.";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: could not write %1")
        .arg(filename));
    return false;
  }
  if (!targetFile.commit()) {
    LOG_ERROR << "Failed to commit restored file.";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: could not commit %1")
        .arg(filename));
    return false;
  }

  LOG_INFO << "Successfully restored " << filename.toStdString();
  return true;
}

bool RustConfigManager::BackupAdditionalConfigFiles() {
  if (!Initialize()) {
    return false;
  }

  QStringList filesToRestore = {"favorites.cfg", "keys.cfg",
                                "keys_default.cfg"};
  bool allSuccess = true;

  for (const QString& filename : filesToRestore) {
    if (!RestoreConfigFile(filename)) {
      allSuccess = false;
    }
  }

  return allSuccess;
}

bool RustConfigManager::RestoreAdditionalConfigFiles() {
  if (!Initialize()) {
    return false;
  }

  QStringList filesToRestore = {"favorites.cfg", "keys.cfg",
                                "keys_default.cfg"};
  bool allSuccess = true;

  for (const QString& filename : filesToRestore) {
    if (!RestoreConfigFile(filename)) {
      allSuccess = false;
    }
  }

  return allSuccess;
}

QString RustConfigManager::GetBackupRoot() const {
  QString appDir = QCoreApplication::applicationDirPath();
  return appDir + "/profiles/rust_backups";
}

QString RustConfigManager::GetVersionedBackupDir() const {
  QString backupRoot = GetBackupRoot();
  QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd");
  return backupRoot + "/" + timestamp;
}

bool RustConfigManager::ShouldCreateNewVersionedBackup() const {
  // Check if the most recent backup is older than 30 days
  QString backupRoot = GetBackupRoot();
  QDir backupDir(backupRoot);

  if (!backupDir.exists()) {
    return true;  // No backups exist, should create one
  }

  // Get all backup directories sorted by name (which is date-based)
  QStringList subdirs = backupDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                            QDir::Name | QDir::Reversed);
  if (subdirs.isEmpty()) {
    return true;  // No backup directories, should create one
  }

  // Parse the date from the most recent backup directory
  QString latestBackup = subdirs.first();
  QDate backupDate = QDate::fromString(latestBackup, "yyyy-MM-dd");

  if (!backupDate.isValid()) {
    return true;  // Invalid date format, create a new backup
  }

  // Check if it's been more than 30 days since the last backup
  return backupDate.daysTo(QDate::currentDate()) > 30;
}

QStringList RustConfigManager::GetAvailableBackups() const {
  QString backupRoot = GetBackupRoot();
  QDir backupDir(backupRoot);

  if (!backupDir.exists()) {
    return QStringList();  // No backups exist
  }

  // Get all backup directories sorted by name (newest first)
  return backupDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                             QDir::Name | QDir::Reversed);
}

bool RustConfigManager::BackupConfigFileToDir(const QString& filename,
                                              const QString& backupDir) {
  QString cfgDir = GetRustCfgDirectory();
  if (cfgDir.isEmpty()) {
    LOG_ERROR << "Failed to determine Rust cfg directory.";
    return false;
  }

  QString sourcePath = cfgDir + "/" + filename;
  QString backupPath = backupDir + "/" + filename;

  // If source file doesn't exist, we consider it a success (nothing to back up)
  if (!QFile::exists(sourcePath)) {
    LOG_INFO << "Config file not found, skipping backup: "
             << filename.toStdString();
    return true;
  }

  // Ensure backup directory exists
  QDir dir(backupDir);
  if (!dir.exists()) {
    dir.mkpath(".");
  }

  // Create regular file backup
  bool success = true;
  if (QFile::copy(sourcePath, backupPath)) {
    LOG_INFO << "Successfully backed up " << filename.toStdString();
  } else {
    LOG_ERROR << "Failed to backup " << filename.toStdString();
    success = false;
  }

  // Additionally, create a human-readable JSON backup
  QFile sourceFile(sourcePath);
  if (sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    // Create a JSON object to store the file content
    QJsonObject fileObj;

    if (filename == "favorites.cfg") {
      // For favorites.cfg which is already JSON, parse it
      QByteArray content = sourceFile.readAll();
      sourceFile.close();

      QJsonDocument favoriteDoc = QJsonDocument::fromJson(content);
      if (!favoriteDoc.isNull()) {
        // Store the parsed JSON directly
        fileObj = favoriteDoc.object();
      } else {
        // If parsing fails, store as lines array
        QJsonArray linesArray;
        QStringList contentLines = QString::fromUtf8(content).split('\n');
        for (const QString& line : contentLines) {
          if (!line.trimmed().isEmpty()) {
            linesArray.append(line);
          }
        }
        fileObj["lines"] = linesArray;
      }
    } else {
      // For keys files, store as array of lines
      QJsonArray linesArray;
      QTextStream in(&sourceFile);
      while (!in.atEnd()) {
        QString line = in.readLine();
        if (!line.trimmed().isEmpty()) {
          linesArray.append(line);
        }
      }
      fileObj["bindings"] = linesArray;
      sourceFile.close();
    }

    // Add timestamp for reference
    fileObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // Write the JSON backup
    QString jsonBackupPath = backupDir + "/" + filename + ".json";
    QFile jsonBackupFile(jsonBackupPath);

    if (jsonBackupFile.open(QIODevice::WriteOnly)) {
      QJsonDocument fileDoc(fileObj);
      jsonBackupFile.write(fileDoc.toJson(QJsonDocument::Indented));
      jsonBackupFile.close();
      LOG_INFO << "Created human-readable JSON backup of "
                << filename.toStdString();
    } else {
      LOG_ERROR << "Failed to create JSON backup of " << filename.toStdString();
      success = false;
    }
  }

  return success;
}

bool RustConfigManager::ValidateAndUpdateBackup() {
  if (!Initialize() || !HasBackup()) {
    return false;
  }

  // Read the current config file
  QFile configFile(configFilePath);
  if (!configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open config file for validation.";
    return false;
  }

  // Parse all settings from the current config file
  std::map<QString, QString> currentSettings;
  QTextStream in(&configFile);
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (!line.isEmpty() && !line.startsWith("//")) {
      QString key, value;
      int equalPos = line.indexOf('=');
      if (equalPos > 0) {
        key = line.left(equalPos).trimmed();
        value = line.mid(equalPos + 1).trimmed();
      } else {
        int spacePos = line.indexOf(' ');
        if (spacePos > 0) {
          key = line.left(spacePos).trimmed();
          value = line.mid(spacePos + 1).trimmed();
          if (value.startsWith("\"") && value.endsWith("\"")) {
            value = value.mid(1, value.length() - 2);
          }
        }
      }
      if (!key.isEmpty() && !value.isEmpty()) {
        currentSettings[key] = value;
      }
    }
  }
  configFile.close();

  // Read the backup file
  QFile backupFile(GetBackupFilePath());
  if (!backupFile.open(QIODevice::ReadOnly)) {
    LOG_ERROR << "Failed to open backup file for validation.";
    return false;
  }

  QJsonDocument doc = QJsonDocument::fromJson(backupFile.readAll());
  backupFile.close();

  if (doc.isNull() || !doc.isObject()) {
    LOG_ERROR << "Backup file is not valid JSON.";
    return false;
  }

  QJsonObject backupObj = doc.object();
  bool needsUpdate = false;
  int addedSettings = 0;

  // Check for settings in current config that are missing from backup
  for (const auto& [key, value] : currentSettings) {
    if (!backupObj.contains(key) && key != "metadata") {
      backupObj[key] = value;
      needsUpdate = true;
      addedSettings++;
    }
  }

  // Update the backup if needed
  if (needsUpdate) {
    QJsonDocument updatedDoc(backupObj);
    if (!backupFile.open(QIODevice::WriteOnly)) {
      LOG_ERROR << "Failed to open backup file for writing updates.";
      return false;
    }

    backupFile.write(updatedDoc.toJson(QJsonDocument::Indented));
    backupFile.close();
    LOG_INFO << "Updated backup with " << addedSettings
             << " new settings from current config.";
  } else {
    LOG_INFO << "Backup is already up-to-date with current config.";
  }

  return true;
}

bool RustConfigManager::RestoreFromVersionedBackup(const QString& backupDir) {
  if (!Initialize()) {
    LOG_ERROR << "Failed to initialize Rust config manager.";
    return false;
  }

  QString fullBackupPath = GetBackupRoot() + "/" + backupDir;

  // Check if the specified backup exists
  QDir dir(fullBackupPath);
  if (!dir.exists()) {
    LOG_ERROR << "Specified backup directory does not exist.";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: selected backup not found"));
    return false;
  }

  // Read the client.cfg.json backup file
  QString jsonBackupPath = fullBackupPath + "/client.cfg.json";
  QFile backupFile(jsonBackupPath);
  if (!backupFile.open(QIODevice::ReadOnly)) {
    LOG_ERROR << "Failed to open versioned backup file for reading.";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: could not open versioned backup"));
    return false;
  }

  QJsonDocument doc = QJsonDocument::fromJson(backupFile.readAll());
  backupFile.close();

  if (doc.isNull() || !doc.isObject()) {
    LOG_ERROR << "Versioned backup file is not valid JSON.";
    return false;
  }

  QJsonObject backupObj = doc.object();

  // Create a map with just the settings from the backup that we want to restore
  std::map<QString, QString> backupSettings;
  int restoredCount = 0;

  // Extract settings from backup (excluding metadata)
  for (auto it = backupObj.begin(); it != backupObj.end(); ++it) {
    if (it.key() != "metadata") {
      backupSettings[it.key()] = it.value().toString();
      restoredCount++;
    }
  }

  // Write the settings to the config file - this will now preserve all existing
  // settings while updating those from the backup
  {
    QString backupError;
    if (!createTimestampedOldBackup(configFilePath, "old", &backupError)) {
      LOG_ERROR << "Rust restore failed: " << backupError.toStdString();
      notifyRustConfigError(
        QStringLiteral("Rust settings restore failed: %1").arg(backupError));
      return false;
    }
  }
  if (!WriteConfigFile(backupSettings)) {
    LOG_ERROR << "Failed to write Rust configuration file after restore.";
    return false;
  }

  // Re-read settings to update our tracked settings
  ReadCurrentSettings();

  LOG_INFO << "Restored " << restoredCount
           << " settings from versioned backup.";

  // Restore additional files like keys.cfg, etc. from the versioned backup
  QStringList filesToRestore = {"favorites.cfg", "keys.cfg",
                                "keys_default.cfg"};
  bool allSuccess = true;

  for (const QString& filename : filesToRestore) {
    if (!RestoreConfigFileFromDir(filename, fullBackupPath)) {
      allSuccess = false;
    }
  }

  if (!allSuccess) {
    LOG_WARN << "Some additional configuration files could not be "
                "restored from versioned backup.";
  }

  return true;
}

bool RustConfigManager::RestoreConfigFileFromDir(const QString& filename,
                                                 const QString& backupDir) {
  QString cfgDir = GetRustCfgDirectory();
  if (cfgDir.isEmpty()) {
    LOG_ERROR << "Failed to determine Rust cfg directory.";
    return false;
  }

  QString targetPath = cfgDir + "/" + filename;
  QString backupPath = backupDir + "/" + filename;

  // First check if we have a JSON backup
  QString jsonBackupPath = backupDir + "/" + filename + ".json";
  if (QFile::exists(jsonBackupPath)) {
    QFile jsonFile(jsonBackupPath);
    if (jsonFile.open(QIODevice::ReadOnly)) {
      QJsonDocument jsonDoc = QJsonDocument::fromJson(jsonFile.readAll());
      jsonFile.close();

      if (!jsonDoc.isNull() && jsonDoc.isObject()) {
        QJsonObject jsonObj = jsonDoc.object();
        QString content;

        if (filename == "favorites.cfg") {
          // For favorites.cfg, process as JSON
          if (jsonObj.contains("lines") && jsonObj["lines"].isArray()) {
            // If stored as lines array, join them with newlines
            QJsonArray linesArray = jsonObj["lines"].toArray();
            QStringList lines;
            for (const QJsonValue& line : linesArray) {
              lines.append(line.toString());
            }
            content = lines.join("\n");
          } else if (jsonObj.contains("content_base64")) {
            // Legacy base64 format
            QByteArray contentBytes = QByteArray::fromBase64(
              jsonObj["content_base64"].toString().toLatin1());
            content = QString::fromUtf8(contentBytes);
          } else if (jsonObj.contains("content")) {
            // Legacy direct content format
            content = jsonObj["content"].toString();
          } else {
            // It's a direct JSON object, convert back to string
            // Remove our timestamp and other metadata fields first
            QJsonObject favoritesObj = jsonObj;
            favoritesObj.remove("timestamp");
            favoritesObj.remove("migrated_from_old_format");

            QJsonDocument favoritesDoc(favoritesObj);
            content = favoritesDoc.toJson(QJsonDocument::Indented);
          }
        } else {
          // For keys files, handle binding arrays
          if (jsonObj.contains("bindings") && jsonObj["bindings"].isArray()) {
            // New format with array of bindings
            QJsonArray bindingsArray = jsonObj["bindings"].toArray();
            QStringList lines;
            for (const QJsonValue& binding : bindingsArray) {
              lines.append(binding.toString());
            }
            content = lines.join("\n");
          } else if (jsonObj.contains("content_base64")) {
            // Legacy base64 format
            QByteArray contentBytes = QByteArray::fromBase64(
              jsonObj["content_base64"].toString().toLatin1());
            content = QString::fromUtf8(contentBytes);
          } else if (jsonObj.contains("content")) {
            // Legacy direct content format
            content = jsonObj["content"].toString();
          }
        }

        if (!content.isEmpty()) {
          // Preserve current file (avoid delete) so we can restore safely.
          if (QFile::exists(targetPath)) {
            const QString ts =
              QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
            const QString oldPath = targetPath + QStringLiteral(".old_") + ts;
            if (!QFile::rename(targetPath, oldPath)) {
              LOG_ERROR << "Failed to preserve existing config file before restore.";
              notifyRustConfigError(
                QStringLiteral("Rust settings restore failed: could not preserve existing %1")
                  .arg(filename));
              return false;
            }
          }

          // Write content atomically to target file
          QSaveFile targetFile(targetPath);
          if (!targetFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            LOG_ERROR << "Failed to open target file for restore.";
            notifyRustConfigError(
              QStringLiteral("Rust settings restore failed: could not write %1")
                .arg(filename));
            return false;
          }
          QTextStream out(&targetFile);
          out << content;
          if (!targetFile.commit()) {
            LOG_ERROR << "Failed to commit restored config file.";
            notifyRustConfigError(
              QStringLiteral("Rust settings restore failed: could not commit %1")
                .arg(filename));
            return false;
          }

          LOG_INFO << "Successfully restored " << filename.toStdString()
                   << " from JSON backup";
          return true;
        }
      }
    }
  }

  // If JSON backup doesn't exist or failed, fall back to regular file backup
  if (!QFile::exists(backupPath)) {
    LOG_WARN << "Backup not found, skipping restore: "
             << filename.toStdString();
    return true;
  }

  // Preserve current file (avoid delete) so we can restore safely.
  if (QFile::exists(targetPath)) {
    const QString ts =
      QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
    const QString oldPath = targetPath + QStringLiteral(".old_") + ts;
    if (!QFile::rename(targetPath, oldPath)) {
      LOG_ERROR << "Failed to preserve existing config file before restore.";
      notifyRustConfigError(
        QStringLiteral("Rust settings restore failed: could not preserve existing %1")
          .arg(filename));
      return false;
    }
  }

  // Restore from backup atomically.
  QFile backupFile(backupPath);
  if (!backupFile.open(QIODevice::ReadOnly)) {
    LOG_ERROR << "Failed to open backup file for restore: [path hidden for privacy]";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: could not read backup for %1")
        .arg(filename));
    return false;
  }
  const QByteArray backupBytes = backupFile.readAll();
  backupFile.close();

  QSaveFile targetFile(targetPath);
  if (!targetFile.open(QIODevice::WriteOnly)) {
    LOG_ERROR << "Failed to open target file for restore.";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: could not write %1")
        .arg(filename));
    return false;
  }
  if (targetFile.write(backupBytes) != backupBytes.size()) {
    LOG_ERROR << "Failed to write restored data.";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: could not write %1")
        .arg(filename));
    return false;
  }
  if (!targetFile.commit()) {
    LOG_ERROR << "Failed to commit restored file.";
    notifyRustConfigError(
      QStringLiteral("Rust settings restore failed: could not commit %1")
        .arg(filename));
    return false;
  }

  LOG_INFO << "Successfully restored " << filename.toStdString();
  return true;
}

}  // namespace rust
}  // namespace optimizations
