#include "storage_analysis.h"

#include <algorithm>
#include <chrono>
#include <iostream>

namespace StorageAnalysis {
// Add constant for max results
constexpr size_t MAX_RESULTS = 100;
constexpr size_t PROGRESS_UPDATE_INTERVAL_MS =
  500;  // Update progress every 500ms

unsigned long long computeTotalSize(const std::wstring& folderPath,
                                    TraversalInfo* timing) {
  unsigned long long folderSize = 0;
  std::wstring searchPath = folderPath + L"\\*";

  WIN32_FIND_DATAW findData;
  HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findData);
  if (hFind == INVALID_HANDLE_VALUE) return 0;

  do {
    // Check timeout if timing info is provided
    if (timing) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - timing->startTime);
      if (elapsed >= timing->timeout) {
        timing->timedOut = true;
        FindClose(hFind);
        return folderSize;
      }
    }

    std::wstring name = findData.cFileName;
    if (name == L"." || name == L"..") continue;

    std::wstring fullPath = folderPath + L"\\" + name;
    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // Skip reparse points (junctions, symlinks) to avoid infinite loops
      if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
      folderSize += computeTotalSize(fullPath, timing);
    } else {
      ULARGE_INTEGER fileSize;
      fileSize.LowPart = findData.nFileSizeLow;
      fileSize.HighPart = findData.nFileSizeHigh;
      folderSize += fileSize.QuadPart;
    }
  } while (FindNextFileW(hFind, &findData) &&
           (timing ? !timing->timedOut : true));

  FindClose(hFind);
  return folderSize;
}

unsigned long long traverseFolder(
  const std::wstring& folderPath,
  std::vector<std::pair<std::wstring, unsigned long long>>& folders,
  std::vector<std::pair<std::wstring, unsigned long long>>& files,
  TraversalInfo& timing) {
  // Check timeout
  auto now = std::chrono::steady_clock::now();
  auto elapsed =
    std::chrono::duration_cast<std::chrono::seconds>(now - timing.startTime);
  if (elapsed >= timing.timeout) {
    timing.timedOut = true;
    return 0;
  }

  // Update progress every 500ms
  auto timeSinceLastUpdate =
    std::chrono::duration_cast<std::chrono::milliseconds>(
      now - timing.lastProgressUpdate);
  if (timeSinceLastUpdate.count() >= PROGRESS_UPDATE_INTERVAL_MS &&
      timing.progressCallback) {
    // Calculate rough progress based on time elapsed (not perfect but gives
    // user feedback)
    int progressPercent = std::min(
      95, static_cast<int>((elapsed.count() * 100) / timing.timeout.count()));

    std::wstring progressMsg =
      L"Scanning: " + folderPath + L" (" +
      std::to_wstring(timing.filesScanned) + L" files, " +
      std::to_wstring(timing.foldersScanned) + L" folders)";
    timing.progressCallback(progressMsg, progressPercent);
    timing.lastProgressUpdate = now;
  }

  unsigned long long totalSize = 0;
  std::wstring searchPath = folderPath + L"\\*";

  WIN32_FIND_DATAW findData;
  // Use FindFirstFileExW for better performance and to ensure we get all files
  HANDLE hFind = FindFirstFileExW(
    searchPath.c_str(),
    FindExInfoBasic,  // Less info for better performance
    &findData, FindExSearchNameMatch, NULL,
    FIND_FIRST_EX_LARGE_FETCH |
      FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY  // Performance optimization + show all
                                          // entries
  );

  if (hFind == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    // Log specific access errors but continue processing
    if (error == ERROR_ACCESS_DENIED) {
      std::wcout << L"Access denied to: " << folderPath << std::endl;
    } else if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
      std::wcout << L"Error " << error << L" accessing: " << folderPath
                 << std::endl;
    }
    return 0;
  }

  timing.foldersScanned++;

  do {
    // Check timeout more frequently during intensive operations
    now = std::chrono::steady_clock::now();
    elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(now - timing.startTime);
    if (elapsed >= timing.timeout) {
      timing.timedOut = true;
      FindClose(hFind);
      return totalSize;
    }

    std::wstring name = findData.cFileName;
    if (name == L"." || name == L"..") continue;

    std::wstring fullPath = folderPath + L"\\" + name;

    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // Skip reparse points (junctions, symlinks) to avoid infinite loops
      // but still count them as folders
      if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        continue;
      }

      // Recursively traverse subdirectory - NO DEPTH LIMIT
      unsigned long long subfolderSize =
        traverseFolder(fullPath, folders, files, timing);

      if (!timing.timedOut) {
        folders.push_back({fullPath, subfolderSize});
        totalSize += subfolderSize;
      }
    } else {
      // Process individual file
      ULARGE_INTEGER fileSize;
      fileSize.LowPart = findData.nFileSizeLow;
      fileSize.HighPart = findData.nFileSizeHigh;

      files.push_back({fullPath, fileSize.QuadPart});
      totalSize += fileSize.QuadPart;
      timing.filesScanned++;
    }
  } while (FindNextFileW(hFind, &findData) && !timing.timedOut);

  FindClose(hFind);
  return totalSize;
}

