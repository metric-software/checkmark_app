#include "BaseApiClient.h"
#include "../core/QtNetworkClient.h"
#include "../serialization/JsonSerializer.h"
#include "../crypto/NullCryptoProvider.h"
#include "../../logging/Logger.h"
#include "../../ApplicationSettings.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QUrl>

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
    if (ApplicationSettings::getInstance().isOfflineModeEnabled()) {
        ApiResponse response;
        response.error = "Offline mode is enabled";
        LOG_WARN << "Network request blocked because Offline Mode is enabled for path: "
                 << builder.build().url.toStdString();
        callback(response);
        return;
    }

    if (!m_networkClient) {
        ApiResponse response;
        response.error = "Network client not configured";
        LOG_ERROR << "Network client not configured";
        callback(response);
        return;
    }
    
    NetworkRequest request = builder.build();
    LOG_WARN << "HTTP request: method=" << static_cast<int>(request.method)
             << " url=" << request.url.toStdString()
             << " cache=" << (useCache ? "on" : "off")
             << " expected=" << expectedProtoType.toStdString();
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
    m_networkClient->sendRequest(request, [this, callback, cacheKey, useCache, ttlSeconds, url = request.url, method = request.method,
                                           expectedProtoType]
                                 (const NetworkResponse& response) {
        LOG_WARN << "HTTP response: status=" << response.statusCode
                 << " success=" << response.success
                 << " bytes=" << response.body.size()
                 << " url=" << url.toStdString();
        if (!response.success && !response.error.isEmpty()) {
            LOG_WARN << "HTTP response error: " << response.error.toStdString();
        }
        handleNetworkResponse(response, url, method, callback, cacheKey, useCache, ttlSeconds, expectedProtoType);
        emit requestCompleted(url, response.success);
    });
}

static QString methodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET: return QStringLiteral("GET");
        case HttpMethod::POST: return QStringLiteral("POST");
        case HttpMethod::PUT: return QStringLiteral("PUT");
        case HttpMethod::DELETE_METHOD: return QStringLiteral("DELETE");
    }
    return QStringLiteral("UNKNOWN");
}

static QString sanitizeForFilename(QString s) {
    s.replace("\\", "_");
    s.replace("/", "_");
    s.replace("?", "_");
    s.replace("&", "_");
    s.replace("=", "_");
    s.replace(":", "_");
    s.replace("*", "_");
    s.replace("\"", "_");
    s.replace("<", "_");
    s.replace(">", "_");
    s.replace("|", "_");
    s.replace(" ", "_");
    while (s.contains("__")) s.replace("__", "_");
    if (s.size() > 120) s = s.left(120);
    return s;
}

