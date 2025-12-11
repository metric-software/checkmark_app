/**
 * @file NvidiaControlPanel.h
 * @brief Interface for NVIDIA GPU driver settings through the NVAPI
 *
 * This class manages NVIDIA driver settings through the NVAPI SDK.
 * It provides methods to detect NVIDIA GPUs, apply optimizations, and
 * create OptimizationEntity objects for the checkmark application.
 */

#pragma once

// NvidiaControlPanel loads NVAPI libraries to interact with NVIDIA Control
// Panel 3D settings. It maintains a predefined list of settings, checks for the
// presence of an NVIDIA GPU, and provides methods to apply, restore, and
// retrieve settings. It should use the official definitions from the NVAPI
// headers. And avoid defining its own enums.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Windows.h>

// Include NVAPI headers
#include "nvapi.h"
#include "NvApiDriverSettings.h"

// Include OptimizationEntity.h
#include "OptimizationEntity.h"

namespace optimizations {
namespace nvidia {

// Forward declaration for NvidiaOptimization
class NvidiaOptimization;

// Forward declaration
std::string GetWindowsVersionString();

/**
 * @brief Information about a configurable NVIDIA driver setting
 */
struct NvidiaSettingInfo {
  std::string id;            // Unique identifier for this setting
  std::string name;          // Display name
  std::string description;   // Description of what this setting does
  std::string category;      // Category (e.g., "3D Settings")
  bool personal_preference;  // Is this mostly a matter of preference?
  int recommended_value;     // Our recommended value
  int default_value;         // Default NVIDIA value
};

// Function pointer types for later addition to NvidiaControlPanel.cpp
typedef NvAPI_Status (*NvAPI_DRS_GetCurrentGlobalProfile_t)(
  NvDRSSessionHandle, NvDRSProfileHandle*);

// Additional variables for NvidiaControlPanel.cpp
extern NvAPI_DRS_GetCurrentGlobalProfile_t NvAPI_DRS_GetCurrentGlobalProfile;

/**
 * @brief Singleton class to manage NVIDIA driver settings
 *
 * This class provides methods to check for NVIDIA GPUs, apply
 * performance optimizations, and create OptimizationEntity objects.
 */
class NvidiaControlPanel {
 public:
  /**
   * @brief Get the singleton instance
   * @return Reference to the NvidiaControlPanel singleton
   */
  static NvidiaControlPanel& GetInstance();

  /**
   * @brief Destructor - cleans up NVIDIA API resources
   */
  ~NvidiaControlPanel();

  /**
   * @brief Check if an NVIDIA GPU is present
   * @return True if an NVIDIA GPU is detected
   */
  bool HasNvidiaGPU() const { return has_nvidia_gpu; }

  /**
   * @brief Initialize the NVIDIA API
   * @return True if initialization was successful
   */
  bool Initialize();

  /**
   * @brief Check if the NVIDIA API is initialized
   * @return True if initialized
   */
  bool IsInitialized() const { return initialized; }

  /**
   * @brief Get NVIDIA API version information
   * @return String containing NVIDIA version info
   */
  std::string GetNvidiaVersionInfo();

  //-----------------------------------------------------------------------
  // VSYNC Settings
  //-----------------------------------------------------------------------

  /**
   * @brief Apply a VSYNC setting value
   * @param value The VSYNC mode value to apply
   * @return True if successful
   */
  bool ApplyVSyncSetting(int value);

  /**
   * @brief Get the current VSYNC setting value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetVSyncSettingValue(int& value);

  /**
   * @brief Restore VSYNC setting to default
   * @return True if successful
   */
  bool RestoreVSyncSetting();

  //-----------------------------------------------------------------------
  // Power Management Settings
  //-----------------------------------------------------------------------

  /**
   * @brief Apply Power Management Mode
   * @param value The power mode value to apply
   * @return True if successful
   */
  bool ApplyPowerManagementMode(int value);

  /**
   * @brief Get the current Power Management Mode value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetPowerManagementModeValue(int& value);

  /**
   * @brief Set Maximum Performance mode
   * @return True if successful
   */
  bool SetMaxPerformanceMode() {
    return ApplyPowerManagementMode(PREFERRED_PSTATE_PREFER_MAX);
  }

  //-----------------------------------------------------------------------
  // Anisotropic Filtering Settings
  //-----------------------------------------------------------------------

  /**
   * @brief Apply Anisotropic Filtering Mode Selector
   * @param value The anisotropic mode selector value
   * @return True if successful
   */
  bool ApplyAnisoModeSelector(int value);

  /**
   * @brief Get the current Anisotropic Filtering Mode Selector value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetAnisoModeSelectorValue(int& value);

  /**
   * @brief Apply Anisotropic Filtering Level
   * @param value The anisotropic level value
   * @return True if successful
   */
  bool ApplyAnisoLevel(int value);

  /**
   * @brief Get the current Anisotropic Filtering Level value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetAnisoLevelValue(int& value);

  /**
   * @brief Restore Anisotropic Filtering to default
   * @return True if successful
   */
  bool RestoreAnisoSettings();

  /**
   * @brief Simplified interface for Anisotropic Filtering
   * @param enabled Whether to force off (true) or use application control
   * (false)
   * @return True if successful
   */
  bool SetAnisotropicFiltering(bool enabled);

  /**
   * @brief Get current state of Anisotropic Filtering (simplified)
   * @param enabled Reference to store enabled state
   * @return True if successful
   */
  bool GetAnisotropicFilteringEnabled(bool& enabled);

