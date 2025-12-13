#include "DiagnosticView.h"

#include <algorithm>  // For std::max
#include <iostream>
#include <memory>
#include <vector>
#include "../logging/Logger.h"

#include <QApplication>  // Add this include for QApplication::processEvents()
#include <QCheckBox>
#include <QDebug>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QSignalBlocker>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "../ApplicationSettings.h"  // Add this include for ApplicationSettings
#include "CustomWidgetWithTitle.h"
#include "SettingsDropdown.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "diagnostic/background_process_worker.h"
#include "hardware/ConstantSystemInfo.h"  // Add this include
#include "renderers/BackgroundProcessRenderer.h"
#include "renderers/CPUResultRenderer.h"
#include "renderers/DiagnosticViewComponents.h"
#include "renderers/DriveResultRenderer.h"
#include "renderers/GPUResultRenderer.h"
#include "renderers/MemoryResultRenderer.h"
#include "renderers/NetworkResultRenderer.h"
#include "network/api/DownloadApiClient.h"

DiagnosticView::DiagnosticView(QWidget* parent)
    : QWidget(parent) {
  LOG_INFO << "[startup] DiagnosticView: ctor begin";
  worker = new DiagnosticWorker(this);

  // Use the shared diagnostics DownloadApiClient owned by MenuManager so caching,
  // general prefetch, and response dumping are consistent across the app.
  downloadClient = MenuManager::getInstance().diagnosticApiClient();
  if (downloadClient) {
    LOG_WARN << "[startup] DiagnosticView: using shared DownloadApiClient from MenuManager";
  } else {
    LOG_WARN << "[startup] DiagnosticView: DownloadApiClient is null (comparison downloads disabled)";
  }

  // Connect to MenuManager for comparison data (centralized menu management)
  connect(&MenuManager::getInstance(), &MenuManager::diagnosticMenuUpdated,
          this, [this](const MenuData& menuData) {
    LOG_INFO << "DiagnosticView: Menu data updated via MenuManager - CPUs: " << menuData.availableCpus.size() 
             << ", GPUs: " << menuData.availableGpus.size()
             << ", Memory: " << menuData.availableMemory.size()
             << ", Drives: " << menuData.availableDrives.size();
    
    // Store the menu data for use by component renderers
    cachedMenuData = menuData;
    menuDataLoaded = true;
    //LOG_INFO << "DiagnosticView: Menu data cached successfully for component comparison dropdowns";
  });
  
  connect(&MenuManager::getInstance(), &MenuManager::menuRefreshError,
          this, [this](const QString& error) {
    LOG_WARN << "DiagnosticView: Menu refresh error: " << error.toStdString();
    // Continue without comparison data - UI will work without it
  });

  LOG_INFO << "[startup] DiagnosticView: setupLayout begin";
  setupLayout();
  LOG_INFO << "[startup] DiagnosticView: setupLayout end";

  // Connect run button
  connect(runButton, &QPushButton::clicked, this,
          &DiagnosticView::onRunDiagnostics);

  // Connect worker signals to update GUI slots - explicitly queued
  connect(worker, &DiagnosticWorker::cpuTestCompleted, this,
          &DiagnosticView::updateCPUResults, Qt::QueuedConnection);
  connect(worker, &DiagnosticWorker::cacheTestCompleted, this,
          &DiagnosticView::updateCacheResults, Qt::QueuedConnection);
  connect(worker, &DiagnosticWorker::memoryTestCompleted, this,
          &DiagnosticView::updateMemoryResults);
  connect(worker, &DiagnosticWorker::gpuTestCompleted, this,
          &DiagnosticView::updateGPUResults);
  connect(worker, &DiagnosticWorker::driveTestCompleted, this,
          &DiagnosticView::updateDriveResults);

  connect(worker, &DiagnosticWorker::diagnosticsFinished, this,
          &DiagnosticView::diagnosticsFinished, Qt::QueuedConnection);
  connect(worker, &DiagnosticWorker::devToolsResultsReady, this,
          &DiagnosticView::updateDevToolsResults);
  // New connection for additional tools results:
  connect(worker, &DiagnosticWorker::additionalToolsResultsReady, this,
          &DiagnosticView::updateAdditionalToolsResults);
  // Add to constructor
  connect(worker, &DiagnosticWorker::storageAnalysisReady, this,
          &DiagnosticView::updateStorageResults);
  // In constructor, add connection:
  connect(worker, &DiagnosticWorker::backgroundProcessTestCompleted, this,
          &DiagnosticView::updateBackgroundProcessResults);
  // In constructor or setup method:
  connect(worker, &DiagnosticWorker::requestAdminElevation, this,
          &DiagnosticView::handleAdminElevation);
  // In constructor:
  connect(worker, &DiagnosticWorker::networkTestCompleted, this,
          &DiagnosticView::updateNetworkResults);

  // Initialize experimental features visibility
  updateExperimentalFeaturesVisibility();

  // Set up timer to check for experimental features changes
  QTimer* experimentalFeaturesTimer = new QTimer(this);
  connect(experimentalFeaturesTimer, &QTimer::timeout, this,
          &DiagnosticView::updateExperimentalFeaturesVisibility);
  experimentalFeaturesTimer->start(1000);  // Check every second

  LOG_INFO << "[startup] DiagnosticView: ctor end";
}

DiagnosticView::~DiagnosticView() {
  try {

    // Cancel any active operations before disconnecting signals
    try {
      if (worker) {
        worker->cancelPendingOperations();
      }
    } catch (const std::exception& e) {
      // Silently handle cleanup exceptions in destructor
    } catch (...) {
      // Silently handle unknown exceptions in destructor
    }

    // First disconnect all connections to avoid any pending signals during
    // cleanup
    try {
      disconnectAllSignals();
    } catch (const std::exception& e) {
      // Silently handle signal disconnection exceptions in destructor
    } catch (...) {
      // Silently handle unknown signal disconnection exceptions in destructor
    }

    // Clean up the worker thread if it exists
    try {
      cleanUpWorkerAndThread();
    } catch (const std::exception& e) {
      // Silently handle cleanup exceptions in destructor
    } catch (...) {
      // Silently handle unknown cleanup exceptions in destructor
    }

    // Add small delay to ensure resources have been released
    QThread::msleep(100);
    QCoreApplication::processEvents();

    // Final safety check in case cleanUpWorkerAndThread didn't handle everything
    if (workerThread) {
      if (workerThread->isRunning()) {
        workerThread->terminate();
        workerThread->wait(1000);
      }
      try {
        delete workerThread;
        workerThread = nullptr;
      } catch (const std::exception& e) {
        // Silently handle worker thread deletion exceptions in destructor
      }
    }

    // Worker will be auto-deleted through thread cleanup
    // but delete it explicitly if it's not in a thread or if cleanup didn't
    // handle it
    if (worker) {
      if (!worker->thread() || worker->thread() == QThread::currentThread()) {
        try {
          delete worker;
          worker = nullptr;
        } catch (const std::exception& e) {
          // Silently handle worker deletion exceptions in destructor
        }
      }
    }

  } catch (const std::exception& e) {
    // Silently handle any exceptions in destructor - avoid logging in destructor
  } catch (...) {
    // Silently handle unknown exceptions in destructor
  }
}

void DiagnosticView::updateExperimentalFeaturesVisibility() {
  // Get experimental features status
  bool experimentalFeaturesEnabled =
    ApplicationSettings::getInstance().getEffectiveExperimentalFeaturesEnabled();

  // Update UI based on experimental features status (with null checks)
  if (developerToolsCheckbox) {
    developerToolsCheckbox->setVisible(experimentalFeaturesEnabled);
  }
  if (storageAnalysisCheckbox) {
    storageAnalysisCheckbox->setVisible(experimentalFeaturesEnabled);
  }
  if (cpuThrottlingTestModeCombo) {
    cpuThrottlingTestModeCombo->setVisible(
      experimentalFeaturesEnabled);  // Add this line to hide/show the
                                     // throttling combo
  }

  // Ensure they're unchecked when hidden (with null checks)
  if (!experimentalFeaturesEnabled) {
    if (developerToolsCheckbox) {
      developerToolsCheckbox->setChecked(false);
    }
    if (storageAnalysisCheckbox) {
      storageAnalysisCheckbox->setChecked(false);
    }

    // Reset CPU throttling test mode to None when experimental features are
    // disabled
    if (cpuThrottlingTestModeCombo) {
      cpuThrottlingTestModeCombo->setCurrentIndex(
        0);  // Set to "Skip CPU Throttling"
    }
    cpuThrottlingTestMode = CpuThrottle_None;  // Update the internal state

    // Also hide the associated UI elements (with null checks)
    if (devToolsGroup) {
      devToolsGroup->setVisible(false);
    }
    if (additionalToolsGroup) {
      additionalToolsGroup->setVisible(false);
    }
    if (storageAnalysisGroup) {
      storageAnalysisGroup->setVisible(false);
    }

    // Update worker settings (with null check)
    if (worker) {
      worker->setDeveloperMode(false);
      worker->setRunStorageAnalysis(false);
      worker->setSkipCpuThrottlingTests(
        true);  // Skip CPU throttling tests when experimental features are
                // disabled
      worker->setExtendedCpuThrottlingTests(false);
    }
  }

  // Reapply recommended settings if Use Recommended is checked
  if (useRecommendedCheckbox && useRecommendedCheckbox->isChecked()) {
    setUseRecommendedSettings(true);
  }
}

QString DiagnosticView::formatResultValue(const QString& label,
                                          const QString& value) {
  return QString("<b>%1:</b> %2<br>").arg(label).arg(value);
}

