#include "routing_view_model.hpp"

#include <algorithm>
#include <string>

#include <QStringList>
#include <QVariantMap>

#include "logging.hpp"
#include "logic.hpp"

namespace mnky::app {
namespace {

QVariantMap nodeRow(const RoutingNode& node, int index) {
  QVariantMap row;
  row.insert(QStringLiteral("port"), node.port);
  row.insert(QStringLiteral("nodeIndex"), index);
  row.insert(QStringLiteral("name"), node.name);
  row.insert(QStringLiteral("shortName"), node.shortName);
  row.insert(QStringLiteral("dynamic"), node.dynamic);
  row.insert(QStringLiteral("connected"), node.connected);
  row.insert(QStringLiteral("input"), node.input);
  return row;
}

RoutingNode fixedNode(int port, const char* name, const char* shortName,
                      bool input) {
  RoutingNode node;
  node.port = port;
  node.name = QString::fromLatin1(name);
  node.shortName = QString::fromLatin1(shortName);
  node.input = input;
  return node;
}

}  // namespace

std::vector<RoutingNode> routingInputNodes(
    const std::vector<MidiGlue::UsbPortInfo>& ports) {
  std::vector<RoutingNode> nodes;
  nodes.push_back(fixedNode(midi::portId(midi::Port::TrsIn1), "TRS IN 1", "TRS1",
                            true));
  nodes.push_back(fixedNode(midi::portId(midi::Port::TrsIn2), "TRS IN 2", "TRS2",
                            true));
  nodes.push_back(fixedNode(midi::portId(midi::Port::UsbIn), "USB IN", "USB",
                            true));
  for (const auto& info : ports) {
    if (!info.input) continue;
    RoutingNode node;
    node.port = info.id;
    node.name = QString::fromStdString(info.name);
    node.shortName = node.name;
    node.dynamic = true;
    node.connected = info.connected;
    node.input = true;
    nodes.push_back(std::move(node));
  }
  return nodes;
}

std::vector<RoutingNode> routingOutputNodes(
    const std::vector<MidiGlue::UsbPortInfo>& ports) {
  std::vector<RoutingNode> nodes;
  nodes.push_back(fixedNode(midi::portId(midi::Port::TrsOut1), "TRS OUT 1", "TRS1",
                            false));
  nodes.push_back(fixedNode(midi::portId(midi::Port::TrsOut2), "TRS OUT 2", "TRS2",
                            false));
  nodes.push_back(fixedNode(midi::portId(midi::Port::UsbOut), "USB OUT", "USB",
                            false));
  for (const auto& info : ports) {
    if (!info.output) continue;
    RoutingNode node;
    node.port = info.id;
    node.name = QString::fromStdString(info.name);
    node.shortName = node.name;
    node.dynamic = true;
    node.connected = info.connected;
    node.input = false;
    nodes.push_back(std::move(node));
  }
  return nodes;
}

RoutingViewModel::RoutingViewModel(MidiGlue& midi, bool noPico, QObject* parent)
    : QObject(parent), midi_(midi), noPico_(noPico) {
  rebuildNodes();
  refreshRoutes();
  refreshFilters();
  refreshPipes();
}

midi::Port RoutingViewModel::portForInput(int node) const {
  if (node < 0 || node >= static_cast<int>(inputs_.size())) return midi::Port::TrsIn1;
  return static_cast<midi::Port>(inputs_[static_cast<std::size_t>(node)].port);
}

midi::Port RoutingViewModel::portForOutput(int node) const {
  if (node < 0 || node >= static_cast<int>(outputs_.size())) return midi::Port::TrsOut1;
  return static_cast<midi::Port>(outputs_[static_cast<std::size_t>(node)].port);
}

int RoutingViewModel::inputNodeForPort(int port) const {
  for (std::size_t index = 0; index < inputs_.size(); ++index) {
    if (inputs_[index].port == port) return static_cast<int>(index);
  }
  return -1;
}

int RoutingViewModel::outputNodeForPort(int port) const {
  for (std::size_t index = 0; index < outputs_.size(); ++index) {
    if (outputs_[index].port == port) return static_cast<int>(index);
  }
  return -1;
}

void RoutingViewModel::rebuildNodes() {
  const auto ports = midi_.usbPorts();
  inputs_ = routingInputNodes(ports);
  outputs_ = routingOutputNodes(ports);
  QVariantList inputs;
  inputs.reserve(static_cast<int>(inputs_.size()));
  for (std::size_t index = 0; index < inputs_.size(); ++index) {
    inputs.push_back(nodeRow(inputs_[index], static_cast<int>(index)));
  }
  QVariantList outputs;
  outputs.reserve(static_cast<int>(outputs_.size()));
  for (std::size_t index = 0; index < outputs_.size(); ++index) {
    outputs.push_back(nodeRow(outputs_[index], static_cast<int>(index)));
  }
  const bool changed = inputs != inputNodes_ || outputs != outputNodes_;
  inputNodes_ = std::move(inputs);
  outputNodes_ = std::move(outputs);
  if (selectedInput_ >= static_cast<int>(inputs_.size())) selectedInput_ = -1;
  if (pipeSource_ >= static_cast<int>(inputs_.size())) pipeSource_ = 0;
  if (pipeDestination_ >= static_cast<int>(outputs_.size())) pipeDestination_ = 0;
  if (changed) emit devicesChanged();
}

void RoutingViewModel::setSelectedInput(int source) {
  const int clamped =
      source >= 0 && source < static_cast<int>(inputs_.size()) ? source : -1;
  if (clamped == selectedInput_) return;
  selectedInput_ = clamped;
  emit selectedInputChanged();
}

bool RoutingViewModel::routeEnabled(int source, int destination) const {
  if (source < 0 || source >= static_cast<int>(inputs_.size()) || destination < 0 ||
      destination >= static_cast<int>(outputs_.size())) {
    return false;
  }
  return midi_.routeEnabled(portForInput(source), portForOutput(destination));
}

void RoutingViewModel::toggleRoute(int source, int destination) {
  if (source < 0 || source >= static_cast<int>(inputs_.size()) || destination < 0 ||
      destination >= static_cast<int>(outputs_.size())) {
    return;
  }
  const auto input = portForInput(source);
  const auto output = portForOutput(destination);
  const bool enabled = !midi_.routeEnabled(input, output);
  midi_.setRoute(input, output, enabled);
  const bool trsRoute = input == midi::Port::TrsIn1 || input == midi::Port::TrsIn2 ||
                        output == midi::Port::TrsOut1 ||
                        output == midi::Port::TrsOut2;
  log(std::string("routing: ") +
      inputs_[static_cast<std::size_t>(source)].name.toStdString() + " -> " +
      outputs_[static_cast<std::size_t>(destination)].name.toStdString() +
      (enabled ? " ON" : " OFF") +
      ((noPico_ && trsRoute) ? " (no-pico: stored but INERT, no Pico)" : ""));
  refreshRoutes();
}

bool RoutingViewModel::inputAvailable(int source) const {
  if (source < 0 || source >= static_cast<int>(inputs_.size())) return false;
  const auto& node = inputs_[static_cast<std::size_t>(source)];
  if (node.dynamic || !node.input) return true;
  // Fixed TRS inputs are Pico-owned; the merged USB input is Pi-owned.
  return !noPico_ || node.port == midi::portId(midi::Port::UsbIn);
}

bool RoutingViewModel::outputAvailable(int destination) const {
  if (destination < 0 || destination >= static_cast<int>(outputs_.size())) {
    return false;
  }
  const auto& node = outputs_[static_cast<std::size_t>(destination)];
  if (node.dynamic) return true;
  return !noPico_ || node.port == midi::portId(midi::Port::UsbOut);
}

void RoutingViewModel::selectInput(int source) {
  if (source < 0 || source >= static_cast<int>(inputs_.size())) {
    setSelectedInput(-1);
    return;
  }
  setSelectedInput(selectedInput_ == source ? -1 : source);
}

void RoutingViewModel::selectOutput(int destination) {
  if (selectedInput_ < 0 || destination < 0 ||
      destination >= static_cast<int>(outputs_.size())) {
    return;
  }
  toggleRoute(selectedInput_, destination);
}

void RoutingViewModel::selectInputPort(int port) {
  const int node = inputNodeForPort(port);
  if (node < 0) {
    setSelectedInput(-1);
    return;
  }
  selectInput(node);
}

void RoutingViewModel::selectOutputPort(int port) {
  const int node = outputNodeForPort(port);
  if (node < 0) return;
  selectOutput(node);
}

void RoutingViewModel::setMaskBit(int source, int channel, bool enabled) {
  if (source < 0 || source >= static_cast<int>(inputs_.size()) || channel < 0 ||
      channel >= 16) {
    return;
  }
  auto mask = midi_.channelMask(portForInput(source));
  const auto bit = static_cast<std::uint16_t>(1u << static_cast<unsigned>(channel));
  mask = enabled ? static_cast<std::uint16_t>(mask | bit)
                 : static_cast<std::uint16_t>(mask & ~bit);
  midi_.setChannelMask(portForInput(source), mask);
  refreshFilters();
}

void RoutingViewModel::setTranspose(int source, int semitones) {
  if (source < 0 || source >= static_cast<int>(inputs_.size())) return;
  midi_.setTranspose(portForInput(source), std::max(-24, std::min(24, semitones)));
  refreshFilters();
}

void RoutingViewModel::adjustTranspose(int source, int delta) {
  if (source < 0 || source >= static_cast<int>(inputs_.size())) return;
  setTranspose(source, midi_.transpose(portForInput(source)) + delta);
}

void RoutingViewModel::reset() {
  // A reset is authoritative for the whole matrix: clear every fixed and
  // per-device pair, then restore the documented TRS IN 1+2 -> USB OUT hub.
  for (int source = 0; source < midi::kMaxPorts; ++source) {
    for (int destination = 0; destination < midi::kMaxPorts; ++destination) {
      const auto from = static_cast<midi::Port>(source);
      const auto to = static_cast<midi::Port>(destination);
      if (!midi::isInput(from) || !midi::isOutput(to)) continue;
      midi_.setRoute(from, to, false);
    }
  }
  for (int source = 0; source < routing::kInputCount; ++source) {
    for (int destination = 0; destination < routing::kOutputCount; ++destination) {
      midi_.setRoute(routing::kInputs[source], routing::kOutputs[destination],
                     routing::kDefaultRoutes[source][destination]);
    }
  }
  setSelectedInput(-1);
  log("routing: RESET to the documented default (TRS IN 1+2 -> USB OUT; "
      "per-device routes off)");
  refreshRoutes();
}

int RoutingViewModel::hitWireTest(double x, double y, double width,
                                  double height) const {
  const auto layout =
      routing::layoutFor(static_cast<float>(width), static_cast<float>(height),
                         static_cast<int>(inputs_.size()),
                         static_cast<int>(outputs_.size()));
  int source = -1;
  int destination = -1;
  if (!routing::hitWire(layout, {static_cast<float>(x), static_cast<float>(y)},
                        source, destination)) {
    return -1;
  }
  return source * static_cast<int>(outputs_.size()) + destination;
}

int RoutingViewModel::hitInputNode(double x, double y, double width,
                                   double height) const {
  const auto layout =
      routing::layoutFor(static_cast<float>(width), static_cast<float>(height),
                         static_cast<int>(inputs_.size()),
                         static_cast<int>(outputs_.size()));
  return routing::hitInputNode(layout, {static_cast<float>(x), static_cast<float>(y)});
}

int RoutingViewModel::hitOutputNode(double x, double y, double width,
                                    double height) const {
  const auto layout =
      routing::layoutFor(static_cast<float>(width), static_cast<float>(height),
                         static_cast<int>(inputs_.size()),
                         static_cast<int>(outputs_.size()));
  return routing::hitOutputNode(layout, {static_cast<float>(x), static_cast<float>(y)});
}

void RoutingViewModel::setShiftPage(int page) {
  const int clamped = std::clamp(page, 0, 1);
  if (clamped == shiftPage_) return;
  shiftPage_ = clamped;
  emit shiftPageChanged();
}

void RoutingViewModel::setFilterPage(int page) {
  const int clamped = std::clamp(page, 0, filterPageCount() - 1);
  if (clamped == filterPage_) return;
  filterPage_ = clamped;
  refreshFilters();
}

int RoutingViewModel::filterPageCount() const {
  return std::max(1, (static_cast<int>(inputs_.size()) + kFilterRowsPerPage - 1) /
                         kFilterRowsPerPage);
}

void RoutingViewModel::setPipeSource(int source) {
  const int clamped = std::clamp(source, 0, static_cast<int>(inputs_.size()) - 1);
  if (clamped == pipeSource_) return;
  pipeSource_ = clamped;
  selectedPipe_ = 0;
  paramPage_ = 0;
  emit pipeRouteChanged();
  refreshPipes();
}

void RoutingViewModel::setPipeDestination(int destination) {
  const int clamped =
      std::clamp(destination, 0, static_cast<int>(outputs_.size()) - 1);
  if (clamped == pipeDestination_) return;
  pipeDestination_ = clamped;
  selectedPipe_ = 0;
  paramPage_ = 0;
  emit pipeRouteChanged();
  refreshPipes();
}

void RoutingViewModel::cyclePipeSource(int delta) {
  setPipeSource(moveIndex(pipeSource_, delta < 0 ? -1 : 1,
                          static_cast<int>(inputs_.size())));
}

void RoutingViewModel::cyclePipeDestination(int delta) {
  setPipeDestination(moveIndex(pipeDestination_, delta < 0 ? -1 : 1,
                               static_cast<int>(outputs_.size())));
}

QString RoutingViewModel::pipeSourceName() const {
  if (pipeSource_ < 0 || pipeSource_ >= static_cast<int>(inputs_.size())) return {};
  return inputs_[static_cast<std::size_t>(pipeSource_)].name;
}

QString RoutingViewModel::pipeDestinationName() const {
  if (pipeDestination_ < 0 ||
      pipeDestination_ >= static_cast<int>(outputs_.size())) {
    return {};
  }
  return outputs_[static_cast<std::size_t>(pipeDestination_)].name;
}

void RoutingViewModel::selectPipe(int index) {
  const int count = pipeCount();
  const int clamped = count == 0 ? 0 : std::clamp(index, 0, count - 1);
  if (clamped == selectedPipe_) return;
  selectedPipe_ = clamped;
  paramPage_ = 0;
  refreshPipes();
}

void RoutingViewModel::cycleSelectedPipe(int delta) {
  if (pipeCount() == 0) return;
  selectPipe(selectedPipe_ + (delta < 0 ? -1 : 1));
}

int RoutingViewModel::paramPageCount() const {
  const auto pipes = midi_.routePipes(pipeInput(), pipeOutput());
  if (selectedPipe_ < 0 || selectedPipe_ >= static_cast<int>(pipes.size())) return 1;
  const auto specs = pipeParamSpecs(pipes[static_cast<std::size_t>(selectedPipe_)].type);
  return std::max(1, (static_cast<int>(specs.size()) + kPipeParamsPerPage - 1) /
                         kPipeParamsPerPage);
}

void RoutingViewModel::setParamPage(int page) {
  const int clamped = std::clamp(page, 0, paramPageCount() - 1);
  if (clamped == paramPage_) return;
  paramPage_ = clamped;
  refreshPipes();
}

void RoutingViewModel::setAddTypeIndex(int index) {
  const int count = static_cast<int>(pipeTypeSpecs().size());
  const int clamped = std::clamp(index, 0, count - 1);
  if (clamped == addTypeIndex_) return;
  addTypeIndex_ = clamped;
  emit addTypeChanged();
}

QString RoutingViewModel::addTypeName() const {
  const auto types = pipeTypeSpecs();
  if (addTypeIndex_ < 0 || addTypeIndex_ >= static_cast<int>(types.size())) {
    return {};
  }
  return QString::fromLatin1(types[static_cast<std::size_t>(addTypeIndex_)].name);
}

midi::Port RoutingViewModel::pipeInput() const {
  return portForInput(pipeSource_);
}

midi::Port RoutingViewModel::pipeOutput() const {
  return portForOutput(pipeDestination_);
}

int RoutingViewModel::pipeCount() const {
  return static_cast<int>(midi_.routePipes(pipeInput(), pipeOutput()).size());
}

QString RoutingViewModel::pipeRouteText() const {
  return pipeSourceName() + QStringLiteral(" -> ") + pipeDestinationName();
}

QString RoutingViewModel::selectedPipeScopeText() const {
  return QString::fromStdString(formatChannelMask(
      midi_.routePipeChannelMask(pipeInput(), pipeOutput(), selectedPipe_)));
}

int RoutingViewModel::selectedPipeChannelMask() const {
  return midi_.routePipeChannelMask(pipeInput(), pipeOutput(), selectedPipe_);
}

void RoutingViewModel::setSelectedPipeChannelMask(int mask) {
  if (mask < 0) mask = 0;
  if (mask > 0xFFFF) mask = 0xFFFF;
  const auto value = static_cast<std::uint16_t>(mask);
  if (!midi_.setRoutePipeChannelMask(pipeInput(), pipeOutput(), selectedPipe_,
                                     value)) {
    return;
  }
  log(std::string("routing: pipe scope ") + pipeRouteText().toStdString() + " #" +
      std::to_string(selectedPipe_ + 1) + " " + formatChannelMask(value));
  refreshPipes();
}

QString RoutingViewModel::pipeStatusText() const {
  const auto route = pipeRouteText();
  const int count = pipeCount();
  if (count == 0) return route + QStringLiteral(" - NO PIPES");
  const bool piOwned = midi::piOwnsPipeChain(pipeInput(), pipeOutput());
  QString status = route + QStringLiteral(" - ") + QString::number(count) +
                   (count == 1 ? QStringLiteral(" PIPE - ") : QStringLiteral(" PIPES - "));
  // Engine split is explicit: a route touching USB (fixed or a per-device
  // registry id) is evaluated by the Pi (Wave 1 only; midifx process() rejects
  // the clocked ids); a TRS->TRS chain is configured on the Pico (0x18/0x19)
  // and its bytes go out with 0x1A, where the firmware evaluates both waves.
  // The Pi applies the channel mask and its source transpose before the 0x1A
  // send, so that route's effective order is mask -> transpose -> Pico chain.
  status += piOwned ? QStringLiteral("PI ENGINE (WAVE 1)")
                    : QStringLiteral("PICO 0x1A CHAIN (mask -> transpose -> chain)");
  if (noPico_ && !piOwned) {
    status += QStringLiteral(" - INERT (NO PICO; 0x18/0x19/0x1A not sent)");
  }
  const auto pipes = midi_.routePipes(pipeInput(), pipeOutput());
  bool wave2 = false;
  int bypassed = 0;
  for (const auto& pipe : pipes) {
    if (pipe.bypass) ++bypassed;
    if (!pipe.bypass && isClockedPipeType(pipe.type)) wave2 = true;
  }
  if (wave2 && piOwned) {
    status += QStringLiteral(" - WAVE 2 NOT IN PI ENGINE: midi::PipeChain::process() "
                             "rejects ids 12..16");
  } else if (wave2 && !(noPico_ && !piOwned)) {
    status += QStringLiteral(" - WAVE 2 LIVE (Pico clocked chain)");
  }
  if (bypassed > 0) {
    status += QStringLiteral(" - ") + QString::number(bypassed) +
              QStringLiteral(" BYPASSED");
  }
  const int scope = selectedPipeChannelMask();
  if (scope != 0xFFFF) {
    status += QStringLiteral(" - ") +
              QString::fromStdString(formatChannelMask(
                  static_cast<std::uint16_t>(scope)));
  }
  if (!midi_.routeEnabled(pipeInput(), pipeOutput())) {
    status += QStringLiteral(" - ROUTE OFF");
  }
  return status;
}

void RoutingViewModel::addPipe() {
  const auto types = pipeTypeSpecs();
  if (addTypeIndex_ < 0 || addTypeIndex_ >= static_cast<int>(types.size())) return;
  const auto type = types[static_cast<std::size_t>(addTypeIndex_)].type;
  if (!midi_.addRoutePipe(pipeInput(), pipeOutput(), type)) {
    log(std::string("routing: pipe add refused for ") + pipeRouteText().toStdString());
    return;
  }
  selectedPipe_ = pipeCount() - 1;
  log(std::string("routing: pipe added to ") + pipeRouteText().toStdString() + ": " +
      types[static_cast<std::size_t>(addTypeIndex_)].name);
  refreshPipes();
}

void RoutingViewModel::removeSelectedPipe() {
  if (!midi_.removeRoutePipe(pipeInput(), pipeOutput(), selectedPipe_)) return;
  selectedPipe_ = std::max(0, std::min(selectedPipe_, pipeCount() - 1));
  refreshPipes();
}

void RoutingViewModel::moveSelectedPipe(int delta) {
  if (!midi_.moveRoutePipe(pipeInput(), pipeOutput(), selectedPipe_, delta)) return;
  selectedPipe_ = std::clamp(selectedPipe_ + (delta < 0 ? -1 : 1), 0,
                             std::max(0, pipeCount() - 1));
  refreshPipes();
}

void RoutingViewModel::movePipe(int index, int delta) {
  if (index < 0 || index >= pipeCount()) return;
  if (midi_.moveRoutePipe(pipeInput(), pipeOutput(), index, delta)) {
    selectedPipe_ = std::clamp(index + (delta < 0 ? -1 : 1), 0,
                               std::max(0, pipeCount() - 1));
  } else {
    selectedPipe_ = index;
  }
  paramPage_ = 0;
  refreshPipes();
}

void RoutingViewModel::removePipe(int index) {
  if (index < 0 || index >= pipeCount()) return;
  if (!midi_.removeRoutePipe(pipeInput(), pipeOutput(), index)) return;
  selectedPipe_ = std::max(0, std::min(index, pipeCount() - 1));
  paramPage_ = 0;
  refreshPipes();
}

void RoutingViewModel::toggleSelectedPipeBypass() {
  const auto pipes = midi_.routePipes(pipeInput(), pipeOutput());
  if (selectedPipe_ < 0 || selectedPipe_ >= static_cast<int>(pipes.size())) return;
  if (!midi_.setRoutePipeBypass(pipeInput(), pipeOutput(), selectedPipe_,
                                !pipes[static_cast<std::size_t>(selectedPipe_)].bypass)) {
    return;
  }
  refreshPipes();
}

void RoutingViewModel::setPipeBypass(int index, bool bypass) {
  if (!midi_.setRoutePipeBypass(pipeInput(), pipeOutput(), index, bypass)) return;
  refreshPipes();
}

void RoutingViewModel::setPipeParam(int pipeIndex, int param, int value) {
  if (!midi_.setRoutePipeParam(pipeInput(), pipeOutput(), pipeIndex, param, value)) {
    return;
  }
  refreshPipes();
}

void RoutingViewModel::cycleAddType(int delta) {
  setAddTypeIndex(addTypeIndex_ + (delta < 0 ? -1 : 1));
}

void RoutingViewModel::refreshPipes() {
  const auto pipes = midi_.routePipes(pipeInput(), pipeOutput());
  if (selectedPipe_ >= static_cast<int>(pipes.size())) {
    selectedPipe_ = pipes.empty() ? 0 : static_cast<int>(pipes.size()) - 1;
  }
  QVariantList rows;
  rows.reserve(static_cast<int>(pipes.size()));
  for (std::size_t index = 0; index < pipes.size(); ++index) {
    const auto& pipe = pipes[index];
    const auto* spec = pipeTypeSpec(pipe.type);
    QVariantMap row;
    row.insert(QStringLiteral("index"), static_cast<int>(index));
    row.insert(QStringLiteral("type"), static_cast<int>(pipe.type));
    row.insert(QStringLiteral("typeName"),
               spec == nullptr ? QStringLiteral("?") : QString::fromLatin1(spec->name));
    row.insert(QStringLiteral("shortName"),
               spec == nullptr ? QStringLiteral("?") : QString::fromLatin1(spec->shortName));
    row.insert(QStringLiteral("bypass"), pipe.bypass);
    row.insert(QStringLiteral("clocked"), spec != nullptr && spec->clocked);
    row.insert(QStringLiteral("paramCount"),
               spec == nullptr ? 0 : static_cast<int>(spec->params.size()));
    row.insert(QStringLiteral("selected"), static_cast<int>(index) == selectedPipe_);
    row.insert(QStringLiteral("chMask"), static_cast<int>(pipe.chMask));
    row.insert(QStringLiteral("scopeText"),
               QString::fromStdString(formatChannelMask(pipe.chMask)));
    const bool piOwned = midi::piOwnsPipeChain(pipeInput(), pipeOutput());
    QString rowStatus = QStringLiteral("ACTIVE");
    if (pipe.bypass) {
      rowStatus = QStringLiteral("BYPASS");
    } else if (noPico_ && !piOwned) {
      rowStatus = QStringLiteral("INERT (NO PICO)");
    } else if (spec != nullptr && spec->clocked) {
      rowStatus = piOwned ? QStringLiteral("PI: NO WAVE-2 ENGINE")
                          : QStringLiteral("ACTIVE (PICO CLOCK)");
    }
    if (pipe.chMask != 0xFFFF) {
      rowStatus += QStringLiteral(" ") +
                   QString::fromStdString(formatChannelMask(pipe.chMask));
    }
    row.insert(QStringLiteral("status"), rowStatus);
    row.insert(QStringLiteral("inert"), noPico_ && !piOwned && !pipe.bypass);
    rows.push_back(row);
  }

  QVariantList params;
  if (selectedPipe_ >= 0 && selectedPipe_ < static_cast<int>(pipes.size())) {
    const auto& pipe = pipes[static_cast<std::size_t>(selectedPipe_)];
    const auto specs = pipeParamSpecs(pipe.type);
    const int pageCount =
        std::max(1, (static_cast<int>(specs.size()) + kPipeParamsPerPage - 1) /
                        kPipeParamsPerPage);
    paramPage_ = std::clamp(paramPage_, 0, pageCount - 1);
    const std::size_t firstParam =
        static_cast<std::size_t>(paramPage_ * kPipeParamsPerPage);
    const std::size_t lastParam =
        std::min(specs.size(), firstParam + static_cast<std::size_t>(kPipeParamsPerPage));
    for (std::size_t index = firstParam; index < lastParam; ++index) {
      const auto& spec = specs[index];
      const int value = index < pipe.params.size()
                            ? clampPipeParam(spec, pipe.params[index])
                            : spec.defaultValue;
      QStringList choices;
      for (int choice = 0; choice < spec.choiceCount; ++choice) {
        choices << QString::fromLatin1(spec.choices[choice]);
      }
      QString editor = QStringLiteral("stepper");
      if (spec.editor == PipeParamEditor::Toggle) editor = QStringLiteral("toggle");
      else if (spec.editor == PipeParamEditor::Choice) editor = QStringLiteral("choice");
      else if (spec.editor == PipeParamEditor::Note) editor = QStringLiteral("note");
      else if (spec.editor == PipeParamEditor::Level) editor = QStringLiteral("level");
      else if (spec.editor == PipeParamEditor::Milliseconds) editor = QStringLiteral("ms");
      else if (spec.editor == PipeParamEditor::Signed) editor = QStringLiteral("signed");
      QVariantMap param;
      param.insert(QStringLiteral("name"), QString::fromLatin1(spec.name));
      param.insert(QStringLiteral("param"), static_cast<int>(index));
      param.insert(QStringLiteral("minimum"), spec.minimum);
      param.insert(QStringLiteral("maximum"), spec.maximum);
      param.insert(QStringLiteral("value"), value);
      param.insert(QStringLiteral("editor"), editor);
      param.insert(QStringLiteral("choices"), choices);
      param.insert(QStringLiteral("valueText"),
                   QString::fromStdString(pipeParamText(spec, value)));
      params.push_back(param);
    }
  }

  const QString routeText = pipeRouteText();
  const QString statusText = pipeStatusText();
  if (rows == pipeRows_ && params == pipeParams_ && routeText == pipeRouteText_ &&
      statusText == pipeStatusText_) {
    return;
  }
  pipeRows_ = std::move(rows);
  pipeParams_ = std::move(params);
  pipeRouteText_ = routeText;
  pipeStatusText_ = statusText;
  emit pipesChanged();
}

void RoutingViewModel::refresh() {
  rebuildNodes();
  refreshRoutes();
  refreshFilters();
  refreshPipes();
  const int inputs = static_cast<int>(midi_.inputCount());
  const int outputs = static_cast<int>(midi_.outputCount());
  if (inputs != usbInputs_ || outputs != usbOutputs_) {
    usbInputs_ = inputs;
    usbOutputs_ = outputs;
    emit devicesChanged();
  }
}

void RoutingViewModel::refreshRoutes() {
  QVariantList routes;
  routes.reserve(static_cast<int>(inputs_.size() * outputs_.size()));
  for (int source = 0; source < static_cast<int>(inputs_.size()); ++source) {
    for (int destination = 0; destination < static_cast<int>(outputs_.size());
         ++destination) {
      routes.push_back(routeEnabled(source, destination));
    }
  }
  if (routes == routes_) return;
  routes_ = std::move(routes);
  emit routesChanged();
}

void RoutingViewModel::refreshFilters() {
  QVariantList masks;
  QVariantList transposes;
  for (const auto& node : inputs_) {
    const auto port = static_cast<midi::Port>(node.port);
    masks.push_back(static_cast<int>(midi_.channelMask(port)));
    transposes.push_back(midi_.transpose(port));
  }
  if (masks != channelMasks_ || transposes != transposes_) {
    channelMasks_ = std::move(masks);
    transposes_ = std::move(transposes);
    emit filtersChanged();
  }

  // The SHIFT filter page shows at most three source rows at a time; a device
  // list longer than that is paged so every row keeps its 44 px touch height.
  filterPage_ = std::clamp(filterPage_, 0, filterPageCount() - 1);
  QVariantList rows;
  const int first = filterPage_ * kFilterRowsPerPage;
  const int last = std::min(static_cast<int>(inputs_.size()),
                            first + kFilterRowsPerPage);
  for (int index = first; index < last; ++index) {
    rows.push_back(nodeRow(inputs_[static_cast<std::size_t>(index)], index));
  }
  if (rows != filterPageRows_) {
    filterPageRows_ = std::move(rows);
    emit filtersChanged();
  }
}

}  // namespace mnky::app
