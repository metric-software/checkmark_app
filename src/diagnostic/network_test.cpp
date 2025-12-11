#include "network_test.h"
#include "../logging/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <thread>

#include <wininet.h>  // Add this include for Internet functions

namespace NetworkTest {

// Initialize the cancellation flag
std::atomic<bool> g_cancelNetworkTest(false);

void cancelNetworkTests() { g_cancelNetworkTest.store(true); }

void resetCancelFlag() { g_cancelNetworkTest.store(false); }

// Helper function to resolve a hostname to IP address
std::string resolveHostname(const std::string& hostname) {
  struct addrinfo hints = {0}, *addrs;
  char ip[INET6_ADDRSTRLEN] = {0};

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(hostname.c_str(), NULL, &hints, &addrs) != 0) {
    return "";
  }

  for (struct addrinfo* addr = addrs; addr != NULL; addr = addr->ai_next) {
    if (addr->ai_family == AF_INET) {  // IPv4
      struct sockaddr_in* ipv4 =
        reinterpret_cast<struct sockaddr_in*>(addr->ai_addr);
      inet_ntop(AF_INET, &(ipv4->sin_addr), ip, INET_ADDRSTRLEN);
      break;
    }
  }

  freeaddrinfo(addrs);
  return std::string(ip);
}

// Helper function for proper wide to narrow string conversion
std::string WideToNarrow(const std::wstring& wide) {
  if (wide.empty()) return std::string();

  // Calculate size needed
  int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0,
                                 nullptr, nullptr);
  if (size <= 0) return std::string();

  // Perform actual conversion
  std::string narrow(size - 1, 0);  // -1 because size includes null terminator
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &narrow[0], size, nullptr,
                      nullptr);

  return narrow;
}

// Helper function to check if an IP is a local or private network address
bool isLocalOrPrivateIP(const std::string& ip) {
  return ip.empty() || ip == "127.0.0.1" || ip.find("192.168.") == 0 ||
         ip.find("10.") == 0 ||
         (ip.find("172.") == 0 &&
          (ip.find("172.16.") == 0 || ip.find("172.17.") == 0 ||
           ip.find("172.18.") == 0 || ip.find("172.19.") == 0 ||
           ip.find("172.2") == 0 || ip.find("172.3") == 0)) ||
         ip.find("169.254.") == 0;
}

PingStats runPingTest(const std::string& host, int numPings, int timeoutMs) {
  PingStats stats;
  stats.targetHost = host;
  stats.targetIp = resolveHostname(host);
  stats.sentPackets = 0;
  stats.receivedPackets = 0;
  stats.packetLossPercent = 0.0;
  stats.minLatencyMs = std::numeric_limits<double>::max();
  stats.maxLatencyMs = 0.0;
  stats.avgLatencyMs = 0.0;
  stats.jitterMs = 0.0;

  // Early check for cancellation
  if (g_cancelNetworkTest.load()) {
    return stats;
  }

  if (stats.targetIp.empty()) {
    LOG_ERROR << "Failed to resolve hostname: [hostname hidden for privacy]";
    stats.packetLossPercent = 100.0;
    stats.minLatencyMs = 0.0;
    return stats;
  }

  // Get primary adapter to use for testing
  NetworkAdapterInfo primaryAdapter = getPrimaryAdapter();
  if (primaryAdapter.ipAddress.empty()) {
    LOG_ERROR << "No valid network adapter found for testing";
    stats.packetLossPercent = 100.0;
    stats.minLatencyMs = 0.0;
    return stats;
  }

  // Initialize ICMP handle
  HANDLE hIcmp = IcmpCreateFile();
  if (hIcmp == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    LOG_ERROR << "Failed to create ICMP handle for " << host << ". Error code: " << error;
    stats.packetLossPercent = 100.0;
    stats.minLatencyMs = 0.0;
    return stats;
  }

  // IP_SOURCE_ADDRESS must be in network byte order
  struct sockaddr_in sourceAddr;
  sourceAddr.sin_family = AF_INET;
  inet_pton(AF_INET, primaryAdapter.ipAddress.c_str(), &sourceAddr.sin_addr);
  sourceAddr.sin_port = 0;

  IP_OPTION_INFORMATION ipOptions = {0};
  ipOptions.Ttl = 128;

  // Allocate memory for ICMP echo reply
  const int ICMP_BUFFER_SIZE = 32;  // Ping packet size
  const int REPLY_BUFFER_SIZE = sizeof(ICMP_ECHO_REPLY) + ICMP_BUFFER_SIZE + 16;
  unsigned char* replyBuffer = new unsigned char[REPLY_BUFFER_SIZE];
  unsigned char sendBuffer[ICMP_BUFFER_SIZE] = {0};

  // Create a timestamp and sequence counter to make each ping unique
  std::mt19937 rng(static_cast<unsigned>(
    std::chrono::steady_clock::now().time_since_epoch().count()));

  // Convert target IP to in_addr structure
  IN_ADDR targetAddr;
  inet_pton(AF_INET, stats.targetIp.c_str(), &targetAddr);

  double totalLatency = 0.0;
  std::vector<double> latencies;

  const int PING_DELAY_MS = 200;  // Delay between pings
  const int MAX_RETRIES = 2;      // Number of retries per ping

  for (int i = 0; i < numPings && !g_cancelNetworkTest.load(); i++) {
    bool pingSuccess = false;

    // Fill send buffer with unique pattern for each ping
    for (int j = 0; j < ICMP_BUFFER_SIZE; j++) {
      sendBuffer[j] = static_cast<unsigned char>(rng() & 0xFF);
    }
    // Add sequence number to make each ping unique
    *reinterpret_cast<int*>(sendBuffer) = i;

    // Try each ping up to MAX_RETRIES+1 times
    for (int retry = 0; retry <= MAX_RETRIES && !pingSuccess; retry++) {
      stats.sentPackets++;

      // Clear reply buffer before each send
      memset(replyBuffer, 0, REPLY_BUFFER_SIZE);

      // Use IcmpSendEcho2, which allows binding to our adapter's IP
      DWORD replyCount = IcmpSendEcho2Ex(
        hIcmp,                                // HANDLE IcmpHandle
        NULL,                                 // HANDLE Event
        NULL,                                 // PIO_APC_ROUTINE ApcRoutine
        NULL,                                 // PVOID ApcContext
        sourceAddr.sin_addr.s_addr,           // IPAddr SourceAddress
        targetAddr.S_un.S_addr,               // IPAddr DestinationAddress
        sendBuffer,                           // LPVOID RequestData
        static_cast<WORD>(ICMP_BUFFER_SIZE),  // WORD RequestSize
        &ipOptions,         // PIP_OPTION_INFORMATION RequestOptions
        replyBuffer,        // LPVOID ReplyBuffer
        REPLY_BUFFER_SIZE,  // DWORD ReplySize
        timeoutMs           // DWORD Timeout
      );

      if (replyCount > 0) {
        PICMP_ECHO_REPLY echoReply = (PICMP_ECHO_REPLY)replyBuffer;
        double latency = static_cast<double>(echoReply->RoundTripTime);

        // Handle 0ms ping results - silently adjust to 0.5ms
        if (latency == 0.0) {
          latency = 0.5;  // Adjust to 0.5ms
        }

        const bool isLocal = isLocalOrPrivateIP(stats.targetIp);

        // Handle unrealistic latency values
        if (latency < 0.0 || (latency < 0.5 && !isLocal)) {
          if (retry < MAX_RETRIES) {
            continue;
          }

          // Skip this response if it's unrealistic
          if (latency <= 0.0) continue;
        }

        pingSuccess = true;
        stats.receivedPackets++;
        totalLatency += latency;
        latencies.push_back(latency);

        if (latency < stats.minLatencyMs) stats.minLatencyMs = latency;
        if (latency > stats.maxLatencyMs) stats.maxLatencyMs = latency;
      }

      // Only delay between attempts if retrying
      if (!pingSuccess && retry < MAX_RETRIES) {
        std::this_thread::sleep_for(
          std::chrono::milliseconds(PING_DELAY_MS / 2));
      }
    }

    // Delay between pings to avoid rate limiting
    if (i < numPings - 1) {
      std::this_thread::sleep_for(std::chrono::milliseconds(PING_DELAY_MS));
    }
  }

  // Close ICMP handle and free memory
  IcmpCloseHandle(hIcmp);
  delete[] replyBuffer;

  // Calculate statistics only if we got responses
  if (!latencies.empty()) {
    stats.avgLatencyMs = totalLatency / stats.receivedPackets;
    stats.latencyValues = latencies;

    // Calculate jitter (average deviation from mean)
    if (latencies.size() > 1) {
      double sum = 0.0;
      for (double latency : latencies) {
        sum += std::abs(latency - stats.avgLatencyMs);
      }
      stats.jitterMs = sum / latencies.size();
    }
  } else {
    // No valid responses received
    stats.minLatencyMs = 0.0;
    stats.maxLatencyMs = 0.0;
  }

  // Calculate packet loss percentage
  stats.packetLossPercent =
    100.0 - ((double)stats.receivedPackets / numPings * 100.0);

  return stats;
}

