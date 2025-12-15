#include "ApplicationSettings.h"

#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include "logging/Logger.h"

#include "hardware/SystemMetricsValidator.h"  // For ValidationResult enum

ApplicationSettings::ApplicationSettings()
    : settings(getSettingsFilePath(), QSettings::IniFormat) {
  // Create profiles directory in user's AppData location
  QString appDataPath = QCoreApplication::applicationDirPath();
  QDir appDataDir(appDataPath);
  if (!appDataDir.exists()) {
    appDataDir.mkpath(".");
  }

  // Create profiles subdirectory
  QDir profilesDir(appDataPath + "/profiles");
  if (!profilesDir.exists()) {
    profilesDir.mkpath(".");
  }

  // Log settings location
  LOG_INFO << "Settings file: [path hidden for privacy]";
  LOG_INFO << "Profiles directory: [path hidden for privacy]";

  // Ensure Offline Mode remains the highest hierarchy setting.
  // If Offline Mode is enabled, force data collection off so persisted state
  // and UI stay consistent across versions.
  if (isOfflineModeEnabled() && getAllowDataCollection()) {
    settings.setValue("Privacy/AllowDataCollection", false);
    settings.setValue("Features/AutomaticDataUploadEnabled", false);
    settings.sync();
  }
}

QString ApplicationSettings::getSettingsFilePath() const {
  // Create a dedicated folder in AppData/Roaming
  QString appDataPath =
    QCoreApplication::applicationDirPath() + "/benchmark_user_data";
  QDir configDir(appDataPath);
  if (!configDir.exists()) {
    configDir.mkpath(".");
  }
  LOG_INFO << "Application data directory: [path hidden for privacy]";
  return appDataPath + "/application_settings.ini";
}

bool ApplicationSettings::hasAcceptedTerms() const {
  return settings.value("Legal/AcceptedTerms", false).toBool();
}

void ApplicationSettings::setTermsAccepted(bool accepted) {
  settings.setValue("Legal/AcceptedTerms", accepted);
  settings.sync();  // Force write to disk immediately
}

// New methods for validation result caching
SystemMetrics::ValidationResult ApplicationSettings::
  getComponentValidationResult(const std::string& componentName) const {
  QString key =
    QString("Validation/%1").arg(QString::fromStdString(componentName));
  int value =
    settings.value(key, static_cast<int>(SystemMetrics::NOT_TESTED)).toInt();
  return intToValidationResult(value);
}

void ApplicationSettings::setComponentValidationResult(
  const std::string& componentName, SystemMetrics::ValidationResult result) {
  QString key =
    QString("Validation/%1").arg(QString::fromStdString(componentName));
  settings.setValue(key, validationResultToInt(result));
  settings.sync();  // Force write to disk immediately
}

void ApplicationSettings::clearAllValidationResults() {
  settings.beginGroup("Validation");
  settings.remove("");  // Remove all keys in the Validation group
  settings.endGroup();
  settings.sync();
}

bool ApplicationSettings::shouldValidateComponent(
  const std::string& componentName) const {
  // Only skip validation for components that have previously succeeded
  SystemMetrics::ValidationResult lastResult =
    getComponentValidationResult(componentName);
  return lastResult != SystemMetrics::SUCCESS;
}

int ApplicationSettings::validationResultToInt(
  SystemMetrics::ValidationResult result) const {
  return static_cast<int>(result);
}

SystemMetrics::ValidationResult ApplicationSettings::intToValidationResult(
  int value) const {
  // Ensure the value is within the valid range
  if (value < 0 || value > static_cast<int>(SystemMetrics::SUCCESS)) {
    return SystemMetrics::NOT_TESTED;
  }
  return static_cast<SystemMetrics::ValidationResult>(value);
}

// New method to reset all settings
void ApplicationSettings::resetAllSettings() {
  // Clear all keys in the settings file
  settings.clear();

  // Sync to ensure changes are written to disk
  settings.sync();
}

// Experimental features settings
bool ApplicationSettings::getExperimentalFeaturesEnabled() const {
  return settings.value("Features/ExperimentalEnabled", false).toBool();
}

void ApplicationSettings::setExperimentalFeaturesEnabled(bool enabled) {
  settings.setValue("Features/ExperimentalEnabled", enabled);
  settings.sync();  // Force write to disk immediately
}

// User system profile methods
QString ApplicationSettings::getValue(const QString& key,
                                      const QString& defaultValue) const {
  return settings.value(key, defaultValue).toString();
}

void ApplicationSettings::setValue(const QString& key, const QString& value) {
  settings.setValue(key, value);
  settings.sync();  // Ensure it's saved immediately
}

// Console visibility settings
bool ApplicationSettings::getConsoleVisible() const {
  return settings.value("UI/ConsoleVisible", false).toBool();
}

void ApplicationSettings::setConsoleVisible(bool visible) {
  settings.setValue("UI/ConsoleVisible", visible);
  settings.sync();  // Force write to disk immediately
}

