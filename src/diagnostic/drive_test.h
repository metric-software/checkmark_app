#pragma once
#ifndef DRIVE_TEST_H
#define DRIVE_TEST_H
#include <vector>

#include "ApplicationSettings.h"
#include "diagnostic/DiagnosticDataStore.h"

// Define the DriveTestResults structure in the header
struct DriveTestResults {
  double sequentialWriteMBps;
  double sequentialReadMBps;
  double randomWriteMBps;
  double randomReadMBps;
  double iops4k;
  double accessTimeMs;
};

void runDriveTests();
DriveTestResults testDrivePerformance(const std::string& path);
double probeDriveSpeed(const std::string& path, size_t probeSize);
void measureAccessTime(const std::string& path, DriveTestResults& results);

#endif
