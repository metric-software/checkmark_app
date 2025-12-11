/**
 * @file OptimizeView.cpp
 * @brief Implementation of the modular settings optimizer UI
 *
 * This file implements the OptimizeView widget which provides a UI for
 * optimizing various system settings organized by categories.
 *
 * Key features:
 * - Settings organized hierarchically by categories (up to 3 levels deep)
 * - Toggle and dropdown setting types with custom getter/setter functions
 * - Recommended vs Custom mode for each category
 * - Support for revert points (session-based and full system defaults)
 */

#include "OptimizeView.h"

#include <filesystem>
#include <iostream>
#include <mutex>
#include "../logging/Logger.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QSet>
#include <QSpacerItem>
#include <QThread>
#include <QTimer>

#include "NvApiDriverSettings.h"
#include "../ApplicationSettings.h"
#include "SettingsDropdown.h"
#include "SettingsToggle.h"
#include "optimization/BackupManager.h"
#include "optimization/ExportSettings.h"
#include "optimization/ImportSettings.h"
#include "optimization/NvidiaControlPanel.h"
#include "optimization/OptimizationEntity.h"
#include "optimization/PowerPlanManager.h"
#include "optimization/Rust optimization/config_manager.h"
#include "optimize_components/RevertManager.h"
#include "optimize_components/SaveProfileDialog.h"
#include "optimize_components/SettingsCategoryConverter.h"
#include "optimize_components/SettingsChecker.h"
#include "optimize_components/SettingsUIBuilder.h"
#include "optimize_components/SettingsValidator.h"
#include "optimize_components/UnknownValueManager.h"

OptimizeView::OptimizeView(QWidget* parent) : QWidget(parent), uiBuilder(this) {
  settingsVisible = false;

  // Connect RevertManager signals
  connect(&revertManager, &optimize_components::RevertManager::settingsReverted,
          this,
          [this](optimize_components::RevertType type, bool success,
                 const QStringList& failedSettings) {
            // After reverting, collect and save any unknown values
            collectAndSaveUnknownValues();
          });

  // Connect the revertTypeSelected signal to actually perform the revert
  connect(&revertManager,
          &optimize_components::RevertManager::revertTypeSelected, this,
          [this](optimize_components::RevertType type) {
            // Delegate to RevertManager to perform the actual revert
            revertManager.revertSettings(type, settingCategories,
                                         settingsWidgets, settingsStates);
          });

  // Connect SettingsChecker progress signals to update status label
  connect(
    &settingsChecker, &optimize_components::SettingsChecker::checkProgress,
    this, [this](int progress, const QString& message) {
      if (statusLabel) {
        setStatusText(QString("%1 (%2%)").arg(message).arg(progress));
        statusLabel->setVisible(true);
        statusLabel->setStyleSheet(
          "color: #4A90E2; font-weight: bold;");  // Bright blue for progress
        QApplication::processEvents();  // Ensure UI updates immediately
      }
    });

  // Connect SettingsChecker completion signal
  connect(
    &settingsChecker, &optimize_components::SettingsChecker::checkComplete,
    this, [this](bool success, const QString& errorMessage) {
      if (statusLabel) {
        if (success) {
          setStatusText("Settings loaded successfully!");
          statusLabel->setStyleSheet(
            "color: #32CD32; font-weight: bold;");  // Bright green for success

          // Hide success message after 2 seconds
          QTimer::singleShot(2000, [this]() {
            if (statusLabel) {
              statusLabel->setVisible(false);
            }
          });
        } else {
          setStatusText(QString("Error: %1").arg(errorMessage));
          statusLabel->setStyleSheet(
            "color: #FF6B6B; font-weight: bold;");  // Bright red for errors

          // Hide error message after 5 seconds
          QTimer::singleShot(5000, [this]() {
            if (statusLabel) {
              statusLabel->setVisible(false);
            }
          });
        }
        QApplication::processEvents();
      }
    });

  // Load advanced settings preference from application settings
  showAdvancedSettings =
    ApplicationSettings::getInstance().getAdvancedSettingsEnabled();

  // Default to enabled for better discoverability
  showRustSettings = true;

  setupLayout();

  // Register callback for missing settings creation
  optimize_components::SettingsCategoryConverter::SetOnSettingCreatedCallback(
    [this](const std::string& settingId) {
      // Prevent recursive calls when check is already in progress
      if (checkInProgress) {
        LOG_ERROR
          << "[OptimizeView] ERROR: Skipping recursive performSettingsCheck() "
             "call during setting creation callback"
         ;
        return;
      }

      // Trigger a refresh of the settings UI
      performSettingsCheck();
    });
}

