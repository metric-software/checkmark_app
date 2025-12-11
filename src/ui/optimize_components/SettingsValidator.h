#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVector>

// Forward declarations
struct SettingCategory;
struct SettingDefinition;

namespace optimize_components {

/**
 * @class SettingsValidator
 * @brief Quality assurance component that validates settings for system
 * compatibility and safety
 *
 * RESPONSIBILITIES:
 * - Validates individual settings against system capabilities and hardware
 * - Checks for conflicting settings that could cause system instability
 * - Provides detailed warnings about potentially problematic configurations
 * - Filters out settings that are inappropriate for the current system
 * - Ensures that only safe and applicable settings are presented to users
 *
 * USAGE:
 * Use ValidateAllSettings() for complete category tree validation before UI
 * display. Call ValidateSettingChange() for real-time validation when users
 * modify settings. Use FilterInvalidSettings() to remove problematic settings
 * from categories in-place. Connect to validationIssuesFound signal to handle
 * warnings and errors.
 *
 * ASSUMPTIONS:
 * - System hardware detection APIs are functional
 * - Settings categories are properly structured with valid metadata
 * - System configuration is stable during validation
 * - Hardware capabilities can be reliably detected
 * - Setting values are within expected data type ranges
 *
 * VALIDATION CRITERIA:
 * - Hardware compatibility (e.g., NVIDIA settings only on NVIDIA systems)
 * - Value range validation (numeric settings within acceptable bounds)
 * - Dependency checking (settings that require other settings to be enabled)
 * - System version compatibility (settings that require specific Windows
 * versions)
 * - Performance impact assessment (settings that may cause significant
 * slowdown)
 *
 * FILTERING BEHAVIOR:
 * - Removes NVIDIA settings on non-NVIDIA systems
 * - Filters out settings requiring newer Windows versions than current
 * - Excludes settings that would conflict with detected hardware limitations
 * - Removes settings with invalid or out-of-range default values
 * - Preserves settings hierarchy while removing invalid items
 *
 * VALIDATION SEVERITY:
 * - Info: Informational messages about setting effects or recommendations
 * - Warning: Settings that may cause minor issues but are generally safe
 * - Error: Settings that could cause system instability or crashes
 *
 * DATA FLOW:
 * SettingCategory[] -> Validation Rules -> Filtered Categories + Issue Reports
 */
class SettingsValidator : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Describes the severity and details of a validation issue
   */
  struct ValidationIssue {
    /**
     * @brief Severity levels for validation issues
     */
    enum class Severity {
      Info,     ///< Informational only - no action required
      Warning,  ///< Potentially problematic - user should be aware
      Error     ///< Definitely problematic - should prevent application
    };

    QString settingId;  ///< ID of the setting that has the issue
    QString message;    ///< Human-readable description of the issue
    Severity severity;  ///< Severity level determining required user action
  };

  /**
   * @brief Constructor - initializes validator with parent for signal handling
   *
   * @param parent QObject parent for proper memory management and signal/slot
   * connections. Typically the main OptimizeView or SettingsChecker that
   * handles validation results.
   */
  explicit SettingsValidator(QObject* parent = nullptr);

  /**
   * @brief Performs comprehensive validation of all settings in the category
   * tree
   *
   * Examines every setting in the provided categories to identify compatibility
   * issues, safety concerns, and potential conflicts. This is typically called
   * before displaying settings to users to ensure only appropriate options are
   * shown.
   *
   * @param categories Const reference to category vector containing all
   * settings to validate. Categories are not modified by this operation.
   *
   * @return QVector<ValidationIssue> List of all validation issues found across
   * all categories. Issues are ordered by severity (errors first, then
   * warnings, then info). Empty vector indicates all settings passed
   * validation.
   *
   * @note This performs deep validation including hardware detection and
   * compatibility checks. May take 1-3 seconds for comprehensive hardware
   * analysis.
   */
  QVector<ValidationIssue> ValidateAllSettings(
    const QVector<SettingCategory>& categories);

  /**
   * @brief Validates a specific setting value change in real-time
   *
   * Performs targeted validation when a user attempts to change a setting
   * value. Checks the new value for compatibility, range validation, and
   * potential conflicts with other settings. Used for immediate feedback during
   * user interaction.
   *
   * @param settingId QString identifier of the setting being changed.
   *                 Must correspond to a valid setting in the categories.
   * @param newValue QVariant containing the proposed new value.
   *                Must be compatible with the setting's expected data type.
   * @param categories Const reference to all categories for context and
   * dependency checking. Used to check for conflicts with other settings.
   *
   * @return QVector<ValidationIssue> List of validation issues specific to this
   * change. Empty vector indicates the change is safe to apply. Errors indicate
   * the change should be blocked.
   *
   * @note This is optimized for speed since it's called during user
   * interaction. Only performs checks relevant to the specific setting being
   * changed.
   */
  QVector<ValidationIssue> ValidateSettingChange(
    const QString& settingId, const QVariant& newValue,
    const QVector<SettingCategory>& categories);

  /**
   * @brief Removes invalid settings from categories based on system
   * compatibility
   *
   * Performs destructive filtering that removes settings which are not
   * appropriate for the current system. This modifies the category structure
   * in-place to ensure only valid, safe settings are available to users.
   *
   * @param categories Mutable reference to category vector to filter.
   *                  Will be modified to remove invalid settings and empty
   * categories.
   *
   * @return int Number of settings that were removed during filtering.
   *            Zero indicates all settings were compatible with the current
   * system.
   *
   * @note This permanently removes settings from the category structure.
   *       Should be called after LoadAndCheckSettings() but before UI creation.
   *       Empty categories (those with no valid settings) are also removed.
   */
  int FilterInvalidSettings(QVector<SettingCategory>& categories);

 signals:
  /**
   * @brief Signal emitted when validation discovers issues requiring user
   * attention
   *
   * @param issues QVector of ValidationIssue objects describing problems found.
   *              UI components should display appropriate warnings or errors
   *              based on the severity levels of the issues.
   */
  void validationIssuesFound(const QVector<ValidationIssue>& issues);

 private:
  /**
   * @brief Internal validation logic for individual settings
   *
   * @param setting SettingDefinition to validate against system capabilities
   * @return QVector<ValidationIssue> Issues found for this specific setting
   */
  QVector<ValidationIssue> ValidateSetting(const SettingDefinition& setting);

  /**
   * @brief Hardware detection utility for NVIDIA GPU presence
   *
   * @return bool True if NVIDIA GPU is present and drivers are accessible.
   *             False if no NVIDIA hardware or drivers not functional.
   */
  bool IsNvidiaGpuPresent();

  /**
   * @brief Hardware compatibility validation for specific settings
   *
   * @param settingId QString identifier of the setting to check
   * @param value QVariant value to validate against hardware capabilities
   * @return QVector<ValidationIssue> Hardware-specific compatibility issues
   */
  QVector<ValidationIssue> CheckHardwareCompatibility(const QString& settingId,
                                                      const QVariant& value);
};

}  // namespace optimize_components
