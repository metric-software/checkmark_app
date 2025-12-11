#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "BenchmarkCharts.h"
#include "../../logging/Logger.h"

QString BenchmarkCharts::generateGpuUsageChart(
  const QString& csvFilePath, const QString& comparisonCsvFilePath) {
  // Parse GPU usage and memory data from the CSV
  QFile file(csvFilePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open CSV file: [path hidden for privacy]";
    return "";
  }

  QTextStream in(&file);
  QString header = in.readLine();
  QStringList headers = header.split(",");

  int gpuUsageIndex = headers.indexOf("GPU Usage");
  int gpuMemUsedIndex = headers.indexOf("GPU Mem Used");
  int gpuMemTotalIndex = headers.indexOf("GPU Mem Total");

  if (gpuUsageIndex < 0 || gpuMemUsedIndex < 0 || gpuMemTotalIndex < 0) {
    LOG_WARN << "Required GPU columns not found in CSV";
    file.close();
    return "";
  }

  // Collect GPU data points
  QVector<QPointF> gpuUsageData;
  QVector<QPointF> gpuMemUsageData;

  int timeCounter = 0;

  while (!in.atEnd()) {
    QString line = in.readLine();
    QStringList fields = line.split(",");

    if (fields.size() <=
        std::max({gpuUsageIndex, gpuMemUsedIndex, gpuMemTotalIndex}))
      continue;

    // Process GPU Usage
    bool usageOk;
    double gpuUsage = fields[gpuUsageIndex].toDouble(&usageOk);

    // Process GPU Memory
    bool memUsedOk, memTotalOk;
    double gpuMemUsed = fields[gpuMemUsedIndex].toDouble(&memUsedOk);
    double gpuMemTotal = fields[gpuMemTotalIndex].toDouble(&memTotalOk);

    if (usageOk && gpuUsage >= 0) {
      gpuUsageData.append(QPointF(timeCounter, gpuUsage));
    }

    if (memUsedOk && memTotalOk && gpuMemTotal > 0) {
      // Calculate memory usage as a percentage
      double memUsagePercent = (gpuMemUsed / gpuMemTotal) * 100.0;
      gpuMemUsageData.append(QPointF(timeCounter, memUsagePercent));
    }

    timeCounter++;
  }

  file.close();

  // Create datasets container
  QVector<QVector<QPointF>> datasets;
  datasets.append(gpuUsageData);
  datasets.append(gpuMemUsageData);

  // Create labels
  QStringList labels = {"GPU Usage (%)", "GPU Memory Usage (%)"};

  // Check if comparison data is provided
  if (comparisonCsvFilePath.isEmpty()) {
    // Generate single dataset chart
    return generateHtmlChart("gpu_usage_chart", "GPU Metrics Over Time",
                             "Time (sample)", "Usage (%)", labels, datasets,
                             YAxisScaleType::Fixed_0_to_100);
  } else {
    // Parse comparison GPU data
    QFile compFile(comparisonCsvFilePath);
    if (!compFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      LOG_ERROR << "Failed to open comparison CSV file: "
                << comparisonCsvFilePath.toStdString();
      // Fall back to non-comparison chart
      return generateHtmlChart("gpu_usage_chart", "GPU Metrics Over Time",
                               "Time (sample)", "Usage (%)", labels, datasets,
                               YAxisScaleType::Fixed_0_to_100);
    }

    QTextStream compIn(&compFile);
    QString compHeader = compIn.readLine();
    QStringList compHeaders = compHeader.split(",");

    int compGpuUsageIndex = compHeaders.indexOf("GPU Usage");
    int compGpuMemUsedIndex = compHeaders.indexOf("GPU Mem Used");
    int compGpuMemTotalIndex = compHeaders.indexOf("GPU Mem Total");

    if (compGpuUsageIndex < 0 || compGpuMemUsedIndex < 0 ||
        compGpuMemTotalIndex < 0) {
      LOG_WARN << "Required GPU columns not found in comparison CSV";
      compFile.close();
      // Fall back to non-comparison chart
      return generateHtmlChart("gpu_usage_chart", "GPU Metrics Over Time",
                               "Time (sample)", "Usage (%)", labels, datasets,
                               YAxisScaleType::Fixed_0_to_100);
    }

    // Collect comparison GPU data points
    QVector<QPointF> compGpuUsageData;
    QVector<QPointF> compGpuMemUsageData;

    int compTimeCounter = 0;

    while (!compIn.atEnd()) {
      QString line = compIn.readLine();
      QStringList fields = line.split(",");

      if (fields.size() <= std::max({compGpuUsageIndex, compGpuMemUsedIndex,
                                     compGpuMemTotalIndex}))
        continue;

      // Process GPU Usage
      bool usageOk;
      double gpuUsage = fields[compGpuUsageIndex].toDouble(&usageOk);

      // Process GPU Memory
      bool memUsedOk, memTotalOk;
      double gpuMemUsed = fields[compGpuMemUsedIndex].toDouble(&memUsedOk);
      double gpuMemTotal = fields[compGpuMemTotalIndex].toDouble(&memTotalOk);

      if (usageOk && gpuUsage >= 0) {
        compGpuUsageData.append(QPointF(compTimeCounter, gpuUsage));
      }

      if (memUsedOk && memTotalOk && gpuMemTotal > 0) {
        // Calculate memory usage as a percentage
        double memUsagePercent = (gpuMemUsed / gpuMemTotal) * 100.0;
        compGpuMemUsageData.append(QPointF(compTimeCounter, memUsagePercent));
      }

      compTimeCounter++;
    }

    compFile.close();

    // Create comparison datasets container
    QVector<QVector<QPointF>> compDatasets;
    compDatasets.append(compGpuUsageData);
    compDatasets.append(compGpuMemUsageData);

    // Generate comparison chart
    return generateHtmlChartWithComparison(
      "gpu_usage_chart", "GPU Metrics Over Time", "Time (sample)", "Usage (%)",
      labels, datasets, compDatasets, YAxisScaleType::Fixed_0_to_100);
  }
}
