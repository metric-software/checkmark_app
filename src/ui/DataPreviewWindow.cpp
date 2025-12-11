#include "DataPreviewWindow.h"

#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QScrollBar>
#include <QTextStream>

#include "SilentNotificationBanner.h"

DataPreviewWindow::DataPreviewWindow(QWidget* parent) : QDialog(parent) {
  setWindowTitle("Data Preview");
  setMinimumSize(800, 800);

  setupUI();
}

void DataPreviewWindow::setupUI() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Add notification banner at the top
  notificationBanner = new SilentNotificationBanner(this);
  mainLayout->addWidget(notificationBanner);

  // Create tab widget
  tabWidget = new QTabWidget(this);
  mainLayout->addWidget(tabWidget);

  // Create button layout
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  uploadButton = new QPushButton("Upload", this);
  cancelButton = new QPushButton("Cancel", this);

  // Style buttons
  uploadButton->setStyleSheet(R"(
        QPushButton {
            background-color: #0078d4;
            color: white;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
        }
        QPushButton:hover { background-color: #1084d8; }
        QPushButton:pressed { background-color: #006cc1; }
    )");

  cancelButton->setStyleSheet(R"(
        QPushButton {
            background-color: #333333;
            color: white;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
        }
        QPushButton:hover { background-color: #404040; }
        QPushButton:pressed { background-color: #292929; }
    )");

  buttonLayout->addStretch();
  buttonLayout->addWidget(cancelButton);
  buttonLayout->addWidget(uploadButton);

  mainLayout->addLayout(buttonLayout);

  // Connect signals
  connect(uploadButton, &QPushButton::clicked, this,
          &DataPreviewWindow::onUploadClicked);
  connect(cancelButton, &QPushButton::clicked, this,
          &DataPreviewWindow::onCancelClicked);
}

void DataPreviewWindow::addFile(const QString& filePath) {
  QFileInfo fileInfo(filePath);
  int tabIndex = tabWidget->count();

  // Create a new tab with the file name
  QString tabName = fileInfo.fileName();

  if (fileInfo.suffix().toLower() == "csv") {
    // Create a table widget for CSV files
    QTableWidget* tableWidget = new QTableWidget(this);
    tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabWidget->addTab(tableWidget, tabName);
  } else {
    // Create a text edit for JSON and TXT files
    QTextEdit* textEdit = new QTextEdit(this);
    textEdit->setReadOnly(true);
    tabWidget->addTab(textEdit, tabName);
  }

  // Store the file path
  tabToFilePath[tabIndex] = filePath;

  // Load the content
  loadFileContent(filePath, tabIndex);
}

void DataPreviewWindow::clearFiles() {
  tabWidget->clear();
  tabToFilePath.clear();
}

void DataPreviewWindow::loadFileContent(const QString& filePath, int tabIndex) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    notificationBanner->showNotification("Could not open file: " + filePath, SilentNotificationBanner::Error);
    return;
  }

  QFileInfo fileInfo(filePath);
  QString extension = fileInfo.suffix().toLower();

  if (extension == "csv") {
    // Process CSV file
    QTableWidget* tableWidget =
      qobject_cast<QTableWidget*>(tabWidget->widget(tabIndex));
    if (!tableWidget) return;

    QTextStream in(&file);

    // Read header
    if (!in.atEnd()) {
      QString headerLine = in.readLine();
      QStringList headers = headerLine.split(",");

      tableWidget->setColumnCount(headers.size());
      tableWidget->setHorizontalHeaderLabels(headers);
    }

    // Read data
    QList<QStringList> rows;
    while (!in.atEnd()) {
      QString line = in.readLine();
      QStringList fields = line.split(",");
      rows.append(fields);
    }

    // Set table dimensions and fill with data
    tableWidget->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
      for (int col = 0; col < rows[row].size(); ++col) {
        if (col < tableWidget->columnCount()) {
          QTableWidgetItem* item = new QTableWidgetItem(rows[row][col]);
          tableWidget->setItem(row, col, item);
        }
      }
    }

    // Auto-adjust columns to fit content
    tableWidget->resizeColumnsToContents();
    tableWidget->resizeRowsToContents();
  } else {
    // Process JSON or TXT files
    QTextEdit* textEdit = qobject_cast<QTextEdit*>(tabWidget->widget(tabIndex));
    if (!textEdit) return;

    QTextStream in(&file);
    QString content = in.readAll();
    textEdit->setText(content);
  }

  file.close();
}

void DataPreviewWindow::onUploadClicked() { accept(); }

void DataPreviewWindow::onCancelClicked() { reject(); }
