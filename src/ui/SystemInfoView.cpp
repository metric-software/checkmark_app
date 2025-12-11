#include "SystemInfoView.h"

#include <QFormLayout>
#include <QHeaderView>
#include <QTableWidget>

#include "hardware/ConstantSystemInfo.h"

SystemInfoView::SystemInfoView(QWidget* parent) : QWidget(parent) {
  setupLayout();
  displaySystemInfo();
}

SystemInfoView::~SystemInfoView() {
  // Cleanup if needed
}

void SystemInfoView::setupLayout() {
  // Create main layout
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Create header widget
  QWidget* headerWidget = new QWidget(this);
  headerWidget->setObjectName("headerWidget");
  headerWidget->setStyleSheet(R"(
        #headerWidget {
            background-color: #1e1e1e;
            border-bottom: 1px solid #333333;
        }
    )");

  QVBoxLayout* headerLayout = new QVBoxLayout(headerWidget);
  headerLayout->setContentsMargins(10, 10, 10, 10);

  QLabel* descLabel = new QLabel(
    "Overview of your PC hardware specifications and system information.",
    this);
  descLabel->setWordWrap(true);
  descLabel->setStyleSheet(
    "color: #ffffff; font-size: 14px; background: transparent;");
  headerLayout->addWidget(descLabel);

  mainLayout->addWidget(headerWidget);

  // Create scrollable content area
  scrollArea = new QScrollArea(this);
  scrollArea->setWidgetResizable(true);
  scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea->setStyleSheet("background-color: #1a1a1a; border: none;");

  QWidget* scrollContent = new QWidget(scrollArea);
  scrollContent->setStyleSheet("background-color: #1a1a1a;");
  QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);
  scrollLayout->setSpacing(20);
  scrollLayout->setContentsMargins(12, 12, 12, 12);

  // Initialize section widgets with consistent styling
  cpuWidget = new CustomWidgetWithTitle("CPU", this);
  cpuWidget->setContentsMargins(0, 0, 0, 0);
  cpuWidget->getContentLayout()->setContentsMargins(12, 4, 12, 12);

  memoryWidget = new CustomWidgetWithTitle("Memory", this);
  memoryWidget->setContentsMargins(0, 0, 0, 0);
  memoryWidget->getContentLayout()->setContentsMargins(12, 4, 12, 12);

  gpuWidget = new CustomWidgetWithTitle("Graphics", this);
  gpuWidget->setContentsMargins(0, 0, 0, 0);
  gpuWidget->getContentLayout()->setContentsMargins(12, 4, 12, 12);

  storageWidget = new CustomWidgetWithTitle("Storage", this);
  storageWidget->setContentsMargins(0, 0, 0, 0);
  storageWidget->getContentLayout()->setContentsMargins(12, 4, 12, 12);

  systemWidget = new CustomWidgetWithTitle("System", this);
  systemWidget->setContentsMargins(0, 0, 0, 0);
  systemWidget->getContentLayout()->setContentsMargins(12, 4, 12, 12);

  // Initialize content labels
  cpuInfoLabel = new QLabel(this);
  memoryInfoLabel = new QLabel(this);
  gpuInfoLabel = new QLabel(this);
  storageInfoLabel = new QLabel(this);
  systemInfoLabel = new QLabel(this);

  // Configure labels
  QVector<QLabel*> labels = {cpuInfoLabel, memoryInfoLabel, gpuInfoLabel,
                             storageInfoLabel, systemInfoLabel};
  for (auto label : labels) {
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setStyleSheet("background: transparent;");
  }

  // Add labels to section widgets
  cpuWidget->getContentLayout()->addWidget(cpuInfoLabel);
  memoryWidget->getContentLayout()->addWidget(memoryInfoLabel);
  gpuWidget->getContentLayout()->addWidget(gpuInfoLabel);
  storageWidget->getContentLayout()->addWidget(storageInfoLabel);
  systemWidget->getContentLayout()->addWidget(systemInfoLabel);

  // Add sections to scroll layout
  scrollLayout->addWidget(cpuWidget);
  scrollLayout->addWidget(memoryWidget);
  scrollLayout->addWidget(gpuWidget);
  scrollLayout->addWidget(storageWidget);
  scrollLayout->addWidget(systemWidget);

  scrollLayout->addStretch();

  // Set up scroll area
  scrollContent->setLayout(scrollLayout);
  scrollArea->setWidget(scrollContent);

  mainLayout->addWidget(scrollArea, 1);  // 1 = stretch factor
}

