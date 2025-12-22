#include "DownloadApiClient.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include <cmath>
#include "../serialization/ProtobufSerializer.h"
#include "../../ApplicationSettings.h"
#include "../../diagnostic/DiagnosticDataStore.h"
#include "../../logging/Logger.h"

DownloadApiClient::DownloadApiClient(QObject* parent)
    : BaseApiClient(parent), m_menuCached(false) {
    // Set protobuf serializer for binary protobuf communication
    setSerializer(std::make_shared<ProtobufSerializer>());
}

void DownloadApiClient::prefetchGeneralDiagnostics(GeneralCallback callback) {
    ensureGeneralDiagnosticsReady(std::move(callback));
}

void DownloadApiClient::fetchMenu(MenuCallback callback) {
    if (!ApplicationSettings::getInstance().getEffectiveAllowDataCollection()) {
        QString error = ApplicationSettings::getInstance().isOfflineModeEnabled()
            ? QStringLiteral("Offline mode is enabled")
            : QStringLiteral("Data collection is disabled");
        LOG_INFO << "DownloadApiClient: Menu fetch blocked: " << error.toStdString();
        emit downloadError(error);
        callback(false, MenuData(), error);
        return;
    }
    LOG_INFO << "DownloadApiClient: Fetching menu from /pb/menu (protobuf)";
    RequestBuilder b = RequestBuilder::get("/pb/menu");
    constexpr int kTTL = 60; // 1 minute cache for menu
    sendRequest(b, QVariant(), [this, callback](const ApiResponse& response) {
        try {
            if (response.success) {
                MenuData menuData = parseMenuData(response.data);
                
                // Validate that we got at least some menu data
                if (menuData.availableCpus.isEmpty() && menuData.availableGpus.isEmpty() && 
                    menuData.availableMemory.isEmpty() && menuData.availableDrives.isEmpty()) {
                    LOG_WARN << "Menu fetch succeeded but returned no component data, using empty menu";
                }
                
                m_cachedMenu = menuData;
                m_menuCached = true;
                
                emit menuFetched(menuData);
                callback(true, menuData, QString());

                // Prefetch general averages alongside menu so comparison slots can populate immediately.
                prefetchGeneralDiagnostics();
            } else {
                LOG_ERROR << "Menu fetch failed: " << response.error.toStdString();
                emit downloadError(response.error);
                callback(false, MenuData(), response.error);
            }
        } catch (const std::exception& e) {
            QString error = QString("Exception during menu processing: %1").arg(e.what());
            LOG_ERROR << "Menu fetch exception: " << e.what();
            emit downloadError(error);
            callback(false, MenuData(), error);
        } catch (...) {
            QString error = "Unknown exception during menu processing";
            LOG_ERROR << "Unknown menu fetch exception";
            emit downloadError(error);
            callback(false, MenuData(), error);
        }
    }, /*useCache=*/true, /*cacheKey=*/QString::fromLatin1("/pb/menu"), kTTL, /*expectedProtoType=*/QStringLiteral("MenuResponse"));
}

