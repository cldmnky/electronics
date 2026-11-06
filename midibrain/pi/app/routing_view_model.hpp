#pragma once
// QML model for the Routing screen: the node/wire patchbay over the fixed
// TRS/merged-USB ports plus every dynamically discovered USB MIDI device
// (midi::PortRegistry ids 6..31, exposed through MidiGlue::usbPorts()), the
// per-source channel masks and transpose, the channel-filter page and the MIDI
// FX pipe chains of any route.
#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>
#include <vector>

#include "midi_glue.hpp"
#include "routing_model.hpp"

namespace mnky::app {

// One routable patchbay endpoint. Port ids 0..5 keep the firmware ABI; a
// dynamic USB device keeps its stable registry id (6..31), so a route and its
// pipe chain survive re-enumeration by identity.
struct RoutingNode {
  int port = 0;
  QString name;
  QString shortName;
  bool dynamic = false;
  bool connected = true;
  bool input = false;
};

// Builds the ordered node rows from the registry snapshot. Fixed inputs are
// TRS IN 1, TRS IN 2 and the merged USB IN; fixed outputs are TRS OUT 1,
// TRS OUT 2 and the merged USB OUT. A device offering both directions appears
// once on each side with the same name and its own registry id.
std::vector<RoutingNode> routingInputNodes(
    const std::vector<MidiGlue::UsbPortInfo>& ports);
std::vector<RoutingNode> routingOutputNodes(
    const std::vector<MidiGlue::UsbPortInfo>& ports);

class RoutingViewModel final : public QObject {
  Q_OBJECT
  // One entry per input/output node: {port, name, shortName, dynamic,
  // connected, input, nodeIndex}. `nodeIndex` is the index in this list and is
  // what routes/channel masks/pipe selections are keyed by.
  Q_PROPERTY(QVariantList inputNodes READ inputNodes NOTIFY devicesChanged)
  Q_PROPERTY(QVariantList outputNodes READ outputNodes NOTIFY devicesChanged)
  Q_PROPERTY(QVariantList filterPageRows READ filterPageRows NOTIFY filtersChanged)
  Q_PROPERTY(QVariantList routes READ routes NOTIFY routesChanged)
  Q_PROPERTY(QVariantList channelMasks READ channelMasks NOTIFY filtersChanged)
  Q_PROPERTY(QVariantList transposes READ transposes NOTIFY filtersChanged)
  Q_PROPERTY(int selectedInput READ selectedInput WRITE setSelectedInput NOTIFY selectedInputChanged)
  Q_PROPERTY(int usbInputs READ usbInputs NOTIFY devicesChanged)
  Q_PROPERTY(int usbOutputs READ usbOutputs NOTIFY devicesChanged)
  Q_PROPERTY(int inputCount READ inputCount NOTIFY devicesChanged)
  Q_PROPERTY(int outputCount READ outputCount NOTIFY devicesChanged)
  Q_PROPERTY(double nodeWidthFraction READ nodeWidthFraction CONSTANT)
  Q_PROPERTY(double wireHandleFraction READ wireHandleFraction CONSTANT)
  Q_PROPERTY(int filterPage READ filterPage WRITE setFilterPage NOTIFY filtersChanged)
  Q_PROPERTY(int filterPageCount READ filterPageCount NOTIFY filtersChanged)
  // Routing SHIFT face page: 0 = channel filter/transpose/clock, 1 = MIDI FX
  // pipe chains (per-route ordered lists).
  Q_PROPERTY(int shiftPage READ shiftPage WRITE setShiftPage NOTIFY shiftPageChanged)
  Q_PROPERTY(int pipeSource READ pipeSource WRITE setPipeSource NOTIFY pipeRouteChanged)
  Q_PROPERTY(int pipeDestination READ pipeDestination WRITE setPipeDestination NOTIFY pipeRouteChanged)
  Q_PROPERTY(QString pipeSourceName READ pipeSourceName NOTIFY pipeRouteChanged)
  Q_PROPERTY(QString pipeDestinationName READ pipeDestinationName NOTIFY pipeRouteChanged)
  Q_PROPERTY(int selectedPipe READ selectedPipe WRITE selectPipe NOTIFY pipesChanged)
  Q_PROPERTY(int addTypeIndex READ addTypeIndex WRITE setAddTypeIndex NOTIFY addTypeChanged)
  Q_PROPERTY(QString addTypeName READ addTypeName NOTIFY addTypeChanged)
  Q_PROPERTY(int pipeCount READ pipeCount NOTIFY pipesChanged)
  Q_PROPERTY(QVariantList pipeRows READ pipeRows NOTIFY pipesChanged)
  Q_PROPERTY(QVariantList pipeParams READ pipeParams NOTIFY pipesChanged)
  Q_PROPERTY(int paramPage READ paramPage NOTIFY pipesChanged)
  Q_PROPERTY(int paramPageCount READ paramPageCount NOTIFY pipesChanged)
  Q_PROPERTY(int paramsPerPage READ paramsPerPage CONSTANT)
  Q_PROPERTY(QString pipeRouteText READ pipeRouteText NOTIFY pipeRouteChanged)
  Q_PROPERTY(QString pipeStatusText READ pipeStatusText NOTIFY pipesChanged)
  Q_PROPERTY(QString selectedPipeScopeText READ selectedPipeScopeText NOTIFY pipesChanged)
  // No-Pico mode: no Pico, so TRS nodes and TRS->TRS pipe chains are inert. The
  // matrix still edits (it is project data) but the UI labels the rows
  // unavailable instead of implying a wire. Fixed for the process lifetime.
  Q_PROPERTY(bool noPicoMode READ noPicoMode CONSTANT)

