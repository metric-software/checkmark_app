#include "drive_test.h"
#include "../logging/Logger.h"

#include "../core/AppNotificationBus.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

#include <windows.h>
#include <winioctl.h>

#include <QDateTime>
#include <QUuid>

#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"

#ifndef FSCTL_LOCK_VOLUME
#define FSCTL_LOCK_VOLUME                                                      \
  CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#ifndef FSCTL_UNLOCK_VOLUME
#define FSCTL_UNLOCK_VOLUME                                                    \
  CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

// Add a function to emit progress updates during drive tests
void emitDriveTestProgress(const QString& message, int progress) {
  // Get reference to DiagnosticDataStore
  auto& dataStore = DiagnosticDataStore::getInstance();

  // Use the progress callback if available
  if (dataStore.getEmitProgressCallback()) {
    dataStore.getEmitProgressCallback()(message, progress);
  }
}

// Add string conversion utility
std::string errorToString(DWORD error) {
  std::ostringstream ss;
  ss << "Error code: " << error;
  return ss.str();
}

static std::string ensureTrailingSlash(std::string s) {
  if (s.empty()) return s;
  const char last = s.back();
  if (last == '\\' || last == '/') return s;
  s.push_back('\\');
  return s;
}

static bool ensureDirectoryExists(const std::string& dirPath, QString* outError) {
  if (dirPath.empty()) {
    if (outError) *outError = QStringLiteral("Directory path is empty");
    return false;
  }

  const BOOL ok = CreateDirectoryA(dirPath.c_str(), nullptr);
  if (ok) return true;
  const DWORD err = GetLastError();
  if (err == ERROR_ALREADY_EXISTS) return true;
  if (outError) {
    *outError = QStringLiteral("CreateDirectory failed (%1)")
                  .arg(QString::fromStdString(errorToString(err)));
  }
  return false;
}

static std::string makeDriveTestDir(const std::string& driveRoot) {
  return ensureTrailingSlash(driveRoot) + "checkmark_drive_test\\";
}

static bool fileExistsA(const std::string& path) {
  const DWORD attrs = GetFileAttributesA(path.c_str());
  return attrs != INVALID_FILE_ATTRIBUTES;
}

static std::string makeUniqueTempFilePathInDir(const std::string& dirPath,
                                               const std::string& prefix,
                                               const std::string& ext) {
  const QString guid = QUuid::createUuid().toString(QUuid::WithoutBraces);
  return ensureTrailingSlash(dirPath) + prefix + guid.toStdString() + ext;
}

static void notifyDriveTestError(const QString& message) {
  AppNotificationBus::post(message, AppNotificationBus::Type::Error, 8000);
}

static void notifyDriveTestWarning(const QString& message) {
  AppNotificationBus::post(message, AppNotificationBus::Type::Warning, 8000);
}

// Update to use DiagnosticDataStore
struct DriveMetric {
  std::string drivePath;
  double seqRead = -1.0;
  double seqWrite = -1.0;
  double iops4k = -1.0;
  double accessTimeMs = -1.0;
};

int calculateDriveScore(const DriveMetric& drive) {
  // Simple scoring algorithm - can be made more sophisticated
  double readScore =
    drive.seqRead / 1000.0 * 40;  // Up to 40 points for read speed
  double writeScore =
    drive.seqWrite / 1000.0 * 30;  // Up to 30 points for write speed
  double iopsScore = drive.iops4k / 1000.0 * 30;  // Up to 30 points for IOPS

  int totalScore = static_cast<int>(readScore + writeScore + iopsScore);
  int minVal = 0;
  int maxVal = 100;
  return (totalScore < minVal) ? minVal
                               : ((totalScore > maxVal) ? maxVal : totalScore);
}

