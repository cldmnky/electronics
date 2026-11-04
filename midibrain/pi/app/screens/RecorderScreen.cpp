#include "RecorderScreen.hpp"

#include "../app_state.hpp"
#include "../audio_glue.hpp"
#include "../link_client.hpp"
#include "../logic.hpp"
#include "../mixer_model.hpp"
#include "../project_logic.hpp"
#include "../ui_style.hpp"

namespace mnky::app {

void RecorderScreen::TakeListModel::setEntries(std::vector<Entry> entries) {
  entries_ = std::move(entries);
}

int RecorderScreen::TakeListModel::getNumRows() {
  return static_cast<int>(entries_.size());
}

void RecorderScreen::TakeListModel::paintListBoxItem(int rowNumber, juce::Graphics& g,
                                                     int width, int height,
                                                     bool rowIsSelected) {
  if (rowNumber < 0 || rowNumber >= static_cast<int>(entries_.size())) return;
  const auto& entry = entries_[static_cast<std::size_t>(rowNumber)];

  g.fillAll(rowIsSelected ? style::panel.brighter(0.18f)
                          : (rowNumber % 2 == 0 ? style::panel : style::panelDark));
  g.setColour(style::text);
  g.setFont(style::font(13.0f, true));
  g.drawText(entry.name, 8, 0, width / 2, height, juce::Justification::centredLeft, true);
  g.setColour(style::textDim);
  g.setFont(style::font(12.0f));
  const auto detail = juce::String(entry.files) + " files  -  " +
                      juce::String(formatDuration(entry.seconds)) + "  -  " +
                      juce::String(formatBytes(entry.bytes));
  g.drawText(detail, width / 2, 0, width / 2 - 8, height, juce::Justification::centredRight,
             true);
}

void RecorderScreen::TakeListModel::selectedRowsChanged(int lastRowSelected) {
  if (onRowSelected) onRowSelected(lastRowSelected);
}

const RecorderScreen::TakeListModel::Entry* RecorderScreen::TakeListModel::entry(
    int row) const {
  if (row < 0 || row >= static_cast<int>(entries_.size())) return nullptr;
  return &entries_[static_cast<std::size_t>(row)];
}

RecorderScreen::RecorderScreen(AppContext& context) : Screen(context) {
  formats_.registerBasicFormats();

  transport_.setFont(style::font(24.0f, true));
  addAndMakeVisible(transport_);

  clock_.setFont(style::font(14.0f));
  clock_.setColour(juce::Label::textColourId, style::textDim);
  clock_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(clock_);

  for (int index = 0; index < kNumStrips; ++index) {
    auto* button = armButtons_.add(new juce::TextButton(juce::String(index + 1)));
    button->setClickingTogglesState(true);
    // Armed is a selected state: the theme fills it with the record colour.
    button->setColour(juce::TextButton::buttonOnColourId, style::record);
    button->onClick = [this, index] {
      context_.mixer.setArm(index, armButtons_[index]->getToggleState());
      syncArmButtons();
    };
    addAndMakeVisible(button);
  }

  startStop_.setButtonText("START TAKE");
  startStop_.setColour(juce::TextButton::buttonColourId, style::record.darker(0.3f));
  startStop_.onClick = [this] {
    // Amount 1 = immediate start/stop; the panel REC button uses 0 so it can
    // pair with PLAY inside the 300 ms chord window.
    context_.state.postIntent(Intent{Intent::Kind::RecToggle, 1});
  };
  addAndMakeVisible(startStop_);

  directoryButton_.setButtonText("...");
  directoryButton_.onClick = [this] { chooseDirectory(); };
  addAndMakeVisible(directoryButton_);

  status_.setFont(style::font(12.0f));
  status_.setColour(juce::Label::textColourId, style::textDim);
  status_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(status_);

  disk_.setFont(style::font(12.0f));
  disk_.setColour(juce::Label::textColourId, style::textDim);
  addAndMakeVisible(disk_);

  takeList_.setModel(&takeModel_);
  takeList_.setRowHeight(26);
  takeList_.setOutlineThickness(1);
  takeModel_.onRowSelected = [this](int) { syncTakeButtons(); };
  addAndMakeVisible(takeList_);

  renameTake_.setButtonText("RENAME");
  renameTake_.onClick = [this] { requestRenameTake(); };
  addAndMakeVisible(renameTake_);
  deleteTake_.setButtonText("DELETE");
  deleteTake_.setColour(juce::TextButton::buttonColourId, style::record.darker(0.3f));
  deleteTake_.onClick = [this] { requestDeleteTake(); };
  addAndMakeVisible(deleteTake_);

  // Overlays stay hidden until a rename/delete is requested; addChildComponent
  // keeps addAndMakeVisible from showing them at startup.
  addChildComponent(keypad_);
  addChildComponent(confirm_);

  syncArmButtons();
  scanTakes();
  refresh();
}

void RecorderScreen::paint(juce::Graphics& g) {
  g.fillAll(style::background);
}

void RecorderScreen::resized() {
  auto area = getLocalBounds().reduced(6);

  auto transportRow = area.removeFromTop(style::kTouch);
  transport_.setBounds(transportRow.removeFromLeft(300));
  clock_.setBounds(transportRow);

  // 18 arm toggles in two rows of nine; each cell is >= 44 px wide.
  const int cellWidth = juce::jmax(style::kTouch, area.getWidth() / 9);
  for (int row = 0; row < 2; ++row) {
    auto rowArea = area.removeFromTop(style::kTouch);
    for (int column = 0; column < 9; ++column) {
      armButtons_[row * 9 + column]->setBounds(rowArea.removeFromLeft(cellWidth).reduced(2));
    }
  }

  auto controlsRow = area.removeFromTop(style::kTouch);
  startStop_.setBounds(controlsRow.removeFromLeft(150).reduced(2));
  directoryButton_.setBounds(controlsRow.removeFromLeft(style::kTouch).reduced(2));
  renameTake_.setBounds(controlsRow.removeFromLeft(90).reduced(2));
  deleteTake_.setBounds(controlsRow.removeFromLeft(90).reduced(2));
  status_.setBounds(controlsRow.reduced(6, 0));

  disk_.setBounds(area.removeFromTop(20));
  takeList_.setBounds(area);
  keypad_.setBounds(getLocalBounds());
  confirm_.setBounds(getLocalBounds());
}

void RecorderScreen::refresh() {
  syncArmButtons();
  updateReadouts();
  // Directory scanning hits the filesystem, so only every ~1.3 s.
  if (++refreshCounter_ % 20 == 0) scanTakes();
}

void RecorderScreen::takeStateChanged() {
  scanTakes();
  updateReadouts();
}

void RecorderScreen::syncArmButtons() {
  for (int index = 0; index < armButtons_.size(); ++index) {
    const bool armed = context_.mixer.arm(index);
    auto* button = armButtons_[index];
    if (button->getToggleState() != armed) {
      button->setToggleState(armed, juce::dontSendNotification);
    }
  }
}

void RecorderScreen::updateReadouts() {
  const auto status = context_.link.status();
  const bool recording = context_.audio.recording();

  const auto transportText =
      juce::String(recording ? "REC " : (status.rolling ? "ROLLING " : "STOPPED ")) +
      juce::String(formatBarBeatTick(status.tick));
  if (transport_.getText() != transportText) transport_.setText(transportText, juce::dontSendNotification);

  auto clockText = juce::String(formatTempo(status.tempoX100)) + "  -  tick " +
                   juce::String(static_cast<juce::int64>(status.tick));
  if (!status.linkUp) clockText << "  -  link down";
  if (clock_.getText() != clockText) clock_.setText(clockText, juce::dontSendNotification);

  const auto buttonText = recording ? "STOP TAKE" : "START TAKE";
  if (startStop_.getButtonText() != buttonText) startStop_.setButtonText(buttonText);

  const auto take = context_.audio.lastTake();
  juce::String statusText;
  if (recording) {
    statusText = "recording take  -  " + context_.state.takeDirectory().getFullPathName();
  } else if (take.files > 0) {
    const double seconds =
        context_.audio.sampleRate() > 0.0
            ? static_cast<double>(take.frames) / context_.audio.sampleRate()
            : 0.0;
    statusText = "last take " + juce::File(take.path).getFileName() + "  -  " +
                 juce::String(take.files) + " files  -  " + juce::String(formatDuration(seconds));
  } else {
    statusText = "arm a channel, then START TAKE";
  }
  if (status_.getText() != statusText) status_.setText(statusText, juce::dontSendNotification);

  const auto directory = context_.state.takeDirectory();
  const auto freeBytes = directory.getBytesFreeOnVolume();
  const auto totalBytes = directory.getVolumeTotalSize();
  auto diskText = "disk " + juce::String(formatBytes(freeBytes)) + " free of " +
                  juce::String(formatBytes(totalBytes)) + "  -  drops " +
                  juce::String(static_cast<juce::int64>(context_.audio.drops()));
  if (disk_.getText() != diskText) disk_.setText(diskText, juce::dontSendNotification);
}

void RecorderScreen::chooseDirectory() {
  chooser_ = std::make_unique<juce::FileChooser>("Take directory",
                                                context_.state.takeDirectory(), juce::String());
  chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                            juce::FileBrowserComponent::canSelectDirectories,
                        [this](const juce::FileChooser& result) {
                          const auto directory = result.getResult();
                          if (directory == juce::File()) return;
                          context_.state.setTakeDirectory(directory);
                          takeStateChanged();
                        });
}

