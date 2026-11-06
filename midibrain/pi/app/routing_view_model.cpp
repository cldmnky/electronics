#include "routing_view_model.hpp"

#include "pipe_preset.hpp"
#include "pipe_ui.hpp"

#include <algorithm>
#include <filesystem>
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
  // A13: the generator is a source-less route kind, listed beside the TRS/USB
  // sources. Its chain head emits on the clock grid; nothing downstream may ask
  // it for an input channel.
  nodes.push_back(fixedNode(midi::kSrcGenerator, "GENERATOR", "GEN", true));
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
  if (inputs_[static_cast<std::size_t>(source)].port == midi::kSrcGenerator) {
    return midi_.generatorRouteEnabled(portForOutput(destination));
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
  if (inputs_[static_cast<std::size_t>(source)].port == midi::kSrcGenerator) {
    // Generator route: no input exists, so the wire is the chain's own gate.
    const bool enabled = !midi_.generatorRouteEnabled(output);
    midi_.setGeneratorRoute(output, enabled);
    log(std::string("routing: GENERATOR -> ") +
        outputs_[static_cast<std::size_t>(destination)].name.toStdString() +
        (enabled ? " ON" : " OFF"));
    refreshRoutes();
    refreshPipes();
    return;
  }
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
  refreshPipes();
}

bool RoutingViewModel::inputAvailable(int source) const {
  if (source < 0 || source >= static_cast<int>(inputs_.size())) return false;
  const auto& node = inputs_[static_cast<std::size_t>(source)];
  // The generator is Pi- or Pico-generated depending on its destination, never
  // dependent on a TRS input receiver.
  if (node.port == midi::kSrcGenerator) return true;
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
  if (inputs_[static_cast<std::size_t>(source)].port == midi::kSrcGenerator) return;
  auto mask = midi_.channelMask(portForInput(source));
  const auto bit = static_cast<std::uint16_t>(1u << static_cast<unsigned>(channel));
  mask = enabled ? static_cast<std::uint16_t>(mask | bit)
                 : static_cast<std::uint16_t>(mask & ~bit);
  midi_.setChannelMask(portForInput(source), mask);
  refreshFilters();
}

