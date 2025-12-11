#pragma once
#include <map>

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStorageInfo>
#include <QString>
#include <QStringList>
#include <QTextStream>

class RustConfigFinder {
 public:
  static QString findConfigFile() {
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

    // Find first valid Rust installation by checking for RustClient.exe
    for (const QString& path : possiblePaths) {
      QFileInfo exeFile(path + "/RustClient.exe");
      if (exeFile.exists() && exeFile.isFile()) {
        QString configPath = path + "/cfg/client.cfg";
        if (QFile::exists(configPath)) {
          return configPath;
        }
      }
    }

    return QString();
  }

  static QString readRawConfig(const QString& configPath) {
    QString rawConfig;
    QFile file(configPath);

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      rawConfig = in.readAll();
      file.close();
    }

    return rawConfig;
  }

  static std::map<QString, QString> parseConfig(const QString& configPath) {
    std::map<QString, QString> config;
    QFile file(configPath);

    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        // Keep only actual config lines (not comments or empty lines)
        if (!line.isEmpty() && !line.startsWith("//")) {
          // Handle both = and space-separated values
          QString key, value;
          int equalPos = line.indexOf('=');
          if (equalPos > 0) {
            key = line.left(equalPos).trimmed();
            value = line.mid(equalPos + 1).trimmed();
          } else {
            // Handle Rust's space-separated format: "key value"
            int spacePos = line.indexOf(' ');
            if (spacePos > 0) {
              key = line.left(spacePos).trimmed();
              value = line.mid(spacePos + 1).trimmed();
              // Remove quotes if present
              value = value.mid(1, value.length() - 2);
            }
          }
          if (!key.isEmpty() && !value.isEmpty()) {
            config[key] = value;
          }
        }
      }
      file.close();
    }

    return config;
  }
};
