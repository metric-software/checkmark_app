#pragma once
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <QString>
#include <thread>
#include <vector>

// Hardcoded log level - can be changed here for now
constexpr int HARDCODED_LOG_LEVEL = 1; // 0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL

enum LogLevel { TRACE_LEVEL=0, DEBUG_LEVEL, INFO_LEVEL, WARN_LEVEL, ERROR_LEVEL, FATAL_LEVEL };

struct LogEntry {
  LogLevel level;
  std::string message;
  std::string file;
  std::string function;
  int line;
  uint64_t timestamp_ms;
  uint64_t thread_id;
};

class Logger {
 public:
  static Logger& instance();

  // initialize logger; if logPath empty -> no file sink
  void init(const std::string& logPath = "", const std::string& crashPath = "",
            LogLevel level = static_cast<LogLevel>(HARDCODED_LOG_LEVEL), size_t maxQueue = 16384);

  // change runtime level
  void setLevel(LogLevel level);
  LogLevel getLevel() const;

  // Submit asynchronously; will be filtered by current level
  void submitAsync(const LogEntry& e);

  // Synchronous crash write (guaranteed): bypass queue, flush immediately
  void writeCrashSync(const LogEntry& e);

  // Flush pending async logs and stop worker
  void shutdown();

  // helper used by LogMessageBuilder
  void submitStr(LogLevel level, const std::string& text,
                 const char* file, const char* function, int line);

  // Check if the logger is initialized
  bool isInitialized() const { return initialized_.load(); }

 private:
  Logger();
  ~Logger();

  void workerThreadFunc();

  // sinks
  void writeToConsole(const LogEntry& e);
  void writeToFile(const LogEntry& e);          // async file sink
  void writeToCrashFileSync(const LogEntry& e); // sync crash sink

  // formatting
  std::string formatEntry(const LogEntry& e);

  // state
  std::atomic<LogLevel> currentLevel_;
  std::atomic<bool> initialized_;
  std::mutex queueMutex_;
  std::condition_variable queueCv_;
  std::queue<LogEntry> queue_;
  size_t maxQueueSize_;
  std::thread worker_;
  std::atomic<bool> running_;

  // file sinks
  std::mutex fileMutex_;
  std::ofstream fileSink_;
  std::ofstream crashFileSink_;
};
 
// stream builder to produce messages with << and submit on destruction
class LogMessageBuilder {
 public:
  LogMessageBuilder(LogLevel lvl, const char* file, const char* func, int line);
  ~LogMessageBuilder();
  
  template <typename T>
  LogMessageBuilder& operator<<(T&& v) {
    oss_ << std::forward<T>(v);
    return *this;
  }

  // Overload for QString to convert to std::string
  LogMessageBuilder& operator<<(const QString& str) {
    oss_ << str.toStdString();
    return *this;
  }

 private:
  LogLevel level_;
  const char* file_;
  const char* func_;
  int line_;
  std::ostringstream oss_;
};

// convenience macros for usage - these automatically handle initialization check
#define LOG_TRACE LogMessageBuilder(TRACE_LEVEL, __FILE__, __func__, __LINE__)
#define LOG_DEBUG LogMessageBuilder(DEBUG_LEVEL, __FILE__, __func__, __LINE__)
#define LOG_INFO  LogMessageBuilder(INFO_LEVEL,  __FILE__, __func__, __LINE__)
#define LOG_WARN  LogMessageBuilder(WARN_LEVEL,  __FILE__, __func__, __LINE__)
#define LOG_ERROR LogMessageBuilder(ERROR_LEVEL, __FILE__, __func__, __LINE__)
#define LOG_FATAL LogMessageBuilder(FATAL_LEVEL, __FILE__, __func__, __LINE__)

