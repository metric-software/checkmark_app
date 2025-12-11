#pragma once

#include <QCheckBox>
#include <QDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

/**
 * @class SaveProfileDialog
 * @brief Dialog for saving current optimization settings as a profile
 *
 * This dialog allows users to:
 * - Specify which types of settings to include (Rust, Advanced)
 * - Enter a name for the profile
 * - Save the profile to the profiles directory
 *
 * The dialog provides filtering options to control what gets exported:
 * - Include Rust settings: Whether to export Rust game settings
 * - Include Advanced settings: Whether to export advanced optimization settings
 */
class SaveProfileDialog : public QDialog {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the Save Profile dialog
   * @param parent Parent widget
   */
  explicit SaveProfileDialog(QWidget* parent = nullptr);

  /**
   * @brief Gets the entered profile name
   * @return Profile name entered by user
   */
  QString getProfileName() const;

  /**
   * @brief Checks if Rust settings should be included
   * @return True if Rust settings checkbox is checked
   */
  bool includeRustSettings() const;

  /**
   * @brief Checks if Advanced settings should be included
   * @return True if Advanced settings checkbox is checked
   */
  bool includeAdvancedSettings() const;

 private slots:
  /**
   * @brief Handles the Save button click
   */
  void onSaveClicked();

  /**
   * @brief Handles the Cancel button click
   */
  void onCancelClicked();

  /**
   * @brief Validates the profile name as user types
   */
  void onProfileNameChanged();

 private:
  /**
   * @brief Sets up the dialog layout and widgets
   */
  void setupLayout();

  /**
   * @brief Validates the current input
   * @return True if input is valid for saving
   */
  bool validateInput() const;

  // UI widgets
  QLineEdit* profileNameEdit;
  QCheckBox* includeRustCheckBox;
  QCheckBox* includeAdvancedCheckBox;
  QPushButton* saveButton;
  QPushButton* cancelButton;
  QLabel* validationLabel;
};
