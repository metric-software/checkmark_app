/**
 * @file SettingsUIBuilder.cpp
 * @brief Implementation of the UI builder component for settings
 *
 * This file implements the SettingsUIBuilder class which is responsible for
 * constructing UI elements (groupboxes, toggles, dropdowns) from
 * SettingCategory data.
 *
 * Duplicate Settings Prevention:
 * The builder uses a static processedSettingIds map to track settings that have
 * already been displayed across all categories. This ensures each setting
 * appears only once in the UI regardless of whether it exists in multiple
 * categories. The tracking map is reset at the start of each top-level category
 * build to ensure consistent behavior across UI rebuilds.
 */

#include "SettingsUIBuilder.h"

#include <iostream>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "../OptimizeView.h"
#include "../SettingsDropdown.h"
#include "../SettingsToggle.h"
#include "optimization/BackupManager.h"
#include "optimization/OptimizationEntity.h"  //This file includes optimizationmanager
#include "optimization/Rust optimization/config_manager.h"

namespace optimize_components {

SettingsUIBuilder::SettingsUIBuilder(QWidget* parent) : parentWidget_(parent) {}

QGroupBox* SettingsUIBuilder::CreateCategoryGroup(
  const SettingCategory& category, int depth) {
  // Get showAdvanced status from parent
  bool showAdvanced =
    qobject_cast<OptimizeView*>(parentWidget_)->getShowAdvancedSettings();

  // Limit recursion depth to 3 levels
  if (depth > 3) {
    return nullptr;
  }

  // Skip completely empty categories
  if (category.settings.isEmpty() && category.subCategories.isEmpty()) {
    return nullptr;
  }

  // Check for valid settings in this category
  bool hasValidSettings = false;
  int validSettingsCount = 0;
  int disabledCount = 0;
  int advancedFilteredCount = 0;
  int dontEditFilteredCount = 0;
  int totalSettingsCount = category.settings.size();

  for (const auto& setting : category.settings) {
    if (setting.isDisabled && !setting.isMissing) {
      disabledCount++;
      continue;
    }

    if (setting.is_advanced && !showAdvanced) {
      advancedFilteredCount++;
      continue;
    }

    // Check dont_edit flag - for now, treat it as informational only
    if (setting.default_value.isValid()) {
      auto* opt =
        optimizations::OptimizationManager::GetInstance().FindOptimizationById(
          setting.id.toStdString());
      if (opt && opt->DontEdit()) {
        dontEditFilteredCount++;
      }
    }

    hasValidSettings = true;
    validSettingsCount++;
  }

  // Pre-check subcategories to see if any will be valid
  bool hasValidSubcategories = false;
  QList<QGroupBox*> validSubcategoryGroups;

  for (const auto& subCategory : category.subCategories) {
    QGroupBox* subGroup = CreateCategoryGroup(subCategory, depth + 1);
    if (subGroup) {
      validSubcategoryGroups.append(subGroup);
      hasValidSubcategories = true;
    }
  }

  // If we have no valid settings and no valid subcategories, skip this category
  if (!hasValidSettings && !hasValidSubcategories) {
    return nullptr;
  }

  // Reset tracking on first call (depth 0)
  if (depth == 0) {
    processedSettingIds_.clear();
  }

  // Create the group box with enhanced styling
  QGroupBox* group = new QGroupBox(category.name, parentWidget_);
  group->setObjectName("category_" + category.id);
  group->setProperty("categoryId", category.id);

  // Apply the initial title styling for consistent appearance
  group->setStyleSheet(R"(
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px;
            color: #ffffff;
            font-weight: bold;
            font-size: 14px;
        }
    )");

  // Create main layout for the group
  QVBoxLayout* layout = new QVBoxLayout(group);
  layout->setContentsMargins(10, 15, 10, 10);
  layout->setSpacing(8);

  // Determine whether this category should be collapsed by default
  bool shouldBeCollapsed = false;
  if (depth == 0) {
    // Get the CategoryMode from parent OptimizeView
    auto optView = qobject_cast<OptimizeView*>(parentWidget_);
    if (optView) {
      auto categoryModes = optView->property("categoryModes").toMap();
      QVariant modeVar = categoryModes.value(category.id);
      if (modeVar.isValid()) {
        int mode = modeVar.toInt();
        // Mode 0 = KeepOriginal, Mode 1 = Recommended, Mode 2 = Custom
        shouldBeCollapsed = (mode == 0 || mode == 1);
      } else {
        shouldBeCollapsed = category.isRecommendedMode;
      }
    } else {
      shouldBeCollapsed = category.isRecommendedMode;
    }
  }

  // Save to our tracking map
  collapsedCategories_[category.id] = shouldBeCollapsed;

  // Top-level header with category controls
  if (depth == 0) {
    // Create header widget for the category
    QWidget* categoryHeader = new QWidget(group);
    categoryHeader->setObjectName("categoryHeader_" + category.id);
    QVBoxLayout* headerLayout = new QVBoxLayout(categoryHeader);
    headerLayout->setContentsMargins(0, 0, 0, 10);
    headerLayout->setSpacing(10);

    // Top row container for description and button
    QWidget* topRow = new QWidget(categoryHeader);
    QHBoxLayout* topRowLayout = new QHBoxLayout(topRow);
    topRowLayout->setContentsMargins(0, 0, 0, 0);

    // Left side container
    QWidget* leftContainer = new QWidget(topRow);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);

    // Category description (always visible)
    if (!category.description.isEmpty()) {
      QLabel* descLabel = new QLabel(category.description, leftContainer);
      descLabel->setWordWrap(true);
      descLabel->setStyleSheet("color: #cccccc; font-size: 12px;");
      descLabel->setProperty("categoryId", category.id);
      leftLayout->addWidget(descLabel);
    }

    topRowLayout->addWidget(leftContainer, 1);

