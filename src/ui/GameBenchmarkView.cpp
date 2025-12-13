#include "ui/GameBenchmarkView.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <QFileInfo>
#include <windows.h>
#include "../logging/Logger.h"

#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "ApplicationSettings.h"
#include "benchmark/BenchmarkConstants.h"
#include "benchmark/BenchmarkStateTracker.h"
#include "benchmark/DemoFileManager.h"
#include "hardware/ConstantSystemInfo.h"
#include "ui/CustomWidgetWithTitle.h"
#include "ui/DetailedGuideDialog.h"
#include "ui/EACWarningDialog.h"
#include "ui/benchmark_results/BenchmarkResultsView.h"

// Color threshold constants - easy to adjust
namespace ColorThresholds {
  // FPS thresholds
  constexpr float FPS_GOOD = 60.0f;    // Green above this
  constexpr float FPS_OK = 30.0f;      // Yellow above this, red below
  
  // CPU usage thresholds (only color when under 15% or over 90%)
  constexpr float CPU_LOW = 15.0f;     // Yellow below this
  constexpr float CPU_HIGH = 90.0f;    // Yellow above this
  
  // GPU usage thresholds (only color when over 90%)
  constexpr float GPU_HIGH = 90.0f;    // Yellow above this
  
  // Memory usage thresholds
  constexpr float MEMORY_WARNING = 80.0f;  // Yellow above this
  constexpr float MEMORY_CRITICAL = 95.0f; // Red above this
  
  // Frame time thresholds (16ms = 60fps baseline)
  constexpr float FRAMETIME_GOOD = 16.0f;    // Green below this
  constexpr float FRAMETIME_OK = 24.0f;      // Yellow below this (50% more than 16ms)
  // Red above 24ms
}

// Helper functions for color determination
static QString getFpsColor(float fps) {
  if (fps >= ColorThresholds::FPS_GOOD) {
    return "#44FF44";  // Green
  } else if (fps >= ColorThresholds::FPS_OK) {
    return "#FFAA00";  // Yellow
  } else {
    return "#FF4444";  // Red
  }
}

static QString getCpuColor(float cpuUsage) {
  if (cpuUsage < ColorThresholds::CPU_LOW || cpuUsage > ColorThresholds::CPU_HIGH) {
    return "#FFAA00";  // Yellow for too low or too high
  } else {
    return "#dddddd";  // White/neutral for normal range
  }
}

static QString getGpuColor(float gpuUsage) {
  if (gpuUsage > ColorThresholds::GPU_HIGH) {
    return "#FFAA00";  // Yellow for very high usage
  } else {
    return "#dddddd";  // White/neutral otherwise
  }
}

static QString getMemoryColor(float memoryPercent) {
  if (memoryPercent > ColorThresholds::MEMORY_CRITICAL) {
    return "#FF4444";  // Red for critical usage
  } else if (memoryPercent > ColorThresholds::MEMORY_WARNING) {
    return "#FFAA00";  // Yellow for warning usage
  } else {
    return "#44FF44";  // Green for normal usage
  }
}

static QString getFrameTimeColor(float frameTime) {
  if (frameTime > ColorThresholds::FRAMETIME_OK) {
    return "#FF4444";  // Red for poor frame time
  } else if (frameTime > ColorThresholds::FRAMETIME_GOOD) {
    return "#FFAA00";  // Yellow for acceptable frame time
  } else {
    return "#44FF44";  // Green for good frame time
  }
}

