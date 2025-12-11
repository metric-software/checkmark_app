#include <QApplication>
#include <QFileInfo>

#include "BenchmarkCharts.h"
#include "../../logging/Logger.h"

QString BenchmarkCharts::generateDashboardHtml(
  const QString& csvFilePath, const QString& comparisonCsvFilePath) {
  // Ensure output directory exists
  QDir outputDir(QApplication::applicationDirPath() + "/html_reports");
  if (!ensureOutputDirExists(outputDir)) {
    return "";
  }

  // Generate all the individual charts first
  QString fpsChart, frameTimeChart, cpuUsageChart, gpuUsageChart, memoryChart;

  if (comparisonCsvFilePath.isEmpty()) {
    fpsChart = generateFpsChart(csvFilePath);
    frameTimeChart = generateFrameTimeMetricsChart(csvFilePath);
    cpuUsageChart = generateCpuUsageChart(csvFilePath);
    gpuUsageChart = generateGpuUsageChart(csvFilePath);
    memoryChart = generateMemoryChart(csvFilePath);
  } else {
    fpsChart = generateFpsChart(csvFilePath, comparisonCsvFilePath);
    frameTimeChart =
      generateFrameTimeMetricsChart(csvFilePath, comparisonCsvFilePath);
    cpuUsageChart = generateCpuUsageChart(csvFilePath, comparisonCsvFilePath);
    gpuUsageChart = generateGpuUsageChart(csvFilePath, comparisonCsvFilePath);
    memoryChart = generateMemoryChart(csvFilePath, comparisonCsvFilePath);
  }

  // Get file info for the timestamp
  QFileInfo fileInfo(csvFilePath);
  QString timestamp = fileInfo.lastModified().toString("yyyy-MM-dd HH:mm:ss");

  // Calculate summary
  BenchmarkSummary summary = calculateBenchmarkSummary(csvFilePath);

  auto fmt = [](double value, int precision, const QString& suffix = QString()) {
    return (value >= 0)
             ? QString::number(value, 'f', precision) + suffix
             : QString("N/A");
  };

  // Create dashboard HTML
  QString htmlFilePath = outputDir.filePath("benchmark_dashboard.html");
  QFile htmlFile(htmlFilePath);

  if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to create dashboard HTML file";
    return "";
  }

  QTextStream out(&htmlFile);

  // Dashboard HTML content, now referencing the individual charts
  out << "<!DOCTYPE html>\n";
  out << "<html>\n";
  out << "<head>\n";
  out << "    <meta charset=\"utf-8\">\n";
  out << "    <title>Benchmark Dashboard</title>\n";
  out << "    <style>\n";
  out << "        body { font-family: 'Segoe UI', 'Helvetica Neue', Arial, "
         "sans-serif; margin: 0; padding: 28px; background-color: #0f1116; "
         "color: #e9edf5; }\n";
  out << "        .container { max-width: 1400px; margin: 0 auto; "
         "background-color: #181c24; padding: 28px; border-radius: 14px; "
         "box-shadow: 0 20px 60px rgba(0,0,0,0.55); border: 1px solid "
         "#232a33; }\n";
  out << "        h1, h2 { color: #f3f5f7; }\n";
  out << "        .dashboard-header { display: flex; justify-content: "
         "space-between; align-items: center; margin-bottom: 20px; "
         "gap: 12px; }\n";
  out << "        .dashboard-header .metadata { color: #c3cad5; font-size: "
         "14px; }\n";
  out << "        .summary-metrics { display: flex; flex-wrap: wrap; "
         "margin-bottom: 14px; gap: 12px; }\n";
  out << "        .metric-card { flex: 1 1 220px; padding: 16px; "
         "background-color: #1f252f; border-radius: 10px; box-shadow: 0 10px "
         "30px rgba(0,0,0,0.35); border: 1px solid #2c333d; }\n";
  out << "        .metric-card h3 { margin-top: 0; color: #e5e9f0; "
         "font-size: 16px; }\n";
  out << "        .metric-value { font-size: 26px; font-weight: 700; color: "
         "#7cc5ff; }\n";
  out << "        .metric-extra { font-size: 13px; color: #aeb7c2; margin-top: "
         "6px; }\n";
  out << "        .charts-container { margin-top: 34px; }\n";
  out << "        .chart-row { display: flex; flex-wrap: wrap; gap: 16px; "
         "margin-bottom: 18px; }\n";
  out << "        .chart-col { flex: 1; min-width: 480px; }\n";
  out << "        .chart-frame { width: 100%; height: 900px; border: 1px solid "
         "#2f363f; background-color: #0f1217; border-radius: 10px; "
         "box-shadow: 0 12px 36px rgba(0,0,0,0.4); overflow: hidden; }\n";
  out << "        @media (max-width: 900px) {\n";
  out << "            .chart-row { flex-direction: column; }\n";
  out << "            .chart-col { min-width: 100%; }\n";
  out << "            .chart-frame { height: 720px; }\n";
  out << "        }\n";
  out << "    </style>\n";
  out << "</head>\n";
  out << "<body>\n";
  out << "    <div class=\"container\">\n";
  out << "        <div class=\"dashboard-header\">\n";
  out << "            <h1>Benchmark Performance Dashboard</h1>\n";
  out << "            <div class=\"metadata\">\n";
  out << "                <p>Benchmark: " << fileInfo.fileName() << "</p>\n";
  out << "                <p>Recorded: " << timestamp << "</p>\n";
  out << "            </div>\n";
  out << "        </div>\n";

  // Comparison metadata if available
  if (!comparisonCsvFilePath.isEmpty()) {
    QFileInfo comparisonInfo(comparisonCsvFilePath);
    QString comparisonTimestamp =
      comparisonInfo.lastModified().toString("yyyy-MM-dd HH:mm:ss");

    out << "        <div class=\"comparison-header\">\n";
    out << "            <h3>Comparison Benchmark</h3>\n";
    out << "            <div class=\"metadata\">\n";
    out << "                <p>Benchmark: " << comparisonInfo.fileName()
        << "</p>\n";
    out << "                <p>Recorded: " << comparisonTimestamp << "</p>\n";
    out << "            </div>\n";
    out << "        </div>\n";
  }

  // Summary metrics
  out << "        <h2>Performance Summary</h2>\n";
  out << "        <div class=\"summary-metrics\">\n";

  // FPS metrics
  out << "            <div class=\"metric-card\">\n";
  out << "                <h3>Average FPS</h3>\n";
  out << "                <div class=\"metric-value\">"
      << fmt(summary.avgFps, 1, " FPS") << "</div>\n";
  out << "                <div class=\"metric-extra\">Min: "
      << fmt(summary.minFps, 1, " FPS")
      << " | Max: " << fmt(summary.maxFps, 1, " FPS") << "</div>\n";
  out << "                <div class=\"metric-extra\">1% Low: "
      << fmt(summary.fps1Low, 1, " FPS")
      << " | 0.1% Low: " << fmt(summary.fps01Low, 1, " FPS") << "</div>\n";
  out << "            </div>\n";

  // Frame time metrics
  out << "            <div class=\"metric-card\">\n";
  out << "                <h3>Average Frame Time</h3>\n";
  out << "                <div class=\"metric-value\">"
      << fmt(summary.avgFrameTime, 2, " ms") << "</div>\n";
  out << "                <div class=\"metric-extra\">Min: "
      << fmt(summary.minFrameTime, 2, " ms")
      << " | Max: " << fmt(summary.maxFrameTime, 2, " ms") << "</div>\n";
  out << "            </div>\n";

  // CPU Usage metrics
  out << "            <div class=\"metric-card\">\n";
  out << "                <h3>CPU Usage</h3>\n";
  out << "                <div class=\"metric-value\">"
      << fmt(summary.avgCpuUsage, 1, "%") << "</div>\n";
  out << "                <div class=\"metric-extra\">Peak: "
      << fmt(summary.maxCpuUsage, 1, "%") << "</div>\n";
  out << "            </div>\n";

  // GPU Usage metrics
  out << "            <div class=\"metric-card\">\n";
  out << "                <h3>GPU Usage</h3>\n";
  out << "                <div class=\"metric-value\">"
      << fmt(summary.avgGpuUsage, 1, "%") << "</div>\n";
  out << "                <div class=\"metric-extra\">Peak: "
      << fmt(summary.maxGpuUsage, 1, "%") << "</div>\n";
  out << "            </div>\n";

  out << "        </div>\n";

  // Charts section
  out << "        <div class=\"charts-container\">\n";
  out << "            <h2>Performance Charts</h2>\n";

  // Row 1: FPS and Frame Time
  out << "            <div class=\"chart-row\">\n";
  out << "                <div class=\"chart-col\">\n";
  out << "                    <h3>FPS Over Time</h3>\n";
  out << "                    <iframe class=\"chart-frame\" scrolling=\"no\" "
         "loading=\"lazy\" src=\""
      << QFileInfo(fpsChart).fileName() << "\"></iframe>\n";
  out << "                </div>\n";
  out << "                <div class=\"chart-col\">\n";
  out << "                    <h3>Frame Time Distribution</h3>\n";
  out << "                    <iframe class=\"chart-frame\" scrolling=\"no\" "
         "loading=\"lazy\" src=\""
      << QFileInfo(frameTimeChart).fileName() << "\"></iframe>\n";
  out << "                </div>\n";
  out << "            </div>\n";

  // Row 2: CPU and GPU Usage
  out << "            <div class=\"chart-row\">\n";
  out << "                <div class=\"chart-col\">\n";
  out << "                    <h3>CPU Usage Over Time</h3>\n";
  out << "                    <iframe class=\"chart-frame\" scrolling=\"no\" "
         "loading=\"lazy\" src=\""
      << QFileInfo(cpuUsageChart).fileName() << "\"></iframe>\n";
  out << "                </div>\n";
  out << "                <div class=\"chart-col\">\n";
  out << "                    <h3>GPU Metrics Over Time</h3>\n";
  out << "                    <iframe class=\"chart-frame\" scrolling=\"no\" "
         "loading=\"lazy\" src=\""
      << QFileInfo(gpuUsageChart).fileName() << "\"></iframe>\n";
  out << "                </div>\n";
  out << "            </div>\n";

  // Row 3: Memory Usage
  out << "            <div class=\"chart-row\">\n";
  out << "                <div class=\"chart-col\">\n";
  out << "                    <h3>Memory Usage Over Time</h3>\n";
  out << "                    <iframe class=\"chart-frame\" scrolling=\"no\" "
         "loading=\"lazy\" src=\""
      << QFileInfo(memoryChart).fileName() << "\"></iframe>\n";
  out << "                </div>\n";
  out << "            </div>\n";

  out << "        </div>\n";
  out << "    </div>\n";
  out << "</body>\n";
  out << "</html>\n";

  htmlFile.close();

  return htmlFilePath;
}

