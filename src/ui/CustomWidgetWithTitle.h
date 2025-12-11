#pragma once

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

class CustomWidgetWithTitle : public QWidget {
  Q_OBJECT

 public:
  explicit CustomWidgetWithTitle(const QString& title,
                                 QWidget* parent = nullptr);
  QVBoxLayout* getContentLayout() { return contentLayout; }
  void setTitle(const QString& newTitle) { titleLabel->setText(newTitle); }
  static const QString TITLE_BG_COLOR;    // Title background color
  static const QString CONTENT_BG_COLOR;  // Content background color
  static const QString BORDER_COLOR;      // Border color

 private:
  QLabel* titleLabel;
  QVBoxLayout* mainLayout;
  QWidget* contentWidget;
  QVBoxLayout* contentLayout;
};
