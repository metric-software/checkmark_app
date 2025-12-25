/**
 * @file NvidiaOptimization.cpp
 * @brief Implementation for NVIDIA driver settings optimizations
 */

#include "optimization/NvidiaOptimization.h"

#include <iostream>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include "optimization/BackupManager.h"
#include "optimization/NvidiaControlPanel.h"

namespace optimizations {
namespace nvidia {

bool NvidiaOptimization::Apply(const OptimizationValue& value) {
  // Convert the value to int
  if (!std::holds_alternative<int>(value)) {
    return false;
  }

  int int_value = std::get<int>(value);

  // Get the NvidiaControlPanel singleton
  auto& nvcp = NvidiaControlPanel::GetInstance();

  // Get the setting ID
  std::string setting_id = GetId();

  const bool isKnownSetting =
    setting_id == "nvidia_vsync" || setting_id == "nvidia_power_mode" ||
    setting_id == "nvidia_aniso_filtering" || setting_id == "nvidia_antialiasing" ||
    setting_id == "nvidia_monitor_tech" || setting_id == "nvidia_gdi_compat" ||
    setting_id == "nvidia_refresh_rate" || setting_id == "nvidia_texture_quality" ||
    setting_id == "nvidia_aniso_sample_opt" || setting_id == "nvidia_threaded_opt";
  if (!isKnownSetting) {
    return false;
  }

  // Backups are a hard precondition for NVIDIA settings writes.
  auto& backupManager = BackupManager::GetInstance();
  if (!backupManager.Initialize()) {
    return false;
  }

  if (backupManager.CheckBackupStatus(BackupType::NvidiaSettings, true) !=
      BackupStatus::CompleteBackup) {
    if (!backupManager.CreateBackup(BackupType::NvidiaSettings, true) ||
        backupManager.CheckBackupStatus(BackupType::NvidiaSettings, true) !=
          BackupStatus::CompleteBackup) {
      return false;
    }
  }

  if (backupManager.CheckBackupStatus(BackupType::NvidiaSettings, false) !=
      BackupStatus::CompleteBackup) {
    if (!backupManager.CreateBackup(BackupType::NvidiaSettings, false) ||
        backupManager.CheckBackupStatus(BackupType::NvidiaSettings, false) !=
          BackupStatus::CompleteBackup) {
      return false;
    }
  }

  // Apply the setting based on ID
  bool success = false;

  if (setting_id == "nvidia_vsync") {
    success = nvcp.ApplyVSyncSetting(int_value);
  } else if (setting_id == "nvidia_power_mode") {
    success = nvcp.ApplyPowerManagementMode(int_value);
  } else if (setting_id == "nvidia_aniso_filtering") {
    success = nvcp.SetAnisotropicFiltering(int_value == 1);
  } else if (setting_id == "nvidia_antialiasing") {
    success = nvcp.SetAntialiasing(int_value == 1);
  } else if (setting_id == "nvidia_monitor_tech") {
    success = nvcp.ApplyMonitorTechnology(int_value);
  } else if (setting_id == "nvidia_gdi_compat") {
    success = nvcp.ApplyGDICompatibility(int_value);
  } else if (setting_id == "nvidia_refresh_rate") {
    success = nvcp.ApplyPreferredRefreshRate(int_value);
  } else if (setting_id == "nvidia_texture_quality") {
    success = nvcp.ApplyTextureFilteringQuality(int_value);
  } else if (setting_id == "nvidia_aniso_sample_opt") {
    success = nvcp.ApplyAnisoSampleOpt(int_value);
  } else if (setting_id == "nvidia_threaded_opt") {
    success = nvcp.ApplyThreadedOptimization(int_value);
  } else {
    return false;
  }

  if (success) {
    // Update the current value
    current_value = int_value;
  }

  return success;
}

bool NvidiaOptimization::Revert() {
  // Restore to default value
  return Apply(default_value);
}

const std::vector<settings::ValueOption>& NvidiaOptimization::GetPossibleValues()
  const {
  // Convert the map to a vector of ValueOption objects on demand
  static std::vector<settings::ValueOption> possible_values;
  possible_values.clear();

  for (const auto& [value, description] : value_options) {
    settings::ValueOption option;
    option.value = value;
    option.description = description;
    possible_values.push_back(option);
  }

  return possible_values;
}

bool NvidiaOptimization::IsNvidiaGPUPresent() {
  return NvidiaControlPanel::GetInstance().HasNvidiaGPU();
}

// Implementation for ConfigurableNvidiaOptimization
ConfigurableNvidiaOptimization::ConfigurableNvidiaOptimization(
  const QJsonObject& config)
    : NvidiaOptimization(
        config["id"].toString().toStdString(),
        config["name"].toString().toStdString(),
        config["description"].toString().toStdString(),
        settings::GetVariantValueOrDefault<int>(
          settings::ParseOptimizationValue(config["current_value"]), 0),
        settings::GetVariantValueOrDefault<int>(
          settings::ParseOptimizationValue(config["recommended_value"]), 0),
        settings::GetVariantValueOrDefault<int>(
          settings::ParseOptimizationValue(config["default_value"]), 0),
        config["category"].toString().toStdString(),
        config["personal_preference"].toBool()) {
  try {
    // Load additional metadata
    if (config.contains("is_advanced")) {
      is_advanced_ = config["is_advanced"].toBool();
    }

    // Parse possible values array if present
    if (config.contains("possible_values") &&
        config["possible_values"].isArray()) {
      const QJsonArray possibleValues = config["possible_values"].toArray();
      for (const QJsonValue& valueJson : possibleValues) {
        if (valueJson.isObject()) {
          QJsonObject valueObj = valueJson.toObject();
          if (valueObj.contains("value") && valueObj.contains("description")) {
            settings::ValueOption option;
            option.value = settings::ParseOptimizationValue(valueObj["value"]);
            option.description =
              valueObj["description"].toString().toStdString();
            possible_values_.push_back(option);
          }
        }
      }
    }
  } catch (const std::exception& e) {
    // std::cout << "ConfigurableNvidiaOptimization: Exception reading metadata
    // for [" << GetId() << "]: "
    //           << e.what() << std::endl;
  }
}

QJsonObject ConfigurableNvidiaOptimization::ToJson() const {
  QJsonObject j;
  j["id"] = QString::fromStdString(GetId());
  j["name"] = QString::fromStdString(GetName());
  j["description"] = QString::fromStdString(GetDescription());
  j["current_value"] = settings::SerializeOptimizationValue(GetCurrentValue());
  j["recommended_value"] =
    settings::SerializeOptimizationValue(GetRecommendedValue());
  j["default_value"] = settings::SerializeOptimizationValue(GetDefaultValue());
  j["category"] = QString::fromStdString(GetCategory());
  j["is_advanced"] = is_advanced_;
  j["personal_preference"] = IsPersonalPreference();
  j["type"] = "nvidia";  // For identification when loading

  // Serialize possible values if available
  if (!possible_values_.empty()) {
    QJsonArray possibleValuesArray;
    for (const auto& option : possible_values_) {
      QJsonObject optionObj;
      optionObj["value"] = settings::SerializeOptimizationValue(option.value);
      optionObj["description"] = QString::fromStdString(option.description);
      possibleValuesArray.append(optionObj);
    }
    j["possible_values"] = possibleValuesArray;
  }

  return j;
}

}  // namespace nvidia
}  // namespace optimizations
