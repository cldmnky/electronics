#include "projects_view_model.hpp"

#include <QDateTime>

#include <algorithm>
#include <chrono>
#include <system_error>
#include <utility>

#include "app_state.hpp"
#include "logging.hpp"
#include "project_io.hpp"
#include "project_logic.hpp"

namespace mnky::app {

ProjectsModel::ProjectsModel(QObject* parent) : QAbstractListModel(parent) {}

int ProjectsModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

QVariant ProjectsModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 ||
      index.row() >= static_cast<int>(entries_.size())) {
    return {};
  }
  const auto& entry = entries_[static_cast<std::size_t>(index.row())];
  switch (role) {
    case NameRole:
      return entry.name;
    case CurrentRole:
      return entry.current;
    case ModifiedRole:
      return QDateTime::fromMSecsSinceEpoch(entry.modifiedMs).toString(
          QStringLiteral("yyyy-MM-dd HH:mm"));
    case PathRole:
      return entry.path;
    default:
      break;
  }
  return {};
}

QHash<int, QByteArray> ProjectsModel::roleNames() const {
  return {{NameRole, "name"},
          {CurrentRole, "current"},
          {ModifiedRole, "modified"},
          {PathRole, "path"}};
}

void ProjectsModel::setEntries(std::vector<Entry> entries) {
  beginResetModel();
  entries_ = std::move(entries);
  endResetModel();
}

