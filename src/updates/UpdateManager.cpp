#include "UpdateManager.h"
#include <iostream>
#include "../logging/Logger.h"
#include <QApplication>
#include <QDebug>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QUrl>
#include <winsparkle/winsparkle.h>
#include "checkmark_version.h"
#include "../network/core/NetworkConfig.h"

UpdateManager* UpdateManager::s_instance = nullptr;

UpdateManager& UpdateManager::getInstance() {
    if (!s_instance) {
        s_instance = new UpdateManager();
    }
    return *s_instance;
}

UpdateManager::UpdateManager(QObject* parent) 
    : QObject(parent), 
      m_checkTimer(new QTimer(this)),
      m_networkManager(new QNetworkAccessManager(this)) {
    
    m_checkTimer->setSingleShot(false);
    m_checkTimer->setInterval(3600000); // Check every hour
    connect(m_checkTimer, &QTimer::timeout, this, &UpdateManager::onUpdateCheck);
    
    // Set default values
    m_currentVersion = CHECKMARK_VERSION_STRING;
    // Prefer server base URL from NetworkConfig and append the canonical appcast path
    const QString baseUrl = NetworkConfig::instance().getBaseUrl();
    if (!baseUrl.isEmpty()) {
        // Ensure HTTPS for appcast
        QUrl bu(baseUrl);
        QString scheme = bu.scheme().trimmed().toLower();
        if (scheme.isEmpty() || scheme == QLatin1String("http")) scheme = QStringLiteral("https");
        QString url = QString("%1://%2").arg(scheme, bu.host());
        if (bu.port() != -1) url += ":" + QString::number(bu.port());
        m_appcastUrl = url + "/appcast.xml";
    } else {
        m_appcastUrl = "https://checkmark.gg/appcast.xml";
    }
}

UpdateManager::~UpdateManager() {
    if (m_initialized) {
        win_sparkle_cleanup();
    }
}

void UpdateManager::initialize() {
    if (m_initialized) return;
    
    LOG_INFO << "Initializing UpdateManager...";
    
    setupWinSparkle();
    m_initialized = true;
    QCoreApplication::setApplicationVersion(m_currentVersion);
    
    // Start periodic checks
    m_checkTimer->start();
    
    // Initial check (delayed to allow app to fully start)
    QTimer::singleShot(30000, this, &UpdateManager::checkForUpdates);
    
    LOG_INFO << "UpdateManager initialized successfully";
}

void UpdateManager::setupWinSparkle() {
    LOG_INFO << "Setting up WinSparkle...";
    
    // Set application details
    win_sparkle_set_appcast_url(m_appcastUrl.toUtf8().constData());
    // Convert UTF-8 QString version to wide string for WinSparkle
    const std::wstring verW = std::wstring(m_currentVersion.toStdWString());
    win_sparkle_set_app_details(L"Metric Software OY", L"Checkmark", verW.c_str());
    
    // Disable automatic updates - we'll handle checking manually
    win_sparkle_set_automatic_check_for_updates(0);
    
    // Set callbacks
    win_sparkle_set_can_shutdown_callback(UpdateManager::canShutdownCallback);
    win_sparkle_set_shutdown_request_callback(UpdateManager::shutdownRequestCallback);
    win_sparkle_set_did_find_update_callback(UpdateManager::updateFoundCallback);
    win_sparkle_set_did_not_find_update_callback(UpdateManager::updateNotFoundCallback);
    win_sparkle_set_error_callback(UpdateManager::updateErrorCallback);
    
    // Initialize WinSparkle
    win_sparkle_init();
    
    LOG_INFO << "WinSparkle initialized with appcast URL: " << m_appcastUrl.toStdString();
}

void UpdateManager::checkForUpdates() {
    if (!m_initialized) {
        LOG_WARN << "UpdateManager not initialized";
        return;
    }
    
    LOG_INFO << "Checking for updates...";
    
    // First check if server is reachable
    checkServerConnection();
}

void UpdateManager::checkServerConnection() {
    QUrl url(m_appcastUrl);
    QNetworkRequest request(url);
    QByteArray ua = QByteArray("checkmark/") + m_currentVersion.toUtf8();
    request.setRawHeader("User-Agent", ua);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    
    QNetworkReply* reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, &UpdateManager::onNetworkReply);
    
    // Set timeout for network request
    QTimer::singleShot(10000, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
}

void UpdateManager::onNetworkReply() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) return;
    
    reply->deleteLater();
    
    if (reply->error() != QNetworkReply::NoError) {
        LOG_WARN << "Server not reachable: " << reply->errorString().toStdString() << " - Defaulting to no update available";
        emit updateNotAvailable();
        return;
    }
    
    LOG_INFO << "Server is reachable, proceeding with WinSparkle update check";
    
    // Server is reachable, now use WinSparkle to check for updates
    win_sparkle_check_update_without_ui();
}

void UpdateManager::setAppcastURL(const QString& url) {
    m_appcastUrl = url;
    if (m_initialized) {
        win_sparkle_set_appcast_url(url.toUtf8().constData());
        LOG_INFO << "Updated appcast URL to: " << url.toStdString();
    }
}

void UpdateManager::setAppVersion(const QString& version) {
    m_currentVersion = version;
    LOG_INFO << "Set current version to: " << version.toStdString();
    if (m_initialized) {
        const std::wstring verW = std::wstring(m_currentVersion.toStdWString());
        win_sparkle_set_app_details(L"Metric Software OY", L"Checkmark", verW.c_str());
    }
}

bool UpdateManager::isUpdateAvailable() const {
    return m_updateAvailable;
}

void UpdateManager::showUpdateDialog() {
    if (!m_initialized) {
        LOG_WARN << "UpdateManager not initialized";
        return;
    }
    
    LOG_INFO << "Showing update dialog...";
    win_sparkle_check_update_with_ui();
}

void UpdateManager::onUpdateCheck() {
    checkForUpdates();
}

// Static callback functions for WinSparkle
void UpdateManager::updateFoundCallback() {
    LOG_INFO << "WinSparkle: Update found";
    if (s_instance) {
        s_instance->m_updateAvailable = true;
        emit s_instance->updateAvailable("Unknown version");
    }
}

void UpdateManager::updateNotFoundCallback() {
    LOG_INFO << "WinSparkle: No update found";
    if (s_instance) {
        s_instance->m_updateAvailable = false;
        emit s_instance->updateNotAvailable();
    }
}

void UpdateManager::updateErrorCallback() {
    LOG_ERROR << "WinSparkle: Update check error";
    if (s_instance) {
        s_instance->m_updateAvailable = false;
        emit s_instance->updateError("Update check failed");
    }
}

int UpdateManager::canShutdownCallback() {
    LOG_INFO << "WinSparkle: Can shutdown callback - allowing shutdown";
    return 1; // Allow shutdown for update
}

void UpdateManager::shutdownRequestCallback() {
    LOG_INFO << "WinSparkle: Shutdown requested for update";
    QApplication::quit();
}
