#include "DetailedGuideDialog.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

DetailedGuideDialog::DetailedGuideDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle("Benchmark Detailed Guide");
  setMinimumSize(600, 500);

  setupUI();
}

void DetailedGuideDialog::setupUI() {
  QVBoxLayout* mainLayout = new QVBoxLayout(this);

  // Create text browser for the content
  textBrowser = new QTextBrowser(this);
  textBrowser->setOpenExternalLinks(true);
  textBrowser->setReadOnly(true);
  textBrowser->setStyleSheet("QTextBrowser { background-color: #252525; color: "
                             "#ffffff; border: 1px solid #383838; }");

  // Set the guide text with proper HTML formatting
  QString guideText =
    "<p>This application will measure your PCs peformance in Rust, by running "
    "rusts own benchmark run with our demo file. "
    "And then it measures system metrics on the background to get a more "
    "detailed result.</p>"

    "<h3>Game settings and Results</h3>"

    "<p>- If your goal is to see how good your performance in game is, we "
    "recommend you use your normal settings for the run.</p>"

    "<p>- If you want to figure out if changing some settings, changes your "
    "fps we recommend you do multiple runs with both settings, "
    "to get accurate data on the impact of the settings. We also recommend "
    "that you switch the setting back and forth between every run. "
    "Since sometimes the results might get slightly worse over time or the "
    "first results act as \"warm up\" for the system, "
    "and the subsequent results are better.</p>"

    "<p>- OVERALL EVEN WITH THE SAME SYSTEM AND SETTINGS, THE RESULTS ARE NOT "
    "VERY CONSISTENT. "
    "If you have questions about the results contact us.</p>"

    "<h3>Info about the Instructions</h3>"

    "<p>1. First you need to add the demo file into the demos folder in rust. "
    "The demo is a pre recorded gameplay event in rust, "
    "where we fly trough the terrain, to test how the system performs in "
    "different events.</p>"

    "<p>2. For the detailed metrics to be collected you need to start the "
    "monitoring in the application before the actual benchmark starts ingame. "
    "The correct duration will be found at the end of the run, and the data "
    "from before the run will be removed from the analysis.</p>"

    "<p>3. The consle command will start the benchmark in game. Depending on "
    "your system, the run should take in total about 3-5 minutes.</p>"

    "<p>4. While the benchmark is running, try to avoid doing anything else on "
    "the PC at the same time for the most accurate result.</p>";

  textBrowser->setHtml(guideText);

  // Create close button
  QPushButton* closeButton = new QPushButton("Close", this);
  closeButton->setStyleSheet(R"(
        QPushButton {
            background-color: #333333;
            color: white;
            border: none;
            padding: 8px 16px;
            border-radius: 4px;
        }
        QPushButton:hover { background-color: #404040; }
        QPushButton:pressed { background-color: #292929; }
    )");

  // Add widgets to layout
  mainLayout->addWidget(textBrowser);

  QHBoxLayout* buttonLayout = new QHBoxLayout();
  buttonLayout->addStretch();
  buttonLayout->addWidget(closeButton);

  mainLayout->addLayout(buttonLayout);

  // Connect close button
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}
