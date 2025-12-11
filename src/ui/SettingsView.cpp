#include "settingsview.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStringLiteral>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include "ApplicationSettings.h"
#include "ui/SettingsToggle.h"
#include "updates/UpdateManager.h"
#include "logging/Logger.h"
#include "checkmark_version.h"

SettingsView::SettingsView(QWidget* parent) : QWidget(parent) {
  // parent -> page_layout -> page_stack_(settings && resources) && bottom_bar.
  auto* page_layout = new QVBoxLayout(this);
  page_layout->setContentsMargins(0, 0, 0, 0);
  page_stack_ = new QStackedWidget(this);

  // settings
  settings_area_ = new QScrollArea(this);
  settings_area_->setObjectName(QStringLiteral("settings_area"));
  settings_area_->setFrameShape(QFrame::NoFrame);
  settings_area_->setWidgetResizable(true);
  auto* settings_widget = new QWidget();
  auto* settings_layout = new QVBoxLayout(settings_widget);
  settings_layout->setContentsMargins(0, 0, 0, 0);
  settings_layout->setSpacing(15);
  // Application Settings category
  auto* app_settings_header = new QLabel("Application Settings", settings_widget);
  app_settings_header->setStyleSheet("font-weight: bold; font-size: 14px; color: #F4F4F4; margin-top: 5px; margin-bottom: 10px;");
  settings_layout->addWidget(app_settings_header);
  
  // Elevated Priority toggle
  elevated_priority_toggle_ =
    new SettingsToggle("elevated_priority", "Run tests with elevated priority",
                       "Enable higher process priority when running tests "
                       "(requires application restart).",
                       settings_widget);
  settings_layout->addWidget(elevated_priority_toggle_);
  
  // Data Collection toggle
  allow_data_collection_toggle_ = new SettingsToggle(
    "allow_data_collection", "Allow data collection",
    "Allow the application to upload your data for analysis and improvements. You can disable this to use the application in offline mode, but we cannot provide better data analysis or personalized results.",
    settings_widget);
  settings_layout->addWidget(allow_data_collection_toggle_);
  
  // Automatic Data Upload toggle
  automatic_data_upload_toggle_ = new SettingsToggle(
    "automatic_data_upload", "Automatic Data Upload",
    "Automatically upload benchmark and diagnostic data when tests complete. When disabled, you will need to manually upload data using the upload dialogs.",
    settings_widget);
  settings_layout->addWidget(automatic_data_upload_toggle_);
  
  // Add space between categories
  settings_layout->addSpacing(25);
  
  // Developer Settings category
  auto* dev_settings_header = new QLabel("Developer Settings", settings_widget);
  dev_settings_header->setStyleSheet("font-weight: bold; font-size: 14px; color: #F4F4F4; margin-top: 5px; margin-bottom: 10px;");
  settings_layout->addWidget(dev_settings_header);
  
  // Validate Metrics on Startup toggle
  validate_metrics_on_startup_toggle_ = new SettingsToggle(
    "validate_metrics_on_startup", "Validate metrics on startup",
    "Run system metrics validation process when the application starts.",
    settings_widget);
  settings_layout->addWidget(validate_metrics_on_startup_toggle_);
  
  // Console Visibility toggle
  console_visibility_toggle_ = new SettingsToggle(
    "console_visibility", "Show Console Window",
    "Show the debug console window (requires application restart).",
    settings_widget);
  settings_layout->addWidget(console_visibility_toggle_);
  
  // Experimental Features toggle
  experimental_features_toggle_ = new SettingsToggle(
    "experimental_features", "Experimental Features",
    "Enable experimental features that may not be fully tested or stable.",
    settings_widget);
  settings_layout->addWidget(experimental_features_toggle_);
  
  // Detailed Logs toggle
  detailed_logs_toggle_ = new SettingsToggle(
    "detailed_logs", "Detailed logs",
    "Enable all log levels (TRACE, DEBUG, INFO, WARN, ERROR, FATAL). When disabled, only ERROR and FATAL logs are shown.",
    settings_widget);
  settings_layout->addWidget(detailed_logs_toggle_);
  // Load current states from settings
  ApplicationSettings& app_settings_instance =
    ApplicationSettings::getInstance();
  bool experimental_enabled =
    app_settings_instance.getExperimentalFeaturesEnabled();
  bool console_visible = app_settings_instance.getConsoleVisible();
  bool elevated_priority = app_settings_instance.getElevatedPriorityEnabled();
  bool validate_metrics_on_startup = app_settings_instance.getValidateMetricsOnStartup();
  bool allow_data_collection = app_settings_instance.getAllowDataCollection();
  bool detailed_logs_enabled = app_settings_instance.getDetailedLogsEnabled();
  bool automatic_data_upload_enabled = app_settings_instance.getAutomaticDataUploadEnabled();
  // Set toggle states (using setEnabled as per existing pattern for
  // SettingsToggle)
  experimental_features_toggle_->setEnabled(experimental_enabled);
  console_visibility_toggle_->setEnabled(console_visible);
  elevated_priority_toggle_->setEnabled(elevated_priority);
  validate_metrics_on_startup_toggle_->setEnabled(validate_metrics_on_startup);
  allow_data_collection_toggle_->setEnabled(allow_data_collection);
  detailed_logs_toggle_->setEnabled(detailed_logs_enabled);
  automatic_data_upload_toggle_->setEnabled(automatic_data_upload_enabled);
  // Connect toggle state changes
  connect(experimental_features_toggle_, &SettingsToggle::stateChanged, this,
          &SettingsView::OnExperimentalFeaturesChanged);
  connect(console_visibility_toggle_, &SettingsToggle::stateChanged, this,
          &SettingsView::OnConsoleVisibilityChanged);
  connect(elevated_priority_toggle_, &SettingsToggle::stateChanged, this,
          &SettingsView::OnElevatedPriorityChanged);
  connect(validate_metrics_on_startup_toggle_, &SettingsToggle::stateChanged, this,
          &SettingsView::OnValidateMetricsOnStartupChanged);
  connect(allow_data_collection_toggle_, &SettingsToggle::stateChanged, this,
          &SettingsView::OnDataCollectionChanged);
  connect(detailed_logs_toggle_, &SettingsToggle::stateChanged, this,
          &SettingsView::OnDetailedLogsChanged);
  connect(automatic_data_upload_toggle_, &SettingsToggle::stateChanged, this,
          &SettingsView::OnAutomaticDataUploadChanged);
  settings_layout->addSpacing(20);
  
  // Reset Settings and Delete Data buttons (first section)
  reset_settings_button_ =
    new QPushButton("Reset All Settings", settings_widget);
  reset_settings_button_->setObjectName("settings_action_button");
  reset_settings_button_->setCursor(Qt::PointingHandCursor);
  reset_settings_button_->installEventFilter(this);
  delete_all_data_button_ =
    new QPushButton("Delete All Application Data", settings_widget);
  delete_all_data_button_->setObjectName("settings_action_button");
  delete_all_data_button_->setCursor(Qt::PointingHandCursor);
  delete_all_data_button_->installEventFilter(this);
  QHBoxLayout* data_button_layout = new QHBoxLayout();
  data_button_layout->addWidget(reset_settings_button_);
  data_button_layout->addWidget(delete_all_data_button_);
  data_button_layout->addStretch(1);
  settings_layout->addLayout(data_button_layout);
  settings_layout->addSpacing(5);
  
  // Application Data Location text (below the buttons)
  QHBoxLayout* appdata_outer_layout = new QHBoxLayout();
  QLabel* appdata_label =
    new QLabel("Application Data Location:", settings_widget);
  appdata_button_ = new QPushButton("Open Folder", settings_widget);
  appdata_button_->setObjectName("hyperlink_button");
  appdata_button_->setFlat(true);
  appdata_button_->setCursor(Qt::PointingHandCursor);
  connect(appdata_button_, &QPushButton::clicked, this,
          &SettingsView::OnOpenAppDataLocation);
  appdata_outer_layout->addWidget(appdata_label);
  appdata_outer_layout->addWidget(appdata_button_);
  appdata_outer_layout->addStretch(1);
  settings_layout->addLayout(appdata_outer_layout);
  settings_layout->addSpacing(20);
  
  // Check for Updates button (second section)
  check_updates_button_ = new QPushButton("Check for Updates", settings_widget);
  check_updates_button_->setObjectName("settings_action_button");
  check_updates_button_->setCursor(Qt::PointingHandCursor);
  connect(check_updates_button_, &QPushButton::clicked, this, &SettingsView::OnCheckUpdatesClicked);
  QHBoxLayout* update_button_layout = new QHBoxLayout();
  update_button_layout->addWidget(check_updates_button_);
  update_button_layout->addStretch(1);
  settings_layout->addLayout(update_button_layout);
  settings_layout->addSpacing(5);
  
  // Update status label below the check updates button
  update_status_label_ = new QLabel("Update status: Checking...", settings_widget);
  update_status_label_->setStyleSheet("color: #C7C7C7; font-size: 12px; margin-left: 0px;");
  settings_layout->addWidget(update_status_label_);
  settings_layout->addSpacing(20);
  settings_layout->addStretch(1);
  settings_area_->setWidget(settings_widget);
  page_stack_->addWidget(settings_area_);

  // Resources
  content_area_ = new QTextBrowser(this);
  content_area_->setObjectName(QStringLiteral("content_area"));
  content_area_->setFrameStyle(QFrame::NoFrame);
  content_area_->document()->setDefaultFont(QFont("Consolas", 10));
  page_stack_->addWidget(content_area_);

  // GDPR page
  gdpr_page_ = new QWidget(this);
  gdpr_page_->setObjectName(QStringLiteral("gdpr_page"));
  auto* gdpr_layout = new QVBoxLayout(gdpr_page_);
  gdpr_layout->setContentsMargins(20, 20, 20, 20);
  gdpr_layout->setSpacing(20);

  // Back button
  auto* back_button_layout = new QHBoxLayout();
  auto* back_button = new QPushButton("← Back", gdpr_page_);
  back_button->setObjectName("back_button");
  back_button->setFlat(true);
  back_button->setCursor(Qt::PointingHandCursor);
  connect(back_button, &QPushButton::clicked, this, [this]() {
    active_page_ = nullptr;
    page_stack_->setCurrentWidget(settings_area_);
    for (auto& res_entry : resources_)
      if (res_entry && res_entry->button) {
        res_entry->button->setProperty("selected", false);
        if (res_entry->button->style())
          res_entry->button->style()->polish(res_entry->button);
      }
  });
  back_button_layout->addWidget(back_button);
  back_button_layout->addStretch();
  gdpr_layout->addLayout(back_button_layout);

  // GDPR title
  auto* gdpr_title = new QLabel("GDPR Data Management", gdpr_page_);
  gdpr_title->setObjectName("gdpr_title");
  QFont titleFont = gdpr_title->font();
  titleFont.setPointSize(16);
  titleFont.setBold(true);
  gdpr_title->setFont(titleFont);
  gdpr_layout->addWidget(gdpr_title);

  // GDPR description
  auto* gdpr_description = new QLabel(
    "Under the General Data Protection Regulation (GDPR), you have the right to:"
    "\n\n• Request a copy of all personal data we have collected about you"
    "\n• Request the deletion of all your personal data from our systems"
    "\n\nPlease note that these functions are not yet implemented and will be available in a future update.",
    gdpr_page_);
  gdpr_description->setWordWrap(true);
  gdpr_description->setObjectName("gdpr_description");
  gdpr_layout->addWidget(gdpr_description);

  // GDPR buttons
  auto* gdpr_buttons_layout = new QVBoxLayout();
  gdpr_buttons_layout->setSpacing(15);

  auto* request_data_button = new QPushButton("Request All Data", gdpr_page_);
  request_data_button->setObjectName("gdpr_button");
  request_data_button->setCursor(Qt::PointingHandCursor);
  connect(request_data_button, &QPushButton::clicked, this, &SettingsView::OnRequestDataClicked);

  auto* delete_data_button = new QPushButton("Request Removal of All Data", gdpr_page_);
  delete_data_button->setObjectName("gdpr_button");
  delete_data_button->setCursor(Qt::PointingHandCursor);
  connect(delete_data_button, &QPushButton::clicked, this, &SettingsView::OnDeleteDataClicked);

  gdpr_buttons_layout->addWidget(request_data_button);
  gdpr_buttons_layout->addWidget(delete_data_button);
  gdpr_layout->addLayout(gdpr_buttons_layout);

  gdpr_layout->addStretch();
  page_stack_->addWidget(gdpr_page_);

  page_layout->addWidget(page_stack_, /*strech = */ 1);
  // Bottom bar
  auto* bottom_layout = new QHBoxLayout(this);
  bottom_layout->setSpacing(20);
  bottom_layout->setAlignment(Qt::AlignBottom | Qt::AlignCenter);
  for (auto& entry : resources_) {
    entry->button = new QPushButton(entry->button_text, this);
    entry->button->setObjectName("resource_label");
    entry->button->setFlat(true);
    entry->button->setCursor(Qt::PointingHandCursor);
    if (entry->resource_path.isEmpty())
      entry->watcher = nullptr;
    else {
      entry->watcher = new QFutureWatcher<QString>(this);
      connect(entry.get()->watcher, &QFutureWatcher<QString>::finished, this,
              [this, item_being_watched_ptr = entry.get()]() {
                if (!item_being_watched_ptr || !item_being_watched_ptr->watcher)
                  return;
                item_being_watched_ptr->data =
                  item_being_watched_ptr->watcher->result().toUtf8();
                if (active_page_ == item_being_watched_ptr) {
                  if (item_being_watched_ptr->use_markdown)
                    content_area_->setMarkdown(
                      QString::fromUtf8(item_being_watched_ptr->data));
                  else
                    content_area_->setPlainText(
                      QString::fromUtf8(item_being_watched_ptr->data));
                }
              });
    }
    connect(
      entry.get()->button, &QPushButton::clicked, this,
      [this, clicked_item_ptr = entry.get()]() {
        // Special handling for GDPR button
        if (clicked_item_ptr->button_text == "GDPR") {
          if (active_page_ == clicked_item_ptr) {  // currently active -> hide
            active_page_ = nullptr;
            page_stack_->setCurrentWidget(settings_area_);
            for (auto& res_entry : resources_)
              if (res_entry && res_entry->button) {
                res_entry->button->setProperty("selected", false);
                if (res_entry->button->style())
                  res_entry->button->style()->polish(res_entry->button);
              }
            return;
          }
          active_page_ = clicked_item_ptr;  // !currently active -> show GDPR page
          page_stack_->setCurrentWidget(gdpr_page_);
          // styles
          for (const auto& res_entry : resources_)
            if (res_entry && res_entry->button) {
              res_entry->button->setProperty("selected",
                                             res_entry.get() == active_page_);
              if (res_entry->button->style())
                res_entry->button->style()->polish(res_entry->button);
            }
          return;
        }

        // Regular resource handling
        if (active_page_ == clicked_item_ptr) {  // currently active -> hide
          active_page_ = nullptr;
          page_stack_->setCurrentWidget(settings_area_);
          content_area_->clear();
          for (auto& res_entry : resources_)
            if (res_entry && res_entry->button) {
              res_entry->button->setProperty("selected", false);
              if (res_entry->button->style())
                res_entry->button->style()->polish(res_entry->button);
            }
          return;
        }
        active_page_ = clicked_item_ptr;  // !currently active -> show
        page_stack_->setCurrentWidget(content_area_);
        // styles
        for (const auto& res_entry : resources_)
          if (res_entry && res_entry->button) {
            res_entry->button->setProperty("selected",
                                           res_entry.get() == active_page_);
            if (res_entry->button->style())
              res_entry->button->style()->polish(res_entry->button);
          }
        if (!active_page_) return;
        if (active_page_->data.isEmpty()) {
          if (!active_page_->resource_path.isEmpty()) {
            content_area_->setPlainText("Loading...");
            if (active_page_->watcher && !active_page_->watcher->isRunning()) {
              active_page_->watcher->setFuture(
                QtConcurrent::run([load_path = active_page_->resource_path]() {
                  QFile file(load_path);
                  if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                    return QString::fromUtf8(file.readAll());
                  return QString("Error: Could not load resource from %1")
                    .arg(load_path);
                }));
            } else if (!active_page_->watcher)
              content_area_->setPlainText(
                QString("Content for '%1' is not loadable (no resource path or "
                        "watcher).")
                  .arg(active_page_->button_text));
          } else
            content_area_->setPlainText(
              QString("Content for '%1' is not available.")
                .arg(active_page_->button_text));
        } else {
          if (active_page_->use_markdown)
            content_area_->setMarkdown(QString::fromUtf8(active_page_->data));
          else
            content_area_->setPlainText(QString::fromUtf8(active_page_->data));
        }
      });
    bottom_layout->addWidget(entry->button);
  }
  bottom_layout->addStretch();
  auto* version_label = new QLabel(QStringLiteral("Beta version %1").arg(QString::fromUtf8(CHECKMARK_VERSION_STRING)), this);
  version_label->setObjectName(QStringLiteral("version_label"));
  bottom_layout->addWidget(version_label);
  page_layout->addLayout(bottom_layout);
  page_stack_->setCurrentWidget(settings_area_);
  for (const auto& res_entry : resources_) {
    if (res_entry && res_entry->button) {
      res_entry->button->setProperty("selected", false);
      if (res_entry->button->style())
        res_entry->button->style()->polish(res_entry->button);
    }
  }
  // Perform initial update check after a brief delay
  QTimer::singleShot(2000, this, [this]() {
    OnCheckUpdatesClicked();
  });

  {
    setStyleSheet(R"(
    #version_label {
      color: #707070;
      padding: 5px 10px;
    }
    #resource_label {
      color: #C7C7C7;
      padding: 5px 10px;
      background: transparent;
      border: none;
    }
    #resource_label:hover, #resource_label[selected="true"] {
      color: #F4F4F4;
    }
    #settings_area {
      padding: 10px;
      background-color: transparent;
      border-radius: 0px;
    }
    #content_area {
      padding: 10px;
      background-color: transparent;
      border-radius: 0px;
    }
    #settings_action_button {
      background-color: #333333;
      color: white;
      border-radius: 4px;
      padding: 8px 16px;
      border: none;
      font-size: 12px;
    }
    #settings_action_button:hover {
      background-color: #404040;
    }
    #settings_action_button:pressed {
      background-color: #292929;
    }
    #hyperlink_button {
      color: #4A90E2;
      text-decoration: underline;
      padding: 5px 10px;
      background: transparent;
      border: none;
    }
    #hyperlink_button:hover {
      color: #75ABED;
    }
    #hyperlink_button:pressed {
      color: #3A80D2;
    }
    #gdpr_page {
      padding: 10px;
      background-color: transparent;
      border-radius: 0px;
    }
    #gdpr_title {
      color: #F4F4F4;
      padding: 10px 0px;
    }
    #gdpr_description {
      color: #C7C7C7;
      padding: 10px 0px;
      line-height: 1.4;
    }
    #back_button {
      color: #4A90E2;
      padding: 5px 10px;
      background: transparent;
      border: none;
      font-size: 14px;
    }
    #back_button:hover {
      color: #75ABED;
    }
    #back_button:pressed {
      color: #3A80D2;
    }
    #gdpr_button {
      background-color: #4A90E2;
      color: white;
      border-radius: 4px;
      padding: 10px 20px;
      border: none;
      font-size: 12px;
    }
    #gdpr_button:hover {
      background-color: #75ABED;
    }
    #gdpr_button:pressed {
      background-color: #3A80D2;
    }
  )");
  }
}

