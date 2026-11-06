#include "ui_tap_runner.hpp"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <cstdio>
#include <utility>

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
  controller_->closeTrackFx();
  controller_->confirmCancel();
  controller_->keypadCancel();
  controller_->closeTempoPopup();
  controller_->closeTabGrid();
  controller_->closeBankGrid();
  controller_->closeTakeSheet();
  controller_->closeTakeEdit();
  controller_->hideValuePopup();
  controller_->setFullscreen(false);
  if (controller_->recording()) controller_->recToggle();
  controller_->stop();
}

void UiTapRunner::buildScenarios() {
  // 1. The tab bar itself: this is the only QML path that calls setTab().
  scenarios_.push_back(Scenario{
      QStringLiteral("tabs"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
      },
      QStringLiteral("tab."), {}, {}, false, true});

  // 2. Shared transport strip.
  scenarios_.push_back(Scenario{
      QStringLiteral("transport"),
      [this] {
        closeOverlays();
        controller_->showShiftFace(false);
        controller_->setTab(0);
      },
      QStringLiteral("transport."), {}, {}, false, true});

  const QStringList scaffoldExcluded = {
      QStringLiteral("tab."), QStringLiteral("transport."),
      QStringLiteral("config."), QStringLiteral("keypad."),
      QStringLiteral("confirm."), QStringLiteral("shift.")};
  const QStringList shiftExcluded = {
      QStringLiteral("tab."), QStringLiteral("transport."),
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
        {QStringLiteral("take.sheet.close")}, {rowId}, false, false});
  }
  // The take sheet's EDIT column: a tap opens the edit popup for that row.
  for (int row = 0; row < 4; ++row) {
    const QString editId = QStringLiteral("take.sheet.edit.%1").arg(row);
    scenarios_.push_back(Scenario{
        QStringLiteral("clips.takesheet.edit.%1").arg(row), openTakeSheetNow,
        editId,
        {QStringLiteral("take.sheet.close"), QStringLiteral("take.sheet.row.")},
        {editId}, false, false});
  }

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

void UiTapRunner::tapNext() {
  if (!scenarioStarted_) {
    if (scenarioIndex_ >= scenarios_.size()) {
      finish();
      return;
    }
    beginScenario();
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
  std::printf("ui-taps: %s: %d tap(s)\n", qPrintable(scenario.name),
              static_cast<int>(tapped_.size()));
  scenarioStarted_ = false;
  ++scenarioIndex_;
  closeOverlays();
  scheduleNext(kScenarioDelayMs);
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
