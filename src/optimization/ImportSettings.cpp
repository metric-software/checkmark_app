#include "ImportSettings.h"

#include <iostream>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "../hardware/ConstantSystemInfo.h"
#include "NvidiaControlPanel.h"
#include "OptimizationEntity.h"
#include "PowerPlanManager.h"
#include "Rust optimization/config_manager.h"
#include "VisualEffectsManager.h"

#include "logging/Logger.h"

namespace optimizations {

ImportResult ImportSettings::ImportSettingsFromFile(
  const std::string& file_path, bool validate_only) {
  ImportResult result;
  result.imported_file_path = QString::fromStdString(file_path);

  try {
    // Check if file exists
    QFile file(QString::fromStdString(file_path));
    if (!file.exists()) {
      result.error_message = "File does not exist: " + file_path;
      return result;
    }

    // Open and read the file
    if (!file.open(QIODevice::ReadOnly)) {
      result.error_message = "Failed to open file for reading: " + file_path;
      return result;
    }

    QByteArray fileData = file.readAll();
    file.close();

    // Parse JSON
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(fileData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
      result.error_message =
        "JSON parse error: " + parseError.errorString().toStdString();
      return result;
    }

    if (!doc.isObject()) {
      result.error_message = "Invalid JSON format: root is not an object";
      return result;
    }

    // Import from the JSON object
    result = ImportSettingsFromJson(doc.object(), validate_only);
    result.imported_file_path = QString::fromStdString(file_path);

    LOG_INFO << "[ImportSettings] Successfully processed file: " << file_path;
    LOG_INFO << "[ImportSettings] Statistics: " << result.imported_settings
             << " imported, " << result.missing_settings << " missing, "
             << result.error_settings << " errors";

  } catch (const std::exception& e) {
    result.error_message = "Exception during import: " + std::string(e.what());
    LOG_ERROR << "[ImportSettings] Error: " << result.error_message;
  }

  return result;
}

ImportResult ImportSettings::ImportSettingsFromJson(const QJsonObject& json_obj,
                                                    bool validate_only) {
  ImportResult result;
  result.success = false;

  try {
    // Validate JSON structure
    if (!ValidateJsonStructure(json_obj)) {
      result.error_message = "Invalid JSON structure for settings import";
      return result;
    }

    // Extract metadata if available
    if (json_obj.contains("metadata")) {
      result.metadata = json_obj["metadata"].toObject();
    }

    // If validation only, return success here
    if (validate_only) {
      result.success = true;
      return result;
    }

    // Import different categories of settings
    if (json_obj.contains("registry")) {
      ImportRegistrySettings(json_obj["registry"].toObject(), result);
    }

    if (json_obj.contains("rust")) {
      ImportRustSettings(json_obj["rust"].toObject(), result);
    }

    if (json_obj.contains("nvidia")) {
      ImportNvidiaSettings(json_obj["nvidia"].toObject(), result);
    }

    if (json_obj.contains("visual_effects")) {
      ImportVisualEffectsSettings(json_obj["visual_effects"].toObject(),
                                  result);
    }

    if (json_obj.contains("power_plan")) {
      ImportPowerPlanSettings(json_obj["power_plan"].toObject(), result);
    }

    result.success = true;

  } catch (const std::exception& e) {
    result.error_message =
      "Exception during JSON import: " + std::string(e.what());
    LOG_ERROR << "[ImportSettings] Error: " << result.error_message;
  }

  return result;
}

QStringList ImportSettings::GetAvailableProfiles(
  const std::string& directory_path) {
  QStringList profiles;

  try {
    QDir directory(QString::fromStdString(directory_path));
    if (!directory.exists()) {
      LOG_WARN << "[ImportSettings] Profile directory does not exist: " << directory_path;
      return profiles;
    }

    // Look for JSON files
    QStringList nameFilters;
    nameFilters << "*.json";

    QFileInfoList fileList =
      directory.entryInfoList(nameFilters, QDir::Files | QDir::Readable);

    for (const QFileInfo& fileInfo : fileList) {
      // Validate that this is actually a settings export file
      if (ValidateProfileFile(fileInfo.absoluteFilePath().toStdString())) {
        profiles.append(fileInfo.absoluteFilePath());
      }
    }

    LOG_INFO << "[ImportSettings] Found " << profiles.size()
              << " valid profile files in " << directory_path;

  } catch (const std::exception& e) {
    LOG_ERROR << "[ImportSettings] Error scanning for profiles: " << e.what();
  }

  return profiles;
}

QJsonObject ImportSettings::GetProfileMetadata(const std::string& file_path) {
  QJsonObject metadata;

  try {
    QFile file(QString::fromStdString(file_path));
    if (!file.open(QIODevice::ReadOnly)) {
      return metadata;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isObject() && doc.object().contains("metadata")) {
      metadata = doc.object()["metadata"].toObject();
    }

  } catch (const std::exception& e) {
    LOG_ERROR << "[ImportSettings] Error reading metadata from " << file_path
              << ": " << e.what();
  }

  return metadata;
}

bool ImportSettings::ValidateProfileFile(const std::string& file_path) {
  try {
    QFile file(QString::fromStdString(file_path));
    if (!file.open(QIODevice::ReadOnly)) {
      return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError) {
      return false;
    }

    if (!doc.isObject()) {
      return false;
    }

    return ValidateJsonStructure(doc.object());

  } catch (const std::exception& e) {
    return false;
  }
}

void ImportSettings::ImportRegistrySettings(const QJsonObject& registry_obj,
                                            ImportResult& result) {
  if (!registry_obj.contains("settings") ||
      !registry_obj["settings"].isArray()) {
    return;
  }

  QJsonArray settings = registry_obj["settings"].toArray();

  for (const auto& setting : settings) {
    QJsonObject settingObj = setting.toObject();

    if (!settingObj.contains("id") || !settingObj.contains("value")) {
      continue;
    }

    QString settingId = settingObj["id"].toString();
    QJsonValue settingValue = settingObj["value"];

    ProcessSingleSetting(settingId, settingValue, "registry", result);
  }
}

void ImportSettings::ImportRustSettings(const QJsonObject& rust_obj,
                                        ImportResult& result) {
  if (!rust_obj.contains("settings") || !rust_obj["settings"].isArray()) {
    return;
  }

  QJsonArray settings = rust_obj["settings"].toArray();

  for (const auto& setting : settings) {
    QJsonObject settingObj = setting.toObject();

    if (!settingObj.contains("id") || !settingObj.contains("value")) {
      continue;
    }

    QString settingId = settingObj["id"].toString();
    QJsonValue settingValue = settingObj["value"];

    ProcessSingleSetting(settingId, settingValue, "rust", result);
  }
}

void ImportSettings::ImportNvidiaSettings(const QJsonObject& nvidia_obj,
                                          ImportResult& result) {
  if (!nvidia_obj.contains("settings") || !nvidia_obj["settings"].isArray()) {
    return;
  }

  QJsonArray settings = nvidia_obj["settings"].toArray();

  for (const auto& setting : settings) {
    QJsonObject settingObj = setting.toObject();

    if (!settingObj.contains("id") || !settingObj.contains("value")) {
      continue;
    }

    QString settingId = settingObj["id"].toString();
    QJsonValue settingValue = settingObj["value"];

    ProcessSingleSetting(settingId, settingValue, "nvidia", result);
  }
}

void ImportSettings::ImportVisualEffectsSettings(const QJsonObject& ve_obj,
                                                 ImportResult& result) {
  if (!ve_obj.contains("profile_id")) {
    return;
  }

  QString settingId = "visual_effects";
  QJsonValue profileValue = ve_obj["profile_id"];

  ProcessSingleSetting(settingId, profileValue, "visual_effects", result);
}

void ImportSettings::ImportPowerPlanSettings(const QJsonObject& pp_obj,
                                             ImportResult& result) {
  if (!pp_obj.contains("guid")) {
    return;
  }

  QString settingId = "power_plan";
  QJsonValue guidValue = pp_obj["guid"];

  ProcessSingleSetting(settingId, guidValue, "power_plan", result);
}

void ImportSettings::ProcessSingleSetting(const QString& setting_id,
                                          const QJsonValue& json_value,
                                          const QString& category,
                                          ImportResult& result) {
  result.total_settings++;

  ImportedSetting importedSetting;
  importedSetting.id = setting_id;
  importedSetting.status = "error";

  try {
    // Check if the setting exists in the current system
    auto& optManager = OptimizationManager::GetInstance();
    auto* optimization =
      optManager.FindOptimizationById(setting_id.toStdString());

    if (!optimization) {
      // Setting doesn't exist in current system
      importedSetting.status = "missing";
      importedSetting.errorMessage = "Setting not found in current system";
      result.missing_settings++;
      result.missing_setting_ids.append(setting_id);
    } else {
      // Convert JSON value to QVariant for UI usage
      if (json_value.isBool()) {
        importedSetting.value = json_value.toBool();
      } else if (json_value.isDouble()) {
        // Handle both integers and doubles
        double doubleVal = json_value.toDouble();
        if (doubleVal == static_cast<int>(doubleVal)) {
          importedSetting.value = static_cast<int>(doubleVal);
        } else {
          importedSetting.value = doubleVal;
        }
      } else if (json_value.isString()) {
        importedSetting.value = json_value.toString();
      } else {
        importedSetting.status = "incompatible";
        importedSetting.errorMessage = "Incompatible value type";
        result.incompatible_settings++;
        result.incompatible_setting_ids.append(setting_id);
      }

      if (importedSetting.status != "incompatible") {
        importedSetting.status = "imported";
        result.imported_settings++;
      }
    }

  } catch (const std::exception& e) {
    importedSetting.status = "error";
    importedSetting.errorMessage = QString::fromStdString(e.what());
    result.error_settings++;
    result.error_setting_ids.append(setting_id);
  }

  // Add to the appropriate category in the result
  result.imported_values[category].append(importedSetting);
}

OptimizationValue ImportSettings::JsonToOptimizationValue(
  const QJsonValue& json_value) {
  if (json_value.isBool()) {
    return json_value.toBool();
  } else if (json_value.isDouble()) {
    double doubleVal = json_value.toDouble();
    if (doubleVal == static_cast<int>(doubleVal)) {
      return static_cast<int>(doubleVal);
    } else {
      return doubleVal;
    }
  } else if (json_value.isString()) {
    return json_value.toString().toStdString();
  }

  // Default fallback
  return std::string("");
}

QVariant ImportSettings::OptimizationValueToQVariant(
  const OptimizationValue& opt_value) {
  if (std::holds_alternative<bool>(opt_value)) {
    return std::get<bool>(opt_value);
  } else if (std::holds_alternative<int>(opt_value)) {
    return std::get<int>(opt_value);
  } else if (std::holds_alternative<double>(opt_value)) {
    return std::get<double>(opt_value);
  } else if (std::holds_alternative<std::string>(opt_value)) {
    return QString::fromStdString(std::get<std::string>(opt_value));
  }

  return QVariant();
}

bool ImportSettings::ValidateJsonStructure(const QJsonObject& json_obj) {
  // Check for required fields that indicate this is an exported settings file

  // Must have at least one category of settings
  bool hasAnyCategory =
    json_obj.contains("registry") || json_obj.contains("rust") ||
    json_obj.contains("nvidia") || json_obj.contains("visual_effects") ||
    json_obj.contains("power_plan");

  if (!hasAnyCategory) {
    return false;
  }

  // Check if it has the stats object (indicates it was created by
  // ExportSettings)
  if (json_obj.contains("stats")) {
    QJsonObject stats = json_obj["stats"].toObject();
    if (stats.contains("total") && stats.contains("exported")) {
      return true;  // This looks like our export format
    }
  }

  // If no stats, check for at least valid category structure
  for (const QString& category : {"registry", "rust", "nvidia"}) {
    if (json_obj.contains(category)) {
      QJsonObject categoryObj = json_obj[category].toObject();
      if (categoryObj.contains("settings") &&
          categoryObj["settings"].isArray()) {
        return true;  // Valid category structure found
      }
    }
  }

  // Check for single-value categories
  if (json_obj.contains("visual_effects")) {
    QJsonObject veObj = json_obj["visual_effects"].toObject();
    if (veObj.contains("profile_id")) {
      return true;
    }
  }

  if (json_obj.contains("power_plan")) {
    QJsonObject ppObj = json_obj["power_plan"].toObject();
    if (ppObj.contains("guid")) {
      return true;
    }
  }

  return false;
}

}  // namespace optimizations
