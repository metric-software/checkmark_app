#include "DemoFileManager.h"

#include <iostream>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStorageInfo>  // Add this header
#include <QSysInfo>
#include <windows.h>

#include "ApplicationSettings.h"
#include "../logging/Logger.h"

static const QString BENCHMARK_FILE_PATTERN = "benchmark_*.dem";
static const QString BENCHMARK_PREFIX = "benchmark_";
static const QString DEM_EXTENSION = ".dem";

DemoFileManager::DemoFileManager(QObject* parent)
    : QObject(parent), m_benchmarkFileName("benchmark") {
  // Ensure we always have a valid benchmark filename, defaulting to benchmark
  if (m_benchmarkFileName.isEmpty()) {
    m_benchmarkFileName = "benchmark";
  }
}

QString DemoFileManager::findLatestBenchmarkFile() const {
  // Get application directory path and the benchmark_demos subdirectory
  QString exePath = QCoreApplication::applicationDirPath();
  QString benchmarkDemosPath = exePath + "/benchmark_demos";
  QDir benchmarkDir(benchmarkDemosPath);

  // Check if the benchmark_demos directory exists
  if (benchmarkDir.exists()) {
    // Find benchmark file with highest number
    QStringList demFiles = benchmarkDir.entryList(
      QStringList() << BENCHMARK_FILE_PATTERN, QDir::Files);

    if (!demFiles.isEmpty()) {
      // Find benchmark with highest number
      int highestNum = 0;
      QString bestFile;

      for (const QString& file : demFiles) {
        // Extract the number between "benchmark_" and ".dem"
        QString numStr = file;
        numStr.remove(BENCHMARK_PREFIX).remove(DEM_EXTENSION);
        bool ok;
        int num = numStr.toInt(&ok);
        if (ok && num > highestNum) {
          highestNum = num;
          bestFile = file;
        }
      }

      if (!bestFile.isEmpty()) {
        return bestFile.left(bestFile.length() - 4);  // Remove .dem extension
      }

      // Check specifically for benchmark.dem as fallback
      if (demFiles.contains("benchmark.dem")) {
        return "benchmark";
      }
    }
  }

  // If no files found, default to benchmark instead of benchmark_not_found
  return "benchmark";
}

bool DemoFileManager::copyDemoFile(const QString& destPath) {
  // Find source file
  QString sourcePath = findSourceDemoFile();
  if (sourcePath.isEmpty()) {
    LOG_ERROR << "[ERROR] Source demo file not found";
    return false;
  }

  // Validate source file
  if (!validateDemoFile(sourcePath)) {
    LOG_ERROR << "[ERROR] Source demo file validation failed: "
              << sourcePath.toStdString();
    return false;
  }

  // Ensure destination directory exists
  QString destDir = QFileInfo(destPath).path();
  if (!ensureDirectoryExists(destDir)) {
    LOG_ERROR << "[ERROR] Failed to create destination directory: "
              << destDir.toStdString();
    return false;
  }

  // If destination already exists and is valid, consider it success
  if (QFileInfo::exists(destPath) && validateDemoFile(destPath)) {
    return true;
  }

  // Remove existing file if present
  if (QFile::exists(destPath)) {
    if (!QFile::remove(destPath)) {
      LOG_ERROR << "[ERROR] Failed to remove existing file: "
                << destPath.toStdString();
      return false;
    }
  }

  // Perform copy
  if (!QFile::copy(sourcePath, destPath)) {
    LOG_ERROR << "[ERROR] Failed to copy file from [source path hidden for privacy] to [dest path hidden for privacy]";
    return false;
  }

  // Verify copied file
  if (!validateDemoFile(destPath)) {
    LOG_ERROR << "[ERROR] Copied file validation failed: "
              << destPath.toStdString();
    QFile::remove(destPath);
    return false;
  }

  return true;
}

