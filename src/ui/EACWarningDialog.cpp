#include "EACWarningDialog.h"

#include <iostream>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ApplicationSettings.h"

#include "logging/Logger.h"

// Initialize static variable
bool EACWarningDialog::shownThisSession = false;

// Static method to create embedded warning
EACWarningWidget* EACWarningDialog::createEmbeddedWarning(QWidget* parent) {
  return new EACWarningWidget(parent);
}

// Dialog constructor - simplified since we're not using it directly
EACWarningDialog::EACWarningDialog(QWidget* parent) : QDialog(parent) {
  // Keep this minimal since we're not using the dialog
  setWindowTitle("Easy Anti-Cheat Notice");
}

// Dialog setup - simplified empty implementation
void EACWarningDialog::setupUI() {
  // Empty implementation since we're not using the dialog
}

EACWarningWidget::EACWarningWidget(QWidget* parent) : QWidget(parent) {
  setStyleSheet("background-color: #1e1e1e;");
  setupUI();
}

void EACWarningWidget::setupUI() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(15);
  mainLayout->setContentsMargins(30, 30, 30, 30);

  // Center the content vertically
  mainLayout->addStretch(1);

  // Content area with dark background
  QWidget* contentWidget = new QWidget(this);
  contentWidget->setStyleSheet(
    "background-color: #252525; border-radius: 4px;");
  QVBoxLayout* contentLayout = new QVBoxLayout(contentWidget);
  contentLayout->setContentsMargins(15, 15, 15, 15);

  // Title moved inside content widget
  QLabel* titleLabel =
    new QLabel("<b>Easy Anti-Cheat Notice</b>", contentWidget);
  titleLabel->setStyleSheet(
    "font-size: 16px; color: #ffffff; background: transparent;");
  titleLabel->setAlignment(Qt::AlignCenter);
  contentLayout->addWidget(titleLabel);

  // Add some spacing between title and text
  contentLayout->addSpacing(10);

  QLabel* messageLabel = new QLabel(contentWidget);
  messageLabel->setWordWrap(true);
  messageLabel->setTextFormat(Qt::RichText);
  messageLabel->setStyleSheet("color: #ffffff; font-size: 13px; line-height: "
                              "150%; background: transparent;");

  // Use a single instance of the message text to avoid duplication
  messageLabel->setText(
    "While our benchmarking process is designed to be compatible with Easy "
    "Anti-Cheat (EAC), "
    "there is a small risk that EAC might flag this application as suspicious "
    "due to its monitoring "
    "of Rust performance.<br><br>"

    "<b>Recommended:</b> For maximum safety, run the benchmark by launching "
    "<span style='color: #00AAFF;'>RustClient.exe</span> directly "
    "from the installation folder instead of "
    "through Steam. This way Rust launches without EAC and should make it "
    "safe.<br><br>"

    "Let us know if you encounter any issues with EAC while using this "
    "application.");

  contentLayout->addWidget(messageLabel);
  mainLayout->addWidget(contentWidget);

  // Don't show again checkbox
  dontShowAgainCheckbox = new QCheckBox("Don't show this message again", this);
  dontShowAgainCheckbox->setStyleSheet("color: #dddddd;");
  mainLayout->addWidget(dontShowAgainCheckbox, 0, Qt::AlignCenter);

  // I Understand button
  QPushButton* okButton = new QPushButton("I Understand", this);
  okButton->setMinimumWidth(120);
  okButton->setStyleSheet(R"(
        QPushButton {
            background-color: #0078d4;
            color: white;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
            font-size: 13px;
        }
        QPushButton:hover {
            background-color: #1084d8;
        }
        QPushButton:pressed {
            background-color: #006cc1;
        }
    )");

  // Center the button
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  buttonLayout->addWidget(okButton);
  buttonLayout->addStretch();
  mainLayout->addLayout(buttonLayout);

  // Add bottom spacing to center content
  mainLayout->addStretch(1);

  // Connect the OK button to save preference and emit signal
  connect(okButton, &QPushButton::clicked, this, [this]() {
    if (dontShowAgainCheckbox->isChecked()) {
      LOG_INFO << "User checked 'Don't show again'";
      EACWarningDialog::setDontShowAgain(true);
    }
    EACWarningDialog::markAsShownForSession();
    emit understood();
  });
}

void EACWarningDialog::savePreference() {
  // Empty implementation since we're not using the dialog
}

bool EACWarningDialog::shouldShowWarning() {
  // Don't show if already shown in this session
  if (shownThisSession) {
    LOG_INFO << "EAC Warning already shown this session, skipping";
    return false;
  }

  // Check settings using ApplicationSettings
  QString value = ApplicationSettings::getInstance().getValue(
    "EACWarning/DontShowAgain", "false");
  LOG_INFO << "EAC Warning check - DontShowAgain value: " << value.toStdString();
  return value != "true";
}

void EACWarningDialog::markAsShownForSession() {
  shownThisSession = true;
  LOG_INFO << "EAC Warning marked as shown for this session";
}

void EACWarningDialog::resetSessionFlag() { shownThisSession = false; }

void EACWarningDialog::setDontShowAgain(bool dontShowAgain) {
  LOG_INFO << "Setting EAC Warning 'DontShowAgain' to: "
           << (dontShowAgain ? "true" : "false");
  ApplicationSettings::getInstance().setValue("EACWarning/DontShowAgain",
                                              dontShowAgain ? "true" : "false");
}
