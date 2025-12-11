#ifndef NETWORKCONFIG_H
#define NETWORKCONFIG_H

// NetworkConfig - Global network configuration singleton
// Used by: All network clients (BaseApiClient, QtNetworkClient)
// Purpose: Centralized storage for base URLs, timeouts, retry settings, user agents
// When to use: Set once at app startup, accessed by all networking components
// Operations: Configuration storage only - no network operations or data transformation

#include <QString>

class NetworkConfig {
public:
    static NetworkConfig& instance();
    
    void setBaseUrl(const QString& url);
    QString getBaseUrl() const;
    
    void setUserAgent(const QString& agent);
    QString getUserAgent() const;
    
    void setTimeout(int timeoutMs);
    int getTimeout() const;
    
    void setRetryCount(int retries);
    int getRetryCount() const;

    // TLS/SSL behavior
    void setAllowInsecureSsl(bool allow);
    bool getAllowInsecureSsl() const;

private:
    NetworkConfig() = default;
    
    // Default base URL provided at build time via CMake define CHECKMARK_DEFAULT_BASE_URL.
    // Fallback to https://checkmark.gg (production) if not defined (for editor/IDE runs).
    // For local development, build with -DCHECKMARK_USE_LOCAL_SERVER=ON
    // or set env var CHECKMARK_BASE_URL at runtime.
    QString m_baseUrl =
#ifdef CHECKMARK_DEFAULT_BASE_URL
    QString::fromUtf8(CHECKMARK_DEFAULT_BASE_URL)
#else
        QString::fromUtf8("https://checkmark.gg")
#endif
    ;
    QString m_userAgent = "WinBenchmark/1.0";
    int m_timeoutMs = 30000;
    int m_retryCount = 3;
    bool m_allowInsecureSsl = false; // default: verify certs (prod-safe)
};

#endif // NETWORKCONFIG_H