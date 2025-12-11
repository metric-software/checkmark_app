#pragma once

#include <iostream>

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QString>
#include <QUrl>
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>

class HtmlReportGenerator {
 public:
  // Open an HTML file in the default browser
  static void openHtmlInBrowser(const QString& filePath);
};
