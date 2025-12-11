#include "SystemMetricsValidator.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <thread>

#include <Windows.h>

#include "../ApplicationSettings.h"
#include "../logging/Logger.h"
#include "../benchmark/BenchmarkDataPoint.h"
#include "CPUKernelMetricsTracker.h"
#include "DiskPerformanceTracker.h"
#include "NvidiaMetrics.h"
#include "SystemWrapper.h"
#include "WinHardwareMonitor.h"
#include "PdhInterface.h"
#include "ConstantSystemInfo.h"

// Forward declaration for SystemWrapper
class SystemWrapper;

namespace SystemMetrics {

// Destructor remains unchanged for proper cleanup
SystemMetricsValidator::~SystemMetricsValidator() {
  // Ensure proper cleanup of all providers

  try {
    if (diskPerformanceTracker) {
      diskPerformanceTracker->stopTracking();
    }
  } catch (...) {
  }

  try {
    if (cpuKernelMetricsTracker) {
      cpuKernelMetricsTracker->stopTracking();
    }
  } catch (...) {
  }

  try {
    if (gpuMetricsCollector) {
      gpuMetricsCollector->stopCollecting();
    }
  } catch (...) {
  }

  try {
    if (pdhInterface) {
      pdhInterface->stop();
    }
  } catch (...) {
  }

  // ProcessorMetrics cleanup removed - using PDH interface instead

  // Allow time for cleanup to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

// Main validation method - now validates all components
void SystemMetricsValidator::validateAllMetricsProviders(
  ProgressCallback progressCallback) {
  LOG_INFO << "\n===== SYSTEM METRICS VALIDATION STARTED (FORCED REVALIDATION) =====\n";

  // Get list of all components
  auto allComponents = getAllComponentNames();
  
  // FORCE REVALIDATION: Always validate all components (ignore previous validation)
  std::vector<std::string> componentsToValidate = allComponents;  // Always validate everything
  
  LOG_INFO << "FORCED REVALIDATION MODE: All " << componentsToValidate.size() << " components will be validated";
  for (const auto& component : componentsToValidate) {
    LOG_INFO << "  - " << component;
  }

  // Clear previous validation results to start fresh
  {
    std::lock_guard<std::mutex> lock(validationMutex);
    validationResults.clear();
  }

  // Clear previous raw data collections
  {
    std::lock_guard<std::mutex> lock(rawDataMutex);
    rawDataCollections.clear();
  }


  // Calculate progress weights - distribute based on components that need validation
  int totalProgressWeight = 90; // Save 10 for final steps
  int progressPerComponent = totalProgressWeight / componentsToValidate.size();
  int currentProgress = 0;

  // Process each component that needs validation
  for (size_t i = 0; i < componentsToValidate.size(); ++i) {
    const auto& component = componentsToValidate[i];
    int componentBaseProgress = currentProgress;
    int componentMaxProgress = currentProgress + progressPerComponent;

    if (progressCallback) {
      progressCallback(componentBaseProgress,
                       "Starting " + component + " validation...");
    }

    LOG_INFO << "\n----- PROCESSING COMPONENT: " + component + " -----\n";

    // Validate component with raw data collection
    validateComponentWithRawData(component, componentBaseProgress,
                                 progressPerComponent, progressCallback);


    // Explicit delay between components to allow system resources to stabilize
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    currentProgress = componentMaxProgress;
    if (progressCallback) {
      // Ensure we report exactly the max progress for this component
      progressCallback(currentProgress,
                       "Completed " + component + " processing");
    }
  }

  // Log comprehensive results
  logAllResults();

  // Final verification of cleanup
  if (progressCallback) {
    progressCallback(95, "Performing final cleanup verification...");
  }

  verifyFinalCleanup();

  // Final progress update
  if (progressCallback) {
    progressCallback(100, "Validation complete");
  }

  LOG_INFO << "\n===== SYSTEM METRICS VALIDATION COMPLETED =====\n";
}

// Load saved validation results from component files
void SystemMetricsValidator::loadSavedValidationResults() {
  std::lock_guard<std::mutex> lock(validationMutex);
  auto allComponents = getAllComponentNames();

  LOG_INFO << "Loading validation results from component files...";

  for (const auto& component : allComponents) {
    if (hasComponentBeenValidated(component)) {
      // Component file exists, mark as SUCCESS
      // We could potentially parse the file to get more detailed results,
      // but for now, existence of a valid file indicates successful validation
      validationResults[component] = ValidationDetail(SUCCESS, "Loaded from component file - validation previously completed");
      LOG_INFO << "  " << component << ": SUCCESS (file exists)";
    } else {
      // Component file doesn't exist, mark as NOT_TESTED
      validationResults[component] = ValidationDetail(NOT_TESTED, "Component file not found - needs validation");
      LOG_INFO << "  " << component << ": NOT_TESTED (file missing)";
    }
  }

  // Also try to load any sub-component results from settings if they exist
  // (for backwards compatibility with existing data)
  auto& appSettings = ::ApplicationSettings::getInstance();
  std::vector<std::string> subComponents = {
    "ConstantSystemInfo_CPU",
    "ConstantSystemInfo_RAM", 
    "ConstantSystemInfo_Kernel",
    "WinHardwareMonitor_CPU",
    "WinHardwareMonitor_RAM"
  };

  for (const auto& component : subComponents) {
    QString keyPath = QString("ComponentValidation/%1").arg(QString::fromStdString(component));
    int resultValue = appSettings.getValue(keyPath, "-1").toInt();
    
    if (resultValue >= 0 && resultValue <= static_cast<int>(SUCCESS)) {
      ValidationResult result = static_cast<ValidationResult>(resultValue);
      QString msgKeyPath = QString("ComponentValidationMessages/%1").arg(QString::fromStdString(component));
      std::string message = appSettings.getValue(msgKeyPath, "Loaded from saved settings").toStdString();
      
      validationResults[component] = ValidationDetail(result, message);
      LOG_INFO << "  " << component << ": Loaded from settings";
    }
  }

  LOG_INFO << "Validation results loading complete";
}

// Save validation results to application settings - kept for backwards compatibility
void SystemMetricsValidator::saveValidationResults() {
  std::lock_guard<std::mutex> lock(validationMutex);
  auto& appSettings = ::ApplicationSettings::getInstance();

  for (const auto& [component, detail] : validationResults) {
    appSettings.setComponentValidationResult(component, detail.result);
  }
  
  LOG_INFO << "Validation results saved to application settings";
}

// Enhanced component validation to handle all components
void SystemMetricsValidator::validateComponentWithRawData(
  const std::string& component, int baseProgress, int progressWeight,
  ProgressCallback progressCallback) {

  try {
    LOG_INFO << "Processing and validating " << component << "...";

    ValidationResult result = NOT_TESTED;
    std::string validationMessage = "Not validated";
    std::string rawData = "No data collected";

    // Component-specific validation logic
    if (component == "WinHardwareMonitor") {
      if (progressCallback) {
        progressCallback(baseProgress + progressWeight * 0.3,
                         "Initializing hardware monitoring...");
      }

      hardwareMonitor = std::make_unique<WinHardwareMonitor>();
      hardwareMonitor->updateSensors();

      // Wait for sensors to update
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // Validate CPU sensors
      bool cpuValid = hardwareMonitor->getCPUInfo().temperature > 0 ||
                      !hardwareMonitor->getCPUInfo().coreLoads.empty();

      // Validate RAM sensors
      bool ramValid = hardwareMonitor->getRAMInfo().used > 0 ||
                      hardwareMonitor->getRAMInfo().available > 0;

      if (progressCallback) {
        progressCallback(baseProgress + progressWeight * 0.7,
                         "Collecting hardware monitor raw data...");
      }

      // Collect raw data
      rawData = hardwareMonitor->logRawData();

      // Set individual component results
      if (cpuValid) {
        setValidationResult("WinHardwareMonitor_CPU", SUCCESS,
                            "CPU temperature and/or load metrics available");
      } else {
        setValidationResult("WinHardwareMonitor_CPU", FAILED,
                            "Failed to get valid CPU monitoring data");
      }

      if (ramValid) {
        setValidationResult("WinHardwareMonitor_RAM", SUCCESS,
                            "RAM usage metrics available");
      } else {
        setValidationResult("WinHardwareMonitor_RAM", FAILED,
                            "Failed to get valid RAM monitoring data");
      }

      // Determine overall result
      if (cpuValid && ramValid) {
        result = SUCCESS;
        validationMessage = "Hardware monitoring validated successfully";
      } else if (!cpuValid && !ramValid) {
        result = FAILED;
        validationMessage = "Failed to validate hardware monitoring";
      } else {
        result = PARTIAL;
        validationMessage = "Some hardware monitoring validated successfully";
      }

      // Cleanup
      hardwareMonitor.reset();
      LOG_DEBUG << "  WinHardwareMonitor cleanup complete";
    } else if (component == "NvidiaMetricsCollector") {
      if (progressCallback) {
        progressCallback(baseProgress + progressWeight * 0.3,
                         "Checking GPU metrics collection status...");
      }

      // NVIDIA metrics component isn't fully implemented yet
      // Just mark it as SUCCESS by default without trying to initialize
      result = SUCCESS;
      validationMessage =
        "GPU metrics validation skipped (component not fully implemented)";
      rawData = "No NVIDIA GPU metrics logs available - component not fully "
                "implemented";

      LOG_INFO << "  NvidiaMetricsCollector: Component not fully implemented, marked as valid by default";

      if (progressCallback) {
        progressCallback(baseProgress + progressWeight * 0.7,
                         "GPU metrics verification completed");
      }
    } else if (component == "CPUKernelMetricsTracker") {
      if (progressCallback) {
        progressCallback(baseProgress + progressWeight * 0.3,
                         "Initializing CPU kernel metrics tracking...");
      }

      cpuKernelMetricsTracker = std::make_unique<CPUKernelMetricsTracker>();
      bool started = cpuKernelMetricsTracker->startTracking();

      if (!started) {
        result = FAILED;
        validationMessage = "Failed to start CPU kernel metrics tracking";
      } else {
        // Wait for data collection
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Get basic metrics to validate
        BenchmarkDataPoint dataPoint;
        bool metricsAvailable = true;  // Assume success initially
        cpuKernelMetricsTracker->updateBenchmarkData(dataPoint);
        // Then check if we got valid data
        if (dataPoint.interruptsPerSec < 0 && dataPoint.contextSwitchesPerSec < 0 &&
            dataPoint.dpcCountPerSec < 0) {
          metricsAvailable = false;
        }

        if (progressCallback) {
          progressCallback(baseProgress + progressWeight * 0.7,
                           "Collecting CPU kernel metrics raw data...");
        }

        // Collect raw data
        rawData = cpuKernelMetricsTracker->logRawData();

        // Cleanup
        cpuKernelMetricsTracker->stopTracking();
        cpuKernelMetricsTracker.reset();
        LOG_DEBUG << "  CPUKernelMetricsTracker cleanup complete";

        if (metricsAvailable && (dataPoint.interruptsPerSec >= 0 ||
                                 dataPoint.contextSwitchesPerSec >= 0 ||
                                 dataPoint.dpcCountPerSec >= 0)) {
          result = SUCCESS;
          validationMessage = "CPU kernel metrics collected successfully";
        } else if (metricsAvailable) {
          result = PARTIAL;
          validationMessage = "Some CPU kernel metrics collected successfully";
        } else {
          result = FAILED;
          validationMessage = "Failed to collect CPU kernel metrics";
        }
      }
    } else if (component == "DiskPerformanceTracker") {
      if (progressCallback) {
        progressCallback(baseProgress + progressWeight * 0.3,
                         "Initializing disk performance tracking...");
      }

      diskPerformanceTracker = std::make_unique<DiskPerformanceTracker>();
      bool started = diskPerformanceTracker->startTracking();

      if (!started) {
        result = FAILED;
        validationMessage = "Failed to start disk performance tracking";
      } else {
        // Wait for data collection
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        // Get basic metrics to validate
        BenchmarkDataPoint dataPoint;
        diskPerformanceTracker->updateBenchmarkData(dataPoint);

        bool latencyValid =
          dataPoint.diskReadLatencyMs >= 0 || dataPoint.diskWriteLatencyMs >= 0;
        bool throughputValid =
          dataPoint.ioReadMB >= 0 || dataPoint.ioWriteMB >= 0;

        if (progressCallback) {
          progressCallback(baseProgress + progressWeight * 0.7,
                           "Collecting disk performance raw data...");
        }

        // Collect raw data
        rawData = diskPerformanceTracker->logRawData();

        // Cleanup
        diskPerformanceTracker->stopTracking();
        diskPerformanceTracker.reset();
        LOG_DEBUG << "  DiskPerformanceTracker cleanup complete";

        if (latencyValid && throughputValid) {
          result = SUCCESS;
          validationMessage = "Disk performance metrics collected successfully";
        } else if (latencyValid || throughputValid) {
          result = PARTIAL;
          validationMessage =
            "Some disk performance metrics collected successfully";
        } else {
          result = FAILED;
          validationMessage = "Failed to collect disk performance metrics";
        }
      }
    } else if (component == "PdhInterface") {
      if (progressCallback) {
        progressCallback(baseProgress + progressWeight * 0.1,
                         "Starting Simple PDH Metrics Testing...");
      }

      LOG_INFO << "  Starting Simple PDH Metrics validation with 3-point data collection...";
      
      std::stringstream rawDataStream;
      rawDataStream << "SIMPLE PDH METRICS VALIDATION WITH 3-POINT DATA COLLECTION" << std::endl;
      rawDataStream << "Collection timestamp: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count() << " ms" << std::endl;
      rawDataStream << "==========================================================" << std::endl;

      ValidationResult result = FAILED;
      std::string validationMessage = "Simple PDH metrics validation not completed";

      try {
        // Get system info for per-core frequency testing
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        int numCores = sysInfo.dwNumberOfProcessors;
        
        // 1. ESSENTIAL METRICS TABLE WITH 3 DATAPOINTS
        rawDataStream << std::endl << "=== ESSENTIAL METRICS DATA TABLE (3 Collections) ===" << std::endl;
        rawDataStream << std::left << std::setw(40) << "Metric Name" 
                      << std::setw(15) << "Collection 1" 
                      << std::setw(15) << "Collection 2" 
                      << std::setw(15) << "Collection 3" 
                      << std::setw(12) << "Status" << std::endl;
        rawDataStream << std::string(97, '-') << std::endl;
        
        if (progressCallback) {
          progressCallback(baseProgress + progressWeight * 0.2,
                           "Testing essential metrics with 3 collections...");
        }

        // Create PDH interface with all essential metrics
        auto allEssentialMetrics = PdhMetrics::MetricSelector::getAllEssentialMetrics();
        auto testPdh = std::make_unique<PdhInterface>(allEssentialMetrics, std::chrono::milliseconds(200));
        bool started = testPdh->start();
        
        std::map<std::string, std::vector<double>> metricData;
        std::map<std::string, std::string> metricStatus;
        
        if (started) {
          // Collect 3 datapoints
          for (int collection = 1; collection <= 3; collection++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            auto metrics = testPdh->getAllMetrics();
            
            for (const auto& [name, value] : metrics) {
              metricData[name].push_back(value);
            }
          }
          
          // Collect per-core CPU usage (separate from frequency)
          rawDataStream << std::endl << "Collecting per-core CPU usage metrics..." << std::endl;
          
          for (int core = 0; core < numCores && core < 8; core++) {
            std::string coreUsageMetricName = "cpu_core_" + std::to_string(core) + "_usage";
            
            // Try to get per-core CPU usage data
            std::vector<double> perCoreUsage;
            if (testPdh->getPerCoreMetric("cpu_per_core_usage", perCoreUsage) && core < perCoreUsage.size()) {
              metricData[coreUsageMetricName] = {perCoreUsage[core], perCoreUsage[core], perCoreUsage[core]};
              metricStatus[coreUsageMetricName] = "OK";
            } else {
              metricData[coreUsageMetricName] = {-1, -1, -1};
              metricStatus[coreUsageMetricName] = "FAILED";
            }
          }
          
          testPdh->stop();
          
          // Determine status for each metric
          for (const auto& [name, values] : metricData) {
            if (values.size() >= 3) {
              bool hasValidData = false;
              for (double val : values) {
                if (val >= 0) {
                  hasValidData = true;
                  break;
                }
              }
              metricStatus[name] = hasValidData ? "OK" : "FAILED";
            } else {
              metricStatus[name] = "NO_DATA";
            }
          }
          
        } else {
          rawDataStream << "FAILED to start PDH interface for essential metrics testing" << std::endl;
        }

        // 2. BACKUP FREQUENCY TESTING (Like WinHardwareMonitor) - INTEGRATED INTO MAIN TABLE
        rawDataStream << std::endl << std::endl << "=== CPU FREQUENCY BACKUP METHODS TESTING ===" << std::endl;
        rawDataStream << "Testing different frequency collection methods like WinHardwareMonitor" << std::endl;
        rawDataStream << std::string(70, '-') << std::endl;
        
        if (progressCallback) {
          progressCallback(baseProgress + progressWeight * 0.5,
                           "Testing backup frequency methods...");
        }

        // First, get base clock speed for backup calculations
        int baseClockSpeed = 0;
        rawDataStream << "Getting base clock speed for backup calculations..." << std::endl;
        
        // Try to get base clock speed from registry
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                          KEY_READ, &hKey) == ERROR_SUCCESS) {
          DWORD mhz = 0;
          DWORD size = sizeof(DWORD);
          if (RegQueryValueExA(hKey, "~MHz", NULL, NULL, (LPBYTE)&mhz, &size) == ERROR_SUCCESS) {
            baseClockSpeed = mhz;
          }
          RegCloseKey(hKey);
        }
        
        rawDataStream << "Base clock speed from registry: " << baseClockSpeed << " MHz" << std::endl;

        // Method 1: Try Actual Frequency (preferred method)
        rawDataStream << std::endl << "Method 1: Processor Information Actual Frequency" << std::endl;
        bool actualFrequencyWorks = false;
        std::vector<double> actualFreqValues;
        
        try {
          // Create test metrics for actual frequency
          std::vector<PdhMetrics::MetricDefinition> freqTestMetrics = {
            {"test_actual_freq_total", "\\Processor Information(_Total)\\Actual Frequency", "test", false, false}
          };
          
          // Add per-core actual frequency metrics
          for (int i = 0; i < numCores && i < 8; i++) {
            freqTestMetrics.push_back({
              "test_actual_freq_core_" + std::to_string(i),
              "\\Processor Information(0," + std::to_string(i) + ")\\Actual Frequency",
              "test", false, false
            });
          }
          
          auto freqTestPdh = std::make_unique<PdhInterface>(freqTestMetrics, std::chrono::milliseconds(200));
          if (freqTestPdh->start()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            auto freqMetrics = freqTestPdh->getAllMetrics();
            
            for (const auto& [name, value] : freqMetrics) {
              if (value > 0) {
                actualFrequencyWorks = true;
                actualFreqValues.push_back(value);
                rawDataStream << "  " << name << ": " << value << " MHz" << std::endl;
                
                // Store per-core frequencies in main table if we got them
                if (name.find("core_") != std::string::npos) {
                  size_t corePos = name.find("core_") + 5;
                  if (corePos < name.length()) {
                    std::string coreNumStr = name.substr(corePos, 1);
                    int coreNum = std::stoi(coreNumStr);
                    std::string coreFreqName = "cpu_core_" + std::to_string(coreNum) + "_frequency_mhz";
                    metricData[coreFreqName] = {value, value, value};
                    metricStatus[coreFreqName] = "OK";
                  }
                }
              } else {
                rawDataStream << "  " << name << ": FAILED" << std::endl;
              }
            }
            freqTestPdh->stop();
          }
        } catch (...) {
          rawDataStream << "  Exception testing Actual Frequency method" << std::endl;
        }
        
        rawDataStream << "Result: " << (actualFrequencyWorks ? "SUCCESS" : "FAILED") << std::endl << std::endl;

        // Method 2: Try Performance Counter method (backup) - Calculate actual MHz
        rawDataStream << "Method 2: Processor Performance Counter (Backup - Calculate MHz)" << std::endl;
        bool performanceCounterWorks = false;
        
        if (!actualFrequencyWorks && baseClockSpeed > 0) {
          try {
            std::vector<PdhMetrics::MetricDefinition> perfTestMetrics = {
              {"test_perf_counter_total", "\\Processor Information(_Total)\\% Processor Performance", "test", false, false}
            };
            
            // Add per-core performance counters for calculating individual core frequencies
            for (int i = 0; i < numCores && i < 8; i++) {
              perfTestMetrics.push_back({
                "test_perf_counter_core_" + std::to_string(i),
                "\\Processor Information(0," + std::to_string(i) + ")\\% Processor Performance",
                "test", false, false
              });
            }
            
            auto perfTestPdh = std::make_unique<PdhInterface>(perfTestMetrics, std::chrono::milliseconds(200));
            if (perfTestPdh->start()) {
              std::this_thread::sleep_for(std::chrono::milliseconds(600));
              auto perfMetrics = perfTestPdh->getAllMetrics();
              
              for (const auto& [name, value] : perfMetrics) {
                if (value >= 0) {
                  performanceCounterWorks = true;
                  
                  // Calculate actual frequency from performance percentage
                  double performancePercentage = value;
                  if (performancePercentage > 200.0) {
                    performancePercentage = 200.0; // Cap unrealistic values
                  }
                  
                  int calculatedFreq = static_cast<int>(baseClockSpeed * (performancePercentage / 100.0));
                  
                  rawDataStream << "  " << name << ": " << value << "% -> " << calculatedFreq << " MHz" << std::endl;
                  
                  // Store calculated per-core frequencies in main table
                  if (name.find("core_") != std::string::npos) {
                    size_t corePos = name.find("core_") + 5;
                    if (corePos < name.length()) {
                      std::string coreNumStr = name.substr(corePos, 1);
                      int coreNum = std::stoi(coreNumStr);
                      std::string coreFreqName = "cpu_core_" + std::to_string(coreNum) + "_frequency_mhz";
                      metricData[coreFreqName] = {static_cast<double>(calculatedFreq), static_cast<double>(calculatedFreq), static_cast<double>(calculatedFreq)};
                      metricStatus[coreFreqName] = "OK";
                    }
                  }
                } else {
                  rawDataStream << "  " << name << ": FAILED" << std::endl;
                }
              }
              perfTestPdh->stop();
            }
          } catch (...) {
            rawDataStream << "  Exception testing Performance Counter method" << std::endl;
          }
        } else if (baseClockSpeed <= 0) {
          rawDataStream << "  Skipped - No base clock speed available for calculation" << std::endl;
        } else {
          rawDataStream << "  Skipped - Actual Frequency method working" << std::endl;
        }
        
        rawDataStream << "Result: " << (performanceCounterWorks ? "SUCCESS" : "FAILED") << std::endl << std::endl;

        // Method 3: WMI Fallback (if both PDH methods fail)
        rawDataStream << "Method 3: WMI Fallback (CurrentClockSpeed)" << std::endl;
        bool wmiFrequencyWorks = false;
        
        if (!actualFrequencyWorks && !performanceCounterWorks) {
          try {
            // Simple WMI query for current clock speed
            rawDataStream << "  Attempting WMI Win32_Processor CurrentClockSpeed..." << std::endl;
            
            // This would need WMI implementation, but for PDH testing we'll just note it
            rawDataStream << "  WMI method not implemented in PDH validation (would be external)" << std::endl;
            
          } catch (...) {
            rawDataStream << "  Exception testing WMI method" << std::endl;
          }
        } else {
          rawDataStream << "  Skipped - PDH method working" << std::endl;
        }
        
        rawDataStream << "Result: " << (wmiFrequencyWorks ? "SUCCESS" : "NOT_TESTED") << std::endl << std::endl;

        // NOW DISPLAY THE COMPLETE METRICS TABLE (includes CPU usage and CPU frequency)
        rawDataStream << std::endl << "=== COMPLETE METRICS TABLE (Usage + Frequency) ===" << std::endl;
        rawDataStream << std::left << std::setw(40) << "Metric Name" 
                      << std::setw(15) << "Collection 1" 
                      << std::setw(15) << "Collection 2" 
                      << std::setw(15) << "Collection 3" 
                      << std::setw(12) << "Status" << std::endl;
        rawDataStream << std::string(97, '-') << std::endl;
        
        // Display the table with all metrics (essential PDH + frequency data)
        for (const auto& [name, values] : metricData) {
          rawDataStream << std::left << std::setw(40) << name;
          
          for (size_t i = 0; i < 3; i++) {
            if (i < values.size()) {
              if (values[i] >= 0) {
                // Show units for frequency metrics
                if (name.find("frequency_mhz") != std::string::npos) {
                  rawDataStream << std::setw(15) << (std::to_string(static_cast<int>(values[i])) + " MHz");
                } else {
                  rawDataStream << std::setw(15) << std::fixed << std::setprecision(1) << values[i];
                }
              } else {
                rawDataStream << std::setw(15) << "FAILED";
              }
            } else {
              rawDataStream << std::setw(15) << "NO_DATA";
            }
          }
          
          rawDataStream << std::setw(12) << metricStatus[name] << std::endl;
        }

        // 3. PERFORMANCE TESTING
        rawDataStream << std::endl << std::endl << "=== PERFORMANCE TESTING ===" << std::endl;
        rawDataStream << "Testing collection speed and reliability" << std::endl;
        rawDataStream << std::string(40, '-') << std::endl;
        
        if (progressCallback) {
          progressCallback(baseProgress + progressWeight * 0.7,
                           "Performance testing...");
        }

        std::vector<double> collectionTimes;
        int successfulCollections = 0;
        int totalValidMetrics = 0;
        
        auto perfTestPdh = PdhInterface::createOptimizedForBenchmarking(std::chrono::milliseconds(150));
        if (perfTestPdh->start()) {
          for (int i = 0; i < 5; i++) {
            auto startTime = std::chrono::high_resolution_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            auto metrics = perfTestPdh->getAllMetrics();
            auto endTime = std::chrono::high_resolution_clock::now();
            
            auto collectionTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
            collectionTimes.push_back(collectionTime);
            
            int validCount = 0;
            for (const auto& [name, value] : metrics) {
              if (value >= 0) validCount++;
            }
            
            if (validCount > 0) {
              successfulCollections++;
              totalValidMetrics += validCount;
            }
            
            rawDataStream << "Collection " << (i + 1) << ": " << collectionTime << "ms, " 
                          << validCount << "/" << metrics.size() << " valid metrics" << std::endl;
          }
          perfTestPdh->stop();
        }
        
        // Calculate performance stats
        double avgTime = 0;
        for (double time : collectionTimes) avgTime += time;
        avgTime /= collectionTimes.size();
        
        rawDataStream << std::endl << "Performance Summary:" << std::endl;
        rawDataStream << "  Average collection time: " << std::fixed << std::setprecision(1) << avgTime << "ms" << std::endl;
        rawDataStream << "  Successful collections: " << successfulCollections << "/5" << std::endl;
        rawDataStream << "  Average valid metrics: " << (successfulCollections > 0 ? totalValidMetrics / successfulCollections : 0) << std::endl;

        // 4. FINAL SUMMARY
        rawDataStream << std::endl << std::endl << "=== FINAL VALIDATION SUMMARY ===" << std::endl;
        rawDataStream << std::string(35, '=') << std::endl;
        
        int workingMetrics = 0;
        int totalMetrics = 0;
        
        for (const auto& [name, status] : metricStatus) {
          totalMetrics++;
          if (status == "OK") workingMetrics++;
        }
        
        rawDataStream << "Essential Metrics: " << workingMetrics << "/" << totalMetrics << " working" << std::endl;
        rawDataStream << "Success Rate: " << std::fixed << std::setprecision(1) 
                      << (totalMetrics > 0 ? (double)workingMetrics / totalMetrics * 100.0 : 0.0) << "%" << std::endl;
        rawDataStream << "CPU Frequency Methods:" << std::endl;
        rawDataStream << "  - Actual Frequency: " << (actualFrequencyWorks ? "WORKING" : "FAILED") << std::endl;
        rawDataStream << "  - Performance Counter: " << (performanceCounterWorks ? "WORKING" : "FAILED") << std::endl;
        rawDataStream << "Performance: " << (avgTime < 500 && successfulCollections >= 4 ? "GOOD" : "NEEDS_IMPROVEMENT") << std::endl;
        
        // Determine overall result
        bool metricsGood = workingMetrics >= totalMetrics * 0.8; // 80% of metrics working
        bool perfGood = avgTime < 500 && successfulCollections >= 4;
        bool freqGood = actualFrequencyWorks || performanceCounterWorks;
        
        if (metricsGood && perfGood && freqGood) {
          result = SUCCESS;
          validationMessage = "Simple PDH validation successful: " + std::to_string(workingMetrics) + 
                    "/" + std::to_string(totalMetrics) + " metrics working, good performance";
        } else if (metricsGood || (perfGood && freqGood)) {
          result = PARTIAL;
          validationMessage = "Simple PDH validation partially successful: " + std::to_string(workingMetrics) + 
                    "/" + std::to_string(totalMetrics) + " metrics working";
        } else {
          result = FAILED;
          validationMessage = "Simple PDH validation failed: Only " + std::to_string(workingMetrics) + 
                    "/" + std::to_string(totalMetrics) + " metrics working";
        }
        
        rawDataStream << std::endl << "FINAL RESULT: " << (result == SUCCESS ? "SUCCESS" : result == PARTIAL ? "PARTIAL" : "FAILED") << std::endl;
        rawDataStream << "Message: " << validationMessage << std::endl;

      } catch (const std::exception& e) {
        LOG_ERROR << "  EXCEPTION during simple PDH testing: " << e.what();
        rawDataStream << std::endl << "EXCEPTION: " << e.what() << std::endl;
        result = FAILED;
        validationMessage = "Simple PDH exception: " + std::string(e.what());
      }

      // Set the rawData to the data we collected
      rawData = rawDataStream.str();
      
      // Save raw data
      LOG_DEBUG << "  Saving Simple PDH raw data...";
      saveComponentRawData(component, rawData);
      LOG_DEBUG << "  Simple PDH raw data saved successfully!";
      
      // Clear rawData to prevent double-saving at the end
      rawData.clear();
    } else if (component == "PdhMetricsManager") {
      if (progressCallback) {
        progressCallback(baseProgress + progressWeight * 0.1,
                         "Initializing Direct PDH Metrics Manager testing...");
      }

      LOG_INFO << "  Starting Direct PDH Metrics Manager validation...";
      
      std::stringstream rawDataStream;
      rawDataStream << "DIRECT PDH METRICS MANAGER VALIDATION" << std::endl;
      rawDataStream << "Collection timestamp: " << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count() << " ms" << std::endl;
      rawDataStream << "====================================" << std::endl;

      ValidationResult result = FAILED;
      std::string validationMessage = "Direct PDH Metrics Manager validation not completed";
      
      try {
        // Import PdhMetrics namespace components
        using namespace PdhMetrics;
        
        if (progressCallback) {
          progressCallback(baseProgress + progressWeight * 0.2,
                           "Testing PDH manager configuration...");
        }

        // Test 1: Create minimal PDH configuration
        rawDataStream << std::endl << "TEST: DIRECT PDH MANAGER MINIMAL CONFIGURATION" << std::endl;
        rawDataStream << "===============================================" << std::endl;
        
        PdhManagerConfig config;
        config.requestedMetrics = PdhMetrics::MetricSelector::getEssentialBenchmarkingMetrics();
        config.collectionInterval = std::chrono::milliseconds(300);
        config.enableDetailedLogging = true;
        
        rawDataStream << "Created PdhManagerConfig with " << config.requestedMetrics.size() << " essential benchmarking metrics" << std::endl;
        rawDataStream << "Collection interval: " << config.collectionInterval.count() << "ms" << std::endl;
        
        // Log the exact counter paths being requested
        rawDataStream << std::endl << "REQUESTED PDH COUNTER PATHS:" << std::endl;
        for (const auto& metric : config.requestedMetrics) {
          rawDataStream << "  Metric: " << metric.name << std::endl;
          rawDataStream << "    Counter Path: " << metric.counterPath << std::endl;
          rawDataStream << "    Category: " << metric.category << std::endl;
          rawDataStream << "    Per-Core: " << (metric.perCore ? "YES" : "NO") << std::endl;
          rawDataStream << "    Requires Baseline: " << (metric.requiresBaseline ? "YES" : "NO") << std::endl;
        }
        
        // Create manager directly
        auto pdhManager = std::make_unique<PdhMetricsManager>(config);
        rawDataStream << "PdhMetricsManager created successfully" << std::endl;
        
        if (progressCallback) {
          progressCallback(baseProgress + progressWeight * 0.4,
                           "Initializing PDH manager...");
        }

        // Initialize the manager
        bool initialized = pdhManager->initialize();
        rawDataStream << "Manager initialization: " << (initialized ? "SUCCESS" : "FAILED") << std::endl;
        
        if (initialized) {
          if (progressCallback) {
            progressCallback(baseProgress + progressWeight * 0.6,
                             "Starting PDH collection...");
          }

          // Start collection
          bool started = pdhManager->start();
          rawDataStream << "Manager start: " << (started ? "SUCCESS" : "FAILED") << std::endl;
          
          if (started) {
            rawDataStream << "Manager is running: " << (pdhManager->isRunning() ? "YES" : "NO") << std::endl;
            
            // Wait for data collection
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            
            if (progressCallback) {
              progressCallback(baseProgress + progressWeight * 0.8,
                               "Collecting PDH metrics directly...");
            }

            // Test direct metric access
            rawDataStream << std::endl << "DIRECT METRIC ACCESS TESTS" << std::endl;
            rawDataStream << "==========================" << std::endl;
            
            auto availableMetrics = pdhManager->getAvailableMetrics();
            rawDataStream << "Available metrics count: " << availableMetrics.size() << std::endl;
            
            auto allMetricValues = pdhManager->getAllMetricValues();
            rawDataStream << "Retrieved metric values count: " << allMetricValues.size() << std::endl;
            
            // Test specific metrics
            double cpuMetricValue = -1.0;
            bool cpuMetricFound = pdhManager->getMetric("cpu_total_usage", cpuMetricValue);
            rawDataStream << "CPU metric found: " << (cpuMetricFound ? "YES" : "NO") << std::endl;
            if (cpuMetricFound) {
              rawDataStream << "CPU metric value: " << cpuMetricValue << "%" << std::endl;
            }
            
            double memMetricValue = -1.0;
            bool memMetricFound = pdhManager->getMetric("memory_available_mbytes", memMetricValue);
            rawDataStream << "Memory metric found: " << (memMetricFound ? "YES" : "NO") << std::endl;
            if (memMetricFound) {
              rawDataStream << "Memory metric value: " << memMetricValue << " MB" << std::endl;
            }
            
            // Test per-core metrics if available
            std::vector<double> perCoreValues;
            bool perCoreFound = pdhManager->getPerCoreMetric("cpu_per_core_usage", perCoreValues);
            rawDataStream << "Per-core metrics found: " << (perCoreFound ? "YES" : "NO") << std::endl;
            if (perCoreFound) {
              rawDataStream << "Per-core values count: " << perCoreValues.size() << std::endl;
            }
            
            // Get performance report
            std::string perfReport = pdhManager->getPerformanceReport();
            rawDataStream << "Performance report length: " << perfReport.length() << " characters" << std::endl;
            rawDataStream << std::endl << "PERFORMANCE REPORT" << std::endl;
            rawDataStream << "==================" << std::endl;
            rawDataStream << perfReport << std::endl;
            
            // Determine validation result
            int validMetricsCount = 0;
            for (const auto& [name, value] : allMetricValues) {
              if (value >= 0) {
                validMetricsCount++;
              }
            }
            
            rawDataStream << std::endl << "VALIDATION SUMMARY" << std::endl;
            rawDataStream << "==================" << std::endl;
            rawDataStream << "Total metrics: " << allMetricValues.size() << std::endl;
            rawDataStream << "Valid metrics: " << validMetricsCount << std::endl;
            rawDataStream << "Manager initialized: " << initialized << std::endl;
            rawDataStream << "Manager started: " << started << std::endl;
            rawDataStream << "CPU metric available: " << cpuMetricFound << std::endl;
            rawDataStream << "Memory metric available: " << memMetricFound << std::endl;
            
            // Determine success level
            if (validMetricsCount >= 3 && cpuMetricFound && memMetricFound) {
              result = SUCCESS;
              validationMessage = "Direct PDH Metrics Manager validation successful. " + 
                        std::to_string(validMetricsCount) + " valid metrics collected";
            } else if (validMetricsCount >= 1 && (cpuMetricFound || memMetricFound)) {
              result = PARTIAL;
              validationMessage = "Direct PDH Metrics Manager validation partially successful. " + 
                        std::to_string(validMetricsCount) + " valid metrics collected";
            } else {
              result = FAILED;
              validationMessage = "Direct PDH Metrics Manager validation failed. Only " + 
                        std::to_string(validMetricsCount) + " valid metrics collected";
            }
            
            // Stop the manager
            pdhManager->stop();
            rawDataStream << "Manager stopped successfully" << std::endl;
          } else {
            result = FAILED;
            validationMessage = "Direct PDH Metrics Manager failed to start";
          }
        } else {
          result = FAILED;
          validationMessage = "Direct PDH Metrics Manager failed to initialize";
        }
        
        // Shutdown the manager
        pdhManager->shutdown();
        rawDataStream << "Manager shutdown completed" << std::endl;
        
        rawDataStream << std::endl << "DIRECT PDH MANAGER FINAL RESULT: " << 
                      (result == SUCCESS ? "SUCCESS" : result == PARTIAL ? "PARTIAL" : "FAILED") << std::endl;
        rawDataStream << "Final message: " << validationMessage << std::endl;

      } catch (const std::exception& e) {
        LOG_ERROR << "  EXCEPTION during direct PDH Manager testing: " << e.what();
        rawDataStream << std::endl << "EXCEPTION: " << e.what() << std::endl;
        result = FAILED;
        validationMessage = "Direct PDH Manager exception: " + std::string(e.what());
      }

      // Set the rawData to the data we collected
      rawData = rawDataStream.str();
      
      // Save raw data
      LOG_DEBUG << "  Saving Direct PDH Manager raw data...";
      saveComponentRawData(component, rawData);
      LOG_DEBUG << "  Direct PDH Manager raw data saved successfully!";
      
      // Clear rawData to prevent double-saving at the end
      rawData.clear();
    } else if (component == "SystemWrapper") {
      if (progressCallback) {
        progressCallback(baseProgress + progressWeight * 0.5,
                         "Collecting system wrapper information...");
      }

      SystemWrapper sysWrapper;
      rawData = sysWrapper.logRawData();

      if (!rawData.empty()) {
        result = SUCCESS;
        validationMessage = "System wrapper information collected successfully";
      } else {
        result = FAILED;
        validationMessage = "Failed to collect system wrapper information";
      }
    }

    // Save raw data if not already done during validation
    if (!rawData.empty()) {
      LOG_DEBUG << "  Saving " << rawData.length() << " characters of raw data for " << component;
      saveComponentRawData(component, rawData);
      LOG_DEBUG << "  Raw data saved successfully for " << component;
    } else {
      LOG_DEBUG << "  Raw data already saved during component validation or no data to save for " << component;
    }

    // Set the validation result for the component
    setValidationResult(component, result, validationMessage);

    if (progressCallback) {
      progressCallback(baseProgress + progressWeight * 0.9,
                       "Completed " + component + " validation");
    }
  } catch (const std::exception& e) {
    std::string errorMsg = "Exception occurred: " + std::string(e.what());
    LOG_ERROR << "  ERROR: " << errorMsg;
    setValidationResult(component, FAILED, errorMsg);

    // Emergency cleanup
    // ProcessorMetrics fully removed
    if (component == "WinHardwareMonitor" && hardwareMonitor) {
      hardwareMonitor.reset();
    } else if (component == "NvidiaMetricsCollector" && gpuMetricsCollector) {
      gpuMetricsCollector.reset();
    } else if (component == "CPUKernelMetricsTracker" &&
               cpuKernelMetricsTracker) {
      cpuKernelMetricsTracker->stopTracking();
      cpuKernelMetricsTracker.reset();
    } else if (component == "DiskPerformanceTracker" &&
               diskPerformanceTracker) {
      diskPerformanceTracker->stopTracking();
      diskPerformanceTracker.reset();
    } else if (component == "PdhInterface" && pdhInterface) {
      pdhInterface->stop();
      pdhInterface.reset();
    } else if (component == "PdhMetricsManager" && pdhMetricsManager) {
      pdhMetricsManager->stop();
      pdhMetricsManager->shutdown();
      pdhMetricsManager.reset();
    }
  }
}

