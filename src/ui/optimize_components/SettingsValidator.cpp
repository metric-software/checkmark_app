/**
 * @file SettingsValidator.cpp
 * @brief Implementation of validation logic for optimization settings
 */

#include "SettingsValidator.h"

#include <iostream>

#include "../../optimization/NvidiaControlPanel.h"
#include "../../optimization/OptimizationEntity.h"
#include "../OptimizeView.h"  // For SettingCategory and SettingDefinition types

namespace optimize_components {

SettingsValidator::SettingsValidator(QObject* parent) : QObject(parent) {}

QVector<SettingsValidator::ValidationIssue> SettingsValidator::
  ValidateAllSettings(const QVector<SettingCategory>& categories) {
  QVector<ValidationIssue> issues;

  // Recursive function to process all settings in the category hierarchy
  std::function<void(const SettingCategory&)> processCategory =
    [this, &issues, &processCategory](const SettingCategory& category) {
      // Process settings in this category
      for (const auto& setting : category.settings) {
        auto settingIssues = ValidateSetting(setting);
        issues.append(settingIssues);
      }

      // Process subcategories
      for (const auto& subCategory : category.subCategories) {
        processCategory(subCategory);
      }
    };

  // Process all top-level categories
  for (const auto& category : categories) {
    processCategory(category);
  }

  // Emit signal with validation results if any issues were found
  if (!issues.isEmpty()) {
    emit validationIssuesFound(issues);
  }

  return issues;
}

QVector<SettingsValidator::ValidationIssue> SettingsValidator::
  ValidateSettingChange(const QString& settingId, const QVariant& newValue,
                        const QVector<SettingCategory>& categories) {
  QVector<ValidationIssue> issues;

  // Find the setting in the categories
  SettingDefinition setting;
  bool settingFound = false;

  // Recursive function to find the setting
  std::function<void(const SettingCategory&)> findSetting =
    [&settingId, &setting, &settingFound,
     &findSetting](const SettingCategory& category) {
      // Check settings in this category
      for (const auto& s : category.settings) {
        if (s.id == settingId) {
          setting = s;
          settingFound = true;
          return;
        }
      }

      // Check subcategories
      for (const auto& subCategory : category.subCategories) {
        if (!settingFound) {
          findSetting(subCategory);
        }

        if (settingFound) {
          return;
        }
      }
    };

  // Find the setting in all categories
  for (const auto& category : categories) {
    findSetting(category);
    if (settingFound) {
      break;
    }
  }

  if (!settingFound) {
    // Setting not found, return an error
    issues.append({settingId, "Setting not found in categories",
                   ValidationIssue::Severity::Error});

    return issues;
  }

  // Check for hardware-specific compatibility issues
  auto hardwareIssues = CheckHardwareCompatibility(settingId, newValue);
  issues.append(hardwareIssues);

  // Check for specific value-related issues
  if (setting.type == SettingType::Dropdown) {
    // Validate the value is one of the possible options
    bool valueValid = false;
    for (const auto& option : setting.possible_values) {
      if (option.value == newValue) {
        valueValid = true;
        break;
      }
    }

    if (!valueValid && setting.possible_values.size() > 0) {
      issues.append({settingId,
                     "Value is not one of the defined options for this setting",
                     ValidationIssue::Severity::Warning});
    }
  }

  // Emit signal with validation results if any issues were found
  if (!issues.isEmpty()) {
    emit validationIssuesFound(issues);
  }

  return issues;
}

int SettingsValidator::FilterInvalidSettings(
  QVector<SettingCategory>& categories) {
  int removedCount = 0;

  // First check if NVIDIA GPU is present for NVIDIA settings
  bool hasNvidiaGpu = IsNvidiaGpuPresent();

  // Recursive function to filter settings in a category
  std::function<void(SettingCategory&)> filterCategory =
    [this, &removedCount, hasNvidiaGpu,
     &filterCategory](SettingCategory& category) {
      // Filter settings in this category
      QVector<SettingDefinition> validSettings;

      for (const auto& setting : category.settings) {
        bool isValid = true;

        // Check if this is a NVIDIA setting
        if (setting.id.startsWith("nvidia_") && !hasNvidiaGpu) {
          // Skip NVIDIA settings if no NVIDIA GPU is present
          isValid = false;
          removedCount++;
          continue;
        }

        // Handle other specific setting validations here
        // For now, we just keep most settings

        if (isValid) {
          validSettings.append(setting);
        }
      }

      // Replace settings with filtered list
      category.settings = validSettings;

      // Filter subcategories and remove empty ones
      QVector<SettingCategory> validSubcategories;

      for (auto subCategory : category.subCategories) {
        // Recursively filter this subcategory
        filterCategory(subCategory);

        // Only keep subcategories that have settings or subcategories
        if (!subCategory.settings.isEmpty() ||
            !subCategory.subCategories.isEmpty()) {
          validSubcategories.append(subCategory);
        } else {
          removedCount++;  // Count removed categories
        }
      }

      // Replace subcategories with filtered list
      category.subCategories = validSubcategories;
    };

  // Process all top-level categories
  for (auto& category : categories) {
    filterCategory(category);
  }

  // Remove any empty top-level categories
  auto it = categories.begin();
  while (it != categories.end()) {
    if (it->settings.isEmpty() && it->subCategories.isEmpty()) {
      it = categories.erase(it);
      removedCount++;
    } else {
      ++it;
    }
  }

  return removedCount;
}

QVector<SettingsValidator::ValidationIssue> SettingsValidator::ValidateSetting(
  const SettingDefinition& setting) {
  QVector<ValidationIssue> issues;

  // Check if this is an NVIDIA setting and if NVIDIA GPU is present
  if (setting.id.startsWith("nvidia_") && !IsNvidiaGpuPresent()) {
    issues.append({setting.id,
                   "NVIDIA GPU not detected - this setting may not apply",
                   ValidationIssue::Severity::Warning});
  }

  // Check for potentially problematic settings
  // This would be expanded based on known issues
  if (setting.id == "registry_DisablePagingExecutive" &&
      setting.recommended_value.toBool()) {
    issues.append(
      {setting.id,
       "Disabling paging executive may cause stability issues on some systems",
       ValidationIssue::Severity::Warning});
  }

  // Check if the setting should be disabled based on system capabilities
  auto& optManager = optimizations::OptimizationManager::GetInstance();
  auto* opt = optManager.FindOptimizationById(setting.id.toStdString());

  if (opt && opt->DontEdit()) {
    issues.append({setting.id,
                   "This setting is not recommended for editing on your system",
                   ValidationIssue::Severity::Info});
  }

  return issues;
}

bool SettingsValidator::IsNvidiaGpuPresent() {
  // Check for NVIDIA GPU by initializing the NVIDIA Control Panel
  auto& nvidiaCP = optimizations::nvidia::NvidiaControlPanel::GetInstance();
  bool hasGpu = nvidiaCP.HasNvidiaGPU();
  return hasGpu;
}

QVector<SettingsValidator::ValidationIssue> SettingsValidator::
  CheckHardwareCompatibility(const QString& settingId, const QVariant& value) {
  QVector<ValidationIssue> issues;

  // Get system information from optimization manager
  auto& optManager = optimizations::OptimizationManager::GetInstance();

  // Check for CPU-specific settings
  if (settingId.contains("cpu_", Qt::CaseInsensitive)) {
    // Here we would check specific CPU capabilities
    // For now this is just a placeholder
  }

  // Check for RAM-related settings
  if (settingId.contains("memory_", Qt::CaseInsensitive)) {
    // Here we would check system RAM amount
    // For now this is just a placeholder
  }

  // Check for GPU-specific settings
  if (settingId.startsWith("nvidia_")) {
    if (!IsNvidiaGpuPresent()) {
      issues.append(
        {settingId,
         "No NVIDIA GPU detected - this setting won't have any effect",
         ValidationIssue::Severity::Error});
    }
  }

  return issues;
}

}  // namespace optimize_components
