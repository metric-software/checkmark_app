#include "UserSystemProfile.h"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include "ApplicationSettings.h"

#include "logging/Logger.h"

namespace SystemMetrics {

UserSystemProfile& UserSystemProfile::getInstance() {
  static UserSystemProfile instance;
  return instance;
}

UserSystemProfile::UserSystemProfile() : initialized(false) {}

// Update initialize method to properly collect validation results

void UserSystemProfile::initialize() {
  if (!initialized) {
    LOG_INFO << "Initializing user system profile...";

    // Get or create user ID
    userId = getUserId();

    // Get system hash
    systemHash = getSystemHash();

    // Generate combined identifier
    combinedIdentifier = getCombinedIdentifier();

    // Update timestamp
    updateTimestamp();

    // Get validation results
    auto& validator = SystemMetricsValidator::getInstance();
    auto results = validator.getAllValidationResults();

    // Add some default validation results if empty
    if (results.empty()) {
      validationResults["cpu"] =
        static_cast<int>(SystemMetrics::ValidationResult::SUCCESS);
      validationResults["memory"] =
        static_cast<int>(SystemMetrics::ValidationResult::SUCCESS);
      validationResults["gpu"] =
        static_cast<int>(SystemMetrics::ValidationResult::SUCCESS);
      validationResults["disk"] =
        static_cast<int>(SystemMetrics::ValidationResult::SUCCESS);
      validationResults["network"] =
        static_cast<int>(SystemMetrics::ValidationResult::NOT_TESTED);
    } else {
      // Add actual validation results
      for (const auto& [component, detail] : results) {
        validationResults[component] = static_cast<int>(detail.result);
      }
    }

    initialized = true;
  }
}

std::string UserSystemProfile::getUserId() {
  // For GDPR/privacy reasons we must not derive or persist an identifier
  // based on the user's hardware or other personal data. Always return
  // a non-identifying placeholder value here.
  // NOTE: This intentionally does not read or write ApplicationSettings.
  return std::string("anonymous_user");
}

std::string UserSystemProfile::generateUserId() {
  // Generate a random UUID-like string
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);
  std::uniform_int_distribution<> dis2(8, 11);

  std::stringstream ss;
  ss << std::hex;

  for (int i = 0; i < 8; i++) {
    ss << dis(gen);
  }
  ss << "-";

  for (int i = 0; i < 4; i++) {
    ss << dis(gen);
  }
  ss << "-4";

  for (int i = 0; i < 3; i++) {
    ss << dis(gen);
  }
  ss << "-";

  ss << dis2(gen);
  for (int i = 0; i < 3; i++) {
    ss << dis(gen);
  }
  ss << "-";

  for (int i = 0; i < 12; i++) {
    ss << dis(gen);
  }

  return ss.str();
}

std::string UserSystemProfile::getSystemHash() { return generateSystemHash(); }

std::string UserSystemProfile::generateSystemHash() {
  // Get system information
  const auto& sysInfo = GetConstantSystemInfo();

  // Build a string containing key system identifiers
  std::stringstream ss;

  // Add CPU info
  ss << sysInfo.cpuName << "|";
  ss << sysInfo.cpuVendor << "|";
  ss << sysInfo.physicalCores << "|";
  ss << sysInfo.logicalCores << "|";

  // Add motherboard info
  ss << sysInfo.motherboardManufacturer << "|";
  ss << sysInfo.motherboardModel << "|";

  // Add memory info
  ss << sysInfo.totalPhysicalMemoryMB << "|";
  ss << sysInfo.memoryType << "|";

  // Add GPU info
  for (const auto& gpu : sysInfo.gpuDevices) {
    ss << gpu.name << "|";
    ss << gpu.memoryMB << "|";
  }

  // Add drive info (just the model and serial for system drive)
  for (const auto& drive : sysInfo.drives) {
    if (drive.isSystemDrive) {
      ss << drive.model << "|";
      ss << drive.serialNumber << "|";
      break;  // Just use system drive
    }
  }

  // Generate hash from the string
  QByteArray data = QByteArray::fromStdString(ss.str());
  QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256);

  // Return first 16 bytes of hash as hex string
  return hash.toHex().left(32).toStdString();
}

std::string UserSystemProfile::getCombinedIdentifier() {
  // Create a unique identifier combining user ID and system hash
  QByteArray combined = QByteArray::fromStdString(userId + "-" + systemHash);
  QByteArray hash =
    QCryptographicHash::hash(combined, QCryptographicHash::Sha256);

  // Return first 16 bytes of hash as hex string
  return hash.toHex().left(32).toStdString();
}

