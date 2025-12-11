#include "PdhMetricsManager.h"

#include <algorithm>
#include <iostream>
#include <sstream>

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include "../../logging/Logger.h"

namespace PdhMetrics {

PdhMetricsManager::PdhMetricsManager(const PdhManagerConfig& config) 
    : config_(config), numCpuCores_(0) {
    detectSystemConfiguration();
    dataCache_ = std::make_shared<PdhDataCache>(numCpuCores_);
}

PdhMetricsManager::~PdhMetricsManager() {
    shutdown();
}

bool PdhMetricsManager::initialize() {
    std::lock_guard<std::mutex> lock(initMutex_);
    
    if (initialized_) {
        return true;
    }
    
    if (config_.requestedMetrics.empty()) {
        LOG_ERROR << "[PDH] ERROR: No metrics provided";
        return false;
    }
    
    // Group metrics by PDH object for efficient batching
    auto groupedMetrics = MetricSelector::getMetricsGroupedByObject(config_.requestedMetrics);
    
    queryGroups_.clear();
    queryGroups_.reserve(groupedMetrics.size());
    
    for (const auto& [objectName, metrics] : groupedMetrics) {
        auto group = std::make_unique<PdhQueryGroup>();
        group->objectName = objectName;
        group->metrics = metrics;
        queryGroups_.push_back(std::move(group));
    }
    
    if (!initializePdhQueries()) {
        cleanupPdhQueries();
        return false;
    }
    
    initialized_ = true;
    LOG_INFO << "[PDH] Initialized with " << config_.requestedMetrics.size() << " metrics";
    return true;
}

bool PdhMetricsManager::start() {
    if (!initialized_ && !initialize()) {
        return false;
    }
    
    if (running_.load()) {
        return true;
    }
    
    shouldStop_.store(false);
    running_.store(true);
    
    collectionThread_ = std::thread(&PdhMetricsManager::collectionThreadMain, this);
    
    LOG_INFO << "[PDH] Started collection thread";
    return true;
}

void PdhMetricsManager::stop() {
    if (!running_.load()) {
        return;
    }
    
    shouldStop_.store(true);
    
    if (collectionThread_.joinable()) {
        collectionThread_.join();
    }
    
    running_.store(false);
    LOG_INFO << "[PDH] Stopped collection thread";
}

void PdhMetricsManager::shutdown() {
    stop();
    cleanupPdhQueries();
    initialized_ = false;
}

bool PdhMetricsManager::getMetric(const std::string& metricName, double& value) const {
    if (!dataCache_) return false;
    return dataCache_->getMetric(metricName, value);
}

bool PdhMetricsManager::getPerCoreMetric(const std::string& metricName, std::vector<double>& coreValues) const {
    if (!dataCache_) return false;
    return dataCache_->getPerCoreMetric(metricName, coreValues);
}

bool PdhMetricsManager::getCoreMetric(const std::string& metricName, size_t coreIndex, double& value) const {
    if (!dataCache_) return false;
    return dataCache_->getCoreMetric(metricName, coreIndex, value);
}

std::map<std::string, double> PdhMetricsManager::getAllMetricValues() const {
    if (!dataCache_) return {};
    return dataCache_->getAllMetricValues();
}

std::vector<std::string> PdhMetricsManager::getAvailableMetrics() const {
    if (!dataCache_) return {};
    return dataCache_->getAvailableMetrics();
}

void PdhMetricsManager::collectionThreadMain() {
    LOG_DEBUG << "[PDH] Collection thread started";
    
    // Establish baseline - collect once and discard
    for (auto& group : queryGroups_) {
        if (group->initialized) {
            PdhCollectQueryData(group->queryHandle);
        }
    }
    
    // Wait one interval for baseline to establish
    std::this_thread::sleep_for(config_.collectionInterval);
    
    while (!shouldStop_.load()) {
        auto collectionStart = std::chrono::steady_clock::now();
        
        // Simple collection - no retries, no delays
        bool success = collectAllMetrics();
        
        auto collectionEnd = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(collectionEnd - collectionStart);
        
        if (config_.enableDetailedLogging) {
            LOG_DEBUG << "[PDH] Collection took " << elapsed.count() << "ms, success: " << success;
        }
        
        // Sleep until next collection
        auto remaining = config_.collectionInterval - elapsed;
        if (remaining > std::chrono::milliseconds(0)) {
            std::this_thread::sleep_for(remaining);
        }
    }
    
    LOG_DEBUG << "[PDH] Collection thread ended";
}

bool PdhMetricsManager::initializePdhQueries() {
    bool anyGroupSucceeded = false;
    for (auto& group : queryGroups_) {
        if (initializeQueryGroup(*group)) {
            anyGroupSucceeded = true;
            LOG_INFO << "[PDH] Successfully initialized group: " << group->objectName;
        } else {
            LOG_WARN << "[PDH] Failed to initialize group: " << group->objectName << " (continuing with other groups)";
        }
    }
    
    if (!anyGroupSucceeded) {
        LOG_ERROR << "[PDH] ERROR: Failed to initialize any PDH query groups";
        return false;
    }
    
    return true;
}

void PdhMetricsManager::cleanupPdhQueries() {
    for (auto& group : queryGroups_) {
        if (group->queryHandle) {
            PdhCloseQuery(group->queryHandle);
            group->queryHandle = nullptr;
        }
        group->initialized = false;
    }
}

bool PdhMetricsManager::initializeQueryGroup(PdhQueryGroup& group) {
    // Create query
    PDH_STATUS status = PdhOpenQuery(nullptr, 0, &group.queryHandle);
    if (status != ERROR_SUCCESS) {
        LOG_ERROR << "[PDH] Failed to open query for " << group.objectName << ": 0x" << std::hex << status << std::dec;
        return false;
    }
    
    LOG_INFO << "[PDH] Initializing query group: " << group.objectName << " with " << group.metrics.size() << " metrics";
    
    // Add counters
    for (const auto& metric : group.metrics) {
        if (metric.perCore) {
            // Add per-core counters
            std::vector<PDH_HCOUNTER> coreCounters;
            coreCounters.reserve(numCpuCores_);
            
            for (size_t i = 0; i < numCpuCores_; ++i) {
                auto counterPath = generateCounterPath(metric, static_cast<int>(i));
                std::wstring wCounterPath(counterPath.begin(), counterPath.end());
                
                PDH_HCOUNTER counter;
                status = PdhAddCounterW(group.queryHandle, wCounterPath.c_str(), 0, &counter);
                
                if (status == ERROR_SUCCESS) {
                    coreCounters.push_back(counter);
                    LOG_INFO << "[PDH] Successfully added per-core counter for core " << i << ": " << counterPath;
                } else {
                    coreCounters.push_back(nullptr);
                    LOG_ERROR << "[PDH] Failed to add per-core counter for core " << i << ": " << counterPath << " (Status: 0x" << std::hex << status << std::dec << ")";
                }
            }
            
            group.perCoreCounters[metric.name] = std::move(coreCounters);
        } else {
            // Add simple counter
            std::string counterPath = generateCounterPath(metric);
            std::wstring wCounterPath(counterPath.begin(), counterPath.end());
            
            PDH_HCOUNTER counter;
            status = PdhAddCounterW(group.queryHandle, wCounterPath.c_str(), 0, &counter);
            
            if (status == ERROR_SUCCESS) {
                group.counters[metric.name] = counter;
            } else {
                LOG_ERROR << "[PDH] Failed to add counter: " << counterPath << " (Status: 0x" << std::hex << status << std::dec << ")";
            }
        }
    }
    
    // Build optimized collectors to avoid map lookups during collection
    group.collectors.clear();
    group.collectors.reserve(group.metrics.size());
    
    for (const auto& metric : group.metrics) {
        bool isWildcard = metric.counterPath.find("(*)") != std::string::npos;
        bool isPerCore = metric.perCore;
        
        if (isPerCore) {
            auto it = group.perCoreCounters.find(metric.name);
            if (it != group.perCoreCounters.end()) {
                // Create collector with per-core counters
                group.collectors.emplace_back(&metric, nullptr, false, true, numCpuCores_);
                group.collectors.back().perCoreCounters = it->second;
            }
        } else {
            auto it = group.counters.find(metric.name);
            if (it != group.counters.end()) {
                // Create collector with single counter
                group.collectors.emplace_back(&metric, it->second, isWildcard, false, numCpuCores_);
            }
        }
    }
    
    LOG_INFO << "[PDH] Built " << group.collectors.size() << " optimized collectors for " << group.objectName;
    
    group.initialized = true;
    return true;
}

bool PdhMetricsManager::collectAllMetrics() {
    if (!initialized_) {
        return false;
    }
    
    auto timestamp = std::chrono::steady_clock::now();
    bool overallSuccess = true;
    
    for (auto& group : queryGroups_) {
        if (group->initialized) {
            if (!collectQueryGroup(*group)) {
                overallSuccess = false;
            }
        }
    }
    
    return overallSuccess;
}

bool PdhMetricsManager::collectQueryGroup(PdhQueryGroup& group) {
    // Collect data
    PDH_STATUS status = PdhCollectQueryData(group.queryHandle);
    if (status != ERROR_SUCCESS) {
        if (config_.enableDetailedLogging) {
            LOG_ERROR << "[PDH] Collection failed for " << group.objectName << ": 0x" << std::hex << status << std::dec;
        }
        return false;
    }
    
    auto timestamp = std::chrono::steady_clock::now();
    bool groupSuccess = true;
    
    // Process all metrics using optimized collectors (no map lookups)
    for (auto& collector : group.collectors) {
        if (collector.isPerCore) {
            if (!collectPerCoreMetricOptimized(collector, timestamp)) {
                groupSuccess = false;
            }
        } else {
            if (!collectSimpleMetricOptimized(collector, timestamp)) {
                groupSuccess = false;
            }
        }
    }
    
    return groupSuccess;
}

bool PdhMetricsManager::collectSimpleMetric(const MetricDefinition& metric, PDH_HCOUNTER counter, const std::chrono::steady_clock::time_point& timestamp) {
    if (!counter) {
        return false;
    }
    
    // Check if this is a wildcard counter
    bool isWildcard = metric.counterPath.find("(*)") != std::string::npos;
    
    if (isWildcard) {
        // Handle wildcard counters
        DWORD bufferSize = 0;
        DWORD itemCount = 0;
        
        PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
        
        if (status == PDH_MORE_DATA && bufferSize > 0) {
            std::vector<char> buffer(bufferSize);
            PPDH_FMT_COUNTERVALUE_ITEM_W items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(buffer.data());
            
            status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
            
            if (status == ERROR_SUCCESS && itemCount > 0) {
                double totalValue = 0.0;
                
                for (DWORD i = 0; i < itemCount; ++i) {
                    if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
                        totalValue += items[i].FmtValue.doubleValue;
                    }
                }
                
                dataCache_->updateMetric(metric.name, totalValue, timestamp);
                return true;
            }
        }
        
        return false;
    } else {
        // Handle single-instance counters
        PDH_FMT_COUNTERVALUE value;
        PDH_STATUS status = PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value);
        
