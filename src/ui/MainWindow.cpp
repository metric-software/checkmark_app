#include "MainWindow.h"
#include "../logging/Logger.h"

#include <iostream>  // Add this line to fix std::cout errors

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>

#include "CustomWidgetWithTitle.h"
#include "updates/UpdateManager.h"
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

  // Set window title to "checkmark"
  setWindowTitle("checkmark");

  // Create main layout
  centralWidget = new QWidget(this);
  QHBoxLayout* mainLayout = new QHBoxLayout(centralWidget);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);
  setCentralWidget(centralWidget);

  // Create navigation bar
  QWidget* navBar = new QWidget(this);
  navBar->setFixedWidth(200);  // Set fixed width for nav bar
  navBar->setObjectName("navBar");

  QVBoxLayout* navLayout = new QVBoxLayout(navBar);
  navLayout->setContentsMargins(10, 10, 10, 10);
  navLayout->setSpacing(5);

  // Initialize member variables and add new System Info button as first item
  systemInfoButton = new QPushButton("System Info", navBar);
  diagnosticsButton = new QPushButton("Diagnostics", navBar);
  optimizeButton = new QPushButton("Optimize", navBar);
  gameBenchmarkButton = new QPushButton("Game Benchmark", navBar);
  updateButton = new QPushButton("Update", navBar);
  settingsButton = new QPushButton("Settings", navBar);
  
  // Hide update button initially (shown only when update is available)
  updateButton->setVisible(false);

  // Define navigation button style
  QString navButtonStyle = R"(
        #navBar QPushButton {
            background-color: transparent;
            color: #ffffff;
            border: none;
            text-align: left;
            padding: 8px 16px;
            border-radius: 4px;
            font-size: 14px;
        }
        #navBar QPushButton:hover {
            background-color: #333333;
        }
        #navBar QPushButton:checked {
            background-color: #363636;  /* Lighter than sidebar (#2a2a2a) */
            border: none;  /* Remove the blue border */
            padding: 8px 16px;  /* Reset padding to default */
        }
    )";

  setStyleSheet(R"(
        /* Base dark theme */
        QMainWindow, QWidget {
            background-color: #1a1a1a;
            color: #ffffff;
        }
    
        /* Scrollbar styling */
        QScrollBar:vertical {
            background: #1a1a1a;
            width: 10px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #424242;
            min-height: 20px;
            border-radius: 5px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: none;
        }
    
        /* Button styling */
        QPushButton {
            background-color: #0078d4;
            color: white;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #1084d8;
        }
        QPushButton:pressed {
            background-color: #006cc1;
        }
        QPushButton:disabled {
            background-color: #666666;
        }
    
        /* Checkbox styling */
        QCheckBox {
            color: #ffffff;
            spacing: 5px;
            padding: 8px 16px;
            background: #333333;
            margin-right: 5px;
            border-radius: 4px;
        }
        QCheckBox::indicator {
            width: 16px;
            height: 16px;
            border-radius: 3px;
        }
        QCheckBox::indicator:unchecked {
            border: 1px solid #666666;
            background: #1e1e1e;
        }
        QCheckBox::indicator:checked {
            border: 1px solid #0078d4;
            background: #0078d4;
        }
    
        /* Progress bar styling */
        QProgressBar {
            border: 1px solid #333333;
            border-radius: 4px;
            background-color: #1e1e1e;
            text-align: center;
            color: white;
        }
        QProgressBar::chunk {
            background-color: #0078d4;
            border-radius: 3px;
        }
    
        /* Label styling in group boxes */
        QGroupBox QLabel {
            color: #ffffff;
            padding: 4px;
            background: transparent; /* Remove background color */
        }

        /* Navigation bar styling */
        #navBar {
            background-color: #2a2a2a; /* Lighter background color */
        }
    )" + navButtonStyle);

  navBar->setStyleSheet(navButtonStyle);

  // Make buttons checkable and set the system info button as checked by default
  systemInfoButton->setCheckable(true);
  diagnosticsButton->setCheckable(true);
  optimizeButton->setCheckable(true);
  gameBenchmarkButton->setCheckable(true);
  updateButton->setCheckable(true);
  settingsButton->setCheckable(true);
  systemInfoButton->setChecked(true);

  // Add buttons to nav layout - System Info first
  navLayout->addWidget(systemInfoButton);
  navLayout->addWidget(diagnosticsButton);
  navLayout->addWidget(optimizeButton);
  navLayout->addWidget(gameBenchmarkButton);
  navLayout->addStretch();
  navLayout->addWidget(updateButton);
  navLayout->addWidget(settingsButton);

  // Create content container
  QWidget* contentContainer = new QWidget(this);
  QVBoxLayout* contentLayout = new QVBoxLayout(contentContainer);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);

  // Silent notification banner (no OS sound)
  notificationBanner = new SilentNotificationBanner(contentContainer);
  contentLayout->addWidget(notificationBanner);

  // Initialize views including the new SystemInfoView
  stackedWidget = new QStackedWidget(this);
  systemInfoView = new SystemInfoView(this);
  diagnosticView = new DiagnosticView(this);
  optimizeView = new OptimizeView(this);
  gameBenchmarkView = new GameBenchmarkView(this);
  settingsView = new SettingsView(this);

  // Add views to stacked widget - SystemInfoView first
  stackedWidget->addWidget(systemInfoView);
  stackedWidget->addWidget(diagnosticView);
  stackedWidget->addWidget(optimizeView);
  stackedWidget->addWidget(gameBenchmarkView);
  stackedWidget->addWidget(settingsView);

  // Add stacked widget to content layout
  contentLayout->addWidget(stackedWidget);

  // Add nav bar and content to main layout
  mainLayout->addWidget(navBar);
  mainLayout->addWidget(contentContainer);

  // Connect navigation buttons including the new system info button
  connect(systemInfoButton, &QPushButton::clicked, this,
          &MainWindow::switchToSystemInfo);
  connect(diagnosticsButton, &QPushButton::clicked, this,
          &MainWindow::switchToDiagnostics);
  connect(optimizeButton, &QPushButton::clicked, this,
          &MainWindow::switchToOptimize);
  connect(gameBenchmarkButton, &QPushButton::clicked, this,
          &MainWindow::switchToGameBenchmark);
  connect(updateButton, &QPushButton::clicked, this,
          &MainWindow::switchToUpdate);
  connect(settingsButton, &QPushButton::clicked, this,
          &MainWindow::switchToSettings);

  // Connect the cleanup slot to aboutToQuit signal
  connect(qApp, &QApplication::aboutToQuit, this,
          &MainWindow::cleanupResources);

  // Initialize and connect UpdateManager
  auto& updateManager = UpdateManager::getInstance();
  connect(&updateManager, &UpdateManager::updateAvailable, this, &MainWindow::onUpdateAvailable);
  connect(&updateManager, &UpdateManager::updateNotAvailable, this, &MainWindow::onUpdateNotAvailable);
  connect(&updateManager, &UpdateManager::updateError, this, &MainWindow::onUpdateError);
  
  // Initialize update manager (delayed to allow app to fully start)
  QTimer::singleShot(2000, [&updateManager]() {
    updateManager.initialize();
  });

  // Set the default window size to 950x800
  resize(950, 800);
}

