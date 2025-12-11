#include "SaveProfileDialog.h"

#include <QDateTime>
#include <QMessageBox>
#include <QRegularExpression>

SaveProfileDialog::SaveProfileDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("Save Settings Profile");
  setModal(true);
  setFixedSize(450, 350);

  // Apply dark theme styling
  setStyleSheet("background-color: #1e1e1e; color: #ffffff;");

  setupLayout();

  // Set initial focus to the profile name field
  profileNameEdit->setFocus();

  // Generate a default profile name with timestamp
  QString defaultName =
    QString("Profile_%1")
      .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm"));
  profileNameEdit->setText(defaultName);
  profileNameEdit->selectAll();
}

QString SaveProfileDialog::getProfileName() const {
  return profileNameEdit->text().trimmed();
}

bool SaveProfileDialog::includeRustSettings() const {
  return includeRustCheckBox->isChecked();
}

bool SaveProfileDialog::includeAdvancedSettings() const {
  return includeAdvancedCheckBox->isChecked();
}

void SaveProfileDialog::setupLayout() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(20, 20, 20, 20);
  mainLayout->setSpacing(12);  // Reduced spacing to fit content better

  // Title label
  QLabel* titleLabel = new QLabel("Create Settings Profile", this);
  titleLabel->setStyleSheet(
    "font-size: 16px; font-weight: bold; color: #ffffff; margin-bottom: 10px;");
  mainLayout->addWidget(titleLabel);

  // Profile name section
  QGroupBox* nameGroup = new QGroupBox("Profile Name", this);
  nameGroup->setStyleSheet("QGroupBox { color: #ffffff; border: 1px solid "
                           "#444444; margin-top: 0.5em; padding: 5px; } "
                           "QGroupBox::title { subcontrol-origin: margin; "
                           "left: 10px; padding: 0 5px; }");
  QVBoxLayout* nameLayout = new QVBoxLayout(nameGroup);
  nameLayout->setContentsMargins(10, 20, 10,
                                 10);  // Better top padding for title
  nameLayout->setSpacing(5);

  profileNameEdit = new QLineEdit(this);
  profileNameEdit->setMinimumHeight(30);  // Ensure minimum height
  profileNameEdit->setStyleSheet(R"(
        QLineEdit {
            background-color: #2d2d2d;
            color: #ffffff;
            border: 1px solid #555555;
            padding: 6px 8px;
            border-radius: 4px;
            font-size: 12px;
            min-height: 18px;
        }
        QLineEdit:focus {
            border: 1px solid #0078d4;
        }
    )");
  profileNameEdit->setPlaceholderText("Enter profile name...");
  connect(profileNameEdit, &QLineEdit::textChanged, this,
          &SaveProfileDialog::onProfileNameChanged);

  nameLayout->addWidget(profileNameEdit);
  mainLayout->addWidget(nameGroup);

  // Options section
  QGroupBox* optionsGroup = new QGroupBox("Include Settings", this);
  optionsGroup->setStyleSheet("QGroupBox { color: #ffffff; border: 1px solid "
                              "#444444; margin-top: 0.5em; padding: 5px; } "
                              "QGroupBox::title { subcontrol-origin: margin; "
                              "left: 10px; padding: 0 5px; }");
  QVBoxLayout* optionsLayout = new QVBoxLayout(optionsGroup);
  optionsLayout->setContentsMargins(15, 25, 15,
                                    15);  // More top padding for the title
  optionsLayout->setSpacing(12);  // Increased spacing between checkboxes

  // Use the same checkbox style as DiagnosticUploadDialog
  QString checkboxStyle = R"(
        QCheckBox {
            color: #ffffff;
            spacing: 5px;
            padding: 4px 6px;
            background: transparent;
            margin-right: 5px;
            border-radius: 3px;
            font-size: 12px;
        }
        QCheckBox::indicator {
            width: 12px;
            height: 12px;
        }
        QCheckBox::indicator:unchecked {
            border: 1px solid #666666;
            background: #1e1e1e;
        }
        QCheckBox::indicator:checked {
            border: 1px solid #0078d4;
            background: #0078d4;
        }
    )";

  // Rust settings checkbox
  includeRustCheckBox = new QCheckBox("Include Rust Settings", this);
  includeRustCheckBox->setChecked(true);  // Default to checked
  includeRustCheckBox->setStyleSheet(checkboxStyle);
  optionsLayout->addWidget(includeRustCheckBox);

  // Advanced settings checkbox
  includeAdvancedCheckBox = new QCheckBox("Include Advanced Settings", this);
  includeAdvancedCheckBox->setChecked(true);  // Default to checked
  includeAdvancedCheckBox->setStyleSheet(checkboxStyle);
  optionsLayout->addWidget(includeAdvancedCheckBox);

  mainLayout->addWidget(optionsGroup);

  // Validation label
  validationLabel = new QLabel(this);
  validationLabel->setStyleSheet("color: #ff6b6b; font-size: 11px;");
  validationLabel->setVisible(false);
  mainLayout->addWidget(validationLabel);

  // Button section
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->setContentsMargins(0, 10, 0, 0);

  cancelButton = new QPushButton("Cancel", this);
  cancelButton->setStyleSheet(R"(
        QPushButton {
            background-color: #555555;
            color: white;
            border: none;
            padding: 8px 20px;
            border-radius: 4px;
            font-size: 12px;
            min-width: 80px;
        }
        QPushButton:hover {
            background-color: #666666;
        }
        QPushButton:pressed {
            background-color: #444444;
        }
    )");
  connect(cancelButton, &QPushButton::clicked, this,
          &SaveProfileDialog::onCancelClicked);

  saveButton = new QPushButton("Save Profile", this);
  saveButton->setStyleSheet(R"(
        QPushButton {
            background-color: #0078d4;
            color: white;
            border: none;
            padding: 8px 20px;
            border-radius: 4px;
            font-weight: bold;
            font-size: 12px;
            min-width: 100px;
        }
        QPushButton:hover {
            background-color: #1084d8;
        }
        QPushButton:pressed {
            background-color: #005ba1;
        }
        QPushButton:disabled {
            background-color: #333333;
            color: #999999;
        }
    )");
  connect(saveButton, &QPushButton::clicked, this,
          &SaveProfileDialog::onSaveClicked);

  buttonLayout->addStretch();
  buttonLayout->addWidget(cancelButton);
  buttonLayout->addWidget(saveButton);

  mainLayout->addLayout(buttonLayout);

  // Initial validation
  onProfileNameChanged();
}

