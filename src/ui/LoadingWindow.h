#pragma once

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

class LoadingWindow : public QDialog {
  Q_OBJECT

 public:
  LoadingWindow(QWidget* parent = nullptr);
  ~LoadingWindow() = default;

 public slots:
  void setProgress(int value);
  void setStatusMessage(const QString& message);

 private:
  QProgressBar* progressBar;
  QLabel* statusLabel;
  QLabel* titleLabel;
};
