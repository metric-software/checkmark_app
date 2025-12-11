/**
 * @file OptimizationEntity.h
 * @brief Core framework for managing system optimization settings
 *
 * Supports Registry, NVIDIA, Visual Effects, Power Plans, and Setting Groups.
 * Each setting tracks original and session values for proper reversion.
 */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace optimizations {

namespace registry {
struct RegistrySettingDefinition;
}

/**
 * @brief Impact level of an optimization on system performance
 */
enum class OptimizationImpact { None, Low, Medium, High };

/**
 * @brief Types of optimizations supported
 */
enum class OptimizationType {
  WindowsRegistry,
  NvidiaSettings,
  VisualEffects,
  PowerPlan,
  SettingGroup
};

/**
 * @brief Value type for optimization settings
 */
using OptimizationValue = std::variant<bool, int, double, std::string>;

namespace settings {

// Helper functions for JSON parsing
OptimizationValue ParseOptimizationValue(const QJsonValue& q_json_value);
QJsonValue SerializeOptimizationValue(const OptimizationValue& value);

// Helper template function to safely get a value from a variant or a default
template <typename T, typename VariantType>
T GetVariantValueOrDefault(const VariantType& variant, T default_value) {
  try {
    if (variant.index() == std::variant_npos) {
      return default_value;
    }
    return std::get<T>(variant);
  } catch (const std::bad_variant_access&) {
    return default_value;
  }
}

// Forward declarations
class OptimizationGroup;
class RegistryOptimization;
class ConfigurableOptimization;

/**
 * @brief Represents a discrete option for an optimization
 */
struct ValueOption {
  OptimizationValue value;
  std::string description;
};

/**
 * @brief Base class for all optimization entities
 */
class OptimizationEntity {
 public:
  OptimizationEntity(const std::string& id, const std::string& name,
                     const std::string& description, OptimizationType type);

  virtual ~OptimizationEntity() = default;

  // Core virtual methods
  virtual bool Apply(const OptimizationValue& value) = 0;
  virtual bool Revert() = 0;
  virtual OptimizationValue GetCurrentValue() const = 0;
  virtual OptimizationValue GetRecommendedValue() const = 0;
  virtual OptimizationValue GetDefaultValue() const = 0;

  // Optional: Get possible values for UI dropdowns
  virtual const std::vector<ValueOption>& GetPossibleValues() const {
    static const std::vector<ValueOption> empty_values;
    return empty_values;
  }

  // Basic accessors
  const std::string& GetId() const { return id_; }
  const std::string& GetName() const { return name_; }
  const std::string& GetDescription() const { return description_; }
  OptimizationType GetType() const { return type_; }

  // Revert point management
  OptimizationValue GetOriginalValue() const { return original_value_; }
  void SetOriginalValue(const OptimizationValue& value) {
    original_value_ = value;
  }

  OptimizationValue GetSessionStartValue() const {
    return session_start_value_;
  }
  void SetSessionStartValue(const OptimizationValue& value) {
    session_start_value_ = value;
  }

  // UI flags
  bool IsAdvanced() const { return is_advanced_; }
  void SetAdvanced(bool advanced) { is_advanced_ = advanced; }

  bool DontEdit() const { return dont_edit_; }
  void SetDontEdit(bool dont_edit) { dont_edit_ = dont_edit; }

  bool IsMissing() const { return is_missing_; }
  void SetMissing(bool missing) { is_missing_ = missing; }

 protected:
  std::string id_;
  std::string name_;
  std::string description_;
  OptimizationType type_;

  // Revert points
  OptimizationValue original_value_;
  OptimizationValue session_start_value_;

  // UI flags
  bool is_advanced_ = true;
  bool dont_edit_ = true;
  bool is_missing_ = false;
};

/**
 * @brief Windows registry-based optimizations
 */
class RegistryOptimization : public OptimizationEntity {
 public:
  RegistryOptimization(const std::string& id, const std::string& name,
                       const std::string& description,
                       const std::string& registry_key,
                       const std::string& registry_value_name,
                       const OptimizationValue& default_value,
                       const OptimizationValue& recommended_value);

  bool Apply(const OptimizationValue& value) override;
  bool Revert() override;
  OptimizationValue GetCurrentValue() const override;
  OptimizationValue GetRecommendedValue() const override;
  OptimizationValue GetDefaultValue() const override;

  const std::string& GetRegistryKey() const { return registry_key_; }
  const std::string& GetRegistryValueName() const {
    return registry_value_name_;
  }

 protected:
  std::string registry_key_;
  std::string registry_value_name_;
  OptimizationValue default_value_;
  OptimizationValue recommended_value_;
};

/**
 * @brief Windows visual effects optimizations
 */
class VisualEffectsOptimization : public OptimizationEntity {
 public:
  VisualEffectsOptimization(const std::string& id, const std::string& name,
                            const std::string& description,
                            const OptimizationValue& default_value,
                            const OptimizationValue& recommended_value);

  bool Apply(const OptimizationValue& value) override;
  bool Revert() override;
  OptimizationValue GetCurrentValue() const override;
  OptimizationValue GetRecommendedValue() const override;
  OptimizationValue GetDefaultValue() const override;

  const std::vector<ValueOption>& GetPossibleValues() const override;

 protected:
  OptimizationValue default_value_;
  OptimizationValue recommended_value_;
  mutable std::vector<ValueOption> possible_values_;
};

/**
 * @brief Configurable registry optimizations loaded from JSON
 */
class ConfigurableOptimization : public RegistryOptimization {
 public:
  explicit ConfigurableOptimization(
    const registry::RegistrySettingDefinition& def);
  ConfigurableOptimization(const QJsonObject& config);

  const std::string& GetCategory() const { return category_; }
  const std::string& GetSubcategory() const { return subcategory_; }
  bool IsPersonalPreference() const { return personal_preference_; }
  bool IsCreationAllowed() const { return creation_allowed_; }
  int GetLevel() const { return level_; }

  const std::vector<ValueOption>& GetPossibleValues() const override {
    return possible_values_;
  }

  QJsonObject ToJson() const;

  // Custom function overrides
  using ApplyFunctionType = std::function<bool(const OptimizationValue&)>;
  void SetCustomApply(ApplyFunctionType fn) {
    custom_apply_fn_ = std::move(fn);
  }

  using GetCurrentValueFunctionType = std::function<OptimizationValue()>;
  void SetCustomGetCurrentValue(GetCurrentValueFunctionType fn) {
    custom_get_current_value_fn_ = std::move(fn);
  }

  bool Apply(const OptimizationValue& value) override {
    if (custom_apply_fn_) {
      return custom_apply_fn_(value);
    }
    return RegistryOptimization::Apply(value);
  }

  OptimizationValue GetCurrentValue() const override {
    if (custom_get_current_value_fn_) {
      return custom_get_current_value_fn_();
    }
    return RegistryOptimization::GetCurrentValue();
  }

 private:
  std::string category_;
  std::string subcategory_;
  bool personal_preference_ = false;
  bool creation_allowed_ = false;
  int level_ = 0;  // 0=normal, 1=optional, 2=experimental
  std::vector<ValueOption> possible_values_;

  ApplyFunctionType custom_apply_fn_;
  GetCurrentValueFunctionType custom_get_current_value_fn_;
};

/**
 * @brief Groups of optimizations (presets)
 */
class OptimizationGroup : public OptimizationEntity {
 public:
  OptimizationGroup(const std::string& id, const std::string& name,
                    const std::string& description);

  bool Apply(const OptimizationValue& value) override;
  bool Revert() override;

  // Groups don't have individual values
  OptimizationValue GetCurrentValue() const override { return false; }
  OptimizationValue GetRecommendedValue() const override { return true; }
  OptimizationValue GetDefaultValue() const override { return false; }

  void AddOptimization(const std::string& optimization_id);
  const std::vector<std::string>& GetOptimizationIds() const {
    return optimization_ids_;
  }

 private:
  std::vector<std::string> optimization_ids_;
};

/**
 * @brief Factory for creating optimization entities
 */
class OptimizationFactory {
 public:
  static std::unique_ptr<OptimizationEntity> CreateRegistryOptimization(
    const std::string& id, const std::string& name,
    const std::string& description, const std::string& registry_key,
    const std::string& registry_value_name,
    const OptimizationValue& default_value,
    const OptimizationValue& recommended_value);

  static std::unique_ptr<OptimizationEntity> CreateFromJson(
    const QJsonObject& config);

  static std::unique_ptr<OptimizationGroup> CreateGroup(
    const std::string& id, const std::string& name,
    const std::string& description,
    const std::vector<std::string>& optimization_ids = {});
};

}  // namespace settings

/**
 * @brief Singleton manager for all optimizations
 */
class OptimizationManager {
 public:
  static OptimizationManager& GetInstance();

  void Initialize();

  // Get optimizations by type or category
  std::vector<settings::OptimizationEntity*> GetOptimizationsByType(
    OptimizationType type);
  std::vector<settings::OptimizationEntity*> GetOptimizationsByCategory(
    const std::string& category);

  // Revert point management
  bool RecordFirstRevertPoint();
  bool RecordSessionRevertPoint();
  bool HasRecordedFirstRevertPoint() const {
    return has_recorded_first_revert_;
  }
  bool HasRecordedSessionRevertPoint() const {
    return has_recorded_session_revert_;
  }

  // Registry settings validation
  bool CheckAllRegistrySettings();

  // Apply and revert operations
  bool ApplyOptimization(const std::string& id, const OptimizationValue& value);
  bool RevertOptimization(const std::string& id,
                          bool revert_to_original = false);

  // Preset management
  bool ApplyPreset(const std::string& preset_id);
  std::string CreateCustomPreset(const std::string& name,
                                 const std::string& description);

  // Persistence operations
  bool SaveRevertPoints(const std::string& filepath);
  bool LoadRevertPoints(const std::string& filepath);
  bool ExportConfigToJson(const std::string& filepath);
  bool ImportConfigFromJson(const std::string& filepath);
  bool LoadOptimizationsFromJson(const std::string& filepath);
  bool LoadAllRegistrySettings();

  // Lookup
  settings::OptimizationEntity* FindOptimizationById(const std::string& id);

  // Path utilities
  std::string GetRevertPointsFilePath();
  std::string GetConfigPath(const std::string& filename);
  std::string GetProfilesPath();

  // Export current settings
  bool ExportSettingsToJson(const std::string& filepath) const;

 private:
  OptimizationManager();
  ~OptimizationManager() = default;

  OptimizationManager(const OptimizationManager&) = delete;
  OptimizationManager& operator=(const OptimizationManager&) = delete;

  void RegisterHardCodedOptimizations();
  void RebuildLookupTables();
  std::string ValueToString(const OptimizationValue& value);

  std::vector<std::unique_ptr<settings::OptimizationEntity>> optimizations_;
  std::unordered_map<OptimizationType,
                     std::vector<settings::OptimizationEntity*>>
    optimizations_by_type_;
  std::unordered_map<std::string, std::vector<settings::OptimizationEntity*>>
    optimizations_by_category_;

  bool has_recorded_first_revert_ = false;
  bool has_recorded_session_revert_ = false;
  bool is_initialized_ = false;

  std::string all_registry_settings_path_;
};

}  // namespace optimizations
