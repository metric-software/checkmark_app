#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <QJsonObject>
#include <windows.h>
#include <powrprof.h>

#include "OptimizationEntity.h"

namespace optimizations {
namespace power {

/**
 * @brief Structure representing a Windows power plan
 */
struct PowerPlan {
  std::wstring guid;
  std::string name;
  bool isActive;
};

/**
 * @brief Manages Windows power plans using Windows Power API
 *
 * Provides functionality to enumerate, query, and switch between power plans.
 * Supports creating Ultimate Performance plan and managing display timeouts.
 */
class PowerPlanManager {
 public:
  /**
   * @brief Get the singleton instance
   * @return Reference to the PowerPlanManager instance
   */
  static PowerPlanManager& GetInstance();

  /**
   * @brief Initialize the manager and enumerate available power plans
   * @return True if initialization successful
   */
  bool Initialize();

  /**
   * @brief Get all available power plans on the system
   * @return Vector of PowerPlan structures
   */
  std::vector<PowerPlan> GetAvailablePowerPlans();

  /**
   * @brief Get the GUID of the currently active power plan
   * @return Wide string containing the current power plan GUID
   */
  std::wstring GetCurrentPowerPlan();

  /**
   * @brief Enable Ultimate Performance power plan (creates if not available)
   * @return GUID of the Ultimate Performance plan, empty if failed
   */
  std::wstring EnableUltimatePerformance();

  /**
   * @brief Set the active power plan by GUID
   * @param guid Wide string containing the power plan GUID
   * @return True if successful
   */
  bool SetPowerPlan(const std::wstring& guid);

  /**
   * @brief Create optimization entity for power plan selection
   * @return Unique pointer to OptimizationEntity for power plans
   */
  std::unique_ptr<settings::OptimizationEntity> CreatePowerPlanOptimization();

  /**
   * @brief Create optimization entity for display timeout settings
   * @return Unique pointer to OptimizationEntity for display timeout
   */
  std::unique_ptr<settings::OptimizationEntity> CreateDisplayTimeoutOptimization();

  // Predefined power plan GUIDs
  const std::wstring BALANCED_GUID = L"381b4222-f694-41f0-9685-ff5bb260df2e";
  const std::wstring HIGH_PERFORMANCE_GUID =
    L"8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c";
  const std::wstring POWER_SAVER_GUID = L"a1841308-3541-4fab-bc81-f71556f20b4a";
  const std::wstring ULTIMATE_PERFORMANCE_TPL_GUID =
    L"e9a42b02-d5df-448d-aa00-03f14749eb61";
  const std::wstring ULTIMATE_PERFORMANCE_GUID =
    L"0cc5b647-c1df-4637-891a-dec35c318583";

 private:
  PowerPlanManager() = default;
  ~PowerPlanManager() = default;
  PowerPlanManager(const PowerPlanManager&) = delete;
  PowerPlanManager& operator=(const PowerPlanManager&) = delete;

  // Internal helper methods
  bool GetPowerPlanFriendlyName(const GUID& schemeGuid,
                                std::string& friendlyName);
  bool GetAllPowerPlans(std::vector<PowerPlan>& plans);
  bool CreateUltimatePerformancePlan();

  // Internal state
  std::vector<PowerPlan> available_plans_;
  std::wstring current_plan_guid_;
  bool is_initialized_ = false;
};

/**
 * @brief Optimization entity for power plan selection
 */
class PowerPlanOptimization : public settings::OptimizationEntity {
 public:
  /**
   * @brief Constructor
   * @param id Unique identifier
   * @param name Display name
   * @param description User-friendly description
   * @param category Category for grouping (default: "Power")
   * @param personal_preference Whether this is a personal preference setting
   * @param impact Performance impact level
   */
  PowerPlanOptimization(const std::string& id, const std::string& name,
                        const std::string& description,
                        const std::string& category = "Power",
                        bool personal_preference = true,
                        OptimizationImpact impact = OptimizationImpact::Medium);

  // OptimizationEntity interface
  bool Apply(const OptimizationValue& value) override;
  bool Revert() override;
  OptimizationValue GetCurrentValue() const override;
  OptimizationValue GetRecommendedValue() const override;
  OptimizationValue GetDefaultValue() const override;
  const std::vector<settings::ValueOption>& GetPossibleValues() const override {
    return possible_values_;
  }

  // Accessors
  const std::string& GetCategory() const { return category_; }
  bool IsPersonalPreference() const { return personal_preference_; }
  OptimizationImpact GetImpact() const { return impact_; }

  /**
   * @brief Add a power plan option for UI selection
   * @param guid Power plan GUID
   * @param description User-friendly description
   */
  void AddValueOption(const std::string& guid, const std::string& description);

 protected:
  std::string category_;
  bool personal_preference_;
  OptimizationImpact impact_;
  std::vector<settings::ValueOption> possible_values_;
};

/**
 * @brief Configurable power plan optimization loaded from JSON
 */
class ConfigurablePowerPlanOptimization : public PowerPlanOptimization {
 public:
  /**
   * @brief Constructor from JSON configuration
   * @param config JSON object containing configuration
   */
  ConfigurablePowerPlanOptimization(const QJsonObject& config);

  const std::string& GetSubcategory() const { return subcategory_; }
  bool IsAdvanced() const { return true; }
  QJsonObject ToJson() const;

 private:
  std::string subcategory_;
};

/**
 * @brief Optimization entity for display timeout settings
 */
class DisplayTimeoutOptimization : public settings::OptimizationEntity {
 public:
  /**
   * @brief Constructor
   * @param id Unique identifier
   * @param name Display name
   * @param description User-friendly description
   * @param category Category for grouping (default: "Power")
   * @param personal_preference Whether this is a personal preference setting
   * @param impact Performance impact level
   */
  DisplayTimeoutOptimization(
    const std::string& id, const std::string& name,
    const std::string& description, const std::string& category = "Power",
    bool personal_preference = true,
    OptimizationImpact impact = OptimizationImpact::Low);

  // OptimizationEntity interface
  bool Apply(const OptimizationValue& value) override;
  bool Revert() override;
  OptimizationValue GetCurrentValue() const override;
  OptimizationValue GetRecommendedValue() const override;
  OptimizationValue GetDefaultValue() const override;
  const std::vector<settings::ValueOption>& GetPossibleValues() const override {
    return possible_values_;
  }

  // Accessors
  const std::string& GetCategory() const { return category_; }
  bool IsPersonalPreference() const { return personal_preference_; }
  OptimizationImpact GetImpact() const { return impact_; }

  // Static utility methods for display timeout management
  static DWORD GetDisplayTimeoutForCurrentPlan();
  static bool SetDisplayTimeoutForCurrentPlan(DWORD timeoutMinutes);
  static bool SetDisplayTimeoutForAllPlans(DWORD timeoutMinutes);
  static void PreserveDisplayTimeoutWhenSwitchingPlans();

 protected:
  std::string category_;
  bool personal_preference_;
  OptimizationImpact impact_;
  std::vector<settings::ValueOption> possible_values_;

  // Static storage for timeout preservation
  static std::map<std::wstring, DWORD> original_display_timeouts_;
  static bool timeouts_preserved_;
};

}  // namespace power
}  // namespace optimizations
