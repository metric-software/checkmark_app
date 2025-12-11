#pragma once

#include <functional>
#include <vector>

#include <QGroupBox>
#include <QMap>
#include <QString>
#include <QVariant>

#include "../SettingsDropdown.h"
#include "../SettingsToggle.h"
#include "optimization/NvidiaOptimization.h"
#include "optimization/OptimizationEntity.h"

// Forward declarations
struct SettingCategory;
struct SettingDefinition;
struct SettingOption;
enum class CategoryMode;

namespace optimizations::settings {
class OptimizationEntity;
}

namespace optimize_components {

/**
 * @class SettingsCategoryConverter
 * @brief Pure data transformation layer that converts backend optimization
 * entities to frontend UI structures
 *
 * CORE RESPONSIBILITY:
 * - Converts backend OptimizationEntity objects into frontend
 * SettingCategory/SettingDefinition structures
 * - Organizes settings into a logical 3-level hierarchy (Category > Subcategory
 * > Settings)
 * - Sets up value getter/setter functions that bridge backend entities and
 * frontend widgets
 * - Handles category tree operations (find, update modes, deduplication)
 * without UI concerns
 *
 * COMPONENT USAGE:
 * - USES OptimizationManager: Retrieves OptimizationEntity objects and their
 * metadata
 * - USES OptimizationEntity: Calls GetCurrentValue(), GetRecommendedValue(),
 * Apply() etc.
 * - USES BackupManager: Accesses original values for comparison and validation
 *
 * USED BY:
 * - SettingsChecker: Calls ConvertToUICategory() to transform loaded
 * optimizations
 * - OptimizeView: Calls FindCategoryById(), SetCategoryMode() for category
 * management
 * - SettingsApplicator: Uses the converted structures to apply setting changes
 *
 * DATA TRANSFORMATION FLOW:
 * OptimizationEntity[] (backend)
 *   ↓ ConvertToUICategory()
 * SettingCategory (with nested SettingDefinition[])
 *   ↓ Used by SettingsUIBuilder
 * Qt Widgets (frontend)
 *
 * CATEGORY HIERARCHY ORGANIZATION:
 * - Level 1 (Category): Groups by optimization type (Registry, NVIDIA, Power,
 * etc.)
 * - Level 2 (Subcategory): Groups by functional area within each type
 * - Level 3 (Settings): Individual settings with their options and functions
 *
 * CLEAR BOUNDARIES:
 * - This class ONLY transforms data structures and manages category tree
 * operations
 * - Does NOT create Qt widgets (delegated to SettingsUIBuilder)
 * - Does NOT load system values (uses pre-loaded OptimizationEntity objects)
 * - Does NOT apply settings to system (delegated to SettingsApplicator)
 * - Does NOT handle UI events or styling (delegated to
 * OptimizeView/SettingsUIBuilder)
 * - Does NOT manage widget lifecycle or memory (pure data operations)
 *
 * FUNCTION SETUP:
 * - Sets up getCurrentValueFn/setToggleValueFn for each setting to call
 * appropriate OptimizationEntity methods
 * - These functions are later called by UI widgets to get/set values without
 * knowing backend details
 * - Provides clean separation between UI logic and backend optimization logic
 *
 * CATEGORY MODE MANAGEMENT:
 * - SetCategoryMode(): Updates category and subcategory modes
 * (KeepOriginal/Recommended/Custom)
 * - SetRecommendedMode(): Legacy compatibility for boolean recommended mode
 * - FindCategoryById(): Recursive search through category tree for updates
 * - All mode changes affect data structures only, not UI appearance
 *
 * FILTERING & VALIDATION:
 * - EnsureUniqueSettings(): Removes duplicate settings based on ID
 * - IsSettingDisabled(): Checks if setting should be excluded from UI
 * - AreSettingsMatchingOriginals(): Validates current vs original values
 * - All filtering preserves data integrity without UI concerns
 *
 * MODIFICATION GUIDELINES:
 * - New optimization types: Add conversion logic in ConvertCategoryGroup()
 * - Category organization: Modify the grouping logic in ConvertToUICategory()
 * - Setting metadata: Update ConvertOptimizationToSetting() method
 * - Tree operations: Add new utility methods following the existing pattern
 * - Do NOT add UI creation, styling, or event handling logic here
 */
class SettingsCategoryConverter {
 public:
  /**
   * @brief Callback type for when a missing setting is successfully created
   */
  using OnSettingCreatedCallback = std::function<void(const std::string&)>;

