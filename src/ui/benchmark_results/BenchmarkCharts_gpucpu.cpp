#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "BenchmarkCharts.h"
#include "../../logging/Logger.h"

QString BenchmarkCharts::generateGpuCpuUsageChart(
  const QString& csvFilePath, const QString& comparisonCsvFilePath) {

  // Parse data for GPU/CPU usage and FPS
  QFile file(csvFilePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open CSV file: [path hidden for privacy]";
    return "";
  }

  QTextStream in(&file);
  QString header = in.readLine();
  QStringList headers = header.split(",");

  int fpsIndex = headers.indexOf("FPS");
  int gpuUsageIndex = headers.indexOf("GPU Usage");

  // Find CPU core usage columns
  QVector<int> coreIndices;
  QRegularExpression corePattern("^Core\\s+\\d+\\s+\\(%\\)$");

  for (int i = 0; i < headers.size(); i++) {
    if (corePattern.match(headers[i].trimmed()).hasMatch()) {
      coreIndices.append(i);
    }
  }

  if (gpuUsageIndex < 0 || fpsIndex < 0 || coreIndices.isEmpty()) {
    LOG_WARN << "Required columns not found in CSV";
    file.close();
    return "";
  }

  // Collect data points
  QVector<QPointF> fpsData;
  QVector<QPointF> gpuUsageData;
  QVector<QPointF> maxCpuUsageData;

  int timeCounter = 0;

  while (!in.atEnd()) {
    QString line = in.readLine();
    QStringList fields = line.split(",");

    int maxField =
      std::max({fpsIndex, gpuUsageIndex,
                *std::max_element(coreIndices.begin(), coreIndices.end())});
    if (fields.size() <= maxField) continue;

    // Extract FPS
    bool fpsOk;
    double fps = fields[fpsIndex].toDouble(&fpsOk);

    // Extract GPU Usage
    bool gpuOk;
    double gpuUsage = fields[gpuUsageIndex].toDouble(&gpuOk);

    // Find max CPU core usage
    double maxCpuUsage = 0.0;
    bool anyCpuValueOk = false;

    for (int coreIdx : coreIndices) {
      bool cpuOk;
      double cpuUsage = fields[coreIdx].toDouble(&cpuOk);
      if (cpuOk && cpuUsage > 0) {
        maxCpuUsage = std::max(maxCpuUsage, cpuUsage);
        anyCpuValueOk = true;
      }
    }

    // Add data points
    if (fpsOk && fps > 0) {
      fpsData.append(QPointF(timeCounter, fps));
    }

    if (gpuOk && gpuUsage >= 0) {
      gpuUsageData.append(QPointF(timeCounter, gpuUsage));
    }

    if (anyCpuValueOk) {
      maxCpuUsageData.append(QPointF(timeCounter, maxCpuUsage));
    }

    timeCounter++;
  }

  file.close();

  // Create datasets container
  QVector<QVector<QPointF>> datasets;
  datasets.append(fpsData);
  datasets.append(gpuUsageData);
  datasets.append(maxCpuUsageData);

  // Create labels
  QStringList labels = {"FPS", "GPU Usage (%)", "Max CPU Core Usage (%)"};

  // Check if comparison data is provided
  if (comparisonCsvFilePath.isEmpty()) {
    // Generate single dataset chart
    return generateHtmlChart(
      "gpu_cpu_chart", "GPU vs CPU Usage (With FPS Overlay)", "Time (sample)",
      "Usage/FPS", labels, datasets, YAxisScaleType::Automatic);
  } else {
    // Parse comparison data
    QFile compFile(comparisonCsvFilePath);
    if (!compFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      LOG_ERROR << "Failed to open comparison CSV file: "
                << comparisonCsvFilePath.toStdString();
      // Fall back to non-comparison chart
      return generateHtmlChart(
        "gpu_cpu_chart", "GPU vs CPU Usage (With FPS Overlay)", "Time (sample)",
        "Usage/FPS", labels, datasets, YAxisScaleType::Automatic);
    }

    QTextStream compIn(&compFile);
    QString compHeader = compIn.readLine();
    QStringList compHeaders = compHeader.split(",");

    int compFpsIndex = compHeaders.indexOf("FPS");
    int compGpuUsageIndex = compHeaders.indexOf("GPU Usage");

    // Find CPU core usage columns in comparison file
    QVector<int> compCoreIndices;

    for (int i = 0; i < compHeaders.size(); i++) {
      if (corePattern.match(compHeaders[i].trimmed()).hasMatch()) {
        compCoreIndices.append(i);
      }
    }

    if (compGpuUsageIndex < 0 || compFpsIndex < 0 ||
        compCoreIndices.isEmpty()) {
      LOG_WARN << "Required columns not found in comparison CSV";
      compFile.close();
      // Fall back to non-comparison chart
      return generateHtmlChart(
        "gpu_cpu_chart", "GPU vs CPU Usage (With FPS Overlay)", "Time (sample)",
        "Usage/FPS", labels, datasets, YAxisScaleType::Automatic);
    }

    // Collect comparison data points
    QVector<QPointF> compFpsData;
    QVector<QPointF> compGpuUsageData;
    QVector<QPointF> compMaxCpuUsageData;

    int compTimeCounter = 0;

    while (!compIn.atEnd()) {
      QString line = compIn.readLine();
      QStringList fields = line.split(",");

      int maxField = std::max(
        {compFpsIndex, compGpuUsageIndex,
         *std::max_element(compCoreIndices.begin(), compCoreIndices.end())});
      if (fields.size() <= maxField) continue;

      // Extract FPS
      bool fpsOk;
      double fps = fields[compFpsIndex].toDouble(&fpsOk);

      // Extract GPU Usage
      bool gpuOk;
      double gpuUsage = fields[compGpuUsageIndex].toDouble(&gpuOk);

      // Find max CPU core usage
      double maxCpuUsage = 0.0;
      bool anyCpuValueOk = false;

      for (int coreIdx : compCoreIndices) {
        bool cpuOk;
        double cpuUsage = fields[coreIdx].toDouble(&cpuOk);
        if (cpuOk && cpuUsage > 0) {
          maxCpuUsage = std::max(maxCpuUsage, cpuUsage);
          anyCpuValueOk = true;
        }
      }

      // Add data points
      if (fpsOk && fps > 0) {
        compFpsData.append(QPointF(compTimeCounter, fps));
      }

      if (gpuOk && gpuUsage >= 0) {
        compGpuUsageData.append(QPointF(compTimeCounter, gpuUsage));
      }

      if (anyCpuValueOk) {
        compMaxCpuUsageData.append(QPointF(compTimeCounter, maxCpuUsage));
      }

      compTimeCounter++;
    }

    compFile.close();

    // Create comparison datasets container
    QVector<QVector<QPointF>> compDatasets;
    compDatasets.append(compFpsData);
    compDatasets.append(compGpuUsageData);
    compDatasets.append(compMaxCpuUsageData);

    // Generate comparison chart
    return generateHtmlChartWithComparison(
      "gpu_cpu_chart", "GPU vs CPU Usage (With FPS Overlay)", "Time (sample)",
      "Usage/FPS", labels, datasets, compDatasets, YAxisScaleType::Automatic);
  }
}
