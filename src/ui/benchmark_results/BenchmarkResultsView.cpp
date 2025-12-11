#include "BenchmarkResultsView.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include <QApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QRegularExpression>
#include <QProcess>
#include <QPushButton>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <QScrollArea>
#include <shlwapi.h>

#include "HtmlReportGenerator.h"
#include "../../network/api/BenchmarkApiClient.h"

#include "logging/Logger.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>  // Add this line to include the ShellExecuteW declaration
#endif

BenchmarkResultsView::BenchmarkResultsView(QWidget* parent) : QWidget(parent) {
  LOG_INFO << "BenchmarkResultsView: Constructor started";

  // Ensure optional visualization buttons start null
  fpsTimeButton = nullptr;
  frameTimeButton = nullptr;
  cpuUsageButton = nullptr;
  gpuUsageButton = nullptr;
  gpuCpuUsageButton = nullptr;
  memoryButton = nullptr;

  try {
    LOG_INFO << "BenchmarkResultsView: Setting up UI";
    setupUI();
    LOG_INFO << "BenchmarkResultsView: UI setup completed";

    // Initialize comparison table and reference values before loading data
    comparisonTable = new QTableWidget(this);
    comparisonTable->hide();  // Add this line to hide the table since it's not
                              // properly added to a layout
    referenceValues.clear();  // Make sure it's initialized

    LOG_INFO << "BenchmarkResultsView: Loading comparison data";
    loadComparisonData();
    LOG_INFO << "BenchmarkResultsView: Comparison data loaded";

    // Load comparison CSV files list
    LOG_INFO << "BenchmarkResultsView: Loading comparison files list";
  
    refreshComparisonFilesList();
    LOG_INFO << "BenchmarkResultsView: Comparison files list loaded";


    LOG_INFO << "BenchmarkResultsView: Refreshing benchmark list";
    refreshBenchmarkList();
    LOG_INFO << "BenchmarkResultsView: Benchmark list refreshed";

    // Auto-fetch comparison sets (prefer cache if fresh, otherwise fetch)
    fetchAllComparisonSets();
  } catch (const std::exception& e) {
    LOG_ERROR << "BenchmarkResultsView: Exception in constructor: " << e.what();
  } catch (...) {
    LOG_ERROR << "BenchmarkResultsView: Unknown exception in constructor";
  }

  LOG_INFO << "BenchmarkResultsView: Constructor completed";
}

// Fix header size and button style
void BenchmarkResultsView::setupUI() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(10);

  // Create a more compact header container
  QWidget* headerWidget = new QWidget(this);
  headerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  headerWidget->setMaximumHeight(84);  // Give more room for added controls
  headerWidget->setStyleSheet(
    "background-color: transparent;");  // Make sure background is transparent

  // Create header with back button and title
  QVBoxLayout* headerLayout = new QVBoxLayout(headerWidget);
  headerLayout->setContentsMargins(10, 2, 10, 2);
  headerLayout->setSpacing(4);

  // First row
  QHBoxLayout* row1 = new QHBoxLayout();
  row1->setSpacing(6);
  backButton = new QPushButton("← Back to Benchmark", this);
  backButton->setStyleSheet(R"(
        QPushButton {
            color: #0078d4;
            background: transparent;
            border: none;
            padding: 2px 4px;
            font-size: 13px;
            text-align: left;
        }
        QPushButton:hover {
            color: #1084d8;
            text-decoration: underline;
        }
        QPushButton:pressed {
            color: #006cc1;
        }
    )");
  backButton->setCursor(Qt::PointingHandCursor);
  connect(backButton, &QPushButton::clicked, this,
          &BenchmarkResultsView::backRequested);

  QLabel* titleLabel = new QLabel("Benchmark Results", this);
  titleLabel->setStyleSheet(
    "color: #ffffff; font-size: 16px; font-weight: bold;");

  // Add row1 widgets
  row1->addWidget(backButton);
  row1->addWidget(titleLabel);
  row1->addStretch();
  headerLayout->addLayout(row1);

  // Second row controls
  QHBoxLayout* row2 = new QHBoxLayout();
  row2->setSpacing(8);

  // Mode selector
  // User runs selector
  QComboBox* userRunsSelector = new QComboBox(this);
  auto comboStyle = QStringLiteral("QComboBox { background-color: #333333; color: #ffffff; border: 1px solid #555555; border-radius: 3px; padding: 2px 8px; min-width: 180px; } QComboBox:disabled { color: #888888; } QComboBox QAbstractItemView { background-color: #333333; color: #ffffff; } QComboBox QAbstractItemView::item { padding: 4px 8px; } QComboBox QAbstractItemView::item:hover { background-color: #404040; }");
  userRunsSelector->setStyleSheet(comboStyle);
  userRunsSelector->addItem("Select benchmark run", QVariant());
  connect(userRunsSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BenchmarkResultsView::onBenchmarkSelected);
  
  // Server run selector (result picker)
  serverRunSelector = new QComboBox(this);
  serverRunSelector->setStyleSheet(comboStyle);
  serverRunSelector->addItem("No comparison selected", QVariant());
  connect(serverRunSelector, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &BenchmarkResultsView::onServerComparisonSelected);

  // Pack row2
  row2->addWidget(new QLabel("User Run:", this));
  row2->addWidget(userRunsSelector);
  row2->addSpacing(12);
  row2->addWidget(new QLabel("Server Comparison:", this));
  row2->addWidget(serverRunSelector);
  row2->addStretch();
  headerLayout->addLayout(row2);

  // Third row for component filters
  QHBoxLayout* row3 = new QHBoxLayout();
  row3->setSpacing(8);

  // Comparison selector (local files) - created but not added to UI layout
  comparisonSelector = new QComboBox(this);
  comparisonSelector->setStyleSheet(R"(
        QComboBox {
            background-color: #333333;
            color: #ffffff;
            border: 1px solid #555555;
            border-radius: 3px;
            padding: 2px 8px;
            min-width: 150px;
        }
        QComboBox:hover {
            border: 1px solid #666666;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 20px;
            border-left: 1px solid #555555;
        }
        QComboBox::down-arrow {
            image: url(:/icons/dropdown_arrow.png);
        }
    )");
  connect(comparisonSelector,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &BenchmarkResultsView::onComparisonSelected);
  comparisonSelector->hide();  // Ensure it's not visible
  row3->addStretch();
  headerLayout->addLayout(row3);

  // Add the header widget to main layout
  mainLayout->addWidget(headerWidget);

  // Add a separator line
  QFrame* separator = new QFrame(this);
  separator->setFrameShape(QFrame::HLine);
  separator->setFrameShadow(QFrame::Sunken);
  separator->setStyleSheet("background-color: #333333;");
  mainLayout->addWidget(separator);

  // Store user runs selector as member variable
  resultsList = userRunsSelector;  // Reuse the variable name for compatibility
      // Common button style
      QString buttonStyle = R"(
        QPushButton {
          background-color: #2a2a2a;
          color: white;
          border: 1px solid #444444;
          padding: 8px;
          border-radius: 4px;
          text-align: left;
        }
        QPushButton:hover {
          background-color: #333333;
          border: 1px solid #666666;
        }
        QPushButton:pressed {
          background-color: #222222;
        }
        QPushButton:disabled {
          background-color: #1e1e1e;
          color: #666666;
          border: 1px solid #333333;
        }
      )";

      // Primary detailed results button (was dashboard)
      dashboardButton = new QPushButton("Detailed Results", this);
      dashboardButton->setStyleSheet(buttonStyle +
        QStringLiteral("\nQPushButton:enabled { background-color: #2f8f2f; border-color: #3fbf3f; }"));
      dashboardButton->setEnabled(false);
      dashboardButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
      dashboardButton->setMaximumWidth(260);

      // Add some horizontal padding via a container layout
      QHBoxLayout* actionRow = new QHBoxLayout();
      actionRow->setContentsMargins(8, 0, 8, 0);
      actionRow->addWidget(dashboardButton, /*stretch*/0, Qt::AlignLeft);
      mainLayout->addLayout(actionRow);

  // Add summary panel on the left (scrollable to avoid stretching window)
  summaryPanel = new QWidget();
  QVBoxLayout* summaryPanelLayout = new QVBoxLayout(summaryPanel);
  summaryPanelLayout->setContentsMargins(10, 10, 10, 10);
  summaryPanelLayout->setSpacing(8);

  QLabel* summaryTitle = new QLabel("Run Summary", summaryPanel);
  summaryTitle->setStyleSheet("color: #ffffff; font-size: 14px; font-weight: bold;");
  summaryPanelLayout->addWidget(summaryTitle);

  summaryTable = new QWidget(summaryPanel);
  summaryGrid = new QGridLayout(summaryTable);
  summaryGrid->setContentsMargins(0, 0, 0, 0);
  summaryGrid->setHorizontalSpacing(12);
  summaryGrid->setVerticalSpacing(6);

  summaryHeaderStyle = "color: #cccccc; font-size: 12px; font-weight: bold;";
  summaryCellStyle = "color: #ffffff; font-size: 12px;";

  rebuildSummaryTable(QStringList{});

  summaryPanelLayout->addWidget(summaryTable);
  summaryPanelLayout->addStretch();

  // Wrap summary panel in a scroll area so long lists don't resize the window
  QScrollArea* summaryScroll = new QScrollArea();
  summaryScroll->setWidget(summaryPanel);
  summaryScroll->setWidgetResizable(true);
  summaryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  summaryScroll->setFrameShape(QFrame::NoFrame);

  // Add summary scroll directly (no split) for full-width summary
  mainLayout->addWidget(summaryScroll);

    // Connect primary action
    connect(dashboardButton, &QPushButton::clicked, this,
      &BenchmarkResultsView::generateDashboard);
}

