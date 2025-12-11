#include "LoadingWindow.h"

#include <QApplication>
#include <QScreen>

LoadingWindow::LoadingWindow(QWidget* parent)
    : QDialog(parent,
              Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
  setWindowTitle("Loading");
  setFixedSize(400, 150);

  QVBoxLayout* layout = new QVBoxLayout(this);
  layout->setContentsMargins(20, 20, 20, 20);

  // Add title label
  titleLabel = new QLabel("Initializing System Metrics Tools", this);
  titleLabel->setStyleSheet(
    "font-size: 16px; font-weight: bold; color: #ffffff; border: none;");
  titleLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(titleLabel);

  // Add spacer
  layout->addSpacing(10);

  // Add status label
  statusLabel = new QLabel("Starting up...", this);
  statusLabel->setStyleSheet("color: #dddddd; border: none;");
  statusLabel->setAlignment(Qt::AlignCenter);
  layout->addWidget(statusLabel);

  // Add spacer
  layout->addSpacing(15);

  // Add progress bar
  progressBar = new QProgressBar(this);
  progressBar->setRange(0, 100);
  progressBar->setValue(0);
  progressBar->setTextVisible(true);
  progressBar->setStyleSheet(R"(
        QProgressBar {
            border: 1px solid #333333;
            border-radius: 4px;
            background-color: #1e1e1e;
            text-align: center;
            color: white;
            height: 20px;
        }
        QProgressBar::chunk {
            background-color: #0078d4;
            border-radius: 3px;
        }
    )");
  layout->addWidget(progressBar);

  // Set window styling
  setStyleSheet("background-color: #1a1a1a; border: 1px solid #333333;");

  // Center the dialog on screen
  setWindowModality(Qt::ApplicationModal);

  // Center on screen
  const QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
  const int x = (screenGeometry.width() - width()) / 2;
  const int y = (screenGeometry.height() - height()) / 2;
  move(x, y);
}

void LoadingWindow::setProgress(int value) {
  progressBar->setValue(value);
  QApplication::processEvents();  // Force UI update
}

void LoadingWindow::setStatusMessage(const QString& message) {
  statusLabel->setText(message);
  QApplication::processEvents();  // Force UI update
}
