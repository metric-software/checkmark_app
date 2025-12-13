#include "UpdateManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>
#include <QXmlStreamReader>

#include "../ApplicationSettings.h"
#include "../logging/Logger.h"
#include "../network/core/NetworkConfig.h"
#include "checkmark_version.h"

namespace {
// Production appcast feed hosted on Cloudflare R2
constexpr auto kDefaultAppcastUrl = "https://downloads.checkmark.gg/appcast.xml";

bool isProductionHost(const QString& host) {
    const QString trimmed = host.trimmed().toLower();
    return trimmed == QLatin1String("checkmark.gg") ||
           trimmed == QLatin1String("www.checkmark.gg") ||
           trimmed == QLatin1String("downloads.checkmark.gg");
}

QString tierToString(UpdateTier tier) {
    switch (tier) {
        case UpdateTier::UpToDate: return QStringLiteral("up-to-date");
        case UpdateTier::Suggestion: return QStringLiteral("suggested");
        case UpdateTier::Critical: return QStringLiteral("critical");
        case UpdateTier::Unknown:
        default: return QStringLiteral("unknown");
    }
}
}

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
    qRegisterMetaType<UpdateStatus>("UpdateStatus");

    m_checkTimer->setSingleShot(false);
    m_checkTimer->setInterval(3600000); // hourly background checks
    connect(m_checkTimer, &QTimer::timeout, this, &UpdateManager::onUpdateCheckTimer);

    m_currentVersion = CHECKMARK_VERSION_STRING;
    m_lastStatus.currentVersion = m_currentVersion;
    m_lastStatus.statusMessage = QStringLiteral("Not checked yet");
    m_appcastUrl = resolvedAppcastUrl();
}

UpdateManager::~UpdateManager() {
    cancelDownload();
}

QString UpdateManager::resolvedAppcastUrl() const {
    const QString baseUrl = NetworkConfig::instance().getBaseUrl();
    const QUrl bu(baseUrl);
    const QString host = bu.host();
    if (!host.isEmpty() && !isProductionHost(host)) {
        QString scheme = bu.scheme().trimmed().toLower();
        if (scheme.isEmpty() || scheme == QLatin1String("http")) scheme = QStringLiteral("https");
        QString url = QString("%1://%2").arg(scheme, bu.host());
        if (bu.port() != -1) url += ":" + QString::number(bu.port());
        return url + "/appcast.xml";
    }
    return QString::fromUtf8(kDefaultAppcastUrl);
}

void UpdateManager::initialize() {
    if (m_initialized) return;

    LOG_WARN << "UpdateManager: initializing";

    m_initialized = true;
    QCoreApplication::setApplicationVersion(m_currentVersion);
    m_checkTimer->start();

    // Initial check shortly after startup to keep UI responsive
    QTimer::singleShot(3000, this, [this]() { checkForUpdates(false); });

    LOG_WARN << "UpdateManager: initialized with appcast URL: " << m_appcastUrl.toStdString();
}

