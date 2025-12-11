#include "HtmlReportGenerator.h"

#include "logging/Logger.h"

void HtmlReportGenerator::openHtmlInBrowser(const QString& filePath) {
  QFileInfo fileInfo(filePath);
  if (!fileInfo.exists()) {
    LOG_ERROR << "Error: HTML file does not exist: [path hidden for privacy]";
    return;
  }

  QString absolutePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
  LOG_INFO << "Opening HTML file in browser: [path hidden for privacy]";

#ifdef Q_OS_WIN
  // 1) Determine default browser executable via AssocQueryString
  wchar_t browser[MAX_PATH] = {0};
  DWORD cch = _countof(browser);
  HRESULT hr = AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE,
                                 L"http",  // query the "http" protocol
                                 L"open",  // the "open" verb
                                 browser, &cch);

  if (SUCCEEDED(hr)) {
    // Convert and quote as needed
    QString exePath = QString::fromWCharArray(browser);
    LOG_INFO << "Default browser exe: [path hidden for privacy]";

    // 2) Launch it with ShellExecute, passing the file:// URL
    QString url = QUrl::fromLocalFile(absolutePath).toString();
    HINSTANCE result =
      ShellExecuteW(nullptr, L"open", (LPCWSTR)exePath.utf16(),
                    (LPCWSTR)url.utf16(), nullptr, SW_SHOWNORMAL);

    if ((intptr_t)result > 32) {  // Success
      LOG_INFO << "Successfully opened HTML file using default browser";
      return;
    }
  }
#endif

  // Fallback method 1: Try using QDesktopServices (cross-platform approach)
  bool success = QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath));
  if (success) {
    LOG_INFO << "Successfully opened HTML file using QDesktopServices";
    return;
  }

#ifdef Q_OS_WIN
  // Fallback method 2: On Windows, try direct command line approach
  QString cmd = QString("cmd.exe /c start \"\" \"%1\"").arg(absolutePath);
  success = QProcess::startDetached(cmd);
  if (success) {
    LOG_INFO << "Successfully opened HTML file using command line";
    return;
  }
#endif

  // Show error message if all methods failed
  QMessageBox::warning(
    nullptr, "Browser Launch Failed",
    "Could not open the HTML file in a browser automatically.\n\n"
    "The file is located at:\n" +
      absolutePath +
      "\n\n"
      "Please open this file manually in your web browser.");
}