void BenchmarkResultsView::rebuildSummaryTable(const QStringList& rowOrder) {
  if (!summaryTable || !summaryGrid) return;

  summaryRowOrder = rowOrder;

  while (QLayoutItem* item = summaryGrid->takeAt(0)) {
    if (item->widget()) item->widget()->deleteLater();
    delete item;
  }

  summarySelectedLabels.clear();
  summaryComparisonLabels.clear();
  summaryAvgLabels.clear();

  if (rowOrder.isEmpty()) {
    QLabel* placeholder = new QLabel("Select a run to see summary metrics", summaryTable);
    placeholder->setWordWrap(true);
    placeholder->setStyleSheet(summaryCellStyle);
    summaryGrid->addWidget(placeholder, 0, 0, 1, 4);
    return;
  }

  summaryGrid->addWidget(new QLabel("Metric", summaryTable), 0, 0);
  summaryGrid->addWidget(new QLabel("Selected Run", summaryTable), 0, 1);
  summaryGrid->addWidget(new QLabel("Comparison Run", summaryTable), 0, 2);
  summaryGrid->addWidget(new QLabel("Your Avg", summaryTable), 0, 3);

  for (int col = 0; col < 4; ++col) {
    QLayoutItem* item = summaryGrid->itemAtPosition(0, col);
    if (item && item->widget()) {
      item->widget()->setStyleSheet(summaryHeaderStyle);
    }
  }

  int row = 1;
  for (const QString& key : rowOrder) {
    const QString metricName = key.section('|', 0, 0);
    const QString statName = key.section('|', 1, 1).toUpper();

    QLabel* metricLabel = new QLabel(QStringLiteral("%1 (%2)").arg(metricName, statName), summaryTable);
    metricLabel->setStyleSheet(summaryCellStyle);
    summaryGrid->addWidget(metricLabel, row, 0);

    auto makeValueLabel = [&]() {
      QLabel* lbl = new QLabel("--", summaryTable);
      lbl->setStyleSheet(summaryCellStyle);
      lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      return lbl;
    };

    QLabel* selLabel = makeValueLabel();
    QLabel* cmpLabel = makeValueLabel();
    QLabel* avgLabel = makeValueLabel();

    summaryGrid->addWidget(selLabel, row, 1);
    summaryGrid->addWidget(cmpLabel, row, 2);
    summaryGrid->addWidget(avgLabel, row, 3);

    summarySelectedLabels.insert(key, selLabel);
    summaryComparisonLabels.insert(key, cmpLabel);
    summaryAvgLabels.insert(key, avgLabel);

    ++row;
  }
}

bool BenchmarkResultsView::loadComparisonData() {
  LOG_INFO << "BenchmarkResultsView: loadComparisonData started";

  if (!comparisonTable) {
    LOG_INFO << "BenchmarkResultsView: comparisonTable is null, creating it";
    comparisonTable = new QTableWidget(this);
  }

  // Create benchmark_results directory if it doesn't exist
  QDir dir("benchmark_results");
  if (!dir.exists()) {
    LOG_INFO << "BenchmarkResultsView: Creating benchmark_results directory";
    QDir().mkpath("benchmark_results");
  }

  QFile file("comparison.json");
  if (!file.exists()) {
    LOG_INFO << "BenchmarkResultsView: Comparison file does not exist, using defaults";
    // Create a default comparison structure
    comparisonData = QJsonObject();
    comparisonData["comparisons"] = QJsonArray();
    return false;
  }

  if (!file.open(QIODevice::ReadOnly)) {
    LOG_ERROR << "Could not open comparison file: " << file.errorString().toStdString();
    return false;
  }

  QByteArray jsonData = file.readAll();
  QJsonDocument document = QJsonDocument::fromJson(jsonData);

  if (document.isNull() || !document.isObject()) {
    LOG_ERROR << "Invalid JSON format in comparison file";
    file.close();
    return false;
  }

  comparisonData = document.object();
  file.close();

  // Update the comparison dropdown
  comparisonSelector->clear();

  QJsonArray comparisons = comparisonData["comparisons"].toArray();
  if (comparisons.isEmpty()) {
    LOG_WARN << "No comparison data found";
    return false;
  }

  for (const QJsonValue& value : comparisons) {
    QJsonObject compObj = value.toObject();
    QString name = compObj["name"].toString();
    if (!name.isEmpty()) {
      comparisonSelector->addItem(name);
    }
  }

  // Update table with first comparison
  if (comparisonSelector->count() > 0) {
    onComparisonSelected(0);
  }

  return true;
}

bool BenchmarkResultsView::cacheIsFresh(int maxAgeMinutes) const {
  QDir comparisonDir("comparison_data");
  if (!comparisonDir.exists()) return false;

  QStringList filters;
  filters << "leader_*.csv";
  QFileInfoList files =
    comparisonDir.entryInfoList(filters, QDir::Files, QDir::Time);
  if (files.isEmpty()) return false;

  QDateTime newest = files.first().lastModified();
  int minutes = newest.secsTo(QDateTime::currentDateTimeUtc()) / 60;
  return minutes < maxAgeMinutes;
}