bool SettingsView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == reset_settings_button_ &&
      event->type() == QEvent::MouseButtonRelease) {
    // Handle the reset settings button click silently
    OnResetSettingsClicked();
    return true;  // Event handled, don't propagate further
  } else if (watched == delete_all_data_button_ &&
             event->type() == QEvent::MouseButtonRelease) {
    // Handle the delete all data button click silently
    OnDeleteAllDataClicked();
    return true;  // Event handled, don't propagate further
  }

  // Pass the event to the parent class
  return QWidget::eventFilter(watched, event);
}

void SettingsView::OnResetSettingsClicked() {
  // Create a custom silent confirmation dialog instead of using QMessageBox
  QDialog confirmDialog(this);
  confirmDialog.setWindowTitle("Reset Settings");
  confirmDialog.setFixedSize(400, 180);
  confirmDialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                               Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

  QVBoxLayout* layout = new QVBoxLayout(&confirmDialog);

  // Create icon and text layout
  QHBoxLayout* headerLayout = new QHBoxLayout();
  QLabel* iconLabel = new QLabel(&confirmDialog);
  iconLabel->setPixmap(
    style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(32, 32));
  headerLayout->addWidget(iconLabel);

  QVBoxLayout* textLayout = new QVBoxLayout();
  QLabel* mainText = new QLabel(
    "Are you sure you want to reset all application settings?", &confirmDialog);
  QFont boldFont = mainText->font();
  boldFont.setBold(true);
  mainText->setFont(boldFont);

  QLabel* infoText = new QLabel("This will reset all settings to their default "
                                "values. This action cannot be undone.",
                                &confirmDialog);
  infoText->setWordWrap(true);

  textLayout->addWidget(mainText);
  textLayout->addWidget(infoText);
  headerLayout->addLayout(textLayout);

  // Create buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  QPushButton* noButton = new QPushButton("No", &confirmDialog);
  QPushButton* yesButton = new QPushButton("Yes", &confirmDialog);

  // Style buttons
  yesButton->setStyleSheet(R"(
    QPushButton {
      background-color: #FF0000;
      color: white;
      border-radius: 4px;
      padding: 6px 15px;
      border: none;
    }
    QPushButton:hover {
      background-color: #FF3333;
    }
    QPushButton:pressed {
      background-color: #CC0000;
    }
  )");

  buttonLayout->addStretch();
  buttonLayout->addWidget(noButton);
  buttonLayout->addWidget(yesButton);

  // Add all layouts to main layout
  layout->addLayout(headerLayout);
  layout->addStretch();
  layout->addLayout(buttonLayout);

  // Connect buttons
  connect(noButton, &QPushButton::clicked, &confirmDialog, &QDialog::reject);
  connect(yesButton, &QPushButton::clicked, &confirmDialog, &QDialog::accept);

  // Set default button and focus
  noButton->setDefault(true);
  noButton->setFocus();

  // Execute dialog silently (no system sounds)
  int result = confirmDialog.exec();

  if (result == QDialog::Accepted) {
    // Reset all settings
    ApplicationSettings::getInstance().resetAllSettings();

    // Update UI to reflect reset settings
    experimental_features_toggle_->setEnabled(false);
    console_visibility_toggle_->setEnabled(false);
    elevated_priority_toggle_->setEnabled(false);
    validate_metrics_on_startup_toggle_->setEnabled(true);  // Default to true
    allow_data_collection_toggle_->setEnabled(true);  // Default to true
    detailed_logs_toggle_->setEnabled(false);  // Default to false
    automatic_data_upload_toggle_->setEnabled(true);  // Default to true

    // Show confirmation with a custom silent dialog
    QDialog infoDialog(this);
    infoDialog.setWindowTitle("Settings Reset");
    infoDialog.setFixedSize(350, 150);
    infoDialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                              Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

    QVBoxLayout* infoLayout = new QVBoxLayout(&infoDialog);

    // Create icon and text layout
    QHBoxLayout* infoHeaderLayout = new QHBoxLayout();
    QLabel* infoIconLabel = new QLabel(&infoDialog);
    infoIconLabel->setPixmap(
      style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));
    infoHeaderLayout->addWidget(infoIconLabel);

    QLabel* infoMainText = new QLabel(
      "All settings have been reset to their default values.", &infoDialog);
    infoMainText->setWordWrap(true);
    infoHeaderLayout->addWidget(infoMainText);

    // Create OK button
    QHBoxLayout* infoButtonLayout = new QHBoxLayout();
    QPushButton* okButton = new QPushButton("OK", &infoDialog);

    infoButtonLayout->addStretch();
    infoButtonLayout->addWidget(okButton);

    // Add all layouts to main layout
    infoLayout->addLayout(infoHeaderLayout);
    infoLayout->addStretch();
    infoLayout->addLayout(infoButtonLayout);

    // Connect button
    connect(okButton, &QPushButton::clicked, &infoDialog, &QDialog::accept);

    // Set default button and focus
    okButton->setDefault(true);
    okButton->setFocus();

    // Execute dialog silently
    infoDialog.exec();
  }
}

