#pragma once

#include <utility>

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QWidget>

// Forward declarations
struct SettingCategory;
struct SettingDefinition;

namespace optimize_components {

/**
 * @class SettingsApplicator
 * @brief Change management component that identifies, applies, and tracks
 * setting modifications
 *
 * RESPONSIBILITIES:
 * - Compares current UI states with desired values to identify required changes
 * - Applies setting changes through appropriate backend mechanisms (registry,
 * APIs, etc.)
 * - Provides progress feedback during potentially long-running apply operations
 * - Handles partial failures gracefully with detailed error reporting
 * - Supports bulk operations for applying recommended or original value sets
 * - Manages category-level operations (apply all settings in a category)
 *
 * USAGE:
 * Use IdentifyChanges() to determine what settings need to be modified.
 * Call ApplyChanges() to execute the identified changes with progress tracking.
 * Use ApplyRecommendedSettings() to bulk-apply optimal values for a category.
 * Use LoadOriginalSettings() to bulk-restore backup values for a category.
 * Connect to progress signals for UI feedback during long operations.
 *
 * ASSUMPTIONS:
 * - OptimizationManager entities are properly initialized and functional
 * - Settings have valid apply functions (setDropdownValueFn, setToggleValueFn)
 * - UI widget state accurately reflects user intentions
 * - Backend systems can handle concurrent setting changes safely
 * - Backup/restore system is available for original value loading
 *
 * CHANGE DETECTION:
 * - Compares UI widget states against optimization entity current values
 * - Identifies only settings where UI state differs from system state
 * - Handles type conversions between UI (QVariant) and backend
 * (OptimizationValue)
 * - Groups changes by category for better progress reporting
 * - Excludes disabled settings from change detection
 *
 * APPLICATION STRATEGY:
 * - Applies changes in dependency order when possible
 * - Continues with remaining changes if individual settings fail
 * - Provides detailed success/failure reporting per setting
 * - Updates UI widget states to reflect successful changes
 * - Preserves UI state for failed changes to allow retry
 *
 * ERROR HANDLING:
 * - Catches and logs exceptions from individual setting applications
 * - Returns partial success information (count + failed setting list)
 * - Emits progress updates even when some settings fail
 * - Maintains system consistency by not rolling back partial changes
 *
 * DATA FLOW:
 * UI Widget States -> Change Detection -> Backend Apply Functions -> Result
 * Reporting
 */
class SettingsApplicator : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Describes a single setting change to be applied
   */
  struct SettingChange {
    QString id;             ///< Unique identifier of the setting
    QString name;           ///< Display name for progress reporting
    QString category;       ///< Category name for grouping and progress
    QVariant currentValue;  ///< Current system value (before change)
    QVariant newValue;      ///< Desired new value (from UI)
    bool
      isToggle;  ///< True if setting uses toggle semantics, false for dropdown
  };

  /**
   * @brief Constructor - initializes applicator with parent for signal handling
   *
   * @param parent QObject parent for proper memory management and signal/slot
   * connections. Typically the main OptimizeView that will handle progress and
   * completion signals.
   */
  explicit SettingsApplicator(QObject* parent = nullptr);

  /**
   * @brief Identifies all settings that need to be changed based on UI state
   * differences
   *
   * Compares the current state of UI widgets against the actual system values
   * to determine which settings need to be applied. This creates a change list
   * without actually modifying any system settings.
   *
   * @param categories Const reference to category tree containing all settings
   * to check. Used to access setting metadata and validation rules.
   * @param settingsStates Const reference to map of setting ID -> UI widget
   * value. Represents the user's desired configuration from the UI.
   *
   * @return QList<SettingChange> List of changes that need to be applied.
   *         Each change includes current value, desired value, and metadata.
   *         Empty list indicates UI state matches system state (no changes
   * needed).
   *
   * @note This is a read-only operation that doesn't modify system settings.
   *       Call ApplyChanges() with the returned list to execute the changes.
   */
  QList<SettingChange> IdentifyChanges(
    const QVector<SettingCategory>& categories,
    const QMap<QString, QVariant>& settingsStates);

