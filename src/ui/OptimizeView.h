#pragma once
/**
 * @file OptimizeView.h
 * @brief Main UI orchestrator for the optimization settings interface
 *
 * CORE RESPONSIBILITY:
 * - Acts as the primary UI coordinator and event handler for the optimization
 * settings interface
 * - Manages the main 3-section layout: header (with toggles), scrollable
 * content area, and action buttons
 * - Orchestrates data flow between backend optimization data and frontend UI
 * components
 * - Handles all user interactions and UI state changes (advanced/Rust toggles,
 * category mode changes)
 * - Supports loading settings profiles from exported JSON files
 *
 * COMPONENT USAGE & DELEGATION:
 * - SettingsChecker: Loads current system values from registry, NVIDIA, etc.
 * (called via performSettingsCheck())
 * - SettingsCategoryConverter: Converts OptimizationEntity objects to
 * SettingCategory UI structures (used in addCategory())
 * - SettingsUIBuilder: Creates actual Qt widgets from SettingCategory data
 * (called via buildSettingsUI())
 * - SettingsApplicator: Handles applying setting changes to the system (called
 * via onApplySettings())
 * - RevertManager: Manages reverting to previous/original settings (called via
 * showRevertDialog())
 * - UnknownValueManager: Persists and restores custom setting values (used
 * throughout)
 * - ImportSettings: Loads settings profiles from exported JSON files (called
 * via onProfileSelected())
 *
 * UI LAYOUT STRUCTURE:
 * ┌─────────────────────────────────────────────────────────────┐
 * │ TOP HEADER (always visible)                                 │
 * │ - Title: "Optimization Settings"                           │
 * │ - "Load Profile:" dropdown (left side)                     │
 * │ - "Show Rust Settings" toggle (right side)                 │
 * │ - "Show Advanced Settings" toggle (right side)             │
 * │ - Horizontal line separator                                 │
 * └─────────────────────────────────────────────────────────────┘
 * │ MIDDLE SECTION (scrollable, initially empty)               │
 * │ - Scroll area containing settings categories                │
 * │ - Empty on startup, populated after "Check Current Settings│
 * │ - Shows categorized settings with dropdowns/toggles        │
 * │ - All actual widget creation delegated to SettingsUIBuilder│
 * └─────────────────────────────────────────────────────────────┘
 * │ BOTTOM PANEL (always visible)                              │
 * │ - "Check Current Settings" button                          │
 * │ - Status label (for operation feedback)                    │
 * │ - "Revert Settings" button                                 │
 * │ - "Apply Settings" button                                  │
 * └─────────────────────────────────────────────────────────────┘
 *
 * CLEAR BOUNDARIES:
 * - This class ONLY handles top-level layout, event routing, and component
 * orchestration
 * - Does NOT create individual setting widgets (delegated to SettingsUIBuilder)
 * - Does NOT convert backend data (delegated to SettingsCategoryConverter)
 * - Does NOT apply settings to system (delegated to SettingsApplicator)
 * - Does NOT import profile files (delegated to ImportSettings)
 * - Does NOT manage widget styling beyond header controls (delegated to
 * SettingsUIBuilder)
 *
 * MODIFICATION GUIDELINES:
 * - Layout changes: Modify setupLayout() method only
 * - New UI functionality: Add to header or bottom panel, delegate complex
 * widget creation to SettingsUIBuilder
 * - Data flow changes: Modify the component orchestration methods
 * (performSettingsCheck, buildSettingsUI, etc.)
 * - Styling changes: Modify SettingsUIBuilder unless it's header/bottom panel
 * specific
 *
 * ===== SETTING VALUE SOURCES =====
 *
 * "Original" Values (Orange tag):
 * - Loaded from BackupManager main backup files
 * - Represent user's system settings before Checkmark made any changes
 * - Used to restore settings to pre-application state
 *
 * "Recommended" Values (Blue tag):
 * - Come directly from OptimizationEntity::GetRecommendedValue()
 * - Defined in hardcoded registry setting definitions or hardcoded for other
 *   setting types
 * - Represent optimal values for performance/functionality
 *
 * Current Values:
 * - Live system values loaded by SettingsChecker from actual registry/system
 * state
 * - Retrieved via OptimizationEntity::GetCurrentValue() which reads current
 * system state
 * - MUST reflect the actual current setting values, not cached or default
 * values
 * - Displayed as the selected option in dropdowns/toggles
 * - Settings with no accessible/valid current values are filtered out from UI
 * - Dropdown shows actual registry values (e.g., "10") not descriptions (e.g.,
 * "Default threshold")
 *
 * Profile Values:
 * - Loaded from exported JSON files via ImportSettings
 * - Applied to UI widgets without changing system settings immediately
 * - User must click "Apply" to actually apply imported profile values to system
 * - Profile loading updates settingsStates map and refreshes UI widgets
 *
 * ===== UNKNOWN VALUE HANDLING SYSTEM =====
 *
 * The system provides a robust mechanism for handling "unknown values" - values
 * that aren't in the predefined options list. This occurs when users have
 * custom settings or when values from other software are detected.
 *
 * HOW UNKNOWN VALUES ARE HANDLED:
 *
 * 1. DETECTION:
 *    - During loadCurrentSettings(), if a value isn't found in a dropdown's
 * predefined options, it's identified as an unknown value.
 *    - Each unknown value is tracked in the 'unknownValues' map, where the key
 * is the setting ID and the value is a list of QVariants representing the
 * unknown values.
 *    - Unknown values are identified by comparing with defined possible values
 * using consistent type handling (string/int/bool).
 *
 * 2. STORAGE:
 *    - All unknown values are persistently saved to
 * settings_backup/unknown_values.json
 *    - This file is managed by BackupManager and preserves unknown values
 * across sessions
 *    - The file format uses a JSON structure that preserves value types
 * (int/string/bool)
 *    - Values are never removed from this file to ensure settings aren't lost
 *    - New values are merged with existing ones
 *
 * 3. DISPLAY:
 *    - When building the UI in buildSettingsUI(), all stored unknown values are
 * added to the appropriate dropdowns with "(Custom)" notation
 *    - Unknown values appear as regular options but are marked as custom
 *    - Special formatting ensures type consistency (int, string, bool)
 *
 * 4. TYPE CONSISTENCY:
 *    - Type checks ensure values like "0" (string) and 0 (int) are treated the
 * same
 *    - Numeric strings are converted to int QVariants for consistency
 *    - Boolean values are consistently handled as bool QVariants
 *
 * 5. PERSISTENCE GUARANTEES:
 *    - Even when custom values aren't currently used, they remain in the
 * dropdown
 *    - If a user changes a setting and later wants to revert, all previous
 * values remain available
 *    - Values detected across different sessions are merged, never overwritten
 *
 * This approach both preserves UI consistency and ensures user customizations
 * are never lost, even when they fall outside the normal range of predefined
 * values.
 */

