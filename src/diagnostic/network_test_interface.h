#pragma once
#include <string>

// Forward declarations only - no Windows includes!
namespace NetworkTest {
// Simple result structure that doesn't depend on Windows headers
struct NetworkTestResult {
  std::string formattedOutput;
  bool isWiFi;
  bool hasIssues;
};

// The only function that diagnostic worker needs to call
NetworkTestResult RunNetworkDiagnostics(int pingCount = 15, int timeoutMs = 800,
                                        bool includeBufferbloat = true,
                                        int bufferbloatDuration = 5);

// Add a function to cancel ongoing network tests
void cancelNetworkTests();
}  // namespace NetworkTest
