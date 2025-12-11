#include "RegistryLogger.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include "../logging/Logger.h"

namespace fs = std::filesystem;

namespace optimizations {
namespace registry {

RegistryLogger& RegistryLogger::GetInstance() {
  static RegistryLogger instance;
  return instance;
}

RegistryLogger::~RegistryLogger() {
  if (log_file_.is_open()) {
    log_file_.close();
  }
}

void RegistryLogger::Initialize(const std::string& app_data_dir) {
  std::lock_guard<std::mutex> lock(log_mutex_);

  if (initialized_) {
    return;
  }

  try {
    // Ensure settings_backup directory exists
    std::string backup_dir = app_data_dir + "/settings_backup";
    fs::create_directories(backup_dir);

    // Set up log file path
    log_file_path_ = backup_dir + "/registry_log.txt";

    // Open log file in append mode
    log_file_.open(log_file_path_, std::ios::app);
    if (!log_file_.is_open()) {
      LOG_ERROR << "[Registry Logger] ERROR: Failed to open log file: "
                << log_file_path_;
      return;
    }

    // Write session start marker
    log_file_ << "\n"
              << GetTimestamp()
              << " [SESSION_START] Registry logging initialized";
    log_file_.flush();

    initialized_ = true;
    LOG_INFO << "[Registry Logger] Initialized successfully. Log file: "
              << log_file_path_;

  } catch (const std::exception& e) {
    LOG_ERROR << "[Registry Logger] ERROR: Failed to initialize: " << e.what()
             ;
  }
}

void RegistryLogger::LogKeyCreation(HKEY hive, const std::string& key_path,
                                    bool success, LONG error_code,
                                    const std::string& setting_id) {
  if (!initialized_) {
    return;
  }

  std::ostringstream log_entry;
  log_entry << GetTimestamp() << " [KEY_CREATE] ";

  if (!setting_id.empty()) {
    log_entry << "Setting: " << setting_id << " | ";
  }

  log_entry << GetHiveName(hive) << "\\" << key_path
            << " | Status: " << (success ? "SUCCESS" : "FAILED");

  if (!success && error_code != 0) {
    log_entry << " | Error: " << error_code;
  }

  WriteLogEntry(log_entry.str());
}

void RegistryLogger::LogValueModification(HKEY hive,
                                          const std::string& key_path,
                                          const std::string& value_name,
                                          const OptimizationValue& value,
                                          bool success, LONG error_code,
                                          const std::string& setting_id) {
  if (!initialized_) {
    return;
  }

  std::ostringstream log_entry;
  log_entry << GetTimestamp() << " [VALUE_SET] ";

  if (!setting_id.empty()) {
    log_entry << "Setting: " << setting_id << " | ";
  }

  log_entry << GetHiveName(hive) << "\\" << key_path << " | Value: \""
            << value_name << "\" = " << ValueToString(value)
            << " | Status: " << (success ? "SUCCESS" : "FAILED");

  if (!success && error_code != 0) {
    log_entry << " | Error: " << error_code;
  }

  WriteLogEntry(log_entry.str());
}

void RegistryLogger::LogValueDeletion(HKEY hive, const std::string& key_path,
                                      const std::string& value_name,
                                      bool success, LONG error_code,
                                      const std::string& setting_id) {
  if (!initialized_) {
    return;
  }

  std::ostringstream log_entry;
  log_entry << GetTimestamp() << " [VALUE_DELETE] ";

  if (!setting_id.empty()) {
    log_entry << "Setting: " << setting_id << " | ";
  }

  log_entry << GetHiveName(hive) << "\\" << key_path << " | Value: \""
            << value_name
            << "\" | Status: " << (success ? "SUCCESS" : "FAILED");

  if (!success && error_code != 0) {
    log_entry << " | Error: " << error_code;
  }

  WriteLogEntry(log_entry.str());
}

std::string RegistryLogger::GetTimestamp() const {
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              now.time_since_epoch()) %
            1000;

  std::ostringstream ss;
  ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
  ss << "." << std::setfill('0') << std::setw(3) << ms.count();

  return ss.str();
}

std::string RegistryLogger::ValueToString(
  const OptimizationValue& value) const {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  } else if (std::holds_alternative<int>(value)) {
    return std::to_string(std::get<int>(value));
  } else if (std::holds_alternative<double>(value)) {
    return std::to_string(std::get<double>(value));
  } else if (std::holds_alternative<std::string>(value)) {
    return "\"" + std::get<std::string>(value) + "\"";
  }
  return "UNKNOWN_TYPE";
}

std::string RegistryLogger::GetHiveName(HKEY hive) const {
  if (hive == HKEY_LOCAL_MACHINE) return "HKEY_LOCAL_MACHINE";
  if (hive == HKEY_CURRENT_USER) return "HKEY_CURRENT_USER";
  if (hive == HKEY_CLASSES_ROOT) return "HKEY_CLASSES_ROOT";
  if (hive == HKEY_USERS) return "HKEY_USERS";
  if (hive == HKEY_CURRENT_CONFIG) return "HKEY_CURRENT_CONFIG";
  return "UNKNOWN_HIVE";
}

void RegistryLogger::WriteLogEntry(const std::string& entry) {
  std::lock_guard<std::mutex> lock(log_mutex_);

  if (!log_file_.is_open()) {
    return;
  }

  log_file_ << entry;
  log_file_.flush();
}

}  // namespace registry
}  // namespace optimizations
