#include "NvidiaControlPanel.h"

#include <memory>
#include <string>

#include <Windows.h>

#include "BackupManager.h"
#include "NvidiaOptimization.h"
#include "logging/Logger.h"

// Include NVIDIA API headers
#include "nvapi.h"
#include "NvApiDriverSettings.h"

namespace optimizations {
namespace nvidia {

// Function to get error string for NvAPI errors
std::string GetNvAPIErrorString(NvAPI_Status status) {
  NvAPI_ShortString szDesc = {0};
  NvAPI_GetErrorMessage(status, szDesc);
  return std::string(szDesc);
}

NvidiaControlPanel::NvidiaControlPanel()
    : initialized(false), has_nvidia_gpu(false), session_handle(nullptr),
      base_profile_handle(nullptr) {

  // Check if NVIDIA GPU is present
  has_nvidia_gpu = HasNvidiaGPUImpl();

  // Initialize session settings
  session_settings.vsync_mode = VSYNCMODE_PASSIVE;
  session_settings.power_management_mode = PREFERRED_PSTATE_OPTIMAL_POWER;
}

NvidiaControlPanel::~NvidiaControlPanel() {
  if (initialized) {
    // Clean up NVIDIA driver settings
    if (session_handle) {
      NvAPI_DRS_DestroySession(static_cast<NvDRSSessionHandle>(session_handle));
      session_handle = nullptr;
      base_profile_handle = nullptr;
    }

    // Unload the NVAPI
    NvAPI_Unload();
  }
}

bool NvidiaControlPanel::Initialize() {
  if (initialized) return true;

  if (!has_nvidia_gpu) {
    LOG_ERROR << "NvidiaControlPanel: Initialize failed - No NVIDIA GPU detected";
    return false;
  }

  // (0) Initialize NVAPI
  NvAPI_Status status = NvAPI_Initialize();
  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: Initialize failed - Failed to initialize NVAPI: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // (1) Create the session handle to access driver settings
  NvDRSSessionHandle hSession = nullptr;
  status = NvAPI_DRS_CreateSession(&hSession);
  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: Initialize failed - Failed to create DRS session: "
      << GetNvAPIErrorString(status);
    NvAPI_Unload();
    return false;
  }
  session_handle = hSession;

  // (2) Load all the system settings into the session
  status = NvAPI_DRS_LoadSettings(hSession);
  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: Initialize failed - Failed to load DRS settings: "
      << GetNvAPIErrorString(status);
    NvAPI_DRS_DestroySession(hSession);
    session_handle = nullptr;
    NvAPI_Unload();
    return false;
  }

  // (3) Obtain the Base profile
  NvDRSProfileHandle hProfile = nullptr;
  status = NvAPI_DRS_GetBaseProfile(hSession, &hProfile);
  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: Initialize failed - Failed to get base profile: "
              << GetNvAPIErrorString(status);
    NvAPI_DRS_DestroySession(hSession);
    session_handle = nullptr;
    NvAPI_Unload();
    return false;
  }
  base_profile_handle = hProfile;

  initialized = true;
  return true;
}

bool NvidiaControlPanel::HasNvidiaGPUImpl() {
  // Initialize NVAPI
  NvAPI_Status status = NvAPI_Initialize();
  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: HasNvidiaGPUImpl failed - Failed to initialize NVAPI: "
      << GetNvAPIErrorString(status);
    return false;
  }

  // Enumerate GPUs
  NvPhysicalGpuHandle gpus[NVAPI_MAX_PHYSICAL_GPUS] = {nullptr};
  NvU32 gpu_count = 0;

  status = NvAPI_EnumPhysicalGPUs(gpus, &gpu_count);
  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: HasNvidiaGPUImpl failed - Failed to enumerate GPUs: "
      << GetNvAPIErrorString(status);
    NvAPI_Unload();
    return false;
  }

  // Unload NVAPI for now - we'll initialize it properly when needed
  NvAPI_Unload();

  return (gpu_count > 0);
}

