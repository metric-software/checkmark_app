#include "AppNotificationBus.h"

#include <QCoreApplication>

AppNotificationBus::AppNotificationBus(QObject* parent)
  : QObject(parent) {
}

AppNotificationBus* AppNotificationBus::instance() {
  static AppNotificationBus* inst = nullptr;
  if (!inst) {
    inst = new AppNotificationBus(QCoreApplication::instance());
  }
  return inst;
}

void AppNotificationBus::post(const QString& message, Type type, int durationMs) {
  // Emitting from worker threads is OK; receivers in the GUI thread will
  // get it via queued delivery.
  emit instance()->notificationRequested(message, type, durationMs);
}
