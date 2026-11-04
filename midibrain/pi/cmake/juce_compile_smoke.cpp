// Compile-only smoke test for the pinned JUCE modules (see ../CMakeLists.txt).
//
// The wave-2 `app/` team owns the real juce_add_gui_app target; this translation
// unit only proves that juce_gui_basics + juce_audio_devices (and the modules
// they pull in) compile with the toolchain on the dev machine and on the Pi.
// It is built into the static library mnky_juce_smoke, is never linked into the
// product, never run, and claims no audio or UI behaviour.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mnkybox::smoke
{
// ARCHITECTURE.md requires >= 44 px touch targets on the 800x480 panel.
constexpr int minTouchTargetPx = 44;

// Skeleton only: the panel buttons themselves are wave-2 UI work.
class PanelButton : public juce::Component
{
public:
    explicit PanelButton (juce::String buttonLabel);

    void paint (juce::Graphics& graphics) override;

    // Device types the Pi backend reports through RtMidi/CoreAudio/ALSA.
    static juce::StringArray audioDeviceTypeNames();

    static bool fitsSevenInchPanel (juce::Rectangle<int> displayArea)
    {
        return displayArea.getWidth() == 800 && displayArea.getHeight() == 480;
    }

private:
    juce::String label;
};

PanelButton::PanelButton (juce::String buttonLabel)
    : label (std::move (buttonLabel))
{
    setSize (minTouchTargetPx, minTouchTargetPx);
    setWantsKeyboardFocus (false);
}

void PanelButton::paint (juce::Graphics& graphics)
{
    // Compile smoke: touch graphics + colour + font APIs used by the app.
    graphics.fillAll (juce::Colours::black.withAlpha (0.85f));
    graphics.setColour (juce::Colours::white);
    graphics.setFont (juce::FontOptions (14.0f));
    graphics.drawText (label, getLocalBounds(), juce::Justification::centred);
}

juce::StringArray PanelButton::audioDeviceTypeNames()
{
    juce::AudioDeviceManager manager;
    juce::OwnedArray<juce::AudioIODeviceType> types;
    manager.createAudioDeviceTypes (types);

    juce::StringArray names;
    for (auto* type : types)
        names.add (type->getTypeName());

    return names;
}
} // namespace mnkybox::smoke