void OptimizeView::setupLayout() {
  // Create main layout
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Add header
  headerWidget = new QWidget(this);
  headerWidget->setMinimumHeight(80);  // Increase height for two rows

  // Create a vertical layout for the header to stack rows
  QVBoxLayout* headerMainLayout = new QVBoxLayout(headerWidget);
  headerMainLayout->setContentsMargins(10, 10, 10, 10);
  headerMainLayout->setSpacing(8);

  // First row: Title and toggles
  QWidget* titleRow = new QWidget(headerWidget);
  QHBoxLayout* titleRowLayout = new QHBoxLayout(titleRow);
  titleRowLayout->setContentsMargins(0, 0, 0, 0);

  // Title label on the left
  QLabel* titleLabel = new QLabel("Optimization Settings", titleRow);
  QFont titleFont = titleLabel->font();
  titleFont.setPointSize(14);
  titleFont.setBold(true);
  titleLabel->setFont(titleFont);
  titleRowLayout->addWidget(titleLabel);

  // Push advanced toggle to the right
  titleRowLayout->addStretch();

  // Add Rust settings toggle controls on right side
  QLabel* rustLabel = new QLabel("Show Rust Settings:", titleRow);
  titleRowLayout->addWidget(rustLabel);

  // Create Rust settings toggle with compact styling
  rustSettingsToggle = new SettingsToggle("rust_settings", "", "", titleRow);
  rustSettingsToggle->setAlignment(
    SettingsToggle::AlignCompact);  // Use compact alignment
  rustSettingsToggle->setEnabled(
    showRustSettings);  // Set to match current preference

  // Explicitly style for this specific toggle
  rustSettingsToggle->setStyleSheet("margin: 0; padding: 0;");

  // Connect toggle signal
  connect(
    rustSettingsToggle, &SettingsToggle::stateChanged, this,
    [this](const QString&, bool enabled) { toggleRustSettings(enabled); });

  titleRowLayout->addWidget(rustSettingsToggle);

  // Add spacing between toggles
  titleRowLayout->addSpacing(20);

  // Add advanced toggle controls on right side
  QLabel* advancedLabel = new QLabel("Show Advanced Settings:", titleRow);
  titleRowLayout->addWidget(advancedLabel);

  // Create advanced settings toggle with compact styling
  advancedSettingsToggle =
    new SettingsToggle("advanced_settings", "", "", titleRow);
  advancedSettingsToggle->setAlignment(
    SettingsToggle::AlignCompact);  // Use compact alignment
  advancedSettingsToggle->setEnabled(
    showAdvancedSettings);  // Set to match current preference

  // Explicitly style for this specific toggle
  advancedSettingsToggle->setStyleSheet("margin: 0; padding: 0;");

  // Connect toggle signal
  connect(
    advancedSettingsToggle, &SettingsToggle::stateChanged, this,
    [this](const QString&, bool enabled) { toggleAdvancedSettings(enabled); });

  titleRowLayout->addWidget(advancedSettingsToggle);

  headerMainLayout->addWidget(titleRow);

  // Second row: Profile dropdown
  QWidget* profileRow = new QWidget(headerWidget);
  QHBoxLayout* profileRowLayout = new QHBoxLayout(profileRow);
  profileRowLayout->setContentsMargins(0, 0, 0, 0);

  // Add profile dropdown controls
  QLabel* profileLabel = new QLabel("Load Profile:", profileRow);
  profileRowLayout->addWidget(profileLabel);

  // Create profile dropdown
  profileDropdown = new QComboBox(profileRow);
  profileDropdown->setMinimumWidth(200);
  profileDropdown->setMaximumWidth(250);
  profileDropdown->setToolTip(
    "Select a settings profile to load. This will update the UI with profile "
    "values but won't apply them until you click Apply.");

  // Setup initial profile list
  setupProfileDropdown();

  // Connect profile selection signal
  connect(profileDropdown, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &OptimizeView::onProfileSelected);

  profileRowLayout->addWidget(profileDropdown);

  // Add stretch to push everything to the left
  profileRowLayout->addStretch();

  headerMainLayout->addWidget(profileRow);

  mainLayout->addWidget(headerWidget);

  // Create a line under the header
  QFrame* line = new QFrame(this);
  line->setFrameShape(QFrame::HLine);
  line->setFrameShadow(QFrame::Sunken);
  mainLayout->addWidget(line);

  // Create container for settings
  settingsContainer = new QWidget(this);
  settingsContainer->setLayout(new QVBoxLayout());
  settingsContainer->layout()->setContentsMargins(10, 10, 10, 10);

  // Create scroll area for settings
  scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setWidget(settingsContainer);
  scrollArea->setFrameShape(QFrame::NoFrame);

  mainLayout->addWidget(scrollArea, 1);

  // Create bottom panel with buttons
  bottomPanel = new QWidget(this);
  QHBoxLayout* bottomLayout = new QHBoxLayout(bottomPanel);

  // Add check settings button
  checkSettingsButton = new QPushButton("Check Current Settings", bottomPanel);
  checkSettingsButton->setToolTip(
    "Check your current system settings against optimal settings");
  connect(checkSettingsButton, &QPushButton::clicked, this,
          &OptimizeView::onCheckCurrentSettings);

  // Create status label for operation feedback
  statusLabel = new QLabel(bottomPanel);
  statusLabel->setVisible(false);  // Hide initially

  // Set fixed width and enable text wrapping
  statusLabel->setFixedWidth(180);
  statusLabel->setWordWrap(true);
  statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  // Set maximum height to accommodate roughly 2 lines of text
  QFontMetrics fontMetrics(statusLabel->font());
  int lineHeight = fontMetrics.lineSpacing();
  statusLabel->setMaximumHeight(lineHeight * 2 + 4);  // +4 for padding

  // Enable text elision when text is too long
  statusLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

  // Set status label styles for different states
  QPalette palette = statusLabel->palette();
  palette.setColor(QPalette::WindowText,
                   QColor(0, 150, 0));  // Green color for normal status
  statusLabel->setPalette(palette);

  // Set font for status label
  QFont statusFont = statusLabel->font();
  statusFont.setBold(true);
  statusLabel->setFont(statusFont);

  // Add apply button
  applyButton = new QPushButton("Apply Settings", bottomPanel);
  applyButton->setToolTip("Apply the selected optimization settings");
  connect(applyButton, &QPushButton::clicked, this,
          &OptimizeView::onApplySettings);

  // Add revert button
  revertButton = new QPushButton("Revert Settings", bottomPanel);
  revertButton->setToolTip("Revert to previous settings");
  connect(revertButton, &QPushButton::clicked, this,
          &OptimizeView::showRevertDialog);

  // Add save profile button
  saveProfileButton = new QPushButton("Save as Profile", bottomPanel);
  saveProfileButton->setToolTip(
    "Save current settings as a profile for later use");
  connect(saveProfileButton, &QPushButton::clicked, this,
          &OptimizeView::onSaveAsProfile);

  // Add to bottom layout with stretch to push to right side
  bottomLayout->addWidget(checkSettingsButton);
  bottomLayout->addWidget(statusLabel);
  bottomLayout->addStretch(1);
  bottomLayout->addWidget(saveProfileButton);
  bottomLayout->addWidget(revertButton);
  bottomLayout->addWidget(applyButton);

  mainLayout->addWidget(bottomPanel);

  // Keep the bottom panel always visible - it should be available for user
  // interaction
  bottomPanel->setVisible(true);
}

void OptimizeView::buildSettingsUI() {
  if (!settingsContainer) {
    LOG_ERROR << "[OptimizeView] ERROR: Settings container not initialized"
             ;
    return;
  }

  // Clear only the settings container, NOT the entire main layout
  // This preserves the header (with status label) and bottom panel

  // Clear widget maps to prevent dangling pointers
  uiBuilder.ClearWidgetMaps();
  settingsWidgets.clear();
  categoryWidgets.clear();

  // Clear only the settings container layout
  QLayout* containerLayout = settingsContainer->layout();
  if (containerLayout) {
    QLayoutItem* item;
    int itemCount = 0;
    while ((item = containerLayout->takeAt(0)) != nullptr) {
      itemCount++;
      if (item->widget()) {
        QWidget* widget = item->widget();
        widget->disconnect();  // Disconnect signals to prevent crashes
        widget->setParent(nullptr);
        delete widget;  // Direct deletion
      }
      delete item;
    }

    // Process any pending events to ensure cleanup is complete
    QApplication::processEvents();
  }

  try {
    // Build the UI from categories
    int totalWidgetsCreated = 0;

    for (int i = 0; i < settingCategories.size(); ++i) {
      const auto& category = settingCategories[i];

      // Filter out Rust settings if toggle is disabled
      if (!showRustSettings && category.id == "rust_game") {
        continue;
      }

      auto categoryGroup = uiBuilder.CreateCategoryGroup(category);
      if (categoryGroup) {
        settingsContainer->layout()->addWidget(categoryGroup);
        totalWidgetsCreated++;
      } else {
        LOG_WARN << "[OptimizeView] WARNING: Category group creation returned "
                     "null for: "
                  << category.id.toStdString();
      }
    }

    // Copy the settings widgets from the UI builder to our map
    const auto& builderWidgets = uiBuilder.GetSettingsWidgets();

    for (auto it = builderWidgets.constBegin(); it != builderWidgets.constEnd();
         ++it) {
      settingsWidgets[it.key()] = it.value();
    }

    // Copy the category widgets from the UI builder as well
    const auto& builderCategoryWidgets = uiBuilder.GetCategoryWidgets();

    for (auto it = builderCategoryWidgets.constBegin();
         it != builderCategoryWidgets.constEnd(); ++it) {
      categoryWidgets[it.key()] = it.value();
    }

    // Register button actions for settings that have them
    std::function<void(const SettingCategory&)> registerButtonActions =
      [this, &registerButtonActions](const SettingCategory& category) {
        // Process settings in this category
        for (const auto& setting : category.settings) {
          if (setting.setButtonActionFn) {
            buttonActions[setting.id] = setting.setButtonActionFn;
          }
        }
        // Process subcategories
        for (const auto& subCategory : category.subCategories) {
          registerButtonActions(subCategory);
        }
      };

    // Register button actions for all categories
    for (const auto& category : settingCategories) {
      registerButtonActions(category);
    }

    // Load unknown values from backup manager
    QMap<QString, QList<QVariant>> unknownValues;
    optimizations::BackupManager& backupManager =
      optimizations::BackupManager::GetInstance();
    backupManager.LoadUnknownValues(unknownValues);

    // Process unknown values if we have saved ones
    for (auto it = unknownValues.constBegin(); it != unknownValues.constEnd();
         ++it) {
      const QString& settingId = it.key();
      const QList<QVariant>& values = it.value();

      for (const QVariant& value : values) {
        // Check if this value is already recorded
        bool alreadyExists = false;
        for (const QVariant& existingValue :
             unknownValueManager.getUnknownValues(settingId)) {
          if (existingValue == value) {
            alreadyExists = true;
            break;
          }
        }

        if (!alreadyExists) {
          unknownValueManager.recordUnknownValue(settingId, value);
        }
      }
    }

    // Add spacer to push content to top
    QVBoxLayout* layout =
      qobject_cast<QVBoxLayout*>(settingsContainer->layout());
    if (layout) {
      layout->addItem(
        new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding));
    } else {
      LOG_ERROR
        << "[OptimizeView] WARNING: Could not cast layout to QVBoxLayout"
       ;
    }

  } catch (const std::exception& e) {
    LOG_INFO << "[OptimizeView] ERROR in buildSettingsUI: " << e.what()
             ;
  }
}

