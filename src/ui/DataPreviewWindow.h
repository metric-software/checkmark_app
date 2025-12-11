#pragma once
#include <QDialog>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include "SilentNotificationBanner.h"

class DataPreviewWindow : public QDialog {
  Q_OBJECT

 public:
  explicit DataPreviewWindow(QWidget* parent = nullptr);

  // Add a file to preview
  void addFile(const QString& filePath);

  // Clear all files
  void clearFiles();

 private slots:
  void onUploadClicked();
  void onCancelClicked();

 private:
  void setupUI();
  void loadFileContent(const QString& filePath, int tabIndex);

  QTabWidget* tabWidget;
  QPushButton* uploadButton;
  QPushButton* cancelButton;
  SilentNotificationBanner* notificationBanner;

  // Map to store file paths and their associated widgets
  QMap<int, QString> tabToFilePath;
};
