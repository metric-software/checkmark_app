#pragma once

#include <mutex>

#include <QDialog>
#include <QMap>
#include <QObject>
#include <QPushButton>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include "../SettingsDropdown.h"
#include "../SettingsToggle.h"

// Forward declarations
struct SettingCategory;
struct SettingDefinition;

namespace optimize_components {

/**
 * @brief Enumeration of revert point types supported by the system
 */
enum class RevertType {
  SessionOriginals,  ///< Revert to values from when the current app session
                     ///< started
  SystemDefaults  ///< Revert to system default values before app ever modified
                  ///< them
};

/**
 * @class RevertManager
 * @brief Backup and restoration component that manages setting revert points
 * and restoration operations
 *
 * RESPONSIBILITIES:
 * - Creates and manages multiple types of setting backup points
 * - Provides user interface for selecting revert operations
 * - Executes bulk restoration operations with progress tracking
 * - Handles session-based revert points (current session starting values)
 * - Interfaces with BackupManager for persistent system default revert points
 * - Manages thread-safe access to stored revert data
 *
 * USAGE:
 * Call storeSessionOriginals() once at application startup after loading
 * settings. Use showRevertDialog() to present user with revert options in a
 * dialog. Call revertSettings() directly to execute specific revert operations.
 * Use isOriginalValue() for validation and UI state management.
 * Connect to signals for handling revert completion and error reporting.
 *
 * ASSUMPTIONS:
 * - Session originals are stored before any user modifications
 * - BackupManager has been initialized with system default revert points
 * - Settings have valid apply functions for restoration
 * - UI widgets accurately reflect system states
 * - Category structure remains consistent between store and restore operations
 *
 * REVERT POINT TYPES:
 * 1. Session Originals: Values captured when the application session started
 *    - Stored in memory (lost when application closes)
 *    - Used for "undo session changes" functionality
 *    - Captured automatically on first settings load
 *
 * 2. System Defaults: Original values before application ever modified settings
 *    - Stored persistently via BackupManager
 *    - Used for "restore to factory defaults" functionality
 *    - Created once when application first runs with admin privileges
 *
 * RESTORATION STRATEGY:
 * - Attempts to restore all settings in the provided categories
 * - Continues with remaining settings if individual restorations fail
 * - Updates UI widgets to reflect successful restorations
 * - Provides detailed error reporting for failed restorations
 * - Uses optimization entity apply functions for actual system changes
 *
 * THREAD SAFETY:
 * - Session original storage and access is mutex-protected
 * - Safe for concurrent access from multiple threads
 * - Dialog operations must be called from UI thread
 *
 * ERROR HANDLING:
 * - Gracefully handles missing revert points
 * - Continues restoration even when individual settings fail
 * - Provides detailed failure reporting through signals
 * - Maintains UI consistency during partial restoration failures
 *
 * DATA FLOW:
 * Settings Values -> Storage -> User Selection -> Restoration -> UI Updates
 */
class RevertManager : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Constructor - initializes revert manager with parent for signal
   * handling
   *
   * @param parent QObject parent for proper memory management and signal/slot
   * connections. Typically the main OptimizeView that will handle revert
   * completion signals.
   */
  explicit RevertManager(QObject* parent = nullptr);

  /**
   * @brief Creates session revert point by capturing current setting values
   *
   * Stores the current values of all settings in the provided categories as
   * session originals. This creates a revert point representing the state when
   * the application session started, before any user modifications were made.
   *
   * @param categories Const reference to category tree containing all settings
   * to store. Values are read from these settings but categories are not
   * modified.
   * @param settingsWidgets Const reference to map of setting ID -> UI widget
   * pointer. Used to read current UI states for settings.
   * @param settingsStates Mutable reference to map of setting ID -> current UI
   * state. Used to track which values were stored as originals.
   *
   * @note This should be called once at application startup after settings are
   * loaded but before any user modifications. Subsequent calls will overwrite
   * previous session originals. Thread-safe for concurrent access.
   */
  void storeSessionOriginals(const QVector<SettingCategory>& categories,
                             const QMap<QString, QWidget*>& settingsWidgets,
                             QMap<QString, QVariant>& settingsStates);

  /**
   * @brief Checks if session revert point has been created
   *
   * @return bool True if storeSessionOriginals() has been called and session
   * revert point exists. False if no session originals have been stored yet.
   *
   * @note Thread-safe for concurrent access. Used to determine if session-based
   *       revert operations are available to the user.
   */
  bool hasStoredSessionOriginals() const;

