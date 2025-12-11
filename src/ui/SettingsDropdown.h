#pragma once

#include <functional>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPainter>
#include <QString>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QVariant>
#include <QWidget>

// Forward declarations
class SettingsDropdown;
class QPushButton;

/**
 * @class TaggedItemDelegate
 * @brief Custom item delegate for rendering items with tags in the dropdown
 * list
 *
 * This delegate ensures that tags are properly displayed in the dropdown list
 * with the correct colors and formatting, consistent with the main display.
 */
class TaggedItemDelegate : public QStyledItemDelegate {
  Q_OBJECT

 public:
  /**
   * @brief Constructs a delegate for the given dropdown
   * @param dropdown The parent dropdown that owns this delegate
   */
  explicit TaggedItemDelegate(SettingsDropdown* dropdown);

  /**
   * @brief Custom paint implementation for dropdown items
   *
   * Draws the item with proper tag styling, handling selection states
   * and maintaining consistent appearance with the main dropdown display.
   */
  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;

  /**
   * @brief Calculates the appropriate size for items with tags
   *
   * Ensures items have enough space to display both the text and any tags.
   */
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;

 private:
  SettingsDropdown* dropdown_;  ///< Parent dropdown that owns this delegate
};

/**
 * @class SettingsDropdown
 * @brief Enhanced dropdown component with tag support and custom styling
 *
 * This class extends QComboBox to provide additional features:
 * 1. Custom styling with consistent appearance across the application
 * 2. Support for visual tags (like "Recommended", "Original") with consistent
 * coloring
 *    - Tags are displayed both in the dropdown list and on the currently
 * selected item
 *    - Multiple tags are supported with compact display when space is limited
 *    - Colors: Recommended = blue, Original = orange
 * 3. Checkmark support for indicating selected items in a list
 * 4. Improved text handling with truncation for long items when tags are
 * present
 * 5. Enhanced visualization of dropdown items with proper tag display
 * 6. Consistent width (270px) across all dropdown instances
 *
 * The component handles both the main display and the popup list
 * consistently, ensuring tags appear in both places with proper styling.
 * When multiple tags are present, shortened text is used to maintain
 * compactness.
 */
class SettingsDropdown : public QComboBox {
  Q_OBJECT

 public:
  /**
   * @brief Tag types that can be applied to dropdown items
   *
   * Each tag has a consistent color and appearance across the application.
   * Multiple tags can be applied to a single item.
   */
  enum class TagType {
    None,         ///< No tag
    Recommended,  ///< Recommended value (blue color)
    Original      ///< Original value (orange color)
  };

  // Role for storing tag data in model
  static const int TagRole = Qt::UserRole + 1;

  /**
   * @brief Constructs a SettingsDropdown with custom styling
   * @param parent Parent widget
   * @param width Width of the dropdown in pixels (default: 180px)
   */
  explicit SettingsDropdown(QWidget* parent = nullptr, int width = 180);

  /**
   * @brief Adds multiple items with optional user data
   * @param texts List of display texts for the items
   * @param userData Optional list of data values for each item (must match size
   * of texts if provided)
   */
  void addItems(const QStringList& texts, const QList<QVariant>& userData);

  /**
   * @brief Adds a single item with optional user data
   * @param text Display text for the item
   * @param userData Optional data value for the item
   */
  void addItem(const QString& text, const QVariant& userData = QVariant());

  /**
   * @brief Sets the default selected index
   * @param index Index to select initially
   */
  void setDefaultIndex(int index);

  /**
   * @brief Gets the user data for the current selected item
   * @return QVariant containing the user data
   */
  QVariant currentData() const;

  /**
   * @brief Convenience accessor for the current index
   * @return Current selected index (same as QComboBox::currentIndex())
   *
   * Added for callers that prefer a getter-style API.
   */
  int getCurrentIndex() const { return currentIndex(); }

  /**
   * @brief Applies styling to the dropdown
   * @param fixedWidth Optional custom width (uses elementWidth if 0)
   *
   * Updates the stylesheet and dimensions of the dropdown.
   */
  void applyStyle(int fixedWidth = 0);

  /**
   * @brief Sets the disabled style for the dropdown
   * @param disabled True to apply disabled styling, false for normal styling
   *
   * Applies visual changes for the disabled state, including
   * opacity effect and grayed colors.
   */
  void setDisabledStyle(bool disabled);

  /**
   * @brief Sets a stronger missing setting style for the dropdown
   * @param missing True to apply missing setting styling, false for normal
   * styling
   *
   * Applies much more obvious visual changes for missing settings,
   * including stronger graying, cross-hatch pattern, and clear unavailable
   * indication.
   */
  void setMissingSettingStyle(bool missing);

  /**
   * @brief Checkmark support - makes the checkmark visible or hidden
   * @param visible True to show the checkmark, false to hide it
   *
   * The checkmark appears to the left of the dropdown when visible.
   */
  void setCheckmarkVisible(bool visible);

  /**
   * @brief Creates the checkmark area if it doesn't exist
   *
   * Automatically called by setCheckmarkVisible if needed.
   */
  void addCheckmarkArea();

  /**
   * @brief Checks if the checkmark area has been created
   * @return True if the checkmark area exists, false otherwise
   */
  bool hasCheckmarkArea() const { return checkmarkArea != nullptr; }

