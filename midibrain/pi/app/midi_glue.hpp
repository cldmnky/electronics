#pragma once
// USB MIDI + routing-matrix glue. Inputs arrive on RtMidi's callback thread or
// on the Pico link thread, so the matrix, the MIDI-learn table and the
// pending-delivery queue are guarded. Deliveries are flushed by the link
// thread, which owns the SPI transport and therefore the TRS outputs.
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "../midi/learn.hpp"
#include "../midi/port_registry.hpp"
#include "../midi/router.hpp"
#include "../midi/usb.hpp"
#include "../session/session.hpp"

namespace mnky::app {

// midi/router.hpp cannot include the session header; keep the two array bounds
// tied together here so a session format change cannot silently truncate.
static_assert(midi::kMaxPorts == session::kSessionMaxPorts,
              "dynamic port bound must match session::kSessionMaxPorts");

// Applies one stored pipe list to a Router (used by project load on the router
// snapshot before it is swapped in, because session::apply() ignores
// RouteState::pipes). False when the pair or a pipe/limit is invalid.
bool applyRoutePipesToRouter(midi::Router& router, midi::Port source,
                             midi::Port destination,
                             const std::vector<session::MidiPipe>& pipes);

class MidiGlue {
 public:
  // Sends one routed message to a TRS destination; false when the link rejected
  // it. Called from the link thread only.
  using TrsSink = std::function<bool(midi::Port, const midi::MidiEvent&)>;

  explicit MidiGlue(bool openVirtualPorts);
  ~MidiGlue();
  MidiGlue(const MidiGlue&) = delete;
  MidiGlue& operator=(const MidiGlue&) = delete;

  bool open();
  void close();
  std::string describe() const;

  // --- Dynamic USB MIDI device endpoints (ids 6..31) ----------------------
  // One routable endpoint per discovered USB MIDI device, keyed by the stable
  // identity string "usb:<name>". `name` is the per-device name produced by the
  // same RtMidi grouping as the System audio inventory
  // (audio_backend.hpp:midiDeviceNamesFromPortNames). An entry with
  // connected == false is a reserved dormant slot: it keeps its id so routes
  // survive an unplug, and it wakes when the identity returns.
  struct UsbPortInfo {
    int id = 0;             // 6..31
    std::string name;       // display name (identity without the "usb:" prefix)
    std::string identity;   // stable session identity, "usb:<name>"
    bool input = false;     // device offers an input endpoint
    bool output = false;    // device offers an output endpoint
    bool connected = false;
    friend bool operator==(const UsbPortInfo&, const UsbPortInfo&) = default;
  };
  // Every registered dynamic id in ascending order, dormant included.
  std::vector<UsbPortInfo> usbPorts() const;
  // Number of currently connected USB MIDI devices.
  std::size_t usbPortCount() const;
  // Re-enumerates RtMidi, reconciles the registry by identity and reopens the
  // per-device input/output ports. Routes are never touched: a missing device
  // goes dormant and keeps its id. Returns true when ports are open afterwards.
  bool refreshDevices();

  // --- Session binding for dynamic ids ------------------------------------
  // Rewrites dynamic ids in `state` from stored ids to live/reserved ids by
  // `state.devices` identity, then rewrites `state.devices` to the live ids.
  // Fixed ports 0..5 are untouched. A missing device is reserved as a dormant
  // slot, so its routes stay intact and are never reassigned to another device.
  // Returns the number of stored ids bound (including dormant reservations).
  std::size_t bindSessionRouter(session::RouterState& state);
  // Fills state.devices from every reserved identity (connected or dormant).
  void captureSessionDevices(session::RouterState& state) const;
  // Save-side router snapshot including dynamic ids: routes (src/dst ints),
  // channels/transpose for all 32 ports and the device identity map. Unlike
  // session::capture() (fixed ports only), this is the dynamic-aware capture a
  // project save should merge into its snapshot.
  session::RouterState captureSessionRouter() const;
  // Authoritative router load for a session snapshot: binds dynamic ids by
  // name, clears every fixed and dynamic route and pipe chain in the live
  // Router, applies routes/channels/transpose for all 32 ids, applies each
  // RouteState::pipes chain to the Router and replaces the stored chain lists
  // (including the TRS mirrors pushed to the Pico).
  void applySessionRouter(const session::RouterState& state);

  void handleInput(const midi::MidiEvent& event);  // any thread
  void pump(const TrsSink& trsSink);               // link thread
  void sendUsbClock(std::uint8_t byte);            // link thread (clock mirror)

