#include "SilentNotificationBanner.h"

SilentNotificationBanner::SilentNotificationBanner(QWidget* parent)
    : QLabel(parent), isVisible(false) {
  setupStyles();
  
  // Set up the widget
  hide();
  setAlignment(Qt::AlignCenter);
  setFixedHeight(0);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  
  // Set up the slide animation
  slideAnimation = new QPropertyAnimation(this, "maximumHeight", this);
  slideAnimation->setDuration(300);
  connect(slideAnimation, &QPropertyAnimation::finished, 
          this, &SilentNotificationBanner::onHideAnimationFinished);
  
  // Set up the hide timer
  hideTimer = new QTimer(this);
  hideTimer->setSingleShot(true);
  connect(hideTimer, &QTimer::timeout, 
          this, &SilentNotificationBanner::hideNotification);
}

void SilentNotificationBanner::showNotification(const QString& message, NotificationType type, int duration) {
  setText(message);
  setStyleSheet(getStyleForType(type));
  
  // Stop any existing animations or timers
  slideAnimation->stop();
  hideTimer->stop();
  
  // Show and animate
  setMaximumHeight(0);
  show();
  
  slideAnimation->setStartValue(0);
  slideAnimation->setEndValue(40);
  slideAnimation->start();
  
  isVisible = true;
  
  // Set up auto-hide timer
  if (duration > 0) {
    hideTimer->start(duration);
  }
}

void SilentNotificationBanner::hideNotification() {
  if (!isVisible) return;
  
  slideAnimation->setStartValue(40);
  slideAnimation->setEndValue(0);
  slideAnimation->start();
  
  isVisible = false;
}

void SilentNotificationBanner::onHideAnimationFinished() {
  if (!isVisible) {
    hide();
  }
}

void SilentNotificationBanner::setupStyles() {
  // Base style will be set per notification type
}

QString SilentNotificationBanner::getStyleForType(NotificationType type) {
  QString baseStyle = R"(
    QLabel {
      color: white;
      padding: 8px;
      border-radius: 4px;
      font-size: 12px;
      background: %1;
    }
  )";
  
  switch (type) {
    case Success:
      return baseStyle.arg("#28a745");
    case Error:
      return baseStyle.arg("#dc3545");
    case Warning:
      return baseStyle.arg("#FF9900");
    case Info:
    default:
      return baseStyle.arg("#0078d4");
  }
}