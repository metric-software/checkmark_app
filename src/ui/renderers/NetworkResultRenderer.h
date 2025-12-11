#pragma once

#include <QString>
#include <QWidget>

namespace DiagnosticRenderers {

class NetworkResultRenderer {
 public:
  static QWidget* createNetworkResultWidget(const QString& result);

 private:
  static QWidget* createMetricBox(const QString& title, const QString& value,
                                  const QString& color = "#0078d4");
  static QWidget* createLatencyBox(const QString& title, double latency,
                                   const QString& color);
  static QString getColorForLatency(double latency);
  static QWidget* createPacketLossBox(double packetLoss);
  static QWidget* createBufferbloatSection(const QString& result);
  static QString extractValueFromResult(const QString& result,
                                        const QString& keyword);
};

}  // namespace DiagnosticRenderers
