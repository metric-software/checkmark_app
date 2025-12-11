#include "PdhDataCache.h"
#include <iostream>
#include <sstream>
#include <iomanip>

#include "logging/Logger.h"

namespace PdhMetrics {

PdhDataCache::PdhDataCache(size_t numCpuCores) : numCpuCores_(numCpuCores) {
    // Pre-allocate some capacity to reduce reallocations - std::map doesn't have reserve()
    // The maps will automatically handle memory allocation as needed
}

void PdhDataCache::updateMetric(const std::string& metricName, double value, 
                               std::chrono::steady_clock::time_point timestamp) {
    std::unique_lock<std::shared_mutex> lock(simpleMetricsMutex_);
    simpleMetrics_[metricName] = MetricValue(value, timestamp, true);
}

void PdhDataCache::updatePerCoreMetric(const std::string& metricName, 
                                      const std::vector<double>& coreValues,
                                      double totalValue,
                                      std::chrono::steady_clock::time_point timestamp) {
    std::unique_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
    
    auto& data = perCoreMetrics_[metricName];
    if (data.coreValues.size() != coreValues.size()) {
        data.coreValues.resize(coreValues.size());
    }
    
    // Update all core values
    for (size_t i = 0; i < coreValues.size(); ++i) {
        data.coreValues[i] = MetricValue(coreValues[i], timestamp, true);
    }
    
    // Update total value
    data.totalValue = MetricValue(totalValue, timestamp, true);
}

void PdhDataCache::updatePerCoreMetric(const std::string& metricName, size_t coreIndex, 
                                      double value, std::chrono::steady_clock::time_point timestamp) {
    std::unique_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
    
    ensurePerCoreCapacity(metricName);
    
    auto& data = perCoreMetrics_[metricName];
    if (coreIndex < data.coreValues.size()) {
        data.coreValues[coreIndex] = MetricValue(value, timestamp, true);
    }
}

void PdhDataCache::markMetricInvalid(const std::string& metricName) {
    // Check simple metrics first
    {
        std::unique_lock<std::shared_mutex> lock(simpleMetricsMutex_);
        auto it = simpleMetrics_.find(metricName);
        if (it != simpleMetrics_.end()) {
            it->second.isValid = false;
            return;
        }
    }
    
    // Check per-core metrics
    {
        std::unique_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
        auto it = perCoreMetrics_.find(metricName);
        if (it != perCoreMetrics_.end()) {
            it->second.totalValue.isValid = false;
            for (auto& coreValue : it->second.coreValues) {
                coreValue.isValid = false;
            }
        }
    }
}

void PdhDataCache::clearAllMetrics() {
    {
        std::unique_lock<std::shared_mutex> lock(simpleMetricsMutex_);
        simpleMetrics_.clear();
    }
    
    {
        std::unique_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
        perCoreMetrics_.clear();
    }
}

bool PdhDataCache::getMetric(const std::string& metricName, double& value) const {
    std::shared_lock<std::shared_mutex> lock(simpleMetricsMutex_);
    auto it = simpleMetrics_.find(metricName);
    if (it != simpleMetrics_.end() && it->second.isValid) {
        value = it->second.value;
        return true;
    }
    return false;
}

bool PdhDataCache::getMetric(const std::string& metricName, MetricValue& metricValue) const {
    std::shared_lock<std::shared_mutex> lock(simpleMetricsMutex_);
    auto it = simpleMetrics_.find(metricName);
    if (it != simpleMetrics_.end()) {
        metricValue = it->second;
        return true;
    }
    return false;
}

bool PdhDataCache::getPerCoreMetric(const std::string& metricName, std::vector<double>& coreValues) const {
    std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
    auto it = perCoreMetrics_.find(metricName);
    if (it != perCoreMetrics_.end()) {
        coreValues.clear();
        coreValues.reserve(it->second.coreValues.size());
        
        for (const auto& coreValue : it->second.coreValues) {
            if (coreValue.isValid) {
                coreValues.push_back(coreValue.value);
            } else {
                coreValues.push_back(-1.0); // Indicate invalid value
            }
        }
        return true;
    }
    return false;
}

bool PdhDataCache::getPerCoreMetric(const std::string& metricName, std::vector<double>& coreValues, 
                                   double& totalValue) const {
    std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
    auto it = perCoreMetrics_.find(metricName);
    if (it != perCoreMetrics_.end()) {
        coreValues.clear();
        coreValues.reserve(it->second.coreValues.size());
        
        for (const auto& coreValue : it->second.coreValues) {
            if (coreValue.isValid) {
                coreValues.push_back(coreValue.value);
            } else {
                coreValues.push_back(-1.0); // Indicate invalid value
            }
        }
        
        totalValue = it->second.totalValue.isValid ? it->second.totalValue.value : -1.0;
        return true;
    }
    return false;
}

bool PdhDataCache::getPerCoreMetric(const std::string& metricName, PerCoreMetricData& data) const {
    std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
    auto it = perCoreMetrics_.find(metricName);
    if (it != perCoreMetrics_.end()) {
        data = it->second;
        return true;
    }
    return false;
}

bool PdhDataCache::getCoreMetric(const std::string& metricName, size_t coreIndex, double& value) const {
    std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
    auto it = perCoreMetrics_.find(metricName);
    if (it != perCoreMetrics_.end() && coreIndex < it->second.coreValues.size()) {
        const auto& coreValue = it->second.coreValues[coreIndex];
        if (coreValue.isValid) {
            value = coreValue.value;
            return true;
        }
    }
    return false;
}

std::map<std::string, double> PdhDataCache::getAllMetricValues() const {
    std::map<std::string, double> result;
    
    // Get simple metrics
    {
        std::shared_lock<std::shared_mutex> lock(simpleMetricsMutex_);
        for (const auto& pair : simpleMetrics_) {
            if (pair.second.isValid) {
                result[pair.first] = pair.second.value;
            }
        }
    }
    
    // Get per-core total values
    {
        std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
        for (const auto& pair : perCoreMetrics_) {
            if (pair.second.totalValue.isValid) {
                result[pair.first + "_total"] = pair.second.totalValue.value;
            }
        }
    }
    
    return result;
}

std::map<std::string, MetricValue> PdhDataCache::getAllMetrics() const {
    std::map<std::string, MetricValue> result;
    
    // Get simple metrics
    {
        std::shared_lock<std::shared_mutex> lock(simpleMetricsMutex_);
        result = simpleMetrics_;
    }
    
    // Get per-core total values  
    {
        std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
        for (const auto& pair : perCoreMetrics_) {
            result[pair.first + "_total"] = pair.second.totalValue;
        }
    }
    
    return result;
}

std::vector<std::string> PdhDataCache::getAvailableMetrics() const {
    std::vector<std::string> result;
    
    // Get simple metric names
    {
        std::shared_lock<std::shared_mutex> lock(simpleMetricsMutex_);
        for (const auto& pair : simpleMetrics_) {
            result.push_back(pair.first);
        }
    }
    
    // Get per-core metric names
    {
        std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
        for (const auto& pair : perCoreMetrics_) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

bool PdhDataCache::hasMetric(const std::string& metricName) const {
    {
        std::shared_lock<std::shared_mutex> lock(simpleMetricsMutex_);
        if (simpleMetrics_.find(metricName) != simpleMetrics_.end()) {
            return true;
        }
    }
    
    {
        std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
        if (perCoreMetrics_.find(metricName) != perCoreMetrics_.end()) {
            return true;
        }
    }
    
    return false;
}

bool PdhDataCache::isMetricValid(const std::string& metricName) const {
    {
        std::shared_lock<std::shared_mutex> lock(simpleMetricsMutex_);
        auto it = simpleMetrics_.find(metricName);
        if (it != simpleMetrics_.end()) {
            return it->second.isValid;
        }
    }
    
    {
        std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
        auto it = perCoreMetrics_.find(metricName);
        if (it != perCoreMetrics_.end()) {
            return it->second.totalValue.isValid;
        }
    }
    
    return false;
}

size_t PdhDataCache::getMetricCount() const {
    size_t count = 0;
    
    {
        std::shared_lock<std::shared_mutex> lock(simpleMetricsMutex_);
        count += simpleMetrics_.size();
    }
    
    {
        std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
        count += perCoreMetrics_.size();
    }
    
    return count;
}

bool PdhDataCache::isMetricFresh(const std::string& metricName, std::chrono::milliseconds maxAge) const {
    auto age = getMetricAge(metricName);
    return age >= std::chrono::milliseconds(0) && age <= maxAge;
}

std::chrono::milliseconds PdhDataCache::getMetricAge(const std::string& metricName) const {
    auto now = std::chrono::steady_clock::now();
    
    {
        std::shared_lock<std::shared_mutex> lock(simpleMetricsMutex_);
        auto it = simpleMetrics_.find(metricName);
        if (it != simpleMetrics_.end()) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.timestamp);
        }
    }
    
    {
        std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
        auto it = perCoreMetrics_.find(metricName);
        if (it != perCoreMetrics_.end()) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.totalValue.timestamp);
        }
    }
    
    return std::chrono::milliseconds(-1); // Metric not found
}