std::vector<NetworkAdapterInfo> getNetworkAdapters() {
  std::vector<NetworkAdapterInfo> adapters;

  // Get adapter addresses
  ULONG size = 15000;  // Initial buffer size
  PIP_ADAPTER_ADDRESSES adapterAddresses = (PIP_ADAPTER_ADDRESSES)malloc(size);

  if (adapterAddresses == NULL) {
    return adapters;
  }

  ULONG result =
    GetAdaptersAddresses(AF_INET,                  // Only IPv4 adapters
                         GAA_FLAG_INCLUDE_PREFIX,  // Include subnet masks
                         NULL, adapterAddresses, &size);

  // Retry with the correct buffer size if needed
  if (result == ERROR_BUFFER_OVERFLOW) {
    free(adapterAddresses);
    adapterAddresses = (PIP_ADAPTER_ADDRESSES)malloc(size);

    if (adapterAddresses == NULL) {
      return adapters;
    }

    result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL,
                                  adapterAddresses, &size);
  }

  // Process each adapter
  if (result == NO_ERROR) {
    for (PIP_ADAPTER_ADDRESSES adapter = adapterAddresses; adapter != NULL;
         adapter = adapter->Next) {
      // Skip non-operational adapters
      if (adapter->OperStatus != IfOperStatusUp) continue;

      // Skip loopback adapter
      if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

      NetworkAdapterInfo info;
      info.adapterName = adapter->AdapterName;

      // Convert wide string description to narrow string
      std::wstring wDescription(adapter->Description);
      info.description = WideToNarrow(wDescription);

      // Check if this is a WiFi adapter
      info.isWifi = (adapter->IfType == IF_TYPE_IEEE80211);

      // Format MAC address
      std::stringstream macStream;
      for (UINT i = 0; i < adapter->PhysicalAddressLength; i++) {
        if (i > 0) macStream << "-";
        macStream << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(adapter->PhysicalAddress[i]);
      }
      info.macAddress = macStream.str();

      // Get IP address
      PIP_ADAPTER_UNICAST_ADDRESS address = adapter->FirstUnicastAddress;
      if (address != NULL) {
        sockaddr_in* sockaddr =
          reinterpret_cast<sockaddr_in*>(address->Address.lpSockaddr);
        char ipBuffer[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sockaddr->sin_addr, ipBuffer, INET_ADDRSTRLEN);
        info.ipAddress = ipBuffer;
      }

      // Get link speed
      info.linkSpeedMbps = static_cast<double>(adapter->TransmitLinkSpeed) /
                           1000000;  // Convert to Mbps

      // Check DHCP status
      info.isDhcpEnabled = (adapter->Flags & IP_ADAPTER_DHCP_ENABLED) != 0;

      adapters.push_back(info);
    }
  }

  free(adapterAddresses);
  return adapters;
}