void RecorderScreen::scanTakes() {
  juce::String selected;
  if (const auto* entry = takeModel_.entry(takeList_.getSelectedRow())) {
    selected = entry->name;
  }
  std::vector<TakeListModel::Entry> entries;
  juce::Array<juce::File> children;
  context_.state.takeDirectory().findChildFiles(children, juce::File::findDirectories, false,
                                                "take-*");
  children.sort();

  // Newest first: take names start with a UTC timestamp, so lexical order is
  // chronological.
  for (int index = children.size() - 1; index >= 0; --index) {
    const auto directory = children.getReference(index);
    TakeListModel::Entry entry;
    entry.name = directory.getFileName();

    juce::Array<juce::File> files;
    directory.findChildFiles(files, juce::File::findFiles, false, "*.wav");
    entry.files = files.size();
    for (const auto& file : files) entry.bytes += file.getSize();

    const auto master = directory.getChildFile("master.wav");
    if (master.existsAsFile()) {
      std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(master));
      if (reader != nullptr && reader->sampleRate > 0.0) {
        entry.seconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
      }
    }
    entries.push_back(entry);
  }

  takeModel_.setEntries(std::move(entries));
  takeList_.updateContent();

  int selection = -1;
  for (int row = 0; row < takeModel_.getNumRows(); ++row) {
    if (const auto* entry = takeModel_.entry(row); entry && entry->name == selected) {
      selection = row;
      break;
    }
  }
  if (selection < 0 && takeModel_.getNumRows() > 0) selection = 0;
  if (selection >= 0) takeList_.selectRow(selection, true);

  // ListBox::updateContent() only re-lays-out the rows and repaints the ones
  // whose bounds changed. A scan that keeps the row count (same take, new file
  // sizes) would otherwise keep showing the sizes read while the WAVs were
  // still open (0 B / 00:00), so repaint the rows explicitly.
  takeList_.repaint();
  syncTakeButtons();
}

