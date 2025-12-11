#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>

#include <QApplication>
#include <QMessageBox>
#include <QStandardPaths>
#include <Windows.h>
#include <QDir>
#include <QCommandLineParser>
#include <QFile>

#include "nvapi.h"
#include "NvApiDriverSettings.h"
#include "ApplicationSettings.h"
#include "hardware/ConstantSystemInfo.h"
#include "hardware/SystemMetricsValidator.h"
#include "optimization/BackupManager.h"
#include "optimization/NvidiaControlPanel.h"
#include "optimization/NvidiaOptimization.h"
#include "optimization/OptimizationEntity.h"
#include "optimization/RegistryLogger.h"
#include "profiles/UserSystemProfile.h"
#include "ui/CustomConsoleWindow.h"
#include "ui/LoadingWindow.h"
#include "ui/MainWindow.h"
#include "ui/TermsOfServiceDialog.h"
#include "hardware/PdhInterface.h"
#include "logging/Logger.h"
#include "network/MenuManager.h"
#include "network/core/FeatureToggleManager.h"

// Global file stream for crash handler (legacy backup system)
std::ofstream g_crash_log_file;

// Global variables for cleanup
std::streambuf* original_cout_buf = nullptr;

// Qt message handler function
void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
  // Log Qt messages to new logger system first, then legacy
  if (Logger::instance().isInitialized()) {
    switch (type) {
      case QtDebugMsg:
        LOG_DEBUG << "Qt: " << msg.toStdString();
        break;
      case QtWarningMsg:
        LOG_WARN << "Qt: " << msg.toStdString();
        break;
      case QtCriticalMsg:
        LOG_ERROR << "Qt: " << msg.toStdString();
        break;
      case QtFatalMsg:
        LOG_FATAL << "Qt: " << msg.toStdString();
        break;
      default:
        break;
    }
  }
  
  // Also keep legacy logging behavior (only log warnings, critical and fatal messages to reduce spam)
  if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
    std::string prefix;

    switch (type) {
      case QtWarningMsg:
        prefix = "[Warning] ";
        break;
      case QtCriticalMsg:
        prefix = "[Error] ";
        break;
      case QtFatalMsg:
        prefix = "[Fatal] ";
        break;
      default:
        return;  // Skip other message types
    }

    // Keep legacy behavior for Qt messages
    std::cout << prefix << msg.toStdString() << std::endl;
  }

  // If it's a fatal message, also log to crash file (legacy backup)
  if (type == QtFatalMsg && g_crash_log_file.is_open()) {
    g_crash_log_file << "Qt Fatal Error: " << msg.toStdString()
                     << std::endl;
    g_crash_log_file.flush();
  }
}

// Update signal handler
void signalHandler(int signum) {
  LOG_INFO << "Signal " << signum << " received. Shutting down gracefully...";

  // Clean up resources
  if (g_crash_log_file.is_open()) {
    g_crash_log_file.flush();
    g_crash_log_file.close();
  }

  // Restore original console buffer if needed
  if (std::cout.rdbuf() != nullptr && original_cout_buf != nullptr) {
    std::cout.rdbuf(original_cout_buf);
  }

  // Exit application
  exit(signum);
}

