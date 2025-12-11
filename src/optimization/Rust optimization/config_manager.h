#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QString>
#include <QTextStream>
#include <QVariant>

namespace optimizations {
namespace rust {

/**
 * @struct RustSetting
 * @brief Structure to hold info about a Rust config setting
 */
struct RustSetting {
  QString key;
  QString currentValue;
  QString optimalValue;
  QString description;
  bool isDifferent;
  bool isBool =
    false;  // Whether this is a boolean setting (toggle vs dropdown)
  QList<QVariant>
    possibleValues;  // List of possible values for dropdown settings
};

/**
 * @class RustConfigManager
 * @brief Manages finding, reading, and validating Rust game configuration
 * settings
 *
 * Uses methods from RustBenchmarkFinder, DemoFileManager, and RustConfigFinder
 * to locate the Rust configuration file and provides functionality to check
 * settings against expected values.
 */
class RustConfigManager {
 public:
  static RustConfigManager& GetInstance();

  /**
   * @brief Initializes the config manager
   * @return True if initialization was successful
   */
  bool Initialize();

  /**
   * @brief Find the Rust configuration file
   * @return Path to the configuration file, or empty string if not found
   */
  QString FindConfigFile();

  /**
   * @brief Check current settings against optimal values
   * @return Number of different settings found, or -1 on error
   */
  int CheckSettings();

  /**
   * @brief Get the list of settings that differ from optimal
   * @return List of settings that differ from optimal
   */
  const std::vector<RustSetting>& GetDifferentSettings() const;

  /**
   * @brief Get all settings
   * @return List of all settings
   */
  const std::map<QString, RustSetting>& GetAllSettings() const;

  /**
   * @brief Apply the optimal settings to the configuration file
   * @return True if settings were applied successfully
   */
  bool ApplyOptimalSettings();

  /**
   * @brief Apply a single setting
   * @param key Setting key
   * @param value New value
   * @return True if successful
   */
  bool ApplySetting(const QString& key, const QString& value);

  /**
   * @brief Create a backup of the current settings
   * @return True if backup was created successfully
   */
  bool CreateBackup();

  /**
   * @brief Check if a backup exists
   * @return True if a backup exists
   */
  bool HasBackup() const;

  /**
   * @brief Restore settings from backup
   * @return True if settings were restored successfully
   */
  bool RestoreFromBackup();

  /**
   * @brief Restore settings from a specific versioned backup
   * @param backupDir Name of backup directory (e.g., "2023-05-15")
   * @return True if settings were restored successfully
   */
  bool RestoreFromVersionedBackup(const QString& backupDir);

  /**
   * @brief Get the path where backups are stored
   * @return Path to backup file
   */
  QString GetBackupFilePath() const;

  /**
   * @brief Get the map of expected (optimal) values
   * @return Map of setting keys to their optimal values
   */
  std::map<QString, QString> GetExpectedValues() const;

  /**
   * @brief Get the raw content of the current config file
   * @return Raw config content as a string
   */
  QString GetRawConfigContent() const;

  /**
   * @brief Create a backup of additional Rust config files
   * @return True if all available files were backed up successfully
   */
  bool BackupAdditionalConfigFiles();

  /**
   * @brief Restore additional Rust config files from backup
   * @return True if all available files were restored successfully
   */
  bool RestoreAdditionalConfigFiles();

  /**
   * @brief Check and update the backup with any new settings from current
   * config
   * @return True if backup was updated or was already up-to-date
   */
  bool ValidateAndUpdateBackup();

  /**
   * @brief Get a list of all available backups
   * @return List of available backup directories sorted by date (newest first)
   */
  QStringList GetAvailableBackups() const;

  /**
   * @brief Get the directory where Rust cfg files are stored
   * @return Path to the cfg directory
   */
  QString GetRustCfgDirectory() const;

 private:
  RustConfigManager();

  // Private methods
  QString configFilePath;
  std::map<QString, RustSetting> settings;
  std::map<QString, RustSetting> expectedValues;
  std::vector<RustSetting> differentSettings;
  bool initialized = false;

  /**
   * @brief Initialize the expected values for settings
   */
  void InitializeExpectedValues();

  /**
   * @brief Initialize the focused settings list from user-defined list
   */
  void InitializeFocusedSettings();

  /**
   * @brief Validate a path to ensure it exists and is readable
   * @param path Path to validate
   * @return True if path is valid
   */
  bool ValidatePath(const QString& path) const;

  /**
   * @brief Read the current settings from the config file
   * @return True if successful
   */
  bool ReadCurrentSettings();

  /**
   * @brief Write settings to the config file
   * @param settings Settings to write
   * @return True if successful
   */
  bool WriteConfigFile(const std::map<QString, QString>& settings);

  /**
   * @brief Create a backup using the old system (keeps original functionality)
   * @return True if successful
   */
  bool CreateBackupUsingOldSystem();

  /**
   * @brief Get the backup path for a specific config file
   * @param filename Original config filename (e.g., "keys.cfg")
   * @return Full path to the backup file
   */
  QString GetConfigBackupPath(const QString& filename) const;

  /**
   * @brief Get a versioned backup directory based on current date
   * @return Path to a timestamped backup directory
   */
  QString GetVersionedBackupDir() const;

  /**
   * @brief Get the backup directory root where all backups are stored
   * @return Path to backup directory
   */
  QString GetBackupRoot() const;

  /**
   * @brief Determines if a new versioned backup should be created
   * @return True if a new versioned backup should be created
   */
  bool ShouldCreateNewVersionedBackup() const;

  /**
   * @brief Backup a single config file
   * @param filename Name of the file to backup (e.g., "keys.cfg")
   * @return True if successful or if file doesn't exist
   */
  bool BackupConfigFile(const QString& filename);

  /**
   * @brief Backup a single config file to a specific backup directory
   * @param filename Name of the file to backup (e.g., "keys.cfg")
   * @param backupDir Target backup directory
   * @return True if successful or if file doesn't exist
   */
  bool BackupConfigFileToDir(const QString& filename, const QString& backupDir);

  /**
   * @brief Restore a single config file from backup
   * @param filename Name of the file to restore (e.g., "keys.cfg")
   * @return True if successful or if backup doesn't exist
   */
  bool RestoreConfigFile(const QString& filename);

  /**
   * @brief Restore a single config file from a specific backup directory
   * @param filename Name of the file to restore (e.g., "keys.cfg")
   * @param backupDir Source backup directory
   * @return True if successful or if backup doesn't exist
   */
  bool RestoreConfigFileFromDir(const QString& filename,
                                const QString& backupDir);

  /**
   * @brief Create a JSON backup of a file with base64 encoding to preserve
   * formatting
   * @param sourcePath Path to the source file
   * @param jsonBackupPath Path to the JSON backup file
   * @return True if successful
   */
  bool CreateJsonBackup(const QString& sourcePath,
                        const QString& jsonBackupPath);
};

}  // namespace rust
}  // namespace optimizations