NetworkAdapterInfo getPrimaryAdapter() {
  auto adapters = getNetworkAdapters();

  // Find the adapter with the default route
  NetworkAdapterInfo primary;

  // Enhanced keywords to identify virtual/VPN adapters
  const std::vector<std::string> virtualKeywords = {
    "NordLynx",   "VPN",     "Virtual",   "Tunnel",  "TAP",       "TUN",
    "Nord",       "OpenVPN", "WireGuard", "Hamachi", "SoftEther", "Express",
    "Cyber",      "Ghost",   "Proton",    "Surf",    "Private",   "IPVanish",
    "Mullvad",    "Adapter", "VMware",    "Hyper-V", "Tunnel",    "Pseudo",
    "VirtualBox", "NDIS"};

  // Also check IP address patterns - VPN adapters often use specific ranges
  const std::vector<std::string> vpnIpPrefixes = {
    "10.5.",  "10.8.", "10.9.",  // Common NordVPN
    "10.10.",                    // Common ExpressVPN
    "10.15.",                    // Common Private Internet Access
    "10.31."                     // Common for some other VPNs
  };

  std::vector<NetworkAdapterInfo> physicalAdapters;
  std::vector<NetworkAdapterInfo> vpnAdapters;

  LOG_INFO << "Network adapters detected:";

  // First pass: categorize adapters as physical or VPN
  for (const auto& adapter : adapters) {
    bool isVirtual = false;

    // Check adapter name against VPN keywords
    for (const auto& keyword : virtualKeywords) {
      if (adapter.description.find(keyword) != std::string::npos) {
        isVirtual = true;
        break;
      }
    }

    // Check IP address against common VPN patterns
    for (const auto& prefix : vpnIpPrefixes) {
      if (adapter.ipAddress.find(prefix) == 0) {
        isVirtual = true;
        break;
      }
    }

    LOG_INFO << "- " << adapter.description << " ([IP hidden for privacy], " << (adapter.isWifi ? "WiFi" : "Wired") << (isVirtual ? ", VIRTUAL/VPN" : "") << ")";

    if (isVirtual) {
      vpnAdapters.push_back(adapter);
    } else {
      physicalAdapters.push_back(adapter);
    }
  }

  // Bind to specific adapter using IP_PKTINFO socket option
  if (!physicalAdapters.empty()) {
    // First priority: Wired connections with non-private IPs
    for (const auto& adapter : physicalAdapters) {
      if (adapter.isWifi) continue;

      bool hasPrivateIp = isLocalOrPrivateIP(adapter.ipAddress);
      if (!hasPrivateIp) {
        LOG_INFO << "Selected wired adapter with public IP: " << adapter.description << " ([IP hidden for privacy])";
        return adapter;
      }
    }

    // Second priority: Any wired connections
    for (const auto& adapter : physicalAdapters) {
      if (!adapter.isWifi) {
        LOG_INFO << "Selected wired adapter with private IP: " << adapter.description << " ([IP hidden for privacy])";
        return adapter;
      }
    }

    // Third priority: WiFi connections
    for (const auto& adapter : physicalAdapters) {
      if (adapter.isWifi) {
        LOG_INFO << "Selected WiFi adapter: " << adapter.description << " ([IP hidden for privacy])";
        return adapter;
      }
    }

    LOG_INFO << "Selected first available physical adapter: " << physicalAdapters[0].description << " ([IP hidden for privacy])";
    return physicalAdapters[0];
  }

  if (!vpnAdapters.empty()) {
    LOG_WARN << "No physical adapters found. Using VPN adapter: " << vpnAdapters[0].description << " ([IP hidden for privacy]). Network test results may not reflect your true connection quality.";
    return vpnAdapters[0];
  }

  LOG_ERROR << "No usable network adapters found!";
  return primary;
}

struct BufferbloatResult {
  double baselineLatencyMs;
  double downloadLatencyMs;
  double uploadLatencyMs;
  double downloadBloatPercent;
  double uploadBloatPercent;
  bool isSignificant;
};

