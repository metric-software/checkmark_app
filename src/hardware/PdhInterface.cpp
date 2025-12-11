#include "PdhInterface.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>

#include "logging/Logger.h"

PdhInterface::PdhInterface(const std::vector<PdhMetrics::MetricCategory>& categories,
                          std::chrono::milliseconds interval) {
    auto metrics = PdhMetrics::MetricSelector::getMetricsForCategories(categories);
    initializeManager(metrics, interval);
}

PdhInterface::PdhInterface(const std::vector<PdhMetrics::MetricDefinition>& metrics,
                          std::chrono::milliseconds interval) {
    initializeManager(metrics, interval);
}

PdhInterface::PdhInterface(std::chrono::milliseconds interval) {
    auto metrics = PdhMetrics::MetricSelector::getAllEssentialMetrics();
    initializeManager(metrics, interval);
}

std::unique_ptr<PdhInterface> PdhInterface::createForCpuMonitoring(std::chrono::milliseconds interval) {
    return std::make_unique<PdhInterface>(std::vector<PdhMetrics::MetricCategory>{PdhMetrics::MetricCategory::CPU_ESSENTIAL}, interval);
}

std::unique_ptr<PdhInterface> PdhInterface::createForSystemMonitoring(std::chrono::milliseconds interval) {
    std::vector<PdhMetrics::MetricCategory> categories = {
        PdhMetrics::MetricCategory::CPU_ESSENTIAL,
        PdhMetrics::MetricCategory::MEMORY_ESSENTIAL,
        PdhMetrics::MetricCategory::DISK_ESSENTIAL,
        PdhMetrics::MetricCategory::SYSTEM_ESSENTIAL
    };
    return std::make_unique<PdhInterface>(categories, interval);
}

std::unique_ptr<PdhInterface> PdhInterface::createForBenchmarking(std::chrono::milliseconds interval) {
    // Use all essential metrics for benchmarking
    return std::make_unique<PdhInterface>(std::vector<PdhMetrics::MetricCategory>{PdhMetrics::MetricCategory::ALL_ESSENTIAL}, interval);
}

std::unique_ptr<PdhInterface> PdhInterface::createOptimizedForBenchmarking(std::chrono::milliseconds interval) {
    // Use essential benchmarking metrics only - optimized for performance
    auto metrics = PdhMetrics::MetricSelector::getEssentialBenchmarkingMetrics();
    return std::make_unique<PdhInterface>(metrics, interval);
}

std::unique_ptr<PdhInterface> PdhInterface::createMinimal(std::chrono::milliseconds interval) {
    auto metrics = PdhMetrics::MetricSelector::getMinimalMetrics();
    return std::make_unique<PdhInterface>(metrics, interval);
}

std::unique_ptr<PdhInterface> PdhInterface::createForPerDiskMonitoring(std::chrono::milliseconds interval) {
    // Use disk essential metrics only (includes LogicalDisk per-disk metrics)
    return std::make_unique<PdhInterface>(std::vector<PdhMetrics::MetricCategory>{PdhMetrics::MetricCategory::DISK_ESSENTIAL}, interval);
}

PdhInterface::~PdhInterface() {
    LOG_INFO << "[PDH] PdhInterface destructor called";
    
    if (manager_) {
        try {
            LOG_INFO << "[PDH] Calling manager_->shutdown() from destructor";
            manager_->shutdown();
            LOG_INFO << "[PDH] manager_->shutdown() completed successfully";
        } catch (const std::exception& e) {
            LOG_ERROR << "[PDH] Exception in manager_->shutdown(): " << e.what();
        } catch (...) {
            LOG_ERROR << "[PDH] Unknown exception in manager_->shutdown()";
        }
    } else {
        LOG_ERROR << "[PDH] manager_ is null in destructor";
    }

    LOG_INFO << "[PDH] PdhInterface destructor completed";
}