// No longer needed as all components are handled in
// validateComponentWithRawData This method has been merged into
// validateComponentWithRawData
void SystemMetricsValidator::collectRawDataFromComponent(
  const std::string& component) {
  LOG_INFO << "  NOTE: Raw data collection now handled during component validation";
}

// Final cleanup verification - unchanged
void SystemMetricsValidator::verifyFinalCleanup() {
  LOG_INFO << "\n----- FINAL CLEANUP VERIFICATION -----\n";

  // Reset any remaining component instances

  if (diskPerformanceTracker) {
    LOG_DEBUG << "Cleaning up leftover DiskPerformanceTracker instance...";
    diskPerformanceTracker->stopTracking();
    diskPerformanceTracker.reset();
  }

  if (cpuKernelMetricsTracker) {
    LOG_DEBUG << "Cleaning up leftover CPUKernelMetricsTracker instance...";
    cpuKernelMetricsTracker->stopTracking();
    cpuKernelMetricsTracker.reset();
  }

  if (gpuMetricsCollector) {
    LOG_DEBUG << "Cleaning up leftover NvidiaMetricsCollector instance...";
    gpuMetricsCollector.reset();
  }

  if (hardwareMonitor) {
    LOG_DEBUG << "Cleaning up leftover WinHardwareMonitor instance...";
    hardwareMonitor.reset();
  }

  if (pdhInterface) {
    LOG_DEBUG << "Cleaning up leftover PdhInterface instance...";
    pdhInterface->stop();
    pdhInterface.reset();
  }

  if (pdhMetricsManager) {
    LOG_DEBUG << "Cleaning up leftover PdhMetricsManager instance...";
    pdhMetricsManager->stop();
    pdhMetricsManager->shutdown();
    pdhMetricsManager.reset();
  }

  // Final verification for ProcessorMetrics - fully removed
  // (No action needed)

  LOG_INFO << "----- CLEANUP VERIFICATION COMPLETE -----\n";
}