void DownloadApiClient::fetchComponentData(const QString& componentType, const QString& modelName, 
                                          ComponentCallback callback) {
    if (!ApplicationSettings::getInstance().getEffectiveAllowDataCollection()) {
        QString error = ApplicationSettings::getInstance().isOfflineModeEnabled()
            ? QStringLiteral("Offline mode is enabled")
            : QStringLiteral("Data collection is disabled");
        LOG_INFO << "DownloadApiClient: Component fetch blocked: " << error.toStdString();
        emit downloadError(error);
        callback(false, ComponentData(), error);
        return;
    }
    // Validate inputs
    if (componentType.isEmpty() || modelName.isEmpty()) {
        QString error = QString("Invalid component request: type='%1', model='%2'")
                         .arg(componentType, modelName);
        emit downloadError(error);
        callback(false, ComponentData(), error);
        return;
    }

    // Special-case: aggregated cross-user averages via /api/diagnostics/general
    if (modelName == generalAverageLabel() &&
        (componentType == QLatin1String("cpu") || componentType == QLatin1String("gpu") ||
         componentType == QLatin1String("memory") || componentType == QLatin1String("drive"))) {
        ensureGeneralDiagnosticsReady([this, componentType, modelName, callback](bool success, const QString& error) {
            if (!success) {
                emit downloadError(error);
                callback(false, ComponentData(), error);
                return;
            }

            if (!m_generalComponents.contains(componentType)) {
                QString e = QString("General diagnostics missing component: %1").arg(componentType);
                emit downloadError(e);
                callback(false, ComponentData(), e);
                return;
            }

            const ComponentData componentData = m_generalComponents.value(componentType);
            emit componentDataFetched(componentType, modelName, componentData);
            callback(true, componentData, QString());
        });
        return;
    }
    LOG_INFO << "DownloadApiClient: Fetching comparison data - type: " << componentType.toStdString()
             << ", model: " << modelName.toStdString();
    
    // Check cache first
    QString cacheKey = generateComponentCacheKey(componentType, modelName);
    if (m_cache && m_cache->contains(cacheKey)) {
        LOG_INFO << "DownloadApiClient: Cache hit for component key: " << cacheKey.toStdString();
        QVariant cachedData = m_cache->get(cacheKey);
        ComponentData componentData = parseComponentData(cachedData);
        emit componentDataFetched(componentType, modelName, componentData);
        callback(true, componentData, QString());
        return;
    }
    LOG_INFO << "DownloadApiClient: Cache miss for component key: " << cacheKey.toStdString();
    
    // Build endpoint URL
    QString endpoint;
    if (m_menuCached && m_cachedMenu.endpoints.contains(componentType)) {
        QString endpointTemplate = m_cachedMenu.endpoints.value(componentType).toString();
        // Ensure the endpoint uses /pb/ prefix
        if (!endpointTemplate.startsWith("/pb/")) {
            endpointTemplate = "/pb" + endpointTemplate;
        }
        if (endpointTemplate.contains("{model_name}")) {
            endpoint = endpointTemplate.replace("{model_name}", QUrl::toPercentEncoding(modelName));
        } else {
            // Menu currently returns base endpoint; attach model query param.
            endpoint = endpointTemplate;
            if (!endpoint.contains("?")) {
                endpoint += QString("?model=%1").arg(QUrl::toPercentEncoding(modelName));
            }
        }
        LOG_INFO << "DownloadApiClient: Using menu-provided endpoint template for type '" 
                 << componentType.toStdString() << "' -> " << endpointTemplate.toStdString();
    } else {
        // Fallback if menu not cached
        endpoint = QString("/pb/component/%1?model=%2").arg(componentType, QUrl::toPercentEncoding(modelName));
        LOG_WARN << "DownloadApiClient: Menu endpoints not cached; using fallback endpoint: " << endpoint.toStdString();
    }
    LOG_INFO << "DownloadApiClient: GET " << endpoint.toStdString();
    
    get(endpoint, [this, componentType, modelName, callback](const ApiResponse& response) {
        if (response.success) {
            ComponentData componentData = parseComponentData(response.data);
            LOG_INFO << "DownloadApiClient: Component response parsed for type '" 
                     << componentType.toStdString() << "' model '" 
                     << modelName.toStdString() << "'";
            
            emit componentDataFetched(componentType, modelName, componentData);
            callback(true, componentData, QString());
        } else {
            LOG_ERROR << "DownloadApiClient: Component fetch failed for type '" 
                      << componentType.toStdString() << "' model '" 
                      << modelName.toStdString() << "' error: "
                      << response.error.toStdString();
            emit downloadError(response.error);
            callback(false, ComponentData(), response.error);
        }
    }, /*useCache=*/true, /*expectedProtoType=*/QStringLiteral("ComponentComparison"));
}

