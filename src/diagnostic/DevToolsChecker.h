#pragma once

#include <QDir>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

class DevToolsChecker : public QObject {
  Q_OBJECT
 public:
  explicit DevToolsChecker(QObject* parent = nullptr);

  void checkAllTools();

 signals:
  void toolCheckResult(const QString& tool, bool found, const QString& version);
  void toolCheckCompleted(const QString& formattedResults);
  void logMessage(const QString& message);

 public slots:
  void checkPythonInstalls();
  void checkNodeInstall();
  void checkGitInstall();
  void checkJavaInstalls();
  void checkCudaInstall();
  void checkCuDNNInstall();
  void checkFFmpegInstall();
  void checkVSInstall();
  void checkAdditionalTools();

 private:
  QString devToolsResults;

  void addResult(const QString& tool, bool found, const QString& version = "");
};
