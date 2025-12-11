#pragma once

// Define the structure here so it can be included where needed
struct CoreBoostMetrics {
  int coreNumber;  // Add this field to match the JSON data
  int idleClock;
  int singleLoadClock;
  int allCoreClock;
  int boostDelta;  // Add this field to match the JSON data
};