// Update exception filter
LONG WINAPI CustomUnhandledExceptionFilter(EXCEPTION_POINTERS* pExceptionInfo) {
  // Get exception code
  DWORD exceptionCode = pExceptionInfo->ExceptionRecord->ExceptionCode;

  // Log the exception to both new logger and legacy system
  if (Logger::instance().isInitialized()) {
    // Use new logger for crash reporting
    LogEntry crashEntry;
    crashEntry.level = FATAL_LEVEL;
    crashEntry.message = "CRASH DETECTED - Exception code: 0x" + 
                        std::to_string(exceptionCode) + " at address: 0x" + 
                        std::to_string(reinterpret_cast<uintptr_t>(pExceptionInfo->ExceptionRecord->ExceptionAddress));
    crashEntry.file = "main.cpp";
    crashEntry.function = "CustomUnhandledExceptionFilter";
    crashEntry.line = __LINE__;
    crashEntry.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    crashEntry.thread_id = 0;
    
    Logger::instance().writeCrashSync(crashEntry);
  }
  
  // Also use legacy crash logging as backup
  if (g_crash_log_file.is_open()) {
    g_crash_log_file << "\n\n==== CRASH DETECTED ====\n";
    g_crash_log_file << "Exception code: 0x" << std::hex << exceptionCode
                     << std::dec << "\n";

    // Common exception codes
    switch (exceptionCode) {
      case EXCEPTION_ACCESS_VIOLATION:
        g_crash_log_file << "Description: ACCESS VIOLATION\n";
        break;
      case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        g_crash_log_file << "Description: ARRAY BOUNDS EXCEEDED\n";
        break;
      case EXCEPTION_BREAKPOINT:
        g_crash_log_file << "Description: BREAKPOINT\n";
        break;
      case EXCEPTION_DATATYPE_MISALIGNMENT:
        g_crash_log_file << "Description: DATATYPE MISALIGNMENT\n";
        break;
      case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        g_crash_log_file << "Description: FLOAT DIVIDE BY ZERO\n";
        break;
      case EXCEPTION_ILLEGAL_INSTRUCTION:
        g_crash_log_file << "Description: ILLEGAL INSTRUCTION\n";
        break;
      case EXCEPTION_IN_PAGE_ERROR:
        g_crash_log_file << "Description: IN PAGE ERROR\n";
        break;
      case EXCEPTION_INT_DIVIDE_BY_ZERO:
        g_crash_log_file << "Description: INTEGER DIVIDE BY ZERO\n";
        break;
      case EXCEPTION_STACK_OVERFLOW:
        g_crash_log_file << "Description: STACK OVERFLOW\n";
        break;
      default:
        g_crash_log_file << "Description: UNKNOWN EXCEPTION\n";
        break;
    }

    // Get exception address
    g_crash_log_file << "Exception address: 0x" << std::hex
                     << pExceptionInfo->ExceptionRecord->ExceptionAddress
                     << std::dec << "\n";

    // Force flush and close
    g_crash_log_file.flush();
    g_crash_log_file.close();
  }

  // Restore original console buffer if needed
  if (std::cout.rdbuf() != nullptr && original_cout_buf != nullptr) {
    std::cout.rdbuf(original_cout_buf);
  }

  // Wait for user before closing
  MessageBoxA(NULL, "Application has crashed. Check log file for details.",
              "Application Crash", MB_OK | MB_ICONERROR);

  return EXCEPTION_CONTINUE_SEARCH;  // Let Windows handle the exception
}

// Get executable path
std::filesystem::path getExecutablePath() {
  wchar_t path[MAX_PATH];
  GetModuleFileNameW(NULL, path, MAX_PATH);
  return std::filesystem::path(path).parent_path();
}

// Forward declaration for main_impl
int main_impl(int argc, char* argv[]);

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
  // Convert Windows command line to standard argc/argv
  int argc;
  LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::string> utf8Args(argc);
  std::vector<char*> argvA(argc + 1, nullptr);

  for (int i = 0; i < argc; i++) {
    int size = WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, nullptr, 0,
                                   nullptr, nullptr);
    utf8Args[i].resize(size);
    WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, &utf8Args[i][0], size,
                        nullptr, nullptr);
    argvA[i] = &utf8Args[i][0];
  }

  LocalFree(argvW);

  // Call the actual main function with the converted arguments
  return main_impl(argc, argvA.data());
}