    // Right side (empty now as toggle button moved below)
    QWidget* rightContainer = new QWidget(topRow);
    QHBoxLayout* rightLayout = new QHBoxLayout(rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    topRowLayout->addWidget(rightContainer, 0,
                            Qt::AlignRight | Qt::AlignVCenter);

    // Add the top row to the header
    headerLayout->addWidget(topRow);

    // Add mode selector
    QWidget* modeContainer = new QWidget(categoryHeader);
    modeContainer->setObjectName("modeContainer_" + category.id);
    QHBoxLayout* modeLayout = new QHBoxLayout(modeContainer);
    modeLayout->setContentsMargins(0, 0, 0, 8);

    QLabel* modeLabel = new QLabel("Mode:", modeContainer);
    modeLabel->setStyleSheet("font-weight: bold; color: #ffffff;");
    modeLayout->addWidget(modeLabel);

    SettingsDropdown* modeDropdown = new SettingsDropdown(modeContainer);
    modeDropdown->setObjectName("mode_" + category.id);
    modeDropdown->addItems(
      QStringList({"Keep Original", "Recommended", "Custom"}),
      QList<QVariant>({0, 1, 2})  // Match CategoryMode enum values
    );

    // Set initial mode
    int initialMode = 2;  // Default to Custom
    if (category.isRecommendedMode) {
      initialMode = 1;  // Recommended
    } else {
      auto optView = qobject_cast<OptimizeView*>(parentWidget_);
      if (optView) {
        auto categoryModes = optView->property("categoryModes").toMap();
        QVariant modeVar = categoryModes.value(category.id);
        if (modeVar.isValid() && modeVar.toInt() == 0) {
          initialMode = 0;  // Keep Original
        }
      }
    }

    modeDropdown->blockSignals(true);
    modeDropdown->setCurrentIndex(initialMode);
    modeDropdown->blockSignals(false);
    modeDropdown->setProperty("isModeSelectorDropdown", true);

    // Connect dropdown to onCategoryModeChanged
    if (auto optView = qobject_cast<QObject*>(parentWidget_)) {
      QObject::connect(
        modeDropdown, QOverload<int>::of(&QComboBox::currentIndexChanged),
        optView, [optView, categoryId = category.id](int index) {
          QMetaObject::invokeMethod(
            optView, "onCategoryModeChanged", Qt::DirectConnection,
            Q_ARG(QString, categoryId), Q_ARG(int, index));
        });
    }

    modeLayout->addWidget(modeDropdown);
    modeLayout->addStretch();

    // Add the mode container to the header layout
    headerLayout->addWidget(modeContainer);

    // Create a container for the toggle button under the mode selector
    QWidget* toggleContainer = new QWidget(categoryHeader);
    toggleContainer->setObjectName("toggleContainer_" + category.id);
    QHBoxLayout* toggleLayout = new QHBoxLayout(toggleContainer);
    toggleLayout->setContentsMargins(0, 0, 0, 0);
    toggleLayout->setAlignment(Qt::AlignLeft);

    // Create the toggle button
    QPushButton* toggleButton =
      new QPushButton(shouldBeCollapsed ? "▼ Show Settings" : "▲ Hide Settings",
                      toggleContainer);
    toggleButton->setObjectName("toggle_" + category.id);
    toggleButton->setProperty("categoryId", category.id);
    toggleButton->setCursor(Qt::PointingHandCursor);
    toggleButton->setStyleSheet(R"(
            QPushButton {
                color: #999999;
                background-color: transparent;
                border: none;
                font-size: 12px;
                padding: 4px 0px;
                text-align: left;
            }
            QPushButton:hover {
                color: #ffffff;
                text-decoration: underline;
            }
        )");

    // Connect toggle button
    QObject::connect(toggleButton, &QPushButton::clicked,
                     [this, group, categoryId = category.id]() {
                       bool currentState =
                         collapsedCategories_.value(categoryId, false);
                       ApplyCollapsedStyle(group, categoryId, !currentState);
                     });

    toggleLayout->addWidget(toggleButton);
    headerLayout->addWidget(toggleContainer);

    // Add the completed header to the main layout
    layout->addWidget(categoryHeader);
  }

  // Create content container for settings
  QWidget* contentContainer = new QWidget(group);
  contentContainer->setObjectName("content_" + category.id);
  contentContainer->setProperty("categoryId", category.id);
  contentContainer->setProperty("collapsible", true);

  QVBoxLayout* contentLayout = new QVBoxLayout(contentContainer);
  contentLayout->setContentsMargins(0, 8, 0, 0);
  contentLayout->setSpacing(8);

  // Settings container inside the content container
  QWidget* settingsContainer = new QWidget(contentContainer);
  settingsContainer->setObjectName("settings_" + category.id);
  settingsContainer->setProperty("categoryId", category.id);

  QVBoxLayout* settingsLayout = new QVBoxLayout(settingsContainer);
  settingsLayout->setContentsMargins(0, 0, 0, 0);
  settingsLayout->setSpacing(2);

  // Add settings to the content container
  const int totalSettings = category.settings.size();
  int addedSettingsCount = 0;

  for (int i = 0; i < totalSettings; i++) {
    const auto& setting = category.settings[i];

    // Skip settings that have already been processed (duplicates)
    if (processedSettingIds_.contains(setting.id) &&
        processedSettingIds_[setting.id]) {
      continue;
    }

    // Skip disabled settings unless they're missing settings
    if (setting.isDisabled && !setting.isMissing) {
      continue;
    }

    // Skip advanced settings if not enabled
    if (setting.is_advanced && !showAdvanced) {
      continue;
    }

    // Mark this setting as processed
    processedSettingIds_[setting.id] = true;
    addedSettingsCount++;

    bool isLastSetting =
      (i == totalSettings - 1) || (addedSettingsCount == totalSettings);

    // Create the appropriate widget based on setting type
    QWidget* settingWidget = nullptr;

    if (setting.isMissing) {
      settingWidget = CreateMissingSettingWidget(setting, category.id);
    } else if (setting.type == SettingType::Toggle) {
      settingWidget = CreateToggleSettingWidget(setting, category.id);
    } else if (setting.type == SettingType::Dropdown) {
      settingWidget = CreateDropdownSettingWidget(setting, category.id);
    } else if (setting.type == SettingType::Button) {
      settingWidget = CreateButtonSettingWidget(setting, category.id);
    }

    if (settingWidget) {
      settingsLayout->addWidget(settingWidget);

      // Add separator if not the last setting
      if (!isLastSetting && addedSettingsCount < validSettingsCount) {
        QFrame* separator = new QFrame(settingsContainer);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Plain);
        separator->setStyleSheet(
          "background-color: #444444; min-height: 1px; max-height: 1px; "
          "margin-left: 30px; margin-right: 30px; border: 0;");
        settingsLayout->addWidget(separator);
      }
    }
  }

  // Add the pre-checked valid subcategories without separator lines
  for (QGroupBox* subGroup : validSubcategoryGroups) {
    settingsLayout->addWidget(subGroup);
  }

  contentLayout->addWidget(settingsContainer);

  // Add content container to the main layout
  layout->addWidget(contentContainer);

  // Store the category widget for later reference
  categoryWidgets_[category.id] = group;

  // Apply initial collapsed state
  if (depth == 0 && shouldBeCollapsed) {
    ApplyCollapsedStyle(group, category.id, true);
  }

  return group;
}