        if (status == ERROR_SUCCESS && value.CStatus == PDH_CSTATUS_VALID_DATA) {
            dataCache_->updateMetric(metric.name, value.doubleValue, timestamp);
            return true;
        }
        
        return false;
    }
}

bool PdhMetricsManager::collectPerCoreMetric(const MetricDefinition& metric, const std::vector<PDH_HCOUNTER>& counters, const std::chrono::steady_clock::time_point& timestamp) {
    std::vector<double> coreValues;
    coreValues.reserve(counters.size());
    double totalValue = 0.0;
    int validCores = 0;
    
    for (size_t i = 0; i < counters.size(); ++i) {
        if (!counters[i]) {
            coreValues.push_back(-1.0);
            continue;
        }
        
        PDH_FMT_COUNTERVALUE value;
        PDH_STATUS status = PdhGetFormattedCounterValue(counters[i], PDH_FMT_DOUBLE, nullptr, &value);
        
        if (status == ERROR_SUCCESS && value.CStatus == PDH_CSTATUS_VALID_DATA) {
            coreValues.push_back(value.doubleValue);
            totalValue += value.doubleValue;
            validCores++;
        } else {
            coreValues.push_back(-1.0);
        }
    }
    
    if (validCores > 0) {
        dataCache_->updatePerCoreMetric(metric.name, coreValues, totalValue, timestamp);
        return true;
    }
    
    return false;
}

