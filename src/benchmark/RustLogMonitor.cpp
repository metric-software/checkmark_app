#include "RustLogMonitor.h"

#include <chrono>
#include <iostream>

#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStorageInfo>
#include <QTextStream>

#include "BenchmarkConstants.h"
#include "../logging/Logger.h"

// Rate-limited logging for RustLogMonitor
class RustLogLimiter {
private:
  static inline int m_processCallCount = 0;
  static inline std::chrono::steady_clock::time_point m_lastDetailedLog;
  static inline constexpr int MAX_DETAILED_LOGS = 3;
  static inline constexpr int STATUS_INTERVAL_SECONDS = 15;
  
public:
  static void resetMonitoring() {
    m_processCallCount = 0;
    m_lastDetailedLog = std::chrono::steady_clock::now();
  }
  
  static bool shouldLogDetails() {
    return m_processCallCount < MAX_DETAILED_LOGS;
  }
  
  static bool shouldLogStatus() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_lastDetailedLog);
    if (elapsed.count() >= STATUS_INTERVAL_SECONDS) {
      m_lastDetailedLog = now;
      return true;
    }
    return false;
  }
  
  static void incrementCallCount() {
    m_processCallCount++;
  }
  
  static void logStatus(const std::string& message) {
    // Removed: status logs (spammy)
  }

  static void logDetails(const std::string& message) {
    // Removed: detailed logs (spammy)
  }

  static void logImportant(const std::string& message) {
    LOG_INFO << "[RustLogMonitor] " << message;
  }
};

// Define the log patterns we're looking for - two-stage detection
const QString RustLogMonitor::BENCHMARK_PREP_PATTERN = "Threaded texture creation has been enabled!";
const QString RustLogMonitor::BENCHMARK_START_PREFIX =
  "No cfg file found for demos: demos/";
// Accept any .cfg in the demos folder, regardless of name/case or extra prefix text
const QRegularExpression RustLogMonitor::BENCHMARK_START_REGEX(
  R"((?i)no cfg file found for demos:\s*demos/[^\s]+\.cfg)");
const QString RustLogMonitor::BENCHMARK_END_PATTERN = "Playing Video";

RustLogMonitor::RustLogMonitor(QObject* parent)
    : QObject(parent),
      m_pollTimer(std::make_unique<QTimer>(this)),
      m_fileWatcher(std::make_unique<QFileSystemWatcher>(this)),
      m_benchmarkDurationTimer(std::make_unique<QTimer>(this)),
      m_fileDiscoveryTimer(std::make_unique<QTimer>(this)) {
  // Set up timer for periodic checking (fallback)
  m_pollTimer->setSingleShot(false);
  m_pollTimer->setInterval(500);  // Check every 500ms
  connect(m_pollTimer.get(), &QTimer::timeout, this,
          &RustLogMonitor::checkForNewContent);

  // Set up file system watcher
  connect(m_fileWatcher.get(), &QFileSystemWatcher::fileChanged, this,
          &RustLogMonitor::onFileChanged);
          
  // Set up benchmark duration timer (using global constant)
  m_benchmarkDurationTimer->setSingleShot(true);
  m_benchmarkDurationTimer->setInterval(static_cast<int>(BenchmarkConstants::TARGET_BENCHMARK_DURATION * 1000));  // Convert to milliseconds
  connect(m_benchmarkDurationTimer.get(), &QTimer::timeout, this, [this]() {
    if (m_useTimerEndDetection && m_benchmarkDetectedActive) {
      RustLogLimiter::logImportant("Benchmark auto-ended after " + std::to_string(static_cast<int>(BenchmarkConstants::TARGET_BENCHMARK_DURATION)) + " seconds");
      LOG_DEBUG << "[DEBUG] Timer ended - resetting flags: prep_detected=false, benchmark_active=false";
      m_benchmarkDetectedActive = false;
      m_benchmarkPrepDetected = false;
      if (m_benchmarkEndCallback) {
        m_benchmarkEndCallback();
      }
      emit benchmarkEnded();
    }
  });
  
  // Set up file discovery timer to periodically check for new log files
  m_fileDiscoveryTimer->setSingleShot(false);
  m_fileDiscoveryTimer->setInterval(10000);  // Check every 10 seconds for new log files
  connect(m_fileDiscoveryTimer.get(), &QTimer::timeout, this,
          &RustLogMonitor::checkForNewLogFiles);
}