  /**
   * @brief Applies a list of setting changes with progress tracking and error
   * handling
   *
   * Executes the provided setting changes by calling the appropriate backend
   * apply functions. Provides progress feedback and handles partial failures
   * gracefully. Updates UI widget states to reflect successful changes.
   *
   * @param changes List of SettingChange objects to apply (typically from
   * IdentifyChanges). Each change will be attempted regardless of previous
   * failures.
   * @param categories Const reference to category tree containing setting
   * definitions. Used to access setting apply functions for executing changes.
   * @param parentWidget Pointer to parent widget for progress dialogs and error
   * messages. May be nullptr if no UI feedback is needed.
   *
   * @return std::pair<int, QStringList> Pair containing:
   *         - First: Number of settings that were successfully applied
   *         - Second: List of setting IDs that failed to apply
   *         If second is empty, all changes were applied successfully.
   *
   * @note This operation may take several seconds for large change sets.
   *       Emits progress signals throughout the operation.
   *       Failed settings remain in their original state.
   */
  std::pair<int, QStringList> ApplyChanges(
    const QList<SettingChange>& changes,
    const QVector<SettingCategory>& categories,
    QWidget* parentWidget = nullptr);

  /**
   * @brief Bulk operation to apply recommended values for all settings in a
   * category
   *
   * Sets all settings in the specified category (and optionally subcategories)
   * to their recommended values as defined by the optimization entities.
   * Updates both system settings and UI widget states to reflect the changes.
   *
   * @param category Const reference to the category to apply recommended
   * settings for. Must contain valid settings with recommended values defined.
   * @param settingsWidgets Const reference to map of setting ID -> UI widget
   * pointer. Used to update widget states after successful application.
   * @param settingsStates Mutable reference to map of setting ID -> current UI
   * state. Will be updated to reflect the new recommended values.
   *
   * @note This applies changes immediately without user confirmation.
   *       Only processes settings that have valid recommended values defined.
   *       Recursively processes subcategories within the category.
   */
  void ApplyRecommendedSettings(const SettingCategory& category,
                                const QMap<QString, QWidget*>& settingsWidgets,
                                QMap<QString, QVariant>& settingsStates);

  /**
   * @brief Bulk operation to restore original values for all settings in a
   * category
   *
   * Loads original values from the backup system and applies them to all
   * settings in the specified category. This effectively reverts the category
   * to its pre-optimization state.
   *
   * @param category Const reference to the category to restore original
   * settings for. Must contain settings that have original values in the backup
   * system.
   * @param settingsWidgets Const reference to map of setting ID -> UI widget
   * pointer. Used to update widget states after successful restoration.
   * @param settingsStates Mutable reference to map of setting ID -> current UI
   * state. Will be updated to reflect the restored original values.
   *
   * @note Requires that backup system has been properly initialized with
   * original values. Only processes settings that have valid original values in
   * the backup. Does not affect settings that were never modified from their
   * originals.
   */
  void LoadOriginalSettings(const SettingCategory& category,
                            const QMap<QString, QWidget*>& settingsWidgets,
                            QMap<QString, QVariant>& settingsStates);

 signals:
  /**
   * @brief Signal emitted when ApplyChanges() operation completes
   *
   * @param successCount Integer number of settings that were successfully
   * applied.
   * @param failedSettings QStringList containing IDs of settings that failed to
   * apply. Empty list indicates all settings were applied successfully.
   */
  void changesApplied(int successCount, const QStringList& failedSettings);

  /**
   * @brief Signal emitted periodically during long-running apply operations
   *
   * @param currentIndex Integer index of the setting currently being processed
   * (0-based).
   * @param totalCount Integer total number of settings in the apply operation.
   * @param settingName QString display name of the setting currently being
   * processed.
   * @param success Boolean indicating if the current setting was applied
   * successfully.
   */
  void progressUpdate(int currentIndex, int totalCount,
                      const QString& settingName, bool success);

 private:
  /**
   * @brief Internal helper to find a setting definition by ID in the category
   * tree
   *
   * @param categories Category tree to search
   * @param id Setting ID to find
   * @return Pointer to SettingDefinition if found, nullptr otherwise
   */
  const SettingDefinition* FindSettingById(
    const QVector<SettingCategory>& categories, const QString& id);

  /**
   * @brief Internal recursive helper for change detection within a category
   *
   * @param category Category to process
   * @param parentPath Category path for grouping (e.g., "Performance/CPU")
   * @param settingsStates UI state map
   * @param changes Mutable list to append found changes to
   * @param recommendedOnly If true, only detect changes to recommended values
   */
  void FindChangesInCategory(const SettingCategory& category,
                             const QString& parentPath,
                             const QMap<QString, QVariant>& settingsStates,
                             QList<SettingChange>& changes,
                             bool recommendedOnly = false);
};

}  // namespace optimize_components