juce::File RecorderScreen::selectedTakeDirectory() const {
  const auto* entry = takeModel_.entry(takeList_.getSelectedRow());
  if (entry == nullptr) return {};
  return context_.state.takeDirectory().getChildFile(entry->name);
}

void RecorderScreen::syncTakeButtons() {
  const bool ready = !context_.audio.recording() && selectedTakeDirectory() != juce::File();
  renameTake_.setEnabled(ready);
  deleteTake_.setEnabled(ready);
}

void RecorderScreen::requestRenameTake() {
  const auto directory = selectedTakeDirectory();
  if (directory == juce::File() || context_.audio.recording()) return;
  keypad_.onAccept = [this, directory](const juce::String& name) -> juce::String {
    if (context_.audio.recording()) return "stop the take before renaming";
    const auto error = validateTakeName(name.toStdString());
    if (!error.empty()) return juce::String(error);
    if (name == directory.getFileName()) return {};
    const auto target = directory.getParentDirectory().getChildFile(name);
    if (target.exists()) return "a take with that name already exists";
    if (!directory.moveFileTo(target)) return "rename failed (is the take open?)";
    takeStateChanged();
    for (int row = 0; row < takeModel_.getNumRows(); ++row) {
      if (const auto* entry = takeModel_.entry(row); entry && entry->name == name) {
        takeList_.selectRow(row, true);
        break;
      }
    }
    return {};
  };
  keypad_.onCancel = {};
  keypad_.open("RENAME TAKE", directory.getFileName());
}

void RecorderScreen::requestDeleteTake() {
  const auto directory = selectedTakeDirectory();
  if (directory == juce::File() || context_.audio.recording()) return;
  const auto name = directory.getFileName();
  confirm_.open("DELETE TAKE",
                "Delete " + name + "? All WAV files in the take folder are removed.",
                "DELETE");
  confirm_.onConfirm = [this, directory, name] {
    if (!directory.deleteRecursively()) {
      status_.setText("delete failed: " + name, juce::dontSendNotification);
      return;
    }
    takeStateChanged();
  };
}

}  // namespace mnky::app
