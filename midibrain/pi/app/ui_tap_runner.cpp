#include "ui_tap_runner.hpp"

#include <QCoreApplication>
#include <QEventLoop>

#include <chrono>
#include <thread>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <QTouchEvent>
#include <QVariant>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "app_controller.hpp"
#include "mixer_view_model.hpp"
#include "routing_view_model.hpp"

namespace mnky::app {
namespace {

constexpr int kTapDelayMs = 45;
constexpr int kScenarioDelayMs = 90;
constexpr int kMaxTapsPerScenario = 600;

bool startsWithAny(const QString& id, const QStringList& prefixes) {
  for (const auto& prefix : prefixes) {
    if (id.startsWith(prefix)) return true;
  }
  return false;
}

// The owner's finger arrives as a touch point, not a synthesized mouse event.
// The gate sends QTouchEvent by default so a handler that takes an exclusive
// grab from a child (the dead EDIT/menu class) fails here; `MNKY_TAP_MOUSE=1`
// keeps the older mouse pass for comparison.
bool touchTaps() {
  static const bool enabled = qEnvironmentVariableIsEmpty("MNKY_TAP_MOUSE");
  return enabled;
}

// A synthetic touchscreen device: the event and every point carry the same
// device, so the delivery agent treats the sequence as a real finger.
QPointingDevice& tapTouchDevice() {
  static QPointingDevice device(QStringLiteral("mnkybox-tap-touch"), 1,
                                QInputDevice::DeviceType::TouchScreen,
                                QPointingDevice::PointerType::Finger,
                                QInputDevice::Capability::Position, 1, 0);
  return device;
}

void sendTouch(QQuickWindow* window, QQuickItem* item, bool press,
               const QPointF& scene) {
  // QEventPoint's public constructor leaves `position()` at 0, which makes the
  // delivery agent hit-test the window origin. Seed a mouse event at the same
  // place and reuse its point: that carries position, scene and global
  // positions, so the synthesized touch lands on the item a finger would hit.
  const QPoint global = window->mapToGlobal(scene.toPoint());
  QMouseEvent seed(press ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                   scene, QPointF(global), Qt::LeftButton,
                   press ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier,
                   &tapTouchDevice());
  QTouchEvent event(press ? QEvent::TouchBegin : QEvent::TouchEnd,
                    &tapTouchDevice(), Qt::NoModifier, {seed.point(0)});
  QCoreApplication::sendEvent(window, &event);
  (void)item;
}

}  // namespace

UiTapRunner* UiTapRunner::active_ = nullptr;

UiTapRunner::UiTapRunner(AppController* controller, QQuickWindow* window,
                         QObject* parent)
    : QObject(parent), controller_(controller), window_(window) {}

UiTapRunner::~UiTapRunner() {
  if (active_ == this) {
    active_ = nullptr;
    qInstallMessageHandler(previousHandler_);
  }
}

void UiTapRunner::start() {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  previousHandler_ = qInstallMessageHandler(&UiTapRunner::messageHandler);
  active_ = this;
  buildScenarios();
  std::printf("ui-taps: %d scenario(s), window %dx%d, offscreen in-process taps\n",
              static_cast<int>(scenarios_.size()), window_->width(),
              window_->height());
  // Let the first screen finish its initial layout/bindings before the first
  // synthetic press; screenshots use the same settle time.
  QTimer::singleShot(500, this, &UiTapRunner::tapNext);
}

void UiTapRunner::scheduleNext(int delayMs) {
  QTimer::singleShot(delayMs, this, &UiTapRunner::tapNext);
}

void UiTapRunner::closeOverlays() {
  controller_->configCancel();
  controller_->closeChannel();
  controller_->closeTrackFx();
  controller_->confirmCancel();
  controller_->keypadCancel();
  controller_->closeTempoPopup();
  controller_->closeTabGrid();
  controller_->closeBankGrid();
  controller_->closeTakeSheet();
  controller_->closeTakeEdit();
  controller_->arrange()->closeClipMenu();
  controller_->arrange()->closeLengthPopup();
  controller_->hideValuePopup();
  controller_->setFullscreen(false);
  if (controller_->recording()) controller_->recToggle();
  controller_->stop();
}

void UiTapRunner::panelPressFocused() {
  controller_->activateFocus();
}

void UiTapRunner::panelExitPress() {
  LinkStatus held;
  held.shift = true;
  controller_->updateShiftState(held);
  LinkStatus released;
  released.shift = false;
  controller_->updateShiftState(released);
}

void UiTapRunner::buildScenarios() {
  // The take-row scenarios below need a real list. The tap run owns a
  // disposable HOME, so write one small 48 kHz mono WAV there (a valid
  // nn-name-nnn take) instead of depending on a developer's take folder; the
  // recorder rescans so the fixture is listed before the first scenario.
  const QString takeDirectory = controller_->recorder()->takeDirectory();
  QDir().mkpath(takeDirectory);
  const QString fixture = takeDirectory + QStringLiteral("/00-fixture-001.wav");
  if (!QFile::exists(fixture)) {
    QFile file(fixture);
    if (file.open(QIODevice::WriteOnly)) {
      QDataStream out(&file);
      out.setByteOrder(QDataStream::LittleEndian);
      constexpr quint32 kFrames = 4800;  // 0.1 s at 48 kHz
      constexpr quint32 kDataBytes = kFrames * 2;
      out.writeRawData("RIFF", 4);
      out << quint32(36 + kDataBytes);
      out.writeRawData("WAVEfmt ", 8);
      out << quint32(16) << quint16(1) << quint16(1) << quint32(48000)
          << quint32(96000) << quint16(2) << quint16(16);
      out.writeRawData("data", 4);
      out << kDataBytes;
      const std::vector<char> silence(kDataBytes, 0);
      out.writeRawData(silence.data(), static_cast<int>(silence.size()));
      file.close();
      controller_->recorder()->takeStateChanged();
    }
  }
  // 1. The nav row itself: this is the only QML path that calls setTab().
  // The fourth destination (More) opens the six-page grid; closeOverlays()
  // closes it before the next scenario.
  scenarios_.push_back(Scenario{
      QStringLiteral("tabs"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
      },
      QStringLiteral("nav."), {}, {}, false, true});

  // 2. Shared transport strip.
  // Stage-1 regression for the top-band leak: the channel popout's header
  // controls sit at y ~40, inside the top 52 px band that used to deliver the
  // same tap to the tab row underneath. RENAME must reach its handler and the
  // popup must survive; a leaked tab tap would navigate and tear it down.
  scenarios_.push_back(Scenario{
      QStringLiteral("popup.topband"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        controller_->openChannel(0);
      },
      QString(), {}, {}, false, false, false, {},
      {QStringLiteral("channel.rename")},
      [this] {
        return controller_->mixer()->channelOpen() && controller_->keypadOpen() &&
               controller_->tab() == 0;
      },
      QStringLiteral("top-band tap leaked: channel popup closed or tab changed")});
  scenarios_.back().topBand = true;

  scenarios_.push_back(Scenario{
      QStringLiteral("transport"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
      },
      QStringLiteral("transport."), {}, {}, false, true});
  // A2: STOP while already stopped returns the playhead to the start (the
  // classic hardware STOP-STOP). SONG mode is the hard case: PLAY normally
  // preserves the stopped position, so only the return-to-zero anchor makes
  // the next start read bar 1.
  const auto stoppedInSong = [this] {
    closeOverlays();
    controller_->showShiftFace(false);
    controller_->setTab(0);
    controller_->setSongMode(true);
    // Park the playhead away from bar 1: only the STOP taps may bring it back,
    // so the scenario cannot pass on the setup's own stop.
    controller_->arrange()->seekToBeat(16.0);
  };
  scenarios_.push_back(Scenario{
      QStringLiteral("transport.stopstop"), stoppedInSong, QString(), {}, {},
      false, true, false, {},
      {QStringLiteral("transport.stop"), QStringLiteral("transport.stop")},
      [this] {
        return !controller_->rolling() &&
               controller_->positionText().startsWith(QStringLiteral("001:1:00"));
      },
      QStringLiteral("STOP twice did not return the position to 001:1:00")});
  scenarios_.push_back(Scenario{
      QStringLiteral("transport.stopstop.play"), stoppedInSong, QString(), {},
      {}, false, true, false, {},
      {QStringLiteral("transport.stop"), QStringLiteral("transport.stop"),
       QStringLiteral("transport.play")},
      [this] {
        return controller_->rolling() &&
               controller_->positionText().startsWith(QStringLiteral("001:1:"));
      },
      QStringLiteral("PLAY after STOP-STOP did not start from bar 1")});

  const QStringList scaffoldExcluded = {
      QStringLiteral("nav."), QStringLiteral("transport."),
      QStringLiteral("config."), QStringLiteral("keypad."),
      QStringLiteral("confirm."), QStringLiteral("shift.")};
  const QStringList shiftExcluded = {
      QStringLiteral("nav."), QStringLiteral("transport."),
      QStringLiteral("config."), QStringLiteral("keypad."),
      QStringLiteral("confirm.")};

  for (int tab = 0; tab < kScreenCount; ++tab) {
    QStringList excluded = scaffoldExcluded;
    if (tab == 1) {
      // "routing.shiftpipes" navigates away from the primary face; tap it in
      // its own scenario so the node/route controls are all reached first.
      excluded << QStringLiteral("routing.shiftpipes");
    }
    if (tab == 0) {
      // A11: the pager grid owns its own scenario, so the screen pass keeps
      // walking the strips instead of opening an overlay over them.
      excluded << QStringLiteral("mixer.bank.pages");
    }
    if (tab == 2) {
      // Stage 4 recorder hierarchy: the track picker, the take sheet, the
      // recording-source popup and the row EDIT/WAVE/menu surfaces own their
      // own passes; opening them mid-walk would hide the controls behind them.
      excluded << QStringLiteral("recorder.takesheet")
               << QStringLiteral("recorder.track.pick")
               << QStringLiteral("recorder.recsource")
               << QStringLiteral("recorder.take.edit.")
               << QStringLiteral("recorder.take.wave.")
               << QStringLiteral("recorder.take.menu.");
    }
    if (tab == 8) {
      // The clip sheet, the LEN stepper, the section rename keypad and the
      // render verb own their scenarios; the screen pass covers the toolbar,
      // section band and clip blocks.
      excluded << QStringLiteral("arrange.rename")
               << QStringLiteral("arrange.length")
               << QStringLiteral("arrange.clipmenu")
               << QStringLiteral("arrange.clip.menu");
    }
    scenarios_.push_back(Scenario{
        QStringLiteral("screen.%1").arg(tab),
        [this, tab] {
          closeOverlays();
          controller_->showShiftFace(false);
          controller_->setTab(tab);
          if (tab == 0) {
            primeMixerTracks();
            controller_->mixer()->setTrackMode(false);
            controller_->mixer()->setBank(0);
          } else if (tab == 1) {
            controller_->routing()->setShiftPage(0);
          } else if (tab == 6) {
            controller_->eq()->selectMain();
            controller_->eq()->selectBand(2);
          } else if (tab == 8) {
            controller_->demoSeedArrange();
            controller_->arrange()->setSongMode(true);
          }
        },
        QString(), excluded, {}, false, true});
  }
  scenarios_.push_back(Scenario{
      QStringLiteral("screen.1.shiftpipes"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(1);
      },
      QStringLiteral("routing.shiftpipes"), {}, {}, false, true});

  // 3. SHIFT faces. Tab 0 has two: the perform-strip send/monitor face and the
  // TRACK face (which owns the four-way INPUT MON selector). Tab 1 has the
  // channel-filter page and the MIDI FX pipe page; the pipe page is primed with
  // one route and one Filter pipe so its row/parameter controls exist.
  for (int tab = 0; tab < kScreenCount; ++tab) {
    scenarios_.push_back(Scenario{
        QStringLiteral("shift.%1").arg(tab),
        [this, tab] {
          closeOverlays();
          controller_->showShiftFace(false);
          controller_->setTab(tab);
          controller_->showShiftFace(true);
          if (tab == 0) {
            primeMixerTracks();
            controller_->mixer()->setTrackMode(false);
            controller_->mixer()->setBank(0);
          } else if (tab == 1) {
            controller_->routing()->setShiftPage(0);
            controller_->routing()->setPipeSource(0);
            controller_->routing()->setPipeDestination(2);
            if (!controller_->routing()->routeEnabled(0, 2)) {
              controller_->routing()->toggleRoute(0, 2);
            }
          } else if (tab == 8) {
            controller_->demoSeedArrange();
            controller_->arrange()->setSongMode(true);
          }
        },
        QStringLiteral("shift."), shiftExcluded, {}, false, false});
  }
  // D8: the OUTPUT page's clock-source choices. The page-switch pass changes
  // pages before its content, so the row gets its own scenario.
  scenarios_.push_back(Scenario{
      QStringLiteral("shift.3.output"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(3);
        controller_->setSystemShiftPage(0);
        controller_->showShiftFace(true);
      },
      QStringLiteral("shift.monitor.clocksource."), {}, {}, false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("shift.0.track"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        controller_->mixer()->setTrackMode(true);
        controller_->mixer()->setTrackIndex(2);
        controller_->showShiftFace(true);
      },
      QStringLiteral("shift."), shiftExcluded, {}, false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("shift.1.pipes"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(1);
        controller_->showShiftFace(true);
        controller_->routing()->setShiftPage(1);
        controller_->routing()->setPipeSource(0);
        controller_->routing()->setPipeDestination(2);
        if (!controller_->routing()->routeEnabled(0, 2)) {
          controller_->routing()->toggleRoute(0, 2);
        }
        if (controller_->routing()->pipeCount() == 0) {
          controller_->routing()->setAddTypeIndex(0);
          controller_->routing()->addPipe();
        }
        controller_->routing()->selectPipe(0);
      },
      QStringLiteral("shift.routing.pipes."),
      QStringList(shiftExcluded) << QStringLiteral("shift.routing.pipes.chain."),
      {}, false, true});
  // Chain preset files live in the chain menu popup and open the shared
  // path-entry keypad, which covers the page: one scenario per action so each
  // one is measured on its own.
  for (const char* action : {"export", "import"}) {
    const QString prefix = QStringLiteral("shift.routing.pipes.chain.") +
                           QLatin1String(action);
    scenarios_.push_back(Scenario{
        QStringLiteral("shift.1.chain.%1").arg(QLatin1String(action)),
        [this] {
          closeOverlays();
          controller_->showShiftFace(false);
          controller_->setTab(1);
          controller_->showShiftFace(true);
          controller_->routing()->setShiftPage(1);
          controller_->routing()->setPipeSource(0);
          controller_->routing()->setPipeDestination(2);
          if (!controller_->routing()->routeEnabled(0, 2)) {
            controller_->routing()->toggleRoute(0, 2);
          }
          if (controller_->routing()->pipeCount() == 0) {
            controller_->routing()->setAddTypeIndex(0);
            controller_->routing()->addPipe();
          }
          controller_->routing()->selectPipe(0);
          controller_->openConfig(QStringLiteral("chainmenu"));
        },
        prefix, shiftExcluded, {prefix}, true, true});
  }

  // A13: the source-less generator route on the pipe page: FROM = GENERATOR,
  // TO = USB OUT, head = CC LFO, no input selected anywhere.
  scenarios_.push_back(Scenario{
      QStringLiteral("shift.1.pipes.generator"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(1);
        primeGeneratorChain();
      },
      QStringLiteral("shift.routing.pipes."),
      QStringList(shiftExcluded) << QStringLiteral("shift.routing.pipes.chain."),
      {}, false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("config.pipetype.generator"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(1);
        primeGeneratorChain();
        controller_->openConfig(QStringLiteral("pipetype"));
      },
      QStringLiteral("config.pipetype."), {},
      {QStringLiteral("config.cancel")}, false, true});
  // 3b. The generated parameter cards, one pass per card family: the FILTER
  // pipe is all bool chips (each opens its choice list), the ARP pipe mixes a
  // numeric arc-dial card with chips. Same generic card, no per-type QML.
  scenarios_.push_back(Scenario{
      QStringLiteral("shift.1.pipes.cards.toggle"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        primePipeChain(0);
      },
      QStringLiteral("shift.routing.pipes.param."), shiftExcluded, {}, false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("shift.1.pipes.cards.knob"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        primePipeChain(1);
      },
      QStringLiteral("shift.routing.pipes.param."), shiftExcluded, {}, false, true});

  // 3c. Dynamic mixer states of their own: the empty mixer (ADD TRACK is the
  // only strip control), the full channel popout, and the add-track popup.
  scenarios_.push_back(Scenario{
      QStringLiteral("mixer.empty"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        clearMixerTracks();
        controller_->mixer()->setTrackMode(false);
      },
      QStringLiteral("mixer."), {QStringLiteral("mixer.bank.pages")}, {}, false,
      true});
  // A11: the page grid itself: every non-empty page cell plus CLOSE.
  scenarios_.push_back(Scenario{
      QStringLiteral("mixer.bankgrid"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        controller_->mixer()->setTrackMode(false);
        controller_->mixer()->setBank(0);
        controller_->openBankGrid();
      },
      QStringLiteral("bank.page."), {},
      {QStringLiteral("bank.page.close")}, false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("mixer.channel"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        controller_->openChannel(1);
      },
      QStringLiteral("channel."), {}, {QStringLiteral("channel.close")}, false, true});
  // Stage 2 item 4h: riding a strip fader moves the value and never opens the
  // channel sheet. The strip has three touch targets (name = select, CH =
  // expand, fader = level); this pass drags the fader and asserts that the
  // popout stayed closed while the gain actually moved.
  {
    auto faderBefore = std::make_shared<double>(0.0);
    Scenario scenario{
        QStringLiteral("mixer.fader-ride"),
        [this, faderBefore] {
          closeOverlays();
          controller_->showShiftFace(false);
          controller_->setTab(0);
          primeMixerTracks();
          controller_->mixer()->setTrackMode(false);
          controller_->mixer()->setBank(0);
          *faderBefore = controller_->mixer()->gainDbAt(0);
        },
        QStringLiteral("mixer.gain."), {}, {QStringLiteral("mixer.gain.0")}, true,
        true, true};
    scenario.verify = [this, faderBefore] {
      return !controller_->mixer()->channelOpen() &&
             controller_->mixer()->gainDbAt(0) != *faderBefore;
    };
    scenario.verifyFailure =
        QStringLiteral("the fader ride opened the channel or moved no gain");
    scenarios_.push_back(scenario);
  }
  scenarios_.push_back(Scenario{
      QStringLiteral("mixer.addtrack"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        controller_->requestAddTrack();
      },
      QStringLiteral("config.addtrack."), {},
      {QStringLiteral("config.addtrack.name")}, false, true});

  // 3d. The per-track FX rack popup (TRACK FX): both slot cards filled with the
  // built-in effects, so every selector, the wet chips, the bypass chips and
  // the delay/reverb parameter cards are tapped. A second pass covers a slot
  // that is still NONE (the selector is the only control on that card).
  // A3: selecting an effect into an empty slot must be audible at once, even
  // when the slot carried the legacy send wet 0; each pick gets its own pass
  // and asserts the model's wet plus (for a CLAP pick) that the instance is
  // loaded rather than silently dry.
  const auto openEmptyFxSlot = [this] {
    closeOverlays();
    controller_->showShiftFace(false);
    controller_->setTab(0);
    primeMixerTracks();
    controller_->mixer()->selectTrackAt(1);
    controller_->mixer()->setFxChoiceAt(1, 0, 0);
    controller_->mixer()->setFxWetAt(1, 0, 0.0);
    controller_->openChannel(1);
    controller_->openFxSlot(0);
  };
  scenarios_.push_back(Scenario{
      QStringLiteral("fxslot.delay.wet"), openEmptyFxSlot,
      QStringLiteral("config.fxslot.1"), {},
      {QStringLiteral("config.fxslot.1")}, true, true, false, {}, {},
      [this] {
        return controller_->mixer()->fxNameAt(1, 0) == QStringLiteral("DELAY") &&
               controller_->mixer()->fxWetAt(1, 0) > 0.0;
      },
      QStringLiteral("DELAY into an empty slot did not start wet")});
  scenarios_.push_back(Scenario{
      QStringLiteral("fxslot.reverb.wet"), openEmptyFxSlot,
      QStringLiteral("config.fxslot.2"), {},
      {QStringLiteral("config.fxslot.2")}, true, true, false, {}, {},
      [this] {
        return controller_->mixer()->fxNameAt(1, 0) == QStringLiteral("REVERB") &&
               controller_->mixer()->fxWetAt(1, 0) > 0.0;
      },
      QStringLiteral("REVERB into an empty slot did not start wet")});
  if (controller_->mixer()->fxChoiceLabels().size() >
      MixerViewModel::kFxChoiceBuiltinCount) {
    scenarios_.push_back(Scenario{
        QStringLiteral("fxslot.plugin.wet"), openEmptyFxSlot,
        QStringLiteral("config.fxslot.3"), {},
        {QStringLiteral("config.fxslot.3")}, true, true, false, {}, {},
        [this] {
          const QString note = controller_->mixer()->fxSlotNote(1, 0);
          return controller_->mixer()->fxNameAt(1, 0) != QStringLiteral("NONE") &&
                 controller_->mixer()->fxWetAt(1, 0) > 0.0 &&
                 !note.contains(QStringLiteral("NOT LOADED")) &&
                 note.contains(QStringLiteral("active"));
        },
        QStringLiteral("a CLAP pick did not load or did not start wet")});
  } else {
    scenarios_.push_back(Scenario{
        QStringLiteral("fxslot.noplugins"), openEmptyFxSlot, QString(), {}, {},
        false, true, false, {}, {},
        [this] {
          return controller_->config()->fxSlotNote().contains(
              QStringLiteral("no CLAP plugins found"));
        },
        QStringLiteral("a build without plugins did not say so")});
  }
  scenarios_.push_back(Scenario{
      QStringLiteral("mixer.trackfx"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        controller_->mixer()->selectTrackAt(1);
        controller_->mixer()->setFxChoiceAt(controller_->mixer()->selectedStrip(),
                                            0, 1);
        controller_->mixer()->setFxChoiceAt(controller_->mixer()->selectedStrip(),
                                            1, 2);
        controller_->openChannel(1);
        controller_->openTrackFx(0);
      },
      QStringLiteral("trackfx."), {}, {QStringLiteral("trackfx.close")}, false,
      true});
  scenarios_.push_back(Scenario{
      QStringLiteral("mixer.trackfx.none"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        controller_->mixer()->selectTrackAt(2);
        controller_->mixer()->setFxChoiceAt(controller_->mixer()->selectedStrip(),
                                            0, 0);
        controller_->mixer()->setFxChoiceAt(controller_->mixer()->selectedStrip(),
                                            1, 0);
        controller_->openTrackFx(1);
      },
      QStringLiteral("trackfx."), {}, {QStringLiteral("trackfx.close")}, false,
      true});
  // Panel parity for the owner crash: the first ring stop of TRACK FX is
  // CLOSE, and a PRESS on it must close the popup (the old dispatch read
  // parts[2] of "trackfx.close" and segfaulted).
  scenarios_.push_back(Scenario{
      QStringLiteral("mixer.trackfx.panelclose"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        controller_->mixer()->selectTrackAt(1);
        controller_->openChannel(1);
        controller_->openTrackFx(0);
        panelPressFocused();
      },
      QStringLiteral("nomatch."), {}, {}, false, false, false, {}, {},
      [this] { return !controller_->fx()->open(); },
      QStringLiteral("panel PRESS on trackfx.close did not close TRACK FX")});
  // EXIT must unwind every overlay, not trap the panel behind the popout. A
  // short press per overlay: channel popout, TRACK FX, bank grid, config,
  // tempo, tab grid, and the deliberate keypad/confirm traps (which cancel by
  // design). All must be closed at the end of the setup.
  scenarios_.push_back(Scenario{
      QStringLiteral("panel.exit"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        controller_->mixer()->selectTrackAt(1);
        controller_->openChannel(1);
        panelExitPress();
        controller_->openTrackFx(0);
        panelExitPress();
        controller_->openBankGrid();
        panelExitPress();
        controller_->openConfig(QStringLiteral("metronome"));
        panelExitPress();
        controller_->openTempoPopup();
        panelExitPress();
        controller_->openTabGrid();
        panelExitPress();
        controller_->requestKeypad(QStringLiteral("EXIT PROBE"), QString(),
                                   QStringLiteral("probe"), 0);
        panelExitPress();
        controller_->requestConfirm(QStringLiteral("EXIT PROBE"),
                                    QStringLiteral("probe"),
                                    QStringLiteral("OK"),
                                    QStringLiteral("probe"), -1);
        panelExitPress();
      },
      QStringLiteral("nomatch."), {}, {}, false, false, false, {}, {},
      [this] {
        return !controller_->mixer()->channelOpen() && !controller_->fx()->open() &&
               !controller_->bankGridOpen() &&
               !controller_->config()->open() &&
               !controller_->tempoPopupOpen() && !controller_->tabGridOpen() &&
               !controller_->keypadOpen() && !controller_->confirmOpen();
      },
      QStringLiteral("a short EXIT press left an overlay open")});
  // 3e. X/Y per track: the grid ids exist on both a bound slot pair and an
  // empty one, and the track row selects the strip both grids bend.
  scenarios_.push_back(Scenario{
      QStringLiteral("screen.7.track"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(7);
        primeMixerTracks();
        controller_->mixer()->setFxChoiceAt(controller_->mixer()->selectedStrip(),
                                            0, 1);
        controller_->mixer()->setFxChoiceAt(controller_->mixer()->selectedStrip(),
                                            1, 2);
      },
      QStringLiteral("xy."), {}, {}, false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("screen.7.none"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(7);
        primeMixerTracks();
        controller_->mixer()->setFxChoiceAt(controller_->mixer()->selectedStrip(),
                                            0, 0);
        controller_->mixer()->setFxChoiceAt(controller_->mixer()->selectedStrip(),
                                            1, 0);
      },
      QStringLiteral("xy."), {}, {}, false, true});

  // 3f. Clip-click pass (owner crash class: "clicking an audio clip" on the
  // Pi). Every surface that selects/loads a take is clicked: the recorder rows
  // and their "..." menus, the take-sheet opener, and each row of the TAKE FOR
  // TRACK sheet (the sheet closes on a pick, so every row gets its own pass).
  // It runs on whatever the take folder holds - on the Pi that is the real
  // session material - because the bug is about real files. The EDIT affordances
  // open the take edit popup, so they run in the edit scenarios below instead
  // (an open popup would hide the rows this pass is about).
  scenarios_.push_back(Scenario{
      QStringLiteral("clips.recorder"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        activateRealTakes();
        controller_->setTab(2);
        controller_->recorder()->refresh();
      },
      // The track selector row would walk to a take-less track and empty the
      // list before its rows are reached; this pass is about the rows. The
      // take-sheet opener and the EDIT affordances own their own passes.
      QString(), QStringList(scaffoldExcluded)
                     << QStringLiteral("recorder.track.")
                     << QStringLiteral("recorder.takesheet")
                     << QStringLiteral("recorder.take.edit.")
                     << QStringLiteral("recorder.take.wave."),
      {}, false, true});
  // The take-row buttons are separate 44 px surfaces on the same delegate. Each
  // gets a one-tap pass that asserts the popup it names is actually open when
  // the tap finishes: a dead EDIT/menu (the owner's Pi report) is a failure
  // here, not just a quiet tap.
  const auto openRecorderRows = [this] {
    closeOverlays();
    controller_->showShiftFace(false);
    controller_->setTab(0);
    primeMixerTracks();
    activateRealTakes();
    controller_->setTab(2);
    controller_->recorder()->selectRow(0);
  };
  scenarios_.push_back(Scenario{
      QStringLiteral("clips.recorder.edit"), openRecorderRows,
      QStringLiteral("recorder.take.edit.0"), {},
      {QStringLiteral("recorder.take.edit.0")}, true, true, false, {}, {},
      [this] { return controller_->timeline()->editOpen(); },
      QStringLiteral("EDIT did not open the take edit popup")});
  scenarios_.push_back(Scenario{
      QStringLiteral("clips.recorder.menu"), openRecorderRows,
      QStringLiteral("recorder.take.menu.0"), {},
      {QStringLiteral("recorder.take.menu.0")}, true, true, false, {}, {},
      [this] {
        return controller_->config()->open() &&
               controller_->config()->kind() == QStringLiteral("takeactions") &&
               controller_->config()->takeTargetRow() == 0;
      },
      QStringLiteral("... did not open the take actions config")});
  scenarios_.push_back(Scenario{
      QStringLiteral("clips.recorder.wave"), openRecorderRows,
      QStringLiteral("recorder.take.wave.0"), {},
      {QStringLiteral("recorder.take.wave.0")}, true, true, false, {}, {},
      [this] { return controller_->timeline()->editOpen(); },
      QStringLiteral("the row waveform did not open the take edit popup")});
  scenarios_.push_back(Scenario{
      QStringLiteral("clips.recorder.row"), openRecorderRows,
      QStringLiteral("recorder.take.0"), {},
      {QStringLiteral("recorder.take.0")}, true, true, false, {}, {},
      [this] {
        return controller_->recorder()->selectedRow() == 0 &&
               controller_->recorder()->activeTake() != QStringLiteral("LIVE INPUT");
      },
      QStringLiteral("the row body did not make the take ACTIVE")});
  // The take sheet itself: CLOSE is its own pass (it ends the sheet), and each
  // row gets a pass that ends on that row because a pick closes the sheet. A
  // folder with fewer rows simply has nothing to tap there, which is why these
  // passes do not require a target.
  const auto openTakeSheetNow = [this] {
    closeOverlays();
    controller_->showShiftFace(false);
    controller_->setTab(0);
    primeMixerTracks();
    activateRealTakes();
    controller_->setTab(2);
    controller_->openTakeSheet(controller_->recorder()->selectedTrack());
  };
  scenarios_.push_back(Scenario{
      QStringLiteral("clips.takesheet.close"), openTakeSheetNow,
      QStringLiteral("take.sheet.close"), {},
      {QStringLiteral("take.sheet.close")}, true, true});
  for (int row = 0; row < 4; ++row) {
    const QString rowId = QStringLiteral("take.sheet.row.%1").arg(row);
    scenarios_.push_back(Scenario{
        QStringLiteral("clips.takesheet.%1").arg(row), openTakeSheetNow, rowId,
        {QStringLiteral("take.sheet.close")}, {rowId}, true, false});
  }
  // The take sheet's EDIT column: a tap opens the edit popup for that row. The
  // terminal id is matched exactly (a prefix would also tap row 10's EDIT).
  for (int row = 0; row < 4; ++row) {
    const QString editId = QStringLiteral("take.sheet.edit.%1").arg(row);
    scenarios_.push_back(Scenario{
        QStringLiteral("clips.takesheet.edit.%1").arg(row), openTakeSheetNow,
        editId,
        {QStringLiteral("take.sheet.close"), QStringLiteral("take.sheet.row.")},
        {editId}, true, false, false, {}, {},
        // A folder with fewer than four rows has no EDIT button to tap; only
        // an existing button must open the popup.
        [this] { return !scenarioSawTarget_ || controller_->timeline()->editOpen(); },
        QStringLiteral("take-sheet EDIT did not open the take edit popup")});
  }

  // Stage 4 track picker: the one selected-track button opens an 18-track
  // sheet; picking a track closes it and selects that track.
  scenarios_.push_back(Scenario{
      QStringLiteral("clips.recorder.trackpicker"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(2);
        controller_->recorder()->setSelectedTrack(0);
        controller_->openTrackPicker();
      },
      QStringLiteral("recorder.track.pick.7"), {},
      {QStringLiteral("recorder.track.pick.7")}, true, true, false, {}, {},
      [this] {
        return !controller_->trackPickerOpen() &&
               controller_->recorder()->selectedTrack() == 7;
      },
      QStringLiteral("the track picker did not select track 7 and close")});

  // 3g. The take edit popup (plan W5): open it from the recorder's EDIT
  // affordance, then walk every control. The handle pass drags the two edge
  // targets by 24 px, so the drag path (not only the press) is exercised.
  const auto openEditPopup = [this] {
    closeOverlays();
    controller_->showShiftFace(false);
    controller_->setTab(0);
    primeMixerTracks();
    activateRealTakes();
    controller_->setTab(2);
    controller_->recorder()->selectRow(0);
    controller_->requestEditTake(0);
  };
  scenarios_.push_back(Scenario{
      QStringLiteral("edit.popup"), openEditPopup, QStringLiteral("edit."),
      {QStringLiteral("edit.in"), QStringLiteral("edit.out")},
      {QStringLiteral("edit.close")}, false, true, false});
  scenarios_.push_back(Scenario{
      QStringLiteral("edit.handles"), openEditPopup, QStringLiteral("edit."), {},
      {QStringLiteral("edit.in"), QStringLiteral("edit.out"),
       QStringLiteral("edit.close")},
      false, true, true});
  // Op passes: each card is selected, its value dragged where it has one, and
  // APPLY runs; UNDO/RENDER/CLOSE stay for the terminal taps of the main pass.
  for (int op = 0; op < 8; ++op) {
    const QString opId = QStringLiteral("edit.op.%1").arg(op);
    scenarios_.push_back(Scenario{
        QStringLiteral("edit.op.%1").arg(op),
        [this, opId] {
          closeOverlays();
          controller_->showShiftFace(false);
          controller_->setTab(0);
          primeMixerTracks();
          activateRealTakes();
          controller_->setTab(2);
          controller_->recorder()->selectRow(0);
          controller_->requestEditTake(0);
          controller_->timeline()->beginSelection(
              controller_->timeline()->selectedTrack(), 1.0);
          controller_->timeline()->updateSelection(5.0);
          controller_->timeline()->endSelection();
        },
        opId, {}, {opId}, false, false});
  }
  // APPLY, UNDO and RENDER once each (they change the region list, so they get
  // their own pass and every earlier scenario starts from a fresh popup).
  scenarios_.push_back(Scenario{
      QStringLiteral("edit.apply"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        activateRealTakes();
        controller_->setTab(2);
        controller_->recorder()->selectRow(0);
        controller_->requestEditTake(0);
        controller_->timeline()->beginSelection(
            controller_->timeline()->selectedTrack(), 1.0);
        controller_->timeline()->updateSelection(5.0);
        controller_->timeline()->endSelection();
        controller_->timeline()->selectEditOp(5);  // NORMALIZE
        controller_->timeline()->applyEditOp();
      },
      QStringLiteral("edit.apply"), {}, {QStringLiteral("edit.apply")}, false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("edit.undo"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        activateRealTakes();
        controller_->setTab(2);
        controller_->recorder()->selectRow(0);
        controller_->requestEditTake(0);
        controller_->timeline()->beginSelection(
            controller_->timeline()->selectedTrack(), 1.0);
        controller_->timeline()->updateSelection(5.0);
        controller_->timeline()->endSelection();
        controller_->timeline()->selectEditOp(5);
        controller_->timeline()->applyEditOp();
      },
      QStringLiteral("edit.undo"), {}, {QStringLiteral("edit.undo")}, false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("edit.render"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
        primeMixerTracks();
        activateRealTakes();
        controller_->setTab(2);
        controller_->recorder()->selectRow(0);
        controller_->requestEditTake(0);
        controller_->timeline()->beginSelection(
            controller_->timeline()->selectedTrack(), 1.0);
        controller_->timeline()->updateSelection(3.0);
        controller_->timeline()->endSelection();
      },
      QStringLiteral("edit.render"), {}, {QStringLiteral("edit.render")}, false, true});

  // 3i. KEYPAD TYPING (owner bug: keys dismissed the keypad and BACK looked
  // dead). This pass opens a track-name keypad, types a real sequence, checks
  // the QML buffer after every key, single-taps DEL (one character), long-holds
  // DEL (clears), types a new name and commits with DONE. The buffer is read
  // from the Keypad objectName probe; no model pointer is held across a rescan.
  scenarios_.push_back(Scenario{
      QStringLiteral("keypad.typing"),
      [this] {
        closeOverlays();
        controller_->setTab(0);
        primeMixerTracks();
        const int slot = controller_->mixer()->nextFreeSlot();
        int target = 0;
        if (slot >= 0 &&
            controller_->mixer()->addTrack(slot, QStringLiteral("drums"), -1,
                                           false, 0)) {
          target = slot;
        }
        controller_->mixer()->setTrackIndex(target);
        controller_->requestRenameTrack(target);
      },
      QStringLiteral("keypad."), {}, {}, false, true});
  {
    Scenario& scenario = scenarios_.back();
    scenario.typing = {
        {QStringLiteral("keypad.key.b"), QStringLiteral("drumsb"), false},
        {QStringLiteral("keypad.key.e"), QStringLiteral("drumsbe"), false},
        {QStringLiteral("keypad.key.a"), QStringLiteral("drumsbea"), false},
        {QStringLiteral("keypad.key.t"), QStringLiteral("drumsbeat"), false},
        {QStringLiteral("keypad.back"), QStringLiteral("drumsbea"), false},
        {QStringLiteral("keypad.back"), QString(), true},
        {QStringLiteral("keypad.key.b"), QStringLiteral("b"), false},
        {QStringLiteral("keypad.key.e"), QStringLiteral("be"), false},
        {QStringLiteral("keypad.key.a"), QStringLiteral("bea"), false},
        {QStringLiteral("keypad.key.t"), QStringLiteral("beat"), false},
        {QStringLiteral("keypad.done"), QStringLiteral("beat"), false}};
    scenario.verify = [this] {
      return controller_->mixer()->trackName() == QStringLiteral("beat");
    };
    scenario.verifyFailure =
        QStringLiteral("DONE did not commit the typed name (expected beat)");
  }

  // 3h. ARRANGE (W4/W5): the section band/clip blocks, the clip "..." sheet,
  // the LEN stepper, section rename, punch-into-song auto-place and RENDER SONG.
  // Each destructive/overlay-opening verb gets its own pass so the modal it
  // opens cannot hide the rest of the screen.
  const auto primeArrange = [this] {
    closeOverlays();
    controller_->showShiftFace(false);
    controller_->setTab(0);
    primeMixerTracks();
    activateRealTakes();
    controller_->demoSeedArrange();
    controller_->arrange()->setSongMode(true);
    controller_->setTab(8);
  };
  scenarios_.push_back(Scenario{
      QStringLiteral("arrange.screen"), primeArrange, QStringLiteral("arrange."),
      {QStringLiteral("arrange.rename"), QStringLiteral("arrange.length"),
       QStringLiteral("arrange.clipmenu"), QStringLiteral("arrange.render")},
      {}, false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("arrange.rename"), primeArrange,
      QStringLiteral("arrange.rename"), {}, {QStringLiteral("arrange.rename")},
      false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("arrange.length"),
      [this, primeArrange] {
        primeArrange();
        controller_->arrange()->openLengthPopup();
      },
      QStringLiteral("arrange.length."), {}, {}, false, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("arrange.render"), primeArrange,
      QStringLiteral("arrange.render"), {}, {QStringLiteral("arrange.render")},
      true, true});
  scenarios_.push_back(Scenario{
      QStringLiteral("arrange.punch"),
      [this, primeArrange] {
        primeArrange();
        controller_->demoPunchAutoPlace();
      },
      QStringLiteral("arrange.clip."), {}, {}, false, true});
  // Clip "..." actions: OPEN EDIT opens the W3 popup, DELETE opens the confirm,
  // so those get terminal-only passes; MOVE/TRIM/DUPLICATE/MUTE each run in
  // their own pass because MOVE/TRIM close the sheet on the pick.
  const auto openClipSheet = [this, primeArrange] {
    primeArrange();
    controller_->arrange()->openClipMenu(0);
  };
  for (int action : {0, 1, 2, 3, 5, 6}) {
    const QString id = QStringLiteral("arrange.clip.action.%1").arg(action);
    scenarios_.push_back(Scenario{
        QStringLiteral("arrange.clip.action.%1").arg(action), openClipSheet, id,
        {}, {id}, false, true});
  }
  scenarios_.push_back(Scenario{
      QStringLiteral("arrange.clip.gain"),
      [this, openClipSheet] {
        openClipSheet();
        controller_->arrange()->clipMenuAction(4);
      },
      QStringLiteral("arrange.clip."),
      {QStringLiteral("arrange.clip.close"), QStringLiteral("arrange.clip.pane.back")},
      {QStringLiteral("arrange.clip.close")}, false, true});

  // 4. Every ConfigPopup kind. One pass taps all non-terminal controls and ends
  // with CANCEL; a second pass opens the same kind and taps OK, so both draft
  // paths and every focus stop in the popup are exercised.
  const QStringList configKinds = {
      QStringLiteral("metronome"), QStringLiteral("sends"),
      QStringLiteral("monitor"),   QStringLiteral("comp"),
      QStringLiteral("eqtype"),    QStringLiteral("delay"),
      QStringLiteral("reverb"),    QStringLiteral("pipechannels"),
      QStringLiteral("timelineops"), QStringLiteral("trackmode"),
      QStringLiteral("recmode"),     QStringLiteral("takeactions"),
      QStringLiteral("trackfromtake"),
      QStringLiteral("inputsource"), QStringLiteral("outputdest"),
      QStringLiteral("fxslot"),    QStringLiteral("addtrack"),
      QStringLiteral("delaydivision"), QStringLiteral("reverbmodel"),
      QStringLiteral("pipesource"), QStringLiteral("pipedest"),
      QStringLiteral("pipetype"), QStringLiteral("pipeparam"),
      QStringLiteral("chainmenu")};
  for (const auto& kind : configKinds) {
    const QString openConfig = kind;
    scenarios_.push_back(Scenario{
        QStringLiteral("config.%1.cancel").arg(kind),
        [this, openConfig] {
          closeOverlays();
          if (openConfig == QStringLiteral("inputsource")) {
            controller_->setTab(0);
            primeMixerTracks();
            controller_->openInputSource(0);
            return;
          }
          if (openConfig == QStringLiteral("outputdest")) {
            controller_->setTab(0);
            primeMixerTracks();
            controller_->openOutputDest(0);
            return;
          }
          if (openConfig == QStringLiteral("fxslot")) {
            controller_->setTab(0);
            primeMixerTracks();
            controller_->openChannel(1);
            controller_->openFxSlot(0);
            return;
          }
          if (openConfig == QStringLiteral("addtrack")) {
            controller_->setTab(0);
            primeMixerTracks();
          }
          if (openConfig == QStringLiteral("pipechannels") ||
              openConfig == QStringLiteral("pipesource") ||
              openConfig == QStringLiteral("pipedest") ||
              openConfig == QStringLiteral("pipetype") ||
              openConfig == QStringLiteral("chainmenu") ||
              openConfig == QStringLiteral("pipeparam")) {
            controller_->showShiftFace(false);
            controller_->setTab(1);
            controller_->routing()->setShiftPage(1);
            controller_->routing()->setPipeSource(0);
            controller_->routing()->setPipeDestination(2);
            if (!controller_->routing()->routeEnabled(0, 2)) {
              controller_->routing()->toggleRoute(0, 2);
            }
            if (controller_->routing()->pipeCount() == 0) {
              controller_->routing()->setAddTypeIndex(0);
              controller_->routing()->addPipe();
            }
            controller_->routing()->selectPipe(0);
          }
          if (openConfig == QStringLiteral("pipeparam")) {
            controller_->routing()->selectPipe(0);
            controller_->openPipeParam(0, 0, nullptr);
            return;
          }
          controller_->openConfig(openConfig);
        },
        QStringLiteral("config."), {},
        {QStringLiteral("config.cancel"), QStringLiteral("config.ok")},
        false, true});
    scenarios_.push_back(Scenario{
        QStringLiteral("config.%1.ok").arg(kind),
        [this, openConfig] {
          closeOverlays();
          if (openConfig == QStringLiteral("inputsource")) {
            controller_->setTab(0);
            primeMixerTracks();
            controller_->openInputSource(0);
            return;
          }
          if (openConfig == QStringLiteral("outputdest")) {
            controller_->setTab(0);
            primeMixerTracks();
            controller_->openOutputDest(0);
            return;
          }
          if (openConfig == QStringLiteral("fxslot")) {
            controller_->setTab(0);
            primeMixerTracks();
            controller_->openChannel(1);
            controller_->openFxSlot(0);
            return;
          }
          if (openConfig == QStringLiteral("addtrack")) {
            controller_->setTab(0);
            primeMixerTracks();
          }
          if (openConfig == QStringLiteral("pipechannels") ||
              openConfig == QStringLiteral("pipesource") ||
              openConfig == QStringLiteral("pipedest") ||
              openConfig == QStringLiteral("pipetype") ||
              openConfig == QStringLiteral("chainmenu") ||
              openConfig == QStringLiteral("pipeparam")) {
            controller_->showShiftFace(false);
            controller_->setTab(1);
            controller_->routing()->setShiftPage(1);
            controller_->routing()->setPipeSource(0);
            controller_->routing()->setPipeDestination(2);
            if (!controller_->routing()->routeEnabled(0, 2)) {
              controller_->routing()->toggleRoute(0, 2);
            }
            if (controller_->routing()->pipeCount() == 0) {
              controller_->routing()->setAddTypeIndex(0);
              controller_->routing()->addPipe();
            }
            controller_->routing()->selectPipe(0);
          }
          if (openConfig == QStringLiteral("pipeparam")) {
            controller_->routing()->selectPipe(0);
            controller_->openPipeParam(0, 0, nullptr);
            return;
          }
          controller_->openConfig(openConfig);
        },
        QStringLiteral("config."), {}, {QStringLiteral("config.ok")},
        true, true});
  }
}

void UiTapRunner::primeGeneratorChain() {
  auto* routing = controller_->routing();
  controller_->showShiftFace(true);
  controller_->setTab(1);
  routing->setShiftPage(1);
  int generator = -1;
  for (const auto& row : routing->inputNodes()) {
    if (row.toMap().value(QStringLiteral("port")).toInt() == midi::kSrcGenerator) {
      generator = row.toMap().value(QStringLiteral("nodeIndex")).toInt();
    }
  }
  int usbOut = -1;
  for (const auto& row : routing->outputNodes()) {
    if (row.toMap().value(QStringLiteral("port")).toInt() ==
        midi::portId(midi::Port::UsbOut)) {
      usbOut = row.toMap().value(QStringLiteral("nodeIndex")).toInt();
    }
  }
  if (generator < 0 || usbOut < 0) return;
  routing->setPipeSource(generator);
  routing->setPipeDestination(usbOut);
  while (routing->pipeCount() > 0) routing->removePipe(0);
  routing->setAddTypeIndex(0);
  routing->addPipe();
  routing->selectPipe(0);
}

void UiTapRunner::primePipeChain(int pipeIndex) {
  auto* routing = controller_->routing();
  controller_->showShiftFace(true);
  controller_->setTab(1);
  routing->setShiftPage(1);
  routing->setPipeSource(0);
  routing->setPipeDestination(2);
  if (!routing->routeEnabled(0, 2)) routing->toggleRoute(0, 2);
  while (routing->pipeCount() > 0) routing->removePipe(0);
  routing->setAddTypeIndex(0);  // FILTER
  routing->addPipe();
  routing->setAddTypeIndex(11);  // ARP
  routing->addPipe();
  routing->selectPipe(pipeIndex);
}

// Activates the first take of every track that has one. On the Pi the take
// folder holds the real session material, so the clip-click pass below runs on
// real multi-MB files instead of fixtures.
void UiTapRunner::activateRealTakes() {
  int firstWithTakes = -1;
  for (int track = 0; track < 8; ++track) {
    controller_->recorder()->setSelectedTrack(track);
    if (controller_->recorder()->takes()->count() == 0) continue;
    if (firstWithTakes < 0) firstWithTakes = track;
    controller_->recorder()->selectRow(0);
    controller_->recorder()->activateSelected();
  }
  // Leave the recorder on a track that HAS takes, so the row surfaces exist for
  // the clip-click pass (a seeded folder may only fill one track).
  controller_->recorder()->setSelectedTrack(firstWithTakes < 0 ? 0 : firstWithTakes);
  controller_->timeline()->setSelectedTrack(firstWithTakes < 0 ? 0 : firstWithTakes);
  controller_->timeline()->refresh();
}

void UiTapRunner::primeMixerTracks() {
  if (!controller_->mixer()->empty()) return;
  controller_->mixer()->clearClipDemo();
  const char* names[] = {"drums", "bass", "keys", "gtr", "vox", "perc"};
  for (int slot = 0; slot < 6; ++slot) {
    controller_->mixer()->addTrack(slot, QString::fromLatin1(names[slot]), slot,
                                   false, 0);
  }
  controller_->mixer()->setTrackMode(false);
  controller_->mixer()->setBank(0);
}

void UiTapRunner::clearMixerTracks() {
  while (controller_->mixer()->stripCount() > 0) {
    const QVariant slot =
        controller_->mixer()->data(controller_->mixer()->index(0, 0),
                                   MixerViewModel::SlotRole);
    controller_->mixer()->removeTrack(slot.toInt());
  }
}

void UiTapRunner::beginScenario() {
  if (scenarioIndex_ >= scenarios_.size()) {
    finish();
    return;
  }
  scenarioStarted_ = true;
  scenarioSawTarget_ = false;
  tapped_.clear();
  typingIndex_ = 0;
  sequenceIndex_ = 0;
  scenarios_[scenarioIndex_].setup();
  std::printf("ui-taps: -- %s\n", qPrintable(scenarios_[scenarioIndex_].name));
  scheduleNext(kScenarioDelayMs);
}

std::vector<UiTapRunner::Target> UiTapRunner::visibleTargets() const {
  std::vector<Target> targets;
  collectTargets(window_->contentItem(), targets);
  QSet<QString> seen;
  std::vector<Target> unique;
  unique.reserve(targets.size());
  for (const auto& target : targets) {
    if (seen.contains(target.focusId)) continue;
    seen.insert(target.focusId);
    unique.push_back(target);
  }
  std::sort(unique.begin(), unique.end(), [](const Target& a, const Target& b) {
    const QPointF left = a.item->mapToScene(QPointF(a.item->width() / 2.0,
                                                    a.item->height() / 2.0));
    const QPointF right = b.item->mapToScene(QPointF(b.item->width() / 2.0,
                                                     b.item->height() / 2.0));
    if (left.y() != right.y()) return left.y() < right.y();
    return left.x() < right.x();
  });
  return unique;
}

void UiTapRunner::collectTargets(QQuickItem* item, std::vector<Target>& targets) {
  if (item == nullptr || !item->isVisible() || item->width() <= 0.0 ||
      item->height() <= 0.0) {
    return;
  }
  const QVariant value = item->property("focusId");
  if (value.isValid()) {
    const QString id = value.toString();
    if (!id.isEmpty()) targets.push_back(Target{item, id});
  }
  for (QQuickItem* child : item->childItems()) {
    collectTargets(child, targets);
  }
}

void UiTapRunner::typingNext() {
  auto& scenario = scenarios_[scenarioIndex_];
  if (typingIndex_ >= static_cast<int>(scenario.typing.size())) {
    finishScenario();
    return;
  }
  const TypeStep& step =
      scenario.typing[static_cast<std::size_t>(typingIndex_)];
  QQuickItem* target = findFocusTarget(step.focusId);
  if (target == nullptr) {
    std::fprintf(stderr, "ui-taps: FAIL %s: no target %s\n",
                 qPrintable(scenario.name), qPrintable(step.focusId));
    ++failures_;
    finishScenario();
    return;
  }
  scenarioSawTarget_ = true;
  tapped_.push_back(step.focusId);
  ++taps_;
  std::printf("ui-taps:    type %s\n", qPrintable(step.focusId));
  if (step.longPress) {
    press(window_, target);
    QTimer::singleShot(700, this, [this, step] {
      QQuickItem* held = findFocusTarget(step.focusId);
      if (held != nullptr) release(window_, held);
      const QString buffer = keypadBuffer();
      std::printf("ui-taps:      hold -> \"%s\" (expected \"%s\")\n",
                  qPrintable(buffer), qPrintable(step.expectedBuffer));
      if (buffer != step.expectedBuffer) {
        std::fprintf(stderr, "ui-taps: FAIL %s: buffer \"%s\" != \"%s\"\n",
                     qPrintable(scenarios_[scenarioIndex_].name),
                     qPrintable(buffer), qPrintable(step.expectedBuffer));
        ++failures_;
      }
      ++typingIndex_;
      scheduleNext(kTapDelayMs);
    });
    return;
  }
  (void)tap(window_, target, 0);
  const QString buffer = keypadBuffer();
  std::printf("ui-taps:      buffer \"%s\" (expected \"%s\")\n",
              qPrintable(buffer), qPrintable(step.expectedBuffer));
  if (buffer != step.expectedBuffer) {
    std::fprintf(stderr, "ui-taps: FAIL %s: buffer \"%s\" != \"%s\"\n",
                 qPrintable(scenario.name), qPrintable(buffer),
                 qPrintable(step.expectedBuffer));
    ++failures_;
  }
  ++typingIndex_;
  scheduleNext(kTapDelayMs);
}

QQuickItem* UiTapRunner::findFocusTarget(const QString& focusId) const {
  for (const auto& target : visibleTargets()) {
    if (target.focusId == focusId) return target.item;
  }
  return nullptr;
}

QQuickItem* UiTapRunner::findObjectByName(QQuickItem* item, const QString& name) {
  if (item == nullptr) return nullptr;
  if (item->objectName() == name) return item;
  for (QQuickItem* child : item->childItems()) {
    if (QQuickItem* found = findObjectByName(child, name)) return found;
  }
  return nullptr;
}

QString UiTapRunner::keypadBuffer() const {
  QQuickItem* keypad = findObjectByName(window_->contentItem(),
                                        QStringLiteral("keypad"));
  if (keypad == nullptr) return QString();
  return keypad->property("buffer").toString();
}

void UiTapRunner::press(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr) return;
  const QPointF scene =
      item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
  if (touchTaps()) {
    sendTouch(window, item, true, scene);
    return;
  }
  const QPoint global = window->mapToGlobal(scene.toPoint());
  QMouseEvent event(QEvent::MouseButtonPress, scene, QPointF(global),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &event);
}

