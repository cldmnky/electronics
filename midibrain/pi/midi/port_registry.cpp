#include "port_registry.hpp"

#include <utility>

namespace mnky::midi {
namespace {

int slotIndex(int id) { return id - kFirstDynamicPort; }

bool validDynamicId(int id) {
  return id >= kFirstDynamicPort && id < kMaxPorts;
}

}  // namespace

std::string usbIdentity(const std::string& deviceName) {
  if (deviceName.rfind(kUsbIdentityPrefix, 0) == 0) return deviceName;
  return std::string(kUsbIdentityPrefix) + deviceName;
}

std::string usbDeviceName(const std::string& identity) {
  const std::size_t prefix = std::char_traits<char>::length(kUsbIdentityPrefix);
  if (identity.rfind(kUsbIdentityPrefix, 0) != 0) return identity;
  return identity.substr(prefix);
}

PortRegistration* PortRegistry::findMutable(const std::string& identity) {
  if (identity.empty()) return nullptr;
  for (auto& slot : slots_) {
    if (slot.id != 0 && slot.identity == identity) return &slot;
  }
  return nullptr;
}

int PortRegistry::allocate(const std::string& identity) {
  if (identity.empty()) return 0;
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    if (slots_[index].id != 0) continue;
    slots_[index] = PortRegistration{};
    slots_[index].id = kFirstDynamicPort + static_cast<int>(index);
    slots_[index].identity = identity;
    return slots_[index].id;
  }
  return 0;
}

int PortRegistry::connect(const std::string& identity, bool input, bool output) {
  if (identity.empty()) return 0;
  PortRegistration* slot = findMutable(identity);
  if (slot == nullptr) {
    const int id = allocate(identity);
    if (id == 0) return 0;
    slot = findMutable(identity);
    if (slot == nullptr) return 0;
  }
  slot->connected = true;
  slot->input = slot->input || input;
  slot->output = slot->output || output;
  return slot->id;
}

bool PortRegistry::disconnect(const std::string& identity) {
  PortRegistration* slot = findMutable(identity);
  if (slot == nullptr) return false;
  slot->connected = false;
  return true;
}

bool PortRegistry::remove(const std::string& identity) {
  PortRegistration* slot = findMutable(identity);
  if (slot == nullptr) return false;
  *slot = PortRegistration{};
  return true;
}

int PortRegistry::reserve(const std::string& identity) {
  if (identity.empty()) return 0;
  PortRegistration* slot = findMutable(identity);
  if (slot != nullptr) return slot->id;
  return allocate(identity);
}

int PortRegistry::find(const std::string& identity) const {
  if (identity.empty()) return 0;
  for (const auto& slot : slots_) {
    if (slot.id != 0 && slot.identity == identity) return slot.id;
  }
  return 0;
}

const PortRegistration* PortRegistry::entry(int id) const {
  if (!validDynamicId(id)) return nullptr;
  const auto& slot = slots_[static_cast<std::size_t>(slotIndex(id))];
  return slot.id == 0 ? nullptr : &slot;
}

std::vector<PortRegistration> PortRegistry::entries() const {
  std::vector<PortRegistration> out;
  out.reserve(slots_.size());
  for (const auto& slot : slots_) {
    if (slot.id != 0) out.push_back(slot);
  }
  return out;
}

std::size_t PortRegistry::connectedCount() const {
  std::size_t count = 0;
  for (const auto& slot : slots_) {
    if (slot.id != 0 && slot.connected) ++count;
  }
  return count;
}

std::size_t PortRegistry::size() const {
  std::size_t count = 0;
  for (const auto& slot : slots_) {
    if (slot.id != 0) ++count;
  }
  return count;
}

void PortRegistry::clear() { slots_.fill(PortRegistration{}); }

std::vector<PortRegistration> PortRegistry::syncDevices(
    const std::vector<std::string>& inputIdentities,
    const std::vector<std::string>& outputIdentities) {
  // Unique identity order: inputs first, then output-only devices. A device
  // offering both directions is registered once with both flags.
  std::map<std::string, std::pair<bool, bool>> inventory;
  for (const auto& identity : inputIdentities) {
    if (identity.empty()) continue;
    auto& flags = inventory[identity];
    flags.first = true;
  }
  for (const auto& identity : outputIdentities) {
    if (identity.empty()) continue;
    auto& flags = inventory[identity];
    flags.second = true;
  }

  // std::map keeps allocation deterministic (alphabetical), so two runs that
  // see the same devices in a different enumeration order agree on ids even
  // before the session rebinds by name.
  for (const auto& [identity, flags] : inventory) {
    const int id = reserve(identity);
    if (id == 0) continue;  // registry full; keep the previous bindings
    PortRegistration* slot = findMutable(identity);
    if (slot == nullptr) continue;
    slot->connected = true;
    slot->input = flags.first;
    slot->output = flags.second;
  }

  // Anything that was connected and is absent from this inventory goes dormant
  // without losing its id or its last known direction flags.
  for (auto& slot : slots_) {
    if (slot.id == 0 || !slot.connected) continue;
    if (inventory.find(slot.identity) == inventory.end()) slot.connected = false;
  }
  return entries();
}

PortRegistry::SessionBinding PortRegistry::bindSession(
    const std::map<int, std::string>& devices) {
  SessionBinding binding;
  for (const auto& [storedId, identity] : devices) {
    if (!validDynamicId(storedId) || identity.empty()) continue;
    const int liveId = reserve(identity);
    if (liveId == 0) {
      binding.unbound.push_back(storedId);
      continue;
    }
    binding.ids[storedId] = liveId;
  }
  return binding;
}

std::map<int, std::string> PortRegistry::sessionDevices() const {
  std::map<int, std::string> devices;
  for (const auto& slot : slots_) {
    if (slot.id != 0) devices[slot.id] = slot.identity;
  }
  return devices;
}

void remapSessionPorts(const std::map<int, int>& ids,
                       std::array<std::uint16_t, kMaxPorts>& channels,
                       std::array<int, kMaxPorts>& transpose) {
  // Snapshot the stored state, reset the dynamic range to the documented
  // defaults, then copy each stored id onto its live slot (or keep it on its
  // own dormant slot when the device is not bound).
  const auto oldChannels = channels;
  const auto oldTranspose = transpose;
  for (int id = kFirstDynamicPort; id < kMaxPorts; ++id) {
    channels[static_cast<std::size_t>(id)] = Router::kAllChannels;
    transpose[static_cast<std::size_t>(id)] = 0;
  }
  for (int id = kFirstDynamicPort; id < kMaxPorts; ++id) {
    const auto mapped = ids.find(id);
    const int target = mapped == ids.end() ? id : mapped->second;
    if (target < kFirstDynamicPort || target >= kMaxPorts) continue;
    channels[static_cast<std::size_t>(target)] =
        oldChannels[static_cast<std::size_t>(id)];
    transpose[static_cast<std::size_t>(target)] =
        oldTranspose[static_cast<std::size_t>(id)];
  }
}

}  // namespace mnky::midi
