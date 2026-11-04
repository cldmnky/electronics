# Third-party dependencies (pinned)

| Dep | Version | Form | License |
|---|---|---|---|
| JUCE | 9.0.5 | CMake FetchContent (`juce-framework/JUCE`, `GIT_TAG 9.0.5`, `GIT_SHALLOW ON`) | GPLv3 / commercial (see JUCE licensing; this project has no license decision yet — do not ship binaries until that is resolved) |
| CLAP | 1.2.10 | CMake FetchContent (`free-audio/clap`, `GIT_TAG 1.2.10`) — headers only (`include/clap/clap.h`) | MIT |
| RtMidi | 6.1.0 | Vendored `rtmidi/RtMidi.{h,cpp}` from `thestk/rtmidi` tag 6.1.0 | MIT (see header comments in the files) |

Rules: pin exact versions here and in CMake; never float on a branch.
Compile RtMidi with `__MACOSX_CORE__` on macOS, `__LINUX_ALSA__` on Linux.
JUCE modules used by the app: `juce_core juce_events juce_data_structures
juce_graphics juce_gui_basics juce_audio_basics juce_audio_devices
juce_audio_formats` — nothing else without a written reason.
