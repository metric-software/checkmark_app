#pragma once

#include <QDialog>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

class TermsOfServiceDialog : public QDialog {
  Q_OBJECT

 public:
  explicit TermsOfServiceDialog(QWidget* parent = nullptr);
  ~TermsOfServiceDialog() override = default;

 private slots:
  void onAcceptClicked();
  void onDeclineClicked();

 private:
  void loadTermsOfService();
  QString getTermsText() const;

  QTextEdit* termsTextEdit;
  QPushButton* acceptButton;
  QPushButton* declineButton;
};
