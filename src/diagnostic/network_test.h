#pragma once

#include <atomic>
#include <future>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include <WinSock2.h>
#include <iphlpapi.h>
#include <Windows.h>
#include <WS2tcpip.h>
#include <icmpapi.h>

namespace NetworkTest {

// Add a cancellation flag
extern std::atomic<bool> g_cancelNetworkTest;

// Add cancellation function
void cancelNetworkTests();

// Reset the cancellation flag
void resetCancelFlag();

// Structure to hold information about current network adapter
struct NetworkAdapterInfo {
  bool isWifi;
  std::string adapterName;
  std::string description;
  std::string macAddress;
  std::string ipAddress;
  double linkSpeedMbps;
  bool isDhcpEnabled;
};

// Structure to hold ping statistics for a test
struct PingStats {
  std::string targetHost;
  std::string targetIp;
  int sentPackets;
  int receivedPackets;
  double packetLossPercent;
  double minLatencyMs;
  double maxLatencyMs;
  double avgLatencyMs;
  double jitterMs;
  std::vector<double> latencyValues;
  std::string region;  // Add region to PingStats
};

// Overall network health data structure
struct NetworkMetrics {
  bool onWifi = false;
  NetworkAdapterInfo primaryAdapter;  // Add this line
  std::vector<NetworkAdapterInfo> activeAdapters;
  std::string routerIp;
  std::vector<PingStats> pingResults;
  bool hasHighLatency = false;
  bool hasHighJitter = false;
  bool hasPacketLoss = false;
  bool possibleBufferbloat = false;
  std::string networkIssues;

  // Additional bufferbloat metrics
  double baselineLatencyMs = 0.0;
  double downloadLatencyMs = 0.0;
  double uploadLatencyMs = 0.0;
  double downloadBloatPercent = 0.0;
  double uploadBloatPercent = 0.0;
  std::string bufferbloatDirection = "";

  // Flag to prevent duplicate bufferbloat testing
  bool bufferbloatTestCompleted = false;

  // Regional latency averages
  std::map<std::string, double> regionalLatencies;
};

// Structure for region-based server tracking
struct ServerInfo {
  std::string hostname;
  std::string region;  // "EU", "NA", "ASIA", etc.
  bool isReliable;     // Some servers are more reliable than others
};

// Run ping test to a specific host
PingStats runPingTest(const std::string& host, int numPings = 10,
                      int timeoutMs = 1000);

// Get all network adapters on the system
std::vector<NetworkAdapterInfo> getNetworkAdapters();

// Get the primary network adapter
NetworkAdapterInfo getPrimaryAdapter();

// Test for bufferbloat issues (latency under load)
bool testForBufferbloat(NetworkMetrics& metrics, int testDurationSeconds = 10);

// Get list of game servers for testing
std::vector<std::string> getGameServerList();

// Get regional server list
std::vector<ServerInfo> getRegionalServerList();

// Run network diagnostics
NetworkMetrics runNetworkDiagnostics(int pingCount = 15, int timeoutMs = 800,
                                     bool includeBufferbloat = true,
                                     int bufferbloatDuration = 5);

// Format network results for display
std::string formatNetworkResults(const NetworkMetrics& metrics);

// Format enhanced network results with summary statistics
std::string formatEnhancedNetworkResults(const NetworkMetrics& metrics);
}  // namespace NetworkTest