void SettingsView::OnExperimentalFeaturesChanged(const QString& id,
                                                 bool enabled) {
  // Save the setting
  ApplicationSettings::getInstance().setExperimentalFeaturesEnabled(enabled);

  // No actual functionality implemented as requested
  // But we can add a silent confirmation if desired:
  /*
  QMessageBox confirmBox;
  confirmBox.setWindowTitle("Settings Updated");
  confirmBox.setText("Experimental features have been " + QString(enabled ?
  "enabled" : "disabled") + ".");
  confirmBox.setStandardButtons(QMessageBox::Ok);
  confirmBox.setIcon(QMessageBox::Information);

  // Suppress notification sound
  confirmBox.setWindowFlags(confirmBox.windowFlags() | Qt::CustomizeWindowHint |
  Qt::WindowTitleHint | Qt::WindowCloseButtonHint |
  Qt::MSWindowsFixedSizeDialogHint);

  confirmBox.exec();
  */
}

void SettingsView::OnConsoleVisibilityChanged(const QString& id, bool enabled) {
  // Save the setting
  ApplicationSettings::getInstance().setConsoleVisible(enabled);

  // Show a gentle reminder that restart is needed
  QDialog infoDialog(this);
  infoDialog.setWindowTitle("Restart Required");
  infoDialog.setFixedSize(350, 150);
  infoDialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                            Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

  QVBoxLayout* layout = new QVBoxLayout(&infoDialog);

  // Create icon and text layout
  QHBoxLayout* headerLayout = new QHBoxLayout();
  QLabel* iconLabel = new QLabel(&infoDialog);
  iconLabel->setPixmap(
    style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));
  headerLayout->addWidget(iconLabel);

  QLabel* messageLabel =
    new QLabel("The console window visibility setting will take effect after "
               "restarting the application.",
               &infoDialog);
  messageLabel->setWordWrap(true);
  headerLayout->addWidget(messageLabel);

  // Create OK button
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  QPushButton* okButton = new QPushButton("OK", &infoDialog);

  buttonLayout->addStretch();
  buttonLayout->addWidget(okButton);

  // Add layouts to main layout
  layout->addLayout(headerLayout);
  layout->addStretch();
  layout->addLayout(buttonLayout);

  // Connect button
  connect(okButton, &QPushButton::clicked, &infoDialog, &QDialog::accept);

  // Set default button
  okButton->setDefault(true);
  okButton->setFocus();

  // Execute dialog silently
  infoDialog.exec();
}

