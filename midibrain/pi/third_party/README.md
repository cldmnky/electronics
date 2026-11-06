# Third-party dependencies (pinned)

| Dep | Version | Form | License |
|---|---|---|---|
| Qt | 6 (code to the 6.4-compatible QML subset; verified 6.12 macOS / 6.4.2 Debian) | System packages (`brew install qt`; `qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qml6-module-qtquick-controls qt6-wayland`) + `find_package(Qt6 ...Quick Multimedia)` | LGPLv3 / commercial (no license decision yet — do not ship binaries until that is resolved; replaces JUCE, removed for multitouch reasons, see decisions log) |
| CLAP | 1.2.10 | CMake FetchContent (`free-audio/clap`, `GIT_TAG 1.2.10`) — headers only (`include/clap/clap.h`) | MIT |
| RtMidi | 6.1.0 | Vendored `rtmidi/RtMidi.{h,cpp}` from `thestk/rtmidi` tag 6.1.0 | MIT (see header comments in the files) |

Rules: pin exact versions here and in CMake; never float on a branch.
JSON in C++: use **nlohmann/json** (single header `json.hpp`, MIT) if a JSON
parser/writer is needed — pin an exact release in the table above before first
use. Do not hand-roll new JSON code. (`session/session.cpp` predates this rule
and keeps its minimal writer/parser for the frozen session format; its known
failure mode was escaping C++ string literals in test fixtures, not the JSON
layer itself.)
Compile RtMidi with `__MACOSX_CORE__` on macOS, `__LINUX_ALSA__` on Linux.
Qt Multimedia connects to PipeWire through its PulseAudio backend
(`pipewire-pulse` on the image) — no JACK-API code anywhere; see
`pi/BUILDROOT-AUDIO.md` for the image-side contract and the Qt
Pulse-plugin build-order requirement.