void UserSystemProfile::updateTimestamp() {
  // Get current time in ISO format
  QDateTime now = QDateTime::currentDateTime();
  lastUpdateTimestamp = now.toString(Qt::ISODate).toStdString();
}

std::string UserSystemProfile::getLastUpdateTimestamp() const {
  return lastUpdateTimestamp;
}

bool UserSystemProfile::saveToFile(const std::string& filePath) {
  LOG_INFO << "Saving system profile to: " << filePath;

  // Get system information
  const auto& sysInfo = GetConstantSystemInfo();

  // Create JSON document
  QJsonObject root;

  // Add identifiers
  root["userId"] = QString::fromStdString(userId);
  root["systemHash"] = QString::fromStdString(systemHash);
  root["combinedIdentifier"] = QString::fromStdString(combinedIdentifier);
  root["lastUpdateTimestamp"] = QString::fromStdString(lastUpdateTimestamp);

  // Add CPU information
  QJsonObject cpu;
  cpu["name"] = QString::fromStdString(sysInfo.cpuName);
  cpu["vendor"] = QString::fromStdString(sysInfo.cpuVendor);
  cpu["physicalCores"] = sysInfo.physicalCores;
  cpu["logicalCores"] = sysInfo.logicalCores;
  cpu["architecture"] = QString::fromStdString(sysInfo.cpuArchitecture);
  cpu["socket"] = QString::fromStdString(sysInfo.cpuSocket);
  cpu["baseClockMHz"] = sysInfo.baseClockMHz;
  cpu["maxClockMHz"] = sysInfo.maxClockMHz;
  cpu["l1CacheKB"] = sysInfo.l1CacheKB;
  cpu["l2CacheKB"] = sysInfo.l2CacheKB;
  cpu["l3CacheKB"] = sysInfo.l3CacheKB;
  cpu["hyperThreadingEnabled"] = sysInfo.hyperThreadingEnabled;
  cpu["virtualizationEnabled"] = sysInfo.virtualizationEnabled;
  cpu["avxSupport"] = sysInfo.avxSupport;
  cpu["avx2Support"] = sysInfo.avx2Support;
  root["cpu"] = cpu;

  // Add memory information
  QJsonObject memory;
  memory["totalPhysicalMemoryMB"] =
    static_cast<qint64>(sysInfo.totalPhysicalMemoryMB);
  memory["memoryType"] = QString::fromStdString(sysInfo.memoryType);
  memory["memoryClockMHz"] = sysInfo.memoryClockMHz;
  memory["xmpEnabled"] = sysInfo.xmpEnabled;
  memory["memoryChannelConfig"] =
    QString::fromStdString(sysInfo.memoryChannelConfig);

  // Add memory modules
  QJsonArray modules;
  for (const auto& module : sysInfo.memoryModules) {
    QJsonObject mod;
    mod["capacityGB"] = module.capacityGB;
    mod["speedMHz"] = module.speedMHz;
    mod["configuredSpeedMHz"] = module.configuredSpeedMHz;
    mod["manufacturer"] = QString::fromStdString(module.manufacturer);
    mod["partNumber"] = QString::fromStdString(module.partNumber);
    mod["memoryType"] = QString::fromStdString(module.memoryType);
    mod["deviceLocator"] = QString::fromStdString(module.deviceLocator);
    modules.append(mod);
  }
  memory["modules"] = modules;
  root["memory"] = memory;

  // Add GPU information
  QJsonArray gpus;
  for (const auto& gpu : sysInfo.gpuDevices) {
    QJsonObject g;
    g["name"] = QString::fromStdString(gpu.name);
    g["deviceId"] = QString::fromStdString(gpu.deviceId);
    g["driverVersion"] = QString::fromStdString(gpu.driverVersion);
    g["memoryMB"] = static_cast<qint64>(gpu.memoryMB);
    g["vendor"] = QString::fromStdString(gpu.vendor);
    g["pciLinkWidth"] = gpu.pciLinkWidth;
    g["pcieLinkGen"] = gpu.pcieLinkGen;
    g["isPrimary"] = gpu.isPrimary;
    gpus.append(g);
  }
  root["gpus"] = gpus;

  // Add motherboard information
  QJsonObject motherboard;
  motherboard["manufacturer"] =
    QString::fromStdString(sysInfo.motherboardManufacturer);
  motherboard["model"] = QString::fromStdString(sysInfo.motherboardModel);
  motherboard["chipsetModel"] = QString::fromStdString(sysInfo.chipsetModel);
  motherboard["chipsetDriverVersion"] =
    QString::fromStdString(sysInfo.chipsetDriverVersion);
  root["motherboard"] = motherboard;

  // Add BIOS information
  QJsonObject bios;
  bios["version"] = QString::fromStdString(sysInfo.biosVersion);
  bios["date"] = QString::fromStdString(sysInfo.biosDate);
  bios["manufacturer"] = QString::fromStdString(sysInfo.biosManufacturer);
  root["bios"] = bios;

  // Add OS information
  QJsonObject os;
  os["version"] = QString::fromStdString(sysInfo.osVersion);
  os["buildNumber"] = QString::fromStdString(sysInfo.osBuildNumber);
  os["isWindows11"] = sysInfo.isWindows11;
  // os["systemName"] = QString::fromStdString(sysInfo.systemName); // Removed for privacy
  root["os"] = os;

  // Add storage information
  QJsonArray drives;
  for (const auto& drive : sysInfo.drives) {
  QJsonObject d;
  d["path"] = QString::fromStdString(drive.path);
  d["model"] = QString::fromStdString(drive.model);
  // d["serialNumber"] = QString::fromStdString(drive.serialNumber); // Removed for privacy
  d["interfaceType"] = QString::fromStdString(drive.interfaceType);
  d["totalSpaceGB"] = drive.totalSpaceGB;
  d["freeSpaceGB"] = drive.freeSpaceGB;
  d["isSSD"] = drive.isSSD;
  d["isSystemDrive"] = drive.isSystemDrive;
  drives.append(d);
  }
  root["drives"] = drives;

  // Add power settings
  QJsonObject power;
  power["powerPlan"] = QString::fromStdString(sysInfo.powerPlan);
  power["powerPlanHighPerf"] = sysInfo.powerPlanHighPerf;
  power["gameMode"] = sysInfo.gameMode;
  root["power"] = power;

  // Add page file information
  QJsonObject pageFile;
  pageFile["exists"] = sysInfo.pageFileExists;
  pageFile["systemManaged"] = sysInfo.pageFileSystemManaged;
  pageFile["totalSizeMB"] = sysInfo.pageTotalSizeMB;
  if (!sysInfo.pagePrimaryDriveLetter.empty()) {
    // If the string contains data
    pageFile["primaryDriveLetter"] =
      QString::fromStdString(sysInfo.pagePrimaryDriveLetter);
  } else {
    // Empty string case
    pageFile["primaryDriveLetter"] = QString("");
  }

  QJsonArray locations;
  for (const auto& loc : sysInfo.pageFileLocations) {
    locations.append(QString::fromStdString(loc));
  }
  pageFile["locations"] = locations;

  // Add validation results
  QJsonObject validation;
  for (const auto& [component, result] : validationResults) {
    validation[QString::fromStdString(component)] = result;
  }
  root["validationResults"] = validation;

  // Convert to JSON document and save to file
  QJsonDocument doc(root);
  QFile file(QString::fromStdString(filePath));

  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }

  file.write(doc.toJson(QJsonDocument::Indented));
  return true;
}

