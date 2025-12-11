#include "BackgroundProcessRenderer.h"

#include <iostream>

#include <QRegularExpression>

#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"

#include "logging/Logger.h"

namespace DiagnosticRenderers {

QString BackgroundProcessRenderer::renderBackgroundProcessResults(
  const QString& result) {
  LOG_INFO << "BackgroundProcessRenderer: Starting to process background results";

  try {
    // Safely get data from DiagnosticDataStore with error handling
    DiagnosticDataStore::BackgroundProcessData bgData;
    try {
      const auto& dataStore = DiagnosticDataStore::getInstance();
      bgData = dataStore.getBackgroundProcessData();
      LOG_INFO << "BackgroundProcessRenderer: Successfully retrieved "
                "background data from store";
    } catch (const std::exception& e) {
      LOG_ERROR << "BackgroundProcessRenderer: Error accessing DiagnosticDataStore: "
                << e.what();
      // Continue with default-constructed bgData (empty)
    }

    // Process info structure remains the same but defined as a local class
    struct ProcessInfo {
      QString name;
      double cpu = 0.0;
      double gpu = 0.0;
      double gpuMemoryMB = 0.0;
      double gpuComputePercent = 0.0;
      double gpuEncoderPercent = 0.0;
      double memory = 0.0;
      int instances = 1;
      bool isHighUsage = false;
      bool isDpcSource = false;
      bool isInterruptSource = false;
      double peakCpu = 0.0;
      int cpuSpikeCount = 0;
    };

    // System resource info - use safe initialization
    double cpuUsage = bgData.systemCpuUsage > 0 ? bgData.systemCpuUsage : 0.0;
    double dpcTime = bgData.systemDpcTime > 0 ? bgData.systemDpcTime : 0.0;
    double intTime =
      bgData.systemInterruptTime > 0 ? bgData.systemInterruptTime : 0.0;

    // Fix the order of GPU usage retrieval to prioritize DiagnosticDataStore

    // Default values if not available
    double gpuUsage = -1.0;
    double diskIO = -1.0;

    // First parse the text for values not in DiagnosticDataStore
    parseSystemResourceInfo(result, cpuUsage, gpuUsage, dpcTime, intTime,
                            diskIO);

    // Then override with DiagnosticDataStore values if available
    if (bgData.systemGpuUsage > 0.0) {
      gpuUsage = bgData.systemGpuUsage;
    }

    // Sanity check for invalid GPU values (NVIDIA uses 0xFFFFFFFF for N/A)
    constexpr double NVIDIA_INVALID_VALUE = 4294967295.0;  // 2^32 - 1
    if (gpuUsage == NVIDIA_INVALID_VALUE || gpuUsage > 100.0) {
      gpuUsage = -1.0;  // Mark as invalid/not available
    }

    // Import process data from DiagnosticDataStore
    QMap<QString, ProcessInfo> processes;
    QStringList recommendations;

    // Process top CPU processes
    for (const auto& proc : bgData.topCpuProcesses) {
      QString name = QString::fromStdString(proc.name);
      if (!processes.contains(name)) {
        ProcessInfo info;
        info.name = name;
        info.cpu = proc.cpuPercent;
        info.peakCpu = proc.peakCpuPercent;
        info.memory = proc.memoryUsageKB / 1024.0;  // Convert KB to MB
        info.gpu = proc.gpuPercent;
        info.instances = proc.instanceCount;
        info.isHighUsage =
          (proc.cpuPercent > 5.0 || proc.memoryUsageKB > 500 * 1024);
        processes[name] = info;
      }
    }

    // Process top memory processes
    for (const auto& proc : bgData.topMemoryProcesses) {
      QString name = QString::fromStdString(proc.name);
      if (processes.contains(name)) {
        // Update existing entry
        processes[name].memory =
          proc.memoryUsageKB / 1024.0;  // Convert KB to MB
        if (proc.cpuPercent > processes[name].cpu) {
          processes[name].cpu = proc.cpuPercent;
        }
        if (proc.gpuPercent > processes[name].gpu) {
          processes[name].gpu = proc.gpuPercent;
        }
        processes[name].isHighUsage |= (proc.memoryUsageKB > 500 * 1024);
      } else {
        // Add new entry
        ProcessInfo info;
        info.name = name;
        info.cpu = proc.cpuPercent;
        info.peakCpu = proc.peakCpuPercent;
        info.memory = proc.memoryUsageKB / 1024.0;  // Convert KB to MB
        info.gpu = proc.gpuPercent;
        info.instances = proc.instanceCount;
        info.isHighUsage = (proc.memoryUsageKB > 500 * 1024);
        processes[name] = info;
      }
    }

    // Process top GPU processes
    for (const auto& proc : bgData.topGpuProcesses) {
      QString name = QString::fromStdString(proc.name);
      // Check if process GPU metrics are valid
      if (proc.gpuPercent == NVIDIA_INVALID_VALUE || proc.gpuPercent > 100.0) {
        continue;  // Skip invalid GPU metrics
      }

      if (processes.contains(name)) {
        // Update existing entry with GPU-specific metrics
        processes[name].gpu = proc.gpuPercent;

        // Extract additional GPU metrics if available in the process data
        QRegularExpression gpuCompute(name + ".*GPU Compute: (\\d+\\.?\\d*)%");
        QRegularExpression gpuMemory(name + ".*GPU Memory: (\\d+\\.?\\d*) MB");
        QRegularExpression gpuEncoder(name + ".*GPU Encoder: (\\d+\\.?\\d*)%");

        auto matchCompute = gpuCompute.match(result);
        auto matchMemory = gpuMemory.match(result);
        auto matchEncoder = gpuEncoder.match(result);

        if (matchCompute.hasMatch()) {
          double value = matchCompute.captured(1).toDouble();
          // Sanity check
          if (value > 0 && value <= 100.0) {
            processes[name].gpuComputePercent = value;
          }
        }

        if (matchEncoder.hasMatch()) {
          double value = matchEncoder.captured(1).toDouble();
          // Sanity check
          if (value > 0 && value <= 100.0) {
            processes[name].gpuEncoderPercent = value;
          }
        }

        if (matchMemory.hasMatch()) {
          double memValue = matchMemory.captured(1).toDouble();
          // Sanitize extremely large values (likely errors)
          if (memValue > 0 &&
              memValue < 32768) {  // Cap at 32GB (reasonable GPU memory)
            processes[name].gpuMemoryMB = memValue;
          }
        }
      } else {
        // Add new entry
        ProcessInfo info;
        info.name = name;
        info.cpu = proc.cpuPercent;
        info.memory = proc.memoryUsageKB / 1024.0;  // Convert KB to MB
        info.gpu = proc.gpuPercent;
        info.instances = proc.instanceCount;
        processes[name] = info;
      }
    }

    // Parse additional information from result string
    QStringList lines = result.split("\n");
    bool inRecommendationsSection = false;
    QString currentProcessName;

    for (const QString& line : lines) {
      if (line.contains("Performance Recommendations:")) {
        inRecommendationsSection = true;
        continue;
      }

      // Look for process lines with DPC/interrupt markers
      if (line.contains("[DPC]") || line.contains("[Interrupt]")) {
        for (auto it = processes.begin(); it != processes.end(); ++it) {
          if (line.contains(it.key())) {
            if (line.contains("[DPC]")) {
              it.value().isDpcSource = true;
            }
            if (line.contains("[Interrupt]")) {
              it.value().isInterruptSource = true;
            }
            break;
          }
        }
      }

      // Collect recommendations
      if (inRecommendationsSection && line.trimmed().startsWith("•")) {
        recommendations.append(line.trimmed().mid(1).trimmed());
      }

      // Look for CPU spike information
      QRegularExpression spikeRegex("with (\\d+) spikes");
      auto spikeMatch = spikeRegex.match(line);
      if (spikeMatch.hasMatch() && !currentProcessName.isEmpty() &&
          processes.contains(currentProcessName)) {
        int spikes = spikeMatch.captured(1).toInt();
        processes[currentProcessName].cpuSpikeCount = spikes;
      }

      // Look for process names to keep track of current process
      if (line.trimmed().startsWith("•")) {
        QString processLine = line.trimmed().mid(1).trimmed();
        if (processLine.contains(".exe")) {
          currentProcessName = processLine.section(".exe", 0, 0) + ".exe";
        } else {
          currentProcessName = processLine.section(" (", 0, 0);
        }
      }
    }

    // Create the HTML display content
    QString html = "<h3>System Resource Usage</h3>";

    // Determine colors based on usage levels
    QString cpuColor = cpuUsage > 20 ? "#FF8C00" : "#0078d4";
    QString gpuColor = gpuUsage > 20 ? "#FF8C00" : "#0078d4";
    QString dpcColor = dpcTime > 1.0 ? "#FF8C00" : "#0078d4";
    QString intColor = intTime > 0.5 ? "#FF8C00" : "#0078d4";

    html += "<table style='border: none; width: 100%; margin-bottom: 15px;'>";

    // Update CPU display to show both average and peak
    html +=
      "<tr><td style='width: 50%'>CPU Usage: <span style='color: " + cpuColor +
      "; font-weight: bold;'>" + QString::number(cpuUsage, 'f', 1) +
      "% (avg)</span>";
    if (bgData.peakSystemCpuUsage > 0) {
      QString peakCpuColor =
        bgData.peakSystemCpuUsage > 20 ? "#FF8C00" : "#0078d4";
      html += "<br><span style='color: " + peakCpuColor +
              "; font-size: 0.9em;'>Peak: " +
              QString::number(bgData.peakSystemCpuUsage, 'f', 1) + "%</span>";
    }
    html += "</td>";

    // Update DPC Time display to show both average and peak
    html += "<td>DPC Time: <span style='color: " + dpcColor +
            "; font-weight: bold;'>" + QString::number(dpcTime, 'f', 2) +
            "% (avg)</span>";
    if (bgData.peakSystemDpcTime > 0) {
      QString peakDpcColor =
        bgData.peakSystemDpcTime > 1.0 ? "#FF8C00" : "#0078d4";
      html += "<br><span style='color: " + peakDpcColor +
              "; font-size: 0.9em;'>Peak: " +
              QString::number(bgData.peakSystemDpcTime, 'f', 2) + "%</span>";
    }
    html += "</td></tr>";

    // GPU information display - handle invalid values and DON'T include GPU name
    html +=
      "<tr><td>GPU: <span style='color: " + gpuColor + "; font-weight: bold;'>";
    if (gpuUsage >= 0) {
      html += QString::number(gpuUsage, 'f', 1) + "% (avg)";
    } else {
      html += "N/A";
    }
    html += "</span>";

    // Add GPU peak value
    if (bgData.peakSystemGpuUsage > 0) {
      QString peakGpuColor =
        bgData.peakSystemGpuUsage > 20 ? "#FF8C00" : "#0078d4";
      html += "<br><span style='color: " + peakGpuColor +
              "; font-size: 0.9em;'>Peak: " +
              QString::number(bgData.peakSystemGpuUsage, 'f', 1) + "%</span>";
    }

    // Get GPU info from ConstantSystemInfo
    const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();
    
    // Add GPU memory if available from first GPU device

    if (!constantInfo.gpuDevices.empty() && constantInfo.gpuDevices[0].memoryMB > 0) {
      html += QString(" <span style='color: #0078d4;'>(Memory: %1 MB)</span>")
                .arg(constantInfo.gpuDevices[0].memoryMB, 0, 'f', 0);
    }

    // Add GPU temperature if available from live metrics (not available in ConstantSystemInfo)
    // If you have a live GPU temperature metric, insert it here. Otherwise, skip.
    // Example (pseudo):
    // if (bgData.gpuTemperature > 0 && bgData.gpuTemperature < 120) {
    //   html += QString(" <span style='color: %1;'>Temp: %2°C</span>")
    //             .arg(bgData.gpuTemperature > 80 ? "#FF8C00" : "#0078d4")
    //             .arg(bgData.gpuTemperature, 0, 'f', 0);
    // }
    // Currently, no live GPU temperature available in bgData or constantInfo.

    html += "</td>";

    // Update Interrupt Time display to show both average and peak
    html += "<td>Interrupt Time: <span style='color: " + intColor +
            "; font-weight: bold;'>" + QString::number(intTime, 'f', 2) +
            "% (avg)</span>";
    if (bgData.peakSystemInterruptTime > 0) {
      QString peakIntColor =
        bgData.peakSystemInterruptTime > 0.5 ? "#FF8C00" : "#0078d4";
      html += "<br><span style='color: " + peakIntColor +
              "; font-size: 0.9em;'>Peak: " +
              QString::number(bgData.peakSystemInterruptTime, 'f', 2) +
              "%</span>";
    }
    html += "</td></tr>";

    // Add disk I/O row
    html += "<tr><td colspan='2'>Disk I/O: ";
    if (bgData.systemDiskIO >= 0) {
      QString diskColor = bgData.systemDiskIO > 50.0 ? "#FF8C00" : "#0078d4";
      html += "<span style='color: " + diskColor + "; font-weight: bold;'>" +
              QString::number(bgData.systemDiskIO, 'f', 1) +
              " MB/s (avg)</span>";
      if (bgData.peakSystemDiskIO > 0) {
        QString peakDiskColor =
          bgData.peakSystemDiskIO > 100.0 ? "#FF8C00" : "#0078d4";
        html += " <span style='color: " + peakDiskColor +
                "; font-size: 0.9em;'>Peak: " +
                QString::number(bgData.peakSystemDiskIO, 'f', 1) +
                " MB/s</span>";
      }
    } else {
      html += "<span style='color: #0078d4; font-weight: bold;'>N/A</span>";
    }
    html += "</td></tr>";

    // Add memory metrics if available from the background process data
    if (bgData.physicalTotalKB > 0) {
      // Calculate memory usage percentages
      double physicalTotalGB = bgData.physicalTotalKB / (1024.0 * 1024.0);
      double physicalAvailableGB =
        bgData.physicalAvailableKB / (1024.0 * 1024.0);
      double physicalUsedGB = physicalTotalGB - physicalAvailableGB;
      double physicalUsedPercent = (physicalUsedGB / physicalTotalGB) * 100.0;

      // Set RAM color based on usage
      QString ramColor = physicalUsedPercent > 80 ? "#FF8C00" : "#0078d4";

      html +=
        QString("<tr><td colspan='2'>RAM Usage: <span style='color: %1; "
                "font-weight: bold;'>%2 GB / %3 GB (%4%)</span></td></tr>")
          .arg(ramColor)
          .arg(physicalUsedGB, 0, 'f', 1)
          .arg(physicalTotalGB, 0, 'f', 1)
          .arg(physicalUsedPercent, 0, 'f', 1);

      // Add committed memory if available
      if (bgData.commitTotalKB > 0 && bgData.commitLimitKB > 0) {
        double commitTotalGB = bgData.commitTotalKB / (1024.0 * 1024.0);
        double commitLimitGB = bgData.commitLimitKB / (1024.0 * 1024.0);
        double commitPercent = (commitTotalGB / commitLimitGB) * 100.0;

        QString commitColor = commitPercent > 80 ? "#FF8C00" : "#0078d4";

        html +=
          QString(
            "<tr><td colspan='2'>Committed Memory: <span style='color: %1; "
            "font-weight: bold;'>%2 GB / %3 GB (%4%)</span></td></tr>")
            .arg(commitColor)
            .arg(commitTotalGB, 0, 'f', 1)
            .arg(commitLimitGB, 0, 'f', 1)
            .arg(commitPercent, 0, 'f', 1);
      }

      // Create a memory breakdown section
      html += "<tr><td colspan='2'><div style='margin-top: 6px; font-weight: "
              "bold;'>Memory Breakdown:</div></td></tr>";

      // Kernel memory (paged + non-paged)
      if (bgData.kernelPagedKB > 0 || bgData.kernelNonPagedKB > 0) {
        double kernelPagedMB = bgData.kernelPagedKB / 1024.0;
        double kernelNonPagedMB = bgData.kernelNonPagedKB / 1024.0;
        double kernelTotalMB = kernelPagedMB + kernelNonPagedMB;

        html +=
          QString("<tr><td colspan='2'>Kernel / Driver: <span style='color: "
                  "#0078d4;'>%1 MB</span> "
                  "<span style='color: #666666; font-size: 0.9em;'>(Paged: %2 "
                  "MB, Non-paged: %3 MB)</span></td></tr>")
            .arg(kernelTotalMB, 0, 'f', 1)
            .arg(kernelPagedMB, 0, 'f', 1)
            .arg(kernelNonPagedMB, 0, 'f', 1);
      }

      // File cache
      if (bgData.systemCacheKB > 0) {
        double systemCacheMB = bgData.systemCacheKB / 1024.0;
        html += QString("<tr><td colspan='2'>File Cache: <span style='color: "
                        "#0078d4;'>%1 MB</span></td></tr>")
                  .arg(systemCacheMB, 0, 'f', 1);
      }

      // User-mode private memory
      if (bgData.userModePrivateKB > 0) {
        double userModePrivateMB = bgData.userModePrivateKB / 1024.0;
        html += QString("<tr><td colspan='2'>User-mode Private: <span "
                        "style='color: #0078d4;'>%1 MB</span></td></tr>")
                  .arg(userModePrivateMB, 0, 'f', 1);
      }

      // Other memory
      if (bgData.otherMemoryKB > 0) {
        double otherMemoryMB = bgData.otherMemoryKB / 1024.0;
        html +=
          QString("<tr><td colspan='2'>Other Memory: <span style='color: "
                  "#0078d4;'>%1 MB</span> "
                  "<span style='color: #666666; font-size: 0.9em;'>(driver "
                  "DMA, firmware, hardware reservations)</span></td></tr>")
            .arg(otherMemoryMB, 0, 'f', 1);
      }
    }

    html += "</table>";

    // Add DPC/Interrupt latency warning if detected
    if (bgData.hasDpcLatencyIssues) {
      html += "<div style='background-color: #442200; padding: 10px; "
              "border-radius: 5px; margin-bottom: 15px;'>";
      html += "<span style='color: #FF8C00; font-weight: bold;'>⚠️ HIGH "
              "DPC/INTERRUPT LATENCY DETECTED!</span><br>";
      html += "<span style='color: #DDDDDD;'>This may indicate driver issues "
              "causing stuttering in games.</span>";
      html += "</div>";
    }

    // Create a function for table rendering to avoid code duplication
    auto renderProcessTable = [](const QMap<QString, ProcessInfo>& procs,
                                 const QString& title,
                                 bool showDpcInfo = false) -> QString {
      if (procs.isEmpty()) {
        return QString();
      }

      QString html = "<h3>" + title + "</h3>";
      html += "<table style='width: 100%; border-collapse: collapse; "
              "margin-bottom: 15px;'>";
      html += "<tr style='background-color: #333333;'>";
      html += "<th style='text-align: left; padding: 8px; border-bottom: 1px "
              "solid #444;'>Application</th>";
      html += "<th style='text-align: right; padding: 8px; border-bottom: 1px "
              "solid #444;'>CPU</th>";
      html += "<th style='text-align: right; padding: 8px; border-bottom: 1px "
              "solid #444;'>Memory</th>";
      html += "<th style='text-align: right; padding: 8px; border-bottom: 1px "
              "solid #444;'>GPU</th>";
      html += "</tr>";

      // Sort processes by memory usage (highest first)
      QList<QString> sortedKeys = procs.keys();
      std::sort(sortedKeys.begin(), sortedKeys.end(),
                [&procs](const QString& a, const QString& b) {
                  return procs[a].memory > procs[b].memory;
                });

      bool alternateRow = false;

      for (const QString& key : sortedKeys) {
        const ProcessInfo& proc = procs[key];

        // Determine row background color for alternating rows
        QString rowStyle = alternateRow ? "background-color: #2d2d2d;"
                                        : "background-color: #252525;";
        alternateRow = !alternateRow;

        // Determine text styles based on resource usage - changed CPU threshold
        // to 1%
        QString cpuStyle = proc.cpu > 1.0 ? "color: #FF8C00; font-weight: bold;"
                                          : "color: #0078d4;";
        QString memStyle = proc.memory > 500.0
                             ? "color: #FF8C00; font-weight: bold;"
                             : "color: #0078d4;";
        QString gpuStyle = proc.gpu > 3.0 ? "color: #FF8C00; font-weight: bold;"
                                          : "color: #0078d4;";
        QString nameStyle = proc.isHighUsage ? "font-weight: bold;" : "";

        html += QString("<tr style='%1'>").arg(rowStyle);

        // Show instance count in name if > 1
        QString displayName = proc.name;
        if (proc.instances > 1) {
          displayName += QString(" (%1 instances)").arg(proc.instances);
        }

        // Add special indicator for DPC/Interrupt sources
        if (showDpcInfo && (proc.isDpcSource || proc.isInterruptSource)) {
          nameStyle = "color: #FF8C00; font-weight: bold;";
        }

        html +=
          QString(
            "<td style='padding: 6px; border-bottom: 1px solid #333; %1'>%2")
            .arg(nameStyle)
            .arg(displayName);

        // Add DPC/Interrupt indicator
        if (showDpcInfo) {
          if (proc.isDpcSource && proc.isInterruptSource) {
            html += " <span style='color: #FF8C00;'>[DPC & Interrupt]</span>";
          } else if (proc.isDpcSource) {
            html += " <span style='color: #FF8C00;'>[DPC]</span>";
          } else if (proc.isInterruptSource) {
            html += " <span style='color: #FF8C00;'>[Interrupt]</span>";
          }
        }
        html += "</td>";

        // CPU column with peak info if available
        html += QString("<td style='text-align: right; padding: 6px; "
                        "border-bottom: 1px solid #333; %1'>%2%")
                  .arg(cpuStyle)
                  .arg(proc.cpu, 0, 'f', 1);

        if (proc.peakCpu > 0 && proc.peakCpu > proc.cpu * 1.2) {
          html += QString(" <span style='font-size: 0.9em; color: "
                          "#AAAAAA;'>(Peak: %1%)</span>")
                    .arg(proc.peakCpu, 0, 'f', 1);
        }

        if (proc.cpuSpikeCount > 0) {
          html +=
            QString(
              " <span style='font-size: 0.9em; color: #FF8C00;'>⚡%1</span>")
              .arg(proc.cpuSpikeCount);
        }
        html += "</td>";

        // Memory column
        html += QString("<td style='text-align: right; padding: 6px; "
                        "border-bottom: 1px solid #333; %1'>%2 MB</td>")
                  .arg(memStyle)
                  .arg(proc.memory, 0, 'f', 0);

        // GPU column with sanity checks
        html += "<td style='text-align: right; padding: 6px; border-bottom: "
                "1px solid #333;'>";

        // Display GPU metrics with proper type separation
        constexpr double NVIDIA_INVALID_VALUE = 4294967295.0;

        // Handle GPU compute percentage
        if (proc.gpuComputePercent > 0 && proc.gpuComputePercent <= 100.0) {
          html += QString("<span style='%1'>%2%</span> <span style='font-size: "
                          "0.9em; color: #AAAAAA;'>(Compute)</span>")
                    .arg(gpuStyle)
                    .arg(proc.gpuComputePercent, 0, 'f', 1);
        }
        // Handle general GPU usage
        else if (proc.gpu > 0 && proc.gpu <= 100.0) {
          html += QString("<span style='%1'>%2%</span>")
                    .arg(gpuStyle)
                    .arg(proc.gpu, 0, 'f', 1);
        } else {
          html += "-";
        }

        // Add GPU memory usage if available (with sanity check)
        if (proc.gpuMemoryMB > 0 &&
            proc.gpuMemoryMB < 32768) {  // Cap at 32GB as a safety
          html += QString("<br><span style='font-size: 0.9em; color: "
                          "#AAAAAA;'>Mem: %1 MB</span>")
                    .arg(proc.gpuMemoryMB, 0, 'f', 0);
        }

        // Add GPU encoder usage if available
        if (proc.gpuEncoderPercent > 0 && proc.gpuEncoderPercent <= 100.0) {
          html += QString("<br><span style='font-size: 0.9em; color: "
                          "#AAAAAA;'>Encoder: %1%</span>")
                    .arg(proc.gpuEncoderPercent, 0, 'f', 1);
        }

        html += "</td></tr>";
      }

      html += "</table>";
      return html;
    };

    // Render all processes
    html += renderProcessTable(
      processes, "Running Applications (Top Resource Users)", true);

    // Add recommendations section
    if (!recommendations.isEmpty()) {
      html += "<h3>Performance Recommendations</h3>";
      html += "<ul style='margin-top: 5px; margin-bottom: 15px;'>";
      for (const QString& rec : recommendations) {
        html += QString("<li style='margin-bottom: 5px;'>%1</li>").arg(rec);
      }
      html += "</ul>";
    }

    // Final generation of HTML before returning
    LOG_INFO << "BackgroundProcessRenderer: HTML content generation completed "
                 "successfully";

    // Create a safe copy to return - prevents any potential reference issues
    QString safeHtmlCopy = html;
    return safeHtmlCopy;
  } catch (const std::exception& e) {
    LOG_ERROR << "BackgroundProcessRenderer: Error processing background results: "
              << e.what();
    return QString("<h3>Background Process Analysis Error</h3><p style='color: "
                   "#FF6666;'>An error occurred while processing background "
                   "results: ") +
           QString(e.what()) + "</p>";
  } catch (...) {
    LOG_ERROR << "BackgroundProcessRenderer: Unknown error processing "
                 "background results";
    return QString(
      "<h3>Background Process Analysis Error</h3><p style='color: #FF6666;'>An "
      "unknown error occurred while processing background results.</p>");
  }
}

void BackgroundProcessRenderer::parseSystemResourceInfo(
  const QString& result, double& cpuUsage, double& gpuUsage, double& dpcTime,
  double& intTime, double& diskIO) {
  QRegularExpression cpuRegex("CPU Usage: (\\d+\\.?\\d*)%");
  QRegularExpression dpcRegex("DPC Time: (\\d+\\.?\\d*)%");
  QRegularExpression intRegex("Interrupt Time: (\\d+\\.?\\d*)%");
  QRegularExpression diskRegex("Disk I/O: (\\d+\\.?\\d*) MB/s");
  QRegularExpression gpuRegex("GPU Usage: (\\d+\\.?\\d*)%");
  QRegularExpression gpuUtilRegex("GPU: (\\d+\\.?\\d*)%");

  QStringList lines = result.split("\n");

  for (const QString& line : lines) {
    // Extract system resource values
    auto cpuMatch = cpuRegex.match(line);
    if (cpuMatch.hasMatch()) {
      double value = cpuMatch.captured(1).toDouble();
      // Sanity check
      if (value >= 0 && value <= 100.0) {
        cpuUsage = value;
      }
    }

    // Try both GPU regex patterns
    auto gpuMatch = gpuRegex.match(line);
    if (gpuMatch.hasMatch()) {
      double value = gpuMatch.captured(1).toDouble();
      // Sanity check
      if (value >= 0 && value <= 100.0) {
        gpuUsage = value;
      }
    }

    // Alternative GPU pattern often found in the output
    auto gpuUtilMatch = gpuUtilRegex.match(line);
    if (gpuUtilMatch.hasMatch()) {
      double value = gpuUtilMatch.captured(1).toDouble();
      // Sanity check
      if (value >= 0 && value <= 100.0) {
        gpuUsage = value;
      }
    }

    auto dpcMatch = dpcRegex.match(line);
    if (dpcMatch.hasMatch()) {
      double value = dpcMatch.captured(1).toDouble();
      // Sanity check
      if (value >= 0 && value <= 100.0) {
        dpcTime = value;
      }
    }

    auto intMatch = intRegex.match(line);
    if (intMatch.hasMatch()) {
      double value = intMatch.captured(1).toDouble();
      // Sanity check
      if (value >= 0 && value <= 100.0) {
        intTime = value;
      }
    }

    auto diskMatch = diskRegex.match(line);
    if (diskMatch.hasMatch()) {
      double value = diskMatch.captured(1).toDouble();
      // Sanity check
      if (value >= 0) {
        diskIO = value;
      }
    }
  }
}

}  // namespace DiagnosticRenderers
