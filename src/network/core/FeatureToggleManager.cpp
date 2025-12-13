#include "FeatureToggleManager.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "../../ApplicationSettings.h"
#include "NetworkConfig.h"
#include "logging/Logger.h"

FeatureToggleManager::FeatureToggleManager(QObject* parent)
    : QObject(parent) {}

void FeatureToggleManager::fetchAndApplyRemoteFlags() {
  try {
    if (ApplicationSettings::getInstance().isOfflineModeEnabled()) {
      LOG_WARN << "FeatureToggleManager: Offline Mode enabled, skipping remote flag fetch";
      ApplicationSettings::getInstance().setRemoteFeatureFlags(false, false, false);
      return;
    }

    auto& config = NetworkConfig::instance();
    QString baseUrl = config.getBaseUrl();
    if (baseUrl.isEmpty()) {
      LOG_WARN << "FeatureToggleManager: Base URL is empty, disabling remote feature flags";
      ApplicationSettings::getInstance().setRemoteFeatureFlags(false, false, false);
      return;
    }

    // Normalize base URL to ensure it has no trailing slash
    if (baseUrl.endsWith('/')) {
      baseUrl.chop(1);
    }

    QUrl url(baseUrl + "/api/app_config");
    if (!url.isValid()) {
      LOG_WARN << "FeatureToggleManager: Invalid config URL, disabling remote feature flags";
      ApplicationSettings::getInstance().setRemoteFeatureFlags(false, false, false);
      return;
    }

    LOG_INFO << "FeatureToggleManager: Fetching remote feature flags from "
             << url.toString().toStdString();

    QNetworkAccessManager networkManager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    QNetworkReply* reply = networkManager.get(request);

    QObject::connect(&networkManager, &QNetworkAccessManager::finished, &loop,
                     &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeoutTimer.start(3000);  // 3s timeout for config fetch
    loop.exec();

    if (timeoutTimer.isActive()) {
      timeoutTimer.stop();
    } else {
      // Timed out - abort request
      if (reply) {
        reply->abort();
      }
    }

    bool allowExperimental = false;
    bool allowUpload = false;
    bool initialized = false;

    if (reply && reply->error() == QNetworkReply::NoError) {
      QByteArray body = reply->readAll();
      QJsonParseError parseError{};
      QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
      if (parseError.error != QJsonParseError::NoError) {
        LOG_WARN << "FeatureToggleManager: Failed to parse app_config JSON: "
                 << parseError.errorString().toStdString();
      } else if (!doc.isObject()) {
        LOG_WARN << "FeatureToggleManager: app_config response is not a JSON object";
      } else {
        QJsonObject obj = doc.object();
        allowExperimental = obj.value("allow_experimental_features").toBool(false);
        // Support both "upload_data" and "allow_upload_data" keys for flexibility
        if (obj.contains("allow_upload_data")) {
          allowUpload = obj.value("allow_upload_data").toBool(false);
        } else {
          allowUpload = obj.value("upload_data").toBool(false);
        }
        initialized = true;
        LOG_INFO << "FeatureToggleManager: Remote flags - experimental="
                 << (allowExperimental ? "true" : "false")
                 << ", upload=" << (allowUpload ? "true" : "false");
      }
    } else {
      if (reply) {
        LOG_WARN << "FeatureToggleManager: Network error fetching app_config: "
                 << reply->errorString().toStdString();
      } else {
        LOG_WARN << "FeatureToggleManager: Network reply is null when fetching app_config";
      }
    }

    if (reply) {
      reply->deleteLater();
    }

    if (!initialized) {
      // Backend unreachable or invalid response -> treat as offline
      LOG_WARN << "FeatureToggleManager: Backend offline or invalid config, disabling remote feature flags";
      ApplicationSettings::getInstance().setRemoteFeatureFlags(false, false, false);
    } else {
      ApplicationSettings::getInstance().setRemoteFeatureFlags(allowExperimental,
                                                               allowUpload, true);
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "FeatureToggleManager: Exception while fetching remote flags: " << e.what();
    ApplicationSettings::getInstance().setRemoteFeatureFlags(false, false, false);
  } catch (...) {
    LOG_ERROR << "FeatureToggleManager: Unknown exception while fetching remote flags";
    ApplicationSettings::getInstance().setRemoteFeatureFlags(false, false, false);
  }
}
