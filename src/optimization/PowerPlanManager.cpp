#include "PowerPlanManager.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <sstream>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <Powersetting.h>
#include <Rpc.h>
#include <objbase.h>

#include "BackupManager.h"
#include "OptimizationEntity.h"

// Required GUIDs for power settings
static const GUID GUID_VIDEO_SUBGROUP = {
  0x7516b95f, 0xf776, 0x4464, {0x8c, 0x53, 0x06, 0x16, 0x7f, 0x40, 0xcc, 0x99}};
static const GUID GUID_VIDEO_POWERDOWN_TIMEOUT = {
  0x3c0bc021, 0xc8a8, 0x4e07, {0xa9, 0x73, 0x6b, 0x14, 0xcb, 0xcb, 0x2b, 0x7e}};

namespace optimizations {
namespace power {

//------------------------------------------------------------------------------
// String Conversion Helpers
//------------------------------------------------------------------------------

static std::string WStringToString(const std::wstring& wstr) {
  if (wstr.empty()) return std::string();

  int bufferSize = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0,
                                       nullptr, nullptr);
  if (bufferSize <= 0) return std::string();

  std::string result(bufferSize - 1, 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], bufferSize,
                      nullptr, nullptr);
  return result;
}

static std::wstring StringToWString(const std::string& str) {
  if (str.empty()) return std::wstring();

  int bufferSize = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
  if (bufferSize <= 0) return std::wstring();

  std::wstring result(bufferSize - 1, 0);
  MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], bufferSize);
  return result;
}

static std::wstring GuidToWString(const GUID& guid) {
  wchar_t guidString[39] = {0};
  StringFromGUID2(guid, guidString, sizeof(guidString) / sizeof(guidString[0]));
  return std::wstring(guidString);
}

//------------------------------------------------------------------------------
// PowerPlanManager Implementation
//------------------------------------------------------------------------------

PowerPlanManager& PowerPlanManager::GetInstance() {
  static PowerPlanManager instance;
  return instance;
}

bool PowerPlanManager::Initialize() {
  if (is_initialized_) {
    return true;
  }

  available_plans_.clear();
  if (!GetAllPowerPlans(available_plans_)) {
    return false;
  }

  // Get current power plan
  GUID* activeGuid = nullptr;
  if (PowerGetActiveScheme(nullptr, &activeGuid) == ERROR_SUCCESS) {
    current_plan_guid_ = GuidToWString(*activeGuid);
    LocalFree(activeGuid);
  }

  is_initialized_ = true;
  return true;
}

bool PowerPlanManager::GetPowerPlanFriendlyName(const GUID& schemeGuid,
                                                std::string& friendlyName) {
  DWORD nameSize = 0;

  // Get required buffer size
  DWORD result = PowerReadFriendlyName(nullptr, &schemeGuid, nullptr, nullptr,
                                       nullptr, &nameSize);
  if (result != ERROR_SUCCESS && result != ERROR_MORE_DATA) {
    return false;
  }

  // Allocate and read friendly name
  std::vector<wchar_t> nameBuffer(nameSize / sizeof(wchar_t));
  result = PowerReadFriendlyName(nullptr, &schemeGuid, nullptr, nullptr,
                                 reinterpret_cast<PUCHAR>(nameBuffer.data()),
                                 &nameSize);
  if (result != ERROR_SUCCESS) {
    return false;
  }

  // Convert to string
  std::wstring wName(nameBuffer.data());
  friendlyName = WStringToString(wName);
  if (friendlyName.empty()) {
    friendlyName = "Unknown Plan";
  }

  return true;
}