// Improved bufferbloat test with upload testing
bool testForBufferbloat(NetworkMetrics& metrics, int testDurationSeconds) {
  // Early check for cancellation
  if (g_cancelNetworkTest.load()) {
    return false;
  }

  // Find the best regional server for testing instead of DNS servers
  LOG_INFO << "Starting bufferbloat test...";
  LOG_INFO << "Finding reliable regional ping target...";

  // Get the server list
  auto servers = getRegionalServerList();

  // First, try regional servers, prioritizing EU, NA, then OCE
  std::vector<std::string> regionPriority = {"EU", "USA", "Oceania"};
  std::string pingTarget;
  bool foundTarget = false;
  double bestLatency = 1000.0;  // Initialize with a high value

  // First try: find a server in one of the priority regions
  for (const auto& region : regionPriority) {
    for (const auto& server : servers) {
      if (server.region.find(region) != std::string::npos) {
        // Test this server
        PingStats quickTest = runPingTest(server.hostname, 3, 1000);
        if (quickTest.receivedPackets > 0 && quickTest.avgLatencyMs > 5.0 &&
            quickTest.avgLatencyMs < bestLatency) {
          pingTarget = server.hostname;
          foundTarget = true;
          bestLatency = quickTest.avgLatencyMs;
          LOG_INFO << "Found potential target: " << pingTarget << " with latency: " << bestLatency << "ms";
        }
      }
    }

    // If we found a good server in this region, stop looking
    if (foundTarget) {
      LOG_INFO << "Using " << pingTarget << " for bufferbloat testing with stable latency of " << bestLatency << "ms.";
      break;
    }
  }

  // If regional servers failed, fall back to DNS servers
  if (!foundTarget) {
    std::vector<std::string> fallbackServers = {
      "8.8.8.8",  // Google DNS
      "1.1.1.1"   // Cloudflare DNS
    };

    for (const auto& server : fallbackServers) {
      PingStats quickTest = runPingTest(server, 3, 1000);
      if (quickTest.receivedPackets > 0) {
        pingTarget = server;
        foundTarget = true;
        LOG_INFO << "Using fallback DNS server " << pingTarget << " for bufferbloat testing.";
        break;
      }
    }
  }

  if (!foundTarget) {
    LOG_ERROR << "Could not find a reliable ping target. Aborting bufferbloat test.";
    return false;
  }

  // Rest of existing function continues from here...
  // Test parameters
  constexpr int baselinePingCount = 10;
  constexpr int loadPingCount = 15;
  constexpr int timeoutMs = 1000;

  // Add overall timeout protection
  const auto startTime = std::chrono::steady_clock::now();
  const auto maxTestDuration =
    std::chrono::seconds(std::min(testDurationSeconds, 30));

  // Measure baseline latency with no network load
  LOG_INFO << "Measuring baseline latency...";
  PingStats baseline = runPingTest(pingTarget, baselinePingCount, timeoutMs);

  if (baseline.receivedPackets == 0) {
    LOG_ERROR << "Baseline ping test failed. Aborting bufferbloat test.";
    return false;
  }

  double baselineLatency = baseline.avgLatencyMs;
  LOG_INFO << "Baseline latency: " << baselineLatency << " ms";

  // Create results structure for detailed metrics
  BufferbloatResult result;
  result.baselineLatencyMs = baselineLatency;
  result.downloadLatencyMs = baselineLatency;
  result.uploadLatencyMs = baselineLatency;
  result.downloadBloatPercent = 0.0;
  result.uploadBloatPercent = 0.0;
  result.isSignificant = false;

  // ============= DOWNLOAD BUFFERBLOAT TEST =============
  LOG_INFO << "Testing download bufferbloat...";

  // Start a separate thread to generate download traffic
  std::atomic<bool> stopDownloadLoad(false);
  std::atomic<bool> downloadStarted(false);

  auto downloadGenerator =
    std::async(std::launch::async, [&stopDownloadLoad, &downloadStarted]() {
      // Use multiple reliable download sources for consistent testing
      const std::vector<std::string> downloadUrls = {
        "http://speedtest.ftp.otenet.gr/files/test100k.db",
        "http://ipv4.download.thinkbroadband.com/5MB.zip",
        "http://speedtest-ny.turnkeyinternet.net/10mb.bin"};

      HINTERNET hInternet = InternetOpenA(
        "BufferBloat Test", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
      if (!hInternet) return;

      // Signal that we've started trying to download
      downloadStarted = true;

      int downloadCount = 0;
      while (!stopDownloadLoad && downloadCount < 15) {  // Increased iterations
        for (const auto& url : downloadUrls) {
          if (stopDownloadLoad) break;

          HINTERNET hConnect = InternetOpenUrlA(
            hInternet, url.c_str(), NULL, 0,
            INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);

          if (hConnect) {
            char buffer[8192];  // Increased buffer for faster downloads
            DWORD bytesRead;

            // Download with high throughput
            int readCount = 0;
            while (!stopDownloadLoad && readCount < 5000) {
              if (!InternetReadFile(hConnect, buffer, sizeof(buffer),
                                    &bytesRead)) {
                break;
              }
              if (bytesRead == 0) break;
              readCount++;
            }

            InternetCloseHandle(hConnect);
          }
        }
        downloadCount++;
      }

      InternetCloseHandle(hInternet);
    });

  // Wait for download to start with timeout
  auto waitStart = std::chrono::steady_clock::now();
  while (!downloadStarted) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (std::chrono::steady_clock::now() - waitStart >
        std::chrono::seconds(3)) {
      LOG_ERROR << "Download didn't start in time. Aborting bufferbloat test.";
      stopDownloadLoad = true;
      if (downloadGenerator.valid()) {
        downloadGenerator.wait();
      }
      return false;
    }
  }

  // Give download time to saturate the connection
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Measure latency under download load
  PingStats downloadLoad = runPingTest(pingTarget, loadPingCount, timeoutMs);

  // Stop the download generator
  stopDownloadLoad = true;
  if (downloadGenerator.valid()) {
    if (downloadGenerator.wait_for(std::chrono::seconds(3)) !=
        std::future_status::ready) {
      LOG_WARN << "Download generator task did not complete gracefully.";
    }
  }

  // Allow network to recover
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Calculate download metrics
  if (downloadLoad.receivedPackets > 0) {
    result.downloadLatencyMs = downloadLoad.avgLatencyMs;
    double downloadDiff = result.downloadLatencyMs - result.baselineLatencyMs;
    result.downloadBloatPercent =
      (downloadDiff / result.baselineLatencyMs) * 100.0;

    std::string downloadMsg = "Download latency: " + std::to_string(result.downloadLatencyMs) + " ms";
    if (downloadDiff >= 0) {
      downloadMsg += " (+" + std::to_string(result.downloadBloatPercent) + "%)";
    }
    LOG_INFO << downloadMsg;
  } else {
    LOG_WARN << "Download test failed to get ping responses. Skipping upload test.";

    // Update metrics with what we have
    metrics.baselineLatencyMs = result.baselineLatencyMs;
    metrics.downloadLatencyMs = 0;
    metrics.possibleBufferbloat = false;
    metrics.bufferbloatTestCompleted = true;
    return false;
  }

  // Check timeout before upload test
  bool skipUploadTest = false;
  if (std::chrono::steady_clock::now() - startTime > maxTestDuration) {
    LOG_WARN << "Bufferbloat test timeout exceeded before upload test. Skipping.";
    skipUploadTest = true;
  }

  // ============= UPLOAD BUFFERBLOAT TEST =============
  if (!skipUploadTest) {
    LOG_INFO << "Testing upload bufferbloat...";

    std::atomic<bool> stopUploadLoad(false);
    std::atomic<bool> uploadStarted(false);

    auto uploadGenerator = std::async(std::launch::async, [&stopUploadLoad,
                                                           &uploadStarted]() {
      // Reliable upload endpoints
      const std::vector<std::string> uploadUrls = {
        "https://httpbin.org/post", "https://postman-echo.com/post"};

      HINTERNET hInternet = InternetOpenA(
        "BufferBloat Upload Test", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
      if (!hInternet) return;

      // Signal that upload test has started
      uploadStarted = true;

      // Create large buffer to upload (1MB)
      const int UPLOAD_BUFFER_SIZE =
        1024 * 1024;  // Increased for better saturation
      std::vector<char> uploadBuffer(UPLOAD_BUFFER_SIZE);

      // Fill with random data
      std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
      for (int i = 0; i < UPLOAD_BUFFER_SIZE; i++) {
        uploadBuffer[i] = static_cast<char>(rng() & 0xFF);
      }

      int uploadCount = 0;
      while (!stopUploadLoad && uploadCount < 15) {  // Increased iterations
        for (const auto& url : uploadUrls) {
          if (stopUploadLoad) break;

          URL_COMPONENTS urlComponents = {0};
          urlComponents.dwStructSize = sizeof(urlComponents);
          urlComponents.dwHostNameLength = 1;
          urlComponents.dwUrlPathLength = 1;

          // Parse the URL
          WCHAR wideUrl[2048];
          MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wideUrl, 2048);

          if (!InternetCrackUrlW(wideUrl, 0, 0, &urlComponents)) {
            continue;
          }

          // Extract hostname and path
          WCHAR hostname[1024] = {0};
          wcsncpy_s(hostname, urlComponents.lpszHostName,
                    urlComponents.dwHostNameLength);

          WCHAR path[2048] = {0};
          wcsncpy_s(path, urlComponents.lpszUrlPath,
                    urlComponents.dwUrlPathLength);

          // Connect to the server
          HINTERNET hConnect =
            InternetConnectW(hInternet, hostname, urlComponents.nPort, NULL,
                             NULL, INTERNET_SERVICE_HTTP, 0, 0);

          if (hConnect) {
            // Open request
            HINTERNET hRequest =
              HttpOpenRequestW(hConnect, L"POST", path, NULL, NULL, NULL,
                               INTERNET_FLAG_NO_CACHE_WRITE |
                                 (urlComponents.nScheme == INTERNET_SCHEME_HTTPS
                                    ? INTERNET_FLAG_SECURE
                                    : 0),
                               0);

            if (hRequest) {
              // Set headers
              const char* headers = "Content-Type: application/octet-stream\r\n"
                                    "Connection: Keep-Alive\r\n";

              HttpAddRequestHeadersA(hRequest, headers, -1,
                                     HTTP_ADDREQ_FLAG_ADD);

              // Send request with larger payload
              BOOL success = HttpSendRequest(
                hRequest, NULL, 0, &uploadBuffer[0], UPLOAD_BUFFER_SIZE);

              // Read response
              if (success) {
                char responseBuffer[4096];
                DWORD bytesRead;

                while (InternetReadFile(hRequest, responseBuffer,
                                        sizeof(responseBuffer), &bytesRead) &&
                       bytesRead > 0) {
                  // Just consume the response
                }
              }

              InternetCloseHandle(hRequest);
            }

            InternetCloseHandle(hConnect);
          }
        }

        uploadCount++;
      }

      InternetCloseHandle(hInternet);
    });

    // Wait for upload to start with timeout
    waitStart = std::chrono::steady_clock::now();
    while (!uploadStarted) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (std::chrono::steady_clock::now() - waitStart >
          std::chrono::seconds(3)) {
        LOG_WARN << "Upload test couldn't start. Skipping upload test.";
        stopUploadLoad = true;
        if (uploadGenerator.valid()) {
          uploadGenerator.wait();
        }
        skipUploadTest = true;
        break;
      }
    }

    if (!skipUploadTest) {
      // Give upload time to saturate the connection
      std::this_thread::sleep_for(std::chrono::seconds(1));

      // Measure latency under upload load
      PingStats uploadLoad = runPingTest(pingTarget, loadPingCount, timeoutMs);

      // Stop the upload generator
      stopUploadLoad = true;
      if (uploadGenerator.valid()) {
        if (uploadGenerator.wait_for(std::chrono::seconds(3)) !=
            std::future_status::ready) {
          LOG_WARN << "Upload generator task did not complete gracefully.";
        }
      }

      // Calculate upload metrics
      if (uploadLoad.receivedPackets > 0) {
        result.uploadLatencyMs = uploadLoad.avgLatencyMs;
        double uploadDiff = result.uploadLatencyMs - result.baselineLatencyMs;
        result.uploadBloatPercent =
          (uploadDiff / result.baselineLatencyMs) * 100.0;

        std::string uploadMsg = "Upload latency: " + std::to_string(result.uploadLatencyMs) + " ms";
        if (uploadDiff >= 0) {
          uploadMsg += " (+" + std::to_string(result.uploadBloatPercent) + "%)";
        }
        LOG_INFO << uploadMsg;
      } else {
        LOG_WARN << "Upload test failed to get ping responses.";
        result.uploadLatencyMs = 0;
      }
    }
  }

  // Analyze bufferbloat severity for both directions
  double worstBloatPercent = result.downloadBloatPercent;
  double worstBloatMs = result.downloadLatencyMs - result.baselineLatencyMs;
  std::string direction = "download";

  // Check if upload bloat is worse
  if (!skipUploadTest && result.uploadLatencyMs > 0) {
    double uploadBloatMs = result.uploadLatencyMs - result.baselineLatencyMs;

    if (result.uploadBloatPercent > worstBloatPercent ||
        uploadBloatMs > worstBloatMs) {
      worstBloatPercent = result.uploadBloatPercent;
      worstBloatMs = uploadBloatMs;
      direction = "upload";
    }
  }

  // Bufferbloat is significant if latency increases by >100% AND by >50ms under
  // load
  result.isSignificant = (worstBloatPercent > 100.0 && worstBloatMs > 50.0);

  // Update metrics object with detailed results
  metrics.possibleBufferbloat = result.isSignificant;
  metrics.baselineLatencyMs = result.baselineLatencyMs;
  metrics.downloadLatencyMs = result.downloadLatencyMs;
  metrics.uploadLatencyMs = result.uploadLatencyMs;
  metrics.downloadBloatPercent = result.downloadBloatPercent;
  metrics.uploadBloatPercent = result.uploadBloatPercent;
  metrics.bufferbloatDirection = direction;
  metrics.bufferbloatTestCompleted = true;

  LOG_INFO << "Bufferbloat test completed. Significant: " << (metrics.possibleBufferbloat ? "Yes" : "No");
  LOG_INFO << "Most affected direction: " << direction;

  return metrics.possibleBufferbloat;
}

