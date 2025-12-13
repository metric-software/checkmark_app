#include "UpdateCenterView.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

#include "../logging/Logger.h"

UpdateCenterView::UpdateCenterView(QWidget* parent) : QWidget(parent) {
  LOG_INFO << "[startup] UpdateCenterView: ctor begin";
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(20, 20, 20, 20);
  rootLayout->setSpacing(16);

  banner_ = new SilentNotificationBanner(this);
  banner_->hide();
  rootLayout->addWidget(banner_);

  statusTitle_ = new QLabel("Updates", this);
  QFont titleFont = statusTitle_->font();
  titleFont.setBold(true);
  titleFont.setPointSize(titleFont.pointSize() + 4);
  statusTitle_->setFont(titleFont);
  rootLayout->addWidget(statusTitle_);

  auto* statusCard = new QFrame(this);
  statusCard->setObjectName("updateStatusCard");
  statusCard->setStyleSheet(
    "#updateStatusCard { background-color: #1f1f1f; border: 1px solid #2f2f2f; "
    "border-radius: 8px; padding: 16px; } "
    "#updateStatusCard QLabel { color: #f2f2f2; }");

  auto* cardLayout = new QVBoxLayout(statusCard);
  currentVersionLabel_ = new QLabel(this);
  latestVersionLabel_ = new QLabel(this);
  messageLabel_ = new QLabel(this);
  messageLabel_->setStyleSheet("color: #c7c7c7;");

  cardLayout->addWidget(currentVersionLabel_);
  cardLayout->addWidget(latestVersionLabel_);
  cardLayout->addWidget(messageLabel_);

  auto* buttonsLayout = new QHBoxLayout();
  updateButton_ = new QPushButton("Download && install", this);
  updateButton_->setCursor(Qt::PointingHandCursor);
  updateButton_->setStyleSheet(
    "QPushButton { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, "
    "stop:0 #3a7bd5, stop:1 #00d2ff); color: white; border: none; "
    "padding: 10px 18px; border-radius: 6px; font-weight: bold; } "
    "QPushButton:disabled { background: #444; color: #888; }");

  checkButton_ = new QPushButton("Check now", this);
  checkButton_->setCursor(Qt::PointingHandCursor);
  checkButton_->setStyleSheet(
    "QPushButton { background-color: #2f2f2f; color: #f2f2f2; border: 1px solid #3f3f3f; "
    "padding: 8px 14px; border-radius: 6px; } "
    "QPushButton:hover { background-color: #3a3a3a; }");

  releaseNotesButton_ = new QPushButton("View release notes", this);
  releaseNotesButton_->setCursor(Qt::PointingHandCursor);
  releaseNotesButton_->setFlat(true);
  releaseNotesButton_->setStyleSheet(
    "QPushButton { color: #4aa3ff; text-decoration: underline; background: transparent; border: none; } "
    "QPushButton:disabled { color: #666666; text-decoration: none; }");
  releaseNotesButton_->setVisible(false);

  buttonsLayout->addWidget(updateButton_);
  buttonsLayout->addWidget(checkButton_);
  buttonsLayout->addWidget(releaseNotesButton_);
  buttonsLayout->addStretch(1);
  cardLayout->addLayout(buttonsLayout);

  progressBar_ = new QProgressBar(this);
  progressBar_->setRange(0, 100);
  progressBar_->setValue(0);
  progressBar_->setTextVisible(false);
  progressBar_->setStyleSheet(
    "QProgressBar { background: #2b2b2b; border: 1px solid #3f3f3f; border-radius: 4px; } "
    "QProgressBar::chunk { background: #4aa3ff; border-radius: 4px; }");
  progressLabel_ = new QLabel(this);
  progressLabel_->setStyleSheet("color: #c7c7c7;");
  setProgressVisible(false);

  cardLayout->addWidget(progressBar_);
  cardLayout->addWidget(progressLabel_);

  rootLayout->addWidget(statusCard);
  rootLayout->addStretch(1);

  connect(checkButton_, &QPushButton::clicked, this, &UpdateCenterView::handleCheckClicked);
  connect(updateButton_, &QPushButton::clicked, this, &UpdateCenterView::handleUpdateClicked);
  connect(releaseNotesButton_, &QPushButton::clicked, this, [this]() {
    if (lastStatus_.releaseNotesLink.isEmpty()) return;
    QDesktopServices::openUrl(QUrl(lastStatus_.releaseNotesLink));
  });

  auto& manager = UpdateManager::getInstance();
  connect(&manager, &UpdateManager::statusChanged, this, &UpdateCenterView::handleStatusChanged);
  connect(&manager, &UpdateManager::downloadProgress, this, &UpdateCenterView::handleDownloadProgress);
  connect(&manager, &UpdateManager::downloadFinished, this, &UpdateCenterView::handleDownloadFinished);
  connect(&manager, &UpdateManager::downloadFailed, this, &UpdateCenterView::handleDownloadFailed);

  handleStatusChanged(manager.lastKnownStatus());
  LOG_INFO << "[startup] UpdateCenterView: ctor end";
}