bool PowerPlanManager::GetAllPowerPlans(std::vector<PowerPlan>& plans) {
  plans.clear();

  // Get current active scheme
  GUID* activeGuid = nullptr;
  std::wstring activeGuidStr;
  if (PowerGetActiveScheme(nullptr, &activeGuid) == ERROR_SUCCESS) {
    activeGuidStr = GuidToWString(*activeGuid);
    LocalFree(activeGuid);
  }

  // Enumerate all power schemes
  ULONG index = 0;
  GUID schemeGuid;
  DWORD bufferSize = sizeof(GUID);
  std::set<std::wstring> seenGuids;

  while (PowerEnumerate(nullptr, nullptr, nullptr, ACCESS_SCHEME, index,
                        reinterpret_cast<UCHAR*>(&schemeGuid),
                        &bufferSize) == ERROR_SUCCESS) {

    std::wstring guidStr = GuidToWString(schemeGuid);

    // Skip duplicates
    if (seenGuids.find(guidStr) != seenGuids.end()) {
      index++;
      continue;
    }
    seenGuids.insert(guidStr);

    // Get friendly name
    std::string friendlyName;
    if (!GetPowerPlanFriendlyName(schemeGuid, friendlyName)) {
      friendlyName = "Unknown Plan";
    }

    // Check if active
    bool isActive = (guidStr == activeGuidStr);

    plans.push_back({guidStr, friendlyName, isActive});
    index++;
  }

  return !plans.empty();
}

bool PowerPlanManager::CreateUltimatePerformancePlan() {
  GetAllPowerPlans(available_plans_);

  // Check if Ultimate Performance plan already exists
  for (const auto& plan : available_plans_) {
    if (plan.name == "Ultimate Performance") {
      return true;
    }
  }

  // Create from template
  GUID srcGuid;
  if (UuidFromStringW(reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(
                        ULTIMATE_PERFORMANCE_TPL_GUID.c_str())),
                      &srcGuid) != RPC_S_OK) {
    return false;
  }

  GUID* destGuid = nullptr;
  if (PowerDuplicateScheme(nullptr, &srcGuid, &destGuid) != ERROR_SUCCESS) {
    return false;
  }

  // Set friendly name
  wchar_t friendlyName[] = L"Ultimate Performance";
  DWORD nameSize = sizeof(friendlyName);
  bool success = (PowerWriteFriendlyName(nullptr, destGuid, nullptr, nullptr,
                                         reinterpret_cast<UCHAR*>(friendlyName),
                                         nameSize) == ERROR_SUCCESS);

  LocalFree(destGuid);

  if (success) {
    return GetAllPowerPlans(available_plans_);
  }
  return false;
}

std::wstring PowerPlanManager::GetCurrentPowerPlan() {
  if (!is_initialized_) {
    Initialize();
  }

  GUID* activeGuid = nullptr;
  if (PowerGetActiveScheme(nullptr, &activeGuid) != ERROR_SUCCESS) {
    return L"";
  }

  std::wstring guidStr = GuidToWString(*activeGuid);
  LocalFree(activeGuid);

  current_plan_guid_ = guidStr;
  return guidStr;
}

std::vector<PowerPlan> PowerPlanManager::GetAvailablePowerPlans() {
  if (!is_initialized_) {
    Initialize();
  }

  if (available_plans_.empty()) {
    GetAllPowerPlans(available_plans_);
  }
  return available_plans_;
}

std::wstring PowerPlanManager::EnableUltimatePerformance() {
  GetAllPowerPlans(available_plans_);

  // Find existing Ultimate Performance plan
  for (const auto& plan : available_plans_) {
    if (plan.name == "Ultimate Performance") {
      return plan.guid;
    }
  }

  // Create new Ultimate Performance plan
  if (CreateUltimatePerformancePlan()) {
    GetAllPowerPlans(available_plans_);
    for (const auto& plan : available_plans_) {
      if (plan.name == "Ultimate Performance") {
        return plan.guid;
      }
    }
  }

  return L"";
}