NetworkMetrics runNetworkDiagnostics(int pingCount, int timeoutMs,
                                     bool includeBufferbloat,
                                     int bufferbloatDuration) {
  NetworkMetrics metrics;

  // Reset cancellation flag at start of test
  resetCancelFlag();

  // Initialize Winsock at the start
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    LOG_ERROR << "WSAStartup failed with error code: " << WSAGetLastError();
    metrics.networkIssues = "Failed to initialize Windows networking";
    metrics.hasPacketLoss = true;
    return metrics;
  }

  // Get network adapter information
  metrics.activeAdapters = getNetworkAdapters();
  NetworkAdapterInfo primaryAdapter = getPrimaryAdapter();
  metrics.onWifi = primaryAdapter.isWifi;
  metrics.primaryAdapter = primaryAdapter;  // Store the primary adapter info

  // Test regional servers only - using our specific server list
  auto servers = getRegionalServerList();

  // Track results by region
  std::map<std::string, std::vector<PingStats>> regionalResults;

  // Count servers with packet loss for improved detection
  int serversWithPacketLoss = 0;

  LOG_INFO << "Running ping tests to server list using " << primaryAdapter.description << "...";

  // Test each server
  for (const auto& server : servers) {
    if (g_cancelNetworkTest.load()) {
      break;
    }

    LOG_INFO << "Testing " << server.hostname << " (" << server.region << ")...";
    PingStats stats = runPingTest(server.hostname, pingCount, timeoutMs);
    stats.region = server.region;  // Add the region to the stats

    // Store both in the main list and regional lists
    metrics.pingResults.push_back(stats);
    regionalResults[server.region].push_back(stats);

    // Update issues flags
    if (stats.receivedPackets > 0) {  // Only if we got any response
      if (stats.avgLatencyMs > 100) {
        metrics.hasHighLatency = true;
      }
      if (stats.jitterMs > 20) {
        metrics.hasHighJitter = true;
      }
      // Count servers with packet loss instead of immediately setting flag
      if (stats.packetLossPercent > 1.0) {
        serversWithPacketLoss++;
      }
    }
  }

  // Only set packet loss flag if 2 or more servers have issues
  metrics.hasPacketLoss = (serversWithPacketLoss >= 2);

  // Calculate regional averages and store them in the metrics
  metrics.regionalLatencies.clear();
  for (const auto& region : regionalResults) {
    double totalLatency = 0.0;
    int validServers = 0;

    for (const auto& stats : region.second) {
      if (stats.receivedPackets > 0) {
        totalLatency += stats.avgLatencyMs;
        validServers++;
      }
    }

    if (validServers > 0) {
      metrics.regionalLatencies[region.first] = totalLatency / validServers;
    }
  }

  // Run bufferbloat test if requested
  if (!g_cancelNetworkTest.load() && includeBufferbloat) {
    testForBufferbloat(metrics, bufferbloatDuration);
  }

  // Generate text summary of issues
  std::stringstream issues;
  if (metrics.hasHighLatency) {
    issues << "High latency detected. ";
  }
  if (metrics.hasHighJitter) {
    issues << "Inconsistent latency (jitter) detected. ";
  }
  if (metrics.hasPacketLoss) {
    issues << "Packet loss detected. ";
  }
  if (metrics.possibleBufferbloat) {
    issues << "Possible bufferbloat detected. ";
  }
  if (metrics.onWifi) {
    issues << "Using WiFi connection (consider wired for better stability). ";
  }

  if (issues.str().empty()) {
    metrics.networkIssues = "No significant network issues detected.";
  } else {
    metrics.networkIssues = issues.str();
  }

  // Clean up when done
  WSACleanup();

  return metrics;
}

