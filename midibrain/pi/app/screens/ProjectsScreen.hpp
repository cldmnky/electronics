#pragma once
// Projects screen (6th tab): one directory per project under
// ~/Music/mnkybox-projects, saved as session-format project.mnkybox. Tapping a
// row loads it (after a dirty confirmation), and NEW/SAVE/RENAME/DELETE manage
// the folder. Rename uses the shared touch keyboard and refuses unsafe names.
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "Screen.hpp"

#include "../confirm_panel.hpp"
#include "../keypad.hpp"
#include "../project_io.hpp"

namespace mnky::app {

class ProjectsScreen final : public Screen {
 public:
  // Called after a successful new/save/load so MainComponent can reset the
  // dirty fingerprint and refresh the FX parameter view.
  explicit ProjectsScreen(AppContext& context, std::function<void()> onSessionChanged);

  void paint(juce::Graphics& g) override;
  void resized() override;
  void refresh() override;

  // Re-scan the project root and keep the selected project when possible.
  void scanProjects();

 private:
  class ProjectListModel final : public juce::ListBoxModel {
   public:
    struct Entry {
      juce::String name;
      juce::File directory;
      juce::int64 modifiedMs = 0;
      bool current = false;
    };

    void setEntries(std::vector<Entry> entries);
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                          bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    const Entry* entry(int row) const;

    std::function<void(int)> onRowClicked;

   private:
    std::vector<Entry> entries_;
  };

  class DirtyDot final : public juce::Component {
   public:
    void paint(juce::Graphics& g) override;
  };

  void createNewProject();
  void saveCurrentProject();
  void requestLoad(int row);
  void loadProject(const juce::File& directory);
  void requestDelete();
  void requestRename();
  void showKeypad(const juce::File& directory);
  juce::File selectedDirectory() const;
  void syncButtons();

  std::function<void()> onSessionChanged_;
  ProjectListModel model_;
  juce::ListBox list_;
  juce::Label current_;
  juce::Label status_;
  juce::Label empty_;
  juce::TextButton newButton_;
  juce::TextButton saveButton_;
  juce::TextButton renameButton_;
  juce::TextButton deleteButton_;
  DirtyDot dot_;
  Keypad keypad_;
  ConfirmPanel confirm_;
  int refreshCounter_ = 0;

  JUCE_DECLARE_NON_COPYABLE(ProjectsScreen)
};

}  // namespace mnky::app
