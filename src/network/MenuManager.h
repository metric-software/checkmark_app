#pragma once

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include "api/DownloadApiClient.h"
#include "api/BenchmarkApiClient.h"

class MenuManager : public QObject {
    Q_OBJECT

public:
    static MenuManager& getInstance();
    
    void initialize();

    // Access to the underlying diagnostics API client (shared singleton-owned instance).
    DownloadApiClient* diagnosticApiClient() const { return m_downloadClient; }
    
    // Diagnostic menu access
    bool isDiagnosticMenuCached() const;
    MenuData getDiagnosticMenu() const;
    void refreshDiagnosticMenu();
    
    // Benchmark menu access
    bool isBenchmarkMenuCached() const;
    QVariant getBenchmarkMenu() const;
    void refreshBenchmarkMenu();
    
    // Force refresh both menus
    void refreshAllMenus();
    
    // Check if refresh is needed based on time
    bool needsRefresh() const;
    
signals:
    void diagnosticMenuUpdated(const MenuData& menuData);
    void benchmarkMenuUpdated(const QVariant& menuData);
    void menuRefreshError(const QString& error);

private slots:
    void onRefreshTimer();
    void onDiagnosticMenuFetched(bool success, const MenuData& menuData, const QString& error);
    void onBenchmarkMenuFetched(bool success, const QVariant& menuData, const QString& error);

private:
    MenuManager(QObject* parent = nullptr);
    ~MenuManager() = default;
    
    void checkAndRefreshMenus();
    
    bool m_initialized = false;
    QTimer* m_refreshTimer;
    
    // Diagnostic menu
    DownloadApiClient* m_downloadClient;
    MenuData m_diagnosticMenu;
    bool m_diagnosticMenuCached = false;
    QDateTime m_diagnosticMenuLastFetched;
    
    // Benchmark menu
    BenchmarkApiClient* m_benchmarkClient;
    QVariant m_benchmarkMenu;
    bool m_benchmarkMenuCached = false;
    QDateTime m_benchmarkMenuLastFetched;
    
    static MenuManager* s_instance;
    
    // Configuration
    static constexpr int REFRESH_INTERVAL_MS = 5 * 60 * 1000; // 5 minutes
    static constexpr int STARTUP_DELAY_MS = 10 * 1000;        // 10 seconds
};