void DownloadApiClient::ensureGeneralDiagnosticsReady(GeneralCallback callback) {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    constexpr int kTTLSeconds = 15 * 60;

    if (!ApplicationSettings::getInstance().getEffectiveAllowDataCollection()) {
        QString error = ApplicationSettings::getInstance().isOfflineModeEnabled()
            ? QStringLiteral("Offline mode is enabled")
            : QStringLiteral("Data collection is disabled");
        if (callback) callback(false, error);
        return;
    }

    if (m_generalCached && m_generalFetchedAtUtc.isValid() &&
        m_generalFetchedAtUtc.secsTo(now) < kTTLSeconds) {
        if (callback) callback(true, QString());
        return;
    }

    if (callback) {
        m_generalWaiters.push_back(std::move(callback));
    }

    if (m_generalFetchInFlight) {
        return;
    }

    m_generalFetchInFlight = true;

    RequestBuilder b = RequestBuilder::get("/pb/diagnostics/general");
    b.addHeader(QStringLiteral("Accept"),
                m_serializer ? m_serializer->getContentType()
                             : QStringLiteral("application/x-protobuf"));
    const QString cacheKey = QStringLiteral("/pb/diagnostics/general");

    sendRequest(b, QVariant(), [this](const ApiResponse& response) {
        m_generalFetchInFlight = false;

        QList<GeneralCallback> waiters = std::move(m_generalWaiters);
        m_generalWaiters.clear();

        if (!response.success) {
            for (const auto& w : waiters) {
                if (w) w(false, response.error);
            }
            return;
        }

        parseAndCacheGeneralDiagnostics(response.data);
        m_generalFetchedAtUtc = QDateTime::currentDateTimeUtc();
        m_generalCached = true;

        for (const auto& w : waiters) {
            if (w) w(true, QString());
        }

    }, /*useCache=*/true, /*cacheKey=*/cacheKey, /*ttlSeconds=*/kTTLSeconds, /*expectedProtoType=*/QStringLiteral("Struct"));
}

static QJsonObject toJsonObjectOrEmpty(const QVariant& v) {
    if (v.type() != QVariant::Map) return QJsonObject();
    return QJsonObject::fromVariantMap(v.toMap());
}