RustLogMonitor::~RustLogMonitor() { stopMonitoring(); }

QString RustLogMonitor::findOutputLogFile() {
  // Check for output_log.txt in the game's root directory
  QStringList possiblePaths;

  // Check Steam registry first
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

  // Add all drives
  for (const QStorageInfo& drive : QStorageInfo::mountedVolumes()) {
    if (drive.isValid() && drive.isReady()) {
      possiblePaths << drive.rootPath() + "SteamLibrary/steamapps/common/Rust";
    }
  }

  // Find first valid Rust installation by checking for RustClient.exe and then
  // for output_log.txt
  for (const QString& path : possiblePaths) {
    QFileInfo exeFile(path + "/RustClient.exe");
    if (exeFile.exists() && exeFile.isFile()) {
      QString logPath = path + "/output_log.txt";
      if (QFile::exists(logPath)) {
        return QDir::toNativeSeparators(logPath);
      }
    }
  }

  return QString();
}

QString RustLogMonitor::findPlayerLogFile() {
  // Get the standard AppData/LocalLow path for player.log
  QString appDataPath =
    QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  if (appDataPath.isEmpty()) {
    // Fallback to USERPROFILE/AppData/LocalLow
    QString userProfile = QString::fromLocal8Bit(qgetenv("USERPROFILE"));
    if (!userProfile.isEmpty()) {
      appDataPath = userProfile + "/AppData/LocalLow";
    }
  } else {
    // QStandardPaths gives us AppData/Local, we need AppData/LocalLow
    appDataPath.replace("/Local", "/LocalLow");
  }

  QString logPath = appDataPath + "/Facepunch Studios Ltd/Rust/player.log";
  logPath = QDir::toNativeSeparators(logPath);

  QFileInfo fileInfo(logPath);
  if (fileInfo.exists() && fileInfo.isFile()) {
    return logPath;
  }

  return QString();
}

void RustLogMonitor::findAndSetupLogFiles() {
  m_logFiles.clear();
  // Start reading from the end of existing files so we don't process old logs
  
  // Try to find both log files
  QString outputLogPath = findOutputLogFile();
  QString playerLogPath = findPlayerLogFile();
  
  LOG_DEBUG << "[DEBUG] Log file discovery - output_log.txt: " 
            << (outputLogPath.isEmpty() ? "NOT FOUND" : outputLogPath.toStdString());
  LOG_DEBUG << "[DEBUG] Log file discovery - player.log: " 
            << (playerLogPath.isEmpty() ? "NOT FOUND" : playerLogPath.toStdString());
  
  if (!outputLogPath.isEmpty()) {
    LogFileInfo info;
    info.path = outputLogPath;
    info.file = std::make_unique<QFile>(outputLogPath);
    info.exists = QFile::exists(outputLogPath);
    if (info.exists) {
      qint64 size = info.file->size();
      // Skip all existing content; only process new lines appended after start
      info.lastPosition = size;
    } else {
      info.lastPosition = 0;
    }
    m_logFiles.push_back(std::move(info));
    RustLogLimiter::logImportant("Found output_log.txt at: " + outputLogPath.toStdString());
    LOG_DEBUG << "[DEBUG] output_log.txt exists: " << (info.exists ? "YES" : "NO")
              << ", start at EOF pos: " << m_logFiles.back().lastPosition;
  }
  
  if (!playerLogPath.isEmpty()) {
    LogFileInfo info;
    info.path = playerLogPath;
    info.file = std::make_unique<QFile>(playerLogPath);
    info.exists = QFile::exists(playerLogPath);
    if (info.exists) {
      qint64 size = info.file->size();
      // Skip all existing content; only process new lines appended after start
      info.lastPosition = size;
    } else {
      info.lastPosition = 0;
    }
    m_logFiles.push_back(std::move(info));
    RustLogLimiter::logImportant("Found player.log at: " + playerLogPath.toStdString());
    LOG_DEBUG << "[DEBUG] player.log exists: " << (info.exists ? "YES" : "NO")
              << ", start at EOF pos: " << m_logFiles.back().lastPosition;
  }

  if (m_logFiles.empty()) {
    RustLogLimiter::logImportant("No Rust log files found at any location");
  }
}

