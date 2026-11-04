#pragma once
// QML model for the Routing screen: the 3x3 patchbay matrix, per-source channel
// masks and transpose, and the pure geometry/hit-test helper the QML Canvas
// uses for nodes and wires.
#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>
#include <vector>

#include "midi_glue.hpp"
#include "pipe_params.hpp"
#include "routing_model.hpp"

namespace mnky::app {

class RoutingViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList routes READ routes NOTIFY routesChanged)
  Q_PROPERTY(QVariantList channelMasks READ channelMasks NOTIFY filtersChanged)
  Q_PROPERTY(QVariantList transposes READ transposes NOTIFY filtersChanged)
  Q_PROPERTY(int selectedInput READ selectedInput WRITE setSelectedInput NOTIFY selectedInputChanged)
  Q_PROPERTY(int usbInputs READ usbInputs NOTIFY devicesChanged)
  Q_PROPERTY(int usbOutputs READ usbOutputs NOTIFY devicesChanged)
  Q_PROPERTY(int inputCount READ inputCount CONSTANT)
  Q_PROPERTY(int outputCount READ outputCount CONSTANT)
  Q_PROPERTY(double nodeWidthFraction READ nodeWidthFraction CONSTANT)
  Q_PROPERTY(double nodeHeightFraction READ nodeHeightFraction CONSTANT)
  Q_PROPERTY(double wireHandleFraction READ wireHandleFraction CONSTANT)
  // Routing SHIFT face page: 0 = channel filter/transpose/clock, 1 = MIDI FX
  // pipe chains (per-route ordered lists).
  Q_PROPERTY(int shiftPage READ shiftPage WRITE setShiftPage NOTIFY shiftPageChanged)
  Q_PROPERTY(int pipeSource READ pipeSource WRITE setPipeSource NOTIFY pipeRouteChanged)
  Q_PROPERTY(int pipeDestination READ pipeDestination WRITE setPipeDestination NOTIFY pipeRouteChanged)
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

 public:
  explicit RoutingViewModel(MidiGlue& midi, QObject* parent = nullptr);

  QVariantList routes() const { return routes_; }
  QVariantList channelMasks() const { return channelMasks_; }
  QVariantList transposes() const { return transposes_; }
  int selectedInput() const { return selectedInput_; }
  void setSelectedInput(int source);
  int usbInputs() const { return usbInputs_; }
  int usbOutputs() const { return usbOutputs_; }
  int inputCount() const { return routing::kInputCount; }
  int outputCount() const { return routing::kOutputCount; }
  double nodeWidthFraction() const { return routing::kNodeWidthFraction; }
  double nodeHeightFraction() const { return routing::kNodeHeightFraction; }
  double wireHandleFraction() const { return routing::kWireHandleFraction; }

  int shiftPage() const { return shiftPage_; }
  void setShiftPage(int page);
  int pipeSource() const { return pipeSource_; }
  void setPipeSource(int source);
  int pipeDestination() const { return pipeDestination_; }
  void setPipeDestination(int destination);
  int selectedPipe() const { return selectedPipe_; }
  void selectPipe(int index);
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

  // Pipe chain edits (Routing SHIFT > PIPES). Each one writes the MidiGlue
  // store, which keeps the Router chain in sync and is what project_io captures.
  Q_INVOKABLE void addPipe();
  Q_INVOKABLE void removeSelectedPipe();
  Q_INVOKABLE void moveSelectedPipe(int delta);
  Q_INVOKABLE void toggleSelectedPipeBypass();
  Q_INVOKABLE void setPipeBypass(int index, bool bypass);
  Q_INVOKABLE void setPipeParam(int pipeIndex, int param, int value);
  Q_INVOKABLE void cycleAddType(int delta);
  Q_INVOKABLE void cycleSelectedPipe(int delta);
  Q_INVOKABLE void setParamPage(int page);

  Q_INVOKABLE bool routeEnabled(int source, int destination) const;
  Q_INVOKABLE void toggleRoute(int source, int destination);
  Q_INVOKABLE void selectInput(int source);
  Q_INVOKABLE void selectOutput(int destination);
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
  void refreshRoutes();
  void refreshFilters();
  void refreshPipes();

  midi::Port pipeInput() const;
  midi::Port pipeOutput() const;

  MidiGlue& midi_;
  QVariantList routes_;
  QVariantList channelMasks_;
  QVariantList transposes_;
  int selectedInput_ = -1;
  int usbInputs_ = 0;
  int usbOutputs_ = 0;
  std::uint32_t routeSignature_ = 0;
  int shiftPage_ = 0;
  int pipeSource_ = 0;
  int pipeDestination_ = 2;  // default: the documented TRS IN1 -> USB OUT hub route
  int selectedPipe_ = 0;
  int addTypeIndex_ = 0;
  static constexpr int kPipeParamsPerPage = 2;
  int paramPage_ = 0;
  QVariantList pipeRows_;
  QVariantList pipeParams_;
  QString pipeRouteText_;
  QString pipeStatusText_;
};

}  // namespace mnky::app
