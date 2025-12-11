#pragma once

#include <QGroupBox>
#include <QMap>
#include <QObject>

#include "RevertManager.h"

// Forward declarations
class QWidget;
struct SettingCategory;
class SettingsToggle;
class SettingsDropdown;
class OptimizeView;

namespace optimize_components {

/**
 * @class SettingsUIBuilder
 * @brief Pure UI widget factory that transforms category data into interactive
 * Qt widgets
 *
 * CORE RESPONSIBILITY:
 * - Creates complete Qt widget hierarchies from SettingCategory data structures
 * - Manages all visual styling, layout, and interactive behavior of setting
 * widgets
 * - Handles widget lifecycle (creation, styling updates, cleanup) without
 * touching data logic
 * - Provides consistent visual appearance and behavior across all setting types
 *
 * COMPONENT USAGE:
 * - USES OptimizeView: Only for accessing advanced settings preference and
 * signal routing
 * - USES SettingsToggle/SettingsDropdown: Creates and configures these custom
 * widgets
 * - USES BackupManager: Retrieves original values for "Original" tag display
 * - USES OptimizationManager: Retrieves recommended values for "Recommended"
 * tag display
 * - USES UnknownValueManager: Loads custom values to add to dropdown options
 *
 * USED BY:
 * - OptimizeView: Calls CreateCategoryGroup() to build the entire settings UI
 * - OptimizeView: Calls ApplyGreyedOutStyle()/ApplyCollapsedStyle() for visual
 * state changes
 *
 * WIDGET HIERARCHY CREATED:
 * QGroupBox (Category)
 * ├── QWidget (Header) [top-level categories only]
 * │   ├── QLabel (Description)
 * │   ├── SettingsDropdown (Mode: Keep Original/Recommended/Custom)
 * │   └── QPushButton (Collapse/Expand Toggle)
 * ├── QWidget (Content Container)
 * │   └── QWidget (Settings Container)
 * │       ├── QWidget (Setting Row) for each setting
 * │       │   ├── QLabel (Setting Name + Tooltip)
 * │       │   └── SettingsToggle/SettingsDropdown/QPushButton (Control)
 * │       └── QGroupBox (Subcategory) [recursive for nested categories]
 *
 * CLEAR BOUNDARIES:
 * - This class ONLY creates and styles Qt widgets
 * - Does NOT modify SettingCategory data structures (read-only access)
 * - Does NOT apply settings to the system (only UI interaction)
 * - Does NOT load current system values (receives them via SettingDefinition
 * functions)
 * - Does NOT convert backend data (receives pre-converted SettingCategory
 * structures)
 * - Does NOT handle events beyond basic Qt widget signals (forwards to
 * OptimizeView)
 *
 * VISUAL STATE MANAGEMENT:
 * - ApplyGreyedOutStyle(): Visually disables category widgets while preserving
 * mode dropdown
 * - ApplyCollapsedStyle(): Shows/hides category content while preserving header
 * - ApplyOriginalTag()/ApplyRecommendedTag(): Adds visual indicators to
 * dropdown options
 * - All styling is self-contained and doesn't affect other components
 *
 * WIDGET TRACKING:
 * - categoryWidgets_: Maps category ID -> created QGroupBox for later style
 * updates
 * - settingsWidgets_: Maps setting ID -> created control widget for value
 * access
 * - Both maps are cleared and rebuilt on each UI refresh to prevent dangling
 * pointers
 *
 * MODIFICATION GUIDELINES:
 * - Widget appearance: Modify the Create*Widget() methods and their styling
 * - New setting types: Add new Create*Widget() method and update
 * CreateCategoryGroup()
 * - Visual states: Modify Apply*Style() methods only
 * - Layout changes: Modify the widget hierarchy creation in
 * CreateCategoryGroup()
 * - Do NOT add data loading, conversion, or system interaction logic here
 */
class SettingsUIBuilder {
 public:
  /**
   * @brief Constructor - initializes builder with parent widget for memory
   * management
   */
  SettingsUIBuilder(QWidget* parent);

  /**
   * @brief Creates complete QGroupBox UI structure for a category and its
   * hierarchy
   * @param category SettingCategory data to convert (must have valid id, name)
   * @param depth Current nesting level (0=top-level, max 3)
   * @return Pointer to created group box, nullptr if no valid content after
   * filtering
   * @note Recursively processes subcategories, prevents duplicate settings
   * automatically
   */
  QGroupBox* CreateCategoryGroup(const SettingCategory& category,
                                 int depth = 0);

