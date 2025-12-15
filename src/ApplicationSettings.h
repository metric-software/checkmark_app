#pragma once

#include <filesystem>
#include <map>
#include <string>

#include <QSettings>
#include <QString>

// Forward declaration to avoid circular dependency
namespace SystemMetrics {
enum ValidationResult;
}

class ApplicationSettings {
 public:
  static ApplicationSettings& getInstance() {
    static ApplicationSettings instance;
    return instance;
  }

  bool hasAcceptedTerms() const;
  void setTermsAccepted(bool accepted);

  // New methods for validation results caching
  SystemMetrics::ValidationResult getComponentValidationResult(
    const std::string& componentName) const;
  void setComponentValidationResult(const std::string& componentName,
                                    SystemMetrics::ValidationResult result);
  void clearAllValidationResults();

  // Check if component should be validated during startup
  bool shouldValidateComponent(const std::string& componentName) const;

  // New methods for settings management
  void resetAllSettings();

  // Experimental features settings
  bool getExperimentalFeaturesEnabled() const;
  void setExperimentalFeaturesEnabled(bool enabled);

  // Console visibility setting
  bool getConsoleVisible() const;
  void setConsoleVisible(bool visible);

  // Elevated priority setting
  bool getElevatedPriorityEnabled() const;
  void setElevatedPriorityEnabled(bool enabled);

  // User system profile methods
  QString getValue(const QString& key, const QString& defaultValue) const;
  void setValue(const QString& key, const QString& value);

  // Advanced settings visibility
  bool getAdvancedSettingsEnabled() const;
  void setAdvancedSettingsEnabled(bool enabled);
  bool isAdvancedSettingExplicitlySet() const;

  // Validate metrics on startup setting
  bool getValidateMetricsOnStartup() const;
  void setValidateMetricsOnStartup(bool enabled);

  // Data collection setting TODO!!!!
  bool getAllowDataCollection() const;
  void setAllowDataCollection(bool enabled);

  // Offline mode setting (disables uploads/downloads)
  bool isOfflineModeEnabled() const;
  void setOfflineModeEnabled(bool enabled);

  // Detailed logs setting (developer oriented)
  bool getDetailedLogsEnabled() const;
  void setDetailedLogsEnabled(bool enabled);

  // Automatic data upload setting
  bool getAutomaticDataUploadEnabled() const;
  void setAutomaticDataUploadEnabled(bool enabled);

  // Remote feature flags (controlled by backend)
  // These are ephemeral (not persisted) and are applied on top of local
  // preferences to compute effective behavior.
  void setRemoteFeatureFlags(bool allowExperimental, bool allowUpload, bool initialized = true);
  bool isRemoteExperimentalAllowed() const;
  bool isRemoteUploadAllowed() const;
  bool areRemoteFeatureFlagsInitialized() const;

  // Effective settings (local preference AND remote flag AND online status)
  // "Allow data collection" is overridden by Offline Mode.
  bool getEffectiveAllowDataCollection() const;
  bool getEffectiveExperimentalFeaturesEnabled() const;
  // Effective permission to upload user-generated benchmark/diagnostic data.
  // This is governed by Offline Mode, Allow Data Collection, and backend remote flags.
  bool getEffectiveAutomaticDataUploadEnabled() const;

 private:
  ApplicationSettings();
  QString getSettingsFilePath() const;
  QSettings settings;

  // Remote feature flags (runtime-only, not persisted)
  bool remoteExperimentalAllowed_ = false;
  bool remoteUploadAllowed_ = false;
  bool remoteFlagsInitialized_ = false;

  // Convert between enum and string for storage
  int validationResultToInt(SystemMetrics::ValidationResult result) const;
  SystemMetrics::ValidationResult intToValidationResult(int value) const;
};