void SettingsView::OnElevatedPriorityChanged(const QString& id, bool enabled) {
  // Save the setting
  ApplicationSettings::getInstance().setElevatedPriorityEnabled(enabled);

  // Show a notice about restart requirement
  QDialog infoDialog(this);
  infoDialog.setWindowTitle("Restart Required");
  infoDialog.setFixedSize(350, 150);
  infoDialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                            Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

  QVBoxLayout* layout = new QVBoxLayout(&infoDialog);

  // Create icon and text layout
  QHBoxLayout* headerLayout = new QHBoxLayout();
  QLabel* iconLabel = new QLabel(&infoDialog);
  iconLabel->setPixmap(
    style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));
  headerLayout->addWidget(iconLabel);

  QLabel* messageLabel = new QLabel("The elevated priority setting will take "
                                    "effect after restarting the application.",
                                    &infoDialog);
  messageLabel->setWordWrap(true);
  headerLayout->addWidget(messageLabel);

  // Create OK button
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  QPushButton* okButton = new QPushButton("OK", &infoDialog);

  buttonLayout->addStretch();
  buttonLayout->addWidget(okButton);

  // Add layouts to main layout
  layout->addLayout(headerLayout);
  layout->addStretch();
  layout->addLayout(buttonLayout);

  // Connect button
  connect(okButton, &QPushButton::clicked, &infoDialog, &QDialog::accept);

  // Set default button
  okButton->setDefault(true);
  okButton->setFocus();

  // Execute dialog silently
  infoDialog.exec();
}