AnalysisResults analyzeStorageUsage(
  const std::wstring& rootPath, std::chrono::seconds timeout,
  std::function<void(const std::wstring&, int)> progressCallback) {
  AnalysisResults results;
  TraversalInfo timing{
    std::chrono::steady_clock::now(), timeout, false, 0, 0, progressCallback,
    std::chrono::steady_clock::now()};

  std::vector<std::pair<std::wstring, unsigned long long>>& folders =
    results.largestFolders;
  std::vector<std::pair<std::wstring, unsigned long long>>& files =
    results.largestFiles;

  // Initial progress update
  if (progressCallback) {
    progressCallback(L"Starting storage analysis of " + rootPath, 0);
  }

  // Traverse all files and folders starting from root - NO DEPTH LIMIT
  unsigned long long rootSize =
    traverseFolder(rootPath, folders, files, timing);

  // Add the root folder to results
  folders.push_back({rootPath, rootSize});
  results.timedOut = timing.timedOut;
  results.totalFilesScanned = timing.filesScanned;
  results.totalFoldersScanned = timing.foldersScanned;

  // Calculate actual duration
  auto endTime = std::chrono::steady_clock::now();
  results.actualDuration =
    std::chrono::duration_cast<std::chrono::milliseconds>(endTime -
                                                          timing.startTime);

  // Progress update for sorting phase
  if (progressCallback) {
    progressCallback(L"Sorting results...", 96);
  }

  // Sort folders by size (largest first)
  if (folders.size() > MAX_RESULTS) {
    std::partial_sort(
      folders.begin(), folders.begin() + MAX_RESULTS, folders.end(),
      [](const auto& a, const auto& b) { return a.second > b.second; });
    folders.resize(MAX_RESULTS);
  } else {
    std::sort(folders.begin(), folders.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
  }

  // Sort files by size (largest first)
  if (files.size() > MAX_RESULTS) {
    std::partial_sort(
      files.begin(), files.begin() + MAX_RESULTS, files.end(),
      [](const auto& a, const auto& b) { return a.second > b.second; });
    files.resize(MAX_RESULTS);
  } else {
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
  }

  // Final progress update
  if (progressCallback) {
    std::wstring finalMsg =
      L"Completed! Scanned " + std::to_wstring(results.totalFilesScanned) +
      L" files and " + std::to_wstring(results.totalFoldersScanned) +
      L" folders";
    if (results.timedOut) {
      finalMsg +=
        L" (timed out after " + std::to_wstring(timeout.count()) + L" seconds)";
    }
    progressCallback(finalMsg, 100);
  }

  return results;
}
}  // namespace StorageAnalysis
