#pragma once

#include <mutex>

#include <QPushButton>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

class CustomConsoleWindow : public QWidget {
  Q_OBJECT

 public:
  explicit CustomConsoleWindow(QWidget* parent = nullptr);
  ~CustomConsoleWindow();

  // Append text to the console
  void appendText(const QString& text);

  // Get singleton instance
  static CustomConsoleWindow* getInstance();

  // Set visibility based on settings
  void setVisibilityFromSettings();

  // Static cleanup method
  static void cleanup();

 public slots:
  void clearText();
  void toggleTagVisibility();

 private:
  QTextEdit* textDisplay;
  QPushButton* clearButton;
  QPushButton* toggleTagButton;
  bool showTags = true;
  static std::mutex textMutex;
  
  // Store original log entries for reformatting
  QStringList originalLogEntries;

  // Format log message with colored tags
  QString formatLogMessage(const QString& text);
  
  // Reformat all existing content
  void refreshAllContent();

  // Override to hide instead of close when X is clicked
  void closeEvent(QCloseEvent* event) override;

  // Singleton instance
  static CustomConsoleWindow* instance;
};

// Custom streambuf to redirect std::cout to both file and our window
class ConsoleOutputBuf : public std::streambuf {
 private:
  std::streambuf* fileBuffer;
  std::string buffer;
  static std::mutex outputMutex;

 protected:
  int overflow(int c) override;
  int sync() override;

 public:
  explicit ConsoleOutputBuf(std::streambuf* fileBuf = nullptr);
  ~ConsoleOutputBuf();
};
