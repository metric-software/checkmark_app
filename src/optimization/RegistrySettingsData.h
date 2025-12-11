#pragma once

#include <string>
#include <vector>

#include "OptimizationEntity.h"  // For OptimizationValue

namespace optimizations {
namespace registry {

struct RegistrySettingOption {
  OptimizationValue value;
  std::string description;
};

struct WrappedRegistrySetting {
  std::string registry_key;
  std::string registry_value_name;
  OptimizationValue enabled_value;
  OptimizationValue disabled_value;
};

struct RegistrySettingDefinition {
  std::string id;
  std::string name;
  std::string description;
  std::string registry_key;
  std::string registry_value_name;
  OptimizationValue default_value;
  OptimizationValue recommended_value;
  std::string category;
  std::string subcategory;
  bool is_advanced = true;
  bool personal_preference = true;
  bool creation_allowed = false;
  int level = 0;
  bool requires_system_refresh = false;
  bool dont_edit = false;
  bool is_wrapper = false;
  std::vector<WrappedRegistrySetting> wrapped_settings;
  std::vector<RegistrySettingOption> possible_values;
};

const std::vector<RegistrySettingDefinition>& GetRegistrySettingDefinitions();

}  // namespace registry
}  // namespace optimizations