std::string PdhDataCache::getDebugInfo() const {
    std::stringstream ss;
    
    ss << "=== PDH Data Cache Info ===\n";
    ss << "CPU Cores: " << numCpuCores_ << "\n";
    ss << "Total Metrics: " << getMetricCount() << "\n";
    
    // Simple metrics count
    {
        std::shared_lock<std::shared_mutex> lock(simpleMetricsMutex_);
        ss << "Simple Metrics: " << simpleMetrics_.size() << "\n";
    }
    
    // Per-core metrics count
    {
        std::shared_lock<std::shared_mutex> lock(perCoreMetricsMutex_);
        ss << "Per-Core Metrics: " << perCoreMetrics_.size() << "\n";
    }
    
    // Statistics
    const auto& stats = getStats();
    ss << "\nCollection Statistics:\n";
    ss << "  Total Collections: " << stats.totalCollections.load() << "\n";
    ss << "  Successful: " << stats.successfulCollections.load() << "\n";
    ss << "  Failed: " << stats.failedCollections.load() << "\n";
    ss << "  Success Rate: " << std::fixed << std::setprecision(1) << stats.getSuccessRate() << "%\n";
    ss << "  Avg Collection Time: " << std::fixed << std::setprecision(2) 
       << stats.avgCollectionTimeMs.load() << " ms\n";
    ss << "  Last Collection Time: " << std::fixed << std::setprecision(2) 
       << stats.lastCollectionTimeMs.load() << " ms\n";
    ss << "  Metrics/sec: " << std::fixed << std::setprecision(1) << stats.getMetricsPerSecond() << "\n";
    
    return ss.str();
}

void PdhDataCache::logCacheStatus() const {
    LOG_INFO << getDebugInfo();
}

void PdhDataCache::ensurePerCoreCapacity(const std::string& metricName) {
    auto& data = perCoreMetrics_[metricName];
    if (data.coreValues.size() < numCpuCores_) {
        data.coreValues.resize(numCpuCores_);
    }
}

std::string PdhDataCache::formatDuration(std::chrono::milliseconds duration) const {
    auto ms = duration.count();
    if (ms < 1000) {
        return std::to_string(ms) + "ms";
    } else if (ms < 60000) {
        return std::to_string(ms / 1000) + "s";
    } else {
        return std::to_string(ms / 60000) + "m";
    }
}

} // namespace PdhMetrics 