#ifndef PCDIAG_UI_SETTINGSVIEW_H
#define PCDIAG_UI_SETTINGSVIEW_H

#include <array>
#include <memory>

#include <QFutureWatcher>
#include <QWidget>

class QStackedWidget;
class QScrollArea;
class QTextBrowser;
class QString;
class QPushButton;
class QLabel;
class SettingsToggle;
struct UpdateStatus;

class SettingsView : public QWidget {
  Q_OBJECT
 public:
  explicit SettingsView(QWidget* parent = nullptr);

  // Add new method for saving settings during cleanup
  void saveSettings();

 private:
  struct ResourceItem {
    QString button_text;
    QString resource_path;
    bool use_markdown = true;
    QPushButton* button = nullptr;
    QByteArray data;
    QFutureWatcher<QString>* watcher = nullptr;

    ResourceItem(QString button_text, QString resource_path,
                 bool use_markdown = false)
        : button_text(button_text), resource_path(resource_path),
          use_markdown(use_markdown) {}
  };

  void OnResetSettingsClicked();
  void OnExperimentalFeaturesChanged(const QString& id, bool enabled);
  void OnConsoleVisibilityChanged(const QString& id, bool enabled);
  void OnElevatedPriorityChanged(const QString& id, bool enabled);
  void OnValidateMetricsOnStartupChanged(const QString& id, bool enabled);
  void OnDataCollectionChanged(const QString& id, bool enabled);
  void OnOfflineModeChanged(const QString& id, bool enabled);
  void OnDetailedLogsChanged(const QString& id, bool enabled);
  void OnAutomaticDataUploadChanged(const QString& id, bool enabled);
  void OnOpenAppDataLocation();
  void OnGDPRClicked();
  void OnRequestDataClicked();
  void OnDeleteDataClicked();
  void OnCheckUpdatesClicked();
  void OnUpdateStatusChanged(const UpdateStatus& status);
  void OnUpdateCheckFailed(const QString& error);
  void OnUpdateCheckStarted();
  bool eventFilter(QObject* watched, QEvent* event) override;
  void OnDeleteAllDataClicked();

  QStackedWidget* page_stack_ = nullptr;
  QScrollArea* settings_area_ = nullptr;
  QTextBrowser* content_area_ = nullptr;
  QWidget* gdpr_page_ = nullptr;
  std::array<std::unique_ptr<ResourceItem>, 4> resources_ = {
    std::make_unique<ResourceItem>("Terms of Service", ":/terms", true),
    std::make_unique<ResourceItem>("Third Party", ":/third-party"),
    std::make_unique<ResourceItem>("Privacy Notice", ":/privacy", true),
    std::make_unique<ResourceItem>("GDPR", "", false)};
  ResourceItem* active_page_ = nullptr;

  SettingsToggle* experimental_features_toggle_ = nullptr;
  SettingsToggle* elevated_priority_toggle_ = nullptr;
  SettingsToggle* console_visibility_toggle_ = nullptr;
  SettingsToggle* validate_metrics_on_startup_toggle_ = nullptr;
  SettingsToggle* allow_data_collection_toggle_ = nullptr;
  SettingsToggle* offline_mode_toggle_ = nullptr;
  SettingsToggle* detailed_logs_toggle_ = nullptr;
  SettingsToggle* automatic_data_upload_toggle_ = nullptr;
  QPushButton* appdata_button_ = nullptr;
  QPushButton* reset_settings_button_ = nullptr;
  QPushButton* delete_all_data_button_ = nullptr;
  QPushButton* check_updates_button_ = nullptr;
  QLabel* update_status_label_ = nullptr;
};

#endif  // PCDIAG_UI_SETTINGSVIEW_H
