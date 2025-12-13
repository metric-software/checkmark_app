#pragma once

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QWidget>

#include "SilentNotificationBanner.h"
#include "../updates/UpdateManager.h"

class UpdateCenterView : public QWidget {
  Q_OBJECT
 public:
  explicit UpdateCenterView(QWidget* parent = nullptr);

 private slots:
  void handleStatusChanged(const UpdateStatus& status);
  void handleCheckClicked();
  void handleUpdateClicked();
  void handleDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
  void handleDownloadFinished(const QString& installerPath);
  void handleDownloadFailed(const QString& error);

 private:
  void applyTierStyle(UpdateTier tier);
  void updateReleaseNotesButton(const UpdateStatus& status);
  void setProgressVisible(bool visible);

  UpdateStatus lastStatus_;
  QLabel* statusTitle_ = nullptr;
  QLabel* currentVersionLabel_ = nullptr;
  QLabel* latestVersionLabel_ = nullptr;
  QLabel* messageLabel_ = nullptr;
  QPushButton* checkButton_ = nullptr;
  QPushButton* updateButton_ = nullptr;
  QPushButton* releaseNotesButton_ = nullptr;
  QProgressBar* progressBar_ = nullptr;
  QLabel* progressLabel_ = nullptr;
  SilentNotificationBanner* banner_ = nullptr;
};
