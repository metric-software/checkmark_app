#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>

#include "../updates/UpdateManager.h"

class CriticalUpdateDialog : public QDialog {
  Q_OBJECT
 public:
  explicit CriticalUpdateDialog(const UpdateStatus& status, QWidget* parent = nullptr);

 signals:
  void updateSelected();
  void skipSelected();

 private:
  void buildUi(const UpdateStatus& status);
};