DriveTestResults testDrivePerformance(const std::string& path) {
  DriveTestResults results = {};

  // First do a quick probe test to determine drive type/speed
  emitDriveTestProgress(
    QString("Drive Test: Probing %1").arg(QString::fromStdString(path)), 67);
  const size_t PROBE_SIZE = 64 * 1024 * 1024;  // 64MB probe
  double probeSpeed = probeDriveSpeed(path, PROBE_SIZE);
  LOG_INFO << "Initial probe speed: " << probeSpeed << " MB/s";

  // Add timeout settings
  const int MAX_TEST_DURATION_SEC =
    25;  // Maximum time for any single test phase
  // Use separate timeout flags for each test type
  bool writeTimeoutDetected = false;
  bool readTimeoutDetected = false;
  bool iopsTimeoutDetected = false;

  // Determine test parameters based on probe speed
  size_t TEST_SIZE;
  int NUM_IOPS_OPERATIONS;
  int NUM_PASSES;

  if (probeSpeed < 50.0) {          // Likely HDD or very slow storage
    TEST_SIZE = 512 * 1024 * 1024;  // 512MB for slow drives
    NUM_IOPS_OPERATIONS = 1000;     // Fewer IOPS operations
    NUM_PASSES = 1;                 // Single pass
    LOG_INFO << "Detected slow drive, using reduced test parameters";
  } else if (probeSpeed < 200.0) {       // Likely SATA SSD
    TEST_SIZE = 1 * 1024 * 1024 * 1024;  // 1GB
    NUM_IOPS_OPERATIONS = 5000;          // Moderate IOPS operations
    NUM_PASSES = 2;                      // Two passes
    LOG_INFO << "Detected medium-speed drive, using standard test parameters";
  } else {                                  // Likely NVMe or fast storage
    TEST_SIZE = 4ULL * 1024 * 1024 * 1024;  // Full 4GB for fast drives
    NUM_IOPS_OPERATIONS = 10000;            // Full IOPS test
    NUM_PASSES = 2;                         // Two passes
    LOG_INFO << "Detected high-speed drive, using full test parameters";
  }

  const size_t BLOCK_SIZE = 1024 * 1024;  // 1MB blocks for sequential tests
  const size_t SMALL_BLOCK = 4096;        // 4KB for IOPS test

  // Keep artifacts in a dedicated folder under the tested drive root.
  const std::string testDir = makeDriveTestDir(path);
  {
    QString dirError;
    if (!ensureDirectoryExists(testDir, &dirError)) {
      notifyDriveTestError(
        QStringLiteral("Drive Test failed: could not create temp folder (%1)").arg(dirError));
      results.sequentialReadMBps = -1.0;
      results.sequentialWriteMBps = -1.0;
      results.iops4k = -1.0;
      results.accessTimeMs = -1.0;
      return results;
    }
  }

  std::string testFile;
  for (int attempt = 0; attempt < 5; ++attempt) {
    testFile = makeUniqueTempFilePathInDir(testDir, "drivebench_", ".tmp");
    if (!fileExistsA(testFile)) break;
  }
  if (testFile.empty() || fileExistsA(testFile)) {
    notifyDriveTestError(
      QStringLiteral("Drive Test failed: could not allocate a unique temp file name"));
    results.sequentialReadMBps = -1.0;
    results.sequentialWriteMBps = -1.0;
    results.iops4k = -1.0;
    results.accessTimeMs = -1.0;
    return results;
  }

  // Create aligned buffer with random data
  void* alignedBuffer = _aligned_malloc(BLOCK_SIZE, 4096);
  if (!alignedBuffer) {
    notifyDriveTestError(QStringLiteral("Drive Test failed: memory allocation failed (aligned buffer)"));
    results.sequentialReadMBps = -1.0;
    results.sequentialWriteMBps = -1.0;
    results.iops4k = -1.0;
    results.accessTimeMs = -1.0;
    return results;
  }

  // Fill with random data
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dis(0, 255);
  unsigned char* buffer = static_cast<unsigned char*>(alignedBuffer);
  for (size_t i = 0; i < BLOCK_SIZE; i++) {
    buffer[i] = static_cast<unsigned char>(dis(gen));
  }

  // Add progress reporting function
  auto reportProgress = [](const char* operation, size_t current,
                           size_t total) {
    const int barWidth = 50;
    float progress = static_cast<float>(current) / total;
    int pos = static_cast<int>(barWidth * progress);

    // Progress output removed - handled by emitDriveTestProgress instead
  };

  // Sequential Write - with cache disabled
  emitDriveTestProgress(QString("Drive Test: Sequential Write Test on %1")
                          .arg(QString::fromStdString(path)),
                        69);
  {
    HANDLE hFile =
      CreateFileA(testFile.c_str(), GENERIC_WRITE,
                  0,  // No sharing
                  NULL, CREATE_NEW,
                  FILE_FLAG_NO_BUFFERING |      // Disable system cache
                    FILE_FLAG_SEQUENTIAL_SCAN,  // Hint sequential access
                  NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
      _aligned_free(alignedBuffer);
      notifyDriveTestError(
        QStringLiteral("Drive Test failed: could not create temp test file (%1)")
          .arg(QString::fromStdString(errorToString(GetLastError()))));
      results.sequentialReadMBps = -1.0;
      results.sequentialWriteMBps = -1.0;
      results.iops4k = -1.0;
      results.accessTimeMs = -1.0;
      return results;
    }

    // Disable disk write cache
    DWORD mode = 0;
    DeviceIoControl(hFile, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &mode, NULL);

    std::vector<double> speeds;
    // Multiple passes to get consistent results
    for (int pass = 0; pass < NUM_PASSES; pass++) {
      auto start = std::chrono::high_resolution_clock::now();
      size_t bytesWritten = 0;
      DWORD written;

      while (bytesWritten < TEST_SIZE) {
        if (!WriteFile(hFile, alignedBuffer, BLOCK_SIZE, &written, NULL)) break;
        bytesWritten += written;
        FlushFileBuffers(hFile);  // Force write to disk

        // Periodically update progress
        if (bytesWritten % (BLOCK_SIZE * 10) == 0) {
          float percent = static_cast<float>(bytesWritten) / TEST_SIZE * 100.0f;
          emitDriveTestProgress(QString("Drive Test: Sequential Write (%1%)")
                                  .arg(static_cast<int>(percent)),
                                69);
        }

        // Check if test is taking too long
        auto current = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(current - start).count();
        if (elapsed > MAX_TEST_DURATION_SEC) {
          writeTimeoutDetected = true;
          LOG_WARN << "Sequential Write test taking too long, stopping early after " << elapsed << " seconds. Drive may be slower than initially detected.";
          break;
        }
      }
      // Progress newline removed - handled by emitDriveTestProgress

      auto end = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration<double>(end - start).count();

      // Only calculate speed if we've written a meaningful amount
      if (bytesWritten > BLOCK_SIZE * 10) {
        speeds.push_back((bytesWritten / 1024.0 / 1024.0) / duration);
      }

      // If timeout was detected, reduce the number of passes
      if (writeTimeoutDetected && pass < NUM_PASSES - 1) {
        LOG_WARN << "Reducing the number of write test passes due to timeout.";
        break;
      }

      // Reset file position
      SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    }

    // Use measurements we actually collected
    if (!speeds.empty()) {
      std::sort(speeds.begin(), speeds.end());
      // If we have fewer passes than expected, adjust the index we're using
      if (speeds.size() > 1) {
        // Use the median of whatever measurements we have
        results.sequentialWriteMBps = speeds[speeds.size() / 2];
      } else {
        // Use the only measurement we have
        results.sequentialWriteMBps = speeds[0];
      }
    } else {
      // No valid measurements collected - keep the default -1.0 value
      LOG_WARN << "Sequential Write test: No valid measurements collected";
    }

    DeviceIoControl(hFile, FSCTL_UNLOCK_VOLUME, NULL, 0, NULL, 0, &mode, NULL);
    CloseHandle(hFile);

    LOG_INFO << "Sequential write test completed: " << results.sequentialWriteMBps << " MB/s";
  }

  // Sequential Read - with cache disabled
  emitDriveTestProgress(QString("Drive Test: Sequential Read Test on %1")
                          .arg(QString::fromStdString(path)),
                        72);
  {
    HANDLE hFile =
      CreateFileA(testFile.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING,
                  FILE_FLAG_NO_BUFFERING | FILE_FLAG_SEQUENTIAL_SCAN |
                    FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
                  NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
      notifyDriveTestError(
        QStringLiteral("Drive Test failed: could not open temp file for sequential read (%1)")
          .arg(QString::fromStdString(errorToString(GetLastError()))));
      _aligned_free(alignedBuffer);
      if (!testFile.empty() && !DeleteFileA(testFile.c_str())) {
        const DWORD err = GetLastError();
        notifyDriveTestWarning(
          QStringLiteral("Drive Test cleanup warning: failed to delete temp file (%1)")
            .arg(QString::fromStdString(errorToString(err))));
      }
      results.sequentialReadMBps = -1.0;
      results.iops4k = -1.0;
      results.accessTimeMs = -1.0;
      return results;
    }

    std::vector<double> speeds;
    for (int pass = 0; pass < NUM_PASSES; pass++) {
      auto start = std::chrono::high_resolution_clock::now();
      size_t bytesRead = 0;
      DWORD read;

      while (bytesRead < TEST_SIZE) {
        if (!ReadFile(hFile, alignedBuffer, BLOCK_SIZE, &read, NULL)) break;
        bytesRead += read;

        // Periodically update progress
        if (bytesRead % (BLOCK_SIZE * 10) == 0) {
          float percent = static_cast<float>(bytesRead) / TEST_SIZE * 100.0f;
          emitDriveTestProgress(QString("Drive Test: Sequential Read (%1%)")
                                  .arg(static_cast<int>(percent)),
                                72);
        }

        // Check if test is taking too long
        auto current = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(current - start).count();
        if (elapsed > MAX_TEST_DURATION_SEC) {
          readTimeoutDetected = true;
          LOG_WARN << "Sequential Read test taking too long, stopping early after " << elapsed << " seconds. Drive may be slower than initially detected.";
          break;
        }
      }
      // Progress newline removed - handled by emitDriveTestProgress

      auto end = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration<double>(end - start).count();

      // Only calculate speed if we've read a meaningful amount
      if (bytesRead > BLOCK_SIZE * 10) {
        speeds.push_back((bytesRead / 1024.0 / 1024.0) / duration);
      }

      // If timeout was detected, reduce the number of passes
      if (readTimeoutDetected && pass < NUM_PASSES - 1) {
        LOG_WARN << "Reducing the number of read test passes due to timeout.";
        break;
      }

      SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
    }

    // Use measurements we actually collected
    if (!speeds.empty()) {
      std::sort(speeds.begin(), speeds.end());
      // If we have fewer passes than expected, adjust the index we're using
      if (speeds.size() > 1) {
        // Use the median of whatever measurements we have
        results.sequentialReadMBps = speeds[speeds.size() / 2];
      } else {
        // Use the only measurement we have
        results.sequentialReadMBps = speeds[0];
      }
    } else {
      // No valid measurements collected - keep the default -1.0 value
      LOG_WARN << "Sequential Read test: No valid measurements collected";
    }

    CloseHandle(hFile);

    LOG_INFO << "Sequential read test completed: " << results.sequentialReadMBps << " MB/s";
  }

  // 4K Random IOPS with direct I/O
  emitDriveTestProgress(QString("Drive Test: 4K Random I/O Test on %1")
                          .arg(QString::fromStdString(path)),
                        75);
  {
    const size_t SMALL_BLOCK = 4096;  // Keep 4K blocks
    std::vector<ULONGLONG> offsets(NUM_IOPS_OPERATIONS);

    for (auto& offset : offsets) {
      offset = (static_cast<ULONGLONG>(dis(gen)) % (TEST_SIZE / SMALL_BLOCK)) *
               SMALL_BLOCK;
    }

    HANDLE hFile = CreateFileA(
      testFile.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
      FILE_FLAG_NO_BUFFERING |
        FILE_FLAG_RANDOM_ACCESS,  // Remove FILE_FLAG_WRITE_THROUGH
      NULL);

    const bool iopsFileOpened = (hFile != INVALID_HANDLE_VALUE);
    if (!iopsFileOpened) {
      notifyDriveTestError(
        QStringLiteral("Drive Test failed: could not open temp file for 4K random I/O (%1)")
          .arg(QString::fromStdString(errorToString(GetLastError()))));
      results.iops4k = -1.0;
    }

    auto start = std::chrono::high_resolution_clock::now();
    DWORD transferred;
    int operationsCompleted = 0;

    if (iopsFileOpened) {
      for (int i = 0; i < NUM_IOPS_OPERATIONS; i++) {
        LARGE_INTEGER li;
        li.QuadPart = offsets[i];
        if (!SetFilePointerEx(hFile, li, NULL, FILE_BEGIN)) {
          notifyDriveTestError(
            QStringLiteral(
              "Drive Test failed: could not seek temp file for 4K random I/O (%1)")
              .arg(QString::fromStdString(errorToString(GetLastError()))));
          results.iops4k = -1.0;
          break;
        }
        if (!WriteFile(hFile, alignedBuffer, SMALL_BLOCK, &transferred, NULL) ||
            transferred != SMALL_BLOCK) {
          notifyDriveTestError(
            QStringLiteral(
              "Drive Test failed: could not write temp file for 4K random I/O (%1)")
              .arg(QString::fromStdString(errorToString(GetLastError()))));
          results.iops4k = -1.0;
          break;
        }
      // Remove FlushFileBuffers(hFile); - this was causing synchronous writes
      // and killing IOPS performance

        operationsCompleted++;

        // Periodically update progress
        if (i % (NUM_IOPS_OPERATIONS / 10) == 0) {
          float percent = static_cast<float>(i) / NUM_IOPS_OPERATIONS * 100.0f;
          emitDriveTestProgress(QString("Drive Test: 4K Random I/O (%1%)")
                                  .arg(static_cast<int>(percent)),
                                75);
        }

        // Check if test is taking too long
        auto current = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration<double>(current - start).count();
        if (elapsed > MAX_TEST_DURATION_SEC) {
          iopsTimeoutDetected = true;
          LOG_WARN
            << "4K random write IOPS test taking too long, stopping early after "
            << elapsed
            << " seconds. Drive may be slower than initially detected.";
          break;
        }
      }
    }

    // Flush once at the end to ensure data is written
    if (iopsFileOpened && results.iops4k >= 0.0) {
      FlushFileBuffers(hFile);
    }
    // Progress newline removed - handled by emitDriveTestProgress

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<double>(end - start).count();

    // Calculate IOPS based on actual operations completed
    if (results.iops4k >= 0.0) {
      results.iops4k = operationsCompleted / duration;
    }

    if (iopsFileOpened) {
      CloseHandle(hFile);
    }

    LOG_INFO << "4K random write IOPS test completed: " << results.iops4k << " IOPS";
  }

  // Access time measurement
  emitDriveTestProgress(QString("Drive Test: Measuring Access Time on %1")
                          .arg(QString::fromStdString(path)),
                        77);
  measureAccessTime(path, results);

  // Summary after all tests are complete
  emitDriveTestProgress(QString("Drive Test: Finalizing Results for %1")
                          .arg(QString::fromStdString(path)),
                        79);
  LOG_INFO << "Drive test summary for [drive path hidden for privacy]:";
  LOG_INFO << "  - Sequential Write: " << results.sequentialWriteMBps << " MB/s";
  LOG_INFO << "  - Sequential Read:  " << results.sequentialReadMBps << " MB/s";
  LOG_INFO << "  - 4K Random IOPS:   " << results.iops4k;
  LOG_INFO << "  - Access Time:      " << results.accessTimeMs << " ms";

  _aligned_free(alignedBuffer);
  if (!testFile.empty() && !DeleteFileA(testFile.c_str())) {
    const DWORD err = GetLastError();
    notifyDriveTestWarning(
      QStringLiteral("Drive Test cleanup warning: failed to delete temp file (%1)")
        .arg(QString::fromStdString(errorToString(err))));
  }
  return results;
}

