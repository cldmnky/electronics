#include "session_view_model.hpp"

#include <algorithm>
#include <cmath>

#include "../../firmware/pico/midi.hpp"
#include "logic.hpp"

namespace mnky::app {
namespace {
std::uint8_t clockFlags(bool clock, bool transport) {
  std::uint8_t flags = 0;
  if (clock) flags |= mb::CLOCK_ROUTE_CLOCK;
  if (transport) flags |= mb::CLOCK_ROUTE_TRANSPORT;
  return flags;
}

int clampMeasure(int measure) { return std::max(1, measure); }
int clampSource(int source) { return source == 1 ? 1 : 0; }
int clampChoice(int value, int maximum) { return std::clamp(value, 0, maximum); }
double clampDb(double db, double low, double high) { return std::clamp(db, low, high); }
}  // namespace

SessionViewModel::SessionViewModel(AppState& state, MixerModel& mixer, LinkClient& link,
                                   SessionSettings& settings, QObject* parent)
    : QObject(parent), state_(state), mixer_(mixer), link_(link), settings_(settings) {}

void SessionViewModel::setPunchPlayFromEnabled(bool enabled) {
  if (settings_.punch.playFrom.enabled == enabled) return;
  settings_.punch.playFrom.enabled = enabled;
  emit punchChanged();
}

void SessionViewModel::setPunchPlayFromMeasure(int measure) {
  const int value = clampMeasure(measure);
  if (settings_.punch.playFrom.measure == value) return;
  settings_.punch.playFrom.measure = value;
  emit punchChanged();
}

void SessionViewModel::setPunchInEnabled(bool enabled) {
  if (settings_.punch.in.enabled == enabled) return;
  settings_.punch.in.enabled = enabled;
  emit punchChanged();
}

void SessionViewModel::setPunchInMeasure(int measure) {
  const int value = clampMeasure(measure);
  if (settings_.punch.in.measure == value) return;
  settings_.punch.in.measure = value;
  emit punchChanged();
}

void SessionViewModel::setPunchOutEnabled(bool enabled) {
  if (settings_.punch.out.enabled == enabled) return;
  settings_.punch.out.enabled = enabled;
  emit punchChanged();
}

void SessionViewModel::setPunchOutMeasure(int measure) {
  const int value = clampMeasure(measure);
  if (settings_.punch.out.measure == value) return;
  settings_.punch.out.measure = value;
  emit punchChanged();
}

void SessionViewModel::setClickMode(int mode) {
  const int value = clampChoice(mode, 2);
  state_.clickMode.store(value);
  state_.clickEnabled.store(value != 0);
  if (state_.clickGainDb.load() == 0.0f && value != 0) {
    // Keep the legacy 0.5 amplitude as the first enabled level unless the user
    // has already set a session gain.
    state_.clickLevel.store(0.5f);
  }
  emit clickChanged();
}

void SessionViewModel::setClickOutput(int output) {
  const int value = clampChoice(output, 2);
  if (state_.clickOutput.load() == value) return;
  state_.clickOutput.store(value);
  emit clickChanged();
}

void SessionViewModel::setClickGainDb(double db) {
  const double value = clampDb(db, -96.0, 20.0);
  if (std::abs(state_.clickGainDb.load() - value) < 0.001) return;
  state_.clickGainDb.store(static_cast<float>(value));
  // Legacy amplitude view: clamp positive dB to 1.0; AudioGlue applies the
  // dB gain itself for boosts.
  state_.clickLevel.store(std::clamp(dbToGain(static_cast<float>(value)), 0.0f, 1.0f));
  emit clickChanged();
}

void SessionViewModel::setPhonesSrc(int source) {
  const int value = clampSource(source);
  if (settings_.cue.phonesSrc == value) return;
  settings_.cue.phonesSrc = value;
  emit monitorChanged();
}

void SessionViewModel::setCueStyle(int style) {
  const int value = clampSource(style);
  if (settings_.cue.style == value) return;
  settings_.cue.style = value;
  emit monitorChanged();
}

void SessionViewModel::setOut34Mode(int mode) {
  const int value = clampSource(mode);
  if (mixer_.out34Mode() == value) return;
  mixer_.setOut34Mode(value);
  emit monitorChanged();
}

void SessionViewModel::setSendCue(int source) {
  settings_.sends.cue = clampSource(source);
  emit sendsChanged();
}
void SessionViewModel::setSendFx1(int source) {
  settings_.sends.fx1 = clampSource(source);
  emit sendsChanged();
}
void SessionViewModel::setSendFx2(int source) {
  settings_.sends.fx2 = clampSource(source);
  emit sendsChanged();
}
void SessionViewModel::setSendOut34(int source) {
  settings_.sends.out34 = clampSource(source);
  emit sendsChanged();
}
void SessionViewModel::setSendRec(int source) {
  settings_.sends.rec = clampSource(source);
  emit sendsChanged();
}

void SessionViewModel::setCompOn(bool on) {
  if (settings_.mainComp.on == on) return;
  settings_.mainComp.on = on;
  emit compChanged();
}
void SessionViewModel::setCompThreshDb(double db) {
  settings_.mainComp.threshDb = static_cast<float>(clampDb(db, -48.0, 0.0));
  emit compChanged();
}
void SessionViewModel::setCompRatio(double ratio) {
  settings_.mainComp.ratio = static_cast<float>(clampDb(ratio, 1.0, 100.0));
  emit compChanged();
}
void SessionViewModel::setCompAttackMs(double ms) {
  settings_.mainComp.attackMs = static_cast<float>(clampDb(ms, 0.0, 1000.0));
  emit compChanged();
}
void SessionViewModel::setCompReleaseMs(double ms) {
  settings_.mainComp.releaseMs = static_cast<float>(clampDb(ms, 0.0, 1000.0));
  emit compChanged();
}
void SessionViewModel::setCompMakeupDb(double db) {
  settings_.mainComp.makeupDb = static_cast<float>(clampDb(db, -36.0, 36.0));
  emit compChanged();
}
void SessionViewModel::setCompMode(int mode) {
  settings_.mainComp.mode = clampSource(mode);
  emit compChanged();
}

void SessionViewModel::setTrsRoute(int output, bool clock, bool transport) {
  if (output < 0 || output > 1) return;
  auto& route = settings_.clock.trs[static_cast<std::size_t>(output)];
  if (route.clock == clock && route.transport == transport) return;
  route.clock = clock;
  route.transport = transport;
  link_.requestClockRoute(static_cast<std::uint8_t>(output), clockFlags(clock, transport));
  emit clockChanged();
}

void SessionViewModel::setTrs1Clock(bool on) {
  setTrsRoute(0, on, settings_.clock.trs[0].transport);
}
void SessionViewModel::setTrs1Transport(bool on) {
  setTrsRoute(0, settings_.clock.trs[0].clock, on);
}
void SessionViewModel::setTrs2Clock(bool on) {
  setTrsRoute(1, on, settings_.clock.trs[1].transport);
}
void SessionViewModel::setTrs2Transport(bool on) {
  setTrsRoute(1, settings_.clock.trs[1].clock, on);
}

void SessionViewModel::setUsbClock(bool on) {
  if (settings_.clock.usb.clock == on) return;
  settings_.clock.usb.clock = on;
  link_.requestUsbClockGates(settings_.clock.usb.clock, settings_.clock.usb.transport);
  emit clockChanged();
}

void SessionViewModel::setUsbTransport(bool on) {
  if (settings_.clock.usb.transport == on) return;
  settings_.clock.usb.transport = on;
  link_.requestUsbClockGates(settings_.clock.usb.clock, settings_.clock.usb.transport);
  emit clockChanged();
}

QString SessionViewModel::clockText() const {
  const auto gate = [](bool on) { return on ? QStringLiteral("on") : QStringLiteral("off"); };
  const auto& trs1 = settings_.clock.trs[0];
  const auto& trs2 = settings_.clock.trs[1];
  return QStringLiteral("GATES clock/transport  TRS1 %1/%2  TRS2 %3/%4  USB %5/%6")
      .arg(gate(trs1.clock), gate(trs1.transport), gate(trs2.clock), gate(trs2.transport))
      .arg(gate(settings_.clock.usb.clock), gate(settings_.clock.usb.transport))
      .arg(trs1.tempoDiv)
      .arg(trs1.syncDelayMs);
}

void SessionViewModel::pushClockToLink() {
  for (int output = 0; output < 2; ++output) {
    const auto& route = settings_.clock.trs[static_cast<std::size_t>(output)];
    link_.requestClockRoute(static_cast<std::uint8_t>(output),
                            clockFlags(route.clock, route.transport));
  }
  link_.requestUsbClockGates(settings_.clock.usb.clock, settings_.clock.usb.transport);
  emit clockChanged();
}

void SessionViewModel::refresh() {
  emit punchChanged();
  emit clickChanged();
  emit monitorChanged();
  emit sendsChanged();
  emit compChanged();
  emit clockChanged();
}

}  // namespace mnky::app
