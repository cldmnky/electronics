#pragma once
// QML view model for the Projects screen: one directory per project under
// ~/Music/mnkybox-projects, saved as session-format project.mnkybox. Rows load
// a project; NEW/SAVE/RENAME/DELETE manage the folder. Rename goes through the
// shared touch keyboard and refuses unsafe names.
#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariant>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include "app_context.hpp"

namespace mnky::app {

class ProjectsModel final : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Roles {
    NameRole = Qt::UserRole + 1,
    CurrentRole,
    ModifiedRole,
    PathRole,
  };

  struct Entry {
    QString name;
    QString path;
    std::int64_t modifiedMs = 0;
    bool current = false;
  };

  explicit ProjectsModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setEntries(std::vector<Entry> entries);
  const Entry* entry(int row) const;

 private:
  std::vector<Entry> entries_;
};

class ProjectsViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(ProjectsModel* model READ model CONSTANT)
  Q_PROPERTY(QString currentName READ currentName NOTIFY stateChanged)
  Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
  Q_PROPERTY(int selectedRow READ selectedRow NOTIFY selectionChanged)

 public:
  explicit ProjectsViewModel(AppContext& context,
                             std::function<void()> onSessionChanged,
                             QObject* parent = nullptr);

  ProjectsModel* model() { return &model_; }
  QString currentName() const { return currentName_; }
  QString statusText() const { return statusText_; }
  int selectedRow() const { return selectedRow_; }

  Q_INVOKABLE void newProject();
  Q_INVOKABLE void saveProject();
  Q_INVOKABLE void loadRow(int row);
  Q_INVOKABLE void selectRow(int row);
  Q_INVOKABLE QString renameSelected(const QString& name);
  Q_INVOKABLE void deleteSelected();
  Q_INVOKABLE void refresh();
  Q_INVOKABLE QString projectName(int row) const;
  Q_INVOKABLE void scanProjects();

  void projectStateChanged();

 signals:
  void stateChanged();
  void selectionChanged();

 private:
  std::filesystem::path selectedDirectory() const;
  void afterSessionChanged();

  AppContext& context_;
  std::function<void()> onSessionChanged_;
  ProjectsModel model_;
  int selectedRow_ = -1;
  int refreshCounter_ = 0;
  QString currentName_;
  QString statusText_;
};

}  // namespace mnky::app
