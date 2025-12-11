#include "SettingsDropdown.h"

#include <iostream>

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QStyledItemDelegate>
#include <QWheelEvent>

// Implementation of TaggedItemDelegate

TaggedItemDelegate::TaggedItemDelegate(SettingsDropdown* dropdown)
    : QStyledItemDelegate(dropdown), dropdown_(dropdown) {}

void TaggedItemDelegate::paint(QPainter* painter,
                               const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
  QStyledItemDelegate::paint(painter, option, index);

  // Get tags for this item
  QVariant tagData = index.data(SettingsDropdown::TagRole);
  if (!tagData.isValid()) {
    return;
  }

  QList<SettingsDropdown::TagType> tags =
    tagData.value<QList<SettingsDropdown::TagType>>();
  if (tags.isEmpty()) {
    return;
  }

  // Draw tags using the same style as the closed dropdown
  painter->save();

  QRect rect = option.rect;
  int tagX = rect.right() - 10;  // Start from right edge with some margin

  for (auto tag : tags) {
    QString tagText;
    QColor tagColor;

    // Use the same color scheme as the closed dropdown
    if (tag == SettingsDropdown::TagType::Original) {
      tagText = "Original";
      tagColor = SettingsDropdown::getTagColor(tag, false);  // Orange
    } else if (tag == SettingsDropdown::TagType::Recommended) {
      tagText = "Recommended";
      tagColor = SettingsDropdown::getTagColor(tag, false);  // Blue
    }

    // Calculate tag size
    QFontMetrics fm(painter->font());
    QRect tagRect = fm.boundingRect(tagText);
    tagRect.adjust(-4, -2, 4, 2);  // Add padding

    // Position tag
    tagX -= tagRect.width() + 5;
    tagRect.moveTopLeft(
      QPoint(tagX, rect.top() + (rect.height() - tagRect.height()) / 2));

    // Draw tag background with consistent styling
    painter->setBrush(tagColor);
    painter->setPen(Qt::NoPen);
    painter->drawRoundedRect(tagRect, 3, 3);

    // Draw tag text
    painter->setPen(Qt::white);
    painter->drawText(tagRect, Qt::AlignCenter, tagText);
  }

  painter->restore();
}

QSize TaggedItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
  QSize size = QStyledItemDelegate::sizeHint(option, index);

  // Ensure minimum height for items with tags
  if (size.height() < 22) {
    size.setHeight(22);
  }

  // For items with tags, ensure they have enough width
  int itemIndex = index.row();
  if (dropdown_ && dropdown_->itemTags.contains(itemIndex) &&
      !dropdown_->itemTags.value(itemIndex).isEmpty()) {
    // Add extra width for tags
    int extraWidth = 0;
    const QList<SettingsDropdown::TagType>& tags =
      dropdown_->itemTags.value(itemIndex);

    for (const SettingsDropdown::TagType& tag : tags) {
      QString tagText = SettingsDropdown::getTagText(tag);
      extraWidth +=
        option.fontMetrics.horizontalAdvance(tagText) + 10;  // 10px padding
    }

    size.setWidth(size.width() + extraWidth);
  }

  return size;
}

// SettingsDropdown implementation

SettingsDropdown::SettingsDropdown(QWidget* parent, int width)
    : QComboBox(parent), elementWidth(width) {
  // Connect signals
  connect(this, QOverload<int>::of(&QComboBox::currentIndexChanged),
          [this](int index) { emit valueChanged(itemData(index)); });

  // Set view properties
  setView(new QListView(this));
  view()->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  view()->setTextElideMode(Qt::ElideRight);
  view()->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  // Create and set the custom item delegate
  itemDelegate = new TaggedItemDelegate(this);
  view()->setItemDelegate(itemDelegate);

  // Install event filter on the view to customize item rendering
  view()->viewport()->installEventFilter(this);
  view()->installEventFilter(this);

  // Set wheel policy to ignore wheel events on the combo box itself
  setFocusPolicy(Qt::StrongFocus);

  // Apply default styling with the specified width
  applyStyle(width);
}

void SettingsDropdown::addItems(const QStringList& texts,
                                const QList<QVariant>& userData) {
  if (texts.size() != userData.size() && !userData.isEmpty()) {
    return;  // Sizes must match if userData is provided
  }

  clear();

  for (int i = 0; i < texts.size(); ++i) {
    if (userData.isEmpty()) {
      QComboBox::addItem(texts[i]);
    } else {
      QComboBox::addItem(texts[i], userData[i]);
    }
  }
}