  void setRoute(midi::Port source, midi::Port destination, bool enabled);
  bool routeEnabled(midi::Port source, midi::Port destination) const;
  void setChannelMask(midi::Port source, std::uint16_t mask);
  std::uint16_t channelMask(midi::Port source) const;
  void setTranspose(midi::Port source, int semitones);
  int transpose(midi::Port source) const;

  // --- MIDI FX pipe chains (Routing SHIFT) ---------------------------------
  // The session stores one ordered chain per input->output pair
  // (session::RouteState::pipes). MidiGlue owns that list, keeps the live
  // Router's chain in sync (Router::setPipes) and is the only writer, so the
  // UI, project capture and the LearnEngine param setters cannot disagree.
  // A chain that Router refuses (invalid type/limits) leaves both the store and
  // the Router unchanged. A route touching USB is evaluated by the Pi engine;
  // a TRS->TRS chain is mirrored to the Pico through the pipeSync_ seam
  // (0x18/0x19) and its routed bytes go out with EV_MIDI_ROUTE_SEND (0x1A), so
  // each side evaluates a chain exactly once.
  std::vector<session::MidiPipe> routePipes(midi::Port source,
                                            midi::Port destination) const;
  bool routeHasPipes(midi::Port source, midi::Port destination) const;
  bool setRoutePipes(midi::Port source, midi::Port destination,
                     const std::vector<session::MidiPipe>& pipes);
  bool addRoutePipe(midi::Port source, midi::Port destination,
                    session::MidiPipeType type);
  bool removeRoutePipe(midi::Port source, midi::Port destination, int index);
  bool moveRoutePipe(midi::Port source, midi::Port destination, int index,
                     int delta);
  bool setRoutePipeBypass(midi::Port source, midi::Port destination, int index,
                          bool bypass);
  bool setRoutePipeParam(midi::Port source, midi::Port destination, int index,
                         int param, int value);
  // Per-pipe channel scope (session::MidiPipe::chMask): the pipe processes only
  // matching channels, everything else passes that pipe unchanged. 0xFFFF is
  // all channels. Works for fixed and per-device (dynamic) routes.
  bool setRoutePipeChannelMask(midi::Port source, midi::Port destination, int index,
                               std::uint16_t mask);
  std::uint16_t routePipeChannelMask(midi::Port source, midi::Port destination,
                                     int index) const;
  // Replaces the stored chains after a project load; the caller applies the same
  // lists to the Router snapshot before swapping it in (session::apply() does
  // not apply RouteState::pipes).
  void setRoutePipeLists(const std::vector<session::RouteState>& routes);
  // Dotted LearnEngine paths for every positional parameter of every stored
  // chain, in route/pipe/param order.
  std::vector<std::string> routePipeLearnTargets() const;

  // Pico mirror seam: AppController installs a callback that queues the chain
  // for LinkClient (0x18 begin + 0x19 pipe chunks) whenever a TRS->TRS chain or
  // that route's enable state changes. Called with lock_ released, so the
  // callback may enqueue link commands or read MidiGlue. Routes touching USB
  // are never passed here; they stay Pi-owned.
  using PipeSyncFn = std::function<void(midi::Port, midi::Port,
                                        const std::vector<session::MidiPipe>&,
                                        bool enabled)>;
  void setPipeSync(PipeSyncFn fn);
  // Re-push all four TRS->TRS chains (startup / project load / RESET).
  void syncTrsPipes();

  // Session capture/apply seam: copy the guarded matrix, mutate the copy, and
  // replace it in one lock so the MIDI input thread never sees a half-applied
  // project.
  midi::Router routerSnapshot() const {
    const std::lock_guard<std::mutex> lock(lock_);
    return router_;
  }
  void replaceRouter(const midi::Router& router) {
    const std::lock_guard<std::mutex> lock(lock_);
    router_ = router;
  }

  std::size_t inputCount() const;
  std::size_t outputCount() const;
  std::uint64_t routedCount() const;

  // --- MIDI learn ----------------------------------------------------------
  // Setters run on the MIDI input thread, so registered targets must be safe
  // there (the app registers MixerModel atomics and the click level only).
  void registerLearnTarget(const std::string& path, midi::LearnEngine::Setter setter);
  std::vector<midi::LearnBinding> learnBindings() const;
  bool addLearnBinding(int cc, int ch, std::string target, int track = 0);
  bool removeLearnBinding(int cc, int ch);
  void clearLearnBindings();
  void setLearnBindings(const std::vector<midi::LearnBinding>& bindings);

