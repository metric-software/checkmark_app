#pragma once
#ifndef MEMORY_TEST_H
#define MEMORY_TEST_H
#include <future>

#include "ApplicationSettings.h"
#include "diagnostic/DiagnosticDataStore.h"

void getMemoryInfo();
void getPageFileInfo();
void runMemoryTests();
void runMemoryTestsMultiple(DiagnosticDataStore::MemoryData* metrics);
std::future<void> runMemoryTestsAsync(DiagnosticDataStore::MemoryData* metrics);
std::string checkXMPStatus(int memoryType, int speed, int configuredSpeed);
std::string checkDualChannelStatus(int moduleCount, int memoryType,
                                   int configuredSpeed);
DiagnosticDataStore::MemoryData::StabilityTestResults runMemoryStabilityTest(
  size_t memorySizeMB = 256);

#endif
