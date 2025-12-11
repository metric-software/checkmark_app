#pragma once

#include <QDialog>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

class DetailedGuideDialog : public QDialog {
  Q_OBJECT

 public:
  explicit DetailedGuideDialog(QWidget* parent = nullptr);

 private:
  void setupUI();

  QTextBrowser* textBrowser;
};
