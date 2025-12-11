#pragma once
#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

namespace StorageAnalysis {
struct AnalysisResults {
  std::vector<std::pair<std::wstring, unsigned long long>> largestFolders;
  std::vector<std::pair<std::wstring, unsigned long long>> largestFiles;
  bool timedOut = false;
  unsigned long long totalFilesScanned = 0;
  unsigned long long totalFoldersScanned = 0;
  std::chrono::milliseconds actualDuration{0};
};

struct TraversalInfo {
  std::chrono::steady_clock::time_point startTime;
  std::chrono::seconds timeout;
  bool timedOut = false;
  unsigned long long filesScanned = 0;
  unsigned long long foldersScanned = 0;
  std::function<void(const std::wstring&, int)> progressCallback = nullptr;
  std::chrono::steady_clock::time_point lastProgressUpdate;
};

// Helper function to compute folder size (now with progress reporting)
unsigned long long computeTotalSize(const std::wstring& folderPath,
                                    TraversalInfo* timing = nullptr);

// Main analysis function - now scans ALL files regardless of depth
unsigned long long traverseFolder(
  const std::wstring& folderPath,
  std::vector<std::pair<std::wstring, unsigned long long>>& folders,
  std::vector<std::pair<std::wstring, unsigned long long>>& files,
  TraversalInfo& timing);

// Function to run the analysis and return formatted results
AnalysisResults analyzeStorageUsage(
  const std::wstring& rootPath = L"C:\\",
  std::chrono::seconds timeout =
    std::chrono::seconds(120),  // Default 2 minutes
  std::function<void(const std::wstring&, int)> progressCallback = nullptr);
}  // namespace StorageAnalysis
