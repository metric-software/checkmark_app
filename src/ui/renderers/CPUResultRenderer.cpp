#include "CPUResultRenderer.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "DiagnosticViewComponents.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"
#include "network/api/DownloadApiClient.h"

#include "logging/Logger.h"


namespace DiagnosticRenderers {

// New static method to load comparison data from JSON files
std::map<QString, CPUComparisonData> CPUResultRenderer::loadCPUComparisonData() {
  std::map<QString, CPUComparisonData> comparisonData;

  // Find the comparison_data folder
  QString appDir = QApplication::applicationDirPath();
  QDir dataDir(appDir + "/comparison_data");

  if (!dataDir.exists()) {
    LOG_ERROR << "Comparison data folder not found: " << dataDir.absolutePath().toStdString();
    return comparisonData;
  }

  // Look for CPU benchmark JSON files
  QStringList filters;
  filters << "cpu_benchmark_*.json";
  dataDir.setNameFilters(filters);

  QStringList cpuFiles = dataDir.entryList(QDir::Files);
  for (const QString& fileName : cpuFiles) {
    QFile file(dataDir.absoluteFilePath(fileName));

    if (file.open(QIODevice::ReadOnly)) {
      QByteArray jsonData = file.readAll();
      QJsonDocument doc = QJsonDocument::fromJson(jsonData);

      if (doc.isObject()) {
        QJsonObject rootObj = doc.object();

        CPUComparisonData cpu;
        cpu.model = rootObj["model"].toString();
        cpu.fullModel = rootObj["full_model"].toString();
        cpu.cores = rootObj["cores"].toInt();
        cpu.threads = rootObj["threads"].toInt();
        cpu.baseClock = rootObj["base_frequency_mhz"].toInt();
        cpu.boostClock = rootObj["boost_frequency_mhz"].toInt();
        cpu.architecture = rootObj["architecture"].toString();

        // Get cache info
        if (rootObj.contains("cache") && rootObj["cache"].isObject()) {
          QJsonObject cacheObj = rootObj["cache"].toObject();
          cpu.l1CacheKB = cacheObj["l1_kb"].toInt();
          cpu.l2CacheKB = cacheObj["l2_kb"].toInt();
          cpu.l3CacheKB = cacheObj["l3_kb"].toInt();
        }

        // Get boost summary
        if (rootObj.contains("boost_summary") &&
            rootObj["boost_summary"].isObject()) {
          QJsonObject boostObj = rootObj["boost_summary"].toObject();
          cpu.boostAllCorePowerW = boostObj["all_core_power_w"].toDouble();
          cpu.boostIdlePowerW = boostObj["idle_power_w"].toDouble();
          cpu.boostSingleCorePowerW =
            boostObj["single_core_power_w"].toDouble();
          cpu.boostBestCore = boostObj["best_boosting_core"].toInt();
          cpu.boostMaxDeltaMhz = boostObj["max_boost_delta_mhz"].toDouble();
        }

        // Get cold start metrics
        if (rootObj.contains("cold_start") &&
            rootObj["cold_start"].isObject()) {
          QJsonObject coldStartObj = rootObj["cold_start"].toObject();
          cpu.coldStartAvg = coldStartObj["avg_response_time_us"].toDouble();
          cpu.coldStartMin = coldStartObj["min_response_time_us"].toDouble();
          cpu.coldStartMax = coldStartObj["max_response_time_us"].toDouble();
          cpu.coldStartStdDev = coldStartObj["std_dev_us"].toDouble();
          cpu.coldStartJitter = coldStartObj["jitter_us"].toDouble();
          if (cpu.coldStartJitter <= 0 && cpu.coldStartMin > 0 &&
              cpu.coldStartMax > 0) {
            cpu.coldStartJitter = cpu.coldStartMax - cpu.coldStartMin;
          }
        }

        // Get benchmark results
        if (rootObj.contains("benchmark_results") &&
            rootObj["benchmark_results"].isObject()) {
          QJsonObject resultsObj = rootObj["benchmark_results"].toObject();
          cpu.singleCoreTime = resultsObj["single_core_ms"].toDouble();

          // Only look for explicit thread counts, don't use multicore values
          cpu.fourThreadTime = resultsObj["four_thread_ms"].toDouble();

          // Keep other benchmark result loading
          cpu.simdScalar = resultsObj["simd_scalar_us"].toDouble();
          cpu.simdAvx = resultsObj["avx_us"].toDouble();
          cpu.primeTime = resultsObj["prime_time_ms"].toDouble();
          cpu.gameSimSmall = resultsObj["game_sim_small_ups"].toDouble();
          cpu.gameSimMedium = resultsObj["game_sim_medium_ups"].toDouble();
          cpu.gameSimLarge = resultsObj["game_sim_large_ups"].toDouble();
        }

        // Get cache latencies
        if (rootObj.contains("cache_latencies") &&
            rootObj["cache_latencies"].isArray()) {
          QJsonArray latencyArray = rootObj["cache_latencies"].toArray();
          for (const QJsonValue& value : latencyArray) {
            if (value.isObject()) {
              QJsonObject latencyObj = value.toObject();
              int sizeKB = latencyObj["size_kb"].toInt();
              double latency = latencyObj["latency"].toDouble();
              cpu.cacheLatencies[sizeKB] = latency;
            }
          }
        }

        // Extract boost metrics if available
        if (rootObj.contains("cores_detail") &&
            rootObj["cores_detail"].isArray()) {
          QJsonArray coresArray = rootObj["cores_detail"].toArray();
          for (const QJsonValue& value : coresArray) {
            if (value.isObject()) {
              QJsonObject coreObj = value.toObject();
              CoreBoostMetrics metrics;

              metrics.coreNumber = coreObj["core_number"].toInt();

              if (coreObj.contains("boost_metrics") &&
                  coreObj["boost_metrics"].isObject()) {
                QJsonObject boostObj = coreObj["boost_metrics"].toObject();
                metrics.allCoreClock = boostObj["all_core_clock_mhz"].toInt();
                metrics.idleClock = boostObj["idle_clock_mhz"].toInt();
                metrics.singleLoadClock =
                  boostObj["single_load_clock_mhz"].toInt();
                metrics.boostDelta = boostObj["boost_delta_mhz"].toInt();
              }

              cpu.boostMetrics.push_back(metrics);
            }
          }
        }

        // Use model name with core count as the key for the map
        QString displayName = cpu.model + " (" + QString::number(cpu.cores) +
                              "/" + QString::number(cpu.threads) + ")";
        comparisonData[displayName] = cpu;
      }

      file.close();
    }
  }

  return comparisonData;
}

// Network-based method to convert ComponentData to CPUComparisonData
CPUComparisonData CPUResultRenderer::convertNetworkDataToCPU(const ComponentData& networkData) {
  CPUComparisonData cpu;
  
  LOG_INFO << "CPUResultRenderer: Converting network data to CPU comparison data";
  
  // Log the COMPLETE ComponentData structure
  std::cout << "\n=== CPUResultRenderer: COMPLETE ComponentData DEBUG INFO ===" << std::endl;
  std::cout << "Component Name: " << networkData.componentName.toStdString() << std::endl;
  
  // Log testData JSON
  QJsonDocument testDataDoc(networkData.testData);
  QString testDataString = testDataDoc.toJson(QJsonDocument::Indented);
  std::cout << "testData JSON:\n" << testDataString.toStdString() << std::endl;
  
  // Log metaData JSON  
  QJsonDocument metaDataDoc(networkData.metaData);
  QString metaDataString = metaDataDoc.toJson(QJsonDocument::Indented);
  std::cout << "metaData JSON:\n" << metaDataString.toStdString() << std::endl;
  
  std::cout << "=== END ComponentData DEBUG INFO ===\n" << std::endl;
  
  // The testData QJsonObject contains the full component structure
  QJsonObject rootData = networkData.testData;
  
  // Extract general info
  cpu.model = rootData.value("model").toString();
  cpu.fullModel = rootData.value("full_model").toString();
  cpu.cores = rootData.value("cores").toInt();
  cpu.threads = rootData.value("threads").toInt();
  cpu.baseClock = rootData.value("base_frequency_mhz").toInt();
  cpu.boostClock = rootData.value("boost_frequency_mhz").toInt();
  cpu.architecture = rootData.value("architecture").toString();

  if (rootData.contains("boost_summary") &&
      rootData["boost_summary"].isObject()) {
    QJsonObject boostObj = rootData["boost_summary"].toObject();
    cpu.boostAllCorePowerW = boostObj.value("all_core_power_w").toDouble();
    cpu.boostIdlePowerW = boostObj.value("idle_power_w").toDouble();
    cpu.boostSingleCorePowerW =
      boostObj.value("single_core_power_w").toDouble();
    cpu.boostBestCore = boostObj.value("best_boosting_core").toInt();
    cpu.boostMaxDeltaMhz = boostObj.value("max_boost_delta_mhz").toDouble();
  }

  // Extract benchmark results from the nested object
  if (rootData.contains("benchmark_results") && rootData["benchmark_results"].isObject()) {
      QJsonObject benchmarks = rootData["benchmark_results"].toObject();
      cpu.singleCoreTime = benchmarks.value("single_core_ms").toDouble();
      cpu.fourThreadTime = benchmarks.value("four_thread_ms").toDouble();
      cpu.simdScalar = benchmarks.value("simd_scalar_us").toDouble();
      cpu.simdAvx = benchmarks.value("avx_us").toDouble();
      cpu.primeTime = benchmarks.value("prime_time_ms").toDouble();
      cpu.gameSimSmall = benchmarks.value("game_sim_small_ups").toDouble();
      cpu.gameSimMedium = benchmarks.value("game_sim_medium_ups").toDouble();
      cpu.gameSimLarge = benchmarks.value("game_sim_large_ups").toDouble();
  }
  
  LOG_INFO << "CPUResultRenderer: Performance data - single_core=" << cpu.singleCoreTime
           << "ms, four_thread=" << cpu.fourThreadTime << "ms, simd_scalar=" << cpu.simdScalar << "us";
  
  // Extract cold start metrics if available
  if (rootData.contains("cold_start") && rootData["cold_start"].isObject()) {
    QJsonObject coldStart = rootData["cold_start"].toObject();
    cpu.coldStartAvg = coldStart.value("avg_response_time_us").toDouble();
    cpu.coldStartMin = coldStart.value("min_response_time_us").toDouble();
    cpu.coldStartMax = coldStart.value("max_response_time_us").toDouble();
    cpu.coldStartStdDev = coldStart.value("std_dev_us").toDouble();
    cpu.coldStartJitter = coldStart.value("jitter_us").toDouble();
    if (cpu.coldStartJitter <= 0 && cpu.coldStartMin > 0 &&
        cpu.coldStartMax > 0) {
      cpu.coldStartJitter = cpu.coldStartMax - cpu.coldStartMin;
    }
    
    LOG_INFO << "CPUResultRenderer: Cold start data - avg=" << cpu.coldStartAvg << "us";
  }
  
  // Extract cache latencies if available
  if (rootData.contains("cache_latencies") && rootData["cache_latencies"].isArray()) {
    QJsonArray cacheArray = rootData["cache_latencies"].toArray();
    for (const QJsonValue& value : cacheArray) {
      if (value.isObject()) {
        QJsonObject cacheObj = value.toObject();
        int sizeKB = cacheObj["size_kb"].toInt();
        double latencyNS = cacheObj["latency"].toDouble();
        cpu.cacheLatencies[sizeKB] = latencyNS;
      }
    }
    LOG_INFO << "CPUResultRenderer: Loaded " << cpu.cacheLatencies.size() << " cache latency entries";
  }
  
  // Extract cache info
  if (rootData.contains("cache") && rootData["cache"].isObject()) {
      QJsonObject cacheInfo = rootData["cache"].toObject();
      cpu.l1CacheKB = cacheInfo.value("l1_kb").toInt();
      cpu.l2CacheKB = cacheInfo.value("l2_kb").toInt();
      cpu.l3CacheKB = cacheInfo.value("l3_kb").toInt();
  }

  // Extract boost metrics
  if (rootData.contains("cores_detail") && rootData["cores_detail"].isArray()) {
      QJsonArray coresDetail = rootData["cores_detail"].toArray();
      for (const QJsonValue& coreVal : coresDetail) {
          QJsonObject coreObj = coreVal.toObject();
          if (coreObj.contains("boost_metrics") && coreObj["boost_metrics"].isObject()) {
              QJsonObject boostObj = coreObj["boost_metrics"].toObject();
              CoreBoostMetrics metrics;
              metrics.allCoreClock = boostObj.value("all_core_clock_mhz").toInt();
              metrics.idleClock = boostObj.value("idle_clock_mhz").toInt();
              metrics.singleLoadClock = boostObj.value("single_load_clock_mhz").toInt();
              metrics.boostDelta = boostObj.value("boost_delta_mhz").toInt();
              cpu.boostMetrics.push_back(metrics);
          }
      }
  }
  
  std::cout << "CPUResultRenderer: Successfully parsed performance data!" << std::endl;
  std::cout << "Single-core: " << cpu.singleCoreTime << "ms, Four-thread: " << cpu.fourThreadTime << "ms" << std::endl;
  
  LOG_INFO << "CPUResultRenderer: Conversion complete";
  return cpu;
}

// Create dropdown data structure from menu (names only, no performance data yet)
std::map<QString, CPUComparisonData> CPUResultRenderer::createDropdownDataFromMenu(const MenuData& menuData) {
  std::map<QString, CPUComparisonData> dropdownData;
  
  // Create placeholder entries for each CPU in the menu
  for (const QString& cpuName : menuData.availableCpus) {
    CPUComparisonData cpu;
    cpu.model = cpuName; // Only the name is known at this point
    // All other fields remain at default/zero values
    dropdownData[cpuName] = cpu;
  }
  
  LOG_INFO << "CPUResultRenderer: Created dropdown data for " << dropdownData.size() << " CPUs from menu";
  return dropdownData;
}

// New method to generate aggregated CPU data
std::map<QString, DiagnosticViewComponents::AggregatedComponentData<CPUComparisonData>> CPUResultRenderer::
  generateAggregatedCPUData(
    const std::map<QString, CPUComparisonData>& individualData) {
  std::map<QString,
           DiagnosticViewComponents::AggregatedComponentData<CPUComparisonData>>
    result;

  // Group results by CPU model (ignoring the individual identifiers)
  std::map<QString, std::vector<std::pair<QString, CPUComparisonData>>>
    groupedData;

  for (const auto& [id, data] : individualData) {
    // Extract base CPU model name without core/thread count
    QString modelName = data.model;

    // Add to the corresponding group
    groupedData[modelName].push_back({id, data});
  }

  // Create aggregated data for each CPU model
  for (const auto& [modelName, dataList] : groupedData) {
    DiagnosticViewComponents::AggregatedComponentData<CPUComparisonData>
      aggregated;
    aggregated.componentName = modelName;
    
    // Store the original full name from the first entry (for API requests)
    if (!dataList.empty()) {
      aggregated.originalFullName = dataList[0].second.model;
    }

    // Start with the first entry as both best and average
    if (!dataList.empty()) {
      const auto& firstData = dataList[0].second;
      aggregated.bestResult = firstData;
      aggregated.averageResult = firstData;

      // Add all individual results
      for (const auto& [id, data] : dataList) {
        aggregated.individualResults[id] = data;
      }

      // We need to handle multiple metrics for "best" - let's define "best"
      // based on key metrics For lower-is-better metrics, find minimum
      double minSingleCore = firstData.singleCoreTime;
      double minFourThread = firstData.fourThreadTime;
      double minSimdScalar = firstData.simdScalar;
      double minSimdAvx = firstData.simdAvx;
      double minPrimeTime = firstData.primeTime;
      double minColdStartAvg = firstData.coldStartAvg;
      double minColdStartMin = firstData.coldStartMin;
      double minColdStartMax = firstData.coldStartMax;
      double minColdStartStdDev = firstData.coldStartStdDev;
      double minColdStartJitter = firstData.coldStartJitter;

      // For higher-is-better metrics, find maximum
      double maxGameSimSmall = firstData.gameSimSmall;
      double maxGameSimMedium = firstData.gameSimMedium;
      double maxGameSimLarge = firstData.gameSimLarge;

      // Initialize sums for averages
      double sumSingleCore = firstData.singleCoreTime;
      double sumFourThread = firstData.fourThreadTime;
      double sumSimdScalar = firstData.simdScalar;
      double sumSimdAvx = firstData.simdAvx;
      double sumPrimeTime = firstData.primeTime;
      double sumColdStartAvg = firstData.coldStartAvg;
      double sumColdStartMin = firstData.coldStartMin;
      double sumColdStartMax = firstData.coldStartMax;
      double sumColdStartStdDev = firstData.coldStartStdDev;
      double sumColdStartJitter = firstData.coldStartJitter;
      double sumGameSimSmall = firstData.gameSimSmall;
      double sumGameSimMedium = firstData.gameSimMedium;
      double sumGameSimLarge = firstData.gameSimLarge;

      // Process all entries after the first
      for (size_t i = 1; i < dataList.size(); i++) {
        const auto& data = dataList[i].second;

        // Update minimums for lower-is-better metrics
        if (data.singleCoreTime > 0) {
          minSingleCore = std::min(minSingleCore, data.singleCoreTime);
          sumSingleCore += data.singleCoreTime;
        }

        if (data.fourThreadTime > 0) {
          minFourThread = std::min(minFourThread, data.fourThreadTime);
          sumFourThread += data.fourThreadTime;
        }

        if (data.simdScalar > 0) {
          minSimdScalar = std::min(minSimdScalar, data.simdScalar);
          sumSimdScalar += data.simdScalar;
        }

        if (data.simdAvx > 0) {
          minSimdAvx = std::min(minSimdAvx, data.simdAvx);
          sumSimdAvx += data.simdAvx;
        }

        if (data.primeTime > 0) {
          minPrimeTime = std::min(minPrimeTime, data.primeTime);
          sumPrimeTime += data.primeTime;
        }

        if (data.coldStartAvg > 0) {
          minColdStartAvg = std::min(minColdStartAvg, data.coldStartAvg);
          sumColdStartAvg += data.coldStartAvg;
        }
        if (data.coldStartMin > 0) {
          minColdStartMin = std::min(minColdStartMin, data.coldStartMin);
          sumColdStartMin += data.coldStartMin;
        }
        if (data.coldStartMax > 0) {
          minColdStartMax = std::min(minColdStartMax, data.coldStartMax);
          sumColdStartMax += data.coldStartMax;
        }
        if (data.coldStartStdDev > 0) {
          minColdStartStdDev = std::min(minColdStartStdDev, data.coldStartStdDev);
          sumColdStartStdDev += data.coldStartStdDev;
        }
        if (data.coldStartJitter > 0) {
          minColdStartJitter = std::min(minColdStartJitter, data.coldStartJitter);
          sumColdStartJitter += data.coldStartJitter;
        }

        // Update maximums for higher-is-better metrics
        if (data.gameSimSmall > 0) {
          maxGameSimSmall = std::max(maxGameSimSmall, data.gameSimSmall);
          sumGameSimSmall += data.gameSimSmall;
        }

        if (data.gameSimMedium > 0) {
          maxGameSimMedium = std::max(maxGameSimMedium, data.gameSimMedium);
          sumGameSimMedium += data.gameSimMedium;
        }

        if (data.gameSimLarge > 0) {
          maxGameSimLarge = std::max(maxGameSimLarge, data.gameSimLarge);
          sumGameSimLarge += data.gameSimLarge;
        }
      }

      // Set the best result values
      aggregated.bestResult.singleCoreTime = minSingleCore;
      aggregated.bestResult.fourThreadTime = minFourThread;
      aggregated.bestResult.simdScalar = minSimdScalar;
      aggregated.bestResult.simdAvx = minSimdAvx;
      aggregated.bestResult.primeTime = minPrimeTime;
      aggregated.bestResult.coldStartAvg = minColdStartAvg;
      aggregated.bestResult.coldStartMin = minColdStartMin;
      aggregated.bestResult.coldStartMax = minColdStartMax;
      aggregated.bestResult.coldStartStdDev = minColdStartStdDev;
      aggregated.bestResult.coldStartJitter = minColdStartJitter;
      aggregated.bestResult.gameSimSmall = maxGameSimSmall;
      aggregated.bestResult.gameSimMedium = maxGameSimMedium;
      aggregated.bestResult.gameSimLarge = maxGameSimLarge;

      // Calculate averages
      size_t count = dataList.size();
      aggregated.averageResult.singleCoreTime = sumSingleCore / count;
      aggregated.averageResult.fourThreadTime = sumFourThread / count;
      aggregated.averageResult.simdScalar = sumSimdScalar / count;
      aggregated.averageResult.simdAvx = sumSimdAvx / count;
      aggregated.averageResult.primeTime = sumPrimeTime / count;
      aggregated.averageResult.coldStartAvg = sumColdStartAvg / count;
      aggregated.averageResult.coldStartMin = sumColdStartMin / count;
      aggregated.averageResult.coldStartMax = sumColdStartMax / count;
      aggregated.averageResult.coldStartStdDev = sumColdStartStdDev / count;
      aggregated.averageResult.coldStartJitter = sumColdStartJitter / count;
      aggregated.averageResult.gameSimSmall = sumGameSimSmall / count;
      aggregated.averageResult.gameSimMedium = sumGameSimMedium / count;
      aggregated.averageResult.gameSimLarge = sumGameSimLarge / count;

      // Copy CPU info from first result (these should be the same for all runs
      // of the same model)
      aggregated.bestResult.model = modelName;
      aggregated.bestResult.fullModel = firstData.fullModel;
      aggregated.bestResult.cores = firstData.cores;
      aggregated.bestResult.threads = firstData.threads;
      aggregated.bestResult.baseClock = firstData.baseClock;
      aggregated.bestResult.boostClock = firstData.boostClock;
      aggregated.bestResult.architecture = firstData.architecture;
      aggregated.bestResult.l1CacheKB = firstData.l1CacheKB;
      aggregated.bestResult.l2CacheKB = firstData.l2CacheKB;
      aggregated.bestResult.l3CacheKB = firstData.l3CacheKB;

      // Same for average result
      aggregated.averageResult.model = modelName;
      aggregated.averageResult.fullModel = firstData.fullModel;
      aggregated.averageResult.cores = firstData.cores;
      aggregated.averageResult.threads = firstData.threads;
      aggregated.averageResult.baseClock = firstData.baseClock;
      aggregated.averageResult.boostClock = firstData.boostClock;
      aggregated.averageResult.architecture = firstData.architecture;
      aggregated.averageResult.l1CacheKB = firstData.l1CacheKB;
      aggregated.averageResult.l2CacheKB = firstData.l2CacheKB;
      aggregated.averageResult.l3CacheKB = firstData.l3CacheKB;

      // For cache latencies, aggregate best and average
      for (auto it = firstData.cacheLatencies.begin();
           it != firstData.cacheLatencies.end(); ++it) {
        int cacheSize = it.key();     // Get the key directly
        double latency = it.value();  // Get the value directly

        double minLatency = latency;
        double sumLatency = latency;
        int validResults = 1;

        // Process remaining entries
        for (size_t i = 1; i < dataList.size(); i++) {
          const auto& data = dataList[i].second;
          if (data.cacheLatencies.contains(
                cacheSize)) {  // Use contains() for clarity
            double val = data.cacheLatencies.value(cacheSize);
            if (val > 0) {
              minLatency = std::min(minLatency, val);
              sumLatency += val;
              validResults++;
            }
          }
        }

        // Set best (minimum) and average latencies
        aggregated.bestResult.cacheLatencies[cacheSize] = minLatency;
        aggregated.averageResult.cacheLatencies[cacheSize] =
          sumLatency / validResults;
      }

      // For boost metrics, use the highest observed boost values
      if (!firstData.boostMetrics.empty()) {
        // Use the first dataset as a starting point
        aggregated.bestResult.boostMetrics = firstData.boostMetrics;
        aggregated.averageResult.boostMetrics = firstData.boostMetrics;

        // Populate arrays with zeros for averaging
        std::vector<int> sumSingleLoadClock(firstData.boostMetrics.size(), 0);
        std::vector<int> sumAllCoreClock(firstData.boostMetrics.size(), 0);
        std::vector<int> countValidBoosts(firstData.boostMetrics.size(),
                                          1);  // Start at 1 for first dataset

        // Add the first dataset's values
        for (size_t i = 0; i < firstData.boostMetrics.size(); i++) {
          sumSingleLoadClock[i] = firstData.boostMetrics[i].singleLoadClock;
          sumAllCoreClock[i] = firstData.boostMetrics[i].allCoreClock;
        }

        // Process other datasets
        for (size_t dataIdx = 1; dataIdx < dataList.size(); dataIdx++) {
          const auto& data = dataList[dataIdx].second;

          // Skip if no boost metrics
          if (data.boostMetrics.empty()) continue;

          // Update best values
          for (size_t i = 0;
               i < std::min(aggregated.bestResult.boostMetrics.size(),
                            data.boostMetrics.size());
               i++) {
            // Update best values (maximum)
            if (data.boostMetrics[i].singleLoadClock >
                aggregated.bestResult.boostMetrics[i].singleLoadClock) {
              aggregated.bestResult.boostMetrics[i].singleLoadClock =
                data.boostMetrics[i].singleLoadClock;
            }

            if (data.boostMetrics[i].allCoreClock >
                aggregated.bestResult.boostMetrics[i].allCoreClock) {
              aggregated.bestResult.boostMetrics[i].allCoreClock =
                data.boostMetrics[i].allCoreClock;
            }

            // Accumulate for averages
            sumSingleLoadClock[i] += data.boostMetrics[i].singleLoadClock;
            sumAllCoreClock[i] += data.boostMetrics[i].allCoreClock;
            countValidBoosts[i]++;
          }
        }

        // Calculate averages
        for (size_t i = 0; i < aggregated.averageResult.boostMetrics.size();
             i++) {
          int currentCount =
            countValidBoosts[i];  // Use temp var to potentially resolve C7732
          if (currentCount > 0) {
            int currentSumSingle = sumSingleLoadClock[i];  // Use temp var
            int currentSumAll = sumAllCoreClock[i];        // Use temp var

            // Access the vector element first to potentially help the compiler
            CoreBoostMetrics& avgMetrics =
              aggregated.averageResult.boostMetrics[i];

            // Split calculations into separate steps to avoid C7732
            int singleResult = currentSumSingle / currentCount;
            avgMetrics.singleLoadClock = singleResult;

            int allCoreResult = currentSumAll / currentCount;
            avgMetrics.allCoreClock = allCoreResult;
          }
        }
      }
    }

    // Add to the result map
    result[modelName] = aggregated;
  }

  return result;
}

// Replace the existing createCPUComparisonDropdown function

QComboBox* CPUResultRenderer::createCPUComparisonDropdown(
  const std::map<QString, CPUComparisonData>& comparisonData,
  QWidget* containerWidget, QWidget* cpuTestsBox, QWidget* gameSimBox,
  const QPair<double, double>& singleCoreVals,
  const QPair<double, double>& fourThreadVals,
  const QPair<double, double>& simdScalarVals,
  const QPair<double, double>& simdAvxVals,
  const QPair<double, double>& primeTimeVals,
  const QPair<double, double>& gameSimSmallVals,
  const QPair<double, double>& gameSimMediumVals,
  const QPair<double, double>& gameSimLargeVals,
  const QPair<double, double>& coldStartVals,
  DownloadApiClient* downloadClient) {

  // Generate aggregated data from individual results
  auto aggregatedData = generateAggregatedCPUData(comparisonData);

  auto updateColdStartDetails =
    [containerWidget](const CPUComparisonData* cpuData, bool isTypical,
                      const QString& displayName) {
      struct ColdStartField {
        const char* labelName;
        double CPUComparisonData::*value;
      };

      const ColdStartField fields[] = {
        {"cold_start_min_value", &CPUComparisonData::coldStartMin},
        {"cold_start_max_value", &CPUComparisonData::coldStartMax},
        {"cold_start_std_value", &CPUComparisonData::coldStartStdDev},
        {"cold_start_jitter_value", &CPUComparisonData::coldStartJitter},
      };

      for (const auto& field : fields) {
        QLabel* valueLabel =
          containerWidget->findChild<QLabel*>(field.labelName);
        if (!valueLabel) {
          continue;
        }

        const QVariant userValueVar = valueLabel->property("userValue");
        if (!userValueVar.isValid()) {
          continue;
        }

        const double userValue = userValueVar.toDouble();
        const QString unitText = valueLabel->property("unit").toString();
        const QString unit = unitText.isEmpty() ? "us" : unitText;
        QString labelText = QString("%1 %2").arg(userValue, 0, 'f', 1).arg(unit);

        const double compValue =
          cpuData ? (cpuData->*field.value) : 0.0;
        if (compValue > 0) {
          const QString prefix =
            isTypical ? "typical: " : displayName + ": ";
          labelText =
            QString("%1<br><span style='color: #FF4444;'>%2%3 %4</span>")
              .arg(labelText)
              .arg(prefix)
              .arg(compValue, 0, 'f', 1)
              .arg(unit);
        }

        valueLabel->setText(labelText);
        valueLabel->setTextFormat(Qt::RichText);
        valueLabel->setWordWrap(true);
      }
    };

  auto updateBoostSection =
    [containerWidget](const CPUComparisonData& cpuData,
                      const QString& displayName, bool isTypical) {
      QLabel* compCpuLabel =
        containerWidget->findChild<QLabel*>("comp_cpu_name");
      QLabel* compBaseClockLabel =
        containerWidget->findChild<QLabel*>("comp_base_clock");
      QLabel* compBoostClockLabel =
        containerWidget->findChild<QLabel*>("comp_boost_clock");
      QLabel* compAllCoreClockLabel =
        containerWidget->findChild<QLabel*>("comp_all_core_clock");

      if (!compCpuLabel && !compBaseClockLabel && !compBoostClockLabel &&
          !compAllCoreClockLabel) {
        return;
      }

      if (compCpuLabel) {
        compCpuLabel->setText(displayName);
        compCpuLabel->setStyleSheet("color: #ffffff; background: transparent;");
      }

      auto setPlaceholder = [](QLabel* label) {
        if (!label) return;
        label->setText("-");
        label->setStyleSheet(
          "color: #888888; font-style: italic; background: transparent;");
      };

      if (compBaseClockLabel) {
        if (cpuData.baseClock > 0) {
          compBaseClockLabel->setText(QString("%1 MHz").arg(cpuData.baseClock));
          compBaseClockLabel->setStyleSheet(
            "color: #FF4444; background: transparent;");
          compBaseClockLabel->setVisible(true);
        } else {
          setPlaceholder(compBaseClockLabel);
        }
      }

      int maxSingleBoost = 0;
      int maxAllCore = 0;
      for (const auto& boost : cpuData.boostMetrics) {
        if (boost.singleLoadClock > maxSingleBoost) {
          maxSingleBoost = boost.singleLoadClock;
        }
        if (boost.allCoreClock > maxAllCore) {
          maxAllCore = boost.allCoreClock;
        }
      }
      if (maxSingleBoost <= 0 && cpuData.boostClock > 0) {
        maxSingleBoost = cpuData.boostClock;
      }
      if (maxSingleBoost <= 0 && cpuData.baseClock > 0 &&
          cpuData.boostMaxDeltaMhz > 0) {
        maxSingleBoost = static_cast<int>(
          std::round(cpuData.baseClock + cpuData.boostMaxDeltaMhz));
      }

      const bool hasBaseClock = cpuData.baseClock > 0;
      const double compSingleBoostPct =
        (hasBaseClock && maxSingleBoost > 0)
          ? (100.0 * (maxSingleBoost - cpuData.baseClock) /
             cpuData.baseClock)
          : 0.0;
      const double compAllCoreBoostPct =
        (hasBaseClock && maxAllCore > 0)
          ? (100.0 * (maxAllCore - cpuData.baseClock) / cpuData.baseClock)
          : 0.0;

      if (compBoostClockLabel) {
        if (maxSingleBoost > 0 ||
            (isTypical && cpuData.boostMaxDeltaMhz > 0)) {
          if (isTypical) {
            if (cpuData.boostMaxDeltaMhz > 0) {
              const QString deltaText =
                QString("typical: +%1 MHz")
                  .arg(cpuData.boostMaxDeltaMhz, 0, 'f', 1);
              compBoostClockLabel->setText(deltaText);
              compBoostClockLabel->setTextFormat(Qt::PlainText);
              compBoostClockLabel->setStyleSheet(
                "color: #FF4444; background: transparent;");
            } else if (hasBaseClock) {
              const QString pctText =
                QString("%1%2%")
                  .arg(compSingleBoostPct >= 0 ? "+" : "")
                  .arg(compSingleBoostPct, 0, 'f', 1);
              compBoostClockLabel->setText("typical: " + pctText);
              compBoostClockLabel->setTextFormat(Qt::PlainText);
              compBoostClockLabel->setStyleSheet(
                "color: #FF4444; background: transparent;");
            } else {
              setPlaceholder(compBoostClockLabel);
            }
          } else {
            QString boostText = QString("%1 MHz").arg(maxSingleBoost);
            if (hasBaseClock) {
              boostText +=
                QString(" <span style='color: #FFAA00;'>(%1%2%)</span>")
                  .arg(compSingleBoostPct >= 0 ? "+" : "")
                  .arg(compSingleBoostPct, 0, 'f', 1);
            }
            compBoostClockLabel->setText(boostText);
            compBoostClockLabel->setTextFormat(Qt::RichText);
            compBoostClockLabel->setStyleSheet(
              "color: #FF4444; background: transparent;");
          }
          compBoostClockLabel->setVisible(true);
        } else {
          setPlaceholder(compBoostClockLabel);
        }
      }

      if (compAllCoreClockLabel) {
        if (maxAllCore > 0) {
          if (isTypical) {
            if (hasBaseClock) {
              const QString pctText =
                QString("%1%2%")
                  .arg(compAllCoreBoostPct >= 0 ? "+" : "")
                  .arg(compAllCoreBoostPct, 0, 'f', 1);
              compAllCoreClockLabel->setText("typical: " + pctText);
              compAllCoreClockLabel->setTextFormat(Qt::PlainText);
              compAllCoreClockLabel->setStyleSheet(
                "color: #FF4444; background: transparent;");
            } else {
              setPlaceholder(compAllCoreClockLabel);
            }
          } else {
            QString boostText = QString("%1 MHz").arg(maxAllCore);
            if (hasBaseClock) {
              boostText +=
                QString(" <span style='color: #FFAA00;'>(%1%2%)</span>")
                  .arg(compAllCoreBoostPct >= 0 ? "+" : "")
                  .arg(compAllCoreBoostPct, 0, 'f', 1);
            }
            compAllCoreClockLabel->setText(boostText);
            compAllCoreClockLabel->setTextFormat(Qt::RichText);
            compAllCoreClockLabel->setStyleSheet(
              "color: #FF4444; background: transparent;");
          }
          compAllCoreClockLabel->setVisible(true);
        } else {
          setPlaceholder(compAllCoreClockLabel);
        }
      }
    };

  struct TestMetric {
    QString objectName;
    double userValue;
    double compValue;
    QString unit;
    bool lowerIsBetter;
    int groupId;
  };

  constexpr int kGroupCore = 0;
  constexpr int kGroupSimd = 1;
  constexpr int kGroupPrime = 2;
  constexpr int kGroupGameSim = 3;
  constexpr int kGroupColdStart = 4;

  const QPair<double, double> singleCoreValsCopy = singleCoreVals;
  const QPair<double, double> fourThreadValsCopy = fourThreadVals;
  const QPair<double, double> simdScalarValsCopy = simdScalarVals;
  const QPair<double, double> simdAvxValsCopy = simdAvxVals;
  const QPair<double, double> primeTimeValsCopy = primeTimeVals;
  const QPair<double, double> gameSimSmallValsCopy = gameSimSmallVals;
  const QPair<double, double> gameSimMediumValsCopy = gameSimMediumVals;
  const QPair<double, double> gameSimLargeValsCopy = gameSimLargeVals;
  const QPair<double, double> coldStartValsCopy = coldStartVals;

  auto buildTests =
    [singleCoreValsCopy, fourThreadValsCopy, simdScalarValsCopy,
     simdAvxValsCopy, primeTimeValsCopy, gameSimSmallValsCopy,
     gameSimMediumValsCopy, gameSimLargeValsCopy, coldStartValsCopy](
      const CPUComparisonData* compData) {
    const double compSingle = compData ? compData->singleCoreTime : 0.0;
    const double compFour = compData ? compData->fourThreadTime : 0.0;
    const double compScalar = compData ? compData->simdScalar : 0.0;
    const double compAvx = compData ? compData->simdAvx : 0.0;
    const double compPrime = compData ? compData->primeTime : 0.0;
    const double compSmall =
      compData ? (compData->gameSimSmall / 1000000.0) : 0.0;
    const double compMedium =
      compData ? (compData->gameSimMedium / 1000000.0) : 0.0;
    const double compLarge =
      compData ? (compData->gameSimLarge / 1000000.0) : 0.0;
    const double compCold = compData ? compData->coldStartAvg : 0.0;

    std::vector<TestMetric> tests = {
      {"comparison_bar_single_core", singleCoreValsCopy.first, compSingle, "ms",
       true, kGroupCore},
      {"comparison_bar_four_thread", fourThreadValsCopy.first, compFour, "ms", true,
       kGroupCore},
      {"comparison_bar_scalar", simdScalarValsCopy.first, compScalar, "I¬s", true,
       kGroupSimd},
      {"comparison_bar_avx", simdAvxValsCopy.first, compAvx, "I¬s", true,
       kGroupSimd},
      {"comparison_bar_prime", primeTimeValsCopy.first, compPrime, "ms", true,
       kGroupPrime},
      {"comparison_bar_small", gameSimSmallValsCopy.first / 1000000.0, compSmall,
       "M ups", false, kGroupGameSim},
      {"comparison_bar_medium", gameSimMediumValsCopy.first / 1000000.0, compMedium,
       "M ups", false, kGroupGameSim},
      {"comparison_bar_large", gameSimLargeValsCopy.first / 1000000.0, compLarge,
       "M ups", false, kGroupGameSim},
      {"comparison_bar_cold_start", coldStartValsCopy.first, compCold, "I¬s", true,
       kGroupColdStart}};

    return tests;
  };

  auto computeGroupMax = [](const std::vector<TestMetric>& tests) {
    QMap<int, double> groupMax;
    for (const auto& test : tests) {
      if (test.userValue > 0) {
        groupMax[test.groupId] =
          std::max(groupMax.value(test.groupId, 0.0), test.userValue);
      }
      if (test.compValue > 0) {
        groupMax[test.groupId] =
          std::max(groupMax.value(test.groupId, 0.0), test.compValue);
      }
    }
    return groupMax;
  };

  auto updateUserBarLayout = [](QWidget* parentContainer, int percentage) {
    QWidget* userBarContainer =
      parentContainer->findChild<QWidget*>("userBarContainer");
    if (!userBarContainer) {
      return;
    }

    QHBoxLayout* userBarLayout =
      userBarContainer->findChild<QHBoxLayout*>("user_bar_layout");
    if (!userBarLayout) {
      return;
    }

    QWidget* userBar = userBarContainer->findChild<QWidget*>("user_bar_fill");
    QWidget* userSpacer =
      userBarContainer->findChild<QWidget*>("user_bar_spacer");
    if (!userBar || !userSpacer) {
      return;
    }

    const int barIndex = userBarLayout->indexOf(userBar);
    const int spacerIndex = userBarLayout->indexOf(userSpacer);
    if (barIndex >= 0) {
      userBarLayout->setStretch(barIndex, percentage);
    }
    if (spacerIndex >= 0) {
      userBarLayout->setStretch(spacerIndex, 100 - percentage);
    }
  };

  auto updateComparisonBars =
    [computeGroupMax, updateUserBarLayout](
      const QList<QWidget*>& allBars, const std::vector<TestMetric>& tests,
      const QString& displayName, bool hasSelection) {
      QHash<QString, TestMetric> testMap;
      for (const auto& test : tests) {
        testMap.insert(test.objectName, test);
      }

      const QMap<int, double> groupMax = computeGroupMax(tests);

      for (QWidget* bar : allBars) {
        auto it = testMap.find(bar->objectName());
        if (it == testMap.end()) {
          continue;
        }

        const TestMetric test = it.value();
        const double maxValue = groupMax.value(test.groupId, 0.0);
        const double scaledMax = maxValue > 0 ? maxValue * 1.25 : 0.0;
        const int userPercentage =
          (test.userValue > 0 && scaledMax > 0)
            ? static_cast<int>(
                std::min(100.0, (test.userValue / scaledMax) * 100.0))
            : 0;

        QWidget* parentContainer = bar->parentWidget();
        if (!parentContainer) {
          continue;
        }

        QLabel* nameLabel =
          parentContainer->findChild<QLabel*>("comp_name_label");
        if (nameLabel) {
          if (hasSelection) {
            nameLabel->setText(displayName);
            nameLabel->setStyleSheet("color: #ffffff; background: transparent;");
          } else {
            nameLabel->setText("Select CPU to compare");
            nameLabel->setStyleSheet(
              "color: #888888; font-style: italic; background: transparent;");
          }
        }

        updateUserBarLayout(parentContainer, userPercentage);

        QLabel* valueLabel = parentContainer->findChild<QLabel*>("value_label");
        QLayout* layout = bar->layout();
        if (layout) {
          QLayoutItem* child;
          while ((child = layout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
          }

          if (!hasSelection || test.compValue <= 0) {
            QWidget* emptyBar = new QWidget();
            emptyBar->setStyleSheet("background-color: transparent;");
            QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
            if (newLayout) {
              newLayout->addWidget(emptyBar);
            }
          } else {
            const int compPercentage =
              (scaledMax > 0)
                ? static_cast<int>(std::min(
                    100.0, (test.compValue / scaledMax) * 100.0))
                : 0;

            QWidget* barWidget = new QWidget();
            barWidget->setFixedHeight(16);
            barWidget->setStyleSheet(
              "background-color: #FF4444; border-radius: 2px;");

            QWidget* spacer = new QWidget();
            spacer->setStyleSheet("background-color: transparent;");

            QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
            if (newLayout) {
              newLayout->addWidget(barWidget, compPercentage);
              newLayout->addWidget(spacer, 100 - compPercentage);
            }
          }
        }

        if (valueLabel) {
          if (!hasSelection || test.compValue <= 0) {
            valueLabel->setText("-");
            valueLabel->setStyleSheet(
              "color: #888888; font-style: italic; background: transparent;");
          } else {
            valueLabel->setText(
              QString("%1 %2").arg(test.compValue, 0, 'f', 1).arg(test.unit));
            valueLabel->setStyleSheet("color: #FF4444; background: transparent;");
          }
        }

        QWidget* userBarContainer =
          parentContainer->findChild<QWidget*>("userBarContainer");
        QWidget* userBarFill = userBarContainer
                                 ? userBarContainer->findChild<QWidget*>(
                                     "user_bar_fill")
                                 : nullptr;
        if (userBarFill) {
          QLabel* existingLabel =
            userBarFill->findChild<QLabel*>("percentageLabel");
          if (existingLabel) {
            delete existingLabel;
          }

          if (hasSelection && test.compValue > 0 && test.userValue > 0) {
            double percentChange = 0;
            if (test.lowerIsBetter) {
              percentChange = ((test.userValue / test.compValue) - 1.0) * 100.0;
            } else {
              percentChange = ((test.userValue / test.compValue) - 1.0) * 100.0;
            }

            QString percentText;
            QString percentColor;

            const bool isBetter =
              (test.lowerIsBetter && percentChange < 0) ||
              (!test.lowerIsBetter && percentChange > 0);
            const bool isApproxEqual = qAbs(percentChange) < 1.0;

            if (isApproxEqual) {
              percentText = "≈";
              percentColor = "#FFAA00";
            } else {
              percentText =
                QString("%1%2%")
                  .arg(isBetter ? "+" : "")
                  .arg(percentChange, 0, 'f', 1);
              percentColor = isBetter ? "#44FF44" : "#FF4444";
            }

            QHBoxLayout* overlayLayout =
              userBarFill->findChild<QHBoxLayout*>("overlayLayout");
            if (!overlayLayout) {
              overlayLayout = new QHBoxLayout(userBarFill);
              overlayLayout->setObjectName("overlayLayout");
              overlayLayout->setContentsMargins(0, 0, 0, 0);
            }

            QLabel* percentageLabel = new QLabel(percentText);
            percentageLabel->setObjectName("percentageLabel");
            percentageLabel->setStyleSheet(
              QString(
                "color: %1; background: transparent; font-weight: bold;")
                .arg(percentColor));
            percentageLabel->setAlignment(Qt::AlignCenter);
            overlayLayout->addWidget(percentageLabel);
          }
        }
      }
    };

  // Create a callback function to handle selection changes
  auto selectionCallback = [containerWidget, cpuTestsBox, gameSimBox,
                            downloadClient, updateColdStartDetails,
                            updateBoostSection, buildTests,
                            updateComparisonBars](
                             const QString& componentName,
                             const QString& originalFullName,
                             DiagnosticViewComponents::AggregationType type,
                             const CPUComparisonData& cpuData) {
    
    // If downloadClient is available and cpuData has no performance data (only name), 
    // fetch the actual data from the server
    if (downloadClient && !componentName.isEmpty() && cpuData.singleCoreTime <= 0) {
      LOG_INFO << "CPUResultRenderer: Fetching network data for CPU: " << componentName.toStdString() << " using original name: " << originalFullName.toStdString();
      
      downloadClient->fetchComponentData("cpu", originalFullName, 
        [containerWidget, cpuTestsBox, gameSimBox, componentName, type,
         updateColdStartDetails, updateBoostSection, buildTests,
         updateComparisonBars]
        (bool success, const ComponentData& networkData, const QString& error) {
          
          if (success) {
            LOG_INFO << "CPUResultRenderer: Successfully fetched CPU data for " << componentName.toStdString();
            
            // Convert network data to CPUComparisonData
            CPUComparisonData fetchedCpuData = convertNetworkDataToCPU(networkData);
            
            // Update comparison bars with fetched data
            QList<QWidget*> allBars;
            allBars = cpuTestsBox->findChildren<QWidget*>(QRegularExpression("^comparison_bar_"));
            QList<QWidget*> gameBars = gameSimBox->findChildren<QWidget*>(QRegularExpression("^comparison_bar_"));
            allBars.append(gameBars);
            
            // Create display name
            QString displayName = (componentName == DownloadApiClient::generalAverageLabel())
              ? componentName
              : componentName + " (" +
                  (type == DiagnosticViewComponents::AggregationType::Best ? "Best)" : "Avg)");
            
            LOG_INFO << "CPUResultRenderer: Updating comparison bars with fetched data";
            
            const std::vector<TestMetric> tests = buildTests(&fetchedCpuData);
            updateComparisonBars(allBars, tests, displayName, true);

            const bool isTypical =
              (componentName == DownloadApiClient::generalAverageLabel());
            updateColdStartDetails(&fetchedCpuData, isTypical, displayName);
            updateBoostSection(fetchedCpuData, displayName, isTypical);
            
          } else {
            LOG_ERROR << "CPUResultRenderer: Failed to fetch CPU data for " << componentName.toStdString() 
                      << ": " << error.toStdString();
            // Continue with empty/placeholder data
          }
        });
      
      return; // Exit early - the network callback will handle the UI update
    }
    
    // Find all comparison bars
    QList<QWidget*> allBars;
    allBars = cpuTestsBox->findChildren<QWidget*>(
      QRegularExpression("^comparison_bar_"));
    QList<QWidget*> gameBars = gameSimBox->findChildren<QWidget*>(
      QRegularExpression("^comparison_bar_"));
    allBars.append(gameBars);

    const bool hasSelection = !componentName.isEmpty();
    const bool isTypical =
      (componentName == DownloadApiClient::generalAverageLabel());
    const QString displayName =
      hasSelection
        ? (componentName == DownloadApiClient::generalAverageLabel()
             ? componentName
             : componentName + " (" +
                 (type == DiagnosticViewComponents::AggregationType::Best
                    ? "Best)"
                    : "Avg)"))
        : QString("Select CPU to compare");

    const std::vector<TestMetric> tests =
      buildTests(hasSelection ? &cpuData : nullptr);
    updateComparisonBars(allBars, tests, displayName, hasSelection);

    if (!hasSelection) {
      QLabel* compCpuLabel =
        containerWidget->findChild<QLabel*>("comp_cpu_name");
      QLabel* compBaseClockLabel =
        containerWidget->findChild<QLabel*>("comp_base_clock");
      QLabel* compBoostClockLabel =
        containerWidget->findChild<QLabel*>("comp_boost_clock");
      QLabel* compAllCoreClockLabel =
        containerWidget->findChild<QLabel*>("comp_all_core_clock");

      if (compCpuLabel) {
        compCpuLabel->setText("Select CPU to compare");
        compCpuLabel->setStyleSheet(
          "color: #888888; font-style: italic; background: transparent;");
      }
      if (compBaseClockLabel) {
        compBaseClockLabel->setText("-");
        compBaseClockLabel->setStyleSheet(
          "color: #888888; font-style: italic; background: transparent;");
      }
      if (compBoostClockLabel) {
        compBoostClockLabel->setText("-");
        compBoostClockLabel->setStyleSheet(
          "color: #888888; font-style: italic; background: transparent;");
      }
      if (compAllCoreClockLabel) {
        compAllCoreClockLabel->setText("-");
        compAllCoreClockLabel->setStyleSheet(
          "color: #888888; font-style: italic; background: transparent;");
      }

      updateColdStartDetails(nullptr, false, QString());
      return;
    }

    updateColdStartDetails(&cpuData, isTypical, displayName);
    updateBoostSection(cpuData, displayName, isTypical);
  };

  // Use the shared helper to create the dropdown
  return DiagnosticViewComponents::createAggregatedComparisonDropdown<
    CPUComparisonData>(aggregatedData, selectionCallback);
}

// New method to create a performance bar with comparison
QWidget* CPUResultRenderer::createComparisonPerformanceBar(
  const QString& label, double value, double comparisonValue, double maxValue,
  const QString& unit, bool lowerIsBetter) {

  return CPUResultRenderer::createComparisonPerformanceBar(
    label, value, comparisonValue, maxValue, unit, QString(), lowerIsBetter);
}

QWidget* CPUResultRenderer::createComparisonPerformanceBar(
  const QString& label, double value, double comparisonValue, double maxValue,
  const QString& unit, const char* description, bool lowerIsBetter) {
  return CPUResultRenderer::createComparisonPerformanceBar(
    label, value, comparisonValue, maxValue, unit,
    QString::fromUtf8(description ? description : ""), lowerIsBetter);
}

QWidget* CPUResultRenderer::createComparisonPerformanceBar(
  const QString& label, double value, double comparisonValue, double maxValue,
  const QString& unit, const QString& description, bool lowerIsBetter) {

  // Use the shared component from DiagnosticViewComponents
  return DiagnosticViewComponents::createComparisonPerformanceBar(
    label, value, comparisonValue, maxValue, unit, description, lowerIsBetter);
}

QWidget* CPUResultRenderer::createCPUResultWidget(
  const QString& result, const std::vector<CoreBoostMetrics>& boostMetrics, 
  const MenuData* networkMenuData, DownloadApiClient* downloadClient) {
  // Get data from DiagnosticDataStore first
  auto& dataStore = DiagnosticDataStore::getInstance();
  const auto& cpuData = dataStore.getCPUData();

  // Get constant system information first to ensure we have CPU name
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();

  // Initialize values with data from DiagnosticDataStore
  std::string cpuModel =
    constantInfo.cpuName;  // Get CPU name directly from constant info
  int coreCount = cpuData.physicalCores > 0 ? cpuData.physicalCores
                                            : constantInfo.physicalCores;
  int threadCount =
    cpuData.threadCount > 0 ? cpuData.threadCount : constantInfo.logicalCores;
  int l1CacheKB = cpuData.cache.l1SizeKB;
  int l2CacheKB = cpuData.cache.l2SizeKB;
  int l3CacheKB = cpuData.cache.l3SizeKB;
  bool hyperThreading = constantInfo.hyperThreadingEnabled;

  // Get performance metrics from DiagnosticDataStore
  double simdScalar = cpuData.simdScalar;
  double simdAvx = cpuData.simdAvx;
  double primeTime = cpuData.primeTime;
  double singleCoreTime = cpuData.singleCoreTime;
  double fourThreadTime = cpuData.fourThreadTime;
  double gameSimSmall = cpuData.gameSimUPS_small;
  double gameSimMedium = cpuData.gameSimUPS_medium;
  double gameSimLarge = cpuData.gameSimUPS_large;

  // Get cold start test results
  double coldStartAvg = cpuData.coldStart.avgResponseTimeUs;
  double coldStartMin = cpuData.coldStart.minResponseTimeUs;
  double coldStartMax = cpuData.coldStart.maxResponseTimeUs;
  double coldStartStdDev = cpuData.coldStart.stdDevUs;

  // Format cache strings
  QString l2Cache = l2CacheKB > 0 ? QString::number(l2CacheKB) + " KB" : "N/A";
  QString l3Cache = l3CacheKB > 0 ? QString::number(l3CacheKB) + " KB" : "N/A";

  // If critical data is missing from DataStore, fall back to parsing the text
  // result
  if (cpuModel == "no_data" || cpuModel.empty() || coreCount <= 0 ||
      threadCount <= 0) {
    QStringList lines = result.split('\n');
    for (const QString& line : lines) {
      if (line.contains("Model:") &&
          (cpuModel == "no_data" || cpuModel.empty()))
        cpuModel = line.split("Model:").last().trimmed().toStdString();

      else if ((line.contains("Physical Cores:") ||
                line.contains("CPU Physical Cores:")) &&
               coreCount <= 0) {
        QString cores = line.split("Cores:").last().trimmed();
        QRegularExpression numRegex("\\d+");
        QRegularExpressionMatch coreMatch = numRegex.match(cores);
        if (coreMatch.hasMatch()) {
          coreCount = coreMatch.captured(0).toInt();
        }
      }

      else if ((line.contains("CPU Threads:") || line.contains("Threads:")) &&
               threadCount <= 0) {
        QString threads = line.split("Threads:").last().trimmed();
        QRegularExpression numRegex("\\d+");
        QRegularExpressionMatch threadMatch = numRegex.match(threads);
        if (threadMatch.hasMatch()) {
          threadCount = threadMatch.captured(0).toInt();
        }
      }

      // If cache info is missing
      if ((l2CacheKB <= 0 || l3CacheKB <= 0) && line.contains("Cache:")) {
        QRegularExpression cacheRegex(
          "L2:\\s*([\\d.]+)\\s*KB,\\s*L3:\\s*([\\d.]+)\\s*KB");
        auto match = cacheRegex.match(line);
        if (match.hasMatch()) {
          l2Cache = match.captured(1).trimmed() + " KB";
          l3Cache = match.captured(2).trimmed() + " KB";
        }
      }

      // If performance metrics are missing
      if (simdScalar <= 0 && line.contains("SIMD Scalar:"))
        simdScalar =
          line.split("Scalar:").last().split("us").first().trimmed().toDouble();

      if (simdAvx <= 0 && line.contains("AVX:"))
        simdAvx =
          line.split("AVX:").last().split("us").first().trimmed().toDouble();

      if (primeTime <= 0 && line.contains("Prime:"))
        primeTime =
          line.split("Prime:").last().split("ms").first().trimmed().toDouble();

      if (singleCoreTime <= 0 && line.contains("Single:"))
        singleCoreTime =
          line.split("Single:").last().split("ms").first().trimmed().toDouble();

      // Add parsing for 4-thread tests
      if (fourThreadTime <= 0 && line.contains("4-Thread:"))
        fourThreadTime = line.split("4-Thread:")
                           .last()
                           .split("ms")
                           .first()
                           .trimmed()
                           .toDouble();

      // No fallback to multi-core - completely ignore it

      if (gameSimSmall <= 0 && line.contains("Game Sim Small:"))
        gameSimSmall =
          line.split("Small:").last().split("ups").first().trimmed().toDouble();

      if (gameSimMedium <= 0 && line.contains("Game Sim Medium:"))
        gameSimMedium =
          line.split("Medium:").last().split("ups").first().trimmed().toDouble();

      if (gameSimLarge <= 0 && line.contains("Game Sim Large:"))
        gameSimLarge =
          line.split("Large:").last().split("ups").first().trimmed().toDouble();

      // Look for cold start metrics
      if (coldStartAvg <= 0 && line.contains("Avg Response:"))
        coldStartAvg = line.split("Avg Response:")
                         .last()
                         .split("µs")
                         .first()
                         .trimmed()
                         .toDouble();

      if (coldStartMin <= 0 && line.contains("Min Response:"))
        coldStartMin = line.split("Min Response:")
                         .last()
                         .split("µs")
                         .first()
                         .trimmed()
                         .toDouble();

      if (coldStartMax <= 0 && line.contains("Max Response:"))
        coldStartMax = line.split("Max Response:")
                         .last()
                         .split("µs")
                         .first()
                         .trimmed()
                         .toDouble();

      if (coldStartStdDev <= 0 && line.contains("Std Deviation:"))
        coldStartStdDev = line.split("Std Deviation:")
                            .last()
                            .split("µs")
                            .first()
                            .trimmed()
                            .toDouble();
    }
  }

  // If we still don't have values, use ConstantSystemInfo
  if (cpuModel == "no_data" || cpuModel.empty())
    cpuModel = constantInfo.cpuName;
  if (coreCount <= 0) coreCount = constantInfo.physicalCores;
  if (threadCount <= 0) threadCount = constantInfo.logicalCores;
  if (!hyperThreading && constantInfo.hyperThreadingEnabled)
    hyperThreading = true;

  // Get boost metrics
  int baseClock = constantInfo.baseClockMHz;

  // If base clock isn't available from ConstantSystemInfo, try
  // No fallback to SystemInfoProvider; use only ConstantSystemInfo

  // Find highest single-core boost frequency and all-core frequencies
  int maxSingleCoreBoost = 0;
  int maxAllCoreFreq = 0;
  double avgAllCoreFreq = 0.0;
  int numCores = boostMetrics.size();

  for (const auto& coreMetric : boostMetrics) {
    if (coreMetric.singleLoadClock > maxSingleCoreBoost) {
      maxSingleCoreBoost = coreMetric.singleLoadClock;
    }
    avgAllCoreFreq += coreMetric.allCoreClock;
    if (coreMetric.allCoreClock > maxAllCoreFreq) {
      maxAllCoreFreq = coreMetric.allCoreClock;
    }
  }

  avgAllCoreFreq = numCores > 0 ? avgAllCoreFreq / numCores : 0;

  // Calculate boost deltas
  int singleCoreDelta = maxSingleCoreBoost - baseClock;
  int allCoreDelta = maxAllCoreFreq - baseClock;

  // Create main container widget with background
  QWidget* container = new QWidget();
  container->setStyleSheet("background-color: #252525; border-radius: 4px;");
  QVBoxLayout* containerLayout = new QVBoxLayout(container);
  containerLayout->setSpacing(10);

  // Create a grid layout for the basic metrics display
  QWidget* basicWidget = new QWidget();
  QGridLayout* basicLayout = new QGridLayout(basicWidget);
  basicLayout->setSpacing(10);

  // Create a layout for the header section with CPU info and dropdown
  QWidget* headerWidget = new QWidget();
  QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 0, 0, 0);

  // Create compact CPU info section with horizontal layout
  QWidget* cpuInfoWidget = new QWidget();
  cpuInfoWidget->setStyleSheet("background-color: #252525; border: 1px solid "
                               "#444444; border-radius: 4px; padding: 8px;");
  QHBoxLayout* cpuInfoLayout = new QHBoxLayout(cpuInfoWidget);
  cpuInfoLayout->setContentsMargins(8, 8, 8, 8);
  cpuInfoLayout->setSpacing(20);

  // Create info item for cores
  QLabel* coresLabel = new QLabel(
    QString("<span style='font-weight: bold; color: "
            "#FFFFFF;'>%1</span><br><span style='color: #888888;'>Cores</span>")
      .arg(coreCount));
  coresLabel->setAlignment(Qt::AlignCenter);

  // Create info item for threads
  QLabel* threadsLabel = new QLabel(
    QString(
      "<span style='font-weight: bold; color: #FFFFFF;'>%1</span><br><span "
      "style='color: #888888;'>Threads</span>")
      .arg(threadCount));
  threadsLabel->setAlignment(Qt::AlignCenter);

  // Add this before line 668 (before adding components to the info layout)

  // Create info item for SMT/Hyperthreading
  QLabel* smtLabel = new QLabel(
    QString("<span style='font-weight: bold; color: "
            "#FFFFFF;'>%1</span><br><span style='color: #888888;'>SMT</span>")
      .arg(hyperThreading ? "Enabled" : "Disabled"));
  smtLabel->setAlignment(Qt::AlignCenter);

  // Create info item for cache
  QString cacheText =
    QString("<span style='font-weight: bold; color: #FFFFFF;'>L2: %1, L3: "
            "%2</span><br><span style='color: #888888;'>Cache</span>")
      .arg(l2Cache)
      .arg(l3Cache);
  QLabel* cacheLabel = new QLabel(cacheText);
  cacheLabel->setAlignment(Qt::AlignCenter);

  // Add all components to the info layout
  cpuInfoLayout->addWidget(coresLabel);
  cpuInfoLayout->addWidget(threadsLabel);
  cpuInfoLayout->addWidget(smtLabel);
  cpuInfoLayout->addWidget(cacheLabel);

  // Add CPU info to header
  headerLayout->addWidget(cpuInfoWidget);

  // Load comparison data (network-based if available, otherwise from files)
  std::map<QString, CPUComparisonData> comparisonData;
  if (networkMenuData && !networkMenuData->availableCpus.isEmpty()) {
    LOG_INFO << "CPUResultRenderer: Using network menu data for comparison dropdowns";
    comparisonData = createDropdownDataFromMenu(*networkMenuData);
  } else {
    LOG_INFO << "CPUResultRenderer: Falling back to local file comparison data";
    comparisonData = loadCPUComparisonData();
  }

  if (downloadClient) {
    CPUComparisonData general;
    general.model = DownloadApiClient::generalAverageLabel();
    comparisonData[DownloadApiClient::generalAverageLabel()] = general;
  }

  // Add to grid layout
  basicLayout->addWidget(headerWidget, 0, 0, 1, 3);

  // Create widgets for CPU tests and game simulation tests
  QWidget* cpuTestsBox = new QWidget();
  cpuTestsBox->setStyleSheet("background-color: #252525;");
  QVBoxLayout* cpuTestsLayout = new QVBoxLayout(cpuTestsBox);
  cpuTestsLayout->setContentsMargins(8, 12, 8, 12);
  cpuTestsLayout->setSpacing(6);

  QLabel* cpuTestsTitle = new QLabel("<b>CPU Tests</b>");
  cpuTestsTitle->setStyleSheet("color: #ffffff; font-size: 14px; background: "
                               "transparent; margin-bottom: 5px;");
  cpuTestsTitle->setContentsMargins(0, 0, 0, 0);
  cpuTestsLayout->addWidget(cpuTestsTitle);

  // Calculate max values for scaling
  double maxSingleCore = singleCoreTime;
  double maxFourThread = cpuData.fourThreadTime;
  double maxSimdScalar = simdScalar;
  double maxSimdAvx = simdAvx;
  double maxPrimeTime = primeTime;
  double maxGameSimSmall = gameSimSmall;
  double maxGameSimMedium = gameSimMedium;
  double maxGameSimLarge = gameSimLarge;
  double maxColdStartResponse = coldStartAvg;

  // Compare with all values in comparison data to find global maximums
  for (const auto& [_, cpuData] : comparisonData) {
    maxSingleCore = std::max(maxSingleCore, cpuData.singleCoreTime);

    // Only include four-thread data if available
    if (cpuData.fourThreadTime > 0) {
      maxFourThread = std::max(maxFourThread, cpuData.fourThreadTime);
    }

    maxSimdScalar = std::max(maxSimdScalar, cpuData.simdScalar);
    maxSimdAvx = std::max(maxSimdAvx, cpuData.simdAvx);
    maxPrimeTime = std::max(maxPrimeTime, cpuData.primeTime);
    maxGameSimSmall = std::max(maxGameSimSmall, cpuData.gameSimSmall);
    maxGameSimMedium = std::max(maxGameSimMedium, cpuData.gameSimMedium);
    maxGameSimLarge = std::max(maxGameSimLarge, cpuData.gameSimLarge);
    if (cpuData.coldStartAvg > 0) {
      maxColdStartResponse =
        std::max(maxColdStartResponse, cpuData.coldStartAvg);
    }
  }

  // Use global max values for consistent scaling
  double maxCoreTime = std::max(maxSingleCore, maxFourThread);
  double maxSimdTime = std::max(maxSimdScalar, maxSimdAvx);
  double maxUPS =
    std::max(maxGameSimSmall, std::max(maxGameSimMedium, maxGameSimLarge));

  // Store value pairs (current, comparison max) for updating later
  QPair<double, double> singleCoreVals(singleCoreTime, maxCoreTime);
  QPair<double, double> fourThreadVals(cpuData.fourThreadTime, maxCoreTime);
  QPair<double, double> simdScalarVals(simdScalar, maxSimdTime);
  QPair<double, double> simdAvxVals(simdAvx, maxSimdTime);
  QPair<double, double> primeTimeVals(primeTime, maxPrimeTime);
  QPair<double, double> gameSimSmallVals(gameSimSmall, maxUPS);
  QPair<double, double> gameSimMediumVals(gameSimMedium, maxUPS);
  QPair<double, double> gameSimLargeVals(gameSimLarge, maxUPS);
  double coldStartMaxScale = std::max(maxColdStartResponse, coldStartAvg);
  coldStartMaxScale = std::max(coldStartMaxScale, coldStartAvg * 1.5);
  coldStartMaxScale = std::max(coldStartMaxScale, 1000.0);
  QPair<double, double> coldStartVals(coldStartAvg, coldStartMaxScale);

  // Add comparison performance bars for CPU tests
  cpuTestsLayout->addWidget(createComparisonPerformanceBar(
    "Single-core", singleCoreTime, 0, maxCoreTime, "ms",
    "Measures single-thread CPU performance and boost behavior. Lower times usually mean snappier app responsiveness and better performance in lightly-threaded games and tools."));

  // Only add 4-thread test if we have valid data from user's system
  if (cpuData.fourThreadTime > 0) {
    // Create the 4-thread bar with the specific object name to match the
    // comparison handler
    QWidget* fourThreadBar =
      DiagnosticViewComponents::createComparisonPerformanceBar(
        "4-Thread", cpuData.fourThreadTime, 0, maxCoreTime, "ms",
        "A small multi-thread test that stresses scheduling and sustained boost across a few cores. Lower times generally indicate better performance in tasks that use several threads.",
        true);

    // Find the bar element inside the returned container and set its object name
    QWidget* innerBar = fourThreadBar->findChild<QWidget*>("comparison_bar");
    if (innerBar) {
      innerBar->setObjectName("comparison_bar_four_thread");
    }

    cpuTestsLayout->addWidget(fourThreadBar);
  }

  cpuTestsLayout->addSpacing(8);

  // Prime calculation test
  cpuTestsLayout->addWidget(createComparisonPerformanceBar(
    "Prime calculation", primeTime, 0, maxPrimeTime, "ms",
    "A math-heavy compute test. Lower times generally indicate stronger raw CPU throughput and can also reflect how well the CPU sustains clocks under load."));

  cpuTestsLayout->addSpacing(8);

  // SIMD tests
  cpuTestsLayout->addWidget(createComparisonPerformanceBar(
    "Scalar ops", simdScalar, 0, maxSimdTime, "μs",
    "A tight CPU instruction loop that highlights per-core execution efficiency. Lower times generally mean better low-level CPU performance."));
  cpuTestsLayout->addWidget(
    createComparisonPerformanceBar("AVX ops", simdAvx, 0, maxSimdTime, "μs",
                                   "Uses wide vector (AVX) instructions. Lower times generally mean stronger SIMD throughput, but some CPUs may downclock under AVX-heavy loads."));

  cpuTestsLayout->addSpacing(8);

  // Add Cold Start Response Test section if data is available
  if (coldStartAvg > 0) {
    const QString coldStartDescription =
      "Measures response time when data is not already cached (a \"cold\" workload). Lower is better; higher values can point to slower memory, suboptimal memory settings, or heavy background activity.";
    // Create the cold start bar with the specific object name to match the
    // comparison handler
    QWidget* coldStartBar =
      DiagnosticViewComponents::createComparisonPerformanceBar(
        "Cold Start Response", coldStartAvg, 0, coldStartVals.second, "μs",
        QString(), true);

    // Find the bar element inside the returned container and set its object name
    QWidget* innerBar = coldStartBar->findChild<QWidget*>("comparison_bar");
    if (innerBar) {
      innerBar->setObjectName("comparison_bar_cold_start");
    }

    cpuTestsLayout->addWidget(coldStartBar);

    // Create a detail widget for cold start test metrics
    QWidget* coldStartDetailWidget = new QWidget();
    QGridLayout* coldStartGrid = new QGridLayout(coldStartDetailWidget);
    coldStartGrid->setContentsMargins(4, 4, 4, 4);
    coldStartGrid->setSpacing(4);

    // Add min, max, and standard deviation metrics
    QLabel* minLabel = new QLabel("Min Response:");
    minLabel->setStyleSheet("color: #888888;");
    QLabel* minValueLabel =
      new QLabel(QString("%1 μs").arg(coldStartMin, 0, 'f', 1));
    minValueLabel->setStyleSheet("color: #FFFFFF; font-weight: bold;");
    minValueLabel->setObjectName("cold_start_min_value");
    minValueLabel->setProperty("userValue", coldStartMin);
    minValueLabel->setProperty("unit", minValueLabel->text().split(' ').last());
    minValueLabel->setTextFormat(Qt::RichText);
    minValueLabel->setWordWrap(true);
    coldStartGrid->addWidget(minLabel, 0, 0);
    coldStartGrid->addWidget(minValueLabel, 0, 1);

    QLabel* maxLabel = new QLabel("Max Response:");
    maxLabel->setStyleSheet("color: #888888;");
    QLabel* maxValueLabel =
      new QLabel(QString("%1 μs").arg(coldStartMax, 0, 'f', 1));
    maxValueLabel->setStyleSheet("color: #FFFFFF; font-weight: bold;");
    maxValueLabel->setObjectName("cold_start_max_value");
    maxValueLabel->setProperty("userValue", coldStartMax);
    maxValueLabel->setProperty("unit", maxValueLabel->text().split(' ').last());
    maxValueLabel->setTextFormat(Qt::RichText);
    maxValueLabel->setWordWrap(true);
    coldStartGrid->addWidget(maxLabel, 0, 2);
    coldStartGrid->addWidget(maxValueLabel, 0, 3);

    QLabel* stdDevLabel = new QLabel("Std Deviation:");
    stdDevLabel->setStyleSheet("color: #888888;");
    QLabel* stdDevValueLabel =
      new QLabel(QString("%1 μs").arg(coldStartStdDev, 0, 'f', 1));
    stdDevValueLabel->setStyleSheet("color: #FFFFFF; font-weight: bold;");
    stdDevValueLabel->setObjectName("cold_start_std_value");
    stdDevValueLabel->setProperty("userValue", coldStartStdDev);
    stdDevValueLabel->setProperty("unit", stdDevValueLabel->text().split(' ').last());
    stdDevValueLabel->setTextFormat(Qt::RichText);
    stdDevValueLabel->setWordWrap(true);
    coldStartGrid->addWidget(stdDevLabel, 1, 0);
    coldStartGrid->addWidget(stdDevValueLabel, 1, 1);

    // Calculate jitter (max - min)
    double jitter = coldStartMax - coldStartMin;
    QLabel* jitterLabel = new QLabel("Jitter:");
    jitterLabel->setStyleSheet("color: #888888;");
    QLabel* jitterValueLabel =
      new QLabel(QString("%1 μs").arg(jitter, 0, 'f', 1));
    jitterValueLabel->setStyleSheet("color: #FFFFFF; font-weight: bold;");
    jitterValueLabel->setObjectName("cold_start_jitter_value");
    jitterValueLabel->setProperty("userValue", jitter);
    jitterValueLabel->setProperty("unit", jitterValueLabel->text().split(' ').last());
    jitterValueLabel->setTextFormat(Qt::RichText);
    jitterValueLabel->setWordWrap(true);
    coldStartGrid->addWidget(jitterLabel, 1, 2);
    coldStartGrid->addWidget(jitterValueLabel, 1, 3);

    // Add the detailed widget to the main layout
    cpuTestsLayout->addWidget(coldStartDetailWidget);

    // Cold start includes a detail table; render the description after it.
    QLabel* coldStartDescriptionLabel = new QLabel(coldStartDescription);
    coldStartDescriptionLabel->setObjectName("description_label");
    coldStartDescriptionLabel->setWordWrap(true);
    coldStartDescriptionLabel->setAlignment(Qt::AlignCenter);
    coldStartDescriptionLabel->setTextFormat(Qt::RichText);
    coldStartDescriptionLabel->setStyleSheet(
      "color: #AAAAAA; font-size: 11px; background: transparent; margin-top: 1px;");
    cpuTestsLayout->addWidget(coldStartDescriptionLabel);
  }

  // Game Simulation section
  QWidget* gameSimBox = new QWidget();
  gameSimBox->setStyleSheet("background-color: #252525;");  // Remove padding
  QVBoxLayout* gameSimLayout = new QVBoxLayout(gameSimBox);
  gameSimLayout->setContentsMargins(8, 12, 8, 12);
  gameSimLayout->setSpacing(6);

  QLabel* gameSimTitle = new QLabel("<b>Game Simulation</b>");
  gameSimTitle->setStyleSheet("color: #ffffff; font-size: 14px; background: "
                              "transparent; margin-bottom: 5px;");
  gameSimTitle->setContentsMargins(0, 0, 0, 0);
  gameSimLayout->addWidget(gameSimTitle);

  // Add comparison performance bars for game simulation
  gameSimLayout->addWidget(createComparisonPerformanceBar(
    "Small (L3)", gameSimSmall / 1000000, 0, maxUPS / 1000000, "M ups",
    "A game-like CPU + memory workload intended to better predict real game performance than pure CPU micro-benchmarks. <b>Small</b> uses a small working set, so cache (L3) handles most of the data.",
    false));
  gameSimLayout->addWidget(createComparisonPerformanceBar(
    "Medium", gameSimMedium / 1000000, 0, maxUPS / 1000000, "M ups",
    "A game-like CPU + memory workload intended to better predict real game performance than pure CPU micro-benchmarks. <b>Medium</b> has a moderate working set, split between cache and RAM.",
    false));
  gameSimLayout->addWidget(
    createComparisonPerformanceBar("Large (RAM)", gameSimLarge / 1000000, 0,
                                   maxUPS / 1000000, "M ups",
                                   "A game-like CPU + memory workload intended to better predict real game performance than pure CPU micro-benchmarks. <b>Large</b> is memory intensive, with much more traffic to RAM.",
                                   false));

  // Add test boxes to the grid layout
  basicLayout->addWidget(cpuTestsBox, 1, 0, 1,
                         3);  // Make CPU tests span all 3 columns
  basicLayout->addWidget(gameSimBox, 2, 0, 1,
                         3);  // Put game sim below, also spanning all 3 columns
  basicLayout->setColumnStretch(0, 1);

  // Create dropdown for CPU comparison
  QComboBox* dropdown = createCPUComparisonDropdown(
    comparisonData, container, cpuTestsBox, gameSimBox, singleCoreVals,
    fourThreadVals, simdScalarVals, simdAvxVals, primeTimeVals,
    gameSimSmallVals, gameSimMediumVals, gameSimLargeVals, coldStartVals,
    downloadClient);
  dropdown->setObjectName("cpu_comparison_dropdown");

  if (downloadClient) {
    const int idx = dropdown->findText(DownloadApiClient::generalAverageLabel());
    if (idx > 0) {
      dropdown->setCurrentIndex(idx);
    }
  }

  // Add dropdown to header layout, aligned to the right
  headerLayout->addStretch(1);
  headerLayout->addWidget(dropdown);

  // Add the basic widget to the container
  containerLayout->addWidget(basicWidget);

  // Only add boost metrics if the boost test was actually run (boostMetrics has
  // data)
  if (!boostMetrics.empty() && (maxSingleCoreBoost > 0 || maxAllCoreFreq > 0)) {
    // Add title for the boost section
    QLabel* boostTitle = new QLabel("<b>CPU Boost Tests:</b>");
    boostTitle->setStyleSheet(
      "color: #ffffff; font-size: 14px; margin-top: 10px;");
    containerLayout->addWidget(boostTitle);

    // Create a widget to hold the boost comparison table
    QWidget* boostWidget = new QWidget();
    QVBoxLayout* boostLayout = new QVBoxLayout(boostWidget);
    boostLayout->setContentsMargins(8, 8, 8, 8);
    boostLayout->setSpacing(6);

    // Create the table-like layout for comparing boost clocks
    QWidget* tableWidget = new QWidget();
    QGridLayout* tableLayout = new QGridLayout(tableWidget);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(8);

    // Create header row
    QLabel* cpuHeader = new QLabel("CPU");
    cpuHeader->setStyleSheet(
      "color: #ffffff; font-weight: bold; background: transparent;");
    tableLayout->addWidget(cpuHeader, 0, 0);

    QLabel* baseHeader = new QLabel("Base Clock");
    baseHeader->setStyleSheet(
      "color: #ffffff; font-weight: bold; background: transparent;");
    tableLayout->addWidget(baseHeader, 0, 1);

    QLabel* singleHeader = new QLabel("Single-Core Boost");
    singleHeader->setStyleSheet(
      "color: #ffffff; font-weight: bold; background: transparent;");
    tableLayout->addWidget(singleHeader, 0, 2);

    QLabel* allCoreHeader = new QLabel("All-Core Boost");
    allCoreHeader->setStyleSheet(
      "color: #ffffff; font-weight: bold; background: transparent;");
    tableLayout->addWidget(allCoreHeader, 0, 3);

    // Add divider line
    QFrame* divider = new QFrame();
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    divider->setLineWidth(1);
    divider->setStyleSheet("background-color: #444444;");
    tableLayout->addWidget(divider, 1, 0, 1, 4);

    // User data row - based on boostMetrics
    QString userCpuName =
      QString::fromStdString(SystemMetrics::GetConstantSystemInfo().cpuName);
    QLabel* userCpuLabel = new QLabel(userCpuName);
    userCpuLabel->setStyleSheet("color: #ffffff; background: transparent;");
    tableLayout->addWidget(userCpuLabel, 2, 0);

    QLabel* userBaseLabel = new QLabel(QString("%1 MHz").arg(baseClock));
    userBaseLabel->setStyleSheet("color: #0078d4; background: transparent;");
    tableLayout->addWidget(userBaseLabel, 2, 1);

    // Calculate boost percentages
    double singleBoostPct =
      baseClock > 0 ? (100.0 * singleCoreDelta / baseClock) : 0;
    double allCoreBoostPct =
      baseClock > 0 ? (100.0 * allCoreDelta / baseClock) : 0;

    QLabel* userSingleLabel =
      new QLabel(QString("%1 MHz <span style='color: #FFAA00;'>(+%2%)</span>")
                   .arg(maxSingleCoreBoost)
                   .arg(singleBoostPct, 0, 'f', 1));
    userSingleLabel->setTextFormat(Qt::RichText);
    userSingleLabel->setStyleSheet("color: #0078d4; background: transparent;");
    tableLayout->addWidget(userSingleLabel, 2, 2);

    QLabel* userAllCoreLabel =
      new QLabel(QString("%1 MHz <span style='color: #FFAA00;'>(+%2%)</span>")
                   .arg(maxAllCoreFreq)
                   .arg(allCoreBoostPct, 0, 'f', 1));
    userAllCoreLabel->setTextFormat(Qt::RichText);
    userAllCoreLabel->setStyleSheet("color: #0078d4; background: transparent;");
    tableLayout->addWidget(userAllCoreLabel, 2, 3);

    // Add placeholders for comparison CPU that will be populated later
    QLabel* compCpuLabel = new QLabel("Select CPU to compare");
    compCpuLabel->setObjectName("comp_cpu_name");
    compCpuLabel->setStyleSheet(
      "color: #888888; font-style: italic; background: transparent;");
    tableLayout->addWidget(compCpuLabel, 3, 0);

    QLabel* compBaseLabel = new QLabel("-");
    compBaseLabel->setObjectName("comp_base_clock");
    compBaseLabel->setStyleSheet(
      "color: #888888; font-style: italic; background: transparent;");
    tableLayout->addWidget(compBaseLabel, 3, 1);

    QLabel* compSingleLabel = new QLabel("-");
    compSingleLabel->setObjectName("comp_boost_clock");
    compSingleLabel->setStyleSheet(
      "color: #888888; font-style: italic; background: transparent;");
    tableLayout->addWidget(compSingleLabel, 3, 2);

    QLabel* compAllCoreLabel = new QLabel("-");
    compAllCoreLabel->setObjectName("comp_all_core_clock");
    compAllCoreLabel->setStyleSheet(
      "color: #888888; font-style: italic; background: transparent;");
    tableLayout->addWidget(compAllCoreLabel, 3, 3);

    boostLayout->addWidget(tableWidget);

    // Add the boost widget to the container
    containerLayout->addWidget(boostWidget);
  }

  // Add CPU throttling test results section if available
  if (cpuData.peakClock > 0) {
    // Add title for the throttling section
    QLabel* throttlingTitle = new QLabel("<b>CPU Throttling Test:</b>");
    throttlingTitle->setStyleSheet(
      "color: #ffffff; font-size: 14px; margin-top: 10px;");
    containerLayout->addWidget(throttlingTitle);

    // Create a widget to hold the throttling test results
    QWidget* throttlingWidget = new QWidget();
    throttlingWidget->setStyleSheet(
      "background-color: #2a2a2a; border-radius: 4px;");
    QVBoxLayout* throttlingLayout = new QVBoxLayout(throttlingWidget);
    throttlingLayout->setContentsMargins(12, 12, 12, 12);
    throttlingLayout->setSpacing(8);

    // Create message based on throttling detection
    QString message;
    QString color;

    if (cpuData.throttlingDetected) {
      double dropPercent = cpuData.clockDropPercent;
      if (dropPercent > 20.0) {
        message = "SIGNIFICANT THROTTLING: Your CPU is experiencing "
                  "substantial frequency reduction "
                  "under load, which may impact performance in sustained "
                  "workloads like gaming.";
        color = "#FF6666";  // Brighter red for significant issues
      } else if (dropPercent > 10.0) {
        message = "MODERATE THROTTLING: Your CPU shows normal thermal/power "
                  "throttling behavior, "
                  "typical for most modern CPUs under sustained load.";
        color = "#FFAA00";  // Orange/amber for moderate throttling
      } else {
        message = "MINOR THROTTLING: Your CPU maintains most of its "
                  "performance under sustained load.";
        color = "#FFDD77";  // Light amber for minor throttling
      }
    } else {
      message = "NO SIGNIFICANT THROTTLING DETECTED: Your CPU maintains "
                "excellent frequency stability under load.";
      color = "#44FF44";  // Green for good results
    }

    // Add the message
    QLabel* throttlingMessage = new QLabel(message);
    throttlingMessage->setWordWrap(true);
    throttlingMessage->setStyleSheet(
      QString("color: %1; font-weight: bold;").arg(color));
    throttlingLayout->addWidget(throttlingMessage);

    // Add details about the peak and sustained clocks
    if (cpuData.peakClock > 0 && cpuData.sustainedClock > 0) {
      QWidget* detailsWidget = new QWidget();
      QGridLayout* detailsLayout = new QGridLayout(detailsWidget);
      detailsLayout->setContentsMargins(0, 8, 0, 0);
      detailsLayout->setSpacing(8);

      QLabel* peakClockLabel = new QLabel("Peak Clock:");
      peakClockLabel->setStyleSheet("color: #dddddd;");
      QLabel* peakClockValue =
        new QLabel(QString("%1 MHz").arg(cpuData.peakClock, 0, 'f', 0));
      peakClockValue->setStyleSheet("color: #ffffff; font-weight: bold;");
      detailsLayout->addWidget(peakClockLabel, 0, 0);
      detailsLayout->addWidget(peakClockValue, 0, 1);

      QLabel* sustainedClockLabel = new QLabel("Sustained Clock:");
      sustainedClockLabel->setStyleSheet("color: #dddddd;");
      QLabel* sustainedClockValue =
        new QLabel(QString("%1 MHz").arg(cpuData.sustainedClock, 0, 'f', 0));
      sustainedClockValue->setStyleSheet("color: #ffffff; font-weight: bold;");
      detailsLayout->addWidget(sustainedClockLabel, 0, 2);
      detailsLayout->addWidget(sustainedClockValue, 0, 3);

      if (cpuData.throttlingDetected) {
        QLabel* dropLabel = new QLabel("Frequency Drop:");
        dropLabel->setStyleSheet("color: #dddddd;");
        QLabel* dropValue =
          new QLabel(QString("%1%").arg(cpuData.clockDropPercent, 0, 'f', 1));
        dropValue->setStyleSheet(
          QString("color: %1; font-weight: bold;").arg(color));
        detailsLayout->addWidget(dropLabel, 1, 0);
        detailsLayout->addWidget(dropValue, 1, 1);

        if (cpuData.throttlingDetectedTime > 0) {
          QLabel* timeLabel = new QLabel("Detected After:");
          timeLabel->setStyleSheet("color: #dddddd;");
          QLabel* timeValue = new QLabel(
            QString("%1 seconds").arg(cpuData.throttlingDetectedTime));
          timeValue->setStyleSheet("color: #ffffff; font-weight: bold;");
          detailsLayout->addWidget(timeLabel, 1, 2);
          detailsLayout->addWidget(timeValue, 1, 3);
        }
      }

      throttlingLayout->addWidget(detailsWidget);
    }

    // Add a note about throttling
    QLabel* noteLabel = new QLabel(
      "Note: Most modern CPUs will reduce their clock speed under sustained "
      "load to stay within "
      "thermal and power limits. Limited throttling is normal and by design.");
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet(
      "color: #bbbbbb; font-style: italic; margin-top: 8px;");
    throttlingLayout->addWidget(noteLabel);

    // Add the throttling widget to the main container
    containerLayout->addWidget(throttlingWidget);
  }

  return container;
}

QWidget* CPUResultRenderer::createCacheResultWidget(
  const QString& result,
  const std::map<QString, CPUComparisonData>& comparisonData,
  const MenuData* networkMenuData, DownloadApiClient* downloadClient) {
  
  LOG_INFO << "CPUResultRenderer: Creating cache result widget with network support";
  
  // Use network data if available, otherwise fall back to local file data
  std::map<QString, CPUComparisonData> finalComparisonData;
  if (networkMenuData && !networkMenuData->availableCpus.isEmpty()) {
    LOG_INFO << "CPUResultRenderer: Using network menu data for cache comparison";
    finalComparisonData = createDropdownDataFromMenu(*networkMenuData);
  } else {
    LOG_INFO << "CPUResultRenderer: Using local file data for cache comparison";
    finalComparisonData = comparisonData;
  }
  // Get data directly from DiagnosticDataStore
  auto& dataStore = DiagnosticDataStore::getInstance();
  const auto& cpuData = dataStore.getCPUData();

  // Get cache sizes
  int l1CacheKB = cpuData.cache.l1SizeKB;
  int l2CacheKB = cpuData.cache.l2SizeKB;
  int l3CacheKB = cpuData.cache.l3SizeKB;

  // Fallback to getting cache sizes from SystemInfoProvider if needed
  // No fallback to SystemInfoProvider; use only ConstantSystemInfo

  // Get latencies for different buffer sizes from the store
  QMap<int, double> cacheLatencies;
  const int sizes[] = {
    32,   64,   128,  256,   512,   1024,
    2048, 4096, 8192, 16384, 32768, 65536  // Add 65536 (64MB) here
  };

  // Fill the cache latencies from data store
  for (int i = 0; i < 12; i++) {  // Update the loop bound to 12
    int sizeKB = sizes[i];
    double latencyNs = 0.0;

    // Try to get latency from raw measurements first
    if (cpuData.cache.rawLatencies.count(sizeKB) > 0) {
      latencyNs = cpuData.cache.rawLatencies.at(sizeKB);
      LOG_INFO << "Found latency for " << sizeKB
               << " KB in raw measurements: " << latencyNs << " ns";
    } else {
      // Fall back to the array if no raw measurement exists
      if (i < 12) {  // Make sure we don't go out of bounds
        latencyNs = cpuData.cache.latencies[i];
        LOG_INFO << "Using array latency for " << sizeKB << " KB: " << latencyNs
                 << " ns";
      }
    }

    if (latencyNs > 0) {
      cacheLatencies[sizeKB] = latencyNs;
      LOG_INFO << "Added to cacheLatencies map: " << sizeKB
               << " KB = " << latencyNs << " ns";
    } else {
      LOG_WARN << "No valid latency found for " << sizeKB << " KB";
    }
  }

  // Create a container for all cache content with background
  QWidget* containerWidget = new QWidget();
  containerWidget->setStyleSheet(
    "background-color: #252525; border-radius: 4px; padding: 4px;");
  QVBoxLayout* mainLayout = new QVBoxLayout(containerWidget);
  mainLayout->setContentsMargins(12, 4, 12,
                                 4);  // Set consistent 12px left/right margins

  // Create a title and dropdown section with horizontal layout
  QWidget* headerWidget = new QWidget();
  QHBoxLayout* headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(0, 0, 0, 0);

  // Create a title for the cache section
  QLabel* cacheTitle =
    new QLabel("<b>Estimated Cache and Memory Latencies</b>");
  cacheTitle->setStyleSheet(
    "color: #ffffff; font-size: 14px; margin-top: 2px;");
  cacheTitle->setContentsMargins(0, 0, 0, 0);
  headerLayout->addWidget(cacheTitle);

  // Add dropdown for CPU comparison
  headerLayout->addStretch(1);

  // Calculate the global max latency for consistent scaling BEFORE creating the
  // dropdown
  const std::vector<int> selectedSizes = {32,   64,   128,   256,   512,  1024,
                                          4096, 8192, 16384, 32768, 65536};
  const int numSizes = static_cast<int>(selectedSizes.size());
  double userMaxLatency = 0;

  // Find max latency in user's data
  for (int i = 0; i < numSizes; i++) {
    int sizeKB = selectedSizes[i];
    if (cacheLatencies.contains(sizeKB)) {
      userMaxLatency = std::max(userMaxLatency, cacheLatencies[sizeKB]);
    }
  }

  // Use user max for initial scale; comparisons rescale on selection change.
  double finalScalingFactor = userMaxLatency;

  // Generate aggregated data from individual results
  auto aggregatedData = generateAggregatedCPUData(finalComparisonData);

  auto updateCacheUserBarLayout = [](QWidget* parentContainer,
                                     int percentage) {
    QWidget* userBarContainer =
      parentContainer->findChild<QWidget*>("userBarContainer");
    if (!userBarContainer) {
      return;
    }

    QHBoxLayout* userBarLayout =
      userBarContainer->findChild<QHBoxLayout*>("user_bar_layout");
    if (!userBarLayout) {
      return;
    }

    QWidget* userBar = userBarContainer->findChild<QWidget*>("user_bar_fill");
    QWidget* userSpacer =
      userBarContainer->findChild<QWidget*>("user_bar_spacer");
    if (!userBar || !userSpacer) {
      return;
    }

    const int barIndex = userBarLayout->indexOf(userBar);
    const int spacerIndex = userBarLayout->indexOf(userSpacer);
    if (barIndex >= 0) {
      userBarLayout->setStretch(barIndex, percentage);
    }
    if (spacerIndex >= 0) {
      userBarLayout->setStretch(spacerIndex, 100 - percentage);
    }
  };

  auto updateCacheBars =
    [containerWidget, cacheLatencies, selectedSizes, numSizes,
     updateCacheUserBarLayout](const CPUComparisonData* compData,
                               const QString& displayName,
                               bool hasSelection) {
      double maxLatency = 0.0;
      for (int i = 0; i < numSizes; i++) {
        const int sizeKB = selectedSizes[i];
        if (cacheLatencies.contains(sizeKB)) {
          maxLatency = std::max(maxLatency, cacheLatencies[sizeKB]);
        }
        if (compData && compData->cacheLatencies.contains(sizeKB)) {
          maxLatency =
            std::max(maxLatency, compData->cacheLatencies[sizeKB]);
        }
      }

      const double scaledMax = maxLatency > 0 ? maxLatency * 1.25 : 0.0;

      QList<QWidget*> allBars = containerWidget->findChildren<QWidget*>(
        QRegularExpression("^comparison_bar_cache.*"));
      for (QWidget* bar : allBars) {
        QWidget* parentContainer = bar->parentWidget();
        if (!parentContainer) {
          continue;
        }

        QLabel* nameLabel =
          parentContainer->findChild<QLabel*>("comp_name_label");
        if (nameLabel) {
          if (hasSelection) {
            nameLabel->setText(displayName);
            nameLabel->setStyleSheet(
              "color: #ffffff; background: transparent;");
          } else {
            nameLabel->setText("Select CPU to compare");
            nameLabel->setStyleSheet(
              "color: #888888; font-style: italic; background: transparent;");
          }
        }

        QString objName = bar->objectName();
        QRegularExpression sizeRegex("cache_(\\d+_[km]b)");
        QRegularExpressionMatch sizeMatch = sizeRegex.match(objName);

        if (!sizeMatch.hasMatch()) {
          continue;
        }

        QString sizeStr = sizeMatch.captured(1);
        int sizeKB = 0;
        if (sizeStr.endsWith("_kb")) {
          sizeKB = sizeStr.split("_").first().toInt();
        } else if (sizeStr.endsWith("_mb")) {
          sizeKB = sizeStr.split("_").first().toInt() * 1024;
        }

        const double userLatency =
          cacheLatencies.contains(sizeKB) ? cacheLatencies[sizeKB] : 0.0;
        const double compLatency =
          (compData && compData->cacheLatencies.contains(sizeKB))
            ? compData->cacheLatencies[sizeKB]
            : 0.0;

        const int userPercentage =
          (userLatency > 0 && scaledMax > 0)
            ? static_cast<int>(
                std::min(100.0, (userLatency / scaledMax) * 100.0))
            : 0;
        updateCacheUserBarLayout(parentContainer, userPercentage);

        QLabel* valueLabel = parentContainer->findChild<QLabel*>("value_label");
        QLayout* layout = bar->layout();
        if (layout) {
          QLayoutItem* child;
          while ((child = layout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
          }

          if (!hasSelection || compLatency <= 0) {
            QWidget* emptyBar = new QWidget();
            emptyBar->setStyleSheet("background-color: transparent;");
            QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
            if (newLayout) {
              newLayout->addWidget(emptyBar);
            }
          } else {
            const int compPercentage =
              (scaledMax > 0)
                ? static_cast<int>(std::min(
                    100.0, (compLatency / scaledMax) * 100.0))
                : 0;

            QWidget* barWidget = new QWidget();
            barWidget->setFixedHeight(16);
            barWidget->setStyleSheet(
              "background-color: #FF4444; border-radius: 2px;");

            QWidget* spacer = new QWidget();
            spacer->setStyleSheet("background-color: transparent;");

            QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
            if (newLayout) {
              newLayout->addWidget(barWidget, compPercentage);
              newLayout->addWidget(spacer, 100 - compPercentage);
            }
          }
        }

        if (valueLabel) {
          if (!hasSelection || compLatency <= 0) {
            valueLabel->setText("-");
            valueLabel->setStyleSheet(
              "color: #888888; font-style: italic; background: transparent;");
          } else {
            valueLabel->setText(QString("%1 ns").arg(compLatency, 0, 'f', 2));
            valueLabel->setStyleSheet(
              "color: #FF4444; background: transparent;");
          }
        }
      }
    };

  // Create a callback function to handle selection changes
  auto selectionCallback = [containerWidget, cacheLatencies,
                            downloadClient, updateCacheBars](
                             const QString& componentName,
                             const QString& originalFullName,
                             DiagnosticViewComponents::AggregationType type,
                             const CPUComparisonData& cpuData) {
    
    LOG_INFO << "CPUResultRenderer (Cache): Cache comparison selection changed to: " 
             << componentName.toStdString() << " (type: " 
             << (type == DiagnosticViewComponents::AggregationType::Best ? "Best" : "Average") << ")";
    
    // Log the received CPU data for debugging
    LOG_INFO << "CPUResultRenderer (Cache): Received CPU data - singleCoreTime: " << cpuData.singleCoreTime 
             << ", fourThreadTime: " << cpuData.fourThreadTime 
             << ", cache latencies count: " << cpuData.cacheLatencies.size();
    
  // If downloadClient is available and either perf data or cache latencies are missing,
  // fetch the actual data from the server
  const bool needsPerf = (cpuData.singleCoreTime <= 0);
  const bool needsCache = (cpuData.cacheLatencies.isEmpty());
  const bool canFetch = (downloadClient != nullptr) && !componentName.trimmed().isEmpty();
  LOG_INFO << "CPUResultRenderer (Cache): fetch gating - canFetch=" << canFetch
       << ", needsPerf=" << needsPerf << ", needsCache=" << needsCache;
  if (canFetch && (needsPerf || needsCache)) {
      LOG_INFO << "CPUResultRenderer (Cache): Fetching network data for CPU: " << componentName.toStdString() << " using original name: " << originalFullName.toStdString();
      
      downloadClient->fetchComponentData("cpu", originalFullName, 
        [containerWidget, cacheLatencies, componentName, type, updateCacheBars]
        (bool success, const ComponentData& networkData, const QString& error) {
          
          if (success) {
            LOG_INFO << "CPUResultRenderer (Cache): Successfully fetched CPU data for " << componentName.toStdString();
            
            // Log the raw network data received
            QJsonDocument doc(networkData.testData);
            LOG_INFO << "CPUResultRenderer (Cache): Raw network data received: " << doc.toJson(QJsonDocument::Compact).toStdString();
            
            // Convert network data to CPUComparisonData
            CPUComparisonData fetchedCpuData = convertNetworkDataToCPU(networkData);
            
            // Log the parsed cache latencies for debugging
            LOG_INFO << "CPUResultRenderer (Cache): Parsed cache latencies count: " << fetchedCpuData.cacheLatencies.size();
            for (const auto& entry : fetchedCpuData.cacheLatencies.toStdMap()) {
              LOG_INFO << "CPUResultRenderer (Cache): Cache latency " << entry.first << " KB = " << entry.second << " ns";
            }
            
            // Create display name
            QString displayName = (componentName == DownloadApiClient::generalAverageLabel())
              ? componentName
              : componentName + " (" +
                  (type == DiagnosticViewComponents::AggregationType::Best ? "Best)" : "Avg)");
            
            LOG_INFO << "CPUResultRenderer (Cache): Updating cache comparison bars with fetched data";

            updateCacheBars(&fetchedCpuData, displayName, true);

          } else {
            LOG_ERROR << "CPUResultRenderer (Cache): Failed to fetch CPU data for " << componentName.toStdString() 
                      << ": " << error.toStdString();
            // Continue with empty/placeholder data
          }
        });
      
      return; // Exit early - the network callback will handle the UI update
    }
    
    const bool hasSelection = !componentName.isEmpty();
    const QString displayName =
      hasSelection
        ? (componentName == DownloadApiClient::generalAverageLabel()
             ? componentName
             : componentName + " (" +
                 (type == DiagnosticViewComponents::AggregationType::Best
                    ? "Best)"
                    : "Avg)"))
        : QString("Select CPU to compare");

    updateCacheBars(hasSelection ? &cpuData : nullptr, displayName, hasSelection);
  };

  // Use the template function to create the dropdown with aggregated data
  QComboBox* dropdown =
    DiagnosticViewComponents::createAggregatedComparisonDropdown<
      CPUComparisonData>(aggregatedData, selectionCallback);
  dropdown->setObjectName("cpu_cache_comparison_dropdown");
  if (downloadClient) {
    const int idx = dropdown->findText(DownloadApiClient::generalAverageLabel());
    if (idx > 0) {
      dropdown->setCurrentIndex(idx);
    }
  }

  headerLayout->addWidget(dropdown);

  // Add the header to the main layout
  mainLayout->addWidget(headerWidget);

  // Create grid layout for latency metrics display
  QWidget* latencyWidget = new QWidget();
  QGridLayout* latencyLayout = new QGridLayout(latencyWidget);
  latencyLayout->setContentsMargins(0, 0, 0, 0);  // No additional margins
  latencyLayout->setSpacing(6);  // Reduced spacing between cache boxes

  // Create latency metric boxes with color-coding based on performance
  QWidget* l1Box =
    createLatencyBox("L1 Cache", cpuData.cache.l1LatencyNs, "#44FF44");
  QWidget* l2Box =
    createLatencyBox("L2 Cache", cpuData.cache.l2LatencyNs, "#88FF88");
  QWidget* l3Box =
    createLatencyBox("L3 Cache", cpuData.cache.l3LatencyNs, "#FFAA00");
  QWidget* memBox =
    createLatencyBox("Memory", cpuData.cache.ramLatencyNs, "#FF6666");

  // Add boxes to grid layout
  latencyLayout->addWidget(l1Box, 0, 0);
  latencyLayout->addWidget(l2Box, 0, 1);
  latencyLayout->addWidget(l3Box, 0, 2);
  latencyLayout->addWidget(memBox, 0, 3);

  // Create a visual representation of cache latency (simple bar chart)
  QWidget* chartWidget = new QWidget();
  chartWidget->setStyleSheet(
    "background-color: #252525;");  // Remove padding entirely

  QVBoxLayout* chartLayout = new QVBoxLayout(chartWidget);
  chartLayout->setContentsMargins(0, 0, 0, 0);  // No additional margins

  QLabel* chartTitle = new QLabel("<b>Results by Buffer Size</b>");
  chartTitle->setStyleSheet("color: #ffffff; font-size: 14px; background: "
                            "transparent; margin-bottom: 5px;");
  chartTitle->setContentsMargins(0, 0, 0, 0);  // No internal margins
  chartLayout->addWidget(chartTitle);

  const QString sizeLabels[] = {"32 KB",  "64 KB", "128 KB", "256 KB",
                                "512 KB", "1 MB",  "4 MB",   "8 MB",
                                "16 MB",  "32 MB", "64 MB"};

  // Reset the static variable in createLatencyBar to use our global maximum
  createLatencyBar("__reset__", finalScalingFactor, "__reset__");

  // Create the bars using the same finalScalingFactor for consistent scaling
  for (int i = 0; i < numSizes; i++) {
    int sizeKB = selectedSizes[i];
    if (cacheLatencies.contains(sizeKB)) {
      double latency = cacheLatencies[sizeKB];
      LOG_INFO << "Creating bar for " << sizeKB << " KB with latency " << latency
               << " ns";

      // Set an object name that follows the pattern expected by the dropdown
      // handler
      QString objName;
      if (sizeKB >= 1024) {
        objName = QString("comparison_bar_cache_%1_mb").arg(sizeKB / 1024);
      } else {
        objName = QString("comparison_bar_cache_%1_kb").arg(sizeKB);
      }
      QWidget* bar = DiagnosticViewComponents::createComparisonPerformanceBar(
        sizeLabels[i], latency, 0, finalScalingFactor, "ns", true);

      // Find the bar element inside the returned container and set its object
      // name
      QWidget* innerBar = bar->findChild<QWidget*>("comparison_bar");
      if (innerBar) {
        innerBar->setObjectName(objName);
      }

      chartLayout->addWidget(bar);
    } else {
      LOG_WARN << "No latency data found for " << sizeKB << " KB, skipping bar";
    }
  }

  // Add explanation text
  QLabel* infoLabel =
    new QLabel("Cache latency measures how quickly your CPU can access data "
               "from different levels of cache and memory. "
               "Lower latency means faster data access and better performance "
               "in applications.");
  infoLabel->setWordWrap(true);
  infoLabel->setStyleSheet(
    "color: #dddddd; font-style: italic; margin-top: 8px;");
  chartLayout->addWidget(infoLabel);

  // Add the chart to the latency layout
  latencyLayout->addWidget(chartWidget, 1, 0, 1, 4);

  // Add the latency widget to the main layout
  mainLayout->addWidget(latencyWidget);

  return containerWidget;
}

QWidget* CPUResultRenderer::createLatencyBox(const QString& title,
                                             double latency,
                                             const QString& color) {
  QWidget* box = new QWidget();
  box->setStyleSheet("background-color: #252525; border-radius: 4px;");

  QVBoxLayout* layout = new QVBoxLayout(box);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(4);

  // Use consistent style with value on top, label below
  QLabel* valueLabel =
    new QLabel(QString("<span style='font-weight: bold; color: %1;'>%2 "
                       "ns</span><br><span style='color: #888888;'>%3</span>")
                 .arg(color)
                 .arg(latency, 0, 'f', 1)
                 .arg(title),
               box);
  valueLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(valueLabel);

  return box;
}

QWidget* CPUResultRenderer::createLatencyBar(const QString& label, double value,
                                             const QString& color) {
  // Handle the reset special case
  static double globalMaxValue = 0.0;
  if (label == "__reset__") {
    globalMaxValue = value;  // Set the global max to the provided value
    return nullptr;
  }

  QWidget* container = new QWidget();
  QVBoxLayout* mainLayout = new QVBoxLayout(container);
  mainLayout->setContentsMargins(0, 1, 0, 1);  // Minimal vertical margins
  mainLayout->setSpacing(1);                   // Minimal spacing

  QHBoxLayout* layout = new QHBoxLayout();
  layout->setContentsMargins(0, 0, 0, 0);  // No internal margins
  layout->setSpacing(8);

  // Add label at the left side of the horizontal layout
  QLabel* nameLabel = new QLabel(label);
  nameLabel->setStyleSheet(
    "color: #ffffff; background: transparent; font-weight: bold;");
  nameLabel->setFixedWidth(60);               // Fixed width for alignment
  nameLabel->setContentsMargins(0, 0, 0, 0);  // No internal margins
  layout->addWidget(nameLabel);

  QWidget* barContainer = new QWidget();
  barContainer->setFixedHeight(20);
  barContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  barContainer->setStyleSheet("background-color: #333333; border-radius: 2px;");

  QHBoxLayout* barLayout = new QHBoxLayout(barContainer);
  barLayout->setContentsMargins(0, 0, 0, 0);
  barLayout->setSpacing(0);

  // Calculate percentage (0-90%) based on value / globalMaxValue
  int percentage = 0;
  if (value <= 0 || globalMaxValue <= 0) {
    percentage = 0;  // No data
  } else {
    // Scale to 0-90% range with common scale
    percentage = static_cast<int>((value / globalMaxValue) * 90);
    percentage = std::min(percentage, 90);  // Cap at 90%
  }

  // For latency tests different colors indicate cache levels
  QWidget* bar = new QWidget();
  bar->setFixedHeight(20);
  bar->setStyleSheet(
    QString("background-color: %1; border-radius: 2px;").arg(color));

  QWidget* spacer = new QWidget();
  spacer->setStyleSheet("background-color: transparent;");

  // Use stretch factors for correct proportion
  barLayout->addWidget(bar, percentage);
  barLayout->addWidget(spacer, 100 - percentage);

  layout->addWidget(barContainer);

  // Show the actual latency value with the same color
  QLabel* valueLabel = new QLabel(QString("%1 ns").arg(value, 0, 'f', 1));
  valueLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  valueLabel->setStyleSheet(
    QString("color: %1; background: transparent;").arg(color));
  layout->addWidget(valueLabel);

  // Remove typical value reference

  mainLayout->addLayout(layout);
  return container;
}

QWidget* CPUResultRenderer::createMetricBox(const QString& title) {
  QWidget* box = new QWidget();
  box->setStyleSheet(
    "background-color: #252525; border-radius: 4px;");  // Remove border

  QVBoxLayout* layout = new QVBoxLayout(box);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(4);

  QLabel* titleLabel = new QLabel(title, box);
  titleLabel->setStyleSheet("color: #0078d4; font-size: 12px; font-weight: "
                            "bold; background: transparent;");
  layout->addWidget(titleLabel);

  return box;
}

}  // namespace DiagnosticRenderers