// Replace checkbox setup with combo box setup in setupLayout()
void DiagnosticView::setupLayout() {
  LOG_INFO << "[startup] DiagnosticView: setupLayout: creating base layouts";
  // Create main layout
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create header widget
  headerWidget = new QWidget(this);
  headerWidget->setObjectName("headerWidget");
  headerWidget->setStyleSheet(R"(
        #headerWidget {
            background-color: #1e1e1e;
            border-bottom: 1px solid #333333;
        }
    )");

  QVBoxLayout* headerLayout = new QVBoxLayout(headerWidget);
  headerLayout->setContentsMargins(10, 10, 10, 10);

  QLabel* descLabel =
    new QLabel("Run hardware diagnostics to analyze your system's performance "
               "and identify potential issues.",
               this);
  descLabel->setWordWrap(true);
  descLabel->setStyleSheet("color: #ffffff; font-size: 14px; background: "
                           "transparent;");  // Increased font size and made
                                             // background transparent
  headerLayout->addWidget(descLabel);

  mainLayout->addWidget(headerWidget);

  // Create single scrollable content area
  QScrollArea* scrollArea = new QScrollArea(this);
  QWidget* scrollContent = new QWidget(scrollArea);
  QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);
  scrollLayout->setSpacing(20);

  // Add these size policies to ensure proper horizontal behavior
  scrollContent->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  LOG_INFO << "[startup] DiagnosticView: setupLayout: initializing labels/widgets";
  // Initialize all labels and widgets
  cpuInfoLabel = new QLabel(this);
  cpuPerfLabel = new QLabel(this);
  cachePerfLabel = new QLabel(this);
  memoryInfoLabel = new QLabel(this);
  memoryPerfLabel = new QLabel(this);
  gpuInfoLabel = new QLabel(this);
  gpuPerfLabel = new QLabel(this);
  systemInfoLabel = new QLabel(this);

  LOG_INFO << "[startup] DiagnosticView: setupLayout: creating section widgets";
  // Replace QGroupBox widgets with CustomWidgetWithTitle
  cpuWidget = new CustomWidgetWithTitle("CPU", this);
  cpuWidget->getContentLayout()->addWidget(cpuInfoLabel);
  cpuWidget->getContentLayout()->addWidget(cpuPerfLabel);

  cacheWidget = new CustomWidgetWithTitle("Cache", this);
  cacheWidget->getContentLayout()->addWidget(cachePerfLabel);

  memoryWidget = new CustomWidgetWithTitle("Memory", this);
  memoryWidget->getContentLayout()->addWidget(memoryInfoLabel);
  memoryWidget->getContentLayout()->addWidget(memoryPerfLabel);

  gpuWidget = new CustomWidgetWithTitle("GPU", this);
  gpuWidget->getContentLayout()->addWidget(gpuInfoLabel);
  gpuWidget->getContentLayout()->addWidget(gpuPerfLabel);

  sysWidget = new CustomWidgetWithTitle("System", this);
  sysWidget->getContentLayout()->addWidget(systemInfoLabel);

  driveWidget = new CustomWidgetWithTitle("Storage", this);
  QVBoxLayout* driveContentLayout = driveWidget->getContentLayout();

  // Initialize drive labels vectors using ConstantSystemInfo
  try {
    LOG_INFO << "[startup] DiagnosticView: setupLayout: reading constant drive info";
    const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
    LOG_INFO << "[startup] DiagnosticView: setupLayout: drive count=" << constantInfo.drives.size();
    for (size_t i = 0; i < constantInfo.drives.size(); i++) {
      QLabel* infoLabel = new QLabel(this);
      QLabel* perfLabel = new QLabel(this);

      driveInfoLabels.append(infoLabel);
      drivePerfLabels.append(perfLabel);

      driveContentLayout->addWidget(infoLabel);
      driveContentLayout->addWidget(perfLabel);
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "[startup] DiagnosticView: setupLayout: exception while creating drive labels: " << e.what();
  } catch (...) {
    LOG_ERROR << "[startup] DiagnosticView: setupLayout: unknown exception while creating drive labels";
  }

  // Initialize dev tools group
  devToolsGroup = new CustomWidgetWithTitle("Developer Tools", this);
  devToolsLabel = new QLabel(this);
  devToolsLabel->setTextFormat(Qt::RichText);
  devToolsLabel->setWordWrap(true);
  devToolsLabel->setMinimumWidth(0);
  devToolsGroup->getContentLayout()->addWidget(devToolsLabel);

  // New: Initialize Additional Tools group
  additionalToolsGroup = new CustomWidgetWithTitle("Additional Tools", this);
  additionalToolsLabel = new QLabel(this);
  additionalToolsLabel->setTextFormat(Qt::RichText);
  additionalToolsLabel->setWordWrap(true);
  additionalToolsLabel->setMinimumWidth(0);
  additionalToolsGroup->getContentLayout()->addWidget(additionalToolsLabel);

  // Initialize storage analysis group
  storageAnalysisGroup =
    new CustomWidgetWithTitle("Storage Analysis Results", this);
  storageAnalysisLabel = new QLabel(this);
  storageAnalysisLabel->setTextFormat(Qt::RichText);
  storageAnalysisLabel->setWordWrap(true);
  storageAnalysisLabel->setMinimumWidth(0);
  storageAnalysisLabel->setOpenExternalLinks(true);
  storageAnalysisGroup->getContentLayout()->addWidget(storageAnalysisLabel);

  // Initialize background process widget
  backgroundProcessWidget =
    new CustomWidgetWithTitle("Background Processes", this);
  backgroundProcessLabel = new QLabel(this);
  backgroundProcessLabel->setTextFormat(Qt::RichText);
  backgroundProcessLabel->setWordWrap(true);
  backgroundProcessLabel->setMinimumWidth(0);
  backgroundProcessWidget->getContentLayout()->addWidget(
    backgroundProcessLabel);

  LOG_INFO << "[startup] DiagnosticView: setupLayout: creating summary and additional sections";
  // Add Analysis Summary widget at the top
  summaryWidget = new CustomWidgetWithTitle("Analysis Summary", this);
  QLabel* placeholderLabel =
    new QLabel("Run diagnostics to see system analysis results here.", this);
  placeholderLabel->setWordWrap(true);
  placeholderLabel->setStyleSheet("color: #888888; font-style: italic;");
  summaryWidget->getContentLayout()->addWidget(placeholderLabel);

  // First, add the summary widget to the scroll layout
  scrollLayout->addWidget(summaryWidget);

  // Then add other widgets in the desired order
  scrollLayout->addWidget(cpuWidget);
  scrollLayout->addWidget(cacheWidget);
  scrollLayout->addWidget(memoryWidget);
  scrollLayout->addWidget(gpuWidget);
  scrollLayout->addWidget(sysWidget);
  scrollLayout->addWidget(driveWidget);
  scrollLayout->addWidget(devToolsGroup);
  scrollLayout->addWidget(additionalToolsGroup);
  scrollLayout->addWidget(storageAnalysisGroup);
  scrollLayout->addWidget(backgroundProcessWidget);

  scrollLayout->setSpacing(20);
  scrollLayout->addStretch();

  LOG_INFO << "[startup] DiagnosticView: setupLayout: configuring scroll area";
  // Configure scroll area
  scrollContent->setLayout(scrollLayout);
  scrollArea->setWidget(scrollContent);
  scrollArea->setWidgetResizable(true);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  // Add scroll area to main layout
  mainLayout->addWidget(scrollArea);

  LOG_INFO << "[startup] DiagnosticView: setupLayout: building control panel";
  // Remove the status panel and integrate status label into control panel
  // Bottom controls with button and progress bar
  QWidget* controlPanel = new QWidget(this);
  controlPanel->setObjectName("controlPanel");
  controlPanel->setStyleSheet(R"(
        #controlPanel {
            background-color: #1e1e1e;
            border-top: 1px solid #333333;
        }
    )");

  QVBoxLayout* controlPanelLayout = new QVBoxLayout(controlPanel);
  controlPanelLayout->setContentsMargins(10, 10, 10, 10);
  controlPanelLayout->setSpacing(4);  // Reduced spacing

  LOG_INFO << "[startup] DiagnosticView: setupLayout: creating dropdowns";
  // Create combo boxes with full descriptive text options
  driveTestModeCombo = new SettingsDropdown(this);
  driveTestModeCombo->addItem("Skip Drive Tests", DriveTest_None);
  driveTestModeCombo->addItem("Quick Drive Test", DriveTest_SystemOnly);
  driveTestModeCombo->addItem("Detailed Drive Test", DriveTest_AllDrives);
  driveTestModeCombo->setDefaultIndex(1);  // Default to Quick

  networkTestModeCombo = new SettingsDropdown(this);
  networkTestModeCombo->addItem("Skip Network Tests", NetworkTest_None);
  networkTestModeCombo->addItem("Quick Network Test", NetworkTest_Basic);
  networkTestModeCombo->addItem("Detailed Network Test", NetworkTest_Extended);
  networkTestModeCombo->setDefaultIndex(1);  // Default to Quick

  cpuThrottlingTestModeCombo = new SettingsDropdown(this);
  cpuThrottlingTestModeCombo->addItem("Skip CPU Throttling", CpuThrottle_None);
  cpuThrottlingTestModeCombo->addItem("Quick CPU Throttling",
                                      CpuThrottle_Basic);
  cpuThrottlingTestModeCombo->addItem("Detailed CPU Throttling",
                                      CpuThrottle_Extended);
  cpuThrottlingTestModeCombo->setDefaultIndex(0);  // Default to Disabled

  LOG_INFO << "[startup] DiagnosticView: setupLayout: creating checkboxes";
  // Keep these as checkboxes
  runGpuTestsCheckbox = new QCheckBox("GPU Tests", this);
  runCpuBoostTestsCheckbox = new QCheckBox("CPU Boost Tests", this);
  developerToolsCheckbox = new QCheckBox("Developer Tools", this);
  storageAnalysisCheckbox = new QCheckBox("Storage Analysis", this);
  
  // New checkboxes for reorganized layout
  runCpuTestsCheckbox = new QCheckBox("CPU Tests", this);
  runMemoryTestsCheckbox = new QCheckBox("Memory Tests", this);
  runBackgroundTestsCheckbox = new QCheckBox("Background Usage", this);

  // Set default checked state for checkboxes
  runGpuTestsCheckbox->setChecked(true);
  runCpuBoostTestsCheckbox->setChecked(true);
  developerToolsCheckbox->setChecked(false);   // Off by default
  storageAnalysisCheckbox->setChecked(false);  // Off by default
  
  // Set default checked state for new checkboxes
  runCpuTestsCheckbox->setChecked(true);       // On by default
  runMemoryTestsCheckbox->setChecked(true);    // On by default
  runBackgroundTestsCheckbox->setChecked(true); // On by default

  LOG_INFO << "[startup] DiagnosticView: setupLayout: styling checkboxes";
  // Apply slightly more compact checkbox style
  QString checkboxStyle = R"(
        QCheckBox {
            color: #ffffff;
            spacing: 3px;
            padding: 2px 4px;
            background: transparent;
            margin-right: 3px;
            border-radius: 3px;
            font-size: 12px;
        }
        QCheckBox::indicator {
            width: 10px;
            height: 10px;
        }
        QCheckBox::indicator:unchecked {
            border: 1px solid #666666;
            background: #1e1e1e;
        }
        QCheckBox::indicator:checked {
            border: 1px solid #0078d4;
            background: #0078d4;
        }
    )";

  runGpuTestsCheckbox->setStyleSheet(checkboxStyle);
  runCpuBoostTestsCheckbox->setStyleSheet(checkboxStyle);
  developerToolsCheckbox->setStyleSheet(checkboxStyle);
  storageAnalysisCheckbox->setStyleSheet(checkboxStyle);
  
  // Apply style to new checkboxes
  runCpuTestsCheckbox->setStyleSheet(checkboxStyle);
  runMemoryTestsCheckbox->setStyleSheet(checkboxStyle);
  runBackgroundTestsCheckbox->setStyleSheet(checkboxStyle);

  // Create a grid layout for test controls
  QGridLayout* testControlsGrid = new QGridLayout();
  testControlsGrid->setSpacing(2);  // Reduced spacing

  // Add "Use Recommended" checkbox at the top spanning all columns
  useRecommendedCheckbox = new QCheckBox("Use Recommended", this);
  useRecommendedCheckbox->setStyleSheet(checkboxStyle);
  useRecommendedCheckbox->setChecked(true);  // Enabled by default
  testControlsGrid->addWidget(useRecommendedCheckbox, 0, 0, 1, 4);

  // Create consistent spacing between columns
  testControlsGrid->setColumnStretch(0, 2);  // Estimated time column
  testControlsGrid->setColumnStretch(1, 2);  // Dropdown column (center) - more stretch
  testControlsGrid->setColumnStretch(2, 1);  // General checkbox column - less stretch
  testControlsGrid->setColumnStretch(3, 1);  // CPU checkbox column - less stretch

  // Add estimated time label without a frame, sharing the first row with
  // controls
  estimatedTimeLabel = new QLabel(this);
  estimatedTimeLabel->setStyleSheet(
    "color: #bbbbbb; font-size: 11px; background: transparent; padding-left: "
    "4px;");
  // Position the time label directly in the first cell
  testControlsGrid->addWidget(estimatedTimeLabel, 1, 0, Qt::AlignLeft);

  // Initialize estimated time
  updateEstimatedTime();

  // COLUMN 1: Add dropdown menus right-aligned
  testControlsGrid->addWidget(driveTestModeCombo, 1, 1, Qt::AlignRight);  // Share row with time frame
  testControlsGrid->addWidget(networkTestModeCombo, 2, 1, Qt::AlignRight);

  // COLUMN 2: General test checkboxes left-aligned
  testControlsGrid->addWidget(runGpuTestsCheckbox, 1, 2, Qt::AlignLeft);  // Share row with time frame
  testControlsGrid->addWidget(runMemoryTestsCheckbox, 2, 2, Qt::AlignLeft);
  testControlsGrid->addWidget(runBackgroundTestsCheckbox, 3, 2, Qt::AlignLeft);
  
  // Experimental checkboxes (will be hidden when experimental features are disabled)
  testControlsGrid->addWidget(developerToolsCheckbox, 4, 2, Qt::AlignLeft);
  testControlsGrid->addWidget(storageAnalysisCheckbox, 5, 2, Qt::AlignLeft);

  // COLUMN 3: CPU-related test controls left-aligned
  testControlsGrid->addWidget(runCpuTestsCheckbox, 1, 3, Qt::AlignLeft);  // Master CPU tests checkbox
  testControlsGrid->addWidget(runCpuBoostTestsCheckbox, 2, 3, Qt::AlignLeft);  // CPU boost sub-option
  testControlsGrid->addWidget(cpuThrottlingTestModeCombo, 3, 3, Qt::AlignLeft);  // CPU throttling sub-option

  // Add the grid layout to the control panel
  controlPanelLayout->addLayout(testControlsGrid);

  LOG_INFO << "[startup] DiagnosticView: setupLayout: connecting dropdowns";
  // Connect signals from combo boxes
  connect(driveTestModeCombo, &SettingsDropdown::valueChanged,
          [this](const QVariant& value) { 
            setDriveTestMode(value.toInt()); 
            updateRunButtonState(); 
          });

  connect(networkTestModeCombo, &SettingsDropdown::valueChanged,
          [this](const QVariant& value) { 
            setNetworkTestMode(value.toInt()); 
            updateRunButtonState(); 
          });

  connect(cpuThrottlingTestModeCombo, &SettingsDropdown::valueChanged,
          [this](const QVariant& value) { 
            setCpuThrottlingTestMode(value.toInt()); 
            updateRunButtonState(); 
          });

  LOG_INFO << "[startup] DiagnosticView: setupLayout: connecting checkboxes";
  // Connect checkbox signals
  connect(runGpuTestsCheckbox, &QCheckBox::toggled, this,
          &DiagnosticView::setRunGpuTests);
  connect(runCpuBoostTestsCheckbox, &QCheckBox::toggled, this,
          &DiagnosticView::setRunCpuBoostTests);
  connect(developerToolsCheckbox, &QCheckBox::toggled, this,
          &DiagnosticView::setDeveloperMode);
  
  // Connect new checkbox signals
  connect(runCpuTestsCheckbox, &QCheckBox::toggled, this,
          &DiagnosticView::setRunCpuTests);
  connect(runMemoryTestsCheckbox, &QCheckBox::toggled, this,
          &DiagnosticView::setRunMemoryTests);
  connect(runBackgroundTestsCheckbox, &QCheckBox::toggled, this,
          &DiagnosticView::setRunBackgroundTests);
  
  // Connect storage analysis checkbox to update button state
  connect(storageAnalysisCheckbox, &QCheckBox::toggled, [this](bool checked) {
    if (worker) {
      worker->setRunStorageAnalysis(checked);
    }
    updateRunButtonState();
  });
  
  // Note: We'll reconnect this signal when worker is recreated in
  // connectWorkerSignals()

  // Always enable save results (with null check)
  if (worker) {
    worker->setSaveResults(true);
  }

  // Connect the new "Use Recommended" checkbox
  connect(useRecommendedCheckbox, &QCheckBox::toggled, this,
          &DiagnosticView::setUseRecommendedSettings);

  // Controls layout with run button and progress bar
  QHBoxLayout* controlsLayout = new QHBoxLayout();
  controlsLayout->setSpacing(6);  // Reduced spacing

  runButton = new QPushButton("Run Diagnostics", controlPanel);

  // Create progress bar aligned with button
  diagnosticProgress = new QProgressBar(controlPanel);
  diagnosticProgress->setMinimum(0);
  diagnosticProgress->setMaximum(100);
  diagnosticProgress->setValue(0);

  // Match the height of buttons to the progress bar
  int progressBarHeight = diagnosticProgress->sizeHint().height();
  runButton->setFixedHeight(progressBarHeight);

  // Add button and progress bar to same row
  controlsLayout->addWidget(runButton);
  controlsLayout->addWidget(diagnosticProgress, 1);

  LOG_INFO << "[startup] DiagnosticView: setupLayout: creating status label";
  // Add status label below in a separate row
  statusLabel = new QLabel("Ready to start diagnostics...", controlPanel);
  statusLabel->setStyleSheet(
    "color: #888888; font-size: 11px; background: transparent;");
  statusLabel->setMaximumHeight(15);  // Keep it thin
  statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  statusLabel->setAlignment(Qt::AlignLeft);

  // Add both controls and status to control panel
  controlPanelLayout->addLayout(controlsLayout);
  controlPanelLayout->addWidget(statusLabel);

  // Apply default/recommended settings after all controls exist to avoid
  // signal handlers touching null widgets during startup.
  {
    std::vector<QObject*> widgetsToBlock = {
      driveTestModeCombo, networkTestModeCombo, cpuThrottlingTestModeCombo,
      runGpuTestsCheckbox, runCpuBoostTestsCheckbox, developerToolsCheckbox,
      storageAnalysisCheckbox, runCpuTestsCheckbox, runMemoryTestsCheckbox,
      runBackgroundTestsCheckbox, useRecommendedCheckbox};
    std::vector<std::unique_ptr<QSignalBlocker>> blockers;
    blockers.reserve(widgetsToBlock.size());
    for (QObject* obj : widgetsToBlock) {
      if (obj) {
        blockers.emplace_back(std::make_unique<QSignalBlocker>(obj));
      }
    }
    setUseRecommendedSettings(true);
  }

  LOG_INFO << "[startup] DiagnosticView: setupLayout end (post-control panel)";
  mainLayout->addWidget(controlPanel);

  // Enable rich text for all info labels
  cpuInfoLabel->setTextFormat(Qt::RichText);
  memoryInfoLabel->setTextFormat(Qt::RichText);
  gpuInfoLabel->setTextFormat(Qt::RichText);
  systemInfoLabel->setTextFormat(Qt::RichText);
  for (auto label : driveInfoLabels) {
    label->setTextFormat(Qt::RichText);
  }

  // Enable rich text for all performance labels
  cpuPerfLabel->setTextFormat(Qt::RichText);
  cachePerfLabel->setTextFormat(Qt::RichText);
  memoryPerfLabel->setTextFormat(Qt::RichText);
  gpuPerfLabel->setTextFormat(Qt::RichText);
  for (auto label : drivePerfLabels) {
    label->setTextFormat(Qt::RichText);
  }

  // Update all content labels to handle text better
  cpuInfoLabel->setWordWrap(true);
  cpuPerfLabel->setWordWrap(true);
  cachePerfLabel->setWordWrap(true);
  memoryInfoLabel->setWordWrap(true);
  memoryPerfLabel->setWordWrap(true);
  gpuInfoLabel->setWordWrap(true);
  gpuPerfLabel->setWordWrap(true);
  systemInfoLabel->setWordWrap(true);

  // Add fixed width/minimum height to ensure proper wrapping
  int contentLabelWidth =
    0;  // Renamed from labelWidth (was causing redefinition error)
  cpuInfoLabel->setMinimumWidth(contentLabelWidth);
  cpuPerfLabel->setMinimumWidth(contentLabelWidth);
  cachePerfLabel->setMinimumWidth(contentLabelWidth);
  memoryInfoLabel->setMinimumWidth(contentLabelWidth);
  memoryPerfLabel->setMinimumWidth(contentLabelWidth);
  gpuInfoLabel->setMinimumWidth(contentLabelWidth);
  gpuPerfLabel->setMinimumWidth(contentLabelWidth);
  systemInfoLabel->setMinimumWidth(contentLabelWidth);

  // Add minimum height to ensure proper spacing
  setMinimumSize(0, 0);  // Set minimum size for widget

  // Hide all result widgets by default - only show systemInfoWidget
  cpuWidget->setVisible(false);
  cacheWidget->setVisible(false);
  memoryWidget->setVisible(false);
  gpuWidget->setVisible(false);
  sysWidget->setVisible(false);
  driveWidget->setVisible(false);
  devToolsGroup->setVisible(false);
  additionalToolsGroup->setVisible(false);
  storageAnalysisGroup->setVisible(false);
  backgroundProcessWidget->setVisible(false);

  // Initialize network widget
  networkWidget = new CustomWidgetWithTitle("Network", this);
  QVBoxLayout* networkLayout = new QVBoxLayout();
  networkLayout->setSpacing(0);
  networkWidget->getContentLayout()->addLayout(networkLayout);

  // Add the network widget to the scroll layout in the correct position (before
  // storage analysis) Assuming scrollLayout is your QVBoxLayout for the scroll
  // area's content
  scrollLayout->addWidget(networkWidget);
  networkWidget->setVisible(false);  // Hide initially
  
  // Initialize run button state based on initial checkbox/dropdown states
  updateRunButtonState();
}