// Update this function to use "/demos" subdirectory if needed
bool DemoFileManager::copyDemoFiles(const QString& destPath) {
  // Find source files
  QStringList sourcePaths = findSourceDemoFiles();
  LOG_INFO << "Source paths found: " << sourcePaths.join(", ").toStdString();

  if (sourcePaths.isEmpty()) {
    LOG_ERROR << "[ERROR] No source demo files found";
    return false;
  }

  // Ensure destination directory exists (use destPath directly, don't append
  // /demos)
  QString destDir = destPath;  // Changed this line - removed + "/demos"
  LOG_INFO << "Destination directory: [path hidden for privacy]";

  if (!ensureDirectoryExists(destDir)) {
    LOG_ERROR << "[ERROR] Failed to create destination directory: "
              << destDir.toStdString();
    return false;
  }

  bool allCopied = true;

  // Copy each demo file
  for (const QString& sourcePath : sourcePaths) {
    QString fileName = QFileInfo(sourcePath).fileName();
    QString fullDestPath =
      destDir + "/" + fileName;  // Now correctly points to demos folder

    LOG_INFO << "Attempting to copy " << fileName.toStdString();
    LOG_INFO << "From: [path hidden for privacy]";
    LOG_INFO << "To: [path hidden for privacy]";

    // Check destination permissions
    QFileInfo destInfo(QFileInfo(fullDestPath).path());
    if (!destInfo.isWritable()) {
      LOG_ERROR << "[ERROR] Destination directory is not writable: "
                << destInfo.filePath().toStdString();
      allCopied = false;
      continue;
    }

    // If destination already exists and is valid, skip it
    if (QFileInfo::exists(fullDestPath) && validateDemoFile(fullDestPath)) {
      LOG_INFO << "Demo file already exists and is valid: " << fileName.toStdString();
      continue;
    }

    // Remove existing file if present
    if (QFile::exists(fullDestPath)) {
      if (!QFile::remove(fullDestPath)) {
        LOG_ERROR << "[ERROR] Failed to remove existing file: "
                  << fullDestPath.toStdString();
        allCopied = false;
        continue;
      }
    }

    // Perform copy
    if (!QFile::copy(sourcePath, fullDestPath)) {
      DWORD error = ::GetLastError();
      QString errorMsg;
      switch (error) {
        case ERROR_ACCESS_DENIED:
          errorMsg = "Access denied";
          break;
        case ERROR_FILE_EXISTS:
          errorMsg = "File already exists";
          break;
        case ERROR_PATH_NOT_FOUND:
          errorMsg = "Path not found";
          break;
        default:
          errorMsg = QString::number((int)error);
      }
      LOG_ERROR << "[ERROR] Failed to copy file. Error: "
                << errorMsg.toStdString();
      allCopied = false;
      continue;
    }

    // Verify copied file
    if (!validateDemoFile(fullDestPath)) {
      LOG_ERROR << "[ERROR] Copied file validation failed: "
                << fullDestPath.toStdString();
      QFile::remove(fullDestPath);
      allCopied = false;
      continue;
    }

    LOG_INFO << "Successfully copied: " << fileName.toStdString();
  }

  return allCopied;
}

QString DemoFileManager::findSourceDemoFile() const {
  // Only check the executable directory
  QString exePath = QCoreApplication::applicationDirPath();
  // Use the benchmark filename we found
  QString demoPath = exePath + "/" + m_benchmarkFileName + DEM_EXTENSION;

  if (QFileInfo::exists(demoPath) && validateDemoFile(demoPath)) {
    return QDir::toNativeSeparators(demoPath);
  }

  return QString();
}

