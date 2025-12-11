#ifndef QTNETWORKCLIENT_H
#define QTNETWORKCLIENT_H

// QtNetworkClient - Qt-based HTTP client implementation
// Used by: BaseApiClient (injected as INetworkClient dependency)
// Purpose: Concrete HTTP transport using QNetworkAccessManager with timeout/retry logic
// When to use: Default HTTP backend - automatically used unless custom client injected
// Operations: HTTPS requests, SSL handling, timeout management, progress signals, error handling

#include "INetworkClient.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QMap>
#include <QSslError>

class QtNetworkClient : public INetworkClient {
    Q_OBJECT

public:
    explicit QtNetworkClient(QObject* parent = nullptr);
    ~QtNetworkClient() override;

    void sendRequest(const NetworkRequest& request, NetworkCallback callback) override;
    void cancelAllRequests() override;

private slots:
    void onRequestFinished();
    void onRequestProgress(qint64 bytesSent, qint64 bytesTotal);
    void onRequestError(QNetworkReply::NetworkError error);
    void onRequestTimeout();
    void onSslErrors(const QList<QSslError>& errors);

private:
    QNetworkAccessManager* m_networkManager;
    QMap<QNetworkReply*, NetworkCallback> m_pendingRequests;
    QMap<QNetworkReply*, QTimer*> m_requestTimers;
    
    QNetworkRequest createQNetworkRequest(const NetworkRequest& request);
    NetworkResponse createNetworkResponse(QNetworkReply* reply);
    void cleanupRequest(QNetworkReply* reply);
};

#endif // QTNETWORKCLIENT_H