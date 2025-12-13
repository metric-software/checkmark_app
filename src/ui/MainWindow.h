#pragma once

#include <memory>

#include <QCheckBox>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QWidget>

#include "DiagnosticView.h"
#include "GameBenchmarkView.h"
#include "OptimizeView.h"
#include "SettingsView.h"
#include "SystemInfoView.h"
#include "SilentNotificationBanner.h"
class UpdateCenterView;
enum class UpdateTier;
struct UpdateStatus;
class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

 private slots:
  void switchToSystemInfo();
  void switchToDiagnostics();
  void switchToOptimize();
  void switchToGameBenchmark();
  void switchToSettings();
  void switchToUpdate();

  // Add a cleanup slot for explicit resource release
  void cleanupResources();

  // Update manager slots
  void onUpdateStatusChanged(const UpdateStatus& status);
  void onCriticalUpdateDetected(const UpdateStatus& status);

 private:
  void setupLayout();
  void setDefaultSize() { setMinimumSize(0, 0); }
  void applyUpdateButtonStyle(UpdateTier tier, const QString& versionText);

  // Add this method to the private section:
  void closeEvent(QCloseEvent* event) override;

  // System info removed - using ConstantSystemInfo directly when needed
  QWidget* centralWidget = nullptr;
  QStackedWidget* stackedWidget = nullptr;
  QPushButton* systemInfoButton = nullptr;
  QPushButton* diagnosticsButton = nullptr;
  QPushButton* optimizeButton = nullptr;
  QPushButton* gameBenchmarkButton = nullptr;
  QPushButton* settingsButton = nullptr;
  QPushButton* updateButton = nullptr;
  UpdateCenterView* updateView = nullptr;
  SystemInfoView* systemInfoView = nullptr;
  DiagnosticView* diagnosticView = nullptr;
  OptimizeView* optimizeView = nullptr;
  GameBenchmarkView* gameBenchmarkView = nullptr;
  SettingsView* settingsView = nullptr;
  SilentNotificationBanner* notificationBanner = nullptr;
  bool criticalDialogShown = false;
};
