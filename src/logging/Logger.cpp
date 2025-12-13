#include "Logger.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace std::chrono;

Logger& Logger::instance() {
  static Logger inst;
  return inst;
}

Logger::Logger()
    : currentLevel_(static_cast<LogLevel>(HARDCODED_LOG_LEVEL)),
      initialized_(false),
      maxQueueSize_(16384),
      running_(false) {}

Logger::~Logger() {
  shutdown();
}

void Logger::init(const std::string& logPath, const std::string& crashPath,
                  LogLevel level, size_t maxQueue) {
  std::lock_guard<std::mutex> lock(queueMutex_);
  
  if (initialized_.load()) {
    return; // Already initialized
  }

  auto basenameOnly = [](const std::string& path) -> std::string {
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash == std::string::npos) {
      return path;
    }
    return path.substr(lastSlash + 1);
  };

  setLevel(level);
  maxQueueSize_ = maxQueue;
  
  // Open log files if paths provided
  if (!logPath.empty()) {
    std::lock_guard<std::mutex> fileLock(fileMutex_);
    fileSink_.open(logPath, std::ios::out | std::ios::app);
    if (!fileSink_.is_open()) {
      // Avoid printing absolute paths (may contain personal info such as usernames).
      std::cerr << "Failed to open log file: " << basenameOnly(logPath) << std::endl;
    }
  }
  
  if (!crashPath.empty()) {
    std::lock_guard<std::mutex> fileLock(fileMutex_);
    crashFileSink_.open(crashPath, std::ios::out | std::ios::app);
    if (!crashFileSink_.is_open()) {
      // Avoid printing absolute paths (may contain personal info such as usernames).
      std::cerr << "Failed to open crash log file: " << basenameOnly(crashPath) << std::endl;
    }
  }
  
  // Start worker thread
  running_ = true;
  worker_ = std::thread(&Logger::workerThreadFunc, this);
  
  initialized_ = true;
}

void Logger::setLevel(LogLevel level) { 
  currentLevel_.store(level); 
}

LogLevel Logger::getLevel() const { 
  return currentLevel_.load(); 
}

void Logger::submitAsync(const LogEntry& e) {
  if (!initialized_.load()) {
    return; // Logger not initialized yet
  }
  
  if (e.level < currentLevel_.load()) {
    return; // Below current log level
  }
  
  {
    std::lock_guard<std::mutex> lk(queueMutex_);
    if (queue_.size() >= maxQueueSize_) {
      // drop policy: drop current (silently)
      return;
    }
    queue_.push(e);
  }
  queueCv_.notify_one();
}

void Logger::submitStr(LogLevel level, const std::string& text,
                       const char* file, const char* function, int line) {
  if (!initialized_.load()) {
    return; // Logger not initialized yet
  }
  
  LogEntry e;
  e.level = level;
  e.message = text;
  e.file = file ? file : "";
  e.function = function ? function : "";
  e.line = line;
  e.timestamp_ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  
  // Get thread ID in a safer way
  std::ostringstream tid;
  tid << std::this_thread::get_id();
  try {
    e.thread_id = std::stoull(tid.str());
  } catch (...) {
    e.thread_id = 0; // fallback
  }
  
  submitAsync(e);
}

void Logger::workerThreadFunc() {
  while (running_) {
    std::unique_lock<std::mutex> lk(queueMutex_);
    queueCv_.wait(lk, [this] { return !queue_.empty() || !running_; });
    
    while (!queue_.empty()) {
      LogEntry e = std::move(queue_.front());
      queue_.pop();
      lk.unlock();
      
      // write to sinks
      try {
        writeToConsole(e);
        writeToFile(e);
      } catch (...) {
        // Swallow errors; this is logging - don't let logging errors crash the app
      }
      lk.lock();
    }
  }
  
  // flush remaining on shutdown
  std::queue<LogEntry> remaining;
  {
    std::lock_guard<std::mutex> lk(queueMutex_);
    remaining = std::move(queue_);
  }
  
  while (!remaining.empty()) {
    try {
      writeToConsole(remaining.front());
      writeToFile(remaining.front());
    } catch (...) {
      // Swallow errors during shutdown too
    }
    remaining.pop();
  }
  
  // ensure file flush
  std::lock_guard<std::mutex> fsl(fileMutex_);
  if (fileSink_.is_open()) {
    fileSink_.flush();
  }
  if (crashFileSink_.is_open()) {
    crashFileSink_.flush();
  }
}