void UiTapRunner::release(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr) return;
  const QPointF scene =
      item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
  if (touchTaps()) {
    sendTouch(window, item, false, scene);
    return;
  }
  const QPoint global = window->mapToGlobal(scene.toPoint());
  QMouseEvent event(QEvent::MouseButtonRelease, scene, QPointF(global),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &event);
}

void UiTapRunner::tapNext() {
  if (!scenarioStarted_) {
    if (scenarioIndex_ >= scenarios_.size()) {
      finish();
      return;
    }
    beginScenario();
    return;
  }
  if (!scenarios_[scenarioIndex_].typing.empty()) {
    typingNext();
    return;
  }
  if (!scenarios_[scenarioIndex_].sequence.isEmpty()) {
    auto& scenario = scenarios_[scenarioIndex_];
    if (sequenceIndex_ >= scenario.sequence.size()) {
      finishScenario();
      return;
    }
    const QString id = scenario.sequence.at(sequenceIndex_);
    QQuickItem* target = findFocusTarget(id);
    if (target == nullptr) {
      std::fprintf(stderr, "ui-taps: FAIL %s: no target %s\n",
                   qPrintable(scenario.name), qPrintable(id));
      ++failures_;
      finishScenario();
      return;
    }
    scenarioSawTarget_ = true;
    ++sequenceIndex_;
    tapped_.push_back(id);
    ++taps_;
    std::printf("ui-taps:    tap %s\n", qPrintable(id));
    if (scenarios_[scenarioIndex_].topBand) {
      const QPointF centre =
          target->mapToScene(QPointF(target->width() / 2.0, target->height() / 2.0));
      std::printf("ui-taps:      centre y %.0f (top band <= 52)\n", centre.y());
      if (centre.y() > 52.0) {
        std::fprintf(stderr,
                     "ui-taps: FAIL %s: %s is not in the top band (y %.0f)\n",
                     qPrintable(scenarios_[scenarioIndex_].name), qPrintable(id),
                     centre.y());
        ++failures_;
      }
    }
    (void)tap(window_, target);
    scheduleNext(kTapDelayMs);
    return;
  }
  if (tapped_.size() > kMaxTapsPerScenario) {
    std::fprintf(stderr, "ui-taps: FAIL %s exceeded %d taps\n",
                 qPrintable(scenarios_[scenarioIndex_].name),
                 kMaxTapsPerScenario);
    ++failures_;
    finishScenario();
    return;
  }

  const auto& scenario = scenarios_[scenarioIndex_];
  const auto targets = visibleTargets();
  std::vector<Target> candidates;
  for (const auto& target : targets) {
    if (tapped_.contains(target.focusId)) continue;
    if (!scenario.allowedPrefix.isEmpty() &&
        !target.focusId.startsWith(scenario.allowedPrefix)) {
      continue;
    }
    if (startsWithAny(target.focusId, scenario.excludedPrefixes)) continue;
    candidates.push_back(target);
  }
  if (!candidates.empty()) scenarioSawTarget_ = true;

  if (scenario.onlyTerminal) {
    for (const auto& terminal : scenario.terminalIds) {
      for (const auto& target : candidates) {
        if (target.focusId != terminal) continue;
        tapped_.push_back(target.focusId);
        ++taps_;
        std::printf("ui-taps:    tap %s\n", qPrintable(target.focusId));
        (void)tap(window_, target.item, scenario.dragTerminal ? 24 : 0);
        scheduleNext(kTapDelayMs);
        return;
      }
    }
    finishScenario();
    return;
  }

  bool hasNonTerminal = false;
  for (const auto& target : candidates) {
    if (!scenario.terminalIds.contains(target.focusId)) {
      hasNonTerminal = true;
      break;
    }
  }
  if (!hasNonTerminal && !scenario.terminalIds.isEmpty()) {
    for (const auto& terminal : scenario.terminalIds) {
      for (const auto& target : candidates) {
        if (target.focusId != terminal) continue;
        tapped_.push_back(target.focusId);
        ++taps_;
        std::printf("ui-taps:    tap %s\n", qPrintable(target.focusId));
        (void)tap(window_, target.item, scenario.dragTerminal ? 24 : 0);
        scheduleNext(kTapDelayMs);
        return;
      }
    }
    finishScenario();
    return;
  }

  for (const auto& target : candidates) {
    if (scenario.terminalIds.contains(target.focusId)) continue;
    tapped_.push_back(target.focusId);
    ++taps_;
    std::printf("ui-taps:    tap %s\n", qPrintable(target.focusId));
    (void)tap(window_, target.item);
    scheduleNext(kTapDelayMs);
    return;
  }
  finishScenario();
}