void RoutingViewModel::setTranspose(int source, int semitones) {
  if (source < 0 || source >= static_cast<int>(inputs_.size())) return;
  if (inputs_[static_cast<std::size_t>(source)].port == midi::kSrcGenerator) return;
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
  midi_.clearGeneratorRoutes();
  midi_.clearOutputInserts();
  insertSlot_ = -1;
  insertHint_.clear();
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

QVariantList RoutingViewModel::filterTabRows() const {
  QVariantList rows;
  for (std::size_t index = 0; index < inputs_.size(); ++index) {
    if (inputs_[index].port == midi::kSrcGenerator) continue;
    rows.push_back(nodeRow(inputs_[index], static_cast<int>(index)));
  }
  return rows;
}

int RoutingViewModel::filterPageCount() const {
  return std::max(1,
                  (static_cast<int>(filterTabRows().size()) + kFilterRowsPerPage - 1) /
                      kFilterRowsPerPage);
}

void RoutingViewModel::setPipeSource(int source) {
  const int clamped = std::clamp(source, 0, static_cast<int>(inputs_.size()) - 1);
  if (clamped == pipeSource_) return;
  pipeSource_ = clamped;
  selectedPipe_ = 0;
  insertSlot_ = -1;
  insertHint_.clear();
  emit pipeRouteChanged();
  refreshPipes();
}

void RoutingViewModel::setPipeDestination(int destination) {
  const int clamped =
      std::clamp(destination, 0, static_cast<int>(outputs_.size()) - 1);
  if (clamped == pipeDestination_) return;
  pipeDestination_ = clamped;
  selectedPipe_ = 0;
  insertSlot_ = -1;
  insertHint_.clear();
  emit pipeRouteChanged();
  refreshPipes();
}

QString RoutingViewModel::pipeSourceName() const {
  if (pipeSource_ < 0 || pipeSource_ >= static_cast<int>(inputs_.size())) return {};
  return inputs_[static_cast<std::size_t>(pipeSource_)].name;
}

QString RoutingViewModel::selectedPipeName() const {
  const auto pipes = currentPipes();
  if (selectedPipe_ < 0 || selectedPipe_ >= static_cast<int>(pipes.size())) return {};
  return pipeui::pipeName(static_cast<int>(pipes[static_cast<std::size_t>(selectedPipe_)].type));
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
  refreshPipes();
}

void RoutingViewModel::cycleSelectedPipe(int delta) {
  if (pipeCount() == 0) return;
  selectPipe(selectedPipe_ + (delta < 0 ? -1 : 1));
}

void RoutingViewModel::setAddTypeIndex(int index) {
  // The list can be the generator-head subset, so map through the id instead of
  // storing the list index.
  const int labels = std::max(1, static_cast<int>(addTypeLabels().size()));
  const int id = addTypeIdAt(std::clamp(index, 0, labels - 1));
  const int clamped = id - 1;
  if (clamped == addTypeIndex_) return;
  addTypeIndex_ = clamped;
  addHint_.clear();
  emit addTypeChanged();
}

QString RoutingViewModel::addTypeName() const {
  return pipeui::pipeName(addTypeIndex_ + 1);
}

midi::Port RoutingViewModel::pipeInput() const {
  return portForInput(pipeSource_);
}

midi::Port RoutingViewModel::pipeOutput() const {
  return portForOutput(pipeDestination_);
}

int RoutingViewModel::pipeSourceId() const {
  if (pipeSource_ < 0 || pipeSource_ >= static_cast<int>(inputs_.size())) return -1;
  return inputs_[static_cast<std::size_t>(pipeSource_)].port;
}

bool RoutingViewModel::generatorSelected() const {
  return !insertSelected() && pipeSourceId() == midi::kSrcGenerator;
}

bool RoutingViewModel::insertSelected() const {
  return insertSlot_ >= 0 && insertSlot_ < insertChainCount();
}

bool RoutingViewModel::outputInsertsAvailable() const {
  return midi_.outputInsertSupported(pipeOutput());
}

int RoutingViewModel::insertChainCount() const {
  if (!outputInsertsAvailable()) return 0;
  return static_cast<int>(midi_.outputInsertChains(pipeOutput()).size());
}

void RoutingViewModel::refreshInsertRows() {
  QVariantList rows;
  if (outputInsertsAvailable()) {
    const auto chains = midi_.outputInsertChains(pipeOutput());
    rows.reserve(static_cast<int>(chains.size()));
    for (std::size_t index = 0; index < chains.size(); ++index) {
      QVariantMap row;
      row.insert(QStringLiteral("index"), static_cast<int>(index));
      row.insert(QStringLiteral("pipeCount"),
                 static_cast<int>(chains[index].pipes.size()));
      row.insert(QStringLiteral("enabled"), chains[index].enabled);
      row.insert(QStringLiteral("selected"),
                 static_cast<int>(index) == insertSlot_);
      row.insert(QStringLiteral("text"),
                 QStringLiteral("%1 %2P").arg(index + 1).arg(chains[index].pipes.size()));
      rows.push_back(row);
    }
  }
  if (insertSlot_ >= static_cast<int>(rows.size())) insertSlot_ = -1;
  insertRows_ = std::move(rows);
}

void RoutingViewModel::selectRouteChain() {
  if (insertSlot_ == -1) return;
  insertSlot_ = -1;
  selectedPipe_ = 0;
  insertHint_.clear();
  refreshPipes();
}

void RoutingViewModel::selectInsertChain(int index) {
  const int count = insertChainCount();
  if (index < 0 || index >= count) return;
  if (insertSlot_ == index) return;
  insertSlot_ = index;
  selectedPipe_ = 0;
  insertHint_.clear();
  refreshPipes();
}

void RoutingViewModel::addInsertChain() {
  if (!outputInsertsAvailable()) {
    insertHint_ = QStringLiteral("OUTPUT INSERTS ARE USB-ONLY (PICO OWNS TRS)");
    emit pipesChanged();
    return;
  }
  if (!midi_.addOutputInsertChain(pipeOutput())) {
    insertHint_ = QStringLiteral("OUTPUT INSERT LIMIT (4) REACHED");
    emit pipesChanged();
    return;
  }
  insertHint_.clear();
  insertSlot_ = insertChainCount() - 1;
  selectedPipe_ = 0;
  log(std::string("routing: output insert added on ") +
      pipeDestinationName().toStdString());
  refreshPipes();
}

void RoutingViewModel::removeInsertChain(int index) {
  if (index < 0 || index >= insertChainCount()) return;
  if (!midi_.removeOutputInsertChain(pipeOutput(), index)) return;
  insertSlot_ = insertChainCount() == 0 ? -1
                                        : std::min(index, insertChainCount() - 1);
  selectedPipe_ = 0;
  insertHint_.clear();
  refreshPipes();
}

void RoutingViewModel::moveInsertChain(int index, int delta) {
  if (index < 0 || index >= insertChainCount()) return;
  if (!midi_.moveOutputInsertChain(pipeOutput(), index, delta)) return;
  insertSlot_ = std::clamp(index + (delta < 0 ? -1 : 1), 0,
                           insertChainCount() - 1);
  refreshPipes();
}

void RoutingViewModel::toggleInsertChain(int index) {
  if (index < 0 || index >= insertChainCount()) return;
  const bool enabled = !midi_.outputInsertEnabled(pipeOutput(), index);
  (void)midi_.setOutputInsertEnabled(pipeOutput(), index, enabled);
  refreshPipes();
}

bool RoutingViewModel::generatorHeadPending() const {
  return generatorSelected() && currentPipes().empty();
}

bool RoutingViewModel::pipeRouteEnabled() const {
  if (insertSelected()) {
    return midi_.outputInsertEnabled(pipeOutput(), insertSlot_);
  }
  return generatorSelected() ? midi_.generatorRouteEnabled(pipeOutput())
                             : midi_.routeEnabled(pipeInput(), pipeOutput());
}

std::vector<session::MidiPipe> RoutingViewModel::currentPipes() const {
  if (insertSelected()) {
    return midi_.outputInsertPipes(pipeOutput(), insertSlot_);
  }
  return generatorSelected() ? midi_.generatorPipes(pipeOutput())
                             : midi_.routePipes(pipeInput(), pipeOutput());
}

int RoutingViewModel::pipeCount() const {
  return static_cast<int>(currentPipes().size());
}

QString RoutingViewModel::pipeRouteText() const {
  if (insertSelected()) {
    return pipeDestinationName() + QStringLiteral(" INSERT ") +
           QString::number(insertSlot_ + 1);
  }
  return pipeSourceName() + QStringLiteral(" -> ") + pipeDestinationName();
}

QString RoutingViewModel::selectedPipeScopeText() const {
  return QString::fromStdString(
      formatChannelMask(static_cast<std::uint16_t>(selectedPipeChannelMask())));
}

int RoutingViewModel::selectedPipeChannelMask() const {
  if (insertSelected()) {
    return midi_.outputInsertPipeChannelMask(pipeOutput(), insertSlot_,
                                             selectedPipe_);
  }
  return generatorSelected()
             ? midi_.generatorPipeChannelMask(pipeOutput(), selectedPipe_)
             : midi_.routePipeChannelMask(pipeInput(), pipeOutput(), selectedPipe_);
}

void RoutingViewModel::setSelectedPipeChannelMask(int mask) {
  if (mask < 0) mask = 0;
  if (mask > 0xFFFF) mask = 0xFFFF;
  const auto value = static_cast<std::uint16_t>(mask);
  const bool stored =
      insertSelected()
          ? midi_.setOutputInsertPipeChannelMask(pipeOutput(), insertSlot_,
                                                 selectedPipe_, value)
          : (generatorSelected()
                 ? midi_.setGeneratorPipeChannelMask(pipeOutput(), selectedPipe_,
                                                     value)
                 : midi_.setRoutePipeChannelMask(pipeInput(), pipeOutput(),
                                                 selectedPipe_, value));
  if (!stored) {
    return;
  }
  log(std::string("routing: pipe scope ") + pipeRouteText().toStdString() + " #" +
      std::to_string(selectedPipe_ + 1) + " " + formatChannelMask(value));
  refreshPipes();
}

QString RoutingViewModel::pipeStatusText() const {
  const auto route = pipeRouteText();
  const int count = pipeCount();
  const bool insert = insertSelected();
  const bool generator = generatorSelected();
  if (count == 0) {
    if (insert) {
      return route + QStringLiteral(" - NO PIPES - INSERT ") +
             (pipeRouteEnabled() ? QStringLiteral("ON") : QStringLiteral("OFF"));
    }
    return route +
           (generator ? QStringLiteral(" - NO PIPES - GENERATOR NEEDS NO INPUT")
                      : QStringLiteral(" - NO PIPES"));
  }
  const bool piOwned = insert ? true
                              : midi::piOwnsPipeChain(pipeSourceId(), pipeOutput());
  QString status = route + QStringLiteral(" - ") + QString::number(count) +
                   (count == 1 ? QStringLiteral(" PIPE - ") : QStringLiteral(" PIPES - "));
  if (insert) {
    status += QStringLiteral("PI ENGINE - EVERY MESSAGE TO ") +
              pipeDestinationName();
  } else if (generator && !piOwned) {
    // The router can only own USB-side chains; a TRS generator route belongs to
    // the Pico and this build's link seam does not carry pseudo-source frames.
    status += QStringLiteral("PICO-OWNED - NOT SENT BY THIS BUILD");
  } else {
  // Engine split, stated as the code is: a route touching USB (fixed or a
  // per-device registry id) is evaluated by the Pi through the shared midifx
  // core, which implements all 16 types; a TRS->TRS chain is configured on the
  // Pico (0x18/0x19) and evaluated by the firmware, whose route_chain drives
  // the same core's clockTick() from its alarm ticks. The Pi pump
  // (MidiGlue::pump) calls Router::tick() only, so the Pi's millisecond pass
  // runs (NoteLength, MidiDelay, the stateless pipes) while the 24-PPQN grid
  // pass that steps an arp/repeater/LFO has no Pi-side caller yet: a clocked
  // pipe on a Pi-owned route holds its notes instead of generating steps.
    status += piOwned ? QStringLiteral("PI ENGINE")
                      : QStringLiteral("PICO 0x1A CHAIN (mask -> transpose -> chain)");
    if (noPico_ && !piOwned) {
      status += QStringLiteral(" - INERT (NO PICO; 0x18/0x19/0x1A not sent)");
    }
  }
  const auto pipes = currentPipes();
  bool clocked = false;
  int bypassed = 0;
  for (const auto& pipe : pipes) {
    if (pipe.bypass) ++bypassed;
    if (!pipe.bypass && pipeui::isClocked(static_cast<int>(pipe.type))) clocked = true;
  }
  if (clocked && piOwned) {
    status += QStringLiteral(" - CLOCKED PIPE: PI HAS NO 24-PPQN TICK");
  } else if (clocked && !(noPico_ && !piOwned) && !generator) {
    status += QStringLiteral(" - CLOCKED PIPE LIVE (Pico clockTick)");
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
  if (!pipeRouteEnabled()) status += QStringLiteral(" - ROUTE OFF");
  return status;
}

void RoutingViewModel::addPipe() {
  const int typeId = addTypeIndex_ + 1;
  if (!pipeui::isKnown(typeId)) return;
  const bool insert = insertSelected();
  if (insert &&
      mnky::midifx::generated::pipeIsGenerator(
          static_cast<std::uint8_t>(typeId))) {
    addHint_ = QStringLiteral("GENERATOR HEADS BELONG TO GENERATOR ROUTES");
    emit addTypeChanged();
    return;
  }
  const auto type = static_cast<session::MidiPipeType>(typeId);
  const bool generator = generatorSelected();
  bool added = false;
  if (insert) {
    added = midi_.addOutputInsertPipe(pipeOutput(), insertSlot_, type);
  } else if (generator) {
    added = midi_.addGeneratorPipe(pipeOutput(), type);
  } else {
    added = midi_.addRoutePipe(pipeInput(), pipeOutput(), type);
  }
  if (!added) {
    log(std::string("routing: pipe add refused for ") + pipeRouteText().toStdString() +
        (generator ? " (a generator chain's head must be a generator type)"
                   : (insert ? " (output insert chain full)" : "")));
    if (generator) {
      addHint_ = QStringLiteral(
          "GENERATOR HEAD MUST BE A GENERATOR TYPE (CC LFO) - PICK IT IN THE ADD LIST");
      emit addTypeChanged();
    } else if (insert) {
      addHint_ = QStringLiteral("OUTPUT INSERT CHAIN IS FULL (8 PIPES)");
      emit addTypeChanged();
    }
    return;
  }
  // A generator route has no source to gate; building its chain is the act
  // that puts it on the air, so the first ADD opens the route gate.
  if (generator && !pipeRouteEnabled()) midi_.setGeneratorRoute(pipeOutput(), true);
  if (insert && !pipeRouteEnabled()) {
    (void)midi_.setOutputInsertEnabled(pipeOutput(), insertSlot_, true);
  }
  if (!addHint_.isEmpty()) {
    addHint_.clear();
    emit addTypeChanged();
  }
  selectedPipe_ = pipeCount() - 1;
  log(std::string("routing: pipe added to ") + pipeRouteText().toStdString() + ": " +
      pipeui::pipeName(typeId).toStdString());
  refreshPipes();
}

void RoutingViewModel::removeSelectedPipe() {
  const bool removed =
      insertSelected()
          ? midi_.removeOutputInsertPipe(pipeOutput(), insertSlot_, selectedPipe_)
          : (generatorSelected()
                 ? midi_.removeGeneratorPipe(pipeOutput(), selectedPipe_)
                 : midi_.removeRoutePipe(pipeInput(), pipeOutput(), selectedPipe_));
  if (!removed) return;
  selectedPipe_ = std::max(0, std::min(selectedPipe_, pipeCount() - 1));
  refreshPipes();
}

void RoutingViewModel::moveSelectedPipe(int delta) {
  const bool moved =
      insertSelected()
          ? midi_.moveOutputInsertPipe(pipeOutput(), insertSlot_, selectedPipe_,
                                       delta)
          : (generatorSelected()
                 ? midi_.moveGeneratorPipe(pipeOutput(), selectedPipe_, delta)
                 : midi_.moveRoutePipe(pipeInput(), pipeOutput(), selectedPipe_,
                                       delta));
  if (!moved) return;
  selectedPipe_ = std::clamp(selectedPipe_ + (delta < 0 ? -1 : 1), 0,
                             std::max(0, pipeCount() - 1));
  refreshPipes();
}

void RoutingViewModel::movePipe(int index, int delta) {
  if (index < 0 || index >= pipeCount()) return;
  const bool moved =
      insertSelected()
          ? midi_.moveOutputInsertPipe(pipeOutput(), insertSlot_, index, delta)
          : (generatorSelected()
                 ? midi_.moveGeneratorPipe(pipeOutput(), index, delta)
                 : midi_.moveRoutePipe(pipeInput(), pipeOutput(), index, delta));
  if (moved) {
    selectedPipe_ = std::clamp(index + (delta < 0 ? -1 : 1), 0,
                               std::max(0, pipeCount() - 1));
  } else {
    selectedPipe_ = index;
  }
  refreshPipes();
}

void RoutingViewModel::removePipe(int index) {
  if (index < 0 || index >= pipeCount()) return;
  const bool removed =
      insertSelected()
          ? midi_.removeOutputInsertPipe(pipeOutput(), insertSlot_, index)
          : (generatorSelected()
                 ? midi_.removeGeneratorPipe(pipeOutput(), index)
                 : midi_.removeRoutePipe(pipeInput(), pipeOutput(), index));
  if (!removed) return;
  selectedPipe_ = std::max(0, std::min(index, pipeCount() - 1));
  refreshPipes();
}

void RoutingViewModel::toggleSelectedPipeBypass() {
  const auto pipes = currentPipes();
  if (selectedPipe_ < 0 || selectedPipe_ >= static_cast<int>(pipes.size())) return;
  setPipeBypass(selectedPipe_,
                !pipes[static_cast<std::size_t>(selectedPipe_)].bypass);
}

void RoutingViewModel::setPipeBypass(int index, bool bypass) {
  const bool stored =
      insertSelected()
          ? midi_.setOutputInsertPipeBypass(pipeOutput(), insertSlot_, index,
                                            bypass)
          : (generatorSelected()
                 ? midi_.setGeneratorPipeBypass(pipeOutput(), index, bypass)
                 : midi_.setRoutePipeBypass(pipeInput(), pipeOutput(), index,
                                            bypass));
  if (!stored) return;
  refreshPipes();
}

void RoutingViewModel::setPipeParam(int pipeIndex, int param, int value) {
  const bool stored =
      insertSelected()
          ? midi_.setOutputInsertPipeParam(pipeOutput(), insertSlot_, pipeIndex,
                                           param, value)
          : (generatorSelected()
                 ? midi_.setGeneratorPipeParam(pipeOutput(), pipeIndex, param,
                                               value)
                 : midi_.setRoutePipeParam(pipeInput(), pipeOutput(), pipeIndex,
                                           param, value));
  if (!stored) return;
  refreshPipes();
}

QString RoutingViewModel::pipeParamValueText(int param, int value) const {
  const auto pipes = currentPipes();
  if (selectedPipe_ < 0 || selectedPipe_ >= static_cast<int>(pipes.size())) return {};
  const int typeId =
      static_cast<int>(pipes[static_cast<std::size_t>(selectedPipe_)].type);
  return pipeui::valueText(typeId, param, value);
}

QString RoutingViewModel::defaultChainName() const {
  if (insertSelected()) {
    return QStringLiteral("chain-ins-%1-%2")
        .arg(midi::portId(pipeOutput()))
        .arg(insertSlot_ + 1);
  }
  return QStringLiteral("chain-%1-%2")
      .arg(generatorSelected() ? QStringLiteral("gen")
                               : QString::number(midi::portId(pipeInput())))
      .arg(midi::portId(pipeOutput()));
}

QString RoutingViewModel::exportPipeChain(const QString& target,
                                          const QString& projectDirectory) {
  std::filesystem::path file;
  const QString resolved = pipe_preset::resolvePath(target, projectDirectory, file);
  if (!resolved.isEmpty()) return resolved;
  const auto pipes = currentPipes();
  if (pipes.empty()) return QStringLiteral("the selected route has no pipes to export");
  pipe_preset::ChainFile chain;
  chain.name = QString::fromStdString(file.stem().string());
  chain.pipes = pipes;
  const QString error = pipe_preset::write(file, chain);
  if (!error.isEmpty()) return error;
  log(std::string("routing: exported ") + std::to_string(chain.pipes.size()) +
      " pipe(s) from " + pipeRouteText().toStdString() + " to " + file.string());
  return {};
}

QString RoutingViewModel::importPipeChain(const QString& target,
                                          const QString& projectDirectory) {
  std::filesystem::path file;
  const QString resolved = pipe_preset::resolvePath(target, projectDirectory, file);
  if (!resolved.isEmpty()) return resolved;
  pipe_preset::ChainFile chain;
  const QString error = pipe_preset::read(file, chain);
  if (!error.isEmpty()) return error;
  const bool stored =
      insertSelected()
          ? midi_.setOutputInsertPipes(pipeOutput(), insertSlot_, chain.pipes)
          : (generatorSelected()
                 ? midi_.setGeneratorPipes(pipeOutput(), chain.pipes)
                 : midi_.setRoutePipes(pipeInput(), pipeOutput(), chain.pipes));
  if (!stored) return QStringLiteral("the router refused the chain");
  selectedPipe_ = 0;
  log(std::string("routing: imported ") + std::to_string(chain.pipes.size()) +
      " pipe(s) into " + pipeRouteText().toStdString() + " from " + file.string());
  refreshPipes();
  return {};
}

int RoutingViewModel::addTypeIdAt(int index) const {
  const int count = static_cast<int>(mnky::midifx::generated::kPipeCount);
  const bool headOnly = generatorHeadPending();
  const bool modifiersOnly = insertSelected();
  if (!headOnly && !modifiersOnly) {
    return std::clamp(index, 0, count - 1) + 1;
  }
  // A source-less generator route may only start with a generator-role head;
  // an output insert is an ordinary modifier chain, so generator heads are not
  // offered there. Both lists map their visible index onto the full type set.
  int filtered = 0;
  for (int id = 1; id <= count; ++id) {
    const bool generator = pipeui::pipeRoleName(id) == QStringLiteral("GENERATOR");
    if (headOnly != generator) continue;
    if (filtered == index) return id;
    ++filtered;
  }
  for (int id = 1; id <= count; ++id) {
    const bool generator = pipeui::pipeRoleName(id) == QStringLiteral("GENERATOR");
    if (headOnly == generator) return id;
  }
  return 1;
}

int RoutingViewModel::addTypeIndexInList() const {
  const QStringList labels = addTypeLabels();
  for (int index = 0; index < labels.size(); ++index) {
    if (addTypeIdAt(index) == addTypeIndex_ + 1) return index;
  }
  return 0;
}

QStringList RoutingViewModel::addTypeLabels() const {
  QStringList labels;
  const int count = static_cast<int>(mnky::midifx::generated::kPipeCount);
  labels.reserve(count);
  const bool headOnly = generatorHeadPending();
  const bool modifiersOnly = insertSelected();
  for (int index = 0; index < count; ++index) {
    const int id = index + 1;
    const bool generator = pipeui::pipeRoleName(id) == QStringLiteral("GENERATOR");
    if (headOnly && !generator) continue;
    if (modifiersOnly && generator) continue;
    labels << pipeui::pipeName(id);
  }
  return labels;
}

void RoutingViewModel::refreshPipes() {
  const auto pipes = currentPipes();
  // A generator route with an empty chain may only add a generator-role head,
  // so the armed type follows the filtered list instead of a stale modifier.
  if (generatorSelected() && pipes.empty() &&
      pipeui::pipeRoleName(addTypeIndex_ + 1) != QStringLiteral("GENERATOR")) {
    addTypeIndex_ = addTypeIdAt(0) - 1;
    emit addTypeChanged();
  } else if (insertSelected() &&
             pipeui::pipeRoleName(addTypeIndex_ + 1) == QStringLiteral("GENERATOR")) {
    // An insert chain takes modifiers only; move the armed type off the
    // generator head instead of silently offering it.
    addTypeIndex_ = addTypeIdAt(0) - 1;
    emit addTypeChanged();
  }
  if (selectedPipe_ >= static_cast<int>(pipes.size())) {
    selectedPipe_ = pipes.empty() ? 0 : static_cast<int>(pipes.size()) - 1;
  }
  QVariantList rows;
  rows.reserve(static_cast<int>(pipes.size()));
  for (std::size_t index = 0; index < pipes.size(); ++index) {
    const auto& pipe = pipes[index];
    const int typeId = static_cast<int>(pipe.type);
    const bool clocked = pipeui::isClocked(typeId);
    QVariantMap row;
    row.insert(QStringLiteral("index"), static_cast<int>(index));
    row.insert(QStringLiteral("type"), typeId);
    row.insert(QStringLiteral("typeName"), pipeui::pipeName(typeId));
    row.insert(QStringLiteral("shortName"), pipeui::pipeShortName(typeId));
    row.insert(QStringLiteral("bypass"), pipe.bypass);
    row.insert(QStringLiteral("clocked"), clocked);
    row.insert(QStringLiteral("paramCount"), pipeui::paramCount(typeId));
    row.insert(QStringLiteral("selected"), static_cast<int>(index) == selectedPipe_);
    row.insert(QStringLiteral("chMask"), static_cast<int>(pipe.chMask));
    row.insert(QStringLiteral("scopeText"),
               QString::fromStdString(formatChannelMask(pipe.chMask)));
    const bool piOwned =
        insertSelected() || midi::piOwnsPipeChain(pipeSourceId(), pipeOutput());
    const bool generatorHead = generatorSelected() && !piOwned;
    QString rowStatus = QStringLiteral("ACTIVE");
    bool warning = false;
    if (pipe.bypass) {
      rowStatus = QStringLiteral("BYPASS");
    } else if (generatorHead) {
      rowStatus = QStringLiteral("NOT SENT (PICO-OWNED)");
      warning = true;
    } else if (noPico_ && !piOwned) {
      rowStatus = QStringLiteral("INERT (NO PICO)");
      warning = true;
    } else if (clocked) {
      if (piOwned) {
        rowStatus = QStringLiteral("NO PI CLOCK TICK");
        warning = true;
      } else {
        rowStatus = QStringLiteral("ACTIVE (PICO CLOCK)");
      }
    }
    if (pipe.chMask != 0xFFFF) {
      rowStatus += QStringLiteral(" ") +
                   QString::fromStdString(formatChannelMask(pipe.chMask));
    }
    row.insert(QStringLiteral("status"), rowStatus);
    row.insert(QStringLiteral("warning"), warning);
    row.insert(QStringLiteral("inert"),
               (generatorHead || (noPico_ && !piOwned)) && !pipe.bypass);
    rows.push_back(row);
  }

  // Every positional parameter of the selected pipe: the screen pages nothing,
  // it shows the whole schema and scrolls when the cards do not fit.
  QVariantList params;
  if (selectedPipe_ >= 0 && selectedPipe_ < static_cast<int>(pipes.size())) {
    const auto& pipe = pipes[static_cast<std::size_t>(selectedPipe_)];
    const int typeId = static_cast<int>(pipe.type);
    params = pipeui::editorRows(typeId, pipe.params, 0, pipeui::paramCount(typeId));
  }

  refreshInsertRows();
  const QString routeText = pipeRouteText();
  const QString statusText = pipeStatusText();
  if (rows == pipeRows_ && params == pipeParams_ && routeText == pipeRouteText_ &&
      statusText == pipeStatusText_ && insertHint_ == pipeInsertHint_) {
    return;
  }
  pipeRows_ = std::move(rows);
  pipeParams_ = std::move(params);
  pipeInsertHint_ = insertHint_;
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
    if (node.port == midi::kSrcGenerator) {
      // A generator has no input channel to filter or transpose; keep the
      // arrays index-aligned with the node list but inert.
      masks.push_back(0xFFFF);
      transposes.push_back(0);
      continue;
    }
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
  // The filter page lists every real input source, never the generator
  // pseudo-source (it has no input channel to filter).
  const QVariantList filterable = filterTabRows();
  filterPage_ = std::clamp(filterPage_, 0, filterPageCount() - 1);
  QVariantList rows;
  const int first = filterPage_ * kFilterRowsPerPage;
  const int last = std::min(static_cast<int>(filterable.size()),
                            first + kFilterRowsPerPage);
  for (int index = first; index < last; ++index) {
    rows.push_back(filterable[static_cast<std::size_t>(index)]);
  }
  if (rows != filterPageRows_) {
    filterPageRows_ = std::move(rows);
    emit filtersChanged();
  }
}

}  // namespace mnky::app