void SettingsView::saveSettings() {
  // Get current settings instance
  ApplicationSettings& settings = ApplicationSettings::getInstance();

  // Ensure settings reflect current UI state (in case they weren't saved
  // already)
  if (experimental_features_toggle_) {
    settings.setExperimentalFeaturesEnabled(
      experimental_features_toggle_->isEnabled());
  }

  if (console_visibility_toggle_) {
    settings.setConsoleVisible(console_visibility_toggle_->isEnabled());
  }

  if (elevated_priority_toggle_) {
    settings.setElevatedPriorityEnabled(elevated_priority_toggle_->isEnabled());
  }

  if (validate_metrics_on_startup_toggle_) {
    settings.setValidateMetricsOnStartup(validate_metrics_on_startup_toggle_->isEnabled());
  }

  if (allow_data_collection_toggle_) {
    settings.setAllowDataCollection(allow_data_collection_toggle_->isEnabled());
  }

  if (detailed_logs_toggle_) {
    settings.setDetailedLogsEnabled(detailed_logs_toggle_->isEnabled());
  }

  if (automatic_data_upload_toggle_) {
    settings.setAutomaticDataUploadEnabled(automatic_data_upload_toggle_->isEnabled());
  }

  // Instead of using saveSettings(), directly save to settings file
  // This is typically done automatically when setting values in
  // ApplicationSettings but we can force a save here by writing settings to
  // QSettings
  QSettings appSettings("MetricSoftware", "Checkmark");
  appSettings.setValue("ExperimentalFeatures",
                       settings.getExperimentalFeaturesEnabled());
  appSettings.setValue("ConsoleVisible", settings.getConsoleVisible());
  appSettings.setValue("ElevatedPriority",
                       settings.getElevatedPriorityEnabled());
  appSettings.setValue("ValidateMetricsOnStartup",
                       settings.getValidateMetricsOnStartup());
  appSettings.setValue("AllowDataCollection",
                       settings.getAllowDataCollection());
  appSettings.setValue("DetailedLogs",
                       settings.getDetailedLogsEnabled());
  appSettings.setValue("AutomaticDataUpload",
                       settings.getAutomaticDataUploadEnabled());
  appSettings.sync();  // Ensure settings are written to disk
}

