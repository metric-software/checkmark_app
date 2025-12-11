#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QObject>
#include <QStandardPaths>
#include <QString>
#include <QRegularExpression>
#include <QTextStream>
#include <QTimer>

class RustLogMonitor : public QObject {
  Q_OBJECT

 public:
  // Dual log file tracking
  struct LogFileInfo {
    QString path;
    std::unique_ptr<QFile> file;
    qint64 lastPosition;
    bool exists;
  };

  explicit RustLogMonitor(QObject* parent = nullptr);
  ~RustLogMonitor();

  // Start/stop monitoring
  bool startMonitoring();
  void stopMonitoring();
  bool isMonitoring() const { return m_isMonitoring; }

  // Prepare for a new benchmark run without stopping monitoring
  void resetForNextRun();

  // Set callbacks for benchmark events
  void setBenchmarkStartCallback(std::function<void()> callback);
  void setBenchmarkEndCallback(std::function<void()> callback);

  // Configuration methods
  void setUseTimerEndDetection(bool enabled) { m_useTimerEndDetection = enabled; }
  bool getUseTimerEndDetection() const { return m_useTimerEndDetection; }

  // Get the log file paths
  QStringList getLogFilePaths() const;

 signals:
  void benchmarkStarted();
  void benchmarkEnded();
  void logLineReceived(const QString& line);

 private slots:
  void checkForNewContent();
  void onFileChanged(const QString& path);
  void checkForNewLogFiles();

 private:
  void findAndSetupLogFiles();
  QString findOutputLogFile();
  QString findPlayerLogFile();
  void processNewLines(LogFileInfo& logFile);
  void processLine(const QString& line, const QString& sourceFile);
  std::vector<LogFileInfo> m_logFiles;

  // Monitoring state
  std::atomic<bool> m_isMonitoring{false};
  std::unique_ptr<QTimer> m_pollTimer;
  std::unique_ptr<QFileSystemWatcher> m_fileWatcher;
  std::unique_ptr<QTimer> m_benchmarkDurationTimer;  // Timer for auto-ending benchmark
  std::unique_ptr<QTimer> m_fileDiscoveryTimer;      // Timer for periodic log file discovery

  // Callbacks
  std::function<void()> m_benchmarkStartCallback;
  std::function<void()> m_benchmarkEndCallback;

  // Benchmark detection patterns - two-stage detection
  static const QString BENCHMARK_PREP_PATTERN;   // "Threaded texture creation has been enabled!"
  static const QString BENCHMARK_START_PREFIX;   // "No cfg file found for demos: demos/"
  static const QRegularExpression BENCHMARK_START_REGEX;  // Match any demos/*.cfg
  static const QString BENCHMARK_END_PATTERN;    // "Playing Video"

  // State tracking
  bool m_benchmarkDetectedActive = false;
  bool m_benchmarkPrepDetected = false;  // First stage detected
  bool m_useTimerEndDetection = true;    // Use timer instead of log pattern for end detection
};