#include <functional>
#include <vector>

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include "optimization/ImportSettings.h"
#include "optimization/OptimizationEntity.h"
#include "ui/SettingsDropdown.h"
#include "ui/SettingsToggle.h"
#include "ui/optimize_components/RevertManager.h"
#include "ui/optimize_components/SettingsApplicator.h"
#include "ui/optimize_components/SettingsCategoryConverter.h"
#include "ui/optimize_components/SettingsChecker.h"
#include "ui/optimize_components/SettingsUIBuilder.h"
#include "ui/optimize_components/UnknownValueManager.h"

// Forward declarations
struct SettingCategory;
namespace optimizations {
namespace rust {
class RustConfigManager;
}
}  // namespace optimizations

// Enum for setting types
enum class SettingType {
  Toggle,
  Dropdown,
  Button  // New type for button-only settings that trigger an action
};

// Enum for revert types
enum class RevertType {
  SessionOriginals,  // Revert to values from when the app session was started
  SystemDefaults  // Revert to system default values before app ever touched them
};

// Enum for category modes
enum class CategoryMode {
  KeepOriginal,  // Use original values from backup
  Recommended,   // Use recommended optimal values
  Custom         // User-customized values
};

// Structure to hold a single dropdown option (matching JSON file format)
struct SettingOption {
  QVariant value;
  QString name;
  QString description;
};

// Structure to hold setting definition (matching JSON file format)
struct SettingDefinition {
  QString id;
  QString name;
  QString description;
  SettingType type;

  // Original fields from JSON
  QString registry_key;
  QString registry_value_name;
  QVariant default_value;
  QVariant recommended_value;
  QString category;
  QString subcategory;
  bool is_advanced = false;
  bool isDisabled = false;  // Flag to disable a setting
  bool isMissing =
    false;  // Flag to indicate if this setting doesn't exist on the system
  int level = 0;  // Setting level: 0=normal, 1=optional, 2=experimental

  // UI-specific fields
  QList<SettingOption> possible_values;  // Options from the JSON file

  // Functions to get/set values
  std::function<bool()> getCurrentValueFn;
  std::function<bool(bool)> setToggleValueFn;
  std::function<QVariant()> getDropdownValueFn;
  std::function<bool(const QVariant&)> setDropdownValueFn;
  std::function<bool()> setButtonActionFn;  // New function for button action
};

// Structure to define a category of settings
struct SettingCategory {
  QString id;
  QString name;
  QString description;
  QVector<SettingDefinition> settings;
  QVector<SettingCategory> subCategories;          // Nested categories
  CategoryMode mode = CategoryMode::KeepOriginal;  // Default to keep original
  bool isRecommendedMode =
    false;  // Flag for recommended vs custom mode (legacy)
};

class OptimizeView : public QWidget {
  Q_OBJECT
 public:
  OptimizeView(QWidget* parent = nullptr);
  void cancelOperations();

  // Add a setting category
  void addCategory(const SettingCategory& category);

  // Clear all categories
  void clearCategories();

  // Clear all categories with optional loading placeholder
  void clearCategories(bool showLoadingPlaceholder);

  // Add public getter for advanced settings flag
  bool getShowAdvancedSettings() const { return showAdvancedSettings; }