void BenchmarkResultsView::loadCachedLeaderboardRuns() {
  QDir comparisonDir("comparison_data");
  if (!comparisonDir.exists()) {
    LOG_WARN << "No comparison_data directory found; skipping local comparison load";
    return;
  }

  QStringList filters;
  filters << "leader_*.csv";
  QFileInfoList files =
    comparisonDir.entryInfoList(filters, QDir::Files, QDir::Time);

  serverRunSelector->blockSignals(true);
  serverRunSelector->clear();
  serverRunSelector->addItem("No comparison selected", QVariant());

  int added = 0;
  for (const QFileInfo& file : files) {
    QString path = file.filePath();

    QFile csv(path);
    if (!csv.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
    QTextStream in(&csv);
    QString header = in.readLine();
    csv.close();
    if (!header.contains("FPS") || !header.contains("Frame Time")) continue;

    double avgFps = calculateAverageFps(path);
    QString label = QString("Cached: %1").arg(file.baseName());
    if (avgFps > 0) {
      label.append(QString(" (%1 FPS)").arg(QString::number(avgFps, 'f', 1)));
    }

    serverRunSelector->addItem(label, path);
    added++;
  }
  serverRunSelector->blockSignals(false);

  if (added == 0) {
    LOG_WARN << "No cached leaderboard data available";
  }

  setDefaultComparisonFromSelector();
}

void BenchmarkResultsView::setDefaultComparisonFromSelector() {
  // Pick the first available comparison as default for charts
  if (!serverRunSelector) return;
  if (serverRunSelector->count() > 1) {
    QString path = serverRunSelector->itemData(1).toString();
    if (!path.isEmpty()) {
      currentComparisonFile = path;
      hasComparisonData = true;
    }
  } else {
    currentComparisonFile.clear();
    hasComparisonData = false;
  }
}

void BenchmarkResultsView::onComparisonSelected(int index) {
  if (index < 0) return;

  hasComparisonData = false;
  currentComparisonFile.clear();

  if (index == 0) {
    // No comparison selected
    return;
  }

  // Check if this is a CSV file comparison or a reference/server public run
  QString selectedText = comparisonSelector->itemText(index);
  // Attempt: if item data holds a run_id prefixed with "run:" then fetch from server
  QVariant itemData = comparisonSelector->itemData(index);
  if (itemData.isValid() && itemData.toString().startsWith("run:")) {
    QString runId = itemData.toString().mid(4);
    LOG_INFO << "BenchmarkResultsView: fetching public run from server, runId=" << runId.toStdString();
    auto* api = new BenchmarkApiClient(this);
    connect(api, &BaseApiClient::requestStarted, this, [](const QString& p){ LOG_INFO << "GET public run started: " << p.toStdString(); });
    connect(api, &BaseApiClient::requestCompleted, this, [](const QString& p, bool ok){ LOG_INFO << "GET public run completed: " << p.toStdString() << ", ok=" << ok; });
    api->getPublicRun(runId, [this, runId](bool ok, const QVariant& data, const QString& err){
      if (!ok) {
        LOG_ERROR << "Public run fetch failed: " << err.toStdString();
        return;
      }
      LOG_INFO << "Public run fetch success for runId=" << runId.toStdString();
      QVariantMap map = data.toMap();
      // Save to temporary CSV in comparison_data
      QDir dir("comparison_data"); if (!dir.exists()) QDir().mkpath("comparison_data");
      QString outPath = dir.filePath(QString("server_%1.csv").arg(runId));
      if (savePublicRunToCsv(map, outPath)) {
        currentComparisonFile = outPath;
        hasComparisonData = true;
        LOG_INFO << "Saved server public run to CSV: " << outPath.toStdString();
        // resultsList is a QComboBox: check currentIndex() and use currentData()
        if (resultsList->currentIndex() > 0) {
          QString filePath = resultsList->currentData().toString();
          updateComparisonTable(filePath);
        }
      }
    });
    return;
  }
  if (selectedText.startsWith("Reference:")) {
    // This is a reference value from the JSON
    int jsonIndex = index - comparisonFiles.size() - 1;
    if (jsonIndex < 0 || comparisonData.isEmpty()) return;

    QJsonArray comparisons = comparisonData["comparisons"].toArray();
    if (jsonIndex >= comparisons.size()) return;

    QJsonObject comparison = comparisons[jsonIndex].toObject();
    QJsonObject metrics = comparison["metrics"].toObject();

    // Update the comparison column in the table
    for (int i = 0; i < referenceValues.size(); ++i) {
      QString metricName = referenceValues[i].metric;

      // Match the metric name to the JSON key
      QString jsonKey = metricName.toLower().replace(" ", "_");

      double value = metrics[jsonKey].toDouble(-1.0);
      referenceValues[i].value = value;

      QTableWidgetItem* compItem = comparisonTable->item(i, 3);
      if (compItem) {
        if (value >= 0) {
          compItem->setText(QString::number(value, 'f', 1));
        } else {
          compItem->setText("-");
        }
      }
    }
  } else {
    // This is a CSV file comparison
    if (index - 1 < comparisonFiles.size()) {
      currentComparisonFile = comparisonSelector->itemData(index).toString();
      if (!currentComparisonFile.isEmpty()) {
        loadComparisonCSVFile(currentComparisonFile);
        hasComparisonData = true;
      }
    }
  }

  // If we have a benchmark selected, update the comparison
  if (resultsList->currentIndex() > 0) {
    QString filePath = resultsList->currentData().toString();
    updateComparisonTable(filePath);
  }
}

void BenchmarkResultsView::fetchAllComparisonSets() {
  LOG_INFO << "BenchmarkResultsView: Fetching aggregated benchmark comparisons";
  fetchAggregatedComparisons();
}

void BenchmarkResultsView::onServerComparisonSelected(int index) {
  if (!serverRunSelector) return;

  if (index <= 0) {
    currentComparisonSummary = RunSummary{};
  } else {
    QString id = serverRunSelector->itemData(index).toString();
    currentComparisonSummary = RunSummary{};
    for (const auto& opt : serverAggregateOptions) {
      if (opt.id == id) {
        currentComparisonSummary = opt.summary;
        break;
      }
    }
  }

  RunSummary selected = RunSummary{};
  if (resultsList && resultsList->currentIndex() > 0) {
    QString filePath = resultsList->currentData().toString();
    selected = computeRunSummary(filePath);
  }
  RunSummary avgSummary = computeUserAverageSummary();
  updateSummaryPanel(selected, currentComparisonSummary, avgSummary);
}

void BenchmarkResultsView::fetchAggregatedComparisons() {
  if (!serverRunSelector) return;

  serverRunSelector->blockSignals(true);
  serverRunSelector->clear();
  serverRunSelector->addItem("No comparison selected", QVariant());
  serverRunSelector->blockSignals(false);

  auto* api = new BenchmarkApiClient(this);
  connect(api, &BaseApiClient::requestStarted, this,
          [](const QString& p) { LOG_INFO << "GET benchmark aggregates started: " << p.toStdString(); });
  connect(api, &BaseApiClient::requestCompleted, this,
          [](const QString& p, bool ok) { LOG_INFO << "GET benchmark aggregates completed: " << p.toStdString()
                                                 << ", ok=" << (ok ? "true" : "false"); });

  api->getBenchmarkAggregates([this, api](bool ok, const QVariant& data, const QString& err) {
    api->deleteLater();
    if (!ok) {
      LOG_ERROR << "Benchmark aggregates fetch failed: " << err.toStdString();
      return;
    }
    if (data.type() != QVariant::Map) {
      LOG_ERROR << "Benchmark aggregates: unexpected payload type " << data.typeName();
      return;
    }
    populateServerComparisonSelector(data.toMap());
  });
}

void BenchmarkResultsView::populateServerComparisonSelector(const QVariantMap& response) {
  serverAggregateOptions.clear();
  currentComparisonSummary = RunSummary{};

  if (!serverRunSelector) return;

  serverRunSelector->blockSignals(true);
  serverRunSelector->clear();
  serverRunSelector->addItem("No comparison selected", QVariant());

  auto addOption = [&](const QString& id, const QString& label, const QVariantMap& summaryMap,
                       const QString& compType, const QString& compName, bool isBest,
                       int runCount, const QVariantMap& meta) {
    ServerAggregateOption opt;
    opt.id = id;
    opt.label = label;
    opt.componentType = compType;
    opt.componentName = compName;
    opt.isBest = isBest;
    opt.runCount = runCount;
    opt.summary = computeRunSummaryFromPublic(summaryMap);
    opt.meta = meta;
    serverAggregateOptions.push_back(opt);
    serverRunSelector->addItem(label, id);
  };

  auto safeSummaryLabel = [](const QVariantMap& summaryMap) -> QString {
    double fps = summaryMap.value(QStringLiteral("avg_fps")).toDouble();
    if (fps <= 0) return QStringLiteral("-- FPS");
    return QStringLiteral("%1 FPS").arg(QString::number(fps, 'f', 1));
  };

  QVariantMap overall = response.value(QStringLiteral("overall")).toMap();
  if (!overall.isEmpty()) {
    int runs = overall.value(QStringLiteral("run_count")).toInt();
    QVariantMap avgSum = overall.value(QStringLiteral("average_summary")).toMap();
    if (!avgSum.isEmpty()) {
      QString label = QString("Overall Avg (%1)").arg(safeSummaryLabel(avgSum));
      addOption(QStringLiteral("overall:avg"), label, avgSum, QStringLiteral("overall"), QString(), false, runs, QVariantMap());
    }

    QVariantMap best = overall.value(QStringLiteral("best_run")).toMap();
    QVariantMap bestSum = best.value(QStringLiteral("summary")).toMap();
    QVariantMap bestMeta = best.value(QStringLiteral("meta")).toMap();
    if (!bestSum.isEmpty()) {
      QString label = QString("Overall Best (%1)").arg(safeSummaryLabel(bestSum));
      addOption(QStringLiteral("overall:best"), label, bestSum, QStringLiteral("overall"), QString(), true, runs, bestMeta);
    }
  }

  QVariantMap comps = response.value(QStringLiteral("components")).toMap();
  auto typeLabel = [](const QString& key) -> QString {
    if (key == "cpu") return QStringLiteral("CPU");
    if (key == "gpu") return QStringLiteral("GPU");
    if (key == "memory_clock") return QStringLiteral("Memory Clock");
    if (key == "memory_total") return QStringLiteral("Memory Total");
    return key.toUpper();
  };

  for (auto it = comps.begin(); it != comps.end(); ++it) {
    QString compType = it.key();
    QVariantList list = it.value().toList();
    for (const QVariant& v : list) {
      QVariantMap agg = v.toMap();
      int runs = agg.value(QStringLiteral("run_count")).toInt();
      QString compName = agg.value(QStringLiteral("component_name")).toString();
      QVariantMap avgSum = agg.value(QStringLiteral("average_summary")).toMap();
      if (!avgSum.isEmpty()) {
        QString lbl = QString("%1 %2 Avg (%3)").arg(typeLabel(compType)).arg(compName, safeSummaryLabel(avgSum));
        QString id = QString("%1:%2:avg").arg(compType, compName);
        addOption(id, lbl, avgSum, compType, compName, false, runs, QVariantMap());
      }

      QVariantMap best = agg.value(QStringLiteral("best_run")).toMap();
      QVariantMap bestSum = best.value(QStringLiteral("summary")).toMap();
      QVariantMap bestMeta = best.value(QStringLiteral("meta")).toMap();
      if (!bestSum.isEmpty()) {
        QString lbl = QString("%1 %2 Best (%3)").arg(typeLabel(compType)).arg(compName, safeSummaryLabel(bestSum));
        QString id = QString("%1:%2:best").arg(compType, compName);
        addOption(id, lbl, bestSum, compType, compName, true, runs, bestMeta);
      }
    }
  }

  serverRunSelector->blockSignals(false);

  if (serverRunSelector->count() > 1) {
    serverRunSelector->setCurrentIndex(1);
    onServerComparisonSelected(1);
  } else {
    currentComparisonSummary = RunSummary{};
    onServerComparisonSelected(0);
  }
}

void BenchmarkResultsView::fetchLeaderboardForMode(const QString& mode) {
  QVariantMap query;
  query.insert("mode", mode);

  LOG_INFO << "BenchmarkResultsView: querying leaderboard, mode="
           << mode.toStdString();

  auto* api = new BenchmarkApiClient(this);
  connect(api, &BaseApiClient::requestStarted, this,
          [](const QString& p) {
            LOG_INFO << "POST leaderboard started: " << p.toStdString();
          });
  connect(api, &BaseApiClient::requestCompleted, this,
          [](const QString& p, bool ok) {
            LOG_INFO << "POST leaderboard completed: " << p.toStdString()
                     << ", ok=" << (ok ? "true" : "false");
          });

  api->queryLeaderboard(
    query, [this, mode](bool ok, const QVariant& data, const QString& err) {
      pendingLeaderboardRequests =
        std::max(0, pendingLeaderboardRequests - 1);

      if (!ok) {
        LOG_ERROR << "Leaderboard fetch failed for mode " << mode.toStdString()
                  << ": " << err.toStdString();
      if (pendingLeaderboardRequests == 0 && !anyLeaderboardSuccess) {
        loadCachedLeaderboardRuns();
      } else if (pendingLeaderboardRequests == 0) {
        setDefaultComparisonFromSelector();
      }
      return;
    }

      if (data.type() != QVariant::Map) {
        LOG_WARN << "Expected QVariantMap but got: " << data.typeName();
      if (pendingLeaderboardRequests == 0 && !anyLeaderboardSuccess) {
        loadCachedLeaderboardRuns();
      } else if (pendingLeaderboardRequests == 0) {
        setDefaultComparisonFromSelector();
      }
      return;
    }

      QVariantMap m = data.toMap();
      QVariantList runs = m.value("runs").toList();
      if (!runs.isEmpty()) {
        anyLeaderboardSuccess = true;
      }

      QDir dir("comparison_data");
      if (!dir.exists()) QDir().mkpath("comparison_data");

      serverRunSelector->blockSignals(true);

      for (const auto& v : runs) {
        QVariantMap run = v.toMap();
        QVariantMap meta = run.value("meta").toMap();
        QString runId = meta.value("run_id").toString();
        if (runId.isEmpty()) continue;

        if (knownServerRunIds.contains(runId)) {
          continue;
        }

        QVariantMap summary = run.value("summary").toMap();
        QString label = QString("[%1] %2 FPS | %3 | %4")
                          .arg(mode)
                          .arg(QString::number(
                            summary.value("avg_fps").toDouble(), 'f', 1))
                          .arg(summary.value("cpu_model").toString())
                          .arg(summary.value("gpu_primary_model").toString());

        QString outPath =
          dir.filePath(QString("leader_%1.csv").arg(runId));

        if (savePublicRunToCsv(run, outPath, &label)) {
          knownServerRunIds.insert(runId);
          serverRunSelector->addItem(label, outPath);
          lastServerRuns.append({label, outPath});
          LOG_INFO << "Saved leaderboard run to CSV (mode="
                   << mode.toStdString() << "): " << outPath.toStdString();
        }
      }

      serverRunSelector->blockSignals(false);

      if (pendingLeaderboardRequests == 0 && !anyLeaderboardSuccess) {
        loadCachedLeaderboardRuns();
      } else if (pendingLeaderboardRequests == 0) {
        setDefaultComparisonFromSelector();
      }
    });
}

bool BenchmarkResultsView::savePublicRunToCsv(const QVariantMap& runMap, const QString& outPath, QString* outLabel) {
  // Convert PublicRunResponse.variant -> CSV with columns similar to local CSV expectations
  QVariantList samples = runMap.value("samples").toList();
  if (samples.isEmpty()) {
    LOG_WARN << "savePublicRunToCsv: no samples present in server response; skipping CSV write";
    return false;
  }

  QFile file(outPath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open csv for write: " << outPath.toStdString();
    return false;
  }
  QTextStream out(&file);
  // Basic header subset to be compatible with our charts
  out << "Time,FPS,Frame Time,Highest Frame Time,Frame Time Variance,GPU Usage,GPU Mem Used,GPU Mem Total,Memory Usage (MB),Total CPU Usage (%)" << "\n";

  // Prepare per-core columns if present
  // Determine max core index
  int maxCore = -1;
  for (const auto& sv : samples) {
    QVariantMap s = sv.toMap();
    QVariantList cores = s.value("core_usages").toList();
    for (const auto& cv : cores) {
      maxCore = std::max(maxCore, cv.toMap().value("core_index").toInt());
    }
  }
  // If cores exist, append header for clocks/usages (we only have usage pct here)
  if (maxCore >= 0) {
    // rewrite header with per-core usage columns appended
    file.seek(0);
    file.resize(0);
    QTextStream out2(&file);
    QString header = "Time,FPS,Frame Time,Highest Frame Time,Frame Time Variance,GPU Usage,GPU Mem Used,GPU Mem Total,Memory Usage (MB),Total CPU Usage (%)";
    for (int i = 0; i <= maxCore; ++i) header += QString(",Core %1 (%)").arg(i);
    out2 << header << "\n";
  }
  // Write rows
  for (const auto& sv : samples) {
    QVariantMap s = sv.toMap();
    auto csv = [&](const QVariant& v){ return v.isNull() ? QString() : v.toString(); };
    QStringList cols;
  cols << csv(s.value("time"))
         << csv(s.value("fps"))
         << csv(s.value("frame_time_ms"))
         << csv(s.value("highest_frame_time_ms"))
         << csv(s.value("frame_time_variance"))
     << csv(s.value("gpu_usage_pct"))
     << (s.value("gpu_mem_used_bytes").isNull() ? QString() : QString::number(s.value("gpu_mem_used_bytes").toULongLong() / 1048576.0, 'f', 2))
     << (s.value("gpu_mem_total_bytes").isNull() ? QString() : QString::number(s.value("gpu_mem_total_bytes").toULongLong() / 1048576.0, 'f', 2))
     << csv(s.value("memory_usage_mb"))
     << QString(); // CPU usage not included in public samples currently

    // per-core usages in order 0..maxCore
    if (maxCore >= 0) {
      QMap<int, double> coreMap;
      for (const auto& cv : s.value("core_usages").toList()) {
        QVariantMap c = cv.toMap(); coreMap.insert(c.value("core_index").toInt(), c.value("usage_pct").toDouble());
      }
      for (int i = 0; i <= maxCore; ++i) cols << (coreMap.contains(i) ? QString::number(coreMap.value(i)) : QString());
    }
    out << cols.join(",") << "\n";
  }
  file.close();

  if (outLabel) {
    QVariantMap meta = runMap.value("meta").toMap();
    QVariantMap sum = runMap.value("summary").toMap();
    *outLabel = QString("%1 FPS | %2 | %3")
                  .arg(QString::number(sum.value("avg_fps").toDouble(), 'f', 1))
                  .arg(sum.value("cpu_model").toString())
                  .arg(sum.value("gpu_primary_model").toString());
  }
  return true;
}

void BenchmarkResultsView::loadComparisonCSVFile(const QString& filePath) {
  LOG_INFO << "Loading comparison CSV file: " << filePath.toStdString();

  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open comparison file: " << filePath.toStdString();
    return;
  }

  // Just verify that the file is valid, for now
  QTextStream in(&file);
  QString header = in.readLine();
  if (!header.contains("FPS") || !header.contains("Frame Time")) {
    file.close();
    LOG_ERROR << "Invalid comparison file format";
    return;
  }

  // File is valid, the actual data reading will happen in the chart generation
  // methods
  file.close();
  currentComparisonFile = filePath;
  LOG_INFO << "Comparison file validated and set";
}