void UpdateManager::checkForUpdates(bool userInitiated) {
    if (!m_initialized) {
        initialize();
    }

    if (m_checkInFlight) {
        LOG_WARN << "Update check already in progress, skipping new request";
        return;
    }

    if (ApplicationSettings::getInstance().isOfflineModeEnabled()) {
        UpdateStatus status = m_lastStatus;
        status.offline = true;
        status.statusMessage = QStringLiteral("Offline mode enabled");
        publishStatus(status, "Offline mode enabled, skipping update check");
        emit checkFailed(status.statusMessage);
        return;
    }

    QUrl appcastUrl(m_appcastUrl);
    QNetworkRequest req(appcastUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", userAgent().toUtf8());

    m_checkInFlight = true;
    emit checkStarted();
    LOG_WARN << "UpdateManager: check started (userInitiated=" << (userInitiated ? "true" : "false")
             << ") url=" << appcastUrl.toString().toStdString();

    QNetworkReply* reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, userInitiated]() {
        handleAppcastReply(reply, userInitiated);
    });
    connect(reply, &QNetworkReply::errorOccurred, this, [reply](QNetworkReply::NetworkError) {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    QTimer::singleShot(15000, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
}

void UpdateManager::handleAppcastReply(QNetworkReply* reply, bool /*userInitiated*/) {
    const auto guard = std::unique_ptr<QNetworkReply, void(*)(QNetworkReply*)>(reply, [](QNetworkReply* r) {
        if (r) r->deleteLater();
    });
    m_checkInFlight = false;

    if (!reply || reply->error() != QNetworkReply::NoError) {
        QString error = reply ? reply->errorString() : QStringLiteral("Unknown network error");
        LOG_WARN << "UpdateManager: update check failed: " << error.toStdString();
        UpdateStatus status = m_lastStatus;
        status.statusMessage = QStringLiteral("Update check failed: %1").arg(error);
        publishStatus(status, "Update check failed");
        emit checkFailed(status.statusMessage);
        return;
    }

    const QByteArray payload = reply->readAll();
    const UpdateStatus parsed = parseAppcast(payload);
    LOG_WARN << "UpdateManager: appcast parsed tier=" << tierToString(parsed.tier).toStdString()
             << " latest=" << parsed.latestVersion.toStdString()
             << " downloadUrl=" << parsed.downloadUrl.toStdString();

    if (parsed.latestVersion.isEmpty() || parsed.downloadUrl.isEmpty()) {
        UpdateStatus status = parsed;
        status.statusMessage = parsed.statusMessage.isEmpty()
                                   ? QStringLiteral("No update information found")
                                   : parsed.statusMessage;
        publishStatus(status, "Appcast missing version or download URL");
        emit checkFailed(status.statusMessage);
        return;
    }

    publishStatus(parsed, "Appcast processed");
}

UpdateStatus UpdateManager::parseAppcast(const QByteArray& payload) const {
    UpdateStatus status;
    status.currentVersion = m_currentVersion;
    status.statusMessage = QStringLiteral("Unable to parse appcast");

    QXmlStreamReader xml(payload);
    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement() && xml.name() == QLatin1String("item")) {
            QString latestVersion;
            QString downloadUrl;
            QString releaseNotes;
            QString releaseNotesLink;
            bool critical = false;

            while (!(xml.isEndElement() && xml.name() == QLatin1String("item")) && !xml.atEnd()) {
                xml.readNext();
                if (!xml.isStartElement()) continue;

                const QString tagName = xml.name().toString();
                const QString tagPrefix = xml.prefix().toString();
                if (tagName == QLatin1String("enclosure")) {
                    const auto attrs = xml.attributes();
                    downloadUrl = attrs.value("url").toString();
                    const QString versionAttr = attrs.hasAttribute("sparkle:version")
                        ? attrs.value("sparkle:version").toString()
                        : attrs.value("version").toString();
                    if (latestVersion.isEmpty() && !versionAttr.isEmpty()) {
                        latestVersion = versionAttr;
                    }
                    if (attrs.hasAttribute("sparkle:criticalUpdate")) {
                        critical = isCriticalValue(attrs.value("sparkle:criticalUpdate").toString());
                    }
                    if (attrs.hasAttribute("critical")) {
                        critical = isCriticalValue(attrs.value("critical").toString());
                    }
                    QString severityAttr;
                    if (attrs.hasAttribute("checkmark:updateSeverity")) {
                        severityAttr = attrs.value("checkmark:updateSeverity").toString();
                    } else if (attrs.hasAttribute("updateSeverity")) {
                        severityAttr = attrs.value("updateSeverity").toString();
                    }
                    if (!severityAttr.isEmpty()) {
                        critical = severityAttr.trimmed().compare(QLatin1String("critical"), Qt::CaseInsensitive) == 0;
                    }
                } else if (tagName == QLatin1String("version") && (tagPrefix.isEmpty() || tagPrefix == QLatin1String("sparkle"))) {
                    if (latestVersion.isEmpty()) {
                        latestVersion = xml.readElementText().trimmed();
                    }
                } else if (tagName == QLatin1String("shortVersionString") && tagPrefix == QLatin1String("sparkle") && latestVersion.isEmpty()) {
                    latestVersion = xml.readElementText().trimmed();
                } else if (tagName == QLatin1String("releaseNotesLink") && tagPrefix == QLatin1String("sparkle")) {
                    releaseNotesLink = xml.readElementText().trimmed();
                } else if (tagName == QLatin1String("description") && releaseNotes.isEmpty()) {
                    releaseNotes = xml.readElementText().trimmed();
                } else if ((tagName == QLatin1String("criticalUpdate") && (tagPrefix == QLatin1String("sparkle") || tagPrefix == QLatin1String("checkmark"))) ||
                           (tagName == QLatin1String("critical") && tagPrefix == QLatin1String("checkmark"))) {
                    critical = isCriticalValue(xml.readElementText().trimmed());
                } else if (tagName == QLatin1String("updateSeverity") && tagPrefix == QLatin1String("checkmark")) {
                    const QString severity = xml.readElementText().trimmed().toLower();
                    critical = (severity == QLatin1String("critical"));
                }
            }

            status.latestVersion = latestVersion;
            status.downloadUrl = downloadUrl;
            status.releaseNotes = releaseNotes;
            status.releaseNotesLink = releaseNotesLink;
            status.tier = determineTier(latestVersion, critical);

            switch (status.tier) {
                case UpdateTier::UpToDate:
                    status.statusMessage = QStringLiteral("Up to date");
                    break;
                case UpdateTier::Critical:
                    status.statusMessage = QStringLiteral("Critical update available");
                    break;
                case UpdateTier::Suggestion:
                    status.statusMessage = QStringLiteral("Update available");
                    break;
                case UpdateTier::Unknown:
                default:
                    status.statusMessage = QStringLiteral("Unknown update status");
                    break;
            }
            return status;
        }
    }

    if (xml.hasError()) {
        status.statusMessage = QStringLiteral("Appcast parse error: %1").arg(xml.errorString());
    }
    return status;
}