void OptimizeView::loadCurrentSettings() {
  // Clear the current settings states
  settingsStates.clear();

  // Create a value cache to ensure each setting ID always uses the same value
  QMap<QString, QVariant> loadedValues;

  // Process all categories and their settings to load current system values
  std::function<void(const SettingCategory&)> processCategory =
    [this, &processCategory, &loadedValues](const SettingCategory& category) {
      // Skip Rust categories if Rust settings are disabled
      if (!showRustSettings &&
          (category.id == "rust_game" || category.id.startsWith("rust_"))) {
        return;
      }

      // Set the recommended mode dropdown
      if (auto* dropdown = qobject_cast<SettingsDropdown*>(
            settingsWidgets.value("mode_" + category.id))) {
        // Convert legacy isRecommendedMode to CategoryMode
        CategoryMode mode;
        if (categoryModes.contains(category.id)) {
          mode = categoryModes[category.id];
        } else {
          // Convert from legacy boolean
          mode = category.isRecommendedMode ? CategoryMode::Recommended
                                            : CategoryMode::Custom;
          categoryModes[category.id] = mode;
        }

        // Set dropdown to match the mode
        int index = static_cast<int>(mode);
        dropdown->setCurrentIndex(index);
      }

      // Process settings
      for (const auto& setting : category.settings) {
        // Skip Rust settings if disabled
        if (!showRustSettings && setting.id.startsWith("rust_")) {
          continue;
        }

        try {
          // Check if we've already loaded a value for this setting ID
          if (loadedValues.contains(setting.id)) {
            // Set the stored value to the widget
            QVariant storedValue = loadedValues[setting.id];
            if (setting.type == SettingType::Toggle) {
              if (auto* toggle = qobject_cast<SettingsToggle*>(
                    settingsWidgets.value(setting.id))) {
                toggle->setEnabled(storedValue.toBool());
                settingsStates[setting.id] = storedValue;
              }
            } else if (setting.type == SettingType::Dropdown) {
              if (auto* dropdown = qobject_cast<SettingsDropdown*>(
                    settingsWidgets.value(setting.id))) {
                int index = dropdown->findData(storedValue);
                if (index >= 0) {
                  dropdown->setCurrentIndex(index);
                  settingsStates[setting.id] = storedValue;
                }
              }
            }
            continue;
          }

          // Handle all settings as dropdown settings
          if (setting.type == SettingType::Toggle ||
              setting.type == SettingType::Dropdown) {
            // Try to find the dropdown widget for this setting
            auto widgetIter = settingsWidgets.find(setting.id);
            if (widgetIter != settingsWidgets.end()) {
              SettingsDropdown* dropdown =
                qobject_cast<SettingsDropdown*>(widgetIter.value());
              if (dropdown) {
                // Get the current value
                QVariant currentValue;

                if (setting.type == SettingType::Toggle &&
                    setting.getCurrentValueFn) {
                  // Convert boolean to its raw equivalent
                  bool boolValue = setting.getCurrentValueFn();

                  // Try to get the actual raw value from the optimization
                  // entity if possible
                  auto& optManager =
                    optimizations::OptimizationManager::GetInstance();
                  auto* opt =
                    optManager.FindOptimizationById(setting.id.toStdString());
                  if (opt) {
                    auto rawCurrentValue = opt->GetCurrentValue();
                    currentValue =
                      categoryConverter.ConvertOptimizationValueToQVariant(
                        rawCurrentValue);
                  } else {
                    currentValue = QVariant(boolValue);
                  }
                } else if (setting.type == SettingType::Dropdown &&
                           setting.getDropdownValueFn) {
                  currentValue = setting.getDropdownValueFn();
                }

                // Filter out invalid values
                if (!currentValue.isValid() ||
                    currentValue.toString() == "__KEY_NOT_FOUND__" ||
                    currentValue.toString() == "ERROR" ||
                    currentValue.toString().isEmpty()) {
                  dropdown->setCurrentIndex(-1);
                  continue;
                }

                // Normalize numeric string values to integers for consistent
                // comparison Exception: Keep Rust settings as strings to match
                // the backup system
                if (currentValue.type() == QVariant::String &&
                    !setting.id.startsWith("rust_")) {
                  bool isNumeric;
                  int numericValue = currentValue.toString().toInt(&isNumeric);
                  if (isNumeric) {
                    currentValue = QVariant(numericValue);
                  }
                }

                // For Rust boolean settings, normalize case to match dropdown
                // format
                if (setting.id.startsWith("rust_") &&
                    currentValue.type() == QVariant::String) {
                  QString strValue = currentValue.toString();
                  if (strValue.toLower() == "true") {
                    currentValue = QVariant("True");
                  } else if (strValue.toLower() == "false") {
                    currentValue = QVariant("False");
                  }
                }

                // Track value in unknown values manager
                unknownValueManager.recordUnknownValue(setting.id,
                                                       currentValue);

                // Find matching value in dropdown
                int directIndex = -1;
                for (int i = 0; i < dropdown->count(); ++i) {
                  if (dropdown->itemData(i) == currentValue) {
                    directIndex = i;
                    break;
                  }
                }

                if (directIndex >= 0) {
                  dropdown->setCurrentIndex(directIndex);
                  settingsStates[setting.id] = currentValue;
                  loadedValues[setting.id] = currentValue;

                  // Apply tags to ALL matching options
                  uiBuilder.ApplyOriginalTag(dropdown, setting.id);
                  uiBuilder.ApplyRecommendedTag(dropdown, setting.id);
                } else {
                  // Try string-based matching as fallback
                  QString strValue = currentValue.toString().toLower();

                  for (int i = 0; i < dropdown->count(); ++i) {
                    QVariant itemData = dropdown->itemData(i);
                    if (itemData.isValid() &&
                        itemData.type() == QVariant::String &&
                        itemData.toString().toLower() == strValue) {

                      dropdown->setCurrentIndex(i);
                      settingsStates[setting.id] = dropdown->itemData(i);
                      loadedValues[setting.id] = dropdown->itemData(i);

                      // Apply tags
                      uiBuilder.ApplyOriginalTag(dropdown, setting.id);
                      uiBuilder.ApplyRecommendedTag(dropdown, setting.id);
                      break;
                    }
                  }
                }
              }
            }
          }
        } catch (const std::exception& e) {
          // Continue with the next setting
        }
      }

      // Process subcategories
      for (const auto& subCategory : category.subCategories) {
        try {
          // Skip Rust subcategories if disabled
          if (!showRustSettings && (subCategory.id.startsWith("rust_") ||
                                    subCategory.id == "rust_game")) {
            continue;
          }
          processCategory(subCategory);
        } catch (const std::exception& e) {
          // Continue with the next subcategory
        }
      }
    };

  // Process all top-level categories
  for (const auto& category : settingCategories) {
    try {
      processCategory(category);
    } catch (const std::exception& e) {
      // Continue with the next category
    }
  }

  // Save any unknown values that were found
  unknownValueManager.saveUnknownValues();
}

