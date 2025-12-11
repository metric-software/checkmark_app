/**
 * @file VisualEffectsManager.h
 * @brief Simplified interface for managing Windows visual effects settings
 *
 * This class provides methods to optimize Windows visual effects
 * using predefined profiles for performance vs appearance.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <Windows.h>

namespace optimizations {
namespace visual_effects {

/**
 * @brief Enum for predefined visual effects profiles
 */
enum class VisualEffectsProfile {
  LetWindowsDecide = 0,  // Default Windows automatic setting
  BestAppearance = 1,    // Maximize visual effects
  BestPerformance = 2,   // Minimize visual effects for performance
  Recommended = 3,       // Custom optimized performance profile
  Custom = 4             // Custom user profile
};

/**
 * @brief Simple manager for Windows visual effects settings
 */
class VisualEffectsManager {
 public:
  /**
   * @brief Get the singleton instance
   * @return Reference to the VisualEffectsManager instance
   */
  static VisualEffectsManager& GetInstance();

  /**
   * @brief Initialize the manager and check Windows compatibility
   * @return True if initialization was successful
   */
  bool Initialize();

  /**
   * @brief Apply a predefined visual effects profile
   *
   * @param profile The profile to apply
   * @return True if successful
   */
  bool ApplyProfile(VisualEffectsProfile profile);

  /**
   * @brief Get the current visual effects profile
   *
   * @return The currently active profile
   */
  VisualEffectsProfile GetCurrentProfile();

  /**
   * @brief Get a friendly name for a profile
   *
   * @param profile The profile to get a name for
   * @return A user-friendly name
   */
  static std::string GetProfileName(VisualEffectsProfile profile);

  /**
   * @brief Get a description for a profile
   *
   * @param profile The profile to get a description for
   * @return A user-friendly description
   */
  static std::string GetProfileDescription(VisualEffectsProfile profile);

 private:
  // Private constructor for singleton
  VisualEffectsManager();
  ~VisualEffectsManager() = default;

  // No copy/move allowed
  VisualEffectsManager(const VisualEffectsManager&) = delete;
  VisualEffectsManager& operator=(const VisualEffectsManager&) = delete;

  /**
   * @brief Apply registry-based settings for a profile
   *
   * @param profile The profile to apply
   * @return True if successful
   */
  bool ApplyRegistrySettings(VisualEffectsProfile profile);

  /**
   * @brief Set UserPreferencesMask for visual effects
   *
   * @param maskBytes The mask bytes to set
   * @param maskSize Size of the mask in bytes
   * @return True if successful
   */
  bool SetUserPreferencesMask(const BYTE* maskBytes, DWORD maskSize);

  /**
   * @brief Read UserPreferencesMask
   *
   * @param maskBytes Buffer to store the mask
   * @param maskSize Size of the buffer
   * @return True if successful
   */
  bool GetUserPreferencesMask(BYTE* maskBytes, DWORD& maskSize);

  /**
   * @brief Apply recommended performance settings
   *
   * @return True if successful
   */
  bool ApplyRecommendedSettings();

  /**
   * @brief Set a registry DWORD value
   *
   * @param keyPath Registry key path
   * @param valueName Name of the value
   * @param value Value to set
   * @return True if successful
   */
  bool SetRegistryDWORD(const std::string& keyPath,
                        const std::string& valueName, DWORD value);

  /**
   * @brief Set a registry string value
   *
   * @param keyPath Registry key path
   * @param valueName Name of the value
   * @param value Value to set
   * @return True if successful
   */
  bool SetRegistryString(const std::string& keyPath,
                         const std::string& valueName,
                         const std::string& value);

  /**
   * @brief Get a registry DWORD value
   *
   * @param keyPath Registry key path
   * @param valueName Name of the value
   * @param value Buffer to store the value
   * @return True if successful
   */
  bool GetRegistryDWORD(const std::string& keyPath,
                        const std::string& valueName, DWORD& value);

  /**
   * @brief Notify system of settings change
   *
   * @return True if successful
   */
  bool NotifySettingsChange();

  // Initialization flag
  bool is_initialized_ = false;
};

}  // namespace visual_effects
}  // namespace optimizations
