#pragma once

#include <functional>

#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QWidget>

// Forward declarations
struct SettingCategory;

namespace optimize_components {

/**
 * @class SettingsChecker
 * @brief Loads optimization entities from backend systems and converts them to
 * UI-ready categories
 *
 * Main entry point for system scanning. Loads optimizations from all sources
 * (registry, NVIDIA, power plans, games), reads current values, and creates
 * backup points for restoration.
 */
class SettingsChecker : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Constructor - initializes checker with parent for signal handling
   */
  explicit SettingsChecker(QObject* parent = nullptr);

  /**
   * @brief Main entry point - loads all optimization types and checks current
   * values
   * @return Complete category tree containing all loaded settings, empty if
   * loading failed
   * @note Potentially long-running operation (2-10 seconds) that emits progress
   * signals
   */
  QVector<SettingCategory> LoadAndCheckSettings();

  /**
   * @brief Checks if application has Windows administrator privileges
   * @return True if running with admin rights, false if standard user
   */
  bool IsRunningAsAdmin() const;

  /**
   * @brief Creates backup points for settings restoration and undo
   * functionality
   * @return True if backup points were successfully created or already exist
   * @note Should be called before applying optimizations to ensure restoration
   * is possible
   */
  bool CreateRevertPoints();

  /**
   * @brief Attempts to add Rust game optimizations if game is installed
   * @param categories Mutable reference to category vector to append Rust
   * settings to
   * @return True if Rust was found and settings were successfully added
   */
  bool AddRustSettings(QVector<SettingCategory>& categories);

 signals:
  /**
   * @brief Progress update during LoadAndCheckSettings() operation
   * @param progress Percentage of completion (0-100)
   * @param message User-friendly status description
   */
  void checkProgress(int progress, const QString& message);

  /**
   * @brief Completion signal when LoadAndCheckSettings() finishes
   * @param success True if operation completed successfully
   * @param errorMessage Error details if success is false, empty on success
   */
  void checkComplete(bool success, const QString& errorMessage);

 private:
  /**
   * @brief Loads registry-based optimizations from JSON configuration
   * @return bool True if registry settings were loaded successfully
   */
  bool LoadRegistrySettings();

  /**
   * @brief Loads NVIDIA graphics driver optimizations via API
   * @return bool True if NVIDIA settings were loaded successfully
   */
  bool LoadNvidiaSettings();

  /**
   * @brief Loads Windows visual effects and performance settings
   * @return bool True if visual effects settings were loaded successfully
   */
  bool LoadVisualEffectsSettings();

  /**
   * @brief Loads Windows power plan configuration settings
   * @return bool True if power plan settings were loaded successfully
   */
  bool LoadPowerPlanSettings();

  /**
   * @brief Converts loaded OptimizationEntity objects to UI category structure
   * @return QVector<SettingCategory> Organized categories ready for UI display
   */
  QVector<SettingCategory> ConvertOptimizationsToCategories();
};

}  // namespace optimize_components
