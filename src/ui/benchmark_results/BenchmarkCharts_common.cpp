#include <algorithm>
#include <numeric>

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include "BenchmarkCharts.h"
#include "../../logging/Logger.h"

// Enum for Y-axis scaling options
QString BenchmarkCharts::getYScaleOptionsJson(YAxisScaleType scaleType,
                                              double minValue,
                                              double maxValue) {
  return "";  // use auto-scaling for now.
}

// Helper function to generate random colors for multiple datasets
QStringList BenchmarkCharts::generateRandomColors(int count) {
  QStringList colors;
  for (int i = 0; i < count; i++) {
    int r = QRandomGenerator::global()->bounded(100, 256);
    int g = QRandomGenerator::global()->bounded(100, 256);
    int b = QRandomGenerator::global()->bounded(100, 256);
    colors.append(QString("rgb(%1, %2, %3)").arg(r).arg(g).arg(b));
  }
  return colors;
}

// Helper function to get line style for different metrics in comparison mode
QString BenchmarkCharts::getLineStyleOptions(int metricIndex,
                                             bool isComparison) {
  // Colors for primary dataset
  const QStringList primaryColors = {
    "rgb(54, 162, 235)",   // Blue
    "rgb(75, 192, 192)",   // Teal
    "rgb(153, 102, 255)",  // Purple
    "rgb(255, 159, 64)"    // Orange
  };

  // Colors for comparison dataset
  const QStringList comparisonColors = {
    "rgb(255, 99, 132)",  // Red
    "rgb(255, 206, 86)",  // Yellow
    "rgb(255, 99, 132)",  // Pink
    "rgb(231, 76, 60)"    // Crimson
  };

  // Line styles for different metrics
  const QStringList lineStyles = {
    "[]",            // Solid (no dash)
    "[5, 5]",        // Dashed
    "[2, 2]",        // Dotted
    "[15, 3, 3, 3]"  // Dash-dot
  };

  // Pick color based on whether it's comparison data and the metric index
  QString color = isComparison
                    ? comparisonColors[metricIndex % comparisonColors.size()]
                    : primaryColors[metricIndex % primaryColors.size()];

  // For comparison data, always use dashed lines of various styles
  // For primary data, use solid lines
  QString borderDash = isComparison
                         ? lineStyles[metricIndex % lineStyles.size()]
                         : "[]";  // Solid line for primary datasets

  return QString(R"(
        borderColor: '%1',
        backgroundColor: '%1',
        borderDash: %2,
        borderWidth: 2,
        pointRadius: 0,
        pointHoverRadius: 4,
        tension: 0.1
    )")
    .arg(color)
    .arg(borderDash);
}

