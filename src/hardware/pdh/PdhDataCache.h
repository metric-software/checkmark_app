#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace PdhMetrics {

// Metric value with timestamp
struct MetricValue {
    double value = 0.0;
    std::chrono::steady_clock::time_point timestamp;
    bool isValid = false;
    
    MetricValue() = default;
    MetricValue(double val, std::chrono::steady_clock::time_point ts, bool valid = true)
        : value(val), timestamp(ts), isValid(valid) {}
};

// Per-core metric data for CPU metrics
struct PerCoreMetricData {
    std::vector<MetricValue> coreValues;
    MetricValue totalValue;
    
    PerCoreMetricData() = default;
    explicit PerCoreMetricData(size_t numCores) : coreValues(numCores) {}
};

// Collection statistics for monitoring performance
struct CollectionStats {
    std::atomic<uint64_t> totalCollections{0};
    std::atomic<uint64_t> successfulCollections{0};
    std::atomic<uint64_t> failedCollections{0};
    std::atomic<uint64_t> totalMetricsCollected{0};
    std::atomic<double> avgCollectionTimeMs{0.0};
    std::atomic<double> lastCollectionTimeMs{0.0};
    
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point lastUpdateTime;
    
    CollectionStats() : startTime(std::chrono::steady_clock::now()) {}
    
    void recordCollection(bool success, double timeMs, uint32_t metricsCount) {
        totalCollections.fetch_add(1, std::memory_order_relaxed);
        if (success) {
            successfulCollections.fetch_add(1, std::memory_order_relaxed);
            totalMetricsCollected.fetch_add(metricsCount, std::memory_order_relaxed);
        } else {
            failedCollections.fetch_add(1, std::memory_order_relaxed);
        }
        
        lastCollectionTimeMs.store(timeMs, std::memory_order_relaxed);
        
        // Update running average (simple exponential moving average)
        double currentAvg = avgCollectionTimeMs.load(std::memory_order_relaxed);
        double newAvg = currentAvg * 0.9 + timeMs * 0.1;
        avgCollectionTimeMs.store(newAvg, std::memory_order_relaxed);
        
        lastUpdateTime = std::chrono::steady_clock::now();
    }
    
    double getSuccessRate() const {
        uint64_t total = totalCollections.load(std::memory_order_relaxed);
        uint64_t successful = successfulCollections.load(std::memory_order_relaxed);
        return total > 0 ? (static_cast<double>(successful) / total) * 100.0 : 0.0;
    }
    
    double getMetricsPerSecond() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime).count();
        uint64_t total = totalMetricsCollected.load(std::memory_order_relaxed);
        return elapsed > 0 ? static_cast<double>(total) / elapsed : 0.0;
    }
};

/**
 * High-performance, thread-safe cache for PDH metrics
 * Uses reader-writer locks for optimal concurrent access
 * Optimized for frequent reads and infrequent writes
 */
class PdhDataCache {
public:
    explicit PdhDataCache(size_t numCpuCores = 0);
    ~PdhDataCache() = default;

    // Non-copyable but movable
    PdhDataCache(const PdhDataCache&) = delete;
    PdhDataCache& operator=(const PdhDataCache&) = delete;
    PdhDataCache(PdhDataCache&&) = default;
    PdhDataCache& operator=(PdhDataCache&&) = default;

    // Write operations (used by collector thread)
    void updateMetric(const std::string& metricName, double value, 
                     std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());
    
    void updatePerCoreMetric(const std::string& metricName, const std::vector<double>& coreValues,
                            double totalValue = 0.0,
                            std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());
    
    void updatePerCoreMetric(const std::string& metricName, size_t coreIndex, double value,
                            std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());
    
    void markMetricInvalid(const std::string& metricName);
    void clearAllMetrics();

    // Read operations (thread-safe, optimized for frequent access)
    bool getMetric(const std::string& metricName, double& value) const;
    bool getMetric(const std::string& metricName, MetricValue& metricValue) const;
    
    bool getPerCoreMetric(const std::string& metricName, std::vector<double>& coreValues) const;
    bool getPerCoreMetric(const std::string& metricName, std::vector<double>& coreValues, double& totalValue) const;
    bool getPerCoreMetric(const std::string& metricName, PerCoreMetricData& data) const;
    
    bool getCoreMetric(const std::string& metricName, size_t coreIndex, double& value) const;

    // Bulk operations for performance
    std::map<std::string, double> getAllMetricValues() const;
    std::map<std::string, MetricValue> getAllMetrics() const;
    std::vector<std::string> getAvailableMetrics() const;
    
    // Utility functions
    bool hasMetric(const std::string& metricName) const;
    bool isMetricValid(const std::string& metricName) const;
    size_t getMetricCount() const;
    size_t getNumCpuCores() const { return numCpuCores_; }
    
    // Age checking
    bool isMetricFresh(const std::string& metricName, std::chrono::milliseconds maxAge) const;
    std::chrono::milliseconds getMetricAge(const std::string& metricName) const;
    
    // Statistics and monitoring
    const CollectionStats& getStats() const { return stats_; }
    void recordCollectionStats(bool success, double timeMs, uint32_t metricsCount) {
        stats_.recordCollection(success, timeMs, metricsCount);
    }
    
    // Debug and diagnostics
    std::string getDebugInfo() const;
    void logCacheStatus() const;

private:
    // CPU core count for per-core metrics
    size_t numCpuCores_;
    
    // Storage for simple metrics
    mutable std::shared_mutex simpleMetricsMutex_;
    std::map<std::string, MetricValue> simpleMetrics_;
    
    // Storage for per-core metrics  
    mutable std::shared_mutex perCoreMetricsMutex_;
    std::map<std::string, PerCoreMetricData> perCoreMetrics_;
    
    // Collection statistics
    mutable CollectionStats stats_;
    
    // Helper methods
    void ensurePerCoreCapacity(const std::string& metricName);
    std::string formatDuration(std::chrono::milliseconds duration) const;
};

} // namespace PdhMetrics 