QString BenchmarkCharts::generateComparisonHtml(
  const QString& csvFilePath, const QString& comparisonCsvFilePath) {
  // Create HTML directory if it doesn't exist
  QDir outputDir(QApplication::applicationDirPath() + "/html_reports");
  if (!ensureOutputDirExists(outputDir)) {
    return QString();
  }

  // Generate file path
  QString filePath = outputDir.filePath("comparison_report.html");

  // Create basic comparison report
  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to create comparison HTML file";
    return QString();
  }

  QTextStream html(&file);

  html << "<!DOCTYPE html>\n";
  html << "<html>\n";
  html << "<head>\n";
  html << "    <meta charset=\"UTF-8\">\n";
  html << "    <title>Benchmark Comparison Report</title>\n";
  html << "    <style>\n";
  html << "        body { font-family: 'Segoe UI', 'Helvetica Neue', Arial, "
          "sans-serif; margin: 20px; background-color: #111418; color: "
          "#e8ecf3; }\n";
  html << "        h1, h2 { color: #f3f5f7; }\n";
  html << "        table { border-collapse: collapse; width: 100%; "
          "background-color: #1b2027; color: #e8ecf3; }\n";
  html << "        th, td { border: 1px solid #242b34; padding: 8px; }\n";
  html << "        th { background-color: #161c24; }\n";
  html << "        .better { background-color: #1f2a23; }\n";
  html << "        .worse { background-color: #2b1c1f; }\n";
  html << "    </style>\n";
  html << "</head>\n";
  html << "<body>\n";
  html << "    <h1>Benchmark Comparison Report</h1>\n";

  html
    << "    <p>Comparison report functionality to be fully implemented.</p>\n";

  html << "</body>\n";
  html << "</html>\n";

  file.close();

  return filePath;
}