  /**
   * @brief Sets a single tag for an item
   * @param index Index of the item to tag
   * @param tagType Type of tag to apply
   *
   * Replaces any existing tags on the item.
   */
  void setItemTag(int index, TagType tagType);

  /**
   * @brief Sets multiple tags for an item
   * @param index Index of the item to tag
   * @param tags List of tags to apply
   *
   * Replaces any existing tags on the item.
   */
  void setItemTags(int index, const QList<TagType>& tags);

  /**
   * @brief Gets all tags for an item
   * @param index Index of the item
   * @return List of tags applied to the item
   */
  QList<TagType> getItemTags(int index) const;

  /**
   * @brief Removes all tags from an item
   * @param index Index of the item to clear tags from
   */
  void clearItemTags(int index);

  /**
   * @brief Gets the full text for a tag type
   * @param tagType Type of tag
   * @return Full text representation of the tag (e.g., "(Recommended)")
   *
   * Used for display when space permits.
   */
  static QString getTagText(TagType tagType);

  /**
   * @brief Gets the shortened text for a tag type
   * @param tagType Type of tag
   * @return Shortened text representation of the tag (e.g., "(Rec)", "(Orig)")
   *
   * Used for display when space is limited or multiple tags are present.
   * More compact than getTagText() to maintain readability with multiple tags.
   */
  static QString getShortTagText(TagType tagType);

  /**
   * @brief Gets the color for a tag type
   * @param tagType Type of tag
   * @param isDisabled Whether to return the disabled state color
   * @return QColor for the tag
   *
   * Color scheme:
   * - Recommended: Blue (#0098ff normal, muted blue when disabled)
   * - Original: Orange (#ff9800 normal, muted orange when disabled)
   * Each tag type has a consistent color across both closed dropdown display
   * and open dropdown list to ensure visual consistency.
   */
  static QColor getTagColor(TagType tagType, bool isDisabled = false);

  /**
   * @brief Creates a styled "Add Setting" button template for missing registry
   * settings
   * @param parent Parent widget for the button
   * @param settingId The setting ID this button will create
   * @return Configured QPushButton with proper styling
   *
   * Button placement requirements:
   * - Must be positioned at the boundary between text and dropdown areas
   * - Uses layout structure: leftSide (flex 3) | button (flex 0, center) |
   * rightSide (flex 0, right)
   * - Blue color (#0078d4) to distinguish it from normal controls
   * - Compact size (24px height) to fit within setting row height
   * - Positioned at the exact junction point where dropdown normally starts
   *
   * This creates consistent button positioning at the natural boundary between
   * text and control areas, matching the visual flow of normal settings.
   */
  static QPushButton* createAddSettingButton(QWidget* parent,
                                             const QString& settingId);

  /**
   * @brief Checks if the dropdown is in disabled state
   * @return True if disabled, false otherwise
   */
  bool isInDisabledState() const { return isDisabled; }

 signals:
  /**
   * @brief Signal emitted when the selected value changes
   * @param value The new data value
   */
  void valueChanged(const QVariant& value);

 protected:
  /**
   * @brief Custom paint event for rendering the dropdown
   * @param event Paint event object
   *
   * Handles drawing the dropdown with proper styling and tag display.
   * Displays tags for the currently selected item when the dropdown is closed.
   * Automatically uses shortened tag text when multiple tags are present or
   * when space is limited to maintain readability.
   */
  void paintEvent(QPaintEvent* event) override;

  /**
   * @brief Event filter for customizing dropdown popup item display
   * @param watched The object being watched
   * @param event The event that occurred
   * @return True if the event was handled, false to continue processing
   *
   * Used to customize the appearance of items in the dropdown list.
   */
  bool eventFilter(QObject* watched, QEvent* event) override;

  /**
   * @brief Custom wheel event handling
   * @param event Wheel event object
   *
   * Prevents wheel scrolling when the dropdown is closed.
   */
  void wheelEvent(QWheelEvent* event) override;

  /**
   * @brief Custom popup display handling
   *
   * Ensures the popup has appropriate width to display items with tags.
   */
  void showPopup() override;

  /**
   * @brief Gets the clean text for an item with tags removed
   * @param index Index of the item
   * @return Text with any tag markers removed
   */
  QString getCleanItemText(int index) const;

  // Make TaggedItemDelegate a friend so it can access private members
  friend class TaggedItemDelegate;

 private:
  /**
   * @brief Gets the normal state stylesheet
   * @return QString containing the CSS stylesheet
   */
  QString styleSheet() const;

  /**
   * @brief Gets the disabled state stylesheet
   * @return QString containing the CSS stylesheet for disabled state
   */
  QString disabledStyleSheet() const;

  /**
   * @brief Gets the missing setting state stylesheet
   * @return QString containing the CSS stylesheet for missing setting state
   */
  QString missingSettingStyleSheet() const;

  bool isDisabled = false;  ///< Flag indicating if the dropdown is disabled
  int elementWidth = 180;   ///< Default width of the dropdown

  /**
   * @brief Map storing tags for each item
   * Key is item index, value is list of tags
   */
  QMap<int, QList<TagType>> itemTags;

  // Checkmark components
  QWidget* checkmarkArea = nullptr;  ///< Container for the checkmark
  QLabel* checkmark = nullptr;       ///< The checkmark label

  // Custom item delegate
  TaggedItemDelegate* itemDelegate =
    nullptr;  ///< Delegate for rendering items with tags
};
