#pragma once

#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <QString>
#include "BenchmarkDataPoint.h"

/**
 * @brief Handles all CSV file operations for benchmark results
 * 
 * This class is responsible for:
 * - Creating and writing CSV header with all metrics
 * - Writing data points to CSV files
 * - Managing file paths and directory creation
 * - Handling per-core CPU metrics
 * - Writing system specs files
 * - Managing benchmark result finalization
 */
class BenchmarkResultFileManager {
public:
    BenchmarkResultFileManager();
    ~BenchmarkResultFileManager();

    // Main file operations
    bool initializeOutputFile(const QString& filename);
    bool writeHeader();
    bool writeDataPoints(const std::vector<BenchmarkDataPoint>& dataPoints, 
                        const std::set<std::string>& diskNames);
    bool finalizeBenchmark(const std::vector<BenchmarkDataPoint>& allData,
                          const QString& userSystemId);
    void closeFile();

    // Configuration
    void setCoreCount(size_t logicalCores, size_t physicalCores);
    
    // Status
    bool isFileOpen() const;
    std::string getFilePath() const;

private:
    // File management
    std::ofstream m_outputFile;
    QString m_outputFilename;
    std::string m_fullPath;
    
    // Core count for per-core metrics
    size_t m_finalUsageCount = 0;  // For logical cores (CPU usage)
    size_t m_finalSpeedCount = 0;  // For physical cores (clock speeds)
    
    // Internal helpers
    bool createDirectories();
    bool openFile();
    void writeCSVHeader();
    void writeDataPoint(const BenchmarkDataPoint& data, 
                       const std::set<std::string>& diskNames);
    
    // Data processing
    std::vector<BenchmarkDataPoint> extractBenchmarkData(
        const std::vector<BenchmarkDataPoint>& allData);
    
    // System specs and final results
    bool writeSystemSpecs(const QString& userSystemId);
    bool writeFinalBenchmarkResults();
    void logBenchmarkAverages(const std::vector<BenchmarkDataPoint>& dataPoints,
                             const std::set<std::string>& diskNames);
    
    // Error handling
    void logError(const std::string& message);
    void logCritical(const std::string& message);
};