bool PdhMetricsManager::collectSimpleMetricOptimized(MetricCollector& collector, const std::chrono::steady_clock::time_point& timestamp) {
    if (!collector.counter) {
        return false;
    }
    
    if (collector.isWildcard) {
        // Handle wildcard counters with pre-allocated buffer
        DWORD bufferSize = static_cast<DWORD>(collector.wildcardBuffer.size());
        DWORD itemCount = 0;
        
        // Try with current buffer size first
        PDH_STATUS status = PdhGetFormattedCounterArrayW(collector.counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, 
                                                        reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(collector.wildcardBuffer.data()));
        
        if (status == PDH_MORE_DATA && bufferSize > collector.wildcardBuffer.size()) {
            // Resize buffer if needed
            collector.wildcardBuffer.resize(bufferSize);
            status = PdhGetFormattedCounterArrayW(collector.counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, 
                                                reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(collector.wildcardBuffer.data()));
        }
        
        if (status == ERROR_SUCCESS && itemCount > 0) {
            PPDH_FMT_COUNTERVALUE_ITEM_W items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(collector.wildcardBuffer.data());
            double totalValue = 0.0;
            
            for (DWORD i = 0; i < itemCount; ++i) {
                if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA) {
                    totalValue += items[i].FmtValue.doubleValue;
                }
            }
            
            dataCache_->updateMetric(collector.metric->name, totalValue, timestamp);
            return true;
        }
        
        return false;
    } else {
        // Handle single-instance counters
        PDH_FMT_COUNTERVALUE value;
        PDH_STATUS status = PdhGetFormattedCounterValue(collector.counter, PDH_FMT_DOUBLE, nullptr, &value);
        
        if (status == ERROR_SUCCESS && value.CStatus == PDH_CSTATUS_VALID_DATA) {
            dataCache_->updateMetric(collector.metric->name, value.doubleValue, timestamp);
            return true;
        }
        
        return false;
    }
}