void DownloadApiClient::parseAndCacheGeneralDiagnostics(const QVariant& data) {
    m_generalComponents.clear();
    m_generalMeta = QJsonObject();

    if (data.type() != QVariant::Map) {
        LOG_ERROR << "General diagnostics data is not a map, got type: " << data.typeName();
        return;
    }

    const QVariantMap root = data.toMap();
    m_generalMeta = toJsonObjectOrEmpty(root.value(QStringLiteral("meta")));

    // High-level diagnostic logging for debugging. Detailed payload is dumped to disk by BaseApiClient.
    try {
        const int sampleCount = m_generalMeta.value(QStringLiteral("sample_count")).toInt();
        LOG_WARN << "General diagnostics: parsed keys=" << root.keys().join(", ").toStdString()
                 << " sample_count=" << sampleCount;
    } catch (...) {
        LOG_WARN << "General diagnostics: parsed (failed to summarize meta)";
    }

    const QString label = generalAverageLabel();

    auto makeComponent = [&](const QString& type, const QJsonObject& testData) {
        ComponentData out;
        out.componentName = label;
        out.testData = testData;
        out.metaData = m_generalMeta;
        m_generalComponents.insert(type, out);
    };

    // CPU: map general schema into existing CPUComparison-shaped JSON (used by renderers)
    if (root.contains(QStringLiteral("cpu")) && root.value(QStringLiteral("cpu")).type() == QVariant::Map) {
        const QVariantMap cpu = root.value(QStringLiteral("cpu")).toMap();
        QJsonObject cpuObj;
        cpuObj.insert(QStringLiteral("model"), label);
        cpuObj.insert(QStringLiteral("full_model"), label);
        cpuObj.insert(QStringLiteral("cores"), static_cast<int>(std::round(cpu.value(QStringLiteral("cores_avg")).toDouble())));
        cpuObj.insert(QStringLiteral("threads"), static_cast<int>(std::round(cpu.value(QStringLiteral("threads_avg")).toDouble())));
        cpuObj.insert(QStringLiteral("benchmark_results"), toJsonObjectOrEmpty(cpu.value(QStringLiteral("benchmark_results"))));

        // cache_latencies: server uses latency_ns; renderers expect latency (ns)
        QJsonArray cacheLatencies;
        const QVariant v = cpu.value(QStringLiteral("cache_latencies"));
        if (v.type() == QVariant::List) {
            const QVariantList lst = v.toList();
            for (const QVariant& item : lst) {
                if (item.type() != QVariant::Map) continue;
                const QVariantMap m = item.toMap();
                QJsonObject e;
                e.insert(QStringLiteral("size_kb"), m.value(QStringLiteral("size_kb")).toInt());
                e.insert(QStringLiteral("latency"), m.value(QStringLiteral("latency_ns")).toDouble());
                cacheLatencies.append(e);
            }
        }
        cpuObj.insert(QStringLiteral("cache_latencies"), cacheLatencies);

        makeComponent(QStringLiteral("cpu"), cpuObj);
    }

    // GPU
    if (root.contains(QStringLiteral("gpu")) && root.value(QStringLiteral("gpu")).type() == QVariant::Map) {
        const QVariantMap gpu = root.value(QStringLiteral("gpu")).toMap();
        QJsonObject gpuObj;
        gpuObj.insert(QStringLiteral("model"), label);
        gpuObj.insert(QStringLiteral("full_model"), label);
        gpuObj.insert(QStringLiteral("benchmark_results"), toJsonObjectOrEmpty(gpu.value(QStringLiteral("benchmark_results"))));
        makeComponent(QStringLiteral("gpu"), gpuObj);
    }

    // Memory
    if (root.contains(QStringLiteral("memory")) && root.value(QStringLiteral("memory")).type() == QVariant::Map) {
        const QVariantMap mem = root.value(QStringLiteral("memory")).toMap();
        QJsonObject memObj;
        memObj.insert(QStringLiteral("model"), label);
        memObj.insert(QStringLiteral("benchmark_results"), toJsonObjectOrEmpty(mem.value(QStringLiteral("benchmark_results"))));
        memObj.insert(QStringLiteral("total_memory_gb"), mem.value(QStringLiteral("total_memory_gb")).toDouble());
        makeComponent(QStringLiteral("memory"), memObj);
    }

    // Drive
    if (root.contains(QStringLiteral("drive")) && root.value(QStringLiteral("drive")).type() == QVariant::Map) {
        const QVariantMap drive = root.value(QStringLiteral("drive")).toMap();
        QJsonObject driveObj;
        driveObj.insert(QStringLiteral("model"), label);
        driveObj.insert(QStringLiteral("benchmark_results"), toJsonObjectOrEmpty(drive.value(QStringLiteral("benchmark_results"))));
        makeComponent(QStringLiteral("drive"), driveObj);
    }

    // Background processes (used for "typical" comparison rows)
    DiagnosticDataStore::BackgroundProcessGeneralMetrics backgroundMetrics;
    bool hasBackgroundMetrics = false;
    if (root.contains(QStringLiteral("background")) && root.value(QStringLiteral("background")).type() == QVariant::Map) {
        const QVariantMap bg = root.value(QStringLiteral("background")).toMap();

        auto readDouble = [](const QVariantMap& m, const QString& key, double fallback = -1.0) -> double {
            if (!m.contains(key)) return fallback;
            bool ok = false;
            const double v = m.value(key).toDouble(&ok);
            return ok ? v : fallback;
        };

        backgroundMetrics.totalCpuUsage = readDouble(bg, QStringLiteral("total_cpu_usage"));
        backgroundMetrics.totalGpuUsage = readDouble(bg, QStringLiteral("total_gpu_usage"));
        backgroundMetrics.systemDpcTime = readDouble(bg, QStringLiteral("system_dpc_time"));
        backgroundMetrics.systemInterruptTime = readDouble(bg, QStringLiteral("system_interrupt_time"));

        auto parseMemoryMetrics = [&](const QVariant& v) -> DiagnosticDataStore::BackgroundProcessGeneralMetrics::MemoryMetrics {
            DiagnosticDataStore::BackgroundProcessGeneralMetrics::MemoryMetrics out;
            if (v.type() != QVariant::Map) return out;
            const QVariantMap mm = v.toMap();
            out.commitLimitMB = readDouble(mm, QStringLiteral("commit_limit_mb"));
            out.commitPercent = readDouble(mm, QStringLiteral("commit_percent"));
            out.commitTotalMB = readDouble(mm, QStringLiteral("commit_total_mb"));
            out.fileCacheMB = readDouble(mm, QStringLiteral("file_cache_mb"));
            out.kernelNonPagedMB = readDouble(mm, QStringLiteral("kernel_nonpaged_mb"));
            out.kernelPagedMB = readDouble(mm, QStringLiteral("kernel_paged_mb"));
            out.kernelTotalMB = readDouble(mm, QStringLiteral("kernel_total_mb"));
            out.otherMemoryMB = readDouble(mm, QStringLiteral("other_memory_mb"));
            out.physicalAvailableMB = readDouble(mm, QStringLiteral("physical_available_mb"));
            out.physicalTotalMB = readDouble(mm, QStringLiteral("physical_total_mb"));
            out.physicalUsedMB = readDouble(mm, QStringLiteral("physical_used_mb"));
            out.physicalUsedPercent = readDouble(mm, QStringLiteral("physical_used_percent"));
            out.userModePrivateMB = readDouble(mm, QStringLiteral("user_mode_private_mb"));
            return out;
        };

        backgroundMetrics.memoryMetrics = parseMemoryMetrics(bg.value(QStringLiteral("memory_metrics")));

        const QVariant byRam = bg.value(QStringLiteral("memory_metrics_by_ram"));
        if (byRam.type() == QVariant::List) {
            const QVariantList bins = byRam.toList();
            backgroundMetrics.memoryMetricsByRam.reserve(static_cast<size_t>(bins.size()));
            for (const QVariant& item : bins) {
                if (item.type() != QVariant::Map) continue;
                const QVariantMap binMap = item.toMap();

                DiagnosticDataStore::BackgroundProcessGeneralMetrics::MemoryMetricsByRamBin bin;
                bin.totalMemoryGB = readDouble(binMap, QStringLiteral("total_memory_gb"));
                bin.sampleCount = binMap.value(QStringLiteral("sample_count")).toInt();
                bin.metrics = parseMemoryMetrics(binMap.value(QStringLiteral("metrics")));

                backgroundMetrics.memoryMetricsByRam.push_back(std::move(bin));
            }
        }

        hasBackgroundMetrics = (backgroundMetrics.totalCpuUsage >= 0.0 ||
                                backgroundMetrics.totalGpuUsage >= 0.0 ||
                                backgroundMetrics.systemDpcTime >= 0.0 ||
                                backgroundMetrics.systemInterruptTime >= 0.0 ||
                                backgroundMetrics.memoryMetrics.physicalTotalMB >= 0.0 ||
                                !backgroundMetrics.memoryMetricsByRam.empty());
    }

    if (hasBackgroundMetrics) {
        DiagnosticDataStore::getInstance().setGeneralBackgroundProcessMetrics(backgroundMetrics);
    }
}