void DiagnosticView::onRunDiagnostics() {
  try {

    // First, disable the run button to prevent multiple starts
    if (runButton) {
      runButton->setEnabled(false);
    }

    // Explicitly disconnect any existing connections to avoid signal loops or
    // conflicts
    disconnectAllSignals();

    // Reset progress bar and status display
    if (diagnosticProgress) {
      // Set to zero directly without signals
      diagnosticProgress->blockSignals(true);
      diagnosticProgress->setValue(0);
      diagnosticProgress->blockSignals(false);
    }

    lastProgressValue = 0;

    if (statusLabel) {
      statusLabel->setText("Initializing diagnostics...");
      statusLabel->setStyleSheet(
        "color: #44FF44; font-size: 11px; background: transparent;");
    }

    // Completely clean up previous worker and thread before creating new ones
    cleanUpWorkerAndThread();

    // Add a small delay after cleanup to ensure resources are fully released
    QThread::msleep(500);
    QCoreApplication::processEvents();

    // Mark DiagnosticDataStore for safe reset before clearing UI and starting
    // new run
    DiagnosticDataStore::getInstance().safelyResetAccess();
    QCoreApplication::processEvents();

    // Clear all previous results with robust error handling
    try {
      clearAllResults();
    } catch (const std::exception& e) {
      // Continue with diagnostics despite cleanup error
    } catch (...) {
      // Continue with diagnostics despite cleanup error
    }

    // Process events to ensure UI is updated
    QCoreApplication::processEvents();

    // Show the summary widget
    if (summaryWidget) {
      try {
        summaryWidget->setVisible(true);
        QCoreApplication::processEvents();
      } catch (const std::exception& e) {
        // Continue despite error
      } catch (...) {
        // Continue despite error
      }
    }

    try {
      // Create a fresh worker instance
      DiagnosticWorker* newWorker = nullptr;
      try {
        newWorker = new DiagnosticWorker(
          nullptr);  // No parent - will be moved to thread
      } catch (const std::exception& e) {
        LOG_ERROR << "CRITICAL ERROR: Exception during worker creation: "
                  << e.what();
        throw;
      } catch (...) {
        LOG_ERROR << "CRITICAL ERROR: Unknown exception during worker creation"
                 ;
        throw;
      }

      worker = newWorker;
      if (!worker) {
        throw std::runtime_error("Failed to create worker instance");
      }

      // Create a new thread for this worker
      QThread* newThread = nullptr;
      try {
        newThread = new QThread(this);

        if (!newThread) {
          throw std::runtime_error("Failed to create worker thread");
        }
        newThread->setObjectName("DiagnosticWorkerThread");
      } catch (const std::exception& e) {
        // Clean up worker if thread creation fails
        LOG_ERROR << "Exception during thread creation: " << e.what()
                 ;
        delete worker;
        worker = nullptr;
        throw;  // Rethrow to outer handler
      } catch (...) {
        LOG_INFO << "Unknown exception during thread creation";
        delete worker;
        worker = nullptr;
        throw;
      }

      workerThread = newThread;

      // Move worker to thread before connecting signals
      LOG_INFO << "Moving worker to thread (before)...";
      try {
        worker->moveToThread(workerThread);
        LOG_INFO << "Worker moved to thread successfully";
      } catch (const std::exception& e) {
        LOG_ERROR << "Exception during worker move to thread: " << e.what()
                 ;
        delete worker;
        worker = nullptr;
        delete workerThread;
        workerThread = nullptr;
        throw;  // Rethrow to outer handler
      } catch (...) {
        LOG_INFO << "Unknown exception during worker move to thread"
                 ;
        delete worker;
        worker = nullptr;
        delete workerThread;
        workerThread = nullptr;
        throw;
      }

      // Connect signals with explicit type safety and queued connections
      LOG_INFO << "Connecting worker signals (before)...";
      try {
        connectWorkerSignals();
        LOG_INFO << "Worker signals connected successfully";
      } catch (const std::exception& e) {
        LOG_ERROR << "Exception during signal connection: " << e.what()
                 ;
        delete worker;
        worker = nullptr;
        delete workerThread;
        workerThread = nullptr;
        throw;  // Rethrow to outer handler
      } catch (...) {
        LOG_INFO << "Unknown exception during signal connection";
        delete worker;
        worker = nullptr;
        delete workerThread;
        workerThread = nullptr;
        throw;
      }

      // Set diagnostic settings on new worker
      if (worker) {
        LOG_INFO << "Configuring worker settings (before)...";
        try {
          LOG_INFO << "Configuring diagnostic settings...";

          worker->setSkipDriveTests(driveTestMode == DriveTest_None);
          worker->setSystemDriveOnlyMode(driveTestMode == DriveTest_SystemOnly);
          worker->setSkipGpuTests(!runGpuTests);
          worker->setDeveloperMode(developerMode);
          worker->setSkipCpuThrottlingTests(cpuThrottlingTestMode ==
                                            CpuThrottle_None);
          worker->setExtendedCpuThrottlingTests(cpuThrottlingTestMode ==
                                                CpuThrottle_Extended);
          worker->setRunCpuBoostTests(runCpuBoostTests);
          worker->setRunStorageAnalysis(storageAnalysisCheckbox &&
                                        storageAnalysisCheckbox->isChecked());
          worker->setSaveResults(true);  // Always save results
          worker->setRunNetworkTests(networkTestMode != NetworkTest_None);
          worker->setExtendedNetworkTests(networkTestMode ==
                                          NetworkTest_Extended);
          
          // Configure new test settings
          worker->setDriveTestMode(static_cast<int>(driveTestMode));
          worker->setNetworkTestMode(static_cast<int>(networkTestMode));
          worker->setCpuThrottlingTestMode(static_cast<int>(cpuThrottlingTestMode));
          worker->setRunMemoryTests(runMemoryTestsCheckbox && runMemoryTestsCheckbox->isChecked());
          worker->setRunBackgroundTests(runBackgroundTestsCheckbox && runBackgroundTestsCheckbox->isChecked());
          worker->setUseRecommendedSettings(useRecommendedCheckbox && useRecommendedCheckbox->isChecked());

          LOG_INFO << "Worker settings configured successfully";
        } catch (const std::exception& e) {
          LOG_ERROR << "Exception during worker configuration: " << e.what()
                   ;
          // Continue anyway - these are just settings
        } catch (...) {
          LOG_INFO << "Unknown exception during worker configuration"
                   ;
          // Continue anyway - these are just settings
        }
      } else {
        LOG_WARN << "WARNING: worker is null when trying to configure settings"
                 ;
      }

      // Start the thread - this will trigger runDiagnosticsInternal via the
      // signal connection
      LOG_INFO << "Starting worker thread (before)...";
      try {
        // Verify the thread and worker are still valid
        if (!workerThread) {
          throw std::runtime_error("Worker thread is null before starting");
        }
        if (!worker) {
          throw std::runtime_error("Worker is null before starting thread");
        }

        LOG_INFO << "Worker pointer: " << worker
                  << ", Thread pointer: " << workerThread;

        // Add a small delay to ensure all connections are established
        QThread::msleep(100);
        QCoreApplication::processEvents();

        // Force thread to be in a clean initial state
        if (workerThread->isRunning()) {
          LOG_INFO << "Thread already running, stopping it first..."
                   ;
          workerThread->quit();
          if (!workerThread->wait(1000)) {
            LOG_WARN << "WARNING: Thread didn't stop cleanly, forcing termination";
            workerThread->terminate();
            workerThread->wait(500);
          }
        }

        // Start the thread
        workerThread->start();

        // Process events to ensure the started signal is delivered
        QCoreApplication::processEvents();

        LOG_INFO << "Worker thread started successfully";
        LOG_INFO << "Thread isRunning: "
                  << (workerThread->isRunning() ? "true" : "false")
                 ;
      } catch (const std::exception& e) {
        LOG_ERROR << "Exception during thread start: " << e.what();
        delete worker;
        worker = nullptr;
        delete workerThread;
        workerThread = nullptr;
        throw;  // Rethrow to outer handler
      } catch (...) {
        LOG_INFO << "Unknown exception during thread start";
        delete worker;
        worker = nullptr;
        delete workerThread;
        workerThread = nullptr;
        throw;
      }

      LOG_INFO << "onRunDiagnostics completed successfully";
    } catch (const std::exception& e) {
      // Handle any exceptions during resource creation
      LOG_ERROR << "Exception during worker/thread setup: " << e.what()
               ;

      // Re-enable run button
      if (runButton) {
        runButton->setEnabled(true);
      }

      // Update status
      if (statusLabel) {
        statusLabel->setText(
          QString("Error starting diagnostics: %1").arg(e.what()));
        statusLabel->setStyleSheet(
          "color: #FF4444; font-size: 11px; background: transparent;");
      }
    } catch (...) {
      LOG_INFO << "Unknown exception during worker/thread setup";

      // Re-enable run button and update status
      if (runButton) {
        runButton->setEnabled(true);
      }
      if (statusLabel) {
        statusLabel->setText("Error starting diagnostics: Unknown error");
        statusLabel->setStyleSheet(
          "color: #FF4444; font-size: 11px; background: transparent;");
      }
    }
  } catch (const std::exception& e) {
    // Handle any exceptions during diagnostics initialization
    LOG_ERROR << "Exception during diagnostics initialization: " << e.what()
             ;
    if (statusLabel) {
      statusLabel->setText(
        QString("Error starting diagnostics: %1").arg(e.what()));
      statusLabel->setStyleSheet(
        "color: #FF4444; font-size: 11px; background: transparent;");
    }
    if (runButton) {
      runButton->setEnabled(true);
    }
  } catch (...) {
    // Catch any other exceptions
    LOG_INFO << "Unknown exception during diagnostics initialization"
             ;
    if (statusLabel) {
      statusLabel->setText("Error starting diagnostics: Unknown error");
      statusLabel->setStyleSheet(
        "color: #FF4444; font-size: 11px; background: transparent;");
    }
    if (runButton) {
      runButton->setEnabled(true);
    }
  }
}