MainWindow::~MainWindow() {
  LOG_INFO << "MainWindow destructor called";
  // Qt will handle the deletion of child widgets
}

void MainWindow::cleanupResources() {
  LOG_INFO << "MainWindow cleanup started";

  try {
    // Cancel operations in views
    if (diagnosticView) {
      try {
        diagnosticView->cancelOperations();
      } catch (const std::exception& e) {
        LOG_ERROR << "Error cleaning up diagnostic view: " << e.what();
      }
    }

    if (optimizeView) {
      try {
        optimizeView->cancelOperations();
      } catch (const std::exception& e) {
        LOG_ERROR << "Error cleaning up optimize view: " << e.what();
      }
    }

    if (gameBenchmarkView) {
      try {
        gameBenchmarkView->cancelOperations();
      } catch (const std::exception& e) {
        LOG_ERROR << "Error cleaning up game benchmark view: " << e.what();
      }
    }

    // Save settings
    if (settingsView) {
      try {
        settingsView->saveSettings();
      } catch (const std::exception& e) {
        LOG_ERROR << "Error saving settings: " << e.what();
      }
    }

    // Disconnect all signals from this object
    this->disconnect();

    LOG_INFO << "MainWindow cleanup complete";
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception during MainWindow cleanup: " << e.what();
  } catch (...) {
    LOG_ERROR << "Unknown exception during MainWindow cleanup";
  }
}

