#ifndef DOWNLOADAPICLIENT_H
#define DOWNLOADAPICLIENT_H

// DownloadApiClient - Component and menu data fetching API client
// Used by: UI components for downloading comparison data
// Purpose: Fetch server menus and component benchmark data with caching and endpoint resolution
// When to use: For downloading comparison data - call directly from UI components
// Operations: Menu fetching, component data retrieval, response parsing, endpoint caching

#include "BaseApiClient.h"
#include <QStringList>
#include <QJsonObject>
#include <QDateTime>

struct ComponentData {
    QString componentName;
    QJsonObject testData;
    QJsonObject metaData;
};

struct MenuData {
    QStringList availableCpus;
    QStringList availableGpus;
    QStringList availableMemory;
    QStringList availableDrives;
    QVariantMap endpoints;
};

using MenuCallback = std::function<void(bool success, const MenuData& data, const QString& error)>;
using ComponentCallback = std::function<void(bool success, const ComponentData& data, const QString& error)>;
using GeneralCallback = std::function<void(bool success, const QString& error)>;

class DownloadApiClient : public BaseApiClient {
    Q_OBJECT

public:
    explicit DownloadApiClient(QObject* parent = nullptr);
    
    void fetchMenu(MenuCallback callback);
    void fetchComponentData(const QString& componentType, const QString& modelName, ComponentCallback callback);

    // Prefetches and caches aggregated cross-user diagnostics averages.
    void prefetchGeneralDiagnostics(GeneralCallback callback = nullptr);

    static QString generalAverageLabel() { return QStringLiteral("Avg for all users"); }
    
    // Cache access
    bool isMenuCached() const;
    MenuData getCachedMenu() const;
    bool isComponentCached(const QString& componentType, const QString& modelName) const;
    ComponentData getCachedComponent(const QString& componentType, const QString& modelName) const;

signals:
    void menuFetched(const MenuData& menuData);
    void componentDataFetched(const QString& componentType, const QString& modelName, const ComponentData& data);
    void downloadError(const QString& errorMessage);

private:
    MenuData m_cachedMenu;
    bool m_menuCached;

    // General (cross-user aggregate) cache
    bool m_generalCached = false;
    bool m_generalFetchInFlight = false;
    QDateTime m_generalFetchedAtUtc;
    QJsonObject m_generalMeta;
    QMap<QString, ComponentData> m_generalComponents; // cpu/gpu/memory/drive (+ future)
    QList<GeneralCallback> m_generalWaiters;
    
    MenuData parseMenuData(const QVariant& data) const;
    ComponentData parseComponentData(const QVariant& data) const;
    void ensureGeneralDiagnosticsReady(GeneralCallback callback);
    void parseAndCacheGeneralDiagnostics(const QVariant& data);
    QString generateComponentCacheKey(const QString& componentType, const QString& modelName) const;
};

#endif // DOWNLOADAPICLIENT_H
