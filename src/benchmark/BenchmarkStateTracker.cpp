#include "BenchmarkStateTracker.h"

#include <iostream>
#include <thread>

#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QSettings>
#include <QStorageInfo>

#include "BenchmarkConstants.h"
#include "BenchmarkDataPoint.h"
#include "PresentDataExports.h"
#include "../logging/Logger.h"

static constexpr int FOLDER_CHECK_INTERVAL_MS = 1000;
static constexpr int BENCHMARK_START_DELAY_MS = 5000;

// Rate-limited logging for BenchmarkStateTracker
class StateTrackerLogger {
private:
  static inline std::chrono::steady_clock::time_point m_lastMetricsLog;
  static inline constexpr int METRICS_INTERVAL_SECONDS = 15;
  
public:
  static void logError(const std::string& msg) {
    LOG_ERROR << "[ERROR] " << msg;
  }
  
  static void logCritical(const std::string& msg) {
    LOG_ERROR << "[CRITICAL] " << msg;
  }
  
  static void logMetrics(const std::string& msg) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastMetricsLog);
    if (elapsed.count() >= METRICS_INTERVAL_SECONDS) {
      m_lastMetricsLog = now;
      LOG_INFO << "[METRICS] " << msg;
    }
  }
};

BenchmarkStateTracker::BenchmarkStateTracker() {
  lastCheck = std::chrono::steady_clock::now();
  stateStartTime = lastCheck;

  // Initialize the log monitor
  m_logMonitor = std::make_unique<RustLogMonitor>();

  // Initialize start delay timer (5 seconds)
  m_startDelayTimer = std::make_unique<QTimer>();
  m_startDelayTimer->setSingleShot(true);
  m_startDelayTimer->setInterval(BENCHMARK_START_DELAY_MS);
  
  // Set up timer callback to actually start benchmark after delay
  QObject::connect(m_startDelayTimer.get(), &QTimer::timeout, [this]() {
    StateTrackerLogger::logCritical(
      std::to_string(BENCHMARK_START_DELAY_MS / 1000) +
      "-second start delay completed - starting benchmark");
    
    // Now update state to RUNNING and add the transition
    std::lock_guard<std::mutex> lock(m_stateMutex);
    currentState = State::RUNNING;
    auto actualStartTime = std::chrono::steady_clock::now();
    stateTransitions.push_back({actualStartTime, true, 0.0});
    
    if (m_benchmarkStartCallback) {
      m_benchmarkStartCallback();
    }
  });

  // Set up callbacks for log-based detection
  m_logMonitor->setBenchmarkStartCallback(
    [this]() { onBenchmarkStartDetected(); });

  m_logMonitor->setBenchmarkEndCallback([this]() { onBenchmarkEndDetected(); });
}

BenchmarkStateTracker::~BenchmarkStateTracker() { cleanup(); }

void BenchmarkStateTracker::onBenchmarkStartDetected() {
  LOG_INFO << "[BenchmarkStateTracker] ***** BENCHMARK START SIGNAL DETECTED *****";
  //StateTrackerLogger::logCritical("Benchmark START signal detected from log monitor - starting 5s delay");
  
  // Don't update state to RUNNING yet - only after the configured start delay
  std::lock_guard<std::mutex> lock(m_stateMutex);
  currentState = State::WAITING;  // Keep in WAITING state during delay
  benchmarkActualStartTime = std::chrono::steady_clock::now();
  
  // Mark log-based detection as active
  logBasedDetectionActive = true;
  LOG_INFO << "[BenchmarkStateTracker] Log-based detection ACTIVE - starting "
           << BENCHMARK_START_DELAY_MS / 1000 << "-second delay";
  //StateTrackerLogger::logCritical("Starting start delay before benchmark begins");
  
  // Start the configured start delay timer instead of calling callback immediately
  if (m_startDelayTimer) {
    m_startDelayTimer->start();
  } else {
    LOG_ERROR << "[BenchmarkStateTracker] ERROR: Start delay timer not initialized!";
    // Fallback: call immediately if timer failed
    if (m_benchmarkStartCallback) {
      m_benchmarkStartCallback();
    }
  }
}

