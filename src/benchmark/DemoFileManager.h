#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class DemoFileManager : public QObject {
  Q_OBJECT
 public:
  explicit DemoFileManager(QObject* parent = nullptr);

  static constexpr qint64 EXPECTED_MIN_SIZE = 1024;      // 1KB
  static constexpr qint64 EXPECTED_MAX_SIZE = 10485760;  // 10MB

  bool copyDemoFile(const QString& destPath);
  bool copyDemoFiles(const QString& destPath);
  QString findRustDemosFolder() const;
  QString findLatestBenchmarkFile() const;

  // New methods to manage benchmark files
  QString findAppBenchmarkFile() const;
  bool isBenchmarkFileInRustDemos(const QString& benchmarkFilename);
  QString getCurrentBenchmarkFilename() const;
  bool copyAppBenchmarkToRustDemos();

  // Validation methods
  bool checkBenchmarkPrerequisites(const QString& processName);
  QString findRustInstallationPath() const;
  bool verifyRustPath(const QString& path) const;
  bool verifyDemosFolder(const QString& path) const;
  bool verifyBenchmarkFolder(const QString& path) const;
  QString normalizeRustPath(const QString& path) const;

  // Settings methods
  void saveRustPath(const QString& path);
  QString getSavedRustPath() const;

 signals:
  void validationError(const QString& error);

 private:
  QString findSourceDemoFile() const;
  QStringList findSourceDemoFiles() const;
  bool validateDemoFile(const QString& path) const;
  bool ensureDirectoryExists(const QString& path) const;
  bool isValidDemoFile(const QString& path) const;
  QString m_benchmarkFileName;
};
