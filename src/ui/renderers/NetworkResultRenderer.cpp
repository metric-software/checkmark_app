#include "NetworkResultRenderer.h"

#include <algorithm>  // Add this for std::max

#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QVBoxLayout>

#include "DiagnosticViewComponents.h"
#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"

namespace DiagnosticRenderers {

QWidget* NetworkResultRenderer::createNetworkResultWidget(
  const QString& result) {
  // Get data from DiagnosticDataStore
  const auto& dataStore = DiagnosticDataStore::getInstance();
  const auto& networkData = dataStore.getNetworkData();

  // Get constant system information
  const auto& constantInfo = SystemMetrics::GetConstantSystemInfo();

  // Create the main container widget
  QWidget* containerWidget = new QWidget();
  QVBoxLayout* mainLayout = new QVBoxLayout(containerWidget);

  // Create a title for the network section
  QLabel* networkTitle = new QLabel("<b>Network Performance Analysis:</b>");
  networkTitle->setStyleSheet(
    "color: #ffffff; font-size: 14px; margin-top: 10px;");
  mainLayout->addWidget(networkTitle);

  // Create layout for network metrics
  QWidget* networkMetricsWidget = new QWidget();
  QGridLayout* networkMetricsLayout = new QGridLayout(networkMetricsWidget);
  networkMetricsLayout->setSpacing(10);

  // Initialize values with data from DiagnosticDataStore
  QString connectionType = networkData.onWifi ? "WiFi" : "Wired";
  double avgLatency = networkData.averageLatencyMs;
  double avgJitter = networkData.averageJitterMs;
  double packetLoss = networkData.averagePacketLoss;
  double downloadBloat = 0.0;
  double uploadBloat = 0.0;

  // Calculate DNS-specific latency average (only 1.1.1.1 and 8.8.8.8)
  double dnsLatency = 0.0;
  int dnsServerCount = 0;

  for (const auto& serverResult : networkData.serverResults) {
    QString serverName = QString::fromStdString(serverResult.hostname.empty()
                                                  ? serverResult.ipAddress
                                                  : serverResult.hostname);
    if (serverName == "1.1.1.1" || serverName == "8.8.8.8") {
      dnsLatency += serverResult.avgLatencyMs;
      dnsServerCount++;
    }
  }

  // Use DNS servers' average if available, otherwise fall back to overall
  // average
  if (dnsServerCount > 0) {
    avgLatency = dnsLatency / dnsServerCount;
  }

  // Calculate bufferbloat percentages if baseline latency is available
  if (networkData.baselineLatencyMs > 0) {
    if (networkData.downloadLatencyMs > 0) {
      downloadBloat =
        ((networkData.downloadLatencyMs - networkData.baselineLatencyMs) /
         networkData.baselineLatencyMs) *
        100.0;
      downloadBloat = (downloadBloat < 0.0) ? 0.0 : downloadBloat;
    }

    if (networkData.uploadLatencyMs > 0) {
      uploadBloat =
        ((networkData.uploadLatencyMs - networkData.baselineLatencyMs) /
         networkData.baselineLatencyMs) *
        100.0;
      uploadBloat = (uploadBloat < 0.0) ? 0.0 : uploadBloat;
    }
  }

  // If critical data is missing from DataStore, fall back to parsing the result
  // string
  if (avgLatency <= 0.0 || connectionType.isEmpty()) {
    // Parse connection type
    if (result.contains("Connection Type: WiFi", Qt::CaseInsensitive)) {
      connectionType = "WiFi";
    }

    // First try to extract latency specifically for DNS servers
    double dnsLatencySum = 0.0;
    int dnsCount = 0;

    // Try to find 1.1.1.1 latency
    QRegularExpression cloudflareRegex(
      "Target:\\s+1\\.1\\.1\\.1.*?Latency:\\s+(\\d+\\.?\\d*)\\s+ms",
      QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch cloudflareMatch = cloudflareRegex.match(result);
    if (cloudflareMatch.hasMatch()) {
      dnsLatencySum += cloudflareMatch.captured(1).toDouble();
      dnsCount++;
    }

    // Try to find 8.8.8.8 latency
    QRegularExpression googleRegex(
      "Target:\\s+8\\.8\\.8\\.8.*?Latency:\\s+(\\d+\\.?\\d*)\\s+ms",
      QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch googleMatch = googleRegex.match(result);
    if (googleMatch.hasMatch()) {
      dnsLatencySum += googleMatch.captured(1).toDouble();
      dnsCount++;
    }

    // Calculate DNS average if we found at least one DNS server
    if (dnsCount > 0) {
      avgLatency = dnsLatencySum / dnsCount;
    } else {
      // Look specifically for NEAR region latency as second option
      QRegularExpression nearRegionRegex(
        "NEAR\\s+Region:\\s*(\\d+\\.?\\d*)\\s*ms");
      QRegularExpressionMatch nearRegionMatch = nearRegionRegex.match(result);
      if (nearRegionMatch.hasMatch()) {
        avgLatency = nearRegionMatch.captured(1).toDouble();
      } else {
        // Fall back to overall average if NEAR region isn't found
        QRegularExpression latencyRegex(
          "Average latency:\\s*(\\d+\\.?\\d*)\\s*ms");
        QRegularExpressionMatch latencyMatch = latencyRegex.match(result);
        if (latencyMatch.hasMatch()) {
          avgLatency = latencyMatch.captured(1).toDouble();
        }
      }
    }
  }

  // Use the same fallbacks for jitter if needed
  if (avgJitter <= 0.0) {
    QRegularExpression jitterRegex("Average jitter:\\s*(\\d+\\.?\\d*)\\s*ms");
    QRegularExpressionMatch jitterMatch = jitterRegex.match(result);
    if (jitterMatch.hasMatch()) {
      avgJitter = jitterMatch.captured(1).toDouble();
    }
  }

  // Extract bufferbloat percentages if not available from datastore
  if (downloadBloat <= 0.0) {
    QRegularExpression downloadBloatRegex(
      "Download test latency:.*?\\+(\\d+\\.?\\d*)%");
    QRegularExpressionMatch downloadBloatMatch =
      downloadBloatRegex.match(result);
    if (downloadBloatMatch.hasMatch()) {
      downloadBloat = downloadBloatMatch.captured(1).toDouble();
    }
  }

  if (uploadBloat <= 0.0) {
    QRegularExpression uploadBloatRegex(
      "Upload test latency:.*?\\+(\\d+\\.?\\d*)%");
    QRegularExpressionMatch uploadBloatMatch = uploadBloatRegex.match(result);
    if (uploadBloatMatch.hasMatch()) {
      uploadBloat = uploadBloatMatch.captured(1).toDouble();
    }
  }

  // Create metric boxes with color-coding based on performance
  QString latencyColor = getColorForLatency(avgLatency);

  // Collect server data for display
  struct ServerData {
    QString server;
    QString location;
    double ping;
    double jitter;
    double loss;
  };

  QList<ServerData> serverList;

  // First try to get server data from the DiagnosticDataStore
  for (const auto& serverResult : networkData.serverResults) {
    ServerData data;
    data.server = QString::fromStdString(serverResult.hostname.empty()
                                           ? serverResult.ipAddress
                                           : serverResult.hostname);
    data.location = QString::fromStdString(serverResult.region);
    data.ping = serverResult.avgLatencyMs;
    data.jitter = serverResult.jitterMs;
    data.loss = serverResult.packetLossPercent;

    if (data.ping > 0) {
      serverList.append(data);
    }
  }

  // If no server data from DataStore, parse from raw result
  if (serverList.isEmpty()) {
    // Updated regex to capture both server name and region information
    QRegularExpression serverRegex(
      "Target:\\s+(.*?)\\s*(?:\\((.+?)\\))?\\s*\n\\s+Latency:\\s+(\\d+\\.?\\d*)"
      "\\s+ms[^\\n]*\n\\s+Jitter:\\s+(\\d+\\.?\\d*)\\s+ms\\s*\n\\s+Packet\\s+"
      "Loss:\\s+(\\d+\\.?\\d*)%");
    QRegularExpressionMatchIterator serverMatches =
      serverRegex.globalMatch(result);

    // Alternative regex if main one doesn't match
    if (!serverMatches.hasNext()) {
      serverRegex = QRegularExpression(
        "(\\w+(?:\\s+\\w+)*)\\s+Region:\\s*(\\w+(?:\\s*\\([^)]+\\))?)\\s*\n.*?"
        "Latency:\\s+(\\d+\\.?\\d*)\\s+ms.*?\\n.*?Jitter:\\s+(\\d+\\.?\\d*)\\s+"
        "ms.*?\\n.*?Packet\\s+Loss:\\s+(\\d+\\.?\\d*)%",
        QRegularExpression::DotMatchesEverythingOption);
      serverMatches = serverRegex.globalMatch(result);
    }

    double dnsPacketLossSum = 0.0;
    int dnsServersCount = 0;

    while (serverMatches.hasNext()) {
      QRegularExpressionMatch match = serverMatches.next();
      ServerData data;
      data.server = match.captured(1).trimmed();
      data.location = match.captured(2).trimmed();
      data.ping = match.captured(3).toDouble();
      data.jitter = match.captured(4).toDouble();
      data.loss = match.captured(5).toDouble();

      serverList.append(data);

      // Calculate packet loss average just from Google and Cloudflare DNS
      // servers
      if (data.server == "8.8.8.8" || data.server == "1.1.1.1") {
        dnsPacketLossSum += data.loss;
        dnsServersCount++;
      }
    }

    // Calculate DNS packet loss average if not available from DataStore
    if (packetLoss <= 0.0 && dnsServersCount > 0) {
      packetLoss = dnsPacketLossSum / dnsServersCount;
    }
  }

  // Create performance metric boxes
  networkMetricsLayout->addWidget(
    createMetricBox("Connection Type", connectionType), 0, 0);
  networkMetricsLayout->addWidget(
    createLatencyBox("Average Latency", avgLatency, latencyColor), 0, 1);
  networkMetricsLayout->addWidget(
    createLatencyBox("Jitter", avgJitter,
                     avgJitter < 15 ? "#44FF44" : "#FFAA00"),
    0, 2);
  networkMetricsLayout->addWidget(createPacketLossBox(packetLoss), 0, 3);

  // Add bufferbloat percentage boxes
  QString downloadBloatColor =
    downloadBloat < 50 ? "#44FF44"
                       : (downloadBloat < 100 ? "#FFAA00" : "#FF6666");
  QString uploadBloatColor =
    uploadBloat < 50 ? "#44FF44" : (uploadBloat < 100 ? "#FFAA00" : "#FF6666");

  networkMetricsLayout->addWidget(
    createMetricBox("Download Latency Increase",
                    QString::number(downloadBloat, 'f', 1) + "%",
                    downloadBloatColor),
    1, 0, 1, 2);
  networkMetricsLayout->addWidget(
    createMetricBox("Upload Latency Increase",
                    QString::number(uploadBloat, 'f', 1) + "%",
                    uploadBloatColor),
    1, 2, 1, 2);

  // Add network metrics widget to the main layout
  mainLayout->addWidget(networkMetricsWidget);

  // Create a table for server results
  QWidget* serverTableWidget = new QWidget();
  QVBoxLayout* serverTableLayout = new QVBoxLayout(serverTableWidget);
  serverTableLayout->setContentsMargins(0, 20, 0, 0);

  // Add table header
  QLabel* tableTitle = new QLabel("Server Connection Details:");
  tableTitle->setStyleSheet("color: #0078d4; font-weight: bold;");
  serverTableLayout->addWidget(tableTitle);

  // Create table grid layout
  QWidget* tableWidget = new QWidget();
  QGridLayout* tableGrid = new QGridLayout(tableWidget);
  tableGrid->setSpacing(8);

  // Add table headers
  QStringList headers = {"Server", "Location", "Ping (ms)", "Jitter (ms)",
                         "Packet Loss (%)"};
  for (int i = 0; i < headers.size(); i++) {
    QLabel* headerLabel = new QLabel(headers[i]);
    headerLabel->setStyleSheet("color: #FFFFFF; font-weight: bold;");
    tableGrid->addWidget(headerLabel, 0, i);
  }

  // Sort servers by ping (latency)
  std::sort(
    serverList.begin(), serverList.end(),
    [](const ServerData& a, const ServerData& b) { return a.ping < b.ping; });

  // Populate table with sorted server data
  int row = 1;
  foreach (const ServerData& data, serverList) {
    // Add server name
    QLabel* serverLabel = new QLabel(data.server);
    serverLabel->setStyleSheet("color: #DDDDDD;");
    tableGrid->addWidget(serverLabel, row, 0);

    // Add location/region
    QLabel* locationLabel = new QLabel(data.location);
    locationLabel->setStyleSheet("color: #AAAAAA;");
    tableGrid->addWidget(locationLabel, row, 1);

    // Add ping with color
    QLabel* pingLabel = new QLabel(QString::number(data.ping, 'f', 1));
    pingLabel->setStyleSheet("color: " + getColorForLatency(data.ping) + ";");
    pingLabel->setAlignment(Qt::AlignCenter);
    tableGrid->addWidget(pingLabel, row, 2);

    // Add jitter with color
    QLabel* jitterLabel = new QLabel(QString::number(data.jitter, 'f', 1));
    QString jitterColor = (data.jitter < 15 ? "#44FF44" : "#FFAA00");
    jitterLabel->setStyleSheet("color: " + jitterColor + ";");
    jitterLabel->setAlignment(Qt::AlignCenter);
    tableGrid->addWidget(jitterLabel, row, 3);

    // Add packet loss with color
    QLabel* lossLabel = new QLabel(QString::number(data.loss, 'f', 1));
    QString lossColor = "#44FF44";
    if (data.loss > 2.0)
      lossColor = "#FF6666";
    else if (data.loss > 0.5)
      lossColor = "#FFAA00";

    lossLabel->setStyleSheet("color: " + lossColor + ";");
    lossLabel->setAlignment(Qt::AlignCenter);
    tableGrid->addWidget(lossLabel, row, 4);

    row++;
  }

  // If no servers were found with the regex, add a placeholder row
  if (row == 1) {
    QLabel* noDataLabel = new QLabel("No server connection data available");
    noDataLabel->setStyleSheet("color: #999999; font-style: italic;");
    tableGrid->addWidget(noDataLabel, 1, 0, 1, 5);
  }

  // Set stretch factors to make the server and location columns wider
  tableGrid->setColumnStretch(0, 2);
  tableGrid->setColumnStretch(1, 2);
  tableGrid->setColumnStretch(2, 1);
  tableGrid->setColumnStretch(3, 1);
  tableGrid->setColumnStretch(4, 1);

  serverTableLayout->addWidget(tableWidget);
  mainLayout->addWidget(serverTableWidget);

  // Add raw data section (collapsible)
  QWidget* rawDataContainer = new QWidget();
  QVBoxLayout* rawDataLayout = new QVBoxLayout(rawDataContainer);
  rawDataLayout->setContentsMargins(0, 10, 0, 0);

  QPushButton* showRawDataBtn = new QPushButton("▼ Show Raw Network Data");
  showRawDataBtn->setStyleSheet(R"(
        QPushButton {
            color: #0078d4;
            border: none;
            text-align: left;
            padding: 4px;
            font-size: 12px;
            background: transparent;
        }
        QPushButton:hover {
            color: #1084d8;
            text-decoration: underline;
        }
    )");

  QWidget* rawDataWidget =
    DiagnosticViewComponents::createRawDataWidget(result);
  rawDataWidget->setVisible(false);

  // Connect button to toggle visibility
  QObject::connect(
    showRawDataBtn, &QPushButton::clicked, [showRawDataBtn, rawDataWidget]() {
      bool visible = rawDataWidget->isVisible();
      rawDataWidget->setVisible(!visible);
      showRawDataBtn->setText(visible ? "▼ Show Raw Network Data"
                                      : "▲ Hide Raw Network Data");
    });

  rawDataLayout->addWidget(showRawDataBtn);
  rawDataLayout->addWidget(rawDataWidget);
  mainLayout->addWidget(rawDataContainer);

  return containerWidget;
}

QWidget* NetworkResultRenderer::createMetricBox(const QString& title,
                                                const QString& value,
                                                const QString& color) {
  QWidget* box = new QWidget();
  box->setStyleSheet(R"(
        QWidget {
            background-color: #252525;
            border: 1px solid #383838;
            border-radius: 4px;
            padding: 8px;
        }
    )");

  QVBoxLayout* layout = new QVBoxLayout(box);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(2);

  QLabel* titleLabel = new QLabel(title);
  titleLabel->setStyleSheet(
    "color: #999999; font-size: 11px; background: transparent;");

  QLabel* valueLabel =
    new QLabel("<span style='color: " + color +
               "; font-size: 16px; font-weight: bold;'>" + value + "</span>");
  valueLabel->setAlignment(Qt::AlignCenter);

  layout->addWidget(titleLabel);
  layout->addWidget(valueLabel);

  return box;
}

QWidget* NetworkResultRenderer::createLatencyBox(const QString& title,
                                                 double latency,
                                                 const QString& color) {
  return createMetricBox(title, QString::number(latency, 'f', 1) + " ms",
                         color);
}

QString NetworkResultRenderer::getColorForLatency(double latency) {
  if (latency < 20)
    return "#44FF44";  // Excellent (green)
  else if (latency < 50)
    return "#88FF88";  // Good (light green)
  else if (latency < 100)
    return "#FFAA00";  // Acceptable (orange)
  else
    return "#FF6666";  // Poor (red)
}

QWidget* NetworkResultRenderer::createPacketLossBox(double packetLoss) {
  QString color = "#44FF44";
  if (packetLoss > 2.0)
    color = "#FF6666";
  else if (packetLoss > 0.5)
    color = "#FFAA00";

  return createMetricBox("Packet Loss",
                         QString::number(packetLoss, 'f', 1) + "%", color);
}

QWidget* NetworkResultRenderer::createBufferbloatSection(
  const QString& result) {
  // Get data from DiagnosticDataStore
  const auto& dataStore = DiagnosticDataStore::getInstance();
  const auto& networkData = dataStore.getNetworkData();

  QWidget* container = new QWidget();
  container->setStyleSheet(R"(
        QWidget {
            background-color: #252525;
            border: 1px solid #383838;
            border-radius: 4px;
        }
    )");

  QVBoxLayout* layout = new QVBoxLayout(container);
  layout->setContentsMargins(10, 10, 10, 10);

  QLabel* titleLabel = new QLabel("Bufferbloat Test:");
  titleLabel->setStyleSheet("color: #0078d4; font-weight: bold;");
  layout->addWidget(titleLabel);

  // Use DiagnosticDataStore data if available
  QString baselineLatency;
  QString downloadLatency;
  QString uploadLatency;
  bool hasBufferbloat = networkData.hasBufferbloat;

  // Format latency data from DataStore if available
  if (networkData.baselineLatencyMs > 0) {
    baselineLatency =
      QString::number(networkData.baselineLatencyMs, 'f', 1) + " ms";

    if (networkData.downloadLatencyMs > 0) {
      double increase =
        ((networkData.downloadLatencyMs - networkData.baselineLatencyMs) /
         networkData.baselineLatencyMs) *
        100.0;
      downloadLatency = QString::number(networkData.downloadLatencyMs, 'f', 1) +
                        " ms (+" + QString::number(increase, 'f', 1) + "%)";

      if (increase > 100.0) {
        downloadLatency += " ⚠️";
      }
    }

    if (networkData.uploadLatencyMs > 0) {
      double increase =
        ((networkData.uploadLatencyMs - networkData.baselineLatencyMs) /
         networkData.baselineLatencyMs) *
        100.0;
      uploadLatency = QString::number(networkData.uploadLatencyMs, 'f', 1) +
                      " ms (+" + QString::number(increase, 'f', 1) + "%)";

      if (increase > 100.0) {
        uploadLatency += " ⚠️";
      }
    }
  }

  // If data not available in DataStore, extract from result string
  if (baselineLatency.isEmpty()) {
    baselineLatency = extractValueFromResult(result, "Baseline latency:");
    downloadLatency = extractValueFromResult(result, "Download test latency:");
    uploadLatency = extractValueFromResult(result, "Upload test latency:");
    hasBufferbloat = result.contains("SIGNIFICANT BUFFERBLOAT DETECTED");
  }

  // Check if we have bufferbloat data
  if (baselineLatency.isEmpty()) {
    QLabel* noDataLabel = new QLabel("No bufferbloat test data available");
    noDataLabel->setStyleSheet("color: #999999;");
    layout->addWidget(noDataLabel);
    return container;
  }

  // Create a grid for latency values
  QGridLayout* latencyGrid = new QGridLayout();
  latencyGrid->setColumnStretch(1, 1);

  // Add baseline latency
  latencyGrid->addWidget(new QLabel("Baseline:"), 0, 0);
  QLabel* baselineLabel = new QLabel(baselineLatency);
  baselineLabel->setStyleSheet("color: #FFFFFF;");
  latencyGrid->addWidget(baselineLabel, 0, 1);

  // Add download latency
  latencyGrid->addWidget(new QLabel("Download:"), 1, 0);
  QLabel* downloadLabel = new QLabel(downloadLatency);

  // Check if there's bufferbloat in download
  if (downloadLatency.contains("⚠️")) {
    downloadLabel->setStyleSheet("color: #FF6666;");
  } else {
    downloadLabel->setStyleSheet("color: #88FF88;");
  }
  latencyGrid->addWidget(downloadLabel, 1, 1);

  // Add upload latency
  latencyGrid->addWidget(new QLabel("Upload:"), 2, 0);
  QLabel* uploadLabel = new QLabel(uploadLatency);

  // Check if there's bufferbloat in upload
  if (uploadLatency.contains("⚠️")) {
    uploadLabel->setStyleSheet("color: #FF6666;");
  } else {
    uploadLabel->setStyleSheet("color: #88FF88;");
  }
  latencyGrid->addWidget(uploadLabel, 2, 1);

  QWidget* gridWidget = new QWidget();
  gridWidget->setLayout(latencyGrid);
  layout->addWidget(gridWidget);

  // Add bufferbloat result summary
  if (hasBufferbloat) {
    QLabel* warningLabel =
      new QLabel("⚠️ Significant bufferbloat detected! Your connection exhibits "
                 "high latency under load.");
    warningLabel->setStyleSheet(
      "color: #FF6666; font-weight: bold; margin-top: 5px;");
    warningLabel->setWordWrap(true);
    layout->addWidget(warningLabel);

    // Extract direction
    QRegularExpression directionRegex("Most affected: (\\w+) traffic");
    QRegularExpressionMatch directionMatch = directionRegex.match(result);
    if (directionMatch.hasMatch()) {
      QLabel* directionLabel = new QLabel("Most affected direction: " +
                                          directionMatch.captured(1).toUpper());
      directionLabel->setStyleSheet("color: #FFAA00;");
      layout->addWidget(directionLabel);
    }
  } else {
    QLabel* goodLabel =
      new QLabel("✓ No significant bufferbloat detected. Your connection "
                 "maintains stable latency under load.");
    goodLabel->setStyleSheet("color: #44FF44; margin-top: 5px;");
    goodLabel->setWordWrap(true);
    layout->addWidget(goodLabel);
  }

  return container;
}

QString NetworkResultRenderer::extractValueFromResult(const QString& result,
                                                      const QString& keyword) {
  QRegularExpression regex(keyword + "\\s*([^\\n]*)");
  QRegularExpressionMatch match = regex.match(result);
  if (match.hasMatch()) {
    return match.captured(1).trimmed();
  }
  return "";
}

}  // namespace DiagnosticRenderers
