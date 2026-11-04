#include "routing_view_model.hpp"

#include <algorithm>

#include <QStringList>
#include <QVariantMap>

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

void RoutingViewModel::setShiftPage(int page) {
  const int clamped = std::clamp(page, 0, 1);
  if (clamped == shiftPage_) return;
  shiftPage_ = clamped;
  emit shiftPageChanged();
}

void RoutingViewModel::setPipeSource(int source) {
  const int clamped = std::clamp(source, 0, routing::kInputCount - 1);
  if (clamped == pipeSource_) return;
  pipeSource_ = clamped;
  selectedPipe_ = 0;
  paramPage_ = 0;
  emit pipeRouteChanged();
  refreshPipes();
}

void RoutingViewModel::setPipeDestination(int destination) {
  const int clamped = std::clamp(destination, 0, routing::kOutputCount - 1);
  if (clamped == pipeDestination_) return;
  pipeDestination_ = clamped;
  selectedPipe_ = 0;
  paramPage_ = 0;
  emit pipeRouteChanged();
  refreshPipes();
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
  return routing::kInputs[static_cast<std::size_t>(pipeSource_)];
}

midi::Port RoutingViewModel::pipeOutput() const {
  return routing::kOutputs[static_cast<std::size_t>(pipeDestination_)];
}

int RoutingViewModel::pipeCount() const {
  return static_cast<int>(midi_.routePipes(pipeInput(), pipeOutput()).size());
}

QString RoutingViewModel::pipeRouteText() const {
  return QString::fromLatin1(routing::kInputNames[static_cast<std::size_t>(pipeSource_)]) +
         QStringLiteral(" -> ") +
         QString::fromLatin1(routing::kOutputNames[static_cast<std::size_t>(pipeDestination_)]);
}

QString RoutingViewModel::pipeStatusText() const {
  const auto route = pipeRouteText();
  const int count = pipeCount();
  if (count == 0) return route + QStringLiteral(" - NO PIPES");
  const bool piOwned = pipeSource_ == 2 || pipeDestination_ == 2;
  QString status = route + QStringLiteral(" - ") + QString::number(count) +
                   (count == 1 ? QStringLiteral(" PIPE - ") : QStringLiteral(" PIPES - "));
  // Engine split is explicit: a route touching USB is evaluated by the Pi
  // (Wave 1 only; midifx process() rejects the clocked ids); a TRS->TRS chain
  // is configured on the Pico (0x18/0x19) and its bytes go out with 0x1A, where
  // the firmware evaluates both waves. The Pi applies the channel mask and its
  // source transpose before the 0x1A send, so that route's effective order is
  // mask -> transpose -> Pico chain.
  status += piOwned ? QStringLiteral("PI ENGINE (WAVE 1)")
                    : QStringLiteral("PICO 0x1A CHAIN (mask -> transpose -> chain)");
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
  } else if (wave2) {
    status += QStringLiteral(" - WAVE 2 LIVE (Pico clocked chain)");
  }
  if (bypassed > 0) status += QStringLiteral(" - ") + QString::number(bypassed) +
                             QStringLiteral(" BYPASSED");
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
  if (!midi_.setRoutePipeParam(pipeInput(), pipeOutput(), pipeIndex, param, value)) return;
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
    const bool piOwned = pipeSource_ == 2 || pipeDestination_ == 2;
    QString rowStatus = QStringLiteral("ACTIVE");
    if (pipe.bypass) {
      rowStatus = QStringLiteral("BYPASS");
    } else if (spec != nullptr && spec->clocked) {
      rowStatus = piOwned ? QStringLiteral("PI: NO WAVE-2 ENGINE")
                          : QStringLiteral("ACTIVE (PICO CLOCK)");
    }
    row.insert(QStringLiteral("status"), rowStatus);
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
