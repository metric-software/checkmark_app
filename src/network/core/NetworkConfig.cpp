#include "NetworkConfig.h"
#include <QProcessEnvironment>
#include <QUrl>

NetworkConfig& NetworkConfig::instance() {
    static NetworkConfig instance;
    // On first access, allow runtime override via env var CHECKMARK_BASE_URL
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        const auto env = QProcessEnvironment::systemEnvironment();
        const QString overrideUrl = env.value(QStringLiteral("CHECKMARK_BASE_URL"));
        if (!overrideUrl.isEmpty()) {
            instance.setBaseUrl(overrideUrl);
        }
        const QString insecure = env.value(QStringLiteral("CHECKMARK_ALLOW_INSECURE_SSL"));
        if (!insecure.isEmpty()) {
            const QString v = insecure.trimmed().toLower();
            const bool allow = (v == QLatin1String("1") || v == QLatin1String("true") || v == QLatin1String("yes"));
            instance.setAllowInsecureSsl(allow);
        }
    // Always normalize whatever default is compiled in (enforce https scheme, strip path)
    instance.setBaseUrl(instance.getBaseUrl());
    }
    return instance;
}

void NetworkConfig::setBaseUrl(const QString& url) {
    const QString trimmed = url.trimmed();

    // If the user provides an explicit scheme (http/https), preserve it.
    // If no scheme is provided, default to https for production safety.
    const QString withScheme = [&trimmed]() -> QString {
        if (trimmed.contains(QStringLiteral("://"))) {
            return trimmed;
        }
        const QString lower = trimmed.toLower();
        const bool isLocal =
            lower.startsWith(QStringLiteral("localhost")) ||
            lower.startsWith(QStringLiteral("127.0.0.1")) ||
            lower.startsWith(QStringLiteral("[::1]")) ||
            lower.startsWith(QStringLiteral("::1"));
        return (isLocal ? QStringLiteral("http://") : QStringLiteral("https://")) + trimmed;
    }();

    QString finalUrl = trimmed;
    QUrl q(withScheme);
    if (q.isValid()) {
        const QString scheme = q.scheme().trimmed().toLower();
        const QString host = q.host().trimmed();
        const int port = q.port();

        // Keep only scheme://host[:port] (strip any path/query/fragment).
        if (!scheme.isEmpty() && !host.isEmpty()) {
            finalUrl = QString("%1://%2").arg(scheme, host);
            if (port != -1) {
                finalUrl += ":" + QString::number(port);
            }
        }
    }

    m_baseUrl = finalUrl;
}

QString NetworkConfig::getBaseUrl() const {
    return m_baseUrl;
}

void NetworkConfig::setUserAgent(const QString& agent) {
    m_userAgent = agent;
}

QString NetworkConfig::getUserAgent() const {
    return m_userAgent;
}

void NetworkConfig::setTimeout(int timeoutMs) {
    m_timeoutMs = timeoutMs;
}

int NetworkConfig::getTimeout() const {
    return m_timeoutMs;
}

void NetworkConfig::setRetryCount(int retries) {
    m_retryCount = retries;
}

int NetworkConfig::getRetryCount() const {
    return m_retryCount;
}

void NetworkConfig::setAllowInsecureSsl(bool allow) {
    m_allowInsecureSsl = allow;
}

bool NetworkConfig::getAllowInsecureSsl() const {
    return m_allowInsecureSsl;
}
