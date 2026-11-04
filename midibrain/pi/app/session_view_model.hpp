#pragma once
// QML view model for the app-owned session-v2 settings: punch markers,
// metronome configuration, monitor/cue routing, send pre/post sources and the
// main-bus compressor. Every setter writes the same field project_io saves in
// the session v2 sections, so the UI and the project file cannot disagree.
#include <QObject>

#include "app_state.hpp"
#include "link_client.hpp"
#include "mixer_model.hpp"
#include "session_settings.hpp"

namespace mnky::app {

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

 private:
  void setTrsRoute(int output, bool clock, bool transport);

  AppState& state_;
  MixerModel& mixer_;
  LinkClient& link_;
  SessionSettings& settings_;
};

}  // namespace mnky::app