QWidget* SettingsUIBuilder::CreateMissingSettingWidget(
  const SettingDefinition& setting, const QString& categoryId) {
  QWidget* container = new QWidget(parentWidget_);
  container->setProperty("categoryId", categoryId);
  container->setFixedHeight(46);
  QHBoxLayout* containerLayout = new QHBoxLayout(container);
  containerLayout->setContentsMargins(0, 3, 0, 6);

  QHBoxLayout* mainLayout = new QHBoxLayout();
  mainLayout->setContentsMargins(0, 3, 0, 3);
  mainLayout->setSpacing(8);

  // Left side container for text content
  QWidget* leftSide = new QWidget(parentWidget_);
  QVBoxLayout* leftLayout = new QVBoxLayout(leftSide);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(3);

  // Create name label
  QLabel* nameLabel = new QLabel(setting.name, leftSide);
  QFont nameFont = nameLabel->font();
  nameFont.setBold(true);
  nameFont.setPointSizeF(nameFont.pointSizeF() * 0.95);
  nameLabel->setFont(nameFont);
  nameLabel->setStyleSheet(
    QString("color: %1;")
      .arg(GetSettingNameColor(setting.level, setting.is_advanced)));
  nameLabel->setProperty("categoryId", categoryId);
  leftLayout->addWidget(nameLabel);

  // Create tooltip content with description and missing info
  QString tooltipContent = "";
  if (!setting.description.isEmpty()) {
    tooltipContent +=
      "<p style='white-space:pre-wrap;'>" + setting.description + "</p>";
  }
  tooltipContent += "<p style='color: #ff9900;'><b>Status:</b> This setting "
                    "doesn't exist on your system.</p>";
  tooltipContent += "<p>Click the blue \"Add Setting\" button to create it if "
                    "you want to use this optimization.</p>";

  // Apply tooltip to the name label and container
  nameLabel->setToolTip(tooltipContent);
  container->setToolTip(tooltipContent);

  // Right side container with controls
  QWidget* rightSide = new QWidget(parentWidget_);
  rightSide->setContentsMargins(0, 4, 0, 0);
  QHBoxLayout* rightLayout = new QHBoxLayout(rightSide);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  // Create a grayed-out disabled dropdown
  auto dropdown = new SettingsDropdown(rightSide, 270);
  dropdown->setObjectName(setting.id);
  dropdown->setProperty("categoryId", categoryId);
  dropdown->setEnabled(false);

  // Add "Not Available" placeholder option
  QStringList options;
  QList<QVariant> values;
  options.append("Setting not available");
  values.append(QVariant());

  dropdown->addItems(options, values);
  dropdown->applyStyle(270);
  dropdown->setCurrentIndex(0);
  dropdown->setMissingSettingStyle(true);

  rightLayout->addWidget(dropdown);

  // Create the "Add Setting" button
  QPushButton* addButton =
    SettingsDropdown::createAddSettingButton(container, setting.id);
  addButton->setProperty("categoryId", categoryId);

  // Add components to layout
  mainLayout->addWidget(leftSide, 3);
  mainLayout->addWidget(addButton, 0, Qt::AlignCenter);
  mainLayout->addWidget(rightSide, 0, Qt::AlignRight | Qt::AlignVCenter);

  containerLayout->addLayout(mainLayout);

  // Connect the "Add Setting" button
  if (auto optView = qobject_cast<QObject*>(parentWidget_)) {
    QObject::connect(addButton, &QPushButton::clicked, optView,
                     [optView, settingId = setting.id]() {
                       QMetaObject::invokeMethod(optView, "onButtonClicked",
                                                 Qt::DirectConnection,
                                                 Q_ARG(QString, settingId));
                     });
  }

  settingsWidgets_[setting.id] = addButton;

  return container;
}

QWidget* SettingsUIBuilder::CreateToggleSettingWidget(
  const SettingDefinition& setting, const QString& categoryId) {
  QWidget* container = new QWidget(parentWidget_);
  container->setProperty("categoryId", categoryId);
  container->setFixedHeight(46);
  QHBoxLayout* containerLayout = new QHBoxLayout(container);
  containerLayout->setContentsMargins(0, 3, 0, 6);

  QHBoxLayout* mainLayout = new QHBoxLayout();
  mainLayout->setContentsMargins(0, 3, 0, 3);
  mainLayout->setSpacing(8);

  // Left side container for text content
  QWidget* leftSide = new QWidget(parentWidget_);
  QVBoxLayout* leftLayout = new QVBoxLayout(leftSide);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(3);

  // Create name label
  QLabel* nameLabel = new QLabel(setting.name, leftSide);
  QFont nameFont = nameLabel->font();
  nameFont.setBold(true);
  nameFont.setPointSizeF(nameFont.pointSizeF() * 0.95);
  nameLabel->setFont(nameFont);
  nameLabel->setStyleSheet(
    QString("color: %1;")
      .arg(GetSettingNameColor(setting.level, setting.is_advanced)));
  nameLabel->setProperty("categoryId", categoryId);
  leftLayout->addWidget(nameLabel);

  // Create a hidden options label for signal connections
  QLabel* optionsLabel = new QLabel(leftSide);
  optionsLabel->setVisible(false);
  optionsLabel->setProperty("categoryId", categoryId);

  // Create tooltip content
  QString tooltipContent = "";
  if (!setting.description.isEmpty()) {
    tooltipContent +=
      "<p style='white-space:pre-wrap;'>" + setting.description + "</p>";
  }

  // Format options for tooltip
  bool recommendedValue = setting.recommended_value.toBool();
  tooltipContent += "<p><b>Options:</b><br>";
  tooltipContent += QString("• <span style='%1'>Enabled</span>%2<br>")
                      .arg(recommendedValue ? "color: #0098ff;" : "")
                      .arg(recommendedValue ? " (Recommended)" : "");

  tooltipContent += QString("• <span style='%1'>Disabled</span>%2</p>")
                      .arg(!recommendedValue ? "color: #0098ff;" : "")
                      .arg(!recommendedValue ? " (Recommended)" : "");

  nameLabel->setToolTip(tooltipContent);
  container->setToolTip(tooltipContent);

  // Right side container with controls
  QWidget* rightSide = new QWidget(parentWidget_);
  rightSide->setContentsMargins(0, 4, 0, 0);
  QHBoxLayout* rightLayout = new QHBoxLayout(rightSide);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  // Create a dropdown instead of a toggle
  auto dropdown = new SettingsDropdown(rightSide, 270);
  dropdown->setObjectName(setting.id);
  dropdown->setProperty("categoryId", categoryId);

  // Add "Enabled" and "Disabled" options
  QStringList options;
  QList<QVariant> values;

  options.append("Enabled");
  values.append(true);

  options.append("Disabled");
  values.append(false);

  dropdown->addItems(options, values);
  dropdown->applyStyle(270);

  // Set initial index to -1 (no selection)
  dropdown->setCurrentIndex(-1);

  // Force a repaint to ensure UI is updated
  dropdown->update();

  rightLayout->addWidget(dropdown);

  // Add both sides to main layout
  mainLayout->addWidget(leftSide, 3);
  mainLayout->addWidget(rightSide, 0, Qt::AlignRight | Qt::AlignVCenter);

  containerLayout->addLayout(mainLayout);

  // Connect to the parent's onToggleChanged method
  if (auto optView = qobject_cast<QObject*>(parentWidget_)) {
    dropdown->setProperty("settingId", setting.id);

    QObject::connect(dropdown, &SettingsDropdown::valueChanged, optView,
                     [optView, settingId = setting.id](const QVariant& value) {
                       bool enabled = value.toBool();
                       QMetaObject::invokeMethod(
                         optView, "onToggleChanged", Qt::DirectConnection,
                         Q_ARG(QString, settingId), Q_ARG(bool, enabled));
                     });

    // Update the highlighted option in the list when selection changes
    QObject::connect(
      dropdown, QOverload<int>::of(&QComboBox::currentIndexChanged),
      [optionsLabel, setting, dropdown](int index) {
        bool enabled = (index == 0);
        bool recommendedValue = setting.recommended_value.toBool();

        // Format the options as a bullet point list with the selected one bold
        QString optionsText;

        QString enabledStyle =
          enabled ? (recommendedValue ? "color: #0098ff; font-weight: bold;"
                                      : "color: #ffffff; font-weight: bold;")
                  : (recommendedValue ? "color: #0098ff;" : "color: #999999;");

        QString disabledStyle =
          !enabled
            ? (!recommendedValue ? "color: #0098ff; font-weight: bold;"
                                 : "color: #ffffff; font-weight: bold;")
            : (!recommendedValue ? "color: #0098ff;" : "color: #999999;");

        optionsText += QString("• <span style='%1'>Enabled</span>%2<br>")
                         .arg(enabledStyle)
                         .arg(recommendedValue ? " (Recommended)" : "");

        optionsText += QString("• <span style='%1'>Disabled</span>%2")
                         .arg(disabledStyle)
                         .arg(!recommendedValue ? " (Recommended)" : "");

        optionsLabel->setText(optionsText);
      });
  }

  settingsWidgets_[setting.id] = dropdown;

  // Initialize the dropdown with its initial state
  dropdown->currentIndexChanged(dropdown->currentIndex());

  return container;
}

