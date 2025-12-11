#pragma once

#include "PdhDataCache.h"
#include "PdhMetricDefinitions.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <pdh.h>

namespace PdhMetrics {

// Simplified configuration for the metrics manager
struct PdhManagerConfig {
    std::vector<MetricDefinition> requestedMetrics;
    std::chrono::milliseconds collectionInterval{1000};
    bool enableDetailedLogging{false};
};

// Optimized collector for avoiding map lookups during collection
struct MetricCollector {
    const MetricDefinition* metric;
    PDH_HCOUNTER counter;
    std::vector<PDH_HCOUNTER> perCoreCounters;  // Empty for simple metrics
    std::vector<char> wildcardBuffer;           // Pre-allocated for wildcard metrics
    std::vector<double> coreValues;             // Pre-allocated for per-core metrics
    bool isWildcard;
    bool isPerCore;
    
    MetricCollector(const MetricDefinition* m, PDH_HCOUNTER c, bool wildcard, bool perCore, size_t numCores) 
        : metric(m), counter(c), isWildcard(wildcard), isPerCore(perCore) {
        if (isPerCore) {
            perCoreCounters.reserve(numCores);
            coreValues.reserve(numCores);
        }
        if (isWildcard) {
            wildcardBuffer.reserve(8192);  // Pre-allocate reasonable buffer size
        }
    }
};

// Simplified PDH query group
struct PdhQueryGroup {
    PDH_HQUERY queryHandle{nullptr};
    std::string objectName;
    std::vector<MetricDefinition> metrics;
    std::map<std::string, PDH_HCOUNTER> counters;
    std::map<std::string, std::vector<PDH_HCOUNTER>> perCoreCounters;
    
    // Optimized collectors to avoid map lookups during collection
    std::vector<MetricCollector> collectors;
    
    bool initialized{false};
};

/**
 * Simplified, high-performance PDH metrics manager
 * Focuses on reliability and simplicity over complex retry mechanisms
 */
class PdhMetricsManager {
public:
    explicit PdhMetricsManager(const PdhManagerConfig& config);
    ~PdhMetricsManager();

    // Non-copyable, non-movable for simplicity
    PdhMetricsManager(const PdhMetricsManager&) = delete;
    PdhMetricsManager& operator=(const PdhMetricsManager&) = delete;
    PdhMetricsManager(PdhMetricsManager&&) = delete;
    PdhMetricsManager& operator=(PdhMetricsManager&&) = delete;

    // Simple lifecycle management
    bool initialize();
    bool start();
    void stop();
    void shutdown();

    // Status
    bool isRunning() const { return running_.load(); }
    bool isInitialized() const { return initialized_; }
    const PdhManagerConfig& getConfig() const { return config_; }
    
    // Data access
    std::shared_ptr<const PdhDataCache> getDataCache() const { return dataCache_; }
    
    // Direct metric access
    bool getMetric(const std::string& metricName, double& value) const;
    bool getPerCoreMetric(const std::string& metricName, std::vector<double>& coreValues) const;
    bool getCoreMetric(const std::string& metricName, size_t coreIndex, double& value) const;
    
    // Bulk operations
    std::map<std::string, double> getAllMetricValues() const;
    std::vector<std::string> getAvailableMetrics() const;
    
    // Status reporting
    std::string getPerformanceReport() const;
    void logStatus() const;

private:
    // Configuration and state
    PdhManagerConfig config_;
    size_t numCpuCores_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shouldStop_{false};
    bool initialized_{false};
    
    // Threading
    std::thread collectionThread_;
    std::mutex initMutex_;
    
    // PDH resources
    std::vector<std::unique_ptr<PdhQueryGroup>> queryGroups_;
    
    // Data storage
    std::shared_ptr<PdhDataCache> dataCache_;
    
    // Collection thread main loop
    void collectionThreadMain();
    
    // PDH management
    bool initializePdhQueries();
    void cleanupPdhQueries();
    bool initializeQueryGroup(PdhQueryGroup& group);
    
    // Simple collection methods
    bool collectAllMetrics();
    bool collectQueryGroup(PdhQueryGroup& group);
    bool collectSimpleMetric(const MetricDefinition& metric, PDH_HCOUNTER counter, const std::chrono::steady_clock::time_point& timestamp);
    bool collectPerCoreMetric(const MetricDefinition& metric, const std::vector<PDH_HCOUNTER>& counters, const std::chrono::steady_clock::time_point& timestamp);
    
    // Optimized collection methods using pre-allocated buffers
    bool collectSimpleMetricOptimized(MetricCollector& collector, const std::chrono::steady_clock::time_point& timestamp);
    bool collectPerCoreMetricOptimized(MetricCollector& collector, const std::chrono::steady_clock::time_point& timestamp);
    
    // Counter path generation
    std::string generateCounterPath(const MetricDefinition& metric, int coreIndex = -1) const;
    
    // System info
    void detectSystemConfiguration();
};

} // namespace PdhMetrics 