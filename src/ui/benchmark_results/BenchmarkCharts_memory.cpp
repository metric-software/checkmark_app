#include <algorithm>

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "BenchmarkCharts.h"
#include "../../logging/Logger.h"

static double toMb(const QString& val, bool* ok) {
  double bytes = val.toDouble(ok);
  return bytes / 1048576.0;
}

QString BenchmarkCharts::generateMemoryChart(
  const QString& csvFilePath, const QString& comparisonCsvFilePath) {
  auto parseMemoryFile = [&](const QString& path, QVector<QPointF>& ramUsage,
                             QVector<QPointF>& ramLoad,
                             QVector<QPointF>& gpuMemUsage,
                             QVector<QPointF>& gpuMemLoad) -> bool {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      LOG_ERROR << "Failed to open CSV file: [path hidden for privacy]";
      return false;
    }

    QTextStream in(&file);
    QString header = in.readLine();
    QStringList headers = header.split(",");

    int memoryUsageIndex = headers.indexOf("Memory Usage (MB)");
    int memoryLoadIndex = headers.indexOf("PDH_Memory_Load(%)");
    if (memoryLoadIndex < 0) memoryLoadIndex = headers.indexOf("Memory Load");
    int memoryAvailableIndex = headers.indexOf("PDH_Memory_Available(MB)");
    int memoryCommitLimitIndex = headers.indexOf("PDH_Memory_Commit_Limit(bytes)");
    int gpuMemUsedIndex = headers.indexOf("GPU Mem Used");
    int gpuMemTotalIndex = headers.indexOf("GPU Mem Total");

    int maxIndex = -1;
    for (int idx : {memoryUsageIndex, memoryLoadIndex, memoryAvailableIndex,
                    memoryCommitLimitIndex, gpuMemUsedIndex, gpuMemTotalIndex}) {
      if (idx > maxIndex) maxIndex = idx;
    }

    int timeCounter = 0;

    while (!in.atEnd()) {
      QStringList fields = in.readLine().split(",");

      if (maxIndex >= 0 && fields.size() <= maxIndex) {
        timeCounter++;
        continue;
      }

      // RAM usage (MB)
      if (memoryUsageIndex >= 0 && memoryUsageIndex < fields.size()) {
        bool ok = false;
        double ram = fields[memoryUsageIndex].toDouble(&ok);
        if (ok && ram >= 0) {
          ramUsage.append(QPointF(timeCounter, ram));
        }
      } else if (memoryAvailableIndex >= 0 && memoryCommitLimitIndex >= 0 &&
                 memoryAvailableIndex < fields.size() &&
                 memoryCommitLimitIndex < fields.size()) {
        bool availOk = false, limitOk = false;
        double availMb = fields[memoryAvailableIndex].toDouble(&availOk);
        double limitMb = toMb(fields[memoryCommitLimitIndex], &limitOk);
        if (availOk && limitOk) {
          double usedMb = std::max(0.0, limitMb - availMb);
          ramUsage.append(QPointF(timeCounter, usedMb));
        }
      }

      // RAM load (%)
      if (memoryLoadIndex >= 0 && memoryLoadIndex < fields.size()) {
        bool ok = false;
        double load = fields[memoryLoadIndex].toDouble(&ok);
        if (ok && load >= 0) {
          ramLoad.append(QPointF(timeCounter, load));
        }
      }

      // GPU memory usage and load
      if (gpuMemUsedIndex >= 0 && gpuMemUsedIndex < fields.size()) {
        bool ok = false;
        double used = fields[gpuMemUsedIndex].toDouble(&ok);
        if (ok && used >= 0) {
          gpuMemUsage.append(QPointF(timeCounter, used));
          if (gpuMemTotalIndex >= 0 && gpuMemTotalIndex < fields.size()) {
            bool totOk = false;
            double total = fields[gpuMemTotalIndex].toDouble(&totOk);
            if (totOk && total > 0) {
              double pct = (used / total) * 100.0;
              gpuMemLoad.append(QPointF(timeCounter, pct));
            }
          }
        }
      }

      timeCounter++;
    }

    file.close();
    return !(ramUsage.isEmpty() && ramLoad.isEmpty() && gpuMemUsage.isEmpty() &&
             gpuMemLoad.isEmpty());
  };

  QVector<QPointF> ramUsageData;
  QVector<QPointF> ramLoadData;
  QVector<QPointF> gpuMemUsageData;
  QVector<QPointF> gpuMemLoadData;

  if (!parseMemoryFile(csvFilePath, ramUsageData, ramLoadData,
                       gpuMemUsageData, gpuMemLoadData)) {
    return "";
  }

  QVector<QPointF> cRamUsageData;
  QVector<QPointF> cRamLoadData;
  QVector<QPointF> cGpuMemUsageData;
  QVector<QPointF> cGpuMemLoadData;

  if (!comparisonCsvFilePath.isEmpty()) {
    parseMemoryFile(comparisonCsvFilePath, cRamUsageData, cRamLoadData,
                    cGpuMemUsageData, cGpuMemLoadData);
  }

  if (ramUsageData.isEmpty() && ramLoadData.isEmpty() &&
      gpuMemUsageData.isEmpty() && gpuMemLoadData.isEmpty() &&
      cRamUsageData.isEmpty() && cRamLoadData.isEmpty() &&
      cGpuMemUsageData.isEmpty() && cGpuMemLoadData.isEmpty()) {
    LOG_WARN << "Memory chart: no memory data found in CSV";
    return "";
  }

  QDir outputDir(QApplication::applicationDirPath() + "/html_reports");
  if (!ensureOutputDirExists(outputDir)) {
    return "";
  }

  QString htmlFilePath = outputDir.filePath("memory_chart.html");
  QFile htmlFile(htmlFilePath);

  if (!htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to create memory dashboard HTML file";
    return "";
  }

  auto pointsToJson = [](const QVector<QPointF>& points) {
    QJsonArray arr;
    for (const auto& p : points) {
      QJsonObject obj;
      obj["x"] = p.x();
      obj["y"] = p.y();
      arr.append(obj);
    }
    return QString::fromUtf8(
      QJsonDocument(arr).toJson(QJsonDocument::Compact));
  };

  QTextStream out(&htmlFile);

  out << "<!DOCTYPE html>\n";
  out << "<html>\n";
  out << "<head>\n";
  out << "    <meta charset=\"utf-8\">\n";
  out << "    <title>Memory Metrics</title>\n";
  out << "    <script "
         "src=\"https://cdn.jsdelivr.net/npm/chart.js@3.7.1\"></script>\n";
  out << "    <style>\n";
  out << "        body { font-family: 'Segoe UI', 'Helvetica Neue', Arial, "
         "sans-serif; margin: 0; padding: 24px; background-color: #111418; "
         "color: #e8ecf3; }\n";
  out << "        .container { max-width: 1400px; margin: 0 auto; "
         "background-color: #181c24; padding: 24px; border-radius: 12px; "
         "box-shadow: 0 20px 60px rgba(0,0,0,0.55); border: 1px solid "
         "#232a33; }\n";
  out << "        h1 { margin-top: 0; color: #f3f5f7; }\n";
  out << "        .chart-grid { display: grid; grid-template-columns: "
         "repeat(auto-fit, minmax(420px, 1fr)); gap: 16px; }\n";
  out << "        .chart-card { background-color: #1f252f; border: 1px solid "
         "#2c333d; border-radius: 10px; padding: 16px; box-shadow: 0 10px "
         "30px rgba(0,0,0,0.35); }\n";
  out << "        .chart-title { margin: 0 0 8px 0; color: #e5e9f0; "
         "font-size: 16px; font-weight: 600; }\n";
  out << "        .chart-shell { position: relative; height: 340px; "
         "background-color: #14181f; border: 1px solid #2f363f; "
         "border-radius: 8px; padding: 8px; box-sizing: border-box; }\n";
  out << "        canvas { width: 100%; height: 100%; }\n";
  out << "        .meta { color: #c3cad5; margin-bottom: 12px; font-size: 13px; }\n";
  out << "    </style>\n";
  out << "</head>\n";
  out << "<body>\n";
  out << "    <div class=\"container\">\n";
  out << "        <h1>Memory Metrics</h1>\n";
  out << "        <div class=\"meta\">Primary: " << QFileInfo(csvFilePath).fileName();
  if (!comparisonCsvFilePath.isEmpty()) {
    out << " &nbsp;&bull;&nbsp; Comparison: "
        << QFileInfo(comparisonCsvFilePath).fileName();
  }
  out << "</div>\n";
  out << "        <div class=\"chart-grid\">\n";
  out << "            <div class=\"chart-card\" id=\"ramUsageCard\">\n";
  out << "                <div class=\"chart-title\">System RAM Usage (MB)</div>\n";
  out << "                <div class=\"chart-shell\"><canvas id=\"ramUsageChart\"></canvas></div>\n";
  out << "            </div>\n";
  out << "            <div class=\"chart-card\" id=\"memoryLoadCard\">\n";
  out << "                <div class=\"chart-title\">Memory Load (%)</div>\n";
  out << "                <div class=\"chart-shell\"><canvas id=\"memoryLoadChart\"></canvas></div>\n";
  out << "            </div>\n";
  out << "            <div class=\"chart-card\" id=\"gpuMemoryCard\">\n";
  out << "                <div class=\"chart-title\">GPU Memory Usage (MB)</div>\n";
  out << "                <div class=\"chart-shell\"><canvas id=\"gpuMemoryChart\"></canvas></div>\n";
  out << "            </div>\n";
  out << "        </div>\n";
  out << "    </div>\n";
  out << "    <script>\n";
  out << "        Chart.defaults.color = '#e6e6e6';\n";
  out << "        Chart.defaults.font.family = '\"Segoe UI\", \"Helvetica Neue\", "
         "Arial, sans-serif';\n";
  out << "        Chart.defaults.plugins.legend.labels.color = '#e6e6e6';\n";
  out << "        Chart.defaults.borderColor = 'rgba(255,255,255,0.08)';\n";
  out << "        const ramUsagePrimary = " << pointsToJson(ramUsageData) << ";\n";
  out << "        const ramUsageComparison = " << pointsToJson(cRamUsageData)
      << ";\n";
  out << "        const ramLoadPrimary = " << pointsToJson(ramLoadData) << ";\n";
  out << "        const ramLoadComparison = " << pointsToJson(cRamLoadData)
      << ";\n";
  out << "        const gpuMemUsagePrimary = " << pointsToJson(gpuMemUsageData)
      << ";\n";
  out << "        const gpuMemUsageComparison = "
      << pointsToJson(cGpuMemUsageData) << ";\n";
  out << "        const gpuMemLoadPrimary = " << pointsToJson(gpuMemLoadData)
      << ";\n";
  out << "        const gpuMemLoadComparison = "
      << pointsToJson(cGpuMemLoadData) << ";\n";
  out << "        const palettePrimary = ['#7cc5ff', '#9ad98f', '#f7b955', "
         "'#c599ff'];\n";
  out << "        const paletteComparison = ['#ff82b7', '#8dc2ff', '#ffd166', "
         "'#d7a6ff'];\n";
  out << "        const makeDataset = (label, data, color, dashed = false) => ({\n";
  out << "            label,\n";
  out << "            data,\n";
  out << "            borderColor: color,\n";
  out << "            backgroundColor: color,\n";
  out << "            borderWidth: 2,\n";
  out << "            borderDash: dashed ? [6, 4] : [],\n";
  out << "            pointRadius: 0,\n";
  out << "            pointHoverRadius: 3,\n";
  out << "            tension: 0.15\n";
  out << "        });\n";
  out << "        const charts = [\n";
  out << "            {\n";
  out << "                cardId: 'ramUsageCard',\n";
  out << "                canvasId: 'ramUsageChart',\n";
  out << "                title: 'System RAM Usage (MB)',\n";
  out << "                yLabel: 'Megabytes',\n";
  out << "                clampHundred: false,\n";
  out << "                datasets: [\n";
  out << "                    makeDataset('System RAM (MB)', ramUsagePrimary, "
         "palettePrimary[0]),\n";
  out << "                    makeDataset('Comparison RAM (MB)', "
         "ramUsageComparison, paletteComparison[0], true)\n";
  out << "                ]\n";
  out << "            },\n";
  out << "            {\n";
  out << "                cardId: 'memoryLoadCard',\n";
  out << "                canvasId: 'memoryLoadChart',\n";
  out << "                title: 'Memory Load (%)',\n";
  out << "                yLabel: 'Percent',\n";
  out << "                clampHundred: true,\n";
  out << "                datasets: [\n";
  out << "                    makeDataset('System RAM Load (%)', ramLoadPrimary, "
         "palettePrimary[1]),\n";
  out << "                    makeDataset('GPU Memory Load (%)', "
         "gpuMemLoadPrimary, palettePrimary[2]),\n";
  out << "                    makeDataset('Comparison RAM Load (%)', "
         "ramLoadComparison, paletteComparison[1], true),\n";
  out << "                    makeDataset('Comparison GPU Memory Load (%)', "
         "gpuMemLoadComparison, paletteComparison[2], true)\n";
  out << "                ]\n";
  out << "            },\n";
  out << "            {\n";
  out << "                cardId: 'gpuMemoryCard',\n";
  out << "                canvasId: 'gpuMemoryChart',\n";
  out << "                title: 'GPU Memory Usage (MB)',\n";
  out << "                yLabel: 'Megabytes',\n";
  out << "                clampHundred: false,\n";
  out << "                datasets: [\n";
  out << "                    makeDataset('GPU Memory (MB)', gpuMemUsagePrimary, "
         "palettePrimary[3]),\n";
  out << "                    makeDataset('Comparison GPU Memory (MB)', "
         "gpuMemUsageComparison, paletteComparison[3], true)\n";
  out << "                ]\n";
  out << "            }\n";
  out << "        ];\n";
  out << "        charts.forEach(cfg => {\n";
  out << "            const filtered = cfg.datasets.filter(ds => ds.data && "
         "ds.data.length);\n";
  out << "            if (!filtered.length) {\n";
  out << "                const card = document.getElementById(cfg.cardId);\n";
  out << "                if (card) card.style.display = 'none';\n";
  out << "                return;\n";
  out << "            }\n";
  out << "            const ctx = "
         "document.getElementById(cfg.canvasId).getContext('2d');\n";
  out << "            new Chart(ctx, {\n";
  out << "                type: 'line',\n";
  out << "                data: { datasets: filtered },\n";
  out << "                options: {\n";
  out << "                    responsive: true,\n";
  out << "                    maintainAspectRatio: false,\n";
  out << "                    interaction: { mode: 'index', intersect: false },\n";
  out << "                    plugins: {\n";
  out << "                        legend: { labels: { color: '#e6e6e6' } },\n";
  out << "                        title: { display: true, text: cfg.title }\n";
  out << "                    },\n";
  out << "                    scales: {\n";
  out << "                        x: {\n";
  out << "                            type: 'linear',\n";
  out << "                            title: { display: true, text: 'Time (sample)' "
         "},\n";
  out << "                            ticks: { color: '#d0d7de' },\n";
  out << "                            grid: { color: 'rgba(255,255,255,0.08)' }\n";
  out << "                        },\n";
  out << "                        y: {\n";
  out << "                            title: { display: true, text: cfg.yLabel },\n";
  out << "                            min: cfg.clampHundred ? 0 : undefined,\n";
  out << "                            max: cfg.clampHundred ? 100 : undefined,\n";
  out << "                            ticks: { color: '#d0d7de' },\n";
  out << "                            grid: { color: 'rgba(255,255,255,0.08)' }\n";
  out << "                        }\n";
  out << "                    }\n";
  out << "                }\n";
  out << "            });\n";
  out << "        });\n";
  out << "    </script>\n";
  out << "</body>\n";
  out << "</html>\n";

  htmlFile.close();

  return htmlFilePath;
}
