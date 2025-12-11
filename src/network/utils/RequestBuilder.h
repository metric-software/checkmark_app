#ifndef REQUESTBUILDER_H
#define REQUESTBUILDER_H

// RequestBuilder - Fluent API for constructing HTTP requests
// Used by: BaseApiClient for building NetworkRequest objects with URLs, headers, body
// Purpose: Simplify HTTP request construction with method chaining and parameter validation
// When to use: Internal utility - used by API clients to build requests programmatically
// Operations: URL building, header management, query parameters, method chaining, validation

#include "../core/INetworkClient.h"
#include <QString>
#include <QMap>

class RequestBuilder {
public:
    RequestBuilder();
    RequestBuilder(const QString& baseUrl);
    
    RequestBuilder& setMethod(HttpMethod method);
    RequestBuilder& setUrl(const QString& url);
    RequestBuilder& setPath(const QString& path);
    RequestBuilder& setBody(const QByteArray& body);
    RequestBuilder& addHeader(const QString& name, const QString& value);
    RequestBuilder& setHeaders(const QMap<QString, QString>& headers);
    RequestBuilder& addQueryParam(const QString& name, const QString& value);
    
    NetworkRequest build() const;
    
    // Convenience methods
    static RequestBuilder get(const QString& url);
    static RequestBuilder post(const QString& url);
    static RequestBuilder put(const QString& url);
    static RequestBuilder del(const QString& url);

private:
    QString m_baseUrl;
    QString m_path;
    HttpMethod m_method;
    QByteArray m_body;
    QMap<QString, QString> m_headers;
    QMap<QString, QString> m_queryParams;
    
    QString buildFullUrl() const;
    QString buildQueryString() const;
};

#endif // REQUESTBUILDER_H