#pragma once

#include <map>
#include <string>

#include "hardware/ConstantSystemInfo.h"
#include "hardware/SystemMetricsValidator.h"

namespace SystemMetrics {

class UserSystemProfile {
 public:
  // Get singleton instance
  static UserSystemProfile& getInstance();

  // Initialize profile (collect all data)
  void initialize();

  // Check if profile is initialized
  bool isInitialized() const { return initialized; }

  // Get or create user ID
  std::string getUserId();

  // Get system hash (based on hardware)
  std::string getSystemHash();

  // Get combined user+system identifier
  std::string getCombinedIdentifier();

  // Save profile to file
  bool saveToFile(const std::string& filePath);

  // Load profile from file
  bool loadFromFile(const std::string& filePath);

  // Get last update timestamp
  std::string getLastUpdateTimestamp() const;

  // Get path to profiles directory
  static std::string getProfilesDirectory();

  // Get default profile file path
  static std::string getDefaultProfilePath();

 private:
  UserSystemProfile();
  ~UserSystemProfile() = default;

  // Prevent copying
  UserSystemProfile(const UserSystemProfile&) = delete;
  UserSystemProfile& operator=(const UserSystemProfile&) = delete;

  // Generate system hash from hardware info
  std::string generateSystemHash();

  // Generate user ID if not exists
  std::string generateUserId();

  // Update timestamp
  void updateTimestamp();

  // Member variables
  std::string userId;
  std::string systemHash;
  std::string combinedIdentifier;
  std::string lastUpdateTimestamp;

  // System validation results (0=NOT_TESTED, 1=FAILED, 2=PARTIAL, 3=SUCCESS)
  std::map<std::string, int> validationResults;

  // Flag to track if profile is initialized
  bool initialized = false;
};

}  // namespace SystemMetrics
