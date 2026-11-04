#include "FxScreen.hpp"

#include "../app_state.hpp"
#include "../audio_glue.hpp"
#include "../logic.hpp"
#include "../logging.hpp"
#include "../mixer_model.hpp"
#include "../ui_style.hpp"

namespace mnky::app {
namespace {
constexpr int kParamLabelWidth = 84;
constexpr int kParamRowHeight = 52;
constexpr int kParamColumns = 2;
}  // namespace

FxSlotPanel::FxSlotPanel(AppContext& context, int slotIndex)
    : context_(context), slot_(context.fx.slot(slotIndex)), slotIndex_(slotIndex) {
  title_.setText(juce::String("SEND ") + juce::String::charToString(
                                             static_cast<juce::juce_wchar>('A' + slotIndex)),
                 juce::dontSendNotification);
  title_.setFont(style::font(16.0f, true));
  title_.setColour(juce::Label::textColourId, style::text);
  addAndMakeVisible(title_);

  plugin_.setButtonText("pick plugin…");
  plugin_.onClick = [this] { choosePlugin(); };
  addAndMakeVisible(plugin_);

  load_.setButtonText("load");
  load_.onClick = [this] { choosePlugin(); };
  addAndMakeVisible(load_);

  eject_.setButtonText("eject");
  eject_.onClick = [this] { ejectPlugin(); };
  addAndMakeVisible(eject_);

  saveState_.setButtonText("save state");
  saveState_.onClick = [this] { chooseStateFile(true); };
  addAndMakeVisible(saveState_);

  loadState_.setButtonText("load state");
  loadState_.onClick = [this] { chooseStateFile(false); };
  addAndMakeVisible(loadState_);

  returnLabel_.setText("RETURN", juce::dontSendNotification);
  returnLabel_.setFont(style::font(12.0f, true));
  returnLabel_.setColour(juce::Label::textColourId, style::textDim);
  addAndMakeVisible(returnLabel_);

  return_.setSliderStyle(juce::Slider::LinearHorizontal);
  return_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 16);
  return_.setRange(static_cast<double>(kMinGainDb), static_cast<double>(kMaxGainDb), 0.1);
  return_.setValue(context_.mixer.fxReturnDb(slotIndex_), juce::dontSendNotification);
  return_.setTextValueSuffix(" dB");
  return_.onValueChange = [this] {
    context_.mixer.setFxReturnDb(slotIndex_, static_cast<float>(return_.getValue()));
  };
  addAndMakeVisible(return_);

  paramViewport_.setScrollBarsShown(true, false, true, false);
  paramViewport_.setViewedComponent(&paramHolder_, false);
  addAndMakeVisible(paramViewport_);

  status_.setFont(style::font(12.0f));
  status_.setColour(juce::Label::textColourId, style::textDim);
  status_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(status_);

  refresh();
}

FxSlotPanel::~FxSlotPanel() = default;

void FxSlotPanel::paint(juce::Graphics& g) {
  style::drawPanel(g, getLocalBounds().toFloat().reduced(1.0f));
}

void FxSlotPanel::resized() {
  auto area = getLocalBounds().reduced(6, 4);

  auto row = area.removeFromTop(style::kTouch);
  title_.setBounds(row.removeFromLeft(66));
  loadState_.setBounds(row.removeFromRight(92));
  saveState_.setBounds(row.removeFromRight(92));
  eject_.setBounds(row.removeFromRight(66));
  load_.setBounds(row.removeFromRight(58));
  plugin_.setBounds(row.reduced(2, 0));

  auto row2 = area.removeFromTop(style::kTouch);
  returnLabel_.setBounds(row2.removeFromLeft(52));
  return_.setBounds(row2.removeFromLeft(juce::jmax(200, row2.getWidth() / 2)));
  status_.setBounds(row2.reduced(6, 0));

  paramViewport_.setBounds(area);
  // Only a width change changes the slider grid; rebuilding on every resize
  // would pull the sliders back to the plugin's last processed values.
  if (!paramSliders_.isEmpty() && paramViewport_.getWidth() != paramLayoutWidth_) {
    rebuildParams();
  }
}

void FxSlotPanel::refresh() {
  const auto path = slot_.pluginPath();
  const auto error = slot_.lastError();

  const auto pluginText = path.isEmpty() ? juce::String("no plugin — pick a .clap")
                                         : juce::File(path).getFileName();
  if (plugin_.getButtonText() != pluginText) plugin_.setButtonText(pluginText);

  juce::String text = path.isEmpty() ? "send idle; return passes the bus through"
                                     : "loaded";
  if (!error.isEmpty()) text << "  ·  " << error;
  if (status_.getText() != text) status_.setText(text, juce::dontSendNotification);
}

void FxSlotPanel::loadDefaultIfEmpty() {
  if (!slot_.pluginPath().isEmpty()) return;
  const auto candidate = FxSlot::defaultPluginPath();
  if (candidate.isEmpty()) {
    log("fx " + juce::String(slotIndex_) + ": no default plugin found");
    refresh();
    return;
  }
  loadPlugin(juce::File(candidate));
}