GameBenchmarkView::GameBenchmarkView(QWidget* parent)
    : QWidget(parent), benchmark(nullptr), isRunning(false),
      receivedFirstMetrics(false), demoManager(nullptr), stackedWidget(nullptr),
      mainContentWidget(nullptr) {
  // LOG_INFO << "GameBenchmarkView: Constructor started" << std::endl;

  try {
    // Initialize the manager objects first
    benchmark = new BenchmarkManager(this);
    demoManager = new DemoFileManager(this);

    // LOG_INFO << "GameBenchmarkView: Managers initialized" << std::endl;

    // Setup the UI
    setupUI();

    // LOG_INFO << "GameBenchmarkView: UI setup completed" << std::endl;

    // Connect signals from benchmark manager
    if (benchmark) {
      connect(benchmark, &BenchmarkManager::benchmarkProgress, this,
              &GameBenchmarkView::onBenchmarkProgress, Qt::QueuedConnection);

      // NOTE: benchmarkMetrics signal disabled to avoid conflicts with benchmarkSample
      // The onBenchmarkSample() function now handles all UI updates including low FPS percentiles
      // connect(benchmark, &BenchmarkManager::benchmarkMetrics, this,
      //         &GameBenchmarkView::onBenchmarkMetrics, Qt::QueuedConnection);
              
      connect(benchmark, &BenchmarkManager::benchmarkSample, this,
              &GameBenchmarkView::onBenchmarkSample, Qt::QueuedConnection);

      connect(benchmark, &BenchmarkManager::benchmarkFinished, this,
              &GameBenchmarkView::onBenchmarkFinished, Qt::QueuedConnection);

      connect(benchmark, &BenchmarkManager::benchmarkError, this,
              &GameBenchmarkView::onBenchmarkError, Qt::QueuedConnection);

      connect(benchmark, &BenchmarkManager::benchmarkStateChanged, this,
              &GameBenchmarkView::onBenchmarkStateChanged, Qt::QueuedConnection);
    }

    // Create and setup progress update timer
    progressUpdateTimer = new QTimer(this);
    progressUpdateTimer->setInterval(
      100);  // Update every 100ms for smooth progress
    connect(progressUpdateTimer, &QTimer::timeout, this,
            &GameBenchmarkView::updateProgressDisplay);

    // Show EAC warning after UI elements are set up
    QTimer::singleShot(100, this, &GameBenchmarkView::showEACWarningIfNeeded);

    // LOG_INFO << "GameBenchmarkView: Constructor completed" << std::endl;

    if (outputContainer) {
      outputContainer->setVisible(false);
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "GameBenchmarkView: Exception in constructor: " << e.what();
  } catch (...) {
    LOG_ERROR << "GameBenchmarkView: Unknown exception in constructor";
  }
}

void GameBenchmarkView::setupUI() {
  // LOG_INFO << "GameBenchmarkView: setupUI started" << std::endl;

  // Initialize member button variables first to avoid null pointer issues
  resultsButton = new QPushButton("Results", this);

  // Create main layout with no margins
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  stackedWidget = new QStackedWidget(this);

  // Add stacked widget to the main layout first
  mainLayout->addWidget(stackedWidget);

  // Create main content widget for normal view
  mainContentWidget = new QWidget();
  QVBoxLayout* contentLayout = new QVBoxLayout(mainContentWidget);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);

  QWidget* rustInfoWidget = new QWidget(mainContentWidget);
  // Add background color to match instructions container
  rustInfoWidget->setStyleSheet(
    QString(R"(
        QWidget {
            background-color: %1;
        }
    )")
      .arg(CustomWidgetWithTitle::CONTENT_BG_COLOR));
  QHBoxLayout* rustInfoLayout = new QHBoxLayout(rustInfoWidget);
  rustInfoLayout->setContentsMargins(
    0, 0, 0, 0);  // No margins to align with Instructions title

  QLabel* rustPathLabel = new QLabel(rustInfoWidget);

  // Check if demoManager is valid
  if (!demoManager) {
    LOG_ERROR << "GameBenchmarkView: ERROR - demoManager is null!";
    demoManager = new DemoFileManager(this);
  }

  QString rustPath;

  try {
    rustPath = demoManager->getSavedRustPath();

    if (rustPath.isEmpty()) {
      rustPath = demoManager->findRustInstallationPath();

      if (!rustPath.isEmpty()) {
        demoManager->saveRustPath(rustPath);
      }
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "GameBenchmarkView: Exception during Rust path operations: "
              << e.what();
    rustPath = "";
  } catch (...) {
    LOG_ERROR
      << "GameBenchmarkView: Unknown exception during Rust path operations";
    rustPath = "";
  }

  QPushButton* rustPathButton = new QPushButton("Change", rustInfoWidget);
  rustPathButton->setFlat(true);
  rustPathButton->setCursor(Qt::PointingHandCursor);
  rustPathButton->setStyleSheet(
    "QPushButton { color: #0078d4; background: transparent; border: none; "
    "text-decoration: underline; }");

  try {
    if (!rustPathLabel) {
      LOG_ERROR << "GameBenchmarkView: ERROR - rustPathLabel is null!";
      rustPathLabel = new QLabel(rustInfoWidget);
    }

    if (!rustPath.isEmpty()) {
      rustPathLabel->setText(
        QString("Found Rust installation folder: %1").arg(rustPath));
      rustPathLabel->setStyleSheet("color: #999999; font-size: 12px;");
    } else {
      rustPathLabel->setText(
        "Rust installation folder not found automatically. Please select it.");
      rustPathLabel->setStyleSheet("color: #999999; font-size: 12px;");
    }

    if (!rustPathButton) {
      LOG_ERROR << "GameBenchmarkView: ERROR - rustPathButton is null!";
      return;
    }

    // Connect the button with safety checks
    connect(rustPathButton, &QPushButton::clicked, this, [this]() {
      // LOG_INFO << "GameBenchmarkView: Browse button clicked" << std::endl;
      if (!demoManager) {
        LOG_ERROR << "GameBenchmarkView: ERROR - demoManager is null in click handler!";
        return;
      }

      QString dir = QFileDialog::getExistingDirectory(
        this, tr("Select Rust Installation Folder"), QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
      if (!dir.isEmpty()) {
        if (demoManager->verifyRustPath(dir)) {
          QString normalizedPath = demoManager->normalizeRustPath(dir);
          demoManager->saveRustPath(normalizedPath);
          // Use a safer approach than direct recursive call
          QTimer::singleShot(0, this, [this]() {
            // Just update the path label instead of full UI rebuild
            if (demoManager) {
              QString path = demoManager->getSavedRustPath();
              for (QLabel* label : findChildren<QLabel*>()) {
                if (label->text().contains("Rust installation folder")) {
                  label->setText(
                    QString("Found Rust installation folder: %1").arg(path));
                  break;
                }
              }
            }
          });
        } else {
          QMessageBox::warning(
            this, "Invalid Folder",
            "The selected folder does not contain a valid Rust installation.");
        }
      }
    });

  } catch (const std::exception& e) {
        LOG_ERROR << "GameBenchmarkView: Exception setting up path label: "
                  << e.what();
  } catch (...) {
    LOG_ERROR << "GameBenchmarkView: Unknown exception setting up path label";
  }


  // Check for null pointers before adding widgets to layout
    if (!rustInfoLayout) {
      LOG_ERROR << "GameBenchmarkView: ERROR - rustInfoLayout is null!";
      return;
    }

    if (!rustPathLabel) {
      LOG_ERROR << "GameBenchmarkView: ERROR - rustPathLabel is null!";
      return;
    }

    if (!rustPathButton) {
      LOG_ERROR << "GameBenchmarkView: ERROR - rustPathButton is null!";
      return;
    }  // Add widgets to layout with try-catch for safety
  try {
    rustInfoLayout->addWidget(rustPathLabel);
    // LOG_INFO << "GameBenchmarkView: Added rustPathLabel to layout" <<
    // std::endl;

    rustInfoLayout->addWidget(rustPathButton);
    // LOG_INFO << "GameBenchmarkView: Added rustPathButton to layout" <<
    // std::endl;

    rustInfoLayout->addStretch();
    // LOG_INFO << "GameBenchmarkView: Added stretch to layout" << std::endl;

    // Add the rust info widget to the content layout FIRST
    if (!contentLayout) {
      LOG_ERROR << "GameBenchmarkView: ERROR - contentLayout is null!";
      return;
    }

    if (!rustInfoWidget) {
      LOG_ERROR << "GameBenchmarkView: ERROR - rustInfoWidget is null!";
      return;
    }

    // Continue with the rest of the UI setup

    // Create notification container (outside the scroll area)
    // LOG_INFO << "GameBenchmarkView: Setting up notification container" <<
    // std::endl;
    QWidget* notificationContainer = new QWidget();
    QVBoxLayout* notificationLayout = new QVBoxLayout(notificationContainer);
    notificationLayout->setContentsMargins(10, 10, 10, 0);

    QLabel* notificationBanner = new QLabel();
    notificationBanner->setStyleSheet(R"(
            QLabel {
                color: white;
                background: #0078d4;
                padding: 8px;
                border-radius: 4px;
                font-size: 12px;
            }
        )");
    notificationBanner->hide();
    notificationBanner->setAlignment(Qt::AlignCenter);
    notificationBanner->setFixedHeight(0);
    notificationBanner->setSizePolicy(QSizePolicy::Preferred,
                                      QSizePolicy::Fixed);

    // Add the banner to its layout
    notificationLayout->addWidget(notificationBanner);
    contentLayout->addWidget(notificationContainer);

    // Store notification banner
    this->notificationBanner = notificationBanner;

    // Add slide animation
    QPropertyAnimation* slideAnimation =
      new QPropertyAnimation(notificationBanner, "maximumHeight", this);
    slideAnimation->setDuration(300);
    this->slideAnimation = slideAnimation;

    // Create scroll area for main content - main part of the view
    // LOG_INFO << "GameBenchmarkView: Creating scroll area" << std::endl;
    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // LOG_INFO << "GameBenchmarkView: Scroll area created" << std::endl;

    // Create a container widget for all scrollable content
    QWidget* scrollContent = new QWidget(scrollArea);
    QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(10, 10, 10, 10);
    scrollLayout->setSpacing(20);

    // LOG_INFO << "GameBenchmarkView: Scroll content widget created" <<
    // std::endl;

    // Style the scroll area
    scrollArea->setStyleSheet(R"(
            QScrollArea {
                background-color: transparent;
                border: none;
            }
            QScrollBar:vertical {
                background: #1e1e1e;
                width: 12px;
                margin: 0px;
            }
            QScrollBar::handle:vertical {
                background: #333333;
                min-height: 20px;
                border-radius: 6px;
            }
            QScrollBar::handle:vertical:hover {
                background: #444444;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                height: 0px;
            }
            QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
                background: none;
            }
        )");

    // Create Rust Benchmark widget
    CustomWidgetWithTitle* benchmarkWidget =
      new CustomWidgetWithTitle("Rust Benchmark", scrollContent);
    QVBoxLayout* benchmarkContentLayout = benchmarkWidget->getContentLayout();

    // LOG_INFO << "GameBenchmarkView: Benchmark widget created" << std::endl;

    // Create instructions container
    QWidget* instructionsContainer = new QWidget(this);
    QVBoxLayout* instructionsLayout = new QVBoxLayout(instructionsContainer);
    instructionsLayout->setContentsMargins(0, 0, 0, 0);
    instructionsLayout->setSpacing(16);

    // LOG_INFO << "GameBenchmarkView: Instructions container created" <<
    // std::endl;

    // Add instructions title
    QLabel* titleLabel = new QLabel("<b>Instructions:</b>", this);
    titleLabel->setStyleSheet("color: #ffffff; font-size: 14px;");
    instructionsLayout->addWidget(titleLabel);

    // Modify the content styling to match DiagnosticView
    instructionsContainer->setStyleSheet(
      QString(R"(
            QWidget {
                background-color: %1;
            }
            QLabel {
                background: transparent;
                color: #ffffff;
            }
        )")
        .arg(CustomWidgetWithTitle::CONTENT_BG_COLOR));

    // Setup demo files information

    QString demosPath = demoManager->findRustDemosFolder();
    QString benchmarkFileName = demoManager->findLatestBenchmarkFile();
    QString benchmarkFilePath = demosPath + "/" + benchmarkFileName + ".dem";
    bool fileExists = QFileInfo::exists(benchmarkFilePath);

    // LOG_INFO << "GameBenchmarkView: Benchmark file path: " <<
    // benchmarkFilePath.toStdString() << std::endl;

    // For UI display, use a hardcoded name
    QString displayFileName = "benchmark demo";

    // First, create a consistent layout for all instruction steps
    // 1. Add benchmark file to Rust demos folder
    QHBoxLayout* firstLineLayout = new QHBoxLayout();
    firstLineLayout->setContentsMargins(16, 0, 0, 0);  // Consistent left margin

    // LOG_INFO << "GameBenchmarkView: Creating first line layout" << std::endl;

    // Create a fixed width area for the checkmark
    QLabel* checkmarkLabel = new QLabel(this);
    checkmarkLabel->setFixedWidth(20);
    if (fileExists) {
      checkmarkLabel->setText("âœ“");
      checkmarkLabel->setStyleSheet(
        "color: #44FF44; font-weight: bold; font-size: 14px; background: "
        "transparent;");
    } else {
      checkmarkLabel->setText("");
      checkmarkLabel->setStyleSheet("background: transparent;");
    }

    // LOG_INFO << "GameBenchmarkView: Checkmark label created" << std::endl;

    // Number label with consistent width
    QLabel* firstStepNumber = new QLabel("1.", this);
    firstStepNumber->setFixedWidth(15);
    firstStepNumber->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    firstStepNumber->setStyleSheet(
      "color: #ffffff; font-size: 12px; background: transparent;");

    // Instruction text without any numbering
    QString benchmarkDemosFolder = QDir::toNativeSeparators(
      QCoreApplication::applicationDirPath() + "/benchmark_demos");
    QString instructionText =
      QString("Add <a href=\"file:///%1\">%2</a> to the "
              "<a href=\"file:///%3\">Rust demos folder</a>.")
        .arg(benchmarkDemosFolder)
        .arg(displayFileName)
        .arg(demosPath);

    QLabel* firstLineLabel = new QLabel(instructionText, this);
    firstLineLabel->setObjectName("firstLineLabel");
    firstLineLabel->setStyleSheet(
      "color: #ffffff; font-size: 12px; background: transparent;");
    firstLineLabel->setOpenExternalLinks(true);
    firstLineLabel->setTextFormat(Qt::RichText);

    // LOG_INFO << "GameBenchmarkView: First line label created" << std::endl;

    // Add "Copy" button with consistent style
    QPushButton* copyButton = new QPushButton("Add Benchmark File", this);
    copyButton->setObjectName("copyButton");
    copyButton->setStyleSheet(R"(
            QPushButton {
                background-color: #0078d4;
                color: white;
                border: none;
                padding: 6px 12px;
                border-radius: 4px;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: #1084d8;
            }
            QPushButton:pressed {
                background-color: #006cc1;
            }
            QPushButton:disabled {
                background-color: #666666;
                color: #999999;
            }
        )");

    // Hide the button completely - keep implementation but make invisible to
    // users
    copyButton->setVisible(true);

    // LOG_INFO << "GameBenchmarkView: Copy button created (hidden)" <<
    // std::endl;

    // Make hyperlinks very clearly greyed out when file exists
    if (fileExists) {
      firstLineLabel->setStyleSheet(
        "color: #999999; font-size: 12px; background: transparent;");
      firstLineLabel->setText(
        QString("Add <a style=\"color: #666666; text-decoration: "
                "none;\">%1</a> to the "
                "<a style=\"color: #666666; text-decoration: none;\">Rust "
                "demos folder</a>.")
          .arg(displayFileName));
      firstStepNumber->setStyleSheet(
        "color: #999999; font-size: 12px; background: transparent;");
      copyButton->setEnabled(false);
    }

    // LOG_INFO << "GameBenchmarkView: First line label updated for file
    // existence" << std::endl;

    firstLineLayout->addWidget(checkmarkLabel);
    firstLineLayout->addWidget(firstStepNumber);
    firstLineLayout->addWidget(firstLineLabel);  // Remove stretch factor
    firstLineLayout->addWidget(copyButton);
    firstLineLayout->addStretch();  // Add stretch after the button to push
                                    // extra space to the right
    instructionsLayout->addLayout(firstLineLayout);

    // LOG_INFO << "GameBenchmarkView: First line layout completed" <<
    // std::endl;

    // Add spacer after first instruction
    QSpacerItem* spacer1 =
      new QSpacerItem(0, 10, QSizePolicy::Fixed, QSizePolicy::Fixed);
    instructionsLayout->addSpacerItem(spacer1);

    // LOG_INFO << "GameBenchmarkView: Spacer after first instruction added" <<
    // std::endl;

    // 2. Start RustClient.exe from the installation folder (NEW STEP)
    QHBoxLayout* newStepLayout = new QHBoxLayout();
    newStepLayout->setContentsMargins(16, 0, 0, 0);  // Consistent left margin

    // LOG_INFO << "GameBenchmarkView: Creating new step layout" << std::endl;

    // Empty space for potential checkmark
    QLabel* emptyCheckmarkNew = new QLabel(this);
    emptyCheckmarkNew->setFixedWidth(20);
    emptyCheckmarkNew->setStyleSheet("background: transparent;");

    // LOG_INFO << "GameBenchmarkView: Creating new step layout 2" << std::endl;

    // Number label with consistent width
    QLabel* newStepNumber = new QLabel("2.", this);
    newStepNumber->setFixedWidth(15);
    newStepNumber->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    newStepNumber->setStyleSheet(
      "color: #ffffff; font-size: 12px; background: transparent;");

    // Get Rust installation path for the hyperlink
    QString rustInstallPath = demoManager->findRustInstallationPath();

    // Create instruction with hyperlink
    QString rustPathInstruction =
      QString("Start <b>RustClient.exe</b> from the <a "
              "href=\"file:///%1\">installation folder</a> (This way EAC won't "
              "start with Rust).")
        .arg(QDir::toNativeSeparators(rustInstallPath));

    QLabel* newStepLabel = new QLabel(rustPathInstruction, this);
    newStepLabel->setStyleSheet(
      "color: #ffffff; font-size: 12px; background: transparent;");
    newStepLabel->setOpenExternalLinks(true);
    newStepLabel->setTextFormat(Qt::RichText);
    newStepLabel->setWordWrap(true);

    // LOG_INFO << "GameBenchmarkView: New step label created" << std::endl;

    newStepLayout->addWidget(emptyCheckmarkNew);
    newStepLayout->addWidget(newStepNumber);
    newStepLayout->addWidget(newStepLabel, 1);
    newStepLayout->addStretch();
    instructionsLayout->addLayout(newStepLayout);

    // Add spacer after new instruction
    QSpacerItem* spacerNew =
      new QSpacerItem(0, 10, QSizePolicy::Fixed, QSizePolicy::Fixed);
    instructionsLayout->addSpacerItem(spacerNew);

    // Add spacer after second instruction
    QSpacerItem* spacer2 =
      new QSpacerItem(0, 10, QSizePolicy::Fixed, QSizePolicy::Fixed);
    instructionsLayout->addSpacerItem(spacer2);

    // 3. Start monitoring metrics (with space for checkmark but no actual
    // checkmark)
    QHBoxLayout* secondLineLayout = new QHBoxLayout();
    secondLineLayout->setContentsMargins(16, 0, 0,
                                         0);  // Consistent left margin

    // Empty space for potential checkmark
    QLabel* emptyCheckmark2 = new QLabel(this);
    emptyCheckmark2->setFixedWidth(20);
    emptyCheckmark2->setStyleSheet("background: transparent;");

    // Number label with consistent width
    QLabel* secondStepNumber = new QLabel("3.", this);
    secondStepNumber->setFixedWidth(15);
    secondStepNumber->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    secondStepNumber->setStyleSheet(
      "color: #ffffff; font-size: 12px; background: transparent;");

    QLabel* secondLineLabel = new QLabel("Start monitoring metrics: ", this);
    secondLineLabel->setStyleSheet(
      "color: #ffffff; font-size: 12px; background: transparent;");

    // Create Start Monitoring button
    benchmarkButton = new QPushButton("Start Monitoring", this);
    benchmarkButton->setStyleSheet(R"(
            QPushButton {
                background-color: #0078d4;
                color: white;
                border: none;
                padding: 6px 12px;
                border-radius: 4px;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: #1084d8;
            }
            QPushButton:pressed {
                background-color: #006cc1;
            }
            QPushButton:disabled {
                background-color: #666666;
                color: #999999;
            }
        )");

    secondLineLayout->addWidget(emptyCheckmark2);
    secondLineLayout->addWidget(secondStepNumber);
    secondLineLayout->addWidget(secondLineLabel);
    secondLineLayout->addWidget(benchmarkButton);
    secondLineLayout->addStretch();
    instructionsLayout->addLayout(secondLineLayout);

    // Add explanatory text below step 3
    QHBoxLayout* explanationLayout = new QHBoxLayout();
    explanationLayout->setContentsMargins(16 + 20 + 15, 0, 0,
                                          0);  // Align with text after number

    QLabel* explanationLabel = new QLabel(
      "Start before the benchmark runs ingame. The correct duration will be "
      "automatically detected after the run for accurate results.",
      this);
    explanationLabel->setStyleSheet(
      "color: #999999; font-size: 11px; background: transparent;");
    explanationLabel->setWordWrap(true);

    // LOG_INFO << "GameBenchmarkView: Explanation label created" << std::endl;

    explanationLayout->addWidget(explanationLabel);
    instructionsLayout->addLayout(explanationLayout);

    // Add spacer after second instruction - reduce vertical spacing
    QSpacerItem* spacer2b =
      new QSpacerItem(0, 10, QSizePolicy::Fixed, QSizePolicy::Fixed);
    instructionsLayout->addSpacerItem(spacer2b);

    // 4. Paste command into Rust console (with space for checkmark but no
    // actual checkmark)
    QHBoxLayout* thirdLineLayout = new QHBoxLayout();
    thirdLineLayout->setContentsMargins(16, 0, 0, 0);  // Consistent left margin

    // Empty space for potential checkmark
    QLabel* emptyCheckmark3 = new QLabel(this);
    emptyCheckmark3->setFixedWidth(20);
    emptyCheckmark3->setStyleSheet("background: transparent;");

    // LOG_INFO << "GameBenchmarkView: Creating third line layout" << std::endl;

    // Number label with consistent width
    QLabel* thirdStepNumber = new QLabel("4.", this);
    thirdStepNumber->setFixedWidth(15);
    thirdStepNumber->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    thirdStepNumber->setStyleSheet(
      "color: #ffffff; font-size: 12px; background: transparent;");

    QLabel* thirdLineLabel =
      new QLabel("Paste this command into Rust console:", this);
    thirdLineLabel->setStyleSheet(
      "color: #ffffff; font-size: 12px; background: transparent;");

    thirdLineLayout->addWidget(emptyCheckmark3);
    thirdLineLayout->addWidget(thirdStepNumber);
    thirdLineLayout->addWidget(thirdLineLabel);
    thirdLineLayout->addStretch();
    instructionsLayout->addLayout(thirdLineLayout);

    // LOG_INFO << "GameBenchmarkView: Third line layout created" << std::endl;

    // Add the command display with proper alignment
    QHBoxLayout* commandLayout = new QHBoxLayout();
    commandLayout->setContentsMargins(
      16 + 20 + 15, 4, 0,
      0);  // Align with text after number, reduced top margin

    QLabel* commandLabel =
      new QLabel("demo.play benchmark", this);
    commandLabel->setStyleSheet(R"(
            QLabel {
                color: #ffffff;
                font-size: 12px;
                padding: 4px 8px;
                background-color: #1e1e1e;
                border: 1px solid #333333;
                border-radius: 3px;
                font-family: 'Consolas', monospace;
            }
        )");
    commandLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QPushButton* copyCommandButton = new QPushButton("Copy", this);
    copyCommandButton->setStyleSheet(R"(
            QPushButton {
                background-color: #333333;
                color: white;
                border: none;
                padding: 2px 8px;
                border-radius: 2px;
                font-size: 11px;
            }
            QPushButton:hover {
                background-color: #404040;
            }
        )");

    commandLayout->addWidget(commandLabel);
    commandLayout->addWidget(copyCommandButton);
    commandLayout->addStretch();
    instructionsLayout->addLayout(commandLayout);

    // Add spacer after third instruction
    QSpacerItem* spacer3 =
      new QSpacerItem(0, 16, QSizePolicy::Fixed, QSizePolicy::Fixed);
    instructionsLayout->addSpacerItem(spacer3);

    // 5. Wait for benchmark to end...
    QHBoxLayout* fourthLineLayout = new QHBoxLayout();
    fourthLineLayout->setContentsMargins(16, 0, 0,
                                         0);  // Consistent left margin

    // Empty space for potential checkmark
    QLabel* emptyCheckmark4 = new QLabel(this);
    emptyCheckmark4->setFixedWidth(20);
    emptyCheckmark4->setStyleSheet("background: transparent;");

    // LOG_INFO << "GameBenchmarkView: Creating fourth line layout" <<
    // std::endl;

    // Number label with consistent width
    QLabel* fourthStepNumber = new QLabel("5.", this);
    fourthStepNumber->setFixedWidth(15);
    fourthStepNumber->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fourthStepNumber->setStyleSheet(
      "color: #ffffff; font-size: 12px; background: transparent;");

    QLabel* fourthLineLabel =
      new QLabel("Wait for the benchmark to end automatically. (2-4min)", this);
    fourthLineLabel->setTextFormat(Qt::RichText);
    fourthLineLabel->setStyleSheet(
      "color: #ffffff; font-size: 12px; background: transparent;");
    fourthLineLabel->setWordWrap(true);

    fourthLineLayout->addWidget(emptyCheckmark4);
    fourthLineLayout->addWidget(fourthStepNumber);
    fourthLineLayout->addWidget(fourthLineLabel,
                                1);  // Give stretch factor to enable word wrap
    instructionsLayout->addLayout(fourthLineLayout);

    // LOG_INFO << "GameBenchmarkView: Fourth line layout created" << std::endl;

    // Add instructions container to content layout
    QWidget* rustInfoContainer = new QWidget();
    rustInfoContainer->setStyleSheet(
      QString(R"(
            QWidget {
                background-color: %1;
            }
        )")
        .arg(CustomWidgetWithTitle::CONTENT_BG_COLOR));
    QVBoxLayout* rustInfoContainerLayout = new QVBoxLayout(rustInfoContainer);
    rustInfoContainerLayout->setContentsMargins(
      0, 8, 12, 4);  // Set left margin to 0 to match Instructions title
    rustInfoContainerLayout->addWidget(rustInfoWidget);

    benchmarkContentLayout->addWidget(rustInfoContainer);
    benchmarkContentLayout->addWidget(instructionsContainer);

    // Add guide button and container
    // Add vertical space before controls container
    QSpacerItem* controlsTopSpacer =
      new QSpacerItem(0, 16, QSizePolicy::Fixed, QSizePolicy::Fixed);
    benchmarkContentLayout->addSpacerItem(controlsTopSpacer);

    // Add a hyperlink-style button for the detailed guide
    QPushButton* guideButton = new QPushButton("Detailed Guide", this);
    guideButton->setStyleSheet(R"(
            QPushButton {
                color: #0078d4;
                border: none;
                text-align: left;
                padding: 2px 0px;
                font-size: 12px;
                background: transparent;
                text-decoration: underline;
            }
            QPushButton:hover {
                color: #1084d8;
            }
        )");
    guideButton->setCursor(Qt::PointingHandCursor);

    // Connect the button to open the detailed guide dialog
    connect(guideButton, &QPushButton::clicked, this, [this]() {
      DetailedGuideDialog dialog(this);
      dialog.exec();
    });

    // Create a container for the guide button with the same background color
    QWidget* guideContainer = new QWidget(this);
    guideContainer->setStyleSheet(
      QString(R"(
            QWidget {
                background-color: %1;
            }
        )")
        .arg(CustomWidgetWithTitle::CONTENT_BG_COLOR));

    // LOG_INFO << "GameBenchmarkView: Guide button created" << std::endl;

    QHBoxLayout* guideLayout = new QHBoxLayout(guideContainer);
    guideLayout->setContentsMargins(12, 8, 12, 4);
    guideLayout->addWidget(guideButton, 0, Qt::AlignLeft);
    guideLayout->addStretch();

    // Add the guide container to the content layout
    benchmarkContentLayout->addWidget(guideContainer);

    // Add the benchmark widget to the scroll layout
    scrollLayout->addWidget(benchmarkWidget);

    // Add stretch to push content to the top
    scrollLayout->addStretch(1);

    // Set the scroll content and add to main layout
    scrollArea->setWidget(scrollContent);
    contentLayout->addWidget(scrollArea,
                             1);  // Give scroll area a stretch factor of 1

    LOG_INFO << "GameBenchmarkView: Scroll area added to main layout";

    // Create output section container (fixed at bottom)
    outputContainer = new QWidget();
    QVBoxLayout* outputContainerLayout = new QVBoxLayout(outputContainer);
    outputContainerLayout->setContentsMargins(10, 0, 10, 10);
    outputContainerLayout->setSpacing(5);

    // Create expand button with hyperlink style - now using member variable
    expandButton = new QPushButton("â–¼ Show Details", this);
    expandButton->setStyleSheet(R"(
            QPushButton {
                color: #0078d4;
                border: none;
                text-align: left;
                padding: 2px;
                font-size: 12px;
                background: transparent;
            }
            QPushButton:hover {
                color: #1084d8;
                text-decoration: underline;
            }
        )");
    expandButton->setCursor(Qt::PointingHandCursor);

    // Create output content widget - compact 2x2 grid layout
    outputContent = new QWidget(this);
    QVBoxLayout* outputMainLayout = new QVBoxLayout(outputContent);
    outputMainLayout->setContentsMargins(10, 10, 10, 10);
    outputMainLayout->setSpacing(5);
    outputContent->setStyleSheet(R"(
            QWidget {
                border: 1px solid #333333;
                border-radius: 4px;
                background-color: #1e1e1e;
            }
        )");

    // Hide output content initially
    outputContent->hide();

    // Create horizontal layout for tables
    QWidget* tablesWidget = new QWidget(this);
    QHBoxLayout* tablesLayout = new QHBoxLayout(tablesWidget);
    tablesLayout->setContentsMargins(5, 5, 5, 5);
    tablesLayout->setSpacing(10);
    tablesWidget->setStyleSheet("QWidget { border: none; background-color: transparent; }");

    // Create excel-style tables using QTableWidget
    
    // 1. FPS Table (1 column for values, with row labels)
    fpsTable = createExcelStyleTable(4, 1, {}, {"FPS", "1% Low", "5% Low", "0.1% Low"});
    
    // 2. System Resources Table (1 column for values, with row labels)
    systemTable = createExcelStyleTable(4, 1, {}, {"CPU", "GPU", "RAM", "VRAM"});
    
    // 3. Timings Table (2 columns: Avg + Max, with row labels)
    timingsTable = createExcelStyleTable(3, 2, {"Avg (ms)", "Max (ms)"}, {"Frame", "GPU", "CPU"});

    // Add tables to horizontal layout
    tablesLayout->addWidget(fpsTable);
    tablesLayout->addWidget(systemTable);
    tablesLayout->addWidget(timingsTable);

    // Create bottom text display (removed progressTextLabel)
    displayTextLabel = new QLabel("Resolution: <span style='color: #ffffff;'>--x--</span> | Process: <span style='color: #dddddd;'>--</span>", this);
    displayTextLabel->setStyleSheet("color: #0078d4; font-size: 9pt; background: transparent; border: none;");
    displayTextLabel->setTextFormat(Qt::RichText);
    displayTextLabel->setAlignment(Qt::AlignLeft);

    // Add to main layout
    outputMainLayout->addWidget(tablesWidget);
    outputMainLayout->addWidget(displayTextLabel);

    // Keep legacy labels for compatibility during transition
    rawFpsLabel = new QLabel("--", this);
    lowFpsLabel = new QLabel("--", this);
    cpuUsageLabel = new QLabel("--", this);
    gpuUsageLabel = new QLabel("--", this);
    memoryUsageLabel = new QLabel("--", this);
    vramUsageLabel = new QLabel("--", this);
    displayInfoLabel = new QLabel("--", this);
    processNameLabel = new QLabel("--", this);
    frameTimeLabel = new QLabel("--", this);
    cpuTimeLabel = new QLabel("--", this);
    gpuTimeLabel = new QLabel("--", this);
    progressLabel = new QLabel("--", this);

    LOG_INFO << "GameBenchmarkView: Compact metric tables created";

    // Add widgets to container
    outputContainerLayout->addWidget(expandButton);
    outputContainerLayout->addWidget(outputContent);

    // Add state label
    stateLabel = new QLabel(this);
    stateLabel->setTextFormat(Qt::RichText);
    stateLabel->setText("<font color='#FFFFFF'>Benchmark status: </font>"
                        "<font color='#FFFFFF'>Ready to start monitoring</font>");
    stateLabel->setAlignment(Qt::AlignLeft);
    outputContainerLayout->addWidget(stateLabel);

    // Always set save to file to true
    benchmark->setSaveToFile(true);

    // Connect signals

    connect(benchmark, &BenchmarkManager::benchmarkProgress, this,
            &GameBenchmarkView::onBenchmarkProgress);

    connect(benchmark, &BenchmarkManager::benchmarkMetrics, this,
            &GameBenchmarkView::onBenchmarkMetrics);

    connect(benchmark, &BenchmarkManager::benchmarkFinished, this,
            &GameBenchmarkView::onBenchmarkFinished);

    connect(benchmark, &BenchmarkManager::benchmarkError, this,
            &GameBenchmarkView::onBenchmarkError);

    // Connect expand button
    connect(expandButton, &QPushButton::clicked, [this]() {
      bool isExpanded = this->outputContent->isVisible();
      this->outputContent->setVisible(!isExpanded);
      this->expandButton->setText(isExpanded ? "â–¼ Show Details"
                                             : "â–² Hide Details");
    });

    // Create cooldown timer
    cooldownTimer = new QTimer(this);
    cooldownTimer->setSingleShot(true);
    cooldownTimer->setInterval(COOLDOWN_MS);

    // Connect cooldown timer
    connect(cooldownTimer, &QTimer::timeout, this, [this]() {
      benchmarkButton->setText("Start Monitoring");
      benchmarkButton->setEnabled(true);
      benchmarkButton->setStyleSheet(R"(
                QPushButton {
                    background-color: #0078d4;
                    color: white;
                    border: none;
                    padding: 6px 12px;
                    border-radius: 4px;
                    font-size: 12px;
                }
                QPushButton:hover {
                    background-color: #1084d8;
                }
                QPushButton:pressed {
                    background-color: #006cc1;
                }
            )");
    });

    // Connect benchmark button
    connect(benchmarkButton, &QPushButton::clicked, this, [this]() {
      if (!isRunning && !cooldownTimer->isActive()) {
        benchmarkButton->setEnabled(false);

        if (benchmark->startBenchmark("RustClient.exe", 600)) {

          isRunning = true;
          benchmarkButton->setText("Stop Monitoring");
          benchmarkButton->setEnabled(true);
        } else {
          benchmarkButton->setEnabled(true);
        }
      } else if (isRunning) {
        benchmarkButton->setEnabled(false);
        if (benchmark->stopBenchmark()) {
        } else {
          benchmarkButton->setEnabled(true);
        }
      }
    });

    // Connect copy command button
    connect(copyCommandButton, &QPushButton::clicked, []() {
      QGuiApplication::clipboard()->setText(
        "demo.play benchmark");
    });

    // Update the click handler for copying demo files
    connect(
      copyButton, &QPushButton::clicked, this,
      [this, notificationBanner, slideAnimation, checkmarkLabel, firstLineLabel,
       firstStepNumber, copyButton]() {
        QString demosPath = demoManager->findRustDemosFolder();
        if (demosPath.isEmpty()) {
          QMessageBox::critical(this, "Error",
                                "Could not find Rust demos folder");
          return;
        }

        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(
          this, "Copy Demo Files", "Add benchmark demo files to demos folder?",
          QMessageBox::Ok | QMessageBox::Cancel);

        if (reply == QMessageBox::Ok) {
          if (demoManager->copyDemoFiles(demosPath)) {
            notificationBanner->setText("âœ“ Demo files copied successfully");
            notificationBanner->setStyleSheet(
              "QLabel { color: white; background: #28a745; padding: 8px; "
              "border-radius: 4px; font-size: 12px; }");

            // Update the checkmark
            checkmarkLabel->setText("âœ“");
            checkmarkLabel->setStyleSheet(
              "color: #44FF44; font-weight: bold; font-size: 14px; background: "
              "transparent;");

            // Gray out the step number
            firstStepNumber->setStyleSheet(
              "color: #999999; font-size: 12px; background: transparent;");

            // Gray out the instruction text and disable the hyperlinks
            // completely
            firstLineLabel->setStyleSheet(
              "color: #999999; font-size: 12px; background: transparent;");
            QString benchmarkFileName = demoManager->findLatestBenchmarkFile();
            firstLineLabel->setText(
              QString("Add <a style=\"color: #666666; text-decoration: "
                      "none;\">%1</a> to the "
                      "<a style=\"color: #666666; text-decoration: "
                      "none;\">Rust demos folder</a>.")
                .arg(benchmarkFileName + ".dem"));

            // Disable the copy button
            copyButton->setEnabled(false);
          } else {
            notificationBanner->setText(
              "âŒ Copy failed - Please add the files manually");
            notificationBanner->setStyleSheet(
              "QLabel { color: white; background: #dc3545; padding: 8px; "
              "border-radius: 4px; font-size: 12px; }");
          }

          notificationBanner->setMaximumHeight(0);
          notificationBanner->show();
          slideAnimation->setStartValue(0);
          slideAnimation->setEndValue(40);
          slideAnimation->start();

          QTimer::singleShot(
            10000, this, [notificationBanner, slideAnimation]() {
              slideAnimation->setStartValue(40);
              slideAnimation->setEndValue(0);
              slideAnimation->start();
              QObject::connect(slideAnimation, &QPropertyAnimation::finished,
                               notificationBanner, &QLabel::hide);
            });
        }
      });

        LOG_INFO << "GameBenchmarkView: Copy button connected";

    // Upload button removed (uploads now automatic)

    // Add connection for benchmark status
    connect(
      benchmark, &BenchmarkManager::benchmarkStatus, this,
      [this, notificationBanner, slideAnimation](const QString& status,
                                                 bool isError) {
        notificationBanner->setText(status);
        notificationBanner->setStyleSheet(
          QString("QLabel { "
                  "color: white; "
                  "background: %1; "
                  "padding: 8px; "
                  "border-radius: 4px; "
                  "font-size: 12px; "
                  "}")
            .arg(isError ? "#dc3545" : "#28a745"));

        notificationBanner->setMaximumHeight(0);
        notificationBanner->show();
        slideAnimation->setStartValue(0);
        slideAnimation->setEndValue(40);
        slideAnimation->start();

        QTimer::singleShot(5000, this, [notificationBanner, slideAnimation]() {
          slideAnimation->setStartValue(40);
          slideAnimation->setEndValue(0);
          slideAnimation->start();
          QObject::connect(slideAnimation, &QPropertyAnimation::finished,
                           notificationBanner, &QLabel::hide);
        });
      });

    // Create a stacked widget for benchmark/results views
    QStackedWidget* resultsStackedWidget = new QStackedWidget(this);

    LOG_INFO << "GameBenchmarkView: Stacked widget created";

    // Create results view
    BenchmarkResultsView* resultsView = new BenchmarkResultsView(this);

    // Add widgets to results stacked widget
    resultsStackedWidget->addWidget(mainContentWidget);
    resultsStackedWidget->addWidget(resultsView);

    // LOG_INFO << "GameBenchmarkView: Results view added to stacked widget";

    // Connect results button to switch views
    connect(resultsButton, &QPushButton::clicked,
            [resultsStackedWidget, resultsView]() {
              resultsView->refreshBenchmarkList();
              resultsStackedWidget->setCurrentIndex(1);
            });

    // Connect back button in results view
    connect(
      resultsView, &BenchmarkResultsView::backRequested,
      [resultsStackedWidget]() { resultsStackedWidget->setCurrentIndex(0); });

    mainLayout->removeWidget(
      stackedWidget);      // Remove the old stackedWidget first
    delete stackedWidget;  // Delete the old widget since we won't use it
    stackedWidget =
      resultsStackedWidget;  // Set our member variable to the new widget
    mainLayout->addWidget(stackedWidget);

    LOG_INFO << "GameBenchmarkView: Stacked widget replaced";

    // Add connection for NVENC usage warning
    connect(benchmark, &BenchmarkManager::nvencUsageDetected, this,
            [this, notificationBanner, slideAnimation](bool isActive) {
              if (isActive) {
                notificationBanner->setText(
                  "âš ï¸ Screen capture detected (NVENC). Stop recording/streaming "
                  "(OBS, Discord Go Live, GeForce Experience/Instant Replay, "
                  "etc.) to avoid skewing FPS and frametime metrics.");
                notificationBanner->setStyleSheet(
                  "QLabel { color: white; background: #FF9900; "
                  "padding: 8px; border-radius: 4px; font-size: 12px; }");

                notificationBanner->setMaximumHeight(0);
                notificationBanner->show();
                slideAnimation->setStartValue(0);
                slideAnimation->setEndValue(40);
                slideAnimation->start();

                // Keep notification visible until NVENC usage stops
              } else {
                // Hide the notification when NVENC usage stops
                slideAnimation->setStartValue(40);
                slideAnimation->setEndValue(0);
                slideAnimation->start();
                QObject::connect(slideAnimation, &QPropertyAnimation::finished,
                                 notificationBanner, &QLabel::hide);
              }
            });

    // Get benchmark filename
    benchmarkFileName = demoManager->getCurrentBenchmarkFilename();
    rustPath = demoManager->getSavedRustPath();
    if (rustPath.isEmpty()) {
      rustPath = demoManager->findRustInstallationPath();
    }

    // Check if file exists in Rust demos folder
    bool fileExistsInRustDemos = false;
    if (!rustPath.isEmpty()) {
      fileExistsInRustDemos =
        demoManager->isBenchmarkFileInRustDemos(benchmarkFileName);
    }

    // Update the checkmark based on whether the file exists in Rust demos folder
    if (fileExistsInRustDemos) {
      checkmarkLabel->setText("âœ“");
      checkmarkLabel->setStyleSheet(
        "color: #44FF44; font-weight: bold; font-size: 14px; background: "
        "transparent;");

      // Gray out the step number
      firstStepNumber->setStyleSheet(
        "color: #999999; font-size: 12px; background: transparent;");

      // Gray out the instruction text and disable the hyperlinks
      firstLineLabel->setStyleSheet(
        "color: #999999; font-size: 12px; background: transparent;");
      firstLineLabel->setText(
        QString("Add <a style=\"color: #666666; text-decoration: "
                "none;\">%1</a> to the "
                "<a style=\"color: #666666; text-decoration: none;\">Rust "
                "demos folder</a>.")
          .arg(benchmarkFileName));
    } else {
      checkmarkLabel->setText("");
      checkmarkLabel->setStyleSheet("background: transparent;");

      // Normal styling for active step
      firstStepNumber->setStyleSheet(
        "color: #ffffff; font-size: 12px; background: transparent;");

      // Active hyperlinks for the instruction
      firstLineLabel->setStyleSheet(
        "color: #ffffff; font-size: 12px; background: transparent;");

      // Make the benchmark file name a clickable link
      firstLineLabel->setText(
        QString("Add <a style=\"color: #0078d4;\">%1</a> to the "
                "<a style=\"color: #0078d4;\">Rust demos folder</a>.")
          .arg(benchmarkFileName));

      // Connect click events for the hyperlinks
      connect(firstLineLabel, &QLabel::linkActivated, this,
              [this, benchmarkFileName](const QString& link) {
                if (link.contains(benchmarkFileName)) {
                  // Show the benchmark file
                  QString exePath = QCoreApplication::applicationDirPath();
                  QDesktopServices::openUrl(
                    QUrl::fromLocalFile(exePath + "/benchmark_demos"));
                } else {
                  // Open Rust demos folder or create it if it doesn't exist
                  QString rustPath = demoManager->getSavedRustPath();
                  if (rustPath.isEmpty()) {
                    rustPath = demoManager->findRustInstallationPath();
                  }

                  if (!rustPath.isEmpty()) {
                    QString demosPath = rustPath + "/demos";
                    QDir demosDir(demosPath);
                    if (!demosDir.exists()) {
                      demosDir.mkpath(".");
                    }
                    QDesktopServices::openUrl(QUrl::fromLocalFile(demosPath));
                  } else {
                    QMessageBox::warning(
                      this, "Rust Not Found",
                      "Please select the Rust installation folder first.");
                  }
                }
              });
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "GameBenchmarkView: Exception in layout setup: " << e.what();
  } catch (...) {
    LOG_ERROR << "GameBenchmarkView: Unknown exception in layout setup";
  }

  // Then ADD this line at the end of setupUI() right before the closing brace,
  // after the EAC warning setup, around line 1282:

  // Create a fixed bottom panel for buttons
  QWidget* bottomPanel = new QWidget(this);
  bottomPanel->setStyleSheet("background-color: #222222;");
  bottomPanel->setFixedHeight(50);

  QHBoxLayout* bottomPanelLayout = new QHBoxLayout(bottomPanel);
  bottomPanelLayout->setContentsMargins(10, 5, 10, 5);

  // Create new button for the bottom panel
  QPushButton* bottomResultsButton = new QPushButton("Results", this);
  bottomResultsButton->setStyleSheet(R"(
        QPushButton {
            color: #ffffff;
            background: #28a745;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
        }
        QPushButton:hover {
            background: #218838;
        }
        QPushButton:pressed {
            background: #1e7e34;
        }
    )");

  // Add spacer to push button to the right
  bottomPanelLayout->addStretch();
  bottomPanelLayout->addWidget(bottomResultsButton);

  // Connect these buttons to the same actions
  connect(bottomResultsButton, &QPushButton::clicked, [this]() {
    if (stackedWidget) {
      BenchmarkResultsView* resultsView = nullptr;

      // Find the resultsView in the stacked widget
      for (int i = 0; i < stackedWidget->count(); ++i) {
        resultsView =
          qobject_cast<BenchmarkResultsView*>(stackedWidget->widget(i));
        if (resultsView) {
          break;
        }
      }

      if (resultsView) {
        resultsView->refreshBenchmarkList();
        stackedWidget->setCurrentWidget(resultsView);
      }
    }
  });

  // First add output container to main layout, then the bottom panel last
  mainLayout->addWidget(outputContainer);
  mainLayout->addWidget(bottomPanel);
}

void GameBenchmarkView::onBenchmarkProgress(int percentage) {
  // This method is no longer used since we now calculate progress based on
  // benchmark state The percentage parameter is based on the maximum monitoring
  // duration (safety mechanism) and doesn't reflect the actual benchmark
  // progress, so we ignore it

  // Progress is now handled by updateProgressDisplay() based on benchmark state
}

void GameBenchmarkView::onBenchmarkMetrics(const PM_METRICS& metrics) {
  // Auto-show output section on first metrics
  if (!receivedFirstMetrics) {
    outputContent->show();
    expandButton->setText("â–² Hide Details");
    receivedFirstMetrics = true;

    // Initialize process name (only once)
    processNameLabel->setText("Process: RustClient.exe");
    displayTextLabel->setText("Resolution: <span style='color: #dddddd;'>--x--</span> | Process: <span style='color: #dddddd;'>RustClient.exe</span>");
  }

  // *** Get the latest data point which contains all system metrics from PDH ***
  auto latestData = benchmark->getLatestDataPoint();

  // Update FPS values with color coding
  QString fpsColor;
  if (metrics.fps < 15.0f) {
    fpsColor = "#FF4444";  // Red
  } else if (metrics.fps < 60.0f) {
    fpsColor = "#FFAA00";  // Yellow
  } else {
    fpsColor = "#44FF44";  // Green
  }

  rawFpsLabel->setText(QString("<span style='color: %1;'>%2</span>")
                         .arg(fpsColor)
                         .arg(metrics.fps, 0, 'f', 1));

  // Update FPS table (main FPS)
  updateTableValue(fpsTable, 0, 1, QString::number(metrics.fps, 'f', 1), fpsColor);

  // Update frame timings with color coding using real max values
  QString frameTimeColor;
  if (metrics.frametime > 30.0f) {
    frameTimeColor = "#FF4444";  // Red for high frame time
  } else if (metrics.frametime > 16.0f) {
    frameTimeColor = "#FFAA00";  // Yellow for medium frame time
  } else {
    frameTimeColor = "#44FF44";  // Green for low frame time
  }

  frameTimeLabel->setText(
    QString("Frame: <span style='color: %1;'>%2</span> ms (Avg) | %3 ms (Max)")
      .arg(frameTimeColor)
      .arg(metrics.frametime, 0, 'f', 2)
      .arg(metrics.maxFrameTime, 0, 'f', 2));

  // Update timings table (Avg -> col 0, Max -> col 1)
  updateTableValue(timingsTable, 0, 0, QString::number(metrics.frametime, 'f', 2), frameTimeColor);
  updateTableValue(timingsTable, 0, 1, QString::number(metrics.maxFrameTime, 'f', 2), frameTimeColor);

  // Update CPU and GPU timings with color coding
  QString cpuTimeColor;
  if (metrics.cpuRenderTime > 30.0f) {
    cpuTimeColor = "#FF4444";  // Red for high CPU time
  } else if (metrics.cpuRenderTime > 16.0f) {
    cpuTimeColor = "#FFAA00";  // Yellow for medium CPU time
  } else {
    cpuTimeColor = "#44FF44";  // Green for low CPU time
  }

  cpuTimeLabel->setText(
    QString("CPU: <span style='color: %1;'>%2</span> ms (Avg) | %3 ms (Max)")
      .arg(cpuTimeColor)
      .arg(metrics.cpuRenderTime, 0, 'f', 2)
      .arg(metrics.maxCpuRenderTime, 0, 'f', 2));

  // Update CPU timing in table (Avg -> col 0, Max -> col 1)
  updateTableValue(timingsTable, 2, 0, QString::number(metrics.cpuRenderTime, 'f', 2), cpuTimeColor);
  updateTableValue(timingsTable, 2, 1, QString::number(metrics.maxCpuRenderTime, 'f', 2), cpuTimeColor);

  QString gpuTimeColor;
  if (metrics.gpuRenderTime > 30.0f) {
    gpuTimeColor = "#FF4444";  // Red for high GPU time
  } else if (metrics.gpuRenderTime > 16.0f) {
    gpuTimeColor = "#FFAA00";  // Yellow for medium GPU time
  } else {
    gpuTimeColor = "#44FF44";  // Green for low GPU time
  }

  gpuTimeLabel->setText(
    QString("GPU: <span style='color: %1;'>%2</span> ms (Avg) | %3 ms (Max)")
      .arg(gpuTimeColor)
      .arg(metrics.gpuRenderTime, 0, 'f', 2)
      .arg(metrics.maxGpuRenderTime, 0, 'f', 2));

  // Update GPU timing in table (Avg -> col 0, Max -> col 1)
  updateTableValue(timingsTable, 1, 0, QString::number(metrics.gpuRenderTime, 'f', 2), gpuTimeColor);
  updateTableValue(timingsTable, 1, 1, QString::number(metrics.maxGpuRenderTime, 'f', 2), gpuTimeColor);

  // Update display resolution
  if (metrics.destWidth > 0 && metrics.destHeight > 0) {
    displayInfoLabel->setText(
      QString("Resolution: <span style='color: #0078d4;'>%1Ã—%2</span>")
        .arg(metrics.destWidth)
        .arg(metrics.destHeight));
    
    // Update resolution in bottom text
    displayTextLabel->setText(QString("Resolution: <span style='color: #ffffff;'>%1Ã—%2</span> | Process: <span style='color: #dddddd;'>RustClient.exe</span>").arg(metrics.destWidth).arg(metrics.destHeight));
  }

  // NOTE: Low FPS percentiles are now updated by onBenchmarkSample() which receives
  // the correct cumulative values from emitUIMetrics(). The latestData here contains
  // per-second percentiles which are not suitable for UI display.

  // *** Update system metrics from PDH data ***
  // (latestData already declared earlier in the function)
  
  // Update CPU usage from PDH data with validation
  float cpuUsage = latestData.procProcessorTime;
  float avgCoreUsage = 0.0f;
  float peakCoreUsage = 0.0f;
  
  QString cpuText;
  
  if (cpuUsage < 0) {
    // Invalid CPU data
    cpuText = "CPU: <span style='color: #888888;'>Data unavailable</span>";
  } else {
    if (!latestData.perCoreCpuUsagePdh.empty()) {
      for (float coreUsage : latestData.perCoreCpuUsagePdh) {
        if (coreUsage >= 0) {  // Only use valid core data
          avgCoreUsage += coreUsage;
          peakCoreUsage = std::max(peakCoreUsage, coreUsage);
        }
      }
      avgCoreUsage /= latestData.perCoreCpuUsagePdh.size();
    } else {
      // Use total CPU usage if per-core data isn't available
      avgCoreUsage = cpuUsage;
      peakCoreUsage = cpuUsage;
    }

    // Color code CPU usage values
    QString cpuColor = avgCoreUsage > 10.0f ? "#44FF44" : "#FFAA00";
    QString peakCoreColor = peakCoreUsage > 10.0f ? "#44FF44" : "#FFAA00";

    cpuText = QString("CPU Avg: <span style='color: %1;'>%2</span>% | Peak Core: <span "
                     "style='color: %3;'>%4</span>%")
                .arg(cpuColor)
                .arg(avgCoreUsage, 0, 'f', 1)
                .arg(peakCoreColor)
                .arg(peakCoreUsage, 0, 'f', 1);
  }
  
  cpuUsageLabel->setText(cpuText);

  // Update CPU usage in table
  if (avgCoreUsage >= 0) {
    QString cpuColor = avgCoreUsage > 10.0f ? "#44FF44" : "#FFAA00";
    QString peakCoreColor = peakCoreUsage > 10.0f ? "#44FF44" : "#FFAA00";
    updateTableValue(systemTable, 0, 1, QString::number(avgCoreUsage, 'f', 1) + "%", cpuColor);
  } else {
    updateTableValue(systemTable, 0, 1, "N/A", "#888888");
  }

  // Update GPU usage from the latest data point with validation
  float gpuUsage = latestData.gpuUtilization;
  QString gpuText;
  
  if (gpuUsage < 0) {
    // Invalid GPU data (negative values indicate collection failure)
    gpuText = "GPU: <span style='color: #888888;'>Data unavailable</span>";
  } else {
    QString gpuColor = getGpuColor(gpuUsage);
    gpuText = QString("GPU: <span style='color: %1;'>%2</span>%")
                .arg(gpuColor)
                .arg(gpuUsage, 0, 'f', 1);
  }
  
  gpuUsageLabel->setText(gpuText);

  // Update GPU usage in system table
  if (gpuUsage >= 0) {
    QString gpuColor = getGpuColor(gpuUsage);
    updateTableValue(systemTable, 1, 0, QString::number(gpuUsage, 'f', 1) + "%", gpuColor);
  } else {
    updateTableValue(systemTable, 1, 0, "N/A", "#888888");
  }

  // Update memory usage from PDH data using ConstantSystemInfo
  float availableMemoryGB = latestData.availableMemoryMB / 1024.0f;
  
  // Get total system memory from ConstantSystemInfo (consistent with BenchmarkManager)
  const auto& sysInfo = SystemMetrics::GetConstantSystemInfo();
  float ramTotalGB = sysInfo.totalPhysicalMemoryMB / 1024.0f;
  
  // Calculate used memory and percentage using the same logic as BenchmarkManager
  float usedMemoryGB = (ramTotalGB - availableMemoryGB);
  float ramUsagePercent = latestData.memoryLoad; // Use the calculated memoryLoad from PDH
  
  // Validate data - if invalid, show as unavailable
  if (ramTotalGB <= 0 || availableMemoryGB < 0 || ramUsagePercent < 0) {
    usedMemoryGB = -1;
    ramUsagePercent = -1;
  }
  QString ramColor;
  QString memoryText;
  
  if (ramUsagePercent < 0 || usedMemoryGB < 0) {
    // Invalid data - show as unavailable
    ramColor = "#888888";  // Gray for unavailable data
    memoryText = "RAM: <span style='color: #888888;'>Data unavailable</span>";
  } else {
    // Valid data - apply color coding
    ramColor = getMemoryColor(ramUsagePercent);
    
    memoryText = QString("RAM: <span style='color: %1;'>%2</span>/%3 GB (%4% used)")
      .arg(ramColor)
      .arg(usedMemoryGB, 0, 'f', 1)
      .arg(ramTotalGB, 0, 'f', 0)
      .arg(ramUsagePercent, 0, 'f', 1);
  }
  
  memoryUsageLabel->setText(memoryText);

  // Update RAM table
  if (ramUsagePercent >= 0 && usedMemoryGB >= 0) {
    QString ramColor;
    ramColor = getMemoryColor(ramUsagePercent);
    updateTableValue(systemTable, 2, 1, QString::number(usedMemoryGB, 'f', 1) + "/" + QString::number(ramTotalGB, 'f', 0) + " GB", ramColor);
  } else {
    updateTableValue(systemTable, 2, 0, "N/A", "#888888");
  }

  // Update VRAM usage from GPU metrics
  float vramUsedGB = latestData.gpuMemUsed / (1024.0f * 1024.0f * 1024.0f);
  float vramTotalGB = latestData.gpuMemTotal / (1024.0f * 1024.0f * 1024.0f);

  if (vramTotalGB > 0.0f) {
    float vramUsagePercent = (vramUsedGB / vramTotalGB) * 100.0f;
    QString vramColor;
    if (vramUsagePercent > 90.0f) {
      vramColor = "#FF4444";  // Red for high VRAM usage
    } else if (vramUsagePercent > 75.0f) {
      vramColor = "#FFAA00";  // Yellow for medium VRAM usage
    } else {
      vramColor = "#44FF44";  // Green for low VRAM usage
    }

    vramUsageLabel->setText(
      QString("VRAM: <span style='color: %1;'>%2</span>/%3 GB (%4%)")
        .arg(vramColor)
        .arg(vramUsedGB, 0, 'f', 1)
        .arg(vramTotalGB, 0, 'f', 1)
        .arg(vramUsagePercent, 0, 'f', 1));
    
    // Update VRAM table
    updateTableValue(systemTable, 3, 1, QString::number(vramUsedGB, 'f', 1) + "/" + QString::number(vramTotalGB, 'f', 1) + " GB", vramColor);
  } else {
    vramUsageLabel->setText("VRAM: <span style='color: #888888;'>N/A</span>");
    updateTableValue(systemTable, 3, 1, "N/A", "#888888");
  }
}

void GameBenchmarkView::onBenchmarkSample(const BenchmarkDataPoint& sample) {
  // Auto-show output section on first metrics
  if (!receivedFirstMetrics) {
    outputContent->show();
    expandButton->setText("â–² Hide Details");
    receivedFirstMetrics = true;

    // Initialize process name (only once)
    processNameLabel->setText("Process: RustClient.exe");
  }

  // Update FPS values with color coding using new thresholds
  QString fpsColor = getFpsColor(sample.fps);

  rawFpsLabel->setText(QString("<span style='color: %1;'>%2</span>")
                         .arg(fpsColor)
                         .arg(sample.fps, 0, 'f', 1));

  // Update FPS table (main FPS)
  updateTableValue(fpsTable, 0, 0, QString::number(sample.fps, 'f', 1), fpsColor);

  // Update frame timings with color coding using new thresholds
  QString frameTimeColor = getFrameTimeColor(sample.frameTime);

  frameTimeLabel->setText(
    QString("Frame: <span style='color: %1;'>%2</span> ms (Avg) | %3 ms (Max)")
      .arg(frameTimeColor)
      .arg(sample.frameTime, 0, 'f', 2)
      .arg(sample.highestFrameTime, 0, 'f', 2));

  // Update timings table
  updateTableValue(timingsTable, 0, 0, QString::number(sample.frameTime, 'f', 2), frameTimeColor);
  updateTableValue(timingsTable, 0, 1, QString::number(sample.highestFrameTime, 'f', 2), frameTimeColor);

  // Update CPU and GPU render times using sample data with new thresholds
  QString cpuTimeColor = getFrameTimeColor(sample.cpuRenderTime);
  QString gpuTimeColor = getFrameTimeColor(sample.gpuRenderTime);
  
  // Update GPU and CPU timing tables
  updateTableValue(timingsTable, 1, 0, QString::number(sample.gpuRenderTime, 'f', 2), gpuTimeColor);
  updateTableValue(timingsTable, 1, 1, QString::number(sample.highestGpuTime, 'f', 2), gpuTimeColor);
  updateTableValue(timingsTable, 2, 0, QString::number(sample.cpuRenderTime, 'f', 2), cpuTimeColor);
  updateTableValue(timingsTable, 2, 1, QString::number(sample.highestCpuTime, 'f', 2), cpuTimeColor);

  // Update low FPS percentiles from the sample (cumulative values from emitUIMetrics)
  float fps1pct = sample.lowFps1Percent;
  float fps01pct = sample.lowFps05Percent;
  float fps5pct = sample.lowFps5Percent;
  
  QString lowFpsText;
  
  // Check if we have valid data
  if (fps1pct < 0 || fps01pct < 0 || fps5pct < 0) {
    // Invalid data - show as unavailable
    lowFpsText = "1% Low: <span style='color: #888888;'>N/A</span> | "
                 "0.1% Low: <span style='color: #888888;'>N/A</span> | "
                 "5% Low: <span style='color: #888888;'>N/A</span>";
  } else {
    // Valid data - apply color coding to each percentile value using new thresholds
    QString color1pct = getFpsColor(fps1pct);
    QString color01pct = getFpsColor(fps01pct);
    QString color5pct = getFpsColor(fps5pct);

    lowFpsText = QString("1% Low: <span style='color: %1;'>%2</span> | "
                         "0.1% Low: <span style='color: %3;'>%4</span> | "
                         "5% Low: <span style='color: %5;'>%6</span>")
                   .arg(color1pct)
                   .arg(fps1pct, 0, 'f', 1)
                   .arg(color01pct)
                   .arg(fps01pct, 0, 'f', 1)
                   .arg(color5pct)
                   .arg(fps5pct, 0, 'f', 1);
  }
  
  lowFpsLabel->setText(lowFpsText);

  // Update FPS percentiles in table
  if (fps1pct >= 0 && fps01pct >= 0 && fps5pct >= 0) {
    QString color1pct = getFpsColor(fps1pct);
    QString color01pct = getFpsColor(fps01pct);
    QString color5pct = getFpsColor(fps5pct);
    
    updateTableValue(fpsTable, 1, 0, QString::number(fps1pct, 'f', 1), color1pct);
    updateTableValue(fpsTable, 2, 0, QString::number(fps5pct, 'f', 1), color5pct);
    updateTableValue(fpsTable, 3, 0, QString::number(fps01pct, 'f', 1), color01pct);
  } else {
    updateTableValue(fpsTable, 1, 0, "N/A", "#888888");
    updateTableValue(fpsTable, 2, 0, "N/A", "#888888");
    updateTableValue(fpsTable, 3, 0, "N/A", "#888888");
  }

  // Update CPU usage from coherent sample data
  float cpuUsage = sample.procProcessorTime;
  QString cpuText;
  
  if (cpuUsage < 0) {
    cpuText = "CPU: <span style='color: #888888;'>Data unavailable</span>";
  } else {
    QString cpuColor = cpuUsage > 50.0f ? "#FF4444" : (cpuUsage > 25.0f ? "#FFAA00" : "#44FF44");
    cpuText = QString("CPU: <span style='color: %1;'>%2</span>%")
                .arg(cpuColor)
                .arg(cpuUsage, 0, 'f', 1);
  }
  
  cpuUsageLabel->setText(cpuText);

  // Update CPU usage in system table
  if (cpuUsage >= 0) {
    QString cpuColor = cpuUsage > 50.0f ? "#FF4444" : (cpuUsage > 25.0f ? "#FFAA00" : "#44FF44");
    updateTableValue(systemTable, 0, 0, QString::number(cpuUsage, 'f', 1) + "%", cpuColor);
  } else {
    updateTableValue(systemTable, 0, 0, "N/A", "#888888");
  }

  // Update GPU usage from coherent sample data
  float gpuUsage = sample.gpuUtilization;
  QString gpuText;
  
  if (gpuUsage < 0) {
    gpuText = "GPU: <span style='color: #888888;'>Data unavailable</span>";
  } else {
    QString gpuColor = getGpuColor(gpuUsage);
    gpuText = QString("GPU: <span style='color: %1;'>%2</span>%")
                .arg(gpuColor)
                .arg(gpuUsage, 0, 'f', 1);
  }
  
  gpuUsageLabel->setText(gpuText);

  // Update GPU usage in system table
  if (gpuUsage >= 0) {
    QString gpuColor = getGpuColor(gpuUsage);
    updateTableValue(systemTable, 1, 0, QString::number(gpuUsage, 'f', 1) + "%", gpuColor);
  } else {
    updateTableValue(systemTable, 1, 0, "N/A", "#888888");
  }

  // Update memory usage from coherent sample data
  float availableMemoryGB = sample.availableMemoryMB / 1024.0f;
  const auto& sysInfo = SystemMetrics::GetConstantSystemInfo();
  float ramTotalGB = sysInfo.totalPhysicalMemoryMB / 1024.0f;
  float usedMemoryGB = (ramTotalGB - availableMemoryGB);
  float ramUsagePercent = sample.memoryLoad;
  
  if (ramTotalGB <= 0 || availableMemoryGB < 0 || ramUsagePercent < 0) {
    memoryUsageLabel->setText("RAM: <span style='color: #888888;'>Data unavailable</span>");
    updateTableValue(systemTable, 2, 0, "N/A", "#888888");
  } else {
    QString ramColor = getMemoryColor(ramUsagePercent);
    
    memoryUsageLabel->setText(
      QString("RAM: <span style='color: %1;'>%2</span> GB / %3 GB (%4%)")
        .arg(ramColor)
        .arg(usedMemoryGB, 0, 'f', 1)
        .arg(ramTotalGB, 0, 'f', 1)
        .arg(ramUsagePercent, 0, 'f', 1));
    
    // Update RAM usage in system table
    updateTableValue(systemTable, 2, 0, QString::number(usedMemoryGB, 'f', 1) + "/" + QString::number(ramTotalGB, 'f', 0) + " GB", ramColor);
  }

  // Update GPU temperature and VRAM
  if (sample.gpuTemp > 0) {
    QString tempColor = sample.gpuTemp > 80.0f ? "#FF4444" : (sample.gpuTemp > 70.0f ? "#FFAA00" : "#44FF44");
    QString tempText = QString(" | Temp: <span style='color: %1;'>%2</span>Â°C")
                         .arg(tempColor)
                         .arg(sample.gpuTemp, 0, 'f', 0);
    gpuUsageLabel->setText(gpuText + tempText);
  } else {
    gpuUsageLabel->setText(gpuText + " | Temp: <span style='color: #888888;'>N/A</span>");
  }

  // Update VRAM usage (if available from GPU metrics)
  if (sample.gpuMemUsed > 0 && sample.gpuMemTotal > 0) {
    float vramUsedGB = sample.gpuMemUsed / (1024.0f * 1024.0f * 1024.0f);
    float vramTotalGB = sample.gpuMemTotal / (1024.0f * 1024.0f * 1024.0f);
    float vramPercent = (sample.gpuMemUsed / static_cast<float>(sample.gpuMemTotal)) * 100.0f;
    
    QString vramColor = getMemoryColor(vramPercent);
    vramUsageLabel->setText(
      QString("VRAM: <span style='color: %1;'>%2</span> GB / %3 GB (%4%)")
        .arg(vramColor)
        .arg(vramUsedGB, 0, 'f', 1)
        .arg(vramTotalGB, 0, 'f', 1)
        .arg(vramPercent, 0, 'f', 1));
    
    // Update VRAM usage in system table
    updateTableValue(systemTable, 3, 0, QString::number(vramUsedGB, 'f', 1) + "/" + QString::number(vramTotalGB, 'f', 1) + " GB", vramColor);
  } else {
    vramUsageLabel->setText("VRAM: <span style='color: #888888;'>N/A</span>");
    updateTableValue(systemTable, 3, 0, "N/A", "#888888");
  }
}

void GameBenchmarkView::onBenchmarkFinished() {

  isRunning = false;
  receivedFirstMetrics = false;

  // Reset benchmark state and stop progress timer
  currentBenchmarkState = BenchmarkStateTracker::State::OFF;
  progressUpdateTimer->stop();
  benchmarkStartTime = std::chrono::steady_clock::time_point{};
  monitoringStartTime = std::chrono::steady_clock::time_point{};

  // Reset all labels to default values with neutral color
  rawFpsLabel->setText("<span style='color: #dddddd;'>--</span>");
  lowFpsLabel->setText("<span style='color: #dddddd;'>1% Low: -- | 0.1% Low: "
                       "-- | 5% Low: --</span>");
  cpuUsageLabel->setText(
    "<span style='color: #dddddd;'>CPU Avg: --% | Peak Core: --%</span>");
  gpuUsageLabel->setText("<span style='color: #dddddd;'>GPU: --%</span>");
  memoryUsageLabel->setText(
    "<span style='color: #dddddd;'>RAM: -- / -- GB</span>");
  vramUsageLabel->setText(
    "<span style='color: #dddddd;'>VRAM: -- / -- GB</span>");
  displayInfoLabel->setText(
    "<span style='color: #dddddd;'>Resolution: --x--</span>");
  processNameLabel->setText("<span style='color: #dddddd;'>Process: --</span>");
  frameTimeLabel->setText(
    "<span style='color: #dddddd;'>Frame: -- ms (Avg) | -- ms (Max)</span>");
  cpuTimeLabel->setText(
    "<span style='color: #dddddd;'>CPU: -- ms (Avg) | -- ms (Max)</span>");
  gpuTimeLabel->setText(
    "<span style='color: #dddddd;'>GPU: -- ms (Avg) | -- ms (Max)</span>");

  outputContent->hide();
  expandButton->setText("â–¼ Show Details");

  // Start cooldown timer - during cooldown show appropriate text and disable button
  benchmarkButton->setText("Cooling down...");
  benchmarkButton->setEnabled(false);
  // Apply disabled style
  benchmarkButton->setStyleSheet(R"(
        QPushButton {
            background-color: #666666;
            color: #999999;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
        }
    )");
  cooldownTimer->start();
}

void GameBenchmarkView::onBenchmarkError(const QString& error) {
  QMessageBox::critical(this, "Benchmark Error", error);
  isRunning = false;
  benchmarkButton->setText("Start Monitoring");
  benchmarkButton->setEnabled(true);
  // Reset to normal style
  benchmarkButton->setStyleSheet(R"(
        QPushButton {
            background-color: #0078d4;
            color: white;
            border: none;
            padding: 6px 12px;
            border-radius: 4px;
            font-size: 12px;
        }
        QPushButton:hover {
            background-color: #1084d8;
        }
        QPushButton:pressed {
            background-color: #006cc1;
        }
    )");
  receivedFirstMetrics = false;  // Reset flag on error too

  // Reset benchmark state and stop progress timer
  currentBenchmarkState = BenchmarkStateTracker::State::OFF;
  progressUpdateTimer->stop();
  benchmarkStartTime = std::chrono::steady_clock::time_point{};
  monitoringStartTime = std::chrono::steady_clock::time_point{};

  // Update state label to reflect error state
  if (stateLabel) {
    stateLabel->setText("<font color='#FFFFFF'>Benchmark status: </font>"
                       "<font color='#FF4444'>Monitoring stopped due to error</font>");
  }
}

QString GameBenchmarkView::findRustDemosFolder() {
  QStringList possiblePaths;

  // Check Steam registry for install path
  QSettings steamRegistry(
    "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam",
    QSettings::NativeFormat);
  QString steamPath = steamRegistry.value("InstallPath").toString();

  if (!steamPath.isEmpty()) {
    possiblePaths << steamPath + "/steamapps/common/Rust";
  }

  // Add default Steam paths
  possiblePaths << "C:/Program Files (x86)/Steam/steamapps/common/Rust"
                << "C:/Program Files/Steam/steamapps/common/Rust";

  // Check all drive letters
  for (char drive = 'C'; drive <= 'Z'; drive++) {
    possiblePaths
      << QString("%1:/SteamLibrary/steamapps/common/Rust").arg(drive);
  }

  // Find first valid Rust installation and check for demos folder
  possiblePaths << "C:/Program Files (x86)/Steam/steamapps/common/Rust"
                << "C:/Program Files/Steam/steamapps/common/Rust";

  // Check all drive letters
  for (char drive = 'C'; drive <= 'Z'; drive++) {
    possiblePaths
      << QString("%1:/SteamLibrary/steamapps/common/Rust").arg(drive);
  }

  // Find first valid Rust installation and check for demos folder
  for (const QString& path : possiblePaths) {
    if (QFileInfo::exists(path)) {
      QString rustPath = QDir::toNativeSeparators(path);
      QString demosPath = QDir::toNativeSeparators(path + "/demos");

      // Return demos path if it exists, otherwise return Rust folder path
      if (QFileInfo::exists(demosPath)) {
        return demosPath;
      }
      return rustPath;
    }
  }

  return QString();
}

QLabel* GameBenchmarkView::createInfoIcon(const QString& tooltipText) {
  QLabel* infoIcon = new QLabel(this);
  infoIcon->setFixedSize(12, 12);
  infoIcon->setStyleSheet(R"(
        QLabel {
            color: #ffffff;
            background-color: #1e1e1e;
            border: 1px solid #0078d4;
            border-radius: 6px;
            font-size: 9px;
            font-weight: bold;
            padding-bottom: 2px;
            margin-left: 4px;
        }
        QLabel:hover {
            background-color: #333333;
            border-color: #1084d8;
        }
    )");
  infoIcon->setAlignment(Qt::AlignCenter);
  infoIcon->setText("i");
  infoIcon->setCursor(Qt::PointingHandCursor);

  infoIcon->installEventFilter(this);
  infoIcon->setProperty("tooltip", tooltipText);

  return infoIcon;
}

bool GameBenchmarkView::eventFilter(QObject* obj, QEvent* event) {
  if (event->type() == QEvent::Enter) {
    QLabel* label = qobject_cast<QLabel*>(obj);
    if (label && label->property("tooltip").isValid()) {
      QToolTip::showText(QCursor::pos(), label->property("tooltip").toString(),
                         label);
    }
  } else if (event->type() == QEvent::Leave) {
    QToolTip::hideText();
  }
  return QWidget::eventFilter(obj, event);
}

QWidget* GameBenchmarkView::createMetricBox(const QString& title) {
  QWidget* box = new QWidget(this);
  // Remove inner borders - only use background
  box->setStyleSheet(R"(
        QWidget {
            background-color: transparent;
            border: none;
        }
    )");
  
  // Set fixed size for consistent grid layout
  box->setFixedSize(210, 120);
  box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  QVBoxLayout* layout = new QVBoxLayout(box);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(4);

  QLabel* titleLabel = new QLabel(title, box);
  titleLabel->setStyleSheet("color: #0078d4; font-size: 12px; font-weight: "
                            "bold; background: transparent;");
  titleLabel->setFixedHeight(16);

  layout->addWidget(titleLabel);

  return box;
}

QTableWidget* GameBenchmarkView::createMetricTable(int rows, int cols) {
  QTableWidget* table = new QTableWidget(rows, cols, this);
  
  // Remove table headers and borders
  table->setShowGrid(false);
  table->horizontalHeader()->setVisible(false);
  table->verticalHeader()->setVisible(false);
  table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setFocusPolicy(Qt::NoFocus);
  
  // Set table style to be transparent with no borders
  table->setStyleSheet(R"(
    QTableWidget {
      background-color: transparent;
      border: none;
      gridline-color: transparent;
    }
    QTableWidget::item {
      border: none;
      padding: 2px 4px;
      background-color: transparent;
    }
  )");
  
  // Set fixed size to prevent layout changes - make wider for better text display
  table->setFixedSize(190, 80);
  table->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  
  // Distribute columns evenly with more width
  for (int i = 0; i < cols; i++) {
    table->setColumnWidth(i, 190 / cols);
  }
  
  // Set row heights
  for (int i = 0; i < rows; i++) {
    table->setRowHeight(i, 80 / rows);
  }
  
  return table;
}

QTableWidget* GameBenchmarkView::createCompactTable(int rows, int cols, int width, int height) {
  QTableWidget* table = new QTableWidget(rows, cols, this);
  
  // Remove headers and borders
  table->setShowGrid(false);
  table->horizontalHeader()->setVisible(false);
  table->verticalHeader()->setVisible(false);
  table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setFocusPolicy(Qt::NoFocus);
  
  // Compact transparent style
  table->setStyleSheet(R"(
    QTableWidget {
      background-color: transparent;
      border: none;
      gridline-color: transparent;
    }
    QTableWidget::item {
      border: none;
      padding: 1px 2px;
      background-color: transparent;
    }
  )");
  
  // Set fixed size
  table->setFixedSize(width, height);
  table->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  
  // Distribute space evenly
  for (int i = 0; i < cols; i++) {
    table->setColumnWidth(i, width / cols);
  }
  for (int i = 0; i < rows; i++) {
    table->setRowHeight(i, height / rows);
  }
  
  return table;
}

QTableWidget* GameBenchmarkView::createExcelStyleTable(int rows, int cols, const QStringList& headers, const QStringList& rowLabels) {
  QTableWidget* table = new QTableWidget(rows, cols, this);
  
  // Set headers if provided
  if (!headers.isEmpty() && headers.size() == cols) {
    table->setHorizontalHeaderLabels(headers);
  } else {
    table->horizontalHeader()->setVisible(false);
  }
  
  if (!rowLabels.isEmpty() && rowLabels.size() == rows) {
    table->setVerticalHeaderLabels(rowLabels);
  } else {
    table->verticalHeader()->setVisible(false);
  }
  
  // Excel-like styling with no visible borders
  table->setStyleSheet(R"(
    QTableWidget {
      background-color: #1e1e1e;
      border: none;
      gridline-color: transparent;
      border-radius: 4px;
      font-size: 9pt;
    }
    QTableWidget::item {
      padding: 4px 8px;
      border: none;
      background-color: #2a2a2a;
    }
    QTableWidget::item:alternate {
      background-color: #323232;
    }
    QHeaderView::section {
      background-color: #333333;
      color: #0078d4;
      font-weight: bold;
      padding: 4px 8px;
      border: none;
      font-size: 8pt;
    }
  )");
  
  // Configure table behavior
  table->setSelectionMode(QAbstractItemView::NoSelection);
  table->setFocusPolicy(Qt::NoFocus);
  table->setAlternatingRowColors(true);
  table->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  
  // Initialize all cells with "--"
  for (int row = 0; row < rows; row++) {
    for (int col = 0; col < cols; col++) {
      QTableWidgetItem* item = new QTableWidgetItem("--");
      item->setTextAlignment(Qt::AlignCenter);
      table->setItem(row, col, item);
    }
  }
  
  // Set wider column widths for better value display
  for (int col = 0; col < cols; col++) {
    if (cols == 1) {
      // Single column tables - wider for values like "999.9%" or "99.9/64 GB"
      table->setColumnWidth(col, 120);
    } else if (cols == 2) {
      // Two column tables - wider for timing values like "99.99"
      table->setColumnWidth(col, 90);
    }
  }
  
  // Auto-resize rows to content
  table->resizeRowsToContents();
  
  // Set fixed height but flexible width to prevent vertical layout changes
  table->setFixedHeight(table->sizeHint().height());
  table->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  
  return table;
}

void GameBenchmarkView::updateTableValue(QTableWidget* table, int row, int col, const QString& value, const QString& color) {
  if (!table || row >= table->rowCount() || col >= table->columnCount()) {
    return;
  }
  
  QTableWidgetItem* item = table->item(row, col);
  if (!item) {
    item = new QTableWidgetItem();
    table->setItem(row, col, item);
  }  QString oldText = item->text();
  item->setText(value);
  item->setForeground(QColor(color));
  
  // Verify the update worked
  QString newText = item->text();
  
  // Force table update/repaint
  table->viewport()->update();
}

void GameBenchmarkView::resetTableValues() {
  // Reset FPS table values (row labels are in vertical headers, values in column 0)
  if (fpsTable) {
    for (int row = 0; row < fpsTable->rowCount(); row++) {
      updateTableValue(fpsTable, row, 0, "--", "#dddddd");
    }
  }
  
  // Reset system table values (row labels are in vertical headers, values in column 0)
  if (systemTable) {
    for (int row = 0; row < systemTable->rowCount(); row++) {
      updateTableValue(systemTable, row, 0, "--", "#dddddd");
    }
  }
  
  // Reset timings table values (row labels are in vertical headers, values in columns 0 and 1)
  if (timingsTable) {
    for (int row = 0; row < timingsTable->rowCount(); row++) {
      updateTableValue(timingsTable, row, 0, "--", "#dddddd");
      updateTableValue(timingsTable, row, 1, "--", "#dddddd");
    }
  }
}

void GameBenchmarkView::cancelOperations() {
  // Cancel any ongoing benchmark operations
  if (benchmark) {
    benchmark->stopBenchmark();
  }
}

void GameBenchmarkView::showEACWarningIfNeeded() {
  // Check if stackedWidget is valid before proceeding
  if (!stackedWidget) {
    LOG_ERROR << "GameBenchmarkView: ERROR - stackedWidget is null in "
                 "showEACWarningIfNeeded!";
    return;
  }

  // Check if mainContentWidget is valid
  if (!mainContentWidget) {
    LOG_ERROR << "GameBenchmarkView: ERROR - mainContentWidget is null in "
                 "showEACWarningIfNeeded!";
    return;
  }

  try {
    // Only proceed if warning should be shown
    if (EACWarningDialog::shouldShowWarning()) {
      // Create embedded warning widget
      EACWarningWidget* warningWidget =
        EACWarningDialog::createEmbeddedWarning(this);

      // Add to stacked widget
      stackedWidget->addWidget(warningWidget);
      stackedWidget->setCurrentWidget(warningWidget);

      // When understood, switch to main content
      connect(warningWidget, &EACWarningWidget::understood, this,
              [this, warningWidget]() {
                if (stackedWidget && mainContentWidget) {
                  // Switch to main content
                  stackedWidget->setCurrentWidget(mainContentWidget);

                  // Remove warning widget from stacked widget
                  stackedWidget->removeWidget(warningWidget);
                  warningWidget->deleteLater();

                  // Show the output container since we've passed the EAC warning
                  if (outputContainer) {
                    outputContainer->setVisible(true);
                  }
                }
              });
    } else {
      stackedWidget->setCurrentWidget(mainContentWidget);
      
      // Show the output container since we're not showing EAC warning
      if (outputContainer) {
        outputContainer->setVisible(true);
      }
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "GameBenchmarkView: Exception in showEACWarningIfNeeded: "
              << e.what();
  } catch (...) {
    
    LOG_ERROR  << "GameBenchmarkView: Unknown exception in showEACWarningIfNeeded";
     
  }
}

void GameBenchmarkView::onBenchmarkStateChanged(const QString& state) {
  // Update the state label
  if (stateLabel) {
    stateLabel->setText(state);
  }

  // Parse the state to determine the current benchmark state
  if (state.contains("OFF")) {
    currentBenchmarkState = BenchmarkStateTracker::State::OFF;
    progressUpdateTimer->stop();
    
    // Reset button state when benchmark goes to OFF (handles both manual stop and automatic completion)
    if (isRunning || benchmarkButton->text() == "Stop Monitoring") {
      isRunning = false;
      benchmarkButton->setText("Cooling down...");
      benchmarkButton->setEnabled(false);
      benchmarkButton->setStyleSheet(R"(
        QPushButton {
            background-color: #666666;
            color: #999999;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
        }
      )");
      cooldownTimer->start();
      
      // Reset timestamps
      benchmarkStartTime = std::chrono::steady_clock::time_point{};
      monitoringStartTime = std::chrono::steady_clock::time_point{};
    }
  } else if (state.contains("Waiting")) {
    currentBenchmarkState = BenchmarkStateTracker::State::WAITING;
    // Start the progress update timer when we start monitoring
    if (!progressUpdateTimer->isActive()) {
      monitoringStartTime = std::chrono::steady_clock::now();
      progressUpdateTimer->start();
    }
    // Ensure outputContainer is visible when monitoring starts
    if (outputContainer) {
      outputContainer->setVisible(true);
    }
  } else if (state.contains("Running")) {
    currentBenchmarkState = BenchmarkStateTracker::State::RUNNING;
    // Record when the benchmark actually started
    if (benchmarkStartTime == std::chrono::steady_clock::time_point{}) {
      benchmarkStartTime = std::chrono::steady_clock::now();
    }
    // Ensure outputContainer is visible when benchmark is running
    if (outputContainer) {
      outputContainer->setVisible(true);
    }
  } else if (state.contains("Finalizing")) {
    currentBenchmarkState = BenchmarkStateTracker::State::COOLDOWN;
  }

  // Update progress display immediately
  updateProgressDisplay();
}

void GameBenchmarkView::updateProgressDisplay() {
  if (!stateLabel) return;

  auto now = std::chrono::steady_clock::now();

  switch (currentBenchmarkState) {
    case BenchmarkStateTracker::State::OFF:
      stateLabel->setText(
        "<font color='#FFFFFF'>Benchmark status: </font>"
        "<font color='#dddddd'>Ready to start monitoring</font>");
      break;

    case BenchmarkStateTracker::State::WAITING:
      {
        // Show elapsed monitoring time and waiting message
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                         now - monitoringStartTime)
                         .count();
        stateLabel->setText(QString("<font color='#FFFFFF'>Benchmark status: </font>"
                                   "<font color='#FFD700'>Waiting for benchmark to start... (%1s)</font>")
                           .arg(elapsed));
        break;
      }

    case BenchmarkStateTracker::State::RUNNING:
      {
        // Calculate progress based on actual benchmark duration
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - benchmarkStartTime)
                         .count() /
                       1000.0;
        double targetDuration = BenchmarkConstants::TARGET_BENCHMARK_DURATION;
        int progressPercent =
          static_cast<int>((elapsed / targetDuration) * 100.0);
        progressPercent = std::min(progressPercent, 100);  // Cap at 100%

        stateLabel->setText(QString("<font color='#FFFFFF'>Benchmark status: </font>"
           "<font color='%1'>Running %2%</font>"
                                   "<font color='#dddddd'> (%3s / %4s)</font>")
         .arg("#44FF44")
                           .arg(progressPercent)
                           .arg(static_cast<int>(elapsed))
                           .arg(static_cast<int>(targetDuration)));
        break;
      }

    case BenchmarkStateTracker::State::COOLDOWN:
      stateLabel->setText("<font color='#FFFFFF'>Benchmark status: </font>"
                         "<font color='#FF9900'>Completed - Finalizing data...</font>");
      break;
  }
}