bool PowerPlanManager::SetPowerPlan(const std::wstring& guid) {
  if (!is_initialized_) {
    Initialize();
  }

  // Get current display timeout before switching
  DWORD currentDisplayTimeout =
    DisplayTimeoutOptimization::GetDisplayTimeoutForCurrentPlan();

  // Create backup if needed
  static bool backupInProgress = false;
  if (!backupInProgress) {
    backupInProgress = true;
    auto& backupManager = BackupManager::GetInstance();
    if (backupManager.CheckBackupStatus(BackupType::PowerPlan, false) !=
        BackupStatus::CompleteBackup) {
      backupManager.CreateBackup(BackupType::PowerPlan, false);
    }
    backupInProgress = false;
  }

  // Remove braces if present
  std::wstring guidCopy = guid;
  if (guidCopy.size() > 2 && guidCopy.front() == L'{' &&
      guidCopy.back() == L'}') {
    guidCopy = guidCopy.substr(1, guidCopy.size() - 2);
  }

  // Convert to GUID and set
  GUID powerGuid;
  if (UuidFromStringW(
        reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(guidCopy.c_str())),
        &powerGuid) != RPC_S_OK) {
    return false;
  }

  DWORD result = PowerSetActiveScheme(nullptr, &powerGuid);
  if (result == ERROR_SUCCESS) {
    current_plan_guid_ = guid;

    // Update active status in available plans
    for (auto& plan : available_plans_) {
      plan.isActive = (plan.guid == guid);
    }

    // Restore display timeout
    Sleep(100);
    DisplayTimeoutOptimization::SetDisplayTimeoutForCurrentPlan(
      currentDisplayTimeout);
    return true;
  }

  return false;
}

std::unique_ptr<settings::OptimizationEntity> PowerPlanManager::
  CreatePowerPlanOptimization() {
  if (!is_initialized_) {
    Initialize();
  }

  GetAllPowerPlans(available_plans_);

  auto powerPlanOpt = std::make_unique<PowerPlanOptimization>(
    "power.plan", "Power Plan",
    "Select the power plan that best suits your needs. Ultimate Performance "
    "provides maximum performance but uses more energy.",
    "Power", true, OptimizationImpact::Medium);

  // Add available power plans as options
  for (const auto& plan : available_plans_) {
    std::string guidStr = WStringToString(plan.guid);
    powerPlanOpt->AddValueOption(guidStr, plan.name);
  }

  // Add Ultimate Performance option if not available
  bool hasUltimatePerformance = false;
  for (const auto& plan : available_plans_) {
    if (plan.name == "Ultimate Performance") {
      hasUltimatePerformance = true;
      break;
    }
  }

  if (!hasUltimatePerformance) {
    std::string ultimateGuidStr =
      WStringToString(ULTIMATE_PERFORMANCE_TPL_GUID);
    powerPlanOpt->AddValueOption(ultimateGuidStr,
                                 "Ultimate Performance (will be created)");
  }

  return powerPlanOpt;
}

std::unique_ptr<settings::OptimizationEntity> PowerPlanManager::
  CreateDisplayTimeoutOptimization() {
  auto displayTimeoutOpt = std::make_unique<DisplayTimeoutOptimization>(
    "power.display_timeout", "Display Timeout",
    "Controls when the display turns off to save power. Setting to 'Never' "
    "prevents interruptions during gaming or work.",
    "Power", true, OptimizationImpact::Low);

  return displayTimeoutOpt;
}

//------------------------------------------------------------------------------
// PowerPlanOptimization Implementation
//------------------------------------------------------------------------------

PowerPlanOptimization::PowerPlanOptimization(const std::string& id,
                                             const std::string& name,
                                             const std::string& description,
                                             const std::string& category,
                                             bool personal_preference,
                                             OptimizationImpact impact)
    : settings::OptimizationEntity(id, name, description,
                                   OptimizationType::PowerPlan),
      category_(category), personal_preference_(personal_preference),
      impact_(impact) {
  SetAdvanced(true);
}

void PowerPlanOptimization::AddValueOption(const std::string& guid,
                                           const std::string& description) {
  settings::ValueOption option;
  option.value = guid;
  option.description = description;
  possible_values_.push_back(option);
}