  //-----------------------------------------------------------------------
  // Antialiasing Settings
  //-----------------------------------------------------------------------

  /**
   * @brief Apply Antialiasing Mode Selector
   * @param value The AA mode selector value
   * @return True if successful
   */
  bool ApplyAAModeSelector(int value);

  /**
   * @brief Get the current Antialiasing Mode Selector value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetAAModeSelectorValue(int& value);

  /**
   * @brief Apply Antialiasing Method
   * @param value The AA method value
   * @return True if successful
   */
  bool ApplyAAMethod(int value);

  /**
   * @brief Get the current Antialiasing Method value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetAAMethodValue(int& value);

  /**
   * @brief Restore Antialiasing to default
   * @return True if successful
   */
  bool RestoreAASettings();

  /**
   * @brief Simplified interface for Antialiasing
   * @param enabled Whether to force off (true) or use application control
   * (false)
   * @return True if successful
   */
  bool SetAntialiasing(bool enabled);

  /**
   * @brief Get current state of Antialiasing (simplified)
   * @param enabled Reference to store enabled state
   * @return True if successful
   */
  bool GetAntialiasingEnabled(bool& enabled);

  //-----------------------------------------------------------------------
  // Display Settings
  //-----------------------------------------------------------------------

  /**
   * @brief Apply Monitor Technology setting (G-SYNC/Fixed Refresh)
   * @param value The monitor technology value
   * @return True if successful
   */
  bool ApplyMonitorTechnology(int value);

  /**
   * @brief Get the current Monitor Technology value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetMonitorTechnologyValue(int& value);

  /**
   * @brief Restore Monitor Technology to default
   * @return True if successful
   */
  bool RestoreMonitorTechnology();

  /**
   * @brief Apply Preferred Refresh Rate setting
   * @param value The refresh rate value
   * @return True if successful
   */
  bool ApplyPreferredRefreshRate(int value);

  /**
   * @brief Get the current Preferred Refresh Rate value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetPreferredRefreshRateValue(int& value);

  /**
   * @brief Restore Preferred Refresh Rate to default
   * @return True if successful
   */
  bool RestorePreferredRefreshRate();

  //-----------------------------------------------------------------------
  // OpenGL Settings
  //-----------------------------------------------------------------------

  /**
   * @brief Apply OpenGL GDI Compatibility setting
   * @param value The GDI compatibility value
   * @return True if successful
   */
  bool ApplyGDICompatibility(int value);

  /**
   * @brief Get the current OpenGL GDI Compatibility value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetGDICompatibilityValue(int& value);

  /**
   * @brief Restore OpenGL GDI Compatibility to default
   * @return True if successful
   */
  bool RestoreGDICompatibility();

  /**
   * @brief Apply Threaded Optimization setting
   * @param value The threaded optimization value
   * @return True if successful
   */
  bool ApplyThreadedOptimization(int value);

  /**
   * @brief Get the current Threaded Optimization value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetThreadedOptimizationValue(int& value);

  /**
   * @brief Restore Threaded Optimization to default
   * @return True if successful
   */
  bool RestoreThreadedOptimization();

  //-----------------------------------------------------------------------
  // Texture Filtering Settings
  //-----------------------------------------------------------------------

  /**
   * @brief Apply Texture Filtering Quality setting
   * @param value The texture filtering quality value
   * @return True if successful
   */
  bool ApplyTextureFilteringQuality(int value);

  /**
   * @brief Get the current Texture Filtering Quality value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetTextureFilteringQualityValue(int& value);

  /**
   * @brief Restore Texture Filtering Quality to default
   * @return True if successful
   */
  bool RestoreTextureFilteringQuality();

  /**
   * @brief Apply Anisotropic Sample Optimization setting
   * @param value The sample optimization value
   * @return True if successful
   */
  bool ApplyAnisoSampleOpt(int value);

  /**
   * @brief Get the current Anisotropic Sample Optimization value
   * @param value Reference to store the current value
   * @return True if successful
   */
  bool GetAnisoSampleOptValue(int& value);

  /**
   * @brief Restore Anisotropic Sample Optimization to default
   * @return True if successful
   */
  bool RestoreAnisoSampleOpt();

  //-----------------------------------------------------------------------
  // General Settings
  //-----------------------------------------------------------------------

  /**
   * @brief Refresh all settings from the driver
   * @return True if successful
   */
  bool RefreshSettings();

  /**
   * @brief Create NVIDIA Optimizations entities
   * @return Vector of OptimizationEntity objects
   */
  std::vector<std::unique_ptr<optimizations::settings::OptimizationEntity>> CreateNvidiaOptimizations();

 private:
  // Private constructor for singleton
  NvidiaControlPanel();

  // No copy/move allowed
  NvidiaControlPanel(const NvidiaControlPanel&) = delete;
  NvidiaControlPanel& operator=(const NvidiaControlPanel&) = delete;
  NvidiaControlPanel(NvidiaControlPanel&&) = delete;
  NvidiaControlPanel& operator=(NvidiaControlPanel&&) = delete;

  // Implementation of HasNvidiaGPU
  bool HasNvidiaGPUImpl();

  // Flag indicating if an NVIDIA GPU is present
  bool has_nvidia_gpu;

  // Flag indicating if we've been initialized
  bool initialized;

  // NVAPI session handle
  NvDRSSessionHandle session_handle;

  // Base profile handle
  NvDRSProfileHandle base_profile_handle;

  // Current session information
  struct {
    int vsync_mode;
    int power_management_mode;
  } session_settings;
};

}  // namespace nvidia
}  // namespace optimizations