QStringList DemoFileManager::findSourceDemoFiles() const {
  QStringList foundFiles;
  QString exePath = QCoreApplication::applicationDirPath();

  // Just check for the one file we need
  QString fullPath = exePath + "/" + m_benchmarkFileName + DEM_EXTENSION;
  LOG_INFO << "Checking exe directory for: " << fullPath.toStdString();

  if (QFileInfo::exists(fullPath)) {
    LOG_INFO << "Found file: [path hidden for privacy]";
    if (validateDemoFile(fullPath)) {
      LOG_INFO << "File validated successfully: [path hidden for privacy]";
      foundFiles << QDir::toNativeSeparators(fullPath);
    } else {
      LOG_ERROR << "File validation failed: [path hidden for privacy]";
    }
  }

  // If file not found, log warning
  if (foundFiles.isEmpty()) {
    LOG_WARN << "Demo file not found in exe directory: [path hidden for privacy]";
    LOG_WARN << "Expected file: " << (m_benchmarkFileName + DEM_EXTENSION).toStdString();
    LOG_WARN << "Make sure the demo file is placed next to the executable.";
  }

  return foundFiles;
}

QString DemoFileManager::findRustDemosFolder() const {
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

  // Add all drives using QStorageInfo
  for (const QStorageInfo& drive : QStorageInfo::mountedVolumes()) {
    if (drive.isValid() && drive.isReady()) {
      possiblePaths << drive.rootPath() + "SteamLibrary/steamapps/common/Rust";
    }
  }

  // Find first valid Rust installation by checking for RustClient.exe
  for (const QString& path : possiblePaths) {
    QFileInfo exeFile(path + "/RustClient.exe");
    if (exeFile.exists() && exeFile.isFile()) {
      QString demosPath = QDir(path).absoluteFilePath("demos");
      if (QDir(demosPath).exists()) {
        return QDir::toNativeSeparators(demosPath);
      }
      return QDir::toNativeSeparators(path);
    }
  }

  return QString();
}

bool DemoFileManager::validateDemoFile(const QString& path) const {
  QFileInfo fi(path);

  // Basic checks
  if (!fi.exists() || !fi.isFile() || !fi.isReadable()) {
    return false;
  }

  // Size checks
  qint64 size = fi.size();
  if (size < EXPECTED_MIN_SIZE || size > EXPECTED_MAX_SIZE) {
    return false;
  }

  return isValidDemoFile(path);
}

bool DemoFileManager::ensureDirectoryExists(const QString& path) const {
  QDir dir(path);
  if (dir.exists()) {
    return true;
  }
  return dir.mkpath(".");
}

bool DemoFileManager::isValidDemoFile(const QString& path) const {
  // TODO: Add more specific demo file validation if needed
  // For example, check file header/format
  return true;
}