// Elevated priority settings
bool ApplicationSettings::getElevatedPriorityEnabled() const {
  return settings.value("Features/ElevatedPriorityEnabled", false).toBool();
}

void ApplicationSettings::setElevatedPriorityEnabled(bool enabled) {
  settings.setValue("Features/ElevatedPriorityEnabled", enabled);
  settings.sync();  // Force write to disk immediately
}

// Advanced settings methods
bool ApplicationSettings::getAdvancedSettingsEnabled() const {
  return settings.value("UI/AdvancedSettingsEnabled", false).toBool();
}

void ApplicationSettings::setAdvancedSettingsEnabled(bool enabled) {
  settings.setValue("UI/AdvancedSettingsEnabled", enabled);
  settings.sync();  // Force write to disk immediately
}

bool ApplicationSettings::isAdvancedSettingExplicitlySet() const {
  return settings.contains("UI/AdvancedSettingsEnabled");
}

// Validate metrics on startup methods
bool ApplicationSettings::getValidateMetricsOnStartup() const {
  return settings.value("Features/ValidateMetricsOnStartup", true).toBool();
}

void ApplicationSettings::setValidateMetricsOnStartup(bool enabled) {
  settings.setValue("Features/ValidateMetricsOnStartup", enabled);
  settings.sync();  // Force write to disk immediately
}

// Data collection methods
bool ApplicationSettings::getAllowDataCollection() const {
  return settings.value("Privacy/AllowDataCollection", true).toBool();
}

void ApplicationSettings::setAllowDataCollection(bool enabled) {
  settings.setValue("Privacy/AllowDataCollection", enabled);
  settings.sync();  // Force write to disk immediately
}

bool ApplicationSettings::isOfflineModeEnabled() const {
  return settings.value("Network/OfflineModeEnabled", false).toBool();
}

void ApplicationSettings::setOfflineModeEnabled(bool enabled) {
  settings.setValue("Network/OfflineModeEnabled", enabled);

  // Offline Mode is a "higher hierarchy" setting: when enabled, disable data
  // collection as well so the UI and persisted state stay consistent.
  if (enabled) {
    settings.setValue("Privacy/AllowDataCollection", false);
    // Keep legacy setting in sync for older codepaths (UI no longer exposes it).
    settings.setValue("Features/AutomaticDataUploadEnabled", false);
  }
  settings.sync();  // Force write to disk immediately
}

// Detailed logs methods
bool ApplicationSettings::getDetailedLogsEnabled() const {
  return settings.value("Features/DetailedLogsEnabled", false).toBool();
}

void ApplicationSettings::setDetailedLogsEnabled(bool enabled) {
  settings.setValue("Features/DetailedLogsEnabled", enabled);
  settings.sync();  // Force write to disk immediately
}

// Automatic data upload methods
bool ApplicationSettings::getAutomaticDataUploadEnabled() const {
  return settings.value("Features/AutomaticDataUploadEnabled", true).toBool();
}

void ApplicationSettings::setAutomaticDataUploadEnabled(bool enabled) {
  settings.setValue("Features/AutomaticDataUploadEnabled", enabled);
  settings.sync();  // Force write to disk immediately
}

// Remote feature flags (runtime-only)
void ApplicationSettings::setRemoteFeatureFlags(bool allowExperimental,
                                                bool allowUpload,
                                                bool initialized) {
  remoteExperimentalAllowed_ = allowExperimental;
  remoteUploadAllowed_ = allowUpload;
  remoteFlagsInitialized_ = initialized;
}

bool ApplicationSettings::isRemoteExperimentalAllowed() const {
  return remoteFlagsInitialized_ && remoteExperimentalAllowed_;
}

bool ApplicationSettings::isRemoteUploadAllowed() const {
  return remoteFlagsInitialized_ && remoteUploadAllowed_;
}

bool ApplicationSettings::areRemoteFeatureFlagsInitialized() const {
  return remoteFlagsInitialized_;
}

bool ApplicationSettings::getEffectiveAllowDataCollection() const {
  return !isOfflineModeEnabled() && getAllowDataCollection();
}

bool ApplicationSettings::getEffectiveExperimentalFeaturesEnabled() const {
  // If remote flags have not yet been fetched, treat experimental features
  // as disabled until we know the backend status.
  if (!remoteFlagsInitialized_) {
    return false;
  }
  return getExperimentalFeaturesEnabled() && remoteExperimentalAllowed_;
}

bool ApplicationSettings::getEffectiveAutomaticDataUploadEnabled() const {
  // Offline mode blocks uploads entirely, even for background tasks.
  if (isOfflineModeEnabled()) {
    return false;
  }

  // Respect the data collection toggle for all uploads (automatic and manual).
  if (!getAllowDataCollection()) {
    return false;
  }

  // If remote flags have not yet been fetched, treat uploads as disabled until
  // we know the backend status.
  if (!remoteFlagsInitialized_) {
    return false;
  }
  return remoteUploadAllowed_;
}