// Helper function to calculate average FPS from a CSV file
double BenchmarkResultsView::calculateAverageFps(const QString& filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return -1.0;
  }
  
  QTextStream in(&file);
  QString header = in.readLine();
  QStringList headers = header.split(",");
  
  int fpsIndex = headers.indexOf("FPS");
  if (fpsIndex < 0) {
    file.close();
    return -1.0;
  }
  
  double totalFps = 0.0;
  int lineCount = 0;
  
  while (!in.atEnd()) {
    QString line = in.readLine();
    QStringList fields = line.split(",");
    
    if (fields.size() <= fpsIndex) continue;
    
    double fps = fields[fpsIndex].toDouble();
    if (fps > 0) {
      totalFps += fps;
      lineCount++;
    }
  }
  
  file.close();
  return lineCount > 0 ? totalFps / lineCount : -1.0;
}

BenchmarkResultsView::RunSummary BenchmarkResultsView::computeRunSummary(
  const QString& filePath) {
  RunSummary summary;
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return summary;
  }

  QTextStream in(&file);
  QString header = in.readLine();
  QStringList headers = header.split(",");

  struct StatAccumulator {
    double sum = 0.0;
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::lowest();
    int count = 0;
  };

  QMap<QString, StatAccumulator> accumulators;
  const int memUsageIdx = headers.indexOf("Memory Usage (MB)");
  const int memAvailIdx = headers.indexOf("PDH_Memory_Available(MB)");
  const int memLimitIdx = headers.indexOf("PDH_Memory_Commit_Limit(bytes)");

  auto addSample = [&](const QString& name, double value) {
    if (!std::isfinite(value)) return;

    StatAccumulator& acc = accumulators[name];
    acc.sum += value;
    acc.count++;
    acc.min = (acc.count == 1) ? value : std::min(acc.min, value);
    acc.max = (acc.count == 1) ? value : std::max(acc.max, value);
  };

  while (!in.atEnd()) {
    QString line = in.readLine();
    QStringList fields = line.split(",");
    if (fields.size() < headers.size()) continue;

    for (int i = 0; i < headers.size(); ++i) {
      QString metricName = headers[i].trimmed();
      if (metricName.isEmpty() || metricName.compare("Time", Qt::CaseInsensitive) == 0)
        continue;
      if (i >= fields.size()) continue;

      bool ok = false;
      double v = fields[i].toDouble(&ok);
      if (!ok) continue;

      addSample(metricName, v);
    }

    // Derive memory usage if explicit column is missing
    if (memUsageIdx < 0 && memAvailIdx >= 0 && memLimitIdx >= 0 &&
        memAvailIdx < fields.size() && memLimitIdx < fields.size()) {
      bool okA = false, okL = false;
      double avail = fields[memAvailIdx].toDouble(&okA);
      double limitBytes = fields[memLimitIdx].toDouble(&okL);
      if (okA && okL) {
        double usedMb = (limitBytes / 1048576.0) - avail;
        if (usedMb >= 0) {
          addSample(QStringLiteral("Memory Usage (MB)"), usedMb);
        }
      }
    }
  }

  file.close();

  for (auto it = accumulators.constBegin(); it != accumulators.constEnd(); ++it) {
    const StatAccumulator& acc = it.value();
    if (acc.count == 0) continue;

    MetricStats stats;
    stats.avg = acc.sum / acc.count;
    stats.min = acc.min;
    stats.max = acc.max;
    summary.metrics.insert(it.key(), stats);
  }

  // Convenience lookups for common metrics
  auto fpsIt = summary.metrics.find(QStringLiteral("FPS"));
  if (fpsIt != summary.metrics.end()) {
    summary.avgFps = fpsIt->avg;
    summary.minFps = fpsIt->min;
    summary.maxFps = fpsIt->max;
  }

  QString cpuKey;
  if (summary.metrics.contains(QStringLiteral("PDH_CPU_Usage(%)"))) {
    cpuKey = QStringLiteral("PDH_CPU_Usage(%)");
  } else if (summary.metrics.contains(QStringLiteral("Total CPU Usage (%)"))) {
    cpuKey = QStringLiteral("Total CPU Usage (%)");
  }
  if (!cpuKey.isEmpty()) {
    summary.avgCpuUsage = summary.metrics.value(cpuKey).avg;
  }

  QRegularExpression pdhCoreRe(QStringLiteral(R"(^PDH_Core\s+\d+\s+CPU\s*\(%\)$)"));
  QRegularExpression coreRe(QStringLiteral(R"(^Core\s+\d+\s*\(%\)$)"));
  double maxCore = -1;
  for (auto it = summary.metrics.constBegin(); it != summary.metrics.constEnd(); ++it) {
    if (pdhCoreRe.match(it.key()).hasMatch() || coreRe.match(it.key()).hasMatch()) {
      maxCore = std::max(maxCore, it.value().max);
    }
  }
  summary.maxCoreUsage = maxCore;

  if (summary.metrics.contains(QStringLiteral("Memory Usage (MB)"))) {
    summary.avgMemUsage = summary.metrics.value(QStringLiteral("Memory Usage (MB)")).avg;
  }

  return summary;
}

BenchmarkResultsView::RunSummary BenchmarkResultsView::computeRunSummaryFromPublic(
  const QVariantMap& summary) {
  RunSummary r;
  if (summary.isEmpty()) return r;

  r.avgFps = summary.value(QStringLiteral("avg_fps")).toDouble();

  QVariantList colStats = summary.value(QStringLiteral("column_stats")).toList();
  for (const QVariant& v : colStats) {
    QVariantMap stat = v.toMap();
    const QString column = stat.value(QStringLiteral("column")).toString();
    const int valid = stat.value(QStringLiteral("valid_samples")).toInt();
    if (column.isEmpty() || valid <= 0) continue;

    MetricStats ms;
    ms.avg = stat.value(QStringLiteral("avg")).toDouble();
    ms.min = stat.value(QStringLiteral("min")).toDouble();
    ms.max = stat.value(QStringLiteral("max")).toDouble();
    r.metrics.insert(column, ms);
  }

  if (!r.metrics.contains(QStringLiteral("FPS")) && r.avgFps >= 0) {
    MetricStats ms;
    ms.avg = r.avgFps;
    r.metrics.insert(QStringLiteral("FPS"), ms);
  }

  // Convenience lookups for common metrics
  auto fpsIt = r.metrics.find(QStringLiteral("FPS"));
  if (fpsIt != r.metrics.end()) {
    r.minFps = fpsIt->min;
    r.maxFps = fpsIt->max;
  }

  QString cpuKey;
  if (r.metrics.contains(QStringLiteral("PDH_CPU_Usage(%)"))) {
    cpuKey = QStringLiteral("PDH_CPU_Usage(%)");
  } else if (r.metrics.contains(QStringLiteral("Total CPU Usage (%)"))) {
    cpuKey = QStringLiteral("Total CPU Usage (%)");
  }
  if (!cpuKey.isEmpty()) {
    r.avgCpuUsage = r.metrics.value(cpuKey).avg;
  }

  QRegularExpression pdhCoreRe(QStringLiteral(R"(^PDH_Core\s+\d+\s+CPU\s*\(%\)$)"));
  QRegularExpression coreRe(QStringLiteral(R"(^Core\s+\d+\s*\(%\)$)"));
  double maxCore = -1.0;
  for (auto it = r.metrics.constBegin(); it != r.metrics.constEnd(); ++it) {
    if (pdhCoreRe.match(it.key()).hasMatch() || coreRe.match(it.key()).hasMatch()) {
      maxCore = std::max(maxCore, it.value().max);
    }
  }
  if (maxCore >= 0) {
    r.maxCoreUsage = maxCore;
  }

  if (r.metrics.contains(QStringLiteral("Memory Usage (MB)"))) {
    r.avgMemUsage = r.metrics.value(QStringLiteral("Memory Usage (MB)")).avg;
  }

  return r;
}