QWidget* SettingsUIBuilder::CreateDropdownSettingWidget(
  const SettingDefinition& setting, const QString& categoryId) {
  QWidget* container = new QWidget(parentWidget_);
  container->setProperty("categoryId", categoryId);
  container->setFixedHeight(46);
  QHBoxLayout* containerLayout = new QHBoxLayout(container);
  containerLayout->setContentsMargins(0, 3, 0, 6);

  QHBoxLayout* mainLayout = new QHBoxLayout();
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(8);

  // Left side container for text content
  QWidget* leftSide = new QWidget(parentWidget_);
  QVBoxLayout* leftLayout = new QVBoxLayout(leftSide);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(3);

  // Create name label
  QLabel* nameLabel = new QLabel(setting.name, leftSide);
  QFont nameFont = nameLabel->font();
  nameFont.setBold(true);
  nameFont.setPointSizeF(nameFont.pointSizeF() * 0.95);
  nameLabel->setFont(nameFont);
  nameLabel->setStyleSheet(
    QString("color: %1;")
      .arg(GetSettingNameColor(setting.level, setting.is_advanced)));
  nameLabel->setProperty("categoryId", categoryId);
  leftLayout->addWidget(nameLabel);

  // Create a hidden options label for signal connections
  QLabel* optionsLabel = new QLabel(leftSide);
  optionsLabel->setVisible(false);
  optionsLabel->setProperty("categoryId", categoryId);

  // Create tooltip content
  QString tooltipContent = "";
  if (!setting.description.isEmpty()) {
    tooltipContent +=
      "<p style='white-space:pre-wrap;'>" + setting.description + "</p>";
  }

  // Format options for tooltip
  tooltipContent += "<p><b>Options:</b><br>";
  for (const auto& option : setting.possible_values) {
    bool isRecommended = (option.value == setting.recommended_value);
    QString itemStyle = isRecommended ? "color: #0098ff;" : "";

    // Format the display based on the actual value type
    QString displayName = FormatOptionDisplay(option.value, setting.id);

    QString description = option.name;
    if (description.contains("(Recommended)")) {
      description =
        description.left(description.indexOf("(Recommended)")).trimmed();
    }

    // Format: "ActualValue (Description)" with recommendation marker
    QString displayText = displayName;
    if (!description.isEmpty() && description != displayName) {
      displayText += " (" + description + ")";
    }

    tooltipContent += QString("• <span style='%1'>%2</span>%3<br>")
                        .arg(itemStyle)
                        .arg(displayText)
                        .arg(isRecommended ? " (Recommended)" : "");
  }
  tooltipContent += "</p>";

  nameLabel->setToolTip(tooltipContent);
  container->setToolTip(tooltipContent);

  // Right side container with controls
  QWidget* rightSide = new QWidget(parentWidget_);
  rightSide->setContentsMargins(0, 4, 0, 0);
  QHBoxLayout* rightLayout = new QHBoxLayout(rightSide);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  // Create dropdown
  auto dropdown = new SettingsDropdown(rightSide, 270);
  dropdown->setObjectName(setting.id);
  dropdown->setProperty("categoryId", categoryId);

  // Build dropdown options
  if (!BuildDropdownOptions(dropdown, setting)) {
    delete container;
    return nullptr;  // Skip settings with no valid options
  }

  dropdown->applyStyle(270);
  dropdown->setCurrentIndex(-1);  // No initial selection
  dropdown->update();

  rightLayout->addWidget(dropdown);
  settingsWidgets_[setting.id] = dropdown;

  // Add both sides to main layout
  mainLayout->addWidget(leftSide, 3);
  mainLayout->addWidget(rightSide, 0, Qt::AlignRight | Qt::AlignVCenter);

  containerLayout->addLayout(mainLayout);

  // Connect to the parent's onDropdownChanged method
  if (auto optView = qobject_cast<QObject*>(parentWidget_)) {
    dropdown->setProperty("settingId", setting.id);

    QObject::connect(dropdown, &SettingsDropdown::valueChanged, optView,
                     [optView, settingId = setting.id](const QVariant& value) {
                       QMetaObject::invokeMethod(
                         optView, "onDropdownChanged", Qt::DirectConnection,
                         Q_ARG(QString, settingId), Q_ARG(QVariant, value));
                     });

    // Update the highlighted option in the list when selection changes
    QObject::connect(
      dropdown, QOverload<int>::of(&QComboBox::currentIndexChanged),
      [optionsLabel, setting, dropdown](int index) {
        // Format the options as a bullet point list with the selected one bold
        QString optionsText;
        for (int i = 0; i < dropdown->count(); i++) {
          QVariant itemValue = dropdown->itemData(i);
          bool isSelected = (i == index);
          bool isRecommended = (itemValue == setting.recommended_value);

          QString itemStyle;
          if (isSelected && isRecommended) {
            itemStyle = "color: #0098ff; font-weight: bold;";
          } else if (isSelected) {
            itemStyle = "color: #ffffff; font-weight: bold;";
          } else if (isRecommended) {
            itemStyle = "color: #0098ff;";
          } else {
            itemStyle = "color: #999999;";
          }

          QString displayName = dropdown->itemText(i);

          // Build tag text for display
          QList<SettingsDropdown::TagType> tags = dropdown->getItemTags(i);
          QString tagText;

          for (auto tag : tags) {
            if (!tagText.isEmpty()) tagText += " ";
            switch (tag) {
              case SettingsDropdown::TagType::Recommended:
                tagText += "(Recommended)";
                break;
              case SettingsDropdown::TagType::Original:
                tagText += "(Original)";
                break;
              default:
                break;
            }
          }

          optionsText += QString("• <span style='%1'>%2</span>%3<br>")
                           .arg(itemStyle)
                           .arg(displayName)
                           .arg(tagText.isEmpty() ? "" : " " + tagText);
        }

        optionsLabel->setText(optionsText);
      });
  }

  // Initialize the list with the current selection highlighted
  dropdown->currentIndexChanged(dropdown->currentIndex());

  return container;
}