 public:
  explicit RoutingViewModel(MidiGlue& midi, bool noPico, QObject* parent = nullptr);

  QVariantList inputNodes() const { return inputNodes_; }
  QVariantList outputNodes() const { return outputNodes_; }
  QVariantList filterPageRows() const { return filterPageRows_; }
  QVariantList routes() const { return routes_; }
  QVariantList channelMasks() const { return channelMasks_; }
  QVariantList transposes() const { return transposes_; }
  int selectedInput() const { return selectedInput_; }
  void setSelectedInput(int source);
  int usbInputs() const { return usbInputs_; }
  int usbOutputs() const { return usbOutputs_; }
  int inputCount() const { return static_cast<int>(inputs_.size()); }
  int outputCount() const { return static_cast<int>(outputs_.size()); }
  double nodeWidthFraction() const { return routing::kNodeWidthFraction; }
  double wireHandleFraction() const { return routing::kWireHandleFraction; }
  int filterPage() const { return filterPage_; }
  Q_INVOKABLE void setFilterPage(int page);
  int filterPageCount() const;

  int shiftPage() const { return shiftPage_; }
  Q_INVOKABLE void setShiftPage(int page);
  int pipeSource() const { return pipeSource_; }
  void setPipeSource(int source);
  int pipeDestination() const { return pipeDestination_; }
  void setPipeDestination(int destination);
  Q_INVOKABLE void cyclePipeSource(int delta);
  Q_INVOKABLE void cyclePipeDestination(int delta);
  QString pipeSourceName() const;
  QString pipeDestinationName() const;
  int selectedPipe() const { return selectedPipe_; }
  Q_INVOKABLE void selectPipe(int index);
  int addTypeIndex() const { return addTypeIndex_; }
  void setAddTypeIndex(int index);
  QString addTypeName() const;
  int pipeCount() const;
  QVariantList pipeRows() const { return pipeRows_; }
  QVariantList pipeParams() const { return pipeParams_; }
  int paramPage() const { return paramPage_; }
  int paramPageCount() const;
  int paramsPerPage() const { return kPipeParamsPerPage; }
  QString pipeRouteText() const;
  QString pipeStatusText() const;
  QString selectedPipeScopeText() const;
  bool noPicoMode() const { return noPico_; }
  // A TRS port has no receiver/sender in no-Pico mode. USB stays available.
  Q_INVOKABLE bool inputAvailable(int source) const;
  Q_INVOKABLE bool outputAvailable(int destination) const;