void OptimizeView::onApplySettings() {
  // Use the SettingsApplicator component to identify changes
  auto changes =
    settingsApplicator.IdentifyChanges(settingCategories, settingsStates);

  // If no changes, inform user and return
  if (changes.isEmpty()) {
    QMessageBox::information(this, "No Changes Needed",
                             "All settings are already at the desired values.");
    return;
  }

  // Create and show confirmation dialog
  QDialog dialog(this);
  dialog.setWindowTitle("Confirm Settings Changes");
  dialog.setMinimumWidth(600);

  // Delegate dialog styling to UIBuilder for consistency
  uiBuilder.ApplyDialogStyling(&dialog);

  QVBoxLayout* layout = new QVBoxLayout(&dialog);

  QLabel* titleLabel =
    new QLabel("The following settings will be changed:", &dialog);
  uiBuilder.ApplyDialogTitleStyling(titleLabel);
  layout->addWidget(titleLabel);

  // Create a scroll area for the changes list
  QScrollArea* scrollArea = new QScrollArea(&dialog);
  scrollArea->setWidgetResizable(true);
  scrollArea->setStyleSheet("border: none;");

  QWidget* scrollContent = new QWidget(scrollArea);
  QVBoxLayout* changesLayout = new QVBoxLayout(scrollContent);

  // Group changes by category
  QMap<QString, QList<optimize_components::SettingsApplicator::SettingChange>>
    categorizedChanges;
  for (const auto& change : changes) {
    categorizedChanges[change.category].append(change);
  }

  // Add each category and its changes
  for (auto it = categorizedChanges.constBegin();
       it != categorizedChanges.constEnd(); ++it) {
    QString categoryName = it.key();
    const QList<optimize_components::SettingsApplicator::SettingChange>&
      changes = it.value();

    QGroupBox* categoryBox = new QGroupBox(categoryName, scrollContent);
    uiBuilder.ApplyDialogCategoryStyling(categoryBox);
    QVBoxLayout* categoryLayout = new QVBoxLayout(categoryBox);

    for (const auto& change : changes) {
      QFrame* changeFrame = new QFrame(categoryBox);
      uiBuilder.ApplyDialogChangeStyling(changeFrame);

      QVBoxLayout* changeLayout = new QVBoxLayout(changeFrame);
      changeLayout->setContentsMargins(8, 8, 8, 8);

      QLabel* nameLabel = new QLabel(change.name, changeFrame);
      uiBuilder.ApplyDialogChangeNameStyling(nameLabel);

      QString valueText;
      if (change.isToggle) {
        valueText =
          QString("Current: %1\nNew: %2")
            .arg(change.currentValue.toBool() ? "Enabled" : "Disabled")
            .arg(change.newValue.toBool() ? "Enabled" : "Disabled");
      } else {
        valueText = QString("Current: %1\nNew: %2")
                      .arg(change.currentValue.toString())
                      .arg(change.newValue.toString());
      }

      QLabel* valueLabel = new QLabel(valueText, changeFrame);
      uiBuilder.ApplyDialogChangeValueStyling(valueLabel);

      changeLayout->addWidget(nameLabel);
      changeLayout->addWidget(valueLabel);

      categoryLayout->addWidget(changeFrame);
    }

    changesLayout->addWidget(categoryBox);
  }

  // Add spacer to push content to the top
  changesLayout->addStretch();

  scrollContent->setLayout(changesLayout);
  scrollArea->setWidget(scrollContent);
  layout->addWidget(scrollArea);

  // Add buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();

  QPushButton* cancelButton = new QPushButton("Cancel", &dialog);
  QPushButton* confirmButton = new QPushButton("Apply Changes", &dialog);

  // Delegate button styling to UIBuilder
  uiBuilder.ApplyDialogButtonStyling(cancelButton, false);
  uiBuilder.ApplyDialogButtonStyling(confirmButton, true);

  buttonLayout->addWidget(cancelButton);
  buttonLayout->addWidget(confirmButton);

  layout->addLayout(buttonLayout);

  // Connect buttons
  connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

  bool changesConfirmed = false;
  connect(confirmButton, &QPushButton::clicked, [&]() {
    changesConfirmed = true;
    dialog.accept();
  });

  // Show dialog
  dialog.exec();

  // If user cancelled, return
  if (!changesConfirmed) {
    return;
  }

  // Apply the changes using SettingsApplicator
  auto [successCount, failedSettings] =
    settingsApplicator.ApplyChanges(changes, settingCategories, this);

  // Make sure we save any unknown values from the UI
  collectAndSaveUnknownValues();
}

void OptimizeView::onToggleChanged(const QString& settingId, bool enabled) {
  settingsStates[settingId] = enabled;
}

void OptimizeView::onDropdownChanged(const QString& settingId,
                                     const QVariant& value) {
  settingsStates[settingId] = value;
}

void OptimizeView::onCheckCurrentSettings() {
  // Guard against multiple simultaneous check operations
  if (checkInProgress) {
    return;
  }

  checkInProgress = true;

  // Disable the check button
  checkSettingsButton->setEnabled(false);

  // Show initial status message - detailed progress will come from
  // SettingsChecker signals
  setStatusText("Starting settings check...");
  statusLabel->setStyleSheet(
    "color: #4A90E2; font-weight: bold;");  // Bright blue for consistency
  statusLabel->setVisible(true);

  // Force UI update before continuing with potentially long operation
  QApplication::processEvents();

  // Use a timer to delay the actual check operation to allow UI to update
  QTimer::singleShot(50, this, &OptimizeView::performSettingsCheck);
}

/**
 * @brief Status Indicator Implementation
 *
 * The status indicator system works as follows:
 *
 * 1. When the "Check Current Settings" button is clicked, the
 * onCheckCurrentSettings method:
 *    - Displays a green status label with "Checking your current settings..."
 * message
 *    - Disables the button to prevent multiple clicks
 *    - Uses QApplication::processEvents() to update the UI immediately
 *    - Defers the actual checking operation using QTimer::singleShot
 *
 * 2. The performSettingsCheck method handles the actual work:
 *    - Executes all the checks that might take several seconds
 *    - Updates the status label with success or error message
 *    - Uses a timer to auto-hide the status message after a delay
 *    - Re-enables the button when done
 *
 * This approach keeps the UI responsive during the potentially long operation
 * by:
 * - Providing immediate visual feedback to the user
 * - Running the heavy work after the UI has been updated
 * - Ensuring the button can't be clicked multiple times
 * - Displaying appropriate success or error messages
 *
 * The status label is styled with:
 * - Green color for normal status messages
 * - Red color for error status messages
 * - Bold font for better visibility
 */

void OptimizeView::performSettingsCheck() {
  try {
    QApplication::setOverrideCursor(Qt::WaitCursor);

    // Clear any existing categories and UI - show loading placeholder during
    // this process
    clearCategories(true);

    // Use SettingsChecker to load and check all setting types
    // Progress updates will be handled by the connected signals
    auto categories = settingsChecker.LoadAndCheckSettings();

    if (categories.isEmpty()) {
      // No settings were loaded - this could be an error
      // Error handling is done by the signal connections now
      QApplication::restoreOverrideCursor();
      checkSettingsButton->setEnabled(true);
      checkInProgress = false;
      return;
    }

    // Add each category to the UI
    for (const auto& category : categories) {
      addCategory(category);
    }

    // Build the settings UI from the loaded categories
    buildSettingsUI();

    // Load current settings values into the UI
    loadCurrentSettings();

    // Store original values if not already stored
    bool sessionOriginalsStored = revertManager.hasStoredSessionOriginals();
    if (!sessionOriginalsStored) {
      storeSessionOriginals();
    }

    QApplication::restoreOverrideCursor();
    QApplication::processEvents();

  } catch (const std::exception& e) {
    QApplication::restoreOverrideCursor();

    // Show error message box for critical errors
    QMessageBox::critical(
      this, "Error",
      QString("Error checking system settings: %1").arg(e.what()));
  }

  // Always re-enable the button and reset progress flag
  checkSettingsButton->setEnabled(true);
  checkInProgress = false;
  QApplication::processEvents();
}