QWidget* SettingsUIBuilder::CreateButtonSettingWidget(
  const SettingDefinition& setting, const QString& categoryId) {
  QWidget* container = new QWidget(parentWidget_);
  container->setProperty("categoryId", categoryId);
  container->setFixedHeight(46);
  QHBoxLayout* containerLayout = new QHBoxLayout(container);
  containerLayout->setContentsMargins(0, 3, 0, 6);

  QHBoxLayout* mainLayout = new QHBoxLayout();
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(8);

  // Left side container for text content
  QWidget* leftSide = new QWidget(parentWidget_);
  QVBoxLayout* leftLayout = new QVBoxLayout(leftSide);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(3);

  // Create name label
  QLabel* nameLabel = new QLabel(setting.name, leftSide);
  QFont nameFont = nameLabel->font();
  nameFont.setBold(true);
  nameFont.setPointSizeF(nameFont.pointSizeF() * 0.95);
  nameLabel->setFont(nameFont);
  nameLabel->setStyleSheet(
    QString("color: %1;")
      .arg(GetSettingNameColor(setting.level, setting.is_advanced)));
  nameLabel->setProperty("categoryId", categoryId);
  leftLayout->addWidget(nameLabel);

  // Create tooltip content
  QString tooltipContent = "";
  if (!setting.description.isEmpty()) {
    tooltipContent +=
      "<p style='white-space:pre-wrap;'>" + setting.description + "</p>";
  }

  nameLabel->setToolTip(tooltipContent);
  container->setToolTip(tooltipContent);

  // Right side container with controls
  QWidget* rightSide = new QWidget(parentWidget_);
  rightSide->setContentsMargins(0, 4, 0, 0);
  QHBoxLayout* rightLayout = new QHBoxLayout(rightSide);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  // Create button
  QPushButton* button = new QPushButton(setting.name, rightSide);
  button->setObjectName(setting.id);
  button->setProperty("categoryId", categoryId);
  button->setProperty("settingId", setting.id);
  button->setFixedWidth(180);

  if (setting.isDisabled) {
    button->setEnabled(false);
  }

  button->setStyleSheet(R"(
        QPushButton {
            background-color: #1e1e1e;
            color: #ffffff;
            border: none;
            padding: 5px 10px;
            border-radius: 2px;
            font-size: 12px;
            min-height: 28px;
        }
        QPushButton:hover {
            background-color: #333333;
        }
        QPushButton:pressed {
            background-color: #0078d4;
        }
        QPushButton:disabled {
            background-color: #1a1a1a;
            color: #666666;
        }
    )");

  rightLayout->addWidget(button);

  // Add both sides to main layout
  mainLayout->addWidget(leftSide, 3);
  mainLayout->addWidget(rightSide, 0, Qt::AlignRight | Qt::AlignVCenter);

  containerLayout->addLayout(mainLayout);

  // Connect to the parent's onButtonClicked method
  if (auto optView = qobject_cast<QObject*>(parentWidget_)) {
    QObject::connect(button, &QPushButton::clicked, optView,
                     [optView, settingId = setting.id]() {
                       QMetaObject::invokeMethod(optView, "onButtonClicked",
                                                 Qt::DirectConnection,
                                                 Q_ARG(QString, settingId));
                     });
  }

  settingsWidgets_[setting.id] = button;

  return container;
}

QString SettingsUIBuilder::FormatOptionDisplay(const QVariant& value,
                                               const QString& settingId) {
  QString displayName;

  if (value.type() == QVariant::Int) {
    displayName = QString::number(value.toInt());
  } else if (value.type() == QVariant::Double) {
    displayName = QString::number(value.toDouble());
  } else if (value.type() == QVariant::Bool) {
    // For Rust settings, show True/False instead of Enabled/Disabled
    if (settingId.startsWith("rust_")) {
      displayName = value.toBool() ? "True" : "False";
    } else {
      displayName = value.toBool() ? "Enabled" : "Disabled";
    }
  } else if (value.type() == QVariant::String) {
    QString strValue = value.toString();
    if (strValue.isEmpty()) {
      displayName = "<Empty>";
    } else {
      displayName = strValue;
    }
  } else {
    displayName = value.toString();
  }

  return displayName;
}

bool SettingsUIBuilder::BuildDropdownOptions(SettingsDropdown* dropdown,
                                             const SettingDefinition& setting) {
  // Create a map to track unique values and avoid duplicates
  QMap<QString, QPair<QVariant, QString>>
    uniqueValues;                 // stringKey -> (actualValue, displayName)
  QList<QVariant> orderedValues;  // to maintain order

  // Helper function to create a consistent string key from QVariant
  auto createValueKey = [](const QVariant& value) -> QString {
    if (value.type() == QVariant::Bool) {
      return QString("bool:") + (value.toBool() ? "true" : "false");
    } else if (value.type() == QVariant::Int) {
      return QString("num:") + QString::number(value.toInt());
    } else if (value.type() == QVariant::Double) {
      return QString("num:") + QString::number(value.toDouble());
    } else if (value.type() == QVariant::String) {
      QString strValue = value.toString();
      // Check if string represents a number
      bool isNumber;
      int intVal = strValue.toInt(&isNumber);
      if (isNumber) {
        return QString("num:") + QString::number(intVal);
      }
      // Check if string represents a boolean
      QString lowerStr = strValue.toLower();
      if (lowerStr == "true" || lowerStr == "false") {
        return QString("bool:") + lowerStr;
      }
      return QString("string:") + strValue.toLower();
    } else {
      return QString("other:") + value.toString().toLower();
    }
  };

  // Add items from possible_values, ensuring no duplicates
  for (const auto& option : setting.possible_values) {
    // Normalize the value first
    QVariant normalizedValue = option.value;

    // For Rust settings, ensure consistent type handling
    if (setting.id.startsWith("rust_")) {
      if (option.value.type() == QVariant::String) {
        QString strValue = option.value.toString();

        // Convert numeric strings to integers for consistency
        bool isNumeric;
        int numValue = strValue.toInt(&isNumeric);
        if (isNumeric) {
          normalizedValue = QVariant(numValue);
        }
        // For boolean strings, keep them as strings for consistent matching
        else if (strValue.toLower() == "true" ||
                 strValue.toLower() == "false") {
          normalizedValue =
            QVariant(strValue.toLower() == "true" ? "True" : "False");
        }
      }
    } else {
      // For non-Rust settings, convert string numbers to integers for
      // consistency
      if (option.value.type() == QVariant::String) {
        bool isNumeric;
        int numValue = option.value.toString().toInt(&isNumeric);
        if (isNumeric) {
          normalizedValue = QVariant(numValue);
        }
      }
    }

    // Show the actual value instead of the description
    QString displayName = FormatOptionDisplay(normalizedValue, setting.id);

    // Only add meaningful display names
    if (displayName.isEmpty() || displayName == "<Empty>") {
      continue;
    }

    // Create a consistent key for this value
    QString valueKey = createValueKey(normalizedValue);

    // If we haven't seen this actual value before, add it
    if (!uniqueValues.contains(valueKey)) {
      uniqueValues[valueKey] = qMakePair(normalizedValue, displayName);
      orderedValues.append(normalizedValue);
    }
  }

  // Add unknown values if they exist
  LoadUnknownValues(setting, uniqueValues, orderedValues, createValueKey);

  // Now create the dropdown items from our unique values
  QStringList options;
  QList<QVariant> values;

  for (const QVariant& value : orderedValues) {
    options.append(uniqueValues[createValueKey(value)].second);
    values.append(value);
  }

  // Skip settings that have no valid options
  if (options.isEmpty()) {
    return false;
  }

  dropdown->addItems(options, values);

  return true;
}