BenchmarkResultsView::RunSummary BenchmarkResultsView::computeUserAverageSummary() {
  RunSummary agg;
  QDir resultsDir("benchmark_results");
  if (!resultsDir.exists()) return agg;

  QStringList filters;
  filters << "*.csv";
  QFileInfoList files = resultsDir.entryInfoList(filters, QDir::Files, QDir::Time);
  if (files.isEmpty()) return agg;

  struct Aggregate {
    double sumMin = 0.0;
    double sumAvg = 0.0;
    double sumMax = 0.0;
    int countMin = 0;
    int countAvg = 0;
    int countMax = 0;
  };

  QMap<QString, Aggregate> totals;

  for (const QFileInfo& fi : files) {
    RunSummary r = computeRunSummary(fi.filePath());
    for (auto it = r.metrics.constBegin(); it != r.metrics.constEnd(); ++it) {
      const MetricStats& ms = it.value();
      Aggregate& aggEntry = totals[it.key()];

      if (ms.min >= 0) { aggEntry.sumMin += ms.min; aggEntry.countMin++; }
      if (ms.avg >= 0) { aggEntry.sumAvg += ms.avg; aggEntry.countAvg++; }
      if (ms.max >= 0) { aggEntry.sumMax += ms.max; aggEntry.countMax++; }
    }
  }

  for (auto it = totals.constBegin(); it != totals.constEnd(); ++it) {
    const Aggregate& a = it.value();
    MetricStats ms;
    ms.min = a.countMin ? a.sumMin / a.countMin : -1;
    ms.avg = a.countAvg ? a.sumAvg / a.countAvg : -1;
    ms.max = a.countMax ? a.sumMax / a.countMax : -1;
    agg.metrics.insert(it.key(), ms);
  }

  auto fpsIt = agg.metrics.find(QStringLiteral("FPS"));
  if (fpsIt != agg.metrics.end()) {
    agg.avgFps = fpsIt->avg;
    agg.minFps = fpsIt->min;
    agg.maxFps = fpsIt->max;
  }

  QString cpuKey;
  if (agg.metrics.contains(QStringLiteral("PDH_CPU_Usage(%)"))) {
    cpuKey = QStringLiteral("PDH_CPU_Usage(%)");
  } else if (agg.metrics.contains(QStringLiteral("Total CPU Usage (%)"))) {
    cpuKey = QStringLiteral("Total CPU Usage (%)");
  }
  if (!cpuKey.isEmpty()) {
    agg.avgCpuUsage = agg.metrics.value(cpuKey).avg;
  }

  QRegularExpression pdhCoreRe(QStringLiteral(R"(^PDH_Core\s+\d+\s+CPU\s*\(%\)$)"));
  QRegularExpression coreRe(QStringLiteral(R"(^Core\s+\d+\s*\(%\)$)"));
  double maxCore = -1.0;
  for (auto it = agg.metrics.constBegin(); it != agg.metrics.constEnd(); ++it) {
    if (pdhCoreRe.match(it.key()).hasMatch() || coreRe.match(it.key()).hasMatch()) {
      maxCore = std::max(maxCore, it.value().max);
    }
  }
  if (maxCore >= 0) agg.maxCoreUsage = maxCore;

  if (agg.metrics.contains(QStringLiteral("Memory Usage (MB)"))) {
    agg.avgMemUsage = agg.metrics.value(QStringLiteral("Memory Usage (MB)")).avg;
  }

  return agg;
}

void BenchmarkResultsView::updateSummaryPanel(const RunSummary& selected,
                                              const RunSummary& comparison,
                                              const RunSummary& avgAll) {
  if (!summaryGrid) return;

  auto makeKey = [](const QString& metric, const QString& stat) {
    return QStringLiteral("%1|%2").arg(metric, stat);
  };

  QSet<QString> metricNames;
  auto collectMetrics = [&](const RunSummary& summary) {
    for (auto it = summary.metrics.constBegin(); it != summary.metrics.constEnd(); ++it) {
      metricNames.insert(it.key());
    }
  };

  collectMetrics(selected);
  collectMetrics(comparison);
  collectMetrics(avgAll);

  QStringList metricList = metricNames.values();
  std::sort(metricList.begin(), metricList.end(), [](const QString& a, const QString& b) {
    return a.toLower() < b.toLower();
  });

  QStringList desiredRows;
  const QStringList statOrder = {QStringLiteral("avg"), QStringLiteral("min"), QStringLiteral("max")};
  for (const QString& metric : metricList) {
    for (const QString& stat : statOrder) {
      desiredRows << makeKey(metric, stat);
    }
  }

  if (desiredRows != summaryRowOrder) {
    rebuildSummaryTable(desiredRows);
  }

  if (summaryRowOrder.isEmpty()) return;

  auto valueFor = [](const RunSummary& summary, const QString& metric, const QString& stat) -> double {
    auto it = summary.metrics.find(metric);
    if (it == summary.metrics.end()) return -1;
    if (stat == "avg") return it->avg;
    if (stat == "min") return it->min;
    if (stat == "max") return it->max;
    return -1;
  };

  auto fmt = [](double v) {
    if (v < 0) return QStringLiteral("--");
    return QString::number(v, 'f', 1);
  };

  for (const QString& key : summaryRowOrder) {
    const QString metric = key.section('|', 0, 0);
    const QString stat = key.section('|', 1, 1);

    QString valSel = fmt(valueFor(selected, metric, stat));
    QString valCmp = fmt(valueFor(comparison, metric, stat));
    QString valAvg = fmt(valueFor(avgAll, metric, stat));

    if (summarySelectedLabels.contains(key)) summarySelectedLabels[key]->setText(valSel);
    if (summaryComparisonLabels.contains(key)) summaryComparisonLabels[key]->setText(valCmp);
    if (summaryAvgLabels.contains(key)) summaryAvgLabels[key]->setText(valAvg);
  }
}

// Fix benchmark list item display - now populates dropdown with avg FPS + date
void BenchmarkResultsView::refreshBenchmarkList() {
  LOG_INFO << "BenchmarkResultsView: refreshBenchmarkList started";

  if (!resultsList) {
    LOG_ERROR << "BenchmarkResultsView: resultsList is null, cannot refresh";
    return;
  }

  resultsList->clear();
  resultsList->addItem("Select benchmark run", QVariant()); // Default item

  // Make sure benchmark_results directory exists
  QDir resultsDir("benchmark_results");
  if (!resultsDir.exists()) {
    LOG_INFO << "BenchmarkResultsView: benchmark_results directory does not "
                 "exist, creating it";
    QDir().mkpath("benchmark_results");
    return;
  }

  QStringList filters;
  filters << "*.csv";
  QFileInfoList files =
    resultsDir.entryInfoList(filters, QDir::Files, QDir::Time);

  for (const QFileInfo& file : files) {
    // Read first few lines to verify it's a valid benchmark file
    QFile csvFile(file.filePath());
    if (csvFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&csvFile);
      QString header = in.readLine();

      // Check if this is a valid benchmark file by looking for known headers
      if (header.contains("FPS") && header.contains("Frame Time")) {
        csvFile.close();
        
        // Calculate average FPS
        double avgFps = calculateAverageFps(file.filePath());
        
        // Format display with avg FPS (orange) and date/time
        QString displayDate = file.lastModified().toString("yyyy-MM-dd HH:mm");
        QString displayText;
        
        if (avgFps > 0) {
          displayText = QString("%1 FPS — %2")
                          .arg(QString::number(avgFps, 'f', 1))
                          .arg(displayDate);
        } else {
          displayText = QString("-- FPS — %1")
                          .arg(displayDate);
        }
        
        resultsList->addItem(displayText, file.filePath());
      } else {
        csvFile.close();
      }
    }
  }
}

void BenchmarkResultsView::onBenchmarkSelected() {
  int index = resultsList->currentIndex();
  if (index <= 0) {
    // Disable all buttons if no benchmark is selected
    if (dashboardButton) dashboardButton->setEnabled(false);
    updateSummaryPanel(RunSummary{}, currentComparisonSummary, RunSummary{});
    return;
  }

  QString filePath = resultsList->itemData(index).toString();
  currentBenchmarkFile = filePath;

  LOG_INFO << "Selected benchmark file: " << filePath.toStdString();

  // Load the benchmark file and check available metrics to determine which
  // buttons to enable
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    LOG_ERROR << "Failed to open benchmark file: " << filePath.toStdString();
    return;
  }

  // Read header to identify available metrics
  QTextStream in(&file);
  QString header = in.readLine();
  QStringList headers = header.split(",");

  LOG_INFO << "CSV Headers: " << header.toStdString();

  // Check for performance metrics
  bool hasFpsData = headers.contains("FPS");
  bool hasFrameTimeData = headers.contains("Frame Time");
  bool hasCpuData = headers.contains("CPU Usage") ||
                    headers.contains("Total CPU Usage (%)") ||
                    headers.contains("PDH_CPU_Usage(%)") ||
                    headers.indexOf(
                      QRegularExpression("^PDH_Core\\s+\\d+\\s+CPU\\s*\\(%\\)")) >=
                      0;
  bool hasGpuData = headers.contains("GPU Usage") ||
                    headers.contains("GPU Usage (%)") ||
                    headers.contains("GPU Mem Used");
  bool hasMemoryData =
    headers.contains("Memory Usage (MB)") ||
    headers.contains("RAM Used") ||
    headers.contains("PDH_Memory_Load(%)") ||
    (headers.indexOf(QRegularExpression("^PDH_Memory_Available\\(MB\\)")) >= 0 &&
     headers.indexOf(QRegularExpression("^PDH_Memory_Commit_Limit\\(bytes\\)")) >= 0) ||
    headers.contains("GPU Mem Used");

  LOG_INFO << "Found metrics - FPS: " << hasFpsData
            << ", Frame Time: " << hasFrameTimeData << ", CPU: " << hasCpuData
            << ", GPU: " << hasGpuData << ", Memory: " << hasMemoryData;

  // Check for CPU core usage data
  bool hasCpuCoreData = false;
  for (const QString& header : headers) {
    if ((header.contains("Core") && header.contains("(%)")) ||
        (header.contains("Core") &&
         header.contains(QRegularExpression("\\d+")))) {
      hasCpuCoreData = true;
      LOG_INFO << "Found CPU core column: " << header.toStdString();
      break;
    }
  }

  LOG_INFO << "Has CPU core data: " << hasCpuCoreData;

  // Enable/disable buttons based on available data
  if (dashboardButton)
    dashboardButton->setEnabled(hasFpsData || hasFrameTimeData || hasCpuData ||
                                hasGpuData);

  file.close();

  // Update summary panel
  RunSummary selectedSummary = computeRunSummary(filePath);
  RunSummary avgSummary = computeUserAverageSummary();
  updateSummaryPanel(selectedSummary, currentComparisonSummary, avgSummary);
}