bool RustLogMonitor::startMonitoring() {
  if (m_isMonitoring) {
    return true;
  }

  findAndSetupLogFiles();
  if (m_logFiles.empty()) {
    RustLogLimiter::logImportant("No log files found - monitoring disabled");
    return false;
  }
  
  // Add all found log files to the file watcher
  for (const auto& logFile : m_logFiles) {
    if (logFile.exists && !m_fileWatcher->files().contains(logFile.path)) {
      m_fileWatcher->addPath(logFile.path);
    }
  }

  m_isMonitoring = true;
  m_benchmarkDetectedActive = false;
  m_benchmarkPrepDetected = false;
  m_useTimerEndDetection = true;  // Enable timer-based end detection

  LOG_DEBUG << "[DEBUG] Monitoring started - flags reset: prep_detected=false, benchmark_active=false";
  
  m_pollTimer->start();
  m_fileDiscoveryTimer->start();  // Start periodic file discovery
  RustLogLimiter::resetMonitoring();

  // Removed: startup logs (spammy)
  RustLogLimiter::logImportant("Log monitoring started with " + std::to_string(m_logFiles.size()) + " files");

  // Do NOT process existing content on start; wait for new lines

  return true;
}

void RustLogMonitor::stopMonitoring() {
  if (!m_isMonitoring) {
    return;
  }

  m_isMonitoring = false;
  m_pollTimer->stop();
  m_fileDiscoveryTimer->stop();  // Stop file discovery timer
  m_benchmarkDurationTimer->stop();  // Stop auto-end timer

  // Remove all monitored files from watcher
  for (const auto& logFile : m_logFiles) {
    if (m_fileWatcher->files().contains(logFile.path)) {
      m_fileWatcher->removePath(logFile.path);
    }
    if (logFile.file && logFile.file->isOpen()) {
      logFile.file->close();
    }
  }
  m_logFiles.clear();
}

void RustLogMonitor::setBenchmarkStartCallback(std::function<void()> callback) {
  m_benchmarkStartCallback = callback;
}

void RustLogMonitor::setBenchmarkEndCallback(std::function<void()> callback) {
  m_benchmarkEndCallback = callback;
}

void RustLogMonitor::onFileChanged(const QString& path) {
  Q_UNUSED(path)
  // File changed, check for new content
  checkForNewContent();
}

void RustLogMonitor::checkForNewContent() {
  if (!m_isMonitoring || m_logFiles.empty()) {
    return;
  }

  // Check all log files for new content
  for (auto& logFile : m_logFiles) {
    // Check if file exists
    QFileInfo fileInfo(logFile.path);
    bool currentExists = fileInfo.exists();
    
    if (!currentExists) {
      // File doesn't exist, reset position and wait
      logFile.lastPosition = 0;
      logFile.exists = false;
      continue;
    }
    
    // File exists now
    if (!logFile.exists) {
      logFile.exists = true;
      // When a monitored file re-appears (rotation), start from beginning
      // because none of this new file's content has been read yet.
      logFile.lastPosition = 0;
      RustLogLimiter::logImportant("Log file appeared: " + logFile.path.toStdString());
    }

    qint64 currentSize = fileInfo.size();

    if (currentSize < logFile.lastPosition) {
      // File was truncated, reset
      logFile.lastPosition = 0;
      m_benchmarkDetectedActive = false;
      m_benchmarkPrepDetected = false;
      RustLogLimiter::logImportant("Log file reset detected: " + logFile.path.toStdString());
    }

    // Process new content from this file
    processNewLines(logFile);
  }
}

