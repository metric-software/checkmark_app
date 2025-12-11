#ifndef INETWORKCLIENT_H
#define INETWORKCLIENT_H

// INetworkClient - Abstract HTTP transport interface
// Used by: BaseApiClient and its subclasses (DownloadApiClient, UploadApiClient)
// Purpose: Low-level HTTP operations (GET/POST/PUT/DELETE) with request/response handling
// When to use: Implement for different HTTP backends (Qt, curl, etc.) - don't use directly
// Operations: Raw HTTP requests, progress tracking, request cancellation

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QMap>
#include <functional>

enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE_METHOD
};

struct NetworkRequest {
    QString url;
    HttpMethod method = HttpMethod::GET;
    QByteArray body;
    QMap<QString, QString> headers;
};

struct NetworkResponse {
    int statusCode = 0;
    QByteArray body;
    QMap<QString, QString> headers;
    QString error;
    bool success = false;
};

using NetworkCallback = std::function<void(const NetworkResponse&)>;

class INetworkClient : public QObject {
    Q_OBJECT

public:
    explicit INetworkClient(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~INetworkClient() = default;
    
    virtual void sendRequest(const NetworkRequest& request, NetworkCallback callback) = 0;
    virtual void cancelAllRequests() = 0;

signals:
    void requestProgress(qint64 bytesSent, qint64 bytesTotal);
};

#endif // INETWORKCLIENT_H