#pragma once

#include <QGridLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include "CustomWidgetWithTitle.h"
#include "hardware/ConstantSystemInfo.h"

class SystemInfoView : public QWidget {
  Q_OBJECT

 public:
  explicit SystemInfoView(QWidget* parent = nullptr);
  ~SystemInfoView();

 private:
  void setupLayout();
  void displaySystemInfo();
  QWidget* createInfoBox(const QString& title, QLabel* contentLabel);
  QWidget* createMetricBox(const QString& title, const QString& value,
                           const QString& color = "#0078d4");
  QWidget* createHardwareSpecsTable(const QStringList& headers,
                                    const QVector<QStringList>& rows,
                                    bool alternateColors = true);

  // Main layout components
  QVBoxLayout* mainLayout;
  QScrollArea* scrollArea;

  // Section containers
  CustomWidgetWithTitle* cpuWidget;
  CustomWidgetWithTitle* memoryWidget;
  CustomWidgetWithTitle* gpuWidget;
  CustomWidgetWithTitle* storageWidget;
  CustomWidgetWithTitle* systemWidget;

  // Labels for content
  QLabel* cpuInfoLabel;
  QLabel* memoryInfoLabel;
  QLabel* gpuInfoLabel;
  QLabel* storageInfoLabel;
  QLabel* systemInfoLabel;
};
