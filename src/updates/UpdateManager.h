#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class UpdateManager : public QObject {
    Q_OBJECT

public:
    static UpdateManager& getInstance();
    
    void initialize();
    void checkForUpdates();
    void setAppcastURL(const QString& url);
    void setAppVersion(const QString& version);
    bool isUpdateAvailable() const;
    void showUpdateDialog();
    
signals:
    void updateAvailable(const QString& version);
    void criticalUpdateAvailable(const QString& version);
    void updateNotAvailable();
    void updateError(const QString& error);

private slots:
    void onUpdateCheck();
    void onNetworkReply();

private:
    UpdateManager(QObject* parent = nullptr);
    ~UpdateManager();
    
    void setupWinSparkle();
    void checkServerConnection();
    static void updateFoundCallback();
    static void updateNotFoundCallback();
    static void updateErrorCallback();
    static int canShutdownCallback();
    static void shutdownRequestCallback();
    
    bool m_initialized = false;
    bool m_updateAvailable = false;
    QString m_appcastUrl;
    QString m_currentVersion;
    QTimer* m_checkTimer;
    QNetworkAccessManager* m_networkManager;
    
    static UpdateManager* s_instance;
};