#include <WinSock2.h>
#include <iphlpapi.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <icmpapi.h>

#include "diagnostic/DiagnosticDataStore.h"
#include "hardware/ConstantSystemInfo.h"

// Now include the actual network test implementation
#include "network_test.h"
#include "network_test_interface.h"

namespace NetworkTest {
// Implementation of the wrapper function that stores results in
// DiagnosticDataStore
NetworkTestResult RunNetworkDiagnostics(int pingCount, int timeoutMs,
                                        bool includeBufferbloat,
                                        int bufferbloatDuration) {
  // Get existing network info from ConstantSystemInfo if available
  auto& sysInfo = SystemMetrics::GetConstantSystemInfo();

  // Call original implementation
  NetworkMetrics metrics = runNetworkDiagnostics(
    pingCount, timeoutMs, includeBufferbloat, bufferbloatDuration);

  // Store results in DiagnosticDataStore
  auto& dataStore = DiagnosticDataStore::getInstance();

  // Convert from NetworkTest::NetworkMetrics to DiagnosticDataStore::NetworkData
  DiagnosticDataStore::NetworkData networkData;
  networkData.onWifi = metrics.onWifi;

  // Calculate average values for summary metrics
  double totalLatency = 0.0;
  double totalJitter = 0.0;
  double totalPacketLoss = 0.0;
  int validServers = 0;

  // Convert each ping result to ServerResult
  for (const auto& ping : metrics.pingResults) {
    if (ping.receivedPackets > 0) {
      DiagnosticDataStore::NetworkData::ServerResult result;
      result.hostname = ping.targetHost;
      result.ipAddress = ping.targetIp;
      result.region = ping.region;
      result.minLatencyMs = ping.minLatencyMs;
      result.maxLatencyMs = ping.maxLatencyMs;
      result.avgLatencyMs = ping.avgLatencyMs;
      result.jitterMs = ping.jitterMs;
      result.packetLossPercent = ping.packetLossPercent;
      result.sentPackets = ping.sentPackets;
      result.receivedPackets = ping.receivedPackets;

      networkData.serverResults.push_back(result);

      // Accumulate for averages
      totalLatency += ping.avgLatencyMs;
      totalJitter += ping.jitterMs;
      totalPacketLoss += ping.packetLossPercent;
      validServers++;
    }
  }

  // Calculate averages if we have valid data
  if (validServers > 0) {
    networkData.averageLatencyMs = totalLatency / validServers;
    networkData.averageJitterMs = totalJitter / validServers;
    networkData.averagePacketLoss = totalPacketLoss / validServers;
  }

  // Store bufferbloat test results
  networkData.baselineLatencyMs = metrics.baselineLatencyMs;
  networkData.downloadLatencyMs = metrics.downloadLatencyMs;
  networkData.uploadLatencyMs = metrics.uploadLatencyMs;
  networkData.hasBufferbloat = metrics.possibleBufferbloat;
  networkData.networkIssues = metrics.networkIssues;

  // Store regional latencies
  for (const auto& region : metrics.regionalLatencies) {
    DiagnosticDataStore::NetworkData::RegionalLatency regionalLatency;
    regionalLatency.region = region.first;
    regionalLatency.latencyMs = region.second;
    networkData.regionalLatencies.push_back(regionalLatency);
  }

  // Update the data store
  dataStore.updateNetworkData(networkData);

  // Convert to simplified result structure with enhanced report
  NetworkTestResult result;
  result.formattedOutput = formatEnhancedNetworkResults(metrics);
  result.isWiFi = metrics.onWifi;
  result.hasIssues = metrics.hasHighLatency || metrics.hasHighJitter ||
                     metrics.hasPacketLoss || metrics.possibleBufferbloat;

  return result;
}
}  // namespace NetworkTest