void UiTapRunner::finishScenario() {
  const auto& scenario = scenarios_[scenarioIndex_];
  if (scenario.expectTargets && !scenarioSawTarget_) {
    std::fprintf(stderr, "ui-taps: FAIL %s: no focusId'd control found\n",
                 qPrintable(scenario.name));
    ++failures_;
  }
  // The assertion runs while the surface the taps produced is still open:
  // closeOverlays() below tears every popup down before the next scenario.
  if (scenario.verify && !scenario.verify()) {
    std::fprintf(stderr, "ui-taps: FAIL %s: %s\n", qPrintable(scenario.name),
                 qPrintable(scenario.verifyFailure));
    ++failures_;
  }
  std::printf("ui-taps: %s: %d tap(s)\n", qPrintable(scenario.name),
              static_cast<int>(tapped_.size()));
  scenarioStarted_ = false;
  ++scenarioIndex_;
  closeOverlays();
  scheduleNext(kScenarioDelayMs);
}

void UiTapRunner::startPanelGate() {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  previousHandler_ = qInstallMessageHandler(&UiTapRunner::messageHandler);
  active_ = this;
  std::printf("panel-taps: window %dx%d, in-process Intents (offscreen)\n",
              window_->width(), window_->height());
  QTimer::singleShot(500, this, &UiTapRunner::runPanelGate);
}