// New helper method to disconnect all signals
void DiagnosticView::disconnectAllSignals() {
  try {
    LOG_INFO << "Disconnecting all previous signals...";

    // Disconnect worker signals if it exists
    if (worker) {
      try {
        disconnect(worker, nullptr, this, nullptr);
        disconnect(this, nullptr, worker, nullptr);
      } catch (const std::exception& e) {
        LOG_ERROR << "Exception during worker signal disconnection: "
                  << e.what();
      }
    }

    // Disconnect thread signals if it exists
    if (workerThread) {
      try {
        disconnect(workerThread, nullptr, nullptr, nullptr);
      } catch (const std::exception& e) {
        LOG_ERROR << "Exception during thread signal disconnection: "
                  << e.what();
      }
    }

    // Disconnect checkbox signals to worker
    if (storageAnalysisCheckbox && worker) {
      try {
        disconnect(storageAnalysisCheckbox, nullptr, worker, nullptr);
      } catch (const std::exception& e) {
        LOG_ERROR << "Exception during checkbox signal disconnection: "
                  << e.what();
      }
    }

    LOG_INFO << "Signal disconnection complete";
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception during signal disconnection: " << e.what()
             ;
  } catch (...) {
    LOG_INFO << "Unknown exception during signal disconnection";
  }
}

// New helper method to thoroughly clean up worker and thread
void DiagnosticView::cleanUpWorkerAndThread() {
  try {
    LOG_INFO << "=== Starting worker and thread cleanup... ===";

    // If we have a worker thread from a previous run, make sure it's properly
    // stopped
    if (workerThread) {
      LOG_INFO << "Worker thread exists: " << workerThread;

      if (workerThread->isRunning()) {
        LOG_INFO << "Previous worker thread is still running, stopping it..."
                 ;

        // First ensure any pending operations in the worker are handled
        if (worker) {
          LOG_INFO << "Worker exists: " << worker
                    << ", canceling operations...";
          try {
            LOG_INFO << "Canceling pending worker operations...";
            worker->cancelPendingOperations();
            LOG_INFO << "Worker operations canceled successfully";
          } catch (const std::exception& e) {
            LOG_ERROR << "Exception during worker operations cancellation: "
                      << e.what();
          } catch (...) {
            LOG_ERROR << "Unknown exception during worker operations cancellation";
          }
        } else {
          LOG_INFO << "Worker is null during cleanup";
        }

        // Stop the thread and wait for it to finish
        LOG_INFO << "Quitting worker thread...";

        // Disconnect all signals from the thread first
        try {
          LOG_INFO << "Disconnecting thread signals...";
          workerThread->disconnect();
          LOG_INFO << "Thread signals disconnected";
        } catch (const std::exception& e) {
          LOG_ERROR << "Exception during thread signal disconnection: "
                    << e.what();
        } catch (...) {
          LOG_INFO << "Unknown exception during thread signal disconnection"
                   ;
        }

        // Now quit the thread
        LOG_INFO << "Calling quit() on worker thread...";
        workerThread->quit();
        LOG_INFO << "quit() called, waiting for thread to stop..."
                 ;

        // Wait with increasing timeout to ensure thread stops
        if (!workerThread->wait(1000)) {
          LOG_WARN << "WARNING: Thread didn't quit after 1 second, waiting 2 more seconds...";
          if (!workerThread->wait(2000)) {  // Wait up to 3 seconds total
            LOG_WARN << "WARNING: Worker thread did not terminate properly, "
                         "forcing termination"
                     ;
            workerThread->terminate();  // Force termination as a last resort
            LOG_INFO << "terminate() called, waiting again...";
            if (workerThread->wait(1000)) {
              LOG_INFO << "Thread terminated successfully after force"
                       ;
            } else {
              LOG_INFO << "CRITICAL: Thread still not terminated after force!"
                       ;
            }
          } else {
            LOG_INFO << "Thread stopped after extended wait";
          }
        } else {
          LOG_INFO << "Thread stopped normally";
        }
      } else {
        LOG_INFO << "Worker thread exists but is not running";
      }

      LOG_INFO << "About to delete worker thread...";

      // Make double sure the thread is not running before deleting
      if (workerThread->isRunning()) {
        LOG_WARN << "WARNING: Thread is still running during deletion!"
                 ;
        workerThread->terminate();
        LOG_INFO << "terminate() called during final check, waiting..."
                 ;
        if (workerThread->wait(500)) {
          LOG_INFO << "Thread finally terminated";
        } else {
          LOG_ERROR << "CRITICAL: Thread STILL running after all termination attempts!";
        }
      }

      // Keep a copy of the pointer for logging
      QThread* threadPtr = workerThread;
      delete workerThread;
      LOG_INFO << "Deleted worker thread: " << threadPtr;
      workerThread = nullptr;
    } else {
      LOG_INFO << "No worker thread to clean up";
    }

    // Clean up previous worker if it exists but is not owned by a thread
    if (worker) {
      LOG_INFO << "Worker exists: " << worker;

      try {
        LOG_INFO << "Canceling any remaining worker operations..."
                 ;
        worker->cancelPendingOperations();
        LOG_INFO << "Worker operations canceled successfully";
      } catch (const std::exception& e) {
        LOG_ERROR << "Exception during final worker cleanup: " << e.what()
                 ;
      } catch (...) {
        LOG_INFO << "Unknown exception during final worker cleanup"
                 ;
      }

      // Check if the worker has a thread and what thread it is
      QThread* workerCurrentThread = worker->thread();
      LOG_INFO << "Worker's current thread: " << workerCurrentThread
               ;
      LOG_INFO << "This thread: " << QThread::currentThread();

      // Only delete the worker explicitly if it's not managed by a thread
      // or if the thread is the current thread (which means the worker is in
      // our thread)
      if (!workerCurrentThread ||
          workerCurrentThread == QThread::currentThread()) {
        LOG_INFO << "About to delete worker object directly...";

        // Ensure all signals are disconnected
        try {
          LOG_INFO << "Disconnecting all worker signals...";
          worker->disconnect();
          LOG_INFO << "Worker signals disconnected";
        } catch (const std::exception& e) {
          LOG_ERROR << "Exception during worker signal disconnection: "
                    << e.what();
        } catch (...) {
          LOG_INFO << "Unknown exception during worker signal disconnection"
                   ;
        }

        // Keep a copy of the pointer for logging
        DiagnosticWorker* workerPtr = worker;
        delete worker;
        LOG_INFO << "Deleted worker object: " << workerPtr;
      } else {
        LOG_INFO << "Worker object will be deleted by its thread: "
                  << workerCurrentThread;
      }
      worker = nullptr;
    } else {
      LOG_INFO << "No worker to clean up";
    }

    // Add a small delay to ensure cleanup is complete
    LOG_INFO << "Adding short delay after cleanup...";
    QThread::msleep(200);
    QCoreApplication::processEvents();
    LOG_INFO << "Process events complete after cleanup";

    LOG_INFO << "=== Worker and thread cleanup complete ===";
  } catch (const std::exception& e) {
    LOG_ERROR << "CRITICAL ERROR: Exception during worker/thread cleanup: "
              << e.what();

    // In case of exception, make sure our pointers are nulled
    try {
      if (workerThread) {
        delete workerThread;
        workerThread = nullptr;
      }
    } catch (...) {
      LOG_INFO << "Error deleting worker thread during exception cleanup"
               ;
      workerThread = nullptr;
    }

    try {
      if (worker &&
          (!worker->thread() || worker->thread() == QThread::currentThread())) {
        delete worker;
        worker = nullptr;
      }
    } catch (...) {
      LOG_INFO << "Error deleting worker during exception cleanup"
               ;
      worker = nullptr;
    }
  } catch (...) {
    LOG_ERROR << "CRITICAL ERROR: Unknown exception during worker/thread cleanup";

    // In case of exception, make sure our pointers are nulled
    workerThread = nullptr;
    worker = nullptr;
  }
}