void BenchmarkResultsView::updateComparisonTable(const QString& resultFile) {
  QFile file(resultFile);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return;
  }

  // Read and parse the CSV file
  QTextStream in(&file);
  QString header = in.readLine();
  QStringList headers = header.split(",");

  // Prepare for metrics calculation
  double totalFps = 0.0;
  double totalFrameTime = 0.0;
  double totalHighestFrameTime = 0.0;
  double highestFrameTimeOverall = 0.0;

  double totalCpuTime = 0.0;
  double totalHighestCpuTime = 0.0;
  double highestCpuTimeOverall = 0.0;

  double totalGpuTime = 0.0;
  double totalHighestGpuTime = 0.0;
  double highestGpuTimeOverall = 0.0;

  double totalFrameTimeVariance = 0.0;
  double highestFrameTimeVariance = 0.0;

  double totalGpuUsage = 0.0;
  double highestGpuUsage = 0.0;

  double totalGpuMemUsed = 0.0;
  double totalGpuMemTotal = 0.0;  // For percentage calculation

  double totalRamUsage = 0.0;
  double totalSystemMemory = 16000.0;  // Default 16GB, should get from system

  double totalCpuUsage = 0.0;
  double highestCpuUsage = 0.0;

  double totalCpuClock = 0.0;
  double highestCpuClock = 0.0;
  int clockSampleCount = 0;

  int lineCount = 0;

  // Find column indices - updated to match the actual CSV format
  int fpsIndex = headers.indexOf("FPS");
  int frameTimeIndex = headers.indexOf("Frame Time");
  int highestFrameTimeIndex = headers.indexOf("Highest Frame Time");
  int cpuTimeIndex = -1;  // Not in the CSV example
  int highestCpuTimeIndex = headers.indexOf("Highest CPU Time");
  int gpuTimeIndex = -1;  // Not in the CSV example
  int highestGpuTimeIndex = headers.indexOf("Highest GPU Time");
  int frameTimeVarianceIndex = headers.indexOf("Frame Time Variance");
  int gpuUsageIndex = headers.indexOf("GPU Usage");
  int gpuMemUsedIndex = headers.indexOf("GPU Mem Used");
  int gpuMemTotalIndex = headers.indexOf("GPU Mem Total");
  int memoryUsageIndex = headers.indexOf("Memory Usage (MB)");
  int cpuUsageIndex = headers.indexOf("Total CPU Usage (%)");

  // Initialize array of core clock indices
  QVector<int> coreClockIndices;
  for (int i = 0; i < headers.size(); i++) {
    if (headers[i].contains("Core") && headers[i].contains("Clock (MHz)")) {
      coreClockIndices.append(i);
    }
  }

  while (!in.atEnd()) {
    QString line = in.readLine();
    QStringList fields = line.split(",");

    if (fields.size() < headers.size()) continue;  // Skip incomplete lines

    // Skip invalid data points
    if (fpsIndex >= 0 && fields[fpsIndex].toDouble() <= 0) continue;

    lineCount++;

    // Process each metric
    if (fpsIndex >= 0) totalFps += fields[fpsIndex].toDouble();
    if (frameTimeIndex >= 0)
      totalFrameTime += fields[frameTimeIndex].toDouble();

    if (highestFrameTimeIndex >= 0) {
      double highFrameTime = fields[highestFrameTimeIndex].toDouble();
      totalHighestFrameTime += highFrameTime;
      highestFrameTimeOverall =
        std::max(highestFrameTimeOverall, highFrameTime);
    }

    if (cpuTimeIndex >= 0) totalCpuTime += fields[cpuTimeIndex].toDouble();

    if (highestCpuTimeIndex >= 0) {
      double highCpuTime = fields[highestCpuTimeIndex].toDouble();
      totalHighestCpuTime += highCpuTime;
      highestCpuTimeOverall = std::max(highestCpuTimeOverall, highCpuTime);
    }

    if (gpuTimeIndex >= 0) totalGpuTime += fields[gpuTimeIndex].toDouble();

    if (highestGpuTimeIndex >= 0) {
      double highGpuTime = fields[highestGpuTimeIndex].toDouble();
      totalHighestGpuTime += highGpuTime;
      highestGpuTimeOverall = std::max(highestGpuTimeOverall, highGpuTime);
    }

    if (frameTimeVarianceIndex >= 0) {
      double variance = fields[frameTimeVarianceIndex].toDouble();
      totalFrameTimeVariance += variance;
      highestFrameTimeVariance = std::max(highestFrameTimeVariance, variance);
    }

    if (gpuUsageIndex >= 0) {
      double gpuUsage = fields[gpuUsageIndex].toDouble();
      totalGpuUsage += gpuUsage;
      highestGpuUsage = std::max(highestGpuUsage, gpuUsage);
    }

    if (gpuMemUsedIndex >= 0)
      totalGpuMemUsed += fields[gpuMemUsedIndex].toDouble();
    if (gpuMemTotalIndex >= 0)
      totalGpuMemTotal =
        fields[gpuMemTotalIndex].toDouble();  // Just take the last one

    if (memoryUsageIndex >= 0)
      totalRamUsage += fields[memoryUsageIndex].toDouble();

    if (cpuUsageIndex >= 0) {
      double cpuUsage = fields[cpuUsageIndex].toDouble();
      totalCpuUsage += cpuUsage;
      highestCpuUsage = std::max(highestCpuUsage, cpuUsage);
    }

    // Process CPU clock speeds
    if (!coreClockIndices.isEmpty()) {
      double maxClockThisRow = 0;
      double totalClockThisRow = 0;
      int validClocks = 0;

      for (int idx : coreClockIndices) {
        if (idx < fields.size()) {
          double clock = fields[idx].toDouble();
          if (clock > 0) {
            totalClockThisRow += clock;
            maxClockThisRow = std::max(maxClockThisRow, clock);
            validClocks++;
          }
        }
      }

      if (validClocks > 0) {
        totalCpuClock += (totalClockThisRow / validClocks);
        highestCpuClock = std::max(highestCpuClock, maxClockThisRow);
        clockSampleCount++;
      }
    }
  }

  file.close();

  // Calculate final averages
  double avgFps = lineCount > 0 ? totalFps / lineCount : -1.0;
  double avgFrameTime = lineCount > 0 ? totalFrameTime / lineCount : -1.0;
  double avgHighestFrameTime =
    lineCount > 0 ? totalHighestFrameTime / lineCount : -1.0;
  double avgCpuTime = lineCount > 0 ? totalCpuTime / lineCount : -1.0;
  double avgHighestCpuTime =
    lineCount > 0 ? totalHighestCpuTime / lineCount : -1.0;
  double avgGpuTime = lineCount > 0 ? totalGpuTime / lineCount : -1.0;
  double avgHighestGpuTime =
    lineCount > 0 ? totalHighestGpuTime / lineCount : -1.0;
  double avgFrameTimeVariance =
    lineCount > 0 ? totalFrameTimeVariance / lineCount : -1.0;
  double avgGpuUsage = lineCount > 0 ? totalGpuUsage / lineCount : -1.0;
  double avgGpuMemUsed = lineCount > 0 ? totalGpuMemUsed / lineCount : -1.0;
  double avgGpuMemUsedPercent =
    (totalGpuMemTotal > 0 && lineCount > 0)
      ? (totalGpuMemUsed / lineCount) / totalGpuMemTotal * 100.0
      : -1.0;
  double avgRamUsage = lineCount > 0 ? totalRamUsage / lineCount : -1.0;
  double avgRamUsagePercent = (totalSystemMemory > 0 && avgRamUsage > 0)
                                ? avgRamUsage / totalSystemMemory * 100.0
                                : -1.0;
  double avgCpuUsage = lineCount > 0 ? totalCpuUsage / lineCount : -1.0;
  double avgCpuClock =
    clockSampleCount > 0 ? totalCpuClock / clockSampleCount : -1.0;

  // Also calculate the overall averages across all benchmark files
  calculateOverallAverages();

  // Now update the table with our calculated values
  for (int row = 0; row < comparisonTable->rowCount(); ++row) {
    QString metric = comparisonTable->item(row, 0)->text();
    QTableWidgetItem* selectedItem = new QTableWidgetItem();
    QTableWidgetItem* avgItem = new QTableWidgetItem();

    double value = -1.0;
    double overallValue = -1.0;

    // Match the metric to our calculated values
    if (metric == "Average FPS") {
      value = avgFps;
      overallValue = overallAverages.avgFps;
    } else if (metric == "Average Frame Time") {
      value = avgFrameTime;
      overallValue = overallAverages.avgFrameTime;
    } else if (metric == "Average Highest Frame Time") {
      value = avgHighestFrameTime;
      overallValue = overallAverages.avgHighestFrameTime;
    } else if (metric == "Highest Frame Time Overall") {
      value = highestFrameTimeOverall;
      overallValue = overallAverages.highestFrameTimeOverall;
    } else if (metric == "Average CPU Time") {
      value = avgCpuTime;
      overallValue = overallAverages.avgCpuTime;
    } else if (metric == "Average Highest CPU Time") {
      value = avgHighestCpuTime;
      overallValue = overallAverages.avgHighestCpuTime;
    } else if (metric == "Highest CPU Time Overall") {
      value = highestCpuTimeOverall;
      overallValue = overallAverages.highestCpuTimeOverall;
    } else if (metric == "Average GPU Time") {
      value = avgGpuTime;
      overallValue = overallAverages.avgGpuTime;
    } else if (metric == "Average Highest GPU Time") {
      value = avgHighestGpuTime;
      overallValue = overallAverages.avgHighestGpuTime;
    } else if (metric == "Highest GPU Time Overall") {
      value = highestGpuTimeOverall;
      overallValue = overallAverages.highestGpuTimeOverall;
    } else if (metric == "Average Frame Time Variance") {
      value = avgFrameTimeVariance;
      overallValue = overallAverages.avgFrameTimeVariance;
    } else if (metric == "Highest Frame Time Variance") {
      value = highestFrameTimeVariance;
      overallValue = overallAverages.highestFrameTimeVariance;
    } else if (metric == "Average GPU Usage (%)") {
      value = avgGpuUsage;
      overallValue = overallAverages.avgGpuUsage;
    } else if (metric == "Highest GPU Usage (%)") {
      value = highestGpuUsage;
      overallValue = overallAverages.highestGpuUsage;
    } else if (metric == "Average GPU Memory Used (MB)") {
      value = avgGpuMemUsed;
      overallValue = overallAverages.avgGpuMemUsed;
    } else if (metric == "Average GPU Memory Used (%)") {
      value = avgGpuMemUsedPercent;
      overallValue = overallAverages.avgGpuMemUsedPercent;
    } else if (metric == "Average RAM Usage (MB)") {
      value = avgRamUsage;
      overallValue = overallAverages.avgRamUsage;
    } else if (metric == "Average RAM Usage (%)") {
      value = avgRamUsagePercent;
      overallValue = overallAverages.avgRamUsagePercent;
    } else if (metric == "Average CPU Usage (%)") {
      value = avgCpuUsage;
      overallValue = overallAverages.avgCpuUsage;
    } else if (metric == "Highest CPU Usage (%)") {
      value = highestCpuUsage;
      overallValue = overallAverages.highestCpuUsage;
    } else if (metric == "Average CPU Clock (MHz)") {
      value = avgCpuClock;
      overallValue = overallAverages.avgCpuClock;
    } else if (metric == "Highest CPU Clock (MHz)") {
      value = highestCpuClock;
      overallValue = overallAverages.highestCpuClock;
    }

    // Set values for selected result
    if (value >= 0) {
      selectedItem->setText(QString::number(value, 'f', 1));
    } else {
      selectedItem->setText("-");
    }

    // Set values for overall average
    if (overallValue >= 0) {
      avgItem->setText(QString::number(overallValue, 'f', 1));
    } else {
      avgItem->setText("-");
    }

    selectedItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    selectedItem->setFlags(selectedItem->flags() & ~Qt::ItemIsEditable);

    avgItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    avgItem->setFlags(avgItem->flags() & ~Qt::ItemIsEditable);

    // Set colors based on comparison with reference value
    double refValue = referenceValues[row].value;
    QBrush color = QColor("#FFFFFF");  // Default white

    if (refValue > 0 && value > 0) {
      if (metric.contains("FPS") || metric.contains("Usage") ||
          metric.contains("Clock")) {
        // For these metrics, higher is better
        if (value > refValue) {
          color = QColor("#44FF44");  // Green
        } else if (value < refValue * 0.8) {
          color = QColor("#FF4444");  // Red
        } else {
          color = QColor("#FFAA00");  // Yellow
        }
      } else if (metric.contains("Time") || metric.contains("Variance")) {
        // For time metrics, lower is better
        if (value < refValue) {
          color = QColor("#44FF44");  // Green
        } else if (value > refValue * 1.2) {
          color = QColor("#FF4444");  // Red
        } else {
          color = QColor("#FFAA00");  // Yellow
        }
      }
    }

    selectedItem->setForeground(color);

    comparisonTable->setItem(row, 1, selectedItem);
    comparisonTable->setItem(row, 2, avgItem);
  }
}