void RustLogMonitor::processNewLines(LogFileInfo& logFile) {
  RustLogLimiter::incrementCallCount();
  
  if (!logFile.file) {
    logFile.file = std::make_unique<QFile>(logFile.path);
  }

  if (!logFile.file->open(QIODevice::ReadOnly | QIODevice::Text)) {
    RustLogLimiter::logImportant("ERROR: Failed to open log file " + logFile.path.toStdString() + " - " + logFile.file->errorString().toStdString());
    return;
  }

  if (!logFile.file->seek(logFile.lastPosition)) {
    RustLogLimiter::logImportant("ERROR: Failed to seek to position " + std::to_string(logFile.lastPosition) + " in " + logFile.path.toStdString());
    logFile.file->close();
    return;
  }

  QTextStream stream(logFile.file.get());
  QString line;
  int linesRead = 0;

  while (stream.readLineInto(&line)) {
    linesRead++;
    if (!line.trimmed().isEmpty()) {
      processLine(line, logFile.path);
      emit logLineReceived(line);
    }
  }

  // Removed: status log for processed lines (spammy)
  
  logFile.lastPosition = logFile.file->pos();
  logFile.file->close();
}

QStringList RustLogMonitor::getLogFilePaths() const {
  QStringList paths;
  for (const auto& logFile : m_logFiles) {
    paths << logFile.path;
  }
  return paths;
}

void RustLogMonitor::processLine(const QString& line, const QString& sourceFile) {
  // Add source file info to debug output
  QString fileName = QFileInfo(sourceFile).baseName();
  
  if (line.contains(BENCHMARK_PREP_PATTERN, Qt::CaseInsensitive)) {
    LOG_DEBUG << "[DEBUG] Found prep pattern 'Threaded texture creation has been enabled!' in [log file name hidden for privacy] - was already detected=" << (m_benchmarkPrepDetected ? "true" : "false");
    if (!m_benchmarkPrepDetected) {
      RustLogLimiter::logImportant("[log file name hidden for privacy] Benchmark prep detected: " + BENCHMARK_PREP_PATTERN.toStdString());
      m_benchmarkPrepDetected = true;
      LOG_DEBUG << "[DEBUG] Prep flag set to TRUE";
    } else {
      LOG_DEBUG << "[DEBUG] Prep pattern found again, but flag already set";
    }
    return;
  }

  // Accept benchmark start from any demos/*.cfg, even if the line has prefixes like "***IMPORTANT***" or different cfg names.
  const bool matchesStart =
    line.contains(BENCHMARK_START_PREFIX, Qt::CaseInsensitive) ||
    BENCHMARK_START_REGEX.match(line).hasMatch();

  if (matchesStart) {
    // ALWAYS log when we see the cfg file message for debugging
    LOG_DEBUG << "[DEBUG] Found 'No cfg file found for demos: demos/*.cfg' in [log file name hidden for privacy] - prep_detected=" << (m_benchmarkPrepDetected ? "true" : "false") 
              << ", benchmark_active=" << (m_benchmarkDetectedActive ? "true" : "false");
    
    if (m_benchmarkPrepDetected && !m_benchmarkDetectedActive) {
      RustLogLimiter::logImportant("[log file name hidden for privacy] Benchmark started - triggering callbacks");
      m_benchmarkDetectedActive = true;
      // Start the auto-end timer if timer-based detection is enabled
      if (m_useTimerEndDetection) {
        m_benchmarkDurationTimer->start();
      }
      if (m_benchmarkStartCallback) {
        m_benchmarkStartCallback();
      }
      emit benchmarkStarted();
    } else if (!m_benchmarkPrepDetected && !m_benchmarkDetectedActive) {
      // WORKAROUND: If we see the start pattern but missed the prep pattern,
      // assume we can start the benchmark anyway (prep might have been missed due to log rotation/timing)
      RustLogLimiter::logImportant("[log file name hidden for privacy] WARNING: Start pattern found but prep not detected - starting anyway as fallback");
      LOG_DEBUG << "[DEBUG] FALLBACK: Starting benchmark without prep detection";
      m_benchmarkPrepDetected = true;  // Set it for consistency
      m_benchmarkDetectedActive = true;
      // Start the auto-end timer if timer-based detection is enabled
      if (m_useTimerEndDetection) {
        m_benchmarkDurationTimer->start();
      }
      if (m_benchmarkStartCallback) {
        m_benchmarkStartCallback();
      }
      emit benchmarkStarted();
    } else if (m_benchmarkDetectedActive) {
      RustLogLimiter::logImportant("[log file name hidden for privacy] WARNING: Start pattern found but benchmark already active");
    }
    return;
  }

  // Keep log-based end detection but disable it when timer-based detection is active
  if (line.contains(BENCHMARK_END_PATTERN)) {
    if (m_benchmarkDetectedActive && !m_useTimerEndDetection) {
      RustLogLimiter::logImportant("Benchmark completed (log-based detection)");
      LOG_DEBUG << "[DEBUG] Log-based end - resetting flags: prep_detected=false, benchmark_active=false";
      m_benchmarkDetectedActive = false;
      m_benchmarkPrepDetected = false;
      m_benchmarkDurationTimer->stop();  // Stop auto-end timer since we detected end
      if (m_benchmarkEndCallback) {
        m_benchmarkEndCallback();
      }
      emit benchmarkEnded();
    } else if (m_benchmarkDetectedActive && m_useTimerEndDetection) {
      RustLogLimiter::logImportant("End pattern detected but timer-based detection is active - ignoring");
    }
    return;
  }
}

