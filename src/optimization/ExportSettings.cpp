#include "ExportSettings.h"

#include <iostream>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "../ApplicationSettings.h"
#include "../hardware/ConstantSystemInfo.h"
#include "NvidiaControlPanel.h"
#include "OptimizationEntity.h"
#include "PowerPlanManager.h"
#include "Rust optimization/config_manager.h"
#include "VisualEffectsManager.h"

#include "logging/Logger.h"

namespace optimizations {

ExportResult ExportSettings::ExportAllSettings(const std::string& file_path,
                                               bool include_metadata) {
  ExportResult result;
  result.exported_file_path = QString::fromStdString(file_path);

  try {
    // Get the JSON object with all settings
    QJsonObject exportObj = ExportAllSettingsToJson(include_metadata);

    // Create the directory if it doesn't exist
    QFileInfo fileInfo(QString::fromStdString(file_path));
    QDir().mkpath(fileInfo.absolutePath());

    // Write to file
    QFile file(QString::fromStdString(file_path));
    if (!file.open(QIODevice::WriteOnly)) {
      result.error_message = "Failed to open file for writing: " + file_path;
      return result;
    }

    QJsonDocument doc(exportObj);
    file.write(doc.toJson());
    file.close();

    // Extract statistics from the export object
    if (exportObj.contains("stats")) {
      QJsonObject stats = exportObj["stats"].toObject();
      result.total_settings = stats["total"].toInt();
      result.exported_settings = stats["exported"].toInt();
      result.missing_settings = stats["missing"].toInt();
      result.error_settings = stats["errors"].toInt();
    }

    result.success = true;
    LOG_INFO << "[ExportSettings] Successfully exported "
              << result.exported_settings << " settings to: " << file_path;

  } catch (const std::exception& e) {
    result.error_message = "Exception during export: " + std::string(e.what());
    LOG_ERROR << "[ExportSettings] Error: " << result.error_message;
  }

  return result;
}

QJsonObject ExportSettings::ExportAllSettingsToJson(bool include_metadata) {
  QJsonObject exportObj;

  // Export metadata if requested
  if (include_metadata) {
    exportObj["metadata"] = GetSystemMetadata();
  }

  // Export all setting categories
  exportObj["registry"] = ExportRegistrySettings();
  exportObj["rust"] = ExportRustSettings();
  exportObj["nvidia"] = ExportNvidiaSettings();
  exportObj["visual_effects"] = ExportVisualEffectsSettings();
  exportObj["power_plan"] = ExportPowerPlanSettings();

  // Calculate and add statistics
  QJsonObject stats;
  int total = 0, exported = 0, missing = 0, errors = 0;

  // Count statistics from each category with settings arrays
  auto countStats = [&](const QJsonObject& categoryObj) {
    if (categoryObj.contains("settings") && categoryObj["settings"].isArray()) {
      QJsonArray settings = categoryObj["settings"].toArray();
      for (const auto& setting : settings) {
        total++;
        QJsonObject settingObj = setting.toObject();
        if (settingObj.contains("status")) {
          QString status = settingObj["status"].toString();
          if (status == "ok")
            exported++;
          else if (status == "missing")
            missing++;
          else if (status == "error")
            errors++;
        }
      }
    }
  };

  countStats(exportObj["registry"].toObject());
  countStats(exportObj["rust"].toObject());
  countStats(exportObj["nvidia"].toObject());

  // Handle single-value exports (visual effects, power plan)
  auto countSingleStat = [&](const QJsonObject& singleObj) {
    if (singleObj.contains("status")) {
      total++;
      QString status = singleObj["status"].toString();
      if (status == "ok")
        exported++;
      else if (status == "missing")
        missing++;
      else if (status == "error")
        errors++;
    }
  };

  countSingleStat(exportObj["visual_effects"].toObject());
  countSingleStat(exportObj["power_plan"].toObject());

  stats["total"] = total;
  stats["exported"] = exported;
  stats["missing"] = missing;
  stats["errors"] = errors;
  exportObj["stats"] = stats;

  return exportObj;
}

QJsonObject ExportSettings::ExportRegistrySettings() {
  QJsonObject registryObj;
  registryObj["category"] = "registry";
  registryObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

  QJsonArray settingsArray;

  try {
    auto& optManager = OptimizationManager::GetInstance();
    auto registryOptimizations =
      optManager.GetOptimizationsByType(OptimizationType::WindowsRegistry);

    for (const auto* optimization : registryOptimizations) {
      QJsonObject settingObj;
      settingObj["id"] = QString::fromStdString(optimization->GetId());

      // Try to get the current value
      try {
        OptimizationValue currentValue = optimization->GetCurrentValue();
        settingObj["value"] = OptimizationValueToJson(currentValue);
        settingObj["status"] = optimization->IsMissing() ? "missing" : "ok";

        // Add registry-specific information
        if (auto regOpt = dynamic_cast<const settings::RegistryOptimization*>(
              optimization)) {
          settingObj["key"] = QString::fromStdString(regOpt->GetRegistryKey());
          settingObj["name"] =
            QString::fromStdString(regOpt->GetRegistryValueName());
        }

      } catch (const std::exception& e) {
        settingObj["value"] = QJsonValue();
        settingObj["status"] = "error";
      }

      settingsArray.append(settingObj);
    }

  } catch (const std::exception& e) {
    LOG_ERROR << "[ExportSettings] Error exporting registry settings: " << e.what();
  }

  registryObj["settings"] = settingsArray;
  return registryObj;
}

QJsonObject ExportSettings::ExportRustSettings() {
  QJsonObject rustObj;
  rustObj["category"] = "rust";
  rustObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

  QJsonArray settingsArray;

  try {
    auto& rustManager = optimizations::rust::RustConfigManager::GetInstance();

    // Get all Rust optimizations
    auto& optManager = OptimizationManager::GetInstance();
    auto rustOptimizations = optManager.GetOptimizationsByType(
      OptimizationType::SettingGroup);  // Assuming Rust configs are in
                                        // SettingGroup

    for (const auto* optimization : rustOptimizations) {
      // Check if this is actually a Rust optimization by name/id pattern
      if (optimization->GetId().find("rust") == std::string::npos &&
          optimization->GetName().find("Rust") == std::string::npos) {
        continue;
      }

      QJsonObject settingObj;
      settingObj["id"] = QString::fromStdString(optimization->GetId());

      try {
        OptimizationValue currentValue = optimization->GetCurrentValue();
        settingObj["value"] = OptimizationValueToJson(currentValue);
        settingObj["status"] = optimization->IsMissing() ? "missing" : "ok";

      } catch (const std::exception& e) {
        settingObj["value"] = QJsonValue();
        settingObj["status"] = "error";
      }

      settingsArray.append(settingObj);
    }

  } catch (const std::exception& e) {
    LOG_ERROR << "[ExportSettings] Error exporting Rust settings: " << e.what();
  }

  rustObj["settings"] = settingsArray;
  return rustObj;
}

QJsonObject ExportSettings::ExportNvidiaSettings() {
  QJsonObject nvidiaObj;
  nvidiaObj["category"] = "nvidia";
  nvidiaObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

  QJsonArray settingsArray;

  try {
    auto& optManager = OptimizationManager::GetInstance();
    auto nvidiaOptimizations =
      optManager.GetOptimizationsByType(OptimizationType::NvidiaSettings);

    for (const auto* optimization : nvidiaOptimizations) {
      QJsonObject settingObj;
      settingObj["id"] = QString::fromStdString(optimization->GetId());

      try {
        OptimizationValue currentValue = optimization->GetCurrentValue();
        settingObj["value"] = OptimizationValueToJson(currentValue);
        settingObj["status"] = optimization->IsMissing() ? "missing" : "ok";

      } catch (const std::exception& e) {
        settingObj["value"] = QJsonValue();
        settingObj["status"] = "error";
      }

      settingsArray.append(settingObj);
    }

  } catch (const std::exception& e) {
    LOG_ERROR << "[ExportSettings] Error exporting NVIDIA settings: " << e.what();
  }

  nvidiaObj["settings"] = settingsArray;
  return nvidiaObj;
}

QJsonObject ExportSettings::ExportVisualEffectsSettings() {
  QJsonObject veObj;
  veObj["category"] = "visual_effects";
  veObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

  try {
    auto& veManager =
      optimizations::visual_effects::VisualEffectsManager::GetInstance();
    auto currentProfile = veManager.GetCurrentProfile();

    veObj["profile_id"] = static_cast<int>(currentProfile);
    veObj["status"] = "ok";

  } catch (const std::exception& e) {
    veObj["profile_id"] = QJsonValue();
    veObj["status"] = "error";
    LOG_ERROR << "[ExportSettings] Error exporting Visual Effects settings: "
              << e.what();
  }

  return veObj;
}

QJsonObject ExportSettings::ExportPowerPlanSettings() {
  QJsonObject ppObj;
  ppObj["category"] = "power_plan";
  ppObj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

  try {
    auto& ppManager = optimizations::power::PowerPlanManager::GetInstance();
    auto currentPlanGuid = ppManager.GetCurrentPowerPlan();

    // Convert wstring to string
    std::string currentPlanGuidStr(currentPlanGuid.begin(),
                                   currentPlanGuid.end());

    ppObj["guid"] = QString::fromStdString(currentPlanGuidStr);
    ppObj["status"] = "ok";

  } catch (const std::exception& e) {
    ppObj["guid"] = QJsonValue();
    ppObj["status"] = "error";
    LOG_ERROR << "[ExportSettings] Error exporting Power Plan settings: " << e.what();
  }

  return ppObj;
}

QJsonObject ExportSettings::GetSystemMetadata() {
  QJsonObject metadata;

  metadata["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
  metadata["version"] = "1.0";

  try {
    // Get system information if available
    const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();

    metadata["cpu"] = QString::fromStdString(constantInfo.cpuName);

    // GPU: use first GPU device if available
    std::string gpuName = "no_data";
    if (!constantInfo.gpuDevices.empty()) {
      gpuName = constantInfo.gpuDevices[0].name;
    }
    metadata["gpu"] = QString::fromStdString(gpuName);

    metadata["ram_gb"] = static_cast<double>(constantInfo.totalPhysicalMemoryMB) / 1024.0;
    metadata["os"] = QString::fromStdString(constantInfo.osVersion);

    // Resolution: use first monitor if available
    std::string resolution = "no_data";
    if (!constantInfo.monitors.empty() && constantInfo.monitors[0].width > 0 && constantInfo.monitors[0].height > 0) {
      resolution = std::to_string(constantInfo.monitors[0].width) + "x" + std::to_string(constantInfo.monitors[0].height);
    }
    metadata["resolution"] = QString::fromStdString(resolution);

  } catch (const std::exception& e) {
    LOG_WARN << "[ExportSettings] Warning: Could not gather system info: " << e.what();
  }

  return metadata;
}

QJsonValue ExportSettings::SafeGetCurrentValue(
  const settings::OptimizationEntity* entity) {
  if (!entity) {
    return QJsonValue();
  }

  try {
    OptimizationValue value = entity->GetCurrentValue();
    return OptimizationValueToJson(value);
  } catch (const std::exception& e) {
    LOG_ERROR << "[ExportSettings] Error getting current value for "
              << entity->GetId() << ": " << e.what();
    return QJsonValue();
  }
}

QJsonValue ExportSettings::OptimizationValueToJson(
  const OptimizationValue& value) {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value);
  } else if (std::holds_alternative<int>(value)) {
    return std::get<int>(value);
  } else if (std::holds_alternative<double>(value)) {
    return std::get<double>(value);
  } else if (std::holds_alternative<std::string>(value)) {
    return QString::fromStdString(std::get<std::string>(value));
  }

  return QJsonValue();
}

}  // namespace optimizations
