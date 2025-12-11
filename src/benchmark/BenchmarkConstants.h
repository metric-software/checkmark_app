// Shared constants for benchmark timing
#pragma once

namespace BenchmarkConstants {
  // Benchmark timing constants
  static constexpr double TARGET_BENCHMARK_DURATION = 124.0;  // seconds
  static constexpr double BENCHMARK_END_BUFFER = 10.0;        // seconds
  static constexpr double MAX_BENCHMARK_TIME = 600.0;         // seconds
  static constexpr double JSON_FILE_CHECK_DELAY = 3.0;        // seconds
  static constexpr double COOLDOWN_DURATION = 20.0;           // seconds

  // Data collection constants
  static constexpr int METRICS_COLLECTION_INTERVAL_MS = 1000; // 1 second
  static constexpr size_t MAX_FRAME_HISTORY = 500;           // frames
  static constexpr size_t MAX_QUEUE_SIZE = 2000;             // items
  
  // Frame time analysis constants
  static constexpr float FRAME_TIME_WINDOW_SECONDS = 5.0f;    // rolling window
  static constexpr float FPS_PERCENTILE_1 = 99.0f;           // 1% low FPS
  static constexpr float FPS_PERCENTILE_5 = 95.0f;           // 5% low FPS  
  static constexpr float FPS_PERCENTILE_01 = 99.5f;          // 0.1% low FPS
  

}  // namespace BenchmarkConstants
