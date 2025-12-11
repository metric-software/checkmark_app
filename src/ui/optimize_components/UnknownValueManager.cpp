/**
 * @file UnknownValueManager.cpp
 * @brief Implementation of the UnknownValueManager class
 */

#include "UnknownValueManager.h"

#include <iostream>

#include "../../optimization/BackupManager.h"
#include "../SettingsDropdown.h"

namespace optimize_components {

UnknownValueManager::UnknownValueManager() {
  // Constructor - initialize empty
}

void UnknownValueManager::addUnknownValueToDropdown(SettingsDropdown* dropdown,
                                                    const QVariant& value,
                                                    const QString& settingId) {
  if (!dropdown || !value.isValid()) {
    return;
  }

  // Normalize the value for consistency
  QVariant normalizedValue = value;

  // Convert string numbers to integers for consistency
  if (value.type() == QVariant::String) {
    bool isNumeric;
    int numValue = value.toString().toInt(&isNumeric);
    if (isNumeric) {
      normalizedValue = QVariant(numValue);
    }
  }

  // Check if this value already exists in the dropdown
  bool alreadyExists = false;
  for (int i = 0; i < dropdown->count(); ++i) {
    QVariant existingValue = dropdown->itemData(i);

    // Type-aware comparison
    if (normalizedValue == existingValue) {
      alreadyExists = true;
      break;
    }

    // Handle numeric comparisons
    if ((normalizedValue.type() == QVariant::Int ||
         normalizedValue.type() == QVariant::Double) &&
        (existingValue.type() == QVariant::Int ||
         existingValue.type() == QVariant::Double)) {
      double normalizedDouble = normalizedValue.toDouble();
      double existingDouble = existingValue.toDouble();
      if (qAbs(normalizedDouble - existingDouble) < 0.0001) {
        alreadyExists = true;
        break;
      }
    }
  }

  if (!alreadyExists) {
    // Create display text for the unknown value
    QString displayText;
    if (normalizedValue.type() == QVariant::Int) {
      displayText = QString::number(normalizedValue.toInt());
    } else if (normalizedValue.type() == QVariant::Double) {
      displayText = QString::number(normalizedValue.toDouble());
    } else if (normalizedValue.type() == QVariant::Bool) {
      displayText = normalizedValue.toBool() ? "Enabled" : "Disabled";
    } else if (normalizedValue.type() == QVariant::String) {
      QString strValue = normalizedValue.toString();
      displayText = strValue.isEmpty() ? "<Empty>" : strValue;
    } else {
      displayText = normalizedValue.toString();
    }

    // Add "(Custom)" suffix
    displayText += " (Custom)";

    // Add to dropdown
    dropdown->addItem(displayText, normalizedValue);

    // Record this unknown value
    recordUnknownValue(settingId, normalizedValue);
  }
}

void UnknownValueManager::saveUnknownValues() {
  // Use BackupManager to save unknown values
  optimizations::BackupManager& backupManager =
    optimizations::BackupManager::GetInstance();
  backupManager.SaveUnknownValues(unknownValues);
}

void UnknownValueManager::loadUnknownValues() {
  // Use BackupManager to load unknown values
  optimizations::BackupManager& backupManager =
    optimizations::BackupManager::GetInstance();
  backupManager.LoadUnknownValues(unknownValues);
}

void UnknownValueManager::forceSaveUnknownValues() {
  // Save current state immediately
  saveUnknownValues();
}

bool UnknownValueManager::hasUnknownValues(const QString& settingId) const {
  return unknownValues.contains(settingId) &&
         !unknownValues[settingId].isEmpty();
}

QList<QVariant> UnknownValueManager::getUnknownValues(
  const QString& settingId) const {
  if (unknownValues.contains(settingId)) {
    return unknownValues[settingId];
  }
  return QList<QVariant>();
}

const QMap<QString, QList<QVariant>>& UnknownValueManager::getAllUnknownValues()
  const {
  return unknownValues;
}

bool UnknownValueManager::recordUnknownValue(const QString& settingId,
                                             const QVariant& value) {
  if (!value.isValid()) {
    return false;
  }

  // Normalize the value
  QVariant normalizedValue = value;
  if (value.type() == QVariant::String) {
    bool isNumeric;
    int numValue = value.toString().toInt(&isNumeric);
    if (isNumeric) {
      normalizedValue = QVariant(numValue);
    }
  }

  // Check if this value is already recorded
  if (unknownValues.contains(settingId)) {
    for (const QVariant& existingValue : unknownValues[settingId]) {
      if (normalizedValue == existingValue) {
        return false;  // Already exists
      }

      // Handle numeric comparisons
      if ((normalizedValue.type() == QVariant::Int ||
           normalizedValue.type() == QVariant::Double) &&
          (existingValue.type() == QVariant::Int ||
           existingValue.type() == QVariant::Double)) {
        double normalizedDouble = normalizedValue.toDouble();
        double existingDouble = existingValue.toDouble();
        if (qAbs(normalizedDouble - existingDouble) < 0.0001) {
          return false;  // Already exists
        }
      }
    }
  }

  // Add the new unknown value
  unknownValues[settingId].append(normalizedValue);
  return true;
}

}  // namespace optimize_components
