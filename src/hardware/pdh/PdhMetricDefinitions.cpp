#include "PdhMetricDefinitions.h"
#include <algorithm>
#include <iostream>

namespace PdhMetrics {

std::vector<MetricDefinition> MetricSelector::getMetricsForCategory(MetricCategory category) {
    switch (category) {
        case MetricCategory::CPU_ESSENTIAL:
            return ESSENTIAL_CPU_METRICS;
        case MetricCategory::MEMORY_ESSENTIAL:
            return ESSENTIAL_MEMORY_METRICS;
        case MetricCategory::DISK_ESSENTIAL:
            return ESSENTIAL_DISK_METRICS;
        case MetricCategory::SYSTEM_ESSENTIAL:
            return ESSENTIAL_SYSTEM_METRICS;
        case MetricCategory::ALL_ESSENTIAL:
            return getAllEssentialMetrics();
        default:
            return {};
    }
}

std::vector<MetricDefinition> MetricSelector::getMetricsForCategories(const std::vector<MetricCategory>& categories) {
    std::vector<MetricDefinition> result;
    
    for (const auto& category : categories) {
        auto categoryMetrics = getMetricsForCategory(category);
        result.insert(result.end(), categoryMetrics.begin(), categoryMetrics.end());
    }
    
    // Remove duplicates based on metric name
    std::sort(result.begin(), result.end(), 
        [](const MetricDefinition& a, const MetricDefinition& b) {
            return a.name < b.name;
        });
        
    result.erase(std::unique(result.begin(), result.end(),
        [](const MetricDefinition& a, const MetricDefinition& b) {
            return a.name == b.name;
        }), result.end());
    
    return result;
}

std::vector<MetricDefinition> MetricSelector::getAllEssentialMetrics() {
    std::vector<MetricDefinition> allMetrics;
    
    // Combine all essential metric categories
    const std::vector<const std::vector<MetricDefinition>*> metricLists = {
        &ESSENTIAL_CPU_METRICS,
        &ESSENTIAL_MEMORY_METRICS,
        &ESSENTIAL_DISK_METRICS,
        &ESSENTIAL_SYSTEM_METRICS
    };
    
    for (const auto* metricList : metricLists) {
        allMetrics.insert(allMetrics.end(), metricList->begin(), metricList->end());
    }
    
    return allMetrics;
}

std::map<std::string, std::vector<MetricDefinition>> MetricSelector::getMetricsGroupedByObject(
    const std::vector<MetricDefinition>& metrics) {
    
    std::map<std::string, std::vector<MetricDefinition>> groupedMetrics;
    
    for (const auto& metric : metrics) {
        // Extract the PDH object name from the counter path
        std::string counterPath = metric.counterPath;
        size_t startPos = counterPath.find('\\', 1); // Find second backslash
        size_t endPos = counterPath.find('(', startPos);
        
        if (endPos == std::string::npos) {
            endPos = counterPath.find('\\', startPos + 1);
        }
        
        std::string objectName;
        if (startPos != std::string::npos && endPos != std::string::npos) {
            objectName = counterPath.substr(startPos + 1, endPos - startPos - 1);
        } else {
            objectName = "Unknown";
        }
        
        groupedMetrics[objectName].push_back(metric);
    }
    
    return groupedMetrics;
}

bool MetricSelector::validateMetricSelection(const std::vector<MetricDefinition>& metrics, 
                                           std::vector<std::string>& errors) {
    errors.clear();
    
    // Check for duplicate metric names
    std::map<std::string, int> nameCount;
    for (const auto& metric : metrics) {
        nameCount[metric.name]++;
        if (nameCount[metric.name] > 1) {
            errors.push_back("Duplicate metric name: " + metric.name);
        }
    }
    
    // Check for invalid counter paths
    for (const auto& metric : metrics) {
        if (metric.counterPath.empty()) {
            errors.push_back("Empty counter path for metric: " + metric.name);
        }
        
        // Basic counter path validation
        if (metric.counterPath.find('\\') != 0) {
            errors.push_back("Invalid counter path format for metric: " + metric.name + 
                           " (should start with \\)");
        }
    }
    
    return errors.empty();
}

std::vector<MetricDefinition> MetricSelector::resolveMetricDependencies(
    const std::vector<MetricDefinition>& requestedMetrics) {
    
    std::vector<MetricDefinition> resolvedMetrics = requestedMetrics;
    
    // For this simplified implementation, we don't have complex dependencies
    // Remove duplicates
    std::sort(resolvedMetrics.begin(), resolvedMetrics.end(), 
        [](const MetricDefinition& a, const MetricDefinition& b) {
            return a.name < b.name;
        });
        
    resolvedMetrics.erase(std::unique(resolvedMetrics.begin(), resolvedMetrics.end(),
        [](const MetricDefinition& a, const MetricDefinition& b) {
            return a.name == b.name;
        }), resolvedMetrics.end());
    
    return resolvedMetrics;
}

// Essential metric set getters
std::vector<MetricDefinition> MetricSelector::getEssentialCpuMetrics() {
    return ESSENTIAL_CPU_METRICS;
}

std::vector<MetricDefinition> MetricSelector::getEssentialMemoryMetrics() {
    return ESSENTIAL_MEMORY_METRICS;
}

std::vector<MetricDefinition> MetricSelector::getEssentialDiskMetrics() {
    return ESSENTIAL_DISK_METRICS;
}

std::vector<MetricDefinition> MetricSelector::getEssentialSystemMetrics() {
    return ESSENTIAL_SYSTEM_METRICS;
}

// Combined essential set for benchmarking
std::vector<MetricDefinition> MetricSelector::getEssentialBenchmarkingMetrics() {
    std::vector<MetricDefinition> result;
    
    // Combine all essential metrics for benchmarking (total ~40 metrics vs 50+)
    auto cpuMetrics = getEssentialCpuMetrics();
    auto memoryMetrics = getEssentialMemoryMetrics();
    auto diskMetrics = getEssentialDiskMetrics();
    auto systemMetrics = getEssentialSystemMetrics();
    
    result.insert(result.end(), cpuMetrics.begin(), cpuMetrics.end());
    result.insert(result.end(), memoryMetrics.begin(), memoryMetrics.end());
    result.insert(result.end(), diskMetrics.begin(), diskMetrics.end());
    result.insert(result.end(), systemMetrics.begin(), systemMetrics.end());
    
    return result;
}

// Minimal set for basic monitoring
std::vector<MetricDefinition> MetricSelector::getMinimalMetrics() {
    return {
        {"cpu_total_usage", "\\Processor(_Total)\\% Processor Time", "cpu_usage", false, true},
        {"cpu_per_core_usage", "\\Processor({0})\\% Processor Time", "cpu_usage", true, true},
        {"memory_available_mbytes", "\\Memory\\Available MBytes", "memory_system", false, false},
        {"memory_committed_bytes", "\\Memory\\Committed Bytes", "memory_system", false, false},
        {"disk_read_bytes_per_sec", "\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", "disk_io", false, false},
        {"disk_write_bytes_per_sec", "\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", "disk_io", false, false},
        {"disk_percent_time", "\\PhysicalDisk(_Total)\\% Disk Time", "disk_latency", false, true},
        {"system_context_switches_per_sec", "\\System\\Context Switches/sec", "system_kernel", false, false}
    };
}

} // namespace PdhMetrics 