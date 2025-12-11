#pragma once

#include <QString>

class BenchmarkSpecsFileManager {
public:
    static void saveSystemSpecsToFile(const QString& benchmarkFileName, bool isUnfinished = false);
    static void updateSpecsFileStatus(const QString& specsFilePath, bool isUnfinished);
    static QString generateNewBenchmarkHash();
    static QString getSystemWarnings();  // Made public
    
private:
    // Helper methods
    static bool checkVirtualization();
    static bool checkHyperV();
    static void checkGameModeStatus();
    static void checkPowerPlan();
};