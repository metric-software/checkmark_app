#include "CriticalUpdateDialog.h"

#include <QDesktopServices>
#include <QFont>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

CriticalUpdateDialog::CriticalUpdateDialog(const UpdateStatus& status, QWidget* parent)
    : QDialog(parent) {
  setWindowTitle("Critical update available");
  setModal(true);
  setMinimumSize(480, 340);
  setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                 Qt::WindowCloseButtonHint);

  buildUi(status);
}

void CriticalUpdateDialog::buildUi(const UpdateStatus& status) {
  auto* layout = new QVBoxLayout(this);

  auto* title = new QLabel("Critical update available — highly recommended", this);
  QFont titleFont = title->font();
  titleFont.setBold(true);
  titleFont.setPointSize(titleFont.pointSize() + 2);
  title->setFont(titleFont);
  layout->addWidget(title);

  const QString versionText = status.latestVersion.isEmpty()
                                ? status.currentVersion
                                : status.latestVersion;
  auto* subtitle = new QLabel(
    QStringLiteral("Version %1 is available. This release is marked critical; "
                   "installing is highly recommended before continuing.")
      .arg(versionText),
    this);
  subtitle->setWordWrap(true);
  layout->addWidget(subtitle);

  if (!status.releaseNotesLink.isEmpty()) {
    auto* link = new QLabel(QStringLiteral("<a href=\"%1\">View release notes</a>")
                              .arg(status.releaseNotesLink.toHtmlEscaped()),
                            this);
    link->setTextFormat(Qt::RichText);
    link->setTextInteractionFlags(Qt::TextBrowserInteraction);
    link->setOpenExternalLinks(true);
    layout->addWidget(link);
  }

  auto* notes = new QTextEdit(this);
  notes->setReadOnly(true);
  notes->setMinimumHeight(140);
  notes->setStyleSheet(
    "QTextEdit { background: #1f1f1f; color: #f0f0f0; border: 1px solid #333; }");
  QString notesText = status.releaseNotes;
  if (notesText.isEmpty()) {
    notesText = QStringLiteral(
      "We detected a critical update while you are running %1.\n\n"
      "Updating ensures you have the latest fixes and protections. "
      "You can continue without updating, but we strongly recommend installing now.")
                  .arg(status.currentVersion);
  }
  notes->setPlainText(notesText);
  layout->addWidget(notes);

  auto* buttonsLayout = new QHBoxLayout();
  auto* skipButton = new QPushButton("Continue without updating", this);
  skipButton->setStyleSheet(
    "QPushButton { background-color: #3a3a3a; color: #ffffff; border: none; "
    "padding: 8px 14px; border-radius: 4px; } "
    "QPushButton:hover { background-color: #4a4a4a; }");

  auto* updateButton = new QPushButton("Update now", this);
  updateButton->setStyleSheet(
    "QPushButton { background-color: #d83b01; color: #ffffff; border: none; "
    "padding: 10px 16px; border-radius: 4px; font-weight: bold; } "
    "QPushButton:hover { background-color: #e15b2a; }");

  connect(skipButton, &QPushButton::clicked, this, [this]() {
    emit skipSelected();
    accept();
  });
  connect(updateButton, &QPushButton::clicked, this, [this]() {
    emit updateSelected();
    accept();
  });

  buttonsLayout->addStretch();
  buttonsLayout->addWidget(skipButton);
  buttonsLayout->addWidget(updateButton);

  layout->addLayout(buttonsLayout);
}