void SystemInfoView::displaySystemInfo() {
  // Get constant system information
  const auto& info = SystemMetrics::GetConstantSystemInfo();

  // CPU Section
  QString cpuTitle = "CPU: " + QString::fromStdString(info.cpuName);
  cpuWidget->setTitle(cpuTitle);

  // Create CPU info box content with grid layout
  QWidget* cpuContent = new QWidget();
  cpuContent->setStyleSheet("background-color: #252525; border-radius: 4px;");
  QGridLayout* cpuGrid = new QGridLayout(cpuContent);
  cpuGrid->setSpacing(15);

  // CPU Basic Info - Use consistent blue color for CPU name
  QWidget* cpuBasicInfo = createMetricBox(
    "Processor", QString::fromStdString(info.cpuName), "#0078d4");
  cpuGrid->addWidget(cpuBasicInfo, 0, 0, 1, 2);

  // CPU specs in boxes - Use consistent blue for standard specs
  QWidget* coresBox = createMetricBox(
    "Physical Cores", QString::number(info.physicalCores), "#0078d4");
  QWidget* threadsBox = createMetricBox(
    "Logical Cores", QString::number(info.logicalCores), "#0078d4");
  QWidget* baseClockBox = createMetricBox(
    "Base Clock", QString::number(info.baseClockMHz) + " MHz", "#0078d4");
  QWidget* archBox = createMetricBox(
    "Architecture", QString::fromStdString(info.cpuArchitecture), "#0078d4");

  cpuGrid->addWidget(coresBox, 1, 0);
  cpuGrid->addWidget(threadsBox, 1, 1);
  cpuGrid->addWidget(baseClockBox, 2, 0);
  cpuGrid->addWidget(archBox, 2, 1);

  // Cache Info
  QWidget* cacheInfo = new QWidget();
  cacheInfo->setStyleSheet("background-color: #252525; border-radius: 4px;");
  QVBoxLayout* cacheLayout = new QVBoxLayout(cacheInfo);
  cacheLayout->setContentsMargins(8, 8, 8, 8);

  QLabel* cacheTitle = new QLabel("<b>Cache Memory</b>");
  cacheTitle->setStyleSheet("color: #0078d4; font-size: 14px;");

  QFormLayout* cacheForm = new QFormLayout();
  cacheForm->setLabelAlignment(Qt::AlignLeft);
  cacheForm->setFormAlignment(Qt::AlignLeft);
  cacheForm->setHorizontalSpacing(10);

  QLabel* l1Label = new QLabel("L1 Cache:");
  QLabel* l2Label = new QLabel("L2 Cache:");
  QLabel* l3Label = new QLabel("L3 Cache:");

  QLabel* l1Value = new QLabel(QString::number(info.l1CacheKB) + " KB");
  QLabel* l2Value = new QLabel(QString::number(info.l2CacheKB) + " KB");
  QLabel* l3Value = new QLabel(QString::number(info.l3CacheKB) + " KB");

  l1Value->setStyleSheet("color: #44FF44;");  // Green
  l2Value->setStyleSheet("color: #88FF88;");  // Light green
  l3Value->setStyleSheet("color: #FFAA00;");  // Orange

  cacheForm->addRow(l1Label, l1Value);
  cacheForm->addRow(l2Label, l2Value);
  cacheForm->addRow(l3Label, l3Value);

  cacheLayout->addWidget(cacheTitle);
  cacheLayout->addLayout(cacheForm);

  cpuGrid->addWidget(cacheInfo, 3, 0, 1, 2);

  // Feature support information
  QWidget* featuresInfo = new QWidget();
  featuresInfo->setStyleSheet("background-color: #252525; border-radius: 4px;");
  QVBoxLayout* featuresLayout = new QVBoxLayout(featuresInfo);
  featuresLayout->setContentsMargins(8, 8, 8, 8);

  QLabel* featuresTitle = new QLabel("<b>CPU Features</b>");
  featuresTitle->setStyleSheet("color: #0078d4; font-size: 14px;");

  QString hyperThreading = info.hyperThreadingEnabled ? "Enabled" : "Disabled";
  QString hyperThreadingColor =
    info.hyperThreadingEnabled ? "#44FF44" : "#AAAAAA";

  QString virtualization = info.virtualizationEnabled ? "Enabled" : "Disabled";
  QString virtualizationColor =
    info.virtualizationEnabled ? "#44FF44" : "#AAAAAA";

  QString avx = info.avxSupport ? "Supported" : "Not Supported";
  QString avxColor = info.avxSupport ? "#44FF44" : "#FF6666";

  QString avx2 = info.avx2Support ? "Supported" : "Not Supported";
  QString avx2Color = info.avx2Support ? "#44FF44" : "#FF6666";

  QLabel* featuresContent =
    new QLabel(QString("Hyper-Threading: <span style='color: %1;'>%2</span><br>"
                       "Virtualization: <span style='color: %3;'>%4</span><br>"
                       "AVX: <span style='color: %5;'>%6</span><br>"
                       "AVX2: <span style='color: %7;'>%8</span>")
                 .arg(hyperThreadingColor, hyperThreading, virtualizationColor,
                      virtualization, avxColor, avx, avx2Color, avx2));

  featuresLayout->addWidget(featuresTitle);
  featuresLayout->addWidget(featuresContent);

  cpuGrid->addWidget(featuresInfo, 4, 0, 1, 2);

  // Replace the basic label with our detailed content
  while (cpuWidget->getContentLayout()->count() > 0) {
    QLayoutItem* item = cpuWidget->getContentLayout()->takeAt(0);
    if (item->widget()) item->widget()->deleteLater();
    delete item;
  }
  cpuWidget->getContentLayout()->addWidget(cpuContent);

  // Memory Section
  memoryWidget->setTitle("Memory: " + QString::fromStdString(info.memoryType) +
                         " " + QString::number(info.memoryClockMHz) + " MHz");

  QWidget* memoryContent = new QWidget();
  memoryContent->setStyleSheet(
    "background-color: #252525; border-radius: 4px;");
  QGridLayout* memoryGrid = new QGridLayout(memoryContent);
  memoryGrid->setSpacing(15);

  // Memory overview - Use consistent blue for standard specs
  double totalGB = info.totalPhysicalMemoryMB / 1024.0;
  QWidget* memTotalBox = createMetricBox(
    "Total Memory", QString::number(totalGB, 'f', 1) + " GB", "#0078d4");
  QWidget* memTypeBox = createMetricBox(
    "Memory Type", QString::fromStdString(info.memoryType), "#0078d4");
  QWidget* memSpeedBox = createMetricBox(
    "Memory Speed", QString::number(info.memoryClockMHz) + " MHz", "#0078d4");

  QString channelConfig = QString::fromStdString(info.memoryChannelConfig);
  QString channelColor =
    channelConfig.contains("Dual", Qt::CaseInsensitive) ? "#44FF44" : "#FFAA00";
  QWidget* memChannelBox =
    createMetricBox("Channel Mode", channelConfig, channelColor);

  QString xmpStatus = info.xmpEnabled ? "Enabled" : "Disabled";
  QString xmpColor = info.xmpEnabled ? "#44FF44" : "#FFAA00";
  QWidget* xmpBox = createMetricBox("XMP Profile", xmpStatus, xmpColor);

  memoryGrid->addWidget(memTotalBox, 0, 0);
  memoryGrid->addWidget(memTypeBox, 0, 1);
  memoryGrid->addWidget(memSpeedBox, 1, 0);
  memoryGrid->addWidget(memChannelBox, 1, 1);
  memoryGrid->addWidget(xmpBox, 2, 0, 1, 2);

  // Memory modules table
  if (!info.memoryModules.empty()) {
    QStringList headers = {"Slot",          "Capacity",
                           "Default Speed", "Configured Speed",
                           "Manufacturer",  "Part Number"};
    QVector<QStringList> rows;

    for (const auto& module : info.memoryModules) {
      QStringList row;
      row << QString::fromStdString(module.deviceLocator);
      row << QString::number(module.capacityGB) + " GB";
      row << QString::number(module.speedMHz) + " MHz";
      row << QString::number(module.configuredSpeedMHz) + " MHz";
      row << QString::fromStdString(module.manufacturer);
      row << QString::fromStdString(module.partNumber);
      rows.append(row);
    }

    QWidget* modulesTable = createHardwareSpecsTable(headers, rows);
    memoryGrid->addWidget(modulesTable, 3, 0, 1, 2);
  }

  // Replace the basic label with our detailed content
  while (memoryWidget->getContentLayout()->count() > 0) {
    QLayoutItem* item = memoryWidget->getContentLayout()->takeAt(0);
    if (item->widget()) item->widget()->deleteLater();
    delete item;
  }
  memoryWidget->getContentLayout()->addWidget(memoryContent);

  // GPU Section
  if (!info.gpuDevices.empty()) {
    // Use primary GPU for title
    QString gpuTitle = "Graphics";
    for (const auto& gpu : info.gpuDevices) {
      if (gpu.isPrimary) {
        gpuTitle = "Graphics: " + QString::fromStdString(gpu.name);
        break;
      }
    }
    gpuWidget->setTitle(gpuTitle);

    QWidget* gpuContent = new QWidget();
    gpuContent->setStyleSheet("background-color: #252525; border-radius: 4px;");
    QVBoxLayout* gpuLayout = new QVBoxLayout(gpuContent);
    gpuLayout->setSpacing(15);

    for (const auto& gpu : info.gpuDevices) {
      QWidget* gpuBox = new QWidget();
      gpuBox->setStyleSheet("background-color: #252525; border-radius: 4px;");
      QGridLayout* gpuGrid = new QGridLayout(gpuBox);
      gpuGrid->setSpacing(15);

      // GPU name and metrics
      QString gpuName = QString::fromStdString(gpu.name);

      // Determine GPU brand color
      QString gpuColor = "#dddddd";  // Default white
      if (gpuName.contains("NVIDIA", Qt::CaseInsensitive) ||
          gpuName.contains("GeForce", Qt::CaseInsensitive)) {
        gpuColor = "#44FF44";  // Green for NVIDIA
      } else if (gpuName.contains("AMD", Qt::CaseInsensitive) ||
                 gpuName.contains("Radeon", Qt::CaseInsensitive)) {
        gpuColor = "#FF4444";  // Red for AMD
      }

      QWidget* nameBox = createMetricBox(gpu.isPrimary ? "Primary GPU" : "GPU",
                                         gpuName, gpuColor);
      gpuGrid->addWidget(nameBox, 0, 0, 1, 2);

      // VRAM, driver version, etc.
      double vramGB = gpu.memoryMB / 1024.0;
      QWidget* vramBox =
        createMetricBox("VRAM", QString::number(vramGB, 'f', 1) + " GB");
      QWidget* driverBox = createMetricBox(
        "Driver Version", QString::fromStdString(gpu.driverVersion));

      gpuGrid->addWidget(vramBox, 1, 0);
      gpuGrid->addWidget(driverBox, 1, 1);

      // PCIe info if available
      if (gpu.pciLinkWidth > 0 || gpu.pcieLinkGen > 0) {
        QWidget* pcieBox = new QWidget();
        pcieBox->setStyleSheet(
          "background-color: #252525; border-radius: 4px;");
        QVBoxLayout* pcieLayout = new QVBoxLayout(pcieBox);
        pcieLayout->setContentsMargins(8, 8, 8, 8);

        QLabel* pcieTitle = new QLabel("<b>PCIe Connection</b>");
        pcieTitle->setStyleSheet("color: #0078d4; font-size: 14px;");

        QString pcieInfo;
        if (gpu.pcieLinkGen > 0 && gpu.pciLinkWidth > 0) {
          pcieInfo = QString("PCIe Gen %1 x%2")
                       .arg(gpu.pcieLinkGen)
                       .arg(gpu.pciLinkWidth);
        } else if (gpu.pciLinkWidth > 0) {
          pcieInfo = QString("PCIe x%1").arg(gpu.pciLinkWidth);
        } else if (gpu.pcieLinkGen > 0) {
          pcieInfo = QString("PCIe Gen %1").arg(gpu.pcieLinkGen);
        }

        QLabel* pcieContent = new QLabel(pcieInfo);
        pcieContent->setStyleSheet("color: #0078d4;");

        pcieLayout->addWidget(pcieTitle);
        pcieLayout->addWidget(pcieContent);

        gpuGrid->addWidget(pcieBox, 2, 0, 1, 2);
      }

      gpuLayout->addWidget(gpuBox);
    }

    // Replace the basic label with our detailed content
    while (gpuWidget->getContentLayout()->count() > 0) {
      QLayoutItem* item = gpuWidget->getContentLayout()->takeAt(0);
      if (item->widget()) item->widget()->deleteLater();
      delete item;
    }
    gpuWidget->getContentLayout()->addWidget(gpuContent);
  } else {
    gpuInfoLabel->setText("<b>No dedicated graphics adapters detected.</b>");
  }

  // Storage Section
  storageWidget->setTitle("Storage");

  QWidget* storageContent = new QWidget();
  storageContent->setStyleSheet(
    "background-color: #252525; border-radius: 4px;");
  QVBoxLayout* storageLayout = new QVBoxLayout(storageContent);
  storageLayout->setSpacing(15);

  for (const auto& drive : info.drives) {
    QWidget* driveBox = new QWidget();
    driveBox->setStyleSheet("background-color: #252525; border-radius: 4px;");
    QGridLayout* driveGrid = new QGridLayout(driveBox);
    driveGrid->setSpacing(15);

    // Drive title and path
    QString drivePath = QString::fromStdString(drive.path);
    QString driveTitle =
      drive.isSystemDrive ? drivePath + " (System Drive)" : drivePath;
    // Use consistent blue for model name
    QWidget* pathBox = createMetricBox(
      driveTitle, QString::fromStdString(drive.model), "#0078d4");
    driveGrid->addWidget(pathBox, 0, 0, 1, 2);

    // Drive specs
    QWidget* totalBox = createMetricBox(
      "Total Capacity", QString::number(drive.totalSpaceGB) + " GB", "#0078d4");

    // Color free space based on percentage - green if >25%, yellow if >10%, red
    // if less
    double freePercentage =
      (double)drive.freeSpaceGB / drive.totalSpaceGB * 100.0;
    QString freeSpaceColor;
    if (freePercentage > 25) {
      freeSpaceColor = "#44FF44";  // Green for plenty of space
    } else if (freePercentage > 10) {
      freeSpaceColor = "#FFAA00";  // Yellow/orange for getting low
    } else {
      freeSpaceColor = "#FF6666";  // Red for very low
    }
    QWidget* freeBox = createMetricBox(
      "Free Space", QString::number(drive.freeSpaceGB) + " GB", freeSpaceColor);

    QString driveType = drive.isSSD ? "SSD" : "HDD";
    QString typeColor = drive.isSSD ? "#44FF44" : "#FFAA00";
    QWidget* typeBox = createMetricBox("Type", driveType, typeColor);

    QWidget* interfaceBox =
      createMetricBox("Interface", QString::fromStdString(drive.interfaceType));

    driveGrid->addWidget(totalBox, 1, 0);
    driveGrid->addWidget(freeBox, 1, 1);
    driveGrid->addWidget(typeBox, 2, 0);
    driveGrid->addWidget(interfaceBox, 2, 1);

    storageLayout->addWidget(driveBox);
  }

  // Replace the basic label with our detailed content
  while (storageWidget->getContentLayout()->count() > 0) {
    QLayoutItem* item = storageWidget->getContentLayout()->takeAt(0);
    if (item->widget()) item->widget()->deleteLater();
    delete item;
  }
  storageWidget->getContentLayout()->addWidget(storageContent);

  // System Section
  systemWidget->setTitle("System Information");

  QWidget* systemContent = new QWidget();
  systemContent->setStyleSheet(
    "background-color: #252525; border-radius: 4px;");
  QGridLayout* systemGrid = new QGridLayout(systemContent);
  systemGrid->setSpacing(15);

  // OS Information - Use consistent blue for all system info
  QString osVersion = QString::fromStdString(info.osVersion);
  if (info.isWindows11) {
    osVersion = "Windows 11";
  }
  if (!info.osBuildNumber.empty()) {
    osVersion += " (Build " + QString::fromStdString(info.osBuildNumber) + ")";
  }

  QWidget* osBox = createMetricBox("Operating System", osVersion, "#0078d4");
  systemGrid->addWidget(osBox, 0, 0, 1, 2);

  // Motherboard and BIOS info
  QWidget* motherboardBox =
    createMetricBox("Motherboard",
                    QString::fromStdString(info.motherboardManufacturer) + " " +
                      QString::fromStdString(info.motherboardModel),
                    "#0078d4");

  QString biosInfo = QString::fromStdString(info.biosVersion);
  if (!info.biosDate.empty()) {
    biosInfo += " (" + QString::fromStdString(info.biosDate) + ")";
  }
  QWidget* biosBox = createMetricBox("BIOS Version", biosInfo, "#0078d4");

  systemGrid->addWidget(motherboardBox, 1, 0);
  systemGrid->addWidget(biosBox, 1, 1);

  // Chipset info
  QWidget* chipsetBox = createMetricBox(
    "Chipset", QString::fromStdString(info.chipsetModel), "#0078d4");
  QString chipsetDriverInfo = QString::fromStdString(info.chipsetDriverVersion);
  QWidget* chipsetDriverBox =
    createMetricBox("Chipset Driver", chipsetDriverInfo, "#0078d4");

  systemGrid->addWidget(chipsetBox, 2, 0);
  systemGrid->addWidget(chipsetDriverBox, 2, 1);

  // Power settings
  QString powerPlan = QString::fromStdString(info.powerPlan);
  QString powerColor = info.powerPlanHighPerf ? "#44FF44" : "#FFAA00";
  QWidget* powerBox = createMetricBox("Power Plan", powerPlan, powerColor);

  QString gameMode = info.gameMode ? "Enabled" : "Disabled";
  QString gameModeColor = info.gameMode ? "#44FF44" : "#AAAAAA";
  QWidget* gameModeBox = createMetricBox("Game Mode", gameMode, gameModeColor);

  systemGrid->addWidget(powerBox, 3, 0);
  systemGrid->addWidget(gameModeBox, 3, 1);

  // Page file info
  if (info.pageFileExists) {
    QWidget* pageFileInfo = new QWidget();
    pageFileInfo->setStyleSheet(
      "background-color: #252525; border-radius: 4px;");
    QVBoxLayout* pageFileLayout = new QVBoxLayout(pageFileInfo);
    pageFileLayout->setContentsMargins(8, 8, 8, 8);

    QLabel* pageFileTitle = new QLabel("<b>Page File Configuration</b>");
    pageFileTitle->setStyleSheet("color: #0078d4; font-size: 14px;");

    QString managedText =
      info.pageFileSystemManaged ? "System Managed" : "Custom Size";
    QString locationsText = "";

    if (!info.pageFileLocations.empty()) {
      locationsText = "Locations: ";
      for (size_t i = 0; i < info.pageFileLocations.size(); i++) {
        if (i > 0) locationsText += ", ";
        locationsText += QString::fromStdString(info.pageFileLocations[i]);

        if (i < info.pageFileCurrentSizesMB.size() &&
            !info.pageFileSystemManaged) {
          locationsText +=
            QString(" (%1 MB)").arg(info.pageFileCurrentSizesMB[i]);
        }
      }
    }

    QLabel* pageFileContent = new QLabel(QString("Total Size: %1 MB<br>"
                                                 "Management: %2<br>%3")
                                           .arg(info.pageTotalSizeMB)
                                           .arg(managedText)
                                           .arg(locationsText));

    pageFileLayout->addWidget(pageFileTitle);
    pageFileLayout->addWidget(pageFileContent);

    systemGrid->addWidget(pageFileInfo, 4, 0, 1, 2);
  }

  // Replace the basic label with our detailed content
  while (systemWidget->getContentLayout()->count() > 0) {
    QLayoutItem* item = systemWidget->getContentLayout()->takeAt(0);
    if (item->widget()) item->widget()->deleteLater();
    delete item;
  }
  systemWidget->getContentLayout()->addWidget(systemContent);
}