void OptimizeView::addCategory(const SettingCategory& category) {
  // Delegate all category management logic to SettingsCategoryConverter
  categoryConverter.AddOrReplaceCategory(settingCategories, category,
                                         getShowAdvancedSettings());
}

void OptimizeView::clearCategories() {
  clearCategories(false);  // Default to not showing placeholder
}

void OptimizeView::clearCategories(bool showLoadingPlaceholder) {
  // Clear data structures
  settingCategories.clear();
  settingsWidgets.clear();
  settingsStates.clear();
  categoryWidgets.clear();
  categoryRecommendedModes.clear();

  // Clear UIBuilder's internal widget maps to prevent dangling pointers
  uiBuilder.ClearWidgetMaps();

  // Instead of hiding the scroll area and bottom panel, clear their contents
  // but keep them visible to maintain layout structure

  // Clear the settings container but don't hide it
  QLayout* layout = settingsContainer->layout();
  if (layout) {
    // Remove all widgets from the layout
    while (QLayoutItem* item = layout->takeAt(0)) {
      if (QWidget* widget = item->widget()) {
        // Disconnect all signals from this widget to prevent crashes during
        // deletion
        widget->disconnect();

        // Remove from parent and delete immediately
        widget->setParent(nullptr);
        delete widget;  // Direct deletion instead of deleteLater()
      }
      delete item;
    }

    // Process any pending events to ensure cleanup is complete
    QApplication::processEvents();

    // Only add placeholder label if we're in a loading state
    if (showLoadingPlaceholder) {
      QLabel* placeholderLabel =
        new QLabel("Loading settings...", settingsContainer);
      placeholderLabel->setAlignment(Qt::AlignCenter);
      placeholderLabel->setStyleSheet(
        "color: #666; font-size: 14px; padding: 20px;");
      layout->addWidget(placeholderLabel);
    }
  }

  // Keep scroll area and bottom panel visible
  scrollArea->setVisible(true);
  bottomPanel->setVisible(true);
  settingsVisible = true;

  // Ensure layout is updated
  updateGeometry();
}

void OptimizeView::cancelOperations() {
  // No-op for now, but could be used to cancel any ongoing operations

  // Make sure buttons are enabled
  applyButton->setEnabled(true);
  checkSettingsButton->setEnabled(true);
}

void OptimizeView::showRevertDialog() { revertManager.showRevertDialog(this); }

void OptimizeView::storeSessionOriginals() {
  // Delegate to the RevertManager to store session originals in memory
  revertManager.storeSessionOriginals(settingCategories, settingsWidgets,
                                      settingsStates);
}

void OptimizeView::onRevertSettings(RevertType type) {
  // Convert enum to RevertManager enum
  optimize_components::RevertType managerType;
  switch (type) {
    case RevertType::SessionOriginals:
      managerType = optimize_components::RevertType::SessionOriginals;
      break;
    case RevertType::SystemDefaults:
      managerType = optimize_components::RevertType::SystemDefaults;
      break;
  }

  // Delegate to RevertManager
  revertManager.revertSettings(managerType, settingCategories, settingsWidgets,
                               settingsStates);

  // Note: We don't need to call collectAndSaveUnknownValues() here as it's
  // handled by the RevertManager signal handler
}

void OptimizeView::addRustSettingsCategory(
  optimizations::rust::RustConfigManager& rustConfigManager) {
  // Create a category for Rust game settings
  SettingCategory rustCategory;
  rustCategory.id = "rust_game_settings";
  rustCategory.name = "Rust Game Settings";
  rustCategory.description =
    "Optimize Rust game settings for maximum performance";
  rustCategory.isRecommendedMode = false;  // Start in Custom mode

  // Create subcategories
  SettingCategory graphicsCategory;
  graphicsCategory.id = "rust_graphics";
  graphicsCategory.name = "Graphics";
  graphicsCategory.description = "Rust graphics settings";
  graphicsCategory.isRecommendedMode = false;

  SettingCategory effectsCategory;
  effectsCategory.id = "rust_effects";
  effectsCategory.name = "Effects";
  effectsCategory.description = "Rust visual effects settings";
  effectsCategory.isRecommendedMode = false;

  SettingCategory otherCategory;
  otherCategory.id = "rust_other";
  otherCategory.name = "Other";
  otherCategory.description = "Rust miscellaneous settings";
  otherCategory.isRecommendedMode = false;

  // Get all settings from the manager
  const auto& allSettings = rustConfigManager.GetAllSettings();

  // Process each setting and put it in the appropriate subcategory
  for (const auto& [key, setting] : allSettings) {
    SettingDefinition def;
    def.id = "rust_" + key;
    def.name = key;
    def.description = "";
    def.is_advanced = false;  // Rust settings are not advanced

    // Determine type based on setting properties
    if (setting.isBool) {
      def.type = SettingType::Dropdown;  // Use dropdown for all settings now

      // Create explicit boolean options for dropdown
      SettingOption trueOption;
      trueOption.value = QVariant(true);
      trueOption.name = "Enabled";
      trueOption.description = "";

      SettingOption falseOption;
      falseOption.value = QVariant(false);
      falseOption.name = "Disabled";
      falseOption.description = "";

      def.possible_values.append(trueOption);
      def.possible_values.append(falseOption);

      // Set getter function for boolean dropdown
      def.getDropdownValueFn = [key, &rustConfigManager]() -> QVariant {
        const auto& settings = rustConfigManager.GetAllSettings();
        auto it = settings.find(key);
        if (it != settings.end()) {
          QString currentVal = it->second.currentValue;
          if (currentVal.isEmpty() || currentVal == "missing") {
            return QVariant(it->second.optimalValue.toLower() == "true");
          }
          return QVariant(currentVal.toLower() == "true");
        }
        return QVariant(false);
      };

      // Set setter function for boolean dropdown
      def.setDropdownValueFn =
        [key, &rustConfigManager](const QVariant& value) -> bool {
        bool boolValue = value.toBool();
        return rustConfigManager.ApplySetting(key,
                                              boolValue ? "True" : "False");
      };

      // Set default and recommended values for boolean dropdown
      def.default_value = QVariant(setting.currentValue.toLower() == "true");
      def.recommended_value =
        QVariant(setting.optimalValue.toLower() == "true");
    } else {
      // This is a dropdown setting
      def.type = SettingType::Dropdown;

      // Set getter function
      def.getDropdownValueFn = [key, &rustConfigManager]() -> QVariant {
        const auto& settings = rustConfigManager.GetAllSettings();
        auto it = settings.find(key);
        if (it != settings.end()) {
          // Make sure to return a valid value from possibleValues
          QString currentVal = it->second.currentValue;
          // If current value is not valid or missing, use the optimal value
          if (currentVal.isEmpty() || currentVal == "missing") {
            return QVariant(it->second.optimalValue);
          }

          // Handle numeric values properly - always convert numeric strings to
          // integers
          bool ok;
          int intVal = currentVal.toInt(&ok);
          if (ok) {
            return QVariant(intVal);
          }

          // Handle boolean values consistently
          if (currentVal.toLower() == "true") {
            return QVariant(true);
          } else if (currentVal.toLower() == "false") {
            return QVariant(false);
          }

          return QVariant(currentVal);
        }
        return QVariant();
      };

      // Set setter function
      def.setDropdownValueFn =
        [key, &rustConfigManager](const QVariant& value) -> bool {
        // Convert numeric QVariant values to string properly
        QString stringValue;
        if (value.type() == QVariant::Int) {
          stringValue = QString::number(value.toInt());
        } else {
          stringValue = value.toString();
        }
        return rustConfigManager.ApplySetting(key, stringValue);
      };

      // Create options for dropdown with consistent styling
      QSet<QString> seenValues;  // Track values to prevent duplicates

      for (const QVariant& val : setting.possibleValues) {
        // Normalize the value for consistent comparison
        QVariant normalizedValue = val;
        QString valueKey;

        if (val.type() == QVariant::String) {
          bool isNumeric;
          int numValue = val.toString().toInt(&isNumeric);
          if (isNumeric) {
            // Convert numeric strings to integers for consistency
            normalizedValue = QVariant(numValue);
            valueKey = QString("int:%1").arg(numValue);
          } else {
            valueKey = QString("string:%1").arg(val.toString().toLower());
          }
        } else if (val.type() == QVariant::Int) {
          valueKey = QString("int:%1").arg(val.toInt());
        } else if (val.type() == QVariant::Bool) {
          valueKey = QString("bool:%1").arg(val.toBool() ? "true" : "false");
        } else {
          valueKey = QString("other:%1").arg(val.toString());
        }

        // Skip if we've already seen this value
        if (seenValues.contains(valueKey)) {
          continue;
        }
        seenValues.insert(valueKey);

        SettingOption option;
        option.value = normalizedValue;

        // Create display name based on the normalized value
        if (normalizedValue.type() == QVariant::Int) {
          option.name = QString::number(normalizedValue.toInt());
        } else if (normalizedValue.type() == QVariant::Bool) {
          option.name = normalizedValue.toBool() ? "Enabled" : "Disabled";
        } else {
          option.name = normalizedValue.toString();
        }

        // Add descriptions consistently with other settings
        if (normalizedValue.toString() == setting.optimalValue) {
          option.description = "Recommended";
        } else {
          option.description = "";
        }

        def.possible_values.append(option);
      }

      // For some specific settings, ensure critical values are included
      if (key == "graphics.maxqueuedframes") {
        // Make sure 0 is included in the options
        bool has0 = false;
        for (const SettingOption& option : def.possible_values) {
          if (option.value.type() == QVariant::Int &&
              option.value.toInt() == 0) {
            has0 = true;
            break;
          }
        }

        if (!has0) {
          SettingOption option;
          option.value = QVariant(0);
          option.name = "0";
          option.description = "No Frame Queuing";
          def.possible_values.append(option);
        }
      }

      // Set default and recommended values
      def.default_value = QVariant(setting.currentValue);
      def.recommended_value = QVariant(setting.optimalValue);
    }

    // Add setting to appropriate subcategory based on name
    if (key.startsWith("graphics.") || key.startsWith("graphicssettings.") ||
        key.startsWith("mesh.") || key.startsWith("tree.") ||
        key.startsWith("water.") || key.startsWith("grass.") ||
        key.startsWith("terrain.") || key.startsWith("render.")) {
      graphicsCategory.settings.append(def);
    } else if (key.startsWith("effects.")) {
      effectsCategory.settings.append(def);
    } else {
      otherCategory.settings.append(def);
    }
  }

  // Add subcategories to main category (only if they have settings)
  if (!graphicsCategory.settings.isEmpty()) {
    rustCategory.subCategories.append(graphicsCategory);
  }

  if (!effectsCategory.settings.isEmpty()) {
    rustCategory.subCategories.append(effectsCategory);
  }

  if (!otherCategory.settings.isEmpty()) {
    rustCategory.subCategories.append(otherCategory);
  }

  // Add the Rust category
  addCategory(rustCategory);

  // Apply recommended settings if in recommended mode (which should be false
  // now)
  if (rustCategory.isRecommendedMode) {
    applyRecommendedSettings(rustCategory);
  }
}

