/**
 * @file BackupManager.h
 * @brief Centralized backup manager for all optimization settings
 *
 * This class provides a unified approach to backing up and restoring various
 * system settings before they are modified by the optimization process.
 *
 * Backup Philosophy:
 * - "Main" backups (settings_backup/main/*.json) are created when a setting is
 * first encountered by the application during a backup operation. They aim to
 * capture the user's original system settings *before* Checkmark applies any
 * changes for that specific setting or group of settings (like a Rust config
 * file).
 * - Once a setting's value (or a file's content) is recorded in a main backup,
 * that specific original value/content for that setting/file is PRESERVED and
 * should NOT be overwritten by subsequent current system values during later
 * main backup operations.
 * - Main backups are ADDITIVE for *new* settings or *new* keys within
 * structured files (e.g., new keys in Rust's client_cfg). If a main backup file
 * already exists:
 *     - For itemized lists (Registry, Nvidia): Existing setting IDs and their
 * backed-up values are kept. New setting IDs found on the system are added with
 * their current values.
 *     - For single-value files (PowerPlan, VisualEffects): The file is
 * typically written once and not updated unless missing.
 *     - For structured files (Rust client_cfg): Existing key-value pairs are
 * preserved. New keys found in the current system's file are added with their
 * current values.
 * - The values from main backups are used to initialize the `original_value_`
 * of `OptimizationEntity` instances and are tagged "(Original)" in the UI.
 *
 * **Missing Registry Settings Backup:**
 * - Registry settings that don't exist on the user's system are handled
 * specially.
 * - If a setting doesn't exist, its original value is recorded as
 * "NON_EXISTENT" in the backup.
 * - When the user creates a missing setting via the "Add Setting" button, the
 * backup system records "NON_EXISTENT" as the original state.
 * - During restore operations, settings with "NON_EXISTENT" original values are
 * deleted from the registry to return the system to its original state.
 * - This ensures that user-created registry settings can be completely removed
 * during restoration.
 *
 * - "Session" backups (settings_backup/session/*.json) are created/updated at
 * the start of each application session and reflect the system settings at that
 * point in time. They ARE overwritten each session.
 *
 * Backup File Structure:
 * - All backups are stored under the `settings_backup` directory relative to
 * the application executable.
 * - Subdirectories:
 *   - `main/`: Contains main backups. Values here are considered the user's
 * true original settings for Checkmark.
 *   - `session/`: Contains session backups, refreshed each session.
 *   - `archive/`: Stores older or potentially corrupted backups for recovery
 * purposes.
 * - Each backup type is stored in a JSON file named according to its type
 * (e.g., `registry.json`, `nvidia.json`).
 *
 * JSON File Formats:
 *   - `registry.json` & `nvidia.json`:
 *     ```json
 *     {
 *       "backup_type": "main"/"session",
 *       "timestamp": "ISO8601_DATETIME_STRING", // Timestamp of this backup
 * file's creation/last update "last_updated": "ISO8601_DATETIME_STRING", //
 * (For main backups) Timestamp of last merge/update "version": 1,
 *       "registry_settings"/"nvidia_settings": [
 *         {
 *           "id": "setting_id_string",
 *           "name": "Setting Name",
 *           "current_value": "VALUE" // For main backup, this is the value at
 * the time of FIRST backup for this ID.
 *                                  // For session backup, this is value at
 * session start.
 *           // (Optional for registry): "registry_key", "registry_value_name"
 *         }
 *         // ... more settings
 *       ]
 *     }
 *     ```
 *     Main backup: When updating, existing IDs preserve their `current_value`.
 * New IDs are added with their system value.
 *
 *   - `power_plan.json`:
 *     ```json
 *     {
 *       "backup_type": "main"/"session",
 *       "timestamp": "ISO8601_DATETIME_STRING",
 *       "version": 1,
 *       "guid": "{POWER_PLAN_GUID_STRING}",
 *       "name": "Power Plan Name"
 *     }
 *     ```
 *     Main backup: Written once. Not updated unless file is missing.
 *
 *   - `visual_effects.json`:
 *     ```json
 *     {
 *       "backup_type": "main"/"session",
 *       "timestamp": "ISO8601_DATETIME_STRING",
 *       "version": 1,
 *       "profile": INTEGER_PROFILE_ID, // Corresponds to VisualEffectsProfile
 * enum "profile_name": "Profile Name String"
 *     }
 *     ```
 *     Main backup: Written once. Not updated unless file is missing.
 *
 *   - `rust_config.json`:
 *     ```json
 *     {
 *       "backup_type": "main"/"session",
 *       "timestamp": "ISO8601_DATETIME_STRING",
 *       "metadata": { "version": "1.0", "last_updated":
 * "ISO8601_DATETIME_STRING", ... }, "client_cfg": { "setting_key_1":
 * "original_value1", // For main backup, value at time of first backup for this
 * key. "setting_key_2": "original_value2",
 *         // ... all settings from client.cfg
 *       },
 *       "favorites_cfg": { JSON representation of favorites.cfg at time of
 * backup }, "keys_cfg": { "bindings": ["line1", "line2", ...]  Content of
 * keys.cfg at time of backup  }, "keys_default_cfg": { "bindings": ["line1",
 * "line2", ...]  Content of keys_default.cfg at time of backup  }
 *     }
 *
 *     Main backup of `client_cfg`: Existing key-value pairs preserve their
 * original backed-up values. New keys found in the current `client.cfg` are
 * added with their current system values. Other `.cfg` file representations
 * (`favorites_cfg`, etc.) in main backup are typically written once and not
 * updated unless the entire `rust_config.json` was missing.
 *
 *   - `user_preferences.json`: Stores UI preferences like `dont_edit` flags for
 * settings. Not a main/session backup.
 *     ```json
 *     {
 *       "setting_id_1": true, // true if user set "don't edit"
 *       "setting_id_2": false
 *     }
 *     ```
 *
 *   - `unknown_values.json`: Tracks custom setting values entered or detected
 * by the user. Not a main/session backup.
 *     ```json
 *     {
 *       "setting_id_1": [
 *         { "type": "QVariant_TypeName", "value": ... },
 *         { "type": "QVariant_TypeName", "value": ... }
 *       ]
 *     }
 *     ```
 *
 * Unknown Setting Values:
 * - When a system setting's current value isn't in a predefined options list,
 * it's tracked as an "unknown value".
 * - These are stored in `settings_backup/unknown_values.json`.
 * - OptimizeView loads these values and adds them to dropdowns as "(Custom)"
 * options.
 * - This ensures user customizations are preserved and accessible.
 *
 * Backup Status Logic (Simplified for `CreateBackup`):
 * - Main Backups: Created if `NoBackupExists` or `PartialBackup`. If
 * `CompleteBackup`, still processed to add any newly discovered settings/keys
 * without overwriting existing original values.
 * - Session Backups: Created if `NoBackupExists`, `PartialBackup`, or
 * `OutdatedSessionBackup` (these are overwritten).
 *
 * IMPORTANT: All component managers (NvidiaControlPanel, PowerPlanManager,
 * RustConfigManager, etc.) must use this BackupManager for creating and
 * restoring backups to ensure consistency and proper backup management.
 */