QString BenchmarkCharts::generateHtmlChart(
  const QString& filename, const QString& title, const QString& xLabel,
  const QString& yLabel, const QStringList& dataLabels,
  const QVector<QVector<QPointF>>& datasets, YAxisScaleType yScaleType,
  double yMinValue, double yMaxValue) {
  // Ensure output directory exists
  QDir outputDir(QApplication::applicationDirPath() + "/html_reports");
  if (!ensureOutputDirExists(outputDir)) {
    return "";
  }

  // Calculate statistics for each dataset
  QVector<double> averages;
  QVector<double> minimums;
  QVector<double> maximums;

  for (const auto& dataset : datasets) {
    if (dataset.isEmpty()) {
      averages.append(0);
      minimums.append(0);
      maximums.append(0);
      continue;
    }

    double sum = 0;
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();

    for (const auto& point : dataset) {
      sum += point.y();
      min = std::min(min, point.y());
      max = std::max(max, point.y());
    }

    averages.append(sum / dataset.size());
    minimums.append(min);
    maximums.append(max);
  }

  // Create HTML file with Chart.js
  QString htmlFilePath = outputDir.filePath(filename + ".html");
  QFile htmlFile(htmlFilePath);

  if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to create HTML file: " << htmlFilePath.toStdString();
    return "";
  }

  QTextStream out(&htmlFile);

  // Convert datasets to JSON
  QStringList datasetJsons;
  QStringList colors = generateRandomColors(datasets.size());

  for (int i = 0; i < datasets.size(); ++i) {
    QJsonArray dataPoints;
    for (const auto& point : datasets[i]) {
      QJsonObject jsonPoint;
      jsonPoint["x"] = point.x();
      jsonPoint["y"] = point.y();
      dataPoints.append(jsonPoint);
    }

    QString datasetJson =
      QString(R"({
            label: '%1',
            data: %2,
            borderColor: '%3',
            backgroundColor: '%3',
            borderWidth: 2,
            pointRadius: 0,
            pointHoverRadius: 4,
            tension: 0.1
        })")
        .arg(dataLabels[i])
        .arg(QString::fromUtf8(
          QJsonDocument(dataPoints).toJson(QJsonDocument::Compact)))
        .arg(colors[i]);

    datasetJsons.append(datasetJson);
  }

  // Build HTML with Chart.js
  out << "<!DOCTYPE html>\n";
  out << "<html>\n";
  out << "<head>\n";
  out << "    <meta charset=\"utf-8\">\n";
  out << "    <title>" << title << "</title>\n";
  out << "    <script "
         "src=\"https://cdn.jsdelivr.net/npm/chart.js@3.7.1\"></script>\n";
  out << "    <script "
         "src=\"https://cdn.jsdelivr.net/npm/"
         "chartjs-plugin-annotation@2.0.0\"></script>\n";
  out << "    <style>\n";
  out << "        body { font-family: 'Segoe UI', 'Helvetica Neue', Arial, "
         "sans-serif; margin: 0; padding: 24px; background-color: #111418; "
         "color: #e8ecf3; }\n";
  out << "        .container { max-width: 1280px; margin: 0 auto; "
         "background-color: #1b2027; padding: 24px; border-radius: 12px; "
         "box-shadow: 0 18px 50px rgba(0, 0, 0, 0.45); border: 1px solid "
         "#232a33; }\n";
  out << "        h1 { color: #f3f5f7; margin-top: 0; }\n";
  out << "        .chart-container { position: relative; height: 520px; width: "
         "100%; background-color: #14181f; border: 1px solid #2f363f; "
         "border-radius: 10px; padding: 10px; box-sizing: border-box; }\n";
  out << "        .stats { margin-top: 18px; padding: 16px; background-color: "
         "#161b22; border-radius: 8px; border: 1px solid #2d333d; }\n";
  out << "        .stats h3 { margin-top: 0; color: #f3f5f7; }\n";
  out << "        .stats table { width: 100%; border-collapse: collapse; "
         "color: #d7dde6; }\n";
  out << "        .stats td, .stats th { padding: 8px; text-align: left; "
         "border-bottom: 1px solid #2b3038; }\n";
  out << "        .stats th { background-color: #20262f; color: #f3f5f7; }\n";
  out << "    </style>\n";
  out << "</head>\n";
  out << "<body>\n";
  out << "    <div class=\"container\">\n";
  out << "        <h1>" << title << "</h1>\n";
  out << "        <div class=\"chart-container\">\n";
  out << "            <canvas id=\"benchmarkChart\"></canvas>\n";
  out << "        </div>\n";
  out << "        <div class=\"stats\">\n";
  out << "            <h3>Statistics</h3>\n";
  out << "            <table>\n";
  out << "                "
         "<tr><th>Metric</th><th>Average</th><th>Minimum</th><th>Maximum</th></"
         "tr>\n";

  for (int i = 0; i < dataLabels.size() && i < averages.size(); ++i) {
    out << "                <tr><td>" << dataLabels[i] << "</td>"
        << "<td>" << QString::number(averages[i], 'f', 2) << "</td>"
        << "<td>" << QString::number(minimums[i], 'f', 2) << "</td>"
        << "<td>" << QString::number(maximums[i], 'f', 2) << "</td></tr>\n";
  }

  out << "            </table>\n";
  out << "        </div>\n";
  out << "    </div>\n";
  out << "    <script>\n";
  out << "        Chart.defaults.color = '#e6e6e6';\n";
  out << "        Chart.defaults.font.family = '\"Segoe UI\", \"Helvetica Neue\", "
         "Arial, sans-serif';\n";
  out << "        Chart.defaults.plugins.legend.labels.color = '#e6e6e6';\n";
  out << "        Chart.defaults.borderColor = 'rgba(255,255,255,0.08)';\n";
  out << "        // Chart configuration\n";
  out << "        const ctx = "
         "document.getElementById('benchmarkChart').getContext('2d');\n";

  // Add datasets
  out << "        const datasets = [\n";
  for (int i = 0; i < datasetJsons.size(); ++i) {
    out << "            " << datasetJsons[i];
    if (i < datasetJsons.size() - 1) {
      out << ",";
    }
    out << "\n";
  }
  out << "        ];\n";

  // Add annotations for average lines
  out << "        const annotations = {};\n";
  for (int i = 0; i < averages.size(); ++i) {
    out << "        annotations['avgLine" << i << "'] = {\n";
    out << "            type: 'line',\n";
    out << "            yMin: " << averages[i] << ",\n";
    out << "            yMax: " << averages[i] << ",\n";
    out << "            borderColor: '" << colors[i] << "',\n";
    out << "            borderWidth: 2,\n";
    out << "            borderDash: [6, 6],\n";
    out << "            label: {\n";
    out << "                display: true,\n";
    out << "                content: 'Avg: "
        << QString::number(averages[i], 'f', 2) << "',\n";
    out << "                position: 'start',\n";
    out << "                backgroundColor: '" << colors[i] << "',\n";
    out << "            }\n";
    out << "        };\n";
  }

  // Create the chart
  out << "        const chart = new Chart(ctx, {\n";
  out << "            type: 'line',\n";
  out << "            data: {\n";
  out << "                datasets: datasets\n";
  out << "            },\n";
  out << "            options: {\n";
  out << "                responsive: true,\n";
  out << "                maintainAspectRatio: false,\n";
  out << "                interaction: {\n";
  out << "                    mode: 'index',\n";
  out << "                    intersect: false,\n";
  out << "                },\n";
  out << "                plugins: {\n";
  out << "                    title: {\n";
  out << "                        display: true,\n";
  out << "                        text: '" << title << "'\n";
  out << "                    },\n";
  out << "                    annotation: {\n";
  out << "                        annotations: annotations\n";
  out << "                    }\n";
  out << "                },\n";
  out << "                scales: {\n";
  out << "                    x: {\n";
  out << "                        type: 'linear',\n";
  out << "                        title: {\n";
  out << "                            display: true,\n";
  out << "                            text: '" << xLabel << "'\n";
  out << "                        },\n";
  out << "                        ticks: { color: '#d0d7de' },\n";
  out << "                        grid: { color: 'rgba(255,255,255,0.08)' }\n";
  out << "                    },\n";
  out << "                    y: {\n";
  out << "                        title: {\n";
  out << "                            display: true,\n";
  out << "                            text: '" << yLabel << "'\n";
  out << "                        },\n";
  out << "                        ticks: { color: '#d0d7de' },\n";
  out << "                        grid: { color: 'rgba(255,255,255,0.08)' },\n";
  out << "                        "
      << getYScaleOptionsJson(yScaleType, yMinValue, yMaxValue) << "\n";
  out << "                    }\n";
  out << "                }\n";
  out << "            }\n";
  out << "        });\n";
  out << "    </script>\n";
  out << "</body>\n";
  out << "</html>\n";

  htmlFile.close();

  return htmlFilePath;
}