void SettingsUIBuilder::LoadUnknownValues(
  const SettingDefinition& setting,
  QMap<QString, QPair<QVariant, QString>>& uniqueValues,
  QList<QVariant>& orderedValues,
  std::function<QString(const QVariant&)> createValueKey) {
  // Load unknown values from backup and add them
  optimizations::BackupManager& unknownBackupManager =
    optimizations::BackupManager::GetInstance();
  QMap<QString, QList<QVariant>> allUnknownValues;

  if (unknownBackupManager.LoadUnknownValues(allUnknownValues)) {
    if (allUnknownValues.contains(setting.id)) {
      QList<QVariant> unknownValuesForSetting = allUnknownValues[setting.id];

      for (const QVariant& unknownValue : unknownValuesForSetting) {
        // Normalize the unknown value for consistent comparison
        QVariant normalizedUnknownValue = unknownValue;

        // For Rust settings, ensure consistent type handling
        if (setting.id.startsWith("rust_")) {
          if (unknownValue.type() == QVariant::String) {
            QString strValue = unknownValue.toString();

            // Convert numeric strings to integers for consistency
            bool isNumeric;
            int numValue = strValue.toInt(&isNumeric);
            if (isNumeric) {
              normalizedUnknownValue = QVariant(numValue);
            }
            // For boolean strings, keep them as strings
            else if (strValue.toLower() == "true" ||
                     strValue.toLower() == "false") {
              normalizedUnknownValue =
                QVariant(strValue.toLower() == "true" ? "True" : "False");
            }
          }
        } else {
          // For non-Rust settings, convert string numbers to integers
          if (unknownValue.type() == QVariant::String) {
            bool isNumeric;
            int numValue = unknownValue.toString().toInt(&isNumeric);
            if (isNumeric) {
              normalizedUnknownValue = QVariant(numValue);
            }
          }
        }

        // Create a consistent key for this unknown value
        QString unknownValueKey = createValueKey(normalizedUnknownValue);

        // Check if this unknown value is already in our unique values map
        if (!uniqueValues.contains(unknownValueKey)) {
          // Create clean display name
          QString displayName =
            FormatOptionDisplay(normalizedUnknownValue, setting.id);

          // Only add if display name is meaningful
          if (!displayName.isEmpty() && displayName != "<Empty>") {
            uniqueValues[unknownValueKey] =
              qMakePair(normalizedUnknownValue, displayName);
            orderedValues.append(normalizedUnknownValue);
          }
        }
      }
    }
  }
}

void SettingsUIBuilder::ApplyGreyedOutStyle(QWidget* widget,
                                            const QString& categoryId,
                                            bool isGreyedOut) {
  // Define a more distinct grayed-out stylesheet for disabled state
  QString disabledStylesheet = R"(
        QWidget[categoryDisabled="true"] {
            color: #555555;
            background-color: rgba(30, 30, 30, 0.3);
        }
        QLabel[categoryDisabled="true"] {
            color: #555555;
        }
        QGroupBox[categoryDisabled="true"] {
            color: #555555;
            background-color: rgba(20, 20, 20, 0.2);
            border: 1px solid #333333;
        }
        QGroupBox[categoryDisabled="true"]::title {
            color: #555555;
        }
    )";

  // Set the disabled property on the widget itself
  if (widget->property("categoryId").toString() == categoryId) {
    widget->setProperty("categoryDisabled", isGreyedOut);
    widget->setStyleSheet(disabledStylesheet);

    // Apply to special widget types
    if (auto* toggle = qobject_cast<SettingsToggle*>(widget)) {
      toggle->setDisabledStyle(isGreyedOut);
    }

    if (auto* dropdown = qobject_cast<SettingsDropdown*>(widget)) {
      // Skip if this is the mode dropdown for this category
      if (widget->objectName() != "mode_" + categoryId) {
        dropdown->setDisabledStyle(isGreyedOut);
      }
    }

    if (auto* groupBox = qobject_cast<QGroupBox*>(widget)) {
      // For group boxes, we need to ensure the title is also greyed
      if (isGreyedOut) {
        groupBox->setStyleSheet(
          "QGroupBox { color: #555555; background-color: rgba(20, 20, 20, "
          "0.2); border: 1px solid #333333; } QGroupBox::title { color: "
          "#555555; }");
      } else {
        groupBox->setStyleSheet("");
      }
    }

    // Set cursor to indicate disabled state
    widget->setCursor(isGreyedOut ? Qt::ForbiddenCursor : Qt::ArrowCursor);

    // Don't disable the mode dropdown itself
    QString objectName = widget->objectName();
    if (objectName == "mode_" + categoryId) {
      widget->setEnabled(true);
      return;
    }
  }

  // Apply to all child widgets recursively
  for (QObject* child : widget->children()) {
    if (QWidget* childWidget = qobject_cast<QWidget*>(child)) {
      // Check if this is a mode dropdown for the current category
      bool isModeSwitchForThisCategory =
        childWidget->objectName() == "mode_" + categoryId;

      // Skip mode dropdown components to keep them enabled
      if (isModeSwitchForThisCategory ||
          childWidget->parent()->objectName() == "mode_" + categoryId) {
        childWidget->setEnabled(true);
        continue;
      }

      // Apply the same greyed out style to the child widget
      ApplyGreyedOutStyle(childWidget, categoryId, isGreyedOut);

      // Also process sub-categories
      if (auto* groupBox = qobject_cast<QGroupBox*>(childWidget)) {
        if (groupBox->property("categoryId").toString() != categoryId) {
          ApplyGreyedOutStyle(
            groupBox, groupBox->property("categoryId").toString(), isGreyedOut);
        }
      }
    }
  }

  // Disable the widget if greyed out, but keep the container that has the mode
  // dropdown enabled
  if (widget->property("categoryId").toString() == categoryId) {
    // Don't disable the mode container widget
    if (QWidget* modeDropdown =
          widget->findChild<QWidget*>("mode_" + categoryId)) {
      if (widget == modeDropdown->parentWidget()->parentWidget()) {
        widget->setProperty("categoryDisabled", isGreyedOut);
        return;
      }
    }
    widget->setEnabled(!isGreyedOut);
  }
}

