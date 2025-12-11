#include "DownloadApiClient.h"
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>
#include "../serialization/ProtobufSerializer.h"
#include "../../logging/Logger.h"

DownloadApiClient::DownloadApiClient(QObject* parent)
    : BaseApiClient(parent), m_menuCached(false) {
    // Set protobuf serializer for binary protobuf communication
    setSerializer(std::make_shared<ProtobufSerializer>());
}

void DownloadApiClient::fetchMenu(MenuCallback callback) {
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
    }, /*useCache=*/true, /*cacheKey=*/QString::fromLatin1("/pb/menu"), kTTL);
}

void DownloadApiClient::fetchComponentData(const QString& componentType, const QString& modelName, 
                                          ComponentCallback callback) {
    // Validate inputs
    if (componentType.isEmpty() || modelName.isEmpty()) {
        QString error = QString("Invalid component request: type='%1', model='%2'")
                         .arg(componentType, modelName);
        emit downloadError(error);
        callback(false, ComponentData(), error);
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
        endpoint = endpointTemplate.replace("{model_name}", QUrl::toPercentEncoding(modelName));
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
    });
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
                QVariant endpointsVariant = dataMap["endpoints"];
                if (endpointsVariant.type() == QVariant::Map) {
                    menu.endpoints = endpointsVariant.toMap();
                    LOG_INFO << "DownloadApiClient: Found endpoints section with " << menu.endpoints.size() << " endpoints";
                } else {
                    LOG_WARN << "DownloadApiClient: 'endpoints' section is not a map, got type: " << endpointsVariant.typeName();
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