std::string formatNetworkResults(const NetworkMetrics& metrics) {
  std::stringstream ss;
  ss << "===== Network Diagnostics Results =====\n\n";

  // Connection type info
  ss << "Connection Type: " << (metrics.onWifi ? "WiFi" : "Wired Ethernet")
     << "\n";

  // Primary adapter details
  if (!metrics.activeAdapters.empty()) {
    const auto& primary = metrics.activeAdapters[0];
    ss << "Network Adapter: " << primary.description << "\n";
    ss << "IP Address: [IP hidden for privacy]\n";
    ss << "Link Speed: " << primary.linkSpeedMbps << " Mbps\n\n";
  }

  // Router ping results
  bool routerPingShown = false;
  for (const auto& ping : metrics.pingResults) {
    if (ping.targetIp == metrics.routerIp) {
      ss << "Router Connectivity:\n";
      ss << "  IP: [IP hidden for privacy]\n";
      ss << "  Latency: " << std::fixed << std::setprecision(1)
         << ping.avgLatencyMs << " ms\n";
      ss << "  Packet Loss: " << ping.packetLossPercent << "%\n\n";
      routerPingShown = true;
      break;
    }
  }

  // Internet connectivity results
  ss << "Internet Connectivity:\n";
  for (const auto& ping : metrics.pingResults) {
    // Skip the router ping as it's already shown
    if (ping.targetIp == metrics.routerIp && routerPingShown) {
      continue;
    }

    ss << "  Target: " << ping.targetHost;
    if (ping.targetHost != ping.targetIp) {
      ss << " ([IP hidden for privacy])";
    }
    ss << "\n";
    ss << "    Latency: " << std::fixed << std::setprecision(1)
       << ping.avgLatencyMs << " ms";
    ss << " (min: " << ping.minLatencyMs << " ms, max: " << ping.maxLatencyMs
       << " ms)\n";
    ss << "    Jitter: " << std::fixed << std::setprecision(1) << ping.jitterMs
       << " ms\n";
    ss << "    Packet Loss: " << ping.packetLossPercent << "%\n";
  }

  // Bufferbloat results
  if (metrics.possibleBufferbloat) {
    ss << "\n⚠️ Bufferbloat Detected: Your network shows signs of latency under "
          "load.\n";
    ss << "   Baseline latency: " << std::fixed << std::setprecision(1)
       << metrics.baselineLatencyMs << " ms\n";
    ss << "   Download latency: " << std::fixed << std::setprecision(1)
       << metrics.downloadLatencyMs << " ms (+" << std::fixed
       << std::setprecision(1) << metrics.downloadBloatPercent << "%)\n";
    ss << "   Upload latency: " << std::fixed << std::setprecision(1)
       << metrics.uploadLatencyMs << " ms (+" << std::fixed
       << std::setprecision(1) << metrics.uploadBloatPercent << "%)\n";
    ss << "   Most affected: "
       << (metrics.bufferbloatDirection == "upload" ? "Upload traffic"
                                                    : "Download traffic")
       << "\n";
    ss << "   This may cause lag spikes during gaming or video calls.\n";
  } else {
    ss << "\n✓ No bufferbloat detected: Your network maintains good latency "
          "under load.\n";
    if (metrics.baselineLatencyMs > 0) {
      ss << "   Baseline latency: " << std::fixed << std::setprecision(1)
         << metrics.baselineLatencyMs << " ms\n";
      ss << "   Under load: " << std::fixed << std::setprecision(1)
         << std::max(metrics.downloadLatencyMs, metrics.uploadLatencyMs)
         << " ms\n";
    }
  }

  // Overall network health assessment
  ss << "\nNetwork Performance Summary:\n";

  if (!metrics.networkIssues.empty()) {
    ss << metrics.networkIssues << "\n";
  }

  if (metrics.hasHighLatency || metrics.hasHighJitter ||
      metrics.hasPacketLoss || metrics.possibleBufferbloat) {
    ss << "\nRecommendations:\n";

    if (metrics.onWifi) {
      ss << "• Consider using a wired Ethernet connection instead of WiFi\n";
      ss << "• Position your device closer to the WiFi router\n";
      ss << "• Check for interference from other wireless devices\n";
    }

    if (metrics.hasHighLatency) {
      ss << "• Contact your ISP about high latency issues\n";
      ss << "• Try connecting at non-peak hours\n";
    }

    if (metrics.hasPacketLoss) {
      ss << "• Check your network cables and connections\n";
      ss << "• Restart your router and modem\n";
      ss << "• Contact your ISP about packet loss issues\n";
    }

    if (metrics.possibleBufferbloat) {
      ss << "• Enable QoS (Quality of Service) settings on your router\n";
      ss << "• Look for 'Smart Queue Management' or 'SQM' settings\n";
      ss << "• Limit your upload/download speeds slightly below maximum\n";
    }
  } else {
    ss << "✓ Your network appears to be performing well for online gaming\n";
  }

  return ss.str();
}

