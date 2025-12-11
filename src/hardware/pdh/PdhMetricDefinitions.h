#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>

/**
 * Essential PDH metric definitions for high-performance benchmarking
 * Focused on the most valuable system metrics only
 */

namespace PdhMetrics {

// Metric data structure
struct MetricDefinition {
    std::string name;           // Friendly name for the metric
    std::string counterPath;    // PDH counter path
    std::string category;       // Category for organization
    bool perCore;              // Whether this metric exists per CPU core
    bool requiresBaseline;     // Whether this metric needs baseline collection
};

// Essential metric categories (simplified)
enum class MetricCategory {
    CPU_ESSENTIAL,
    MEMORY_ESSENTIAL,
    DISK_ESSENTIAL,
    SYSTEM_ESSENTIAL,
    ALL_ESSENTIAL
};

// Essential CPU metrics - comprehensive but focused set
const std::vector<MetricDefinition> ESSENTIAL_CPU_METRICS = {
    // CPU usage metrics (total)
    {"cpu_total_usage", "\\Processor(_Total)\\% Processor Time", "cpu_usage", false, true},
    {"cpu_user_time", "\\Processor(_Total)\\% User Time", "cpu_usage", false, true},
    {"cpu_privileged_time", "\\Processor(_Total)\\% Privileged Time", "cpu_usage", false, true},
    {"cpu_idle_time", "\\Processor(_Total)\\% Idle Time", "cpu_usage", false, true},
    
    // CPU usage metrics (per-core)
    {"cpu_per_core_usage", "\\Processor({0})\\% Processor Time", "cpu_usage", true, true},
    
    // CPU frequency metrics (total and per-core)
    {"cpu_actual_frequency", "\\Processor Information(_Total)\\Actual Frequency", "cpu_frequency", false, false},
    {"cpu_per_core_actual_freq", "\\Processor Information(0,{0})\\Actual Frequency", "cpu_frequency", true, false},
    
    // CPU interrupt metrics (total only)
    {"cpu_interrupts_per_sec", "\\Processor(_Total)\\Interrupts/sec", "cpu_interrupts", false, false},
    {"cpu_dpc_time", "\\Processor(_Total)\\% DPC Time", "cpu_interrupts", false, true},
    {"cpu_interrupt_time", "\\Processor(_Total)\\% Interrupt Time", "cpu_interrupts", false, true},
    {"cpu_dpcs_queued_per_sec", "\\Processor(_Total)\\DPCs Queued/sec", "cpu_interrupts", false, false},
    {"cpu_dpc_rate", "\\Processor(_Total)\\DPC Rate", "cpu_interrupts", false, false},
    
    // CPU power state metrics (total only)
    {"cpu_c1_time", "\\Processor(_Total)\\% C1 Time", "cpu_power", false, true},
    {"cpu_c2_time", "\\Processor(_Total)\\% C2 Time", "cpu_power", false, true},
    {"cpu_c3_time", "\\Processor(_Total)\\% C3 Time", "cpu_power", false, true},
    {"cpu_c1_transitions_per_sec", "\\Processor(_Total)\\C1 Transitions/sec", "cpu_power", false, false},
    {"cpu_c2_transitions_per_sec", "\\Processor(_Total)\\C2 Transitions/sec", "cpu_power", false, false},
    {"cpu_c3_transitions_per_sec", "\\Processor(_Total)\\C3 Transitions/sec", "cpu_power", false, false}
};

// Essential Memory metrics - enhanced with additional valuable metrics
const std::vector<MetricDefinition> ESSENTIAL_MEMORY_METRICS = {
    // Core memory metrics
    {"memory_available_mbytes", "\\Memory\\Available MBytes", "memory_system", false, false},
    {"memory_committed_bytes", "\\Memory\\Committed Bytes", "memory_system", false, false},
    {"memory_commit_limit", "\\Memory\\Commit Limit", "memory_system", false, false},
    
    // Memory activity metrics
    {"memory_page_faults_per_sec", "\\Memory\\Page Faults/sec", "memory_system", false, false},
    {"memory_pages_per_sec", "\\Memory\\Pages/sec", "memory_system", false, false},
    
    // Memory pool metrics (important for system health)
    {"memory_pool_nonpaged_bytes", "\\Memory\\Pool Nonpaged Bytes", "memory_system", false, false},
    {"memory_pool_paged_bytes", "\\Memory\\Pool Paged Bytes", "memory_system", false, false},
    
    // System code metrics
    {"memory_system_code_bytes", "\\Memory\\System Code Total Bytes", "memory_system", false, false},
    {"memory_system_driver_bytes", "\\Memory\\System Driver Total Bytes", "memory_system", false, false}
};

// Essential Disk metrics - enhanced with comprehensive I/O and latency metrics
const std::vector<MetricDefinition> ESSENTIAL_DISK_METRICS = {
    // Physical disk I/O metrics
    {"disk_read_bytes_per_sec", "\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", "disk_io", false, false},
    {"disk_write_bytes_per_sec", "\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", "disk_io", false, false},
    {"disk_reads_per_sec", "\\PhysicalDisk(_Total)\\Disk Reads/sec", "disk_io", false, false},
    {"disk_writes_per_sec", "\\PhysicalDisk(_Total)\\Disk Writes/sec", "disk_io", false, false},
    {"disk_transfers_per_sec", "\\PhysicalDisk(_Total)\\Disk Transfers/sec", "disk_io", false, false},
    {"disk_bytes_per_sec", "\\PhysicalDisk(_Total)\\Disk Bytes/sec", "disk_io", false, false},
    
    // Physical disk latency and queue metrics
    {"disk_avg_read_queue_length", "\\PhysicalDisk(_Total)\\Avg. Disk Read Queue Length", "disk_latency", false, false},
    {"disk_avg_write_queue_length", "\\PhysicalDisk(_Total)\\Avg. Disk Write Queue Length", "disk_latency", false, false},
    {"disk_avg_queue_length", "\\PhysicalDisk(_Total)\\Avg. Disk Queue Length", "disk_latency", false, false},
    {"disk_avg_read_time", "\\PhysicalDisk(_Total)\\Avg. Disk sec/Read", "disk_latency", false, false},
    {"disk_avg_write_time", "\\PhysicalDisk(_Total)\\Avg. Disk sec/Write", "disk_latency", false, false},
    {"disk_avg_transfer_time", "\\PhysicalDisk(_Total)\\Avg. Disk sec/Transfer", "disk_latency", false, false},
    {"disk_percent_time", "\\PhysicalDisk(_Total)\\% Disk Time", "disk_latency", false, true},
    {"disk_percent_read_time", "\\PhysicalDisk(_Total)\\% Disk Read Time", "disk_latency", false, true},
    {"disk_percent_write_time", "\\PhysicalDisk(_Total)\\% Disk Write Time", "disk_latency", false, true},
    
    // Logical disk utilization metrics (per-drive)
    {"disk_logical_percent_time", "\\LogicalDisk(*)\\% Disk Time", "per_disk", false, true},
    {"disk_logical_percent_read_time", "\\LogicalDisk(*)\\% Disk Read Time", "per_disk", false, true},
    {"disk_logical_percent_write_time", "\\LogicalDisk(*)\\% Disk Write Time", "per_disk", false, true},
    {"disk_logical_percent_idle_time", "\\LogicalDisk(*)\\% Idle Time", "per_disk", false, true}
};

// Essential System kernel metrics
const std::vector<MetricDefinition> ESSENTIAL_SYSTEM_METRICS = {
    {"system_context_switches_per_sec", "\\System\\Context Switches/sec", "system_kernel", false, false},
    {"system_system_calls_per_sec", "\\System\\System Calls/sec", "system_kernel", false, false},
    {"system_processor_queue_length", "\\System\\Processor Queue Length", "system_kernel", false, false},
    {"system_processes", "\\System\\Processes", "system_kernel", false, false},
    {"system_threads", "\\System\\Threads", "system_kernel", false, false}
};

// Helper functions to get metric sets
class MetricSelector {
public:
    static std::vector<MetricDefinition> getMetricsForCategory(MetricCategory category);
    static std::vector<MetricDefinition> getMetricsForCategories(const std::vector<MetricCategory>& categories);
    static std::vector<MetricDefinition> getAllEssentialMetrics();
    
    // Get metrics grouped by PDH object type for optimal batching
    static std::map<std::string, std::vector<MetricDefinition>> getMetricsGroupedByObject(
        const std::vector<MetricDefinition>& metrics);
    
    // Validate metric selection and check for dependencies
    static bool validateMetricSelection(const std::vector<MetricDefinition>& metrics, 
                                       std::vector<std::string>& errors);
    
    // Get the minimum set of metrics needed (resolves dependencies)
    static std::vector<MetricDefinition> resolveMetricDependencies(
        const std::vector<MetricDefinition>& requestedMetrics);

    // Essential metric set getters
    static std::vector<MetricDefinition> getEssentialCpuMetrics();
    static std::vector<MetricDefinition> getEssentialMemoryMetrics();
    static std::vector<MetricDefinition> getEssentialDiskMetrics();
    static std::vector<MetricDefinition> getEssentialSystemMetrics();
    
    // Combined essential set for benchmarking
    static std::vector<MetricDefinition> getEssentialBenchmarkingMetrics();
    
    // Minimal set for basic monitoring
    static std::vector<MetricDefinition> getMinimalMetrics();
};

} // namespace PdhMetrics 