void SettingsView::OnDeleteAllDataClicked() {
  // Create a custom confirmation dialog
  QDialog confirmDialog(this);
  confirmDialog.setWindowTitle("Delete All Application Data");
  confirmDialog.setFixedSize(450, 220);
  confirmDialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                               Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

  QVBoxLayout* layout = new QVBoxLayout(&confirmDialog);

  // Create icon and text layout
  QHBoxLayout* headerLayout = new QHBoxLayout();
  QLabel* iconLabel = new QLabel(&confirmDialog);
  iconLabel->setPixmap(
    style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(32, 32));
  headerLayout->addWidget(iconLabel);

  QVBoxLayout* textLayout = new QVBoxLayout();
  QLabel* mainText = new QLabel(
    "Are you sure you want to delete ALL application data?", &confirmDialog);
  QFont boldFont = mainText->font();
  boldFont.setBold(true);
  mainText->setFont(boldFont);

  QLabel* infoText = new QLabel("This will permanently delete:\n"
                                "• All application settings\n"
                                "• All diagnostic and benchmark results\n"
                                "• All debug logs\n"
                                "• All user profiles\n\n"
                                "This action cannot be undone.",
                                &confirmDialog);
  infoText->setWordWrap(true);

  textLayout->addWidget(mainText);
  textLayout->addWidget(infoText);
  headerLayout->addLayout(textLayout);

  // Create buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  QPushButton* noButton = new QPushButton("No", &confirmDialog);
  QPushButton* yesButton =
    new QPushButton("Yes, Delete Everything", &confirmDialog);

  // Style buttons
  yesButton->setStyleSheet(R"(
    QPushButton {
      background-color: #AA0000;
      color: white;
      border-radius: 4px;
      padding: 6px 15px;
      border: none;
    }
    QPushButton:hover {
      background-color: #CC0000;
    }
    QPushButton:pressed {
      background-color: #880000;
    }
  )");

  buttonLayout->addStretch();
  buttonLayout->addWidget(noButton);
  buttonLayout->addWidget(yesButton);

  // Add all layouts to main layout
  layout->addLayout(headerLayout);
  layout->addStretch();
  layout->addLayout(buttonLayout);

  // Connect buttons
  connect(noButton, &QPushButton::clicked, &confirmDialog, &QDialog::reject);
  connect(yesButton, &QPushButton::clicked, &confirmDialog, &QDialog::accept);

  // Set default button and focus
  noButton->setDefault(true);
  noButton->setFocus();

  // Execute dialog silently
  int result = confirmDialog.exec();

  if (result == QDialog::Accepted) {
    // Reset all settings first (this is safe as it's internal to the
    // application)
    ApplicationSettings::getInstance().resetAllSettings();

    // Create a list to track deletion results
    struct DeleteResult {
      QString path;
      bool success;
      QString fileType;
    };
    QVector<DeleteResult> deletionResults;

    // Safely delete each category of files with verification
    bool deletedSomething = false;

    // 1. Delete main application settings in AppData
    QString settingsLocation =
      QCoreApplication::applicationDirPath() + "/benchmark_user_data";
    QString settingsFile = settingsLocation + "/application_settings.ini";

    if (QFile::exists(settingsFile)) {
      QFileInfo fileInfo(settingsFile);
      // Verify it's actually an INI file
      if (fileInfo.suffix().toLower() == "ini") {
        bool success = QFile::remove(settingsFile);
        deletionResults.append({settingsFile, success, "INI"});
        deletedSomething |= success;
      }
    }

    // 2. Delete profile data in AppData
    QString appDataLocation = QCoreApplication::applicationDirPath();
    QString profilesDir = appDataLocation + "/profiles";

    if (QDir(profilesDir).exists()) {
      QDir dir(profilesDir);
      // Only delete JSON files from the profiles directory
      QStringList profileFilters;
      profileFilters << "*.json";

      QFileInfoList profileFiles =
        dir.entryInfoList(profileFilters, QDir::Files);
      for (const QFileInfo& fileInfo : profileFiles) {
        QString filePath = fileInfo.absoluteFilePath();
        bool success = QFile::remove(filePath);
        deletionResults.append({filePath, success, "JSON"});
        deletedSomething |= success;
      }
    }

    // 3. Delete debug logs in application directory
    QString appDir = QCoreApplication::applicationDirPath();
    QString logsDir = appDir + "/debug logging";

    if (QDir(logsDir).exists()) {
      QDir dir(logsDir);
      // Only delete log and txt files from logs directory
      QStringList logFilters;
      logFilters << "*.log" << "*.txt";

      QFileInfoList logFiles = dir.entryInfoList(logFilters, QDir::Files);
      for (const QFileInfo& fileInfo : logFiles) {
        QString filePath = fileInfo.absoluteFilePath();
        bool success = QFile::remove(filePath);
        deletionResults.append(
          {filePath, success, fileInfo.suffix().toUpper()});
        deletedSomething |= success;
      }
    }

    // 4. Delete benchmark results and comparisons
    QString benchmarkDir = appDir + "/comparisons";

    if (QDir(benchmarkDir).exists()) {
      QDir dir(benchmarkDir);
      // Only delete specific file types from the benchmark directory
      QStringList benchmarkFilters;
      benchmarkFilters << "*.json" << "*.csv" << "*.txt" << "*.dat"
                       << "*.report";

      QFileInfoList benchmarkFiles =
        dir.entryInfoList(benchmarkFilters, QDir::Files);
      for (const QFileInfo& fileInfo : benchmarkFiles) {
        QString filePath = fileInfo.absoluteFilePath();
        bool success = QFile::remove(filePath);
        deletionResults.append(
          {filePath, success, fileInfo.suffix().toUpper()});
        deletedSomething |= success;
      }
    }

    // 5. Delete any diagnostic data files in specific diagnostic directory
    QString diagnosticDir = appDir + "/diagnostics";

    if (QDir(diagnosticDir).exists()) {
      QDir dir(diagnosticDir);
      // Only delete specific file types
      QStringList diagnosticFilters;
      diagnosticFilters << "*.json" << "*.csv" << "*.txt" << "*.dat"
                        << "*.report";

      QFileInfoList diagnosticFiles =
        dir.entryInfoList(diagnosticFilters, QDir::Files);
      for (const QFileInfo& fileInfo : diagnosticFiles) {
        QString filePath = fileInfo.absoluteFilePath();
        bool success = QFile::remove(filePath);
        deletionResults.append(
          {filePath, success, fileInfo.suffix().toUpper()});
        deletedSomething |= success;
      }
    }

    // Update UI to reflect reset settings
    experimental_features_toggle_->setEnabled(false);
    console_visibility_toggle_->setEnabled(false);
    elevated_priority_toggle_->setEnabled(false);
    validate_metrics_on_startup_toggle_->setEnabled(true);  // Default to true
    allow_data_collection_toggle_->setEnabled(true);  // Default to true
    detailed_logs_toggle_->setEnabled(false);  // Default to false
    automatic_data_upload_toggle_->setEnabled(true);  // Default to true

    // Show confirmation with a custom silent dialog
    QDialog infoDialog(this);
    infoDialog.setWindowTitle("Data Deleted");
    infoDialog.setFixedSize(350, 180);
    infoDialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                              Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

    QVBoxLayout* infoLayout = new QVBoxLayout(&infoDialog);

    // Create icon and text layout
    QHBoxLayout* infoHeaderLayout = new QHBoxLayout();
    QLabel* infoIconLabel = new QLabel(&infoDialog);
    infoIconLabel->setPixmap(
      style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));
    infoHeaderLayout->addWidget(infoIconLabel);

    QString resultMessage;
    if (!deletedSomething && deletionResults.isEmpty()) {
      resultMessage = "No application data was found to delete.";
    } else if (std::all_of(deletionResults.begin(), deletionResults.end(),
                           [](const DeleteResult& r) { return r.success; })) {
      resultMessage = "All application data has been deleted successfully.";
    } else {
      resultMessage =
        "Most application data was deleted, but some files could not be "
        "removed. "
        "You may need to restart the application to complete the process.";
    }

    QLabel* infoMainText = new QLabel(resultMessage, &infoDialog);
    infoMainText->setWordWrap(true);
    infoHeaderLayout->addWidget(infoMainText);

    // Create OK button
    QHBoxLayout* infoButtonLayout = new QHBoxLayout();
    QPushButton* okButton = new QPushButton("OK", &infoDialog);

    infoButtonLayout->addStretch();
    infoButtonLayout->addWidget(okButton);

    // Add all layouts to main layout
    infoLayout->addLayout(infoHeaderLayout);
    infoLayout->addStretch();
    infoLayout->addLayout(infoButtonLayout);

    // Connect button
    connect(okButton, &QPushButton::clicked, &infoDialog, &QDialog::accept);

    // Set default button and focus
    okButton->setDefault(true);
    okButton->setFocus();

    // Execute dialog silently
    infoDialog.exec();
  }
}

