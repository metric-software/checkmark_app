#pragma once

#include <QObject>

// FeatureToggleManager - fetches remote feature flags from the backend
// at application startup and applies them to ApplicationSettings.
//
// Behavior:
// - If the backend is reachable and returns a config, the flags are applied.
// - If the backend is unreachable or returns invalid data, all remote flags
//   remain disabled, effectively turning off controlled features.
class FeatureToggleManager : public QObject {
  Q_OBJECT

 public:
  explicit FeatureToggleManager(QObject* parent = nullptr);

  // Fetch and apply feature flags synchronously with a short timeout.
  // Safe to call once during startup after QApplication has been created.
  void fetchAndApplyRemoteFlags();
};

