#include "BenchmarkCharts.h"

bool BenchmarkCharts::ensureOutputDirExists(QDir& outputDir) {
  if (!outputDir.exists()) {
    if (!outputDir.mkpath(".")) {
      std::cout << "Failed to create html_reports directory" << std::endl;
      return false;
    }
  }
  return true;
}

QString BenchmarkCharts::getDatasetOptionsJson(bool isComparison) {
  if (isComparison) {
    // Comparison dataset styling (red, dashed lines)
    return R"(
            borderColor: 'rgba(255, 99, 132, 1)',
            backgroundColor: 'rgba(255, 99, 132, 0.2)',
            borderDash: [5, 5],
            borderWidth: 2,
            pointRadius: 0,
            pointHoverRadius: 4,
            tension: 0.1
        )";
  } else {
    // Primary dataset styling (blue, solid lines)
    return R"(
            borderColor: 'rgba(54, 162, 235, 1)',
            backgroundColor: 'rgba(54, 162, 235, 0.2)',
            borderWidth: 2,
            pointRadius: 0,
            pointHoverRadius: 4,
            tension: 0.1
        )";
  }
}

QString BenchmarkCharts::processComparisonData(const QString& dataColumn,
                                               const QString& csvFilePath,
                                               bool includeLowPercentiles) {
  QFile file(csvFilePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return "";  // Empty string if we can't open the file
  }

  QTextStream in(&file);
  QString header = in.readLine();
  QStringList headers = header.split(",");

  int dataIndex = headers.indexOf(dataColumn);
  if (dataIndex < 0) {
    file.close();
    return "";  // Column not found
  }

  QJsonArray dataPoints;
  int timeCounter = 0;

  while (!in.atEnd()) {
    QString line = in.readLine();
    QStringList fields = line.split(",");

    if (fields.size() <= dataIndex) continue;

    bool ok;
    double value = fields[dataIndex].toDouble(&ok);
    if (ok) {
      QJsonObject point;
      point["x"] = timeCounter++;
      point["y"] = value;
      dataPoints.append(point);
    }
  }

  file.close();

  QJsonDocument doc(dataPoints);
  return doc.toJson(QJsonDocument::Compact);
}

// Removed duplicate definition of calculateBenchmarkSummary,
// as it is defined in BenchmarkCharts_summary.cpp
