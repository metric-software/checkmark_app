#pragma once
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStorageInfo>
#include <QString>
#include <QThread>

#include "logging/Logger.h"

class RustBenchmarkFinder {
 public:
  static QString findLatestBenchmark() {
    QStringList possiblePaths;

    // Check Steam registry first
    QSettings steamRegistry(
      "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam",
      QSettings::NativeFormat);
    QString steamPath = steamRegistry.value("InstallPath").toString();
    if (!steamPath.isEmpty()) {
      possiblePaths << steamPath + "/steamapps/common/Rust";
    }

    // Add common Steam paths
    possiblePaths << "C:/Program Files (x86)/Steam/steamapps/common/Rust"
                  << "C:/Program Files/Steam/steamapps/common/Rust";

    // Add all drives
    for (const QStorageInfo& drive : QStorageInfo::mountedVolumes()) {
      if (drive.isValid() && drive.isReady()) {
        possiblePaths << drive.rootPath() +
                           "SteamLibrary/steamapps/common/Rust";
      }
    }

    // Find latest benchmark file
    QDateTime latestTime;
    QString latestFile;

    for (const QString& basePath : possiblePaths) {
      QString benchmarkPath = basePath + "/benchmark";
      QDir dir(benchmarkPath);

      if (dir.exists()) {
        QStringList filters;
        filters << "*.json";
        // Sort by last modified time, newest first (removed QDir::Reversed)
        QFileInfoList files =
          dir.entryInfoList(filters, QDir::Files, QDir::Time);

        if (!files.isEmpty()) {
          QFileInfo newest = files.first();
          if (latestFile.isEmpty() || newest.lastModified() > latestTime) {
            latestTime = newest.lastModified();
            latestFile = newest.absoluteFilePath();
          }
        }
      }
    }

    return latestFile;
  }

  static QList<QPair<QString, QJsonObject>> findRecentBenchmarks(
    const QDateTime& startTime, const QDateTime& endTime) {
    QList<QPair<QString, QJsonObject>> recentBenchmarks;
    QStringList possiblePaths;

    try {
      // Get possible Rust paths
      QSettings steamRegistry(
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam",
        QSettings::NativeFormat);
      QString steamPath = steamRegistry.value("InstallPath").toString();
      if (!steamPath.isEmpty()) {
        possiblePaths << steamPath + "/steamapps/common/Rust";
      }

      possiblePaths << "C:/Program Files (x86)/Steam/steamapps/common/Rust"
                    << "C:/Program Files/Steam/steamapps/common/Rust";

      for (const QStorageInfo& drive : QStorageInfo::mountedVolumes()) {
        if (drive.isValid() && drive.isReady()) {
          possiblePaths << drive.rootPath() +
                             "SteamLibrary/steamapps/common/Rust";
        }
      }

      // Find all benchmark files within the time window
      for (const QString& basePath : possiblePaths) {
        QString benchmarkPath = basePath + "/benchmark";
        QDir dir(benchmarkPath);

        if (dir.exists()) {
          QStringList filters;
          filters << "*.json";
          QFileInfoList files =
            dir.entryInfoList(filters, QDir::Files, QDir::Time);

          for (const QFileInfo& file : files) {
            QDateTime fileTime = file.lastModified();

            // Only include files created during our benchmark
            if (fileTime >= startTime && fileTime <= endTime) {
              // Add small delay to ensure file is fully written
              QThread::msleep(100);

              QJsonObject benchData =
                readBenchmarkData(file.absoluteFilePath());
              if (!benchData.isEmpty()) {
                recentBenchmarks.append({file.absoluteFilePath(), benchData});
              }
            }
          }
        }
      }
    } catch (const std::exception& e) {
      LOG_INFO << "Error finding recent benchmarks: " << e.what();
    }

    return recentBenchmarks;
  }

  static QJsonObject readBenchmarkData(const QString& path) {
    try {
      QFile file(path);
      if (!file.open(QIODevice::ReadOnly)) {
        LOG_INFO << "Failed to open benchmark file: [path hidden for privacy]";
        return QJsonObject();
      }

      // Add small delay to ensure file is fully accessible
      QThread::msleep(50);

      QByteArray data = file.readAll();
      file.close();

      QJsonDocument doc = QJsonDocument::fromJson(data);
      if (doc.isNull()) {
        LOG_INFO << "Failed to parse benchmark JSON from: [path hidden for privacy]";
        return QJsonObject();
      }

      return doc.object();
    } catch (const std::exception& e) {
      LOG_INFO << "Error reading benchmark data: " << e.what();
      return QJsonObject();
    }
  }
};
