#include "SettingsApplicator.h"

#include <iostream>

#include <QCoreApplication>
#include <QDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "../../optimization/OptimizationEntity.h"
#include "../OptimizeView.h"  // For SettingCategory and SettingDefinition types
#include "../SettingsDropdown.h"
#include "../SettingsToggle.h"

namespace optimize_components {

SettingsApplicator::SettingsApplicator(QObject* parent) : QObject(parent) {}

QList<SettingsApplicator::SettingChange> SettingsApplicator::IdentifyChanges(
  const QVector<SettingCategory>& categories,
  const QMap<QString, QVariant>& settingsStates) {
  // List to hold identified changes
  QList<SettingChange> changesToApply;

  // Process each top-level category
  for (const auto& category : categories) {
    FindChangesInCategory(category, "", settingsStates, changesToApply, false);
  }

  return changesToApply;
}

void SettingsApplicator::FindChangesInCategory(
  const SettingCategory& category, const QString& parentPath,
  const QMap<QString, QVariant>& settingsStates, QList<SettingChange>& changes,
  bool recommendedOnly) {
  // Build the full category path for display
  QString categoryPath =
    parentPath.isEmpty() ? category.name : parentPath + " > " + category.name;

  // Get optimization manager for value checking
  auto& optManager = optimizations::OptimizationManager::GetInstance();

  // If in recommended mode, check recommended settings
  if (recommendedOnly || category.isRecommendedMode) {
    // Check each setting against recommended value
    for (const auto& setting : category.settings) {
      QString settingId = setting.id;

      // Special handling for Rust settings
      if (settingId.startsWith("rust_")) {
        // Extract the actual Rust setting key
        QString rustKey = settingId.mid(5);  // Remove "rust_" prefix

        // Get current and recommended values
        QVariant currentValue;
        if (setting.getDropdownValueFn) {
          currentValue = setting.getDropdownValueFn();
        } else if (setting.getCurrentValueFn) {
          currentValue = QVariant(setting.getCurrentValueFn());
        }

        QVariant newValue = setting.recommended_value;

        // Skip if no values or no change needed
        if (!currentValue.isValid() || !newValue.isValid() ||
            currentValue == newValue) {
          continue;
        }

        // Add to changes list
        changes.append(SettingChange{settingId, setting.name, categoryPath,
                                     currentValue, newValue,
                                     setting.type == SettingType::Toggle});

        continue;  // Skip the standard handling
      }

      // Standard handling for non-Rust settings
      // Get the optimization entity
      auto* opt = optManager.FindOptimizationById(settingId.toStdString());
      if (!opt) {
        continue;
      }

      // Skip settings with dont_edit flag
      if (opt->DontEdit()) {
        continue;
      }

      // Get current value
      auto currentOptValue = opt->GetCurrentValue();
      bool valueFound = false;
      QVariant currentValue;

      // Check if the value is "not found"
      if (std::holds_alternative<std::string>(currentOptValue) &&
          std::get<std::string>(currentOptValue) == "__KEY_NOT_FOUND__") {
        continue;
      }

      // Convert to QVariant
      if (std::holds_alternative<int>(currentOptValue)) {
        currentValue = QVariant(std::get<int>(currentOptValue));
        valueFound = true;
      } else if (std::holds_alternative<std::string>(currentOptValue)) {
        currentValue = QVariant(
          QString::fromStdString(std::get<std::string>(currentOptValue)));
        valueFound = true;
      } else if (std::holds_alternative<bool>(currentOptValue)) {
        currentValue = QVariant(std::get<bool>(currentOptValue));
        valueFound = true;
      }

      if (!valueFound) {
        continue;
      }

      // Get recommended value
      QVariant newValue = setting.recommended_value;

      // Skip if no change needed
      if (currentValue == newValue) {
        continue;
      }

      // Add to changes list
      changes.append(SettingChange{settingId, setting.name, categoryPath,
                                   currentValue, newValue,
                                   setting.type == SettingType::Toggle});
    }
  } else {
    // Check individual settings based on UI state
    for (const auto& setting : category.settings) {
      QString settingId = setting.id;
      if (!settingsStates.contains(settingId)) {
        continue;
      }

      // Special handling for Rust settings
      if (settingId.startsWith("rust_")) {
        // Extract the actual Rust setting key
        QString rustKey = settingId.mid(5);  // Remove "rust_" prefix

        // Get current value
        QVariant currentValue;
        if (setting.getDropdownValueFn) {
          currentValue = setting.getDropdownValueFn();
        } else if (setting.getCurrentValueFn) {
          currentValue = QVariant(setting.getCurrentValueFn());
        }

        // Get new value from UI
        QVariant newValue = settingsStates[settingId];

        // Skip if no values or no change needed
        if (!currentValue.isValid() || !newValue.isValid() ||
            currentValue == newValue) {
          continue;
        }

        // Add to changes list
        changes.append(SettingChange{settingId, setting.name, categoryPath,
                                     currentValue, newValue,
                                     setting.type == SettingType::Toggle});

        continue;  // Skip the standard handling
      }

      // Standard handling for non-Rust settings
      // Get the optimization entity
      auto* opt = optManager.FindOptimizationById(settingId.toStdString());
      if (!opt) {
        continue;
      }

      // Skip settings with dont_edit flag
      if (opt->DontEdit()) {
        continue;
      }

      // Get current value
      auto currentOptValue = opt->GetCurrentValue();
      bool valueFound = false;
      QVariant currentValue;

      // Check if the value is "not found"
      if (std::holds_alternative<std::string>(currentOptValue) &&
          std::get<std::string>(currentOptValue) == "__KEY_NOT_FOUND__") {
        continue;
      }

      // Convert to QVariant
      if (std::holds_alternative<int>(currentOptValue)) {
        currentValue = QVariant(std::get<int>(currentOptValue));
        valueFound = true;
      } else if (std::holds_alternative<std::string>(currentOptValue)) {
        currentValue = QVariant(
          QString::fromStdString(std::get<std::string>(currentOptValue)));
        valueFound = true;
      } else if (std::holds_alternative<bool>(currentOptValue)) {
        currentValue = QVariant(std::get<bool>(currentOptValue));
        valueFound = true;
      }

      if (!valueFound) {
        continue;
      }

      // Get new value from UI
      QVariant newValue = settingsStates[settingId];

      // Skip if no change needed
      if (currentValue == newValue) {
        continue;
      }

      // Add to changes list
      changes.append(SettingChange{settingId, setting.name, categoryPath,
                                   currentValue, newValue,
                                   setting.type == SettingType::Toggle});
    }
  }

  // Process subcategories
  for (const auto& subCategory : category.subCategories) {
    FindChangesInCategory(subCategory, categoryPath, settingsStates, changes,
                          recommendedOnly || subCategory.isRecommendedMode);
  }
}

std::pair<int, QStringList> SettingsApplicator::ApplyChanges(
  const QList<SettingChange>& changes,
  const QVector<SettingCategory>& categories, QWidget* parent) {
  // If no changes, do nothing and return
  if (changes.isEmpty()) {
    return {0, QStringList()};
  }

  // Create and show progress dialog
  QDialog progressDialog(parent);
  progressDialog.setWindowTitle("Applying Settings");
  progressDialog.setFixedWidth(500);
  progressDialog.setMinimumHeight(400);
  progressDialog.setStyleSheet("background-color: #1e1e1e; color: #ffffff;");
  progressDialog.setModal(true);
  progressDialog.setWindowFlags(progressDialog.windowFlags() &
                                ~Qt::WindowContextHelpButtonHint);

  QVBoxLayout* progressLayout = new QVBoxLayout(&progressDialog);
  progressLayout->setSpacing(5);
  progressLayout->setContentsMargins(10, 10, 10, 10);

  QLabel* progressTitle =
    new QLabel("Applying settings changes...", &progressDialog);
  progressTitle->setStyleSheet(
    "font-size: 14px; font-weight: bold; margin-bottom: 5px;");
  progressLayout->addWidget(progressTitle);

  // Create a scroll area for the progress items
  QScrollArea* progressScrollArea = new QScrollArea(&progressDialog);
  progressScrollArea->setWidgetResizable(true);
  progressScrollArea->setStyleSheet("border: none;");
  progressScrollArea->setMinimumHeight(200);

  QWidget* progressContent = new QWidget(progressScrollArea);
  QVBoxLayout* progressItemsLayout = new QVBoxLayout(progressContent);
  progressItemsLayout->setAlignment(Qt::AlignTop);
  progressItemsLayout->setSpacing(4);
  progressItemsLayout->setContentsMargins(5, 5, 5, 5);

  // Create a progress item for each change to apply
  QMap<QString, QWidget*> progressItems;
  QMap<QString, QLabel*> statusLabels;

  for (const auto& change : changes) {
    QWidget* itemWidget = new QWidget(progressContent);
    QHBoxLayout* itemLayout = new QHBoxLayout(itemWidget);
    itemLayout->setContentsMargins(2, 2, 2, 2);

    QLabel* nameLabel = new QLabel(change.name, itemWidget);
    nameLabel->setStyleSheet("color: #ffffff;");

    // Add status indicator (initially pending)
    QLabel* statusLabel = new QLabel("⋯", itemWidget);  // Dots for pending
    statusLabel->setStyleSheet(
      "color: #cccccc; font-size: 16px; min-width: 20px;");
    statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    itemLayout->addWidget(nameLabel);
    itemLayout->addStretch();
    itemLayout->addWidget(statusLabel);

    progressItemsLayout->addWidget(itemWidget);

    // Store for later updating
    progressItems[change.id] = itemWidget;
    statusLabels[change.id] = statusLabel;
  }

  // Add summary label at the bottom
  QLabel* summaryLabel = new QLabel("Processing...", &progressDialog);
  summaryLabel->setStyleSheet("color: #cccccc; margin-top: 5px;");
  summaryLabel->setAlignment(Qt::AlignCenter);

  progressContent->setLayout(progressItemsLayout);
  progressScrollArea->setWidget(progressContent);
  progressLayout->addWidget(progressScrollArea, 1);
  progressLayout->addWidget(summaryLabel, 0);

  // Add "Close" button (initially disabled)
  QPushButton* closeButton = new QPushButton("Close", &progressDialog);
  closeButton->setFixedHeight(28);
  closeButton->setStyleSheet(R"(
        QPushButton {
            background-color: #0078d4;
            color: white;
            border: none;
            padding: 4px 16px;
            border-radius: 4px;
            margin-top: 5px;
        }
        QPushButton:hover {
            background-color: #1084d8;
        }
        QPushButton:disabled {
            background-color: #555555;
        }
    )");
  closeButton->setEnabled(false);
  progressLayout->addWidget(closeButton, 0, Qt::AlignRight);

  connect(closeButton, &QPushButton::clicked, &progressDialog,
          &QDialog::accept);

  // Show progress dialog but don't start event loop yet
  progressDialog.show();

  // Track success/failure
  QStringList failedSettings;
  int successCount = 0;

  // Process all confirmed changes
  for (int i = 0; i < changes.size(); i++) {
    const auto& change = changes[i];

    // Allow UI to update
    QCoreApplication::processEvents();

    // Emit progress update
    emit progressUpdate(i, changes.size(), change.name, false);

    bool success = false;

    // Find the setting across all categories
    const SettingDefinition* setting = FindSettingById(categories, change.id);

    if (!setting) {
      failedSettings << change.name;

      // Update status in UI
      if (statusLabels.contains(change.id)) {
        statusLabels[change.id]->setText("❌");  // Red X for failure
        statusLabels[change.id]->setStyleSheet(
          "color: #ff4444; font-size: 16px; min-width: 20px;");
      }
      continue;
    }

    // Apply the setting based on type
    if (change.isToggle) {
      if (setting->setToggleValueFn) {
        success = setting->setToggleValueFn(change.newValue.toBool());
      }
    } else {
      if (setting->setDropdownValueFn) {
        success = setting->setDropdownValueFn(change.newValue);
      }
    }

    // Update status in UI
    if (statusLabels.contains(change.id)) {
      if (success) {
        statusLabels[change.id]->setText("✓");  // Green checkmark for success
        statusLabels[change.id]->setStyleSheet(
          "color: #44ff44; font-size: 16px; min-width: 20px;");
        successCount++;
      } else {
        statusLabels[change.id]->setText("❌");  // Red X for failure
        statusLabels[change.id]->setStyleSheet(
          "color: #ff4444; font-size: 16px; min-width: 20px;");
        failedSettings << change.name;
      }
    }

    // Emit progress update with success status
    emit progressUpdate(i, changes.size(), change.name, success);

    // Allow UI to update again
    QCoreApplication::processEvents();

    // Short delay to make the progress visible
    QThread::msleep(50);
  }

  // Update summary
  if (failedSettings.isEmpty()) {
    summaryLabel->setText("All settings were applied successfully!");
    summaryLabel->setStyleSheet(
      "color: #44ff44; margin-top: 5px; font-weight: bold;");
  } else {
    summaryLabel->setText(
      QString("Applied %1 of %2 settings. Some settings failed to apply.")
        .arg(successCount)
        .arg(changes.size()));
    summaryLabel->setStyleSheet(
      "color: #ff9944; margin-top: 5px; font-weight: bold;");

    // Add additional information about administrator privileges
    QLabel* adminLabel = new QLabel(
      "Make sure you're running as administrator to apply all settings.",
      &progressDialog);
    adminLabel->setStyleSheet("color: #cccccc; margin-top: 2px;");
    adminLabel->setAlignment(Qt::AlignCenter);
    progressLayout->insertWidget(progressLayout->count() - 1, adminLabel);
  }

  // Add restart notice if at least one setting was successfully applied
  if (successCount > 0) {
    QLabel* restartLabel = new QLabel(
      "Some settings may require a system restart to take full effect.",
      &progressDialog);
    restartLabel->setStyleSheet("color: #cccccc; margin-top: 2px;");
    restartLabel->setAlignment(Qt::AlignCenter);
    progressLayout->insertWidget(progressLayout->count() - 1, restartLabel);
  }

  // Enable close button
  closeButton->setEnabled(true);

  // Emit signal for application completion
  emit changesApplied(successCount, failedSettings);

  // Run the dialog's event loop
  progressDialog.exec();

  return {successCount, failedSettings};
}

void SettingsApplicator::ApplyRecommendedSettings(
  const SettingCategory& category,
  const QMap<QString, QWidget*>& settingsWidgets,
  QMap<QString, QVariant>& settingsStates) {
  // Get optimization manager for checking current values
  auto& optManager = optimizations::OptimizationManager::GetInstance();

  // Apply recommended values for this category
  for (const auto& setting : category.settings) {
    if (setting.recommended_value.isValid()) {
      QString settingId = setting.id;

      // Get the optimization entity to check current value
      auto* opt = optManager.FindOptimizationById(settingId.toStdString());
      if (!opt) {
        continue;
      }

      // Get current value
      auto currentOptValue = opt->GetCurrentValue();
      bool valueFound = false;
      QVariant currentValue;

      // Convert current value to QVariant for comparison
      if (std::holds_alternative<int>(currentOptValue)) {
        currentValue = QVariant(std::get<int>(currentOptValue));
        valueFound = true;
      } else if (std::holds_alternative<std::string>(currentOptValue)) {
        currentValue = QVariant(
          QString::fromStdString(std::get<std::string>(currentOptValue)));
        valueFound = true;
      } else if (std::holds_alternative<bool>(currentOptValue)) {
        currentValue = QVariant(std::get<bool>(currentOptValue));
        valueFound = true;
      }

      if (!valueFound) {
        continue;
      }

      // Skip if already set to recommended value
      if (currentValue == setting.recommended_value) {
        continue;
      }

      bool success = false;
      if (setting.type == SettingType::Toggle) {
        if (setting.setToggleValueFn) {
          success =
            setting.setToggleValueFn(setting.recommended_value.toBool());

          // Also update UI
          if (auto* toggle = qobject_cast<SettingsToggle*>(
                settingsWidgets.value(settingId))) {
            toggle->setEnabled(setting.recommended_value.toBool());
          }

          // Update in-memory state
          if (success) {
            settingsStates[settingId] = setting.recommended_value;
          }
        }
      } else if (setting.type == SettingType::Dropdown) {
        if (setting.setDropdownValueFn) {
          success = setting.setDropdownValueFn(setting.recommended_value);

          // Also update UI
          if (auto* dropdown = qobject_cast<SettingsDropdown*>(
                settingsWidgets.value(settingId))) {
            // Find the recommended value in the dropdown
            int recommendedIndex =
              dropdown->findData(setting.recommended_value);
            if (recommendedIndex >= 0) {
              dropdown->setCurrentIndex(recommendedIndex);
              settingsStates[setting.id] = setting.recommended_value;

              // Apply Recommended tag
              dropdown->setItemTag(recommendedIndex,
                                   SettingsDropdown::TagType::Recommended);
            }
          }

          // Update in-memory state
          if (success) {
            settingsStates[settingId] = setting.recommended_value;
          }
        }
      }
    }
  }

  // Apply recommended settings to subcategories
  for (const auto& subCategory : category.subCategories) {
    ApplyRecommendedSettings(subCategory, settingsWidgets, settingsStates);
  }
}

void SettingsApplicator::LoadOriginalSettings(
  const SettingCategory& category,
  const QMap<QString, QWidget*>& settingsWidgets,
  QMap<QString, QVariant>& settingsStates) {
  // Get optimization manager for value checking
  auto& optManager = optimizations::OptimizationManager::GetInstance();

  // Apply original values to all settings in this category
  for (const auto& setting : category.settings) {
    // Skip Rust settings for now (they have a different backup mechanism)
    if (setting.id.startsWith("rust_")) {
      continue;
    }

    auto* opt = optManager.FindOptimizationById(setting.id.toStdString());
    if (!opt) {
      continue;
    }

    // Get original value from the optimization entity itself
    auto originalValue = opt->GetOriginalValue();

    // Skip if no original value was recorded
    if (originalValue.valueless_by_exception() ||
        (std::holds_alternative<std::string>(originalValue) &&
         std::get<std::string>(originalValue).empty())) {
      continue;
    }

    // Apply the original value
    opt->Apply(originalValue);

    // Update UI
    if (setting.type == SettingType::Toggle) {
      if (std::holds_alternative<bool>(originalValue)) {
        bool value = std::get<bool>(originalValue);

        // Update UI
        if (auto* toggle = qobject_cast<SettingsToggle*>(
              settingsWidgets.value(setting.id))) {
          toggle->setEnabled(value);
        }

        // Update in-memory state
        settingsStates[setting.id] = value;
      }
    } else if (setting.type == SettingType::Dropdown) {
      QVariant value;

      // Convert to QVariant based on type
      if (std::holds_alternative<int>(originalValue)) {
        value = QVariant(std::get<int>(originalValue));
      } else if (std::holds_alternative<std::string>(originalValue)) {
        value = QVariant(
          QString::fromStdString(std::get<std::string>(originalValue)));
      } else if (std::holds_alternative<bool>(originalValue)) {
        value = QVariant(std::get<bool>(originalValue));
      } else if (std::holds_alternative<double>(originalValue)) {
        value = QVariant(std::get<double>(originalValue));
      }

      // Update UI
      if (auto* dropdown = qobject_cast<SettingsDropdown*>(
            settingsWidgets.value(setting.id))) {
        // Find index of matching value
        int index = dropdown->findData(value);
        if (index >= 0) {
          dropdown->setCurrentIndex(index);
        }
      }

      // Update in-memory state
      settingsStates[setting.id] = value;
    }
  }

  // Also process subcategories
  for (const auto& subCategory : category.subCategories) {
    LoadOriginalSettings(subCategory, settingsWidgets, settingsStates);
  }
}

const SettingDefinition* SettingsApplicator::FindSettingById(
  const QVector<SettingCategory>& categories, const QString& id) {
  // Recursive helper function to find setting in category hierarchy
  std::function<const SettingDefinition*(const SettingCategory&)>
    findInCategory =
      [&findInCategory,
       &id](const SettingCategory& category) -> const SettingDefinition* {
    // Check this category's settings
    for (const auto& setting : category.settings) {
      if (setting.id == id) {
        return &setting;
      }
    }

    // Check subcategories
    for (const auto& subCategory : category.subCategories) {
      const SettingDefinition* found = findInCategory(subCategory);
      if (found) return found;
    }

    return nullptr;
  };

  // Search in all top-level categories
  for (const auto& category : categories) {
    const SettingDefinition* result = findInCategory(category);
    if (result) return result;
  }

  return nullptr;
}

}  // namespace optimize_components