// Add this function to calculate overall averages across all benchmark files
void BenchmarkResultsView::calculateOverallAverages() {
  QDir resultsDir("benchmark_results");
  if (!resultsDir.exists()) {
    return;
  }

  // Reset all averages
  overallAverages = AverageMetrics();

  // Track totals across all files
  AverageMetrics totals;
  int fileCount = 0;

  QStringList filters;
  filters << "*.csv";
  QFileInfoList files =
    resultsDir.entryInfoList(filters, QDir::Files, QDir::Time);

  for (const QFileInfo& file : files) {
    QFile csvFile(file.filePath());
    if (csvFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&csvFile);
      QString header = in.readLine();

      // Check if this is a valid benchmark file
      if (header.contains("FPS") && header.contains("Frame Time")) {
        fileCount++;

        // Process this file
        QStringList headers = header.split(",");

        // Find column indices
        int fpsIndex = headers.indexOf("FPS");
        int frameTimeIndex = headers.indexOf("Frame Time");
        int highestFrameTimeIndex = headers.indexOf("Highest Frame Time");
        int cpuTimeIndex = -1;  // Not in the CSV example
        int highestCpuTimeIndex = headers.indexOf("Highest CPU Time");
        int gpuTimeIndex = -1;  // Not in the CSV example
        int highestGpuTimeIndex = headers.indexOf("Highest GPU Time");
        int frameTimeVarianceIndex = headers.indexOf("Frame Time Variance");
        int gpuUsageIndex = headers.indexOf("GPU Usage");
        int gpuMemUsedIndex = headers.indexOf("GPU Mem Used");
        int gpuMemTotalIndex = headers.indexOf("GPU Mem Total");
        int memoryUsageIndex = headers.indexOf("Memory Usage (MB)");
        int cpuUsageIndex = headers.indexOf("Total CPU Usage (%)");

        // Initialize array of core clock indices
        QVector<int> coreClockIndices;
        for (int i = 0; i < headers.size(); i++) {
          if (headers[i].contains("Core") &&
              headers[i].contains("Clock (MHz)")) {
            coreClockIndices.append(i);
          }
        }

        AverageMetrics fileMetrics;
        int lineCount = 0;

        while (!in.atEnd()) {
          QString line = in.readLine();
          QStringList fields = line.split(",");

          if (fields.size() < headers.size()) continue;
          if (fpsIndex >= 0 && fields[fpsIndex].toDouble() <= 0) continue;

          lineCount++;

          // Process each metric
          if (fpsIndex >= 0)
            fileMetrics.totalFps += fields[fpsIndex].toDouble();
          if (frameTimeIndex >= 0)
            fileMetrics.totalFrameTime += fields[frameTimeIndex].toDouble();

          if (highestFrameTimeIndex >= 0) {
            double val = fields[highestFrameTimeIndex].toDouble();
            fileMetrics.totalHighestFrameTime += val;
            fileMetrics.highestFrameTimeOverall =
              std::max(fileMetrics.highestFrameTimeOverall, val);
          }

          if (cpuTimeIndex >= 0)
            fileMetrics.totalCpuTime += fields[cpuTimeIndex].toDouble();

          if (highestCpuTimeIndex >= 0) {
            double val = fields[highestCpuTimeIndex].toDouble();
            fileMetrics.totalHighestCpuTime += val;
            fileMetrics.highestCpuTimeOverall =
              std::max(fileMetrics.highestCpuTimeOverall, val);
          }

          if (gpuTimeIndex >= 0)
            fileMetrics.totalGpuTime += fields[gpuTimeIndex].toDouble();

          if (highestGpuTimeIndex >= 0) {
            double val = fields[highestGpuTimeIndex].toDouble();
            fileMetrics.totalHighestGpuTime += val;
            fileMetrics.highestGpuTimeOverall =
              std::max(fileMetrics.highestGpuTimeOverall, val);
          }

          if (frameTimeVarianceIndex >= 0) {
            double val = fields[frameTimeVarianceIndex].toDouble();
            fileMetrics.totalFrameTimeVariance += val;
            fileMetrics.highestFrameTimeVariance =
              std::max(fileMetrics.highestFrameTimeVariance, val);
          }

          if (gpuUsageIndex >= 0) {
            double val = fields[gpuUsageIndex].toDouble();
            fileMetrics.totalGpuUsage += val;
            fileMetrics.highestGpuUsage =
              std::max(fileMetrics.highestGpuUsage, val);
          }

          if (gpuMemUsedIndex >= 0)
            fileMetrics.totalGpuMemUsed += fields[gpuMemUsedIndex].toDouble();
          if (gpuMemTotalIndex >= 0)
            fileMetrics.gpuMemTotal = fields[gpuMemTotalIndex].toDouble();

          if (memoryUsageIndex >= 0)
            fileMetrics.totalRamUsage += fields[memoryUsageIndex].toDouble();

          if (cpuUsageIndex >= 0) {
            double val = fields[cpuUsageIndex].toDouble();
            fileMetrics.totalCpuUsage += val;
            fileMetrics.highestCpuUsage =
              std::max(fileMetrics.highestCpuUsage, val);
          }

          // Process CPU clock speeds
          if (!coreClockIndices.isEmpty()) {
            double maxClockThisRow = 0;
            double totalClockThisRow = 0;
            int validClocks = 0;

            for (int idx : coreClockIndices) {
              if (idx < fields.size()) {
                double clock = fields[idx].toDouble();
                if (clock > 0) {
                  totalClockThisRow += clock;
                  maxClockThisRow = std::max(maxClockThisRow, clock);
                  validClocks++;
                }
              }
            }

            if (validClocks > 0) {
              fileMetrics.totalCpuClock += (totalClockThisRow / validClocks);
              fileMetrics.highestCpuClock =
                std::max(fileMetrics.highestCpuClock, maxClockThisRow);
              fileMetrics.clockSampleCount++;
            }
          }
        }

        csvFile.close();

        // Calculate file averages and add to totals
        if (lineCount > 0) {
          // Calculate file averages
          fileMetrics.avgFps = fileMetrics.totalFps / lineCount;
          fileMetrics.avgFrameTime = fileMetrics.totalFrameTime / lineCount;
          fileMetrics.avgHighestFrameTime =
            fileMetrics.totalHighestFrameTime / lineCount;
          fileMetrics.avgCpuTime = fileMetrics.totalCpuTime / lineCount;
          fileMetrics.avgHighestCpuTime =
            fileMetrics.totalHighestCpuTime / lineCount;
          fileMetrics.avgGpuTime = fileMetrics.totalGpuTime / lineCount;
          fileMetrics.avgHighestGpuTime =
            fileMetrics.totalHighestGpuTime / lineCount;
          fileMetrics.avgFrameTimeVariance =
            fileMetrics.totalFrameTimeVariance / lineCount;
          fileMetrics.avgGpuUsage = fileMetrics.totalGpuUsage / lineCount;
          fileMetrics.avgGpuMemUsed = fileMetrics.totalGpuMemUsed / lineCount;
          fileMetrics.avgGpuMemUsedPercent =
            fileMetrics.gpuMemTotal > 0
              ? (fileMetrics.avgGpuMemUsed / fileMetrics.gpuMemTotal * 100.0)
              : -1.0;
          fileMetrics.avgRamUsage = fileMetrics.totalRamUsage / lineCount;
          fileMetrics.avgRamUsagePercent =
            fileMetrics.avgRamUsage / 16000.0 * 100.0;  // Assuming 16GB total
          fileMetrics.avgCpuUsage = fileMetrics.totalCpuUsage / lineCount;
          fileMetrics.avgCpuClock =
            fileMetrics.clockSampleCount > 0
              ? fileMetrics.totalCpuClock / fileMetrics.clockSampleCount
              : -1.0;

          // Add file metrics to overall totals
          totals.avgFps += fileMetrics.avgFps;
          totals.avgFrameTime += fileMetrics.avgFrameTime;
          totals.avgHighestFrameTime += fileMetrics.avgHighestFrameTime;
          totals.avgCpuTime += fileMetrics.avgCpuTime;
          totals.avgHighestCpuTime += fileMetrics.avgHighestCpuTime;
          totals.avgGpuTime += fileMetrics.avgGpuTime;
          totals.avgHighestGpuTime += fileMetrics.avgHighestGpuTime;
          totals.avgFrameTimeVariance += fileMetrics.avgFrameTimeVariance;
          totals.avgGpuUsage += fileMetrics.avgGpuUsage;
          totals.avgGpuMemUsed += fileMetrics.avgGpuMemUsed;
          totals.avgGpuMemUsedPercent += fileMetrics.avgGpuMemUsedPercent;
          totals.avgRamUsage += fileMetrics.avgRamUsage;
          totals.avgRamUsagePercent += fileMetrics.avgRamUsagePercent;
          totals.avgCpuUsage += fileMetrics.avgCpuUsage;
          totals.avgCpuClock += fileMetrics.avgCpuClock;

          // Keep track of highest values across all files
          totals.highestFrameTimeOverall =
            std::max(totals.highestFrameTimeOverall,
                     fileMetrics.highestFrameTimeOverall);
          totals.highestCpuTimeOverall = std::max(
            totals.highestCpuTimeOverall, fileMetrics.highestCpuTimeOverall);
          totals.highestGpuTimeOverall = std::max(
            totals.highestGpuTimeOverall, fileMetrics.highestGpuTimeOverall);
          totals.highestFrameTimeVariance =
            std::max(totals.highestFrameTimeVariance,
                     fileMetrics.highestFrameTimeVariance);
          totals.highestGpuUsage =
            std::max(totals.highestGpuUsage, fileMetrics.highestGpuUsage);
          totals.highestCpuUsage =
            std::max(totals.highestCpuUsage, fileMetrics.highestCpuUsage);
          totals.highestCpuClock =
            std::max(totals.highestCpuClock, fileMetrics.highestCpuClock);
        }
      }
    }
  }

  // Calculate final averages across all files
  if (fileCount > 0) {
    overallAverages.avgFps = totals.avgFps / fileCount;
    overallAverages.avgFrameTime = totals.avgFrameTime / fileCount;
    overallAverages.avgHighestFrameTime =
      totals.avgHighestFrameTime / fileCount;
    overallAverages.avgCpuTime = totals.avgCpuTime / fileCount;
    overallAverages.avgHighestCpuTime = totals.avgHighestCpuTime / fileCount;
    overallAverages.avgGpuTime = totals.avgGpuTime / fileCount;
    overallAverages.avgHighestGpuTime = totals.avgHighestGpuTime / fileCount;
    overallAverages.avgFrameTimeVariance =
      totals.avgFrameTimeVariance / fileCount;
    overallAverages.avgGpuUsage = totals.avgGpuUsage / fileCount;
    overallAverages.avgGpuMemUsed = totals.avgGpuMemUsed / fileCount;
    overallAverages.avgGpuMemUsedPercent =
      totals.avgGpuMemUsedPercent / fileCount;
    overallAverages.avgRamUsage = totals.avgRamUsage / fileCount;
    overallAverages.avgRamUsagePercent = totals.avgRamUsagePercent / fileCount;
    overallAverages.avgCpuUsage = totals.avgCpuUsage / fileCount;
    overallAverages.avgCpuClock = totals.avgCpuClock / fileCount;

    // Set highest values
    overallAverages.highestFrameTimeOverall = totals.highestFrameTimeOverall;
    overallAverages.highestCpuTimeOverall = totals.highestCpuTimeOverall;
    overallAverages.highestGpuTimeOverall = totals.highestGpuTimeOverall;
    overallAverages.highestFrameTimeVariance = totals.highestFrameTimeVariance;
    overallAverages.highestGpuUsage = totals.highestGpuUsage;
    overallAverages.highestCpuUsage = totals.highestCpuUsage;
    overallAverages.highestCpuClock = totals.highestCpuClock;
  }
}

