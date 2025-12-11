#pragma once

#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

class SilentNotificationBanner : public QLabel {
  Q_OBJECT

public:
  enum NotificationType {
    Success,    // Green background
    Error,      // Red background  
    Warning,    // Orange background
    Info        // Blue background
  };

  explicit SilentNotificationBanner(QWidget* parent = nullptr);
  
  void showNotification(const QString& message, NotificationType type = Info, int duration = 5000);
  void hideNotification();

private slots:
  void onHideAnimationFinished();

private:
  void setupStyles();
  QString getStyleForType(NotificationType type);
  
  QPropertyAnimation* slideAnimation;
  QTimer* hideTimer;
  bool isVisible;
};