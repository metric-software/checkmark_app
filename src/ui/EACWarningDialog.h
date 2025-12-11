#pragma once

#include <QCheckBox>
#include <QDialog>
#include <QSettings>
#include <QString>
#include <QWidget>

// Forward declare for the embedded interface
class EACWarningWidget;

class EACWarningDialog : public QDialog {
  Q_OBJECT

 public:
  explicit EACWarningDialog(QWidget* parent = nullptr);

  // Add a method to create an embedded warning widget
  static EACWarningWidget* createEmbeddedWarning(QWidget* parent);

  // Check if the dialog should be shown
  static bool shouldShowWarning();

  // Mark that the warning has been shown this session
  static void markAsShownForSession();

  // Reset the session flag when app restarts
  static void resetSessionFlag();

  // Save user preference to not show again
  static void setDontShowAgain(bool dontShowAgain);

 private:
  QCheckBox* dontShowAgainCheckbox;

  void setupUI();
  void savePreference();

  // Track if warning was shown during this session
  static bool shownThisSession;
};

// Add a new class for embedded warning that isn't a dialog
class EACWarningWidget : public QWidget {
  Q_OBJECT

 public:
  explicit EACWarningWidget(QWidget* parent = nullptr);

 signals:
  void understood();

 private:
  QCheckBox* dontShowAgainCheckbox;
  void setupUI();
};
