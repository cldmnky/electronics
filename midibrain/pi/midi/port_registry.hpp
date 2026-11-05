#pragma once
// Runtime port registry for dynamic USB MIDI endpoints (ids 6..31).
//
// Ports 0..5 are the fixed TRS/merged ports owned by the firmware ABI (see
// router.hpp); they are never registered here. A discovered USB MIDI device
// gets one dynamic id with a stable identity string ("usb:<name>", the session
// contract). The entry is direction-tagged: a device that offers both an input
// and an output endpoint keeps one id and sets both flags, because the device
// is the routable unit and its name is the identity that survives replug order.
//
// A hot-unplug does not free the id: the slot becomes dormant (connected ==
// false) so every route/channel/transpose already stored against that id stays
// intact and wakes when the same identity returns. A slot is only freed by the
// explicit remove(), which the app may use only after dropping those routes.
//
// On session load, bindSession() maps the stored ids to live/reserved slots by
// identity, never by number: a device that comes back on a different
// enumeration order still gets its routes. Missing devices are reserved as
// dormant slots, so their routes stay bound and are not silently reassigned to
// another device. sessionDevices() is the save-side map (all reserved
// identities, connected or dormant, so an unplugged device's binding survives
// a save too).
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "router.hpp"

namespace mnky::midi {

inline constexpr const char* kUsbIdentityPrefix = "usb:";

// "usb:" + the per-device name from the app's RtMidi inventory grouping.
std::string usbIdentity(const std::string& deviceName);
// The device name without the "usb:" prefix; returns the input unchanged when
// it does not carry the prefix.
std::string usbDeviceName(const std::string& identity);

struct PortRegistration {
  int id = 0;  // 0 = free slot; otherwise kFirstDynamicPort..kMaxPorts-1
  std::string identity;
  bool input = false;    // device offers a MIDI input endpoint
  bool output = false;   // device offers a MIDI output endpoint
  bool connected = false;
  friend bool operator==(const PortRegistration&, const PortRegistration&) = default;
};

class PortRegistry {
 public:
  // Reconciles the registry with the currently enumerated device identities
  // (per direction, already grouped by the caller). New identities get the
  // lowest free dynamic id, existing identities keep their id, and an identity
  // that is gone becomes dormant. Returns every non-free slot in id order.
  std::vector<PortRegistration> syncDevices(
      const std::vector<std::string>& inputIdentities,
      const std::vector<std::string>& outputIdentities);

  // Registers/updates one identity and marks it connected; direction flags are
  // unioned, so calling once per direction is safe. Returns the id, or 0 when
  // all kMaxDynamicPorts slots are reserved.
  int connect(const std::string& identity, bool input, bool output);

  // Hot-unplug: keeps the id and identity reserved, only clears `connected`.
  // False when the identity was never registered.
  bool disconnect(const std::string& identity);

  // Explicit forget: frees the slot for reuse. The caller must already have
  // dropped every route/channel/transpose that referenced this id, otherwise a
  // future device could be silently reassigned to it.
  bool remove(const std::string& identity);

  // Session load: finds or creates a dormant slot for identity (id stays 0 only
  // when the registry is full).
  int reserve(const std::string& identity);

  // Id currently holding identity (connected or dormant), or 0.
  int find(const std::string& identity) const;
  const PortRegistration* entry(int id) const;
  // Every non-free slot in id order.
  std::vector<PortRegistration> entries() const;
  std::size_t connectedCount() const;
  std::size_t size() const;
  void clear();

  // Stored session id -> live/reserved id, by identity. An identity that is not
  // connected yet is reserved, so its routes stay dormant-but-intact.
  struct SessionBinding {
    std::map<int, int> ids;      // stored id -> live id
    std::vector<int> unbound;    // stored dynamic ids with no free slot
  };
  SessionBinding bindSession(const std::map<int, std::string>& devices);

  // Save-side map: every reserved identity, connected or dormant.
  std::map<int, std::string> sessionDevices() const;

 private:
  PortRegistration* findMutable(const std::string& identity);
  int allocate(const std::string& identity);

  std::array<PortRegistration, kMaxDynamicPorts> slots_{};
};

// Rewrites route endpoints in place using a SessionBinding::ids map. Works for
// any route type with public `src`/`dst` ints (session::RouteState).
template <class Route>
void remapRouteIds(const std::map<int, int>& ids, std::vector<Route>& routes) {
  for (auto& route : routes) {
    const auto src = ids.find(route.src);
    if (src != ids.end()) route.src = src->second;
    const auto dst = ids.find(route.dst);
    if (dst != ids.end()) route.dst = dst->second;
  }
}

// Rewrites the per-port channel mask/transpose arrays by identity. Fixed ports
// 0..5 are copied untouched; dynamic ids absent from `ids` keep their stored
// slot (dormant device); mapped ids move to their live slot and the old slot
// returns to the documented defaults.
void remapSessionPorts(const std::map<int, int>& ids,
                       std::array<std::uint16_t, kMaxPorts>& channels,
                       std::array<int, kMaxPorts>& transpose);

}  // namespace mnky::midi