bool DemoFileManager::checkBenchmarkPrerequisites(const QString& processName) {
  // Only check prerequisites for Rust
  if (processName.compare("RustClient.exe", Qt::CaseInsensitive) != 0) {
    return true;
  }

  // Check if we have a saved path for Rust installation
  QString rustPath = getSavedRustPath();

  // If we don't have a saved path, try to find it automatically
  if (rustPath.isEmpty()) {
    rustPath = findRustInstallationPath();

    // If we found a valid path, save it for future use
    if (!rustPath.isEmpty()) {
      LOG_INFO << "Found Rust installation automatically at: "
                << rustPath.toStdString();
      saveRustPath(rustPath);
    }
  } else {
    LOG_INFO << "Using saved Rust installation path: [path hidden for privacy]";
  }

  // If we still don't have a path, we need to ask the user
  if (rustPath.isEmpty()) {
    emit validationError("Rust installation not found. Please select the Rust "
                         "installation folder manually.");
    return false;
  }

  // Verify the path contains RustClient.exe
  if (!verifyRustPath(rustPath)) {
    LOG_ERROR
      << "RustClient.exe not found in the specified Rust installation folder: "
      << rustPath.toStdString();
    emit validationError(
      "RustClient.exe not found in the specified Rust installation folder. "
      "Please select the correct Rust installation folder.");
    return false;
  }

  // Check if demos folder exists
  QString demosPath = rustPath + "/demos";
  if (!QDir(demosPath).exists()) {
    LOG_WARN << "Demos folder not found at: [path hidden for privacy]"
             ;
    emit validationError("Demos folder not found. Please create the 'demos' "
                         "folder in your Rust installation directory.");
    return false;
  }

  // Look for the preferred benchmark file (benchmark.dem) in Rust demos
  QDir demosDir(rustPath + "/demos");
  QString preferredFile = "benchmark.dem";
  if (demosDir.exists(preferredFile)) {
    LOG_INFO << "Preferred benchmark file found in Rust demos folder: "
              << demosDir.absoluteFilePath(preferredFile).toStdString()
             ;
    m_benchmarkFileName = "benchmark";  // Set to the preferred file name
  } else {
    // If the preferred file is not found, check if any benchmark file exists in
    // app's benchmark_demos folder
    QString appBenchmarkFile = findAppBenchmarkFile();

    if (!appBenchmarkFile.isEmpty()) {
      // We found a benchmark file in the app directory, but it's not in Rust
      // demos
      QFileInfo fileInfo(appBenchmarkFile);
      m_benchmarkFileName = fileInfo.baseName();

      LOG_INFO << "Found benchmark file in application directory: "
                << appBenchmarkFile.toStdString();
      LOG_WARN << "But the file is not in Rust demos folder";

      emit validationError("Required benchmark file (" + fileInfo.fileName() +
                           ") is not in the Rust demos folder.\n\n"
                           "Please copy " +
                           fileInfo.fileName() +
                           " from the application's benchmark_demos folder "
                           "to the Rust demos folder: " +
                           demosPath);
      return false;
    }

    // Look for any benchmark files in Rust demos folder
    QStringList benchmarkFiles =
      demosDir.entryList(QStringList() << BENCHMARK_FILE_PATTERN, QDir::Files);

    if (!benchmarkFiles.isEmpty()) {
      // Found some benchmark file in Rust demos, use the first one
      QString foundFile = benchmarkFiles.first();
      m_benchmarkFileName =
        foundFile.left(foundFile.length() - 4);  // Remove .dem extension
      LOG_INFO << "Found alternative benchmark file in Rust demos folder: "
                << foundFile.toStdString();
    } else {
      // No benchmark demo files found - this is now acceptable for the new demo system
      LOG_INFO << "No benchmark demo files found, using new demo system with 'demo.play benchmark' command";
      m_benchmarkFileName = "benchmark";  // Set default name for new demo system
    }
  }

  // All prerequisites are met
  LOG_INFO << "All benchmark prerequisites are met:";
  LOG_INFO << "- Rust installation: [path hidden for privacy]";
  LOG_INFO << "- Demos folder: [path hidden for privacy]";
  LOG_INFO << "- Demo file: "
            << (demosPath + "/" + m_benchmarkFileName + ".dem").toStdString()
           ;

  return true;
}

QString DemoFileManager::findRustInstallationPath() const {
  QStringList possiblePaths;

  // Check Steam registry for install path
  QSettings steamRegistry(
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam",
    QSettings::NativeFormat);
  QString steamPath = steamRegistry.value("InstallPath").toString();

  if (!steamPath.isEmpty()) {
    possiblePaths << steamPath + "/steamapps/common/Rust";
  }

  // Add default Steam paths
  possiblePaths << "C:/Program Files (x86)/Steam/steamapps/common/Rust"
                << "C:/Program Files/Steam/steamapps/common/Rust";

  // Check all drive letters
  for (const QStorageInfo& drive : QStorageInfo::mountedVolumes()) {
    if (drive.isValid() && drive.isReady()) {
      possiblePaths << drive.rootPath() + "SteamLibrary/steamapps/common/Rust";
    }
  }

  // Find first valid Rust installation
  for (const QString& path : possiblePaths) {
    if (verifyRustPath(path)) {
      return path;
    }
  }

  return QString();
}

bool DemoFileManager::verifyRustPath(const QString& path) const {
  if (path.isEmpty()) {
    return false;
  }

  // Normalize the path to ensure we're checking the right location
  QString normalizedPath = normalizeRustPath(path);
  if (normalizedPath.isEmpty()) {
    return false;
  }

  // Check for RustClient.exe
  QFileInfo exeFile(normalizedPath + "/RustClient.exe");
  return exeFile.exists() && exeFile.isFile();
}