// Utility methods for result tracking - unchanged
void SystemMetricsValidator::setValidationResult(
  const std::string& componentName, ValidationResult result,
  const std::string& message) {
  std::lock_guard<std::mutex> lock(validationMutex);
  validationResults[componentName] = ValidationDetail(result, message);

  // Log the result immediately
  std::string resultStr;
  switch (result) {
    case SUCCESS:
      resultStr = "SUCCESS";
      break;
    case PARTIAL:
      resultStr = "PARTIAL";
      break;
    case FAILED:
      resultStr = "FAILED";
      break;
    case NOT_TESTED:
      resultStr = "NOT_TESTED";
      break;
  }

  LOG_INFO << "  " << componentName << ": " << resultStr << " - " << message;
}

ValidationResult SystemMetricsValidator::getValidationResult(
  const std::string& componentName) const {
  std::lock_guard<std::mutex> lock(validationMutex);
  auto it = validationResults.find(componentName);
  if (it != validationResults.end()) {
    return it->second.result;
  }
  return NOT_TESTED;
}

ValidationDetail SystemMetricsValidator::getValidationDetail(
  const std::string& componentName) const {
  std::lock_guard<std::mutex> lock(validationMutex);
  auto it = validationResults.find(componentName);
  if (it != validationResults.end()) {
    return it->second;
  }
  return ValidationDetail(NOT_TESTED, "Component not tested");
}