void SettingsDropdown::addItem(const QString& text, const QVariant& userData) {
  QComboBox::addItem(text, userData);
}

void SettingsDropdown::setDefaultIndex(int index) {
  if (index >= 0 && index < count()) {
    setCurrentIndex(index);
  }
}

QVariant SettingsDropdown::currentData() const {
  return itemData(currentIndex());
}

void SettingsDropdown::applyStyle(int fixedWidth) {
  setStyleSheet(isDisabled ? disabledStyleSheet() : styleSheet());

  // Use the elementWidth if no width is specified (fixedWidth = 0)
  int widthToApply = (fixedWidth > 0) ? fixedWidth : elementWidth;
  setFixedWidth(widthToApply);
}

void SettingsDropdown::setDisabledStyle(bool disabled) {
  isDisabled = disabled;

  // Apply appropriate style based on disabled state
  setStyleSheet(disabled ? disabledStyleSheet() : styleSheet());

  // Apply opacity effect for a more obvious disabled appearance
  if (disabled) {
    QGraphicsOpacityEffect* opacityEffect = new QGraphicsOpacityEffect(this);
    opacityEffect->setOpacity(0.7);  // 70% opacity for disabled state
    setGraphicsEffect(opacityEffect);
  } else {
    setGraphicsEffect(nullptr);  // Remove opacity effect when enabled
  }

  // Force a repaint to update tag colors
  update();
}

void SettingsDropdown::setMissingSettingStyle(bool missing) {
  isDisabled = missing;  // Also mark as disabled

  // Apply appropriate style based on missing state
  setStyleSheet(missing ? missingSettingStyleSheet() : styleSheet());

  // Apply stronger opacity effect for missing settings
  if (missing) {
    QGraphicsOpacityEffect* opacityEffect = new QGraphicsOpacityEffect(this);
    opacityEffect->setOpacity(
      0.4);  // Much stronger opacity for missing settings
    setGraphicsEffect(opacityEffect);
  } else {
    setGraphicsEffect(nullptr);  // Remove opacity effect when enabled
  }

  // Disable the dropdown completely for missing settings
  setEnabled(!missing);

  // Force a repaint to update appearance
  update();
}

void SettingsDropdown::paintEvent(QPaintEvent* event) {
  // Custom painting for dropdown
  QPainter painter(this);
  painter.setPen(isDisabled ? QColor(120, 120, 120) : Qt::white);

  // Get current display text and remove any existing tags
  QString displayText = currentText();
  QString cleanText = displayText;

  // Remove all instances of existing tags from clean text
  cleanText =
    cleanText.replace("(Recommended)", "").replace("(Original)", "").trimmed();

  // Get current tags for the selected item
  QList<TagType> currentTags;
  int index = currentIndex();
  if (index >= 0 && index < count()) {
    // First try to get from the item data (TagRole)
    QVariant tagData = itemData(index, TagRole);
    if (tagData.isValid()) {
      currentTags = tagData.value<QList<TagType>>();
    } else if (itemTags.contains(index)) {
      // Fallback to itemTags map
      currentTags = itemTags.value(index);
    }
  }

  // Style and layout the dropdown
  QStyleOptionComboBox opt;
  initStyleOption(&opt);

  // Draw the control background
  style()->drawPrimitive(QStyle::PE_PanelButtonCommand, &opt, &painter, this);

  // Calculate available space for text and tags
  QRect textRect = rect().adjusted(10, 0, -25, 0);
  int availableWidth = textRect.width();

  // Draw the main text without tags
  int mainTextWidth = painter.fontMetrics().horizontalAdvance(cleanText);

  // Check if we need to truncate the main text when tags are present
  QString displayMainText = cleanText;
  if (!currentTags.isEmpty() &&
      mainTextWidth >
        availableWidth * 0.6) {  // Allow only 60% for main text if tags exist
    // Find where to truncate
    int truncatePos = 0;
    for (int i = 0; i < cleanText.length(); i++) {
      if (painter.fontMetrics().horizontalAdvance(cleanText.left(i) + "...") >
          availableWidth * 0.6) {
        truncatePos = i - 1;
        break;
      }
    }
    if (truncatePos > 0) {
      displayMainText = cleanText.left(truncatePos) + "...";
    }
  }

  // Draw the main text
  painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, displayMainText);

  // If we have tags to display
  if (!currentTags.isEmpty()) {
    // Calculate total width needed for tags
    int totalTagWidth = 0;
    bool useShortTags = currentTags.size() > 1;

    for (const TagType& tag : currentTags) {
      QString tagText = useShortTags ? getShortTagText(tag) : getTagText(tag);
      totalTagWidth +=
        painter.fontMetrics().horizontalAdvance(tagText) + 5;  // 5px spacing
    }

    // Check if we need to use short tags due to space constraints
    if (!useShortTags && totalTagWidth > availableWidth * 0.4) {
      useShortTags = true;
      // Recalculate with short tags
      totalTagWidth = 0;
      for (const TagType& tag : currentTags) {
        QString tagText = getShortTagText(tag);
        totalTagWidth += painter.fontMetrics().horizontalAdvance(tagText) + 5;
      }
    }

    // Draw tags from right to left
    int currentX = textRect.right() - 5;  // Start 5px from right edge

    for (const TagType& tag : currentTags) {
      QString tagText = useShortTags ? getShortTagText(tag) : getTagText(tag);
      int tagWidth = painter.fontMetrics().horizontalAdvance(tagText);

      QRect tagRect(currentX - tagWidth, textRect.top(), tagWidth,
                    textRect.height());

      // Draw tag with appropriate color
      painter.setPen(getTagColor(tag, isDisabled));
      painter.drawText(tagRect, Qt::AlignVCenter | Qt::AlignRight, tagText);

      // Move left for next tag
      currentX -= (tagWidth + 5);
    }
  }

  // Draw dropdown arrow
  painter.setPen(isDisabled ? QColor(120, 120, 120) : Qt::white);
  QRect arrowRect = rect().adjusted(width() - 20, 0, -5, 0);
  painter.drawText(arrowRect, Qt::AlignVCenter | Qt::AlignRight, "▼");
}