std::string NvidiaControlPanel::GetNvidiaVersionInfo() {
  std::string versionInfo;

  // Initialize NVAPI if needed
  if (!initialized && has_nvidia_gpu) {
    Initialize();
  }

  if (initialized) {
    NvAPI_ShortString version = {0};
    NvAPI_Status status = NvAPI_GetInterfaceVersionString(version);
    if (status == NVAPI_OK) {
      versionInfo = "NVAPI Version: ";
      versionInfo += version;
    } else {
      versionInfo = "Failed to get NVAPI version: ";
      versionInfo += GetNvAPIErrorString(status);
    }
  } else {
    versionInfo = "NVAPI not initialized";
  }

  return versionInfo;
}

bool NvidiaControlPanel::ApplyVSyncSetting(int value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId =
    VSYNCMODE_ID;  // VSYNC setting ID from NvApiDriverSettings.h
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: ApplyVSyncSetting failed - Failed to set VSYNC setting: "
      << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: ApplyVSyncSetting failed - Failed to save settings: "
      << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetVSyncSettingValue(int& value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         VSYNCMODE_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetVSyncSettingValue failed - Failed to get "
                 "VSYNC setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetVSyncSettingValue failed - VSYNC setting is "
                 "not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::RestoreVSyncSetting() {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // VSYNCMODE_DEFAULT is defined in NvApiDriverSettings.h as VSYNCMODE_PASSIVE
  return ApplyVSyncSetting(VSYNCMODE_PASSIVE);
}

bool NvidiaControlPanel::ApplyPowerManagementMode(int value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId = PREFERRED_PSTATE_ID;  // Power Management Mode setting
                                              // ID from NvApiDriverSettings.h
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyPowerManagementMode failed - Failed to set "
                 "power mode: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyPowerManagementMode failed - Failed to "
                 "save settings: "
              << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetPowerManagementModeValue(int& value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         PREFERRED_PSTATE_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetPowerManagementModeValue failed - Failed to "
                 "get power mode setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetPowerManagementModeValue failed - Power mode "
                 "setting is not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::ApplyAnisoModeSelector(int value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId = ANISO_MODE_SELECTOR_ID;  // Anisotropic Mode Selector ID
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyAnisoModeSelector failed - Failed to set "
                 "Anisotropic Mode Selector setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyAnisoModeSelector failed - Failed to save "
                 "settings: "
              << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetAnisoModeSelectorValue(int& value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         ANISO_MODE_SELECTOR_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetAnisoModeSelectorValue failed - Failed to "
                 "get Anisotropic Mode Selector setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetAnisoModeSelectorValue failed - Anisotropic "
                 "Mode Selector setting is not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::ApplyAnisoLevel(int value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId = ANISO_MODE_LEVEL_ID;  // Anisotropic Level ID
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyAnisoLevel failed - Failed to set "
                 "Anisotropic Level setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: ApplyAnisoLevel failed - Failed to save settings: "
      << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetAnisoLevelValue(int& value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         ANISO_MODE_LEVEL_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetAnisoLevelValue failed - Failed to get "
                 "Anisotropic Level setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetAnisoLevelValue failed - Anisotropic Level "
                 "setting is not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::RestoreAnisoSettings() {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Only restore the selector to default (application-controlled) and don't
  // touch the level
  return ApplyAnisoModeSelector(ANISO_MODE_SELECTOR_APP);
}

bool NvidiaControlPanel::ApplyAAModeSelector(int value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId = AA_MODE_SELECTOR_ID;  // AA Mode Selector ID
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyAAModeSelector failed - Failed to set AA "
                 "Mode Selector setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: ApplyAAModeSelector failed - Failed to save settings: "
      << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetAAModeSelectorValue(int& value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         AA_MODE_SELECTOR_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetAAModeSelectorValue failed - Failed to get "
                 "AA Mode Selector setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetAAModeSelectorValue failed - AA Mode "
                 "Selector setting is not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::RestoreAASettings() {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Only restore the selector to default (application-controlled) and don't
  // touch the method
  return ApplyAAModeSelector(AA_MODE_SELECTOR_APP_CONTROL);
}

bool NvidiaControlPanel::ApplyAAMethod(int value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId = AA_MODE_METHOD_ID;  // AA Method ID
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: ApplyAAMethod failed - Failed to set AA Method setting: "
      << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyAAMethod failed - Failed to save settings: "
              << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetAAMethodValue(int& value) {
  if (!has_nvidia_gpu) {
    return false;
  }

  if (!initialized && !Initialize()) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         AA_MODE_METHOD_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetAAMethodValue failed - Failed to get AA "
                 "Method setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetAAMethodValue failed - AA Method setting is "
                 "not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::SetAnisotropicFiltering(bool enabled) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  if (enabled) {
    // Enable override but set to OFF (NONE/POINT)
    bool selectorResult = ApplyAnisoModeSelector(ANISO_MODE_SELECTOR_USER);
    bool levelResult =
      ApplyAnisoLevel(ANISO_MODE_LEVEL_NONE_POINT);  // Force OFF
    return selectorResult && levelResult;
  } else {
    // Disable by setting to application-controlled (default)
    return RestoreAnisoSettings();
  }
}

bool NvidiaControlPanel::GetAnisotropicFilteringEnabled(bool& enabled) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    enabled = false;
    return false;
  }

  int selectorValue = ANISO_MODE_SELECTOR_APP;
  if (!GetAnisoModeSelectorValue(selectorValue)) {
    enabled = false;
    return false;
  }

  // Consider it enabled if it's in USER mode (overriding app settings)
  enabled = (selectorValue == ANISO_MODE_SELECTOR_USER);
  return true;
}

bool NvidiaControlPanel::SetAntialiasing(bool enabled) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  if (enabled) {
    // Enable override but set to OFF
    bool selectorResult = ApplyAAModeSelector(AA_MODE_SELECTOR_OVERRIDE);
    bool methodResult = ApplyAAMethod(AA_MODE_METHOD_NONE);  // Force OFF
    return selectorResult && methodResult;
  } else {
    // Disable by setting to application-controlled (default)
    return RestoreAASettings();
  }
}

bool NvidiaControlPanel::GetAntialiasingEnabled(bool& enabled) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    enabled = false;
    return false;
  }

  int selectorValue = AA_MODE_SELECTOR_APP_CONTROL;
  if (!GetAAModeSelectorValue(selectorValue)) {
    enabled = false;
    return false;
  }

  // Consider it enabled if it's in OVERRIDE or ENHANCE mode
  enabled = (selectorValue == AA_MODE_SELECTOR_OVERRIDE ||
             selectorValue == AA_MODE_SELECTOR_ENHANCE);
  return true;
}