bool PowerPlanOptimization::Apply(const OptimizationValue& value) {
  auto& manager = PowerPlanManager::GetInstance();

  if (!std::holds_alternative<std::string>(value)) {
    return false;
  }

  std::string guidStr = std::get<std::string>(value);

  // Remove braces if present
  if (guidStr.size() > 2 && guidStr.front() == '{' && guidStr.back() == '}') {
    guidStr = guidStr.substr(1, guidStr.size() - 2);
  }

  // Check if this is Ultimate Performance template GUID
  std::string ultimateTemplateGuidStr =
    WStringToString(manager.ULTIMATE_PERFORMANCE_TPL_GUID);

  if (guidStr == ultimateTemplateGuidStr) {
    std::wstring ultimateGuid = manager.EnableUltimatePerformance();
    if (ultimateGuid.empty()) {
      return false;
    }
    guidStr = WStringToString(ultimateGuid);
  }

  std::wstring guid = StringToWString(guidStr);
  return manager.SetPowerPlan(guid);
}

bool PowerPlanOptimization::Revert() {
  if (std::holds_alternative<std::string>(session_start_value_)) {
    return Apply(session_start_value_);
  }

  // Default to Balanced
  auto& manager = PowerPlanManager::GetInstance();
  std::string guidStr = WStringToString(manager.BALANCED_GUID);
  return Apply(guidStr);
}

OptimizationValue PowerPlanOptimization::GetCurrentValue() const {
  auto& manager = PowerPlanManager::GetInstance();
  std::wstring currentGuid = manager.GetCurrentPowerPlan();
  return WStringToString(currentGuid);
}

OptimizationValue PowerPlanOptimization::GetRecommendedValue() const {
  auto& manager = PowerPlanManager::GetInstance();
  return WStringToString(manager.HIGH_PERFORMANCE_GUID);
}

OptimizationValue PowerPlanOptimization::GetDefaultValue() const {
  auto& manager = PowerPlanManager::GetInstance();
  return WStringToString(manager.BALANCED_GUID);
}

//------------------------------------------------------------------------------
// ConfigurablePowerPlanOptimization Implementation
//------------------------------------------------------------------------------

ConfigurablePowerPlanOptimization::ConfigurablePowerPlanOptimization(
  const QJsonObject& config)
    : PowerPlanOptimization(
        config.value("id").toString().toStdString(),
        config.value("name").toString().toStdString(),
        config.value("description").toString().toStdString(),
        config.contains("category")
          ? config.value("category").toString().toStdString()
          : "Power",
        config.contains("personal_preference")
          ? config.value("personal_preference").toBool()
          : true,
        OptimizationImpact::Medium) {

  if (config.contains("subcategory")) {
    subcategory_ = config.value("subcategory").toString().toStdString();
  }

  is_advanced_ = true;

  // Load possible value options
  if (config.contains("possible_values") &&
      config.value("possible_values").isArray()) {
    const QJsonArray possibleValues = config.value("possible_values").toArray();
    for (const QJsonValue& val : possibleValues) {
      if (val.isObject()) {
        QJsonObject valueObj = val.toObject();
        if (valueObj.contains("value") && valueObj.contains("description")) {
          OptimizationValue parsed_value =
            settings::ParseOptimizationValue(valueObj.value("value"));
          if (std::holds_alternative<std::string>(parsed_value)) {
            std::string guid_str = std::get<std::string>(parsed_value);
            std::string description =
              valueObj.value("description").toString().toStdString();
            if (!guid_str.empty()) {
              AddValueOption(guid_str, description);
            }
          }
        }
      }
    }
  }
}

