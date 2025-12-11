#include "CustomConsoleWindow.h"

#include <QApplication>
#include <QCloseEvent>
#include <QFont>
#include <QRegularExpression>
#include <QScrollBar>

#include "../ApplicationSettings.h"

// Initialize static members
CustomConsoleWindow* CustomConsoleWindow::instance = nullptr;
std::mutex CustomConsoleWindow::textMutex;
std::mutex ConsoleOutputBuf::outputMutex;

CustomConsoleWindow::CustomConsoleWindow(QWidget* parent)
    : QWidget(parent, Qt::Window) {
  setWindowTitle("checkmark Console Output");
  setMinimumSize(800, 600);

  // Create layout
  QVBoxLayout* layout = new QVBoxLayout(this);

  // Create text display with monospace font
  textDisplay = new QTextEdit(this);
  textDisplay->setReadOnly(true);
  textDisplay->setAcceptRichText(true);  // Enable HTML formatting
  QFont font("Consolas, Courier New, monospace");
  font.setPointSize(9);
  textDisplay->setFont(font);
  textDisplay->setStyleSheet("background-color: #1a1a1a; color: #d4d4d4;");
  layout->addWidget(textDisplay);

  // Create clear button
  clearButton = new QPushButton("Clear Console", this);
  clearButton->setStyleSheet(R"(
        QPushButton {
            background-color: #333333;
            color: white;
            border: none;
            padding: 6px 12px;
            border-radius: 4px;
        }
        QPushButton:hover { background-color: #404040; }
        QPushButton:pressed { background-color: #2a2a2a; }
    )");
  connect(clearButton, &QPushButton::clicked, this,
          &CustomConsoleWindow::clearText);

  // Create toggle tag button
  toggleTagButton = new QPushButton("Hide Tags", this);
  toggleTagButton->setStyleSheet(R"(
        QPushButton {
            background-color: #333333;
            color: white;
            border: none;
            padding: 6px 12px;
            border-radius: 4px;
        }
        QPushButton:hover { background-color: #404040; }
        QPushButton:pressed { background-color: #2a2a2a; }
    )");
  connect(toggleTagButton, &QPushButton::clicked, this,
          &CustomConsoleWindow::toggleTagVisibility);

  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  buttonLayout->addWidget(toggleTagButton);
  buttonLayout->addWidget(clearButton);
  layout->addLayout(buttonLayout);
}

CustomConsoleWindow::~CustomConsoleWindow() { instance = nullptr; }

CustomConsoleWindow* CustomConsoleWindow::getInstance() {
  if (!instance) {
    // Create on first use
    instance = new CustomConsoleWindow();
  }
  return instance;
}

void CustomConsoleWindow::setVisibilityFromSettings() {
  bool showConsole = ApplicationSettings::getInstance().getConsoleVisible();
  setVisible(showConsole);
}

void CustomConsoleWindow::appendText(const QString& text) {
  std::lock_guard<std::mutex> lock(textMutex);

  // Update UI in the main thread
  QMetaObject::invokeMethod(
    this,
    [this, text]() {
      // Store the original text for later reformatting
      originalLogEntries.append(text);
      
      // Parse and format the log message with orange tags
      QString formattedText = formatLogMessage(text);
      textDisplay->append(formattedText);

      // Auto-scroll to bottom
      QScrollBar* scrollbar = textDisplay->verticalScrollBar();
      scrollbar->setValue(scrollbar->maximum());
    },
    Qt::QueuedConnection);
}

QString CustomConsoleWindow::formatLogMessage(const QString& text) {
  // Check if this looks like a log message from our logger
  // Format: YYYY-MM-DD HH:MM:SS.mmm [LEVEL] [tid=XXXXX] (filename:line function) message
  
  // Simple regex to match the logger format - find the end of the tag part
  // Look for pattern: timestamp [LEVEL] [tid=...] (...) then the message starts
  static QRegularExpression logPattern(R"(^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} \[.*?\](?:\s*\[tid=\d+\])?\s*\([^)]+\))\s*(.*)$)");
  
  QRegularExpressionMatch match = logPattern.match(text);
  if (match.hasMatch()) {
    QString tagPart = match.captured(1);
    QString messagePart = match.captured(2);
    
    // Determine message color based on log level
    QString messageColor = "#d4d4d4"; // Default white
    if (tagPart.contains("[ERROR]")) {
      messageColor = "#FFFF99"; // Light yellow for ERROR
    } else if (tagPart.contains("[FATAL]")) {
      messageColor = "#FF6B6B"; // Red for FATAL
    }
    
    // Format based on showTags setting
    if (showTags) {
      return QString("<span style='color: #FFA500;'>%1</span> <span style='color: %2;'>%3</span>")
          .arg(tagPart.toHtmlEscaped())
          .arg(messageColor)
          .arg(messagePart.toHtmlEscaped());
    } else {
      // Show only the message part when tags are hidden
      return QString("<span style='color: %1;'>%2</span>")
          .arg(messageColor)
          .arg(messagePart.toHtmlEscaped());
    }
  }
  
  // If it doesn't match our log format, return as-is with default color
  return QString("<span style='color: #d4d4d4;'>%1</span>").arg(text.toHtmlEscaped());
}

void CustomConsoleWindow::clearText() { 
  textDisplay->clear(); 
  originalLogEntries.clear();
}

void CustomConsoleWindow::toggleTagVisibility() {
  showTags = !showTags;
  toggleTagButton->setText(showTags ? "Hide Tags" : "Show Tags");
  
  // Refresh all existing content with new tag visibility setting
  refreshAllContent();
}

void CustomConsoleWindow::refreshAllContent() {
  // Clear the display
  textDisplay->clear();
  
  // Reformat and append all stored entries
  for (const QString& entry : originalLogEntries) {
    QString formattedText = formatLogMessage(entry);
    textDisplay->append(formattedText);
  }
  
  // Auto-scroll to bottom
  QScrollBar* scrollbar = textDisplay->verticalScrollBar();
  scrollbar->setValue(scrollbar->maximum());
}

void CustomConsoleWindow::closeEvent(QCloseEvent* event) {
  // Hide instead of closing when X is clicked
  event->ignore();
  hide();
}

void CustomConsoleWindow::cleanup() {
  if (instance) {
    instance->hide();
    instance->deleteLater();
    instance = nullptr;
  }
}

// ConsoleOutputBuf implementation
ConsoleOutputBuf::ConsoleOutputBuf(std::streambuf* fileBuf)
    : fileBuffer(fileBuf) {}

ConsoleOutputBuf::~ConsoleOutputBuf() { sync(); }

int ConsoleOutputBuf::overflow(int c) {
  if (c == EOF) return EOF;

  if (c == '\n') {
    buffer += static_cast<char>(c);

    // Lock and output the complete line
    {
      std::lock_guard<std::mutex> lock(outputMutex);

      // Write to file if available
      if (fileBuffer) {
        for (char ch : buffer) {
          fileBuffer->sputc(ch);
        }
        fileBuffer->pubsync();
      }

      // Send to custom console window
      if (CustomConsoleWindow::getInstance()) {
        CustomConsoleWindow::getInstance()->appendText(
          QString::fromStdString(buffer));
      }
    }

    buffer.clear();
    return c;
  }

  buffer += static_cast<char>(c);
  return c;
}

int ConsoleOutputBuf::sync() {
  if (!buffer.empty()) {
    // Lock and output any remaining content
    {
      std::lock_guard<std::mutex> lock(outputMutex);

      // Write to file if available
      if (fileBuffer) {
        for (char ch : buffer) {
          fileBuffer->sputc(ch);
        }
        fileBuffer->pubsync();
      }

      // Send to custom console window if available
      if (CustomConsoleWindow::getInstance()) {
        CustomConsoleWindow::getInstance()->appendText(
          QString::fromStdString(buffer));
      }
    }

    buffer.clear();
  }

  return 0;
}
