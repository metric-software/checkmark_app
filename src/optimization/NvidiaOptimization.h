/**
 * @file NvidiaOptimization.h
 * @brief Optimization entity for NVIDIA GPU driver settings
 */

#pragma once

#include <map>
#include <string>

#include <QJsonObject>

#include "nvapi.h"
#include "NvApiDriverSettings.h"
#include "NvidiaControlPanel.h"
#include "OptimizationEntity.h"

namespace optimizations {

// Add a new optimization type for NVIDIA settings
enum class OptimizationType;  // Forward declaration to avoid circular dependency

namespace nvidia {

// Define this constant based on nvapi.h driver status
const NvAPI_Status NVAPI_NVIDIA_DRIVER_NOT_LOADED =
  static_cast<NvAPI_Status>(-6);

/**
 * @brief OptimizationEntity for NVIDIA GPU driver settings
 *
 * This class represents a configurable NVIDIA setting that can be
 * optimized for performance or other goals.
 */
class NvidiaOptimization : public settings::OptimizationEntity {
 public:
  /**
   * @brief Constructor for NvidiaOptimization
   * @param id Unique identifier for this optimization
   * @param name Display name
   * @param description Description of what this optimization does
   * @param current_value Current value of the setting
   * @param recommended_value Recommended value for performance
   * @param default_value Default value from NVIDIA
   * @param category Category for organization
   * @param is_personal_preference Whether this is mostly a matter of preference
   */
  NvidiaOptimization(const std::string& id, const std::string& name,
                     const std::string& description, int current_value,
                     int recommended_value, int default_value,
                     const std::string& category, bool is_personal_preference)
      : OptimizationEntity(id, name, description,
                           OptimizationType::NvidiaSettings),
        current_value(current_value), recommended_value(recommended_value),
        default_value(default_value), category(category),
        is_personal_preference(is_personal_preference) {
    // NVIDIA settings are not advanced
    SetAdvanced(false);
  }

  /**
   * @brief Get the current value of this optimization
   * @return Current value
   */
  OptimizationValue GetCurrentValue() const override { return current_value; }

  /**
   * @brief Get the recommended value for this optimization
   * @return Recommended value
   */
  OptimizationValue GetRecommendedValue() const override {
    return recommended_value;
  }

  /**
   * @brief Get the default value for this optimization
   * @return Default value
   */
  OptimizationValue GetDefaultValue() const override { return default_value; }

  /**
   * @brief Apply the optimization with a specified value
   * @param value Value to apply
   * @return True if successful
   */
  bool Apply(const OptimizationValue& value) override;

  /**
   * @brief Revert optimization to default settings
   * @return True if successful
   */
  bool Revert() override;

  /**
   * @brief Add a value option for this optimization
   * @param value Numeric value
   * @param description Human-readable description
   */
  void AddValueOption(int value, const std::string& description) {
    value_options[value] = description;
  }

  /**
   * @brief Get the map of value options for this optimization
   * @return Map of value to description
   */
  const std::map<int, std::string>& GetValueOptions() const {
    return value_options;
  }

  /**
   * @brief Get the category for this optimization
   * @return Category string
   */
  std::string GetCategory() const { return category; }

  /**
   * @brief Check if this is a personal preference setting
   * @return True if this is mainly a matter of preference
   */
  bool IsPersonalPreference() const { return is_personal_preference; }

  // Helper function to check if NVIDIA GPU is available
  bool IsNvidiaGPUPresent();

  // Get the NVIDIA setting ID (same as our ID)
  const std::string& GetNvidiaSettingId() const { return GetId(); }

  // Get the recommended and default values as integers
  int GetRecommendedIntValue() const { return recommended_value; }
  int GetDefaultIntValue() const { return default_value; }

  // Add possible values vector accessor
  const std::vector<settings::ValueOption>& GetPossibleValues() const override;

 private:
  int current_value;
  int recommended_value;
  int default_value;
  std::string category;
  bool is_personal_preference;
  std::map<int, std::string> value_options;
};

// Class for configurable NVIDIA optimizations loaded from JSON
class ConfigurableNvidiaOptimization : public NvidiaOptimization {
 public:
  ConfigurableNvidiaOptimization(const QJsonObject& config);

  // Additional methods and properties
  const std::string& GetSubcategory() const { return subcategory_; }
  bool IsAdvanced() const { return is_advanced_; }

  // Override base class method
  const std::vector<settings::ValueOption>& GetPossibleValues() const override {
    return possible_values_;
  }

  // Convert to JSON for serialization
  QJsonObject ToJson() const;

 private:
  std::string subcategory_;
  bool is_advanced_ = false;
  std::vector<settings::ValueOption> possible_values_;
};

}  // namespace nvidia
}  // namespace optimizations