  // Add public getter for Rust settings flag
  bool getShowRustSettings() const { return showRustSettings; }

  // Add public getter for RevertManager
  optimize_components::RevertManager* getRevertManager() {
    return &revertManager;
  }

 private slots:
  /**
   * @brief Settings check and rendering flow
   *
   * This method implements the following process when "Check Current Settings"
   * is clicked:
   * 1. Load all defined settings from various sources:
   *    - Registry settings from hardcoded definitions
   *    - Hardcoded settings for NVIDIA, Visual Effects, Power Plans, etc.
   *    - Rust game settings if available
   *
   * 2. For each setting, check if it exists and is accessible on the user's
   * system
   *    - Registry settings are checked using Windows Registry access
   *    - NVIDIA settings via NVIDIA API
   *    - Other settings via their respective APIs
   *
   * 3. For accessible settings, retrieve their current values from the system
   *    - These values are collected in optimizations::OptimizationManager
   *
   * 4. Create a deduplicated list of settings with their current values
   *    - Each unique setting ID appears only once in the UI
   *    - Current value is consistently displayed across all instances
   *
   * 5. Add previously used "unknown values" as additional options
   *    - Unknown values are loaded from backup storage
   *    - They're added as custom options to dropdown settings
   *
   * 6. Render the UI components based on this consolidated data
   *    - Each setting appears only once with accurate current value
   */
  void onCheckCurrentSettings();

  void onApplySettings();
  void onToggleChanged(const QString& settingId, bool enabled);
  void onDropdownChanged(const QString& settingId, const QVariant& value);
  void onRecommendedModeChanged(const QString& categoryId, bool isRecommended);
  void onCategoryModeChanged(const QString& categoryId, int modeIndex);
  void onRevertSettings(RevertType type);
  void showRevertDialog();
  void onButtonClicked(const QString& settingId);

  // Profile management slots
  void onProfileSelected(int index);
  void refreshProfileList();
  void onSaveAsProfile();

  // New slot for actually performing check after UI updates
  void performSettingsCheck();

 private:
  void setupLayout();
  void buildSettingsUI();
  void clearMainLayout();  // Clear widgets from main layout
  void loadCurrentSettings();
  void storeSessionOriginals();  // Delegate to RevertManager
  QGroupBox* createCategoryGroup(const SettingCategory& category,
                                 int depth = 0);
  void applyRecommendedSettings(const SettingCategory& category);

  // Helper method for status text handling
  void setStatusText(const QString& text);

  // Profile management methods
  void loadSettingsProfile(const QString& profilePath);
  void applyImportedSettingsToUI(
    const optimizations::ImportResult& importResult);
  QString getProfilesDirectory();
  void setupProfileDropdown();

  // Unknown value management has been moved to UnknownValueManager class

  // Method to collect unknown values from UI and save them
  void collectAndSaveUnknownValues();

  // Rust optimization related methods
  void addRustSettingsCategory(
    optimizations::rust::RustConfigManager& rustConfigManager);

  // UI update methods
  void toggleAdvancedSettings(bool show);
  void toggleRustSettings(bool show);
  void loadOriginalSettingsForCategory(const SettingCategory& category);

  QVBoxLayout* mainLayout;
  QScrollArea* scrollArea;
  QWidget* scrollContent;
  QVBoxLayout* scrollLayout;
  QPushButton* applyButton;
  QPushButton* revertButton;  // New button for revert
  QPushButton* checkSettingsButton;
  QPushButton* saveProfileButton;  // New button for saving profiles
  QLabel* statusLabel;             // Status indicator for operations
  QWidget* bottomPanel;
  QWidget* headerWidget;       // Header widget
  QWidget* settingsContainer;  // Container for settings

  // Profile management widgets
  QComboBox* profileDropdown = nullptr;

  // Storage for settings categories
  QVector<SettingCategory> settingCategories;

  // Maps to track settings and their states
  QMap<QString, QWidget*> settingsWidgets;
  QMap<QString, QVariant> settingsStates;

  // Maps to track category widgets and their state
  QMap<QString, QGroupBox*> categoryWidgets;
  QMap<QString, CategoryMode> categoryModes;

  // Map to store button action functions
  QMap<QString, std::function<bool()>> buttonActions;

  // Components
  optimize_components::UnknownValueManager unknownValueManager;
  optimize_components::RevertManager revertManager;
  optimize_components::SettingsUIBuilder uiBuilder;
  optimize_components::SettingsCategoryConverter categoryConverter;
  optimize_components::SettingsApplicator settingsApplicator;
  optimize_components::SettingsChecker settingsChecker;

  // Flag to track if settings are visible
  bool settingsVisible = false;

  // Flag to track if check is in progress
  bool checkInProgress = false;

  // New member variables
  SettingsToggle* advancedSettingsToggle = nullptr;
  SettingsToggle* rustSettingsToggle = nullptr;
  bool showAdvancedSettings = false;
  bool showRustSettings = false;

  // Legacy compatibility map
  QMap<QString, bool> categoryRecommendedModes;
};