void OptimizeView::onButtonClicked(const QString& settingId) {
  // Call any registered button action
  auto it = buttonActions.find(settingId);
  if (it != buttonActions.end() && it.value()) {
    bool success = it.value()();
    if (!success) {
      // Show error message
      QMessageBox::warning(this, "Button Action Failed",
                           "The requested action could not be completed.");
    }
  }
}

void OptimizeView::toggleAdvancedSettings(bool show) {
  // Prevent UI rebuild during active check
  if (checkInProgress) {
    showAdvancedSettings = show;
    ApplicationSettings::getInstance().setAdvancedSettingsEnabled(show);
    return;
  }

  showAdvancedSettings = show;

  // Save the preference
  ApplicationSettings::getInstance().setAdvancedSettingsEnabled(show);

  // Rebuild the UI to reflect the change
  buildSettingsUI();
  loadCurrentSettings();
}

void OptimizeView::toggleRustSettings(bool show) {
  // Prevent UI rebuild during active check
  if (checkInProgress) {
    showRustSettings = show;
    return;
  }

  showRustSettings = show;

  // Rebuild the UI to reflect the change
  buildSettingsUI();
  loadCurrentSettings();
}

void OptimizeView::onCategoryModeChanged(const QString& categoryId,
                                         int modeIndex) {
  // Convert index to CategoryMode enum
  CategoryMode mode;
  switch (modeIndex) {
    case 0:  // First option - Keep Original
      mode = CategoryMode::KeepOriginal;
      break;
    case 1:  // Second option - Recommended
      mode = CategoryMode::Recommended;
      break;
    case 2:  // Third option - Custom
      mode = CategoryMode::Custom;
      break;
    default:
      mode = CategoryMode::Custom;
  }

  // Update the stored mode
  categoryModes[categoryId] = mode;

  // For backward compatibility, also update the boolean flag
  categoryRecommendedModes[categoryId] = (mode == CategoryMode::Recommended);

  // Find the category
  SettingCategory* category =
    categoryConverter.FindCategoryById(categoryId, settingCategories);
  if (!category) {
    return;
  }

  // Update the category's mode
  categoryConverter.SetCategoryMode(*category, mode, true, categoryModes);

  // Find the group box for this category
  QGroupBox* groupBox = categoryWidgets.value(categoryId);
  if (!groupBox) {
    return;
  }

  // Apply UI styles based on mode
  bool shouldGreyOut = (mode != CategoryMode::Custom);
  uiBuilder.ApplyGreyedOutStyle(groupBox, categoryId, shouldGreyOut);
  uiBuilder.ApplyCollapsedStyle(groupBox, categoryId, shouldGreyOut);

  // Apply the appropriate settings based on mode
  switch (mode) {
    case CategoryMode::KeepOriginal:
      // Load original values for this category
      settingsApplicator.LoadOriginalSettings(*category, settingsWidgets,
                                              settingsStates);
      break;
    case CategoryMode::Recommended:
      // Apply recommended values for this category
      settingsApplicator.ApplyRecommendedSettings(*category, settingsWidgets,
                                                  settingsStates);
      break;
    case CategoryMode::Custom:
      // Do nothing for custom mode - user controls settings
      break;
  }
}

void OptimizeView::collectAndSaveUnknownValues() {
  // Make sure to collect any current values in UI that might be unknown
  std::function<void(const SettingCategory&)> collectCurrentUnknownValues =
    [this, &collectCurrentUnknownValues](const SettingCategory& category) {
      // Process settings
      for (const auto& setting : category.settings) {
        if (setting.type == SettingType::Dropdown) {
          if (auto* dropdown = qobject_cast<SettingsDropdown*>(
                settingsWidgets.value(setting.id))) {
            // Get the current value from the dropdown
            int currentIndex = dropdown->currentIndex();
            if (currentIndex >= 0) {
              QVariant currentValue = dropdown->itemData(currentIndex);

              // Check if this is already in the predefined values
              bool isInPredefinedValues = false;
              for (const SettingOption& option : setting.possible_values) {
                if (option.value == currentValue) {
                  isInPredefinedValues = true;
                  break;
                }
              }

              // If not in predefined values, record it with the manager
              if (!isInPredefinedValues) {
                unknownValueManager.recordUnknownValue(setting.id,
                                                       currentValue);
              }
            }
          }
        }
      }

      // Process subcategories
      for (const auto& subCategory : category.subCategories) {
        collectCurrentUnknownValues(subCategory);
      }
    };

  // Collect values from current UI state
  for (const auto& category : settingCategories) {
    collectCurrentUnknownValues(category);
  }

  // Now save everything
  unknownValueManager.saveUnknownValues();
}