  /**
   * @brief Set callback function for when a missing setting is created
   * @param callback Function to call when a setting is successfully created
   */
  static void SetOnSettingCreatedCallback(OnSettingCreatedCallback callback);

  /**
   * @brief Converts a list of optimization entities to a complete UI category
   * tree
   *
   * This is the main entry point for data transformation from backend to
   * frontend. Takes a flat list of optimization entities and organizes them
   * into a hierarchical category structure suitable for UI display.
   *
   * @param optimizationsList Vector of OptimizationEntity pointers that are
   * already initialized with current system values. Must not be null. Entities
   * should have valid category/subcategory metadata.
   *
   * @return SettingCategory Root category containing the complete hierarchy:
   *         - Top-level categories (e.g., "Performance", "Graphics")
   *         - Subcategories within each category (e.g., "CPU", "GPU")
   *         - Individual settings as SettingDefinition objects
   *         Returns empty category if input list is empty.
   *
   * @note The returned category tree preserves the original entity organization
   *       but may reorganize settings for better UI presentation.
   */
  static SettingCategory ConvertToUICategory(
    const std::vector<optimizations::settings::OptimizationEntity*>&
      optimizationsList);

  /**
   * @brief Utility function to convert backend OptimizationValue to frontend
   * QVariant
   *
   * Handles type conversion between the backend's std::variant-based
   * OptimizationValue and Qt's QVariant system used by the UI components.
   * Supports bool, int, double, and string conversions with proper type
   * preservation.
   *
   * @param value OptimizationValue from an OptimizationEntity (bool, int,
   * double, or string)
   * @return QVariant with the same value but in Qt's type system.
   *         Returns invalid QVariant if input contains unsupported type.
   *
   * @note This function is used extensively by UI components to bridge
   *       the type system gap between backend and frontend.
   */
  static QVariant ConvertOptimizationValueToQVariant(
    const optimizations::OptimizationValue& value);

  /**
   * @brief Recursive search function to find a category by ID in the category
   * tree
   *
   * Performs depth-first search through the category hierarchy to locate
   * a category with the specified ID. Searches both direct categories and
   * all nested subcategories.
   *
   * @param id QString identifier of the category to find (case-sensitive)
   * @param categories Mutable reference to category vector to search in.
   *                  May be modified if category structure needs updates.
   *
   * @return SettingCategory* Pointer to the found category, or nullptr if not
   * found. Returned pointer is valid as long as the input categories vector
   * exists.
   *
   * @note Modifies the input vector to ensure consistent state during search.
   *       Use this for both reading and updating category properties.
   */
  static SettingCategory* FindCategoryById(
    const QString& id, QVector<SettingCategory>& categories);

  /**
   * @brief Legacy function to set recommended mode for a category hierarchy
   *
   * Updates the isRecommendedMode flag for the specified category and
   * optionally all its subcategories. This is a legacy function maintained for
   * backward compatibility. New code should use SetCategoryMode() instead.
   *
   * @param category Mutable reference to the category to update
   * @param isRecommended True to enable recommended mode, false for custom mode
   * @param recursive If true, applies the change to all subcategories
   * recursively. If false, only affects the specified category.
   *
   * @note This only updates the legacy isRecommendedMode boolean flag.
   *       For full CategoryMode support, use SetCategoryMode().
   */
  static void SetRecommendedMode(SettingCategory& category, bool isRecommended,
                                 bool recursive = true);

  /**
   * @brief Modern function to set category mode with full enum support
   *
   * Updates the category mode using the full CategoryMode enum (KeepOriginal,
   * Recommended, Custom) and propagates changes through the category hierarchy
   * as needed. Also updates the external category modes tracking map.
   *
   * @param category Mutable reference to the category to update
   * @param mode CategoryMode enum value to set (KeepOriginal=0, Recommended=1,
   * Custom=2)
   * @param propagateToSubcategories If true, recursively applies mode to all
   * subcategories. If false, only affects the specified category.
   * @param categoryModes Mutable reference to external map that tracks category
   * modes for the entire application. Will be updated with new mode.
   *
   * @note This is the preferred method for mode changes as it supports
   *       the full range of category modes and maintains consistency.
   */
  static void SetCategoryMode(SettingCategory& category, CategoryMode mode,
                              bool propagateToSubcategories,
                              QMap<QString, CategoryMode>& categoryModes);