QString SettingsDropdown::styleSheet() const {
  // Create the stylesheet with dynamic width
  return QString(R"(
        QComboBox {
            color: #ffffff;
            background-color: #1e1e1e;
            border: none;
            padding: 5px 10px;
            max-width: %1px;
            width: %1px;
            font-size: 12px;
        }
        QComboBox:hover {
            background-color: #333333;
        }
        QComboBox::drop-down {
            width: 20px;
            border-left: none;
            subcontrol-origin: padding;
            subcontrol-position: right center;
        }
        QComboBox::down-arrow {
            image: none;
            width: 10px;
            height: 10px;
        }
        QComboBox QAbstractItemView {
            background-color: #252525;
            color: #ffffff;
            border: 1px solid #444444;
            selection-background-color: #0078d4;
            font-size: 12px;
            padding: 2px;
        }
        QComboBox QAbstractItemView::item {
            min-height: 22px;
            padding: 2px 6px;
            border-radius: 2px;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #0078d4;
            color: white;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #333333;
        }
    )")
    .arg(elementWidth);
}

QString SettingsDropdown::disabledStyleSheet() const {
  // Create the disabled stylesheet with dynamic width
  return QString(R"(
        QComboBox {
            color: #666666;
            background-color: #1a1a1a;
            border: none;
            padding: 5px 10px;
            max-width: %1px;
            width: %1px;
            font-size: 12px;
        }
        QComboBox:hover {
            background-color: #2a2a2a;
        }
        QComboBox::drop-down {
            width: 20px;
            border-left: none;
            subcontrol-origin: padding;
            subcontrol-position: right center;
        }
        QComboBox::down-arrow {
            image: none;
            width: 10px;
            height: 10px;
        }
        QComboBox QAbstractItemView {
            background-color: #1a1a1a;
            color: #666666;
            border: 1px solid #444444;
            selection-background-color: #444444;
            font-size: 12px;
            padding: 2px;
        }
        QComboBox QAbstractItemView::item {
            min-height: 22px;
            padding: 2px 6px;
            border-radius: 2px;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #444444;
            color: #aaaaaa;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #252525;
        }
    )")
    .arg(elementWidth);
}