void FxSlotPanel::choosePlugin() {
  auto start = slot_.pluginPath().isEmpty() ? juce::File() : juce::File(slot_.pluginPath());
  if (!start.exists()) start = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                  .getChildFile("Library/Audio/Plug-Ins/CLAP");
  if (!start.exists()) start = context_.state.takeDirectory();

  chooser_ = std::make_unique<juce::FileChooser>("Load CLAP plugin", start,
                                                 "*.clap;*.dylib;*.so");
  chooser_->launchAsync(juce::FileBrowserComponent::openMode |
                            juce::FileBrowserComponent::canSelectFiles |
                            juce::FileBrowserComponent::canSelectDirectories,
                        [this](const juce::FileChooser& chooser) {
                          const auto file = chooser.getResult();
                          if (file == juce::File()) return;
                          loadPlugin(file);
                        });
}

void FxSlotPanel::loadPlugin(const juce::File& file) {
  slot_.load(file.getFullPathName(), context_.audio.sampleRate(), audio::kBlockSize);
  rebuildParams();
  resized();
  refresh();
  repaint();
}

void FxSlotPanel::ejectPlugin() {
  slot_.unload();
  rebuildParams();
  refresh();
  repaint();
}

juce::File FxSlotPanel::defaultStateFile() const {
  const auto name = slotIndex_ == 1 ? "fx-slot-B.state" : "fx-slot-A.state";
  return context_.state.takeDirectory().getParentDirectory().getChildFile(name);
}

void FxSlotPanel::chooseStateFile(bool save) {
  const int flags = save ? (juce::FileBrowserComponent::saveMode |
                            juce::FileBrowserComponent::warnAboutOverwriting)
                         : (juce::FileBrowserComponent::openMode |
                            juce::FileBrowserComponent::canSelectFiles);
  chooser_ = std::make_unique<juce::FileChooser>(save ? "Save FX state" : "Load FX state",
                                                 defaultStateFile(), "*.state");
  chooser_->launchAsync(flags, [this, save](const juce::FileChooser& chooser) {
    const auto file = chooser.getResult();
    if (file == juce::File()) return;
    if (save) {
      slot_.saveState(file);
    } else {
      slot_.loadState(file);
      rebuildParams();
    }
    refresh();
  });
}

void FxSlotPanel::rebuildParams() {
  paramLabels_.clear();
  paramSliders_.clear();
  paramIds_.clear();

  const auto params = slot_.params();
  paramLayoutWidth_ = paramViewport_.getWidth();
  const int usableWidth = juce::jmax(240, paramViewport_.getWidth());
  const int columnWidth = usableWidth / kParamColumns;
  const int rows = (static_cast<int>(params.size()) + kParamColumns - 1) / kParamColumns;
  paramHolder_.setSize(usableWidth, juce::jmax(1, rows) * kParamRowHeight);

  for (int i = 0; i < static_cast<int>(params.size()); ++i) {
    const auto& param = params[static_cast<std::size_t>(i)];

    auto* label = paramLabels_.add(new juce::Label());
    label->setText(juce::String(param.name), juce::dontSendNotification);
    label->setFont(style::font(12.0f, true));
    label->setColour(juce::Label::textColourId, style::textDim);
    paramHolder_.addAndMakeVisible(label);

    auto* slider = paramSliders_.add(new juce::Slider());
    slider->setSliderStyle(juce::Slider::LinearHorizontal);
    slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 16);
    const double range = param.max - param.min;
    if (range > 0.0) {
      slider->setRange(param.min, param.max, range / 1000.0);
      slider->setValue(param.value, juce::dontSendNotification);
      const clap_id id = param.id;
      slider->onValueChange = [this, slider, id] { slot_.setParam(id, slider->getValue()); };
    } else {
      slider->setRange(0.0, 1.0, 0.0);
      slider->setEnabled(false);
    }
    paramHolder_.addAndMakeVisible(slider);
    paramIds_.push_back(param.id);

    const int row = i / kParamColumns;
    const int column = i % kParamColumns;
    auto cell = juce::Rectangle<int>(column * columnWidth, row * kParamRowHeight,
                                     columnWidth, kParamRowHeight)
                    .reduced(4, 4);
    label->setBounds(cell.removeFromLeft(juce::jmin(kParamLabelWidth, cell.getWidth() / 2)));
    slider->setBounds(cell);
  }
}

FxScreen::FxScreen(AppContext& context) : Screen(context) {
  slots_.add(new FxSlotPanel(context, 0));
  slots_.add(new FxSlotPanel(context, 1));
  for (auto* slot : slots_) addAndMakeVisible(slot);
}

void FxScreen::resized() {
  auto area = getLocalBounds();
  const int half = area.getHeight() / 2;
  slots_[0]->setBounds(area.removeFromTop(half));
  slots_[1]->setBounds(area);
}

void FxScreen::refresh() {
  for (auto* slot : slots_) slot->refresh();
}

void FxScreen::loadDefaults() {
  slots_[0]->loadDefaultIfEmpty();
}

}  // namespace mnky::app