QJsonObject ConfigurablePowerPlanOptimization::ToJson() const {
  QJsonObject j;
  j["id"] = QString::fromStdString(GetId());
  j["name"] = QString::fromStdString(GetName());
  j["description"] = QString::fromStdString(GetDescription());
  j["category"] = QString::fromStdString(GetCategory());
  j["subcategory"] = QString::fromStdString(subcategory_);
  j["is_advanced"] = IsAdvanced();
  j["personal_preference"] = IsPersonalPreference();
  j["type"] = "power";

  // Serialize possible values
  if (!GetPossibleValues().empty()) {
    QJsonArray possibleValuesArray;
    for (const auto& option : GetPossibleValues()) {
      QJsonObject optionObj;
      optionObj["value"] = settings::SerializeOptimizationValue(option.value);
      optionObj["description"] = QString::fromStdString(option.description);
      possibleValuesArray.append(optionObj);
    }
    j["possible_values"] = possibleValuesArray;
  }

  return j;
}

//------------------------------------------------------------------------------
// DisplayTimeoutOptimization Implementation
//------------------------------------------------------------------------------

// Static member definitions
std::map<std::wstring, DWORD>
  DisplayTimeoutOptimization::original_display_timeouts_;
bool DisplayTimeoutOptimization::timeouts_preserved_ = false;

DisplayTimeoutOptimization::DisplayTimeoutOptimization(
  const std::string& id, const std::string& name,
  const std::string& description, const std::string& category,
  bool personal_preference, OptimizationImpact impact)
    : settings::OptimizationEntity(id, name, description,
                                   OptimizationType::PowerPlan),
      category_(category), personal_preference_(personal_preference),
      impact_(impact) {

  SetAdvanced(true);

  // Initialize timeout options (in minutes)
  possible_values_ = {
    {1, "1 minute"},    {2, "2 minutes"},   {3, "3 minutes"},
    {5, "5 minutes"},   {10, "10 minutes"}, {15, "15 minutes"},
    {20, "20 minutes"}, {25, "25 minutes"}, {30, "30 minutes"},
    {45, "45 minutes"}, {60, "1 hour"},     {120, "2 hours"},
    {180, "3 hours"},   {240, "4 hours"},   {300, "5 hours"},
    {0, "Never"}};
}

DWORD DisplayTimeoutOptimization::GetDisplayTimeoutForCurrentPlan() {
  GUID* activeGuid = nullptr;
  if (PowerGetActiveScheme(nullptr, &activeGuid) != ERROR_SUCCESS) {
    return 15;  // Default to 15 minutes
  }

  DWORD timeoutValue = 15;
  DWORD result =
    PowerReadACValueIndex(nullptr, activeGuid, &GUID_VIDEO_SUBGROUP,
                          &GUID_VIDEO_POWERDOWN_TIMEOUT, &timeoutValue);

  LocalFree(activeGuid);

  if (result == ERROR_SUCCESS) {
    return timeoutValue / 60;  // Convert seconds to minutes
  }

  return 15;  // Default fallback
}

bool DisplayTimeoutOptimization::SetDisplayTimeoutForCurrentPlan(
  DWORD timeoutMinutes) {
  GUID* activeGuid = nullptr;
  if (PowerGetActiveScheme(nullptr, &activeGuid) != ERROR_SUCCESS) {
    return false;
  }

  DWORD timeoutSeconds = timeoutMinutes * 60;

  // Set for both AC and DC power
  DWORD result1 =
    PowerWriteACValueIndex(nullptr, activeGuid, &GUID_VIDEO_SUBGROUP,
                           &GUID_VIDEO_POWERDOWN_TIMEOUT, timeoutSeconds);

  DWORD result2 =
    PowerWriteDCValueIndex(nullptr, activeGuid, &GUID_VIDEO_SUBGROUP,
                           &GUID_VIDEO_POWERDOWN_TIMEOUT, timeoutSeconds);

  bool success = (result1 == ERROR_SUCCESS && result2 == ERROR_SUCCESS);

  if (success) {
    PowerSetActiveScheme(nullptr, activeGuid);
  }

  LocalFree(activeGuid);
  return success;
}

