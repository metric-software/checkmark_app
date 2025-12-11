#pragma once

#include <QLabel>
#include <QString>
#include <QWidget>



namespace DiagnosticRenderers {

class BackgroundProcessRenderer {
 public:
  // Parse and render background process results
  static QString renderBackgroundProcessResults(const QString& result);

 private:
  // Helper methods for parsing result details
  static void parseSystemResourceInfo(const QString& result, double& cpuUsage,
                                      double& gpuUsage, double& dpcTime,
                                      double& intTime, double& diskIO);
};

}  // namespace DiagnosticRenderers
