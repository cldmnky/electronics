#include "routing_view_model.hpp"

#include <algorithm>

#include "logging.hpp"

namespace mnky::app {

RoutingViewModel::RoutingViewModel(MidiGlue& midi, QObject* parent)
    : QObject(parent), midi_(midi) {
  refreshRoutes();
  refreshFilters();
  refresh();
}

void RoutingViewModel::setSelectedInput(int source) {
  const int clamped = source >= 0 && source < routing::kInputCount ? source : -1;
  if (clamped == selectedInput_) return;
  selectedInput_ = clamped;
  emit selectedInputChanged();
}

bool RoutingViewModel::routeEnabled(int source, int destination) const {
  if (source < 0 || source >= routing::kInputCount || destination < 0 ||
      destination >= routing::kOutputCount) {
    return false;
  }
  return midi_.routeEnabled(routing::kInputs[source], routing::kOutputs[destination]);
}

void RoutingViewModel::toggleRoute(int source, int destination) {
  if (source < 0 || source >= routing::kInputCount || destination < 0 ||
      destination >= routing::kOutputCount) {
    return;
  }
  const auto input = routing::kInputs[source];
  const auto output = routing::kOutputs[destination];
  const bool enabled = !midi_.routeEnabled(input, output);
  midi_.setRoute(input, output, enabled);
  log(std::string("routing: ") + routing::kInputNames[source] + " -> " +
      routing::kOutputNames[destination] + (enabled ? " ON" : " OFF"));
  refreshRoutes();
}

void RoutingViewModel::selectInput(int source) {
  if (source < 0 || source >= routing::kInputCount) {
    setSelectedInput(-1);
    return;
  }
  setSelectedInput(selectedInput_ == source ? -1 : source);
}

void RoutingViewModel::selectOutput(int destination) {
  if (selectedInput_ < 0) return;
  toggleRoute(selectedInput_, destination);
}

void RoutingViewModel::setMaskBit(int source, int channel, bool enabled) {
  if (source < 0 || source >= routing::kInputCount || channel < 0 || channel >= 16) return;
  auto mask = midi_.channelMask(routing::kInputs[source]);
  const auto bit = static_cast<std::uint16_t>(1u << static_cast<unsigned>(channel));
  mask = enabled ? static_cast<std::uint16_t>(mask | bit)
                 : static_cast<std::uint16_t>(mask & ~bit);
  midi_.setChannelMask(routing::kInputs[source], mask);
  refreshFilters();
}

void RoutingViewModel::setTranspose(int source, int semitones) {
  if (source < 0 || source >= routing::kInputCount) return;
  midi_.setTranspose(routing::kInputs[source], std::max(-24, std::min(24, semitones)));
  refreshFilters();
}

void RoutingViewModel::adjustTranspose(int source, int delta) {
  if (source < 0 || source >= routing::kInputCount) return;
  setTranspose(source, midi_.transpose(routing::kInputs[source]) + delta);
}

void RoutingViewModel::reset() {
  for (int source = 0; source < routing::kInputCount; ++source) {
    for (int destination = 0; destination < routing::kOutputCount; ++destination) {
      midi_.setRoute(routing::kInputs[source], routing::kOutputs[destination],
                     routing::kDefaultRoutes[source][destination]);
    }
  }
  setSelectedInput(-1);
  log("routing: RESET to the documented default (TRS IN 1+2 -> USB OUT)");
  refreshRoutes();
}

int RoutingViewModel::hitWireTest(double x, double y, double width, double height) const {
  const auto layout = routing::layoutFor(static_cast<float>(width), static_cast<float>(height));
  int source = -1;
  int destination = -1;
  if (!routing::hitWire(layout, {static_cast<float>(x), static_cast<float>(y)}, source,
                        destination)) {
    return -1;
  }
  return source * routing::kOutputCount + destination;
}

int RoutingViewModel::hitInputNode(double x, double y, double width, double height) const {
  const auto layout = routing::layoutFor(static_cast<float>(width), static_cast<float>(height));
  return routing::hitInputNode(layout, {static_cast<float>(x), static_cast<float>(y)});
}

int RoutingViewModel::hitOutputNode(double x, double y, double width, double height) const {
  const auto layout = routing::layoutFor(static_cast<float>(width), static_cast<float>(height));
  return routing::hitOutputNode(layout, {static_cast<float>(x), static_cast<float>(y)});
}

void RoutingViewModel::refresh() {
  refreshRoutes();
  refreshFilters();
  const int inputs = static_cast<int>(midi_.inputCount());
  const int outputs = static_cast<int>(midi_.outputCount());
  if (inputs != usbInputs_ || outputs != usbOutputs_) {
    usbInputs_ = inputs;
    usbOutputs_ = outputs;
    emit devicesChanged();
  }
}

void RoutingViewModel::refreshRoutes() {
  std::uint32_t signature = 0;
  QVariantList routes;
  routes.reserve(routing::kInputCount * routing::kOutputCount);
  for (int source = 0; source < routing::kInputCount; ++source) {
    for (int destination = 0; destination < routing::kOutputCount; ++destination) {
      const bool enabled = midi_.routeEnabled(routing::kInputs[source],
                                              routing::kOutputs[destination]);
      routes.push_back(enabled);
      if (enabled) {
        signature |= 1u << static_cast<unsigned>(source * routing::kOutputCount +
                                                 destination);
      }
    }
  }
  if (signature == routeSignature_ && routes_ == routes) return;
  routeSignature_ = signature;
  routes_ = routes;
  emit routesChanged();
}

void RoutingViewModel::refreshFilters() {
  QVariantList masks;
  QVariantList transposes;
  for (int source = 0; source < routing::kInputCount; ++source) {
    masks.push_back(static_cast<int>(midi_.channelMask(routing::kInputs[source])));
    transposes.push_back(midi_.transpose(routing::kInputs[source]));
  }
  if (masks != channelMasks_ || transposes != transposes_) {
    channelMasks_ = masks;
    transposes_ = transposes;
    emit filtersChanged();
  }
}

}  // namespace mnky::app
