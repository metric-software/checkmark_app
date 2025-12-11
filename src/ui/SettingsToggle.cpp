#include "SettingsToggle.h"

#include <QMouseEvent>
#include <QStyle>
#include <QTimer>

class ClickableSlider : public QSlider {
 public:
  ClickableSlider(Qt::Orientation orientation, QWidget* parent = nullptr)
      : QSlider(orientation, parent) {
    // Immediately prevent default Qt styling
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
      "background-color: transparent;");  // Remove default background

    // Initial styling based on default state (off)
    updateStyle(false);
  }

  void updateStyle(bool enabled) {
    // Complete style override to prevent Qt's default styles
    QString styleSheet = QString(R"(
            /* Base slider styling */
            QSlider {
                background: transparent;
                border: none;
                min-height: 18px;
            }
            /* Remove default focus styling */
            QSlider::focus {
                border: none;
                outline: none;
            }
            /* Main track styling */
            QSlider::groove:horizontal {
                height: 18px;
                background: %1;
                border-radius: 9px;
                margin: 0px;
                border: none;
            }
            /* Handle (thumb) styling */
            QSlider::handle:horizontal {
                width: 18px;
                height: 18px;
                background: #FFFFFF;
                border-radius: 9px;
                border: 1px solid #333333;
                margin: 0px;
            }
            /* Ensure sub-controls are styled consistently */
            QSlider::sub-page:horizontal {
                background: transparent;
                border: none;
            }
            QSlider::add-page:horizontal {
                background: transparent;
                border: none;
            }
        )")
                           .arg(enabled ? "#0078d4" : "#555555");

    setStyleSheet(styleSheet);

    // Set cursor to indicate this is clickable
    setCursor(Qt::PointingHandCursor);
  }

  void updateDisabledStyle(bool disabled) {
    // Get current state
    bool enabled = value() == 1;

    // Complete style override with more visually distinct disabled state
    // Ensures we maintain our custom styling and don't revert to Qt defaults
    QString styleSheet =
      QString(R"(
            /* Base slider styling */
            QSlider {
                background: transparent;
                border: none;
                min-height: 18px;
            }
            /* Remove default focus styling */
            QSlider::focus {
                border: none;
                outline: none;
            }
            /* Main track styling */
            QSlider::groove:horizontal {
                height: 18px;
                background: %1;
                border-radius: 9px;
                margin: 0px;
                border: 1px solid %2;
            }
            /* Handle (thumb) styling */
            QSlider::handle:horizontal {
                width: 18px;
                height: 18px;
                background: %3;
                border-radius: 9px;
                border: 1px solid %4;
                margin: 0px;
            }
            /* Ensure sub-controls are styled consistently */
            QSlider::sub-page:horizontal {
                background: transparent;
                border: none;
            }
            QSlider::add-page:horizontal {
                background: transparent;
                border: none;
            }
        )")
        .arg(disabled
               ? "#303030"
               : (enabled ? "#0078d4"
                          : "#555555"),  // Much darker background when disabled
             disabled ? "#444444" : "#222222",  // Border for groove
             disabled ? "#888888" : "#FFFFFF",  // Grey handle when disabled
             disabled ? "#555555"
                      : "#333333"  // Darker border for handle when disabled
        );

    setStyleSheet(styleSheet);

    // Also modify the widget opacity property to enhance disabled appearance
    if (disabled) {
      setProperty("opacity", 0.6);
    } else {
      setProperty("opacity", 1.0);
    }

    // Ensure style is applied immediately
    style()->polish(this);
    update();

    // Additional visual cue - make cursor indicate non-interactive state
    setCursor(disabled ? Qt::ForbiddenCursor : Qt::PointingHandCursor);
  }

 protected:
  void showEvent(QShowEvent* event) override {
    // Ensure style is refreshed when widget becomes visible
    updateStyle(value() == 1);
    QSlider::showEvent(event);
  }

  void resizeEvent(QResizeEvent* event) override {
    // Refresh style on resize to ensure proper rendering
    updateStyle(value() == 1);
    QSlider::resizeEvent(event);
  }

  void mousePressEvent(QMouseEvent* event) override {
    // Only handle mouse events if the slider is enabled
    if (!isEnabled()) {
      event->ignore();
      return;
    }

    if (event->button() == Qt::LeftButton) {
      // Toggle value between 0 and 1
      bool newState = value() == 0;
      setValue(newState ? 1 : 0);

      // Update style immediately
      updateStyle(newState);

      event->accept();
    } else {
      QSlider::mousePressEvent(event);
    }
  }
};

