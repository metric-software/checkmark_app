#include "CPUResultRenderer.h"

#include <algorithm>
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

        // Get cold start metrics
        if (rootObj.contains("cold_start") &&
            rootObj["cold_start"].isObject()) {
          QJsonObject coldStartObj = rootObj["cold_start"].toObject();
          cpu.coldStartAvg = coldStartObj["avg_response_time_us"].toDouble();
          cpu.coldStartMin = coldStartObj["min_response_time_us"].toDouble();
          cpu.coldStartMax = coldStartObj["max_response_time_us"].toDouble();
          cpu.coldStartStdDev = coldStartObj["std_dev_us"].toDouble();
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
              metrics.singleLoadClock = boostObj.value("single_load_clock_mhz").toInt();
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

  // Create a callback function to handle selection changes
  auto selectionCallback = [containerWidget, cpuTestsBox, gameSimBox,
                            singleCoreVals, fourThreadVals, simdScalarVals,
                            simdAvxVals, primeTimeVals, gameSimSmallVals,
                            gameSimMediumVals, gameSimLargeVals, coldStartVals,
                            downloadClient](
                             const QString& componentName,
                             const QString& originalFullName,
                             DiagnosticViewComponents::AggregationType type,
                             const CPUComparisonData& cpuData) {
    
    // If downloadClient is available and cpuData has no performance data (only name), 
    // fetch the actual data from the server
    if (downloadClient && !componentName.isEmpty() && cpuData.singleCoreTime <= 0) {
      LOG_INFO << "CPUResultRenderer: Fetching network data for CPU: " << componentName.toStdString() << " using original name: " << originalFullName.toStdString();
      
      downloadClient->fetchComponentData("cpu", originalFullName, 
        [containerWidget, cpuTestsBox, gameSimBox, singleCoreVals, fourThreadVals, 
         simdScalarVals, simdAvxVals, primeTimeVals, gameSimSmallVals,
         gameSimMediumVals, gameSimLargeVals, coldStartVals, componentName, type]
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
            
            // Build test data with the fetched values (same logic as main callback)
            struct TestData {
              QString objectName;
              double value;
              QString unit;
            };
            
            std::vector<TestData> tests = {
              {"comparison_bar_single_core", fetchedCpuData.singleCoreTime, "ms"},
              {"comparison_bar_four_thread", fetchedCpuData.fourThreadTime, "ms"},
              {"comparison_bar_scalar", fetchedCpuData.simdScalar, "μs"},
              {"comparison_bar_avx", fetchedCpuData.simdAvx, "μs"},
              {"comparison_bar_prime", fetchedCpuData.primeTime, "ms"},
              {"comparison_bar_small", fetchedCpuData.gameSimSmall / 1000000.0, "M ups"},
              {"comparison_bar_medium", fetchedCpuData.gameSimMedium / 1000000.0, "M ups"},
              {"comparison_bar_large", fetchedCpuData.gameSimLarge / 1000000.0, "M ups"},
              {"comparison_bar_cold_start", fetchedCpuData.coldStartAvg, "μs"}
            };
            
            // Update each comparison bar with fetched data
            for (QWidget* bar : allBars) {
              QWidget* parentContainer = bar->parentWidget();
              if (parentContainer) {
                QLabel* nameLabel = parentContainer->findChild<QLabel*>("comp_name_label");
                if (nameLabel) {
                  nameLabel->setText(displayName);
                  nameLabel->setStyleSheet("color: #ffffff; background: transparent;");
                }
                
                // Update the value label and bar based on bar type
                for (const auto& test : tests) {
                  if (bar->objectName() == test.objectName && test.value > 0) {
                    LOG_INFO << "CPUResultRenderer: Updating bar " << test.objectName.toStdString() 
                             << " with value " << test.value;
                    
                    QLabel* valueLabel = bar->parentWidget()->findChild<QLabel*>("value_label");
                    if (valueLabel) {
                      valueLabel->setText(QString("%1 %2").arg(test.value, 0, 'f', 1).arg(test.unit));
                      valueLabel->setStyleSheet("color: #FF4444; background: transparent;");
                    }
                    
                    // Also update the bar visual (simplified approach)
                    QLayout* layout = bar->layout();
                    if (layout) {
                      // Remove existing items
                      QLayoutItem* child;
                      while ((child = layout->takeAt(0)) != nullptr) {
                        delete child->widget();
                        delete child;
                      }
                      
                      // Scale comparison using the same maxValue as the user's bar for this test
                      double maxValue = 0.0;
                      const QString obj = bar->objectName();
                      if (obj == "comparison_bar_single_core") maxValue = singleCoreVals.second;
                      else if (obj == "comparison_bar_four_thread") maxValue = fourThreadVals.second;
                      else if (obj == "comparison_bar_scalar") maxValue = simdScalarVals.second;
                      else if (obj == "comparison_bar_avx") maxValue = simdAvxVals.second;
                      else if (obj == "comparison_bar_prime") maxValue = primeTimeVals.second;
                      else if (obj == "comparison_bar_small") maxValue = gameSimSmallVals.second / 1000000.0;
                      else if (obj == "comparison_bar_medium") maxValue = gameSimMediumVals.second / 1000000.0;
                      else if (obj == "comparison_bar_large") maxValue = gameSimLargeVals.second / 1000000.0;
                      else if (obj == "comparison_bar_cold_start") maxValue = coldStartVals.second;

                      const double scaledMaxValue = maxValue * 1.25;
                      const int percentage = (test.value <= 0 || scaledMaxValue <= 0)
                        ? 0
                        : static_cast<int>(std::min(100.0, (test.value / scaledMaxValue) * 100.0));

                      QWidget* barWidget = new QWidget();
                      barWidget->setFixedHeight(16);
                      barWidget->setStyleSheet("background-color: #FF4444; border-radius: 2px;");
                      
                      QWidget* spacer = new QWidget();
                      spacer->setStyleSheet("background-color: transparent;");
                      
                      QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
                      if (newLayout) {
                        newLayout->addWidget(barWidget, percentage);
                        newLayout->addWidget(spacer, 100 - percentage);
                      }
                    }
                    break;
                  }
                }
              }
            }
            
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

    if (componentName.isEmpty()) {
      // Reset all bars if user selects the placeholder option
      for (QWidget* bar : allBars) {
        QLabel* valueLabel = bar->findChild<QLabel*>("value_label");
        QLabel* nameLabel =
          bar->parentWidget()->findChild<QLabel*>("comp_name_label");

        if (valueLabel) {
          valueLabel->setText("-");
          valueLabel->setStyleSheet(
            "color: #888888; font-style: italic; background: transparent;");
        }

        if (nameLabel) {
          nameLabel->setText("Select CPU to compare");
          nameLabel->setStyleSheet(
            "color: #888888; font-style: italic; background: transparent;");
        }

        QLayout* layout = bar->layout();
        if (layout) {
          // Clear existing layout
          QLayoutItem* child;
          while ((child = layout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
          }

          // Add empty placeholder
          QWidget* emptyBar = new QWidget();
          emptyBar->setStyleSheet("background-color: transparent;");

          QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
          if (newLayout) {
            newLayout->addWidget(emptyBar);
          }
        }
      }
      // Clear boost section as well when placeholder is selected
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

      return;
    }

    // Build test data with the aggregated values
    struct TestData {
      QString objectName;
      double value;
      double maxValue;
      QString unit;
      bool lowerIsBetter;
    };

    // Extract max values from pairs for clarity and to potentially resolve
    // compiler issues
    double maxSingleCoreTime = singleCoreVals.second;
    double maxFourThreadTime = fourThreadVals.second;
    auto simdScalarSecond = simdScalarVals.second;
    double maxSimdScalarVal = simdScalarSecond;
    auto simdAvxSecond = simdAvxVals.second;
    double maxSimdAvxVal = simdAvxSecond;
    double maxPrimeTimeVal = primeTimeVals.second;
    double maxGameSimSmallVal = gameSimSmallVals.second;
    double maxGameSimMediumVal = gameSimMediumVals.second;
    double maxGameSimLargeVal = gameSimLargeVals.second;
    double maxColdStartVal = coldStartVals.second;

    std::vector<TestData> tests = {
      {"comparison_bar_single_core", cpuData.singleCoreTime, maxSingleCoreTime,
       "ms", true},
      {"comparison_bar_four_thread", cpuData.fourThreadTime, maxFourThreadTime,
       "ms", true},
      {"comparison_bar_scalar", cpuData.simdScalar, maxSimdScalarVal, "μs",
       true},
      {"comparison_bar_avx", cpuData.simdAvx, maxSimdAvxVal, "μs", true},
      {"comparison_bar_prime", cpuData.primeTime, maxPrimeTimeVal, "ms", true},
      {"comparison_bar_small", cpuData.gameSimSmall / 1000000.0,
       maxGameSimSmallVal / 1000000.0, "M ups", false},
      {"comparison_bar_medium", cpuData.gameSimMedium / 1000000.0,
       maxGameSimMediumVal / 1000000.0, "M ups", false},
      {"comparison_bar_large", cpuData.gameSimLarge / 1000000.0,
       maxGameSimLargeVal / 1000000.0, "M ups", false},
      {"comparison_bar_cold_start", cpuData.coldStartAvg, maxColdStartVal, "μs",
       true}};

    // Update boost info section if it exists
    QLabel* compCpuLabel = containerWidget->findChild<QLabel*>("comp_cpu_name");
    QLabel* compBaseClockLabel =
      containerWidget->findChild<QLabel*>("comp_base_clock");
    QLabel* compBoostClockLabel =
      containerWidget->findChild<QLabel*>("comp_boost_clock");
    QLabel* compAllCoreClockLabel =
      containerWidget->findChild<QLabel*>("comp_all_core_clock");

    // Get boost details from the CPU data
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

    // Calculate boost percentages for comparison CPU
    double compSingleBoostPct =
      cpuData.baseClock > 0
        ? (100.0 * (maxSingleBoost - cpuData.baseClock) / cpuData.baseClock)
        : 0;
    double compAllCoreBoostPct =
      cpuData.baseClock > 0
        ? (100.0 * (maxAllCore - cpuData.baseClock) / cpuData.baseClock)
        : 0;

    // Create display name with aggregation type
    QString displayName = (componentName == DownloadApiClient::generalAverageLabel())
      ? componentName
      : componentName + " (" +
          (type == DiagnosticViewComponents::AggregationType::Best ? "Best)" : "Avg)");

    // Update the CPU name in the boost section
    if (compCpuLabel) {
      compCpuLabel->setText(displayName);
      compCpuLabel->setStyleSheet("color: #ffffff; background: transparent;");
    }

    // Update base clock with consistent styling
    if (compBaseClockLabel && cpuData.baseClock > 0) {
      compBaseClockLabel->setText(QString("%1 MHz").arg(cpuData.baseClock));
      compBaseClockLabel->setStyleSheet(
        "color: #FF4444; background: transparent;");
      compBaseClockLabel->setVisible(true);
    }

    // Update single-core boost with consistent styling
    if (compBoostClockLabel && maxSingleBoost > 0) {
      compBoostClockLabel->setText(
        QString("%1 MHz <span style='color: #FFAA00;'>(+%2%)</span>")
          .arg(maxSingleBoost)
          .arg(compSingleBoostPct, 0, 'f', 1));
      compBoostClockLabel->setTextFormat(Qt::RichText);
      compBoostClockLabel->setStyleSheet(
        "color: #FF4444; background: transparent;");
      compBoostClockLabel->setVisible(true);
    }

    // Update all-core boost with consistent styling
    if (compAllCoreClockLabel && maxAllCore > 0) {
      compAllCoreClockLabel->setText(
        QString("%1 MHz <span style='color: #FFAA00;'>(+%2%)</span>")
          .arg(maxAllCore)
          .arg(compAllCoreBoostPct, 0, 'f', 1));
      compAllCoreClockLabel->setTextFormat(Qt::RichText);
      compAllCoreClockLabel->setStyleSheet(
        "color: #FF4444; background: transparent;");
      compAllCoreClockLabel->setVisible(true);
    }

    // Update all comparison bars
    for (QWidget* bar : allBars) {
      // Find the parent that contains the name_label (two levels up in
      // hierarchy)
      QWidget* parentContainer = bar->parentWidget();
      if (parentContainer) {
        QLabel* nameLabel =
          parentContainer->findChild<QLabel*>("comp_name_label");
        if (nameLabel) {
          nameLabel->setText(displayName);
          nameLabel->setStyleSheet("color: #ffffff; background: transparent;");
        }

        for (const auto& test : tests) {
          if (bar->objectName() == test.objectName) {
            // Skip updating this comparison if value is missing or invalid
            if (test.value <= 0) {
              continue;  // Skip this comparison entirely
            }

            // Find the value label and update it
            QLabel* valueLabel =
              bar->parentWidget()->findChild<QLabel*>("value_label");
            if (valueLabel) {
              valueLabel->setText(
                QString("%1 %2").arg(test.value, 0, 'f', 1).arg(test.unit));
              valueLabel->setStyleSheet(
                "color: #FF4444; background: transparent;");
            }

            // Update the bar width with scaled value
            QLayout* layout = bar->layout();
            if (layout) {
              // Remove existing items
              QLayoutItem* child;
              while ((child = layout->takeAt(0)) != nullptr) {
                delete child->widget();
                delete child;
              }

              // Calculate scaled percentage (0-100%)
              double scaledMax = test.maxValue * 1.25;  // 1/0.8 = 1.25
              int percentage = test.value <= 0
                                 ? 0
                                 : static_cast<int>(std::min(
                                     100.0, (test.value / scaledMax) * 100.0));

              // Create bar and spacer
              QWidget* barWidget = new QWidget();
              barWidget->setFixedHeight(16);
              barWidget->setStyleSheet(
                "background-color: #FF4444; border-radius: 2px;");

              QWidget* spacer = new QWidget();
              spacer->setStyleSheet("background-color: transparent;");

              QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
              if (newLayout) {
                newLayout->addWidget(barWidget, percentage);
                newLayout->addWidget(spacer, 100 - percentage);
              }
            }

            // Also update the user bar to show percentage difference
            QWidget* userBar =
              parentContainer->findChild<QWidget*>("userBarContainer");
            if (userBar) {
              // Find if there's an existing percentage label to remove
              QLabel* existingLabel =
                userBar->findChild<QLabel*>("percentageLabel");
              if (existingLabel) {
                delete existingLabel;
              }

              // Get the matching user value to calculate percentage
              double userValue = 0;
              if (test.objectName == "comparison_bar_single_core")
                userValue = singleCoreVals.first;
              else if (test.objectName == "comparison_bar_four_thread")
                userValue = fourThreadVals.first;
              else if (test.objectName == "comparison_bar_scalar")
                userValue = simdScalarVals.first;
              else if (test.objectName == "comparison_bar_avx")
                userValue = simdAvxVals.first;
              else if (test.objectName == "comparison_bar_prime")
                userValue = primeTimeVals.first;
              else if (test.objectName == "comparison_bar_small")
                userValue = gameSimSmallVals.first / 1000000;
              else if (test.objectName == "comparison_bar_medium")
                userValue = gameSimMediumVals.first / 1000000;
              else if (test.objectName == "comparison_bar_large")
                userValue = gameSimLargeVals.first / 1000000;
              else if (test.objectName == "comparison_bar_cold_start")
                userValue = coldStartVals.first;

              // Only add percentage if we have valid values
              if (userValue > 0 && test.value > 0) {
                // Calculate percentage difference
                double percentChange = 0;
                if (test.lowerIsBetter) {
                  // For lower-is-better metrics, negative percent means user is
                  // better
                  percentChange = ((userValue / test.value) - 1.0) * 100.0;
                } else {
                  // For higher-is-better metrics, positive percent means user
                  // is better
                  percentChange = ((userValue / test.value) - 1.0) * 100.0;
                }

                QString percentText;
                QString percentColor;

                // Determine if user result is better or worse
                bool isBetter = (test.lowerIsBetter && percentChange < 0) ||
                                (!test.lowerIsBetter && percentChange > 0);
                bool isApproxEqual = qAbs(percentChange) < 1.0;

                if (isApproxEqual) {
                  percentText = "≈";
                  percentColor = "#FFAA00";  // Yellow for equal
                } else {
                  percentText =
                    QString("%1%2%")
                      .arg(isBetter ? "+"
                                    : "")  // Add + prefix for better results
                      .arg(percentChange, 0, 'f', 1);
                  percentColor =
                    isBetter ? "#44FF44"
                             : "#FF4444";  // Green for better, red for worse
                }

                // Create an overlay layout if it doesn't exist
                QHBoxLayout* overlayLayout =
                  userBar->findChild<QHBoxLayout*>("overlayLayout");
                if (!overlayLayout) {
                  overlayLayout = new QHBoxLayout(userBar);
                  overlayLayout->setObjectName("overlayLayout");
                  overlayLayout->setContentsMargins(0, 0, 0, 0);
                }

                // Create and add percentage label
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

            break;
          }
        }
      }
    }
  };

  // Use the shared helper to create the dropdown
  return DiagnosticViewComponents::createAggregatedComparisonDropdown<
    CPUComparisonData>(aggregatedData, selectionCallback);
}

// New method to create a performance bar with comparison
QWidget* CPUResultRenderer::createComparisonPerformanceBar(
  const QString& label, double value, double comparisonValue, double maxValue,
  const QString& unit, bool lowerIsBetter) {

  // Use the shared component from DiagnosticViewComponents
  return DiagnosticViewComponents::createComparisonPerformanceBar(
    label, value, comparisonValue, maxValue, unit, lowerIsBetter);
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
    // No cold start comparison data yet, but keep the structure for future
    // compatibility
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
  QPair<double, double> coldStartVals(
    coldStartAvg,
    std::max(coldStartAvg * 1.5, 1000.0));  // Reasonable default max

  // Add comparison performance bars for CPU tests
  cpuTestsLayout->addWidget(createComparisonPerformanceBar(
    "Single-core", singleCoreTime, 0, maxCoreTime, "ms"));

  // Only add 4-thread test if we have valid data from user's system
  if (cpuData.fourThreadTime > 0) {
    // Create the 4-thread bar with the specific object name to match the
    // comparison handler
    QWidget* fourThreadBar =
      DiagnosticViewComponents::createComparisonPerformanceBar(
        "4-Thread", cpuData.fourThreadTime, 0, maxCoreTime, "ms", true);

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
    "Prime calculation", primeTime, 0, maxPrimeTime, "ms"));

  cpuTestsLayout->addSpacing(8);

  // SIMD tests
  cpuTestsLayout->addWidget(createComparisonPerformanceBar(
    "Scalar ops", simdScalar, 0, maxSimdTime, "μs"));
  cpuTestsLayout->addWidget(
    createComparisonPerformanceBar("AVX ops", simdAvx, 0, maxSimdTime, "μs"));

  cpuTestsLayout->addSpacing(8);

  // Add Cold Start Response Test section if data is available
  if (coldStartAvg > 0) {
    // Create the cold start bar with the specific object name to match the
    // comparison handler
    QWidget* coldStartBar =
      DiagnosticViewComponents::createComparisonPerformanceBar(
        "Cold Start Response", coldStartAvg, 0, coldStartVals.second, "μs",
        true);

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
    coldStartGrid->addWidget(minLabel, 0, 0);
    coldStartGrid->addWidget(minValueLabel, 0, 1);

    QLabel* maxLabel = new QLabel("Max Response:");
    maxLabel->setStyleSheet("color: #888888;");
    QLabel* maxValueLabel =
      new QLabel(QString("%1 μs").arg(coldStartMax, 0, 'f', 1));
    maxValueLabel->setStyleSheet("color: #FFFFFF; font-weight: bold;");
    coldStartGrid->addWidget(maxLabel, 0, 2);
    coldStartGrid->addWidget(maxValueLabel, 0, 3);

    QLabel* stdDevLabel = new QLabel("Std Deviation:");
    stdDevLabel->setStyleSheet("color: #888888;");
    QLabel* stdDevValueLabel =
      new QLabel(QString("%1 μs").arg(coldStartStdDev, 0, 'f', 1));
    stdDevValueLabel->setStyleSheet("color: #FFFFFF; font-weight: bold;");
    coldStartGrid->addWidget(stdDevLabel, 1, 0);
    coldStartGrid->addWidget(stdDevValueLabel, 1, 1);

    // Calculate jitter (max - min)
    double jitter = coldStartMax - coldStartMin;
    QLabel* jitterLabel = new QLabel("Jitter:");
    jitterLabel->setStyleSheet("color: #888888;");
    QLabel* jitterValueLabel =
      new QLabel(QString("%1 μs").arg(jitter, 0, 'f', 1));
    jitterValueLabel->setStyleSheet("color: #FFFFFF; font-weight: bold;");
    coldStartGrid->addWidget(jitterLabel, 1, 2);
    coldStartGrid->addWidget(jitterValueLabel, 1, 3);

    // Add the detailed widget to the main layout
    cpuTestsLayout->addWidget(coldStartDetailWidget);
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
    "Small (L3)", gameSimSmall / 1000000, 0, maxUPS / 1000000, "M ups", false));
  gameSimLayout->addWidget(createComparisonPerformanceBar(
    "Medium", gameSimMedium / 1000000, 0, maxUPS / 1000000, "M ups", false));
  gameSimLayout->addWidget(
    createComparisonPerformanceBar("Large (RAM)", gameSimLarge / 1000000, 0,
                                   maxUPS / 1000000, "M ups", false));

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

  // Calculate RAM/L3 latency ratio
  double memoryToL3Ratio =
    cpuData.cache.l3LatencyNs > 0
      ? cpuData.cache.ramLatencyNs / cpuData.cache.l3LatencyNs
      : 0;

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
  const int selectedSizes[] = {32,   64,   128,   256,   512,  1024,
                               4096, 8192, 16384, 32768, 65536};
  const int numSizes = sizeof(selectedSizes) / sizeof(selectedSizes[0]);
  double globalMaxLatency = 0;

  // Find max latency in user's data
  for (int i = 0; i < numSizes; i++) {
    int sizeKB = selectedSizes[i];
    if (cacheLatencies.contains(sizeKB)) {
      globalMaxLatency = std::max(globalMaxLatency, cacheLatencies[sizeKB]);
    }
  }

  // Also check comparison CPUs but ONLY for the specific sizes we display
  for (const auto& cpuEntry : comparisonData) {
    const CPUComparisonData& cpuCompData = cpuEntry.second;
    for (int i = 0; i < numSizes; i++) {
      int sizeKB = selectedSizes[i];
      if (cpuCompData.cacheLatencies.contains(sizeKB)) {
        double latency = cpuCompData.cacheLatencies[sizeKB];
        if (latency > 0) {
          globalMaxLatency = std::max(globalMaxLatency, latency);
        }
      }
    }
  }

  // Apply a small margin to avoid bars touching the edge
  // Pre-adjust finalScalingFactor to account for the 1.25 scaling in
  // createComparisonPerformanceBar
  double finalScalingFactor = globalMaxLatency * 1.1 / 1.25;

  // Generate aggregated data from individual results
  auto aggregatedData = generateAggregatedCPUData(finalComparisonData);

  // Create a callback function to handle selection changes
  auto selectionCallback = [containerWidget, cacheLatencies,
                            finalScalingFactor, downloadClient](
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
        [containerWidget, cacheLatencies, finalScalingFactor, componentName, type]
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
            
            // Update cache latency bars
            QList<QWidget*> allBars = containerWidget->findChildren<QWidget*>(
              QRegularExpression("^comparison_bar_cache.*"));
            for (QWidget* bar : allBars) {
              QLabel* nameLabel = bar->parentWidget()->findChild<QLabel*>("comp_name_label");
              if (nameLabel) {
                nameLabel->setText(displayName);
                nameLabel->setStyleSheet("color: #ffffff; background: transparent;");
              }

              // Extract the size from object name (comparison_bar_cache_XX_kb/mb)
              QString objName = bar->objectName();
              QRegularExpression sizeRegex("cache_(\\d+_[km]b)");
              QRegularExpressionMatch sizeMatch = sizeRegex.match(objName);

              if (sizeMatch.hasMatch()) {
                QString sizeStr = sizeMatch.captured(1);
                int sizeKB = 0;

                if (sizeStr.endsWith("_kb")) {
                  sizeKB = sizeStr.split("_").first().toInt();
                } else if (sizeStr.endsWith("_mb")) {
                  sizeKB = sizeStr.split("_").first().toInt() * 1024;
                }

                // Find the corresponding latency in the fetched CPU data
                double compLatency = 0;
                if (fetchedCpuData.cacheLatencies.contains(sizeKB)) {
                  compLatency = fetchedCpuData.cacheLatencies[sizeKB];
                }

                if (compLatency > 0) {
                  LOG_INFO << "CPUResultRenderer (Cache): Updating cache bar for " << sizeKB << "KB with latency " << compLatency;
                  
                  QLabel* valueLabel = bar->parentWidget()->findChild<QLabel*>("value_label");
                  if (valueLabel) {
                    valueLabel->setText(QString("%1 ns").arg(compLatency, 0, 'f', 2));
                    valueLabel->setStyleSheet("color: #FF4444; background: transparent;");
                  }

                  // Update the bar visual
                  QLayout* layout = bar->layout();
                  if (layout) {
                    // Remove existing items
                    QLayoutItem* child;
                    while ((child = layout->takeAt(0)) != nullptr) {
                      delete child->widget();
                      delete child;
                    }

                    // Calculate percentage for bar scaling
                    double scaledMaxLatency = finalScalingFactor * 1.25;
                    int percentage = compLatency <= 0 ? 0 : 
                      static_cast<int>(std::min(100.0, (compLatency / scaledMaxLatency) * 100.0));

                    // Create comparison bar
                    QWidget* barWidget = new QWidget();
                    barWidget->setFixedHeight(16);
                    barWidget->setStyleSheet("background-color: #FF4444; border-radius: 2px;");

                    QWidget* spacer = new QWidget();
                    spacer->setStyleSheet("background-color: transparent;");

                    QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
                    if (newLayout) {
                      newLayout->addWidget(barWidget, percentage);
                      newLayout->addWidget(spacer, 100 - percentage);
                    }
                  }
                }
              }
            }
            
          } else {
            LOG_ERROR << "CPUResultRenderer (Cache): Failed to fetch CPU data for " << componentName.toStdString() 
                      << ": " << error.toStdString();
            // Continue with empty/placeholder data
          }
        });
      
      return; // Exit early - the network callback will handle the UI update
    }
    
    // If we reach here, we have cached data and no network fetch is needed
    if (!componentName.isEmpty() && cpuData.singleCoreTime > 0) {
      LOG_INFO << "CPUResultRenderer (Cache): Using cached CPU data for " << componentName.toStdString();
      LOG_INFO << "CPUResultRenderer (Cache): Cached data - singleCoreTime: " << cpuData.singleCoreTime 
               << ", cache latencies count: " << cpuData.cacheLatencies.size();
      
      // Log the cached cache latencies
      for (const auto& entry : cpuData.cacheLatencies.toStdMap()) {
        LOG_INFO << "CPUResultRenderer (Cache): Cached cache latency " << entry.first << " KB = " << entry.second << " ns";
      }
    }
    
    // Reset comparison data if empty selection
    if (componentName.isEmpty()) {
      QList<QWidget*> allBars = containerWidget->findChildren<QWidget*>(
        QRegularExpression("^comparison_bar_cache.*"));
      for (QWidget* bar : allBars) {
        QLabel* valueLabel = bar->findChild<QLabel*>("value_label");
        QLabel* nameLabel =
          bar->parentWidget()->findChild<QLabel*>("comp_name_label");

        if (valueLabel) {
          valueLabel->setText("-");
          valueLabel->setStyleSheet(
            "color: #888888; font-style: italic; background: transparent;");
        }

        if (nameLabel) {
          nameLabel->setText("Select CPU to compare");
          nameLabel->setStyleSheet(
            "color: #888888; font-style: italic; background: transparent;");
        }

        QLayout* layout = bar->layout();
        if (layout) {
          // Clear existing layout
          QLayoutItem* child;
          while ((child = layout->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
          }

          // Add empty placeholder
          QWidget* emptyBar = new QWidget();
          emptyBar->setStyleSheet("background-color: transparent;");

          QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
          if (newLayout) {
            newLayout->addWidget(emptyBar);
          }
        }
      }
      return;
    }

    // Create display name with aggregation type
    QString displayName = (componentName == DownloadApiClient::generalAverageLabel())
      ? componentName
      : componentName + " (" +
          (type == DiagnosticViewComponents::AggregationType::Best ? "Best)" : "Avg)");

    // Update cache latency bars
    QList<QWidget*> allBars = containerWidget->findChildren<QWidget*>(
      QRegularExpression("^comparison_bar_cache.*"));
    for (QWidget* bar : allBars) {
      QLabel* nameLabel =
        bar->parentWidget()->findChild<QLabel*>("comp_name_label");
      if (nameLabel) {
        nameLabel->setText(displayName);
        nameLabel->setStyleSheet("color: #ffffff; background: transparent;");
      }

      // Extract the size from object name (comparison_bar_cache_XX_kb)
      QString objName = bar->objectName();
      QRegularExpression sizeRegex("cache_(\\d+_[km]b)");
      QRegularExpressionMatch match = sizeRegex.match(objName);

      if (match.hasMatch()) {
        QString sizeStr = match.captured(1).replace("_", " ").toUpper();
        int sizeKB = 0;

        // Convert size string to KB value
        if (sizeStr.contains("KB")) {
          sizeKB = sizeStr.split(" ").first().toInt();
        } else if (sizeStr.contains("MB")) {
          sizeKB = sizeStr.split(" ").first().toInt() * 1024;
        }

        // Check if we have data for this size
        if (sizeKB > 0 && cpuData.cacheLatencies.contains(sizeKB)) {
          double latency = cpuData.cacheLatencies[sizeKB];

          // Find and update the value label
          QLabel* valueLabel =
            bar->parentWidget()->findChild<QLabel*>("value_label");
          if (valueLabel) {
            valueLabel->setText(QString("%1 ns").arg(latency, 0, 'f', 1));
            valueLabel->setStyleSheet(
              "color: #FF4444; background: transparent;");
          }

          // Update the bar
          QLayout* layout = bar->layout();
          if (layout) {
            // Remove existing items
            QLayoutItem* child;
            while ((child = layout->takeAt(0)) != nullptr) {
              delete child->widget();
              delete child;
            }

            // Use finalScalingFactor directly for consistent scaling
            double scaledMaxValue = finalScalingFactor * 1.25;
            int percentage = latency <= 0
                               ? 0
                               : static_cast<int>(std::min(
                                   100.0, (latency / scaledMaxValue) * 100.0));

            LOG_INFO << "Latency bar " << sizeKB << " KB: " << latency << " ns - "
                     << percentage << "%";

            // Create bar and spacer
            QWidget* barWidget = new QWidget();
            barWidget->setFixedHeight(16);
            barWidget->setStyleSheet(
              "background-color: #FF4444; border-radius: 2px;");

            QWidget* spacer = new QWidget();
            spacer->setStyleSheet("background-color: transparent;");

            QHBoxLayout* newLayout = qobject_cast<QHBoxLayout*>(layout);
            if (newLayout) {
              newLayout->addWidget(barWidget, percentage);
              newLayout->addWidget(spacer, 100 - percentage);
            }
          }
        }
      }
    }
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

  // Add RAM/L3 ratio
  QString ratioText =
    QString("RAM/L3 latency ratio: <b>%1x</b>").arg(memoryToL3Ratio, 0, 'f', 2);
  QLabel* ratioLabel = new QLabel(ratioText);
  ratioLabel->setStyleSheet("color: #FFAA00; margin-top: 8px;");
  chartLayout->addWidget(ratioLabel);

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

  // Add detailed latency table in a collapsible section
  QWidget* detailedDataContainer = new QWidget();
  QVBoxLayout* detailedDataLayout = new QVBoxLayout(detailedDataContainer);

  QPushButton* showDetailsBtn =
    new QPushButton("▼ Show Detailed Cache Latencies");
  showDetailsBtn->setStyleSheet(R"(
        QPushButton {
            color: #0078d4;
            border: none;
            text-align: left;
            padding: 4px;
            font-size: 12px;
            background: transparent;
        }
        QPushButton:hover {
            color: #1084d8;
            text-decoration: underline;
        }
    )");

  // Create table widget and keep it hidden initially
  QWidget* detailsWidget =
    createDetailedLatencyTable(cacheLatencies, l2CacheKB, l3CacheKB);
  detailsWidget->setVisible(false);

  // Connect button to toggle visibility
  QObject::connect(
    showDetailsBtn, &QPushButton::clicked, [showDetailsBtn, detailsWidget]() {
      bool visible = detailsWidget->isVisible();
      detailsWidget->setVisible(!visible);
      showDetailsBtn->setText(visible ? "▼ Show Detailed Cache Latencies"
                                      : "▲ Hide Detailed Cache Latencies");
    });

  detailedDataLayout->addWidget(showDetailsBtn);
  detailedDataLayout->addWidget(detailsWidget);

  // Add to main layout
  mainLayout->addWidget(detailedDataContainer);

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

QWidget* CPUResultRenderer::createDetailedLatencyTable(
  const QMap<int, double>& cacheLatencies, int l2CacheKB, int l3CacheKB) {
  QWidget* detailsWidget = new QWidget();
  QVBoxLayout* detailsLayout = new QVBoxLayout(detailsWidget);
  detailsLayout->setContentsMargins(0, 0, 0, 0);

  // Create a table to show all latency results
  QTableWidget* latencyTable = new QTableWidget(cacheLatencies.size(), 4);
  latencyTable->setHorizontalHeaderLabels(
    {"Cache Size", "Latency", "Likely Cache Level", "Memory Access Time"});
  latencyTable->setStyleSheet(
    "background-color: #1e1e1e; color: #dddddd; border: 1px solid #333333;");

  // Set appropriate sizing policy to prevent horizontal expansion
  latencyTable->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  latencyTable->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  latencyTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  // Add these header resize modes to prevent horizontal expansion
  latencyTable->horizontalHeader()->setSectionResizeMode(
    0, QHeaderView::ResizeToContents);
  latencyTable->horizontalHeader()->setSectionResizeMode(
    1, QHeaderView::ResizeToContents);
  latencyTable->horizontalHeader()->setSectionResizeMode(
    2, QHeaderView::ResizeToContents);
  latencyTable->horizontalHeader()->setSectionResizeMode(3,
                                                         QHeaderView::Stretch);

  int row = 0;
  for (auto it = cacheLatencies.begin(); it != cacheLatencies.end(); ++it) {
    int sizeKB = it.key();
    double latencyNs = it.value();

    QTableWidgetItem* sizeItem = new QTableWidgetItem();

    // Format size (show in MB if >=1024 KB)
    QString sizeText;
    if (sizeKB >= 1024) {
      sizeText = QString("%1 MB").arg(sizeKB / 1024);
    } else {
      sizeText = QString("%1 KB").arg(sizeKB);
    }

    sizeItem->setText(sizeText);
    latencyTable->setItem(row, 0, sizeItem);

    QTableWidgetItem* latencyItem = new QTableWidgetItem();
    latencyItem->setText(QString("%1 ns").arg(latencyNs, 0, 'f', 1));
    latencyTable->setItem(row, 1, latencyItem);

    QTableWidgetItem* cacheTypeItem = new QTableWidgetItem();
    QString cacheLevel;
    QColor levelColor;

    if (sizeKB <= 64) {
      cacheLevel = "L1 Cache";
      levelColor = QColor("#44FF44");
    } else if (sizeKB <= l2CacheKB) {
      cacheLevel = "L2 Cache";
      levelColor = QColor("#88FF88");
    } else if (sizeKB <= l3CacheKB) {
      cacheLevel = "L3 Cache";
      levelColor = QColor("#FFAA00");
    } else {
      cacheLevel = "Main Memory";
      levelColor = QColor("#FF6666");
    }

    cacheTypeItem->setText(cacheLevel);
    cacheTypeItem->setForeground(QBrush(levelColor));
    latencyTable->setItem(row, 2, cacheTypeItem);

    // Calculate CPU cycles at 3GHz (common base clock)
    double cpuCycles = latencyNs * 3.0;  // 3GHz = 3 cycles per nanosecond
    QTableWidgetItem* cyclesItem = new QTableWidgetItem();
    cyclesItem->setText(QString("%1 cycles").arg(cpuCycles, 0, 'f', 1));
    latencyTable->setItem(row, 3, cyclesItem);

    row++;
  }

  latencyTable->resizeColumnsToContents();
  latencyTable->setFixedHeight(200);
  detailsLayout->addWidget(latencyTable);

  return detailsWidget;
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
