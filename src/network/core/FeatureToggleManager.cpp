#include "FeatureToggleManager.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
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
             << url.toString().toStdString()
             << " (baseUrl=" << baseUrl.toStdString()
             << ", insecureSsl="
             << (NetworkConfig::instance().getAllowInsecureSsl() ? "true" : "false")
             << ")";

    QNetworkAccessManager networkManager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    QNetworkReply* reply = networkManager.get(request);

    QObject::connect(reply, &QNetworkReply::sslErrors, this,
                     [reply](const QList<QSslError>& errors) {
                       if (!reply) return;
                       if (NetworkConfig::instance().getAllowInsecureSsl()) {
                         LOG_WARN << "FeatureToggleManager: SSL errors ignored due to "
                                     "CHECKMARK_ALLOW_INSECURE_SSL";
                         reply->ignoreSslErrors();
                       } else {
                         LOG_WARN << "FeatureToggleManager: SSL errors (not ignored): "
                                  << errors.size();
                       }
                     });

    QObject::connect(&networkManager, &QNetworkAccessManager::finished, &loop,
                     &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timeoutTimer.start(10000);  // 10s timeout for config fetch (allow for TLS/DNS)
    loop.exec();

    const bool timedOut = !timeoutTimer.isActive();
    if (timeoutTimer.isActive()) {
      timeoutTimer.stop();
    } else {
      // Timed out - abort request
      if (reply) {
        LOG_WARN << "FeatureToggleManager: Timeout fetching app_config; aborting request";
        reply->abort();
      }
    }

    bool allowExperimental = false;
    bool allowUpload = false;
    bool initialized = false;

    if (reply && !timedOut && reply->error() == QNetworkReply::NoError) {
      const int statusCode =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
      QByteArray body = reply->readAll();
      LOG_INFO << "FeatureToggleManager: app_config HTTP status=" << statusCode
               << " bytes=" << body.size();

      if (statusCode < 200 || statusCode >= 300) {
        LOG_WARN << "FeatureToggleManager: app_config returned HTTP "
                 << statusCode << ", treating as invalid response";
      } else {
      QJsonParseError parseError{};
      QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
      if (parseError.error != QJsonParseError::NoError) {
        LOG_WARN << "FeatureToggleManager: Failed to parse app_config JSON: "
                 << parseError.errorString().toStdString()
                 << " body_prefix="
                 << QString::fromUtf8(body.left(200)).toStdString();
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
      }
    } else {
      if (reply) {
        LOG_WARN << "FeatureToggleManager: Network error fetching app_config: "
                 << reply->errorString().toStdString()
                 << " (code=" << static_cast<int>(reply->error()) << ")";
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