bool DemoFileManager::verifyDemosFolder(const QString& path) const {
  QString normalizedPath = normalizeRustPath(path);
  if (normalizedPath.isEmpty()) {
    return false;
  }

  QString demosPath = normalizedPath + "/demos";
  QDir demosDir(demosPath);

  // Check if demos folder exists
  if (!demosDir.exists()) {
    return false;
  }

  // Check specifically for the benchmark.dem file (preferred file)
  QString preferredFile = "benchmark.dem";
  QString fullPreferredPath = demosDir.absoluteFilePath(preferredFile);

  if (QFileInfo::exists(fullPreferredPath) && validateDemoFile(fullPreferredPath)) {
    return true;
  }

  // If preferred file not found, check for any benchmark file
  QStringList benchmarkFiles =
    demosDir.entryList(QStringList() << BENCHMARK_FILE_PATTERN, QDir::Files);
  return !benchmarkFiles.isEmpty();
}

bool DemoFileManager::verifyBenchmarkFolder(const QString& path) const {
  // The benchmark folder is automatically created by the game when needed
  // Always return true to indicate this check passes
  return true;
}

QString DemoFileManager::normalizeRustPath(const QString& path) const {
  if (path.isEmpty()) {
    return QString();
  }

  // Path is already correct
  if (QFileInfo(path + "/RustClient.exe").exists()) {
    return path;
  }

  // Try to find RustClient.exe in the path
  QDir dir(path);
  if (QFileInfo(dir.absolutePath() + "/RustClient.exe").exists()) {
    return dir.absolutePath();
  }

  // Check if we're in a subdirectory of Rust
  QDir parentDir = dir;
  while (parentDir.cdUp()) {
    if (QFileInfo(parentDir.absolutePath() + "/RustClient.exe").exists()) {
      return parentDir.absolutePath();
    }

    // Check "Rust" directory in current path
    if (QFileInfo(parentDir.absolutePath() + "/Rust/RustClient.exe").exists()) {
      return parentDir.absolutePath() + "/Rust";
    }
  }

  // Check if path is a parent directory of Rust
  if (QFileInfo(path + "/Rust/RustClient.exe").exists()) {
    return path + "/Rust";
  }

  return QString();
}

void DemoFileManager::saveRustPath(const QString& path) {
  ApplicationSettings::getInstance().setValue("Rust/InstallPath", path);
}

QString DemoFileManager::getSavedRustPath() const {
  return ApplicationSettings::getInstance().getValue("Rust/InstallPath", "");
}

// Find the benchmark file from the application's benchmark_demos folder
QString DemoFileManager::findAppBenchmarkFile() const {
  QString exePath = QCoreApplication::applicationDirPath();
  QString demosFolderPath = exePath + "/benchmark_demos";
  QDir demosDir(demosFolderPath);

  if (!demosDir.exists()) {
    LOG_WARN << "Benchmark demos folder not found in application directory: "
              << demosFolderPath.toStdString();
    return QString();
  }

  // Look specifically for benchmark.dem first (preferred file)
  QString preferredFile = "benchmark.dem";
  if (QFileInfo(demosFolderPath + "/" + preferredFile).exists()) {
    LOG_INFO << "Found preferred benchmark file: "
              << preferredFile.toStdString();
    return demosFolderPath + "/" + preferredFile;
  }

  // If preferred file not found, find any matching benchmark file
  QStringList files =
    demosDir.entryList(QStringList() << BENCHMARK_FILE_PATTERN, QDir::Files);
  if (!files.isEmpty()) {
    QString foundFile = files.first();
    LOG_INFO << "Found benchmark file: [path hidden for privacy]"
             ;
    return demosFolderPath + "/" + foundFile;
  }

  LOG_WARN << "No benchmark files found in application benchmark_demos folder"
           ;
  return QString();
}