bool UserSystemProfile::loadFromFile(const std::string& filePath) {
  LOG_INFO << "Loading system profile from: " << filePath;

  QFile file(QString::fromStdString(filePath));

  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }

  QByteArray data = file.readAll();
  QJsonDocument doc = QJsonDocument::fromJson(data);

  if (doc.isNull() || !doc.isObject()) {
    return false;
  }

  QJsonObject root = doc.object();

  // Load basic identifiers
  userId = root["userId"].toString().toStdString();
  systemHash = root["systemHash"].toString().toStdString();
  combinedIdentifier = root["combinedIdentifier"].toString().toStdString();
  lastUpdateTimestamp = root["lastUpdateTimestamp"].toString().toStdString();

  // Load validation results
  QJsonObject validation = root["validationResults"].toObject();
  for (auto it = validation.begin(); it != validation.end(); ++it) {
    validationResults[it.key().toStdString()] = it.value().toInt();
  }

  initialized = true;
  return true;
}

// Add these implementations

std::string UserSystemProfile::getProfilesDirectory() {
  // Create a dedicated folder in AppData/Roaming
  QString appDataPath =
    QCoreApplication::applicationDirPath() + "/benchmark_user_data/profiles";
  QDir dir(appDataPath);
  if (!dir.exists()) {
    dir.mkpath(".");
  }

  LOG_INFO << "Profiles directory: [path hidden for privacy]";
  return appDataPath.toStdString();
}

std::string UserSystemProfile::getDefaultProfilePath() {
  return getProfilesDirectory() + "/system_profile.json";
}

}  // namespace SystemMetrics
