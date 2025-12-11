#ifndef BASEAPICLIENT_H
#define BASEAPICLIENT_H

// BaseApiClient - Composable high-level API client foundation
// Used by: DownloadApiClient, UploadApiClient as base class for specialized endpoints
// Purpose: Combines HTTP, serialization, encryption, caching into unified API operations
// When to use: Inherit for domain-specific API clients - handles cross-cutting concerns
// Operations: GET/POST/PUT/DELETE with caching, serialization, encryption, progress tracking

#include <QObject>
#include <memory>
#include "../core/INetworkClient.h"
#include "../serialization/ISerializer.h"
#include "../crypto/ICryptoProvider.h"
#include "../utils/NetworkCache.h"
#include "../utils/RequestBuilder.h"

struct ApiResponse {
    bool success = false;
    int statusCode = 0;
    QVariant data;
    QString error;
    QMap<QString, QString> headers;
};

using ApiCallback = std::function<void(const ApiResponse&)>;

class BaseApiClient : public QObject {
    Q_OBJECT

public:
    explicit BaseApiClient(QObject* parent = nullptr);
    virtual ~BaseApiClient();
    
    // Configuration
    void setNetworkClient(std::shared_ptr<INetworkClient> client);
    void setSerializer(std::shared_ptr<ISerializer> serializer);
    void setCryptoProvider(std::shared_ptr<ICryptoProvider> crypto);
    void setCache(std::shared_ptr<NetworkCache> cache);
    
    // Request methods
    void get(const QString& path, ApiCallback callback, bool useCache = true,
             const QString& expectedProtoType = QString());
    void post(const QString& path, const QVariant& data, ApiCallback callback,
              const QString& expectedProtoType = QString());
    void put(const QString& path, const QVariant& data, ApiCallback callback,
             const QString& expectedProtoType = QString());
    void del(const QString& path, ApiCallback callback,
             const QString& expectedProtoType = QString());
    
    // Advanced request method
    void sendRequest(const RequestBuilder& builder, const QVariant& data, 
                    ApiCallback callback, bool useCache = false, const QString& cacheKey = "", int ttlSeconds = 0,
                    const QString& expectedProtoType = QString());

signals:
    void requestStarted(const QString& path);
    void requestCompleted(const QString& path, bool success);
    void requestProgress(qint64 bytesSent, qint64 bytesTotal);

protected:
    std::shared_ptr<INetworkClient> m_networkClient;
    std::shared_ptr<ISerializer> m_serializer;
    std::shared_ptr<ICryptoProvider> m_cryptoProvider;
    std::shared_ptr<NetworkCache> m_cache;

private:
    void handleNetworkResponse(const NetworkResponse& response, ApiCallback callback, 
                              const QString& cacheKey, bool shouldCache, int ttlSeconds,
                              const QString& expectedProtoType);
    QString generateCacheKey(const QString& path, const QVariant& data = QVariant()) const;
    ApiResponse createApiResponse(const NetworkResponse& response,
                                  const QString& expectedProtoType) const;
    
private slots:
    void onRequestProgress(qint64 bytesSent, qint64 bytesTotal);
};

#endif // BASEAPICLIENT_H
