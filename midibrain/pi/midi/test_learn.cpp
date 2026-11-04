// Host self-test for the mnkybox MIDI-learn table and dispatch. No framework:
// a failing check prints FAIL and the process exits non-zero, so
// `test_learn && echo PASS` is the acceptance gate. Headless, no MIDI device.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "learn.hpp"

namespace {

int g_failures = 0;

void check(bool ok, const char* what, const char* file, int line) {
  if (ok) return;
  std::fprintf(stderr, "FAIL %s (%s:%d)\n", what, file, line);
  ++g_failures;
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

void pass(const char* name) { std::printf("PASS %s\n", name); }

using mnky::midi::LearnBinding;
using mnky::midi::LearnEngine;
using mnky::midi::MidiEvent;
using mnky::midi::Port;

MidiEvent controlChange(int ch, int cc, int value, Port source = Port::TrsIn1,
                        std::uint32_t stampUs = 1000) {
  MidiEvent ev;
  ev.source = source;
  ev.size = 3;
  ev.data[0] = static_cast<std::uint8_t>(0xB0 | ((ch - 1) & 0x0F));
  ev.data[1] = static_cast<std::uint8_t>(cc);
  ev.data[2] = static_cast<std::uint8_t>(value);
  ev.stampUs = stampUs;
  return ev;
}

MidiEvent message(Port source, std::uint8_t status, std::uint8_t d1, std::uint8_t d2) {
  MidiEvent ev;
  ev.source = source;
  ev.size = 3;
  ev.data[0] = status;
  ev.data[1] = d1;
  ev.data[2] = d2;
  ev.stampUs = 1000;
  return ev;
}

bool hasTarget(const std::vector<LearnBinding>& bindings, int cc, int ch,
               const std::string& target) {
  for (const LearnBinding& binding : bindings) {
    if (binding.cc == cc && binding.ch == ch) return binding.target == target;
  }
  return false;
}

// ---------------------------------------------------------------------------
// table: bind, list, bounds, duplicate rejection, remove
// ---------------------------------------------------------------------------
void bindTable() {
  LearnEngine e;
  CHECK(e.listBindings().empty());

  CHECK(e.addBinding(74, 1, "mixer.ch3.gain"));
  CHECK(e.addBinding(7, 16, "fx.0.mix"));
  CHECK(e.addBinding(74, 2, "mixer.ch4.pan", 3));

  const std::vector<LearnBinding> bindings = e.listBindings();
  CHECK(bindings.size() == 3);
  CHECK((bindings[0] == LearnBinding{7, 16, "fx.0.mix", 0}));
  CHECK((bindings[1] == LearnBinding{74, 1, "mixer.ch3.gain", 0}));
  CHECK((bindings[2] == LearnBinding{74, 2, "mixer.ch4.pan", 3}));

  // cc 0..127 and channels 1..16 only; an empty target is not resolvable.
  CHECK(!e.addBinding(-1, 1, "mixer.ch1.gain"));
  CHECK(!e.addBinding(128, 1, "mixer.ch1.gain"));
  CHECK(!e.addBinding(74, 0, "mixer.ch1.gain"));
  CHECK(!e.addBinding(74, 17, "mixer.ch1.gain"));
  CHECK(!e.addBinding(74, 1, ""));
  CHECK(e.listBindings().size() == 3);

  // Duplicate (cc, ch) is rejected even with another target or track and does
  // not overwrite the original binding.
  CHECK(!e.addBinding(74, 1, "mixer.ch5.gain"));
  CHECK(!e.addBinding(74, 1, "mixer.ch3.gain", 9));
  CHECK(e.listBindings().size() == 3);
  CHECK(hasTarget(e.listBindings(), 74, 1, "mixer.ch3.gain"));

  // Same cc on another channel / another cc on the same channel are distinct.
  CHECK(e.addBinding(74, 3, "mixer.ch5.gain"));
  CHECK(e.addBinding(75, 1, "mixer.ch6.gain"));
  CHECK(e.listBindings().size() == 5);

  CHECK(e.removeBinding(74, 2));
  CHECK(!e.removeBinding(74, 2));
  CHECK(!e.removeBinding(-1, 1));
  CHECK(!e.removeBinding(74, 17));
  CHECK(e.listBindings().size() == 4);
  CHECK(!hasTarget(e.listBindings(), 74, 2, "mixer.ch4.pan"));
  pass("learn bind table");
}

// ---------------------------------------------------------------------------
// dispatch: value scaling, unmapped CCs, non-CC messages
// ---------------------------------------------------------------------------
void dispatchScaling() {
  LearnEngine e;
  std::vector<double> values;
  CHECK(e.addBinding(74, 1, "mixer.ch3.gain"));
  e.registerTarget("mixer.ch3.gain", [&values](double value) { values.push_back(value); });

  CHECK(e.dispatch(controlChange(1, 74, 0)));
  CHECK(e.dispatch(controlChange(1, 74, 64)));
  CHECK(e.dispatch(controlChange(1, 74, 127)));
  CHECK(values.size() == 3);
  if (values.size() == 3) {
    CHECK(values[0] == 0.0);
    CHECK(std::fabs(values[1] - 64.0 / 127.0) < 1e-12);
    CHECK(values[2] == 1.0);
  }
  pass("learn dispatch value scaling");
}

void dispatchIgnoresOthers() {
  LearnEngine e;
  int calls = 0;
  CHECK(e.addBinding(74, 1, "mixer.ch3.gain"));
  e.registerTarget("mixer.ch3.gain", [&calls](double) { ++calls; });

  // Unmapped CC on the bindable channel.
  CHECK(!e.dispatch(controlChange(1, 75, 64)));
  // Mapped CC on a channel with no binding.
  CHECK(!e.dispatch(controlChange(2, 74, 64)));
  CHECK(calls == 0);

  // Non-control-change: note on/off, pitch bend, aftertouch, program change.
  CHECK(!e.dispatch(message(Port::TrsIn1, 0x90, 60, 100)));
  CHECK(!e.dispatch(message(Port::TrsIn1, 0x80, 60, 64)));
  CHECK(!e.dispatch(message(Port::TrsIn1, 0xE0, 0, 64)));
  CHECK(!e.dispatch(message(Port::TrsIn1, 0xD0, 100, 0)));
  CHECK(!e.dispatch(message(Port::TrsIn1, 0xC0, 5, 0)));

  // Realtime clock, and a truncated control change, are ignored too.
  MidiEvent clock;
  clock.source = Port::TrsIn1;
  clock.size = 1;
  clock.data[0] = 0xF8;
  CHECK(!e.dispatch(clock));
  MidiEvent truncated = controlChange(1, 74, 64);
  truncated.size = 2;
  CHECK(!e.dispatch(truncated));
  CHECK(calls == 0);

  // A bound CC with no registered target is still matched but calls nothing.
  CHECK(e.addBinding(7, 1, "mixer.ch2.gain"));
  CHECK(e.dispatch(controlChange(1, 7, 100)));
  CHECK(calls == 0);
  pass("learn ignores unmapped/non-CC");
}

// ---------------------------------------------------------------------------
// channel isolation (and source-port agnosticism)
// ---------------------------------------------------------------------------
void channelIsolation() {
  LearnEngine e;
  int ch1 = 0;
  int ch2 = 0;
  int ch16 = 0;
  e.registerTarget("one", [&ch1](double) { ++ch1; });
  e.registerTarget("two", [&ch2](double) { ++ch2; });
  e.registerTarget("sixteen", [&ch16](double) { ++ch16; });
  CHECK(e.addBinding(74, 1, "one"));
  CHECK(e.addBinding(74, 2, "two"));
  CHECK(e.addBinding(74, 16, "sixteen"));

  CHECK(e.dispatch(controlChange(1, 74, 10)));
  CHECK(ch1 == 1 && ch2 == 0 && ch16 == 0);
  CHECK(e.dispatch(controlChange(2, 74, 10)));
  CHECK(ch1 == 1 && ch2 == 1 && ch16 == 0);
  CHECK(e.dispatch(controlChange(16, 74, 10)));
  CHECK(ch1 == 1 && ch2 == 1 && ch16 == 1);

  CHECK(!e.dispatch(controlChange(3, 74, 10)));
  CHECK(ch1 + ch2 + ch16 == 3);

  // The same control from any input port maps to the same binding.
  CHECK(e.dispatch(controlChange(1, 74, 10, Port::UsbIn)));
  CHECK(ch1 == 2);
  pass("learn channel isolation");
}

// ---------------------------------------------------------------------------
// target registration and clear
// ---------------------------------------------------------------------------
void targetsAndClear() {
  LearnEngine e;
  int first = 0;
  int second = 0;
  e.registerTarget("mixer.ch1.gain", [&first](double) { ++first; });
  CHECK(e.addBinding(7, 1, "mixer.ch1.gain"));
  CHECK(e.dispatch(controlChange(1, 7, 100)));
  CHECK(first == 1);

  // Re-registering a path replaces its setter.
  e.registerTarget("mixer.ch1.gain", [&second](double) { ++second; });
  CHECK(e.dispatch(controlChange(1, 7, 100)));
  CHECK(first == 1 && second == 1);

  // An empty setter unregisters the path; the binding stays and still matches.
  e.registerTarget("mixer.ch1.gain", {});
  CHECK(e.dispatch(controlChange(1, 7, 100)));
  CHECK(first == 1 && second == 1);

  // clear() drops bindings only; registered targets survive for re-binding.
  e.registerTarget("mixer.ch1.gain", [&second](double) { ++second; });
  e.clear();
  CHECK(e.listBindings().empty());
  CHECK(!e.dispatch(controlChange(1, 7, 100)));
  CHECK(second == 1);
  CHECK(e.addBinding(7, 1, "mixer.ch1.gain"));
  CHECK(e.dispatch(controlChange(1, 7, 100)));
  CHECK(second == 2);
  pass("learn target registration/clear");
}

}  // namespace

int main() {
  bindTable();
  dispatchScaling();
  dispatchIgnoresOthers();
  channelIsolation();
  targetsAndClear();

  if (g_failures != 0) {
    std::fprintf(stderr, "FAILURES %d\n", g_failures);
    return 1;
  }
  std::printf("PASS test_learn\n");
  return 0;
}
