#pragma once

#include <QObject>
#include <QString>

class AppNotificationBus : public QObject {
  Q_OBJECT

public:
  enum class Type {
    Success,
    Error,
    Warning,
    Info,
  };
  Q_ENUM(Type)

  static AppNotificationBus* instance();

  // Thread-safe convenience for callers.
  static void post(const QString& message, Type type = Type::Info, int durationMs = 5000);

signals:
  void notificationRequested(const QString& message, AppNotificationBus::Type type, int durationMs);

private:
  explicit AppNotificationBus(QObject* parent = nullptr);
};