  // Pipe chain edits (Routing SHIFT > PIPES). Each one writes the MidiGlue
  // store, which keeps the Router chain in sync and is what project_io captures.
  Q_INVOKABLE void addPipe();
  Q_INVOKABLE void removeSelectedPipe();
  Q_INVOKABLE void moveSelectedPipe(int delta);
  // One-call row actions for QML: select and move/remove in a single model
  // refresh. Calling selectPipe() and then a second method from a ListView
  // delegate destroys the delegate mid-handler and invalidates its context, so
  // the row buttons use these instead.
  Q_INVOKABLE void movePipe(int index, int delta);
  Q_INVOKABLE void removePipe(int index);
  Q_INVOKABLE void toggleSelectedPipeBypass();
  Q_INVOKABLE void setPipeBypass(int index, bool bypass);
  Q_INVOKABLE void setPipeParam(int pipeIndex, int param, int value);
  // Value text of one positional parameter of the selected pipe with its schema
  // unit ("50%", "250 ms", "+4 st", "C4"); the knob editor's value popup.
  Q_INVOKABLE QString pipeParamValueText(int param, int value) const;
  // Chain preset files (phase 4): the selected route's ordered chain as one
  // JSON file. A value without '/' resolves against projectDirectory (a bare
  // preset name); the .json extension is added. Empty result = success, else a
  // one-line reason the keypad shows without closing.
  Q_INVOKABLE QString exportPipeChain(const QString& target,
                                      const QString& projectDirectory);
  Q_INVOKABLE QString importPipeChain(const QString& target,
                                      const QString& projectDirectory);
  // Default export name for the selected route, e.g. "chain-1-3".
  Q_INVOKABLE QString defaultChainName() const;
  Q_INVOKABLE void cycleAddType(int delta);
  Q_INVOKABLE void cycleSelectedPipe(int delta);
  Q_INVOKABLE void setParamPage(int page);
  // Per-pipe channel scope (session::MidiPipe::chMask) of the selected pipe.
  // 0xFFFF = all channels; the engine only processes matching channels and
  // passes the rest through that pipe unchanged.
  Q_INVOKABLE int selectedPipeChannelMask() const;
  Q_INVOKABLE void setSelectedPipeChannelMask(int mask);

  Q_INVOKABLE bool routeEnabled(int source, int destination) const;
  Q_INVOKABLE void toggleRoute(int source, int destination);
  Q_INVOKABLE void selectInput(int source);
  Q_INVOKABLE void selectOutput(int destination);
  // Panel-focus path: the focus id carries the stable port id, not the node
  // index (a device list can change while the ring keeps its stop).
  Q_INVOKABLE void selectInputPort(int port);
  Q_INVOKABLE void selectOutputPort(int port);
  Q_INVOKABLE void setMaskBit(int source, int channel, bool enabled);
  Q_INVOKABLE void setTranspose(int source, int semitones);
  Q_INVOKABLE void adjustTranspose(int source, int delta);
  Q_INVOKABLE void reset();
  // -1 when the tap misses every wire, otherwise source * outputCount + dest.
  Q_INVOKABLE int hitWireTest(double x, double y, double width, double height) const;
  Q_INVOKABLE int hitInputNode(double x, double y, double width, double height) const;
  Q_INVOKABLE int hitOutputNode(double x, double y, double width, double height) const;

  void refresh();

 signals:
  void routesChanged();
  void filtersChanged();
  void selectedInputChanged();
  void devicesChanged();
  void shiftPageChanged();
  void pipeRouteChanged();
  void pipesChanged();
  void addTypeChanged();

 private:
  void rebuildNodes();
  void refreshRoutes();
  void refreshFilters();
  void refreshPipes();

  midi::Port portForInput(int node) const;
  midi::Port portForOutput(int node) const;
  int inputNodeForPort(int port) const;
  int outputNodeForPort(int port) const;
  midi::Port pipeInput() const;
  midi::Port pipeOutput() const;

  MidiGlue& midi_;
  const bool noPico_;
  std::vector<RoutingNode> inputs_;
  std::vector<RoutingNode> outputs_;
  QVariantList inputNodes_;
  QVariantList outputNodes_;
  QVariantList filterPageRows_;
  QVariantList routes_;
  QVariantList channelMasks_;
  QVariantList transposes_;
  int selectedInput_ = -1;
  int usbInputs_ = 0;
  int usbOutputs_ = 0;
  int filterPage_ = 0;
  int shiftPage_ = 0;
  int pipeSource_ = 0;
  int pipeDestination_ = 2;  // default: the documented TRS IN1 -> USB OUT hub route
  int selectedPipe_ = 0;
  int addTypeIndex_ = 0;
  static constexpr int kPipeParamsPerPage = 2;
  static constexpr int kFilterRowsPerPage = 3;
  int paramPage_ = 0;
  QVariantList pipeRows_;
  QVariantList pipeParams_;
  QString pipeRouteText_;
  QString pipeStatusText_;
};

}  // namespace mnky::app
