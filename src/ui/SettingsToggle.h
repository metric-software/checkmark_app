#pragma once
/**
 * @file SettingsToggle.h
 * @brief A customizable toggle control with aligned text and description
 *
 * The SettingsToggle is a composite UI component that combines:
 * - A text label (name)
 * - A sliding toggle control
 * - An optional description text
 * - An optional checkmark indicator
 *
 * USAGE:
 * 1. Basic toggle: Just shows a name and toggle
 *    SettingsToggle* toggle = new SettingsToggle("id", "Setting Name", "");
 *
 * 2. With description: Adds explanatory text below the toggle
 *    SettingsToggle* toggle = new SettingsToggle("id", "Setting Name", "This is
 * what the setting does");
 *
 * 3. Alignment: Control horizontal text/toggle placement
 *    toggle->setAlignment(SettingsToggle::AlignLeft);  // Text left, toggle
 * left toggle->setAlignment(SettingsToggle::AlignRight); // Text left, toggle
 * right (default) toggle->setAlignment(SettingsToggle::AlignCompact); // Text
 * and toggle close together
 *
 * 4. State: Get/set the toggle state
 *    toggle->setEnabled(true);    // Turn on
 *    bool state = toggle->isEnabled(); // Get current state
 *
 * 5. Styling: Control appearance when in greyed-out mode
 *    toggle->setDisabledStyle(true);  // Apply greyed-out appearance
 */
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>  // Changed from QCheckBox
#include <QWidget>

class SettingsToggle : public QWidget {
  Q_OBJECT
 public:
  // Alignment options
  enum Alignment {
    AlignLeft,    // Name and toggle both aligned to left
    AlignRight,   // Name aligned left, toggle aligned right
    AlignCompact  // Name and toggle placed close together
  };

  SettingsToggle(const QString& id, const QString& name,
                 const QString& description, QWidget* parent = nullptr);

  bool isEnabled() const;
  void setEnabled(bool enabled);
  void setDisabledStyle(bool disabled);
  QString getId() const { return settingId; }

  // New methods for checkmark support
  void setCheckmarkVisible(bool visible);
  void addCheckmarkArea();  // Call to add a dedicated space for checkmark
  bool hasCheckmarkArea() const { return checkmarkArea != nullptr; }

  // Set alignment mode
  void setAlignment(Alignment align);

 protected:
  // Add showEvent override
  void showEvent(QShowEvent* event) override;

 signals:
  void stateChanged(const QString& id, bool enabled);

 private:
  QString settingId;
  QLabel* nameLabel;
  QLabel* descriptionLabel;
  QSlider* toggle;                   // Changed from QCheckBox
  QWidget* checkmarkArea = nullptr;  // Optional space for checkmark
  QLabel* checkmark = nullptr;       // Optional checkmark
  QHBoxLayout* topLayout = nullptr;  // Store the layout for alignment changes
  Alignment currentAlignment = AlignRight;  // Default alignment
};
