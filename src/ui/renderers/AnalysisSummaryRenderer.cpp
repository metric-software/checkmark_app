#include "AnalysisSummaryRenderer.h"

#include <iostream>

#include <QDate>
#include <QLabel>
#include <QVBoxLayout>

namespace DiagnosticRenderers {

// Color codes for analysis results
static const QString COLOR_SUCCESS = "#44FF44";
static const QString COLOR_GOOD = "#88FF88";
static const QString COLOR_NEUTRAL = "#DDDDDD";
static const QString COLOR_WARNING = "#FFAA00";
static const QString COLOR_CRITICAL = "#FF6666";
static const QString COLOR_INFO = "#44AAFF";
static const QString COLOR_MUTED = "#888888";

// Driver age thresholds (months)
static constexpr int GPU_DRIVER_CRITICAL_AGE = 6;
static constexpr int GPU_DRIVER_OLD_AGE = 3;
static constexpr int DRIVER_CRITICAL_AGE = 24;
static constexpr int DRIVER_OLD_AGE = 12;

// Performance thresholds
static constexpr int CPU_EXCELLENT_THRESHOLD = 500;
static constexpr int CPU_GOOD_THRESHOLD = 1000;
static constexpr int CPU_AVERAGE_THRESHOLD = 2000;

QWidget* AnalysisSummaryRenderer::createAnalysisSummaryWidget() {
  auto& dataStore = DiagnosticDataStore::getInstance();
  const auto& cpuData = dataStore.getCPUData();
  const auto& memoryData = dataStore.getMemoryData();
  const auto& gpuData = dataStore.getGPUData();
  const auto& driveData = dataStore.getDriveData();
  const auto& bgData = dataStore.getBackgroundProcessData();
  const auto& networkData = dataStore.getNetworkData();
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();

  // Create widget for summary
  QWidget* summaryWidget = new QWidget();
  QVBoxLayout* summaryLayout = new QVBoxLayout(summaryWidget);
  summaryLayout->setContentsMargins(0, 0, 0, 0);
  summaryLayout->setSpacing(10);

  // Check if we have data to analyze
  if (cpuData.name == "no_data" && memoryData.bandwidth <= 0 &&
      gpuData.averageFPS <= 0) {
    QLabel* placeholderLabel =
      new QLabel("Run diagnostics to see system analysis results here.");
    placeholderLabel->setWordWrap(true);
    placeholderLabel->setStyleSheet("color: #888888; font-style: italic;");
    summaryLayout->addWidget(placeholderLabel);
    return summaryWidget;
  }

  // Lists for categorized findings
  QStringList criticalIssues;
  QStringList issues;
  QStringList recommendations;
  QStringList performanceSummary;

  // Run analysis for each component
  analyzeCPU(cpuData, constantInfo, criticalIssues, issues, recommendations,
             performanceSummary);
  analyzeMemory(memoryData, criticalIssues, issues, recommendations,
                performanceSummary);
  analyzePageFile(memoryData, driveData, criticalIssues, issues,
                  performanceSummary);
  analyzeDriveSpace(constantInfo, criticalIssues, issues, performanceSummary);
  analyzeDrivers(constantInfo, issues, recommendations, performanceSummary);
  analyzeGPU(gpuData, performanceSummary);
  analyzeBackgroundProcesses(bgData, issues, recommendations,
                             performanceSummary);
  analyzeNetwork(networkData, issues, recommendations, performanceSummary);

  // Build the final analysis content
  QLabel* resultsLabel = new QLabel();
  resultsLabel->setTextFormat(Qt::RichText);
  resultsLabel->setWordWrap(true);

  QString analysisContent;

  // Remove the System Analysis header - it's redundant with the widget title

  // Add performance summary
  if (!performanceSummary.isEmpty()) {
    analysisContent += "<h4>Performance Summary:</h4>";
    for (const QString& item : performanceSummary) {
      analysisContent += item + "<br>";
    }
    analysisContent += "<br>";
  }

  // Add critical issues
  if (!criticalIssues.isEmpty()) {
    analysisContent += "<h4>Critical Issues:</h4>";
    for (const QString& issue : criticalIssues) {
      analysisContent += issue + "<br>";
    }
    analysisContent += "<br>";
  }

  // Add issues
  if (!issues.isEmpty()) {
    analysisContent += "<h4>Issues:</h4>";
    for (const QString& issue : issues) {
      analysisContent += issue + "<br>";
    }
    analysisContent += "<br>";
  }

  // Add recommendations
  if (!recommendations.isEmpty()) {
    analysisContent += "<h4>Recommendations:</h4>";
    for (const QString& rec : recommendations) {
      analysisContent += rec + "<br>";
    }
  }

  // If no issues found, show positive message
  if (criticalIssues.isEmpty() && issues.isEmpty() &&
      recommendations.isEmpty()) {
    analysisContent = "<p style='color: #44FF44;'>✓ No issues detected. Your "
                      "system is performing well.</p>";
  }

  // Add disclaimer at the bottom
  analysisContent +=
    "<br><p style='color: #888888; font-size: 90%; margin-top: 15px;'>Note: "
    "These results are provided as guidance only and have not been verified. "
    "Please double-check them manually and follow instructions from your "
    "device manufacturers.</p>";

  resultsLabel->setText(analysisContent);
  summaryLayout->addWidget(resultsLabel);

  return summaryWidget;
}

void AnalysisSummaryRenderer::analyzeCPU(
  const DiagnosticDataStore::CPUData& cpuData,
  const SystemMetrics::ConstantSystemInfo& constantInfo,
  QStringList& criticalIssues, QStringList& issues,
  QStringList& recommendations, QStringList& performanceSummary) {
  // CPU throttling
  if (cpuData.throttlingDetected) {
    criticalIssues.append(
      QString("<span style='color: #FF6666;'>❌ CPU throttling detected: "
              "Performance drops by %1% under sustained load</span>")
        .arg(cpuData.clockDropPercent, 0, 'f', 1));
  } else {
    performanceSummary.append(
      "<span style='color: #44FF44;'>✓ No CPU throttling detected under "
      "sustained load.</span>");
  }

  // CPU boost behavior - check if maxBoostDelta is valid and too small
  if (cpuData.maxBoostDelta >= 0 && cpuData.maxBoostDelta <= 100) {
    criticalIssues.append(
      "<span style='color: #FF6666;'>❌ CPU boost is not working properly. "
      "Your CPU is not increasing clock speeds under load.</span>");
  } else if (cpuData.maxBoostDelta > 100) {
    performanceSummary.append(
      QString("<span style='color: #44FF44;'>✓ CPU boost is working properly "
              "(boost delta: %1 MHz).</span>")
        .arg(cpuData.maxBoostDelta));
  }

  // Hyperthreading check - only recommend disabling if 8+ physical cores
  if (constantInfo.hyperThreadingEnabled && constantInfo.physicalCores >= 8) {
    recommendations.append(
      "<span style='color: #44AAFF;'>ℹ️ Disabling Hyper-Threading/SMT in BIOS "
      "may improve gaming performance by ~5% in some games.</span>");
  } else if (constantInfo.hyperThreadingEnabled &&
             constantInfo.physicalCores < 8) {
    performanceSummary.append(
      "<span style='color: #44FF44;'>✓ Hyper-Threading is enabled and "
      "recommended for this CPU core count.</span>");
  } else if (!constantInfo.hyperThreadingEnabled) {
    performanceSummary.append(
      "<span style='color: #88FF88;'>✓ Hyper-Threading is disabled (may "
      "benefit performance in some games).</span>");
  }

  // Physical cores check for known CPU models
  bool coreCountIssueFound = false;
  if (cpuData.name.find("5800X") != std::string::npos &&
      cpuData.physicalCores < 8) {
    criticalIssues.append(
      "<span style='color: #FF6666;'>❌ Some CPU physical cores appear to be "
      "disabled. Expected 8 cores for this CPU model.</span>");
    coreCountIssueFound = true;
  } else if (cpuData.name.find("5900X") != std::string::npos &&
             cpuData.physicalCores < 12) {
    criticalIssues.append(
      "<span style='color: #FF6666;'>❌ Some CPU physical cores appear to be "
      "disabled. Expected 12 cores for this CPU model.</span>");
    coreCountIssueFound = true;
  } else if (cpuData.name.find("5950X") != std::string::npos &&
             cpuData.physicalCores < 16) {
    criticalIssues.append(
      "<span style='color: #FF6666;'>❌ Some CPU physical cores appear to be "
      "disabled. Expected 16 cores for this CPU model.</span>");
    coreCountIssueFound = true;
  } else if (cpuData.name.find("12700K") != std::string::npos &&
             cpuData.physicalCores < 12) {
    criticalIssues.append(
      "<span style='color: #FF6666;'>❌ Some CPU physical cores appear to be "
      "disabled. Expected 12 cores for this CPU model.</span>");
    coreCountIssueFound = true;
  } else if (cpuData.name.find("13700K") != std::string::npos &&
             cpuData.physicalCores < 16) {
    criticalIssues.append(
      "<span style='color: #FF6666;'>❌ Some CPU physical cores appear to be "
      "disabled. Expected 16 cores for this CPU model.</span>");
    coreCountIssueFound = true;
  }

  // Add positive feedback for CPU core count if no issues found
  if (!coreCountIssueFound && cpuData.physicalCores > 0) {
    performanceSummary.append(
      QString("<span style='color: #44FF44;'>✓ All CPU physical cores are "
              "enabled (%1 cores detected).</span>")
        .arg(cpuData.physicalCores));
  }

  // Add basic performance assessment
  // Use fourThreadTime with fallback to eightThreadTime
  double threadTime =
    cpuData.fourThreadTime > 0 ? cpuData.fourThreadTime : -1.0;
  if (threadTime > 0) {
    QString perfMsg;
    if (threadTime < 500)
      perfMsg =
        "<span style='color: #44FF44;'>CPU performance is excellent.</span>";
    else if (threadTime < 1000)
      perfMsg = "<span style='color: #88FF88;'>CPU performance is good.</span>";
    else if (threadTime < 2000)
      perfMsg =
        "<span style='color: #DDDDDD;'>CPU performance is average.</span>";
    else
      perfMsg = "<span style='color: #FF6666;'>CPU performance is below "
                "average.</span>";

    performanceSummary.append(perfMsg);
  }

  // C-State Analysis - Power Management Effectiveness
  if (cpuData.cStates.c1TimePercent >= 0 ||
      cpuData.cStates.c2TimePercent >= 0 ||
      cpuData.cStates.c3TimePercent >= 0) {
    // Check if C-states are enabled and working
    if (!cpuData.cStates.cStatesEnabled) {
      // C-states disabled = GOOD for performance
      if (cpuData.cStates.c2TimePercent == 0 &&
          cpuData.cStates.c3TimePercent == 0) {
        performanceSummary.append(
          "<span style='color: #44FF44;'>✓ CPU C-States (C2/C3) are disabled "
          "for optimal gaming performance.</span>");
      } else {
        performanceSummary.append(
          "<span style='color: #88FF88;'>✓ CPU C-States show minimal usage - "
          "good for performance.</span>");
      }
    } else {
      // C-states are enabled - recommend disabling for better performance
      if (cpuData.cStates.powerEfficiencyScore >= 80) {
        recommendations.append(
          "<span style='color: #44AAFF;'>ℹ️ CPU power management is excellent "
          "for power efficiency. For best gaming performance, consider "
          "disabling C-States in BIOS.</span>");
      } else if (cpuData.cStates.powerEfficiencyScore >= 60) {
        recommendations.append(
          "<span style='color: #44AAFF;'>ℹ️ CPU power management is working "
          "well. For optimal gaming performance, consider disabling C-States "
          "in BIOS.</span>");
      } else if (cpuData.cStates.powerEfficiencyScore >= 40) {
        recommendations.append(
          "<span style='color: #44AAFF;'>ℹ️ CPU C-States are enabled. Consider "
          "disabling them in BIOS for better gaming performance and reduced "
          "latency.</span>");
      } else {
        issues.append("<span style='color: #FFAA00;'>⚠️ CPU C-States are "
                      "enabled but working poorly. Disable C-States in BIOS "
                      "for better gaming performance.</span>");
      }

      // Check for excessive C-state transitions (performance impact)
      double totalTransitions = cpuData.cStates.c1TransitionsPerSec +
                                cpuData.cStates.c2TransitionsPerSec +
                                cpuData.cStates.c3TransitionsPerSec;
      if (totalTransitions > 500) {
        issues.append(
          "<span style='color: #FFAA00;'>⚠️ Very high C-state transition rate "
          "detected. This causes micro-stuttering in latency-sensitive "
          "applications. Disable C-States in BIOS.</span>");
      } else if (totalTransitions > 100) {
        recommendations.append(
          "<span style='color: #44AAFF;'>ℹ️ Moderate C-state transition rate "
          "detected. For best gaming performance, consider disabling C-States "
          "in BIOS.</span>");
      } else if (totalTransitions < 1) {
        performanceSummary.append(
          "<span style='color: #44FF44;'>✓ Very low C-state transition rate - "
          "good for performance consistency.</span>");
      }
    }
  } else {
    recommendations.append(
      "<span style='color: #888888;'>ℹ️ C-state analysis data not available - "
      "requires background monitoring during diagnostics.</span>");
  }
}

void AnalysisSummaryRenderer::analyzeMemory(
  const DiagnosticDataStore::MemoryData& memData, QStringList& criticalIssues,
  QStringList& issues, QStringList& recommendations,
  QStringList& performanceSummary) {
  if (memData.bandwidth > 0) {
    // Memory type analysis
    QString memType = QString::fromStdString(memData.memoryType);

    // Memory performance analysis based on type
    bool memoryPerformanceIssueFound = false;
    if (memData.bandwidth < 15000 &&
        memType.contains("DDR4", Qt::CaseInsensitive)) {
      issues.append("<span style='color: #FFAA00;'>⚠️ Low memory bandwidth for "
                    "DDR4. Check if XMP/DOCP is enabled in BIOS.</span>");
      memoryPerformanceIssueFound = true;
    } else if (memData.bandwidth < 30000 &&
               memType.contains("DDR5", Qt::CaseInsensitive)) {
      issues.append("<span style='color: #FFAA00;'>⚠️ Low memory bandwidth for "
                    "DDR5. Check if XMP/DOCP is enabled in BIOS.</span>");
      memoryPerformanceIssueFound = true;
    }

    if (!memoryPerformanceIssueFound) {
      if (memType.contains("DDR4", Qt::CaseInsensitive) &&
          memData.bandwidth >= 25000) {
        performanceSummary.append("<span style='color: #44FF44;'>✓ DDR4 memory "
                                  "bandwidth is excellent.</span>");
      } else if (memType.contains("DDR5", Qt::CaseInsensitive) &&
                 memData.bandwidth >= 40000) {
        performanceSummary.append("<span style='color: #44FF44;'>✓ DDR5 memory "
                                  "bandwidth is excellent.</span>");
      } else if (memData.bandwidth >= 15000) {
        performanceSummary.append(
          "<span style='color: #88FF88;'>✓ Memory bandwidth is adequate for "
          "current memory type.</span>");
      }
    }

    // XMP status
    if (!memData.xmpEnabled) {
      issues.append(
        "<span style='color: #FFAA00;'>⚠️ XMP/DOCP is not enabled. Enabling it "
        "in BIOS can improve memory performance.</span>");
    } else {
      performanceSummary.append(
        "<span style='color: #44FF44;'>✓ XMP/DOCP is enabled for optimal "
        "memory performance.</span>");
    }

    // Check for mixed RAM kits
    if (memData.modules.size() >= 2) {
      bool mixedKits = false;
      bool differentSpeeds = false;

      // Check for consistency in manufacturer or part numbers
      std::string firstManufacturer = memData.modules[0].manufacturer;
      std::string firstPartNumber = memData.modules[0].partNumber;
      int firstSpeed = memData.modules[0].speedMHz;

      for (size_t i = 1; i < memData.modules.size(); i++) {
        if (memData.modules[i].manufacturer != firstManufacturer ||
            memData.modules[i].partNumber != firstPartNumber) {
          mixedKits = true;
        }

        if (memData.modules[i].speedMHz != firstSpeed) {
          differentSpeeds = true;
        }
      }

      if (mixedKits) {
        issues.append(
          "<span style='color: #FFAA00;'>⚠️ Mixed RAM kits detected. This can "
          "cause stability issues and reduced performance.</span>");
      } else {
        performanceSummary.append("<span style='color: #44FF44;'>✓ All memory "
                                  "modules are from matching kits.</span>");
      }

      if (differentSpeeds) {
        issues.append("<span style='color: #FFAA00;'>⚠️ RAM modules are running "
                      "at different speeds. All modules will be limited to the "
                      "slowest speed.</span>");
      } else {
        performanceSummary.append(
          "<span style='color: #44FF44;'>✓ All memory modules are running at "
          "the same speed.</span>");
      }

      // Check for single channel configuration
      if (memData.channelStatus.find("Single") != std::string::npos) {
        issues.append(
          "<span style='color: #FFAA00;'>⚠️ RAM is running in Single Channel "
          "mode. This can significantly impact performance.</span>");
      } else if (memData.channelStatus.find("Dual") != std::string::npos) {
        performanceSummary.append(
          "<span style='color: #44FF44;'>✓ Memory is running in Dual Channel "
          "mode for optimal performance.</span>");
      } else if (memData.channelStatus.find("Quad") != std::string::npos) {
        performanceSummary.append(
          "<span style='color: #44FF44;'>✓ Memory is running in Quad Channel "
          "mode for maximum performance.</span>");
      }
    } else if (memData.modules.size() == 1) {
      recommendations.append(
        "<span style='color: #44AAFF;'>ℹ️ Single memory module detected. Adding "
        "a second matching module would enable dual channel for better "
        "performance.</span>");
    }

    // Add performance summary for memory
    QString memPerfMsg;
    if (memData.bandwidth > 40000)
      memPerfMsg =
        "<span style='color: #44FF44;'>Memory bandwidth is excellent.</span>";
    else if (memData.bandwidth > 25000)
      memPerfMsg =
        "<span style='color: #88FF88;'>Memory bandwidth is good.</span>";
    else if (memData.bandwidth > 15000)
      memPerfMsg =
        "<span style='color: #DDDDDD;'>Memory bandwidth is average.</span>";
    else
      memPerfMsg = "<span style='color: #FF6666;'>Memory bandwidth is below "
                   "average.</span>";

    performanceSummary.append(memPerfMsg);
  }
}

void AnalysisSummaryRenderer::analyzePageFile(
  const DiagnosticDataStore::MemoryData& memData,
  const DiagnosticDataStore::DriveData& driveData, QStringList& criticalIssues,
  QStringList& issues, QStringList& performanceSummary) {
  if (memData.pageFile.exists) {
    if (memData.pageFile.totalSizeMB < 1024) {
      criticalIssues.append(
        QString("<span style='color: %1;'>❌ Page file size is too small. "
                "Recommended minimum is 4GB.</span>")
          .arg(COLOR_CRITICAL));
    } else {
      performanceSummary.append(
        QString("<span style='color: %1;'>✓ Page file size is adequate (%2 "
                "MB).</span>")
          .arg(COLOR_SUCCESS)
          .arg(static_cast<int>(memData.pageFile.totalSizeMB)));
    }

    // Only recommend moving page file if there are multiple drives
    if (driveData.drives.size() > 1) {
      double fastestSpeed = 0.0;
      std::string fastestDrive;

      for (const auto& drive : driveData.drives) {
        if (drive.seqRead > fastestSpeed) {
          fastestSpeed = drive.seqRead;
          fastestDrive = drive.drivePath;
        }
      }

      if (!fastestDrive.empty() &&
          memData.pageFile.primaryDrive != fastestDrive) {
        char pageFileDriveLetter = 0;
        if (!memData.pageFile.primaryDrive.empty()) {
          pageFileDriveLetter = std::toupper(memData.pageFile.primaryDrive[0]);
        }

        char fastestDriveLetter = 0;
        if (!fastestDrive.empty()) {
          fastestDriveLetter = std::toupper(fastestDrive[0]);
        }

        if (pageFileDriveLetter != 0 && fastestDriveLetter != 0 &&
            pageFileDriveLetter != fastestDriveLetter) {
          issues.append(
            QString("<span style='color: %1;'>⚠️ Page file is not on the "
                    "fastest drive. Consider moving it to drive %2.</span>")
              .arg(COLOR_WARNING)
              .arg(QString::fromStdString(fastestDrive)));
        } else {
          performanceSummary.append(
            QString("<span style='color: %1;'>✓ Page file is located on the "
                    "fastest available drive.</span>")
              .arg(COLOR_SUCCESS));
        }
      } else {
        performanceSummary.append(
          QString(
            "<span style='color: %1;'>✓ Page file placement is optimal.</span>")
            .arg(COLOR_SUCCESS));
      }
    } else {
      performanceSummary.append(
        QString("<span style='color: %1;'>✓ Page file is on the system drive "
                "(only drive available).</span>")
          .arg(COLOR_SUCCESS));
    }
  } else {
    criticalIssues.append(
      QString(
        "<span style='color: %1;'>❌ No page file detected. This can cause "
        "stability issues when physical memory is exhausted.</span>")
        .arg(COLOR_CRITICAL));
  }
}

void AnalysisSummaryRenderer::analyzeDriveSpace(
  const SystemMetrics::ConstantSystemInfo& constantInfo,
  QStringList& criticalIssues, QStringList& issues,
  QStringList& performanceSummary) {
  bool driveSpaceIssuesFound = false;

  for (const auto& drive : constantInfo.drives) {
    if (drive.isSystemDrive && drive.freeSpaceGB < 10) {
      criticalIssues.append(
        QString("<span style='color: #FF6666;'>❌ System drive (%1) has "
                "critically low free space (%2 GB).</span>")
          .arg(QString::fromStdString(drive.path))
          .arg(drive.freeSpaceGB, 0, 'f', 1));
      driveSpaceIssuesFound = true;
    } else if (drive.isSystemDrive && drive.freeSpaceGB < 30) {
      issues.append(QString("<span style='color: #FFAA00;'>⚠️ System drive (%1) "
                            "is low on free space (%2 GB).</span>")
                      .arg(QString::fromStdString(drive.path))
                      .arg(drive.freeSpaceGB, 0, 'f', 1));
      driveSpaceIssuesFound = true;
    }
  }

  // Add positive feedback if no issues found
  if (!driveSpaceIssuesFound) {
    for (const auto& drive : constantInfo.drives) {
      if (drive.isSystemDrive) {
        performanceSummary.append(
          QString("<span style='color: #44FF44;'>✓ System drive (%1) has "
                  "adequate free space (%2 GB).</span>")
            .arg(QString::fromStdString(drive.path))
            .arg(drive.freeSpaceGB, 0, 'f', 1));
        break;  // Only show for the system drive
      }
    }
  }
}

void AnalysisSummaryRenderer::analyzeDrivers(
  const SystemMetrics::ConstantSystemInfo& constantInfo, QStringList& issues,
  QStringList& recommendations, QStringList& performanceSummary) {
  QStringList missingDriverInfo;
  bool allDriversUpToDate = true;

  // BIOS age check
  if (!constantInfo.biosDate.empty()) {
    std::string biosDateStr = constantInfo.biosDate;
    QDate biosDate;

    if (biosDateStr.length() >= 10) {
      if (biosDateStr[2] == '/' && biosDateStr[5] == '/') {
        int month = std::stoi(biosDateStr.substr(0, 2));
        int day = std::stoi(biosDateStr.substr(3, 2));
        int year = std::stoi(biosDateStr.substr(6, 4));
        biosDate = QDate(year, month, day);
      } else if (biosDateStr[4] == '/' && biosDateStr[7] == '/') {
        int year = std::stoi(biosDateStr.substr(0, 4));
        int month = std::stoi(biosDateStr.substr(5, 2));
        int day = std::stoi(biosDateStr.substr(8, 2));
        biosDate = QDate(year, month, day);
      }
    }

    if (biosDate.isValid()) {
      QDate currentDate = QDate::currentDate();
      int monthsAgo = biosDate.daysTo(currentDate) / 30;

      if (monthsAgo > DRIVER_CRITICAL_AGE) {
        issues.append(
          QString("<span style='color: %1;'>⚠️ BIOS is over 2 years old (%2). "
                  "Consider updating to the latest version.</span>")
            .arg(COLOR_WARNING)
            .arg(QString::fromStdString(biosDateStr)));
        allDriversUpToDate = false;
      } else if (monthsAgo > DRIVER_OLD_AGE) {
        recommendations.append(
          QString("<span style='color: %1;'>ℹ️ BIOS is over 1 year old (%2). "
                  "Updates may be available.</span>")
            .arg(COLOR_INFO)
            .arg(QString::fromStdString(biosDateStr)));
        allDriversUpToDate = false;
      } else {
        performanceSummary.append(
          QString(
            "<span style='color: %1;'>✓ BIOS is recently updated (%2).</span>")
            .arg(COLOR_SUCCESS)
            .arg(QString::fromStdString(biosDateStr)));
      }
    }
  } else {
    missingDriverInfo.append(
      QString(
        "<span style='color: %1;'>BIOS date information unavailable</span>")
        .arg(COLOR_MUTED));
  }

  // Check chipset driver
  if (!constantInfo.chipsetDrivers.empty() &&
      constantInfo.chipsetDrivers[0].isDateValid) {
    std::string driverDateStr = constantInfo.chipsetDrivers[0].driverDate;
    QDate driverDate = parseDriverDate(driverDateStr);

    if (driverDate.isValid()) {
      QDate currentDate = QDate::currentDate();
      int monthsAgo = driverDate.daysTo(currentDate) / 30;

      if (monthsAgo > DRIVER_CRITICAL_AGE) {
        issues.append(
          QString("<span style='color: %1;'>⚠️ Chipset driver is over 2 years "
                  "old (%2). Consider updating to the latest version.</span>")
            .arg(COLOR_WARNING)
            .arg(QString::fromStdString(driverDateStr)));
        allDriversUpToDate = false;
      } else if (monthsAgo > DRIVER_OLD_AGE) {
        recommendations.append(
          QString("<span style='color: %1;'>ℹ️ Chipset driver is over 1 year "
                  "old (%2). Check manufacturer for updates.</span>")
            .arg(COLOR_INFO)
            .arg(QString::fromStdString(driverDateStr)));
        allDriversUpToDate = false;
      } else {
        performanceSummary.append(
          QString("<span style='color: %1;'>✓ Chipset driver is recently "
                  "updated (%2).</span>")
            .arg(COLOR_SUCCESS)
            .arg(QString::fromStdString(driverDateStr)));
      }
    }
  } else if (constantInfo.chipsetDriverVersion.empty() ||
             constantInfo.chipsetDriverVersion == "Unknown") {
    issues.append(
      QString(
        "<span style='color: %1;'>⚠️ Chipset driver information unavailable. "
        "Ensure appropriate chipset drivers are installed.</span>")
        .arg(COLOR_WARNING));
    allDriversUpToDate = false;
  } else if (constantInfo.chipsetDrivers.empty() ||
             !constantInfo.chipsetDrivers[0].isDateValid) {
    missingDriverInfo.append(QString("<span style='color: %1;'>Chipset driver "
                                     "date information unavailable</span>")
                               .arg(COLOR_MUTED));
  }

  // Check audio drivers
  bool hasValidAudioDriverDate = false;
  for (const auto& driver : constantInfo.audioDrivers) {
    if (driver.isDateValid) {
      hasValidAudioDriverDate = true;
      QDate driverDate = parseDriverDate(driver.driverDate);

      if (driverDate.isValid()) {
        QDate currentDate = QDate::currentDate();
        int monthsAgo = driverDate.daysTo(currentDate) / 30;

        if (monthsAgo > DRIVER_CRITICAL_AGE) {
          recommendations.append(
            QString("<span style='color: %1;'>ℹ️ Audio driver '%2' is over 2 "
                    "years old (%3). Consider checking for updates.</span>")
              .arg(COLOR_INFO)
              .arg(QString::fromStdString(driver.deviceName))
              .arg(QString::fromStdString(driver.driverDate)));
          allDriversUpToDate = false;
          break;
        } else if (monthsAgo < DRIVER_OLD_AGE &&
                   constantInfo.audioDrivers.size() == 1) {
          performanceSummary.append(
            QString("<span style='color: %1;'>✓ Audio driver is recently "
                    "updated (%2).</span>")
              .arg(COLOR_SUCCESS)
              .arg(QString::fromStdString(driver.driverDate)));
        }
      }
    }
  }

  if (!hasValidAudioDriverDate && !constantInfo.audioDrivers.empty()) {
    missingDriverInfo.append(QString("<span style='color: %1;'>Audio driver "
                                     "date information unavailable</span>")
                               .arg(COLOR_MUTED));
  } else if (constantInfo.audioDrivers.empty()) {
    missingDriverInfo.append(
      QString("<span style='color: %1;'>No audio drivers detected</span>")
        .arg(COLOR_MUTED));
  }

  // Check network drivers
  bool hasValidNetworkDriverDate = false;
  for (const auto& driver : constantInfo.networkDrivers) {
    if (driver.isDateValid) {
      hasValidNetworkDriverDate = true;
      QDate driverDate = parseDriverDate(driver.driverDate);

      if (driverDate.isValid()) {
        QDate currentDate = QDate::currentDate();
        int monthsAgo = driverDate.daysTo(currentDate) / 30;

        if (monthsAgo > DRIVER_CRITICAL_AGE) {
          recommendations.append(
            QString("<span style='color: %1;'>ℹ️ Network driver '%2' is over 2 "
                    "years old (%3). Consider checking for updates.</span>")
              .arg(COLOR_INFO)
              .arg(QString::fromStdString(driver.deviceName))
              .arg(QString::fromStdString(driver.driverDate)));
          allDriversUpToDate = false;
        } else if (monthsAgo < DRIVER_OLD_AGE &&
                   constantInfo.networkDrivers.size() == 1) {
          performanceSummary.append(
            QString("<span style='color: %1;'>✓ Network driver is recently "
                    "updated (%2).</span>")
              .arg(COLOR_SUCCESS)
              .arg(QString::fromStdString(driver.driverDate)));
        }
      }
    }
  }

  if (!hasValidNetworkDriverDate && !constantInfo.networkDrivers.empty()) {
    missingDriverInfo.append(QString("<span style='color: %1;'>Network driver "
                                     "date information unavailable</span>")
                               .arg(COLOR_MUTED));
  } else if (constantInfo.networkDrivers.empty()) {
    missingDriverInfo.append(
      QString("<span style='color: %1;'>No network drivers detected</span>")
        .arg(COLOR_MUTED));
  }

  // Check GPU drivers - stricter time window than other drivers
  bool hasValidGpuDriverDate = false;
  for (const auto& gpu : constantInfo.gpuDevices) {
    if (gpu.driverDate != "Unknown") {
      hasValidGpuDriverDate = true;
      QDate driverDate = parseDriverDate(gpu.driverDate);

      if (driverDate.isValid()) {
        QDate currentDate = QDate::currentDate();
        int monthsAgo = driverDate.daysTo(currentDate) / 30;

        if (monthsAgo > GPU_DRIVER_CRITICAL_AGE) {
          issues.append(
            QString(
              "<span style='color: %1;'>⚠️ GPU driver for %2 is over 6 months "
              "old (%3). Consider updating to the latest version.</span>")
              .arg(COLOR_WARNING)
              .arg(QString::fromStdString(gpu.name))
              .arg(QString::fromStdString(gpu.driverDate)));
          allDriversUpToDate = false;
        } else if (monthsAgo > GPU_DRIVER_OLD_AGE) {
          recommendations.append(
            QString("<span style='color: %1;'>ℹ️ GPU driver for %2 is over 3 "
                    "months old (%3). Check for updates.</span>")
              .arg(COLOR_INFO)
              .arg(QString::fromStdString(gpu.name))
              .arg(QString::fromStdString(gpu.driverDate)));
          allDriversUpToDate = false;
        } else {
          performanceSummary.append(
            QString("<span style='color: %1;'>✓ GPU driver is recently updated "
                    "(%2).</span>")
              .arg(COLOR_SUCCESS)
              .arg(QString::fromStdString(gpu.driverDate)));
        }
      }
    }
  }

  if (!hasValidGpuDriverDate && !constantInfo.gpuDevices.empty()) {
    missingDriverInfo.append(QString("<span style='color: %1;'>GPU driver date "
                                     "information unavailable</span>")
                               .arg(COLOR_MUTED));
  }

  // Add general driver status to performance summary if all are up-to-date
  if (allDriversUpToDate && !constantInfo.chipsetDrivers.empty() &&
      !constantInfo.audioDrivers.empty() &&
      !constantInfo.networkDrivers.empty() &&
      !constantInfo.gpuDevices.empty() && missingDriverInfo.isEmpty()) {
    performanceSummary.append(QString("<span style='color: %1;'>✓ All system "
                                      "drivers are recently updated.</span>")
                                .arg(COLOR_SUCCESS));
  }

  // Add missing driver info section to recommendations if needed
  if (!missingDriverInfo.isEmpty()) {
    recommendations.append(
      QString(
        "<span style='color: %1;'>Not verified (missing information):</span>")
        .arg(COLOR_MUTED));
    for (const QString& info : missingDriverInfo) {
      recommendations.append(QString("  %1").arg(info));
    }
  }
}

QDate AnalysisSummaryRenderer::parseDriverDate(const std::string& dateStr) {
  QDate result;

  // Handle dates with no leading zeros (e.g., "6-15-2020")
  if (dateStr.length() >= 8) {
    size_t firstSep = dateStr.find_first_of("-/");
    size_t lastSep = dateStr.find_last_of("-/");

    if (firstSep != std::string::npos && lastSep != std::string::npos &&
        firstSep != lastSep) {
      try {
        int month = std::stoi(dateStr.substr(0, firstSep));
        int day =
          std::stoi(dateStr.substr(firstSep + 1, lastSep - firstSep - 1));
        int year = std::stoi(dateStr.substr(lastSep + 1));

        // Handle 2-digit years
        if (year < 100) {
          year = (year < 50) ? year + 2000 : year + 1900;
        }

        result = QDate(year, month, day);
      } catch (...) {
        // Parse error
      }
    }
    // Try YYYY-MM-DD or YYYY/MM/DD
    else if ((dateStr.length() >= 10) &&
             (dateStr[4] == '-' || dateStr[4] == '/') &&
             (dateStr[7] == '-' || dateStr[7] == '/')) {
      try {
        int year = std::stoi(dateStr.substr(0, 4));
        int month = std::stoi(dateStr.substr(5, 2));
        int day = std::stoi(dateStr.substr(8, 2));
        result = QDate(year, month, day);
      } catch (...) {
        // Parse error
      }
    }
  }

  return result;
}

void AnalysisSummaryRenderer::analyzeGPU(
  const DiagnosticDataStore::GPUData& gpuData,
  QStringList& performanceSummary) {
  if (gpuData.averageFPS > 0) {
    QString gpuPerfMsg;
    if (gpuData.averageFPS > 200)
      gpuPerfMsg =
        "<span style='color: #44FF44;'>GPU performance is excellent.</span>";
    else if (gpuData.averageFPS > 120)
      gpuPerfMsg =
        "<span style='color: #88FF88;'>GPU performance is good.</span>";
    else if (gpuData.averageFPS > 60)
      gpuPerfMsg =
        "<span style='color: #DDDDDD;'>GPU performance is average.</span>";
    else
      gpuPerfMsg = "<span style='color: #FF6666;'>GPU performance is below "
                   "average.</span>";

    performanceSummary.append(gpuPerfMsg);
  }
}

void AnalysisSummaryRenderer::analyzeBackgroundProcesses(
  const DiagnosticDataStore::BackgroundProcessData& bgData, QStringList& issues,
  QStringList& recommendations, QStringList& performanceSummary) {
  bool backgroundIssuesFound = false;

  if (bgData.systemCpuUsage > 0) {
    if (bgData.hasDpcLatencyIssues) {
      issues.append("<span style='color: #FFAA00;'>⚠️ High DPC/interrupt "
                    "latency detected. This may cause stuttering in games. Try "
                    "closing resource-intensive programs.</span>");
      backgroundIssuesFound = true;
    } else {
      performanceSummary.append("<span style='color: #44FF44;'>✓ DPC/interrupt "
                                "latency is within normal ranges.</span>");
    }

    if (bgData.systemCpuUsage > 20) {
      issues.append(
        QString("<span style='color: #FFAA00;'>⚠️ High background CPU usage "
                "detected (%1%). Consider optimizing startup programs.</span>")
          .arg(bgData.systemCpuUsage, 0, 'f', 1));
      backgroundIssuesFound = true;
    } else if (bgData.systemCpuUsage > 10) {
      recommendations.append(
        QString("<span style='color: #44AAFF;'>ℹ️ Moderate background CPU usage "
                "(%1%). Consider reviewing non-essential applications.</span>")
          .arg(bgData.systemCpuUsage, 0, 'f', 1));
    } else {
      performanceSummary.append(
        QString("<span style='color: #44FF44;'>✓ Background CPU usage is low "
                "(%1%) - optimal for performance.</span>")
          .arg(bgData.systemCpuUsage, 0, 'f', 1));
    }

    // Check GPU usage if available
    if (bgData.systemGpuUsage > 0) {
      if (bgData.systemGpuUsage > 15) {
        issues.append(
          QString("<span style='color: #FFAA00;'>⚠️ High background GPU usage "
                  "detected (%1%). Check for mining software or unnecessary "
                  "GPU-accelerated applications.</span>")
            .arg(bgData.systemGpuUsage, 0, 'f', 1));
        backgroundIssuesFound = true;
      } else {
        performanceSummary.append(
          QString("<span style='color: #44FF44;'>✓ Background GPU usage is "
                  "normal (%1%).</span>")
            .arg(bgData.systemGpuUsage, 0, 'f', 1));
      }
    }
  }

  // Analyze "Other Memory" for potential driver/kernel memory leaks
  if (bgData.otherMemoryKB > 0) {
    double otherMemoryGB =
      bgData.otherMemoryKB / (1024.0 * 1024.0);  // Convert KB to GB

    if (otherMemoryGB > 10.0) {
      // Critical threshold - recommend driver updates and Windows reinstall
      issues.append(QString("<span style='color: #FF6666;'>❌ Excessive 'Other "
                            "Memory' usage detected (%1 GB). This strongly "
                            "indicates a driver or kernel memory leak.</span>")
                      .arg(otherMemoryGB, 0, 'f', 1));
      recommendations.append(
        "<span style='color: #44AAFF;'>ℹ️ Update all drivers (especially GPU, "
        "chipset, and network drivers) and consider reinstalling Windows to "
        "resolve potential memory leaks.</span>");
      backgroundIssuesFound = true;
    } else if (otherMemoryGB > 5.0) {
      // Warning threshold - potential memory leak
      issues.append(QString("<span style='color: #FFAA00;'>⚠️ High 'Other "
                            "Memory' usage detected (%1 GB). This may indicate "
                            "a driver or kernel-related memory leak.</span>")
                      .arg(otherMemoryGB, 0, 'f', 1));
      recommendations.append(
        "<span style='color: #44AAFF;'>ℹ️ Consider updating system drivers, "
        "especially GPU and chipset drivers, to resolve potential memory "
        "leaks.</span>");
      backgroundIssuesFound = true;
    } else if (otherMemoryGB > 0) {
      // Normal range - show positive feedback
      performanceSummary.append(
        QString("<span style='color: #44FF44;'>✓ 'Other Memory' usage is "
                "normal (%1 GB) - no driver memory leaks detected.</span>")
          .arg(otherMemoryGB, 0, 'f', 1));
    }
  }

  // If no major issues were found, provide summary
  if (!backgroundIssuesFound && bgData.systemCpuUsage <= 10) {
    performanceSummary.append(
      "<span style='color: #44FF44;'>✓ Background system activity is optimal "
      "for gaming and performance applications.</span>");
  }
}

void AnalysisSummaryRenderer::analyzeNetwork(
  const DiagnosticDataStore::NetworkData& networkData, QStringList& issues,
  QStringList& recommendations, QStringList& performanceSummary) {
  if (networkData.averageLatencyMs > 0) {
    bool networkIssuesFound = false;

    if (networkData.hasBufferbloat) {
      issues.append(
        "<span style='color: #FFAA00;'>⚠️ Network bufferbloat detected. This "
        "can cause latency spikes during gaming.</span>");
      networkIssuesFound = true;
    } else {
      performanceSummary.append("<span style='color: #44FF44;'>✓ No network "
                                "bufferbloat detected.</span>");
    }

    // Calculate average latency to DNS servers (Google and Cloudflare)
    double dnsLatency = 0.0;
    int dnsCount = 0;

    for (const auto& server : networkData.serverResults) {
      // Only use Google DNS and Cloudflare DNS for latency evaluation
      if (server.ipAddress == "8.8.8.8" || server.ipAddress == "1.1.1.1") {
        dnsLatency += server.avgLatencyMs;
        dnsCount++;
      }
    }

    // Use DNS servers' average if available, otherwise fall back to overall
    // average
    double latencyToCheck = networkData.averageLatencyMs;
    if (dnsCount > 0) {
      latencyToCheck = dnsLatency / dnsCount;
    }

    if (latencyToCheck > 100) {
      issues.append(
        QString("<span style='color: #FFAA00;'>⚠️ High network latency (%1 ms). "
                "This may impact online gaming performance.</span>")
          .arg(latencyToCheck, 0, 'f', 1));
      networkIssuesFound = true;
    } else if (latencyToCheck > 50) {
      performanceSummary.append(
        QString("<span style='color: #88FF88;'>✓ Network latency is acceptable "
                "(%1 ms) for most applications.</span>")
          .arg(latencyToCheck, 0, 'f', 1));
    } else if (latencyToCheck > 20) {
      performanceSummary.append(
        QString("<span style='color: #44FF44;'>✓ Network latency is good (%1 "
                "ms) for gaming.</span>")
          .arg(latencyToCheck, 0, 'f', 1));
    } else {
      performanceSummary.append(
        QString("<span style='color: #44FF44;'>✓ Network latency is excellent "
                "(%1 ms) for competitive gaming.</span>")
          .arg(latencyToCheck, 0, 'f', 1));
    }

    if (networkData.onWifi && networkData.averageJitterMs > 5) {
      recommendations.append(
        "<span style='color: #44AAFF;'>ℹ️ Using WiFi with noticeable jitter. "
        "Consider switching to a wired connection for gaming.</span>");
    } else if (networkData.onWifi && networkData.averageJitterMs <= 5) {
      performanceSummary.append(
        "<span style='color: #88FF88;'>✓ WiFi connection has low jitter - good "
        "for wireless gaming.</span>");
    } else if (!networkData.onWifi) {
      performanceSummary.append(
        "<span style='color: #44FF44;'>✓ Using wired connection - optimal for "
        "gaming and low latency applications.</span>");
    }

    // Check packet loss
    if (networkData.averagePacketLoss > 1.0) {
      issues.append(
        QString("<span style='color: #FFAA00;'>⚠️ Packet loss detected (%1%). "
                "This can cause connection issues.</span>")
          .arg(networkData.averagePacketLoss, 0, 'f', 1));
      networkIssuesFound = true;
    } else if (networkData.averagePacketLoss > 0.1) {
      recommendations.append(
        QString("<span style='color: #DDDDDD;'>Minor packet loss detected "
                "(%1%) - acceptable for most uses.</span>")
          .arg(networkData.averagePacketLoss, 0, 'f', 1));
    } else {
      performanceSummary.append("<span style='color: #44FF44;'>✓ No "
                                "significant packet loss detected.</span>");
    }

    // Overall network summary
    if (!networkIssuesFound && latencyToCheck <= 50 &&
        networkData.averagePacketLoss <= 0.1) {
      performanceSummary.append(
        "<span style='color: #44FF44;'>✓ Network connection is optimal for "
        "gaming and streaming applications.</span>");
    }
  } else {
    recommendations.append(
      "<span style='color: #888888;'>ℹ️ Network analysis data not available - "
      "network tests may have been skipped.</span>");
  }
}

}  // namespace DiagnosticRenderers
