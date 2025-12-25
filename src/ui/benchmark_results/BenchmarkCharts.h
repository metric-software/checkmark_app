#pragma once

#include "logging/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QVector>

class BenchmarkCharts {
 public:
  // Enum for Y-axis scaling options
  enum class YAxisScaleType { Automatic, Fixed_0_to_100, Fixed_Custom };

  // Structure for benchmark summary metrics
  struct BenchmarkSummary {
    // Section time boundaries (in seconds)
    static constexpr int beachStartTime = 0;
    static constexpr int beachEndTime = 26;
    static constexpr int flyingStartTime = 26;
    static constexpr int flyingEndTime = 114;
    static constexpr int outpostStartTime = 114;
    static constexpr int outpostEndTime = 124;

    // Section labels
    static const inline QString beachLabel = "Beach";
    static const inline QString jungleLabel = "Jungle";
    static const inline QString outpostLabel = "Outpost";
    static const inline QString overallLabel = "Overall";

    // Beach section metrics
    double beachAvgFps = -1.0;
    double beach1LowFps = -1.0;
    double beach5LowFps = -1.0;

    // Flying section metrics (labeled as "Jungle" for users)
    double flyingAvgFps = -1.0;
    double flying1LowFps = -1.0;
    double flying5LowFps = -1.0;

    // Outpost section metrics
    double outpostAvgFps = -1.0;
    double outpost1LowFps = -1.0;
    double outpost5LowFps = -1.0;

    // Overall metrics (whole benchmark run)
    double overallAvgFps = -1.0;
    double overall1LowFps = -1.0;
    double overall5LowFps = -1.0;

    // Analysis flags
    bool gpuBottleneckLight = false;   // GPU usage > 90% for 5+ seconds
    bool gpuBottleneckSevere = false;  // GPU usage > 90% for 30+ seconds
    bool ramUsageWarning = false;      // Memory load > 90% at any point
    bool vramUsageWarning = false;     // GPU memory usage > 85% at any point
    bool fpsStutteringDetected =
      false;                   // Frame time variance > 3 for 15+ seconds
    int smallFreezeCount = 0;  // Count of highest frame time > 50ms
    int fpsFreezeCount = 0;    // Count of highest frame time > 100ms

    // Legacy metrics - will not be used in the new implementation
    double avgFps = -1.0;
    double minFps = -1.0;
    double maxFps = -1.0;
    double fps1Low = -1.0;
    double fps01Low = -1.0;

    double avgFrameTime = -1.0;
    double minFrameTime = -1.0;
    double maxFrameTime = -1.0;
    double frameTime1High = -1.0;
    double frameTime01High = -1.0;

    double avgCpuUsage = -1.0;
    double maxCpuUsage = -1.0;

    double avgGpuUsage = -1.0;
    double maxGpuUsage = -1.0;

    double avgMemoryUsage = -1.0;
    double maxMemoryUsage = -1.0;
  };

  // Core chart generation methods
  static QString generateHtmlChart(
    const QString& filename, const QString& title, const QString& xLabel,
    const QString& yLabel, const QStringList& dataLabels,
    const QVector<QVector<QPointF>>& datasets,
    YAxisScaleType yScaleType = YAxisScaleType::Automatic,
    double yMinValue = 0.0, double yMaxValue = 100.0);

  static QString generateHtmlChartWithComparison(
    const QString& filename, const QString& title, const QString& xLabel,
    const QString& yLabel, const QStringList& dataLabels,
    const QVector<QVector<QPointF>>& primaryDatasets,
    const QVector<QVector<QPointF>>& comparisonDatasets,
    YAxisScaleType yScaleType = YAxisScaleType::Automatic,
    double yMinValue = 0.0, double yMaxValue = 100.0);

  // Specific chart generation methods
  static QString generateCpuUsageChart(
    const QString& csvFilePath, const QString& comparisonCsvFilePath = "");
  static QString generateFpsChart(const QString& csvFilePath,
                                  const QString& comparisonCsvFilePath = "");
  static QString generateGpuUsageChart(
    const QString& csvFilePath, const QString& comparisonCsvFilePath = "");
  static QString generateMemoryChart(const QString& csvFilePath,
                                     const QString& comparisonCsvFilePath = "");
  static QString generateFrameTimeMetricsChart(
    const QString& csvFilePath, const QString& comparisonCsvFilePath = "");
  static QString generateGpuCpuUsageChart(
    const QString& csvFilePath, const QString& comparisonCsvFilePath = "");
  static QString generateSectionalSummaryHtml(const QString& csvFilePath);

  // Dashboard and report generation
  static QString generateDashboardHtml(
    const QString& csvFilePath, const QString& comparisonCsvFilePath = "");
  static QString generateComparisonHtml(
    const QString& csvFilePath, const QString& comparisonCsvFilePath = "");

  // Utility and Helper methods
  static BenchmarkSummary calculateBenchmarkSummary(const QString& csvFilePath);
  static bool ensureOutputDirExists(QDir& outputDir);
  static QString processComparisonData(const QString& dataColumn,
                                       const QString& csvFilePath,
                                       bool includeLowPercentiles);
  static QString getYScaleOptionsJson(YAxisScaleType scaleType, double minValue,
                                      double maxValue);
  static QStringList generateRandomColors(int count);
  static QString getLineStyleOptions(int metricIndex, bool isComparison);
  static QString getDatasetOptionsJson(bool isComparison);

 private:
  // Private members or helpers can go here if needed in the future.
  // Removed duplicate declarations that were already public static.
};