// Rename the original main function
int main_impl(int argc, char* argv[])
#else
int main(int argc, char* argv[])
#endif
{
  // Save original cout buffer at the very start
  original_cout_buf = std::cout.rdbuf();

  // Register signal handlers
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);
  signal(SIGABRT, signalHandler);

  // Set application information
  QCoreApplication::setApplicationName("checkmark");
  QCoreApplication::setOrganizationName("checkmark");

  // Initialize QApplication early so we can create UI elements
  std::unique_ptr<QApplication> app =
    std::make_unique<QApplication>(argc, argv);
  app->setApplicationDisplayName("checkmark");

  // Ensure we have initial backups for all settings types
  // optimizations::BackupManager::GetInstance().Initialize();
  // optimizations::BackupManager::GetInstance().CreateInitialBackups();

  // Set process priority based on user setting
  if (ApplicationSettings::getInstance().getElevatedPriorityEnabled()) {
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    LOG_INFO << "Process priority set to ABOVE_NORMAL";
  } else {
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    LOG_INFO << "Process priority set to NORMAL";
  }

  // Configure logging
  const bool enable_logging_file = true;

  // Store original exception filter to restore on clean exit
  LPTOP_LEVEL_EXCEPTION_FILTER originalExceptionFilter = nullptr;

  // File output variables
  std::ofstream log_file;
  ConsoleOutputBuf* console_output_buf = nullptr;

  // Connect cleanup handler to application exit
  QObject::connect(app.get(), &QApplication::aboutToQuit, [&]() {
    try {
      LOG_INFO << "Application shutting down, performing cleanup...";

      // Clean up the custom console window
      CustomConsoleWindow::cleanup();

      // Restore original exception filter during clean exit
      if (originalExceptionFilter != nullptr) {
        SetUnhandledExceptionFilter(originalExceptionFilter);
        originalExceptionFilter = nullptr;
      }

      // Clean up logging
      if (enable_logging_file) {
        if (Logger::instance().isInitialized()) {
          LOG_INFO << "=== New Logger System shutting down ===";
          Logger::instance().shutdown();
        }
        
        // Keep this std::cout for legacy system cleanup message
        std::cout << "=== Log ended ===" << std::endl;

        // Restore original cout buffer first
        std::cout.rdbuf(original_cout_buf);

        // Delete custom output buffer
        delete console_output_buf;
        console_output_buf = nullptr;

        // Close files
        if (log_file.is_open()) {
          log_file.flush();
          log_file.close();
        }

        if (g_crash_log_file.is_open()) {
          g_crash_log_file.flush();
          g_crash_log_file.close();
        }
      }

      LOG_INFO << "Cleanup complete.";
    } catch (const std::exception& e) {
      LOG_ERROR << "Error during shutdown cleanup: " << e.what();
    } catch (...) {
      LOG_ERROR << "Unknown error during shutdown cleanup";
    }
  });

  try {
    // Create our custom console window first
    CustomConsoleWindow* customConsole = CustomConsoleWindow::getInstance();

    // Set up file logging if enabled
    if (enable_logging_file) {
      // Create debug logging directory
      auto exe_path = getExecutablePath();
      std::filesystem::path log_dir = exe_path / "debug logging";

      if (!std::filesystem::exists(log_dir)) {
        std::filesystem::create_directory(log_dir);
      }

      // Create timestamp for log filename
      auto now = std::time(nullptr);
      auto tm = *std::localtime(&now);
      std::ostringstream timestamp;
      timestamp << std::put_time(&tm, "%Y%m%d_%H%M%S");

      // Create log file path and open the file
      std::filesystem::path log_path =
        log_dir / ("log_" + timestamp.str() + ".txt");
      log_file.open(log_path, std::ios::out);

      // Set global crash log file
      g_crash_log_file.open(log_dir / ("crash_" + timestamp.str() + ".txt"),
                            std::ios::out);

      // Install unhandled exception filter and save the original
      originalExceptionFilter =
        SetUnhandledExceptionFilter(CustomUnhandledExceptionFilter);

      if (log_file.is_open()) {
        // Set up custom output buffer for std::cout
        console_output_buf = new ConsoleOutputBuf(log_file.rdbuf());

        // Redirect stdout
        std::cout.rdbuf(console_output_buf);

        // Initialize new logger system first
        std::filesystem::path new_log_path = log_dir / ("new_log_" + timestamp.str() + ".txt");
        std::filesystem::path new_crash_path = log_dir / ("new_crash_" + timestamp.str() + ".txt");
        
        // Check if detailed logs are enabled in settings
        ApplicationSettings& settings = ApplicationSettings::getInstance();
        LogLevel logLevel = settings.getDetailedLogsEnabled() ? TRACE_LEVEL : ERROR_LEVEL;
        
      Logger::instance().init(new_log_path.string(), new_crash_path.string(), logLevel);
          
          // Log initial info to both systems
      std::cout << "=== Log started at "
                << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
                     << " ===" << std::endl;
                    
          LOG_INFO << "=== New Logger System started at " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << " ===";
          LOG_INFO << "Legacy logging system kept as backup";
        }
      }

      // Developer bypass: if a SECRETS file exists next to the executable and
      // contains the magic phrase, unlock all remote-gated experimental and
      // upload features regardless of backend status.
      {
        bool devBypass = false;
        QString secretsPath = QCoreApplication::applicationDirPath() + "/SECRETS";
        QFile secretsFile(secretsPath);
        if (secretsFile.exists() && secretsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
          QByteArray content = secretsFile.readAll();
          if (content.contains("CHECKMARK_DEV_BYPASS")) {
            devBypass = true;
          }
          secretsFile.close();
        }

        if (devBypass) {
          ApplicationSettings::getInstance().setDeveloperBypassEnabled(true);
          LOG_WARN << "Developer bypass ENABLED via SECRETS file";
        }
      }
  
      // Fetch remote feature flags from backend. If the backend is offline or
      // returns an invalid response, all remote-controlled features will be
      // treated as disabled for this run.
      {
      FeatureToggleManager featureToggleManager;
      featureToggleManager.fetchAndApplyRemoteFlags();
    }

    // Set custom console visibility based on settings
    customConsole->setVisibilityFromSettings();

    // Initialize UserSystemProfile
    QString appDataPath = QCoreApplication::applicationDirPath();
    QString profilePath = appDataPath + "/profiles";

    // Create directory if it doesn't exist (using QDir directly)
    QDir(profilePath).mkpath(".");

    // Initialize the user profile
    auto& userProfile = SystemMetrics::UserSystemProfile::getInstance();
    userProfile.initialize();

    // Save the profile using the standard location
    userProfile.saveToFile(userProfile.getDefaultProfilePath());

    // Create and show loading window
    LoadingWindow loadingWindow;
    loadingWindow.show();
    loadingWindow.setStatusMessage("Starting application...");
    loadingWindow.setProgress(0);

    // Set Qt message handler to redirect to both logging systems
    qInstallMessageHandler(qtMessageHandler);

    // Update loading window
    loadingWindow.setStatusMessage("Collecting system information...");
    loadingWindow.setProgress(5);

    // Update loading window
    loadingWindow.setStatusMessage("Initializing hardware monitoring...");
    loadingWindow.setProgress(10);

    // Collect constant system info
    SystemMetrics::CollectConstantSystemInfo();

    // Check if metrics validation should run on startup
    if (ApplicationSettings::getInstance().getValidateMetricsOnStartup()) {
      // Update loading window
      loadingWindow.setStatusMessage("Validating system metrics providers...");

      // System metrics validation with progress callback
      LOG_INFO << "Running optimized system metrics validation...";
      SystemMetrics::SystemMetricsValidator::getInstance()
        .validateAllMetricsProviders(
          [&loadingWindow](int progress, const std::string& message) {
            // Map progress from validator (0-100) to our range (10-60)
            int adjustedProgress = 10 + (progress * 50) / 100;
            loadingWindow.setProgress(adjustedProgress);
            loadingWindow.setStatusMessage(QString::fromStdString(message));
          });
    } else {
      LOG_INFO << "Skipping system metrics validation (disabled in settings)";
      loadingWindow.setProgress(60);  // Skip to post-validation progress
    }

    // Update loading window - post validation
    loadingWindow.setStatusMessage("Initializing optimization systems...");
    loadingWindow.setProgress(70);

    // Check terms of service
    bool needToShowTerms =
      !ApplicationSettings::getInstance().hasAcceptedTerms();

    // Setup optimizations
    LOG_INFO << "Initializing optimization systems...";
    optimizations::OptimizationManager::GetInstance().Initialize();
    loadingWindow.setProgress(85);

    // Finalizing initialization
    loadingWindow.setStatusMessage("Finalizing startup...");
    loadingWindow.setProgress(95);

    // Finish loading
    loadingWindow.setProgress(100);
    loadingWindow.setStatusMessage("Preparing to launch application...");

    // Show terms of service dialog if needed
    if (needToShowTerms) {
      loadingWindow.hide();
      TermsOfServiceDialog tosDialog;
      if (tosDialog.exec() != QDialog::Accepted) {
        // User declined the terms
        return 0;  // Exit application
      }
    } else {
      loadingWindow.hide();
    }

    // Initialize MenuManager for centralized menu fetching
    LOG_INFO << "Initializing MenuManager for diagnostic and benchmark menus...";
    MenuManager::getInstance().initialize();

    // Create the main window
    MainWindow w;
    w.show();

    return app->exec();
  } catch (const std::exception& e) {
    // Make sure we're writing to the console for error messages
    std::cout.rdbuf(original_cout_buf);

    std::string error_msg = "Application failed to start: ";
    error_msg += e.what();

    std::cout << "[ERROR] " << error_msg << std::endl;
    
    // Also try to log via new logger if possible
    LOG_ERROR << error_msg;

    if (log_file.is_open()) {
      log_file << "[ERROR] " << error_msg << std::endl;
      log_file.flush();
      log_file.close();
    }

    if (g_crash_log_file.is_open()) {
      g_crash_log_file << "CAUGHT EXCEPTION: " << error_msg << std::endl;
      g_crash_log_file.flush();
      g_crash_log_file.close();
    }

    QMessageBox::critical(nullptr, "Error", QString::fromStdString(error_msg));

    delete console_output_buf;
    console_output_buf = nullptr;

    return 1;
  }
}