void BenchmarkResultsView::generateFpsTimeChart() {
  if (currentBenchmarkFile.isEmpty()) return;

  QString htmlFile;
  if (hasComparisonData && !currentComparisonFile.isEmpty()) {
    htmlFile = BenchmarkCharts::generateFpsChart(currentBenchmarkFile,
                                                 currentComparisonFile);
  } else {
    htmlFile = BenchmarkCharts::generateFpsChart(currentBenchmarkFile);
  }

  if (!htmlFile.isEmpty()) {
    HtmlReportGenerator::openHtmlInBrowser(htmlFile);
  }
}

void BenchmarkResultsView::generateFrameTimeChart() {
  if (currentBenchmarkFile.isEmpty()) return;

  QString htmlFile;
  if (hasComparisonData && !currentComparisonFile.isEmpty()) {
    htmlFile = BenchmarkCharts::generateFrameTimeMetricsChart(
      currentBenchmarkFile, currentComparisonFile);
  } else {
    htmlFile =
      BenchmarkCharts::generateFrameTimeMetricsChart(currentBenchmarkFile);
  }

  if (!htmlFile.isEmpty()) {
    HtmlReportGenerator::openHtmlInBrowser(htmlFile);
  }
}

void BenchmarkResultsView::generateCpuUsageChart() {
  if (currentBenchmarkFile.isEmpty()) return;

  QString htmlFile;
  if (hasComparisonData && !currentComparisonFile.isEmpty()) {
    htmlFile = BenchmarkCharts::generateCpuUsageChart(currentBenchmarkFile,
                                                      currentComparisonFile);
  } else {
    htmlFile = BenchmarkCharts::generateCpuUsageChart(currentBenchmarkFile);
  }

  if (!htmlFile.isEmpty()) {
    HtmlReportGenerator::openHtmlInBrowser(htmlFile);
  }
}

void BenchmarkResultsView::generateGpuUsageChart() {
  if (currentBenchmarkFile.isEmpty()) return;

  QString htmlFile;
  if (hasComparisonData && !currentComparisonFile.isEmpty()) {
    htmlFile = BenchmarkCharts::generateGpuUsageChart(currentBenchmarkFile,
                                                      currentComparisonFile);
  } else {
    htmlFile = BenchmarkCharts::generateGpuUsageChart(currentBenchmarkFile);
  }

  if (!htmlFile.isEmpty()) {
    HtmlReportGenerator::openHtmlInBrowser(htmlFile);
  }
}

void BenchmarkResultsView::generateGpuCpuUsageChart() {
  if (currentBenchmarkFile.isEmpty()) return;

  QString htmlFile;
  if (hasComparisonData && !currentComparisonFile.isEmpty()) {
    htmlFile = BenchmarkCharts::generateGpuCpuUsageChart(currentBenchmarkFile,
                                                         currentComparisonFile);
  } else {
    htmlFile = BenchmarkCharts::generateGpuCpuUsageChart(currentBenchmarkFile);
  }

  if (!htmlFile.isEmpty()) {
    HtmlReportGenerator::openHtmlInBrowser(htmlFile);
  }
}

void BenchmarkResultsView::generateMemoryChart() {
  if (currentBenchmarkFile.isEmpty()) return;

  QString htmlFile;
  if (hasComparisonData && !currentComparisonFile.isEmpty()) {
    htmlFile = BenchmarkCharts::generateMemoryChart(currentBenchmarkFile,
                                                    currentComparisonFile);
  } else {
    htmlFile = BenchmarkCharts::generateMemoryChart(currentBenchmarkFile);
  }

  if (!htmlFile.isEmpty()) {
    HtmlReportGenerator::openHtmlInBrowser(htmlFile);
  }
}

void BenchmarkResultsView::generateDashboard() {
  if (currentBenchmarkFile.isEmpty()) return;

  QString htmlFile;
  if (hasComparisonData && !currentComparisonFile.isEmpty()) {
    htmlFile = BenchmarkCharts::generateDashboardHtml(currentBenchmarkFile,
                                                      currentComparisonFile);
  } else {
    htmlFile = BenchmarkCharts::generateDashboardHtml(currentBenchmarkFile);
  }

  if (!htmlFile.isEmpty()) {
    HtmlReportGenerator::openHtmlInBrowser(htmlFile);
  }
}

// Add this new method to scan for comparison CSV files
void BenchmarkResultsView::refreshComparisonFilesList() {
  // First add an empty option for no comparison
  comparisonSelector->clear();
  comparisonSelector->addItem("Select Comparison...");

  // Look for CSV files in the comparison_data directory
  QDir comparisonDir("comparison_data");
  if (comparisonDir.exists()) {
    QStringList filters;
    filters << "*.csv";  // Only look for CSV files
    QFileInfoList files =
      comparisonDir.entryInfoList(filters, QDir::Files, QDir::Time);

    comparisonFiles.clear();

    for (const QFileInfo& file : files) {
      // Check file extension first, before any further processing
      if (file.suffix().toLower() != "csv") {
        continue;
      }

      // Read first few lines to verify it's a valid benchmark file
      QFile csvFile(file.filePath());
      if (csvFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&csvFile);
        QString header = in.readLine();

        // Check if this is a valid benchmark file by looking for known headers
        if (header.contains("FPS") && header.contains("Frame Time")) {
          // Just display a formatted filename
          QString displayName = file.baseName();
          comparisonSelector->addItem(displayName, file.filePath());
          comparisonFiles.append(file.filePath());
        }
        csvFile.close();
      }
    }
  }

  // Skip loading reference values from JSON for now
  // The test_1 reference will no longer be included
}

