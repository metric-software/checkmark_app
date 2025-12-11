#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>

#include "../SettingsDropdown.h"

namespace optimize_components {

/**
 * @class UnknownValueManager
 * @brief Value discovery and preservation component that manages non-standard
 * setting values
 *
 * RESPONSIBILITIES:
 * - Discovers and tracks setting values that aren't in predefined optimization
 * options
 * - Provides persistent storage of custom values across application sessions
 * - Dynamically adds discovered values to UI dropdown controls
 * - Prevents value duplication through type-aware comparison and normalization
 * - Integrates with BackupManager to preserve custom settings during system
 * changes
 * - Maintains compatibility with third-party software and manual user
 * configurations
 *
 * USAGE:
 * Call loadUnknownValues() at application startup to restore previously
 * discovered values. Use addUnknownValueToDropdown() when encountering values
 * not in predefined options. Call recordUnknownValue() to track new values
 * without immediate UI updates. Use saveUnknownValues() periodically and
 * forceSaveUnknownValues() at critical points. Check hasUnknownValues() and
 * getUnknownValues() for validation and UI state management.
 *
 * ASSUMPTIONS:
 * - BackupManager is available for persistent storage operations
 * - UI dropdowns can accommodate dynamically added options
 * - System settings may contain values set by external applications
 * - Value types remain consistent between discovery and restoration
 * - Settings maintain their IDs consistently across application sessions
 *
 * VALUE DISCOVERY:
 * - Automatically triggered when current system values don't match predefined
 * options
 * - Occurs during settings loading and user interaction with dropdowns
 * - Captures values from manual registry edits, third-party tools, and system
 * updates
 * - Handles type conversions to normalize values for consistent storage
 * - Preserves original data types while enabling cross-type comparison
 *
 * DEDUPLICATION STRATEGY:
 * - Performs type-aware comparison to avoid duplicate entries
 * - Normalizes numeric strings to integers when appropriate
 * - Handles bool/int/string conversions for flexible matching
 * - Maintains one canonical representation per unique logical value
 * - Preserves user-friendly display names while storing raw values
 *
 * PERSISTENCE:
 * - Stores unknown values in application data directory via BackupManager
 * - Survives application restarts and system changes
 * - Automatically loaded at startup before UI construction
 * - Saved periodically during normal operation and at critical points
 * - Integrates with main backup system for consistency
 *
 * UI INTEGRATION:
 * - Dynamically adds custom values to dropdown controls with "(Custom)"
 * notation
 * - Maintains proper dropdown ordering (predefined options first, custom
 * options after)
 * - Supports tag application (Original, Recommended) for custom values
 * - Preserves custom values when rebuilding UI or refreshing settings
 * - Handles dropdown item selection and change notifications properly
 *
 * DATA FLOW:
 * System Values -> Discovery -> Storage -> UI Integration -> User Selection
 */
class UnknownValueManager {
 public:
  /**
   * @brief Constructor - initializes unknown value manager
   *
   * Creates empty tracking structures and prepares for value discovery
   * operations. Does not perform any file I/O - call loadUnknownValues() for
   * that.
   */
  UnknownValueManager();

  /**
   * @brief Dynamically adds discovered unknown value to dropdown control
   *
   * When a setting's current value doesn't match any predefined options, this
   * method adds it to the dropdown as a custom option with "(Custom)" notation.
   * Handles type normalization and duplicate checking to ensure clean UI
   * presentation.
   *
   * @param dropdown SettingsDropdown widget to add the value to.
   *                Must be valid and already populated with predefined options.
   * @param value QVariant containing the discovered unknown value.
   *             Will be normalized (e.g., numeric strings converted to
   * integers).
   * @param settingId QString identifier of the setting for tracking purposes.
   *                 Used to group unknown values by setting and for
   * persistence.
   *
   * @note This method immediately updates the UI dropdown and tracks the value
   * internally. The value is marked with "(Custom)" suffix to distinguish from
   * predefined options. Automatic type conversion ensures consistent
   * representation across discovery sessions.
   */
  void addUnknownValueToDropdown(SettingsDropdown* dropdown,
                                 const QVariant& value,
                                 const QString& settingId);