std::vector<std::unique_ptr<optimizations::settings::OptimizationEntity>> NvidiaControlPanel::
  CreateNvidiaOptimizations() {
  std::vector<std::unique_ptr<optimizations::settings::OptimizationEntity>>
    optimizations;

  if (!has_nvidia_gpu) {
    return optimizations;
  }

  // Initialize the API if it's not already initialized
  if (!initialized && !Initialize()) {
    LOG_ERROR << "NvidiaControlPanel: CreateNvidiaOptimizations failed - Failed to initialize NVIDIA API";
    return optimizations;
  }

  // Create VSYNC optimization
  int currentVSyncValue = VSYNCMODE_PASSIVE;
  GetVSyncSettingValue(currentVSyncValue);

  auto vsyncOptimization = std::make_unique<nvidia::NvidiaOptimization>(
    "nvidia_vsync", "Vertical Sync",
    "Controls synchronization between GPU frame rate and display refresh rate. "
    "Force off for higher performance, force on to reduce screen tearing.",
    currentVSyncValue,
    VSYNCMODE_FORCEOFF,  // Recommended value - disable VSYNC for best
                         // performance
    VSYNCMODE_PASSIVE,   // Default value - application controlled
    "3D Settings",
    true  // Personal preference
  );

  // Add VSYNC possible values
  vsyncOptimization->AddValueOption(VSYNCMODE_PASSIVE,
                                    "Application-controlled");
  vsyncOptimization->AddValueOption(VSYNCMODE_FORCEON, "Force on");
  vsyncOptimization->AddValueOption(VSYNCMODE_FORCEOFF, "Force off");

  // Add it to the list
  optimizations.push_back(std::move(vsyncOptimization));

  // Create Power Management Mode optimization
  int currentPowerValue = PREFERRED_PSTATE_OPTIMAL_POWER;
  GetPowerManagementModeValue(currentPowerValue);

  auto powerOptimization = std::make_unique<nvidia::NvidiaOptimization>(
    "nvidia_power_mode", "Power Management Mode",
    "Controls GPU power management. Set to 'Prefer maximum performance' for "
    "best gaming performance, 'Optimal power' for energy efficiency.",
    currentPowerValue,
    PREFERRED_PSTATE_PREFER_MAX,     // Recommended value - max performance
    PREFERRED_PSTATE_OPTIMAL_POWER,  // Default value - optimal power
    "3D Settings",
    false  // Not personal preference, performance vs power tradeoff
  );

  // Add Power Management Mode possible values
  powerOptimization->AddValueOption(PREFERRED_PSTATE_OPTIMAL_POWER,
                                    "Optimal power");
  powerOptimization->AddValueOption(PREFERRED_PSTATE_PREFER_MAX,
                                    "Prefer maximum performance");
  powerOptimization->AddValueOption(PREFERRED_PSTATE_ADAPTIVE, "Adaptive");

  // Add it to the list
  optimizations.push_back(std::move(powerOptimization));

  // Create simplified Anisotropic Filtering optimization
  bool anisoEnabled = false;
  GetAnisotropicFilteringEnabled(anisoEnabled);

  auto anisoOptimization = std::make_unique<nvidia::NvidiaOptimization>(
    "nvidia_aniso_filtering", "Anisotropic Filtering",
    "Improves texture quality at oblique angles. When enabled, this option "
    "overrides any application setting to force anisotropic filtering OFF for "
    "maximum performance.",
    anisoEnabled ? 1 : 0,  // Convert bool to int
    1,  // Recommended value - force OFF for maximum performance
    0,  // Default value - application-controlled
    "3D Settings",
    false  // Not entirely personal preference, impacts performance
  );

  // Add simplified Anisotropic Filtering possible values
  anisoOptimization->AddValueOption(0, "Application-controlled");
  anisoOptimization->AddValueOption(1, "Override - Force OFF");

  // Add it to the list
  optimizations.push_back(std::move(anisoOptimization));

  // Create simplified Antialiasing optimization
  bool aaEnabled = false;
  GetAntialiasingEnabled(aaEnabled);

  auto aaOptimization = std::make_unique<nvidia::NvidiaOptimization>(
    "nvidia_antialiasing", "Antialiasing",
    "Reduces jagged edges on 3D objects. When enabled, this option overrides "
    "any application setting to force antialiasing OFF for maximum "
    "performance.",
    aaEnabled ? 1 : 0,  // Convert bool to int
    1,                  // Recommended value - force OFF for maximum performance
    0,                  // Default value - application-controlled
    "3D Settings",
    false  // Now more of a performance consideration
  );

  // Add simplified Antialiasing possible values
  aaOptimization->AddValueOption(0, "Application-controlled");
  aaOptimization->AddValueOption(1, "Override - Force OFF");

  // Add it to the list
  optimizations.push_back(std::move(aaOptimization));

  // Create Monitor Technology optimization
  int currentMonitorTechValue = VRR_APP_OVERRIDE_ALLOW;
  GetMonitorTechnologyValue(currentMonitorTechValue);

  auto monitorTechOptimization = std::make_unique<nvidia::NvidiaOptimization>(
    "nvidia_monitor_tech", "Monitor Technology",
    "Controls G-SYNC/VRR usage. Set to 'Fixed Refresh' for consistent frame "
    "pacing without G-SYNC/VRR, which can reduce input latency in competitive "
    "games.",
    currentMonitorTechValue,
    VRR_APP_OVERRIDE_FIXED_REFRESH,  // Recommended value - Fixed Refresh for
                                     // consistent frame pacing
    VRR_APP_OVERRIDE_ALLOW,          // Default value - Allow G-SYNC if enabled
    "3D Settings",
    true  // Personal preference
  );

  // Add Monitor Technology possible values
  monitorTechOptimization->AddValueOption(VRR_APP_OVERRIDE_ALLOW,
                                          "Application-controlled");
  monitorTechOptimization->AddValueOption(VRR_APP_OVERRIDE_FIXED_REFRESH,
                                          "Fixed Refresh");

  // Add it to the list
  optimizations.push_back(std::move(monitorTechOptimization));

  // Create OpenGL GDI Compatibility optimization
  int currentGDIValue = OGL_CPL_GDI_COMPATIBILITY_AUTO;
  GetGDICompatibilityValue(currentGDIValue);

  auto gdiOptimization = std::make_unique<nvidia::NvidiaOptimization>(
    "nvidia_gdi_compat", "OpenGL GDI Compatibility",
    "Controls OpenGL compatibility with Windows GDI. Set to 'Prefer Disabled' "
    "for better OpenGL performance in most applications.",
    currentGDIValue,
    OGL_CPL_GDI_COMPATIBILITY_PREFER_DISABLED,  // Recommended value - Prefer
                                                // Disabled for better
                                                // performance
    OGL_CPL_GDI_COMPATIBILITY_AUTO,             // Default value - Auto
    "3D Settings",
    false  // Not personal preference
  );

  // Add OpenGL GDI Compatibility possible values
  gdiOptimization->AddValueOption(OGL_CPL_GDI_COMPATIBILITY_AUTO, "Auto");
  gdiOptimization->AddValueOption(OGL_CPL_GDI_COMPATIBILITY_PREFER_DISABLED,
                                  "Prefer Disabled");

  // Add it to the list
  optimizations.push_back(std::move(gdiOptimization));

  // Create Preferred Refresh Rate optimization
  int currentRefreshValue = REFRESH_RATE_OVERRIDE_APPLICATION_CONTROLLED;
  GetPreferredRefreshRateValue(currentRefreshValue);

  auto refreshOptimization = std::make_unique<nvidia::NvidiaOptimization>(
    "nvidia_refresh_rate", "Preferred Refresh Rate",
    "Controls which refresh rate to use for full-screen applications. Set to "
    "'Highest Available' to always use your monitor's maximum refresh rate for "
    "smoother gameplay.",
    currentRefreshValue,
    REFRESH_RATE_OVERRIDE_HIGHEST_AVAILABLE,  // Recommended value - Highest
                                              // Available for smoothest
                                              // experience
    REFRESH_RATE_OVERRIDE_APPLICATION_CONTROLLED,  // Default value -
                                                   // Application Controlled
    "3D Settings",
    false  // Not personal preference
  );

  // Add Preferred Refresh Rate possible values
  refreshOptimization->AddValueOption(
    REFRESH_RATE_OVERRIDE_APPLICATION_CONTROLLED, "Application-controlled");
  refreshOptimization->AddValueOption(REFRESH_RATE_OVERRIDE_HIGHEST_AVAILABLE,
                                      "Highest Available");

  // Add it to the list
  optimizations.push_back(std::move(refreshOptimization));

  // Create Texture Filtering Quality optimization
  int currentQualityValue = QUALITY_ENHANCEMENTS_QUALITY;
  GetTextureFilteringQualityValue(currentQualityValue);

  auto textureQualityOptimization =
    std::make_unique<nvidia::NvidiaOptimization>(
      "nvidia_texture_quality", "Texture Filtering - Quality",
      "Controls the quality vs. performance balance of texture filtering. Set "
      "to 'High Performance' for maximum performance in competitive games.",
      currentQualityValue,
      QUALITY_ENHANCEMENTS_HIGHPERFORMANCE,  // Recommended value - High
                                             // Performance
      QUALITY_ENHANCEMENTS_QUALITY,          // Default value - Quality
      "3D Settings",
      false  // Not personal preference
    );

  // Add Texture Filtering Quality possible values
  textureQualityOptimization->AddValueOption(QUALITY_ENHANCEMENTS_HIGHQUALITY,
                                             "High Quality");
  textureQualityOptimization->AddValueOption(QUALITY_ENHANCEMENTS_QUALITY,
                                             "Quality");
  textureQualityOptimization->AddValueOption(QUALITY_ENHANCEMENTS_PERFORMANCE,
                                             "Performance");
  textureQualityOptimization->AddValueOption(
    QUALITY_ENHANCEMENTS_HIGHPERFORMANCE, "High Performance");

  // Add it to the list
  optimizations.push_back(std::move(textureQualityOptimization));

  // Create Anisotropic Sample Optimization
  int currentAnisoOptValue = PS_TEXFILTER_ANISO_OPTS2_OFF;
  GetAnisoSampleOptValue(currentAnisoOptValue);

  auto anisoOptOptimization = std::make_unique<nvidia::NvidiaOptimization>(
    "nvidia_aniso_sample_opt",
    "Texture Filtering - Anisotropic Sample Optimization",
    "Optimizes performance when using anisotropic filtering. Enable for better "
    "performance with minimal quality impact when anisotropic filtering is "
    "active.",
    currentAnisoOptValue,
    PS_TEXFILTER_ANISO_OPTS2_ON,   // Recommended value - On for better
                                   // performance
    PS_TEXFILTER_ANISO_OPTS2_OFF,  // Default value - Off
    "3D Settings",
    false  // Not personal preference
  );

  // Add Anisotropic Sample Optimization possible values
  anisoOptOptimization->AddValueOption(PS_TEXFILTER_ANISO_OPTS2_OFF, "Off");
  anisoOptOptimization->AddValueOption(PS_TEXFILTER_ANISO_OPTS2_ON, "On");

  // Add it to the list
  optimizations.push_back(std::move(anisoOptOptimization));

  // Create Threaded Optimization
  int currentThreadOptValue = 0;  // Default is 0 (Auto)
  GetThreadedOptimizationValue(currentThreadOptValue);

  auto threadOptimization = std::make_unique<nvidia::NvidiaOptimization>(
    "nvidia_threaded_opt", "Threaded Optimization",
    "Controls multi-threaded optimization for OpenGL applications. Enable for "
    "better performance in multi-threaded applications.",
    currentThreadOptValue,
    OGL_THREAD_CONTROL_ENABLE,  // Recommended value - On for better performance
    0,                          // Default value - Auto (0)
    "3D Settings",
    false  // Not personal preference
  );

  // Add Threaded Optimization possible values
  threadOptimization->AddValueOption(0, "Auto");
  threadOptimization->AddValueOption(OGL_THREAD_CONTROL_ENABLE, "On");

  // Add it to the list
  optimizations.push_back(std::move(threadOptimization));

  return optimizations;
}

