#include "NetworkCache.h"
#include <QTimer>

NetworkCache::NetworkCache(QObject* parent)
    : QObject(parent)
    , m_defaultTTL(300) { // 5 minutes default
    startCleanupTimer();
}

void NetworkCache::set(const QString& key, const QVariant& data, int ttlSeconds) {
    CacheEntry entry;
    entry.data = data;
    entry.timestamp = QDateTime::currentDateTime();
    entry.ttlSeconds = (ttlSeconds > 0) ? ttlSeconds : m_defaultTTL;
    
    m_cache[key] = entry;
}

QVariant NetworkCache::get(const QString& key) const {
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        const CacheEntry& entry = it.value();
        if (!entry.isExpired()) {
            return entry.data;
        }
    }
    return QVariant();
}

bool NetworkCache::contains(const QString& key) const {
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        return !it.value().isExpired();
    }
    return false;
}

void NetworkCache::remove(const QString& key) {
    m_cache.remove(key);
}

void NetworkCache::clear() {
    m_cache.clear();
}

void NetworkCache::setDefaultTTL(int seconds) {
    m_defaultTTL = seconds;
}

int NetworkCache::getDefaultTTL() const {
    return m_defaultTTL;
}

size_t NetworkCache::size() const {
    return static_cast<size_t>(m_cache.size());
}

QStringList NetworkCache::keys() const {
    return m_cache.keys();
}

void NetworkCache::cleanupExpiredEntries() {
    QStringList expiredKeys;
    
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it.value().isExpired()) {
            expiredKeys.append(it.key());
        }
    }
    
    for (const QString& key : expiredKeys) {
        m_cache.remove(key);
        emit entryExpired(key);
    }
}

void NetworkCache::startCleanupTimer() {
    QTimer* cleanupTimer = new QTimer(this);
    cleanupTimer->setInterval(60000); // Clean up every minute
    connect(cleanupTimer, &QTimer::timeout, this, &NetworkCache::cleanupExpiredEntries);
    cleanupTimer->start();
}