  /**
   * @brief Persists all tracked unknown values to storage
   *
   * Serializes the complete unknown values map to persistent storage via
   * BackupManager. This ensures unknown values survive application restarts and
   * system changes. Called periodically during normal operation and before
   * application shutdown.
   *
   * @note Uses BackupManager's SaveUnknownValues() method for integration with
   *       the main backup system. Handles file I/O errors gracefully.
   *       Safe to call frequently - only writes when values have changed.
   */
  void saveUnknownValues();

  /**
   * @brief Restores previously discovered unknown values from storage
   *
   * Loads the complete unknown values map from persistent storage via
   * BackupManager. This should be called at application startup before UI
   * construction to ensure all previously discovered values are available in
   * dropdown controls.
   *
   * @note Uses BackupManager's LoadUnknownValues() method for consistency.
   *       Handles missing or corrupted storage files gracefully.
   *       Unknown values are automatically added to dropdowns during UI
   * construction.
   */
  void loadUnknownValues();

  /**
   * @brief Forces immediate persistence of all current unknown values
   *
   * Performs immediate save operation regardless of change state. Used at
   * critical points like after applying settings or before major operations to
   * ensure no unknown values are lost if the application terminates
   * unexpectedly.
   *
   * @note This is a synchronous operation that may block briefly for file I/O.
   *       More aggressive than saveUnknownValues() - always writes to storage.
   *       Use sparingly as frequent forced saves can impact performance.
   */
  void forceSaveUnknownValues();

  /**
   * @brief Checks if any unknown values exist for a specific setting
   *
   * @param settingId QString identifier of the setting to check.
   *                 Must match the ID used during value discovery.
   *
   * @return bool True if the setting has any tracked unknown values.
   *             False if only predefined values have been encountered.
   *
   * @note Used for UI state management and validation logic.
   *       Does not trigger any I/O operations - uses in-memory tracking data.
   */
  bool hasUnknownValues(const QString& settingId) const;

  /**
   * @brief Retrieves all unknown values for a specific setting
   *
   * @param settingId QString identifier of the setting to get values for.
   *                 Must match the ID used during value discovery.
   *
   * @return QList<QVariant> List of all unknown values discovered for this
   * setting. Values are stored in their normalized form (e.g., integers not
   * strings). Empty list if no unknown values exist for this setting.
   *
   * @note Values are returned in discovery order. List is a copy -
   * modifications won't affect the internal tracking. Use recordUnknownValue()
   * to add new values.
   */
  QList<QVariant> getUnknownValues(const QString& settingId) const;

  /**
   * @brief Provides read-only access to complete unknown values collection
   *
   * @return const QMap<QString, QList<QVariant>>& Reference to internal map
   * where keys are setting IDs and values are lists of unknown values for each
   * setting. Reference remains valid until UnknownValueManager is destroyed.
   *
   * @note Used for bulk operations, debugging, and integration with other
   * components. Map is read-only - use recordUnknownValue() for modifications.
   *       Provides efficient access to all tracked data without copying.
   */
  const QMap<QString, QList<QVariant>>& getAllUnknownValues() const;

  /**
   * @brief Records a new unknown value without immediate UI update
   *
   * Adds a value to the internal tracking system without modifying any UI
   * controls. Used when discovering values during background operations or when
   * UI updates will be handled separately.
   *
   * @param settingId QString identifier of the setting the value belongs to.
   *                 Used for grouping and persistence.
   * @param value QVariant containing the unknown value to record.
   *             Will be normalized for consistent storage.
   *
   * @return bool True if the value was newly added to the tracking system.
   *             False if the value was already tracked (no changes made).
   *
   * @note This method only updates internal tracking - call
   * addUnknownValueToDropdown() if immediate UI updates are needed. Performs
   * duplicate checking and type normalization automatically.
   */
  bool recordUnknownValue(const QString& settingId, const QVariant& value);

 private:
  /**
   * @brief Internal storage for discovered unknown values
   *
   * Maps setting ID to list of unknown values for that setting.
   * Values are stored in normalized form for consistent comparison.
   * Maintained in discovery order within each setting's value list.
   */
  QMap<QString, QList<QVariant>> unknownValues;
};

}  // namespace optimize_components