SettingsToggle::SettingsToggle(const QString& id, const QString& name,
                               const QString& description, QWidget* parent)
    : QWidget(parent), settingId(id) {
  // Set consistent margins and padding
  setContentsMargins(0, 0, 0, 0);

  // Main layout with reduced margins
  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(2);  // Small spacing between elements

  // Top layout for name and toggle
  topLayout = new QHBoxLayout();
  topLayout->setContentsMargins(0, 0, 0, 0);
  topLayout->setSpacing(5);  // Consistent spacing

  // Create name label only if name is not empty
  if (!name.isEmpty()) {
    nameLabel = new QLabel(name, this);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    nameLabel->setAlignment(Qt::AlignVCenter |
                            Qt::AlignLeft);  // Vertically center
  } else {
    nameLabel = nullptr;  // Set to nullptr to indicate no label
  }

  // Create checkmark area with consistent sizing
  checkmarkArea = new QWidget(this);
  checkmarkArea->setFixedWidth(24);
  checkmarkArea->setFixedHeight(18);  // Match toggle height

  // Create checkmark label
  QHBoxLayout* checkLayout = new QHBoxLayout(checkmarkArea);
  checkLayout->setContentsMargins(0, 0, 0, 0);
  checkLayout->setAlignment(Qt::AlignCenter);

  checkmark = new QLabel("✓", checkmarkArea);
  checkmark->setStyleSheet(
    "color: #0098ff; font-weight: bold; font-size: 18px;");
  checkmark->setAlignment(Qt::AlignCenter);
  checkmark->setContentsMargins(0, 0, 0, 0);
  checkmark->setVisible(false);

  checkLayout->addWidget(checkmark);

  // Create toggle with consistent sizing
  toggle = new ClickableSlider(Qt::Horizontal, this);
  toggle->setMinimum(0);
  toggle->setMaximum(1);
  toggle->setSingleStep(1);
  toggle->setPageStep(1);
  toggle->setFixedWidth(40);
  toggle->setFixedHeight(18);

  // Description label with consistent styling
  if (!description.isEmpty()) {
    descriptionLabel = new QLabel(description, this);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setStyleSheet("color: #666666;");
    descriptionLabel->setAlignment(Qt::AlignVCenter |
                                   Qt::AlignLeft);  // Vertically center
    descriptionLabel->setContentsMargins(0, 0, 0, 0);
  } else {
    descriptionLabel = nullptr;  // Set to nullptr to indicate no description
  }

  // Initially set up right alignment (default)
  setAlignment(AlignRight);

  // Add description only if provided
  if (descriptionLabel) {
    mainLayout->addWidget(descriptionLabel);
  }

  // Connect slider value changes to update style and emit signal
  connect(toggle, &QSlider::valueChanged, this, [this](int value) {
    static_cast<ClickableSlider*>(toggle)->updateStyle(value == 1);
    emit stateChanged(settingId, value == 1);
  });

  // Update style after the widget is fully shown - do this in multiple stages
  // to ensure the style is applied properly

  // First, set the style immediately
  bool currentState = toggle->value() == 1;
  static_cast<ClickableSlider*>(toggle)->updateStyle(currentState);
  toggle->style()->polish(toggle);
  toggle->update();

  // Then set it again after the event loop has processed
  QTimer::singleShot(0, this, [this]() {
    bool currentState = toggle->value() == 1;
    static_cast<ClickableSlider*>(toggle)->updateStyle(currentState);
    toggle->style()->polish(toggle);
    toggle->update();
  });
}