  /**
   * @brief Deduplication utility to ensure each setting appears only once in
   * the UI
   *
   * Recursively processes a category tree to remove duplicate settings based on
   * setting ID. The first occurrence of a setting is kept, subsequent
   * duplicates are removed. This prevents the same setting from appearing
   * multiple times in different categories or subcategories.
   *
   * @param category Mutable reference to the category tree to process.
   *                Will be modified to remove duplicate settings.
   * @param addedSettingIds Mutable reference to map tracking which setting IDs
   *                       have already been processed. Key=settingId,
   * Value=true if added. Will be updated as settings are processed.
   *
   * @note This modifies the category structure in-place. Should be called
   *       after initial category creation but before UI building.
   */
  static void EnsureUniqueSettings(SettingCategory& category,
                                   QMap<QString, bool>& addedSettingIds);

  /**
   * @brief Filters categories to only include those with valid content for UI
   * display
   *
   * Removes categories that have no valid settings after applying advanced
   * settings filter. A category is considered valid if it has at least one
   * non-disabled, non-advanced (when advanced is disabled) setting, or has
   * valid subcategories.
   *
   * @param categories Mutable reference to category vector to filter in-place
   * @param showAdvancedSettings Whether advanced settings should be included
   * @return Number of categories removed during filtering
   */
  static int FilterValidCategories(QVector<SettingCategory>& categories,
                                   bool showAdvancedSettings);

  /**
   * @brief Adds or replaces a category in the category list with deduplication
   *
   * If a category with the same ID already exists, it will be replaced.
   * Otherwise, the category will be added after filtering and deduplication.
   *
   * @param categories Mutable reference to the category list to modify
   * @param newCategory Category to add or replace
   * @param showAdvancedSettings Whether to include advanced settings during
   * filtering
   * @return True if category was added/replaced, false if filtered out due to
   * no valid content
   */
  static bool AddOrReplaceCategory(QVector<SettingCategory>& categories,
                                   const SettingCategory& newCategory,
                                   bool showAdvancedSettings);

  /**
   * @brief Checks if category has any valid content for UI display
   */

  /**
   * @brief Validation utility to check if category settings match their
   * original values
   *
   * Recursively examines all settings in a category tree to determine if their
   * current values match their original (pre-optimization) values. Used for
   * UI state management and revert point validation.
   *
   * @param category Const reference to the category tree to examine.
   *                Does not modify the category structure.
   *
   * @return bool True if ALL settings in the category and its subcategories
   *             match their original values. False if any setting has been
   * modified or if original values cannot be determined.
   *
   * @note Compares against original values stored in the backup system.
   *       Requires that backup/revert points have been properly created.
   */
  static bool AreSettingsMatchingOriginals(const SettingCategory& category);

 private:
  // Internal helper functions for data conversion and setup
  static void AddDefaultOptions(
    SettingDefinition& settingDef,
    const optimizations::OptimizationValue& currentValue);
  static void SetupValueFunctions(
    SettingDefinition& settingDef,
    optimizations::settings::OptimizationEntity* opt);

  // Internal category creation methods
  static SettingCategory ConvertCategoryGroup(
    const std::string& categoryName,
    const std::vector<optimizations::settings::OptimizationEntity*>&
      optimizations);

  static SettingCategory ConvertSubcategoryGroup(
    const std::string& subcategoryName,
    const std::vector<optimizations::settings::OptimizationEntity*>&
      optimizations);

  static SettingDefinition ConvertOptimizationToSetting(
    optimizations::settings::OptimizationEntity* opt);

  static void SetupToggleSetting(
    SettingDefinition& setting,
    optimizations::settings::OptimizationEntity* opt);
  static void SetupDropdownSetting(
    SettingDefinition& setting,
    optimizations::settings::OptimizationEntity* opt);
  static QString GetCategoryDescription(const std::string& categoryName);
  static QString GetSubcategoryDescription(const std::string& subcategoryName);
  static bool IsSettingDisabled(
    optimizations::settings::OptimizationEntity* opt);

  // Static callback for setting creation notifications
  static OnSettingCreatedCallback onSettingCreatedCallback_;
};

}  // namespace optimize_components