  // Capture mode: while armed, every control-change updates a lock-free
  // snapshot the GUI thread polls; the sequence number changes per event.
  struct LearnCapture {
    bool armed = false;
    std::uint64_t seq = 0;
    int cc = 0;
    int ch = 1;
    int value = 0;
  };
  void armLearnCapture(bool armed);
  LearnCapture learnCapture() const;

  // CC123 on all 16 channels of every USB + TRS output. Queued as ordinary
  // deliveries so only the link thread touches the transport.
  void panic();

  // --- MIDI monitor (Monitor screen) ---------------------------------------
  // Every message this glue sees is appended to a bounded ring: inputs as they
  // arrive on the RtMidi/Pico threads, routed outputs when the link thread
  // flushes them, and the Pi-side USB clock mirror when it emits a byte. The
  // Pico generates clock/transport for the TRS outputs itself and does not
  // echo it back, so TRS OUT rows show routed messages only.
  static constexpr std::size_t kMonitorEvents = 128;
  struct MonitorEvent {
    midi::Port port = midi::Port::TrsIn1;
    bool outbound = false;
    std::uint8_t size = 0;
    std::uint8_t data[3] = {};
    std::uint64_t stampUs = 0;
  };
  struct MonitorCounts {
    std::uint64_t notes = 0;
    std::uint64_t cc = 0;
    std::uint64_t clock = 0;
    std::uint64_t other = 0;
    friend bool operator==(const MonitorCounts&, const MonitorCounts&) = default;
  };
  struct MonitorSnapshot {
    // Newest last. `events` is only the first `count` entries.
    std::array<MonitorEvent, kMonitorEvents> events{};
    std::size_t count = 0;
    std::array<MonitorCounts, midi::kPortCount> counts{};
    std::uint64_t total = 0;
  };
  MonitorSnapshot monitorSnapshot() const;
  // Drops the ring and zeroes the per-port counters (Monitor screen RESET).
  void clearMonitor();

 private:
  void recordMonitor(midi::Port port, bool outbound, const std::uint8_t* data,
                     std::uint8_t size);
  // Queues one TRS->TRS chain through pipeSync_ (no-op for a Pi-owned pair).
  void notifyPipeSync(midi::Port source, midi::Port destination);
  struct Delivery {
    midi::Port destination;
    midi::MidiEvent event;
  };

  mutable std::mutex lock_;
  midi::Router router_;
  // Stored chains: the fixed 3x3 matrix is indexed by input*3 + output; any
  // route touching a dynamic USB id lives in the sparse map keyed by
  // source*kMaxPorts + destination. Kept in sync with router_ by
  // setRoutePipes() and captured into session::RouteState::pipes by project_io.
  std::array<std::vector<session::MidiPipe>, 9> routePipes_{};
  std::map<int, std::vector<session::MidiPipe>> dynamicRoutePipes_{};
  midi::UsbMidi usb_;
  // Dynamic USB device endpoints (ids 6..31), guarded by lock_. Ports are
  // reopened on an inventory change but the registry never frees an id while
  // routes may still reference it.
  midi::PortRegistry registry_;
  std::vector<std::string> deviceInventory_;
  bool devicesOpen_ = false;
  std::deque<Delivery> outbox_;
  const bool virtualPorts_;
  PipeSyncFn pipeSync_;
  std::uint64_t routed_ = 0;
  // Last Router pipe-rejection count this glue logged, so the log gets one line
  // per new rejection instead of one per routed message.
  std::uint64_t loggedPipeRejections_ = 0;
  std::string description_;
  // Monitor ring + per-port counters, guarded by lock_ (same lock as the
  // matrix, so a snapshot can never straddle a half-updated event).
  std::array<MonitorEvent, kMonitorEvents> monitorEvents_{};
  std::size_t monitorNext_ = 0;
  std::size_t monitorCount_ = 0;
  std::array<MonitorCounts, midi::kPortCount> monitorCounts_{};
  std::uint64_t monitorTotal_ = 0;

  mutable std::mutex learnLock_;
  midi::LearnEngine learn_;

  std::atomic<bool> learnCaptureArmed_{false};
  std::atomic<std::uint64_t> learnCaptureSeq_{0};
  std::atomic<int> learnCaptureCc_{0};
  std::atomic<int> learnCaptureCh_{1};
  std::atomic<int> learnCaptureValue_{0};
};

}  // namespace mnky::app
