#include "BaseApiClient.h"
#include "../core/QtNetworkClient.h"
#include "../serialization/JsonSerializer.h"
#include "../crypto/NullCryptoProvider.h"
#include "../../logging/Logger.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

BaseApiClient::BaseApiClient(QObject* parent)
    : QObject(parent) {
    
    // Set default implementations
    m_networkClient = std::make_shared<QtNetworkClient>(this);
    m_serializer = std::make_shared<JsonSerializer>();
    m_cryptoProvider = std::make_shared<NullCryptoProvider>();
    m_cache = std::make_shared<NetworkCache>(this);
    
    // Connect progress signals
    connect(m_networkClient.get(), &INetworkClient::requestProgress,
            this, &BaseApiClient::onRequestProgress);
}

BaseApiClient::~BaseApiClient() = default;

void BaseApiClient::setNetworkClient(std::shared_ptr<INetworkClient> client) {
    if (m_networkClient) {
        disconnect(m_networkClient.get(), nullptr, this, nullptr);
    }
    
    m_networkClient = client;
    
    if (m_networkClient) {
        connect(m_networkClient.get(), &INetworkClient::requestProgress,
                this, &BaseApiClient::onRequestProgress);
    }
}

void BaseApiClient::setSerializer(std::shared_ptr<ISerializer> serializer) {
    m_serializer = serializer;
}

void BaseApiClient::setCryptoProvider(std::shared_ptr<ICryptoProvider> crypto) {
    m_cryptoProvider = crypto;
}

void BaseApiClient::setCache(std::shared_ptr<NetworkCache> cache) {
    m_cache = cache;
}

void BaseApiClient::get(const QString& path, ApiCallback callback, bool useCache,
                       const QString& expectedProtoType) {
    QString cacheKey = generateCacheKey(path);
    
    // Check cache first if enabled
    if (useCache && m_cache && m_cache->contains(cacheKey)) {
        QVariant cachedData = m_cache->get(cacheKey);
        ApiResponse response;
        response.success = true;
        response.statusCode = 200;
        response.data = cachedData;
        callback(response);
        return;
    }
    
    RequestBuilder builder = RequestBuilder::get(path);
    // use default TTL from NetworkCache when useCache==true
    sendRequest(builder, QVariant(), callback, useCache, cacheKey, /*ttlSeconds*/0, expectedProtoType);
}

void BaseApiClient::post(const QString& path, const QVariant& data, ApiCallback callback,
                        const QString& expectedProtoType) {
    LOG_INFO << "BaseApiClient::post to path: " << path.toStdString();
    RequestBuilder builder = RequestBuilder::post(path);
    sendRequest(builder, data, callback, /*useCache*/false, /*cacheKey*/QString(), /*ttlSeconds*/0,
                expectedProtoType);
}

void BaseApiClient::put(const QString& path, const QVariant& data, ApiCallback callback,
                       const QString& expectedProtoType) {
    RequestBuilder builder = RequestBuilder::put(path);
    sendRequest(builder, data, callback, /*useCache*/false, /*cacheKey*/QString(), /*ttlSeconds*/0,
                expectedProtoType);
}

void BaseApiClient::del(const QString& path, ApiCallback callback,
                       const QString& expectedProtoType) {
    RequestBuilder builder = RequestBuilder::del(path);
    sendRequest(builder, QVariant(), callback, /*useCache*/false, /*cacheKey*/QString(), /*ttlSeconds*/0,
                expectedProtoType);
}

