#include "RecorderScreen.hpp"

#include "../app_state.hpp"
#include "../audio_glue.hpp"
#include "../link_client.hpp"
#include "../logic.hpp"
#include "../mixer_model.hpp"
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
    context_.state.postIntent(Intent{Intent::Kind::RecToggle, 0});
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
  addAndMakeVisible(takeList_);

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
  status_.setBounds(controlsRow.reduced(6, 0));

  disk_.setBounds(area.removeFromTop(20));
  takeList_.setBounds(area);
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
  // ListBox::updateContent() only re-lays-out the rows and repaints the ones
  // whose bounds changed. A scan that keeps the row count (same take, new file
  // sizes) would otherwise keep showing the sizes read while the WAVs were
  // still open (0 B / 00:00), so repaint the rows explicitly.
  takeList_.repaint();
}

}  // namespace mnky::app
