#pragma once
// USB MIDI I/O for the mnkybox Pi core: an RtMidi wrapper that merges every
// open input into Port::UsbIn and fans UsbOut out to every open output.
// RtMidi stays out of this header (pimpl) so consumers do not need the vendor
// include path; usb.cpp defines the backend macro (__MACOSX_CORE__ here,
// __LINUX_ALSA__ on the Pi) before including RtMidi.h.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "router.hpp"

namespace mnky::midi {

class UsbMidi {
 public:
  // Virtual ports let the emulator (or another app) reach us without hardware.
  static constexpr const char* kVirtualPortName = "mnkybox";
  // device index of events that arrive through our virtual input port.
  static constexpr std::uint8_t kVirtualDevice = 0xFF;

  UsbMidi();
  ~UsbMidi();
  UsbMidi(const UsbMidi&) = delete;
  UsbMidi& operator=(const UsbMidi&) = delete;

  std::vector<std::string> listInputs();
  std::vector<std::string> listOutputs();

  // Opens every enumerated port (device index = enumeration index) and, with
  // `withVirtual`, our virtual "mnkybox" input. Returns the number opened;
  // enumerated ports that fail to open are skipped and recorded in lastError().
  std::size_t openAll(bool withVirtual = false,
                      const std::string& virtualName = kVirtualPortName);
  std::size_t outputAll(bool withVirtual = false,
                        const std::string& virtualName = kVirtualPortName);
  void closeAll();

  // Input callback: invoked on the backend's input thread with a fully formed
  // event (source UsbIn, device index, Pi capture time).
  void setInputCallback(std::function<void(const MidiEvent&)> fn);

  // UsbOut fan-out; false when no output port is open or a send failed.
  bool send(const MidiEvent& ev);

  // Clamps one backend message into an event; size 0 means "not forwardable"
  // (empty, longer than 3 bytes, or not starting with a status byte).
  static MidiEvent makeEvent(const std::uint8_t* bytes, std::size_t size,
                             std::uint8_t device, std::uint64_t stampUs);

  std::size_t inputCount() const;
  std::size_t outputCount() const;
  const std::string& lastError() const;

 private:
  struct Impl;

  static void onInput(double timeStamp, std::vector<unsigned char>* message, void* userData);

  std::unique_ptr<Impl> impl_;
};

// Pi monotonic microseconds: the timeline for MidiEvent::stampUs and for
// UsbClock inputs.
std::uint64_t piNowUs();

}  // namespace mnky::midi