// Check if the benchmark file exists in the Rust demos folder
bool DemoFileManager::isBenchmarkFileInRustDemos(
  const QString& benchmarkFilename) {
  QString rustPath = getSavedRustPath();
  if (rustPath.isEmpty()) {
    rustPath = findRustInstallationPath();
  }

  if (rustPath.isEmpty()) {
    LOG_ERROR << "Rust installation path not found";
    return false;
  }

  QString demosFolderPath = rustPath + "/demos";

  // First, prioritize checking for benchmark.dem file (preferred file)
  QString preferredFile = "benchmark.dem";
  QString preferredFilePath = demosFolderPath + "/" + preferredFile;

  if (QFileInfo::exists(preferredFilePath) &&
      validateDemoFile(preferredFilePath)) {
    LOG_INFO << "Preferred benchmark file found in Rust demos folder: "
              << preferredFilePath.toStdString();
    m_benchmarkFileName = "benchmark";  // Update the benchmark filename
    return true;
  }

  // Extract just the filename if a full path was provided
  QString filename = QFileInfo(benchmarkFilename).fileName();

  // If no extension is provided, add .dem
  if (!filename.endsWith(".dem")) {
    filename += ".dem";
  }

  // Don't check for the provided filename if it's the same as the preferred one
  if (filename != preferredFile) {
    QString fullPath = demosFolderPath + "/" + filename;

    bool exists = QFileInfo::exists(fullPath) && validateDemoFile(fullPath);

    if (exists) {
      LOG_INFO << "Benchmark file found in Rust demos folder: "
                << fullPath.toStdString();
      // Update the benchmark filename to match the found file (without
      // extension)
      m_benchmarkFileName = QFileInfo(filename).baseName();
      return true;
    } else {
      LOG_WARN << "Benchmark file NOT found in Rust demos folder: "
                << fullPath.toStdString();
    }
  } else {
    LOG_WARN << "Benchmark.dem NOT found in Rust demos folder: "
              << preferredFilePath.toStdString();
  }

  return false;
}

// Helper function to get the name of the current benchmark file we're using
QString DemoFileManager::getCurrentBenchmarkFilename() const {
  return m_benchmarkFileName + ".dem";
}

// Helper function to copy the application benchmark file to the Rust demos
// folder
bool DemoFileManager::copyAppBenchmarkToRustDemos() {
  QString appBenchmarkFile = findAppBenchmarkFile();
  if (appBenchmarkFile.isEmpty()) {
    LOG_WARN << "No application benchmark file found to copy";
    return false;
  }

  QString rustPath = getSavedRustPath();
  if (rustPath.isEmpty()) {
    rustPath = findRustInstallationPath();
  }

  if (rustPath.isEmpty()) {
    LOG_ERROR << "Rust installation path not found";
    return false;
  }

  QString demosFolderPath = rustPath + "/demos";
  QDir demosDir(demosFolderPath);

  if (!demosDir.exists()) {
    bool created = demosDir.mkpath(".");
    if (!created) {
      LOG_ERROR << "Failed to create Rust demos folder: "
                << demosFolderPath.toStdString();
      return false;
    }
    LOG_INFO << "Created Rust demos folder: [path hidden for privacy]"
             ;
  }

  QString destPath =
    demosFolderPath + "/" + QFileInfo(appBenchmarkFile).fileName();

  if (QFileInfo::exists(destPath) && validateDemoFile(destPath)) {
    LOG_INFO << "Benchmark file already exists in Rust demos folder: "
              << destPath.toStdString();
    return true;
  }

  if (QFileInfo::exists(destPath)) {
    if (!QFile::remove(destPath)) {
      LOG_ERROR << "Failed to remove existing file: [path hidden for privacy]"
               ;
      return false;
    }
  }

  bool success = QFile::copy(appBenchmarkFile, destPath);
  if (success) {
    LOG_INFO << "Successfully copied benchmark file to Rust demos folder"
             ;
    // Update m_benchmarkFileName to match the file we just copied
    m_benchmarkFileName = QFileInfo(appBenchmarkFile).baseName();
  } else {
    LOG_ERROR << "Failed to copy benchmark file to Rust demos folder"
             ;
  }

  return success;
}