const ProjectsModel::Entry* ProjectsModel::entry(int row) const {
  if (row < 0 || row >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[static_cast<std::size_t>(row)];
}

ProjectsViewModel::ProjectsViewModel(AppContext& context,
                                     std::function<void()> onSessionChanged,
                                     QObject* parent)
    : QObject(parent),
      context_(context),
      onSessionChanged_(std::move(onSessionChanged)) {
  scanProjects();
  refresh();
}

void ProjectsViewModel::newProject() {
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
  const auto base = defaultProjectName(seconds);
  auto directory = project::root() / base;
  std::error_code error;
  for (int suffix = 2; std::filesystem::exists(directory, error); ++suffix) {
    directory = project::root() / (base + "-" + std::to_string(suffix));
  }

  std::string saveError;
  if (!project::save(context_, directory, saveError)) {
    statusText_ = QStringLiteral("new failed: ") + QString::fromStdString(saveError);
    emit stateChanged();
    return;
  }
  context_.state.setCurrentProject(directory);
  statusText_ = QStringLiteral("created ") + QString::fromStdString(directory.filename().string());
  afterSessionChanged();
  scanProjects();
  for (int row = 0; row < model_.rowCount(); ++row) {
    if (const auto* entry = model_.entry(row);
        entry && entry->path == QString::fromStdString(directory.string())) {
      selectRow(row);
      break;
    }
  }
  refresh();
}

void ProjectsViewModel::saveProject() {
  auto directory = context_.state.currentProject();
  if (directory.empty()) {
    newProject();
    return;
  }
  std::string error;
  if (!project::save(context_, directory, error)) {
    statusText_ = QStringLiteral("save failed: ") + QString::fromStdString(error);
    emit stateChanged();
    return;
  }
  statusText_ = QStringLiteral("saved ") + QString::fromStdString(directory.filename().string());
  afterSessionChanged();
  scanProjects();
  refresh();
}

void ProjectsViewModel::loadRow(int row) {
  const auto* entry = model_.entry(row);
  if (entry == nullptr) return;
  std::string error;
  const std::filesystem::path directory(entry->path.toStdString());
  if (!project::load(context_, directory, error)) {
    statusText_ = QStringLiteral("load failed: ") + QString::fromStdString(error);
    log("project: load failed for " + directory.string() + ": " + error);
    emit stateChanged();
    return;
  }
  context_.state.setCurrentProject(directory);
  statusText_ = QStringLiteral("loaded ") + entry->name;
  afterSessionChanged();
  scanProjects();
  refresh();
}

void ProjectsViewModel::selectRow(int row) {
  const int clamped = row >= 0 && row < model_.rowCount() ? row : -1;
  if (clamped == selectedRow_) return;
  selectedRow_ = clamped;
  emit selectionChanged();
}

QString ProjectsViewModel::renameSelected(const QString& name) {
  const auto directory = selectedDirectory();
  if (directory.empty()) return QStringLiteral("select a project to rename");
  const auto text = name.toStdString();
  const auto error = validateProjectName(text);
  if (!error.empty()) return QString::fromStdString(error);
  if (text == directory.filename().string()) return {};
  const auto target = directory.parent_path() / text;
  std::error_code existsError;
  if (std::filesystem::exists(target, existsError)) {
    return QStringLiteral("a project with that name already exists");
  }
  std::error_code moveError;
  std::filesystem::rename(directory, target, moveError);
  if (moveError) return QStringLiteral("rename failed (is the folder open?)");

  const auto current = context_.state.currentProject();
  if (!current.empty() && current.string() == directory.string()) {
    context_.state.setCurrentProject(target);
  }
  statusText_ = QStringLiteral("renamed to ") + name;
  scanProjects();
  refresh();
  for (int row = 0; row < model_.rowCount(); ++row) {
    if (const auto* entry = model_.entry(row); entry && entry->name == name) {
      selectRow(row);
      break;
    }
  }
  return {};
}

void ProjectsViewModel::deleteSelected() {
  const auto directory = selectedDirectory();
  if (directory.empty()) {
    statusText_ = QStringLiteral("select a project to delete");
    emit stateChanged();
    return;
  }
  const auto name = QString::fromStdString(directory.filename().string());
  std::error_code error;
  std::filesystem::remove_all(directory, error);
  if (error) {
    statusText_ = QStringLiteral("delete failed: ") + name;
    emit stateChanged();
    return;
  }
  const auto current = context_.state.currentProject();
  if (!current.empty() && current.string() == directory.string()) {
    context_.state.setCurrentProject({});
    // The state no longer has a session file behind it; keep the dirty
    // indicator honest instead of implying it is saved.
    context_.state.markDirty();
  }
  statusText_ = QStringLiteral("deleted ") + name;
  scanProjects();
  refresh();
}

void ProjectsViewModel::refresh() {
  const auto current = context_.state.currentProject();
  const QString name = current.empty()
                           ? QStringLiteral("untitled - SAVE stores the current state")
                           : QString::fromStdString(current.filename().string());
  if (currentName_ != name) {
    currentName_ = name;
    emit stateChanged();
  }
  if (++refreshCounter_ % 20 == 0) scanProjects();
}

QString ProjectsViewModel::projectName(int row) const {
  const auto* entry = model_.entry(row);
  return entry == nullptr ? QString() : entry->name;
}

void ProjectsViewModel::scanProjects() {
  const QString selected =
      model_.entry(selectedRow_) != nullptr ? model_.entry(selectedRow_)->name : QString();
  const auto current = context_.state.currentProject();
  std::vector<ProjectsModel::Entry> entries;
  for (const auto& item : project::list()) {
    ProjectsModel::Entry entry;
    entry.name = QString::fromStdString(item.name);
    entry.path = QString::fromStdString(item.directory.string());
    entry.modifiedMs = item.modifiedMs;
    entry.current = !current.empty() && item.directory.string() == current.string();
    entries.push_back(entry);
  }
  model_.setEntries(std::move(entries));

  int selection = -1;
  for (int row = 0; row < model_.rowCount(); ++row) {
    if (const auto* entry = model_.entry(row);
        entry && !selected.isEmpty() && entry->name == selected) {
      selection = row;
      break;
    }
  }
  if (selection < 0 && model_.rowCount() > 0) selection = 0;
  selectedRow_ = selection;
  emit selectionChanged();
  emit stateChanged();
}

void ProjectsViewModel::projectStateChanged() {
  scanProjects();
  refresh();
}

std::filesystem::path ProjectsViewModel::selectedDirectory() const {
  if (const auto* entry = model_.entry(selectedRow_)) {
    return std::filesystem::path(entry->path.toStdString());
  }
  return context_.state.currentProject();
}

void ProjectsViewModel::afterSessionChanged() {
  if (onSessionChanged_) onSessionChanged_();
}

}  // namespace mnky::app