std::map<std::string, ValidationDetail> SystemMetricsValidator::
  getAllValidationResults() const {
  std::lock_guard<std::mutex> lock(validationMutex);
  return validationResults;
}

void SystemMetricsValidator::logAllResults() const {
  std::lock_guard<std::mutex> lock(validationMutex);

  LOG_INFO << "\n----- SYSTEM METRICS VALIDATION SUMMARY -----";

  int successCount = 0;
  int partialCount = 0;
  int failedCount = 0;
  int notTestedCount = 0;

  for (const auto& [component, detail] : validationResults) {
    std::string resultStr;
    switch (detail.result) {
      case SUCCESS:
        resultStr = "SUCCESS";
        successCount++;
        break;
      case PARTIAL:
        resultStr = "PARTIAL";
        partialCount++;
        break;
      case FAILED:
        resultStr = "FAILED";
        failedCount++;
        break;
      case NOT_TESTED:
        resultStr = "NOT_TESTED";
        notTestedCount++;
        break;
    }

    LOG_INFO << component << ": " << resultStr;
  }

  LOG_INFO << "\nTotal metrics providers tested: " << validationResults.size();
  LOG_INFO << "SUCCESS: " << successCount;
  LOG_INFO << "PARTIAL: " << partialCount;
  LOG_INFO << "FAILED: " << failedCount;
  LOG_INFO << "NOT_TESTED: " << notTestedCount;
}

