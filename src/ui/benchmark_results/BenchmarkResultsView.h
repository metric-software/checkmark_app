#pragma once

#include <vector>

#include <QComboBox>
#include <QDir>
#include <QJsonArray>
#include <QDateTime>
#include <QJsonObject>
#include <QListWidget>
#include <QMap>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QPair>
#include <QSplitter>
#include <QString>
#include <QSet>
#include <QTableWidget>
#include <QWidget>

#include "BenchmarkCharts.h"

class BenchmarkResultsView : public QWidget {
  Q_OBJECT

 public:
  explicit BenchmarkResultsView(QWidget* parent = nullptr);
  void refreshBenchmarkList();

 signals:
  void backRequested();

 private slots:
  void onBenchmarkSelected();
  void onComparisonSelected(int index);
  void onServerComparisonSelected(int index);
  void generateFpsTimeChart();
  void generateFrameTimeChart();
  void generateCpuUsageChart();
  void generateGpuUsageChart();
  void generateMemoryChart();
  void generateGpuCpuUsageChart();
  void generateDashboard();
  void loadComparisonCSVFile(const QString& filePath);

 private:
  // Structure for reference comparison data
  struct ReferenceData {
    QString metric;
    double value;
  };

  struct MetricStats {
    double min = -1;
    double avg = -1;
    double max = -1;
  };

  struct RunSummary {
    QMap<QString, MetricStats> metrics;  // column label -> stats

    // Convenience fields for common metrics
    double avgFps = -1;
    double minFps = -1;
    double maxFps = -1;
    double avgCpuUsage = -1;
    double maxCoreUsage = -1;
    double avgMemUsage = -1;
  };

  // Structure to store average metrics
  struct AverageMetrics {
    // Running totals
    double totalFps = 0.0;
    double totalFrameTime = 0.0;
    double totalHighestFrameTime = 0.0;
    double totalCpuTime = 0.0;
    double totalHighestCpuTime = 0.0;
    double totalGpuTime = 0.0;
    double totalHighestGpuTime = 0.0;
    double totalFrameTimeVariance = 0.0;
    double totalGpuUsage = 0.0;
    double totalGpuMemUsed = 0.0;
    double totalRamUsage = 0.0;
    double totalCpuUsage = 0.0;
    double totalCpuClock = 0.0;

    // Highest values
    double highestFrameTimeOverall = 0.0;
    double highestCpuTimeOverall = 0.0;
    double highestGpuTimeOverall = 0.0;
    double highestFrameTimeVariance = 0.0;
    double highestGpuUsage = 0.0;
    double highestCpuUsage = 0.0;
    double highestCpuClock = 0.0;

    // Final calculated averages
    double avgFps = -1.0;
    double avgFrameTime = -1.0;
    double avgHighestFrameTime = -1.0;
    double avgCpuTime = -1.0;
    double avgHighestCpuTime = -1.0;
    double avgGpuTime = -1.0;
    double avgHighestGpuTime = -1.0;
    double avgFrameTimeVariance = -1.0;
    double avgGpuUsage = -1.0;
    double avgGpuMemUsed = -1.0;
    double avgGpuMemUsedPercent = -1.0;
    double avgRamUsage = -1.0;
    double avgRamUsagePercent = -1.0;
    double avgCpuUsage = -1.0;
    double avgCpuClock = -1.0;

    // Other tracking values
    int clockSampleCount = 0;
    double gpuMemTotal = 0.0;
  };

  // UI setup and data loading methods
  void setupUI();
  bool loadComparisonData();
  void updateComparisonTable(const QString& resultFile);
  void calculateOverallAverages();
  void refreshComparisonFilesList();
  void fetchAllComparisonSets();
  void fetchLeaderboardForMode(const QString& mode);
  void loadCachedLeaderboardRuns();
  bool cacheIsFresh(int maxAgeMinutes) const;
  void setDefaultComparisonFromSelector();
  bool savePublicRunToCsv(const QVariantMap& runMap, const QString& outPath, QString* outLabel = nullptr);
  double calculateAverageFps(const QString& filePath);  // Helper to calculate average FPS from CSV

