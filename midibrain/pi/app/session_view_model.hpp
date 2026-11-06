#pragma once
// QML view model for the app-owned session-v2 settings: punch markers,
// metronome configuration, monitor/cue routing, send pre/post sources, the
// main-bus compressor and the device output channel pairs. Every setter writes
// the same field project_io saves in the session v2 sections, so the UI and the
// project file cannot disagree.
#include <QObject>
#include <QStringList>

#include "app_state.hpp"
#include "link_client.hpp"
#include "mixer_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

// The device output pickers offer adjacent pairs over the channel range the
// session format accepts (deviceOut clamps to 0..31), so 16 pairs: 1&2 .. 31&32.
inline constexpr int kDeviceOutPairs = 16;

class SessionViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool punchPlayFromEnabled READ punchPlayFromEnabled WRITE setPunchPlayFromEnabled NOTIFY punchChanged)
  Q_PROPERTY(int punchPlayFromMeasure READ punchPlayFromMeasure WRITE setPunchPlayFromMeasure NOTIFY punchChanged)
  Q_PROPERTY(bool punchInEnabled READ punchInEnabled WRITE setPunchInEnabled NOTIFY punchChanged)
  Q_PROPERTY(int punchInMeasure READ punchInMeasure WRITE setPunchInMeasure NOTIFY punchChanged)
  Q_PROPERTY(bool punchOutEnabled READ punchOutEnabled WRITE setPunchOutEnabled NOTIFY punchChanged)
  Q_PROPERTY(int punchOutMeasure READ punchOutMeasure WRITE setPunchOutMeasure NOTIFY punchChanged)
  Q_PROPERTY(int clickMode READ clickMode WRITE setClickMode NOTIFY clickChanged)
  Q_PROPERTY(int clickOutput READ clickOutput WRITE setClickOutput NOTIFY clickChanged)
  Q_PROPERTY(double clickGainDb READ clickGainDb WRITE setClickGainDb NOTIFY clickChanged)
  Q_PROPERTY(int phonesSrc READ phonesSrc WRITE setPhonesSrc NOTIFY monitorChanged)
  Q_PROPERTY(int cueStyle READ cueStyle WRITE setCueStyle NOTIFY monitorChanged)
  Q_PROPERTY(int out34Mode READ out34Mode WRITE setOut34Mode NOTIFY monitorChanged)
  Q_PROPERTY(int sendCue READ sendCue WRITE setSendCue NOTIFY sendsChanged)
  Q_PROPERTY(int sendFx1 READ sendFx1 WRITE setSendFx1 NOTIFY sendsChanged)
  Q_PROPERTY(int sendFx2 READ sendFx2 WRITE setSendFx2 NOTIFY sendsChanged)
  Q_PROPERTY(int sendOut34 READ sendOut34 WRITE setSendOut34 NOTIFY sendsChanged)
  Q_PROPERTY(int sendRec READ sendRec WRITE setSendRec NOTIFY sendsChanged)
  Q_PROPERTY(bool compOn READ compOn WRITE setCompOn NOTIFY compChanged)
  Q_PROPERTY(double compThreshDb READ compThreshDb WRITE setCompThreshDb NOTIFY compChanged)
  Q_PROPERTY(double compRatio READ compRatio WRITE setCompRatio NOTIFY compChanged)
  Q_PROPERTY(double compAttackMs READ compAttackMs WRITE setCompAttackMs NOTIFY compChanged)
  Q_PROPERTY(double compReleaseMs READ compReleaseMs WRITE setCompReleaseMs NOTIFY compChanged)
  Q_PROPERTY(double compMakeupDb READ compMakeupDb WRITE setCompMakeupDb NOTIFY compChanged)
  Q_PROPERTY(int compMode READ compMode WRITE setCompMode NOTIFY compChanged)
  Q_PROPERTY(bool trs1Clock READ trs1Clock WRITE setTrs1Clock NOTIFY clockChanged)
  Q_PROPERTY(bool trs1Transport READ trs1Transport WRITE setTrs1Transport NOTIFY clockChanged)
  Q_PROPERTY(bool trs2Clock READ trs2Clock WRITE setTrs2Clock NOTIFY clockChanged)
  Q_PROPERTY(bool trs2Transport READ trs2Transport WRITE setTrs2Transport NOTIFY clockChanged)
  Q_PROPERTY(bool usbClock READ usbClock WRITE setUsbClock NOTIFY clockChanged)
  Q_PROPERTY(bool usbTransport READ usbTransport WRITE setUsbTransport NOTIFY clockChanged)
  Q_PROPERTY(QString clockText READ clockText NOTIFY clockChanged)
  // D8: the clock-source choice on SYSTEM SHIFT > OUTPUT. 0 internal, 1 follow
  // TRS IN1, 2 follow USB. USB is not a clock source in this engine (the Pico
  // has no USB-device view), so choice 2 sets transportFollow[2] only and
  // leaves the generator internal; the row says so.
  Q_PROPERTY(int clockSourceChoice READ clockSourceChoice WRITE setClockSourceChoice NOTIFY clockChanged)
  Q_PROPERTY(QString clockSourceText READ clockSourceText NOTIFY clockChanged)
  Q_PROPERTY(QString clockSourceNote READ clockSourceNote CONSTANT)
  // Device output routing (session v2 deviceOut): the pair index of the device
  // channels that carry Main 1&2, OUT3&4 and the phones bus. A pair index p is
  // the device channels 2p and 2p+1 (0-based), so 0 = 1&2, 1 = 3&4, 2 = 5&6.
  // audio::Engine::setDeviceOut() takes the map; the missing piece is the
  // app-side handoff (AudioGlue has no applyDeviceOut()), so the pickers write
  // session state that is saved but not yet applied per block.
  Q_PROPERTY(QStringList deviceOutPairLabels READ deviceOutPairLabels CONSTANT)
  Q_PROPERTY(int deviceOutMain READ deviceOutMain WRITE setDeviceOutMain NOTIFY deviceOutChanged)
  Q_PROPERTY(int deviceOutOut34 READ deviceOutOut34 WRITE setDeviceOutOut34 NOTIFY deviceOutChanged)
  Q_PROPERTY(int deviceOutPhones READ deviceOutPhones WRITE setDeviceOutPhones NOTIFY deviceOutChanged)

 public:
  SessionViewModel(AppState& state, MixerModel& mixer, LinkClient& link,
                   SessionSettings& settings, QObject* parent = nullptr);

  bool punchPlayFromEnabled() const { return settings_.punch.playFrom.enabled; }
  int punchPlayFromMeasure() const { return settings_.punch.playFrom.measure; }
  bool punchInEnabled() const { return settings_.punch.in.enabled; }
  int punchInMeasure() const { return settings_.punch.in.measure; }
  bool punchOutEnabled() const { return settings_.punch.out.enabled; }
  int punchOutMeasure() const { return settings_.punch.out.measure; }
  int clickMode() const { return state_.clickMode.load(); }
  int clickOutput() const { return state_.clickOutput.load(); }
  double clickGainDb() const { return state_.clickGainDb.load(); }
  int phonesSrc() const { return settings_.cue.phonesSrc; }
  int cueStyle() const { return settings_.cue.style; }
  int out34Mode() const { return mixer_.out34Mode(); }
  int sendCue() const { return settings_.sends.cue; }
  int sendFx1() const { return settings_.sends.fx1; }
  int sendFx2() const { return settings_.sends.fx2; }
  int sendOut34() const { return settings_.sends.out34; }
  int sendRec() const { return settings_.sends.rec; }
  bool compOn() const { return settings_.mainComp.on; }
  double compThreshDb() const { return settings_.mainComp.threshDb; }
  double compRatio() const { return settings_.mainComp.ratio; }
  double compAttackMs() const { return settings_.mainComp.attackMs; }
  double compReleaseMs() const { return settings_.mainComp.releaseMs; }
  double compMakeupDb() const { return settings_.mainComp.makeupDb; }
  int compMode() const { return settings_.mainComp.mode; }
  bool trs1Clock() const { return settings_.clock.trs[0].clock; }
  bool trs1Transport() const { return settings_.clock.trs[0].transport; }
  bool trs2Clock() const { return settings_.clock.trs[1].clock; }
  bool trs2Transport() const { return settings_.clock.trs[1].transport; }
  bool usbClock() const { return settings_.clock.usb.clock; }
  bool usbTransport() const { return settings_.clock.usb.transport; }
  // One-line summary of what the gates do and what cannot be sent: the wire's
  // EV_CLOCK_ROUTE carries only the clock and transport bits.
  QString clockText() const;
  int clockSourceChoice() const;
  QString clockSourceText() const;
  // What the choice can and cannot do in this build: the session carries it,
  // the Pico follow command is not part of the app's link seam yet.
  QString clockSourceNote() const;
  // "1&2" .. "31&32": the pair labels the three device-out pickers offer.
  QStringList deviceOutPairLabels() const;
  int deviceOutMain() const { return deviceOutPair(settings_.deviceOut.main); }
  int deviceOutOut34() const { return deviceOutPair(settings_.deviceOut.out34); }
  int deviceOutPhones() const { return deviceOutPair(settings_.deviceOut.phones); }

  void setPunchPlayFromEnabled(bool enabled);
  void setPunchPlayFromMeasure(int measure);
  void setPunchInEnabled(bool enabled);
  void setPunchInMeasure(int measure);
  void setPunchOutEnabled(bool enabled);
  void setPunchOutMeasure(int measure);
  void setClickMode(int mode);
  void setClickOutput(int output);
  void setClickGainDb(double db);
  void setPhonesSrc(int source);
  void setCueStyle(int style);
  void setOut34Mode(int mode);
  void setSendCue(int source);
  void setSendFx1(int source);
  void setSendFx2(int source);
  void setSendOut34(int source);
  void setSendRec(int source);
  void setCompOn(bool on);
  void setCompThreshDb(double db);
  void setCompRatio(double ratio);
  void setCompAttackMs(double ms);
  void setCompReleaseMs(double ms);
  void setCompMakeupDb(double db);
  void setCompMode(int mode);
  void setTrs1Clock(bool on);
  void setTrs1Transport(bool on);
  void setTrs2Clock(bool on);
  void setTrs2Transport(bool on);
  void setUsbClock(bool on);
  void setUsbTransport(bool on);
  void setDeviceOutMain(int pair);
  void setDeviceOutOut34(int pair);
  void setDeviceOutPhones(int pair);
  void setClockSourceChoice(int choice);

  // Project load replaced the settings object wholesale.
  Q_INVOKABLE void refresh();
  // Push the session clock gates to the link again (project load, startup).
  void pushClockToLink();

 signals:
  void punchChanged();
  void clickChanged();
  void monitorChanged();
  void sendsChanged();
  void compChanged();
  void clockChanged();
  void deviceOutChanged();

 private:
  void setTrsRoute(int output, bool clock, bool transport);
  // Pair index of a stored pair; a hand-edited session can hold any channels, so
  // the picker shows the pair that starts at the stored left channel.
  static int deviceOutPair(const std::array<int, 2>& pair);
  void setDeviceOutPair(int bus, int pair);

  AppState& state_;
  MixerModel& mixer_;
  LinkClient& link_;
  SessionSettings& settings_;
};

}  // namespace mnky::app
