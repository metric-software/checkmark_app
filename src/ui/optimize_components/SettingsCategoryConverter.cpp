/**
 * @file SettingsCategoryConverter.cpp
 * @brief Implementation of converter from optimization entities to UI
 * categories
 */

#include "SettingsCategoryConverter.h"

#include <iostream>
#include <set>
#include <variant>

#include <QJsonArray>

#include "../../ApplicationSettings.h"
#include "../../optimization/BackupManager.h"
#include "../../optimization/NvidiaControlPanel.h"
#include "../../optimization/OptimizationEntity.h"
#include "../../optimization/RegistrySettings.h"
#include "../OptimizeView.h"

#include "logging/Logger.h"

namespace optimize_components {

// Static member definition
SettingsCategoryConverter::OnSettingCreatedCallback
  SettingsCategoryConverter::onSettingCreatedCallback_;

void SettingsCategoryConverter::SetOnSettingCreatedCallback(
  OnSettingCreatedCallback callback) {
  onSettingCreatedCallback_ = std::move(callback);
}

SettingCategory SettingsCategoryConverter::ConvertToUICategory(
  const std::vector<optimizations::settings::OptimizationEntity*>&
    optimizations) {

  SettingCategory rootCategory;
  rootCategory.id = "root";
  rootCategory.name = "All Settings";
  rootCategory.description = "Complete list of optimization settings";

  if (optimizations.empty()) {
    return rootCategory;
  }

  // Group optimizations by category
  std::map<std::string,
           std::vector<optimizations::settings::OptimizationEntity*>>
    categoryGroups;

  for (auto* opt : optimizations) {
    if (!opt) continue;

    std::string categoryName = "General";  // Default category
    if (auto* regOpt =
          dynamic_cast<optimizations::settings::RegistryOptimization*>(opt)) {
      // For RegistryOptimization, check if it's a ConfigurableOptimization
      if (auto* configOpt =
            dynamic_cast<optimizations::settings::ConfigurableOptimization*>(
              regOpt)) {
        categoryName = configOpt->GetCategory();
      } else {
        categoryName = "Registry";
      }
    } else if (opt->GetType() ==
               optimizations::OptimizationType::NvidiaSettings) {
      categoryName = "NVIDIA";
    } else if (opt->GetType() ==
               optimizations::OptimizationType::VisualEffects) {
      categoryName = "Visual Effects";
    } else if (opt->GetType() == optimizations::OptimizationType::PowerPlan) {
      categoryName = "Power";
    }

    if (categoryName.empty()) {
      categoryName = "Miscellaneous";
    }

    categoryGroups[categoryName].push_back(opt);
  }

  // Convert each category group
  for (const auto& [categoryName, categoryOpts] : categoryGroups) {
    SettingCategory category = ConvertCategoryGroup(categoryName, categoryOpts);
    if (!category.settings.isEmpty() || !category.subCategories.isEmpty()) {
      rootCategory.subCategories.append(category);
    }
  }

  return rootCategory;
}

SettingCategory SettingsCategoryConverter::ConvertCategoryGroup(
  const std::string& categoryName,
  const std::vector<optimizations::settings::OptimizationEntity*>&
    optimizations) {

  SettingCategory category;
  category.id =
    QString::fromStdString(categoryName).toLower().replace(" ", "_");
  category.name = QString::fromStdString(categoryName);
  category.description = GetCategoryDescription(categoryName);
  category.isRecommendedMode = false;

  // Group optimizations by subcategory
  std::map<std::string,
           std::vector<optimizations::settings::OptimizationEntity*>>
    subcategoryGroups;

  for (auto* opt : optimizations) {
    if (!opt) continue;

    try {
      std::string id = opt->GetId();

      // Skip settings that are marked as disabled or invalid
      if (IsSettingDisabled(opt)) {
        LOG_INFO << "[SettingsCategoryConverter] Skipping disabled/invalid setting: " << id;
        continue;
      }

      std::string subcategoryName = "General";  // Default subcategory
      if (auto* configOpt =
            dynamic_cast<optimizations::settings::ConfigurableOptimization*>(
              opt)) {
        subcategoryName = configOpt->GetSubcategory();
      }
      if (subcategoryName.empty()) {
        subcategoryName = "General";
      }

      subcategoryGroups[subcategoryName].push_back(opt);

    } catch (const std::exception& e) {
      continue;
    }
  }

  // Convert subcategory groups
  for (const auto& [subcategoryName, subcategoryOpts] : subcategoryGroups) {
    SettingCategory subcategory =
      ConvertSubcategoryGroup(subcategoryName, subcategoryOpts);
    if (!subcategory.settings.isEmpty()) {
      category.subCategories.append(subcategory);
    }
  }

  return category;
}

SettingCategory SettingsCategoryConverter::ConvertSubcategoryGroup(
  const std::string& subcategoryName,
  const std::vector<optimizations::settings::OptimizationEntity*>&
    optimizations) {

  SettingCategory subcategory;
  subcategory.id =
    QString::fromStdString(subcategoryName).toLower().replace(" ", "_");
  subcategory.name = QString::fromStdString(subcategoryName);
  subcategory.description = GetSubcategoryDescription(subcategoryName);
  subcategory.isRecommendedMode = false;

  int processedSettings = 0;
  int skippedSettings = 0;

  for (auto* opt : optimizations) {
    if (!opt) continue;

    try {
      SettingDefinition setting = ConvertOptimizationToSetting(opt);
      if (!setting.id.isEmpty()) {
        subcategory.settings.append(setting);
        processedSettings++;
      } else {
        skippedSettings++;
      }
    } catch (const std::exception& e) {
      skippedSettings++;
    }
  }

  return subcategory;
}

SettingDefinition SettingsCategoryConverter::ConvertOptimizationToSetting(
  optimizations::settings::OptimizationEntity* opt) {

  SettingDefinition setting;

  if (!opt) {
    return setting;  // Return empty setting
  }

  // Apply filtering logic for experimental features and creation permissions
  if (auto* configOpt =
        dynamic_cast<optimizations::settings::ConfigurableOptimization*>(opt)) {
      // Filter out level 2+ settings if experimental features are not enabled
      if (configOpt->GetLevel() >= 2) {
        ApplicationSettings& appSettings = ApplicationSettings::getInstance();
       if (!appSettings.getEffectiveExperimentalFeaturesEnabled()) {
        LOG_INFO << "[Settings Filter] Skipping level "
                  << configOpt->GetLevel() << " setting '" << opt->GetId()
                  << "' - experimental features disabled";
        return setting;  // Return empty setting to filter out
      }
    }

    // Filter out missing settings that don't allow creation
    if (opt->IsMissing() && !configOpt->IsCreationAllowed()) {
      LOG_INFO << "[Settings Filter] Skipping missing setting '"
                << opt->GetId() << "' - creation not allowed";
      return setting;  // Return empty setting to filter out
    }
  }

  try {
    setting.id = QString::fromStdString(opt->GetId());
    setting.name = QString::fromStdString(opt->GetName());
    setting.description = QString::fromStdString(opt->GetDescription());
    setting.is_advanced = opt->IsAdvanced();
    setting.isDisabled = opt->DontEdit();
    setting.isMissing = opt->IsMissing();

    // Set the level property - only available for ConfigurableOptimization
    if (auto* configOpt =
          dynamic_cast<optimizations::settings::ConfigurableOptimization*>(
            opt)) {
      setting.level = configOpt->GetLevel();
    } else {
      setting.level =
        0;  // Default to normal level for non-configurable optimizations
    }

    // ALWAYS treat as dropdown - no more toggle logic
    setting.type = SettingType::Dropdown;

    // Get current, default, and recommended values using raw values
    try {
      auto currentValue = opt->GetCurrentValue();
      setting.default_value = ConvertOptimizationValueToQVariant(currentValue);
    } catch (const std::exception& e) {
    }

    try {
      auto recommendedValue = opt->GetRecommendedValue();
      setting.recommended_value =
        ConvertOptimizationValueToQVariant(recommendedValue);
    } catch (const std::exception& e) {
    }

    // Setup dropdown functionality first
    SetupDropdownSetting(setting, opt);

    // Setup special button function for missing settings
    if (setting.isMissing) {
      setting.setButtonActionFn = [opt]() -> bool {
        // When user clicks "Add Setting", create the missing registry path with
        // the recommended value
        try {
          auto recommendedValue = opt->GetRecommendedValue();

          // Record the non-existent state in backup BEFORE creating the
          // registry path
          auto& backupManager = optimizations::BackupManager::GetInstance();
          if (!backupManager.RecordNonExistentSetting(opt->GetId())) {
            LOG_WARN << "[UI] Warning: Failed to record non-existent state "
                        "for setting " << opt->GetId();
            // Continue anyway - the backup might already exist
          }

          // Get the registry settings instance and create the missing path
          auto& registrySettings =
            optimizations::registry::RegistrySettings::GetInstance();
          bool success = registrySettings.CreateMissingRegistryPath(
            opt->GetId(), recommendedValue);

          if (success) {
            // Mark the setting as no longer missing
            opt->SetMissing(false);

            LOG_INFO << "[UI] Successfully created missing registry setting: "
                      << opt->GetId();

            // Request UI refresh by triggering a settings check
            if (SettingsCategoryConverter::onSettingCreatedCallback_) {
              SettingsCategoryConverter::onSettingCreatedCallback_(
                opt->GetId());
            }

            return true;
          } else {
            LOG_WARN << "[UI] Failed to create missing registry setting: "
                      << opt->GetId();
            return false;
          }
        } catch (const std::exception& e) {
          LOG_WARN << "[UI] Exception while creating missing setting "
                   << opt->GetId() << ": " << e.what();
          return false;
        }
      };
    }

    // Special handling for wallpaper master control
    if (setting.id == "win.wallpaper.master.control") {
      // Override the apply function to handle multiple registry keys
      if (auto* configOpt =
            dynamic_cast<optimizations::settings::ConfigurableOptimization*>(
              opt)) {
        configOpt->SetCustomApply(
          [](const optimizations::OptimizationValue& value) -> bool {
            LOG_INFO << "[WallpaperMaster] Applying master wallpaper control "
                     << "with value: ";

            int mode = 0;
            if (std::holds_alternative<int>(value)) {
              mode = std::get<int>(value);
            }
            LOG_INFO << mode;

            bool success = true;

            if (mode == 0) {
              // Use Current Picture - Set to picture mode, disable spotlight
              LOG_INFO << "[WallpaperMaster] Setting to Picture mode";
              success &=
                optimizations::registry::RegistrySettings::ApplyRegistryValue(
                  "HKEY_CURRENT_"
                  "USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"
                  "\\Wallpapers",
                  "BackgroundType", 0, 0, "win.wallpaper.master.control");
              success &=
                optimizations::registry::RegistrySettings::ApplyRegistryValue(
                  "HKEY_CURRENT_"
                  "USER\\Software\\Microsoft\\Windows\\CurrentVersion\\ContentD"
                  "eliveryManager",
                  "SubscribedContent-338389Enabled", 0, 1,
                  "win.wallpaper.master.control");
            } else if (mode == 1) {
              // Solid Black Background
              LOG_INFO << "[WallpaperMaster] Setting to Solid Black mode";
              success &=
                optimizations::registry::RegistrySettings::ApplyRegistryValue(
                  "HKEY_CURRENT_"
                  "USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"
                  "\\Wallpapers",
                  "BackgroundType", 1, 0, "win.wallpaper.master.control");
              success &=
                optimizations::registry::RegistrySettings::ApplyRegistryValue(
                  "HKEY_CURRENT_"
                  "USER\\Software\\Microsoft\\Windows\\CurrentVersion\\ContentD"
                  "eliveryManager",
                  "SubscribedContent-338389Enabled", 0, 1,
                  "win.wallpaper.master.control");
              success &=
                optimizations::registry::RegistrySettings::ApplyRegistryValue(
                  "HKEY_CURRENT_USER\\Control Panel\\Colors", "Background",
                  std::string("0 0 0"), std::string("0 78 158"),
                  "win.wallpaper.master.control");
              success &=
                optimizations::registry::RegistrySettings::ApplyRegistryValue(
                  "HKEY_CURRENT_USER\\Control Panel\\Desktop", "Wallpaper",
                  std::string(""), std::string(""),
                  "win.wallpaper.master.control");
            } else if (mode == 2) {
              // Windows Spotlight
              LOG_INFO << "[WallpaperMaster] Setting to Spotlight mode";
              success &=
                optimizations::registry::RegistrySettings::ApplyRegistryValue(
                  "HKEY_CURRENT_"
                  "USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"
                  "\\Wallpapers",
                  "BackgroundType", 0, 0, "win.wallpaper.master.control");
              success &=
                optimizations::registry::RegistrySettings::ApplyRegistryValue(
                  "HKEY_CURRENT_"
                  "USER\\Software\\Microsoft\\Windows\\CurrentVersion\\ContentD"
                  "eliveryManager",
                  "SubscribedContent-338389Enabled", 1, 1,
                  "win.wallpaper.master.control");
            }

            if (success) {
              LOG_INFO << "[WallpaperMaster] Successfully applied wallpaper mode "
                        << mode;
            } else {
              LOG_WARN << "[WallpaperMaster] Failed to apply wallpaper mode "
                       << mode;
            }

            return success;
          });

        // Override the get current value function to detect current mode
        configOpt->SetCustomGetCurrentValue(
          []() -> optimizations::OptimizationValue {
            // Read current registry values to determine the mode
            auto backgroundType =
              optimizations::registry::RegistrySettings::GetRegistryValue(
                "HKEY_CURRENT_"
                "USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
                "Wallpapers",
                "BackgroundType", 0);
            auto spotlightEnabled =
              optimizations::registry::RegistrySettings::GetRegistryValue(
                "HKEY_CURRENT_"
                "USER\\Software\\Microsoft\\Windows\\CurrentVersion\\ContentDel"
                "iveryManager",
                "SubscribedContent-338389Enabled", 1);

            int bgType = 0;
            int spotlightState = 1;

            if (std::holds_alternative<int>(backgroundType)) {
              bgType = std::get<int>(backgroundType);
            }
            if (std::holds_alternative<int>(spotlightEnabled)) {
              spotlightState = std::get<int>(spotlightEnabled);
            }

            LOG_INFO << "[WallpaperMaster] Current state - BackgroundType: "
                      << bgType << ", Spotlight: " << spotlightState;

            // Determine mode based on current settings
            if (bgType == 1) {
              // Solid color mode
              return 1;
            } else if (bgType == 0 && spotlightState == 1) {
              // Spotlight mode
              return 2;
            } else {
              // Picture mode (default)
              return 0;
            }
          });
      }
    }

    // Check if we have proper possible values from the optimization entity
    auto possibleValues = opt->GetPossibleValues();

    if (possibleValues.empty() || setting.possible_values.isEmpty()) {
      // No predefined possible values - create them based on current and
      // recommended values

      // Clear any existing values
      setting.possible_values.clear();

      // Get the raw current and recommended values
      QVariant currentVal = setting.default_value;
      QVariant recommendedVal = setting.recommended_value;

      QList<QVariant> uniqueValues;  // Use QList to avoid duplicates

      // Add current value if valid
      if (currentVal.isValid() && !currentVal.isNull()) {
        uniqueValues.append(currentVal);
      }

      // Add recommended value if valid and different
      if (recommendedVal.isValid() && !recommendedVal.isNull()) {
        // Check if it's already in the list to avoid duplicates
        bool alreadyExists = false;
        for (const QVariant& existing : uniqueValues) {
          if (existing == recommendedVal) {
            alreadyExists = true;
            break;
          }
        }
        if (!alreadyExists) {
          uniqueValues.append(recommendedVal);
        }
      }

      // If we still don't have enough values, create default boolean options
      if (uniqueValues.size() < 2) {
        // Try to determine the type from the values we have
        bool shouldUseBoolean = false;

        // Check if current or recommended values suggest boolean type
        if (currentVal.type() == QVariant::Bool ||
            recommendedVal.type() == QVariant::Bool) {
          shouldUseBoolean = true;
        } else if (currentVal.type() == QVariant::Int ||
                   recommendedVal.type() == QVariant::Int) {
          // Check if integer values are 0/1 (boolean-like)
          int currentInt = currentVal.toInt();
          int recommendedInt = recommendedVal.toInt();
          if ((currentInt == 0 || currentInt == 1) &&
              (recommendedInt == 0 || recommendedInt == 1)) {
            shouldUseBoolean = true;
          }
        }

        if (shouldUseBoolean) {
          // Create boolean-style options but keep raw values
          uniqueValues.clear();
          if (currentVal.type() == QVariant::Bool) {
            uniqueValues.append(true);
            uniqueValues.append(false);
          } else {
            // Use integer values 1 and 0
            uniqueValues.append(1);
            uniqueValues.append(0);
          }
        } else {
          // For non-boolean types, try to add some sensible defaults
          if (currentVal.type() == QVariant::String ||
              recommendedVal.type() == QVariant::String) {
            // For string values, just use what we have
            // The unknown value manager will add other values later if needed
          } else {
            // For numeric values, add some defaults around the
            // current/recommended values But only if they make sense
          }
        }
      }

      // Convert the unique values to SettingOption format
      for (const QVariant& value : uniqueValues) {
        SettingOption option;
        option.value = value;

        // Create a clean display name based on the raw value
        if (value.type() == QVariant::Bool) {
          option.name = value.toBool() ? "Enabled" : "Disabled";
        } else if (value.type() == QVariant::Int) {
          int intVal = value.toInt();
          if (intVal == 1 || intVal == 0) {
            // Boolean-like integer values
            option.name = (intVal == 1) ? "Enabled" : "Disabled";
          } else {
            // Regular integer values - show the raw number
            option.name = QString::number(intVal);
          }
        } else if (value.type() == QVariant::String) {
          // String values - show the raw string
          option.name = value.toString();
        } else {
          // Other types - convert to string
          option.name = value.toString();
        }

        option.description = option.name;  // Use same for description
        setting.possible_values.append(option);
      }
    }

  } catch (const std::exception& e) {
    // Return empty setting on error
    setting = SettingDefinition();
  }

  return setting;
}

void SettingsCategoryConverter::SetupToggleSetting(
  SettingDefinition& setting,
  optimizations::settings::OptimizationEntity* opt) {
  // Setup toggle-specific function bindings
  setting.setToggleValueFn = [opt](bool enabled) -> bool {
    try {
      return opt->Apply(enabled);
    } catch (const std::exception& e) {
      return false;
    }
  };

  setting.getCurrentValueFn = [opt]() -> bool {
    try {
      auto currentValue = opt->GetCurrentValue();
      if (std::holds_alternative<bool>(currentValue)) {
        return std::get<bool>(currentValue);
      }
      return false;
    } catch (const std::exception& e) {
      return false;
    }
  };
}

void SettingsCategoryConverter::SetupDropdownSetting(
  SettingDefinition& setting,
  optimizations::settings::OptimizationEntity* opt) {
  // Convert possible values to SettingOption format
  auto possibleValues = opt->GetPossibleValues();
  for (const auto& valueOption : possibleValues) {
    SettingOption option;
    option.value = ConvertOptimizationValueToQVariant(valueOption.value);
    option.name = QString::fromStdString(valueOption.description);
    option.description = QString::fromStdString(valueOption.description);
    setting.possible_values.append(option);
  }

  // Setup dropdown-specific function bindings
  setting.setDropdownValueFn = [opt](const QVariant& value) -> bool {
    try {
      // Convert QVariant back to OptimizationValue
      optimizations::OptimizationValue optValue;
      if (value.type() == QVariant::Bool) {
        optValue = value.toBool();
      } else if (value.type() == QVariant::Int) {
        optValue = value.toInt();
      } else if (value.type() == QVariant::Double) {
        optValue = value.toDouble();
      } else if (value.type() == QVariant::String) {
        optValue = value.toString().toStdString();
      } else {
        return false;
      }

      return opt->Apply(optValue);
    } catch (const std::exception& e) {
      return false;
    }
  };

  setting.getDropdownValueFn = [opt]() -> QVariant {
    try {
      auto currentValue = opt->GetCurrentValue();
      return ConvertOptimizationValueToQVariant(currentValue);
    } catch (const std::exception& e) {
      return QVariant();
    }
  };
}

QVariant SettingsCategoryConverter::ConvertOptimizationValueToQVariant(
  const optimizations::OptimizationValue& value) {
  if (std::holds_alternative<bool>(value)) {
    return QVariant(std::get<bool>(value));
  } else if (std::holds_alternative<int>(value)) {
    return QVariant(std::get<int>(value));
  } else if (std::holds_alternative<double>(value)) {
    return QVariant(std::get<double>(value));
  } else if (std::holds_alternative<std::string>(value)) {
    return QVariant(QString::fromStdString(std::get<std::string>(value)));
  }
  return QVariant();  // Return invalid QVariant for unsupported types
}

bool SettingsCategoryConverter::IsSettingDisabled(
  optimizations::settings::OptimizationEntity* opt) {
  if (!opt) return true;

  // Check if setting is disabled by user preference
  if (opt->DontEdit()) return true;

  // Don't disable missing settings - they will be handled specially in the UI
  // with an "Add Setting" button
  if (opt->IsMissing()) return false;

  // Check if setting has a valid current value (for existing settings only)
  try {
    auto currentValue = opt->GetCurrentValue();
    QVariant currentVariant = ConvertOptimizationValueToQVariant(currentValue);

    // Filter out invalid values that indicate missing/inaccessible settings
    if (!currentVariant.isValid() || currentVariant.toString() == "ERROR" ||
        (currentVariant.type() == QVariant::String &&
         currentVariant.toString().isEmpty())) {
      // If the value indicates an inaccessible setting, but the setting wasn't
      // marked as missing by the registry checker, it likely means the setting
      // exists but has access issues. Mark as disabled.
      return true;
    }

    // Special handling for "__KEY_NOT_FOUND__" - these should be treated as
    // missing settings, not disabled ones. If a setting has "__KEY_NOT_FOUND__"
    // but wasn't marked as missing by the registry checker, it's likely a
    // timing issue. Don't disable these settings - let them be processed as
    // potentially missing.
    if (currentVariant.toString() == "__KEY_NOT_FOUND__") {
      // Mark this setting as missing if it wasn't already
      if (!opt->IsMissing()) {
        opt->SetMissing(true);
      }
      return false;  // Don't disable, let it be handled as a missing setting
    }
  } catch (const std::exception& e) {
    // If we can't get the current value and the setting isn't marked as
    // missing, it likely means there are access issues. Mark as disabled.
    return true;
  }

  return false;
}

QString SettingsCategoryConverter::GetCategoryDescription(
  const std::string& categoryName) {
  // Simple implementation - could be expanded with a lookup table
  return QString("Settings for %1").arg(QString::fromStdString(categoryName));
}

QString SettingsCategoryConverter::GetSubcategoryDescription(
  const std::string& subcategoryName) {
  return QString("Sub-settings for %1")
    .arg(QString::fromStdString(subcategoryName));
}

SettingCategory* SettingsCategoryConverter::FindCategoryById(
  const QString& id, QVector<SettingCategory>& categories) {
  for (auto& category : categories) {
    if (category.id == id) {
      return &category;
    }

    // Search recursively in subcategories
    SettingCategory* found = FindCategoryById(id, category.subCategories);
    if (found) {
      return found;
    }
  }
  return nullptr;
}

void SettingsCategoryConverter::SetRecommendedMode(SettingCategory& category,
                                                   bool isRecommended,
                                                   bool recursive) {
  category.isRecommendedMode = isRecommended;

  if (recursive) {
    for (auto& subCategory : category.subCategories) {
      SetRecommendedMode(subCategory, isRecommended, recursive);
    }
  }
}

void SettingsCategoryConverter::SetCategoryMode(
  SettingCategory& category, CategoryMode mode, bool propagateToSubcategories,
  QMap<QString, CategoryMode>& categoryModes) {

  // Set the mode for this category
  category.mode = mode;
  categoryModes[category.id] = mode;

  // Update the isRecommendedMode flag based on the mode
  category.isRecommendedMode = (mode == CategoryMode::Recommended);

  // Propagate to subcategories if requested
  if (propagateToSubcategories) {
    for (auto& subCategory : category.subCategories) {
      SetCategoryMode(subCategory, mode, propagateToSubcategories,
                      categoryModes);
    }
  }
}

void SettingsCategoryConverter::EnsureUniqueSettings(
  SettingCategory& category, QMap<QString, bool>& addedSettingIds) {
  // Remove duplicate settings
  QVector<SettingDefinition> uniqueSettings;
  for (const auto& setting : category.settings) {
    if (!addedSettingIds.contains(setting.id)) {
      addedSettingIds[setting.id] = true;
      uniqueSettings.append(setting);
    }
  }
  category.settings = uniqueSettings;

  // Process subcategories recursively
  for (auto& subCategory : category.subCategories) {
    EnsureUniqueSettings(subCategory, addedSettingIds);
  }
}

bool SettingsCategoryConverter::AreSettingsMatchingOriginals(
  const SettingCategory& category) {
  // Check all settings in this category
  for (const auto& setting : category.settings) {
    // Get current value
    QVariant currentValue;
    if (setting.type == SettingType::Toggle && setting.getCurrentValueFn) {
      currentValue = setting.getCurrentValueFn();
    } else if (setting.type == SettingType::Dropdown &&
               setting.getDropdownValueFn) {
      currentValue = setting.getDropdownValueFn();
    } else {
      continue;  // Skip if no getter function
    }

    // Get original value from backup
    optimizations::BackupManager& backupManager =
      optimizations::BackupManager::GetInstance();
    QVariant originalValue =
      backupManager.GetOriginalValueFromBackup(setting.id.toStdString());

    if (originalValue.isValid() && currentValue != originalValue) {
      return false;  // Found a setting that doesn't match its original
    }
  }

  // Check subcategories recursively
  for (const auto& subCategory : category.subCategories) {
    if (!AreSettingsMatchingOriginals(subCategory)) {
      return false;
    }
  }

  return true;  // All settings match their originals
}

int SettingsCategoryConverter::FilterValidCategories(
  QVector<SettingCategory>& categories, bool showAdvancedSettings) {
  int removedCount = 0;

  // Helper function to count valid settings in a category
  auto countValidSettings =
    [showAdvancedSettings](const SettingCategory& cat) -> int {
    int validCount = 0;
    for (const auto& setting : cat.settings) {
      if (setting.isDisabled) continue;
      if (setting.is_advanced && !showAdvancedSettings) continue;
      validCount++;
    }
    return validCount;
  };

  // Recursive function to check if a category has valid content
  std::function<bool(const SettingCategory&)> hasValidContent =
    [&countValidSettings,
     &hasValidContent](const SettingCategory& cat) -> bool {
    // Check if this category has valid settings
    if (countValidSettings(cat) > 0) {
      return true;
    }
    // Check subcategories
    for (const auto& subCat : cat.subCategories) {
      if (hasValidContent(subCat)) {
        return true;
      }
    }
    return false;
  };

  // Filter categories in-place
  auto it = categories.begin();
  while (it != categories.end()) {
    if (!hasValidContent(*it)) {
      it = categories.erase(it);
      removedCount++;
    } else {
      ++it;
    }
  }

  return removedCount;
}

bool SettingsCategoryConverter::AddOrReplaceCategory(
  QVector<SettingCategory>& categories, const SettingCategory& newCategory,
  bool showAdvancedSettings) {
  // Helper function to count valid settings
  auto countValidSettings =
    [showAdvancedSettings](const SettingCategory& cat) -> int {
    int validCount = 0;
    for (const auto& setting : cat.settings) {
      if (setting.isDisabled) continue;
      if (setting.is_advanced && !showAdvancedSettings) continue;
      validCount++;
    }
    return validCount;
  };

  // Check if the new category has valid content
  std::function<bool(const SettingCategory&)> hasValidContent =
    [&countValidSettings,
     &hasValidContent](const SettingCategory& cat) -> bool {
    if (countValidSettings(cat) > 0) {
      return true;
    }
    for (const auto& subCat : cat.subCategories) {
      if (hasValidContent(subCat)) {
        return true;
      }
    }
    return false;
  };

  // Skip categories with no valid content
  if (!hasValidContent(newCategory)) {
    return false;
  }

  // Filter out empty subcategories
  SettingCategory filteredCategory = newCategory;
  QVector<SettingCategory> nonEmptySubcategories;

  for (const auto& subCategory : filteredCategory.subCategories) {
    if (hasValidContent(subCategory)) {
      nonEmptySubcategories.append(subCategory);
    }
  }
  filteredCategory.subCategories = nonEmptySubcategories;

  // Skip if category became empty after filtering
  if (countValidSettings(filteredCategory) == 0 &&
      filteredCategory.subCategories.isEmpty()) {
    return false;
  }

  // Check if a category with this ID already exists
  for (int i = 0; i < categories.size(); ++i) {
    if (categories[i].id == filteredCategory.id) {
      // Replace the existing category
      categories[i] = filteredCategory;
      return true;
    }
  }

  // Deduplicate settings in the new category
  SettingCategory deduplicatedCategory = filteredCategory;
  QMap<QString, bool> localSettingIds;

  // Populate existing IDs from all categories
  for (const auto& category : categories) {
    std::function<void(const SettingCategory&)> populateExistingIds =
      [&localSettingIds, &populateExistingIds](const SettingCategory& cat) {
        for (const auto& setting : cat.settings) {
          localSettingIds[setting.id] = true;
        }
        for (const auto& subcat : cat.subCategories) {
          populateExistingIds(subcat);
        }
      };
    populateExistingIds(category);
  }

  // Apply deduplication
  EnsureUniqueSettings(deduplicatedCategory, localSettingIds);

  // Add the deduplicated category if it still has content
  if (countValidSettings(deduplicatedCategory) > 0 ||
      !deduplicatedCategory.subCategories.isEmpty()) {
    categories.append(deduplicatedCategory);
    return true;
  }

  return false;
}

}  // namespace optimize_components
