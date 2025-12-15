/**
 * @file SettingsChecker.cpp
 * @brief Implementation of settings loading and checking functionality
 */

#include "SettingsChecker.h"

#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include <Windows.h>
#endif

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QThread>

#include "../OptimizeView.h"  // For SettingCategory and SettingDefinition types
#include "SettingsCategoryConverter.h"
#include "optimization/BackupManager.h"
#include "optimization/NvidiaOptimization.h"
#include "optimization/OptimizationEntity.h"  //This file includes optimizationmanager
#include "optimization/PowerPlanManager.h"
#include "optimization/RegistryLogger.h"
#include "optimization/Rust optimization/config_manager.h"
#include "optimization/VisualEffectsManager.h"

#include "logging/Logger.h"

namespace optimize_components {

SettingsChecker::SettingsChecker(QObject* parent) : QObject(parent) {}

QVector<SettingCategory> SettingsChecker::LoadAndCheckSettings() {
  QVector<SettingCategory> categories;

  try {
    emit checkProgress(10, "Initializing optimization system...");
    QApplication::processEvents();  // Allow UI to update

    // Load registry settings
    emit checkProgress(20, "Loading Windows registry settings...");
    QApplication::processEvents();
    bool registryLoaded = LoadRegistrySettings();
    if (!registryLoaded) {
      emit checkComplete(false, "Failed to load registry settings");
      return categories;  // Return empty if critical component fails
    }

    // Load NVIDIA settings (optional - may fail if no NVIDIA GPU)
    emit checkProgress(40, "Checking for NVIDIA graphics settings...");
    QApplication::processEvents();
    bool nvidiaLoaded = LoadNvidiaSettings();

    // Load Visual Effects settings
    emit checkProgress(55, "Loading Windows visual effects settings...");
    QApplication::processEvents();
    bool visualEffectsLoaded = LoadVisualEffectsSettings();

    // Load Power Plan settings
    emit checkProgress(65, "Loading Windows power plan settings...");
    QApplication::processEvents();
    bool powerPlanLoaded = LoadPowerPlanSettings();

    // Create backup revert points for all loaded settings
    emit checkProgress(75, "Creating backup points for restoration...");
    QApplication::processEvents();
    bool revertPointsCreated = CreateRevertPoints();

    // Convert optimizations to UI categories
    emit checkProgress(85, "Processing optimization settings...");
    QApplication::processEvents();
    auto& optManager = optimizations::OptimizationManager::GetInstance();

    // Get all optimizations by type
    auto registryOpts = optManager.GetOptimizationsByType(
      optimizations::OptimizationType::WindowsRegistry);
    auto nvidiaOpts = optManager.GetOptimizationsByType(
      optimizations::OptimizationType::NvidiaSettings);
    auto visualEffectsOpts = optManager.GetOptimizationsByType(
      optimizations::OptimizationType::VisualEffects);
    auto powerPlanOpts = optManager.GetOptimizationsByType(
      optimizations::OptimizationType::PowerPlan);

    // Combine all optimizations
    std::vector<optimizations::settings::OptimizationEntity*> allOptimizations;
    allOptimizations.insert(allOptimizations.end(), registryOpts.begin(),
                            registryOpts.end());
    allOptimizations.insert(allOptimizations.end(), nvidiaOpts.begin(),
                            nvidiaOpts.end());
    allOptimizations.insert(allOptimizations.end(), visualEffectsOpts.begin(),
                            visualEffectsOpts.end());
    allOptimizations.insert(allOptimizations.end(), powerPlanOpts.begin(),
                            powerPlanOpts.end());

    // Convert to UI category structure
    SettingCategory rootCategory =
      optimize_components::SettingsCategoryConverter::ConvertToUICategory(
        allOptimizations);

    // Add the root category's subcategories to our categories list
    for (const auto& subCategory : rootCategory.subCategories) {
      categories.append(subCategory);
    }

    // Add Rust settings to the appropriate category
    emit checkProgress(95, "Checking for Rust game settings...");
    QApplication::processEvents();
    bool rustAdded = AddRustSettings(categories);

    emit checkProgress(100, "Settings check completed successfully!");
    QApplication::processEvents();
    emit checkComplete(true, "");

  } catch (const std::exception& e) {
    // Log error but don't crash
    emit checkComplete(
      false, QString("Error during settings check: %1").arg(e.what()));
  }

  return categories;
}

bool SettingsChecker::LoadRegistrySettings() {
  try {
    emit checkProgress(22, "Initializing registry settings manager...");
    QApplication::processEvents();

    auto& optManager = optimizations::OptimizationManager::GetInstance();

    optManager.Initialize();

    emit checkProgress(25, "Loading registry optimization definitions...");
    QApplication::processEvents();

    bool loadResult = optManager.LoadAllRegistrySettings();

    // Check current registry values to trigger path validation logging
    if (loadResult) {
      emit checkProgress(35, "Validating registry paths...");
      QApplication::processEvents();
      LOG_INFO << "[Registry Debug] Checking registry paths for missing entries...";
      optManager.CheckAllRegistrySettings();
    }

    return loadResult;
  } catch (const std::exception& e) {
    return false;
  }
}

bool SettingsChecker::LoadNvidiaSettings() {
  try {
    // Initialize NVIDIA control panel with timeout protection
    auto& nvidiaCP = optimizations::nvidia::NvidiaControlPanel::GetInstance();

    bool initialized = false;
    try {
      initialized = nvidiaCP.Initialize();
    } catch (const std::exception& e) {
      return true;  // Return true since NVIDIA is optional
    } catch (...) {
      return true;  // Return true since NVIDIA is optional
    }

    if (!initialized) {
      return true;  // Return true since this is optional
    }

    // Load NVIDIA optimizations
    auto& optManager = optimizations::OptimizationManager::GetInstance();

    try {
      auto nvidiaOpts = nvidiaCP.CreateNvidiaOptimizations();
      // The OptimizationManager should automatically manage these
    } catch (const std::exception& e) {
      return true;  // Still return true since NVIDIA is optional
    }

    return true;
  } catch (const std::exception& e) {
    return true;  // Return true since NVIDIA is optional
  } catch (...) {
    return true;  // Return true since NVIDIA is optional
  }
}

bool SettingsChecker::LoadVisualEffectsSettings() {
  try {
    auto& visualEffectsManager =
      optimizations::visual_effects::VisualEffectsManager::GetInstance();

    bool initialized = false;
    try {
      initialized = visualEffectsManager.Initialize();
    } catch (const std::exception& e) {
      return false;  // Visual Effects failure is more critical
    }

    return initialized;
  } catch (const std::exception& e) {
    return false;
  }
}

bool SettingsChecker::LoadPowerPlanSettings() {
  try {
    auto& powerPlanManager =
      optimizations::power::PowerPlanManager::GetInstance();

    bool initialized = false;
    try {
      initialized = powerPlanManager.Initialize();
    } catch (const std::exception& e) {
      return false;  // Power Plan failure is more critical
    }

    return initialized;
  } catch (const std::exception& e) {
    return false;
  }
}

bool SettingsChecker::CreateRevertPoints() {
  try {
    emit checkProgress(77, "Initializing backup system...");
    QApplication::processEvents();

    auto& backupManager = optimizations::BackupManager::GetInstance();
    if (!backupManager.Initialize()) {
      return false;
    }

    // Initialize registry logger with the same directory as backup manager
    emit checkProgress(80, "Setting up registry logging...");
    QApplication::processEvents();

    auto& registryLogger =
      optimizations::registry::RegistryLogger::GetInstance();
    registryLogger.Initialize(
      QCoreApplication::applicationDirPath().toStdString());

    // Create main backups for all types if they don't exist
    emit checkProgress(82, "Creating system backup points...");
    QApplication::processEvents();

    return backupManager.CreateInitialBackups();

  } catch (const std::exception& e) {
    return false;
  }
}

bool SettingsChecker::AddRustSettings(QVector<SettingCategory>& categories) {
  try {
    emit checkProgress(96, "Detecting Rust game installation...");
    QApplication::processEvents();

    // Try to initialize the Rust config manager
    auto& rustManager = optimizations::rust::RustConfigManager::GetInstance();
    if (!rustManager.Initialize()) {
      return false;
    }

    emit checkProgress(97, "Loading Rust configuration settings...");
    QApplication::processEvents();

    // Check Rust settings to get current values
    int differentCount = rustManager.CheckSettings();
    if (differentCount == -1) {
      return false;
    }

    emit checkProgress(98, "Processing Rust game settings...");
    QApplication::processEvents();

    // Get all Rust settings
    const auto& allRustSettings = rustManager.GetAllSettings();
    if (allRustSettings.empty()) {
      return false;
    }

    // Create Rust as its own top-level category
    SettingCategory rustCategory;
    rustCategory.id = "rust_game";
    rustCategory.name = "Rust Game";
    rustCategory.description =
      "Rust game configuration settings for optimal performance";
    rustCategory.mode = CategoryMode::Custom;  // Start in custom mode

    // Create subcategories for organization
    SettingCategory graphicsCategory;
    graphicsCategory.id = "rust_graphics";
    graphicsCategory.name = "Graphics";
    graphicsCategory.description = "Rust graphics and rendering settings";
    graphicsCategory.mode = CategoryMode::Custom;

    SettingCategory effectsCategory;
    effectsCategory.id = "rust_effects";
    effectsCategory.name = "Effects";
    effectsCategory.description = "Rust visual effects settings";
    effectsCategory.mode = CategoryMode::Custom;

    SettingCategory otherCategory;
    otherCategory.id = "rust_other";
    otherCategory.name = "Other";
    otherCategory.description = "Rust miscellaneous settings";
    otherCategory.mode = CategoryMode::Custom;

    // Process each Rust setting
    for (const auto& [key, rustSetting] : allRustSettings) {
      SettingDefinition def;
      def.id = "rust_" + key;
      def.name = key;
      def.description = rustSetting.description.isEmpty()
                          ? QString("Rust setting: %1").arg(key)
                          : rustSetting.description;
      def.is_advanced = false;  // Rust settings are not advanced
      def.isDisabled = false;

      // ALWAYS use dropdown type for Rust settings
      def.type = SettingType::Dropdown;

      // Start with the possible values from the rust setting
      QSet<QString>
        uniqueValues;  // Track unique string values to avoid duplicates

      // Add the predefined possible values first
      for (const QVariant& possibleValue : rustSetting.possibleValues) {
        QString strValue = possibleValue.toString();
        if (!uniqueValues.contains(strValue)) {
          SettingOption option;
          option.value =
            QVariant(strValue);  // Store as string to maintain consistency
          option.name = strValue;
          option.description = "";
          def.possible_values.append(option);
          uniqueValues.insert(strValue);
        }
      }

      // Ensure current value is included in possible values
      QString currentValue = rustSetting.currentValue;
      if (!currentValue.isEmpty() && currentValue != "missing" &&
          !uniqueValues.contains(currentValue)) {
        SettingOption currentOption;
        currentOption.value = QVariant(currentValue);
        currentOption.name = currentValue;
        currentOption.description = "";
        def.possible_values.append(currentOption);
        uniqueValues.insert(currentValue);
      }

      // Ensure optimal value is included in possible values
      QString optimalValue = rustSetting.optimalValue;
      if (!optimalValue.isEmpty() && !uniqueValues.contains(optimalValue)) {
        SettingOption optimalOption;
        optimalOption.value = QVariant(optimalValue);
        optimalOption.name = optimalValue;
        optimalOption.description = "";
        def.possible_values.append(optimalOption);
        uniqueValues.insert(optimalValue);
      }

      // Set default and recommended values using raw string values
      def.default_value = QVariant(currentValue);
      def.recommended_value = QVariant(optimalValue);

      // Set getter function - return raw string values to maintain consistency
      def.getDropdownValueFn = [key, &rustManager]() -> QVariant {
        const auto& settings = rustManager.GetAllSettings();
        auto it = settings.find(key);
        if (it != settings.end()) {
          QString currentVal = it->second.currentValue;

          // Handle missing values by returning optimal value
          if (currentVal.isEmpty() || currentVal == "missing") {
            return QVariant(it->second.optimalValue);
          }

          // Return the raw value as string to maintain consistency with backup
          // system
          return QVariant(currentVal);
        }
        return QVariant();
      };

      // Set setter function - convert any input to appropriate string format
      def.setDropdownValueFn = [key,
                                &rustManager](const QVariant& value) -> bool {
        QString stringValue = value.toString();
        return rustManager.ApplySetting(key, stringValue);
      };

      // Add setting to appropriate subcategory based on key name
      if (key.startsWith("graphics.") || key.startsWith("graphicssettings.") ||
          key.startsWith("mesh.") || key.startsWith("tree.") ||
          key.startsWith("water.") || key.startsWith("grass.") ||
          key.startsWith("terrain.") || key.startsWith("render.")) {
        graphicsCategory.settings.append(def);
      } else if (key.startsWith("effects.")) {
        effectsCategory.settings.append(def);
      } else {
        otherCategory.settings.append(def);
      }
    }

    // Add subcategories to main category (only if they have settings)
    if (!graphicsCategory.settings.isEmpty()) {
      rustCategory.subCategories.append(graphicsCategory);
    }

    if (!effectsCategory.settings.isEmpty()) {
      rustCategory.subCategories.append(effectsCategory);
    }

    if (!otherCategory.settings.isEmpty()) {
      rustCategory.subCategories.append(otherCategory);
    }

    // Add the Rust category as its own top-level category
    if (!rustCategory.subCategories.isEmpty()) {
      categories.append(rustCategory);
      return true;
    } else {
      return false;
    }

  } catch (const std::exception& e) {
    return false;
  } catch (...) {
    return false;
  }
}

bool SettingsChecker::IsRunningAsAdmin() const {
  // Check if the application is running with elevated privileges.
  // Prefer a token membership check over filesystem probes (safer and more accurate).
#ifdef _WIN32
  BOOL isMember = FALSE;
  PSID adminGroup = NULL;
  SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
  if (!AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                &adminGroup)) {
    return false;
  }

  if (!CheckTokenMembership(NULL, adminGroup, &isMember)) {
    isMember = FALSE;
  }

  FreeSid(adminGroup);
  return isMember == TRUE;
#else
  return false;
#endif
}

}  // namespace optimize_components
