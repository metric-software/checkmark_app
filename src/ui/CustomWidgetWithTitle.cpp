#include "CustomWidgetWithTitle.h"

// Define static color constants
const QString CustomWidgetWithTitle::TITLE_BG_COLOR = "#2d2d2d";
const QString CustomWidgetWithTitle::CONTENT_BG_COLOR = "#242424";
const QString CustomWidgetWithTitle::BORDER_COLOR = "#333333";

CustomWidgetWithTitle::CustomWidgetWithTitle(const QString& title,
                                             QWidget* parent)
    : QWidget(parent) {
  // Create the main layout
  mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(1, 1, 1, 1);  // Small margin for border
  mainLayout->setSpacing(0);

  // Create title container widget to ensure full width
  QWidget* titleContainer = new QWidget(this);
  titleContainer->setObjectName("titleContainer");
  titleContainer->setStyleSheet(QString(R"(
        #titleContainer {
            background-color: %1;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
    )")
                                  .arg(TITLE_BG_COLOR));

  // Create title layout
  QHBoxLayout* titleLayout = new QHBoxLayout(titleContainer);
  titleLayout->setContentsMargins(12, 8, 12, 8);

  // Create the title label
  titleLabel = new QLabel(title, titleContainer);
  titleLabel->setStyleSheet(R"(
        QLabel {
            color: #ffffff;
            font-weight: bold;
            background-color: transparent;
        }
    )");

  // Add label to title layout
  titleLayout->addWidget(titleLabel);

  // Create content widget
  contentWidget = new QWidget(this);
  contentWidget->setObjectName("contentWidget");
  contentWidget->setStyleSheet(QString(R"(
        #contentWidget {
            background-color: %1;
            border-bottom-left-radius: 4px;
            border-bottom-right-radius: 4px;
        }
        QLabel {
            background-color: %1;
            color: #ffffff;
        }
    )")
                                 .arg(CONTENT_BG_COLOR));

  // Create content layout
  contentLayout = new QVBoxLayout(contentWidget);
  contentLayout->setContentsMargins(12, 12, 12, 12);

  // Add widgets to main layout
  mainLayout->addWidget(titleContainer);
  mainLayout->addWidget(contentWidget);

  // Set widget style
  setStyleSheet(QString(R"(
        CustomWidgetWithTitle {
            border: 1px solid %1;
            border-radius: 4px;
        }
    )")
                  .arg(BORDER_COLOR));
}
