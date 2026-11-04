#pragma once
// QML model for the Routing screen: the 3x3 patchbay matrix, per-source channel
// masks and transpose, and the pure geometry/hit-test helper the QML Canvas
// uses for nodes and wires.
#include <QObject>
#include <QVariantList>

#include <cstdint>
#include <vector>

#include "midi_glue.hpp"
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

 private:
  void refreshRoutes();
  void refreshFilters();

  MidiGlue& midi_;
  QVariantList routes_;
  QVariantList channelMasks_;
  QVariantList transposes_;
  int selectedInput_ = -1;
  int usbInputs_ = 0;
  int usbOutputs_ = 0;
  std::uint32_t routeSignature_ = 0;
};

}  // namespace mnky::app