QString BenchmarkCharts::generateHtmlChartWithComparison(
  const QString& filename, const QString& title, const QString& xLabel,
  const QString& yLabel, const QStringList& dataLabels,
  const QVector<QVector<QPointF>>& primaryDatasets,
  const QVector<QVector<QPointF>>& comparisonDatasets,
  YAxisScaleType yScaleType, double yMinValue, double yMaxValue) {
  // Ensure output directory exists
  QDir outputDir(QApplication::applicationDirPath() + "/html_reports");
  if (!ensureOutputDirExists(outputDir)) {
    return "";
  }

  // Calculate statistics for primary datasets
  QVector<double> primaryAverages;
  QVector<double> primaryMinimums;
  QVector<double> primaryMaximums;

  for (const auto& dataset : primaryDatasets) {
    if (dataset.isEmpty()) {
      primaryAverages.append(0);
      primaryMinimums.append(0);
      primaryMaximums.append(0);
      continue;
    }

    double sum = 0;
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();

    for (const auto& point : dataset) {
      sum += point.y();
      min = std::min(min, point.y());
      max = std::max(max, point.y());
    }

    primaryAverages.append(sum / dataset.size());
    primaryMinimums.append(min);
    primaryMaximums.append(max);
  }

  // Calculate statistics for comparison datasets
  QVector<double> comparisonAverages;
  QVector<double> comparisonMinimums;
  QVector<double> comparisonMaximums;

  for (const auto& dataset : comparisonDatasets) {
    if (dataset.isEmpty()) {
      comparisonAverages.append(0);
      comparisonMinimums.append(0);
      comparisonMaximums.append(0);
      continue;
    }

    double sum = 0;
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();

    for (const auto& point : dataset) {
      sum += point.y();
      min = std::min(min, point.y());
      max = std::max(max, point.y());
    }

    comparisonAverages.append(sum / dataset.size());
    comparisonMinimums.append(min);
    comparisonMaximums.append(max);
  }

  // Create HTML file with Chart.js
  QString htmlFilePath = outputDir.filePath(filename + ".html");
  QFile htmlFile(htmlFilePath);

  if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to create HTML file: " << htmlFilePath.toStdString();
    return "";
  }

  QTextStream out(&htmlFile);

  // Convert datasets to JSON
  QStringList datasetJsons;

  // Primary datasets (use blue tones)
  for (int i = 0; i < primaryDatasets.size(); ++i) {
    QJsonArray dataPoints;
    for (const auto& point : primaryDatasets[i]) {
      QJsonObject jsonPoint;
      jsonPoint["x"] = point.x();
      jsonPoint["y"] = point.y();
      dataPoints.append(jsonPoint);
    }

    QString datasetJson =
      QString(R"({
            label: '%1',
            data: %2,
            %3
        })")
        .arg(dataLabels[i])
        .arg(QString::fromUtf8(
          QJsonDocument(dataPoints).toJson(QJsonDocument::Compact)))
        .arg(getLineStyleOptions(i, false));

    datasetJsons.append(datasetJson);
  }

  // Comparison datasets (use red tones with dashed lines)
  for (int i = 0; i < comparisonDatasets.size(); ++i) {
    if (i >= dataLabels.size()) break;  // Safety check

    QJsonArray dataPoints;
    for (const auto& point : comparisonDatasets[i]) {
      QJsonObject jsonPoint;
      jsonPoint["x"] = point.x();
      jsonPoint["y"] = point.y();
      dataPoints.append(jsonPoint);
    }

    QString datasetJson =
      QString(R"({
            label: 'Comparison %1',
            data: %2,
            %3
        })")
        .arg(dataLabels[i])
        .arg(QString::fromUtf8(
          QJsonDocument(dataPoints).toJson(QJsonDocument::Compact)))
        .arg(getLineStyleOptions(i, true));

    datasetJsons.append(datasetJson);
  }

  // Build HTML with Chart.js
  out << "<!DOCTYPE html>\n";
  out << "<html>\n";
  out << "<head>\n";
  out << "    <meta charset=\"utf-8\">\n";
  out << "    <title>" << title << "</title>\n";
  out << "    <script "
         "src=\"https://cdn.jsdelivr.net/npm/chart.js@3.7.1\"></script>\n";
  out << "    <script "
         "src=\"https://cdn.jsdelivr.net/npm/"
         "chartjs-plugin-annotation@2.0.0\"></script>\n";
  out << "    <style>\n";
  out << "        body { font-family: 'Segoe UI', 'Helvetica Neue', Arial, "
         "sans-serif; margin: 0; padding: 24px; background-color: #111418; "
         "color: #e8ecf3; }\n";
  out << "        .container { max-width: 1280px; margin: 0 auto; "
         "background-color: #1b2027; padding: 24px; border-radius: 12px; "
         "box-shadow: 0 18px 50px rgba(0, 0, 0, 0.45); border: 1px solid "
         "#232a33; }\n";
  out << "        h1 { color: #f3f5f7; margin-top: 0; }\n";
  out << "        .chart-container { position: relative; height: 520px; width: "
         "100%; background-color: #14181f; border: 1px solid #2f363f; "
         "border-radius: 10px; padding: 10px; box-sizing: border-box; }\n";
  out << "        .stats { margin-top: 18px; padding: 16px; background-color: "
         "#161b22; border-radius: 8px; border: 1px solid #2d333d; color: "
         "#d7dde6; }\n";
  out << "        .stats h3 { margin-top: 0; color: #f3f5f7; }\n";
  out << "        .stats table { width: 100%; border-collapse: collapse; }\n";
  out << "        .stats td, .stats th { padding: 8px; text-align: left; "
         "border-bottom: 1px solid #2b3038; }\n";
  out << "        .stats th { background-color: #20262f; color: #f3f5f7; }\n";
  out
    << "        .legend-item { display: inline-block; margin-right: 20px; "
       "color: #d0d7de; }\n";
  out << "        .legend-color { display: inline-block; width: 20px; height: "
         "10px; margin-right: 5px; border-radius: 3px; }\n";
  out << "        .primary-line { background-color: #7cc5ff; }\n";
  out << "        .comparison-line { background-color: #ff82b7; border-top: 2px "
         "dashed #ff82b7; height: 0; }\n";
  out << "        .legend-container { margin-bottom: 15px; }\n";
  out << "        .controls { margin: 10px 0; color: #d0d7de; }\n";
  out << "        .controls label { margin-right: 8px; font-weight: 600; }\n";
  out << "    </style>\n";
  out << "</head>\n";
  out << "<body>\n";
  out << "    <div class=\"container\">\n";
  out << "        <h1>" << title << "</h1>\n";
  out << "        <div class=\"controls\">\n";
  out << "            <label><input type=\"checkbox\" id=\"showComparison\" "
         "checked> Show comparison</label>\n";
  out << "        </div>\n";

  // Add custom legend
  out << "        <div class=\"legend-container\">\n";
  out << "            <div class=\"legend-item\"><span class=\"legend-color "
         "primary-line\"></span>Current Run</div>\n";
  out << "            <div class=\"legend-item\"><span class=\"legend-color "
         "comparison-line\"></span>Comparison Run</div>\n";
  out << "        </div>\n";

  // Add a tiny metadata description for the comparison run(s)
  int compSeriesCount = comparisonDatasets.size();
  int compPoints = 0;
  for (const auto& ds : comparisonDatasets) compPoints += ds.size();
  if (compSeriesCount > 0 && compPoints > 0) {
    out << "        <p style=\"color:#666; margin-top:4px;\">"
        << "Comparison mode: " << compSeriesCount
        << " series loaded (" << compPoints << " points). Dashed lines"
        << " represent comparison data." << "</p>\n";
  }

  out << "        <div class=\"chart-container\">\n";
  out << "            <canvas id=\"benchmarkChart\"></canvas>\n";
  out << "        </div>\n";
  out << "        <div class=\"stats\">\n";
  out << "            <h3>Statistics</h3>\n";
  out << "            <table>\n";
  out << "                <tr><th>Metric</th><th>Current "
         "Avg</th><th>Comparison Avg</th><th>Difference</th></tr>\n";

  for (int i = 0; i < dataLabels.size() && i < primaryAverages.size() &&
                  i < comparisonAverages.size();
       ++i) {
    double difference = primaryAverages[i] - comparisonAverages[i];
    double percentChange = comparisonAverages[i] != 0
                             ? (difference / comparisonAverages[i] * 100)
                             : 0;

    QString differenceStr = QString("%1 (%2%)")
                              .arg(QString::number(difference, 'f', 2))
                              .arg(QString::number(percentChange, 'f', 2));

    out << "                <tr><td>" << dataLabels[i] << "</td>"
        << "<td>" << QString::number(primaryAverages[i], 'f', 2) << "</td>"
        << "<td>" << QString::number(comparisonAverages[i], 'f', 2) << "</td>"
        << "<td>" << differenceStr << "</td></tr>\n";
  }

  out << "            </table>\n";
  out << "        </div>\n";
  out << "    </div>\n";
  out << "    <script>\n";
  out << "        Chart.defaults.color = '#e6e6e6';\n";
  out << "        Chart.defaults.font.family = '\"Segoe UI\", \"Helvetica Neue\", "
         "Arial, sans-serif';\n";
  out << "        Chart.defaults.plugins.legend.labels.color = '#e6e6e6';\n";
  out << "        Chart.defaults.borderColor = 'rgba(255,255,255,0.08)';\n";
  out << "        // Chart configuration\n";
  out << "        const ctx = "
         "document.getElementById('benchmarkChart').getContext('2d');\n";

  // Add datasets
  out << "        const datasets = [\n";
  for (int i = 0; i < datasetJsons.size(); ++i) {
    out << "            " << datasetJsons[i];
    if (i < datasetJsons.size() - 1) {
      out << ",";
    }
    out << "\n";
  }
  out << "        ];\n";

  // Add annotations for average lines - only for primary datasets to keep the
  // chart clean
  out << "        const annotations = {};\n";
  for (int i = 0; i < primaryAverages.size(); ++i) {
    // Use the same color as the dataset for the average line
    out << "        annotations['avgLine" << i << "'] = {\n";
    out << "            type: 'line',\n";
    out << "            yMin: " << primaryAverages[i] << ",\n";
    out << "            yMax: " << primaryAverages[i] << ",\n";
    out << "            borderColor: 'rgb(54, 162, 235)',\n";
    out << "            borderWidth: 2,\n";
    out << "            borderDash: [6, 6],\n";
    out << "            label: {\n";
    out << "                display: true,\n";
    out << "                content: 'Avg: "
        << QString::number(primaryAverages[i], 'f', 2) << "',\n";
    out << "                position: 'start',\n";
    out << "                backgroundColor: 'rgb(54, 162, 235)',\n";
    out << "            }\n";
    out << "        };\n";
  }

  // Create the chart
  out << "        const chart = new Chart(ctx, {\n";
  out << "            type: 'line',\n";
  out << "            data: {\n";
  out << "                datasets: datasets\n";
  out << "            },\n";
  out << "            options: {\n";
  out << "                responsive: true,\n";
  out << "                maintainAspectRatio: false,\n";
  out << "                interaction: {\n";
  out << "                    mode: 'index',\n";
  out << "                    intersect: false,\n";
  out << "                },\n";
  out << "                plugins: {\n";
  out << "                    title: {\n";
  out << "                        display: true,\n";
  out << "                        text: '" << title << "'\n";
  out << "                    },\n";
  out << "                    annotation: {\n";
  out << "                        annotations: annotations\n";
  out << "                    }\n";
  out << "                },\n";
  out << "                scales: {\n";
  out << "                    x: {\n";
  out << "                        type: 'linear',\n";
  out << "                        title: {\n";
  out << "                            display: true,\n";
  out << "                            text: '" << xLabel << "'\n";
  out << "                        },\n";
  out << "                        ticks: { color: '#d0d7de' },\n";
  out << "                        grid: { color: 'rgba(255,255,255,0.08)' }\n";
  out << "                    },\n";
  out << "                    y: {\n";
  out << "                        title: {\n";
  out << "                            display: true,\n";
  out << "                            text: '" << yLabel << "'\n";
  out << "                        },\n";
  out << "                        ticks: { color: '#d0d7de' },\n";
  out << "                        grid: { color: 'rgba(255,255,255,0.08)' },\n";
  out << "                        "
      << getYScaleOptionsJson(yScaleType, yMinValue, yMaxValue) << "\n";
  out << "                    }\n";
  out << "                }\n";
  out << "            }\n";
  out << "        });\n";
  out << "        document.getElementById('showComparison').addEventListener('change', (e) => {\n";
  out << "            const show = e.target.checked;\n";
  out << "            chart.data.datasets = show ? datasets : datasets.filter(ds => !(ds.label && ds.label.startsWith('Comparison')));\n";
  out << "            chart.update();\n";
  out << "        });\n";
  out << "    </script>\n";
  out << "</body>\n";
  out << "</html>\n";

  htmlFile.close();

  return htmlFilePath;
}
