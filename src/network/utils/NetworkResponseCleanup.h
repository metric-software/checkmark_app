#pragma once

#include <QString>

struct NetworkResponseCleanupStats {
    int deleted = 0;
    int skipped = 0;
    int failed = 0;
    QString directory;
    QString error;
};

// Clears BaseApiClient's on-disk network response dumps under:
//   <applicationDirPath>/network_responses
// This function is intentionally strict about what it deletes:
// - Only operates on the exact "network_responses" directory directly under applicationDirPath
// - Only deletes regular, non-symlink files with expected naming + extensions
// - Never recurses into subdirectories
NetworkResponseCleanupStats clearNetworkResponsesDirectoryInAppDir(const QString& applicationDirPath);

// Convenience wrapper around QCoreApplication::applicationDirPath().
// Safe to call once during startup after a Q(Core)Application exists.
NetworkResponseCleanupStats clearNetworkResponsesDirectoryOnStartup();

