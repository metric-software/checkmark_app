#include "QtNetworkClient.h"
#include "NetworkConfig.h"
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>
#include <QSslConfiguration>

QtNetworkClient::QtNetworkClient(QObject* parent)
    : INetworkClient(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

QtNetworkClient::~QtNetworkClient() {
    cancelAllRequests();
}

void QtNetworkClient::sendRequest(const NetworkRequest& request, NetworkCallback callback) {
    QNetworkRequest qRequest = createQNetworkRequest(request);
    QNetworkReply* reply = nullptr;
    
    switch (request.method) {
        case HttpMethod::GET:
            reply = m_networkManager->get(qRequest);
            break;
        case HttpMethod::POST:
            reply = m_networkManager->post(qRequest, request.body);
            break;
        case HttpMethod::PUT:
            reply = m_networkManager->put(qRequest, request.body);
            break;
        case HttpMethod::DELETE_METHOD:
            reply = m_networkManager->deleteResource(qRequest);
            break;
    }
    
    if (!reply) {
        NetworkResponse response;
        response.error = "Failed to create network request";
        callback(response);
        return;
    }
    
    m_pendingRequests[reply] = callback;
    
    connect(reply, &QNetworkReply::finished, this, &QtNetworkClient::onRequestFinished);
    connect(reply, &QNetworkReply::uploadProgress, this, &QtNetworkClient::onRequestProgress);
    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &QtNetworkClient::onRequestError);
    connect(reply, QOverload<const QList<QSslError>&>::of(&QNetworkReply::sslErrors),
            this, &QtNetworkClient::onSslErrors);
    
    // Setup timeout
    int timeout = NetworkConfig::instance().getTimeout();
    if (timeout > 0) {
        QTimer* timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(timeout);
        connect(timer, &QTimer::timeout, this, &QtNetworkClient::onRequestTimeout);
        
        m_requestTimers[reply] = timer;
        timer->start();
    }
}

void QtNetworkClient::cancelAllRequests() {
    for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ++it) {
        QNetworkReply* reply = it.key();
        reply->abort();
        cleanupRequest(reply);
    }
    m_pendingRequests.clear();
}

void QtNetworkClient::onRequestFinished() {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || !m_pendingRequests.contains(reply)) {
        return;
    }
    
    NetworkCallback callback = m_pendingRequests[reply];
    NetworkResponse response = createNetworkResponse(reply);
    
    cleanupRequest(reply);
    callback(response);
}

void QtNetworkClient::onRequestProgress(qint64 bytesSent, qint64 bytesTotal) {
    emit requestProgress(bytesSent, bytesTotal);
}

void QtNetworkClient::onRequestError(QNetworkReply::NetworkError error) {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply || !m_pendingRequests.contains(reply)) {
        return;
    }
    
    // Let onRequestFinished handle the error response
}

void QtNetworkClient::onRequestTimeout() {
    QTimer* timer = qobject_cast<QTimer*>(sender());
    if (!timer) {
        return;
    }
    
    for (auto it = m_requestTimers.begin(); it != m_requestTimers.end(); ++it) {
        if (it.value() == timer) {
            QNetworkReply* reply = it.key();
            reply->abort();
            
            NetworkResponse response;
            response.error = "Request timed out";
            
            if (m_pendingRequests.contains(reply)) {
                NetworkCallback callback = m_pendingRequests[reply];
                cleanupRequest(reply);
                callback(response);
            }
            break;
        }
    }
}

QNetworkRequest QtNetworkClient::createQNetworkRequest(const NetworkRequest& request) {
    QNetworkRequest qRequest(QUrl(request.url));
    
    // Set default headers
    qRequest.setHeader(QNetworkRequest::UserAgentHeader, NetworkConfig::instance().getUserAgent());
    
    // Set custom headers
    for (auto it = request.headers.begin(); it != request.headers.end(); ++it) {
        qRequest.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }
    
    return qRequest;
}

NetworkResponse QtNetworkClient::createNetworkResponse(QNetworkReply* reply) {
    NetworkResponse response;
    
    response.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    response.body = reply->readAll();
    response.success = (reply->error() == QNetworkReply::NoError && 
                       response.statusCode >= 200 && response.statusCode < 300);
    
    if (reply->error() != QNetworkReply::NoError) {
        response.error = reply->errorString();
    }
    
    // Extract headers
    const QList<QByteArray> headerList = reply->rawHeaderList();
    for (const QByteArray& headerName : headerList) {
        response.headers[QString::fromUtf8(headerName)] = 
            QString::fromUtf8(reply->rawHeader(headerName));
    }
    
    return response;
}

void QtNetworkClient::onSslErrors(const QList<QSslError>& errors) {
    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        return;
    }
    if (NetworkConfig::instance().getAllowInsecureSsl()) {
        // For local development, optionally ignore SSL certificate errors
        qDebug() << "SSL errors ignored due to CHECKMARK_ALLOW_INSECURE_SSL:" << errors;
        reply->ignoreSslErrors();
    } else {
        qDebug() << "SSL errors encountered (not ignored):" << errors;
        // Default behavior: do not ignore; the request will fail with SSL error
    }
}

void QtNetworkClient::cleanupRequest(QNetworkReply* reply) {
    if (m_requestTimers.contains(reply)) {
        m_requestTimers[reply]->stop();
        m_requestTimers[reply]->deleteLater();
        m_requestTimers.remove(reply);
    }
    
    m_pendingRequests.remove(reply);
    reply->deleteLater();
}