bool SaveProfileDialog::validateInput() const {
  QString name = profileNameEdit->text().trimmed();

  // Check if name is empty
  if (name.isEmpty()) {
    return false;
  }

  // Check for invalid characters in filename
  QRegularExpression invalidChars(R"([<>:"/\\|?*])");
  if (invalidChars.match(name).hasMatch()) {
    return false;
  }

  // Check if name is too long
  if (name.length() > 100) {
    return false;
  }

  return true;
}

void SaveProfileDialog::onProfileNameChanged() {
  QString name = profileNameEdit->text().trimmed();

  if (name.isEmpty()) {
    validationLabel->setText("Profile name cannot be empty");
    validationLabel->setVisible(true);
    saveButton->setEnabled(false);
    return;
  }

  // Check for invalid characters
  QRegularExpression invalidChars(R"([<>:"/\\|?*])");
  if (invalidChars.match(name).hasMatch()) {
    validationLabel->setText(
      "Profile name contains invalid characters: < > : \" / \\ | ? *");
    validationLabel->setVisible(true);
    saveButton->setEnabled(false);
    return;
  }

  // Check length
  if (name.length() > 100) {
    validationLabel->setText("Profile name is too long (max 100 characters)");
    validationLabel->setVisible(true);
    saveButton->setEnabled(false);
    return;
  }

  // Valid input
  validationLabel->setVisible(false);
  saveButton->setEnabled(true);
}

void SaveProfileDialog::onSaveClicked() {
  if (validateInput()) {
    accept();  // Close dialog with accepted result
  }
}

void SaveProfileDialog::onCancelClicked() {
  reject();  // Close dialog with rejected result
}