void PdhInterface::initializeManager(const std::vector<PdhMetrics::MetricDefinition>& metrics,
                                    std::chrono::milliseconds interval) {
    try {
        LOG_INFO << "[PDH] Initializing manager with " << metrics.size() << " metrics, interval: " 
                 << interval.count() << "ms";

        if (metrics.empty()) {
            LOG_ERROR << "[PDH] No metrics provided to initialize manager";
            return;
        }
        
        // Debug: Print the first few metrics being requested
        LOG_INFO << "[PDH] First few metrics being requested:";
        for (size_t i = 0; i < std::min<size_t>(10, metrics.size()); i++) {
            LOG_INFO << "[PDH]   " << i << ": " << metrics[i].name << " -> " << metrics[i].counterPath;
        }
        
        PdhMetrics::PdhManagerConfig config;
        config.requestedMetrics = metrics;
        config.collectionInterval = interval;
        config.enableDetailedLogging = false;  // Disable verbose collection logs - keep only summary reports

        LOG_INFO << "[PDH] Creating PdhMetricsManager...";
        manager_ = std::make_unique<PdhMetrics::PdhMetricsManager>(config);
        
        if (manager_) {
            LOG_INFO << "[PDH] PdhMetricsManager created successfully";
        } else {
            LOG_ERROR << "[PDH] Failed to create PdhMetricsManager";
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR << "[PDH] Exception in initializeManager: " << e.what();
        manager_.reset();
    } catch (...) {
        LOG_ERROR << "[PDH] Unknown exception in initializeManager";
        manager_.reset();
    }
}

bool PdhInterface::start() {
    if (!manager_) {
        LOG_ERROR << "[PDH] ERROR: Cannot start - manager is null";
        return false;
    }
    
    try {
        LOG_INFO << "[PDH] Initializing PdhMetricsManager...";
        bool initialized = manager_->initialize();
        if (!initialized) {
            LOG_ERROR << "[PDH] ERROR: PdhMetricsManager initialization failed";
            return false;
        }

        LOG_INFO << "[PDH] PdhMetricsManager initialized successfully";
        LOG_INFO << "[PDH] Starting metrics collection...";
        
        bool started = manager_->start();
        if (started) {
            LOG_INFO << "[PDH] PdhMetricsManager started successfully";
        } else {
            LOG_ERROR << "[PDH] ERROR: Failed to start PdhMetricsManager";
        }
        
        return started;
        
    } catch (const std::exception& e) {
        LOG_ERROR << "[PDH] ERROR: Exception during start: " << e.what();
        return false;
    } catch (...) {
        LOG_ERROR << "[PDH] ERROR: Unknown exception during start";
        return false;
    }
}

void PdhInterface::stop() {
    LOG_INFO << "[PDH] PdhInterface::stop() called";
    
    if (manager_) {
        try {
            LOG_INFO << "[PDH] Calling manager_->stop()";
            manager_->stop();
            LOG_INFO << "[PDH] manager_->stop() completed successfully";
        } catch (const std::exception& e) {
            LOG_ERROR << "[PDH] ERROR: Exception in manager_->stop(): " << e.what();
        } catch (...) {
            LOG_ERROR << "[PDH] ERROR: Unknown exception in manager_->stop()";
        }
    } else {
        LOG_ERROR << "[PDH] manager_ is null, nothing to stop";
    }

    LOG_INFO << "[PDH] PdhInterface::stop() completed";
}

bool PdhInterface::isRunning() const {
    return manager_ && manager_->isRunning();
}

bool PdhInterface::getMetric(const std::string& metricName, double& value) const {
    if (!manager_) return false;
    return manager_->getMetric(metricName, value);
}

bool PdhInterface::getPerCoreMetric(const std::string& metricName, std::vector<double>& coreValues) const {
    if (!manager_) return false;
    return manager_->getPerCoreMetric(metricName, coreValues);
}

bool PdhInterface::getCoreMetric(const std::string& metricName, size_t coreIndex, double& value) const {
    if (!manager_) return false;
    return manager_->getCoreMetric(metricName, coreIndex, value);
}

std::map<std::string, double> PdhInterface::getAllMetrics() const {
    if (!manager_) return {};
    return manager_->getAllMetricValues();
}

std::vector<std::string> PdhInterface::getAvailableMetrics() const {
    if (!manager_) return {};
    return manager_->getAvailableMetrics();
}

double PdhInterface::getCpuUsage() const {
    double value = -1.0;
    getMetric("cpu_total_usage", value);
    return value;
}

double PdhInterface::getMemoryUsageMB() const {
    double availableMB = -1.0;
    if (getMetric("memory_available_mbytes", availableMB) && availableMB > 0) {
        return availableMB;
    }
    return -1.0;
}

double PdhInterface::getDiskReadMBps() const {
    double value = -1.0;
    getMetric("disk_read_bytes_per_sec", value);
    if (value >= 0) {
        return value / (1024.0 * 1024.0); // Convert bytes/sec to MB/sec
    }
    return -1.0;
}

double PdhInterface::getDiskWriteMBps() const {
    double value = -1.0;
    getMetric("disk_write_bytes_per_sec", value);
    if (value >= 0) {
        return value / (1024.0 * 1024.0); // Convert bytes/sec to MB/sec
    }
    return -1.0;
}

std::vector<double> PdhInterface::getPerCoreCpuUsage() const {
    std::vector<double> coreValues;
    getPerCoreMetric("cpu_per_core_usage", coreValues);
    return coreValues;
}

size_t PdhInterface::getCpuCoreCount() const {
    if (!manager_) return 0;
    auto dataCache = manager_->getDataCache();
    if (dataCache) {
        return dataCache->getNumCpuCores();
    }
    return 0;
}

bool PdhInterface::hasMetric(const std::string& metricName) const {
    if (!manager_) return false;
    auto dataCache = manager_->getDataCache();
    if (dataCache) {
        return dataCache->hasMetric(metricName);
    }
    return false;
}

bool PdhInterface::isMetricValid(const std::string& metricName) const {
    if (!manager_) return false;
    auto dataCache = manager_->getDataCache();
    if (dataCache) {
        return dataCache->isMetricValid(metricName);
    }
    return false;
}

std::string PdhInterface::getPerformanceReport() const {
    if (!manager_) return "PDH Interface not initialized";
    return manager_->getPerformanceReport();
}

void PdhInterface::logStatus() const {
    if (manager_) {
        manager_->logStatus();
    }
}

std::chrono::milliseconds PdhInterface::getCollectionInterval() const {
    if (!manager_) return std::chrono::milliseconds(0);
    return manager_->getConfig().collectionInterval;
}

void PdhInterface::enableDetailedLogging(bool enable) {
    // Note: This would require modifying the manager's config
    // For now, detailed logging is disabled by default
}

// Utility functions implementation
namespace PdhUtils {

std::vector<PdhMetrics::MetricDefinition> getCpuMetrics() {
    return PdhMetrics::MetricSelector::getEssentialCpuMetrics();
}

std::vector<PdhMetrics::MetricDefinition> getMemoryMetrics() {
    return PdhMetrics::MetricSelector::getEssentialMemoryMetrics();
}

std::vector<PdhMetrics::MetricDefinition> getDiskMetrics() {
    return PdhMetrics::MetricSelector::getEssentialDiskMetrics();
}

std::vector<PdhMetrics::MetricDefinition> getSystemMetrics() {
    return PdhMetrics::MetricSelector::getEssentialSystemMetrics();
}

std::vector<PdhMetrics::MetricDefinition> getBenchmarkingMetrics() {
    return PdhMetrics::MetricSelector::getEssentialBenchmarkingMetrics();
}

std::vector<PdhMetrics::MetricDefinition> getMinimalMetrics() {
    return PdhMetrics::MetricSelector::getMinimalMetrics();
}

std::vector<PdhMetrics::MetricDefinition> getEssentialBenchmarkingMetrics() {
    return PdhMetrics::MetricSelector::getEssentialBenchmarkingMetrics();
}

void printAvailableMetrics() {
    LOG_INFO << "=== Available Essential PDH Metrics ===";

    auto allMetrics = PdhMetrics::MetricSelector::getAllEssentialMetrics();
    auto groupedMetrics = PdhMetrics::MetricSelector::getMetricsGroupedByObject(allMetrics);
    
    for (const auto& [objectName, metrics] : groupedMetrics) {
        LOG_INFO << "\n" << objectName << " (" << metrics.size() << " metrics):";
        for (const auto& metric : metrics) {
            LOG_INFO << "  " << metric.name << " - " << metric.counterPath;
            if (metric.perCore) {
                LOG_INFO << " (per-core)";
            }
            if (metric.requiresBaseline) {
                LOG_INFO << " (requires baseline)";
            }
            
        }
    }

    LOG_INFO << "\nTotal: " << allMetrics.size() << " essential metrics available";
}

void printMetricCategories() {
    LOG_INFO << "=== Essential PDH Metric Categories ===";

    const std::vector<std::pair<PdhMetrics::MetricCategory, std::string>> categories = {
        {PdhMetrics::MetricCategory::CPU_ESSENTIAL, "CPU Essential"},
        {PdhMetrics::MetricCategory::MEMORY_ESSENTIAL, "Memory Essential"},
        {PdhMetrics::MetricCategory::DISK_ESSENTIAL, "Disk Essential"},
        {PdhMetrics::MetricCategory::SYSTEM_ESSENTIAL, "System Essential"},
        {PdhMetrics::MetricCategory::ALL_ESSENTIAL, "All Essential"}
    };
    
    for (const auto& [category, name] : categories) {
        auto metrics = PdhMetrics::MetricSelector::getMetricsForCategory(category);
        LOG_INFO << name << ": " << metrics.size() << " metrics";
    }
}



} // namespace PdhUtils 