void Logger::writeToConsole(const LogEntry& e) {
  std::string s = formatEntry(e);
  // Use cout which will be redirected by the existing ConsoleOutputBuf system
  std::cout << s << std::endl;
}

void Logger::writeToFile(const LogEntry& e) {
  std::lock_guard<std::mutex> lk(fileMutex_);
  if (fileSink_.is_open()) {
    fileSink_ << formatEntry(e) << '\n';
    // Flush periodically for important messages
    if (e.level >= WARN_LEVEL) {
      fileSink_.flush();
    }
  }
}

void Logger::writeToCrashFileSync(const LogEntry& e) {
  std::lock_guard<std::mutex> lk(fileMutex_);
  std::string formatted = formatEntry(e);
  
  if (crashFileSink_.is_open()) {
    crashFileSink_ << formatted << std::endl;
    crashFileSink_.flush();
  } else if (fileSink_.is_open()) {
    // fallback to regular file if crash file not present
    fileSink_ << "[CRASH] " << formatted << std::endl;
    fileSink_.flush();
  } else {
    // as a last resort, write to cerr synchronously
    std::cerr << "[CRASH] " << formatted << std::endl;
  }
}

void Logger::writeCrashSync(const LogEntry& e) { 
  writeToCrashFileSync(e); 
}

std::string Logger::formatEntry(const LogEntry& e) {
  std::ostringstream oss;
  
  // Format timestamp
  auto ms = e.timestamp_ms;
  std::time_t t = ms / 1000;
  std::tm tm;
  
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  
  oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "." 
      << std::setfill('0') << std::setw(3) << (ms % 1000);
  
  // Log level names
  static const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
  oss << " [" << names[e.level] << "]";
  
  // Thread ID
  if (e.thread_id != 0) {
    oss << " [tid=" << e.thread_id << "]";
  }
  
  // File location info (only include filename, not full path)
  if (!e.file.empty()) {
    std::string filename = e.file;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
      filename = filename.substr(lastSlash + 1);
    }
    oss << " (" << filename;
    if (e.line > 0) {
      oss << ":" << e.line;
    }
    if (!e.function.empty()) {
      oss << " " << e.function;
    }
    oss << ")";
  }
  
  oss << " " << e.message;
  return oss.str();
}

void Logger::shutdown() {
  if (!running_.load()) {
    return;
  }
  
  running_ = false;
  queueCv_.notify_all();
  
  if (worker_.joinable()) {
    worker_.join();
  }
  
  std::lock_guard<std::mutex> lk(fileMutex_);
  if (fileSink_.is_open()) {
    fileSink_.flush();
    fileSink_.close();
  }
  if (crashFileSink_.is_open()) {
    crashFileSink_.flush();
    crashFileSink_.close();
  }
  
  initialized_ = false;
}

// LogMessageBuilder implementation
LogMessageBuilder::LogMessageBuilder(LogLevel lvl, const char* file,
                                     const char* func, int line)
    : level_(lvl), file_(file), func_(func), line_(line) {}

LogMessageBuilder::~LogMessageBuilder() {
  try {
    Logger& logger = Logger::instance();
    if (logger.isInitialized()) {
      logger.submitStr(level_, oss_.str(), file_, func_, line_);
      
      if (level_ == FATAL_LEVEL) {
        // ensure FATALs are forced to crash sink synchronously too
        LogEntry e;
        e.level = level_;
        e.message = oss_.str();
        e.file = file_ ? file_ : "";
        e.function = func_ ? func_ : "";
        e.line = line_;
        e.timestamp_ms = duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()).count();
        
        std::ostringstream tid;
        tid << std::this_thread::get_id();
        try {
          e.thread_id = std::stoull(tid.str());
        } catch (...) {
          e.thread_id = 0;
        }
        
        logger.writeCrashSync(e);
      }
    } else {
      // Fallback to cout if logger not initialized
      static const char* names[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
      std::cout << "[" << names[level_] << "] " << oss_.str() << std::endl;
    }
  } catch (...) {
    // nothing to do - don't let logging errors propagate
  }
}