// Update the enhanced report function

std::string formatEnhancedNetworkResults(const NetworkMetrics& metrics) {
  std::stringstream ss;
  ss << "===== NETWORK DIAGNOSTICS SUMMARY =====\n\n";

  // CONNECTION INFO (remains mostly the same)
  ss << "CONNECTION INFO\n";
  ss << "---------------\n";
  ss << "Connection Type: " << (metrics.onWifi ? "WiFi" : "Wired Ethernet")
     << "\n";

  if (!metrics.activeAdapters.empty()) {
    const auto& primary = metrics.activeAdapters[0];
    ss << "Adapter: " << primary.description << "\n";
    ss << "IP Address: [IP hidden for privacy]\n";
    ss << "Link Speed: " << primary.linkSpeedMbps << " Mbps\n";
  }

  // Count valid and failed connections
  int validConnections = 0;
  int failedConnections = 0;
  double totalLatency = 0.0;
  double minLatency = std::numeric_limits<double>::max();
  double maxLatency = 0.0;
  double totalJitter = 0.0;
  double totalPacketLoss = 0.0;
  double worstPacketLoss = 0.0;

  for (const auto& ping : metrics.pingResults) {
    // Skip router ping when calculating internet stats
    if (ping.targetIp == metrics.routerIp) continue;

    if (ping.receivedPackets > 0) {
      validConnections++;
      totalLatency += ping.avgLatencyMs;
      totalJitter += ping.jitterMs;

      if (ping.avgLatencyMs < minLatency) minLatency = ping.avgLatencyMs;
      if (ping.avgLatencyMs > maxLatency) maxLatency = ping.avgLatencyMs;

      // Only count packet loss for connections that responded at least once
      totalPacketLoss += ping.packetLossPercent;
      if (ping.packetLossPercent > worstPacketLoss)
        worstPacketLoss = ping.packetLossPercent;
    } else {
      failedConnections++;
    }
  }

  // Calculate averages for servers that had at least one response
  double avgLatency =
    validConnections > 0 ? totalLatency / validConnections : 0.0;
  double avgJitter =
    validConnections > 0 ? totalJitter / validConnections : 0.0;
  double avgPacketLoss =
    validConnections > 0 ? totalPacketLoss / validConnections : 0.0;

  ss << "\n";

  // CONNECTIVITY SUMMARY (enhanced)
  ss << "CONNECTIVITY SUMMARY\n";
  ss << "-------------------\n";
  ss << "Completely failed connections: " << failedConnections << "\n";
  ss << "Successful connections: " << validConnections << "\n";
  ss << "Average packet loss: " << std::fixed << std::setprecision(1)
     << avgPacketLoss << "%\n";
  ss << "Worst packet loss: " << std::fixed << std::setprecision(1)
     << worstPacketLoss << "%\n";

  if (avgPacketLoss > 1.0) {
    ss << "⚠️ Your packet loss is higher than ideal. Values under 1% are "
          "preferred for gaming.\n";
  } else {
    ss << "✓ Your packet loss is within acceptable ranges for online gaming.\n";
  }
  ss << "\n";

  // REGIONAL LATENCY BREAKDOWN (new section)
  ss << "REGIONAL LATENCY SUMMARY\n";
  ss << "------------------------\n";
  if (!metrics.regionalLatencies.empty()) {
    for (const auto& region : metrics.regionalLatencies) {
      ss << region.first << " Region: " << std::fixed << std::setprecision(1)
         << region.second << " ms average\n";
    }
  } else {
    // Global summary if no regional data
    ss << "Global latency: " << std::fixed << std::setprecision(1) << avgLatency
       << " ms average\n";
  }
  ss << "\n";

  // LATENCY DETAILS
  ss << "LATENCY DETAILS\n";
  ss << "---------------\n";
  ss << "Fastest response: " << std::fixed << std::setprecision(1) << minLatency
     << " ms\n";
  ss << "Average latency: " << std::fixed << std::setprecision(1) << avgLatency
     << " ms\n";
  ss << "Slowest response: " << std::fixed << std::setprecision(1) << maxLatency
     << " ms\n";
  ss << "Average jitter: " << std::fixed << std::setprecision(1) << avgJitter
     << " ms\n";

  // Add latency interpretation
  if (avgLatency < 20) {
    ss << "✓ Your average latency is excellent for online gaming.\n";
  } else if (avgLatency < 50) {
    ss << "✓ Your average latency is very good for online gaming.\n";
  } else if (avgLatency < 100) {
    ss << "Your average latency is acceptable for most online games.\n";
  } else {
    ss << "⚠️ Your average latency may cause issues in fast-paced online "
          "games.\n";
  }

  if (avgJitter < 5) {
    ss << "✓ Your connection stability (jitter) is excellent.\n";
  } else if (avgJitter < 15) {
    ss << "✓ Your connection stability (jitter) is good.\n";
  } else {
    ss << "⚠️ Your connection shows inconsistent latency which may cause "
          "stuttering.\n";
  }
  ss << "\n";

  // Add DETAILED SERVER RESULTS section for renderer to parse
  ss << "SERVER CONNECTION DETAILS\n";
  ss << "-----------------------\n";
  for (const auto& ping : metrics.pingResults) {
    if (ping.receivedPackets > 0) {
      ss << "Target: " << ping.targetHost;
      if (!ping.region.empty()) {
        ss << " (" << ping.region << ")";
      }
      ss << "\n";
      ss << "  Latency: " << std::fixed << std::setprecision(1)
         << ping.avgLatencyMs << " ms";
      ss << " (min: " << ping.minLatencyMs << " ms, max: " << ping.maxLatencyMs
         << " ms)\n";
      ss << "  Jitter: " << std::fixed << std::setprecision(1) << ping.jitterMs
         << " ms\n";
      ss << "  Packet Loss: " << ping.packetLossPercent << "%\n\n";
    }
  }

  // BUFFERBLOAT RESULTS (improved interpretation)
  ss << "BUFFERBLOAT RESULTS\n";
  ss << "------------------\n";
  if (metrics.baselineLatencyMs > 0) {
    ss << "Baseline latency: " << std::fixed << std::setprecision(1)
       << metrics.baselineLatencyMs << " ms\n";
    ss << "Upload test latency: " << std::fixed << std::setprecision(1)
       << metrics.uploadLatencyMs << " ms";

    // Handle upload reporting when latency is lower than baseline
    double uploadDiff = metrics.uploadLatencyMs - metrics.baselineLatencyMs;
    if (uploadDiff < 0) {
      ss << " (no increase)\n";
    } else {
      if (metrics.uploadBloatPercent > 100 && uploadDiff > 50) {
        ss << " (⚠️ +" << std::fixed << std::setprecision(1) << uploadDiff
           << " ms, +" << std::fixed << std::setprecision(1)
           << metrics.uploadBloatPercent << "%)\n";
      } else {
        ss << " (+" << std::fixed << std::setprecision(1)
           << metrics.uploadBloatPercent << "%)\n";
      }
    }

    ss << "Download test latency: " << std::fixed << std::setprecision(1)
       << metrics.downloadLatencyMs << " ms";

    // Handle download reporting when latency is lower than baseline
    double downloadDiff = metrics.downloadLatencyMs - metrics.baselineLatencyMs;
    if (downloadDiff < 0) {
      ss << " (no increase)\n";
    } else {
      if (metrics.downloadBloatPercent > 100 && downloadDiff > 50) {
        ss << " (⚠️ +" << std::fixed << std::setprecision(1) << downloadDiff
           << " ms, +" << std::fixed << std::setprecision(1)
           << metrics.downloadBloatPercent << "%)\n";
      } else {
        ss << " (+" << std::fixed << std::setprecision(1)
           << metrics.downloadBloatPercent << "%)\n";
      }
    }

    if (metrics.possibleBufferbloat) {
      ss << "Result: ⚠️ SIGNIFICANT BUFFERBLOAT DETECTED\n";
      ss << "Most affected: "
         << (metrics.bufferbloatDirection == "upload" ? "Upload traffic"
                                                      : "Download traffic")
         << "\n";
      ss << "This can cause lag spikes during gaming when others use your "
            "internet.\n";
    } else {
      ss << "Result: ✓ No significant bufferbloat detected\n";
      ss << "Your connection maintains stable latency under load.\n";
    }
  } else {
    ss << "No bufferbloat test results available\n";
  }

  // Rest of the function remains similar...

  return ss.str();
}

// Add implementation at the bottom of the file

std::vector<ServerInfo> getRegionalServerList() {
  std::vector<ServerInfo> servers = {
    // USA servers
    {"206.71.50.230", "USA (New York)", false},
    {"209.142.68.29", "USA (Chicago)", false},

    // "NEAR" servers (DNS)
    {"8.8.8.8", "NEAR", true},
    {"1.1.1.1", "NEAR", true},

    // EU servers
    {"5.9.24.56", "EU (Germany)", false},
    {"172.232.53.171", "EU (Paris)", false},
    {"172.232.134.84", "EU (Sweden)", false},

    // Oceania servers
    {"139.130.4.5", "Oceania", false},
    {"211.29.132.66", "Oceania", false},
  };

  return servers;
}
}  // namespace NetworkTest
