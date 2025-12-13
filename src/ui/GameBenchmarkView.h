#pragma once
#include <chrono>  // Add this include

#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QPropertyAnimation>  // Add this include for slideAnimation
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QWidget>

#include "benchmark/BenchmarkConstants.h"  // Add this include
#include "benchmark/BenchmarkManager.h"
#include "benchmark/BenchmarkStateTracker.h"  // Add this include
#include "benchmark/DemoFileManager.h"

class GameBenchmarkView : public QWidget {
  Q_OBJECT

 public:
  explicit GameBenchmarkView(QWidget* parent = nullptr);
  void cancelOperations();
  void showEACWarningIfNeeded();

 protected:
  // Add event filter to handle hover tooltips
  bool eventFilter(QObject* obj, QEvent* event) override;

 private slots:
  void onBenchmarkProgress(
    int percentage);  // Deprecated - kept for compatibility but no longer used
  void onBenchmarkMetrics(const PM_METRICS& metrics);
  void onBenchmarkSample(const BenchmarkDataPoint& sample);  // New coherent data slot
  void onBenchmarkFinished();
  void onBenchmarkError(const QString& error);
  void onBenchmarkStateChanged(const QString& state);  // Add this slot

 private:
  void setupUI();
  void updateProgressDisplay();  // Add this method

  QLabel* createInfoIcon(const QString& tooltipText);
  QString findRustDemosFolder();
  bool copyDemoFile();
  QWidget* createMetricBox(const QString& title);
  QTableWidget* createMetricTable(int rows, int cols);
  QTableWidget* createCompactTable(int rows, int cols, int width = 120, int height = 60);
  QTableWidget* createExcelStyleTable(int rows, int cols, const QStringList& headers = QStringList(), const QStringList& rowLabels = QStringList());
  void updateTableValue(QTableWidget* table, int row, int col, const QString& value, const QString& color = "#dddddd");
  void resetTableValues();

  DemoFileManager* demoManager;
  QWidget* outputContent;
  QWidget* outputContainer = nullptr;  // Add this line
  QPushButton* expandButton;
  BenchmarkManager* benchmark;
  QPushButton* benchmarkButton;
  QPushButton* resultsButton;  // Add missing button

  QWidget* mainContentWidget;
  QStackedWidget* stackedWidget;

  // Progress label
  QLabel* progressLabel;

  // Simple table-based metric displays using QLabel cells
  QTableWidget* fpsTable;          // Excel-style table: FPS | 1% | 5% | 0.1%
  QTableWidget* systemTable;      // Excel-style table: CPU/GPU/RAM/VRAM
  QTableWidget* timingsTable;     // Excel-style table: Frame/GPU/CPU timings
  
  // QTableWidget tables for excel-style metrics display
  
  // Bottom text displays
  QLabel* progressTextLabel;
  QLabel* displayTextLabel;

  // Legacy labels (keep for now during transition)
  QLabel* rawFpsLabel;
  QLabel* lowFpsLabel;
  QLabel* cpuUsageLabel;
  QLabel* gpuUsageLabel;
  QLabel* memoryUsageLabel;
  QLabel* vramUsageLabel;
  QLabel* displayInfoLabel;
  QLabel* processNameLabel;
  QLabel* frameTimeLabel;
  QLabel* cpuTimeLabel;
  QLabel* gpuTimeLabel;

  QLabel* stateLabel;
  bool isRunning;
  bool receivedFirstMetrics;
  QTimer* cooldownTimer;
  QTimer* progressUpdateTimer;  // Add this timer for updating progress
  static const int COOLDOWN_MS = 3000;

  // Benchmark state tracking
  BenchmarkStateTracker::State currentBenchmarkState =
    BenchmarkStateTracker::State::OFF;
  std::chrono::steady_clock::time_point benchmarkStartTime;
  std::chrono::steady_clock::time_point monitoringStartTime;

  // Notification banner components
  QLabel* notificationBanner = nullptr;
  QPropertyAnimation* slideAnimation = nullptr;
};