// --panel-taps: the mechanical panel-intent gate (docs/panel-parity-audit.md
// section 5). It injects the same Intents the Pico sends, so a stop that no
// widget implements, a ring that skips a visible control or an EXIT that does
// not unwind fails here instead of on the owner's bench.
void UiTapRunner::runPanelGate() {
  int failures = 0;
  int stops = 0;
  QStringList rings;
  const auto fail = [&](const QString& context, const QString& what) {
    ++failures;
    std::fprintf(stderr, "panel-taps: FAIL %s: %s\n", qPrintable(context),
                 qPrintable(what));
  };
  const auto intent = [this](Intent::Kind kind, int amount = 0) {
    Intent value;
    value.kind = kind;
    value.amount = amount;
    controller_->perform(value);
  };
  // A busy wait, not an event loop: the 66 ms link poll carries shift=false
  // and would release the synthetic SHIFT press mid-hold.
  const auto waitMs = [](int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  };
  const auto visibleIds = [this] {
    QSet<QString> ids;
    for (const auto& target : visibleTargets()) ids.insert(target.focusId);
    return ids;
  };
  // Later redesign stages own these: mixer strip selectors, the channel
  // monitor row, the EDIT value chip, the EQ SHIFT shadow ids and the MIDI
  // port rows. They are reported as known gaps, not failures, so this gate
  // locks in stage-1 parity without pretending to fix stage 2/5 work.
  const QStringList knownGapPrefixes = {
      QStringLiteral("edit.value"), QStringLiteral("shift.eq.band."),
      // MIDI MONITOR rows: the ListView virtualizes off-viewport delegates, so
      // the ring can name a row that has no live widget until it is scrolled
      // into view. The tap path still walks the ring and scrolls on focus.
      QStringLiteral("midimon.port."),
      // Stage 3/4/5 screens own these; the audit already lists them (EQ node
      // visibility, ARRANGE toolbar, recorder punch rows, SYSTEM summaries).
      QStringLiteral("eq.node."), QStringLiteral("eq.subject.track"),
      QStringLiteral("arrange.addsection"), QStringLiteral("arrange.dupsection"),
      QStringLiteral("arrange.loopsection"), QStringLiteral("arrange.length"),
      QStringLiteral("arrange.grid."), QStringLiteral("shift.routing.reset"),
      QStringLiteral("shift.punch."), QStringLiteral("shift.monitor.summary.")};
  const auto isKnownGap = [&](const QString& id) {
    for (const auto& prefix : knownGapPrefixes) {
      if (id.startsWith(prefix)) return true;
    }
    return false;
  };
  std::printf("panel-taps: no-pico=%d, %d track(s) seeded\n",
              controller_->noPicoMode() ? 1 : 0, controller_->mixer()->stripCount());

  // The take/timeline fixtures live in buildScenarios(); reusing it here keeps
  // the recorder take rows and the project rows non-empty without duplicating
  // the WAV writer.
  buildScenarios();
  primeMixerTracks();

  struct Context {
    QString name;
    std::function<void()> setup;
    // The page grid is four columns wide; NAV is a flat ring walk, so RIGHT
    // visits every stop of the list the DB9 sends.
    Intent::Kind walkKind = Intent::Kind::NavDown;
  };
  std::vector<Context> contexts;
  const auto reset = [this] {
    closeOverlays();
    controller_->showShiftFace(false);
  };
  for (int tab = 0; tab < kScreenCount; ++tab) {
    Context context;
    context.name = QStringLiteral("tab.%1").arg(tab);
    context.setup = [this, reset, tab] {
      reset();
      controller_->setTab(tab);
      if (tab == 0) {
        controller_->mixer()->setTrackMode(false);
        controller_->mixer()->setBank(0);
        controller_->mixer()->setSelectedStrip(0);
      }
    };
    contexts.push_back(context);
  }
  const auto faceReset = [this, reset](int tab) {
    reset();
    controller_->setTab(tab);
    if (tab == 0) {
      controller_->mixer()->setTrackMode(false);
      controller_->mixer()->setBank(0);
    }
    controller_->showShiftFace(true);
  };
  for (int tab = 0; tab < kScreenCount; ++tab) {
    if (!controller_->tabHasShiftFace(tab)) continue;
    Context context;
    context.name = QStringLiteral("shift.%1").arg(tab);
    context.setup = [faceReset, tab] { faceReset(tab); };
    contexts.push_back(context);
  }
  {
    Context context;
    context.name = QStringLiteral("tabgrid");
    context.setup = [this, reset] {
      reset();
      controller_->setTab(0);
      controller_->openTabGrid();
    };
    context.walkKind = Intent::Kind::NavRight;
    contexts.push_back(context);
  }
  {
    Context context;
    context.name = QStringLiteral("tab.0.track");
    context.setup = [this, reset] {
      reset();
      controller_->setTab(0);
      controller_->mixer()->setTrackMode(true);
      controller_->mixer()->setBank(0);
    };
    contexts.push_back(context);
  }
  {
    Context context;
    context.name = QStringLiteral("shift.0.track");
    context.setup = [this, faceReset] {
      faceReset(0);
      controller_->mixer()->setTrackMode(true);
      controller_->mixer()->setBank(0);
    };
    contexts.push_back(context);
  }
  {
    Context context;
    context.name = QStringLiteral("bankgrid");
    context.setup = [this, reset] {
      reset();
      controller_->setTab(0);
      controller_->mixer()->setTrackMode(false);
      controller_->mixer()->setBank(0);
      controller_->openBankGrid();
    };
    context.walkKind = Intent::Kind::NavRight;
    contexts.push_back(context);
  }
  {
    Context context;
    context.name = QStringLiteral("tempo");
    context.setup = [this, reset] {
      reset();
      controller_->setTab(0);
      controller_->openTempoPopup();
    };
    contexts.push_back(context);
  }
  {
    Context context;
    context.name = QStringLiteral("config.metronome");
    context.setup = [this, reset] {
      reset();
      controller_->setTab(0);
      controller_->openConfig(QStringLiteral("metronome"));
    };
    contexts.push_back(context);
  }
  // G7: the keypad is no longer a dead modal. Its keys, cursor, edit verbs and
  // the two exits are ring stops, so the whole NAME/RENAME/IMPORT path can be
  // typed from the panel; the encoder still walks the same ring.
  {
    Context context;
    context.name = QStringLiteral("keypad");
    // The keypad ring is a linear list over its keys; its NAV is 2D (NavDown
    // jumps a keyboard row = +10). The generic ring-truth loop walks the list,
    // so it must use the linear keys (NavRight = +1) like every other context.
    context.walkKind = Intent::Kind::NavRight;
    context.setup = [this, reset] {
      reset();
      controller_->setTab(0);
      controller_->requestRenameTrack(0);
    };
    contexts.push_back(context);
  }

  // 1-2. Ring truth: non-empty, no duplicates, every stop has a visible widget.
  // 3. Ring walk: NavDown n+1 times visits every stop once and wraps.
  for (const auto& context : contexts) {
    context.setup();
    const std::vector<QString> ids = controller_->focusIds_;
    const QString& name = context.name;
    if (ids.empty()) {
      // Stage 5: every face ring must be non-empty. This used to allowlist
      // G2 (Projects) as a known gap; the mapping is fixed, so an empty face
      // ring is a hard failure now.
      fail(name, QStringLiteral("empty face ring"));
      continue;
    }
    QSet<QString> unique;
    const QSet<QString> visible = visibleIds();
    for (const auto& id : ids) {
      if (id.isEmpty()) fail(name, QStringLiteral("empty focus id"));
      if (unique.contains(id)) fail(name, QStringLiteral("duplicate %1").arg(id));
      unique.insert(id);
      if (!visible.contains(id) && !isKnownGap(id)) {
        fail(name, QStringLiteral("ring id %1 has no visible widget").arg(id));
      }
    }
    rings.push_back(name + QStringLiteral(": ") + QStringList(ids.begin(), ids.end()).join(QStringLiteral(", ")));
    // Walk from index 0 and record what NAV reports.
    controller_->focusIndex_ = 0;
    controller_->focusId_ = ids.front();
    QStringList walked;
    const int count = static_cast<int>(ids.size());
    walked.push_back(ids.front());
    for (int step = 0; step < count; ++step) {
      intent(context.walkKind);
      walked.push_back(controller_->focusId_);
    }
    QSet<QString> seen;
    for (int step = 0; step < count; ++step) {
      if (seen.contains(walked[step])) {
        fail(name, QStringLiteral("ring revisits %1 before the wrap").arg(walked[step]));
      }
      seen.insert(walked[step]);
    }
    if (walked.at(1 % count) != ids.at(1 % count)) {
      fail(name, QStringLiteral("step 1 gave %1, expected %2")
                      .arg(walked.at(1 % count), ids.at(1 % count)));
    }
    if (walked.back() != ids.front()) {
      fail(name, QStringLiteral("ring did not wrap to %1 (got %2)")
                      .arg(ids.front(), walked.back()));
    }
    // 4. PRESS every stop once from a fresh setup. A crash fails the process;
    // visibility/activation contracts are asserted for the shell below.
    for (int index = 0; index < count; ++index) {
      context.setup();
      if (static_cast<int>(controller_->focusIds_.size()) != count) continue;
      controller_->focusIndex_ = index;
      controller_->focusId_ = ids.at(index);
      intent(Intent::Kind::NavPress);
      ++stops;
    }
  }

  // 4b. Shell activation contract: each shell stop performs its documented
  // action (the touch gate proves the same handlers).
  const auto shellSetup = [this, reset] {
    reset();
    controller_->setTab(0);
  };
  const auto focusAndPress = [this, intent, &fail](const QString& id) {
    const auto& ids = controller_->focusIds_;
    const auto found = std::find(ids.begin(), ids.end(), id);
    if (found == ids.end()) {
      fail(QStringLiteral("shell.press"), QStringLiteral("%1 not in ring").arg(id));
      return;
    }
    controller_->focusIndex_ = static_cast<int>(std::distance(ids.begin(), found));
    controller_->focusId_ = id;
    intent(Intent::Kind::NavPress);
  };
  shellSetup();
  focusAndPress(QStringLiteral("nav.recorder"));
  if (controller_->tab() != 2) fail(QStringLiteral("shell.press"), QStringLiteral("nav.recorder did not select tab 2"));
  shellSetup();
  focusAndPress(QStringLiteral("nav.arrange"));
  if (controller_->tab() != 8) fail(QStringLiteral("shell.press"), QStringLiteral("nav.arrange did not select tab 8"));
  shellSetup();
  focusAndPress(QStringLiteral("nav.more"));
  if (!controller_->tabGridOpen()) fail(QStringLiteral("shell.press"), QStringLiteral("nav.more did not open the page grid"));
  shellSetup();
  focusAndPress(QStringLiteral("transport.tempo"));
  if (!controller_->tempoPopupOpen()) fail(QStringLiteral("shell.press"), QStringLiteral("transport.tempo did not open the tempo popup"));
  shellSetup();
  focusAndPress(QStringLiteral("transport.shift"));
  if (controller_->panelMode() != QStringLiteral("shift")) fail(QStringLiteral("shell.press"), QStringLiteral("transport.shift did not enter the SHIFT face"));
  shellSetup();
  focusAndPress(QStringLiteral("transport.recmode"));
  if (!controller_->config()->open()) fail(QStringLiteral("shell.press"), QStringLiteral("transport.recmode did not open RECMODE"));
  shellSetup();
  const bool songBefore = controller_->songMode();
  focusAndPress(QStringLiteral("transport.song"));
  if (controller_->songMode() == songBefore) fail(QStringLiteral("shell.press"), QStringLiteral("transport.song did not toggle SONG"));
  shellSetup();

  // 5. Encoder: the focused value moves by one detent and comes back.
  reset();
  controller_->setTab(0);
  controller_->openTempoPopup();
  const int tempoBefore = static_cast<int>(controller_->tempoBpm());
  intent(Intent::Kind::EncoderTurn, 1);
  const int tempoUp = static_cast<int>(controller_->tempoBpm());
  intent(Intent::Kind::EncoderTurn, -1);
  const int tempoBack = static_cast<int>(controller_->tempoBpm());
  if (tempoUp != tempoBefore + 1) fail(QStringLiteral("encoder"), QStringLiteral("EncoderTurn(+1) did not step the focused tempo"));
  if (tempoBack != tempoBefore) fail(QStringLiteral("encoder"), QStringLiteral("EncoderTurn(-1) did not restore the tempo"));

  // 6. SHIFT: a 600 ms held frame enters the face iff the tab has one; EXIT
  // leaves it and never leaves a face tab with an empty ring.
  for (int tab = 0; tab < kScreenCount; ++tab) {
    reset();
    controller_->setTab(tab);
    LinkStatus held;
    held.shift = true;
    controller_->updateShiftState(held);
    waitMs(600);
    controller_->updateShiftState(held);
    const bool entered = controller_->panelMode() == QStringLiteral("shift");
    const bool expected = controller_->tabHasShiftFace(tab);
    if (entered != expected) {
      fail(QStringLiteral("shift.%1").arg(tab),
           QStringLiteral("face %1, expected %2").arg(entered ? 1 : 0).arg(expected ? 1 : 0));
    }
    if (entered && controller_->focusIds_.empty()) {
      // Stage 5: Projects (tab 4) is fixed too; every entered face needs a ring.
      fail(QStringLiteral("shift.%1").arg(tab), QStringLiteral("empty face ring"));
    }
    LinkStatus released;
    controller_->updateShiftState(released);
    if (controller_->panelMode() == QStringLiteral("shift")) {
      fail(QStringLiteral("shift.%1").arg(tab), QStringLiteral("EXIT left the face open"));
    }
  }

  // 7. EXIT unwinds the overlays stage 1 owns (stage 2 adds channel/TRACK FX/
  // bank grid, where the audit already documents the gaps).
  const auto exitCloses = [this, &fail](const QString& name, const std::function<void()>& open,
                                        const std::function<bool()>& closed) {
    closeOverlays();
    controller_->setTab(0);
    open();
    panelExitPress();
    if (!closed()) fail(QStringLiteral("exit.%1").arg(name), QStringLiteral("EXIT did not close it"));
  };
  exitCloses(QStringLiteral("tabgrid"), [this] { controller_->openTabGrid(); },
             [this] { return !controller_->tabGridOpen(); });
  exitCloses(QStringLiteral("tempo"), [this] { controller_->openTempoPopup(); },
             [this] { return !controller_->tempoPopupOpen(); });
  exitCloses(QStringLiteral("config"),
             [this] { controller_->openConfig(QStringLiteral("metronome")); },
             [this] { return !controller_->config()->open(); });
  exitCloses(QStringLiteral("keypad"),
             [this] { controller_->requestKeypad(QStringLiteral("T"), QString(), QStringLiteral("test"), 0); },
             [this] { return !controller_->keypadOpen(); });
  exitCloses(QStringLiteral("confirm"),
             [this] { controller_->requestConfirm(QStringLiteral("T"), QStringLiteral("M"), QStringLiteral("OK"), QStringLiteral("test"), 0); },
             [this] { return !controller_->confirmOpen(); });
  // The KnobChip precision sheet is the topmost layer while it is up, so EXIT
  // must spend its press on it (G9 for the touch-only sheet).
  exitCloses(QStringLiteral("knobsheet"),
             [this] { controller_->setKnobSheetOpen(true); },
             [this] { return !controller_->knobSheetOpen(); });

  // 7b. G9: opening an overlay saves the invoking stop and closing it restores
  // that stop, not index 0. Each branch opens from a real ring stop.
  const auto focusReturns = [this, &fail](const QString& name, int tab,
                                          const QString& invoking,
                                          const std::function<void()>& open,
                                          const std::function<void()>& close) {
    closeOverlays();
    controller_->setTab(tab);
    const auto& ids = controller_->focusIds_;
    const auto found = std::find(ids.begin(), ids.end(), invoking);
    if (found == ids.end()) {
      fail(QStringLiteral("focus.%1").arg(name),
           QStringLiteral("%1 not in the ring").arg(invoking));
      return;
    }
    controller_->focusIndex_ = static_cast<int>(std::distance(ids.begin(), found));
    controller_->focusId_ = invoking;
    open();
    close();
    if (controller_->focusId() != invoking) {
      fail(QStringLiteral("focus.%1").arg(name),
           QStringLiteral("close left %1, expected %2")
               .arg(controller_->focusId(), invoking));
    }
  };
  focusReturns(QStringLiteral("tempo"), 0, QStringLiteral("transport.tempo"),
               [this] { controller_->openTempoPopup(); },
               [this] { controller_->closeTempoPopup(); });
  focusReturns(QStringLiteral("tabgrid"), 0, QStringLiteral("nav.more"),
               [this] { controller_->openTabGrid(); },
               [this] { controller_->closeTabGrid(); });
  focusReturns(QStringLiteral("config"), 0, QStringLiteral("transport.recmode"),
               [this] { controller_->openConfig(QStringLiteral("metronome")); },
               [this] { controller_->configCancel(); });
  focusReturns(QStringLiteral("keypad"), 0, QStringLiteral("transport.recmode"),
               [this] { controller_->requestRenameTrack(0); },
               [this] { controller_->keypadCancel(); });
  focusReturns(QStringLiteral("channel"), 0, QStringLiteral("mixer.expand.0"),
               [this] { controller_->openChannel(0); },
               [this] { controller_->closeChannel(); });
  focusReturns(QStringLiteral("trackpicker"), 2, QStringLiteral("recorder.track.pick"),
               [this] { controller_->openTrackPicker(); },
               [this] { controller_->closeTrackPicker(); });
  focusReturns(QStringLiteral("takesheet"), 2, QStringLiteral("recorder.takesheet"),
               [this] { controller_->openTakeSheet(0); },
               [this] { controller_->closeTakeSheet(); });

  // 8. Three-band shell reconciliation: every visible control in the nav and
  // transport bands is a ring stop (or an explicit touch duplicate).
  closeOverlays();
  controller_->setTab(0);
  const QSet<QString> shellRing(controller_->focusIds_.begin(), controller_->focusIds_.end());
  const QSet<QString> visible = visibleIds();
  for (const auto& id : visible) {
    if (!(id.startsWith(QStringLiteral("nav.")) ||
          id.startsWith(QStringLiteral("transport.")))) {
      continue;
    }
    if (shellRing.contains(id)) continue;
    // The drawn transport glyphs stay tappable on hardware, where the panel
    // owns the same verbs; the chip is touch-only.
    if (!controller_->noPicoMode() &&
        (id == QStringLiteral("transport.play") || id == QStringLiteral("transport.stop") ||
         id == QStringLiteral("transport.rec"))) {
      continue;
    }
    fail(QStringLiteral("shell.visible"),
         QStringLiteral("%1 is visible but not in the shell ring").arg(id));
  }

  // 9. Report.
  const int warnings = static_cast<int>(warnings_.size());
  std::printf("panel-taps: %d context(s), %d stop(s), %d failure(s), %d QML warning(s)\n",
              static_cast<int>(contexts.size()), stops, failures, warnings);
  for (const auto& ring : rings) std::printf("panel-taps: ring %s\n", qPrintable(ring));
  for (const auto& warning : warnings_) std::printf("panel-taps: QML: %s\n", qPrintable(warning));
  QCoreApplication::exit((failures == 0 && warnings == 0) ? 0 : 1);
}