void OptimizeView::onRecommendedModeChanged(const QString& categoryId,
                                            bool isRecommended) {
  // Find the category
  SettingCategory* category =
    categoryConverter.FindCategoryById(categoryId, settingCategories);
  if (!category) {
    return;
  }

  // Update the category's mode
  categoryRecommendedModes[categoryId] = isRecommended;
  categoryConverter.SetRecommendedMode(*category, isRecommended, true);

  // Find the group box for this category
  QGroupBox* groupBox = categoryWidgets.value(categoryId);
  if (!groupBox) {
    return;
  }

  // Apply UI styles based on recommended mode
  uiBuilder.ApplyGreyedOutStyle(groupBox, categoryId, isRecommended);
  uiBuilder.ApplyCollapsedStyle(groupBox, categoryId, isRecommended);

  // Apply recommended settings if in recommended mode
  if (isRecommended) {
    applyRecommendedSettings(*category);
  }
}

void OptimizeView::applyRecommendedSettings(const SettingCategory& category) {
  // Delegate to the SettingsApplicator component
  settingsApplicator.ApplyRecommendedSettings(category, settingsWidgets,
                                              settingsStates);

  // Also apply to subcategories if they inherit the recommended mode
  for (const auto& subCategory : category.subCategories) {
    if (subCategory.isRecommendedMode ||
        (categoryRecommendedModes.contains(subCategory.id) &&
         categoryRecommendedModes[subCategory.id])) {
      applyRecommendedSettings(subCategory);
    }
  }
}

void OptimizeView::clearMainLayout() {
  LOG_WARN << "[OptimizeView] WARNING: clearMainLayout is clearing the ENTIRE "
               "UI including header and bottom panel!"
           ;

  if (mainLayout) {
    // Clear widget maps to prevent dangling pointers
    uiBuilder.ClearWidgetMaps();
    settingsWidgets.clear();
    categoryWidgets.clear();

    // Clear the layout
    QLayoutItem* item;
    int itemCount = 0;
    while ((item = mainLayout->takeAt(0)) != nullptr) {
      itemCount++;
      if (item->widget()) {
        item->widget()->deleteLater();
      }
      delete item;
    }
  } else {
    LOG_ERROR << "[OptimizeView] ERROR: mainLayout is null";
  }
}

void OptimizeView::setStatusText(const QString& text) {
  if (!statusLabel) {
    return;
  }

  // Get font metrics to calculate text dimensions
  QFontMetrics fontMetrics(statusLabel->font());
  int labelWidth = statusLabel->width();
  int maxHeight = statusLabel->maximumHeight();
  int lineHeight = fontMetrics.lineSpacing();
  int maxLines = maxHeight / lineHeight;

  // Check if text fits within the available space
  QRect boundingRect = fontMetrics.boundingRect(
    QRect(0, 0, labelWidth, maxHeight),
    Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);

  if (boundingRect.height() <= maxHeight) {
    // Text fits, use as-is
    statusLabel->setText(text);
  } else {
    // Text is too long, need to truncate with elision
    QString elidedText = text;

    // Calculate how much text can fit in the available lines
    int availableLines = maxLines;

    // Use elided text that fits within the height constraint
    while (availableLines > 0) {
      QString testText = elidedText;
      if (availableLines == 1) {
        // For the last line, use elided text
        testText =
          fontMetrics.elidedText(elidedText, Qt::ElideRight, labelWidth);
      }

      QRect testRect = fontMetrics.boundingRect(
        QRect(0, 0, labelWidth, maxHeight),
        Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, testText);

      if (testRect.height() <= lineHeight * availableLines) {
        statusLabel->setText(testText);
        return;
      }

      // Remove some text and try again
      if (elidedText.length() > 10) {
        elidedText = elidedText.left(elidedText.length() - 10);
      } else {
        break;
      }
    }

    // Fallback: use simple elided text for one line
    QString fallbackText =
      fontMetrics.elidedText(text, Qt::ElideRight, labelWidth);
    statusLabel->setText(fallbackText);
  }
}

// Profile Management Methods

void OptimizeView::setupProfileDropdown() {
  if (!profileDropdown) {
    return;
  }

  // Clear existing items
  profileDropdown->clear();

  // Add default "Select Profile" option
  profileDropdown->addItem("Select Profile...", QVariant());

  // Refresh the profile list
  refreshProfileList();
}

void OptimizeView::refreshProfileList() {
  if (!profileDropdown) {
    return;
  }

  // Store current selection to restore it if possible
  QString currentProfilePath;
  int currentIndex = profileDropdown->currentIndex();
  if (currentIndex > 0) {  // Skip the "Select Profile..." option
    currentProfilePath = profileDropdown->itemData(currentIndex).toString();
  }

  // Remove all items except the first "Select Profile..." item
  while (profileDropdown->count() > 1) {
    profileDropdown->removeItem(1);
  }

  try {
    // Get profiles directory
    QString profilesDir = getProfilesDirectory();

    // Get available profile files
    QStringList profiles = optimizations::ImportSettings::GetAvailableProfiles(
      profilesDir.toStdString());

    // Add each profile to the dropdown
    for (const QString& profilePath : profiles) {
      QFileInfo fileInfo(profilePath);
      QString fileName = fileInfo.baseName();  // Filename without extension

      // Use just the clean filename as the display name
      QString displayName = fileName;

      profileDropdown->addItem(displayName, profilePath);
    }

    // Restore previous selection if it still exists
    if (!currentProfilePath.isEmpty()) {
      int newIndex = profileDropdown->findData(currentProfilePath);
      if (newIndex >= 0) {
        profileDropdown->blockSignals(true);
        profileDropdown->setCurrentIndex(newIndex);
        profileDropdown->blockSignals(false);
      }
    }

    LOG_INFO << "[OptimizeView] Loaded " << profiles.size()
              << " profile(s) from " << profilesDir.toStdString();

  } catch (const std::exception& e) {
    LOG_INFO << "[OptimizeView] Error refreshing profile list: " << e.what()
             ;
  }
}

void OptimizeView::onProfileSelected(int index) {
  if (!profileDropdown || index <= 0) {
    // Index 0 is "Select Profile..." - do nothing
    return;
  }

  QString profilePath = profileDropdown->itemData(index).toString();
  if (profilePath.isEmpty()) {
    return;
  }

  LOG_INFO << "[OptimizeView] Profile selected: " << profilePath.toStdString()
           ;

  // Load the selected profile
  loadSettingsProfile(profilePath);

  // Reset dropdown to "Select Profile..." after loading
  profileDropdown->blockSignals(true);
  profileDropdown->setCurrentIndex(0);
  profileDropdown->blockSignals(false);
}