QWidget* SystemInfoView::createInfoBox(const QString& title,
                                       QLabel* contentLabel) {
  QWidget* box = new QWidget();
  box->setStyleSheet("background-color: #252525; border-radius: 4px;");

  QVBoxLayout* layout = new QVBoxLayout(box);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(4);

  QLabel* titleLabel = new QLabel(title, box);
  titleLabel->setStyleSheet("color: #0078d4; font-size: 14px; font-weight: "
                            "bold; background: transparent;");
  layout->addWidget(titleLabel);

  if (contentLabel) {
    contentLabel->setTextFormat(Qt::RichText);
    contentLabel->setWordWrap(true);
    contentLabel->setStyleSheet("background: transparent;");
    layout->addWidget(contentLabel);
  }

  return box;
}

QWidget* SystemInfoView::createMetricBox(const QString& title,
                                         const QString& value,
                                         const QString& color) {
  QWidget* box = new QWidget();
  box->setStyleSheet("background-color: #292929; border-radius: 4px;");

  QVBoxLayout* layout = new QVBoxLayout(box);
  layout->setContentsMargins(8, 8, 8, 8);
  layout->setSpacing(4);

  // Create a single label with white title and colored value
  QLabel* contentLabel = new QLabel(
    QString("<span style='color: #ffffff; font-weight: bold;'>%1:</span> <span "
            "style='color: %2; font-weight: bold;'>%3</span>")
      .arg(title)
      .arg(color)
      .arg(value));

  contentLabel->setTextFormat(Qt::RichText);
  contentLabel->setWordWrap(true);
  contentLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  layout->addWidget(contentLabel);

  return box;
}