void BaseApiClient::sendRequest(const RequestBuilder& builder, const QVariant& data, 
                               ApiCallback callback, bool useCache, const QString& cacheKey, int ttlSeconds,
                               const QString& expectedProtoType) {
    if (!m_networkClient) {
        ApiResponse response;
        response.error = "Network client not configured";
        LOG_ERROR << "Network client not configured";
        callback(response);
        return;
    }
    
    NetworkRequest request = builder.build();
    LOG_INFO << "Sending " << static_cast<int>(request.method) << " request to: " << request.url.toStdString();
    emit requestStarted(request.url);
    
    // Serialize data if provided
    if (!data.isNull() && m_serializer) {
        if (!m_serializer->canSerialize(data)) {
            ApiResponse response;
            response.error = "Data cannot be serialized with current serializer";
            emit requestCompleted(request.url, false);
            callback(response);
            return;
        }
        
        SerializationResult serResult = m_serializer->serialize(data);
        if (!serResult.success) {
            ApiResponse response;
            response.error = "Serialization failed: " + serResult.error;
            LOG_ERROR << "Serialization failed: " << serResult.error.toStdString();
            emit requestCompleted(request.url, false);
            callback(response);
            return;
        }
        
        request.body = serResult.data;
        LOG_INFO << "Request body size: " << request.body.size() << " bytes";
        
        // Set content type
        if (!request.headers.contains("Content-Type")) {
            request.headers["Content-Type"] = m_serializer->getContentType();
        }
        
        // Encrypt if crypto provider is available and not null
        if (m_cryptoProvider && m_cryptoProvider->getAlgorithm() != CryptoAlgorithm::NONE) {
            // For now, we don't encrypt since we don't have server public key management
            // This is where we would encrypt: request.body = encrypted data
        }
    }
    
    // Send the request
    LOG_INFO << "Dispatching request to network client...";
    m_networkClient->sendRequest(request, [this, callback, cacheKey, useCache, ttlSeconds, url = request.url,
                                           expectedProtoType]
                                 (const NetworkResponse& response) {
        LOG_INFO << "Network response received - success: " << response.success 
                 << ", status: " << response.statusCode;
        if (!response.success) {
            LOG_ERROR << "Network response error: " << response.error.toStdString();
        }
    handleNetworkResponse(response, callback, cacheKey, useCache, ttlSeconds, expectedProtoType);
        emit requestCompleted(url, response.success);
    });
}

void BaseApiClient::handleNetworkResponse(const NetworkResponse& response, ApiCallback callback, 
                     const QString& cacheKey, bool shouldCache, int ttlSeconds,
                     const QString& expectedProtoType) {
    ApiResponse apiResponse = createApiResponse(response, expectedProtoType);
    
    if (apiResponse.success && shouldCache && m_cache && !cacheKey.isEmpty()) {
    // ttlSeconds==0 -> NetworkCache uses its default TTL
    m_cache->set(cacheKey, apiResponse.data, ttlSeconds);
    }
    
    callback(apiResponse);
}

QString BaseApiClient::generateCacheKey(const QString& path, const QVariant& data) const {
    QString key = path;
    
    if (!data.isNull()) {
        // Add a hash of the data to make the cache key unique
        QByteArray dataBytes;
        if (data.canConvert<QByteArray>()) {
            dataBytes = data.toByteArray();
        } else if (data.type() == QVariant::Map || data.type() == QVariant::List) {
            // Stable JSON canonicalization for map/list
            QJsonDocument jd = (data.type() == QVariant::Map)
                ? QJsonDocument(QJsonObject::fromVariantMap(data.toMap()))
                : QJsonDocument(QJsonArray::fromVariantList(data.toList()));
            dataBytes = jd.toJson(QJsonDocument::Compact);
        } else {
            dataBytes = data.toString().toUtf8();
        }
        QByteArray hash = QCryptographicHash::hash(dataBytes, QCryptographicHash::Md5);
        key += "_" + hash.toHex();
    }
    
    return key;
}