void OptimizeView::loadSettingsProfile(const QString& profilePath) {
  try {
    // Show loading status
    if (statusLabel) {
      setStatusText("Loading profile...");
      statusLabel->setStyleSheet("color: #4A90E2; font-weight: bold;");
      statusLabel->setVisible(true);
      QApplication::processEvents();
    }

    // Import the settings from the profile file
    optimizations::ImportResult importResult =
      optimizations::ImportSettings::ImportSettingsFromFile(
        profilePath.toStdString());

    if (!importResult.success) {
      QString errorMsg =
        QString("Failed to load profile: %1")
          .arg(QString::fromStdString(importResult.error_message));
      LOG_INFO << "[OptimizeView] " << errorMsg.toStdString();

      if (statusLabel) {
        setStatusText(errorMsg);
        statusLabel->setStyleSheet("color: #FF6B6B; font-weight: bold;");

        // Hide error message after 5 seconds
        QTimer::singleShot(5000, [this]() {
          if (statusLabel) {
            statusLabel->setVisible(false);
          }
        });
      }
      return;
    }

    // Apply the imported settings to the UI
    applyImportedSettingsToUI(importResult);

    // Show success status
    if (statusLabel) {
      QString successMsg =
        QString("Profile loaded: %1 settings applied, %2 missing, %3 errors")
          .arg(importResult.imported_settings)
          .arg(importResult.missing_settings)
          .arg(importResult.error_settings);

      setStatusText(successMsg);
      statusLabel->setStyleSheet("color: #32CD32; font-weight: bold;");

      // Hide success message after 3 seconds
      QTimer::singleShot(3000, [this]() {
        if (statusLabel) {
          statusLabel->setVisible(false);
        }
      });
    }

    LOG_INFO << "[OptimizeView] Profile loaded successfully: "
              << importResult.imported_settings << " settings imported, "
              << importResult.missing_settings << " missing";

  } catch (const std::exception& e) {
    QString errorMsg = QString("Exception loading profile: %1").arg(e.what());
    LOG_INFO << "[OptimizeView] " << errorMsg.toStdString();

    if (statusLabel) {
      setStatusText(errorMsg);
      statusLabel->setStyleSheet("color: #FF6B6B; font-weight: bold;");

      QTimer::singleShot(5000, [this]() {
        if (statusLabel) {
          statusLabel->setVisible(false);
        }
      });
    }
  }
}

void OptimizeView::applyImportedSettingsToUI(
  const optimizations::ImportResult& importResult) {
  // Update settingsStates with the imported values
  for (auto categoryIt = importResult.imported_values.constBegin();
       categoryIt != importResult.imported_values.constEnd(); ++categoryIt) {

    const QList<optimizations::ImportedSetting>& settingsInCategory =
      categoryIt.value();

    for (const auto& importedSetting : settingsInCategory) {
      if (importedSetting.status == "imported") {
        // Update the settings state
        settingsStates[importedSetting.id] = importedSetting.value;

        // Update the UI widget to reflect the new value
        QWidget* widget = settingsWidgets.value(importedSetting.id);
        if (!widget) {
          continue;
        }

        // Handle different widget types
        if (auto* dropdown = qobject_cast<SettingsDropdown*>(widget)) {
          // Find the matching value in the dropdown
          int targetIndex = -1;
          for (int i = 0; i < dropdown->count(); ++i) {
            QVariant itemData = dropdown->itemData(i);
            if (itemData == importedSetting.value) {
              targetIndex = i;
              break;
            }
          }

          if (targetIndex >= 0) {
            dropdown->blockSignals(true);
            dropdown->setCurrentIndex(targetIndex);
            dropdown->blockSignals(false);
          } else {
            // Value not found in dropdown - add it as an unknown value
            unknownValueManager.recordUnknownValue(importedSetting.id,
                                                   importedSetting.value);
            LOG_INFO << "[OptimizeView] Added unknown value for "
                      << importedSetting.id.toStdString() << ": "
                      << importedSetting.value.toString().toStdString()
                     ;
          }
        } else if (auto* toggle = qobject_cast<SettingsToggle*>(widget)) {
          bool boolValue = importedSetting.value.toBool();
          toggle->blockSignals(true);
          toggle->setEnabled(boolValue);
          toggle->blockSignals(false);
        }
      }
    }
  }

  // Save any unknown values that were added
  unknownValueManager.saveUnknownValues();

  LOG_INFO << "[OptimizeView] Applied imported settings to UI widgets"
           ;
}

QString OptimizeView::getProfilesDirectory() {
  // Use the profiles directory in the application root
  QString appDir = QCoreApplication::applicationDirPath();
  QString profilesDir = QDir(appDir).absoluteFilePath("profiles");

  // Create profiles directory if it doesn't exist
  QDir profilesDirObj(profilesDir);
  if (!profilesDirObj.exists()) {
    if (profilesDirObj.mkpath(".")) {
      LOG_INFO << "[OptimizeView] Created profiles directory: "
                << profilesDir.toStdString();
    } else {
      LOG_INFO << "[OptimizeView] Failed to create profiles directory: "
                << profilesDir.toStdString();
    }
  }

  return profilesDir;
}

void OptimizeView::onSaveAsProfile() {
  // Create and show the save profile dialog
  SaveProfileDialog dialog(this);

  // If user cancels, do nothing
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  // Get the dialog settings
  QString profileName = dialog.getProfileName();
  bool includeRust = dialog.includeRustSettings();
  bool includeAdvanced = dialog.includeAdvancedSettings();

  try {
    // Show saving status
    if (statusLabel) {
      setStatusText("Saving profile...");
      statusLabel->setStyleSheet("color: #4A90E2; font-weight: bold;");
      statusLabel->setVisible(true);
      QApplication::processEvents();
    }

    // Create profiles directory in application root
    QString appDir = QCoreApplication::applicationDirPath();
    QString profilesDir = QDir(appDir).absoluteFilePath("profiles");

    // Create the profiles directory if it doesn't exist
    QDir profilesDirObj(profilesDir);
    if (!profilesDirObj.exists()) {
      if (!profilesDirObj.mkpath(".")) {
        throw std::runtime_error("Failed to create profiles directory: " +
                                 profilesDir.toStdString());
      }
      LOG_INFO << "[OptimizeView] Created profiles directory: "
                << profilesDir.toStdString();
    }

    // Create the profile file path
    QString profileFileName = profileName + ".json";
    QString profileFilePath = profilesDirObj.absoluteFilePath(profileFileName);

    // Check if file already exists
    if (QFile::exists(profileFilePath)) {
      QMessageBox::StandardButton reply = QMessageBox::question(
        this, "File Exists",
        QString(
          "A profile named '%1' already exists. Do you want to overwrite it?")
          .arg(profileName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

      if (reply != QMessageBox::Yes) {
        if (statusLabel) {
          statusLabel->setVisible(false);
        }
        return;
      }
    }

    // Export settings using ExportSettings
    optimizations::ExportResult result =
      optimizations::ExportSettings::ExportAllSettings(
        profileFilePath.toStdString(),
        true  // include metadata
      );

    if (!result.success) {
      throw std::runtime_error("Export failed: " + result.error_message);
    }

    // TODO: In the future, we could filter the exported JSON based on
    // includeRust and includeAdvanced flags For now, we export everything and
    // note the user's preferences

    // Show success message
    if (statusLabel) {
      QString successMsg =
        QString("Profile '%1' saved successfully! (%2 settings exported)")
          .arg(profileName)
          .arg(result.exported_settings);

      setStatusText(successMsg);
      statusLabel->setStyleSheet("color: #32CD32; font-weight: bold;");

      // Hide success message after 3 seconds
      QTimer::singleShot(3000, [this]() {
        if (statusLabel) {
          statusLabel->setVisible(false);
        }
      });
    }

    // Refresh the profile dropdown to include the new profile
    refreshProfileList();

    LOG_INFO << "[OptimizeView] Successfully saved profile: "
              << profileFilePath.toStdString() << " ("
              << result.exported_settings << " settings)";

  } catch (const std::exception& e) {
    QString errorMsg = QString("Failed to save profile: %1").arg(e.what());
    LOG_INFO << "[OptimizeView] " << errorMsg.toStdString();

    if (statusLabel) {
      setStatusText(errorMsg);
      statusLabel->setStyleSheet("color: #FF6B6B; font-weight: bold;");

      // Hide error message after 5 seconds
      QTimer::singleShot(5000, [this]() {
        if (statusLabel) {
          statusLabel->setVisible(false);
        }
      });
    }

    // Also show a message box for critical errors
    QMessageBox::critical(this, "Save Profile Error", errorMsg);
  }
}