void UiTapRunner::finish() {
  std::printf("ui-taps: %d tap(s), %d QML warning(s), %d failure(s)\n", taps_,
              static_cast<int>(warnings_.size()), failures_);
  for (const auto& warning : warnings_) {
    std::printf("ui-taps: QML: %s\n", qPrintable(warning));
  }
  QCoreApplication::exit((failures_ == 0 && warnings_.isEmpty()) ? 0 : 1);
}

bool UiTapRunner::tap(QQuickWindow* window, QQuickItem* item, int dragPixels) {
  if (window == nullptr || item == nullptr) return false;
  const QPointF scene =
      item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
  if (scene.x() < 0.0 || scene.y() < 0.0 || scene.x() >= window->width() ||
      scene.y() >= window->height()) {
    return false;
  }
  if (touchTaps()) {
    sendTouch(window, item, true, scene);
    if (dragPixels != 0) {
      const QPointF end = scene + QPointF(dragPixels, 0);
      const QPoint endGlobal = window->mapToGlobal(end.toPoint());
      QMouseEvent seed(QEvent::MouseMove, end, QPointF(endGlobal), Qt::NoButton,
                       Qt::LeftButton, Qt::NoModifier, &tapTouchDevice());
      QTouchEvent move(QEvent::TouchUpdate, &tapTouchDevice(), Qt::NoModifier,
                       {seed.point(0)});
      QCoreApplication::sendEvent(window, &move);
    }
    sendTouch(window, item, false, scene + QPointF(dragPixels, 0));
    return true;
  }
  const QPoint global = window->mapToGlobal(scene.toPoint());
  QMouseEvent press(QEvent::MouseButtonPress, scene, QPointF(global),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &press);
  QPointF end = scene;
  if (dragPixels != 0) {
    // One move event is enough: the drag handlers clamp the beat and repaint on
    // the first position change.
    end = scene + QPointF(dragPixels, 0);
    const QPoint moveGlobal = window->mapToGlobal(end.toPoint());
    QMouseEvent move(QEvent::MouseMove, end, QPointF(moveGlobal), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &move);
  }
  const QPoint endGlobal = window->mapToGlobal(end.toPoint());
  QMouseEvent release(QEvent::MouseButtonRelease, end, QPointF(endGlobal),
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(window, &release);
  return true;
}

bool UiTapRunner::isQmlWarning(const QMessageLogContext& context,
                               const QString& message) {
  const QString file = context.file == nullptr ? QString() : QString::fromUtf8(context.file);
  if (file.endsWith(QStringLiteral(".qml"))) return true;
  if (context.category != nullptr &&
      QByteArray(context.category) == QByteArray("qml")) {
    return true;
  }
  return message.contains(QStringLiteral("TypeError")) ||
         message.contains(QStringLiteral("ReferenceError")) ||
         message.contains(QStringLiteral("is not a function")) ||
         message.contains(QStringLiteral("non-existent property")) ||
         message.contains(QStringLiteral("Unable to assign")) ||
         message.contains(QStringLiteral("Cannot assign")) ||
         message.contains(QStringLiteral("QML ")) ||
         message.contains(QStringLiteral("QML:")) ||
         message.contains(QStringLiteral("qml:"));
}

void UiTapRunner::recordQmlWarning(const QMessageLogContext& context,
                                   const QString& message) {
  const QString file = context.file == nullptr ? QString() : QString::fromUtf8(context.file);
  QString line = file;
  if (!line.isEmpty() && context.line > 0) line += QLatin1Char(':') + QString::number(context.line);
  if (!line.isEmpty()) line += QStringLiteral(": ");
  line += message;
  warnings_.push_back(line);
  std::fprintf(stderr, "ui-taps: QML WARNING %s\n", qPrintable(line));
}

void UiTapRunner::messageHandler(QtMsgType type,
                                 const QMessageLogContext& context,
                                 const QString& message) {
  UiTapRunner* runner = active_;
  const bool qml = isQmlWarning(context, message);
  if (runner != nullptr && qml &&
      (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)) {
    runner->recordQmlWarning(context, message);
  }
  if (runner != nullptr && runner->previousHandler_ != nullptr) {
    runner->previousHandler_(type, context, message);
    return;
  }
  const QByteArray text = message.toLocal8Bit();
  std::fprintf(stderr, "%s\n", text.constData());
}

}  // namespace mnky::app