void BenchmarkStateTracker::onBenchmarkEndDetected() {
  //StateTrackerLogger::logCritical("Benchmark END detected (timer or log-based)");
  
  // Update internal state
  std::lock_guard<std::mutex> lock(m_stateMutex);
  currentState = State::COOLDOWN;
  benchmarkActualEndTime = std::chrono::steady_clock::now();
  stateTransitions.push_back({benchmarkActualEndTime, false, 0.0});
  
  // Mark this as a valid benchmark since we detected proper end (timer or log-based)
  validBenchmarkFound = true;
  logBasedDetectionActive = true;
  //StateTrackerLogger::logCritical("Marking benchmark as VALID - detected end");
  
  if (m_benchmarkEndCallback) {
    m_benchmarkEndCallback();
  }
}

void BenchmarkStateTracker::cleanup() {
  // Keep log monitoring running to avoid missing lines between runs.
  // We'll just reset internal detection state below.
  if (m_logMonitor && m_logMonitor->isMonitoring()) {
    m_logMonitor->resetForNextRun();
  }
  
  // Stop the start delay timer if it's running
  if (m_startDelayTimer && m_startDelayTimer->isActive()) {
    m_startDelayTimer->stop();
  }

  rustFolder.clear();
  benchmarkFolder.clear();
  initialBenchmarkFiles.clear();
  initialFileCount = -1;
  benchmarkFolderFound = false;
  logBasedDetectionActive = false;
}

bool BenchmarkStateTracker::initialize(uint32_t processId) {
  std::lock_guard<std::mutex> lock(m_stateMutex);
  // Reset state but keep log monitor running to avoid blind spots
  cleanup();
  targetProcessId = processId;
  currentState = State::WAITING;
  stateStartTime = std::chrono::steady_clock::now();
  validBenchmarkFound = false;
  validSegmentSignaled = false;
  runStartTime = std::chrono::steady_clock::now();
  logBasedDetectionActive = false;

  // Configure log monitor to use timer-based end detection (uses global constant)
  m_logMonitor->setUseTimerEndDetection(true);
  
  // Start monitoring only if it's not already running
  if (!m_logMonitor->isMonitoring()) {
    bool logMonitorStarted = m_logMonitor->startMonitoring();
    if (!logMonitorStarted) {
      StateTrackerLogger::logError("Log monitoring failed, using fallback detection");
    }
  } else {
    // Ensure we begin with a clean detection state for the new run
    m_logMonitor->resetForNextRun();
  }

  rustFolder = findRustFolder();
  if (!rustFolder.isEmpty()) {
    benchmarkFolder = rustFolder + "/benchmark";
    benchmarkFolderFound = checkBenchmarkFolder();
  }
  return true;
}

QString BenchmarkStateTracker::findRustFolder() {
  QStringList possiblePaths;

  // Check Steam registry first (same logic as in RustBenchmarkFinder)
  QSettings steamRegistry(
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam",
    QSettings::NativeFormat);
  QString steamPath = steamRegistry.value("InstallPath").toString();
  if (!steamPath.isEmpty()) {
    possiblePaths << steamPath + "/steamapps/common/Rust";
  }

  // Add common Steam paths
  possiblePaths << "C:/Program Files (x86)/Steam/steamapps/common/Rust"
                << "C:/Program Files/Steam/steamapps/common/Rust";

  // Try all available drives
  for (const QStorageInfo& drive : QStorageInfo::mountedVolumes()) {
    if (drive.isValid() && drive.isReady()) {
      possiblePaths << drive.rootPath() + "SteamLibrary/steamapps/common/Rust";
    }
  }

  // Find first valid Rust installation by checking for RustClient.exe
  for (const QString& path : possiblePaths) {
    QFileInfo exeFile(path + "/RustClient.exe");
    if (exeFile.exists() && exeFile.isFile()) {
      return QDir::toNativeSeparators(path);
    }
  }

  return QString();
}