void UpdateCenterView::handleStatusChanged(const UpdateStatus& status) {
  lastStatus_ = status;
  checkButton_->setEnabled(true);
  checkButton_->setText("Check now");
  currentVersionLabel_->setText(QStringLiteral("Current version: %1").arg(status.currentVersion));
  const QString latest = status.latestVersion.isEmpty() ? QStringLiteral("n/a") : status.latestVersion;
  latestVersionLabel_->setText(QStringLiteral("Latest available: %1").arg(latest));
  messageLabel_->setText(status.statusMessage);

  applyTierStyle(status.tier);

  updateButton_->setEnabled(status.hasUpdate());
  updateButton_->setText(status.tier == UpdateTier::Critical
                           ? QStringLiteral("Install critical update")
                           : QStringLiteral("Download && install"));
  updateReleaseNotesButton(status);

  if (status.offline) {
    banner_->showNotification("Offline mode enabled - update checks paused",
                              SilentNotificationBanner::Info);
  } else if (status.tier == UpdateTier::Critical) {
    banner_->showNotification("Critical update available",
                              SilentNotificationBanner::Warning);
  } else {
    banner_->hideNotification();
  }
}

void UpdateCenterView::handleCheckClicked() {
  banner_->hideNotification();
  checkButton_->setEnabled(false);
  checkButton_->setText("Checking...");
  UpdateManager::getInstance().checkForUpdates(true);
}

void UpdateCenterView::handleUpdateClicked() {
  setProgressVisible(true);
  progressLabel_->setText("Downloading installer...");
  progressBar_->setValue(0);
  UpdateManager::getInstance().downloadAndInstallLatest();
}

void UpdateCenterView::handleDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
  if (bytesTotal <= 0) return;
  const int percent = static_cast<int>((bytesReceived * 100) / bytesTotal);
  progressBar_->setValue(percent);
  progressLabel_->setText(
    QStringLiteral("Downloading... %1% (%2 / %3 MB)")
      .arg(percent)
      .arg(QString::number(bytesReceived / (1024 * 1024.0), 'f', 1))
      .arg(QString::number(bytesTotal / (1024 * 1024.0), 'f', 1)));
}

void UpdateCenterView::handleDownloadFinished(const QString& installerPath) {
  progressLabel_->setText(QStringLiteral("Installer downloaded - launching..."));
  banner_->showNotification(
    QStringLiteral("Installing from %1").arg(installerPath),
    SilentNotificationBanner::Success);
  setProgressVisible(false);
}

void UpdateCenterView::handleDownloadFailed(const QString& error) {
  banner_->showNotification(QStringLiteral("Update download failed: %1").arg(error),
                            SilentNotificationBanner::Error);
  setProgressVisible(false);
  checkButton_->setEnabled(true);
  checkButton_->setText("Check now");
}

void UpdateCenterView::applyTierStyle(UpdateTier tier) {
  QString color;
  switch (tier) {
    case UpdateTier::Critical:
      color = "#ff4d4f";
      break;
    case UpdateTier::Suggestion:
      color = "#ffb347";
      break;
    case UpdateTier::UpToDate:
      color = "#4aa3ff";
      break;
    case UpdateTier::Unknown:
    default:
      color = "#c7c7c7";
      break;
  }
  statusTitle_->setStyleSheet(QStringLiteral("color: %1;").arg(color));
  messageLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(color));
}

void UpdateCenterView::updateReleaseNotesButton(const UpdateStatus& status) {
  const bool hasLink = !status.releaseNotesLink.isEmpty();
  releaseNotesButton_->setVisible(hasLink);
  releaseNotesButton_->setEnabled(hasLink);
}

void UpdateCenterView::setProgressVisible(bool visible) {
  progressBar_->setVisible(visible);
  progressLabel_->setVisible(visible);
  if (!visible) {
    progressBar_->setValue(0);
  }
  checkButton_->setEnabled(!visible);
}
