#pragma once
// QML view model for the per-track FX rack popup (TRACK FX): two slot cards,
// FX1 then FX2, bound to the mixer's rack on the track the popup opened on.
//
// The rack lives in MixerModel (fxSlots[2]) and reaches the engine through
// AudioGlue's control-thread forwarder, so this class formats values and
// forwards edits only. A CLAP slot's parameter cards and note come from
// MixerViewModel (plugin discovery + FxManager), so this popup is the same
// schema-driven card list for built-ins and plugins; the old global FX-tab CLAP
// slot view models retired with the tab.
#include <QObject>
#include <QString>
#include <QVariantList>

#include "mixer_model.hpp"
#include "mixer_view_model.hpp"

namespace mnky::app {

class TrackFxViewModel;

// One slot card of the popup. Its values are read through the owner, which
// keeps a single track/rack binding for both cards.
class FxSlotCard final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString title READ title CONSTANT)
  Q_PROPERTY(QString effectName READ effectName NOTIFY changed)
  Q_PROPERTY(int choice READ choice NOTIFY changed)
  Q_PROPERTY(bool bypass READ bypass NOTIFY changed)
  Q_PROPERTY(double wet READ wet NOTIFY changed)
  Q_PROPERTY(int paramCount READ paramCount NOTIFY changed)
  Q_PROPERTY(QVariantList params READ params NOTIFY changed)
  Q_PROPERTY(QString note READ note NOTIFY changed)
  Q_PROPERTY(bool builtin READ builtin NOTIFY changed)

 public:
  FxSlotCard(TrackFxViewModel& owner, int slot);

  QString title() const;
  QString effectName() const;
  int choice() const;
  bool bypass() const;
  double wet() const;
  int paramCount() const;
  QVariantList params() const;
  QString note() const;
  bool builtin() const;
  int slot() const { return slot_; }
  // Re-emits changed(): the owner calls it after a rack edit or a track switch.
  void refresh() { emit changed(); }

 signals:
  void changed();

 private:
  TrackFxViewModel& owner_;
  const int slot_;
};

class TrackFxViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool open READ open NOTIFY openChanged)
  Q_PROPERTY(int track READ track NOTIFY trackChanged)
  Q_PROPERTY(int focusSlot READ focusSlot NOTIFY openChanged)
  Q_PROPERTY(QString title READ title NOTIFY trackChanged)
  Q_PROPERTY(QString subline READ subline NOTIFY trackChanged)
  Q_PROPERTY(QString footer READ footer NOTIFY trackChanged)
  Q_PROPERTY(FxSlotCard* fx1 READ fx1 CONSTANT)
  Q_PROPERTY(FxSlotCard* fx2 READ fx2 CONSTANT)

 public:
  TrackFxViewModel(MixerModel& model, MixerViewModel& mixer,
                   QObject* parent = nullptr);

  bool open() const { return open_; }
  int track() const;
  int focusSlot() const { return focusSlot_; }
  QString title() const;
  QString subline() const;
  QString footer() const;
  FxSlotCard* fx1() { return &fx1_; }
  FxSlotCard* fx2() { return &fx2_; }

  // Opens on `track` (its slot is selected in the mixer, so the channel popout
  // and the mixer agree which strip is being configured) and highlights the
  // slot the caller came from.
  Q_INVOKABLE void openFor(int track, int slot);
  Q_INVOKABLE void close();
  // Re-reads both cards (mixer rack edits, session load, track switch).
  void refresh();

  Q_INVOKABLE void setChoice(int slot, int choice);
  Q_INVOKABLE void setWet(int slot, double wet);
  Q_INVOKABLE void toggleBypass(int slot);
  Q_INVOKABLE void setParam(int slot, int index, double value);

  // Card reads.
  QString effectNameAt(int slot) const;
  int choiceAt(int slot) const;
  bool bypassAt(int slot) const;
  double wetAt(int slot) const;
  QVariantList paramsAt(int slot) const;
  QString noteAt(int slot) const;
  bool builtinAt(int slot) const;

 signals:
  void openChanged();
  void trackChanged();

 private:
  MixerModel& model_;
  MixerViewModel& mixer_;
  FxSlotCard fx1_;
  FxSlotCard fx2_;
  bool open_ = false;
  int focusSlot_ = 0;
};

}  // namespace mnky::app
