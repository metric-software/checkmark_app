/*
 * PdhInterface - Windows Performance Data Helper (PDH) System Metrics
 * 
 * ESSENTIAL PDH METRICS SUPPORTED:
 * 
 * === CPU METRICS ===
 * - cpu_total_usage: Total CPU usage percentage (% Processor Time)
 * - cpu_user_time: User time percentage (% User Time)
 * - cpu_privileged_time: Privileged/kernel time percentage (% Privileged Time)
 * - cpu_idle_time: CPU idle time percentage (% Idle Time)
 * - cpu_per_core_usage: Per-core CPU usage percentages
 * - cpu_actual_frequency: CPU actual frequency in MHz
 * - cpu_per_core_actual_freq: Per-core actual frequency (when available)
 * - cpu_interrupts_per_sec: Interrupts per second
 * - cpu_dpc_time: DPC time percentage (% DPC Time)
 * - cpu_interrupt_time: Interrupt time percentage (% Interrupt Time)
 * - cpu_dpcs_queued_per_sec: DPCs queued per second
 * - cpu_dpc_rate: DPC rate
 * - cpu_c1_time, cpu_c2_time, cpu_c3_time: C-state time percentages
 * - cpu_c1_transitions_per_sec, cpu_c2_transitions_per_sec, cpu_c3_transitions_per_sec: C-state transitions
 * 
 * === MEMORY METRICS ===
 * - memory_available_mbytes: Available physical memory in MB
 * - memory_committed_bytes: Total committed memory in bytes
 * - memory_commit_limit: Memory commit limit in bytes
 * - memory_page_faults_per_sec: Page faults per second
 * - memory_pages_per_sec: Pages per second
 * - memory_pool_nonpaged_bytes: Non-paged pool memory in bytes
 * - memory_pool_paged_bytes: Paged pool memory in bytes
 * - memory_system_code_bytes: System code memory in bytes
 * - memory_system_driver_bytes: System driver memory in bytes
 * 
 * === DISK I/O METRICS ===
 * - disk_read_bytes_per_sec: Disk read rate in bytes/second
 * - disk_write_bytes_per_sec: Disk write rate in bytes/second
 * - disk_reads_per_sec: Disk reads per second
 * - disk_writes_per_sec: Disk writes per second
 * - disk_transfers_per_sec: Total disk transfers per second
 * - disk_bytes_per_sec: Total disk bytes per second
 * - disk_avg_read_queue_length: Average disk read queue length
 * - disk_avg_write_queue_length: Average disk write queue length
 * - disk_avg_queue_length: Average disk queue length
 * - disk_avg_read_time: Average disk read time in seconds
 * - disk_avg_write_time: Average disk write time in seconds
 * - disk_avg_transfer_time: Average disk transfer time in seconds
 * - disk_percent_time: Disk busy time percentage
 * - disk_percent_read_time: Disk read time percentage
 * - disk_percent_write_time: Disk write time percentage
 * - disk_logical_percent_time: Per-drive disk time percentages
 * - disk_logical_percent_read_time: Per-drive read time percentages
 * - disk_logical_percent_write_time: Per-drive write time percentages
 * - disk_logical_percent_idle_time: Per-drive idle time percentages
 * 
 * === SYSTEM KERNEL METRICS ===
 * - system_context_switches_per_sec: Context switches per second
 * - system_system_calls_per_sec: System calls per second
 * - system_processor_queue_length: Processor queue length
 * - system_processes: Number of processes
 * - system_threads: Number of threads
 *
 * NOTE: Metric availability depends on system configuration and Windows version.
 * This interface provides access to comprehensive system-wide performance counters
 * as defined in PdhMetricDefinitions.h.
 */

#pragma once

#include "pdh/PdhMetricsManager.h"
#include "pdh/PdhMetricDefinitions.h"

#include <memory>
#include <string>
#include <vector>
#include <map>