UpdateTier UpdateManager::determineTier(const QString& latestVersion, bool isCritical) const {
    const QVersionNumber current = QVersionNumber::fromString(m_currentVersion);
    const QVersionNumber remote = QVersionNumber::fromString(latestVersion);

    if (current.isNull() || remote.isNull()) {
        return UpdateTier::Unknown;
    }

    if (remote <= current) {
        return UpdateTier::UpToDate;
    }

    return isCritical ? UpdateTier::Critical : UpdateTier::Suggestion;
}

bool UpdateManager::isCriticalValue(const QString& value) const {
    const QString lowered = value.trimmed().toLower();
    return lowered == QLatin1String("true") ||
           lowered == QLatin1String("1") ||
           lowered == QLatin1String("yes") ||
           lowered == QLatin1String("critical");
}

QString UpdateManager::userAgent() const {
    return QStringLiteral("checkmark/%1").arg(m_currentVersion);
}

void UpdateManager::setAppcastURL(const QString& url) {
    m_appcastUrl = url;
    LOG_INFO << "Updated appcast URL to: " << url.toStdString();
}

void UpdateManager::setAppVersion(const QString& version) {
    m_currentVersion = version;
    m_lastStatus.currentVersion = version;
    LOG_INFO << "Set current version to: " << version.toStdString();
}

UpdateStatus UpdateManager::lastKnownStatus() const {
    return m_lastStatus;
}

QString UpdateManager::downloadTargetPath(const QUrl& url, const QString& version) const {
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::tempPath();
    }

    QDir dir(baseDir);
    dir.mkpath(QStringLiteral("checkmark-updater"));
    dir.cd(QStringLiteral("checkmark-updater"));

    QString fileName = QFileInfo(url.path()).fileName();
    if (fileName.isEmpty()) {
        const QString versionTag = version.isEmpty() ? QStringLiteral("latest") : version;
        fileName = QStringLiteral("checkmark-%1-installer.exe").arg(versionTag);
    }

    return dir.filePath(fileName);
}

