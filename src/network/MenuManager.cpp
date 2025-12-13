#include "MenuManager.h"
#include "../logging/Logger.h"
#include "../ApplicationSettings.h"

MenuManager* MenuManager::s_instance = nullptr;

MenuManager& MenuManager::getInstance() {
    if (!s_instance) {
        s_instance = new MenuManager();
    }
    return *s_instance;
}

MenuManager::MenuManager(QObject* parent) 
    : QObject(parent),
      m_refreshTimer(new QTimer(this)),
      m_downloadClient(new DownloadApiClient(this)),
      m_benchmarkClient(new BenchmarkApiClient(this)) {
    
    // Set up periodic refresh timer
    m_refreshTimer->setSingleShot(false);
    m_refreshTimer->setInterval(REFRESH_INTERVAL_MS);
    connect(m_refreshTimer, &QTimer::timeout, this, &MenuManager::onRefreshTimer);
    
    LOG_INFO << "MenuManager created with refresh interval: " << (REFRESH_INTERVAL_MS / 1000) << " seconds";
}

void MenuManager::initialize() {
    if (m_initialized) {
        LOG_WARN << "MenuManager already initialized";
        return;
    }
    
    LOG_INFO << "Initializing MenuManager...";
    
    // Start periodic refresh timer
    m_refreshTimer->start();
    
    // Schedule initial menu fetch with delay to allow app to fully start
    QTimer::singleShot(STARTUP_DELAY_MS, this, &MenuManager::refreshAllMenus);
    
    m_initialized = true;
    LOG_INFO << "MenuManager initialized successfully";
}

bool MenuManager::isDiagnosticMenuCached() const {
    return m_diagnosticMenuCached;
}

MenuData MenuManager::getDiagnosticMenu() const {
    return m_diagnosticMenu;
}

void MenuManager::refreshDiagnosticMenu() {
    LOG_INFO << "MenuManager: Refreshing diagnostic menu";
    
    m_downloadClient->fetchMenu([this](bool success, const MenuData& menuData, const QString& error) {
        onDiagnosticMenuFetched(success, menuData, error);
    });
}

bool MenuManager::isBenchmarkMenuCached() const {
    return m_benchmarkMenuCached;
}

QVariant MenuManager::getBenchmarkMenu() const {
    return m_benchmarkMenu;
}

void MenuManager::refreshBenchmarkMenu() {
    LOG_INFO << "MenuManager: Refreshing benchmark menu";
    
    m_benchmarkClient->getBenchmarkMenu([this](bool success, const QVariant& menuData, const QString& error) {
        onBenchmarkMenuFetched(success, menuData, error);
    });
}

void MenuManager::refreshAllMenus() {
    if (ApplicationSettings::getInstance().isOfflineModeEnabled()) {
        LOG_WARN << "MenuManager: Offline Mode enabled, skipping menu refresh";
        return;
    }

    LOG_INFO << "MenuManager: Refreshing all menus";
    refreshDiagnosticMenu();
    refreshBenchmarkMenu();
}

bool MenuManager::needsRefresh() const {
    if (!m_diagnosticMenuCached && !m_benchmarkMenuCached) {
        return true; // No menus cached yet
    }
    
    QDateTime now = QDateTime::currentDateTime();
    
    // Check if diagnostic menu needs refresh (older than 5 minutes)
    bool diagnosticNeedsRefresh = !m_diagnosticMenuCached || 
        m_diagnosticMenuLastFetched.secsTo(now) > (REFRESH_INTERVAL_MS / 1000);
    
    // Check if benchmark menu needs refresh (older than 5 minutes)
    bool benchmarkNeedsRefresh = !m_benchmarkMenuCached || 
        m_benchmarkMenuLastFetched.secsTo(now) > (REFRESH_INTERVAL_MS / 1000);
    
    return diagnosticNeedsRefresh || benchmarkNeedsRefresh;
}

void MenuManager::onRefreshTimer() {
    LOG_INFO << "MenuManager: Periodic refresh timer triggered";
    checkAndRefreshMenus();
}

void MenuManager::checkAndRefreshMenus() {
    if (needsRefresh()) {
        LOG_INFO << "MenuManager: Menu refresh needed, fetching updated menus";
        refreshAllMenus();
    } else {
        LOG_INFO << "MenuManager: Menus are still fresh, skipping refresh";
    }
}

void MenuManager::onDiagnosticMenuFetched(bool success, const MenuData& menuData, const QString& error) {
    if (success) {
        m_diagnosticMenu = menuData;
        m_diagnosticMenuCached = true;
        m_diagnosticMenuLastFetched = QDateTime::currentDateTime();
        
        LOG_INFO << "MenuManager: Diagnostic menu updated successfully - CPUs: " << menuData.availableCpus.size() 
                 << ", GPUs: " << menuData.availableGpus.size()
                 << ", Memory: " << menuData.availableMemory.size()
                 << ", Drives: " << menuData.availableDrives.size();
        
        emit diagnosticMenuUpdated(menuData);
    } else {
        LOG_ERROR << "MenuManager: Diagnostic menu fetch failed: " << error.toStdString();
        emit menuRefreshError(QString("Diagnostic menu fetch failed: %1").arg(error));
    }
}

void MenuManager::onBenchmarkMenuFetched(bool success, const QVariant& menuData, const QString& error) {
    if (success) {
        m_benchmarkMenu = menuData;
        m_benchmarkMenuCached = true;
        m_benchmarkMenuLastFetched = QDateTime::currentDateTime();
        
        LOG_INFO << "MenuManager: Benchmark menu updated successfully";
        
        emit benchmarkMenuUpdated(menuData);
    } else {
        LOG_ERROR << "MenuManager: Benchmark menu fetch failed: " << error.toStdString();
        emit menuRefreshError(QString("Benchmark menu fetch failed: %1").arg(error));
    }
}