/**
 * Simple, high-level interface for accessing Windows PDH metrics
 * Provides easy access to the high-performance batched PDH collection system
 * 
 * Usage:
 *   // Initialize with specific metrics
 *   PdhInterface pdh({PdhMetrics::MetricCategory::CPU_USAGE, PdhMetrics::MetricCategory::MEMORY_SYSTEM});
 *   
 *   // Start collection
 *   pdh.start();
 *   
 *   // Get metrics
 *   double cpuUsage;
 *   if (pdh.getMetric("cpu_total_usage", cpuUsage)) {
 *       std::cout << "CPU Usage: " << cpuUsage << "%" << std::endl;
 *   }
 *   
 *   // Stop when done
 *   pdh.stop();
 */
class PdhInterface {
public:
    // Constructors for different initialization methods
    
    // Initialize with metric categories
    explicit PdhInterface(const std::vector<PdhMetrics::MetricCategory>& categories,
                         std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    
    // Initialize with specific metrics
    explicit PdhInterface(const std::vector<PdhMetrics::MetricDefinition>& metrics,
                         std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    
    // Initialize with all available metrics (for testing/debugging)
    explicit PdhInterface(std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    
    // Factory methods for common use cases
    static std::unique_ptr<PdhInterface> createForCpuMonitoring(
        std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    
    static std::unique_ptr<PdhInterface> createForSystemMonitoring(
        std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    
    static std::unique_ptr<PdhInterface> createForBenchmarking(
        std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    
    static std::unique_ptr<PdhInterface> createOptimizedForBenchmarking(
        std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    
    static std::unique_ptr<PdhInterface> createMinimal(
        std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    
    static std::unique_ptr<PdhInterface> createForPerDiskMonitoring(
        std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
    
    ~PdhInterface();

    // Non-copyable but movable
    PdhInterface(const PdhInterface&) = delete;
    PdhInterface& operator=(const PdhInterface&) = delete;
    PdhInterface(PdhInterface&&) = default;
    PdhInterface& operator=(PdhInterface&&) = default;

    // Lifecycle management
    bool start();
    void stop();
    bool isRunning() const;

    // Simple metric access
    bool getMetric(const std::string& metricName, double& value) const;
    bool getPerCoreMetric(const std::string& metricName, std::vector<double>& coreValues) const;
    bool getCoreMetric(const std::string& metricName, size_t coreIndex, double& value) const;
    
    // Bulk operations
    std::map<std::string, double> getAllMetrics() const;
    std::vector<std::string> getAvailableMetrics() const;
    
    // Convenience methods for common metrics
    double getCpuUsage() const;              // Total CPU usage percentage
    double getMemoryUsageMB() const;         // Memory usage in MB
    double getDiskReadMBps() const;          // Disk read rate in MB/s
    double getDiskWriteMBps() const;         // Disk write rate in MB/s
    std::vector<double> getPerCoreCpuUsage() const;  // Per-core CPU usage
    
    // System information
    size_t getCpuCoreCount() const;
    bool hasMetric(const std::string& metricName) const;
    bool isMetricValid(const std::string& metricName) const;
    
    // Performance and diagnostics
    std::string getPerformanceReport() const;
    void logStatus() const;
    
    // Configuration access
    std::chrono::milliseconds getCollectionInterval() const;
    void enableDetailedLogging(bool enable);

private:
    std::unique_ptr<PdhMetrics::PdhMetricsManager> manager_;
    
    // Internal initialization
    void initializeManager(const std::vector<PdhMetrics::MetricDefinition>& metrics,
                          std::chrono::milliseconds interval);
};

// Utility functions for metric selection
namespace PdhUtils {
    
    // Get commonly used metric sets
    std::vector<PdhMetrics::MetricDefinition> getCpuMetrics();
    std::vector<PdhMetrics::MetricDefinition> getMemoryMetrics();
    std::vector<PdhMetrics::MetricDefinition> getDiskMetrics();
    std::vector<PdhMetrics::MetricDefinition> getSystemMetrics();
    std::vector<PdhMetrics::MetricDefinition> getBenchmarkingMetrics();
    std::vector<PdhMetrics::MetricDefinition> getMinimalMetrics();
    
    // Helper to print available metrics
    void printAvailableMetrics();
    void printMetricCategories();

}