void SettingsView::OnValidateMetricsOnStartupChanged(const QString& id, bool enabled) {
  // Save the setting
  ApplicationSettings::getInstance().setValidateMetricsOnStartup(enabled);
}

void SettingsView::OnDataCollectionChanged(const QString& id, bool enabled) {
  // Save the setting
  ApplicationSettings::getInstance().setAllowDataCollection(enabled);
}

void SettingsView::OnDetailedLogsChanged(const QString& id, bool enabled) {
  // Save the setting
  ApplicationSettings::getInstance().setDetailedLogsEnabled(enabled);
  
  // Update the logger level immediately
  Logger& logger = Logger::instance();
  if (enabled) {
    logger.setLevel(TRACE_LEVEL);  // Enable all log levels
  } else {
    logger.setLevel(ERROR_LEVEL);  // Only ERROR and FATAL
  }
}

void SettingsView::OnRequestDataClicked() {
  // Create a placeholder dialog for data request
  QDialog infoDialog(this);
  infoDialog.setWindowTitle("Data Request");
  infoDialog.setFixedSize(400, 180);
  infoDialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                            Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

  QVBoxLayout* layout = new QVBoxLayout(&infoDialog);

  // Create icon and text layout
  QHBoxLayout* headerLayout = new QHBoxLayout();
  QLabel* iconLabel = new QLabel(&infoDialog);
  iconLabel->setPixmap(
    style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));
  headerLayout->addWidget(iconLabel);

  QLabel* messageLabel = new QLabel(
    "Data request functionality is not yet implemented. This feature will be available in a future update when the backend systems are ready.",
    &infoDialog);
  messageLabel->setWordWrap(true);
  headerLayout->addWidget(messageLabel);

  // Create OK button
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  QPushButton* okButton = new QPushButton("OK", &infoDialog);

  buttonLayout->addStretch();
  buttonLayout->addWidget(okButton);

  // Add layouts to main layout
  layout->addLayout(headerLayout);
  layout->addStretch();
  layout->addLayout(buttonLayout);

  // Connect button
  connect(okButton, &QPushButton::clicked, &infoDialog, &QDialog::accept);

  // Set default button
  okButton->setDefault(true);
  okButton->setFocus();

  // Execute dialog silently
  infoDialog.exec();
}