void UpdateManager::publishStatus(const UpdateStatus& status, const QString& reason) {
    m_lastStatus = status;

    LOG_WARN << "UpdateManager: status " << reason.toStdString()
             << " tier=" << tierToString(status.tier).toStdString()
             << " current=" << m_currentVersion.toStdString()
             << " latest=" << status.latestVersion.toStdString();

    emit statusChanged(m_lastStatus);

    if (m_lastStatus.tier == UpdateTier::Critical && !m_criticalPromptShown) {
        m_criticalPromptShown = true;
        emit criticalUpdateDetected(m_lastStatus);
    }
}

void UpdateManager::downloadAndInstallLatest() {
    if (m_activeDownload) {
        LOG_WARN << "Update download already in progress";
        return;
    }

    if (!m_lastStatus.hasUpdate() || m_lastStatus.downloadUrl.isEmpty()) {
        emit downloadFailed(QStringLiteral("No update available to download"));
        LOG_ERROR << "UpdateManager: download requested but no update available";
        return;
    }

    const QUrl url(m_lastStatus.downloadUrl);
    if (!url.isValid()) {
        emit downloadFailed(QStringLiteral("Invalid download URL"));
        LOG_ERROR << "UpdateManager: invalid download URL " << m_lastStatus.downloadUrl.toStdString();
        return;
    }

    const QString targetPath = downloadTargetPath(url, m_lastStatus.latestVersion);
    m_downloadFile = std::make_unique<QFile>(targetPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        emit downloadFailed(QStringLiteral("Unable to write installer to %1").arg(targetPath));
        m_downloadFile.reset();
        LOG_ERROR << "UpdateManager: cannot write installer to " << targetPath.toStdString();
        return;
    }

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", userAgent().toUtf8());

    m_activeDownload = m_networkManager->get(req);
    emit downloadStarted(m_lastStatus.latestVersion);
    LOG_WARN << "UpdateManager: download started for " << url.toString().toStdString();

    connect(m_activeDownload, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadFile) {
            m_downloadFile->write(m_activeDownload->readAll());
        }
    });
    connect(m_activeDownload, &QNetworkReply::downloadProgress, this, &UpdateManager::downloadProgress);
    connect(m_activeDownload, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError) {
        if (m_downloadFile) {
            m_downloadFile->close();
            m_downloadFile->remove();
            m_downloadFile.reset();
        }
    });
    connect(m_activeDownload, &QNetworkReply::finished, this, [this]() {
        QNetworkReply* reply = m_activeDownload;
        m_activeDownload = nullptr;

        if (!reply) return;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            if (m_downloadFile) {
                m_downloadFile->remove();
                m_downloadFile.reset();
            }
            emit downloadFailed(reply->errorString());
            LOG_ERROR << "UpdateManager: download finished with error: " << reply->errorString().toStdString();
            return;
        }

        if (m_downloadFile) {
            m_downloadFile->flush();
            m_downloadFile->close();
            const QString installerPath = m_downloadFile->fileName();
            emit downloadFinished(installerPath);
            LOG_WARN << "UpdateManager: download finished, launching installer at "
                     << installerPath.toStdString();
            launchInstaller(installerPath);
            m_downloadFile.reset();
        }
    });
}

void UpdateManager::cancelDownload() {
    if (m_activeDownload) {
        m_activeDownload->abort();
        m_activeDownload->deleteLater();
        m_activeDownload = nullptr;
    }
    if (m_downloadFile) {
        m_downloadFile->close();
        m_downloadFile->remove();
        m_downloadFile.reset();
    }
}

void UpdateManager::launchInstaller(const QString& installerPath) {
    LOG_INFO << "Launching installer: " << installerPath.toStdString();
    const bool started = QProcess::startDetached(installerPath, {});
    if (!started) {
        emit downloadFailed(QStringLiteral("Failed to launch installer"));
        return;
    }

    emit installerLaunched(installerPath);

    // Allow a short grace period for the installer to take over before quitting.
    QTimer::singleShot(500, qApp, []() {
        qApp->quit();
    });
}

void UpdateManager::onUpdateCheckTimer() {
    checkForUpdates(false);
}