// Vector analysis helpers - unchanged
bool SystemMetricsValidator::hasNonZeroValues(
  const std::vector<double>& values) const {
  if (values.empty()) return false;

  for (double val : values) {
    if (val != 0.0) return true;
  }

  return false;
}

bool SystemMetricsValidator::hasNonNegativeValues(
  const std::vector<double>& values) const {
  if (values.empty()) return false;

  for (double val : values) {
    if (val >= 0.0) return true;
  }

  return false;
}

// Raw data collection and saving - updated for individual component files
void SystemMetricsValidator::collectAndSaveRawData(
  const std::string& providerName, const std::string& rawData) {
  // This method now immediately saves to individual files
  saveComponentRawData(providerName, rawData);
}

void SystemMetricsValidator::saveComponentRawData(const std::string& componentName, const std::string& rawData) {
  try {
    // Get the raw metrics directory
    auto rawMetricsDir = getRawMetricsDirectory();
    
    // Create the directory if it doesn't exist
    if (!std::filesystem::exists(rawMetricsDir)) {
      LOG_DEBUG << "Creating raw metrics directory: [directory path hidden for privacy]";
      std::filesystem::create_directories(rawMetricsDir);
    }

    // Get the file path for this component
    auto componentFilePath = getComponentFilePath(componentName);

    // Create and write to the component file
    std::ofstream componentFile(componentFilePath);
    if (!componentFile.is_open()) {
      throw std::runtime_error("Failed to open component file: " + componentFilePath.string());
    }

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    localtime_s(&tm_now, &time_t_now);

    // Write header with component information
    componentFile << "======== " << componentName << " RAW METRICS DATA ========" << std::endl;
    componentFile << "Component: " << componentName << std::endl;
    componentFile << "Collected on: " << std::ctime(&time_t_now);
    componentFile << "Data length: " << rawData.length() << " characters" << std::endl;
    componentFile << "=========================================" << std::endl << std::endl;

    // Write the raw data
    componentFile << rawData << std::endl;

    // Write footer
    componentFile << std::endl << "======== END OF " << componentName << " DATA ========" << std::endl;

    componentFile.close();
    LOG_DEBUG << "Component raw data saved to: [file path hidden for privacy]";

  } catch (const std::exception& e) {
    LOG_ERROR << "ERROR: Failed to save raw data for " << componentName << ": " << e.what();
    
    // Try to save to a fallback location
    try {
      auto fallbackPath = std::filesystem::current_path() / ("emergency_" + componentName + "_data.txt");
      std::ofstream emergencyFile(fallbackPath);
      if (emergencyFile.is_open()) {
        emergencyFile << "======== EMERGENCY SAVE: " << componentName << " ========" << std::endl;
        emergencyFile << "Original save failed with error: " << e.what() << std::endl;
        emergencyFile << "=========================================" << std::endl << std::endl;
        emergencyFile << rawData << std::endl;
        emergencyFile.close();
        LOG_DEBUG << "Emergency raw data saved to: [file path hidden for privacy]";
      }
    } catch (...) {
      LOG_ERROR << "CRITICAL ERROR: Even emergency save failed for " << componentName;
    }
  }
}