NvidiaControlPanel& NvidiaControlPanel::GetInstance() {
  static NvidiaControlPanel instance;
  return instance;
}

bool NvidiaControlPanel::RefreshSettings() {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Reload settings from the driver by calling LoadSettings again
  NvAPI_Status status =
    NvAPI_DRS_LoadSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: RefreshSettings failed - Failed to reload settings: "
      << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::ApplyMonitorTechnology(int value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId = VRR_APP_OVERRIDE_ID;  // Monitor Technology setting ID
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyMonitorTechnology failed - Failed to set "
                 "Monitor Technology setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyMonitorTechnology failed - Failed to save "
                 "settings: "
              << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetMonitorTechnologyValue(int& value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         VRR_APP_OVERRIDE_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetMonitorTechnologyValue failed - Failed to "
                 "get Monitor Technology setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetMonitorTechnologyValue failed - Monitor "
                 "Technology setting is not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::RestoreMonitorTechnology() {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  return ApplyMonitorTechnology(VRR_APP_OVERRIDE_DEFAULT);
}

bool NvidiaControlPanel::ApplyGDICompatibility(int value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId =
    OGL_CPL_GDI_COMPATIBILITY_ID;  // OpenGL GDI Compatibility setting ID
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyGDICompatibility failed - Failed to set "
                 "OpenGL GDI Compatibility setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: ApplyGDICompatibility failed - Failed to save settings: "
      << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetGDICompatibilityValue(int& value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         OGL_CPL_GDI_COMPATIBILITY_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetGDICompatibilityValue failed - Failed to get "
                 "OpenGL GDI Compatibility setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetGDICompatibilityValue failed - OpenGL GDI "
                 "Compatibility setting is not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::RestoreGDICompatibility() {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  return ApplyGDICompatibility(OGL_CPL_GDI_COMPATIBILITY_DEFAULT);
}

bool NvidiaControlPanel::ApplyPreferredRefreshRate(int value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId =
    REFRESH_RATE_OVERRIDE_ID;  // Preferred Refresh Rate setting ID
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyPreferredRefreshRate failed - Failed to "
                 "set Preferred Refresh Rate setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyPreferredRefreshRate failed - Failed to "
                 "save settings: "
              << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetPreferredRefreshRateValue(int& value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         REFRESH_RATE_OVERRIDE_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetPreferredRefreshRateValue failed - Failed to "
                 "get Preferred Refresh Rate setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetPreferredRefreshRateValue failed - Preferred "
                 "Refresh Rate setting is not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::RestorePreferredRefreshRate() {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  return ApplyPreferredRefreshRate(
    REFRESH_RATE_OVERRIDE_APPLICATION_CONTROLLED);
}

bool NvidiaControlPanel::ApplyTextureFilteringQuality(int value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId =
    QUALITY_ENHANCEMENTS_ID;  // Texture Filtering Quality setting ID
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyTextureFilteringQuality failed - Failed to "
                 "set Texture Filtering Quality setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyTextureFilteringQuality failed - Failed to "
                 "save settings: "
              << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetTextureFilteringQualityValue(int& value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         QUALITY_ENHANCEMENTS_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetTextureFilteringQualityValue failed - Failed "
                 "to get Texture Filtering Quality setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetTextureFilteringQualityValue failed - "
                 "Texture Filtering Quality setting is not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::RestoreTextureFilteringQuality() {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  return ApplyTextureFilteringQuality(QUALITY_ENHANCEMENTS_DEFAULT);
}

bool NvidiaControlPanel::ApplyAnisoSampleOpt(int value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId =
    PS_TEXFILTER_ANISO_OPTS2_ID;  // Anisotropic Sample Optimization setting ID
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyAnisoSampleOpt failed - Failed to set "
                 "Anisotropic Sample Optimization setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR
      << "NvidiaControlPanel: ApplyAnisoSampleOpt failed - Failed to save settings: "
      << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetAnisoSampleOptValue(int& value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         PS_TEXFILTER_ANISO_OPTS2_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetAnisoSampleOptValue failed - Failed to get "
                 "Anisotropic Sample Optimization setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetAnisoSampleOptValue failed - Anisotropic "
                 "Sample Optimization setting is not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::RestoreAnisoSampleOpt() {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  return ApplyAnisoSampleOpt(PS_TEXFILTER_ANISO_OPTS2_DEFAULT);
}

bool NvidiaControlPanel::ApplyThreadedOptimization(int value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;
  nvSetting.settingId =
    OGL_THREAD_CONTROL_ID;  // Threaded Optimization setting ID
  nvSetting.settingType = NVDRS_DWORD_TYPE;
  nvSetting.u32CurrentValue = value;

  // Apply the setting
  NvAPI_Status status = NvAPI_DRS_SetSetting(
    static_cast<NvDRSSessionHandle>(session_handle),
    static_cast<NvDRSProfileHandle>(base_profile_handle), &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyThreadedOptimization failed - Failed to "
                 "set Threaded Optimization setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Save the settings
  status =
    NvAPI_DRS_SaveSettings(static_cast<NvDRSSessionHandle>(session_handle));
  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: ApplyThreadedOptimization failed - Failed to "
                 "save settings: "
              << GetNvAPIErrorString(status);
    return false;
  }

  return true;
}

bool NvidiaControlPanel::GetThreadedOptimizationValue(int& value) {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  // Create the setting structure
  NVDRS_SETTING nvSetting = {0};
  nvSetting.version = NVDRS_SETTING_VER;

  // Get the setting
  NvAPI_Status status =
    NvAPI_DRS_GetSetting(static_cast<NvDRSSessionHandle>(session_handle),
                         static_cast<NvDRSProfileHandle>(base_profile_handle),
                         OGL_THREAD_CONTROL_ID, &nvSetting);

  if (status != NVAPI_OK) {
    LOG_ERROR << "NvidiaControlPanel: GetThreadedOptimizationValue failed - Failed to "
                 "get Threaded Optimization setting: "
              << GetNvAPIErrorString(status);
    return false;
  }

  // Make sure it's the right type
  if (nvSetting.settingType != NVDRS_DWORD_TYPE) {
    LOG_ERROR << "NvidiaControlPanel: GetThreadedOptimizationValue failed - Threaded "
                 "Optimization setting is not a DWORD type";
    return false;
  }

  // Return the value
  value = nvSetting.u32CurrentValue;
  return true;
}

bool NvidiaControlPanel::RestoreThreadedOptimization() {
  if (!has_nvidia_gpu || (!initialized && !Initialize())) {
    return false;
  }

  return ApplyThreadedOptimization(OGL_THREAD_CONTROL_DEFAULT);
}

}  // namespace nvidia
}  // namespace optimizations
