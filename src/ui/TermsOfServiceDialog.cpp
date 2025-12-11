#include "TermsOfServiceDialog.h"

#include <QFile>
#include <QLabel>
#include <QMessageBox>

#include "../ApplicationSettings.h"

TermsOfServiceDialog::TermsOfServiceDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("Terms of Service");
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
  setMinimumSize(600, 500);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Header
  QLabel* headerLabel = new QLabel("Please read the Terms of Service", this);
  QFont headerFont = headerLabel->font();
  headerFont.setBold(true);
  headerFont.setPointSize(headerFont.pointSize() + 2);
  headerLabel->setFont(headerFont);
  mainLayout->addWidget(headerLabel);

  // Terms text area
  termsTextEdit = new QTextEdit(this);
  termsTextEdit->setReadOnly(true);
  termsTextEdit->setStyleSheet(
    "background-color: #252525; color: #e0e0e0; border: 1px solid #333333;");
  loadTermsOfService();
  mainLayout->addWidget(termsTextEdit);

  // Buttons
  QHBoxLayout* buttonLayout = new QHBoxLayout();
  acceptButton = new QPushButton("Accept", this);
  declineButton = new QPushButton("Decline", this);

  acceptButton->setStyleSheet("background-color: #0078d4; color: white;");
  declineButton->setStyleSheet("background-color: #d83b01; color: white;");

  connect(acceptButton, &QPushButton::clicked, this,
          &TermsOfServiceDialog::onAcceptClicked);
  connect(declineButton, &QPushButton::clicked, this,
          &TermsOfServiceDialog::onDeclineClicked);

  buttonLayout->addStretch();
  buttonLayout->addWidget(declineButton);
  buttonLayout->addWidget(acceptButton);

  mainLayout->addLayout(buttonLayout);

  setLayout(mainLayout);
}

void TermsOfServiceDialog::loadTermsOfService() {
  // Try to load from file first
  QFile file(":/terms_of_service.txt");
  if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    termsTextEdit->setPlainText(QString::fromUtf8(file.readAll()));
    file.close();
  } else {
    // Fallback to hardcoded terms
    termsTextEdit->setPlainText(getTermsText());
  }
}

QString TermsOfServiceDialog::getTermsText() const {
  return "TERMS OF SERVICE\n\n"
         "Last Updated: March 30, 2025\n\n"
         "1. ACCEPTANCE OF TERMS\n\n"
         "By using this application, you agree to be bound by these Terms of "
         "Service.\n\n"
         "2. DESCRIPTION OF SERVICE\n\n"
         "This application provides system diagnostics, benchmarking, and "
         "optimization tools for your computer.\n\n"
         "3. USER CONDUCT\n\n"
         "You agree to use this application only for lawful purposes and in a "
         "way that does not infringe the rights of any third party.\n\n"
         "4. PRIVACY\n\n"
         "Our application may collect system information for diagnostic "
         "purposes. This information is only used to provide the requested "
         "services.\n\n"
         "5. DISCLAIMER OF WARRANTIES\n\n"
         "THIS SOFTWARE IS PROVIDED \"AS IS\" WITHOUT WARRANTY OF ANY KIND. "
         "THE CREATORS DISCLAIM ALL WARRANTIES, EITHER EXPRESS OR IMPLIED, "
         "INCLUDING BUT NOT LIMITED TO IMPLIED WARRANTIES OF MERCHANTABILITY "
         "AND FITNESS FOR A PARTICULAR PURPOSE.\n\n"
         "6. LIMITATION OF LIABILITY\n\n"
         "IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY "
         "CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, "
         "TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE "
         "SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n\n"
         "7. INDEMNIFICATION\n\n"
         "You agree to indemnify and hold harmless the creators of this "
         "application from any claims resulting from your use of the "
         "application.\n\n"
         "8. TERMINATION\n\n"
         "We reserve the right to terminate your access to the application at "
         "any time without notice.\n\n"
         "9. CHANGES TO TERMS\n\n"
         "We reserve the right to modify these terms at any time. Your "
         "continued use of the application after such changes constitutes your "
         "acceptance of the new terms.\n\n"
         "10. GOVERNING LAW\n\n"
         "These terms shall be governed by and construed in accordance with "
         "applicable laws.\n\n";
}

void TermsOfServiceDialog::onAcceptClicked() {
  ApplicationSettings::getInstance().setTermsAccepted(true);
  accept();
}

void TermsOfServiceDialog::onDeclineClicked() {
  QMessageBox::warning(
    this, "Terms Declined",
    "You must accept the Terms of Service to use this application.");
  reject();
}
