#ifndef NETWORKCACHE_H
#define NETWORKCACHE_H

// NetworkCache - TTL-based caching for API responses
// Used by: DownloadApiClient for menu and component data caching
// Purpose: Store API responses with automatic expiration to reduce server requests
// When to use: Automatically used by API clients - configure TTL per cache entry
// Operations: TTL-based storage, automatic cleanup, key-value access, expiration signals

#include <QObject>
#include <QVariant>
#include <QString>
#include <QDateTime>
#include <QMap>

struct CacheEntry {
    QVariant data;
    QDateTime timestamp;
    int ttlSeconds;
    
    bool isExpired() const {
        return ttlSeconds > 0 && timestamp.addSecs(ttlSeconds) < QDateTime::currentDateTime();
    }
};

class NetworkCache : public QObject {
    Q_OBJECT

public:
    explicit NetworkCache(QObject* parent = nullptr);
    
    void set(const QString& key, const QVariant& data, int ttlSeconds = 0);
    QVariant get(const QString& key) const;
    bool contains(const QString& key) const;
    void remove(const QString& key);
    void clear();
    
    void setDefaultTTL(int seconds);
    int getDefaultTTL() const;
    
    size_t size() const;
    QStringList keys() const;

signals:
    void entryExpired(const QString& key);

private slots:
    void cleanupExpiredEntries();

private:
    QMap<QString, CacheEntry> m_cache;
    int m_defaultTTL;
    
    void startCleanupTimer();
};

#endif // NETWORKCACHE_H