#pragma once

#include <map>
#include <memory>
#include <string>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>

namespace optimizations {

// Forward declarations
namespace rust {
class RustConfigManager;
}
namespace registry {
class RegistryBackupUtility;
}

/**
 * @brief Enumeration of optimization types that can be backed up
 */
enum class BackupType {
  Registry,
  RustConfig,
  NvidiaSettings,
  VisualEffects,
  PowerPlan,
  FullRegistryExport,  // New: Full registry .reg file export
  All
};

/**
 * @brief Status of a backup operation
 */
enum class BackupStatus {
  NoBackupExists,         // No backup file exists
  PartialBackup,          // Some settings missing from backup
  OutdatedSessionBackup,  // Session backup is from a previous session
  CompleteBackup,         // Backup complete, no action needed
  BackupError             // Error during backup operation
};

/**
 * @brief Singleton manager for all backup operations
 */
class BackupManager {
 public:
  /**
   * @brief Get singleton instance
   * @return Reference to the BackupManager instance
   */
  static BackupManager& GetInstance();

  /**
   * @brief Initialize the backup manager
   * @return True if initialization successful
   */
  bool Initialize();

  /**
   * @brief Check backup status for a specific type
   * @param type The type of backup to check
   * @param isMain Whether to check the main backup or session backup
   * @return Status of the backup
   */
  BackupStatus CheckBackupStatus(BackupType type, bool isMain);

  /**
   * @brief Create or update a backup for a specific type
   * @param type The type of settings to backup
   * @param isMain Whether to create/update the main backup
   * @return True if the backup was successful
   */
  bool CreateBackup(BackupType type, bool isMain);

  /**
   * @brief Create all backups if needed
   * @return True if all backups were successful
   */
  bool CreateAllBackupsIfNeeded();

  /**
   * @brief Create initial backups for all categories to facilitate testing
   * @return True if successful
   */
  bool CreateInitialBackups();

  /**
   * @brief Restore settings from backup
   * @param type The type of settings to restore
   * @param isMain Whether to restore from the main backup or session backup
   * @return True if restore was successful
   */
  bool RestoreFromBackup(BackupType type, bool isMain);

  /**
   * @brief Get the path to the backup directory
   * @return Path to the backup directory
   */
  QString GetBackupDirectory() const;

  /**
   * @brief Get the path to a specific backup file
   * @param type The type of backup
   * @param isMain Whether this is a main backup or session backup
   * @return Path to the backup file
   */
  QString GetBackupFilePath(BackupType type, bool isMain) const;

  /**
   * @brief Save user preferences (dont_edit flags, etc.) to a file
   * @return True if the save was successful
   */
  bool SaveUserPreferences();

  /**
   * @brief Load user preferences (dont_edit flags, etc.) from a file
   * @return True if the load was successful
   */
  bool LoadUserPreferences();

