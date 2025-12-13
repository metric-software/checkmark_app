#include "NetworkResponseCleanup.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

static Qt::CaseSensitivity pathCaseSensitivity() {
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

static QString normalizePath(QString path) {
    path = QDir::cleanPath(path);
    path.replace('\\', '/');
    return path;
}

static bool normalizedPathEquals(const QString& a, const QString& b) {
    return QString::compare(normalizePath(a), normalizePath(b), pathCaseSensitivity()) == 0;
}

static bool normalizedPathStartsWithDir(const QString& filePath, const QString& dirPath) {
    QString fileNorm = normalizePath(filePath);
    QString dirNorm = normalizePath(dirPath);
    if (!dirNorm.endsWith('/')) {
        dirNorm += '/';
    }
    return fileNorm.startsWith(dirNorm, pathCaseSensitivity());
}

NetworkResponseCleanupStats clearNetworkResponsesDirectoryInAppDir(const QString& applicationDirPath) {
    NetworkResponseCleanupStats stats;

    const QDir appDir(applicationDirPath);
    const QString absAppDir = normalizePath(appDir.absolutePath());
    const QString absNetworkDir = normalizePath(QDir(absAppDir).absoluteFilePath(QStringLiteral("network_responses")));
    stats.directory = absNetworkDir;

    const QFileInfo appDirInfo(appDir.absolutePath());
    if (!appDirInfo.exists() || !appDirInfo.isDir()) {
        stats.error = QStringLiteral("applicationDirPath is not an existing directory");
        return stats;
    }

    const QFileInfo networkDirInfo(absNetworkDir);
    if (!networkDirInfo.exists()) {
        return stats; // Nothing to clear.
    }
    if (!networkDirInfo.isDir()) {
        stats.error = QStringLiteral("network_responses path exists but is not a directory");
        return stats;
    }
    if (networkDirInfo.isSymLink()) {
        stats.error = QStringLiteral("refusing to clear network_responses because it is a symlink");
        return stats;
    }

    QString canonicalAppDir = normalizePath(QDir(absAppDir).canonicalPath());
    if (canonicalAppDir.isEmpty()) {
        canonicalAppDir = absAppDir;
    }
    QString canonicalNetworkDir = normalizePath(QDir(absNetworkDir).canonicalPath());
    if (canonicalNetworkDir.isEmpty()) {
        canonicalNetworkDir = absNetworkDir;
    }

    if (!canonicalNetworkDir.endsWith(QStringLiteral("/network_responses"), pathCaseSensitivity())) {
        stats.error = QStringLiteral("refusing to clear: directory name is not exactly network_responses");
        return stats;
    }

    const QString expectedCanonicalNetworkDir =
        normalizePath(QDir(canonicalAppDir).absoluteFilePath(QStringLiteral("network_responses")));
    if (!normalizedPathEquals(canonicalNetworkDir, expectedCanonicalNetworkDir)) {
        stats.error = QStringLiteral("refusing to clear: network_responses is not directly under applicationDirPath");
        return stats;
    }

    // Only delete the exact file types BaseApiClient produces.
    // Example:
    //   20251213_222436_893_GET__pb_diagnostics_general_56409fe0.parsed.json
    static const QRegularExpression kAllowedName(
        QStringLiteral(R"(^\d{8}_\d{6}_\d{3}_(GET|POST|PUT|DELETE)_.+_[0-9a-fA-F]{8}\.(raw\.bin|body\.bin|parsed\.json|parsed\.txt|meta\.txt)$)"));

    QDirIterator it(canonicalNetworkDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();

        if (!fi.isFile() || fi.isSymLink()) {
            stats.skipped++;
            continue;
        }

        const QString fileCanonical = normalizePath(fi.canonicalFilePath());
        if (fileCanonical.isEmpty() || !normalizedPathStartsWithDir(fileCanonical, canonicalNetworkDir)) {
            stats.skipped++;
            continue;
        }

        if (!kAllowedName.match(fi.fileName()).hasMatch()) {
            stats.skipped++;
            continue;
        }

        const qint64 size = fi.size();
        if (size < 0) {
            stats.skipped++;
            continue;
        }

        if (QFile::remove(fi.absoluteFilePath())) {
            stats.deleted++;
        } else {
            stats.failed++;
        }
    }

    return stats;
}

NetworkResponseCleanupStats clearNetworkResponsesDirectoryOnStartup() {
    return clearNetworkResponsesDirectoryInAppDir(QCoreApplication::applicationDirPath());
}