bool DisplayTimeoutOptimization::SetDisplayTimeoutForAllPlans(
  DWORD timeoutMinutes) {
  auto& manager = PowerPlanManager::GetInstance();
  auto plans = manager.GetAvailablePowerPlans();

  bool allSuccess = true;
  DWORD timeoutSeconds = timeoutMinutes * 60;

  for (const auto& plan : plans) {
    std::wstring guidStr = plan.guid;

    // Remove braces if present
    if (guidStr.size() > 2 && guidStr.front() == L'{' &&
        guidStr.back() == L'}') {
      guidStr = guidStr.substr(1, guidStr.size() - 2);
    }

    GUID planGuid;
    if (UuidFromStringW(
          reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(guidStr.c_str())),
          &planGuid) != RPC_S_OK) {
      allSuccess = false;
      continue;
    }

    DWORD result1 =
      PowerWriteACValueIndex(nullptr, &planGuid, &GUID_VIDEO_SUBGROUP,
                             &GUID_VIDEO_POWERDOWN_TIMEOUT, timeoutSeconds);

    DWORD result2 =
      PowerWriteDCValueIndex(nullptr, &planGuid, &GUID_VIDEO_SUBGROUP,
                             &GUID_VIDEO_POWERDOWN_TIMEOUT, timeoutSeconds);

    if (result1 != ERROR_SUCCESS || result2 != ERROR_SUCCESS) {
      allSuccess = false;
    }
  }

  return allSuccess;
}

void DisplayTimeoutOptimization::PreserveDisplayTimeoutWhenSwitchingPlans() {
  if (timeouts_preserved_) {
    return;
  }

  auto& manager = PowerPlanManager::GetInstance();
  auto plans = manager.GetAvailablePowerPlans();

  for (const auto& plan : plans) {
    std::wstring guidStr = plan.guid;

    if (guidStr.size() > 2 && guidStr.front() == L'{' &&
        guidStr.back() == L'}') {
      guidStr = guidStr.substr(1, guidStr.size() - 2);
    }

    GUID planGuid;
    if (UuidFromStringW(
          reinterpret_cast<RPC_WSTR>(const_cast<wchar_t*>(guidStr.c_str())),
          &planGuid) != RPC_S_OK) {
      continue;
    }

    DWORD timeoutValue = 15 * 60;  // Default 15 minutes in seconds

    DWORD result =
      PowerReadACValueIndex(nullptr, &planGuid, &GUID_VIDEO_SUBGROUP,
                            &GUID_VIDEO_POWERDOWN_TIMEOUT, &timeoutValue);

    if (result == ERROR_SUCCESS) {
      original_display_timeouts_[plan.guid] =
        timeoutValue / 60;  // Store in minutes
    }
  }

  timeouts_preserved_ = true;
}

bool DisplayTimeoutOptimization::Apply(const OptimizationValue& value) {
  DWORD timeoutMinutes = 15;  // Default

  if (std::holds_alternative<int>(value)) {
    timeoutMinutes = static_cast<DWORD>(std::get<int>(value));
  } else if (std::holds_alternative<double>(value)) {
    timeoutMinutes = static_cast<DWORD>(std::get<double>(value));
  }

  return SetDisplayTimeoutForAllPlans(timeoutMinutes);
}

bool DisplayTimeoutOptimization::Revert() {
  if (std::holds_alternative<int>(session_start_value_)) {
    return Apply(session_start_value_);
  }

  return Apply(15);  // Default to 15 minutes
}

OptimizationValue DisplayTimeoutOptimization::GetCurrentValue() const {
  return static_cast<int>(GetDisplayTimeoutForCurrentPlan());
}

OptimizationValue DisplayTimeoutOptimization::GetRecommendedValue() const {
  return 0;  // Never turn off display for gaming/performance
}

OptimizationValue DisplayTimeoutOptimization::GetDefaultValue() const {
  return 15;  // Default Windows setting: 15 minutes
}

}  // namespace power
}  // namespace optimizations
