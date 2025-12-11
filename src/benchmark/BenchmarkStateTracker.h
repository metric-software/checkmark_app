#pragma once
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QDir>
#include <QFileInfoList>
#include <QString>
#include <QTimer>
#include <windows.h>

#include "BenchmarkConstants.h"
#include "RustLogMonitor.h"

// Forward declarations
struct PM_METRICS;
struct BenchmarkDataPoint;

class BenchmarkStateTracker {
 public:
  enum class State { OFF, WAITING, RUNNING, COOLDOWN };
  enum class Signal { BENCHMARK_START, BENCHMARK_END };

  // Use shared constants
  static constexpr double COOLDOWN_DURATION =
    BenchmarkConstants::COOLDOWN_DURATION;
  static constexpr double MAX_BENCHMARK_TIME =
    BenchmarkConstants::MAX_BENCHMARK_TIME;
  static constexpr double TARGET_BENCHMARK_DURATION =
    BenchmarkConstants::TARGET_BENCHMARK_DURATION;

  enum class StopReason { NORMAL, TIMEOUT, MANUAL };

  StopReason getStopReason() const { return stopReason; }

  struct StateTransition {
    std::chrono::steady_clock::time_point timestamp;
    bool isStart;
    double yellowness;  // Kept for compatibility
  };

  BenchmarkStateTracker();
  ~BenchmarkStateTracker();

  bool initialize(uint32_t processId);
  State updateState(const PM_METRICS& metrics,
                    const BenchmarkDataPoint& processMetrics);
  void cleanup();
  void stopBenchmark();

  bool isValidBenchmark() const;
  std::pair<float, float> getBenchmarkTimeRange() const;

  const std::vector<StateTransition>& getTransitions() const {
    return stateTransitions;
  }
  State getCurrentState() const;
  
  // Simple signal callbacks - just notify, don't do complex logic
  void setBenchmarkStartCallback(std::function<void()> callback) {
    m_benchmarkStartCallback = callback;
  }
  
  void setBenchmarkEndCallback(std::function<void()> callback) {
    m_benchmarkEndCallback = callback;
  }

 private:
  std::mutex m_stateMutex;
  // Log-based benchmark detection (NEW APPROACH)
  std::unique_ptr<RustLogMonitor> m_logMonitor;
  void onBenchmarkStartDetected();
  void onBenchmarkEndDetected();

  // Legacy file detection methods (kept for compatibility)
  QString findRustFolder();
  bool checkBenchmarkFolder();

  // State tracking
  State currentState = State::OFF;
  std::vector<StateTransition> stateTransitions;

  // Timing
  std::chrono::steady_clock::time_point lastCheck;
  std::chrono::steady_clock::time_point stateStartTime;
  std::chrono::steady_clock::time_point runStartTime;
  std::chrono::steady_clock::time_point cooldownStartTime;
  std::chrono::steady_clock::time_point benchmarkActualStartTime;
  std::chrono::steady_clock::time_point benchmarkActualEndTime;

  // Path monitoring (kept for compatibility)
  QString rustFolder;
  QString benchmarkFolder;
  QFileInfoList initialBenchmarkFiles;
  int initialFileCount = -1;
  bool benchmarkFolderFound = false;

  // Benchmark validation
  bool validBenchmarkFound = false;
  bool validSegmentSignaled = false;
  bool logBasedDetectionActive = false;
  float benchmarkStartTime = 0.0f;
  float benchmarkEndTime = 0.0f;

  uint32_t targetProcessId = 0;
  StopReason stopReason = StopReason::NORMAL;
  
  // Simple callbacks for immediate notification to BenchmarkManager
  std::function<void()> m_benchmarkStartCallback;
  std::function<void()> m_benchmarkEndCallback;
  
  // Delay timer for benchmark start
  std::unique_ptr<QTimer> m_startDelayTimer;
};
