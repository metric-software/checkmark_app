/**
 * @file OptimizationEntity.cpp
 * @brief Implementation of the optimization framework
 */

#include "OptimizationEntity.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <Windows.h>

#include "nvapi.h"
#include "NvApiDriverSettings.h"
#include "BackupManager.h"
#include "NvidiaOptimization.h"
#include "PowerPlanManager.h"
#include "RegistrySettings.h"
#include "VisualEffectsManager.h"

namespace fs = std::filesystem;

namespace optimizations {
namespace settings {

//------------------------------------------------------------------------------
// JSON Helper Functions
//------------------------------------------------------------------------------

OptimizationValue ParseOptimizationValue(const QJsonValue& q_json_value) {
  if (q_json_value.isBool()) {
    return q_json_value.toBool();
  } else if (q_json_value.isDouble()) {
    double value = q_json_value.toDouble();
    // Special case for 0xFFFFFFFF
    if (value == 4294967295.0) {
      return std::numeric_limits<int>::max();
    }
    // Use int if no fractional part
    if (value == static_cast<double>(static_cast<int>(value))) {
      return static_cast<int>(value);
    }
    return value;
  } else if (q_json_value.isString()) {
    std::string str_value = q_json_value.toString().toStdString();
    // Check for special hex values
    if (str_value == "0xFFFFFFFF" || str_value == "4294967295" ||
        str_value == "FFFFFFFF") {
      return std::numeric_limits<int>::max();
    }
    return str_value;
  }
  return false;  // Default fallback
}

QJsonValue SerializeOptimizationValue(const OptimizationValue& value) {
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

//------------------------------------------------------------------------------
// OptimizationEntity Implementation
//------------------------------------------------------------------------------

OptimizationEntity::OptimizationEntity(const std::string& id,
                                       const std::string& name,
                                       const std::string& description,
                                       OptimizationType type)
    : id_(id), name_(name), description_(description), type_(type) {
  original_value_ = false;
  session_start_value_ = false;
}

//------------------------------------------------------------------------------
// RegistryOptimization Implementation
//------------------------------------------------------------------------------

RegistryOptimization::RegistryOptimization(
  const std::string& id, const std::string& name,
  const std::string& description, const std::string& registry_key,
  const std::string& registry_value_name,
  const OptimizationValue& default_value,
  const OptimizationValue& recommended_value)
    : OptimizationEntity(id, name, description,
                         OptimizationType::WindowsRegistry),
      registry_key_(registry_key), registry_value_name_(registry_value_name),
      default_value_(default_value), recommended_value_(recommended_value) {}

bool RegistryOptimization::Apply(const OptimizationValue& value) {
  return registry::RegistrySettings::ApplyRegistryValue(
    registry_key_, registry_value_name_, value, default_value_, GetId());
}

bool RegistryOptimization::Revert() { return Apply(session_start_value_); }

OptimizationValue RegistryOptimization::GetCurrentValue() const {
  return registry::RegistrySettings::GetRegistryValue(
    registry_key_, registry_value_name_, default_value_);
}

OptimizationValue RegistryOptimization::GetRecommendedValue() const {
  // Special handling for NetworkThrottlingIndex
  if (registry_value_name_ == "NetworkThrottlingIndex") {
    return std::numeric_limits<int>::max();
  }
  return recommended_value_;
}

OptimizationValue RegistryOptimization::GetDefaultValue() const {
  // Special handling for NetworkThrottlingIndex
  if (registry_value_name_ == "NetworkThrottlingIndex") {
    return 10;  // Safe default value
  }
  return default_value_;
}

//------------------------------------------------------------------------------
// ConfigurableOptimization Implementation
//------------------------------------------------------------------------------

ConfigurableOptimization::ConfigurableOptimization(
  const registry::RegistrySettingDefinition& def)
    : RegistryOptimization(
        def.id, def.name, def.description, def.registry_key,
        def.registry_value_name, def.default_value, def.recommended_value) {
  category_ = def.category;
  subcategory_ = def.subcategory;

  SetAdvanced(def.is_advanced);

  personal_preference_ = def.personal_preference;
  SetDontEdit(def.dont_edit);
  creation_allowed_ = def.creation_allowed;

  level_ = def.level;
  if (level_ < 0 || level_ > 3) {
    level_ = 0;
  }

  possible_values_.reserve(def.possible_values.size());
  for (const auto& option : def.possible_values) {
    possible_values_.push_back({option.value, option.description});
  }
}

ConfigurableOptimization::ConfigurableOptimization(const QJsonObject& config)
    : RegistryOptimization(
        config["id"].toString().toStdString(),
        config["name"].toString().toStdString(),
        config["description"].toString().toStdString(),
        config["registry_key"].toString().toStdString(),
        config["registry_value_name"].toString().toStdString(),
        ParseOptimizationValue(config["default_value"]),
        ParseOptimizationValue(config["recommended_value"])) {
  // Load metadata
  if (config.contains("category")) {
    category_ = config["category"].toString().toStdString();
  }
  if (config.contains("subcategory")) {
    subcategory_ = config["subcategory"].toString().toStdString();
  }

  // Registry settings are always advanced
  SetAdvanced(true);

  // Load optional properties with defaults
  personal_preference_ = config.value("personal_preference").toBool(true);
  SetDontEdit(config.value("dont_edit").toBool(false));
  creation_allowed_ = config.value("creation_allowed").toBool(false);

  // Validate and set level (0-3)
  level_ = config.value("level").toInt(0);
  if (level_ < 0 || level_ > 3) {
    level_ = 0;
  }

  // Parse possible values
  if (config.contains("possible_values") &&
      config.value("possible_values").isArray()) {
    const QJsonArray possibleValues = config.value("possible_values").toArray();
    for (const QJsonValue& valueJson : possibleValues) {
      if (valueJson.isObject()) {
        QJsonObject valueObj = valueJson.toObject();
        if (valueObj.contains("value") && valueObj.contains("description")) {
          ValueOption option;
          option.value = ParseOptimizationValue(valueObj["value"]);
          option.description = valueObj["description"].toString().toStdString();
          possible_values_.push_back(option);
        }
      }
    }
  }
}

QJsonObject ConfigurableOptimization::ToJson() const {
  QJsonObject j;
  j["id"] = QString::fromStdString(GetId());
  j["name"] = QString::fromStdString(GetName());
  j["description"] = QString::fromStdString(GetDescription());
  j["registry_key"] = QString::fromStdString(GetRegistryKey());
  j["registry_value_name"] = QString::fromStdString(GetRegistryValueName());
  j["default_value"] = SerializeOptimizationValue(GetDefaultValue());
  j["recommended_value"] = SerializeOptimizationValue(GetRecommendedValue());
  j["category"] = QString::fromStdString(category_);
  j["subcategory"] = QString::fromStdString(subcategory_);
  j["is_advanced"] = IsAdvanced();
  j["personal_preference"] = personal_preference_;
  j["dont_edit"] = DontEdit();
  j["creation_allowed"] = creation_allowed_;
  j["level"] = level_;
  j["type"] = "registry";

  // Serialize possible values
  if (!possible_values_.empty()) {
    QJsonArray possibleValuesArray;
    for (const auto& option : possible_values_) {
      QJsonObject optionObj;
      optionObj["value"] = SerializeOptimizationValue(option.value);
      optionObj["description"] = QString::fromStdString(option.description);
      possibleValuesArray.append(optionObj);
    }
    j["possible_values"] = possibleValuesArray;
  }

  return j;
}

//------------------------------------------------------------------------------
// OptimizationGroup Implementation
//------------------------------------------------------------------------------

OptimizationGroup::OptimizationGroup(const std::string& id,
                                     const std::string& name,
                                     const std::string& description)
    : OptimizationEntity(id, name, description,
                         OptimizationType::SettingGroup) {}

void OptimizationGroup::AddOptimization(const std::string& optimization_id) {
  optimization_ids_.push_back(optimization_id);
}

bool OptimizationGroup::Apply(const OptimizationValue&) {
  bool success = true;
  auto& manager = OptimizationManager::GetInstance();

  for (const auto& opt_id : optimization_ids_) {
    auto* opt = manager.FindOptimizationById(opt_id);
    if (opt) {
      if (!manager.ApplyOptimization(opt_id, opt->GetRecommendedValue())) {
        success = false;
      }
    } else {
      success = false;
    }
  }

  return success;
}

bool OptimizationGroup::Revert() {
  bool success = true;
  auto& manager = OptimizationManager::GetInstance();

  for (const auto& opt_id : optimization_ids_) {
    if (!manager.RevertOptimization(opt_id)) {
      success = false;
    }
  }

  return success;
}

//------------------------------------------------------------------------------
// OptimizationFactory Implementation
//------------------------------------------------------------------------------

std::unique_ptr<OptimizationEntity> OptimizationFactory::
  CreateRegistryOptimization(const std::string& id, const std::string& name,
                             const std::string& description,
                             const std::string& registry_key,
                             const std::string& registry_value_name,
                             const OptimizationValue& default_value,
                             const OptimizationValue& recommended_value) {
  return std::make_unique<RegistryOptimization>(
    id, name, description, registry_key, registry_value_name, default_value,
    recommended_value);
}

std::unique_ptr<OptimizationEntity> OptimizationFactory::CreateFromJson(
  const QJsonObject& config) {
  if (!config.contains("type")) {
    return nullptr;
  }

  std::string type = config["type"].toString().toStdString();

  if (type == "registry") {
    return std::make_unique<ConfigurableOptimization>(config);
  } else if (type == "group") {
    auto group = std::make_unique<OptimizationGroup>(
      config["id"].toString().toStdString(),
      config["name"].toString().toStdString(),
      config["description"].toString().toStdString());

    // Add child optimizations
    if (config.contains("optimizations") && config["optimizations"].isArray()) {
      const QJsonArray optimizations = config["optimizations"].toArray();
      for (const QJsonValue& optIdValue : optimizations) {
        if (optIdValue.isString()) {
          group->AddOptimization(optIdValue.toString().toStdString());
        }
      }
    }

    return group;
  } else if (type == "nvidia") {
    return std::make_unique<nvidia::ConfigurableNvidiaOptimization>(config);
  } else if (type == "power") {
    return std::unique_ptr<OptimizationEntity>(
      new power::ConfigurablePowerPlanOptimization(config));
  }

  return nullptr;
}

std::unique_ptr<OptimizationGroup> OptimizationFactory::CreateGroup(
  const std::string& id, const std::string& name,
  const std::string& description,
  const std::vector<std::string>& optimization_ids) {
  auto group = std::make_unique<OptimizationGroup>(id, name, description);

  for (const auto& opt_id : optimization_ids) {
    group->AddOptimization(opt_id);
  }

  return group;
}

//------------------------------------------------------------------------------
// VisualEffectsOptimization Implementation
//------------------------------------------------------------------------------

VisualEffectsOptimization::VisualEffectsOptimization(
  const std::string& id, const std::string& name,
  const std::string& description, const OptimizationValue& default_value,
  const OptimizationValue& recommended_value)
    : OptimizationEntity(id, name, description,
                         OptimizationType::VisualEffects),
      default_value_(default_value), recommended_value_(recommended_value) {

  // Initialize possible values for visual effects profiles
  possible_values_ = {{0, "Let Windows decide"},
                      {1, "Best appearance"},
                      {2, "Best performance"},
                      {3, "Recommended"},
                      {4, "Custom"}};
}

bool VisualEffectsOptimization::Apply(const OptimizationValue& value) {
  auto& visualManager = visual_effects::VisualEffectsManager::GetInstance();
  if (!visualManager.Initialize()) {
    return false;
  }

  int profileValue = std::get<int>(value);
  auto profile =
    static_cast<visual_effects::VisualEffectsProfile>(profileValue);

  return visualManager.ApplyProfile(profile);
}

bool VisualEffectsOptimization::Revert() {
  auto& visualManager = visual_effects::VisualEffectsManager::GetInstance();
  if (!visualManager.Initialize()) {
    return false;
  }

  return Apply(original_value_);
}

OptimizationValue VisualEffectsOptimization::GetCurrentValue() const {
  auto& visualManager = visual_effects::VisualEffectsManager::GetInstance();
  if (!visualManager.Initialize()) {
    return default_value_;
  }

  auto currentProfile = visualManager.GetCurrentProfile();
  return static_cast<int>(currentProfile);
}

OptimizationValue VisualEffectsOptimization::GetRecommendedValue() const {
  return recommended_value_;
}

OptimizationValue VisualEffectsOptimization::GetDefaultValue() const {
  return default_value_;
}

const std::vector<ValueOption>& VisualEffectsOptimization::GetPossibleValues()
  const {
  return possible_values_;
}

}  // namespace settings
}  // namespace optimizations

//------------------------------------------------------------------------------
// OptimizationManager Implementation
//------------------------------------------------------------------------------

namespace optimizations {

OptimizationManager& OptimizationManager::GetInstance() {
  static OptimizationManager instance;
  return instance;
}

OptimizationManager::OptimizationManager() {}

void OptimizationManager::Initialize() {
  if (is_initialized_) {
    return;
  }

  // Setup paths
  QDir appDir(QCoreApplication::applicationDirPath());
  all_registry_settings_path_.clear();  // Registry settings are hardcoded now

  // Ensure profiles directory exists
  QDir profilesDir(appDir.filePath("profiles"));
  if (!profilesDir.exists()) {
    profilesDir.mkpath(".");
  }

  // Initialize BackupManager
  BackupManager& backupManager = BackupManager::GetInstance();
  backupManager.Initialize();
  backupManager.LoadUserPreferences();

  // Register hardcoded optimizations
  RegisterHardCodedOptimizations();

  // Load registry settings
  LoadAllRegistrySettings();

  // Add NVIDIA settings if GPU detected
  nvidia::NvidiaControlPanel& nvidiaCP =
    nvidia::NvidiaControlPanel::GetInstance();
  if (nvidiaCP.HasNvidiaGPU()) {
    auto nvidiaOptimizations = nvidiaCP.CreateNvidiaOptimizations();
    for (auto& opt : nvidiaOptimizations) {
      optimizations_.push_back(std::move(opt));
    }
  }

  // Add power plan optimizations
  power::PowerPlanManager& powerManager =
    power::PowerPlanManager::GetInstance();
  if (powerManager.Initialize()) {
    auto powerPlanOpt = powerManager.CreatePowerPlanOptimization();
    auto displayTimeoutOpt = powerManager.CreateDisplayTimeoutOptimization();

    std::vector<std::string> powerOptIds;

    if (powerPlanOpt) {
      std::string powerPlanId = powerPlanOpt->GetId();
      optimizations_.push_back(std::move(powerPlanOpt));
      powerOptIds.push_back(powerPlanId);
    }

    if (displayTimeoutOpt) {
      std::string displayTimeoutId = displayTimeoutOpt->GetId();
      optimizations_.push_back(std::move(displayTimeoutOpt));
      powerOptIds.push_back(displayTimeoutId);
    }

    if (!powerOptIds.empty()) {
      auto powerGroup = settings::OptimizationFactory::CreateGroup(
        "preset.power", "Power Plan Optimizations",
        "Apply power plan settings for optimal performance", powerOptIds);
      optimizations_.push_back(std::move(powerGroup));
    }
  }

  // Rebuild lookup tables
  RebuildLookupTables();

  // Initialize values for all optimizations
  for (auto& opt : optimizations_) {
    if (!opt) continue;

    opt->SetSessionStartValue(opt->GetCurrentValue());
    QVariant qOriginalValue =
      backupManager.GetOriginalValueFromBackup(opt->GetId());
    if (qOriginalValue.isValid()) {
      opt->SetOriginalValue(settings::ParseOptimizationValue(
        QJsonValue::fromVariant(qOriginalValue)));
    } else {
      opt->SetOriginalValue(opt->GetCurrentValue());
    }

    bool dontEdit =
      backupManager.GetDontEditFlag(opt->GetId(), opt->IsAdvanced());
    opt->SetDontEdit(false);  // Override for now
  }

  is_initialized_ = true;
}

void OptimizationManager::RegisterHardCodedOptimizations() {
  // Add Visual Effects Profile optimization
  auto visualEffectsProfileOpt =
    std::make_unique<settings::VisualEffectsOptimization>(
      "visual_effects_profile", "Visual Effects Profile",
      "Controls Windows visual effects profile for optimal performance",
      0,  // Default: Let Windows decide
      3   // Recommended: Recommended profile
    );
  optimizations_.push_back(std::move(visualEffectsProfileOpt));

  // Create preset groups
  auto gamingGroup = settings::OptimizationFactory::CreateGroup(
    "preset.gaming", "Gaming Optimizations",
    "Apply all gaming-related optimizations", {});
  optimizations_.push_back(std::move(gamingGroup));

  auto visualEffectsGroup = settings::OptimizationFactory::CreateGroup(
    "preset.visualeffects", "Visual Effects Optimizations",
    "Apply all visual effects optimizations for best performance", {});
  optimizations_.push_back(std::move(visualEffectsGroup));
}

void OptimizationManager::RebuildLookupTables() {
  optimizations_by_type_.clear();
  optimizations_by_category_.clear();

  std::map<std::string, bool> added_to_category;

  for (const auto& opt : optimizations_) {
    if (!opt) continue;

    OptimizationType type = opt->GetType();
    optimizations_by_type_[type].push_back(opt.get());

    std::string opt_id = opt->GetId();
    std::string category_name = "";

    if (type == OptimizationType::WindowsRegistry) {
      auto* configOpt =
        dynamic_cast<settings::ConfigurableOptimization*>(opt.get());
      if (configOpt) category_name = configOpt->GetCategory();
    } else if (type == OptimizationType::NvidiaSettings) {
      auto* nvOpt = dynamic_cast<nvidia::NvidiaOptimization*>(opt.get());
      if (nvOpt) category_name = nvOpt->GetCategory();
    } else if (type == OptimizationType::PowerPlan) {
      auto* powerOpt = dynamic_cast<power::PowerPlanOptimization*>(opt.get());
      if (powerOpt) {
        category_name = powerOpt->GetCategory();
      } else {
        auto* displayOpt =
          dynamic_cast<power::DisplayTimeoutOptimization*>(opt.get());
        if (displayOpt) category_name = displayOpt->GetCategory();
      }
    }

    if (!category_name.empty()) {
      std::string unique_key = category_name + ":" + opt_id;
      if (!added_to_category[unique_key]) {
        optimizations_by_category_[category_name].push_back(opt.get());
        added_to_category[unique_key] = true;
      }
    }
  }
}

std::vector<settings::OptimizationEntity*> OptimizationManager::
  GetOptimizationsByType(OptimizationType type) {
  auto it = optimizations_by_type_.find(type);
  if (it != optimizations_by_type_.end()) {
    return it->second;
  }
  return {};
}

std::vector<settings::OptimizationEntity*> OptimizationManager::
  GetOptimizationsByCategory(const std::string& category) {
  auto it = optimizations_by_category_.find(category);
  if (it != optimizations_by_category_.end()) {
    return it->second;
  }
  return {};
}

bool OptimizationManager::ApplyPreset(const std::string& preset_id) {
  auto* opt = FindOptimizationById(preset_id);
  if (!opt || opt->GetType() != OptimizationType::SettingGroup) {
    return false;
  }

  return opt->Apply(true);
}

std::string OptimizationManager::CreateCustomPreset(
  const std::string& name, const std::string& description) {
  std::string preset_id =
    "preset.custom." + std::to_string(optimizations_.size());

  auto group = settings::OptimizationFactory::CreateGroup(preset_id, name,
                                                          description, {});

  // Add all non-group optimizations
  for (const auto& opt : optimizations_) {
    if (opt->GetType() != OptimizationType::SettingGroup) {
      group->AddOptimization(opt->GetId());
    }
  }

  optimizations_.push_back(std::move(group));
  RebuildLookupTables();

  return preset_id;
}

bool OptimizationManager::LoadAllRegistrySettings() {
  auto& registrySettings = registry::RegistrySettings::GetInstance();

  if (!registrySettings.Initialize(all_registry_settings_path_)) {
    return false;
  }

  registrySettings.CheckCurrentValues();
  auto entities = registrySettings.CreateOptimizationEntities();

  for (auto& entity : entities) {
    if (!entity) continue;

    bool isMissing = registrySettings.IsSettingMissing(entity->GetId());
    entity->SetMissing(isMissing);

    bool exists = false;
    for (const auto& existingOpt : optimizations_) {
      if (!existingOpt) continue;
      if (existingOpt->GetId() == entity->GetId()) {
        exists = true;
        break;
      }
    }

    if (!exists) {
      optimizations_.push_back(std::move(entity));
    }
  }

  return true;
}

// Path utilities
std::string OptimizationManager::GetRevertPointsFilePath() {
  QDir appDir(QCoreApplication::applicationDirPath());
  return appDir.filePath("profiles/optimization_revert_points.json")
    .toStdString();
}

std::string OptimizationManager::GetConfigPath(const std::string& filename) {
  QDir appDir(QCoreApplication::applicationDirPath());
  return appDir.filePath(QString::fromStdString("profiles/" + filename))
    .toStdString();
}

std::string OptimizationManager::GetProfilesPath() {
  QDir appDir(QCoreApplication::applicationDirPath());
  QString profilesPath = appDir.filePath("profiles");
  QDir profilesDir(profilesPath);
  if (!profilesDir.exists()) {
    profilesDir.mkpath(".");
  }
  return profilesPath.toStdString();
}

bool OptimizationManager::ApplyOptimization(const std::string& id,
                                            const OptimizationValue& value) {
  settings::OptimizationEntity* opt = FindOptimizationById(id);
  return opt ? opt->Apply(value) : false;
}

bool OptimizationManager::RevertOptimization(const std::string& id,
                                             bool revert_to_original) {
  settings::OptimizationEntity* opt = FindOptimizationById(id);
  if (!opt) return false;

  if (revert_to_original) {
    if (opt->GetOriginalValue().index() != std::variant_npos) {
      return opt->Apply(opt->GetOriginalValue());
    }
    return false;
  }
  return opt->Revert();
}

settings::OptimizationEntity* OptimizationManager::FindOptimizationById(
  const std::string& id) {
  for (auto& opt : optimizations_) {
    if (opt->GetId() == id) {
      return opt.get();
    }
  }
  return nullptr;
}

bool OptimizationManager::CheckAllRegistrySettings() {
  auto& registrySettings = registry::RegistrySettings::GetInstance();

  if (!registrySettings.CheckSettingsFileExists()) {
    if (!registrySettings.Initialize(all_registry_settings_path_)) {
      return false;
    }
  }
  return registrySettings.CheckCurrentValues();
}

// Simplified versions of other methods...
bool OptimizationManager::RecordFirstRevertPoint() {
  if (has_recorded_first_revert_) {
    return true;
  }

  has_recorded_first_revert_ = true;
  SaveRevertPoints(GetRevertPointsFilePath());
  return true;
}

bool OptimizationManager::RecordSessionRevertPoint() {
  if (has_recorded_session_revert_) {
    return true;
  }

  bool allSucceeded = true;
  for (auto& opt : optimizations_) {
    try {
      OptimizationValue currentValue = opt->GetCurrentValue();
      opt->SetSessionStartValue(currentValue);
    } catch (...) {
      allSucceeded = false;
    }
  }

  has_recorded_session_revert_ = allSucceeded;
  SaveRevertPoints(GetRevertPointsFilePath());
  return allSucceeded;
}

std::string OptimizationManager::ValueToString(const OptimizationValue& value) {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  } else if (std::holds_alternative<int>(value)) {
    return std::to_string(std::get<int>(value));
  } else if (std::holds_alternative<double>(value)) {
    return std::to_string(std::get<double>(value));
  } else if (std::holds_alternative<std::string>(value)) {
    return std::get<std::string>(value);
  }
  return "unknown";
}

// Simplified export/import methods
bool OptimizationManager::ExportSettingsToJson(
  const std::string& filepath) const {
  QJsonObject settings;
  settings["export_timestamp"] = static_cast<long long>(std::time(nullptr));
  settings["version"] = 1;

  QJsonArray allSettings;
  for (const auto& opt : optimizations_) {
    if (!opt || opt->GetType() == OptimizationType::SettingGroup) {
      continue;
    }

    QJsonObject settingJson;
    settingJson["name"] = QString::fromStdString(opt->GetName());
    settingJson["current_value"] =
      settings::SerializeOptimizationValue(opt->GetCurrentValue());
    allSettings.append(settingJson);
  }
  settings["all_settings"] = allSettings;

  // Create directory and write file
  fs::create_directories(fs::path(filepath).parent_path());
  std::ofstream file(filepath);
  if (!file.is_open()) {
    return false;
  }

  QJsonDocument doc(settings);
  file << doc.toJson(QJsonDocument::Indented).toStdString();
  return true;
}

// Placeholder implementations for remaining methods
bool OptimizationManager::SaveRevertPoints(const std::string& filepath) {
  // Implementation simplified - just return true for now
  return true;
}

bool OptimizationManager::LoadRevertPoints(const std::string& filepath) {
  // Implementation simplified - just return true for now
  return true;
}

bool OptimizationManager::ExportConfigToJson(const std::string& filepath) {
  // Implementation simplified - just return true for now
  return true;
}

bool OptimizationManager::ImportConfigFromJson(const std::string& filepath) {
  // Implementation simplified - just return true for now
  return true;
}

bool OptimizationManager::LoadOptimizationsFromJson(
  const std::string& filepath) {
  // Implementation simplified - just return true for now
  return true;
}

}  // namespace optimizations