  // UI components
  QComboBox* resultsList;  // Changed from QListWidget to QComboBox for user runs dropdown
  QTableWidget* comparisonTable;
  QPushButton* backButton;
  QComboBox* comparisonSelector;  // Used for comparison functionality (not visible in UI)
  QComboBox* serverRunSelector;          // server aggregate comparison selector
  QMap<QString, QLabel*> summarySelectedLabels;  // metric -> label
  QMap<QString, QLabel*> summaryAvgLabels;       // metric -> label
  QMap<QString, QLabel*> summaryComparisonLabels; // metric -> label for server comparison
  QStringList summaryRowOrder;
  QWidget* summaryTable = nullptr;
  QGridLayout* summaryGrid = nullptr;
  QString summaryHeaderStyle;
  QString summaryCellStyle;
  QWidget* summaryPanel = nullptr;

  // Visualization buttons
  QPushButton* fpsTimeButton;
  QPushButton* frameTimeButton;
  QPushButton* cpuUsageButton;
  QPushButton* gpuUsageButton;
  QPushButton* gpuCpuUsageButton;
  QPushButton* memoryButton;
  QPushButton* dashboardButton;

  // Data storage
  QDir resultsDir;
  QString currentBenchmarkFile;
  QJsonObject comparisonData;
  AverageMetrics overallAverages;

  // Comparison CSV data
  QString currentComparisonFile;
  QStringList comparisonFiles;
  bool hasComparisonData = false;
  static constexpr int CACHE_MAX_AGE_MINUTES = 60;

  struct ServerAggregateOption {
    QString id;
    QString label;
    QString componentType;
    QString componentName;
    bool isBest = false;
    int runCount = 0;
    RunSummary summary;
    QVariantMap meta;
  };

  QVector<ServerAggregateOption> serverAggregateOptions;
  RunSummary currentComparisonSummary;
  QVector<QPair<QString, QString>> lastServerRuns; // {label, path/identifier}
  int pendingLeaderboardRequests = 0;
  bool anyLeaderboardSuccess = false;
  QSet<QString> knownServerRunIds;

  RunSummary computeRunSummary(const QString& filePath);
  RunSummary computeRunSummaryFromPublic(const QVariantMap& summary);
  RunSummary computeUserAverageSummary();
  void updateSummaryPanel(const RunSummary& selected,
                          const RunSummary& comparison,
                          const RunSummary& avgAll);
  void rebuildSummaryTable(const QStringList& rowOrder);
  void fetchAggregatedComparisons();
  void populateServerComparisonSelector(const QVariantMap& response);

  // Reference values for comparisons
  std::vector<ReferenceData> referenceValues = {
    {"Average FPS", -1.0},
    {"Average Frame Time", -1.0},
    {"Average Highest Frame Time", -1.0},
    {"Highest Frame Time Overall", -1.0},
    {"Average CPU Time", -1.0},
    {"Average Highest CPU Time", -1.0},
    {"Highest CPU Time Overall", -1.0},
    {"Average GPU Time", -1.0},
    {"Average Highest GPU Time", -1.0},
    {"Highest GPU Time Overall", -1.0},
    {"Average Frame Time Variance", -1.0},
    {"Highest Frame Time Variance", -1.0},
    {"Average GPU Usage (%)", -1.0},
    {"Highest GPU Usage (%)", -1.0},
    {"Average GPU Memory Used (MB)", -1.0},
    {"Average GPU Memory Used (%)", -1.0},
    {"Average RAM Usage (MB)", -1.0},
    {"Average RAM Usage (%)", -1.0},
    {"Average CPU Usage (%)", -1.0},
    {"Highest CPU Usage (%)", -1.0},
    {"Average CPU Clock (MHz)", -1.0},
    {"Highest CPU Clock (MHz)", -1.0}};
};
