#pragma once

#include <QFile>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <memory>

enum class UpdateTier {
    Unknown = 0,
    UpToDate,
    Suggestion,
    Critical,
};

struct UpdateStatus {
    UpdateTier tier = UpdateTier::Unknown;
    QString currentVersion;
    QString latestVersion;
    QString downloadUrl;
    QString releaseNotes;
    QString releaseNotesLink;
    QString statusMessage;
    bool offline = false;

    bool hasUpdate() const {
        return tier == UpdateTier::Suggestion || tier == UpdateTier::Critical;
    }
};

Q_DECLARE_METATYPE(UpdateStatus)

class UpdateManager : public QObject {
    Q_OBJECT

public:
    static UpdateManager& getInstance();

    void initialize();
    void checkForUpdates(bool userInitiated = false);
    void setAppcastURL(const QString& url);
    void setAppVersion(const QString& version);
    UpdateStatus lastKnownStatus() const;
    void downloadAndInstallLatest();
    void cancelDownload();

signals:
    void checkStarted();
    void statusChanged(const UpdateStatus& status);
    void checkFailed(const QString& error);
    void criticalUpdateDetected(const UpdateStatus& status);
    void downloadStarted(const QString& version);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFinished(const QString& installerPath);
    void downloadFailed(const QString& error);
    void installerLaunched(const QString& installerPath);

private slots:
    void onUpdateCheckTimer();

private:
    UpdateManager(QObject* parent = nullptr);
    ~UpdateManager();

    void handleAppcastReply(QNetworkReply* reply, bool userInitiated);
    UpdateStatus parseAppcast(const QByteArray& payload) const;
    UpdateTier determineTier(const QString& latestVersion, bool isCritical) const;
    bool isCriticalValue(const QString& value) const;
    QString downloadTargetPath(const QUrl& url, const QString& version) const;
    void checkForDemoUpdate(bool userInitiated);
    void handleDemoManifestReply(QNetworkReply* reply);
    void startDemoDownload(const QString& url, const QString& filename,
                           const QString& version, const QString& sha256,
                           qint64 expectedSize);
    bool validateDemoFile(const QString& path, const QString& sha256,
                          qint64 expectedSize) const;
    QString resolveBenchmarkStorageDir() const;
    QString userAgent() const;
    QString resolvedAppcastUrl() const;
    void publishStatus(const UpdateStatus& status, const QString& reason);
    void launchInstaller(const QString& installerPath);

    bool m_initialized = false;
    bool m_checkInFlight = false;
    bool m_criticalPromptShown = false;
    QString m_appcastUrl;
    QString m_demoManifestUrl;
    QString m_currentVersion;
    QString m_latestDemoVersion;
    QString m_latestDemoPath;
    QTimer* m_checkTimer = nullptr;
    QNetworkAccessManager* m_networkManager = nullptr;
    QNetworkReply* m_activeDownload = nullptr;
    QNetworkReply* m_demoDownload = nullptr;
    std::unique_ptr<QFile> m_downloadFile;
    bool m_demoCheckInFlight = false;
    UpdateStatus m_lastStatus;

    static UpdateManager* s_instance;
};