  /**
   * @brief Displays interactive dialog for user to select revert operation type
   *
   * Shows a modal dialog with options for different revert operations (session
   * originals vs system defaults). The dialog provides clear descriptions of
   * what each option does and confirmation before executing the selected revert
   * operation.
   *
   * @param parent QWidget parent for the dialog for proper modal behavior and
   * positioning. Should be the main application window or settings view.
   *
   * @note This method must be called from the UI thread. The dialog is modal
   * and will block until the user makes a selection or cancels. Emits signals
   * based on user choice and revert operation results.
   */
  void showRevertDialog(QWidget* parent);

  /**
   * @brief Executes revert operation for specified revert point type
   *
   * Performs bulk restoration of settings to a previous state based on the
   * specified revert type. Updates both system settings and UI widget states to
   * reflect the restored values.
   *
   * @param type RevertType enum specifying which revert point to use:
   *            - SessionOriginals: Restore to values from current session start
   *            - SystemDefaults: Restore to original system values from
   * BackupManager
   * @param categories Const reference to category tree containing settings to
   * revert. All settings in these categories will be processed for reversion.
   * @param settingsWidgets Const reference to map of setting ID -> UI widget
   * pointer. Used to update widget states after successful restoration.
   * @param settingsStates Mutable reference to map of setting ID -> current UI
   * state. Will be updated to reflect the restored values.
   *
   * @note This operation may take several seconds for large setting sets.
   *       Emits settingsReverted signal when complete with success/failure
   * details. Failed settings remain in their current state.
   */
  void revertSettings(RevertType type,
                      const QVector<SettingCategory>& categories,
                      const QMap<QString, QWidget*>& settingsWidgets,
                      QMap<QString, QVariant>& settingsStates);

  /**
   * @brief Validation utility to check if a value matches stored original for a
   * setting
   *
   * Compares the provided value against the stored session original value for
   * the specified setting. Used for UI state management and validation logic.
   *
   * @param settingId QString identifier of the setting to check.
   *                 Must correspond to a setting that was included in
   * storeSessionOriginals.
   * @param value QVariant value to compare against the stored session original.
   *             Type should match the original setting value type.
   *
   * @return bool True if the provided value matches the stored session
   * original. False if values differ or no session original exists for this
   * setting.
   *
   * @note Thread-safe for concurrent access. Only checks session originals, not
   *       system defaults (use BackupManager for system default comparisons).
   */
  bool isOriginalValue(const QString& settingId, const QVariant& value) const;

 signals:
  /**
   * @brief Signal emitted when a revert operation completes
   *
   * @param type RevertType indicating which type of revert was performed.
   * @param success Boolean indicating overall success of the revert operation.
   *               True if all settings were successfully reverted.
   * @param failedSettings QStringList containing IDs of settings that failed to
   * revert. Empty list indicates complete success.
   */
  void settingsReverted(RevertType type, bool success,
                        const QStringList& failedSettings);

  /**
   * @brief Signal emitted when user selects revert type in the dialog
   *
   * @param type RevertType selected by the user from the revert dialog.
   *            Emitted before the actual revert operation begins.
   */
  void revertTypeSelected(RevertType type);

 private:
  /**
   * @brief Internal implementation for session originals restoration
   *
   * @param categories Category tree to process
   * @param settingsWidgets Widget map for UI updates
   * @param settingsStates State map to update
   * @return bool True if all settings restored successfully
   */
  bool revertToSessionOriginals(const QVector<SettingCategory>& categories,
                                const QMap<QString, QWidget*>& settingsWidgets,
                                QMap<QString, QVariant>& settingsStates);

  /**
   * @brief Internal implementation for system defaults restoration
   *
   * @param categories Category tree to process
   * @param settingsWidgets Widget map for UI updates
   * @param settingsStates State map to update
   * @return bool True if all settings restored successfully
   */
  bool revertToSystemDefaults(const QVector<SettingCategory>& categories,
                              const QMap<QString, QWidget*>& settingsWidgets,
                              QMap<QString, QVariant>& settingsStates);

  // Internal state storage - SESSION ORIGINALS ARE MEMORY-ONLY
  QMap<QString, QVariant>
    sessionOriginalValues;  ///< Session original values by setting ID (MEMORY
                            ///< ONLY - lost on app close)
  bool sessionOriginalsStored =
    false;  ///< Flag indicating if session originals exist in memory
  mutable std::mutex
    sessionOriginalsMutex;  ///< Thread safety for session original access
};

}  // namespace optimize_components
