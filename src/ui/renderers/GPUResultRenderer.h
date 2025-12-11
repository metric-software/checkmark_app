#ifndef GPURESULTRENDERER_H
#define GPURESULTRENDERER_H

#include <map>

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include "DiagnosticViewComponents.h"  // Add this include for DiagnosticViewComponents

// Include for network types - need full definition for method signatures
#include "network/api/DownloadApiClient.h"

namespace DiagnosticRenderers {

// Add comparison data structure for GPU
struct GPUComparisonData {
  QString model;
  QString fullModel;
  QString vendor;
  int vramMB;
  QString driverVersion;
  int pcieGen;
  int pciLinkWidth;

  // Performance metrics
  double fps;
  int frames;
};

class GPUResultRenderer {
 public:
  // Create the main GPU results widget from raw result data
  static QWidget* createGPUResultWidget(const QString& result, const MenuData* networkMenuData = nullptr, DownloadApiClient* downloadClient = nullptr);

  // Network-based methods
  static GPUComparisonData convertNetworkDataToGPU(const ComponentData& networkData);
  static std::map<QString, GPUComparisonData> createDropdownDataFromMenu(const MenuData& menuData);

 private:
  // Helper methods
  static QWidget* createGpuMetricBox(const QString& title, const QString& value,
                                     const QString& color = "#0078d4");
  static QWidget* createFpsGauge(float fps);

  // Add comparison data methods
  static std::map<QString, GPUComparisonData> loadGPUComparisonData();
  static QComboBox* createGPUComparisonDropdown(
    const std::map<QString, GPUComparisonData>& comparisonData,
    QWidget* containerWidget, const QPair<double, double>& fpsVals,
    DownloadApiClient* downloadClient = nullptr);

  // Add this method declaration to the GPUResultRenderer class
  static std::map<QString, DiagnosticViewComponents::AggregatedComponentData<GPUComparisonData>> generateAggregatedGPUData(
    const std::map<QString, GPUComparisonData>& individualData);
};

}  // namespace DiagnosticRenderers

#endif  // GPURESULTRENDERER_H
