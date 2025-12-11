#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "BenchmarkCharts.h"
#include "../../logging/Logger.h"

QString BenchmarkCharts::generateFrameTimeMetricsChart(
  const QString& csvFilePath, const QString& comparisonCsvFilePath) {
  // Parse frame time data from the CSV
  QFile file(csvFilePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open CSV file: [path hidden for privacy]";
    return "";
  }

  QTextStream in(&file);
  QString header = in.readLine();
  QStringList headers = header.split(",");

  int frameTimeIndex = headers.indexOf("Frame Time");
  int highestFrameTimeIndex = headers.indexOf("Highest Frame Time");

  if (frameTimeIndex < 0) {
    LOG_WARN << "Frame Time column not found in CSV";
    file.close();
    return "";
  }

  // Collect frame time data points
  QVector<QPointF> frameTimeData;
  QVector<QPointF> highestFrameTimeData;

  int timeCounter = 0;

  while (!in.atEnd()) {
    QString line = in.readLine();
    QStringList fields = line.split(",");

    if (fields.size() <= std::max(frameTimeIndex, highestFrameTimeIndex))
      continue;

    bool okFrameTime;
    double frameTime = fields[frameTimeIndex].toDouble(&okFrameTime);

    if (okFrameTime && frameTime > 0) {
      frameTimeData.append(QPointF(timeCounter, frameTime));

      // Add highest frame time if available in CSV
      if (highestFrameTimeIndex >= 0) {
        bool okHighestFrameTime;
        double highestFrameTime =
          fields[highestFrameTimeIndex].toDouble(&okHighestFrameTime);

        if (okHighestFrameTime && highestFrameTime > 0) {
          highestFrameTimeData.append(QPointF(timeCounter, highestFrameTime));
        } else {
          // Fallback to using frame time if highest frame time is invalid
          highestFrameTimeData.append(QPointF(timeCounter, frameTime));
        }
      } else {
        // If Highest Frame Time column doesn't exist, use the frame time
        highestFrameTimeData.append(QPointF(timeCounter, frameTime));
      }

      timeCounter++;
    }
  }

  file.close();

  // Create dataset container
  QVector<QVector<QPointF>> datasets;
  datasets.append(frameTimeData);
  datasets.append(highestFrameTimeData);

  // Create labels
  QStringList labels = {"Frame Time", "Highest Frame Time"};

  // Check if comparison data is provided
  if (comparisonCsvFilePath.isEmpty()) {
    // Generate single dataset chart
    return generateHtmlChart("frame_time_chart", "Frame Time Distribution",
                             "Time (sample)", "Frame Time (ms)", labels,
                             datasets, YAxisScaleType::Automatic);
  } else {
    // Parse comparison frame time data
    QFile compFile(comparisonCsvFilePath);
    if (!compFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      LOG_ERROR << "Failed to open comparison CSV file: "
                << comparisonCsvFilePath.toStdString();
      // Fall back to non-comparison chart
      return generateHtmlChart("frame_time_chart", "Frame Time Distribution",
                               "Time (sample)", "Frame Time (ms)", labels,
                               datasets, YAxisScaleType::Automatic);
    }

    QTextStream compIn(&compFile);
    QString compHeader = compIn.readLine();
    QStringList compHeaders = compHeader.split(",");

    int compFrameTimeIndex = compHeaders.indexOf("Frame Time");
    int compHighestFrameTimeIndex = compHeaders.indexOf("Highest Frame Time");

    if (compFrameTimeIndex < 0) {
      LOG_WARN << "Frame Time column not found in comparison CSV";
      compFile.close();
      // Fall back to non-comparison chart
      return generateHtmlChart("frame_time_chart", "Frame Time Distribution",
                               "Time (sample)", "Frame Time (ms)", labels,
                               datasets, YAxisScaleType::Automatic);
    }

    // Collect comparison frame time data points
    QVector<QPointF> compFrameTimeData;
    QVector<QPointF> compHighestFrameTimeData;

    int compTimeCounter = 0;

    while (!compIn.atEnd()) {
      QString line = compIn.readLine();
      QStringList fields = line.split(",");

      if (fields.size() <=
          std::max(compFrameTimeIndex, compHighestFrameTimeIndex))
        continue;

      bool okFrameTime;
      double frameTime = fields[compFrameTimeIndex].toDouble(&okFrameTime);

      if (okFrameTime && frameTime > 0) {
        compFrameTimeData.append(QPointF(compTimeCounter, frameTime));

        // Add highest frame time if available in CSV
        if (compHighestFrameTimeIndex >= 0) {
          bool okHighestFrameTime;
          double highestFrameTime =
            fields[compHighestFrameTimeIndex].toDouble(&okHighestFrameTime);

          if (okHighestFrameTime && highestFrameTime > 0) {
            compHighestFrameTimeData.append(
              QPointF(compTimeCounter, highestFrameTime));
          } else {
            // Fallback to using frame time if highest frame time is invalid
            compHighestFrameTimeData.append(
              QPointF(compTimeCounter, frameTime));
          }
        } else {
          // If Highest Frame Time column doesn't exist, use the frame time
          compHighestFrameTimeData.append(QPointF(compTimeCounter, frameTime));
        }

        compTimeCounter++;
      }
    }

    compFile.close();

    // Create comparison dataset container
    QVector<QVector<QPointF>> compDatasets;
    compDatasets.append(compFrameTimeData);
    compDatasets.append(compHighestFrameTimeData);

    // Generate comparison chart
    return generateHtmlChartWithComparison(
      "frame_time_chart", "Frame Time Distribution", "Time (sample)",
      "Frame Time (ms)", labels, datasets, compDatasets,
      YAxisScaleType::Automatic);
  }
}