// New helper method to connect worker signals
void DiagnosticView::connectWorkerSignals() {
  if (!worker || !workerThread) {
    LOG_INFO << "Cannot connect signals - worker or thread is null"
             ;
    return;
  }

  try {
    LOG_INFO << "Connecting worker signals...";

    // Keep track of connection successes for debugging
    int successCount = 0;

    // Connect thread finished signal to cleanup worker - this must be first and
    // MUST be direct
    try {
      LOG_INFO << "Connecting thread finished signal (critical)..."
               ;
      bool success = connect(workerThread, &QThread::finished, worker,
                             &QObject::deleteLater, Qt::DirectConnection);

      if (success) {
        LOG_INFO << "Thread finished signal connected successfully"
                 ;
        successCount++;
      } else {
        LOG_ERROR << "CRITICAL ERROR: Failed to connect thread finished signal"
                 ;
        throw std::runtime_error("Failed to connect thread finished signal");
      }
    } catch (const std::exception& e) {
      LOG_ERROR << "Exception connecting thread finished signal: " << e.what()
               ;
      throw;  // Critical connection - rethrow
    }

    // Connect worker signals to update GUI slots - explicitly queued
    try {
      LOG_INFO << "Connecting worker update signals...";
      bool success = true;

      // Track individual connections
      success = success && connect(worker, &DiagnosticWorker::testStarted, this,
                                   &DiagnosticView::updateTestStatus,
                                   Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success = success &&
                connect(worker, &DiagnosticWorker::progressUpdated, this,
                        &DiagnosticView::updateProgress, Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success = success && connect(worker, &DiagnosticWorker::cpuTestCompleted,
                                   this, &DiagnosticView::updateCPUResults,
                                   Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success =
        success &&
        connect(worker, &DiagnosticWorker::cacheTestCompleted, this,
                &DiagnosticView::updateCacheResults, Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success =
        success &&
        connect(worker, &DiagnosticWorker::memoryTestCompleted, this,
                &DiagnosticView::updateMemoryResults, Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success = success && connect(worker, &DiagnosticWorker::gpuTestCompleted,
                                   this, &DiagnosticView::updateGPUResults,
                                   Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success =
        success &&
        connect(worker, &DiagnosticWorker::driveTestCompleted, this,
                &DiagnosticView::updateDriveResults, Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success =
        success &&
        connect(worker, &DiagnosticWorker::backgroundProcessTestCompleted, this,
                &DiagnosticView::updateBackgroundProcessResults,
                Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success =
        success &&
        connect(worker, &DiagnosticWorker::devToolsResultsReady, this,
                &DiagnosticView::updateDevToolsResults, Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success = success &&
                connect(worker, &DiagnosticWorker::additionalToolsResultsReady,
                        this, &DiagnosticView::updateAdditionalToolsResults,
                        Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success =
        success &&
        connect(worker, &DiagnosticWorker::storageAnalysisReady, this,
                &DiagnosticView::updateStorageResults, Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      // Use queued connection for diagnosticsFinished to avoid thread issues
      // with UI updates
      success =
        success &&
        connect(worker, &DiagnosticWorker::diagnosticsFinished, this,
                &DiagnosticView::diagnosticsFinished, Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      success =
        success &&
        connect(worker, &DiagnosticWorker::networkTestCompleted, this,
                &DiagnosticView::updateNetworkResults, Qt::QueuedConnection);
      successCount += success ? 1 : 0;

      LOG_INFO << successCount
                << " worker update signals connected successfully";

      if (!success) {
        LOG_WARN << "WARNING: Some worker update signals failed to connect"
                 ;
      }
    } catch (const std::exception& e) {
      LOG_ERROR << "Exception connecting worker update signals: " << e.what()
               ;
      throw;  // Critical connections - rethrow
    }

    // Connect request admin elevation signal
    try {
      LOG_INFO << "Connecting admin elevation signal...";
      bool success =
        connect(worker, &DiagnosticWorker::requestAdminElevation, this,
                &DiagnosticView::handleAdminElevation, Qt::QueuedConnection);

      if (success) {
        LOG_INFO << "Admin elevation signal connected successfully"
                 ;
        successCount++;
      } else {
        LOG_WARN << "Warning: Failed to connect admin elevation signal"
                 ;
        // Non-critical, continue
      }
    } catch (const std::exception& e) {
      LOG_ERROR << "Exception connecting admin elevation signal: " << e.what()
               ;
      // Non-critical, continue
    }

    // Use QueuedConnection for thread started signal to avoid deadlock
    try {
      LOG_INFO << "Connecting thread started signal...";
      bool success = connect(workerThread, &QThread::started, worker,
                             &DiagnosticWorker::runDiagnosticsInternal,
                             Qt::QueuedConnection);

      if (success) {
        LOG_INFO << "Thread started signal connected successfully"
                 ;
        successCount++;
      } else {
        LOG_ERROR << "CRITICAL ERROR: Failed to connect thread started signal"
                 ;
        throw std::runtime_error("Failed to connect thread started signal");
      }
    } catch (const std::exception& e) {
      LOG_ERROR << "Exception connecting thread started signal: " << e.what()
               ;
      throw;  // Critical connection - rethrow
    }

    // Connect checkbox signals to the new worker
    if (storageAnalysisCheckbox && worker) {
      try {
        LOG_INFO << "Connecting checkbox signals...";
        bool success = connect(storageAnalysisCheckbox, &QCheckBox::toggled,
                               worker, &DiagnosticWorker::setRunStorageAnalysis,
                               Qt::QueuedConnection);

        if (success) {
          LOG_INFO << "Checkbox signals connected successfully";
          successCount++;
        } else {
          LOG_WARN << "Warning: Failed to connect checkbox signals"
                   ;
          // Non-critical, continue
        }
      } catch (const std::exception& e) {
        LOG_ERROR << "Exception connecting checkbox signals: " << e.what()
                 ;
        // Non-critical, continue
      }
    } else {
      LOG_WARN << "Storage analysis checkbox or worker is null, skipping connection";
    }

    LOG_INFO << "Worker signals connected: " << successCount
              << " successful connections";
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception during signal connection: " << e.what()
             ;
    throw;  // Rethrow to caller
  } catch (...) {
    LOG_INFO << "Unknown exception during signal connection";
    throw;  // Rethrow to caller
  }
}

void DiagnosticView::updateCPUResults(const QString& result) {
  // Simple logging
  LOG_INFO << "CPU: Starting to update results";

  try {
    // Basic null check
    if (!cpuWidget) {
      LOG_ERROR << "CPU: ERROR - Widget pointer is null";
      return;
    }

    // Safely clear previous content
    try {
      if (auto layout = cpuWidget->getContentLayout()) {
        LOG_INFO << "CPU: Clearing previous content";

        // Take and delete items one by one
        while (layout->count() > 0) {
          QLayoutItem* item = layout->takeAt(0);
          if (item) {
            if (item->widget()) {
              item->widget()->deleteLater();
            }
            delete item;
          }
        }
      }
      LOG_INFO << "CPU: Previous content cleared";
    } catch (const std::exception& e) {
      LOG_INFO << "CPU: Error clearing previous content: " << e.what()
               ;
    }

    // Load CPU comparison data first
    try {
      LOG_INFO << "CPU: Loading CPU comparison data";
      cpuComparisonData =
        DiagnosticRenderers::CPUResultRenderer::loadCPUComparisonData();
      LOG_INFO << "CPU: Loaded " << cpuComparisonData.size()
                << " CPU comparison entries";
    } catch (const std::exception& e) {
      LOG_INFO << "CPU: Error loading comparison data: " << e.what()
               ;
      // Continue despite error, just with empty comparison data
      cpuComparisonData.clear();
    }

    // Create new widget content
    QWidget* cpuResultWidget = nullptr;
    try {
      // Check if the labels need to be recreated
      if (!cpuInfoLabel) {
        LOG_INFO << "CPU: Recreating info label that was deleted";
        cpuInfoLabel = new QLabel(this);
        cpuInfoLabel->setTextFormat(Qt::RichText);
        cpuInfoLabel->setWordWrap(true);
      }

      if (!cpuPerfLabel) {
        LOG_INFO << "CPU: Recreating perf label that was deleted";
        cpuPerfLabel = new QLabel(this);
        cpuPerfLabel->setTextFormat(Qt::RichText);
        cpuPerfLabel->setWordWrap(true);
      }

      // Get CPU boost metrics from worker if available
      std::vector<CoreBoostMetrics> boostMetrics;
      if (worker) {
        boostMetrics = worker->getCpuBoostMetrics();
      }

      const MenuData* menuData = menuDataLoaded ? &cachedMenuData : nullptr;
      cpuResultWidget =
        DiagnosticRenderers::CPUResultRenderer::createCPUResultWidget(
          result, boostMetrics, menuData, downloadClient);
      LOG_INFO << "CPU: New content widget created";
    } catch (const std::exception& e) {
      LOG_INFO << "CPU: Error creating new content: " << e.what();
      return;
    }

    // Add widget to layout
    if (cpuResultWidget && cpuWidget->getContentLayout()) {
      cpuWidget->getContentLayout()->addWidget(cpuResultWidget);
      cpuWidget->setVisible(true);
      LOG_INFO << "CPU: Results displayed successfully";
    }
  } catch (const std::exception& e) {
    LOG_INFO << "CPU: Exception in results update: " << e.what();
  } catch (...) {
    LOG_INFO << "CPU: Unknown exception in results update";
  }
}

void DiagnosticView::updateMemoryResults(const QString& result) {
  // Simple logging
  LOG_INFO << "Memory: Starting to update results";

  try {
    // Basic null check
    if (!memoryWidget) {
      LOG_ERROR << "Memory: ERROR - Widget pointer is null";
      return;
    }

    // Safely clear previous content
    try {
      if (auto layout = memoryWidget->getContentLayout()) {
        LOG_INFO << "Memory: Clearing previous content";

        // Take and delete items one by one
        while (layout->count() > 0) {
          QLayoutItem* item = layout->takeAt(0);
          if (item) {
            if (item->widget()) {
              item->widget()->deleteLater();
            }
            delete item;
          }
        }
      }
      LOG_INFO << "Memory: Previous content cleared";
    } catch (const std::exception& e) {
      LOG_INFO << "Memory: Error clearing previous content: " << e.what()
               ;
    }

    // Create new widget content
    QWidget* memoryResultWidget = nullptr;
    try {
      const MenuData* menuData = menuDataLoaded ? &cachedMenuData : nullptr;
      memoryResultWidget =
        DiagnosticRenderers::MemoryResultRenderer::createMemoryResultWidget(
          result, menuData, downloadClient);
      LOG_INFO << "Memory: New content widget created";
    } catch (const std::exception& e) {
      LOG_INFO << "Memory: Error creating new content: " << e.what()
               ;
      return;
    }

    // Add widget to layout
    if (memoryResultWidget && memoryWidget->getContentLayout()) {
      memoryWidget->getContentLayout()->addWidget(memoryResultWidget);
      memoryWidget->setVisible(true);
      LOG_INFO << "Memory: Results displayed successfully";
    }
  } catch (const std::exception& e) {
    LOG_INFO << "Memory: Exception in results update: " << e.what()
             ;
  } catch (...) {
    LOG_INFO << "Memory: Unknown exception in results update";
  }
}

void DiagnosticView::updateGPUResults(const QString& result) {
  try {
    LOG_INFO << "Updating GPU results...";

    // Check if the widget still exists
    if (!gpuWidget) {
      LOG_WARN << "Warning: GPU widget is null during result update"
               ;
      return;
    }

    // Clear previous content safely
    try {
      QLayout* layout = gpuWidget->getContentLayout();
      if (layout) {
        while (QLayoutItem* item = layout->takeAt(0)) {
          if (item->widget()) {
            item->widget()->deleteLater();
          }
          delete item;
        }
      }
    } catch (const std::exception& e) {
      LOG_ERROR << "Exception clearing GPU widget: " << e.what();
    }

    // Check if the labels need to be recreated
    if (!gpuInfoLabel) {
      LOG_INFO << "GPU: Recreating info label that was deleted";
      gpuInfoLabel = new QLabel(this);
      gpuInfoLabel->setTextFormat(Qt::RichText);
      gpuInfoLabel->setWordWrap(true);
    }

    if (!gpuPerfLabel) {
      LOG_INFO << "GPU: Recreating perf label that was deleted";
      gpuPerfLabel = new QLabel(this);
      gpuPerfLabel->setTextFormat(Qt::RichText);
      gpuPerfLabel->setWordWrap(true);
    }

    try {
      // Use the GPU result renderer to create the GPU widget content
      const MenuData* menuData = menuDataLoaded ? &cachedMenuData : nullptr;
      QWidget* gpuResultWidget =
        DiagnosticRenderers::GPUResultRenderer::createGPUResultWidget(result, menuData, downloadClient);
      if (gpuResultWidget && gpuWidget && gpuWidget->getContentLayout()) {
        gpuWidget->getContentLayout()->addWidget(gpuResultWidget);

        // Show the GPU widget
        gpuWidget->setVisible(true);
      } else {
        LOG_WARN << "Warning: Failed to create or add GPU result widget"
                 ;
      }
    } catch (const std::exception& e) {
      LOG_ERROR << "Exception creating GPU result widget: " << e.what()
               ;

      // Create a basic label as fallback
      if (gpuWidget && gpuWidget->getContentLayout()) {
        QLabel* errorLabel =
          new QLabel("Error rendering GPU results: " + QString(e.what()), this);
        errorLabel->setWordWrap(true);
        errorLabel->setStyleSheet("color: #FF4444;");
        gpuWidget->getContentLayout()->addWidget(errorLabel);
        gpuWidget->setVisible(true);
      }
    }

    LOG_INFO << "GPU results updated successfully";
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception in updateGPUResults: " << e.what();
  } catch (...) {
    LOG_INFO << "Unknown exception in updateGPUResults";
  }
}

void DiagnosticView::updateDriveResults(const QString& result) {
  try {
    LOG_INFO << "Updating drive results...";

    // Check if widgets exist before updating
    if (!driveWidget) {
      LOG_ERROR << "Error: Drive widget is null, cannot update results"
               ;
      return;
    }

    // Add extra safety through QMetaObject::invokeMethod to ensure UI updates
    // on main thread
    QMetaObject::invokeMethod(
      this,
      [this, result]() {
        try {
          // Clear previous content safely
          QLayout* layout = driveWidget->getContentLayout();
          if (!layout) {
            LOG_ERROR << "Error: Drive widget layout is null";
            return;
          }

          // Remove old widgets
          while (layout->count() > 0) {
            QLayoutItem* item = layout->takeAt(0);
            if (item->widget()) {
              // Use delayed deletion
              item->widget()->setParent(nullptr);
              item->widget()->deleteLater();
            }
            delete item;
            QCoreApplication::processEvents();
          }

          // Process events to ensure deleted widgets are properly cleaned up
          QCoreApplication::processEvents();

          // Make sure drive info/perf label vectors are initialized
          if (driveInfoLabels.isEmpty() || drivePerfLabels.isEmpty()) {
            LOG_INFO << "Drive: Recreating drive label vectors that were cleared";

            // Clear out any old data first
            driveInfoLabels.clear();
            drivePerfLabels.clear();

            // Initialize with empty labels - we'll let the renderer fill in the
            // details
            const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
            for (size_t i = 0; i < constantInfo.drives.size(); i++) {
              QLabel* infoLabel = new QLabel(this);
              infoLabel->setTextFormat(Qt::RichText);
              QLabel* perfLabel = new QLabel(this);
              perfLabel->setTextFormat(Qt::RichText);

              driveInfoLabels.append(infoLabel);
              drivePerfLabels.append(perfLabel);
            }
          }

          // Use the Drive result renderer to create the drive widget content
          const MenuData* menuData = menuDataLoaded ? &cachedMenuData : nullptr;
          QWidget* driveResultWidget =
            DiagnosticRenderers::DriveResultRenderer::createDriveResultWidget(
              result, menuData, downloadClient);

          if (driveResultWidget && driveWidget->getContentLayout()) {
            driveWidget->getContentLayout()->addWidget(driveResultWidget);
            driveWidget->setVisible(true);
            QCoreApplication::processEvents();
            LOG_INFO << "Drive results updated successfully";
          } else {
            LOG_INFO << "Error creating drive result widget";
          }
        } catch (const std::exception& e) {
          LOG_ERROR << "Exception in drive update: " << e.what();

          // Create a basic error label as fallback
          if (driveWidget && driveWidget->getContentLayout()) {
            QLabel* errorLabel = new QLabel(
              "Error rendering drive results: " + QString(e.what()), this);
            errorLabel->setWordWrap(true);
            errorLabel->setStyleSheet("color: #FF4444;");
            driveWidget->getContentLayout()->addWidget(errorLabel);
            driveWidget->setVisible(true);
          }
        } catch (...) {
          LOG_INFO << "Unknown exception in drive update";
        }
      },
      Qt::QueuedConnection);
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception in updateDriveResults: " << e.what();
  } catch (...) {
    LOG_INFO << "Unknown exception in updateDriveResults";
  }
}

void DiagnosticView::updateSystemInfo(const QString& result) {
  // Check if the label needs to be recreated
  if (!systemInfoLabel) {
    LOG_INFO << "System: Recreating label that was deleted";
    systemInfoLabel = new QLabel(this);
    systemInfoLabel->setTextFormat(Qt::RichText);
    systemInfoLabel->setWordWrap(true);

    // Add to layout if possible
    if (sysWidget && sysWidget->getContentLayout()) {
      sysWidget->getContentLayout()->addWidget(systemInfoLabel);
    }
  }

  QStringList lines = result.split('\n');
  QStringList formattedLines;
  for (const QString& line : lines) {
    QStringList parts = line.split(':');
    if (parts.size() == 2) {
      formattedLines.append(
        QString("%1:\t<span style='color: #0078d4;'>%2</span><br>")
          .arg(parts[0])
          .arg(parts[1].trimmed()));
    } else {
      formattedLines.append(line);
    }
  }
  systemInfoLabel->setText(formattedLines.join(""));
  if (sysWidget) {
    sysWidget->setVisible(true);
  }
}

void DiagnosticView::updateCacheResults(const QString& result) {
  // Clear previous content
  while (cacheWidget->getContentLayout()->count() > 0) {
    QLayoutItem* item = cacheWidget->getContentLayout()->takeAt(0);
    if (item->widget()) {
      item->widget()->deleteLater();
    }
    delete item;
  }

  // Check if the label needs to be recreated
  if (!cachePerfLabel) {
    LOG_INFO << "Cache: Recreating label that was deleted";
    cachePerfLabel = new QLabel(this);
    cachePerfLabel->setTextFormat(Qt::RichText);
    cachePerfLabel->setWordWrap(true);
  }

  // Use the CPU result renderer to create the cache widget content, passing the
  // comparison data and network menu data
  const MenuData* menuData = menuDataLoaded ? &cachedMenuData : nullptr;
  QWidget* cacheResultWidget =
    DiagnosticRenderers::CPUResultRenderer::createCacheResultWidget(
      result, cpuComparisonData, menuData, downloadClient);
  cacheWidget->getContentLayout()->addWidget(cacheResultWidget);

  // Show the cache widget
  if (cacheWidget) {
    cacheWidget->setVisible(true);
  }
}

void DiagnosticView::diagnosticsFinished() {
  try {
    // Guard against multiple simultaneous calls
    if (m_isCurrentlyExecuting) {
      LOG_WARN << "Ignoring duplicate diagnosticsFinished call (already processing)";
      return;
    }

    m_isCurrentlyExecuting = true;

    LOG_INFO << "DiagnosticView::diagnosticsFinished called";

    if (downloadClient) {
      downloadClient->prefetchGeneralDiagnostics([](bool success, const QString& error) {
        if (success) {
          LOG_INFO << "Prefetched general diagnostics averages for comparison slots";
        } else {
          LOG_WARN << "Failed to prefetch general diagnostics averages: " << error.toStdString();
        }
      });
    }

    // Force progress bar to 100%
    lastProgressValue = 100;
    if (diagnosticProgress) {
      diagnosticProgress->setValue(100);
    }

    if (runButton) {
      runButton->setEnabled(true);
    }

    // Reset status label color to gray when finished
    if (statusLabel) {
      statusLabel->setStyleSheet(
        "color: #888888; font-size: 11px; background: transparent;");
      statusLabel->setText("Diagnostics completed successfully.");
    }

    // Clear previous content from summary widget
    if (summaryWidget && summaryWidget->getContentLayout()) {
      try {
        while (QLayoutItem* item =
                 summaryWidget->getContentLayout()->takeAt(0)) {
          if (item->widget()) {
            item->widget()->deleteLater();
          }
          delete item;
        }

        // Use QMetaObject::invokeMethod to ensure UI updates happen in the main
        // thread This avoids the QObject::setParent threading issues
        QMetaObject::invokeMethod(
          this,
          [this]() {
            try {
              QWidget* analysisWidget = DiagnosticRenderers::
                AnalysisSummaryRenderer::createAnalysisSummaryWidget();
              if (analysisWidget && summaryWidget &&
                  summaryWidget->getContentLayout()) {
                summaryWidget->getContentLayout()->addWidget(analysisWidget);
              }
            } catch (const std::exception& e) {
              LOG_INFO << "Error creating analysis summary widget: "
                        << e.what();

              // Add fallback message if summary creation fails
              if (summaryWidget && summaryWidget->getContentLayout()) {
                QLabel* errorLabel = new QLabel(
                  "Error creating analysis summary. Check logs for details.",
                  this);
                errorLabel->setWordWrap(true);
                errorLabel->setStyleSheet(
                  "color: #FF4444; font-style: italic;");
                summaryWidget->getContentLayout()->addWidget(errorLabel);
              }
            }
          },
          Qt::QueuedConnection);
      } catch (const std::exception& e) {
        LOG_INFO << "Error clearing summary widget: " << e.what();
      }
    }

    // Process any pending events before continuing cleanup
    QCoreApplication::processEvents();

    // Add a delay before triggering a garbage collection cycle
    // This can help ensure resources from the test are fully released
    QTimer::singleShot(500, []() {
      // Suggest a garbage collection cycle to clean up resources
      QCoreApplication::processEvents();
      LOG_INFO << "Triggering cleanup after diagnostics completion"
               ;
    });

    // Reset signal guard
    m_isCurrentlyExecuting = false;

    LOG_INFO << "DiagnosticView::diagnosticsFinished completed";
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception in diagnosticsFinished(): " << e.what()
             ;

    // Try to show error in UI if possible
    if (statusLabel) {
      statusLabel->setText("Error finalizing diagnostics: " +
                           QString(e.what()));
      statusLabel->setStyleSheet(
        "color: #FF4444; font-size: 11px; background: transparent;");
    }

    // Make sure run button is enabled
    if (runButton) {
      runButton->setEnabled(true);
    }

    // Reset signal guard
    m_isCurrentlyExecuting = false;
  } catch (...) {
    LOG_INFO << "Unknown exception in diagnosticsFinished()";

    // Try to show error in UI if possible
    if (statusLabel) {
      statusLabel->setText("Error finalizing diagnostics: Unknown error");
      statusLabel->setStyleSheet(
        "color: #FF4444; font-size: 11px; background: transparent;");
    }

    // Make sure run button is enabled
    if (runButton) {
      runButton->setEnabled(true);
    }

    // Reset signal guard
    m_isCurrentlyExecuting = false;
  }
}

void DiagnosticView::setRunDriveTests(bool run) {
  if (worker) {
    worker->setSkipDriveTests(!run);  // Invert the value for the worker
  }
}

void DiagnosticView::setRunGpuTests(bool run) {
  runGpuTests = run;
  if (worker) {
    worker->setSkipGpuTests(!run);  // Invert the value for the worker
  }
  updateRunButtonState();
}

void DiagnosticView::setDeveloperMode(bool enabled) {
  developerMode = enabled;
  if (worker) {
    worker->setDeveloperMode(enabled);
  }
  if (devToolsGroup) {
    devToolsGroup->setVisible(enabled);
  }
  if (additionalToolsGroup) {
    additionalToolsGroup->setVisible(
      enabled);  // Also show/hide additional tools group
  }
  updateRunButtonState();
}

void DiagnosticView::updateDevToolsResults(const QString& result) {
  // Check if label exists, recreate if needed
  if (!devToolsLabel) {
    LOG_INFO << "DevTools: Recreating label that was deleted";
    devToolsLabel = new QLabel(this);
    devToolsLabel->setTextFormat(Qt::RichText);
    devToolsLabel->setWordWrap(true);
    devToolsLabel->setMinimumWidth(0);

    // Add to layout if possible
    if (devToolsGroup && devToolsGroup->getContentLayout()) {
      devToolsGroup->getContentLayout()->addWidget(devToolsLabel);
    }
  }

  devToolsLabel->setText(result);
  if (devToolsGroup) {
    devToolsGroup->setVisible(true);
  }
}

// Add new slot to update the Additional Tools label
void DiagnosticView::updateAdditionalToolsResults(const QString& result) {
  // Check if label exists, recreate if needed
  if (!additionalToolsLabel) {
    LOG_INFO << "AdditionalTools: Recreating label that was deleted"
             ;
    additionalToolsLabel = new QLabel(this);
    additionalToolsLabel->setTextFormat(Qt::RichText);
    additionalToolsLabel->setWordWrap(true);
    additionalToolsLabel->setMinimumWidth(0);

    // Add to layout if possible
    if (additionalToolsGroup && additionalToolsGroup->getContentLayout()) {
      additionalToolsGroup->getContentLayout()->addWidget(additionalToolsLabel);
    }
  }

  additionalToolsLabel->setText(result);
  if (additionalToolsGroup) {
    additionalToolsGroup->setVisible(true);
  }
}

// Add implementation of formatter and slot

void DiagnosticView::updateStorageResults(
  const StorageAnalysis::AnalysisResults& results) {
  // Check if label exists, recreate if needed
  if (!storageAnalysisLabel) {
    LOG_INFO << "StorageAnalysis: Recreating label that was deleted"
             ;
    storageAnalysisLabel = new QLabel(this);
    storageAnalysisLabel->setTextFormat(Qt::RichText);
    storageAnalysisLabel->setWordWrap(true);
    storageAnalysisLabel->setMinimumWidth(0);
    storageAnalysisLabel->setOpenExternalLinks(true);

    // Add to layout if possible
    if (storageAnalysisGroup && storageAnalysisGroup->getContentLayout()) {
      storageAnalysisGroup->getContentLayout()->addWidget(storageAnalysisLabel);
    }
  }

  QString html;

  // Add summary statistics at the top
  html += "<h3>Analysis Summary:</h3>";
  html += QString("<p><b>Scanned:</b> %1 files, %2 folders<br>")
            .arg(results.totalFilesScanned)
            .arg(results.totalFoldersScanned);

  // Convert duration to seconds for display
  double durationSeconds = results.actualDuration.count() / 1000.0;
  html +=
    QString("<b>Duration:</b> %1 seconds").arg(durationSeconds, 0, 'f', 1);

  if (results.timedOut) {
    html +=
      " <span style='color: #ffaa00;'>(Timed out - partial results)</span>";
  }
  html += "</p><br>";

  // Show folders
  html += "<h3>Largest Folders:</h3><br>";
  const auto numFoldersToShow =
    std::min<size_t>(30, results.largestFolders.size());
  for (size_t i = 0; i < numFoldersToShow; i++) {
    const auto& result = results.largestFolders[i];
    QString path = QString::fromStdWString(result.first);
    QString size = DiagnosticViewComponents::formatStorageSize(result.second);
    html += QString("%1. <a href=\"file:///%2\">%2</a> - %3<br>")
              .arg(i + 1)
              .arg(path)
              .arg(size);
  }

  // Show files (modified to link to containing directory)
  html += "<br><h3>Largest Files:</h3><br>";
  const auto numFilesToShow = std::min<size_t>(30, results.largestFiles.size());
  for (size_t i = 0; i < numFilesToShow; i++) {
    const auto& result = results.largestFiles[i];
    QString filePath = QString::fromStdWString(result.first);
    QString dirPath = QFileInfo(filePath).absolutePath();
    QString size = DiagnosticViewComponents::formatStorageSize(result.second);

    // Show file name but link to directory
    html +=
      QString("%1. %2 <a href=\"file:///%3\">(Open Location)</a> - %4<br>")
        .arg(i + 1)
        .arg(filePath)
        .arg(dirPath)
        .arg(size);
  }

  storageAnalysisLabel->setText(html);
  if (storageAnalysisGroup) {
    storageAnalysisGroup->setVisible(true);
  }
}

void DiagnosticView::setRunCpuThrottlingTests(bool run) {
  if (worker) {
    worker->setSkipCpuThrottlingTests(!run);  // Invert the value for the worker
  }
}

// Simple, direct and safe implementation of background process results update
void DiagnosticView::updateBackgroundProcessResults(const QString& result) {
  // Simple logging
  LOG_INFO << "BackgroundProcess: Starting to update results";

  try {
    // Basic null check
    if (!backgroundProcessWidget) {
      LOG_ERROR << "BackgroundProcess: ERROR - Widget pointer is null"
               ;
      return;
    }

    // Generate the HTML content directly
    QString html;
    try {
      html = DiagnosticRenderers::BackgroundProcessRenderer::
        renderBackgroundProcessResults(result);
      LOG_INFO << "BackgroundProcess: Successfully rendered HTML content"
               ;
    } catch (const std::exception& e) {
      LOG_INFO << "BackgroundProcess: Error in renderer: " << e.what()
               ;
      html = "<p style='color:#FF6666'>Error rendering results: " +
             QString(e.what()) + "</p>";
    }

    // Check if backgroundProcessLabel exists, recreate if needed
    if (!backgroundProcessLabel) {
      LOG_INFO << "BackgroundProcess: Recreating label that was deleted"
               ;
      backgroundProcessLabel = new QLabel(this);
      backgroundProcessLabel->setTextFormat(Qt::RichText);
      backgroundProcessLabel->setWordWrap(true);
      backgroundProcessLabel->setMinimumWidth(0);

      // Add to layout if possible
      if (backgroundProcessWidget->getContentLayout()) {
        backgroundProcessWidget->getContentLayout()->addWidget(
          backgroundProcessLabel);
      }
    }

    // Direct update approach - set content and make visible
    backgroundProcessLabel->setText(html);
    if (backgroundProcessWidget) {
      backgroundProcessWidget->setVisible(true);
    }

    LOG_INFO << "BackgroundProcess: Results displayed successfully"
             ;
  } catch (const std::exception& e) {
    LOG_INFO << "BackgroundProcess: Exception in results update: " << e.what()
             ;
  } catch (...) {
    LOG_INFO << "BackgroundProcess: Unknown exception in results update"
             ;
  }
}

void DiagnosticView::updateTestStatus(const QString& testName) {
  // Update the status label with current test information (with null check)
  if (statusLabel) {
    statusLabel->setText(testName);
    // Change color to green when tests are running
    statusLabel->setStyleSheet(
      "color: #44FF44; font-size: 11px; background: transparent;");
  }
}

void DiagnosticView::updateProgress(int progress) {
  try {
    // Block signals during update to prevent unexpected events
    if (diagnosticProgress) {
      diagnosticProgress->blockSignals(true);

      // Ensure progress never decreases and stays within valid range
      lastProgressValue = std::max(lastProgressValue, std::min(progress, 100));

      // Update the progress bar with our controlled value
      diagnosticProgress->setValue(lastProgressValue);

      // Process events to update UI immediately
      QApplication::processEvents();

      // Restore signal handling
      diagnosticProgress->blockSignals(false);
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception in updateProgress(): " << e.what();
  } catch (...) {
    LOG_INFO << "Unknown exception in updateProgress()";
  }
}

QWidget* DiagnosticView::createSystemMetricBox(const QString& title,
                                               QLabel* contentLabel) {
  QWidget* box = new QWidget(this);
  box->setStyleSheet(R"(
        QWidget {
            background-color: #252525;
            border: 1px solid #383838;
            border-radius: 4px;
        }
        QLabel {
            border: none;
            background: transparent;
        }
    )");

  // Set a flexible size policy that allows the box to shrink when needed
  box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

  QVBoxLayout* layout = new QVBoxLayout(box);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(4);

  QLabel* titleLabel = new QLabel(title, box);
  titleLabel->setStyleSheet("color: #0078d4; font-size: 12px; font-weight: "
                            "bold; background: transparent; border: none;");
  titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);  // Align to top-left
  layout->addWidget(titleLabel);

  if (contentLabel) {
    contentLabel->setTextFormat(Qt::RichText);
    contentLabel->setWordWrap(true);
    contentLabel->setStyleSheet("border: none; background: transparent;");
    layout->addWidget(contentLabel);
  }

  return box;
}

QWidget* DiagnosticView::createPerformanceBox(const QString& title,
                                              double value,
                                              const QString& unit) {
  return DiagnosticViewComponents::createMetricBox(
    title, QString("%1 %2").arg(value, 0, 'f', 1).arg(unit),
    getColorForPerformance(value, unit));
}

// Add this helper function for determining colors
QString DiagnosticView::getColorForPerformance(double value,
                                               const QString& unit) {
  if (unit == "ms") {
    if (value < 50)
      return "#44FF44";  // Green (excellent)
    else if (value < 100)
      return "#88FF88";  // Light green (good)
    else if (value < 200)
      return "#FFAA00";  // Orange (average)
    else
      return "#FF6666";  // Red (poor)
  }
  return "#0078d4";  // Default blue
}

void DiagnosticView::setRunCpuBoostTests(bool run) {
  runCpuBoostTests = run;
  if (worker) {
    worker->setRunCpuBoostTests(run);
  }
  updateRunButtonState();
}

// Fix the handleAdminElevation method implementation

void DiagnosticView::handleAdminElevation() {
  QDialog dialog;
  dialog.setWindowTitle("Limited Diagnostics Mode");
  dialog.setFixedWidth(400);

  QVBoxLayout* layout = new QVBoxLayout(&dialog);

  QLabel* iconLabel = new QLabel;
  iconLabel->setPixmap(QApplication::style()
                         ->standardIcon(QStyle::SP_MessageBoxWarning)
                         .pixmap(32, 32));

  QLabel* msgLabel = new QLabel(
    "Some tests require administrator privileges for accurate results.");
  msgLabel->setWordWrap(true);

  QLabel* infoLabel =
    new QLabel("Running without administrator privileges may result in limited "
               "or inaccurate diagnostics for system components, drives, and "
               "hardware access.");
  infoLabel->setWordWrap(true);

  QDialogButtonBox* buttonBox =
    new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No);
  buttonBox->button(QDialogButtonBox::Yes)->setText("Restart as Administrator");
  buttonBox->button(QDialogButtonBox::No)->setText("Continue Limited");
  buttonBox->button(QDialogButtonBox::No)->setDefault(true);

  QHBoxLayout* topLayout = new QHBoxLayout;
  topLayout->addWidget(iconLabel);
  topLayout->addWidget(msgLabel, 1);

  layout->addLayout(topLayout);
  layout->addWidget(infoLabel);
  layout->addWidget(buttonBox);

  QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);

  if (dialog.exec() == QDialog::Accepted) {
    if (worker && worker->restartAsAdmin()) {
      QCoreApplication::quit();
      return;
    }
  }

  // Replace the direct log call with since log() is private
  LOG_INFO << "Running with limited diagnostics (no administrator privileges)"
           ;

  // Instead of direct invocation which can cause threading issues,
  // use a signal to trigger the internal run method
  if (worker && worker->thread() && worker->thread()->isRunning()) {
    // Continue with normal run without admin privileges
    worker->runDiagnostics();
  }
}

void DiagnosticView::setRunNetworkTests(bool run) {
  if (worker) {
    worker->setRunNetworkTests(run);  // Directly pass to worker
  }
}

void DiagnosticView::updateNetworkResults(const QString& result) {
  try {
    LOG_INFO << "Updating network results...";

    // Check if network widget exists
    if (!networkWidget) {
      LOG_ERROR << "Network: ERROR - Widget pointer is null";
      return;
    }

    // Clear previous content safely
    if (auto layout = networkWidget->getContentLayout()) {
      // Take and delete items one by one
      while (layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (item) {
          if (item->widget()) {
            item->widget()->deleteLater();
          }
          delete item;
        }
      }
    }

    // Use the Network result renderer to create the network widget content
    QWidget* networkResultWidget =
      DiagnosticRenderers::NetworkResultRenderer::createNetworkResultWidget(
        result);

    // Make sure we have a valid widget and layout before continuing
    if (networkResultWidget && networkWidget &&
        networkWidget->getContentLayout()) {
      networkWidget->getContentLayout()->addWidget(networkResultWidget);
      networkWidget->setVisible(true);
      LOG_INFO << "Network results displayed successfully";
    } else {
      LOG_ERROR << "Network: Error - Unable to create or add network results widget";
    }
  } catch (const std::exception& e) {
    LOG_INFO << "Network: Exception in results update: " << e.what();
  } catch (...) {
    LOG_INFO << "Network: Unknown exception in results update";
  }
}

void DiagnosticView::setDriveTestMode(int index) {
  driveTestMode = static_cast<DriveTestMode>(index);

  // Update worker settings
  if (worker) {
    switch (driveTestMode) {
      case DriveTest_None:
        worker->setSkipDriveTests(true);
        worker->setSystemDriveOnlyMode(false);  // Not relevant when skipping
        break;
      case DriveTest_SystemOnly:
        worker->setSkipDriveTests(false);
        worker->setSystemDriveOnlyMode(true);
        break;
      case DriveTest_AllDrives:
        worker->setSkipDriveTests(false);
        worker->setSystemDriveOnlyMode(false);
        break;
    }
  }
}

void DiagnosticView::setNetworkTestMode(int index) {
  networkTestMode = static_cast<NetworkTestMode>(index);

  // Update worker settings
  if (worker) {
    switch (networkTestMode) {
      case NetworkTest_None:
        worker->setRunNetworkTests(false);
        worker->setExtendedNetworkTests(false);  // Not relevant when skipping
        break;
      case NetworkTest_Basic:
        worker->setRunNetworkTests(true);
        worker->setExtendedNetworkTests(false);
        break;
      case NetworkTest_Extended:
        worker->setRunNetworkTests(true);
        worker->setExtendedNetworkTests(true);
        break;
    }
  }
}

void DiagnosticView::setCpuThrottlingTestMode(int index) {
  cpuThrottlingTestMode = static_cast<CpuThrottlingTestMode>(index);

  // Update worker settings
  if (worker) {
    switch (cpuThrottlingTestMode) {
      case CpuThrottle_None:
        worker->setSkipCpuThrottlingTests(true);
        worker->setExtendedCpuThrottlingTests(
          false);  // Not relevant when skipping
        break;
      case CpuThrottle_Basic:
        worker->setSkipCpuThrottlingTests(false);
        worker->setExtendedCpuThrottlingTests(false);
        break;
      case CpuThrottle_Extended:
        worker->setSkipCpuThrottlingTests(false);
        worker->setExtendedCpuThrottlingTests(true);
        break;
    }
  }
}

void DiagnosticView::setUseRecommendedSettings(bool useRecommended) {
  // Some builds may strip optional controls (like manual upload buttons); guard against missing widgets
  if (!driveTestModeCombo || !networkTestModeCombo || !cpuThrottlingTestModeCombo ||
      !runGpuTestsCheckbox || !runCpuBoostTestsCheckbox || !developerToolsCheckbox ||
      !storageAnalysisCheckbox) {
    LOG_WARN << "DiagnosticView: skipping recommended settings update because one or more controls are missing";
    return;
  }

  // Apply disabled visual style to dropdowns when in "recommended" mode
  QString enabledDropdownStyle = "";  // Default style from SettingsDropdown
  QString disabledDropdownStyle = R"(
        QComboBox {
            color: #777777;
            background-color: #1e1e1e;
            border: none;
            padding: 5px 10px;
            max-width: 180px;
            width: 180px;
            font-size: 12px;
        }
        QComboBox::drop-down {
            width: 20px;
            border-left: none;
            subcontrol-origin: padding;
            subcontrol-position: right center;
        }
    )";

  // Apply disabled visual style to checkboxes when in "recommended" mode
  QString enabledCheckboxStyle = R"(
        QCheckBox {
            color: #ffffff;
            spacing: 3px;
            padding: 2px 4px;
            background: transparent;
            margin-right: 3px;
            border-radius: 3px;
            font-size: 12px;
        }
        QCheckBox::indicator {
            width: 10px;
            height: 10px;
        }
        QCheckBox::indicator:unchecked {
            border: 1px solid #666666;
            background: #1e1e1e;
        }
        QCheckBox::indicator:checked {
            border: 1px solid #0078d4;
            background: #0078d4;
        }
    )";

  QString disabledCheckboxStyle = R"(
        QCheckBox {
            color: #777777;
            spacing: 3px;
            padding: 2px 4px;
            background: transparent;
            margin-right: 3px;
            border-radius: 3px;
            font-size: 12px;
        }
        QCheckBox::indicator {
            width: 10px;
            height: 10px;
        }
        QCheckBox::indicator:unchecked {
            border: 1px solid #444444;
            background: #1e1e1e;
        }
        QCheckBox::indicator:checked {
            border: 1px solid #444444;
            background: #555555;
        }
    )";

  // Enable/disable all control functionality
  driveTestModeCombo->setEnabled(!useRecommended);
  networkTestModeCombo->setEnabled(!useRecommended);
  cpuThrottlingTestModeCombo->setEnabled(!useRecommended);
  runGpuTestsCheckbox->setEnabled(!useRecommended);
  runCpuBoostTestsCheckbox->setEnabled(!useRecommended);
  developerToolsCheckbox->setEnabled(!useRecommended);
  storageAnalysisCheckbox->setEnabled(!useRecommended);

  // Apply visual styling based on enabled state
  if (useRecommended) {
    // Apply disabled visual style
    driveTestModeCombo->setStyleSheet(disabledDropdownStyle);
    networkTestModeCombo->setStyleSheet(disabledDropdownStyle);
    cpuThrottlingTestModeCombo->setStyleSheet(disabledDropdownStyle);

    runGpuTestsCheckbox->setStyleSheet(disabledCheckboxStyle);
    runCpuBoostTestsCheckbox->setStyleSheet(disabledCheckboxStyle);
    developerToolsCheckbox->setStyleSheet(disabledCheckboxStyle);
    storageAnalysisCheckbox->setStyleSheet(disabledCheckboxStyle);
  } else {
    // Reset to normal style
    driveTestModeCombo->applyStyle();
    networkTestModeCombo->applyStyle();
    cpuThrottlingTestModeCombo->applyStyle();

    runGpuTestsCheckbox->setStyleSheet(enabledCheckboxStyle);
    runCpuBoostTestsCheckbox->setStyleSheet(enabledCheckboxStyle);
    developerToolsCheckbox->setStyleSheet(enabledCheckboxStyle);
    storageAnalysisCheckbox->setStyleSheet(enabledCheckboxStyle);
  }

  if (useRecommended) {
    // Set recommended settings
    // Detailed drive test (index 2)
    driveTestModeCombo->setCurrentIndex(2);
    driveTestMode = DriveTest_AllDrives;

    // Quick network test (index 1)
    networkTestModeCombo->setCurrentIndex(1);
    networkTestMode = NetworkTest_Basic;

    // Skip CPU throttling (index 0)
    cpuThrottlingTestModeCombo->setCurrentIndex(0);
    cpuThrottlingTestMode = CpuThrottle_None;

    // Enable GPU and CPU boost tests
    runGpuTestsCheckbox->setChecked(true);
    runGpuTests = true;

    runCpuBoostTestsCheckbox->setChecked(true);
    runCpuBoostTests = true;

    // Disable experimental features
    developerToolsCheckbox->setChecked(false);
    developerMode = false;

    storageAnalysisCheckbox->setChecked(false);

    // Always save results (with null check)
    if (worker) {
      worker->setSaveResults(true);

      // Update worker settings
      worker->setSkipDriveTests(false);
      worker->setSystemDriveOnlyMode(false);
      worker->setRunNetworkTests(true);
      worker->setExtendedNetworkTests(false);
      worker->setSkipGpuTests(false);
      worker->setSkipCpuThrottlingTests(true);
      worker->setExtendedCpuThrottlingTests(false);
      worker->setRunCpuBoostTests(true);
      worker->setDeveloperMode(false);
      worker->setRunStorageAnalysis(false);
    }

    // Update estimated time based on the new settings
    updateEstimatedTime();
  }
}

// Add this method after setupLayout() to create the estimated time label
void DiagnosticView::updateEstimatedTime() {
  // Get drive count from ConstantSystemInfo
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
  int driveCount = constantInfo.drives.size();

  // Calculate estimated time: base 3 minutes + 1 minute per drive
  int estimatedMinutes = 3 + driveCount;

  // Update the label (with null check)
  if (estimatedTimeLabel) {
    QString timeText = QString("Estimated time: %1 min").arg(estimatedMinutes);
    estimatedTimeLabel->setText(timeText);
  }
}

void DiagnosticView::cancelOperations() {
  // Stop background process worker if running
  if (backgroundProcessWorker) {
    backgroundProcessWorker->cancelOperation();
  }

  // Stop any background processes or threads
  for (QProcess* process : activeProcesses) {
    if (process && process->state() == QProcess::Running) {
      // Try graceful termination first
      process->terminate();
      if (!process->waitForFinished(500)) {
        // Force kill if needed
        process->kill();
      }
    }
  }

  // If we have a worker thread running, stop it
  if (workerThread && workerThread->isRunning()) {
    if (worker) {
      worker->cancelPendingOperations();
      disconnect(worker, nullptr, this, nullptr);
    }

    disconnect(workerThread, nullptr, nullptr, nullptr);
    workerThread->quit();
    if (!workerThread->wait(3000)) {
      workerThread->terminate();
      workerThread->wait();
    }
  }

  // Set flag to false
  isRunning = false;

  // Reset UI state - since updateUIState doesn't exist, do direct updates (with
  // null checks)
  if (runButton) {
    runButton->setEnabled(true);
  }
  if (diagnosticProgress) {
    diagnosticProgress->setValue(0);
  }
  if (statusLabel) {
    statusLabel->setText("Diagnostics cancelled.");
  }

  LOG_INFO << "DiagnosticView operations cancelled";
}

// Add this new helper method to clear all previous results
void DiagnosticView::clearAllResults() {
  try {
    LOG_INFO << "Clearing all diagnostic results...";

    // Clear widgets with custom layouts
    auto clearWidgetLayout = [this](CustomWidgetWithTitle* widget) {
      if (!widget) return;

      try {
        QLayout* layout = widget->getContentLayout();
        if (layout) {
          // Process events to allow for proper widget deletion
          QCoreApplication::processEvents();

          // Take items one by one and delete them safely
          QLayoutItem* item;
          while ((item = layout->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget()) {
              // Use delayed deletion to prevent immediate crashes
              w->setParent(nullptr);
              w->deleteLater();
            }
            delete item;

            // Process events periodically during cleanup
            QCoreApplication::processEvents();
          }
        }
        widget->setVisible(false);
      } catch (const std::exception& e) {
        LOG_INFO << "Error clearing widget layout: " << e.what();
      }
    };

    // Process events before starting cleanup
    QCoreApplication::processEvents();

    // Clear each result widget individually with proper error handling
    clearWidgetLayout(cpuWidget);
    cpuInfoLabel = nullptr;
    cpuPerfLabel = nullptr;

    clearWidgetLayout(cacheWidget);
    cachePerfLabel = nullptr;

    clearWidgetLayout(memoryWidget);
    memoryInfoLabel = nullptr;
    memoryPerfLabel = nullptr;

    clearWidgetLayout(gpuWidget);
    gpuInfoLabel = nullptr;
    gpuPerfLabel = nullptr;

    clearWidgetLayout(sysWidget);
    systemInfoLabel = nullptr;

    clearWidgetLayout(driveWidget);
    // Clear drive info and perf labels
    driveInfoLabels.clear();
    drivePerfLabels.clear();

    clearWidgetLayout(devToolsGroup);
    devToolsLabel = nullptr;

    clearWidgetLayout(additionalToolsGroup);
    additionalToolsLabel = nullptr;

    clearWidgetLayout(storageAnalysisGroup);
    storageAnalysisLabel = nullptr;

    clearWidgetLayout(backgroundProcessWidget);
    backgroundProcessLabel = nullptr;

    clearWidgetLayout(networkWidget);

    // Clear all text in labels - batch operations to reduce events
    QList<QLabel*> allLabels;

    // Don't add nulled labels to the batch

    // Add drive labels to the batch - not needed since we cleared vectors

    // Process events to ensure all deletions are processed
    QCoreApplication::processEvents();

    // Reset summary widget specially, as it always needs content
    if (summaryWidget) {
      try {
        QLayout* layout = summaryWidget->getContentLayout();
        if (layout) {
          // Clear existing content
          QLayoutItem* item;
          while ((item = layout->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget()) {
              // Explicitly set parent to null before deletion
              // to break any potential circular references
              w->setParent(nullptr);
              w->deleteLater();
            }
            delete item;

            // Process events after each item to ensure proper cleanup
            QCoreApplication::processEvents();
          }

          // Create and add new placeholder - use a QTimer to delay creation
          // to ensure previous items are fully deleted
          QTimer::singleShot(50, this, [this]() {
            if (summaryWidget && summaryWidget->getContentLayout()) {
              QLabel* placeholderLabel = new QLabel(
                "Run diagnostics to see system analysis results here.", this);
              placeholderLabel->setWordWrap(true);
              placeholderLabel->setStyleSheet(
                "color: #888888; font-style: italic;");
              summaryWidget->getContentLayout()->addWidget(placeholderLabel);
              summaryWidget->setVisible(true);
              QCoreApplication::processEvents();
            }
          });
        }
      } catch (const std::exception& e) {
        LOG_INFO << "Error resetting summary widget: " << e.what()
                 ;
      }
    }

    // Reset progress and status
    try {
      if (diagnosticProgress) {
        diagnosticProgress->blockSignals(true);
        diagnosticProgress->setValue(0);
        diagnosticProgress->blockSignals(false);
      }

      if (statusLabel) {
        statusLabel->setText("Ready to start diagnostics...");
        statusLabel->setStyleSheet(
          "color: #888888; font-size: 11px; background: transparent;");
      }
    } catch (const std::exception& e) {
      LOG_INFO << "Error resetting UI elements: " << e.what();
    }

    // Clear any stored comparison data that might be referencing freed memory
    cpuComparisonData.clear();

    // Final processing of any pending events
    QCoreApplication::processEvents();

    // Force garbage collection
    QTimer::singleShot(100, this, []() {
      // Request garbage collection through a delayed call
      QCoreApplication::processEvents();
      LOG_INFO << "Delayed cleanup completed";
    });

    LOG_INFO << "Clearing results completed";
  } catch (const std::exception& e) {
    // Handle any exceptions during clearing results
    LOG_ERROR << "Exception during clearAllResults: " << e.what();
  } catch (...) {
    // Catch any other exceptions
    LOG_INFO << "Unknown exception during clearAllResults";
  }
}

// New slot implementations for test control checkboxes
void DiagnosticView::setRunCpuTests(bool run) {
  // When master CPU tests checkbox is toggled, enable/disable CPU sub-options
  if (runCpuBoostTestsCheckbox) {
    runCpuBoostTestsCheckbox->setEnabled(run);
  }
  if (cpuThrottlingTestModeCombo) {
    cpuThrottlingTestModeCombo->setEnabled(run);
  }
  
  // If CPU tests are disabled, uncheck sub-options
  if (!run) {
    if (runCpuBoostTestsCheckbox) {
      runCpuBoostTestsCheckbox->setChecked(false);
    }
    if (cpuThrottlingTestModeCombo) {
      cpuThrottlingTestModeCombo->setCurrentIndex(0);  // Set to "Skip CPU Throttling"
    }
  }
  
  updateRunButtonState();
}

void DiagnosticView::setRunMemoryTests(bool run) {
  if (worker) {
    worker->setRunMemoryTests(run);
  }
  updateRunButtonState();
}

void DiagnosticView::setRunBackgroundTests(bool run) {
  if (worker) {
    worker->setRunBackgroundTests(run);
  }
  updateRunButtonState();
}

void DiagnosticView::updateRunButtonState() {
  // Check if at least one test is enabled
  bool anyTestEnabled = false;
  
  // Check main test categories
  if (runGpuTestsCheckbox && runGpuTestsCheckbox->isChecked()) anyTestEnabled = true;
  if (runMemoryTestsCheckbox && runMemoryTestsCheckbox->isChecked()) anyTestEnabled = true;
  if (runBackgroundTestsCheckbox && runBackgroundTestsCheckbox->isChecked()) anyTestEnabled = true;
  
  // Check CPU tests (either boost tests or throttling tests)
  if (runCpuTestsCheckbox && runCpuTestsCheckbox->isChecked()) {
    if ((runCpuBoostTestsCheckbox && runCpuBoostTestsCheckbox->isChecked()) ||
        (cpuThrottlingTestModeCombo && cpuThrottlingTestModeCombo->getCurrentIndex() > 0)) {
      anyTestEnabled = true;
    }
  }
  
  // Check drive tests
  if (driveTestModeCombo && driveTestModeCombo->getCurrentIndex() > 0) anyTestEnabled = true;
  
  // Check network tests
  if (networkTestModeCombo && networkTestModeCombo->getCurrentIndex() > 0) anyTestEnabled = true;
  
  // Check experimental features
  if (developerToolsCheckbox && developerToolsCheckbox->isChecked()) anyTestEnabled = true;
  if (storageAnalysisCheckbox && storageAnalysisCheckbox->isChecked()) anyTestEnabled = true;
  
  // Update run button state
  if (runButton) {
    runButton->setEnabled(anyTestEnabled);
  }
  
  // Update status text
  if (statusLabel) {
    if (anyTestEnabled) {
      statusLabel->setText("Ready to start diagnostics...");
    } else {
      statusLabel->setText("Select at least one test");
    }
  }
}