static void dumpNetworkExchangeToDisk(const QString& url, HttpMethod method, const NetworkResponse& response,
                                      const QString& expectedProtoType, const QString& typeHint,
                                      const ApiResponse& apiResponse, const QByteArray& decryptedBody) {
    try {
        const QString baseDir = QCoreApplication::applicationDirPath() + QStringLiteral("/network_responses");
        QDir().mkpath(baseDir);

        const QString ts = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
        const QByteArray urlHash = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex().left(8);
        const QUrl qurl(url);
        const QString pathPart = sanitizeForFilename(qurl.path() + (qurl.hasQuery() ? ("?" + qurl.query()) : QString()));
        const QString prefix = QStringLiteral("%1_%2_%3_%4")
            .arg(ts, methodToString(method), pathPart, QString::fromLatin1(urlHash));

        // Raw body (as received)
        {
            QFile f(baseDir + "/" + prefix + ".raw.bin");
            if (f.open(QIODevice::WriteOnly)) {
                f.write(response.body);
            }
        }

        // Decrypted/plain body bytes
        if (decryptedBody != response.body) {
            QFile f(baseDir + "/" + prefix + ".body.bin");
            if (f.open(QIODevice::WriteOnly)) {
                f.write(decryptedBody);
            }
        }

        // Parsed payload (best-effort)
        if (apiResponse.data.type() == QVariant::Map || apiResponse.data.type() == QVariant::List) {
            QFile f(baseDir + "/" + prefix + ".parsed.json");
            if (f.open(QIODevice::WriteOnly)) {
                QJsonDocument doc = QJsonDocument::fromVariant(apiResponse.data);
                f.write(doc.toJson(QJsonDocument::Indented));
            }
        } else if (apiResponse.data.canConvert<QString>()) {
            QFile f(baseDir + "/" + prefix + ".parsed.txt");
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream tsOut(&f);
                tsOut << apiResponse.data.toString();
            }
        }

        // Meta info
        {
            QFile f(baseDir + "/" + prefix + ".meta.txt");
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream out(&f);
                out << "url=" << url << "\n";
                out << "method=" << methodToString(method) << "\n";
                out << "status=" << response.statusCode << "\n";
                out << "success=" << response.success << "\n";
                out << "expected=" << expectedProtoType << "\n";
                out << "typeHint=" << typeHint << "\n";
                out << "rawBytes=" << response.body.size() << "\n";
                out << "bodyBytes=" << decryptedBody.size() << "\n";
                if (!apiResponse.error.isEmpty()) out << "error=" << apiResponse.error << "\n";
                out << "\nresponse_headers:\n";
                for (auto it = response.headers.begin(); it != response.headers.end(); ++it) {
                    out << it.key() << ": " << it.value() << "\n";
                }
            }
        }
    } catch (...) {
        // Never let dumping affect app behavior.
    }
}

void BaseApiClient::handleNetworkResponse(const NetworkResponse& response, const QString& url, HttpMethod method,
                     ApiCallback callback, const QString& cacheKey, bool shouldCache, int ttlSeconds,
                     const QString& expectedProtoType) {
    ApiResponse apiResponse = createApiResponse(response, url, method, expectedProtoType);
    
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

ApiResponse BaseApiClient::createApiResponse(const NetworkResponse& response, const QString& url, HttpMethod method,
                                             const QString& expectedProtoType) const {
    ApiResponse apiResponse;
    apiResponse.success = response.success;
    apiResponse.statusCode = response.statusCode;
    apiResponse.headers = response.headers;
    
    if (!response.success) {
        apiResponse.error = response.error;
        dumpNetworkExchangeToDisk(url, method, response, expectedProtoType, QString(), apiResponse, response.body);
        return apiResponse;
    }

    // Decrypt if needed
    QByteArray responseData = response.body;
    if (m_cryptoProvider && m_cryptoProvider->getAlgorithm() != CryptoAlgorithm::NONE) {
        // For now, we don't decrypt since we're using NullCryptoProvider
        // This is where we would decrypt: responseData = decrypted data
    }

    
    // Deserialize response
    QString typeHint;
    if (m_serializer && !responseData.isEmpty()) {
        typeHint = expectedProtoType;
        if (typeHint.isEmpty()) {
            typeHint = response.headers.value(QStringLiteral("X-Protobuf-Message"));
        }
        DeserializationResult deserResult = m_serializer->deserialize(responseData, typeHint);
        if (deserResult.success) {
            apiResponse.data = deserResult.data;
        } else {
            apiResponse.success = false;
            apiResponse.error = "Deserialization failed: " + deserResult.error;
        }
    } else {
        // Raw data
        apiResponse.data = QString::fromUtf8(responseData);
    }

    dumpNetworkExchangeToDisk(url, method, response, expectedProtoType, typeHint, apiResponse, responseData);
    LOG_WARN << "HTTP parsed: url=" << url.toStdString()
             << " status=" << apiResponse.statusCode
             << " ok=" << apiResponse.success
             << " typeHint=" << typeHint.toStdString()
             << " variantType=" << (apiResponse.success ? apiResponse.data.typeName() : "n/a");
    
    return apiResponse;
}

void BaseApiClient::onRequestProgress(qint64 bytesSent, qint64 bytesTotal) {
    emit requestProgress(bytesSent, bytesTotal);
}
