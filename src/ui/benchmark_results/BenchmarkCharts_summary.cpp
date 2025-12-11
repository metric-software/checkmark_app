#include <algorithm>
#include <cmath>
#include <numeric>

#include <QApplication>
#include <QRegularExpression>

#include "logging/Logger.h"

#include "BenchmarkCharts.h"

BenchmarkCharts::BenchmarkSummary BenchmarkCharts::calculateBenchmarkSummary(
  const QString& filePath) {

  BenchmarkSummary summary;

  // Open the CSV file
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open CSV file for summary calculation";
    return summary;
  }

  QTextStream in(&file);

  // Read header line
  QString header = in.readLine();
  QStringList headers = header.split(",");

  // Find column indices
  int timeIndex = headers.indexOf("Time");
  int fpsIndex = headers.indexOf("FPS");
  int frameTimeIndex = headers.indexOf("Frame Time");
  int frameTime1HighIndex = headers.indexOf("1% High Frame Time");
  int frameTime5HighIndex = headers.indexOf("5% High Frame Time");
  int cpuUsageIndex = headers.indexOf("PDH_CPU_Usage(%)");
  if (cpuUsageIndex < 0) cpuUsageIndex = headers.indexOf("CPU Usage");
  int gpuUsageIndex = headers.indexOf("GPU Usage");
  if (gpuUsageIndex < 0) gpuUsageIndex = headers.indexOf("GPU Utilization");
  int memoryLoadIndex = headers.indexOf("PDH_Memory_Load(%)");
  if (memoryLoadIndex < 0) memoryLoadIndex = headers.indexOf("Memory Load");
  int gpuMemUsedIndex = headers.indexOf("GPU Mem Used");
  int gpuMemTotalIndex = headers.indexOf("GPU Mem Total");
  int frameTimeVarianceIndex = headers.indexOf("Frame Time Variance");
  int highestFrameTimeIndex = headers.indexOf("Highest Frame Time");
  QVector<int> cpuCoreIndices;
  QRegularExpression corePattern(
    R"(^\s*(PDH_)?Core\s+\d+\s+CPU?\s*\(%\)\s*$)",
    QRegularExpression::CaseInsensitiveOption);
  for (int i = 0; i < headers.size(); i++) {
    if (corePattern.match(headers[i].trimmed()).hasMatch()) {
      cpuCoreIndices.append(i);
    }
  }

  // Check if required columns exist
  if (fpsIndex < 0) {
    LOG_ERROR << "Failed to find FPS column in CSV file";
    file.close();
    return summary;
  }

  // First, read the initial time value for normalization
  // We'll need to read the file in two passes if time column exists
  int firstTimeValue = 0;
  bool hasTimeColumn = (timeIndex >= 0);

  if (hasTimeColumn) {
    // Store the current position
    qint64 initialPos = in.pos();

    // Find the first valid time value
    bool foundFirstTime = false;

    while (!in.atEnd() && !foundFirstTime) {
      QString line = in.readLine();
      QStringList fields = line.split(",");

      if (fields.size() <= timeIndex) continue;

      bool ok;
      double timeValue = fields[timeIndex].toDouble(&ok);
      if (ok) {
        firstTimeValue = static_cast<int>(timeValue);
        foundFirstTime = true;
      }
    }

    // Reset the file position to after the header
    in.seek(initialPos);

    LOG_INFO << "First time value found: " << firstTimeValue;
  }

  // Create vectors for each section
  QVector<double> beachFpsValues;
  QVector<double> beachFrameTime1HighValues;
  QVector<double> beachFrameTime5HighValues;

  QVector<double> flyingFpsValues;
  QVector<double> flyingFrameTime1HighValues;
  QVector<double> flyingFrameTime5HighValues;

  QVector<double> outpostFpsValues;
  QVector<double> outpostFrameTime1HighValues;
  QVector<double> outpostFrameTime5HighValues;

  // Create vectors for overall metrics
  QVector<double> overallFpsValues;
  QVector<double> overallFrameTimeValues;
  QVector<double> overallFrameTime1HighValues;
  QVector<double> overallFrameTime5HighValues;
  QVector<double> cpuUsageValues;
  QVector<double> gpuUsageValues;
  QVector<double> memoryLoadValues;

  // Analysis tracking variables
  int gpuHighUsageCount = 0;
  bool ramWarningTriggered = false;
  bool vramWarningTriggered = false;
  int highFrameTimeVarianceCount = 0;
  int smallFreezeCount = 0;
  int fpsFreezeCount = 0;

  // Read data line by line
  int lineCounter = 0;
  while (!in.atEnd()) {
    QString line = in.readLine();
    QStringList fields = line.split(",");

    // Skip lines that don't have enough fields
    int maxIndex = fpsIndex;
    for (int idx : {frameTimeIndex,
                    frameTime1HighIndex,
                    frameTime5HighIndex,
                    cpuUsageIndex,
                    gpuUsageIndex,
                    memoryLoadIndex,
                    gpuMemUsedIndex,
                    gpuMemTotalIndex,
                    frameTimeVarianceIndex,
                    highestFrameTimeIndex}) {
      if (idx > maxIndex) maxIndex = idx;
    }
    for (int idx : cpuCoreIndices) {
      if (idx > maxIndex) maxIndex = idx;
    }

    if (fields.size() <= maxIndex) {
      lineCounter++;
      continue;
    }

    // Get the normalized time value (in seconds)
    int normalizedTimeInSeconds = 0;

    if (hasTimeColumn && timeIndex < fields.size()) {
      bool ok;
      double timeValue = fields[timeIndex].toDouble(&ok);
      if (ok) {
        // Normalize the time by subtracting the first time value
        normalizedTimeInSeconds = static_cast<int>(timeValue) - firstTimeValue;
      } else {
        // If time column exists but this value is invalid, use line counter
        normalizedTimeInSeconds = lineCounter;
      }
    } else {
      // No time column, use line counter
      normalizedTimeInSeconds = lineCounter;
    }

    // Parse FPS value
    bool fpsOk = false;
    double fps = fields[fpsIndex].toDouble(&fpsOk);

    // Parse frame time value if available
    if (frameTimeIndex >= 0 && frameTimeIndex < fields.size()) {
      bool ftOk = false;
      double frameTime = fields[frameTimeIndex].toDouble(&ftOk);
      if (ftOk && frameTime > 0) {
        overallFrameTimeValues.append(frameTime);
      }
    }

    // Parse 1% and 5% high frame time values if available
    bool frameTime1HighOk = false;
    bool frameTime5HighOk = false;
    double frameTime1High = -1.0;
    double frameTime5High = -1.0;

    if (frameTime1HighIndex >= 0 && frameTime1HighIndex < fields.size()) {
      frameTime1High = fields[frameTime1HighIndex].toDouble(&frameTime1HighOk);
    }

    if (frameTime5HighIndex >= 0 && frameTime5HighIndex < fields.size()) {
      frameTime5High = fields[frameTime5HighIndex].toDouble(&frameTime5HighOk);
    }

    double cpuUsageValue = -1.0;
    bool cpuUsageOk = false;
    if (cpuUsageIndex >= 0 && cpuUsageIndex < fields.size()) {
      cpuUsageValue = fields[cpuUsageIndex].toDouble(&cpuUsageOk);
    }
    if (!cpuUsageOk && !cpuCoreIndices.isEmpty()) {
      double totalUsage = 0.0;
      int validCores = 0;

      for (int coreIdx : cpuCoreIndices) {
        if (coreIdx < fields.size()) {
          bool ok = false;
          double usage = fields[coreIdx].toDouble(&ok);
          if (ok && usage >= 0) {
            totalUsage += usage;
            validCores++;
          }
        }
      }

      if (validCores > 0) {
        cpuUsageValue = totalUsage / validCores;
        cpuUsageOk = true;
      }
    }
    if (cpuUsageOk && cpuUsageValue >= 0) {
      cpuUsageValues.append(cpuUsageValue);
    }

    double gpuUsageValue = -1.0;
    bool gpuUsageOk = false;
    if (gpuUsageIndex >= 0 && gpuUsageIndex < fields.size()) {
      gpuUsageValue = fields[gpuUsageIndex].toDouble(&gpuUsageOk);
      if (gpuUsageOk && gpuUsageValue >= 0) {
        gpuUsageValues.append(gpuUsageValue);
      }
    }

    double memoryLoadValue = -1.0;
    bool memoryLoadOk = false;
    if (memoryLoadIndex >= 0 && memoryLoadIndex < fields.size()) {
      memoryLoadValue = fields[memoryLoadIndex].toDouble(&memoryLoadOk);
      if (memoryLoadOk && memoryLoadValue >= 0) {
        memoryLoadValues.append(memoryLoadValue);
      }
    }

    // Skip invalid data points
    if (!fpsOk || fps <= 0) {
      lineCounter++;
      continue;
    }

    // Always add to overall metrics
    overallFpsValues.append(fps);
    if (frameTime1HighOk && frameTime1High > 0) {
      overallFrameTime1HighValues.append(frameTime1High);
    }
    if (frameTime5HighOk && frameTime5High > 0) {
      overallFrameTime5HighValues.append(frameTime5High);
    }

    // Determine which section this data point belongs to based on normalized
    // time
    if (normalizedTimeInSeconds >= BenchmarkSummary::beachStartTime &&
        normalizedTimeInSeconds < BenchmarkSummary::beachEndTime) {
      // Beach section
      beachFpsValues.append(fps);
      if (frameTime1HighOk && frameTime1High > 0) {
        beachFrameTime1HighValues.append(frameTime1High);
      }
      if (frameTime5HighOk && frameTime5High > 0) {
        beachFrameTime5HighValues.append(frameTime5High);
      }
    } else if (normalizedTimeInSeconds >= BenchmarkSummary::flyingStartTime &&
               normalizedTimeInSeconds < BenchmarkSummary::flyingEndTime) {
      // Flying section
      flyingFpsValues.append(fps);
      if (frameTime1HighOk && frameTime1High > 0) {
        flyingFrameTime1HighValues.append(frameTime1High);
      }
      if (frameTime5HighOk && frameTime5High > 0) {
        flyingFrameTime5HighValues.append(frameTime5High);
      }
    } else if (normalizedTimeInSeconds >= BenchmarkSummary::outpostStartTime &&
               normalizedTimeInSeconds < BenchmarkSummary::outpostEndTime) {
      // Outpost section
      outpostFpsValues.append(fps);
      if (frameTime1HighOk && frameTime1High > 0) {
        outpostFrameTime1HighValues.append(frameTime1High);
      }
      if (frameTime5HighOk && frameTime5High > 0) {
        outpostFrameTime5HighValues.append(frameTime5High);
      }
    }

    // Perform analysis on this data point if the required columns exist

    // GPU bottleneck check
    if (gpuUsageOk && gpuUsageValue > 90.0) {
      gpuHighUsageCount++;
    }

    // RAM usage check
    if (memoryLoadOk && memoryLoadValue > 90.0) {
      ramWarningTriggered = true;
    }

    // VRAM usage check
    if (gpuMemUsedIndex >= 0 && gpuMemUsedIndex < fields.size() &&
        gpuMemTotalIndex >= 0 && gpuMemTotalIndex < fields.size()) {
      bool memUsedOk = false, memTotalOk = false;
      double memUsed = fields[gpuMemUsedIndex].toDouble(&memUsedOk);
      double memTotal = fields[gpuMemTotalIndex].toDouble(&memTotalOk);

      if (memUsedOk && memTotalOk && memTotal > 0) {
        double vramUsagePercent = (memUsed / memTotal) * 100.0;
        if (vramUsagePercent > 85.0) {
          vramWarningTriggered = true;
        }
      }
    }

    // Frame time variance check
    if (frameTimeVarianceIndex >= 0 && frameTimeVarianceIndex < fields.size()) {
      bool varianceOk = false;
      double variance = fields[frameTimeVarianceIndex].toDouble(&varianceOk);
      if (varianceOk && variance > 3.0) {
        highFrameTimeVarianceCount++;
      }
    }

    // Frame freeze checks
    if (highestFrameTimeIndex >= 0 && highestFrameTimeIndex < fields.size()) {
      bool frameTimeOk = false;
      double highestFrameTime =
        fields[highestFrameTimeIndex].toDouble(&frameTimeOk);
      if (frameTimeOk) {
        if (highestFrameTime > 100.0) {
          fpsFreezeCount++;
        } else if (highestFrameTime > 50.0) {
          smallFreezeCount++;
        }
      }
    }

    lineCounter++;
  }

  file.close();

  // Debug output
  LOG_INFO << "Data points collected - Beach: " << beachFpsValues.size()
            << ", Flying: " << flyingFpsValues.size()
            << ", Outpost: " << outpostFpsValues.size()
            << ", Overall: " << overallFpsValues.size();

  auto computeAvgMinMax = [](const QVector<double>& values, double& avg,
                             double& minVal, double& maxVal) {
    if (values.isEmpty()) {
      avg = minVal = maxVal = -1.0;
      return;
    }

    minVal = *std::min_element(values.begin(), values.end());
    maxVal = *std::max_element(values.begin(), values.end());
    avg = std::accumulate(values.begin(), values.end(), 0.0) /
          static_cast<double>(values.size());
  };

  auto percentileLow = [](QVector<double> values, double fraction) -> double {
    if (values.isEmpty() || fraction <= 0.0) return -1.0;
    std::sort(values.begin(), values.end());
    int idx = static_cast<int>(std::floor(fraction * values.size()));
    int maxIdx = static_cast<int>(values.size() - 1);
    idx = std::clamp(idx, 0, maxIdx);
    return values[idx];
  };

  // Calculate Beach section statistics
  if (!beachFpsValues.isEmpty()) {
    // Calculate average FPS
    summary.beachAvgFps =
      std::accumulate(beachFpsValues.begin(), beachFpsValues.end(), 0.0) /
      beachFpsValues.size();

    // Calculate 1% Low FPS from 1% High Frame Time
    if (!beachFrameTime1HighValues.isEmpty()) {
      // Calculate average 1% High Frame Time
      double avg1HighFrameTime =
        std::accumulate(beachFrameTime1HighValues.begin(),
                        beachFrameTime1HighValues.end(), 0.0) /
        beachFrameTime1HighValues.size();
      // Convert to FPS (1000 ms / frame time in ms)
      summary.beach1LowFps =
        (avg1HighFrameTime > 0) ? 1000.0 / avg1HighFrameTime : -1.0;
    }

    // Calculate 5% Low FPS from 5% High Frame Time
    if (!beachFrameTime5HighValues.isEmpty()) {
      // Calculate average 5% High Frame Time
      double avg5HighFrameTime =
        std::accumulate(beachFrameTime5HighValues.begin(),
                        beachFrameTime5HighValues.end(), 0.0) /
        beachFrameTime5HighValues.size();
      // Convert to FPS (1000 ms / frame time in ms)
      summary.beach5LowFps =
        (avg5HighFrameTime > 0) ? 1000.0 / avg5HighFrameTime : -1.0;
    }
  }

  // Calculate Flying section statistics
  if (!flyingFpsValues.isEmpty()) {
    // Calculate average FPS
    summary.flyingAvgFps =
      std::accumulate(flyingFpsValues.begin(), flyingFpsValues.end(), 0.0) /
      flyingFpsValues.size();

    // Calculate 1% Low FPS from 1% High Frame Time
    if (!flyingFrameTime1HighValues.isEmpty()) {
      // Calculate average 1% High Frame Time
      double avg1HighFrameTime =
        std::accumulate(flyingFrameTime1HighValues.begin(),
                        flyingFrameTime1HighValues.end(), 0.0) /
        flyingFrameTime1HighValues.size();
      // Convert to FPS (1000 ms / frame time in ms)
      summary.flying1LowFps =
        (avg1HighFrameTime > 0) ? 1000.0 / avg1HighFrameTime : -1.0;
    }

    // Calculate 5% Low FPS from 5% High Frame Time
    if (!flyingFrameTime5HighValues.isEmpty()) {
      // Calculate average 5% High Frame Time
      double avg5HighFrameTime =
        std::accumulate(flyingFrameTime5HighValues.begin(),
                        flyingFrameTime5HighValues.end(), 0.0) /
        flyingFrameTime5HighValues.size();
      // Convert to FPS (1000 ms / frame time in ms)
      summary.flying5LowFps =
        (avg5HighFrameTime > 0) ? 1000.0 / avg5HighFrameTime : -1.0;
    }
  }

  // Calculate Outpost section statistics
  if (!outpostFpsValues.isEmpty()) {
    // Calculate average FPS
    summary.outpostAvgFps =
      std::accumulate(outpostFpsValues.begin(), outpostFpsValues.end(), 0.0) /
      outpostFpsValues.size();

    // Calculate 1% Low FPS from 1% High Frame Time
    if (!outpostFrameTime1HighValues.isEmpty()) {
      // Calculate average 1% High Frame Time
      double avg1HighFrameTime =
        std::accumulate(outpostFrameTime1HighValues.begin(),
                        outpostFrameTime1HighValues.end(), 0.0) /
        outpostFrameTime1HighValues.size();
      // Convert to FPS (1000 ms / frame time in ms)
      summary.outpost1LowFps =
        (avg1HighFrameTime > 0) ? 1000.0 / avg1HighFrameTime : -1.0;
    }

    // Calculate 5% Low FPS from 5% High Frame Time
    if (!outpostFrameTime5HighValues.isEmpty()) {
      // Calculate average 5% High Frame Time
      double avg5HighFrameTime =
        std::accumulate(outpostFrameTime5HighValues.begin(),
                        outpostFrameTime5HighValues.end(), 0.0) /
        outpostFrameTime5HighValues.size();
      // Convert to FPS (1000 ms / frame time in ms)
      summary.outpost5LowFps =
        (avg5HighFrameTime > 0) ? 1000.0 / avg5HighFrameTime : -1.0;
    }
  }

  // Calculate Overall statistics
  if (!overallFpsValues.isEmpty()) {
    // Calculate average FPS
    summary.overallAvgFps =
      std::accumulate(overallFpsValues.begin(), overallFpsValues.end(), 0.0) /
      overallFpsValues.size();

    // Calculate 1% Low FPS from 1% High Frame Time
    if (!overallFrameTime1HighValues.isEmpty()) {
      // Calculate average 1% High Frame Time
      double avg1HighFrameTime =
        std::accumulate(overallFrameTime1HighValues.begin(),
                        overallFrameTime1HighValues.end(), 0.0) /
        overallFrameTime1HighValues.size();
      // Convert to FPS (1000 ms / frame time in ms)
      summary.overall1LowFps =
        (avg1HighFrameTime > 0) ? 1000.0 / avg1HighFrameTime : -1.0;
    }

    // Calculate 5% Low FPS from 5% High Frame Time
    if (!overallFrameTime5HighValues.isEmpty()) {
      // Calculate average 5% High Frame Time
      double avg5HighFrameTime =
        std::accumulate(overallFrameTime5HighValues.begin(),
                        overallFrameTime5HighValues.end(), 0.0) /
        overallFrameTime5HighValues.size();
      // Convert to FPS (1000 ms / frame time in ms)
    summary.overall5LowFps =
      (avg5HighFrameTime > 0) ? 1000.0 / avg5HighFrameTime : -1.0;
    }
  }

  // Legacy/aggregate metrics for dashboard and existing cards
  double fpsMin = -1.0, fpsMax = -1.0;
  computeAvgMinMax(overallFpsValues, summary.avgFps, fpsMin, fpsMax);
  summary.minFps = fpsMin;
  summary.maxFps = fpsMax;

  double fps1LowPercentile = percentileLow(overallFpsValues, 0.01);
  double fps01LowPercentile = percentileLow(overallFpsValues, 0.001);
  summary.fps1Low =
    (summary.overall1LowFps > 0) ? summary.overall1LowFps : fps1LowPercentile;
  summary.fps01Low = fps01LowPercentile;

  double frameTimeMin = -1.0, frameTimeMax = -1.0;
  computeAvgMinMax(
    overallFrameTimeValues, summary.avgFrameTime, frameTimeMin, frameTimeMax);
  summary.minFrameTime = frameTimeMin;
  summary.maxFrameTime = frameTimeMax;

  double cpuMin = -1.0, cpuMax = -1.0;
  computeAvgMinMax(cpuUsageValues, summary.avgCpuUsage, cpuMin, cpuMax);
  summary.maxCpuUsage = cpuMax;

  double gpuMin = -1.0, gpuMax = -1.0;
  computeAvgMinMax(gpuUsageValues, summary.avgGpuUsage, gpuMin, gpuMax);
  summary.maxGpuUsage = gpuMax;

  double memMin = -1.0, memMax = -1.0;
  computeAvgMinMax(memoryLoadValues, summary.avgMemoryUsage, memMin, memMax);
  summary.maxMemoryUsage = memMax;

  // Set analysis flags
  summary.gpuBottleneckLight = (gpuHighUsageCount >= 5);
  summary.gpuBottleneckSevere = (gpuHighUsageCount >= 30);
  summary.ramUsageWarning = ramWarningTriggered;
  summary.vramUsageWarning = vramWarningTriggered;
  summary.fpsStutteringDetected = (highFrameTimeVarianceCount >= 15);
  summary.smallFreezeCount = smallFreezeCount;
  summary.fpsFreezeCount = fpsFreezeCount;

  return summary;
}