  /**
   * @brief Get the path to the user preferences file
   * @return Path to the user preferences file
   */
  QString GetUserPreferencesFilePath() const;

  /**
   * @brief Get the original value for a setting from the main backup
   * @param optimization_id ID of the optimization
   * @return Original value from main backup, or null QVariant if not found
   */
  QVariant GetOriginalValueFromBackup(const std::string& optimization_id) const;

  /**
   * @brief Set the dont_edit flag for a specific optimization
   * @param optimization_id ID of the optimization
   * @param dont_edit Value of the dont_edit flag
   * @return True if the flag was successfully set
   */
  bool SetDontEditFlag(const std::string& optimization_id, bool dont_edit);

  /**
   * @brief Get the dont_edit flag for a specific optimization
   * @param optimization_id ID of the optimization
   * @param default_value Default value to return if not found
   * @return Value of the dont_edit flag
   */
  bool GetDontEditFlag(const std::string& optimization_id,
                       bool default_value = true) const;

  /**
   * @brief Get the path to the unknown values file
   * @return Path to the unknown values file
   */
  QString GetUnknownValuesFilePath() const;

  /**
   * @brief Load unknown values from the backup file
   * @param unknownValues Reference to a map where values will be stored
   * @return True if the load was successful
   */
  bool LoadUnknownValues(QMap<QString, QList<QVariant>>& unknownValues) const;

  /**
   * @brief Save unknown values to the backup file
   * @param unknownValues Map of setting IDs to lists of unknown values
   * @return True if the save was successful
   */
  bool SaveUnknownValues(const QMap<QString, QList<QVariant>>& unknownValues);

  /**
   * @brief Add a missing setting to the main backup with its current system
   * value
   *
   * This method carefully adds a single missing setting to the main backup
   * without overriding any existing settings. It's used when a setting is found
   * on the system but wasn't captured in the original backup.
   *
   * @param optimization_id ID of the optimization setting
   * @param current_value Current system value to store as the "original" value
   * @return True if the setting was successfully added to the main backup
   */
  bool AddMissingSettingToMainBackup(const std::string& optimization_id,
                                     const QVariant& current_value);

  /**
   * @brief Record a non-existent setting in the main backup
   *
   * This method records that a registry setting didn't exist on the system
   * originally. When the user creates the setting via "Add Setting", this
   * backup entry allows complete restoration by deleting the user-created
   * setting during restore.
   *
   * @param optimization_id ID of the optimization setting that doesn't exist
   * @return True if the non-existent state was successfully recorded in the
   * main backup
   */
  bool RecordNonExistentSetting(const std::string& optimization_id);

 private:
  BackupManager();
  ~BackupManager() = default;

  BackupManager(const BackupManager&) = delete;
  BackupManager& operator=(const BackupManager&) = delete;

  /**
   * @brief Create the backup directory if it doesn't exist
   * @return True if directory exists or was created
   */
  bool EnsureBackupDirectoryExists() const;

  /**
   * @brief Backup Rust configuration settings
   * @param isMain Whether to create a main backup or session backup
   * @return True if successful
   */
  bool BackupRustSettings(bool isMain);

  /**
   * @brief Backup registry settings
   * @param isMain Whether to create a main backup or session backup
   * @return True if successful
   */
  bool BackupRegistrySettings(bool isMain);

  /**
   * @brief Backup NVIDIA settings
   * @param isMain Whether to create a main backup or session backup
   * @return True if successful
   */
  bool BackupNvidiaSettings(bool isMain);

  /**
   * @brief Backup Visual Effects settings
   * @param isMain Whether to create a main backup or session backup
   * @return True if successful
   */
  bool BackupVisualEffectsSettings(bool isMain);

  /**
   * @brief Backup Power Plan settings
   * @param isMain Whether to create a main backup or session backup
   * @return True if successful
   */
  bool BackupPowerPlanSettings(bool isMain);

  /**
   * @brief Create full registry export backup using RegistryBackupUtility
   * @param isMain Whether to create a main backup or session backup
   * @return True if successful
   */
  bool BackupFullRegistryExport(bool isMain);

  /**
   * @brief Helper method to restore additional Rust config files
   * @param backupObj The backup JSON object containing additional config files
   * @param cfgDir Directory where Rust configuration files are stored
   */
  void RestoreRustAdditionalFiles(const QJsonObject& backupObj,
                                  const QString& cfgDir);

  /**
   * @brief Check if a file exists and is readable
   * @param path Path to the file
   * @return True if the file exists and is readable
   */
  bool FileExists(const QString& path) const;

  // Member variables
  bool initialized = false;

  // Maps to track whether main and session backups have been created
  std::map<BackupType, bool> has_main_backup;
  std::map<BackupType, bool> has_session_backup;

  // Backup creation timestamps
  std::map<BackupType, QDateTime> main_backup_timestamp;
  std::map<BackupType, QDateTime> session_backup_timestamp;
};

}  // namespace optimizations