QString SettingsDropdown::missingSettingStyleSheet() const {
  // Create a subtle missing setting stylesheet with same shape but darker/lower
  // contrast
  return QString(R"(
        QComboBox {
            color: #555555;
            background-color: #151515;
            border: none;
            padding: 5px 10px;
            max-width: %1px;
            width: %1px;
            font-size: 12px;
        }
        QComboBox:hover {
            background-color: #1a1a1a;
        }
        QComboBox:disabled {
            color: #444444;
            background-color: #121212;
        }
        QComboBox::drop-down {
            width: 20px;
            border-left: none;
            subcontrol-origin: padding;
            subcontrol-position: right center;
        }
        QComboBox::down-arrow {
            image: none;
            width: 10px;
            height: 10px;
        }
        QComboBox QAbstractItemView {
            background-color: #151515;
            color: #555555;
            border: 1px solid #333333;
            selection-background-color: #333333;
            font-size: 12px;
            padding: 2px;
        }
        QComboBox QAbstractItemView::item {
            min-height: 22px;
            padding: 2px 6px;
            border-radius: 2px;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #333333;
            color: #888888;
        }
        QComboBox QAbstractItemView::item:hover {
            background-color: #1a1a1a;
        }
    )")
    .arg(elementWidth);
}

bool SettingsDropdown::eventFilter(QObject* watched, QEvent* event) {
  // We're using a custom delegate now, so we don't need to do custom painting
  // in the event filter. However, we'll keep the event filter for other
  // customizations that might be needed.

  if (watched == view() && event->type() == QEvent::Show) {
    // When the view is shown (dropdown opened), make sure it's wide enough
    // to accommodate items with tags
    int maxWidth = width();

    for (int i = 0; i < count(); i++) {
      if (itemTags.contains(i) && !itemTags[i].isEmpty()) {
        // Calculate extra width needed for tags
        int textWidth = fontMetrics().horizontalAdvance(getCleanItemText(i));
        int tagWidth = 0;

        for (const TagType& tag : itemTags[i]) {
          tagWidth += fontMetrics().horizontalAdvance(getTagText(tag)) + 10;
        }

        int totalWidth =
          textWidth + tagWidth + 30;  // 30px for padding and margin
        maxWidth = qMax(maxWidth, totalWidth);
      }
    }

    // Set the view width to accommodate the widest item
    view()->setMinimumWidth(maxWidth);
  }

  // Let the event propagate
  return QComboBox::eventFilter(watched, event);
}

void SettingsDropdown::addCheckmarkArea() {
  // If already has checkmark area, don't add again
  if (checkmarkArea) return;

  // Get parent widget to correctly position the checkmark
  QWidget* parentWidget = qobject_cast<QWidget*>(parent());
  if (!parentWidget || !parentWidget->layout()) return;

  // Create checkmark area with adjusted positioning
  checkmarkArea = new QWidget(parentWidget);
  checkmarkArea->setFixedWidth(30);
  checkmarkArea->setFixedHeight(28);  // Match dropdown height

  // Create checkmark label with centered positioning
  QHBoxLayout* checkLayout = new QHBoxLayout(checkmarkArea);
  checkLayout->setContentsMargins(0, 0, 0, 0);
  checkLayout->setAlignment(Qt::AlignCenter);

  checkmark = new QLabel("✓", checkmarkArea);
  checkmark->setStyleSheet(
    "color: #0098ff; font-weight: bold; font-size: 24px;");
  checkmark->setAlignment(Qt::AlignCenter);
  checkmark->setVisible(false);

  checkLayout->addWidget(checkmark);

  // Add checkmark to parent layout, correctly positioned
  if (QHBoxLayout* parentHLayout =
        qobject_cast<QHBoxLayout*>(parentWidget->layout())) {
    // Find the index of the dropdown in the parent layout
    int dropdownIndex = -1;
    for (int i = 0; i < parentHLayout->count(); i++) {
      if (parentHLayout->itemAt(i)->widget() == this) {
        dropdownIndex = i;
        break;
      }
    }

    if (dropdownIndex >= 0) {
      // Insert the checkmark area before the dropdown
      parentHLayout->insertWidget(dropdownIndex, checkmarkArea);
    }
  }
}

void SettingsDropdown::setCheckmarkVisible(bool visible) {
  if (!checkmark) {
    addCheckmarkArea();
  }

  if (checkmark) {
    checkmark->setVisible(visible);
  }
}

void SettingsDropdown::wheelEvent(QWheelEvent* event) {
  // Ignore wheel events when dropdown is not opened
  // The view()->isVisible() check determines if the dropdown popup is shown
  if (!view()->isVisible()) {
    event->ignore();
    return;
  }

  // Only allow wheel events when the dropdown is open
  QComboBox::wheelEvent(event);
}

// Add new methods for tag support
QString SettingsDropdown::getTagText(TagType tagType) {
  switch (tagType) {
    case TagType::Recommended:
      return "(Recommended)";
    case TagType::Original:
      return "(Original)";
    default:
      return "";
  }
}

