#include "ProjectsScreen.hpp"

#include <string>
#include <utility>

#include "../app_state.hpp"
#include "../logging.hpp"
#include "../project_logic.hpp"
#include "../ui_style.hpp"

namespace mnky::app {

void ProjectsScreen::ProjectListModel::setEntries(std::vector<Entry> entries) {
  entries_ = std::move(entries);
}

int ProjectsScreen::ProjectListModel::getNumRows() {
  return static_cast<int>(entries_.size());
}

void ProjectsScreen::ProjectListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                        int width, int height,
                                                        bool rowIsSelected) {
  if (rowNumber < 0 || rowNumber >= static_cast<int>(entries_.size())) return;
  const auto& entry = entries_[static_cast<std::size_t>(rowNumber)];

  g.fillAll(rowIsSelected ? style::panel.brighter(0.18f)
                          : (rowNumber % 2 == 0 ? style::panel : style::panelDark));
  g.setColour(entry.current ? style::accent : style::text);
  g.setFont(style::font(14.0f, true));
  g.drawText(entry.name, 8, 0, width / 2, height, juce::Justification::centredLeft, true);

  g.setColour(style::textDim);
  g.setFont(style::font(11.0f));
  auto detail = juce::String("session  -  ") + juce::Time(entry.modifiedMs).formatted(
                                                     "%Y-%m-%d %H:%M");
  if (entry.current) detail << "  -  current";
  g.drawText(detail, width / 2, 0, width / 2 - 8, height, juce::Justification::centredRight,
             true);
}

void ProjectsScreen::ProjectListModel::listBoxItemClicked(int row, const juce::MouseEvent&) {
  if (onRowClicked) onRowClicked(row);
}