QString BenchmarkCharts::generateSectionalSummaryHtml(
  const QString& csvFilePath) {
  // Create the output directory if it doesn't exist
  QDir outputDir("html_reports");
  if (!ensureOutputDirExists(outputDir)) {
    return QString();
  }

  // Calculate the benchmark summary from the CSV data
  BenchmarkSummary summary = calculateBenchmarkSummary(csvFilePath);

  // Get file metadata
  QFileInfo fileInfo(csvFilePath);
  QString fileName = fileInfo.fileName();
  QString timestamp = fileInfo.lastModified().toString("yyyy-MM-dd HH:mm:ss");

  // Create the output HTML file
  QString outputFileName = "benchmark_summary_" + fileInfo.baseName() + ".html";
  QString outputFilePath = outputDir.filePath(outputFileName);

  QFile outFile(outputFilePath);
  if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to create HTML output file";
    return QString();
  }

  QTextStream out(&outFile);

  // Begin HTML document
  out << "<!DOCTYPE html>\n";
  out << "<html lang=\"en\">\n";
  out << "<head>\n";
  out << "    <meta charset=\"UTF-8\">\n";
  out << "    <meta name=\"viewport\" content=\"width=device-width, "
         "initial-scale=1.0\">\n";
  out << "    <title>Benchmark Summary</title>\n";
  out << "    <style>\n";
  out << "        body {\n";
  out << "            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;\n";
  out << "            line-height: 1.6;\n";
  out << "            color: #e8ecf3;\n";
  out << "            max-width: 1200px;\n";
  out << "            margin: 0 auto;\n";
  out << "            padding: 24px;\n";
  out << "            background-color: #111418;\n";
  out << "        }\n";
  out << "        h1 {\n";
  out << "            color: #f3f5f7;\n";
  out << "            border-bottom: 2px solid #4da3ff;\n";
  out << "            padding-bottom: 10px;\n";
  out << "        }\n";
  out << "        h2 {\n";
  out << "            color: #f3f5f7;\n";
  out << "            margin-top: 30px;\n";
  out << "        }\n";
  out << "        .metadata {\n";
  out << "            background-color: #161c24;\n";
  out << "            border-left: 4px solid #4da3ff;\n";
  out << "            padding: 10px 15px;\n";
  out << "            margin-bottom: 30px;\n";
  out << "            border-radius: 6px;\n";
  out << "            color: #c3cad5;\n";
  out << "        }\n";
  out << "        .metadata p {\n";
  out << "            margin: 5px 0;\n";
  out << "        }\n";
  out << "        .section-cards {\n";
  out << "            display: flex;\n";
  out << "            flex-wrap: wrap;\n";
  out << "            gap: 20px;\n";
  out << "            margin-bottom: 30px;\n";
  out << "        }\n";
  out << "        .section-card {\n";
  out << "            flex: 1;\n";
  out << "            min-width: 300px;\n";
  out << "            background-color: #1b2027;\n";
  out << "            border-radius: 8px;\n";
  out << "            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.35);\n";
  out << "            padding: 20px;\n";
  out << "            border: 1px solid #242b34;\n";
  out << "            transition: transform 0.2s ease;\n";
  out << "        }\n";
  out << "        .section-card:hover {\n";
  out << "            transform: translateY(-3px);\n";
  out << "        }\n";
  out << "        .analysis-card {\n";
  out << "            flex: 1;\n";
  out << "            min-width: 300px;\n";
  out << "            background-color: #1b2027;\n";
  out << "            border-radius: 8px;\n";
  out << "            box-shadow: 0 10px 30px rgba(0, 0, 0, 0.35);\n";
  out << "            padding: 20px;\n";
  out << "            border: 1px solid #242b34;\n";
  out << "        }\n";
  out << "        .warning-item {\n";
  out << "            margin-bottom: 12px;\n";
  out << "            padding: 10px 12px;\n";
  out << "            border-radius: 6px;\n";
  out << "            color: #e8ecf3;\n";
  out << "        }\n";
  out << "        .warning-severe {\n";
  out << "            background-color: #2b1c1f;\n";
  out << "            border-left: 4px solid #e74c3c;\n";
  out << "        }\n";
  out << "        .warning-moderate {\n";
  out << "            background-color: #2b2416;\n";
  out << "            border-left: 4px solid #f39c12;\n";
  out << "        }\n";
  out << "        .warning-info {\n";
  out << "            background-color: #17212b;\n";
  out << "            border-left: 4px solid #4da3ff;\n";
  out << "        }\n";
  out << "        .section-title {\n";
  out << "            font-size: 1.4em;\n";
  out << "            color: #e5e9f0;\n";
  out << "            margin-top: 0;\n";
  out << "            padding-bottom: 10px;\n";
  out << "            border-bottom: 1px solid #2d333d;\n";
  out << "        }\n";
  out << "        .metric {\n";
  out << "            margin: 15px 0;\n";
  out << "        }\n";
  out << "        .metric-name {\n";
  out << "            font-weight: 500;\n";
  out << "            color: #aeb7c2;\n";
  out << "        }\n";
  out << "        .metric-value {\n";
  out << "            font-size: 1.8em;\n";
  out << "            font-weight: 600;\n";
  out << "            color: #e5e9f0;\n";
  out << "        }\n";
  out << "        .metric-value.good {\n";
  out << "            color: #27ae60;\n";
  out << "        }\n";
  out << "        .metric-value.average {\n";
  out << "            color: #f39c12;\n";
  out << "        }\n";
  out << "        .metric-value.poor {\n";
  out << "            color: #e74c3c;\n";
  out << "        }\n";
  out << "        .metric-unit {\n";
  out << "            font-size: 0.9em;\n";
  out << "            color: #9ba5b3;\n";
  out << "            margin-left: 3px;\n";
  out << "        }\n";
  out << "        footer {\n";
  out << "            text-align: center;\n";
  out << "            margin-top: 50px;\n";
  out << "            padding-top: 20px;\n";
  out << "            border-top: 1px solid #242b34;\n";
  out << "            color: #9aa2af;\n";
  out << "        }\n";
  out << "    </style>\n";
  out << "</head>\n";
  out << "<body>\n";
  out << "    <h1>Benchmark Performance Summary</h1>\n";

  // Metadata section
  out << "    <div class=\"metadata\">\n";
  out << "        <p><strong>Benchmark File:</strong> " << fileName << "</p>\n";
  out << "        <p><strong>Recorded:</strong> " << timestamp << "</p>\n";
  out << "        <p><strong>Section Breakdown:</strong> ";
  out << BenchmarkSummary::beachLabel << " (0-26s), ";
  out << BenchmarkSummary::jungleLabel << " (26-114s), ";
  out << BenchmarkSummary::outpostLabel << " (114-124s)</p>\n";
  out << "    </div>\n";

  // Overall Performance
  out << "    <h2>Overall Performance</h2>\n";
  out << "    <div class=\"section-cards\">\n";

  // Overall performance card
  out << "        <div class=\"section-card\">\n";
  out << "            <h3 class=\"section-title\">"
      << BenchmarkSummary::overallLabel << "</h3>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">Average FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.overallAvgFps > 80)
    out << " good";
  else if (summary.overallAvgFps > 50)
    out << " average";
  else if (summary.overallAvgFps > 0)
    out << " poor";
  out << "\">"
      << (summary.overallAvgFps > 0
            ? QString::number(summary.overallAvgFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">1% Low FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.overall1LowFps > 60)
    out << " good";
  else if (summary.overall1LowFps > 30)
    out << " average";
  else if (summary.overall1LowFps > 0)
    out << " poor";
  out << "\">"
      << (summary.overall1LowFps > 0
            ? QString::number(summary.overall1LowFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">5% Low FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.overall5LowFps > 70)
    out << " good";
  else if (summary.overall5LowFps > 40)
    out << " average";
  else if (summary.overall5LowFps > 0)
    out << " poor";
  out << "\">"
      << (summary.overall5LowFps > 0
            ? QString::number(summary.overall5LowFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "        </div>\n";

  // Analysis card
  out << "        <div class=\"analysis-card\">\n";
  out << "            <h3 class=\"section-title\">Performance Analysis</h3>\n";

  bool hasWarnings = false;

  // GPU bottleneck warnings
  if (summary.gpuBottleneckSevere) {
    hasWarnings = true;
    out << "            <div class=\"warning-item warning-severe\">\n";
    out << "                <strong>GPU Bottleneck Detected:</strong> Your GPU "
           "is running at >90% usage for extended periods. ";
    out << "Consider lowering graphics settings or resolution for better "
           "performance.\n";
    out << "            </div>\n";
  } else if (summary.gpuBottleneckLight) {
    hasWarnings = true;
    out << "            <div class=\"warning-item warning-moderate\">\n";
    out << "                <strong>Potential GPU Bottleneck:</strong> Your "
           "GPU is hitting high usage for short periods. ";
    out << "Consider lowering some graphics settings for more consistent "
           "performance.\n";
    out << "            </div>\n";
  }

  // RAM usage warning
  if (summary.ramUsageWarning) {
    hasWarnings = true;
    out << "            <div class=\"warning-item warning-severe\">\n";
    out << "                <strong>High Memory Usage:</strong> Your system is "
           "running low on available RAM. ";
    out << "This can cause performance issues and stuttering. Consider closing "
           "background applications.\n";
    out << "            </div>\n";
  }

  // VRAM usage warning
  if (summary.vramUsageWarning) {
    hasWarnings = true;
    out << "            <div class=\"warning-item warning-moderate\">\n";
    out << "                <strong>High VRAM Usage:</strong> Your GPU is "
           "using >85% of available VRAM. ";
    out << "Consider lowering texture quality settings, especially mipmap "
           "levels.\n";
    out << "            </div>\n";
  }

  // Stuttering warning
  if (summary.fpsStutteringDetected) {
    hasWarnings = true;
    out << "            <div class=\"warning-item warning-moderate\">\n";
    out << "                <strong>FPS Stuttering Detected:</strong> High "
           "frame time variance may be causing ";
    out << "perceptible stuttering during gameplay.\n";
    out << "            </div>\n";
  }

  // Frame freeze info
  if (summary.fpsFreezeCount > 0 || summary.smallFreezeCount > 0) {
    hasWarnings = true;
    out << "            <div class=\"warning-item warning-info\">\n";
    out << "                <strong>Frame Freezes:</strong> ";
    if (summary.fpsFreezeCount > 0) {
      out << summary.fpsFreezeCount << " severe freezes detected (>100ms). ";
    }
    if (summary.smallFreezeCount > 0) {
      out << summary.smallFreezeCount << " minor hitches detected (>50ms).";
    }
    out << "\n";
    out << "            </div>\n";
  }

  if (!hasWarnings) {
    out << "            <div class=\"warning-item warning-info\">\n";
    out << "                <strong>Good Performance:</strong> No significant "
           "performance issues detected.\n";
    out << "            </div>\n";
  }

  out << "        </div>\n";
  out << "    </div>\n";

  // Section Performance
  out << "    <h2>Section Performance</h2>\n";
  out << "    <div class=\"section-cards\">\n";

  // Beach section card
  out << "        <div class=\"section-card\">\n";
  out << "            <h3 class=\"section-title\">"
      << BenchmarkSummary::beachLabel << "</h3>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">Average FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.beachAvgFps > 80)
    out << " good";
  else if (summary.beachAvgFps > 50)
    out << " average";
  else if (summary.beachAvgFps > 0)
    out << " poor";
  out << "\">"
      << (summary.beachAvgFps > 0 ? QString::number(summary.beachAvgFps, 'f', 1)
                                  : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">1% Low FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.beach1LowFps > 60)
    out << " good";
  else if (summary.beach1LowFps > 30)
    out << " average";
  else if (summary.beach1LowFps > 0)
    out << " poor";
  out << "\">"
      << (summary.beach1LowFps > 0
            ? QString::number(summary.beach1LowFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">5% Low FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.beach5LowFps > 70)
    out << " good";
  else if (summary.beach5LowFps > 40)
    out << " average";
  else if (summary.beach5LowFps > 0)
    out << " poor";
  out << "\">"
      << (summary.beach5LowFps > 0
            ? QString::number(summary.beach5LowFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "        </div>\n";

  // Jungle section card (using flying data but jungle label)
  out << "        <div class=\"section-card\">\n";
  out << "            <h3 class=\"section-title\">"
      << BenchmarkSummary::jungleLabel << "</h3>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">Average FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.flyingAvgFps > 80)
    out << " good";
  else if (summary.flyingAvgFps > 50)
    out << " average";
  else if (summary.flyingAvgFps > 0)
    out << " poor";
  out << "\">"
      << (summary.flyingAvgFps > 0
            ? QString::number(summary.flyingAvgFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">1% Low FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.flying1LowFps > 60)
    out << " good";
  else if (summary.flying1LowFps > 30)
    out << " average";
  else if (summary.flying1LowFps > 0)
    out << " poor";
  out << "\">"
      << (summary.flying1LowFps > 0
            ? QString::number(summary.flying1LowFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">5% Low FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.flying5LowFps > 70)
    out << " good";
  else if (summary.flying5LowFps > 40)
    out << " average";
  else if (summary.flying5LowFps > 0)
    out << " poor";
  out << "\">"
      << (summary.flying5LowFps > 0
            ? QString::number(summary.flying5LowFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "        </div>\n";

  // Outpost section card
  out << "        <div class=\"section-card\">\n";
  out << "            <h3 class=\"section-title\">"
      << BenchmarkSummary::outpostLabel << "</h3>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">Average FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.outpostAvgFps > 80)
    out << " good";
  else if (summary.outpostAvgFps > 50)
    out << " average";
  else if (summary.outpostAvgFps > 0)
    out << " poor";
  out << "\">"
      << (summary.outpostAvgFps > 0
            ? QString::number(summary.outpostAvgFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">1% Low FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.outpost1LowFps > 60)
    out << " good";
  else if (summary.outpost1LowFps > 30)
    out << " average";
  else if (summary.outpost1LowFps > 0)
    out << " poor";
  out << "\">"
      << (summary.outpost1LowFps > 0
            ? QString::number(summary.outpost1LowFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "            <div class=\"metric\">\n";
  out << "                <div class=\"metric-name\">5% Low FPS</div>\n";
  out << "                <div class=\"metric-value";
  if (summary.outpost5LowFps > 70)
    out << " good";
  else if (summary.outpost5LowFps > 40)
    out << " average";
  else if (summary.outpost5LowFps > 0)
    out << " poor";
  out << "\">"
      << (summary.outpost5LowFps > 0
            ? QString::number(summary.outpost5LowFps, 'f', 1)
            : "N/A")
      << "<span class=\"metric-unit\"> FPS</span></div>\n";
  out << "            </div>\n";

  out << "        </div>\n";

  out << "    </div>\n";

  // Footer
  out << "    <footer>\n";
  out << "        <p>Generated by checkmark benchmark tool</p>\n";
  out << "    </footer>\n";

  out << "</body>\n";
  out << "</html>\n";

  outFile.close();

  return outputFilePath;
}