QWidget* SystemInfoView::createHardwareSpecsTable(
  const QStringList& headers, const QVector<QStringList>& rows,
  bool alternateColors) {
  QTableWidget* table = new QTableWidget(rows.size(), headers.size());
  table->setHorizontalHeaderLabels(headers);
  table->setStyleSheet(R"(
        QTableWidget {
            background-color: #292929;
            border: none;
            border-radius: 4px;
            color: #ffffff;
        }
        QHeaderView::section {
            background-color: #333333;
            color: #ffffff;
            border: none;
            padding: 4px;
        }
        QTableWidget::item {
            border: none;
            padding: 4px;
        }
        QTableWidget::item:alternate {
            background-color: #2d2d2d;
        }
    )");

  // Configure table properties
  table->setAlternatingRowColors(alternateColors);
  table->verticalHeader()->setVisible(false);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);

  // Set horizontal header resize mode
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

  // Populate table with data
  for (int row = 0; row < rows.size(); ++row) {
    const QStringList& rowData = rows[row];
    for (int col = 0; col < qMin(rowData.size(), headers.size()); ++col) {
      QTableWidgetItem* item = new QTableWidgetItem(rowData[col]);
      item->setTextAlignment(Qt::AlignCenter);
      table->setItem(row, col, item);
    }
  }

  // Set reasonable fixed height based on number of rows
  table->setFixedHeight(rows.size() * 30 + 30);  // 30px row height + header

  return table;
}