bool DownloadApiClient::isMenuCached() const {
    return m_menuCached;
}

MenuData DownloadApiClient::getCachedMenu() const {
    return m_cachedMenu;
}

bool DownloadApiClient::isComponentCached(const QString& componentType, const QString& modelName) const {
    if (!m_cache) {
        return false;
    }
    
    QString cacheKey = generateComponentCacheKey(componentType, modelName);
    return m_cache->contains(cacheKey);
}

ComponentData DownloadApiClient::getCachedComponent(const QString& componentType, const QString& modelName) const {
    if (!m_cache) {
        return ComponentData();
    }
    
    QString cacheKey = generateComponentCacheKey(componentType, modelName);
    QVariant cachedData = m_cache->get(cacheKey);
    return parseComponentData(cachedData);
}

MenuData DownloadApiClient::parseMenuData(const QVariant& data) const {
    MenuData menu;
    
    try {
        LOG_INFO << "DownloadApiClient: Starting to parse menu data";
        LOG_INFO << "DownloadApiClient: Raw data type: " << data.typeName();
        
        if (data.isNull() || !data.isValid()) {
            LOG_ERROR << "Menu data is null or invalid";
            return menu; // Return empty menu
        }
        
        if (data.type() != QVariant::Map) {
            LOG_ERROR << "Menu data is not a map, got type: " << data.typeName();
            return menu; // Return empty menu
        }
        
        QVariantMap dataMap = data.toMap();
        if (dataMap.isEmpty()) {
            LOG_WARN << "Menu data map is empty";
            return menu; // Return empty menu
        }
        
        LOG_INFO << "DownloadApiClient: Data map keys: " << dataMap.keys().join(", ").toStdString();
        
        // Parse available components - handle both old format (nested under "available") and new format (direct keys)
        QVariantMap available;
        
        try {
            if (dataMap.contains("available")) {
                LOG_INFO << "DownloadApiClient: Found 'available' section in menu response (old format)";
                QVariant availableVariant = dataMap["available"];
                if (availableVariant.type() == QVariant::Map) {
                    available = availableVariant.toMap();
                    LOG_INFO << "DownloadApiClient: Available section keys: " << available.keys().join(", ").toStdString();
                } else {
                    LOG_ERROR << "DownloadApiClient: 'available' section is not a map, got type: " << availableVariant.typeName();
                }
            } else if (dataMap.contains("available_cpus") || dataMap.contains("available_gpus") || 
                       dataMap.contains("availableCpus") || dataMap.contains("availableGpus")) {
                LOG_INFO << "DownloadApiClient: Found direct available keys in menu response (new format)";
                available = dataMap; // Use the root level map directly
                LOG_INFO << "DownloadApiClient: Direct available keys: " << available.keys().join(", ").toStdString();
            } else {
                LOG_WARN << "DownloadApiClient: No available components found in menu response";
                LOG_WARN << "DownloadApiClient: Expected either 'available' section or direct 'available_cpus/available_gpus' keys";
                // Continue with empty available map - not a critical error
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "Exception parsing available components section: " << e.what();
        }
    
        if (!available.isEmpty()) {
            // Handle CPUs - try both "cpu" and "available_cpus" keys
            try {
                QVariantList cpuList;
                if (available.contains("cpu")) {
                    QVariant cpuVariant = available["cpu"];
                    if (cpuVariant.type() == QVariant::List) {
                        cpuList = cpuVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: CPU data is not a list, got type: " << cpuVariant.typeName();
                    }
                } else if (available.contains("available_cpus")) {
                    QVariant cpuVariant = available["available_cpus"];
                    if (cpuVariant.type() == QVariant::List) {
                        cpuList = cpuVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: available_cpus data is not a list, got type: " << cpuVariant.typeName();
                    }
                } else if (available.contains("availableCpus")) {
                    QVariant cpuVariant = available["availableCpus"];
                    if (cpuVariant.type() == QVariant::List) {
                        cpuList = cpuVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: availableCpus data is not a list, got type: " << cpuVariant.typeName();
                    }
                }
                
                if (!cpuList.isEmpty()) {
                    LOG_INFO << "DownloadApiClient: Found CPU list with " << cpuList.size() << " items";
                    for (const QVariant& item : cpuList) {
                        try {
                            QString cpuName = item.toString();
                            if (!cpuName.trimmed().isEmpty()) {
                                menu.availableCpus.append(cpuName);
                                LOG_INFO << "DownloadApiClient: Added CPU: " << cpuName.toStdString();
                            } else {
                                LOG_WARN << "DownloadApiClient: Skipping empty/invalid CPU item";
                            }
                        } catch (const std::exception& e) {
                            LOG_WARN << "Exception processing CPU item: " << e.what();
                        }
                    }
                } else {
                    LOG_WARN << "DownloadApiClient: No CPU list found in response";
                }
            } catch (const std::exception& e) {
                LOG_ERROR << "Exception parsing CPU list: " << e.what();
            }
        
            // Handle GPUs - try both "gpu" and "available_gpus" keys  
            try {
                QVariantList gpuList;
                if (available.contains("gpu")) {
                    QVariant gpuVariant = available["gpu"];
                    if (gpuVariant.type() == QVariant::List) {
                        gpuList = gpuVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: GPU data is not a list, got type: " << gpuVariant.typeName();
                    }
                } else if (available.contains("available_gpus")) {
                    QVariant gpuVariant = available["available_gpus"];
                    if (gpuVariant.type() == QVariant::List) {
                        gpuList = gpuVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: available_gpus data is not a list, got type: " << gpuVariant.typeName();
                    }
                } else if (available.contains("availableGpus")) {
                    QVariant gpuVariant = available["availableGpus"];
                    if (gpuVariant.type() == QVariant::List) {
                        gpuList = gpuVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: availableGpus data is not a list, got type: " << gpuVariant.typeName();
                    }
                }
                
                if (!gpuList.isEmpty()) {
                    LOG_INFO << "DownloadApiClient: Found GPU list with " << gpuList.size() << " items";
                    for (const QVariant& item : gpuList) {
                        try {
                            QString gpuName = item.toString();
                            if (!gpuName.trimmed().isEmpty()) {
                                menu.availableGpus.append(gpuName);
                                LOG_INFO << "DownloadApiClient: Added GPU: " << gpuName.toStdString();
                            } else {
                                LOG_WARN << "DownloadApiClient: Skipping empty/invalid GPU item";
                            }
                        } catch (const std::exception& e) {
                            LOG_WARN << "Exception processing GPU item: " << e.what();
                        }
                    }
                } else {
                    LOG_WARN << "DownloadApiClient: No GPU list found in response";
                }
            } catch (const std::exception& e) {
                LOG_ERROR << "Exception parsing GPU list: " << e.what();
            }
        
            // Handle Memory - try both "memory" and "available_memory" keys
            try {
                QVariantList memoryList;
                if (available.contains("memory")) {
                    QVariant memoryVariant = available["memory"];
                    if (memoryVariant.type() == QVariant::List) {
                        memoryList = memoryVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: Memory data is not a list, got type: " << memoryVariant.typeName();
                    }
                } else if (available.contains("available_memory")) {
                    QVariant memoryVariant = available["available_memory"];
                    if (memoryVariant.type() == QVariant::List) {
                        memoryList = memoryVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: available_memory data is not a list, got type: " << memoryVariant.typeName();
                    }
                } else if (available.contains("availableMemory")) {
                    QVariant memoryVariant = available["availableMemory"];
                    if (memoryVariant.type() == QVariant::List) {
                        memoryList = memoryVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: availableMemory data is not a list, got type: " << memoryVariant.typeName();
                    }
                }
                
                if (!memoryList.isEmpty()) {
                    LOG_INFO << "DownloadApiClient: Found Memory list with " << memoryList.size() << " items";
                    for (const QVariant& item : memoryList) {
                        try {
                            QString memoryName = item.toString();
                            if (!memoryName.trimmed().isEmpty()) {
                                menu.availableMemory.append(memoryName);
                                LOG_INFO << "DownloadApiClient: Added Memory: " << memoryName.toStdString();
                            } else {
                                LOG_WARN << "DownloadApiClient: Skipping empty/invalid Memory item";
                            }
                        } catch (const std::exception& e) {
                            LOG_WARN << "Exception processing Memory item: " << e.what();
                        }
                    }
                } else {
                    LOG_WARN << "DownloadApiClient: No Memory list found in response";
                }
            } catch (const std::exception& e) {
                LOG_ERROR << "Exception parsing Memory list: " << e.what();
            }
            
            // Handle Drives - try both "drive" and "available_drives" keys
            try {
                QVariantList driveList;
                if (available.contains("drive")) {
                    QVariant driveVariant = available["drive"];
                    if (driveVariant.type() == QVariant::List) {
                        driveList = driveVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: Drive data is not a list, got type: " << driveVariant.typeName();
                    }
                } else if (available.contains("available_drives")) {
                    QVariant driveVariant = available["available_drives"];
                    if (driveVariant.type() == QVariant::List) {
                        driveList = driveVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: available_drives data is not a list, got type: " << driveVariant.typeName();
                    }
                } else if (available.contains("availableDrives")) {
                    QVariant driveVariant = available["availableDrives"];
                    if (driveVariant.type() == QVariant::List) {
                        driveList = driveVariant.toList();
                    } else {
                        LOG_WARN << "DownloadApiClient: availableDrives data is not a list, got type: " << driveVariant.typeName();
                    }
                }
                
                if (!driveList.isEmpty()) {
                    LOG_INFO << "DownloadApiClient: Found Drive list with " << driveList.size() << " items";
                    for (const QVariant& item : driveList) {
                        try {
                            QString driveName = item.toString();
                            if (!driveName.trimmed().isEmpty()) {
                                menu.availableDrives.append(driveName);
                                LOG_INFO << "DownloadApiClient: Added Drive: " << driveName.toStdString();
                            } else {
                                LOG_WARN << "DownloadApiClient: Skipping empty/invalid Drive item";
                            }
                        } catch (const std::exception& e) {
                            LOG_WARN << "Exception processing Drive item: " << e.what();
                        }
                    }
                } else {
                    LOG_WARN << "DownloadApiClient: No Drive list found in response";
                }
            } catch (const std::exception& e) {
                LOG_ERROR << "Exception parsing Drive list: " << e.what();
            }
    }
    
        // Parse endpoints
        try {
            if (dataMap.contains("endpoints")) {
                const QVariant endpointsVariant = dataMap["endpoints"];
                QVariantMap endpointsMap;

                if (endpointsVariant.type() == QVariant::Map) {
                    endpointsMap = endpointsVariant.toMap();
                } else if (endpointsVariant.type() == QVariant::List) {
                    // Back-compat if map fields decode as repeated entries {key,value}.
                    const QVariantList endpointsList = endpointsVariant.toList();
                    for (const QVariant& endpointItem : endpointsList) {
                        if (endpointItem.type() != QVariant::Map) continue;
                        const QVariantMap entry = endpointItem.toMap();
                        const QString key = entry.value("key").toString();
                        const QString value = entry.value("value").toString();
                        if (!key.isEmpty() && !value.isEmpty()) {
                            endpointsMap[key] = value;
                        }
                    }
                } else {
                    LOG_WARN << "DownloadApiClient: 'endpoints' section has unexpected type: "
                             << endpointsVariant.typeName();
                }

                if (!endpointsMap.isEmpty()) {
                    menu.endpoints = endpointsMap;
                    LOG_INFO << "DownloadApiClient: Found endpoints section with " << menu.endpoints.size() << " endpoints";
                } else {
                    LOG_WARN << "DownloadApiClient: Endpoints present but parsed empty";
                }
            } else {
                LOG_WARN << "DownloadApiClient: No 'endpoints' section found in menu response";
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "Exception parsing endpoints section: " << e.what();
        }
        
        LOG_INFO << "DownloadApiClient: Menu parsing complete - CPUs: " << menu.availableCpus.size() 
                 << ", GPUs: " << menu.availableGpus.size() 
                 << ", Memory: " << menu.availableMemory.size() 
                 << ", Drives: " << menu.availableDrives.size();
        
    } catch (const std::exception& e) {
        LOG_ERROR << "Exception during menu data parsing: " << e.what();
        // Return whatever we managed to parse
    } catch (...) {
        LOG_ERROR << "Unknown exception during menu data parsing";
        // Return whatever we managed to parse
    }
    
    return menu;
}

ComponentData DownloadApiClient::parseComponentData(const QVariant& data) const {
    ComponentData componentData;
    
    try {
        LOG_INFO << "DownloadApiClient: Starting to parse component data";
        LOG_INFO << "DownloadApiClient: Component data type: " << data.typeName();
        
        if (data.isNull() || !data.isValid()) {
            LOG_ERROR << "Component data is null or invalid";
            return componentData; // Return empty component data
        }
        
        if (data.type() != QVariant::Map) {
            LOG_ERROR << "Component data is not a map, got type: " << data.typeName();
            return componentData; // Return empty component data
        }
        
        QVariantMap dataMap = data.toMap();
        if (dataMap.isEmpty()) {
            LOG_WARN << "Component data map is empty";
            return componentData; // Return empty component data
        }
        
        // The data is from a ComponentComparison message, which has a 'oneof' field.
        // The ProtobufSerializer converts this into a map with a key like "cpu", "gpu", etc.
        const QStringList componentTypes = {"cpu", "gpu", "memory", "drive"};
        
        for (const QString& type : componentTypes) {
            if (dataMap.contains(type)) {
                LOG_INFO << "DownloadApiClient: Found component data for type: " << type.toStdString();
                QVariantMap componentDetails = dataMap[type].toMap();

                // The entire 'componentDetails' map is the test data.
                componentData.testData = QJsonObject::fromVariantMap(componentDetails);
                
                // The model name is one of the fields inside the details.
                if (componentDetails.contains("model")) {
                    componentData.componentName = componentDetails["model"].toString();
                } else if (componentDetails.contains("full_model")) {
                    // Fallback to another possible name field
                    componentData.componentName = componentDetails["full_model"].toString();
                }
                
                LOG_INFO << "DownloadApiClient: Parsed component '" << componentData.componentName.toStdString() 
                         << "' with " << componentDetails.size() << " data fields.";
                
                // We found the component, no need to check other types in the 'oneof'.
                break; 
            }
        }
        
        // There is no separate "meta" field in the ComponentComparison message,
        // so metaData will be empty, which is expected.
        
        LOG_INFO << "DownloadApiClient: Component data parsing complete";
        
    } catch (const std::exception& e) {
        LOG_ERROR << "Exception during component data parsing: " << e.what();
        // Return whatever we managed to parse
    } catch (...) {
        LOG_ERROR << "Unknown exception during component data parsing";
        // Return whatever we managed to parse
    }
    
    return componentData;
}

QString DownloadApiClient::generateComponentCacheKey(const QString& componentType, const QString& modelName) const {
    return QString("component_%1_%2").arg(componentType, modelName);
}