void MainWindow::switchToSystemInfo() {
  stackedWidget->setCurrentWidget(systemInfoView);
  systemInfoButton->setChecked(true);
  diagnosticsButton->setChecked(false);
  optimizeButton->setChecked(false);
  gameBenchmarkButton->setChecked(false);
  updateButton->setChecked(false);
  settingsButton->setChecked(false);
}

  void MainWindow::switchToDiagnostics() {
    stackedWidget->setCurrentWidget(diagnosticView);
    systemInfoButton->setChecked(false);
    diagnosticsButton->setChecked(true);
    optimizeButton->setChecked(false);
  gameBenchmarkButton->setChecked(false);
  updateButton->setChecked(false);
    settingsButton->setChecked(false);
  }
  
  void MainWindow::switchToOptimize() {
    // Treat all optimization functionality as experimental. Only allow
    // switching to this view when experimental features are effectively
    // enabled (local preference AND backend flag AND online).
    if (!ApplicationSettings::getInstance().getEffectiveExperimentalFeaturesEnabled()) {
      if (notificationBanner) {
        notificationBanner->showNotification(
          "Optimization features are experimental and are currently disabled.",
          SilentNotificationBanner::Warning,
          5000);
      }
      // Ensure button state reflects that we're not on the Optimize view
      optimizeButton->setChecked(false);
      return;
    }

    stackedWidget->setCurrentWidget(optimizeView);
    systemInfoButton->setChecked(false);
  diagnosticsButton->setChecked(false);
  optimizeButton->setChecked(true);
  gameBenchmarkButton->setChecked(false);
  updateButton->setChecked(false);
  settingsButton->setChecked(false);
}

void MainWindow::switchToGameBenchmark() {
  // Update active button appearance
  systemInfoButton->setChecked(false);
  diagnosticsButton->setChecked(false);
  optimizeButton->setChecked(false);
  gameBenchmarkButton->setChecked(true);
  updateButton->setChecked(false);
  settingsButton->setChecked(false);

  // Show EAC warning before showing the view
  gameBenchmarkView->showEACWarningIfNeeded();

  // Switch to game benchmark view
  stackedWidget->setCurrentWidget(gameBenchmarkView);
}

void MainWindow::switchToSettings() {
  stackedWidget->setCurrentWidget(settingsView);
  systemInfoButton->setChecked(false);
  diagnosticsButton->setChecked(false);
  optimizeButton->setChecked(false);
  gameBenchmarkButton->setChecked(false);
  updateButton->setChecked(false);
  settingsButton->setChecked(true);
}

void MainWindow::switchToUpdate() {
  // Don't switch to a view, just show the update dialog
  auto& updateManager = UpdateManager::getInstance();
  updateManager.showUpdateDialog();
  
  // Reset button states (don't check update button)
  systemInfoButton->setChecked(false);
  diagnosticsButton->setChecked(false);
  optimizeButton->setChecked(false);
  gameBenchmarkButton->setChecked(false);
  updateButton->setChecked(false);
  settingsButton->setChecked(false);
  
  // Return to the current view (keep the previous view active)
  // We can check which view was active or default to system info
  systemInfoButton->setChecked(true);
  stackedWidget->setCurrentWidget(systemInfoView);
}

void MainWindow::onUpdateAvailable(const QString& version) {
  LOG_INFO << "Update available: " << version.toStdString();
  updateButton->setVisible(true);
  updateButton->setText("🔄 Update Available");
  updateButton->setStyleSheet(R"(
    #navBar QPushButton {
      background-color: #ff6b35 !important;
      color: white !important;
      border: none;
      text-align: left;
      padding: 8px 16px;
      border-radius: 4px;
      font-size: 14px;
      font-weight: bold;
    }
    #navBar QPushButton:hover {
      background-color: #ff8c42 !important;
    }
    #navBar QPushButton:pressed {
      background-color: #e55a2b !important;
    }
  )");
}

void MainWindow::onUpdateNotAvailable() {
  LOG_INFO << "No update available";
  updateButton->setVisible(false);
}

void MainWindow::onUpdateError(const QString& error) {
  LOG_ERROR << "Update check error: " << error.toStdString();
  updateButton->setVisible(false);
}

void MainWindow::closeEvent(QCloseEvent* event) {
  try {
    // Do immediate cleanup that can't wait for aboutToQuit
    LOG_INFO << "MainWindow is closing...";

    // Save settings immediately
    if (settingsView) {
      settingsView->saveSettings();
    }

    // Cancel any ongoing operations in views
    // This ensures we don't have hanging threads
    if (diagnosticView) diagnosticView->cancelOperations();
    if (optimizeView) optimizeView->cancelOperations();
    if (gameBenchmarkView) gameBenchmarkView->cancelOperations();

    // Accept the close event
    event->accept();

    // Post a quit message to ensure application terminates
    QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
  } catch (const std::exception& e) {
    LOG_ERROR << "Error during window close: " << e.what();
    event->accept();  // Still close even if there's an error
    QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
  }
}