  /**
   * @brief Applies visual styling for disabled/greyed-out state to category
   * widgets
   * @param widget Root widget to apply styling to (must have categoryId
   * property)
   * @param categoryId Unique identifier of the category being styled
   * @param isGreyedOut True to apply disabled styling, false to restore normal
   * styling
   * @note Mode dropdown for the specified category remains enabled when greyed
   * out
   */
  void ApplyGreyedOutStyle(QWidget* widget, const QString& categoryId,
                           bool isGreyedOut);

  /**
   * @brief Applies collapsed visual state to category content while preserving
   * header
   * @param groupBox Target QGroupBox to apply collapsed styling to
   * @param categoryId Unique identifier of the category
   * @param isCollapsed True to collapse content, false to expand
   * @note Updates toggle button state and content visibility automatically
   */
  void ApplyCollapsedStyle(QGroupBox* groupBox, const QString& categoryId,
                           bool isCollapsed);

  /**
   * @brief Determines if a category should be hidden due to lack of visible
   * content
   * @param categoryId Unique identifier of the category to check
   * @return True if category has no visible settings/subcategories and should
   * be hidden
   */
  bool ShouldHideEmptyCategory(const QString& categoryId);

  /**
   * @brief Applies "Original" visual tag to dropdown items matching backup
   * values
   * @param dropdown Target SettingsDropdown widget to update
   * @param settingId Unique identifier for retrieving backup value
   * @note Handles type conversions between backup values and dropdown data
   * types
   */
  void ApplyOriginalTag(SettingsDropdown* dropdown, const QString& settingId);

  /**
   * @brief Applies "Recommended" visual tag to dropdown items matching optimal
   * values
   * @param dropdown Target SettingsDropdown widget to update
   * @param settingId Unique identifier for retrieving recommended value
   * @note Retrieves recommended values from OptimizationManager entities
   */
  void ApplyRecommendedTag(SettingsDropdown* dropdown,
                           const QString& settingId);

  /**
   * @brief Provides read-only access to created category widget map
   */
  const QMap<QString, QGroupBox*>& GetCategoryWidgets() const {
    return categoryWidgets_;
  }

  /**
   * @brief Provides read-only access to created settings widget map
   */
  const QMap<QString, QWidget*>& GetSettingsWidgets() const {
    return settingsWidgets_;
  }

  /**
   * @brief Clears all internal widget tracking maps
   * @note Essential when rebuilding UI to prevent dangling pointer crashes
   */
  void ClearWidgetMaps();

  // Dialog styling methods for consistent appearance
  void ApplyDialogStyling(QDialog* dialog);
  void ApplyDialogTitleStyling(QLabel* titleLabel);
  void ApplyDialogCategoryStyling(QGroupBox* categoryBox);
  void ApplyDialogChangeStyling(QFrame* changeFrame);
  void ApplyDialogChangeNameStyling(QLabel* nameLabel);
  void ApplyDialogChangeValueStyling(QLabel* valueLabel);
  void ApplyDialogButtonStyling(QPushButton* button, bool isPrimary);

 private:
  // Helper methods for creating specific widget types
  QWidget* CreateMissingSettingWidget(const SettingDefinition& setting,
                                      const QString& categoryId);
  QWidget* CreateToggleSettingWidget(const SettingDefinition& setting,
                                     const QString& categoryId);
  QWidget* CreateDropdownSettingWidget(const SettingDefinition& setting,
                                       const QString& categoryId);
  QWidget* CreateButtonSettingWidget(const SettingDefinition& setting,
                                     const QString& categoryId);

  // Helper methods for dropdown management
  QString FormatOptionDisplay(const QVariant& value, const QString& settingId);
  bool BuildDropdownOptions(SettingsDropdown* dropdown,
                            const SettingDefinition& setting);
  void LoadUnknownValues(
    const SettingDefinition& setting,
    QMap<QString, QPair<QVariant, QString>>& uniqueValues,
    QList<QVariant>& orderedValues,
    std::function<QString(const QVariant&)> createValueKey);

  // Helper method for value comparison
  bool CompareValues(const QVariant& value1, const QVariant& value2,
                     const QString& settingId);

  // Helper method to determine setting name color based on level and advanced
  // properties
  QString GetSettingNameColor(int level, bool isAdvanced);

  QWidget*
    parentWidget_;  ///< Parent widget for memory management and state access

  // Widget tracking maps
  QMap<QString, QGroupBox*>
    categoryWidgets_;  ///< Map of category ID to created QGroupBox
  QMap<QString, QWidget*>
    settingsWidgets_;  ///< Map of setting ID to created control widget

  // Internal tracking maps
  QMap<QString, bool>
    processedSettingIds_;  ///< Prevents duplicate setting widgets (reset per
                           ///< top-level category)
  QMap<QString, bool>
    collapsedCategories_;  ///< Tracks collapsed state for ApplyCollapsedStyle
                           ///< consistency
};

}  // namespace optimize_components