const ProjectsScreen::ProjectListModel::Entry* ProjectsScreen::ProjectListModel::entry(
    int row) const {
  if (row < 0 || row >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[static_cast<std::size_t>(row)];
}

void ProjectsScreen::DirtyDot::paint(juce::Graphics& g) {
  g.setColour(style::accent);
  g.fillEllipse(getLocalBounds().toFloat());
}

ProjectsScreen::ProjectsScreen(AppContext& context,
                               std::function<void()> onSessionChanged)
    : Screen(context), onSessionChanged_(std::move(onSessionChanged)) {
  model_.onRowClicked = [this](int row) { requestLoad(row); };

  list_.setModel(&model_);
  list_.setRowHeight(36);
  list_.setOutlineThickness(1);
  list_.setColour(juce::ListBox::backgroundColourId, style::panelDark);
  addAndMakeVisible(list_);

  current_.setFont(style::font(12.0f, true));
  current_.setColour(juce::Label::textColourId, style::text);
  current_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(current_);

  status_.setFont(style::font(11.0f));
  status_.setColour(juce::Label::textColourId, style::textDim);
  status_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(status_);

  empty_.setText("no projects yet - NEW creates one in ~/Music/mnkybox-projects",
                 juce::dontSendNotification);
  empty_.setFont(style::font(13.0f));
  empty_.setColour(juce::Label::textColourId, style::textDim);
  empty_.setJustificationType(juce::Justification::centred);
  addAndMakeVisible(empty_);

  newButton_.onClick = [this] { createNewProject(); };
  addAndMakeVisible(newButton_);
  saveButton_.onClick = [this] { saveCurrentProject(); };
  addAndMakeVisible(saveButton_);
  renameButton_.onClick = [this] { requestRename(); };
  addAndMakeVisible(renameButton_);
  deleteButton_.setColour(juce::TextButton::buttonColourId, style::record.darker(0.3f));
  deleteButton_.onClick = [this] { requestDelete(); };
  addAndMakeVisible(deleteButton_);

  dot_.setInterceptsMouseClicks(false, false);
  dot_.setVisible(false);
  addChildComponent(dot_);

  addChildComponent(keypad_);
  addChildComponent(confirm_);

  scanProjects();
  refresh();
}

void ProjectsScreen::paint(juce::Graphics& g) {
  g.fillAll(style::background);
}

void ProjectsScreen::resized() {
  auto area = getLocalBounds().reduced(6);
  auto controls = area.removeFromTop(style::kTouch);
  newButton_.setButtonText("NEW");
  saveButton_.setButtonText("SAVE");
  renameButton_.setButtonText("RENAME");
  deleteButton_.setButtonText("DELETE");
  newButton_.setBounds(controls.removeFromLeft(88).reduced(2));
  saveButton_.setBounds(controls.removeFromLeft(88).reduced(2));
  renameButton_.setBounds(controls.removeFromLeft(100).reduced(2));
  deleteButton_.setBounds(controls.removeFromLeft(100).reduced(2));
  dot_.setBounds(saveButton_.getRight() - 11, saveButton_.getY() + 3, 7, 7);

  current_.setBounds(area.removeFromTop(18));
  status_.setBounds(area.removeFromTop(16));
  list_.setBounds(area);
  empty_.setBounds(area);

  keypad_.setBounds(getLocalBounds());
  confirm_.setBounds(getLocalBounds());
}

void ProjectsScreen::refresh() {
  const auto current = context_.state.currentProject();
  const auto currentText =
      current == juce::File() ? juce::String("project: untitled - SAVE stores the current state")
                              : "project: " + current.getFileName();
  if (current_.getText() != currentText) current_.setText(currentText, juce::dontSendNotification);

  const bool dirty = context_.state.dirty.load();
  if (dot_.isVisible() != dirty) dot_.setVisible(dirty);
  syncButtons();

  if (++refreshCounter_ % 20 == 0) scanProjects();
}

void ProjectsScreen::scanProjects() {
  juce::String selected;
  if (const auto* entry = model_.entry(list_.getSelectedRow())) selected = entry->name;

  const auto current = context_.state.currentProject();
  std::vector<ProjectListModel::Entry> entries;
  for (const auto& item : project::list()) {
    ProjectListModel::Entry entry;
    entry.name = item.name;
    entry.directory = item.directory;
    entry.modifiedMs = item.modifiedMs;
    entry.current = current != juce::File() &&
                    item.directory.getFullPathName() == current.getFullPathName();
    entries.push_back(entry);
  }
  const bool empty = entries.empty();
  model_.setEntries(std::move(entries));
  list_.updateContent();
  list_.repaint();
  empty_.setVisible(empty);

  int selection = -1;
  for (int row = 0; row < model_.getNumRows(); ++row) {
    if (const auto* entry = model_.entry(row)) {
      if (selected.isNotEmpty() && entry->name == selected) {
        selection = row;
        break;
      }
    }
  }
  if (selection < 0 && model_.getNumRows() > 0) selection = 0;
  if (selection >= 0) list_.selectRow(selection, true);
  syncButtons();
}

void ProjectsScreen::createNewProject() {
  const auto seconds = juce::Time::currentTimeMillis() / 1000;
  const auto base = juce::String(defaultProjectName(seconds));
  auto directory = project::root().getChildFile(base);
  for (int suffix = 2; directory.exists(); ++suffix) {
    directory = project::root().getChildFile(base + "-" + juce::String(suffix));
  }

  std::string error;
  if (!project::save(context_, directory, error)) {
    status_.setText("new failed: " + juce::String(error), juce::dontSendNotification);
    return;
  }
  context_.state.setCurrentProject(directory);
  status_.setText("created " + directory.getFileName(), juce::dontSendNotification);
  if (onSessionChanged_) onSessionChanged_();
  scanProjects();
  for (int row = 0; row < model_.getNumRows(); ++row) {
    if (const auto* entry = model_.entry(row); entry && entry->name == directory.getFileName()) {
      list_.selectRow(row, true);
      break;
    }
  }
}

void ProjectsScreen::saveCurrentProject() {
  auto directory = context_.state.currentProject();
  if (directory == juce::File()) {
    createNewProject();
    return;
  }
  std::string error;
  if (!project::save(context_, directory, error)) {
    status_.setText("save failed: " + juce::String(error), juce::dontSendNotification);
    return;
  }
  status_.setText("saved " + directory.getFileName(), juce::dontSendNotification);
  if (onSessionChanged_) onSessionChanged_();
  scanProjects();
}

void ProjectsScreen::requestLoad(int row) {
  const auto* entry = model_.entry(row);
  if (entry == nullptr) return;
  const auto directory = entry->directory;
  if (context_.state.dirty.load()) {
    confirm_.open("LOAD PROJECT",
                  "Discard unsaved changes and load " + entry->name + "?",
                  "DISCARD + LOAD");
    confirm_.onConfirm = [this, directory] { loadProject(directory); };
    return;
  }
  loadProject(directory);
}

void ProjectsScreen::loadProject(const juce::File& directory) {
  std::string error;
  if (!project::load(context_, directory, error)) {
    status_.setText("load failed: " + juce::String(error), juce::dontSendNotification);
    log("project: load failed for " + directory.getFullPathName() + ": " +
        juce::String(error));
    return;
  }
  context_.state.setCurrentProject(directory);
  status_.setText("loaded " + directory.getFileName(), juce::dontSendNotification);
  if (onSessionChanged_) onSessionChanged_();
  scanProjects();
}

juce::File ProjectsScreen::selectedDirectory() const {
  if (const auto* entry = model_.entry(list_.getSelectedRow())) return entry->directory;
  return context_.state.currentProject();
}

void ProjectsScreen::requestDelete() {
  const auto directory = selectedDirectory();
  if (directory == juce::File()) {
    status_.setText("select a project to delete", juce::dontSendNotification);
    return;
  }
  const auto name = directory.getFileName();
  confirm_.open("DELETE PROJECT",
                "Delete " + name +
                    "? The folder and its project.mnkybox are removed. This cannot be undone.",
                "DELETE");
  confirm_.onConfirm = [this, directory, name] {
    const bool removed = directory.deleteRecursively();
    if (!removed) {
      status_.setText("delete failed: " + name, juce::dontSendNotification);
      return;
    }
    const auto current = context_.state.currentProject();
    if (current != juce::File() &&
        current.getFullPathName() == directory.getFullPathName()) {
      context_.state.setCurrentProject(juce::File());
      // The state no longer has a session file behind it; keep the dirty
      // indicator honest instead of implying it is saved.
      context_.state.markDirty();
    }
    status_.setText("deleted " + name, juce::dontSendNotification);
    scanProjects();
  };
}

void ProjectsScreen::requestRename() {
  const auto directory = selectedDirectory();
  if (directory == juce::File()) {
    status_.setText("select a project to rename", juce::dontSendNotification);
    return;
  }
  showKeypad(directory);
}

void ProjectsScreen::showKeypad(const juce::File& directory) {
  keypad_.onAccept = [this, directory](const juce::String& name) -> juce::String {
    const auto error = validateProjectName(name.toStdString());
    if (!error.empty()) return juce::String(error);
    if (name == directory.getFileName()) return {};
    const auto target = directory.getParentDirectory().getChildFile(name);
    if (target.exists()) return "a project with that name already exists";
    if (!directory.moveFileTo(target)) return "rename failed (is the folder open?)";

    const auto current = context_.state.currentProject();
    if (current != juce::File() &&
        current.getFullPathName() == directory.getFullPathName()) {
      context_.state.setCurrentProject(target);
    }
    status_.setText("renamed to " + name, juce::dontSendNotification);
    scanProjects();
    for (int row = 0; row < model_.getNumRows(); ++row) {
      if (const auto* entry = model_.entry(row); entry && entry->name == name) {
        list_.selectRow(row, true);
        break;
      }
    }
    return {};
  };
  keypad_.onCancel = {};
  keypad_.open("RENAME PROJECT", directory.getFileName());
}

void ProjectsScreen::syncButtons() {
  const bool hasProject = selectedDirectory() != juce::File();
  renameButton_.setEnabled(hasProject);
  deleteButton_.setEnabled(hasProject);
}

}  // namespace mnky::app