void RustLogMonitor::checkForNewLogFiles() {
  if (!m_isMonitoring) {
    return;
  }
  
  size_t oldFileCount = m_logFiles.size();
  
  // Try to find both log files again
  QString outputLogPath = findOutputLogFile();
  QString playerLogPath = findPlayerLogFile();
  
  // Check if we found a new output_log.txt that we don't already have
  if (!outputLogPath.isEmpty()) {
    bool alreadyHave = false;
    for (const auto& logFile : m_logFiles) {
      if (logFile.path == outputLogPath) {
        alreadyHave = true;
        break;
      }
    }
    if (!alreadyHave) {
      LogFileInfo info;
      info.path = outputLogPath;
      info.file = std::make_unique<QFile>(outputLogPath);
      info.exists = QFile::exists(outputLogPath);
      if (info.exists) {
  qint64 size = info.file->size();
  // Start from EOF to avoid reading historical content
  info.lastPosition = size;
      } else {
        info.lastPosition = 0;
      }
      m_logFiles.push_back(std::move(info));
      
      // Add to file watcher
      if (info.exists && !m_fileWatcher->files().contains(outputLogPath)) {
        m_fileWatcher->addPath(outputLogPath);
      }
      
      RustLogLimiter::logImportant("NEW log file discovered: output_log.txt at " + outputLogPath.toStdString());
      LOG_DEBUG << "[DEBUG] NEW output_log.txt found and added to monitoring";
  // Do not process existing content; wait for new lines
    }
  }
  
  // Check if we found a new player.log that we don't already have
  if (!playerLogPath.isEmpty()) {
    bool alreadyHave = false;
    for (const auto& logFile : m_logFiles) {
      if (logFile.path == playerLogPath) {
        alreadyHave = true;
        break;
      }
    }
    if (!alreadyHave) {
      LogFileInfo info;
      info.path = playerLogPath;
      info.file = std::make_unique<QFile>(playerLogPath);
      info.exists = QFile::exists(playerLogPath);
      if (info.exists) {
  qint64 size = info.file->size();
  // Start from EOF to avoid reading historical content
  info.lastPosition = size;
      } else {
        info.lastPosition = 0;
      }
      m_logFiles.push_back(std::move(info));
      
      // Add to file watcher
      if (info.exists && !m_fileWatcher->files().contains(playerLogPath)) {
        m_fileWatcher->addPath(playerLogPath);
      }
      
      RustLogLimiter::logImportant("NEW log file discovered: player.log at " + playerLogPath.toStdString());
      LOG_DEBUG << "[DEBUG] NEW player.log found and added to monitoring";
  // Do not process existing content; wait for new lines
    }
  }
  
  if (m_logFiles.size() != oldFileCount) {
    LOG_DEBUG << "[DEBUG] File discovery check complete - now monitoring " 
              << m_logFiles.size() << " files (was " << oldFileCount << ")";
  }
}

void RustLogMonitor::resetForNextRun() {
  // Reset internal flags without stopping monitoring
  m_benchmarkDetectedActive = false;
  m_benchmarkPrepDetected = false;
  if (m_benchmarkDurationTimer) {
    m_benchmarkDurationTimer->stop();
  }
  // IMPORTANT: Do NOT change lastPosition here. We continue monitoring and
  // only read content appended after the last processed position, ensuring
  // we don't skip lines that arrive right at run boundaries.
}