void SettingsView::OnDeleteDataClicked() {
  // Create a placeholder dialog for data deletion request
  QDialog infoDialog(this);
  infoDialog.setWindowTitle("Data Removal Request");
  infoDialog.setFixedSize(400, 180);
  infoDialog.setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint |
                            Qt::WindowTitleHint | Qt::WindowCloseButtonHint);

  QVBoxLayout* layout = new QVBoxLayout(&infoDialog);

  // Create icon and text layout
  QHBoxLayout* headerLayout = new QHBoxLayout();
  QLabel* iconLabel = new QLabel(&infoDialog);
  iconLabel->setPixmap(
    style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(32, 32));
  headerLayout->addWidget(iconLabel);

  QLabel* messageLabel = new QLabel(
    "Data removal request functionality is not yet implemented. This feature will be available in a future update when the backend systems are ready.",
    &infoDialog);
  messageLabel->setWordWrap(true);
  headerLayout->addWidget(messageLabel);

  // Create OK button
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  QPushButton* okButton = new QPushButton("OK", &infoDialog);

  buttonLayout->addStretch();
  buttonLayout->addWidget(okButton);

  // Add layouts to main layout
  layout->addLayout(headerLayout);
  layout->addStretch();
  layout->addLayout(buttonLayout);

  // Connect button
  connect(okButton, &QPushButton::clicked, &infoDialog, &QDialog::accept);

  // Set default button
  okButton->setDefault(true);
  okButton->setFocus();

  // Execute dialog silently
  infoDialog.exec();
}

void SettingsView::OnOpenAppDataLocation() {
  // Get the application data location using Qt's standard paths
  QString appDataPath = QCoreApplication::applicationDirPath();

  // Make sure the directory exists
  QDir dir(appDataPath);
  if (!dir.exists()) {
    dir.mkpath(".");
  }

  // Open the folder using the default file explorer
  QDesktopServices::openUrl(QUrl::fromLocalFile(appDataPath));
}

void SettingsView::OnCheckUpdatesClicked() {
  // Update UI to show checking state
  if (update_status_label_) {
    update_status_label_->setText("Update status: Checking...");
    update_status_label_->setStyleSheet("color: #C7C7C7; font-size: 12px;");
  }
  
  // Disable the button during check
  if (check_updates_button_) {
    check_updates_button_->setEnabled(false);
    check_updates_button_->setText("Checking...");
  }
  
  // Get UpdateManager instance and connect signals
  UpdateManager& updateManager = UpdateManager::getInstance();
  
  // Connect signals for this check (disconnect any previous connections)
  disconnect(&updateManager, &UpdateManager::updateAvailable, this, nullptr);
  disconnect(&updateManager, &UpdateManager::criticalUpdateAvailable, this, nullptr);
  disconnect(&updateManager, &UpdateManager::updateNotAvailable, this, nullptr);
  disconnect(&updateManager, &UpdateManager::updateError, this, nullptr);
  
  connect(&updateManager, &UpdateManager::updateAvailable, this, [this](const QString& version) {
    OnUpdateCheckComplete(true, false);  // normal update
  });
  
  connect(&updateManager, &UpdateManager::criticalUpdateAvailable, this, [this](const QString& version) {
    OnUpdateCheckComplete(true, true);  // critical update
  });
  
  connect(&updateManager, &UpdateManager::updateNotAvailable, this, [this]() {
    OnUpdateCheckComplete(false, false);
  });
  
  connect(&updateManager, &UpdateManager::updateError, this, [this](const QString& error) {
    OnUpdateCheckComplete(false, false);  // Treat error as up to date
  });
  
  // Initialize UpdateManager if not already done
  updateManager.initialize();
  
  // Start the update check
  updateManager.checkForUpdates();
}

void SettingsView::OnUpdateCheckComplete(bool updateAvailable, bool isCritical) {
  
  // Re-enable the button
  if (check_updates_button_) {
    check_updates_button_->setEnabled(true);
    check_updates_button_->setText("Check for Updates");
  }
  
  // Update the status label
  if (update_status_label_) {
    if (updateAvailable) {
      if (isCritical) {
        update_status_label_->setText("Update status: Critical Update Available");
        update_status_label_->setStyleSheet("color: #FF0000; font-size: 12px; font-weight: bold;");  // Red, bold
        
        // TODO: For critical updates, we would force the update here in the future
        // For now, just display the status
      } else {
        update_status_label_->setText("Update status: Update Available");
        update_status_label_->setStyleSheet("color: #FF9900; font-size: 12px;");  // Orange
      }
    } else {
      update_status_label_->setText("Update status: Up To Date");
      update_status_label_->setStyleSheet("color: #4A90E2; font-size: 12px;");  // Blue
    }
  }
}

void SettingsView::OnAutomaticDataUploadChanged(const QString& id, bool enabled) {
  // Save the setting
  ApplicationSettings::getInstance().setAutomaticDataUploadEnabled(enabled);
}