void SettingsUIBuilder::ApplyCollapsedStyle(QGroupBox* groupBox,
                                            const QString& categoryId,
                                            bool isCollapsed) {
  if (!groupBox) return;

  // Store collapsed state
  collapsedCategories_[categoryId] = isCollapsed;

  // Find toggle button and update its text
  QPushButton* toggleButton =
    groupBox->findChild<QPushButton*>("toggle_" + categoryId);
  if (toggleButton) {
    toggleButton->setText(isCollapsed ? "▼ Show Settings" : "▲ Hide Settings");
  }

  // Hide/show all widgets marked as collapsible
  QList<QWidget*> collapsibleWidgets = groupBox->findChildren<QWidget*>();
  for (QWidget* widget : collapsibleWidgets) {
    if (widget->property("collapsible").toBool()) {
      // Don't hide mode selector, toggle button or their containers
      if (widget->objectName() == "modeContainer_" + categoryId ||
          widget->objectName() == "mode_" + categoryId ||
          widget->objectName() == "toggleContainer_" + categoryId ||
          widget->objectName() == "toggle_" + categoryId) {
        continue;
      }
      widget->setVisible(!isCollapsed);
    }
  }

  // Apply same styling for both collapsed and expanded states
  groupBox->setStyleSheet(R"(
        QGroupBox {
            background-color: transparent;
            border: 1px solid #444444;
            border-radius: 5px;
            padding: 10px;
            margin-top: 1ex;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 5px;
            color: #ffffff;
            font-weight: bold;
            font-size: 14px;
        }
    )");
}

bool SettingsUIBuilder::ShouldHideEmptyCategory(const QString& categoryId) {
  QGroupBox* group = categoryWidgets_.value(categoryId);
  if (!group) return true;

  // Check if this category has any visible settings
  QWidget* contentContainer =
    group->findChild<QWidget*>("content_" + categoryId);
  if (!contentContainer) return true;

  // Count visible child widgets that are actual settings
  int visibleSettingsCount = 0;

  // Look for toggle and dropdown settings
  QList<SettingsToggle*> toggles =
    contentContainer->findChildren<SettingsToggle*>();
  QList<SettingsDropdown*> dropdowns =
    contentContainer->findChildren<SettingsDropdown*>();
  QList<QPushButton*> buttons = contentContainer->findChildren<QPushButton*>();

  for (auto* toggle : toggles) {
    if (toggle->property("categoryId").toString() != categoryId) continue;
    if (toggle->isVisible()) visibleSettingsCount++;
  }

  for (auto* dropdown : dropdowns) {
    if (dropdown->property("categoryId").toString() != categoryId ||
        dropdown->objectName().startsWith("mode_"))
      continue;
    if (dropdown->isVisible()) visibleSettingsCount++;
  }

  for (auto* button : buttons) {
    if (button->property("categoryId").toString() != categoryId ||
        button->objectName().startsWith("toggle_"))
      continue;
    if (button->isVisible()) visibleSettingsCount++;
  }

  // Count visible subcategories
  QList<QGroupBox*> subGroups = contentContainer->findChildren<QGroupBox*>();
  int visibleSubcategoriesCount = 0;

  for (auto* subGroup : subGroups) {
    if (subGroup->isVisible()) visibleSubcategoriesCount++;
  }

  return (visibleSettingsCount == 0 && visibleSubcategoriesCount == 0);
}

void SettingsUIBuilder::ApplyOriginalTag(SettingsDropdown* dropdown,
                                         const QString& settingId) {
  if (!dropdown || dropdown->count() == 0) {
    return;
  }

  try {
    QVariant originalValue;

    // Handle Rust settings specially
    if (settingId.startsWith("rust_")) {
      // For Rust settings, get original value from backup system
      optimizations::BackupManager& backupManager =
        optimizations::BackupManager::GetInstance();
      originalValue =
        backupManager.GetOriginalValueFromBackup(settingId.toStdString());

      // If not found in main backup, try to add it with current system value
      if (!originalValue.isValid() || originalValue.isNull()) {
        QString rustKey = settingId.mid(5);  // Remove "rust_" prefix

        // Try to get the current value from RustConfigManager
        auto& rustManager =
          optimizations::rust::RustConfigManager::GetInstance();
        QVariant currentSystemValue;

        try {
          const auto& allSettings = rustManager.GetAllSettings();
          auto it = allSettings.find(rustKey);
          if (it != allSettings.end()) {
            QString currentVal = it->second.currentValue;
            if (!currentVal.isEmpty() && currentVal != "missing") {
              currentSystemValue = QVariant(currentVal);
            }
          }
        } catch (...) {
          // Failed to get current value
        }

        // Only add to backup if we have a valid current system value
        if (currentSystemValue.isValid() &&
            !currentSystemValue.toString().isEmpty() &&
            currentSystemValue.toString() != "missing") {

          // Add this Rust setting to the main backup
          if (backupManager.AddMissingSettingToMainBackup(
                settingId.toStdString(), currentSystemValue)) {
            // Try to get the backup value again after adding it
            originalValue =
              backupManager.GetOriginalValueFromBackup(settingId.toStdString());
          }
        }

        // If still no backup value found, skip this setting
        if (!originalValue.isValid() || originalValue.isNull()) {
          return;
        }
      }
    } else {
      // Regular settings - get from backup manager
      optimizations::BackupManager& backupManager =
        optimizations::BackupManager::GetInstance();
      originalValue =
        backupManager.GetOriginalValueFromBackup(settingId.toStdString());

      // Handle missing backup values - add them to main backup if needed
      if (!originalValue.isValid() || originalValue.isNull()) {
        // Try to get the current system value
        QVariant currentSystemValue;
        try {
          auto& optManager = optimizations::OptimizationManager::GetInstance();
          auto* opt = optManager.FindOptimizationById(settingId.toStdString());
          if (opt) {
            auto rawValue = opt->GetCurrentValue();
            // Convert to QVariant
            if (std::holds_alternative<bool>(rawValue)) {
              currentSystemValue = std::get<bool>(rawValue);
            } else if (std::holds_alternative<int>(rawValue)) {
              currentSystemValue = std::get<int>(rawValue);
            } else if (std::holds_alternative<double>(rawValue)) {
              currentSystemValue = std::get<double>(rawValue);
            } else if (std::holds_alternative<std::string>(rawValue)) {
              std::string strValue = std::get<std::string>(rawValue);
              currentSystemValue = QString::fromStdString(strValue);
            }
          }
        } catch (...) {
          // Failed to get current value
        }

        // Only add to backup if we have a valid current system value
        if (currentSystemValue.isValid() &&
            !currentSystemValue.toString().isEmpty() &&
            currentSystemValue.toString() != "__KEY_NOT_FOUND__" &&
            currentSystemValue.toString() != "ERROR") {

          // Add this setting to the main backup with its current system value
          backupManager.AddMissingSettingToMainBackup(settingId.toStdString(),
                                                      currentSystemValue);

          // Try to get the backup value again after adding it
          originalValue =
            backupManager.GetOriginalValueFromBackup(settingId.toStdString());
        }
      }
    }

    if (!originalValue.isValid() || originalValue.isNull()) {
      return;
    }

    // Check all items in the dropdown for matches
    for (int i = 0; i < dropdown->count(); ++i) {
      try {
        QVariant itemData = dropdown->itemData(i);
        if (!itemData.isValid()) {
          continue;
        }

        // Enhanced comparison logic
        bool valuesMatch = CompareValues(originalValue, itemData, settingId);

        if (valuesMatch) {
          // Add the Original tag to this item
          QList<SettingsDropdown::TagType> tags = dropdown->getItemTags(i);

          if (!tags.contains(SettingsDropdown::TagType::Original)) {
            tags.append(SettingsDropdown::TagType::Original);
            dropdown->setItemTags(i, tags);
          }
        }
      } catch (const std::exception& e) {
        continue;
      }
    }

  } catch (const std::exception& e) {
  }
}