bool PdhMetricsManager::collectPerCoreMetricOptimized(MetricCollector& collector, const std::chrono::steady_clock::time_point& timestamp) {
    // Clear and reuse the pre-allocated vector
    collector.coreValues.clear();
    collector.coreValues.reserve(collector.perCoreCounters.size());
    
    double totalValue = 0.0;
    int validCores = 0;
    
    for (size_t i = 0; i < collector.perCoreCounters.size(); ++i) {
        if (!collector.perCoreCounters[i]) {
            collector.coreValues.push_back(-1.0);
            continue;
        }
        
        PDH_FMT_COUNTERVALUE value;
        PDH_STATUS status = PdhGetFormattedCounterValue(collector.perCoreCounters[i], PDH_FMT_DOUBLE, nullptr, &value);
        
        if (status == ERROR_SUCCESS && value.CStatus == PDH_CSTATUS_VALID_DATA) {
            collector.coreValues.push_back(value.doubleValue);
            totalValue += value.doubleValue;
            validCores++;
        } else {
            collector.coreValues.push_back(-1.0);
        }
    }
    
    if (validCores > 0) {
        dataCache_->updatePerCoreMetric(collector.metric->name, collector.coreValues, totalValue, timestamp);
        return true;
    }
    
    return false;
}

std::string PdhMetricsManager::generateCounterPath(const MetricDefinition& metric, int coreIndex) const {
    std::string path = metric.counterPath;
    
    if (coreIndex >= 0) {
        size_t pos = path.find("{0}");
        if (pos != std::string::npos) {
            path.replace(pos, 3, std::to_string(coreIndex));
        }
    }
    
    return path;
}

std::string PdhMetricsManager::getPerformanceReport() const {
    std::stringstream ss;
    ss << "=== PDH Metrics Manager Status ===\n";
    ss << "Running: " << (isRunning() ? "Yes" : "No") << "\n";
    ss << "Initialized: " << (isInitialized() ? "Yes" : "No") << "\n";
    ss << "Interval: " << config_.collectionInterval.count() << "ms\n";
    ss << "Query Groups: " << queryGroups_.size() << "\n";
    
    if (dataCache_) {
        ss << "Available Metrics: " << dataCache_->getMetricCount() << "\n";
    }
    
    return ss.str();
}

void PdhMetricsManager::logStatus() const {
    LOG_INFO << getPerformanceReport();
}

void PdhMetricsManager::detectSystemConfiguration() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    numCpuCores_ = sysInfo.dwNumberOfProcessors;
}

} // namespace PdhMetrics 