// Helper function to quickly probe drive speed
double probeDriveSpeed(const std::string& path, size_t probeSize) {
  const std::string testDir = makeDriveTestDir(path);
  {
    QString dirError;
    if (!ensureDirectoryExists(testDir, &dirError)) {
      notifyDriveTestError(
        QStringLiteral("Drive probe failed: could not create temp folder (%1)").arg(dirError));
      return 100.0;  // Default to medium speed on failure
    }
  }

  std::string testFile;
  for (int attempt = 0; attempt < 5; ++attempt) {
    testFile = makeUniqueTempFilePathInDir(testDir, "drivebench_probe_", ".tmp");
    if (!fileExistsA(testFile)) break;
  }
  if (testFile.empty() || fileExistsA(testFile)) {
    notifyDriveTestError(
      QStringLiteral("Drive probe failed: could not allocate a unique temp file name"));
    return 100.0;
  }
  const size_t BLOCK_SIZE = 1024 * 1024;  // 1MB blocks

  // Create aligned buffer with random data
  void* alignedBuffer = _aligned_malloc(BLOCK_SIZE, 4096);
  if (!alignedBuffer) {
    return 100.0;  // Default to medium speed on failure
  }

  // Fill with random data
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dis(0, 255);
  unsigned char* buffer = static_cast<unsigned char*>(alignedBuffer);
  for (size_t i = 0; i < BLOCK_SIZE; i++) {
    buffer[i] = static_cast<unsigned char>(dis(gen));
  }

  double speed = 0.0;

  // Quick sequential write test
  HANDLE hFile =
    CreateFileA(testFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_NEW,
                FILE_FLAG_NO_BUFFERING,  // Remove FILE_FLAG_WRITE_THROUGH for
                                         // better probe accuracy
                NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    notifyDriveTestError(
      QStringLiteral("Drive probe failed: could not create temp file (%1)")
        .arg(QString::fromStdString(errorToString(GetLastError()))));
    _aligned_free(alignedBuffer);
    if (!testFile.empty() && !DeleteFileA(testFile.c_str())) {
      const DWORD err = GetLastError();
      notifyDriveTestWarning(
        QStringLiteral("Drive probe cleanup warning: failed to delete temp file (%1)")
          .arg(QString::fromStdString(errorToString(err))));
    }
    return 100.0;
  }

  auto start = std::chrono::high_resolution_clock::now();
  size_t bytesWritten = 0;
  DWORD written;

  while (bytesWritten < probeSize) {
    if (!WriteFile(hFile, alignedBuffer, BLOCK_SIZE, &written, NULL)) break;
    bytesWritten += written;
  }

  // Flush at the end to ensure data is written
  FlushFileBuffers(hFile);

  auto end = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration<double>(end - start).count();
  speed = (bytesWritten / 1024.0 / 1024.0) / duration;

  CloseHandle(hFile);

  _aligned_free(alignedBuffer);
  if (!testFile.empty() && !DeleteFileA(testFile.c_str())) {
    const DWORD err = GetLastError();
    notifyDriveTestWarning(
      QStringLiteral("Drive probe cleanup warning: failed to delete temp file (%1)")
        .arg(QString::fromStdString(errorToString(err))));
  }

  return speed;
}