bool BenchmarkStateTracker::checkBenchmarkFolder() {
  if (rustFolder.isEmpty()) {
    rustFolder = findRustFolder();
    if (rustFolder.isEmpty()) {
      return false;
    }
    benchmarkFolder = rustFolder + "/benchmark";
  }

  QDir dir(benchmarkFolder);
  if (!dir.exists()) {
    return false;
  }

  QStringList filters;
  filters << "*.json";
  initialBenchmarkFiles = dir.entryInfoList(filters, QDir::Files, QDir::Time);
  initialFileCount = initialBenchmarkFiles.size();

  return true;
}

// File detection removed - using log-based detection only

// Simplified updateState method - log-based detection only
BenchmarkStateTracker::State BenchmarkStateTracker::updateState(
  const PM_METRICS& metrics, const BenchmarkDataPoint& processMetrics) {

  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(m_stateMutex);

  // If we're in COOLDOWN state, just return it
  if (currentState == State::COOLDOWN) {
    return currentState;
  }

  // State transitions are handled asynchronously by onBenchmarkStart/EndDetected callbacks
  // This function only handles overall timeout protection

  // Check for overall benchmark timeout
  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - runStartTime).count();
  if (elapsed >= MAX_BENCHMARK_TIME) {
    if (currentState != State::COOLDOWN) {
      StateTrackerLogger::logError("Benchmark timeout after " + std::to_string(MAX_BENCHMARK_TIME) + "s");
      currentState = State::COOLDOWN;
      stopReason = StopReason::TIMEOUT;
      stateTransitions.push_back({now, false, 0.0});
    }
    return currentState;
  }

  return currentState;
}

void BenchmarkStateTracker::stopBenchmark() {
  std::lock_guard<std::mutex> lock(m_stateMutex);
  auto now = std::chrono::steady_clock::now();

  if (currentState == State::RUNNING || currentState == State::WAITING) {
    //StateTrackerLogger::logCritical("Benchmark manually stopped");
    // Do not stop log monitoring; keep it alive for the next run
    if (m_logMonitor && m_logMonitor->isMonitoring()) {
      m_logMonitor->resetForNextRun();
    }
    
    // Stop the start delay timer if it's running
    if (m_startDelayTimer && m_startDelayTimer->isActive()) {
      m_startDelayTimer->stop();
      //StateTrackerLogger::logCritical("Stopped start delay timer due to manual stop");
    }
    
    currentState = State::COOLDOWN;
    cooldownStartTime = now;
    stopReason = StopReason::MANUAL;

    // If log-based detection was active, we already have valid data
    if (logBasedDetectionActive) {
      validBenchmarkFound = true;
    }
  }
}

bool BenchmarkStateTracker::isValidBenchmark() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_stateMutex));
  return validBenchmarkFound;
}

std::pair<float, float> BenchmarkStateTracker::getBenchmarkTimeRange() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_stateMutex));
  if (!validBenchmarkFound) {
    return {0.0f, 0.0f};
  }

  // If we have log-based timing, use it for more accurate range
  if (logBasedDetectionActive &&
      benchmarkActualStartTime != std::chrono::steady_clock::time_point{} &&
      benchmarkActualEndTime != std::chrono::steady_clock::time_point{}) {

    float logStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                           benchmarkActualStartTime - runStartTime)
                           .count() /
                         1000.0f;
    float logEndTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                         benchmarkActualEndTime - runStartTime)
                         .count() /
                       1000.0f;

    return {logStartTime, logEndTime};
  }

  // Fallback to old method
  return {benchmarkStartTime, benchmarkEndTime};
}

BenchmarkStateTracker::State BenchmarkStateTracker::getCurrentState() const {
  std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_stateMutex));
  return currentState;
}
