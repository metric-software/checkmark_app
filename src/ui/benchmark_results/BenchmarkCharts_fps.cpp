#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "BenchmarkCharts.h"
#include "../../logging/Logger.h"

QString BenchmarkCharts::generateFpsChart(
  const QString& csvFilePath, const QString& comparisonCsvFilePath) {
  // Parse FPS data from the CSV
  QFile file(csvFilePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open CSV file: [path hidden for privacy]";
    return "";
  }

  QTextStream in(&file);
  QString header = in.readLine();
  QStringList headers = header.split(",");

  int fpsIndex = headers.indexOf("FPS");
  if (fpsIndex < 0) {
    LOG_WARN << "FPS column not found in CSV";
    file.close();
    return "";
  }

  // Collect FPS data points
  QVector<QPointF> fpsData;
  int timeCounter = 0;

  while (!in.atEnd()) {
    QString line = in.readLine();
    QStringList fields = line.split(",");

    if (fields.size() <= fpsIndex) continue;

    bool ok;
    double fps = fields[fpsIndex].toDouble(&ok);
    if (ok && fps > 0) {
      fpsData.append(QPointF(timeCounter++, fps));
    }
  }

  file.close();

  // Create dataset container
  QVector<QVector<QPointF>> datasets;
  datasets.append(fpsData);

  // Create label
  QStringList labels = {"FPS"};

  // Check if comparison data is provided
  if (comparisonCsvFilePath.isEmpty()) {
    // Generate single dataset chart
    return generateHtmlChart("fps_chart", "FPS Over Time", "Time (sample)",
                             "FPS", labels, datasets,
                             YAxisScaleType::Automatic);
  } else {
    // Parse comparison FPS data
    QFile compFile(comparisonCsvFilePath);
    if (!compFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      LOG_ERROR << "Failed to open comparison CSV file: "
                << comparisonCsvFilePath.toStdString();
      // Fall back to non-comparison chart
      return generateHtmlChart("fps_chart", "FPS Over Time", "Time (sample)",
                               "FPS", labels, datasets,
                               YAxisScaleType::Automatic);
    }

    QTextStream compIn(&compFile);
    QString compHeader = compIn.readLine();
    QStringList compHeaders = compHeader.split(",");

    int compFpsIndex = compHeaders.indexOf("FPS");
    if (compFpsIndex < 0) {
      LOG_WARN << "FPS column not found in comparison CSV";
      compFile.close();
      // Fall back to non-comparison chart
      return generateHtmlChart("fps_chart", "FPS Over Time", "Time (sample)",
                               "FPS", labels, datasets,
                               YAxisScaleType::Automatic);
    }

    // Collect comparison FPS data points
    QVector<QPointF> compFpsData;
    int compTimeCounter = 0;

    while (!compIn.atEnd()) {
      QString line = compIn.readLine();
      QStringList fields = line.split(",");

      if (fields.size() <= compFpsIndex) continue;

      bool ok;
      double fps = fields[compFpsIndex].toDouble(&ok);
      if (ok && fps > 0) {
        compFpsData.append(QPointF(compTimeCounter++, fps));
      }
    }

    compFile.close();

    // Create comparison dataset container
    QVector<QVector<QPointF>> compDatasets;
    compDatasets.append(compFpsData);

    // Generate comparison chart
    return generateHtmlChartWithComparison(
      "fps_chart", "FPS Over Time", "Time (sample)", "FPS", labels, datasets,
      compDatasets, YAxisScaleType::Automatic);
  }
}