// Add progress updates to the existing measureAccessTime function
void measureAccessTime(const std::string& path, DriveTestResults& results) {
  const int NUM_SAMPLES = 2000;
  const int ITERATIONS_PER_SAMPLE =
    5;  // Multiple reads per position for more accurate timing
  const size_t BLOCK_SIZE = 4096;
  const int WARM_UP_SAMPLES = 200;                 // More extensive warm-up
  const size_t MIN_FILE_SIZE = 128 * 1024 * 1024;  // Ensure at least 128MB file

  emitDriveTestProgress(QString("Drive Test: Starting Access Time Test on %1")
                          .arg(QString::fromStdString(path)),
                        77);

  const std::string testDir = makeDriveTestDir(path);
  {
    QString dirError;
    if (!ensureDirectoryExists(testDir, &dirError)) {
      notifyDriveTestError(
        QStringLiteral("Access Time Test failed: could not create temp folder (%1)").arg(dirError));
      results.accessTimeMs = 0.0;
      return;
    }
  }

  // Stable name inside our dedicated folder; never touch the drive root.
  std::string testFile = ensureTrailingSlash(testDir) + "access_time_drivebench.tmp";
  bool needToCreateFile = false;

  // First check if existing file is available and large enough
  HANDLE hFile =
    CreateFileA(testFile.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING,
                FILE_FLAG_NO_BUFFERING | FILE_FLAG_RANDOM_ACCESS, NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    LOG_INFO << "Access time measurement: Test file not found, creating new one";
    needToCreateFile = true;
  } else {
    size_t fileSize = GetFileSize(hFile, NULL);
    CloseHandle(hFile);

    if (fileSize < MIN_FILE_SIZE) {
      LOG_INFO << "Access time measurement: Existing file too small (" << (fileSize / 1024) << " KB), creating larger file";
      needToCreateFile = true;
    }
  }

  // Create a dedicated file for access time measurement if needed
  if (needToCreateFile) {
    emitDriveTestProgress(
      QString("Drive Test: Creating Test File for Access Time Test"), 77);
    LOG_INFO << "Creating dedicated file for access time measurement...";

    // If an old temp file exists, rotate it instead of overwriting.
    if (fileExistsA(testFile)) {
      const QString ts = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_HHmmss_zzz");
      const std::string rotated = ensureTrailingSlash(testDir) +
        std::string("access_time_drivebench.old_") + ts.toStdString() + ".tmp";
      if (!MoveFileExA(testFile.c_str(), rotated.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        const DWORD err = GetLastError();
        notifyDriveTestWarning(
          QStringLiteral("Access Time Test warning: failed to rotate old temp file (%1)")
            .arg(QString::fromStdString(errorToString(err))));
      }
    }

    // Create file with sufficient size
    HANDLE hCreateFile = CreateFileA(
      testFile.c_str(), GENERIC_WRITE | GENERIC_READ, 0, NULL, CREATE_NEW,
      FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);

    if (hCreateFile == INVALID_HANDLE_VALUE) {
      results.accessTimeMs = 0.0;
      notifyDriveTestError(
        QStringLiteral("Access Time Test failed: could not create temp file (%1)")
          .arg(QString::fromStdString(errorToString(GetLastError()))));
      return;
    }

    // Allocate buffer for writing
    void* writeBuf = _aligned_malloc(BLOCK_SIZE, 4096);
    if (!writeBuf) {
      CloseHandle(hCreateFile);
      results.accessTimeMs = 0.0;
      LOG_ERROR << "Access time measurement: Failed to allocate write buffer";
      return;
    }

    // Fill with random data
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 255);
    unsigned char* buffer = static_cast<unsigned char*>(writeBuf);
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
      buffer[i] = static_cast<unsigned char>(dis(gen));
    }

    // Write sufficient blocks to create large enough file
    size_t blocksToWrite = MIN_FILE_SIZE / BLOCK_SIZE;
    for (size_t i = 0; i < blocksToWrite; i++) {
      DWORD written;
      if (!WriteFile(hCreateFile, writeBuf, BLOCK_SIZE, &written, NULL)) {
        _aligned_free(writeBuf);
        CloseHandle(hCreateFile);
        results.accessTimeMs = 0.0;
        notifyDriveTestError(
          QStringLiteral("Access Time Test failed: write error (%1)")
            .arg(QString::fromStdString(errorToString(GetLastError()))));
        LOG_ERROR << "Access time measurement: Failed to write to test file: " << errorToString(GetLastError());
        return;
      }

      if (i % 1000 == 0) {
        // Progress output handled by emitDriveTestProgress instead

        // Update progress every 10%
        if (i % (blocksToWrite / 10) == 0) {
          int percent = static_cast<int>(i * 100 / blocksToWrite);
          emitDriveTestProgress(
            QString("Drive Test: Preparing Access Time Test File (%1%)")
              .arg(percent),
            77);
        }
      }
    }
    // Progress completion handled by emitDriveTestProgress

    FlushFileBuffers(hCreateFile);
    CloseHandle(hCreateFile);
    _aligned_free(writeBuf);
  }

  // Now open the file for testing
  hFile = CreateFileA(testFile.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING,
                      FILE_FLAG_NO_BUFFERING | FILE_FLAG_RANDOM_ACCESS, NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    results.accessTimeMs = 0.0;
    notifyDriveTestError(
      QStringLiteral("Access Time Test failed: could not open temp file for reading (%1)")
        .arg(QString::fromStdString(errorToString(GetLastError()))));
    LOG_ERROR << "Access time measurement: Failed to open test file: " << errorToString(GetLastError());
    return;
  }

  std::vector<double> accessTimes;

  // Generate random positions
  std::random_device rd;
  std::mt19937 gen(rd());
  size_t fileSize = GetFileSize(hFile, NULL);

  LOG_INFO << "Access time measurement: Using file of size " << (fileSize / (1024 * 1024)) << " MB";

  std::uniform_int_distribution<ULONGLONG> dis(0, (fileSize / BLOCK_SIZE) - 10);
  std::vector<ULONGLONG> randomPositions(NUM_SAMPLES);

  for (auto& pos : randomPositions) {
    pos = dis(gen) * BLOCK_SIZE;
  }

  // Allocate buffer for reading
  void* readBuf = _aligned_malloc(BLOCK_SIZE, 4096);
  if (!readBuf) {
    CloseHandle(hFile);
    results.accessTimeMs = 0.0;
    LOG_ERROR << "Access time measurement: Failed to allocate read buffer";
    return;
  }

  // Comprehensive warm-up phase
  emitDriveTestProgress(QString("Drive Test: Warming Up Disk Cache"), 77);
  LOG_INFO << "Warming up disk cache for access time measurement...";
  for (int i = 0; i < WARM_UP_SAMPLES; i++) {
    LARGE_INTEGER li;
    // Use a different pattern for warm-up to avoid caching the test data
    li.QuadPart = (dis(gen) * BLOCK_SIZE);
    SetFilePointerEx(hFile, li, NULL, FILE_BEGIN);
    DWORD bytesRead;
    ReadFile(hFile, readBuf, BLOCK_SIZE, &bytesRead, NULL);

    // Force clearing the buffer between reads to prevent CPU caching
    memset(readBuf, 0, BLOCK_SIZE);

    // Update progress periodically
    if (i % (WARM_UP_SAMPLES / 5) == 0) {
      int percent = static_cast<int>(i * 100 / WARM_UP_SAMPLES);
      emitDriveTestProgress(
        QString("Drive Test: Warming Up Disk Cache (%1%)").arg(percent), 77);
    }
  }
  LOG_INFO << "Warming up disk cache completed.";

  emitDriveTestProgress(QString("Drive Test: Measuring Access Time"), 78);
  LOG_INFO << "Measuring disk access time...";

  // Force a flush of system caches
  FlushFileBuffers(hFile);

  // Use high-resolution timer for measurement
  LARGE_INTEGER frequency;
  QueryPerformanceFrequency(&frequency);

  // Measure access times using Windows high-performance counter
  for (int i = 0; i < NUM_SAMPLES; i++) {
    double totalTime = 0.0;

    for (int iter = 0; iter < ITERATIONS_PER_SAMPLE; iter++) {
      // Clear CPU caches between reads
      memset(readBuf, 0, BLOCK_SIZE);

      // Use a different position each time, but not from our sample set
      LARGE_INTEGER li;
      li.QuadPart = randomPositions[i];

      // Ensure we're at the right position
      SetFilePointerEx(hFile, li, NULL, FILE_BEGIN);

      // Measure the read operation
      LARGE_INTEGER start, end;
      QueryPerformanceCounter(&start);

      DWORD bytesRead = 0;
      ReadFile(hFile, readBuf, BLOCK_SIZE, &bytesRead, NULL);

      QueryPerformanceCounter(&end);

      // Calculate time in milliseconds
      double elapsed =
        (double)(end.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
      totalTime += elapsed;
    }

    // Use average time for this position
    accessTimes.push_back(totalTime / ITERATIONS_PER_SAMPLE);

    // Update progress periodically (every 10% of samples)
    if (i % (NUM_SAMPLES / 10) == 0) {
      int percent = static_cast<int>(i * 100 / NUM_SAMPLES);
      emitDriveTestProgress(
        QString("Drive Test: Measuring Access Time (%1%)").arg(percent), 78);
    }
  }

  LOG_INFO << "Access time measurement completed.";

  // Analysis phase
  emitDriveTestProgress(QString("Drive Test: Analyzing Access Time Results"),
                        79);

  // Remove outliers
  size_t originalSize = accessTimes.size();
  if (accessTimes.size() > 10) {
    std::sort(accessTimes.begin(), accessTimes.end());
    // Trim outliers from both ends: 5% from top and 5% from bottom
    size_t trimCount = accessTimes.size() * 0.05;
    accessTimes.erase(accessTimes.begin(), accessTimes.begin() + trimCount);
    accessTimes.resize(accessTimes.size() - trimCount);
    LOG_INFO << "Access time statistics: Removed " << (originalSize - accessTimes.size()) << " outliers from " << originalSize << " samples";
  }

  // Calculate median access time
  std::sort(accessTimes.begin(), accessTimes.end());

  // Ensure we have at least one valid measurement
  if (accessTimes.empty()) {
    results.accessTimeMs = 0.001;  // Fallback minimum value
    LOG_WARN << "Access time result: No valid measurements, using fallback value of 0.001 ms";
  } else {
    double median = accessTimes[accessTimes.size() / 2];

    // If median is extremely small but not zero, it's likely an SSD
    // Make sure we don't return absolute zero
    results.accessTimeMs = (median < 0.001 && median > 0.0) ? 0.001 : median;

    // Calculate min, max and average for reporting
    double min = accessTimes.front();
    double max = accessTimes.back();
    double sum = 0;
    for (const auto& time : accessTimes) {
      sum += time;
    }
    double avg = sum / accessTimes.size();

    LOG_INFO << "Access time results:";
    LOG_INFO << "  - Min:    " << min << " ms";
    LOG_INFO << "  - Max:    " << max << " ms";
    LOG_INFO << "  - Avg:    " << avg << " ms";
    LOG_INFO << "  - Median: " << median << " ms (used as final result)";

    // Provide context for the result
    if (results.accessTimeMs < 0.2)
      LOG_INFO << "Drive type indication: Very fast access time (likely NVMe SSD)";
    else if (results.accessTimeMs < 1.0)
      LOG_INFO << "Drive type indication: Fast access time (likely SATA SSD)";
    else if (results.accessTimeMs < 10.0)
      LOG_INFO << "Drive type indication: Moderate access time (likely HDD or hybrid)";
    else
      LOG_INFO << "Drive type indication: Slow access time (likely HDD with high latency)";
  }

  _aligned_free(readBuf);
  CloseHandle(hFile);

  // Cleanup: this is a temporary test artifact.
  if (!testFile.empty() && !DeleteFileA(testFile.c_str())) {
    const DWORD err = GetLastError();
    notifyDriveTestWarning(
      QStringLiteral("Access Time Test cleanup warning: failed to delete temp file (%1)")
        .arg(QString::fromStdString(errorToString(err))));
  }
}

