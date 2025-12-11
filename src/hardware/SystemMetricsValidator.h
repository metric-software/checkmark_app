#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>

// Include the full class definitions instead of forward declarations
#include "CPUKernelMetricsTracker.h"
#include "DiskPerformanceTracker.h"
#include "NvidiaMetrics.h"
#include "WinHardwareMonitor.h"
#include "PdhInterface.h"
#include "pdh/PdhMetricsManager.h"

namespace SystemMetrics {

// Result enum for tracking validation status
enum ValidationResult { NOT_TESTED = 0, FAILED = 1, PARTIAL = 2, SUCCESS = 3 };

// Structure to store detailed validation result
struct ValidationDetail {
  ValidationResult result = NOT_TESTED;
  std::string message;
  std::chrono::steady_clock::time_point timestamp;

  ValidationDetail(ValidationResult r = NOT_TESTED, const std::string& msg = "")
      : result(r), message(msg), timestamp(std::chrono::steady_clock::now()) {}
};

// Type definition for progress callback
using ProgressCallback =
  std::function<void(int progress, const std::string& message)>;

// Main validator class - implements singleton pattern
class SystemMetricsValidator {
 public:
  static SystemMetricsValidator& getInstance() {
    static SystemMetricsValidator instance;
    return instance;
  }

  // Run all validation tests with progress reporting
  void validateAllMetricsProviders(ProgressCallback progressCallback = nullptr);

  // Result getters
  ValidationResult getValidationResult(const std::string& componentName) const;
  ValidationDetail getValidationDetail(const std::string& componentName) const;
  std::map<std::string, ValidationDetail> getAllValidationResults() const;

  // Result logging
  void logAllResults() const;

  // Save validation results to application settings
  void saveValidationResults();

  // Check if a specific component has been validated (file exists)
  bool hasComponentBeenValidated(const std::string& componentName) const;

  // Check if all components have been validated
  bool hasBeenValidated() const;

  // Get the raw metrics directory path
  std::filesystem::path getRawMetricsDirectory() const;

  // Get the file path for a specific component
  std::filesystem::path getComponentFilePath(const std::string& componentName) const;

  // Load saved validation results from component files
  void loadSavedValidationResults();

  // Utility methods
  static constexpr int COLLECTION_TIME_MS = 2000;  // Standard collection time

 private:
  SystemMetricsValidator() = default;
  ~SystemMetricsValidator();

  // Helper methods
  void setValidationResult(const std::string& componentName,
                           ValidationResult result, const std::string& message);
  bool hasNonZeroValues(const std::vector<double>& values) const;
  bool hasNonNegativeValues(const std::vector<double>& values) const;

  // Raw data collection and storage helpers - updated to save individual files
  void collectAndSaveRawData(const std::string& providerName,
                             const std::string& rawData);
  void saveComponentRawData(const std::string& componentName, const std::string& rawData);

  // Raw data storage
  std::mutex rawDataMutex;
  std::map<std::string, std::string> rawDataCollections;

  // Component instances for testing
  std::unique_ptr<DiskPerformanceTracker> diskPerformanceTracker;
  std::unique_ptr<CPUKernelMetricsTracker> cpuKernelMetricsTracker;
  std::unique_ptr<NvidiaMetricsCollector> gpuMetricsCollector;
  std::unique_ptr<WinHardwareMonitor> hardwareMonitor;
  std::unique_ptr<PdhInterface> pdhInterface;
  std::unique_ptr<PdhMetrics::PdhMetricsManager> pdhMetricsManager;

  // Storage for validation results
  mutable std::mutex validationMutex;
  std::map<std::string, ValidationDetail> validationResults;

  // Methods for streamlined validation (combined approach)
  void validateComponentWithRawData(const std::string& component,
                                    int baseProgress, int progressWeight,
                                    ProgressCallback progressCallback);
  void collectRawDataFromComponent(const std::string& component);
  void verifyFinalCleanup();


  // List of all components that should be validated
  std::vector<std::string> getAllComponentNames() const;
};

// Helper function to collect constant system info
void CollectConstantSystemInfo();

}  // namespace SystemMetrics
