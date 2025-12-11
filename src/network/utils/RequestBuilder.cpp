#include "RequestBuilder.h"
#include "../core/NetworkConfig.h"
#include <QUrl>
#include <QUrlQuery>

RequestBuilder::RequestBuilder() : m_method(HttpMethod::GET) {
    m_baseUrl = NetworkConfig::instance().getBaseUrl();
}

RequestBuilder::RequestBuilder(const QString& baseUrl) 
    : m_baseUrl(baseUrl), m_method(HttpMethod::GET) {
}

RequestBuilder& RequestBuilder::setMethod(HttpMethod method) {
    m_method = method;
    return *this;
}

RequestBuilder& RequestBuilder::setUrl(const QString& url) {
    // If it's a full URL, extract base and path
    QUrl qurl(url);
    if (qurl.isValid() && !qurl.scheme().isEmpty()) {
        QString scheme = qurl.scheme().trimmed().toLower();
        QString host = qurl.host();
        int port = qurl.port();
        if (scheme.isEmpty() || scheme == QLatin1String("http")) {
            scheme = QStringLiteral("https");
            if (port == 80) port = -1; // drop default http port
        }
        m_baseUrl = QString("%1://%2").arg(scheme, host);
        if (port != -1) {
            m_baseUrl += ":" + QString::number(port);
        }
        m_path = qurl.path();
        
        // Extract query parameters
        QUrlQuery query(qurl);
        const auto queryItems = query.queryItems();
        for (const auto& item : queryItems) {
            m_queryParams[item.first] = item.second;
        }
    } else {
        // Assume it's a path
        m_path = url;
    }
    return *this;
}

RequestBuilder& RequestBuilder::setPath(const QString& path) {
    m_path = path;
    return *this;
}

RequestBuilder& RequestBuilder::setBody(const QByteArray& body) {
    m_body = body;
    return *this;
}

RequestBuilder& RequestBuilder::addHeader(const QString& name, const QString& value) {
    m_headers[name] = value;
    return *this;
}

RequestBuilder& RequestBuilder::setHeaders(const QMap<QString, QString>& headers) {
    m_headers = headers;
    return *this;
}

RequestBuilder& RequestBuilder::addQueryParam(const QString& name, const QString& value) {
    m_queryParams[name] = value;
    return *this;
}

NetworkRequest RequestBuilder::build() const {
    NetworkRequest request;
    request.method = m_method;
    request.url = buildFullUrl();
    request.body = m_body;
    request.headers = m_headers;
    
    return request;
}

RequestBuilder RequestBuilder::get(const QString& url) {
    return RequestBuilder().setMethod(HttpMethod::GET).setUrl(url);
}

RequestBuilder RequestBuilder::post(const QString& url) {
    return RequestBuilder().setMethod(HttpMethod::POST).setUrl(url);
}

RequestBuilder RequestBuilder::put(const QString& url) {
    return RequestBuilder().setMethod(HttpMethod::PUT).setUrl(url);
}

RequestBuilder RequestBuilder::del(const QString& url) {
    return RequestBuilder().setMethod(HttpMethod::DELETE_METHOD).setUrl(url);
}

QString RequestBuilder::buildFullUrl() const {
    QString url = m_baseUrl;
    
    if (!m_path.isEmpty()) {
        if (!url.endsWith('/') && !m_path.startsWith('/')) {
            url += '/';
        }
        url += m_path;
    }
    
    QString queryString = buildQueryString();
    if (!queryString.isEmpty()) {
        url += "?" + queryString;
    }
    
    return url;
}

QString RequestBuilder::buildQueryString() const {
    if (m_queryParams.isEmpty()) {
        return QString();
    }
    
    QUrlQuery query;
    for (auto it = m_queryParams.begin(); it != m_queryParams.end(); ++it) {
        query.addQueryItem(it.key(), it.value());
    }
    
    return query.toString();
}