void SettingsUIBuilder::ApplyRecommendedTag(SettingsDropdown* dropdown,
                                            const QString& settingId) {
  if (!dropdown || dropdown->count() == 0) {
    return;
  }

  try {
    QVariant recommendedValue;

    // Handle Rust settings specially
    if (settingId.startsWith("rust_")) {
      // For Rust settings, get the recommended value from RustConfigManager
      QString rustKey = settingId.mid(5);  // Remove "rust_" prefix

      auto& rustManager = optimizations::rust::RustConfigManager::GetInstance();
      const auto& allSettings = rustManager.GetAllSettings();
      auto it = allSettings.find(rustKey);

      if (it != allSettings.end()) {
        QString optimalValue = it->second.optimalValue;
        recommendedValue =
          QVariant(optimalValue);  // Keep as string for consistency
      } else {
        return;
      }
    } else {
      // Regular settings - get from optimization entity
      auto& optManager = optimizations::OptimizationManager::GetInstance();
      auto* optimization =
        optManager.FindOptimizationById(settingId.toStdString());

      if (!optimization) {
        return;
      }

      optimizations::OptimizationValue recommendedOptValue =
        optimization->GetRecommendedValue();

      // Convert OptimizationValue to QVariant
      if (std::holds_alternative<bool>(recommendedOptValue)) {
        recommendedValue = std::get<bool>(recommendedOptValue);
      } else if (std::holds_alternative<int>(recommendedOptValue)) {
        recommendedValue = std::get<int>(recommendedOptValue);
      } else if (std::holds_alternative<double>(recommendedOptValue)) {
        recommendedValue = std::get<double>(recommendedOptValue);
      } else if (std::holds_alternative<std::string>(recommendedOptValue)) {
        recommendedValue =
          QString::fromStdString(std::get<std::string>(recommendedOptValue));
      }
    }

    if (!recommendedValue.isValid()) {
      return;
    }

    // Check all items in the dropdown for matches
    for (int i = 0; i < dropdown->count(); ++i) {
      try {
        QVariant itemData = dropdown->itemData(i);
        if (!itemData.isValid()) {
          continue;
        }

        // Compare values with enhanced type conversion
        bool valuesMatch = CompareValues(recommendedValue, itemData, settingId);

        if (valuesMatch) {
          // Add the Recommended tag to this item
          QList<SettingsDropdown::TagType> tags = dropdown->getItemTags(i);

          if (!tags.contains(SettingsDropdown::TagType::Recommended)) {
            tags.append(SettingsDropdown::TagType::Recommended);
            dropdown->setItemTags(i, tags);
          }
        }
      } catch (const std::exception& e) {
        continue;
      }
    }

  } catch (const std::exception& e) {
  }
}

bool SettingsUIBuilder::CompareValues(const QVariant& value1,
                                      const QVariant& value2,
                                      const QString& settingId) {
  // Direct comparison first
  if (value1 == value2) {
    return true;
  }

  // Type-specific comparisons for common cases
  if (value1.type() == QVariant::Int && value2.type() == QVariant::Int) {
    return value1.toInt() == value2.toInt();
  }

  if (value1.type() == QVariant::Bool && value2.type() == QVariant::Bool) {
    return value1.toBool() == value2.toBool();
  }

  // String-based comparison for mixed types
  if (value1.canConvert<QString>() && value2.canConvert<QString>()) {
    QString str1 = value1.toString();
    QString str2 = value2.toString();

    // Check numeric equivalence (e.g., "0" and 0, "1" and 1)
    bool ok1, ok2;
    int int1 = str1.toInt(&ok1);
    int int2 = str2.toInt(&ok2);
    if (ok1 && ok2) {
      return int1 == int2;
    }

    // Check boolean equivalence
    QString str1Lower = str1.toLower();
    QString str2Lower = str2.toLower();
    if ((str1Lower == "true" || str1Lower == "false") &&
        (str2Lower == "true" || str2Lower == "false")) {
      return str1Lower == str2Lower;
    }
  }

  return false;
}

QString SettingsUIBuilder::GetSettingNameColor(int level, bool isAdvanced) {
  // Priority order: experimental > advanced > optional > normal
  if (level == 2) {
    return "#ff6b6b";  // Red for experimental
  }
  if (isAdvanced) {
    return "#ffa500";  // Orange for advanced
  }
  if (level == 1) {
    return "#87ceeb";  // Light blue for optional
  }
  return "#ffffff";  // White for normal (level 0 or undefined)
}

void SettingsUIBuilder::ClearWidgetMaps() {
  settingsWidgets_.clear();
  categoryWidgets_.clear();
  processedSettingIds_.clear();
  collapsedCategories_.clear();
}

// Dialog styling methods for consistent appearance across all dialogs
void SettingsUIBuilder::ApplyDialogStyling(QDialog* dialog) {
  dialog->setStyleSheet("background-color: #1e1e1e; color: #ffffff;");
}

void SettingsUIBuilder::ApplyDialogTitleStyling(QLabel* titleLabel) {
  titleLabel->setStyleSheet(
    "font-size: 14px; font-weight: bold; color: #ffffff; margin-bottom: 10px;");
}

void SettingsUIBuilder::ApplyDialogCategoryStyling(QGroupBox* categoryBox) {
  categoryBox->setStyleSheet("QGroupBox { color: #ffffff; border: 1px solid "
                             "#444444; margin-top: 0.5em; } "
                             "QGroupBox::title { subcontrol-origin: margin; "
                             "left: 10px; padding: 0 5px; }");
}

void SettingsUIBuilder::ApplyDialogChangeStyling(QFrame* changeFrame) {
  changeFrame->setFrameShape(QFrame::StyledPanel);
  changeFrame->setStyleSheet(
    "background-color: #2d2d2d; border-radius: 4px; padding: 5px;");
}

void SettingsUIBuilder::ApplyDialogChangeNameStyling(QLabel* nameLabel) {
  nameLabel->setStyleSheet("font-weight: bold; color: #ffffff;");
}

void SettingsUIBuilder::ApplyDialogChangeValueStyling(QLabel* valueLabel) {
  valueLabel->setStyleSheet("color: #cccccc;");
}

void SettingsUIBuilder::ApplyDialogButtonStyling(QPushButton* button,
                                                 bool isPrimary) {
  if (isPrimary) {
    button->setStyleSheet(R"(
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
        )");
  } else {
    button->setStyleSheet(R"(
            QPushButton {
                background-color: #555555;
                color: white;
                border: none;
                padding: 8px 16px;
                border-radius: 4px;
            }
            QPushButton:hover {
                background-color: #666666;
            }
        )");
  }
}

}  // namespace optimize_components
