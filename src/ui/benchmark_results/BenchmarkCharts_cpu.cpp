#include <algorithm>

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "BenchmarkCharts.h"
#include "../../logging/Logger.h"

QString BenchmarkCharts::generateCpuUsageChart(
  const QString& csvFilePath, const QString& comparisonCsvFilePath) {
  // Parse CPU core usage data from the CSV
  QFile file(csvFilePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open CSV file: [path hidden for privacy]";
    return "";
  }

  QTextStream in(&file);
  QString header = in.readLine();
  QStringList headers = header.split(",");

  // Find all CPU core usage columns using regex to avoid capturing clock metrics.
  // Matches current headers "PDH_Core 0 CPU (%)" and legacy "Core 0 (%)".
  QVector<int> coreIndices;
  QRegularExpression corePattern(
    R"(^\s*(PDH_)?Core\s+\d+\s+CPU?\s*\(%\)\s*$)",
    QRegularExpression::CaseInsensitiveOption);

  // Optional total CPU usage column
  int totalCpuIndex = headers.indexOf("PDH_CPU_Usage(%)");

  for (int i = 0; i < headers.size(); i++) {
    if (corePattern.match(headers[i].trimmed()).hasMatch()) {
      coreIndices.append(i);
    }
  }

  if (coreIndices.isEmpty()) {
    LOG_WARN << "No CPU core usage columns found in CSV";
    file.close();
    return "";
  }

  // Prepare data structures for CPU core usage
  QVector<QVector<double>> coreData(coreIndices.size());
  QVector<QPointF> maxCoreUsageData;
  QVector<QPointF> avgCoreUsageData;
  QVector<QPointF> totalCpuUsageData;

  int timeCounter = 0;

  while (!in.atEnd()) {
    QString line = in.readLine();
    QStringList fields = line.split(",");

    if (fields.size() <=
        *std::max_element(coreIndices.begin(), coreIndices.end()))
      continue;

    // Process each core's data point
    double totalUsage = 0;
    double maxUsage = 0;
    int validCores = 0;

    for (int i = 0; i < coreIndices.size(); i++) {
      int idx = coreIndices[i];
      bool ok;
      double usage = fields[idx].toDouble(&ok);

      if (ok && usage >= 0) {
        coreData[i].append(usage);
        totalUsage += usage;
        maxUsage = std::max(maxUsage, usage);
        validCores++;
      }
    }

    // Calculate average and add data points
    if (validCores > 0) {
      double avgUsage = totalUsage / validCores;
      maxCoreUsageData.append(QPointF(timeCounter, maxUsage));
      avgCoreUsageData.append(QPointF(timeCounter, avgUsage));
    }

    // Total CPU usage (if present)
    if (totalCpuIndex >= 0 && totalCpuIndex < fields.size()) {
      bool ok = false;
      double total = fields[totalCpuIndex].toDouble(&ok);
      if (ok && total >= 0) {
        totalCpuUsageData.append(QPointF(timeCounter, total));
      }
    }

    timeCounter++;
  }

  file.close();

  // Create datasets container
  QVector<QVector<QPointF>> datasets;
  QStringList labels;

  if (!totalCpuUsageData.isEmpty()) {
    datasets.append(totalCpuUsageData);
    labels.append("Total CPU Usage (%)");
  }

  datasets.append(maxCoreUsageData);
  datasets.append(avgCoreUsageData);
  labels << "Highest CPU Core Usage (%)" << "Avg CPU Usage (%)";

  if (datasets.isEmpty()) {
    LOG_WARN << "No CPU usage data found";
    return "";
  }

  // Check if comparison data is provided
  if (comparisonCsvFilePath.isEmpty()) {
    // Generate single dataset chart
    return generateHtmlChart("cpu_usage_chart", "CPU Core Usage Over Time",
                             "Time (sample)", "CPU Usage (%)", labels, datasets,
                             YAxisScaleType::Fixed_0_to_100);
  } else {
    // Parse comparison CPU core usage data
    QFile compFile(comparisonCsvFilePath);
    if (!compFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      LOG_ERROR << "Failed to open comparison CSV file: "
                << comparisonCsvFilePath.toStdString();
      // Fall back to non-comparison chart
      return generateHtmlChart("cpu_usage_chart", "CPU Core Usage Over Time",
                               "Time (sample)", "CPU Usage (%)", labels,
                               datasets, YAxisScaleType::Fixed_0_to_100);
    }

    QTextStream compIn(&compFile);
    QString compHeader = compIn.readLine();
    QStringList compHeaders = compHeader.split(",");

    // Find all CPU core usage columns in comparison data
    QVector<int> compCoreIndices;
    int compTotalCpuIndex = compHeaders.indexOf("PDH_CPU_Usage(%)");

    for (int i = 0; i < compHeaders.size(); i++) {
      if (corePattern.match(compHeaders[i].trimmed()).hasMatch()) {
        compCoreIndices.append(i);
      }
    }

    if (compCoreIndices.isEmpty() && compTotalCpuIndex < 0) {
      LOG_WARN << "No CPU core usage columns found in comparison CSV";
      compFile.close();
      // Fall back to non-comparison chart
      return generateHtmlChart("cpu_usage_chart", "CPU Core Usage Over Time",
                               "Time (sample)", "CPU Usage (%)", labels,
                               datasets, YAxisScaleType::Fixed_0_to_100);
    }

    // Prepare data structures for comparison CPU core usage
    QVector<QVector<double>> compCoreData(compCoreIndices.size());
    QVector<QPointF> compMaxCoreUsageData;
    QVector<QPointF> compAvgCoreUsageData;
    QVector<QPointF> compTotalCpuUsageData;

    int compTimeCounter = 0;

    while (!compIn.atEnd()) {
      QString line = compIn.readLine();
      QStringList fields = line.split(",");

      if (fields.size() <=
          *std::max_element(compCoreIndices.begin(), compCoreIndices.end()))
        continue;

      // Process each core's data point in comparison data
      double totalUsage = 0;
      double maxUsage = 0;
      int validCores = 0;

      for (int i = 0; i < compCoreIndices.size(); i++) {
        int idx = compCoreIndices[i];
        bool ok;
        double usage = fields[idx].toDouble(&ok);

        if (ok && usage >= 0) {
          compCoreData[i].append(usage);
          totalUsage += usage;
          maxUsage = std::max(maxUsage, usage);
          validCores++;
        }
      }

      // Calculate average and add data points for comparison data
      if (validCores > 0) {
        double avgUsage = totalUsage / validCores;
        compMaxCoreUsageData.append(QPointF(compTimeCounter, maxUsage));
        compAvgCoreUsageData.append(QPointF(compTimeCounter, avgUsage));
      }

      // Total CPU usage (if present)
      if (compTotalCpuIndex >= 0 && compTotalCpuIndex < fields.size()) {
        bool ok = false;
        double total = fields[compTotalCpuIndex].toDouble(&ok);
        if (ok && total >= 0) {
          compTotalCpuUsageData.append(QPointF(compTimeCounter, total));
        }
      }

      compTimeCounter++;
    }

    compFile.close();

    // Create comparison datasets container
    QVector<QVector<QPointF>> compDatasets;
    QStringList compLabels;

    if (!compTotalCpuUsageData.isEmpty()) {
      compDatasets.append(compTotalCpuUsageData);
      compLabels.append("Total CPU Usage (%)");
    }

    compDatasets.append(compMaxCoreUsageData);
    compDatasets.append(compAvgCoreUsageData);
    compLabels << "Highest CPU Core Usage (%)" << "Avg CPU Usage (%)";

    // Generate comparison chart
    return generateHtmlChartWithComparison(
      "cpu_usage_chart", "CPU Core Usage Over Time", "Time (sample)",
      "CPU Usage (%)", labels, datasets, compDatasets,
      YAxisScaleType::Fixed_0_to_100);
  }
}