void SettingsToggle::setAlignment(Alignment align) {
  currentAlignment = align;

  // Remove all items from the top layout first
  while (topLayout->count() > 0) {
    QLayoutItem* item = topLayout->takeAt(0);
    if (item->widget()) {
      // Don't delete widgets, just remove them from layout
      topLayout->removeWidget(item->widget());
    }
    delete item;
  }

  // Add widgets according to the specified alignment
  switch (align) {
    case AlignLeft:
      // Both name and toggle aligned to left
      if (nameLabel) {
        topLayout->addWidget(nameLabel);
      }
      topLayout->addWidget(checkmarkArea);
      topLayout->addWidget(toggle);
      topLayout->addStretch();
      break;

    case AlignRight:
      // Name left, toggle right
      if (nameLabel) {
        topLayout->addWidget(nameLabel);
        topLayout->addStretch(
          1);  // Use stretch factor 1 for consistent right alignment
      }
      topLayout->addWidget(checkmarkArea);
      topLayout->addWidget(toggle);
      break;

    case AlignCompact:
      // Name and toggle close together
      if (nameLabel) {
        topLayout->addWidget(nameLabel);
      }
      topLayout->addWidget(checkmarkArea);
      topLayout->addWidget(toggle);
      topLayout->addStretch();
      break;
  }

  // Make sure the top layout is properly set
  if (layout()->count() == 0 || layout()->itemAt(0)->layout() != topLayout) {
    // Add top layout to main layout if not already added
    qobject_cast<QVBoxLayout*>(layout())->insertLayout(0, topLayout);
  }
}

bool SettingsToggle::isEnabled() const { return toggle->value() == 1; }

void SettingsToggle::setEnabled(bool enabled) {
  // First set the value
  toggle->setValue(enabled ? 1 : 0);

  // Ensure style is updated when set programmatically
  static_cast<ClickableSlider*>(toggle)->updateStyle(enabled);

  // Force a style refresh
  toggle->style()->polish(toggle);
  toggle->update();
}

void SettingsToggle::setDisabledStyle(bool disabled) {
  // Update the toggle's visual state to show disabled appearance
  // This applies our custom styling while maintaining our look
  static_cast<ClickableSlider*>(toggle)->updateDisabledStyle(disabled);

  // Update label colors with more distinct disabled styling
  if (disabled) {
    // Apply a stronger disabled appearance to the whole widget
    // But don't override toggle styling which is handled by ClickableSlider
    setStyleSheet(
      "background-color: rgba(30, 30, 30, 0.5); border-radius: 3px;");

    // Make the text more visibly disabled (darker grey)
    if (nameLabel) {
      nameLabel->setStyleSheet("color: #555555;");
    }
    if (descriptionLabel) {
      descriptionLabel->setStyleSheet("color: #555555;");
    }

    // Disable interaction but ensure our styling remains
    toggle->setEnabled(false);

    // Force a refresh to ensure custom styling is applied
    QTimer::singleShot(0, this, [this]() {
      bool currentState = toggle->value() == 1;
      static_cast<ClickableSlider*>(toggle)->updateDisabledStyle(true);
      toggle->style()->polish(toggle);
      toggle->update();
    });

    // Set cursor to indicate non-interactive state
    setCursor(Qt::ForbiddenCursor);
  } else {
    // Remove disabled styling but maintain our custom styling
    setStyleSheet("");

    // Restore normal colors
    if (nameLabel) {
      nameLabel->setStyleSheet("");
    }
    if (descriptionLabel) {
      descriptionLabel->setStyleSheet("color: #666666;");  // Desc always grey
    }

    // Re-enable interaction
    toggle->setEnabled(true);

    // Force a refresh to ensure custom styling is applied
    QTimer::singleShot(0, this, [this]() {
      bool currentState = toggle->value() == 1;
      static_cast<ClickableSlider*>(toggle)->updateStyle(currentState);
      toggle->style()->polish(toggle);
      toggle->update();
    });

    // Restore normal cursor
    setCursor(Qt::ArrowCursor);
  }
}

void SettingsToggle::setCheckmarkVisible(bool visible) {
  if (checkmark) {
    checkmark->setVisible(visible);
  }
}

// This method is no longer needed as we're creating the checkmark area
// immediately but we'll keep a minimal implementation for compatibility
void SettingsToggle::addCheckmarkArea() {
  // Already created in constructor - nothing to do here
  return;
}

// Add showEvent override to ensure proper style initialization
void SettingsToggle::showEvent(QShowEvent* event) {
  // Refresh the toggle style to ensure it renders correctly when first shown
  bool currentState = toggle->value() == 1;
  static_cast<ClickableSlider*>(toggle)->updateStyle(currentState);

  // Force an immediate style refresh
  toggle->style()->polish(toggle);
  toggle->update();

  // Process the event
  QWidget::showEvent(event);

  // Apply again after Qt has finished its styling (deferred using a zero-timer)
  QTimer::singleShot(0, this, [this]() {
    bool currentState = toggle->value() == 1;
    static_cast<ClickableSlider*>(toggle)->updateStyle(currentState);
    toggle->style()->polish(toggle);
    toggle->update();
  });
}