void printDriveHealth(const char* drive) {
  // Get SMART data using WMI or DeviceIOControl
  HANDLE hDevice = CreateFileA(drive, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, 0, NULL);

  if (hDevice != INVALID_HANDLE_VALUE) {
    // Query SMART attributes here
    // Temperature, Power-on Hours, Bad Sectors etc.
    CloseHandle(hDevice);
  }
}

// Replace runDriveTests to use DiagnosticDataStore
void runDriveTests() {
  LOG_INFO << "[Drive Test] Running...";

  auto& dataStore = DiagnosticDataStore::getInstance();
  std::vector<DriveMetric> metrics;

  HANDLE currentThread = GetCurrentThread();
  int originalPriority = GetThreadPriority(currentThread);

  // Check if elevated priority is enabled in settings
  bool elevatedPriorityEnabled =
    ApplicationSettings::getInstance().getElevatedPriorityEnabled();
  if (elevatedPriorityEnabled) {
    SetThreadPriority(currentThread, THREAD_PRIORITY_ABOVE_NORMAL);
    LOG_INFO << "[Drive Test] Running with elevated thread priority (enabled in settings)";
  }

  char driveStrings[256];
  if (GetLogicalDriveStringsA(sizeof(driveStrings), driveStrings) == 0) {
    LOG_ERROR << "Failed to retrieve drives.";
    return;
  }

  const char* drive = driveStrings;
  while (*drive) {
    LOG_INFO << "Testing Drive: [drive path hidden for privacy]";

    try {
      DriveMetric driveMetric;
      driveMetric.drivePath = drive;

      auto results = testDrivePerformance(drive);
      driveMetric.seqWrite = results.sequentialWriteMBps;
      driveMetric.seqRead = results.sequentialReadMBps;
      driveMetric.iops4k = results.iops4k;
      driveMetric.accessTimeMs = results.accessTimeMs;

      metrics.push_back(driveMetric);

      // Update DriveMetrics in DiagnosticDataStore one by one
      dataStore.updateDriveMetrics(driveMetric.drivePath, driveMetric.seqRead,
                                   driveMetric.seqWrite, driveMetric.iops4k,
                                   driveMetric.accessTimeMs);
    } catch (const std::exception& e) {
      LOG_ERROR << "Drive test failed for [drive path hidden for privacy]: " << e.what();
    }

    drive += strlen(drive) + 1;
  }

  // Restore original priority at the end
  if (elevatedPriorityEnabled) {
    SetThreadPriority(currentThread, originalPriority);
  }
}