QString SettingsDropdown::getShortTagText(TagType tagType) {
  switch (tagType) {
    case TagType::Recommended:
      return "(Rec)";
    case TagType::Original:
      return "(Orig)";
    default:
      return "";
  }
}

QColor SettingsDropdown::getTagColor(TagType tagType, bool isDisabled) {
  if (isDisabled) {
    // Use muted colors when disabled
    switch (tagType) {
      case TagType::Recommended:
        return QColor(80, 120, 160);  // Muted blue
      case TagType::Original:
        return QColor(160, 120, 80);  // Muted orange
      default:
        return QColor(120, 120, 120);  // Grey
    }
  } else {
    switch (tagType) {
      case TagType::Recommended:
        return QColor(0, 152, 255);  // Blue
      case TagType::Original:
        return QColor(255, 152, 0);  // Orange
      default:
        return QColor(255, 255, 255);  // White
    }
  }
}

void SettingsDropdown::setItemTag(int index, TagType tagType) {
  if (index >= 0 && index < count()) {
    QList<TagType> tags;
    if (tagType != TagType::None) {
      tags.append(tagType);
    }
    setItemTags(index, tags);  // Use the unified method
  }
}

void SettingsDropdown::setItemTags(int index, const QList<TagType>& tags) {
  if (index < 0 || index >= count()) {
    return;
  }

  // Update the itemTags map for paintEvent to use
  if (tags.isEmpty()) {
    itemTags.remove(index);
    setItemData(index, QVariant(), TagRole);
  } else {
    itemTags[index] = tags;
    QVariant tagData;
    tagData.setValue(tags);
    setItemData(index, tagData, TagRole);
  }

  // Force a repaint of both the main dropdown and the view if visible
  update();  // Update the main dropdown display
  if (view()->isVisible()) {
    view()->viewport()->update();
  }
}

QList<SettingsDropdown::TagType> SettingsDropdown::getItemTags(
  int index) const {
  return itemTags.value(index);
}

void SettingsDropdown::clearItemTags(int index) {
  itemTags.remove(index);

  // Force a repaint of the view if it's visible
  if (view()->isVisible()) {
    view()->viewport()->update();
  }
  update();  // Update the main dropdown display too
}

// Add the new helper method to get clean text
QString SettingsDropdown::getCleanItemText(int index) const {
  if (index < 0 || index >= count()) {
    return QString();
  }

  QString text = itemText(index);
  // Remove tag markers if present
  return text.replace("(Recommended)", "").replace("(Original)", "").trimmed();
}

void SettingsDropdown::showPopup() {
  // Before showing the popup, ensure the view has proper size
  // Calculate the width needed to accommodate all items with tags
  int maxWidth = width();

  for (int i = 0; i < count(); i++) {
    if (itemTags.contains(i) && !itemTags[i].isEmpty()) {
      // Calculate extra width needed for tags
      int textWidth = fontMetrics().horizontalAdvance(getCleanItemText(i));
      int tagWidth = 0;

      for (const TagType& tag : itemTags[i]) {
        tagWidth += fontMetrics().horizontalAdvance(getTagText(tag)) + 10;
      }

      int totalWidth =
        textWidth + tagWidth + 30;  // 30px for padding and margin
      maxWidth = qMax(maxWidth, totalWidth);
    }
  }

  // Set the view width to accommodate the widest item
  view()->setMinimumWidth(maxWidth);

  // Call the base implementation to show the popup
  QComboBox::showPopup();
}

QPushButton* SettingsDropdown::createAddSettingButton(
  QWidget* parent, const QString& settingId) {
  // Create the "Add Setting" button with proper styling and size
  QPushButton* addButton = new QPushButton("Add Setting", parent);
  addButton->setObjectName("add_" + settingId);
  addButton->setProperty("settingId", settingId);

  // Set button styles - blue color to stand out, compact size for overlay
  addButton->setStyleSheet(R"(
        QPushButton {
            background-color: #0078d4;
            color: #ffffff;
            border: none;
            padding: 4px 12px;
            border-radius: 4px;
            font-size: 11px;
            font-weight: bold;
            min-height: 24px;
            max-height: 24px;
            min-width: 80px;
        }
        QPushButton:hover {
            background-color: #106ebe;
        }
        QPushButton:pressed {
            background-color: #005a9e;
        }
    )");

  // Set size policy to ensure consistent sizing
  addButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  addButton->setCursor(Qt::PointingHandCursor);

  return addButton;
}