ApiResponse BaseApiClient::createApiResponse(const NetworkResponse& response,
                                             const QString& expectedProtoType) const {
    ApiResponse apiResponse;
    apiResponse.success = response.success;
    apiResponse.statusCode = response.statusCode;
    apiResponse.headers = response.headers;
    
    if (!response.success) {
        apiResponse.error = response.error;
        return apiResponse;
    }
    
    // FIRST CONTACT: log raw network response bytes and relevant headers before any processing
    try {
        QString contentType = response.headers.value(QStringLiteral("Content-Type"));
        LOG_INFO << "Raw network response - status:" << response.statusCode << ", size:" << response.body.size() << "bytes, Content-Type:" << contentType.toStdString();

        // Log a small hex preview of the first bytes (up to 64)
        QString rawHex;
        int maxBytes = qMin(response.body.size(), 64);
        for (int i = 0; i < maxBytes; ++i) {
            rawHex += QString("%1 ").arg(static_cast<unsigned char>(response.body[i]), 2, 16, QChar('0'));
        }
        if (!rawHex.isEmpty()) {
            LOG_INFO << "Raw response head (hex): " << rawHex.toStdString();
        }
    } catch (const std::exception& e) {
        LOG_WARN << "Failed to log raw network response: " << e.what();
    }

    // Decrypt if needed
    QByteArray responseData = response.body;
    if (m_cryptoProvider && m_cryptoProvider->getAlgorithm() != CryptoAlgorithm::NONE) {
        // For now, we don't decrypt since we're using NullCryptoProvider
        // This is where we would decrypt: responseData = decrypted data
    }

    // POST-DECRYPT: log decrypted/plain bytes (attempt text preview and hex fallback)
    try {
        LOG_INFO << "Post-decrypt response size: " << responseData.size() << " bytes";
        QString textPreview = QString::fromUtf8(responseData);
        bool isMostlyText = !textPreview.trimmed().isEmpty() && textPreview.size() <= 1024 && !textPreview.contains(QChar('\0'));
        if (isMostlyText) {
            QString preview = textPreview;
            if (preview.size() > 1024) preview = preview.left(1024) + QStringLiteral("...");
            LOG_INFO << "Post-decrypt text preview: " << preview.toStdString();
        } else {
            // hex preview of first 128 bytes
            QString hexPreview;
            int maxBytes2 = qMin(responseData.size(), 128);
            for (int i = 0; i < maxBytes2; ++i) {
                hexPreview += QString("%1 ").arg(static_cast<unsigned char>(responseData[i]), 2, 16, QChar('0'));
            }
            if (!hexPreview.isEmpty()) {
                LOG_INFO << "Post-decrypt hex preview: " << hexPreview.toStdString();
            }
        }
    } catch (const std::exception& e) {
        LOG_WARN << "Failed to log post-decrypt response: " << e.what();
    }
    
    // Deserialize response
    if (m_serializer && !responseData.isEmpty()) {
        QString typeHint = expectedProtoType;
        if (typeHint.isEmpty()) {
            typeHint = response.headers.value(QStringLiteral("X-Protobuf-Message"));
        }
        DeserializationResult deserResult = m_serializer->deserialize(responseData, typeHint);
        if (deserResult.success) {
            apiResponse.data = deserResult.data;

            // Log deserialized result (type + small preview)
            try {
                LOG_INFO << "Deserialization successful - QVariant type: " << QString(deserResult.data.typeName()).toStdString();
                if (deserResult.data.type() == QVariant::Map) {
                    QVariantMap vm = deserResult.data.toMap();
                    QJsonObject jo = QJsonObject::fromVariantMap(vm);
                    QByteArray j = QJsonDocument(jo).toJson(QJsonDocument::Compact);
                    QString out = QString::fromUtf8(j);
                    if (out.size() > 2000) out = out.left(2000) + QStringLiteral("...");
                    LOG_INFO << "Deserialized JSON preview: " << out.toStdString();
                } else if (deserResult.data.canConvert<QString>()) {
                    QString s = deserResult.data.toString();
                    QString out = s;
                    if (out.size() > 2000) out = out.left(2000) + QStringLiteral("...");
                    LOG_INFO << "Deserialized string preview: " << out.toStdString();
                }
            } catch (const std::exception& e) {
                LOG_WARN << "Failed to log deserialized data preview: " << e.what();
            }

        } else {
            apiResponse.success = false;
            apiResponse.error = "Deserialization failed: " + deserResult.error;
        }
    } else {
        // Raw data
        apiResponse.data = QString::fromUtf8(responseData);
    }
    
    return apiResponse;
}

void BaseApiClient::onRequestProgress(qint64 bytesSent, qint64 bytesTotal) {
    emit requestProgress(bytesSent, bytesTotal);
}
