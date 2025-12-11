#include "RevertManager.h"

#include <iostream>
#include <mutex>

#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#include "../../optimization/BackupManager.h"
#include "../../optimization/OptimizationEntity.h"
#include "../OptimizeView.h"  // For SettingCategory and SettingDefinition types

#include "logging/Logger.h"

namespace optimize_components {

RevertManager::RevertManager(QObject* parent)
    : QObject(parent), sessionOriginalsStored(false) {}

void RevertManager::storeSessionOriginals(
  const QVector<SettingCategory>& categories,
  const QMap<QString, QWidget*>& settingsWidgets,
  QMap<QString, QVariant>& settingsStates) {
  // Use the member mutex to ensure thread safety
  std::lock_guard<std::mutex> lock(sessionOriginalsMutex);

  if (sessionOriginalsStored) {
    LOG_INFO << "[RevertManager] Session originals already stored, skipping";
    return;  // Already stored, don't overwrite
  }

  LOG_INFO << "[RevertManager] === STORING SESSION ORIGINALS IN MEMORY ===";
  LOG_INFO << "[RevertManager] NOTE: Session originals are stored in "
               "application memory only";
  LOG_INFO << "[RevertManager] NOTE: These values are lost when the application closes";
  LOG_INFO << "[RevertManager] Number of categories: " << categories.size();
  LOG_INFO << "[RevertManager] Number of settings widgets: "
            << settingsWidgets.size();
  LOG_INFO << "[RevertManager] Number of settings states: "
            << settingsStates.size();

  // Get the optimization manager instance
  auto& optManager = optimizations::OptimizationManager::GetInstance();

  // Create a temporary map to store values - we'll only update the member
  // variable after all values are collected to avoid partial updates
  QMap<QString, QVariant> tempOriginalValues;

  // Function to recursively process categories
  std::function<void(const SettingCategory&)> processCategory =
    [&tempOriginalValues, &optManager, &processCategory, &settingsWidgets,
     &settingsStates](const SettingCategory& category) {
      LOG_INFO << "[RevertManager] Processing category: "
                << category.name.toStdString()
                << " (ID: " << category.id.toStdString() << ")";
      LOG_INFO << "[RevertManager]   Settings in category: "
                << category.settings.size();

      // Process settings
      for (const auto& setting : category.settings) {
        LOG_INFO << "[RevertManager]   Processing setting: "
                  << setting.name.toStdString()
                  << " (ID: " << setting.id.toStdString() << ")";
        // Strategy: Use multiple sources to get the most accurate current value
        // 1. Check if we have a UI widget with current selection
        // 2. Check if we have stored states
        // 3. Fallback to optimization entity

        QVariant currentValue;
        bool valueFound = false;
        QString valueSource;

        // Try to get value from UI widget first (most accurate for current
        // session state)
        if (auto* widget = settingsWidgets.value(setting.id)) {
          if (auto* dropdown = qobject_cast<SettingsDropdown*>(widget)) {
            int currentIndex = dropdown->currentIndex();
            if (currentIndex >= 0) {
              currentValue = dropdown->itemData(currentIndex);
              valueFound = true;
              valueSource = "UI Widget";
              LOG_INFO << "[RevertManager]     Found value from UI widget: "
                       << currentValue.toString().toStdString();
            }
          } else if (auto* toggle = qobject_cast<SettingsToggle*>(widget)) {
            currentValue = QVariant(toggle->isEnabled());
            valueFound = true;
            valueSource = "UI Toggle";
            LOG_INFO << "[RevertManager]     Found value from UI toggle: "
                     << currentValue.toString().toStdString();
          }
        }

        // Try to get value from stored states if widget didn't work
        if (!valueFound && settingsStates.contains(setting.id)) {
          currentValue = settingsStates[setting.id];
          valueFound = true;
          valueSource = "Stored States";
          LOG_INFO << "[RevertManager]     Found value from stored states: "
                    << currentValue.toString().toStdString();
        }

        // Fallback to optimization entity
        if (!valueFound) {
          if (setting.type == SettingType::Toggle &&
              setting.getCurrentValueFn) {
            // Store the current value
            bool boolValue = setting.getCurrentValueFn();
            currentValue = QVariant(boolValue);
            valueFound = true;
            valueSource = "Toggle Function";
            LOG_INFO << "[RevertManager]     Found value from toggle function: "
                     << currentValue.toString().toStdString();
          } else if (setting.type == SettingType::Dropdown) {
            // Try dropdown function first
            if (setting.getDropdownValueFn) {
              currentValue = setting.getDropdownValueFn();
              if (currentValue.isValid() &&
                  !currentValue.toString().isEmpty() &&
                  currentValue.toString() != "__KEY_NOT_FOUND__" &&
                  currentValue.toString() != "ERROR") {
                valueFound = true;
                valueSource = "Dropdown Function";
                LOG_INFO << "[RevertManager]     Found value from dropdown function: "
                          << currentValue.toString().toStdString();
              }
            }

            // Fallback to optimization entity
            if (!valueFound) {
              auto* opt =
                optManager.FindOptimizationById(setting.id.toStdString());
              if (opt) {
                auto rawCurrentValue = opt->GetCurrentValue();

                // Skip if value is "not found"
                if (std::holds_alternative<std::string>(rawCurrentValue) &&
                    std::get<std::string>(rawCurrentValue) ==
                      "__KEY_NOT_FOUND__") {
                  LOG_INFO << "[RevertManager]     Optimization entity "
                            << "returned __KEY_NOT_FOUND__, skipping";
                  continue;
                }

                // Convert optimization value to QVariant
                if (std::holds_alternative<int>(rawCurrentValue)) {
                  currentValue = QVariant(std::get<int>(rawCurrentValue));
                  valueFound = true;
                  valueSource = "Optimization Entity (int)";
                } else if (std::holds_alternative<std::string>(
                             rawCurrentValue)) {
                  currentValue = QVariant(QString::fromStdString(
                    std::get<std::string>(rawCurrentValue)));
                  valueFound = true;
                  valueSource = "Optimization Entity (string)";
                } else if (std::holds_alternative<bool>(rawCurrentValue)) {
                  currentValue = QVariant(std::get<bool>(rawCurrentValue));
                  valueFound = true;
                  valueSource = "Optimization Entity (bool)";
                }

                if (valueFound) {
                  LOG_INFO << "[RevertManager]     Found value from "
                            "optimization entity: "
                            << currentValue.toString().toStdString();
                }
              } else {
                LOG_INFO << "[RevertManager]     No optimization entity found "
                          "for setting ID: "
                          << setting.id.toStdString();
              }
            }
          }
        }

        // Store the value if we found one
        if (valueFound && currentValue.isValid()) {
          tempOriginalValues[setting.id] = currentValue;
          LOG_INFO << "[RevertManager]     ✓ Stored session original: "
                    << setting.id.toStdString() << " = "
                    << currentValue.toString().toStdString()
                    << " (source: " << valueSource.toStdString() << ")";
        } else {
          LOG_INFO << "[RevertManager]     ✗ No valid value found for setting: "
                    << setting.id.toStdString();
        }
      }

      // Process subcategories
      for (const auto& subCategory : category.subCategories) {
        processCategory(subCategory);
      }
    };

  // Process all top-level categories
  for (const auto& category : categories) {
    processCategory(category);
  }

  // Only after all values are collected, assign to the in-memory storage
  sessionOriginalValues = tempOriginalValues;
  sessionOriginalsStored = true;

  LOG_INFO << "[RevertManager] === SESSION ORIGINALS IN-MEMORY STORAGE COMPLETE ===";
  LOG_INFO << "[RevertManager] Total session originals stored in memory: "
           << sessionOriginalValues.size();
  LOG_INFO << "[RevertManager] These values exist only in application memory "
               "and will be lost when app closes";

  // Log stored values for verification
  if (sessionOriginalValues.size() <= 20) {  // Only log if reasonable number
    for (auto it = sessionOriginalValues.constBegin();
         it != sessionOriginalValues.constEnd(); ++it) {
      LOG_INFO << "[RevertManager]   " << it.key().toStdString() << " = "
                << it.value().toString().toStdString();
    }
  } else {
    LOG_INFO << "[RevertManager]   (Too many values to log individually)";
  }
}

bool RevertManager::hasStoredSessionOriginals() const {
  return sessionOriginalsStored;
}

void RevertManager::showRevertDialog(QWidget* parent) {
  LOG_INFO << "[RevertManager] Showing revert dialog";

  QDialog* dialog = new QDialog(parent);
  dialog->setWindowTitle("Revert Settings");
  dialog->setFixedWidth(400);
  dialog->setStyleSheet("background-color: #1e1e1e; color: #ffffff;");

  QVBoxLayout* layout = new QVBoxLayout(dialog);

  QLabel* titleLabel = new QLabel("Select Revert Option", dialog);
  titleLabel->setStyleSheet(
    "font-size: 16px; font-weight: bold; color: #ffffff; margin-bottom: 10px;");

  QLabel* descLabel = new QLabel(
    "Choose one of the following options to revert your settings:", dialog);
  descLabel->setWordWrap(true);
  descLabel->setStyleSheet("color: #cccccc; margin-bottom: 15px;");

  // Session originals option
  QPushButton* sessionButton =
    new QPushButton("Revert to Session Start", dialog);
  sessionButton->setStyleSheet(R"(
        QPushButton {
            background-color: #333333;
            color: white;
            border: none;
            padding: 10px;
            text-align: left;
            border-radius: 4px;
        }
        QPushButton:hover {
            background-color: #444444;
        }
    )");

  QLabel* sessionDesc =
    new QLabel("Restore all settings to the values they had when you first "
               "opened the application in this session.",
               dialog);
  sessionDesc->setWordWrap(true);
  sessionDesc->setStyleSheet(
    "color: #cccccc; margin-left: 10px; margin-bottom: 15px;");

  // System defaults option
  QPushButton* systemButton =
    new QPushButton("Revert to System Defaults", dialog);
  systemButton->setStyleSheet(R"(
        QPushButton {
            background-color: #333333;
            color: white;
            border: none;
            padding: 10px;
            text-align: left;
            border-radius: 4px;
        }
        QPushButton:hover {
            background-color: #444444;
        }
    )");

  QLabel* systemDesc =
    new QLabel("Restore all settings to their original system default values "
               "(settings before this application modified them).",
               dialog);
  systemDesc->setWordWrap(true);
  systemDesc->setStyleSheet("color: #cccccc; margin-left: 10px;");

  // Cancel button
  QPushButton* cancelButton = new QPushButton("Cancel", dialog);
  cancelButton->setStyleSheet(R"(
        QPushButton {
            background-color: #555555;
            color: white;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
            margin-top: 15px;
        }
        QPushButton:hover {
            background-color: #666666;
        }
    )");

  // Add widgets to layout
  layout->addWidget(titleLabel);
  layout->addWidget(descLabel);
  layout->addWidget(sessionButton);
  layout->addWidget(sessionDesc);
  layout->addWidget(systemButton);
  layout->addWidget(systemDesc);
  layout->addWidget(cancelButton, 0, Qt::AlignRight);

  // Connect buttons
  connect(sessionButton, &QPushButton::clicked, [this, dialog]() {
    LOG_INFO << "[RevertManager] Session originals button clicked";
    dialog->accept();
    emit this->revertTypeSelected(RevertType::SessionOriginals);
  });

  connect(systemButton, &QPushButton::clicked, [this, dialog]() {
    LOG_INFO << "[RevertManager] System defaults button clicked";
    dialog->accept();
    emit this->revertTypeSelected(RevertType::SystemDefaults);
  });

  connect(cancelButton, &QPushButton::clicked, dialog, &QDialog::reject);

  // Show dialog
  dialog->exec();
  dialog->deleteLater();
}

void RevertManager::revertSettings(
  RevertType type, const QVector<SettingCategory>& categories,
  const QMap<QString, QWidget*>& settingsWidgets,
  QMap<QString, QVariant>& settingsStates) {
  LOG_INFO << "[RevertManager] Starting revert operation for type: "
           << (type == RevertType::SystemDefaults ? "SystemDefaults"
                                                  : "SessionOriginals");
  LOG_INFO << "[RevertManager] Number of categories: " << categories.size();

  bool success = false;
  QStringList failedSettings;

  switch (type) {
    case RevertType::SessionOriginals:
      LOG_INFO << "[RevertManager] Calling revertToSessionOriginals";
      success =
        revertToSessionOriginals(categories, settingsWidgets, settingsStates);
      break;
    case RevertType::SystemDefaults:
      LOG_INFO << "[RevertManager] Calling revertToSystemDefaults";
      success =
        revertToSystemDefaults(categories, settingsWidgets, settingsStates);
      break;
  }

  LOG_INFO << "[RevertManager] Revert operation completed. Success: "
            << (success ? "true" : "false");

  // Emit signal to notify listeners about the revert operation result
  emit settingsReverted(type, success, failedSettings);
}

bool RevertManager::revertToSessionOriginals(
  const QVector<SettingCategory>& categories,
  const QMap<QString, QWidget*>& settingsWidgets,
  QMap<QString, QVariant>& settingsStates) {
  LOG_INFO << "[RevertManager] === REVERTING TO SESSION ORIGINALS (IN-MEMORY) ===";
  LOG_INFO << "[RevertManager] NOTE: Using only in-memory session originals, no files involved";
  LOG_INFO << "[RevertManager] Session originals stored in memory: " << sessionOriginalsStored;
  LOG_INFO << "[RevertManager] Available session originals: " << sessionOriginalValues.size();
  LOG_INFO << "[RevertManager] Categories to process: " << categories.size();

  // Check if in-memory session originals are available
  if (!sessionOriginalsStored || sessionOriginalValues.isEmpty()) {
    LOG_WARN << "[RevertManager] In-memory session originals not available";

    QMessageBox::warning(
      nullptr, "Session Originals Not Available",
      "Session originals were not captured during this session.\n\n"
      "This usually happens if you haven't run 'Check Current Settings' yet in "
      "this session.\n\n"
      "Please run 'Check Current Settings' first, then you can use session "
      "revert.\n\n"
      "Note: Session originals are stored in memory only and are lost when the "
      "application closes.",
      QMessageBox::Ok);
    return false;
  }

  bool allSucceeded = true;
  QStringList failedSettings;

  // Function to recursively process categories
  std::function<void(const SettingCategory&)> processCategory =
    [this, &allSucceeded, &failedSettings, &processCategory, &settingsWidgets,
     &settingsStates](const SettingCategory& category) {
      // Process settings
      for (const auto& setting : category.settings) {
        QString settingId = setting.id;

        LOG_INFO << "[RevertManager]   Processing setting: "
                  << setting.name.toStdString()
                  << " (ID: " << settingId.toStdString() << ")";

        if (!sessionOriginalValues.contains(settingId)) {
          LOG_WARN << "[RevertManager]     ✗ No session original stored for "
                    "this setting, skipping";
          continue;
        }

        QVariant value = sessionOriginalValues[settingId];
        LOG_INFO << "[RevertManager]     Session original value: "
                  << value.toString().toStdString();

        bool success = false;

        if (setting.type == SettingType::Toggle) {
          if (setting.setToggleValueFn) {
            success = setting.setToggleValueFn(value.toBool());
            LOG_INFO << "[RevertManager]     Applying toggle value: "
                      << value.toBool() << " -> "
                      << (success ? "SUCCESS" : "FAILED");

            // Also update UI
            if (auto* toggle = qobject_cast<SettingsToggle*>(
                  settingsWidgets.value(settingId))) {
              toggle->setEnabled(value.toBool());
              LOG_INFO << "[RevertManager]     Updated UI toggle widget";
            }

            // Update in-memory state
            if (success) {
              settingsStates[settingId] = value;
              LOG_INFO << "[RevertManager]     Updated settings state";
            }
          } else {
            LOG_WARN << "[RevertManager]     No setToggleValueFn available " "for toggle setting";
          }
        } else if (setting.type == SettingType::Dropdown) {
          if (setting.setDropdownValueFn) {
            success = setting.setDropdownValueFn(value);
            LOG_INFO << "[RevertManager]     Applying dropdown value: "
                      << value.toString().toStdString() << " -> "
                      << (success ? "SUCCESS" : "FAILED");

            // Also update UI
            if (auto* dropdown = qobject_cast<SettingsDropdown*>(
                  settingsWidgets.value(settingId))) {
              // Find the index of the value in the dropdown
              int index = dropdown->findData(value);
              if (index >= 0) {
                dropdown->setCurrentIndex(index);
                LOG_INFO << "[RevertManager]     Updated UI dropdown widget to index: "
                          << index;
              } else {
                LOG_WARN << "[RevertManager]     WARNING: Could not find "
                          "value in dropdown options";
              }
            } else {
              LOG_WARN << "[RevertManager]     WARNING: No dropdown widget "
                           "found for setting";
            }

            // Update in-memory state
            if (success) {
              settingsStates[settingId] = value;
              LOG_INFO << "[RevertManager]     Updated settings state";
            }
          } else {
            LOG_WARN << "[RevertManager]     No setDropdownValueFn available "
                      "for dropdown setting";
          }
        }

        if (!success) {
          allSucceeded = false;
          failedSettings << setting.name;
          LOG_WARN << "[RevertManager]     ✗ Failed to revert setting: "
                    << setting.name.toStdString();
        } else {
          LOG_INFO << "[RevertManager]     ✓ Successfully reverted setting: "
                    << setting.name.toStdString();
        }
      }

      // Process subcategories
      for (const auto& subCategory : category.subCategories) {
        processCategory(subCategory);
      }
    };

  // Process all top-level categories
  for (const auto& category : categories) {
    processCategory(category);
  }

  LOG_INFO << "[RevertManager] === SESSION REVERT COMPLETE ===";
  LOG_INFO << "[RevertManager] Overall success: "
           << (allSucceeded ? "true" : "false");
  if (!failedSettings.isEmpty()) {
    LOG_WARN << "[RevertManager] Failed settings: "
             << failedSettings.join(", ").toStdString();
  }

  // Show result message
  if (!allSucceeded) {
    QMessageBox::warning(nullptr, "Error",
                         "Failed to revert the following settings:\n" +
                           failedSettings.join("\n") +
                           "\nMake sure you're running as administrator.");
  } else {
    QMessageBox::information(
      nullptr, "Success",
      "All settings were reverted successfully to session originals.");
  }

  return allSucceeded;
}

bool RevertManager::revertToSystemDefaults(
  const QVector<SettingCategory>& categories,
  const QMap<QString, QWidget*>& settingsWidgets,
  QMap<QString, QVariant>& settingsStates) {
  // Get the optimization manager and backup manager instances
  auto& optManager = optimizations::OptimizationManager::GetInstance();
  auto& backupManager = optimizations::BackupManager::GetInstance();

  // Initialize backup manager if not already done
  if (!backupManager.Initialize()) {
    LOG_ERROR << "[RevertManager] ERROR: Failed to initialize BackupManager";
    QMessageBox::critical(nullptr, "Error",
                          "Failed to initialize backup manager.");
    return false;
  }

  bool allSucceeded = true;
  QStringList failedSettings;
  QStringList ignoredSettings;

  LOG_INFO << "[RevertManager] === Reverting to System Defaults ===";

  // Function to recursively process categories
  std::function<void(const SettingCategory&)> processCategory =
    [&optManager, &backupManager, &allSucceeded, &failedSettings,
     &ignoredSettings, &processCategory, &settingsWidgets,
     &settingsStates](const SettingCategory& category) {
      // Process settings
      for (const auto& setting : category.settings) {
        QString settingId = setting.id;
        bool success = false;

        // Get the optimization entity
        auto* opt = optManager.FindOptimizationById(settingId.toStdString());
        if (!opt) {
          LOG_WARN << "[RevertManager] Warning: Optimization entity not found for setting: "
                    << settingId.toStdString();
          continue;
        }

        // Get the original value from the main backup
        QVariant originalValue =
          backupManager.GetOriginalValueFromBackup(settingId.toStdString());

        // Check if the original value indicates a missing/non-existent setting
        if (!originalValue.isValid() ||
            originalValue.toString() == "NON_EXISTENT" ||
            originalValue.toString() == "__KEY_NOT_FOUND__" ||
            originalValue.toString() == "KEY_NOT_FOUND") {

          ignoredSettings << setting.name;
          LOG_WARN << "[RevertManager] Ignoring missing/non-existent setting: "
                   << settingId.toStdString() << " (original value: "
                   << originalValue.toString().toStdString() << ")";
          continue;
        }

        LOG_INFO << "[RevertManager] Reverting setting: " << settingId.toStdString()
                 << " to original value: "
                 << originalValue.toString().toStdString();

        // Convert QVariant back to OptimizationValue and apply it
        optimizations::OptimizationValue optValue;
        if (originalValue.type() == QVariant::Bool) {
          optValue = originalValue.toBool();
        } else if (originalValue.type() == QVariant::Int) {
          optValue = originalValue.toInt();
        } else if (originalValue.type() == QVariant::Double) {
          optValue = originalValue.toDouble();
        } else {
          optValue = originalValue.toString().toStdString();
        }

        success = opt->Apply(optValue);

        // Update UI
        if (setting.type == SettingType::Toggle) {
          if (originalValue.type() == QVariant::Bool) {
            bool value = originalValue.toBool();

            // Update UI
            if (auto* toggle = qobject_cast<SettingsToggle*>(
                  settingsWidgets.value(settingId))) {
              toggle->setEnabled(value);
            }

            // Update in-memory state
            if (success) {
              settingsStates[settingId] = value;
            }
          }
        } else if (setting.type == SettingType::Dropdown) {
          // Update UI
          if (auto* dropdown = qobject_cast<SettingsDropdown*>(
                settingsWidgets.value(settingId))) {
            // Find index of matching value
            int index = dropdown->findData(originalValue);
            if (index >= 0) {
              dropdown->setCurrentIndex(index);
            }
          }

          // Update in-memory state
          if (success) {
            settingsStates[settingId] = originalValue;
          }
        }

        if (!success) {
          allSucceeded = false;
          failedSettings << setting.name;
          LOG_ERROR << "[RevertManager] Failed to apply original value for setting: "
                    << settingId.toStdString();
        } else {
          LOG_INFO << "[RevertManager] Successfully reverted setting: "
                   << settingId.toStdString();
        }
      }

      // Process subcategories
      for (const auto& subCategory : category.subCategories) {
        processCategory(subCategory);
      }
    };

  // Process all top-level categories
  for (const auto& category : categories) {
    processCategory(category);
  }

  // Print summary of ignored settings
  if (!ignoredSettings.isEmpty()) {
    LOG_INFO << "[RevertManager] === Ignored Settings (Missing/Non-existent) ===";
    for (const QString& settingName : ignoredSettings) {
      LOG_INFO << "[RevertManager] - " << settingName.toStdString();
    }
    LOG_INFO << "[RevertManager] Total ignored settings: " << ignoredSettings.size();
  }

  // Show result message
  QString statusMessage;
  if (!allSucceeded) {
    statusMessage =
      "Failed to revert the following settings to system defaults:\n" +
      failedSettings.join("\n") +
      "\nMake sure you're running as administrator.";
    if (!ignoredSettings.isEmpty()) {
      statusMessage +=
        "\n\nThe following settings were ignored (missing/non-existent):\n" +
        ignoredSettings.join("\n");
    }
    QMessageBox::warning(nullptr, "Error", statusMessage);
  } else {
    statusMessage = "Settings were reverted successfully to system defaults.";
    if (!ignoredSettings.isEmpty()) {
      statusMessage +=
        "\n\nNote: " + QString::number(ignoredSettings.size()) +
        " settings were ignored because they don't exist on your system.";
    }
    QMessageBox::information(nullptr, "Success", statusMessage);
  }

  LOG_INFO << "[RevertManager] === Revert to System Defaults Complete ===";

  return allSucceeded;
}

bool RevertManager::isOriginalValue(const QString& settingId,
                                    const QVariant& value) const {
  std::lock_guard<std::mutex> lock(sessionOriginalsMutex);

  if (!sessionOriginalsStored || !sessionOriginalValues.contains(settingId)) {
    return false;
  }

  const QVariant& originalValue = sessionOriginalValues.value(settingId);

  // Direct comparison
  if (originalValue == value) {
    return true;
  }

  // Handle numeric string vs int comparison
  if (originalValue.type() == QVariant::Int &&
      value.type() == QVariant::String) {
    bool ok;
    int numValue = value.toString().toInt(&ok);
    return ok && numValue == originalValue.toInt();
  }

  if (originalValue.type() == QVariant::String &&
      value.type() == QVariant::Int) {
    bool ok;
    int numOriginal = originalValue.toString().toInt(&ok);
    return ok && numOriginal == value.toInt();
  }

  // Handle boolean comparison
  if (originalValue.type() == QVariant::Bool &&
      value.type() == QVariant::String) {
    QString strValue = value.toString().toLower();
    return (strValue == "true" && originalValue.toBool()) ||
           (strValue == "false" && !originalValue.toBool());
  }

  if (originalValue.type() == QVariant::String &&
      value.type() == QVariant::Bool) {
    QString strOriginal = originalValue.toString().toLower();
    return (strOriginal == "true" && value.toBool()) ||
           (strOriginal == "false" && !value.toBool());
  }

  return false;
}

}  // namespace optimize_components