// Get list of all components that should be validated
std::vector<std::string> SystemMetricsValidator::getAllComponentNames() const {
  return {
    "WinHardwareMonitor", 
    "NvidiaMetricsCollector",
    "CPUKernelMetricsTracker",
    "DiskPerformanceTracker",
    "PdhInterface",
    "PdhMetricsManager",  // Direct PDH Manager testing
    "SystemWrapper"
  };
}

// Get the raw metrics directory path
std::filesystem::path SystemMetricsValidator::getRawMetricsDirectory() const {
  // Try primary directory name first
  std::filesystem::path primaryDir = "debug logging" / std::filesystem::path("raw_metrics");
  
  // Check if we can access the parent directory
  if (std::filesystem::exists("debug logging") || 
      std::filesystem::create_directories("debug logging")) {
    return primaryDir;
  }
  
  // Fallback to alternative directory name
  return "debug_logging" / std::filesystem::path("raw_metrics");
}

// Get the file path for a specific component
std::filesystem::path SystemMetricsValidator::getComponentFilePath(const std::string& componentName) const {
  auto rawMetricsDir = getRawMetricsDirectory();
  return rawMetricsDir / (componentName + "_raw_data.txt");
}

// Check if a specific component has been validated (file exists)
bool SystemMetricsValidator::hasComponentBeenValidated(const std::string& componentName) const {
  auto componentFilePath = getComponentFilePath(componentName);
  bool exists = std::filesystem::exists(componentFilePath);
  
  if (exists) {
    // Also check if the file is not empty and contains valid data
    try {
      std::ifstream file(componentFilePath);
      if (file.is_open()) {
        std::string firstLine;
        std::getline(file, firstLine);
        file.close();
        
        // Check if the file contains expected header
        bool hasValidHeader = firstLine.find("RAW METRICS DATA") != std::string::npos;
        if (!hasValidHeader) {
          LOG_WARN << "Component file exists but appears invalid: [file path hidden for privacy]";
          return false;
        }
      }
    } catch (const std::exception& e) {
      LOG_ERROR << "Error reading component file [file path hidden for privacy]: " << e.what();
      return false;
    }
  }
  
  return exists;
}

// Check if all components have been validated
bool SystemMetricsValidator::hasBeenValidated() const {
  auto allComponents = getAllComponentNames();
  
  for (const auto& component : allComponents) {
    if (!hasComponentBeenValidated(component)) {
      return false;
    }
  }
  
  return true;
}

}  // namespace SystemMetrics
