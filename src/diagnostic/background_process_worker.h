#pragma once
#include <QAtomicInt>
#include <QObject>
#include <QThread>

#include "background_process_monitor.h"

class BackgroundProcessWorker : public QObject {
  Q_OBJECT
 public:
  explicit BackgroundProcessWorker(QObject* parent = nullptr)
      : QObject(parent), m_isCancelled(0) {}

  // Add method to cancel operation
  void cancelOperation() { m_isCancelled.storeRelease(1); }

  // Add method to check if operation is cancelled
  bool isCancelled() const { return m_isCancelled.loadAcquire() != 0; }

 public slots:
  void startMonitoring(int durationSeconds) {
    // Reset the cancel flag before starting
    m_isCancelled.storeRelease(0);

    // This runs in the worker thread
    auto result = BackgroundProcessMonitor::monitorBackgroundProcesses(
      durationSeconds, this);

    // Only emit result if not cancelled
    if (!isCancelled()) {
      emit monitoringFinished(result);
    }
  }

 signals:
  void monitoringFinished(BackgroundProcessMonitor::MonitoringResult result);

 private:
  QAtomicInt m_isCancelled;
};
