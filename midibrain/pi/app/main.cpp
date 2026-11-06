// mnkybox - Qt 6 QML touchscreen DAW application (Pi 5 + Pico link).
//
// Flags:
//   --emulator          use the emulator backends (scripted Pico SPI link,
//                       generated audio) instead of the Linux hardware paths.
//   --no-pico           bare Pi 5, no Pico attached: LinkClient is built
//                       without an SPI transport, PicoLink or READY GPIO (no
//                       handshake, no reconnect spam). The Pi free-runs the USB
//                       clock and owns the transport; USB audio/MIDI, recorder,
//                       takes, mixer, FX, EQ, X/Y and projects stay live. TRS
//                       MIDI/clock, panel controls and LEDs are absent because
//                       that hardware is on the Pico. Independent of
//                       --emulator: use both for a deterministic desktop run.
//   --unittests         run the non-visual C++ checks and exit; no window, no
//                       audio device, no MIDI port is opened.
//   --screenshots DIR   headless proof: load every QML screen, grab the window
//                       and write 30-qt-*.png into DIR, then exit. Implies the
//                       emulator backends like --emulator. Combine with
//                       QT_QPA_PLATFORM=offscreen (the guest/macOS proof runs).
//   --screenshots-live DIR
//                       the same grab run but against the real audio backend
//                       (used for the PipeWire/Pulse evidence run).
//   --ui-taps           headless QML tap driver: load every screen and SHIFT
//                       face, synthesize a real mouse press/release at every
//                       visible control carrying a focusId, and fail on any QML
//                       TypeError/warning. Implies --emulator and forces the
//                       offscreen platform. HOME is a temporary directory, so
//                       project/take taps cannot touch real user data.
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app_controller.hpp"
#include "logging.hpp"
#include "ui_tap_runner.hpp"

namespace mnky::app {
int runLogicUnitTests();
}

#ifndef MNKY_APP_VERSION
#define MNKY_APP_VERSION "0.1.0"
#endif

namespace {

constexpr const char* kInstanceLockName = "mnkybox-ui.lock";

QString argumentValue(const QStringList& arguments, const QString& name) {
  const int index = arguments.indexOf(name);
  if (index < 0 || index + 1 >= arguments.size()) return {};
  return arguments.at(index + 1);
}

class ScreenshotRunner final : public QObject {
 public:
  ScreenshotRunner(mnky::app::AppController* controller, QQuickWindow* window,
                   const QString& directory, QObject* parent = nullptr)
      : QObject(parent),
        controller_(controller),
        window_(window),
        directory_(directory) {
    steps_.push_back({"31-mixer", [this] {
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->mixer()->clearClipDemo();
                        controller_->mixer()->setMonitor(0, 0);
                        controller_->mixer()->setMonitor(1, 1);
                        controller_->mixer()->setMonitor(2, 2);
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->recorder()->selectRow(0);
                        controller_->recorder()->activateSelected();
                        controller_->setTab(0);
                      }});
    steps_.push_back({"31-clip", [this] {
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->mixer()->showClipDemo();
                        controller_->setTab(0);
                      }});
    steps_.push_back({"31-track", [this] {
                        controller_->mixer()->clearClipDemo();
                        controller_->mixer()->setTrackMode(true);
                        controller_->mixer()->setTrackIndex(2);
                        controller_->setTab(0);
                      }});
    steps_.push_back({"31-takes", [this] {
                        controller_->mixer()->setTrackMode(false);
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->recorder()->selectRow(0);
                        controller_->recorder()->activateSelected();
                        controller_->setTab(3);
                      }});
    steps_.push_back({"31-punch", [this] {
                        controller_->session()->setPunchPlayFromEnabled(true);
                        controller_->session()->setPunchPlayFromMeasure(1);
                        controller_->session()->setPunchInEnabled(true);
                        controller_->session()->setPunchInMeasure(5);
                        controller_->session()->setPunchOutEnabled(true);
                        controller_->session()->setPunchOutMeasure(9);
                        controller_->setToolsPage(0);
                        controller_->setTab(6);
                      }});
    steps_.push_back({"31-monitor", [this] {
                        controller_->session()->setClickMode(2);
                        controller_->session()->setClickOutput(1);
                        controller_->session()->setClickGainDb(-9.0);
                        controller_->session()->setPhonesSrc(0);
                        controller_->session()->setCueStyle(1);
                        controller_->session()->setOut34Mode(0);
                        controller_->session()->setCompOn(true);
                        controller_->session()->setCompThreshDb(-18.0);
                        controller_->session()->setCompRatio(4.0);
                        controller_->session()->setCompAttackMs(10.0);
                        controller_->session()->setCompReleaseMs(120.0);
                        controller_->session()->setCompMakeupDb(3.0);
                        controller_->session()->setCompMode(1);
                        controller_->setToolsPage(1);
                        controller_->setTab(6);
                      }});
    steps_.push_back({"31-tools", [this] {
                        controller_->setToolsPage(2);
                        controller_->tools()->showLowSpaceDemo();
                        controller_->setTab(6);
                      }});
    steps_.push_back({"31-keypad", [this] {
                        controller_->setTab(3);
                        controller_->requestImportTake();
                      }});
    steps_.push_back({"31-value-popup", [this] {
                        controller_->keypadCancel();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->mixer()->setPan(1, 0.51);
                        // The tab first: AppController::setTab() hides a readout
                        // left over from the previous screen.
                        controller_->setTab(0);
                        controller_->showValuePopup(QStringLiteral("IN 02 PAN"),
                                                    QStringLiteral("pan"), 0.5);
                      }});
    steps_.push_back({"32-loop", [this] {
                        controller_->keypadCancel();
                        controller_->confirmCancel();
                        controller_->hideValuePopup();
                        controller_->showShiftFace(true);
                        controller_->recorder()->setSelectedTrack(2);
                        controller_->recorder()->setLoopEnabled(true);
                        controller_->recorder()->setLoopStartBeats(1.0);
                        controller_->recorder()->setLoopEndBeats(5.0);
                        controller_->setTab(3);
                      }});
    steps_.push_back({"32-io", [this] {
                        controller_->hideValuePopup();
                        controller_->showShiftFace(true);
                        controller_->mixer()->setTrackMode(true);
                        controller_->mixer()->setTrackIndex(2);
                        controller_->mixer()->setTrackInCh(4);
                        controller_->mixer()->setTrackInPair(true);
                        controller_->mixer()->setTrackOutDst(1);
                        controller_->setTab(0);
                      }});
    steps_.push_back({"32-clock", [this] {
                        controller_->hideValuePopup();
                        controller_->showShiftFace(true);
                        controller_->session()->setTrs1Clock(true);
                        controller_->session()->setTrs1Transport(false);
                        controller_->session()->setTrs2Clock(false);
                        controller_->session()->setTrs2Transport(true);
                        controller_->session()->setUsbClock(true);
                        controller_->session()->setUsbTransport(true);
                        controller_->setTab(1);
                      }});
    steps_.push_back({"32-monitor", [this] {
                        controller_->hideValuePopup();
                        controller_->showShiftFace(false);
                        // Real events through the live path: PLAY mirrors a USB
                        // START/clock bytes, PANIC emits CC123 on every output.
                        controller_->play();
                        controller_->shiftAction(1);
                        controller_->midiMonitor()->setSelectedPort(3);
                        controller_->midiMonitor()->setHideClock(true);
                        controller_->setTab(6);
                      }});
    steps_.push_back({"33-eq", [this] {
                        controller_->hideValuePopup();
                        controller_->showShiftFace(false);
                        controller_->eq()->selectMain();
                        controller_->eq()->selectSubject(2);
                        // Band 1 low shelf, band 2 notch, band 3 presence,
                        // band 4 air; band 3 selected (pink).
                        eqBand(0, 2, 120.0, 4.0, 0.8);
                        eqBand(1, 3, 500.0, -3.0, 2.5);
                        eqBand(2, 3, 3000.0, 2.5, 1.5);
                        eqBand(3, 4, 9000.0, 3.0, 0.8);
                        controller_->eq()->selectBand(2);
                        controller_->hideValuePopup();
                        controller_->setTab(7);
                      }});
    steps_.push_back({"33-eq-popup", [this] {
                        // Transient readout on a Q change (Hz/dB/Q popups).
                        controller_->eq()->selectBand(1);
                        controller_->eq()->adjustQ(0.5);
                        controller_->setTab(7);
                      }});
    steps_.push_back({"33-eq-full", [this] {
                        controller_->hideValuePopup();
                        controller_->eq()->selectBand(2);
                        controller_->setFullscreen(true);
                        controller_->setTab(7);
                        controller_->hideValuePopup();
                      }});
    steps_.push_back({"33-xy", [this] {
                        controller_->hideValuePopup();
                        controller_->setFullscreen(false);
                        // Two fingers, one per grid, held down together: the
                        // TouchPoint ids (41, 42) are the QML point ids.
                        controller_->xy()->touchBegin(0, 41, 0.18, 0.72);
                        controller_->xy()->touchBegin(1, 42, 0.62, 0.35);
                        controller_->hideValuePopup();
                        controller_->setTab(8);
                      }});
    steps_.push_back({"33-xy-params", [this] {
                        // Secondary parameters of the reverb grid live in the
                        // shared ConfigPopup (draft + OK), not on the grid.
                        controller_->hideValuePopup();
                        controller_->setTab(8);
                        controller_->openConfig(QStringLiteral("reverb"));
                      }});
    steps_.push_back({"33-xy-freeze", [this] {
                        controller_->configCancel();
                        controller_->xy()->toggleFreeze();
                        controller_->setTab(8);
                      }});
    steps_.push_back({"33-eq-type", [this] {
                        controller_->setFullscreen(false);
                        controller_->eq()->selectBand(2);
                        controller_->setTab(7);
                        controller_->openConfig(QStringLiteral("eqtype"));
                      }});
    steps_.push_back({"33-eq-shift", [this] {
                        // SHIFT face: the band sliders and the six type buttons
                        // (the panel reaches it with a 500 ms EXIT long-hold).
                        controller_->configCancel();
                        controller_->hideValuePopup();
                        controller_->showShiftFace(true);
                        controller_->setTab(7);
                      }});
    steps_.push_back({"34-edit", [this] {
                        controller_->configCancel();
                        controller_->hideValuePopup();
                        controller_->showShiftFace(false);
                        // Adopt the first take of the sample tracks (the guest
                        // take directory has ch00/ch01 files). Tracks without a
                        // take keep the empty-lane placeholder.
                        for (int track = 0; track < 6; ++track) {
                          controller_->recorder()->setSelectedTrack(track);
                          controller_->recorder()->selectRow(0);
                          controller_->recorder()->activateSelected();
                        }
                        controller_->timeline()->refresh();
                        // A real trim/split/gain chain so the lane shows ordered
                        // region blocks and the undo stack has content.
                        controller_->timeline()->setSelectedTrack(1);
                        controller_->timeline()->beginSelection(1, 1.0);
                        controller_->timeline()->updateSelection(17.0);
                        controller_->timeline()->endSelection();
                        (void)controller_->timeline()->applyOp(0, 0.0, 0.0, 0.0);
                        controller_->timeline()->setCursorBeats(9.0);
                        (void)controller_->timeline()->applyOp(1, 0.0, 0.0, 0.0);
                        controller_->timeline()->beginSelection(1, 1.0);
                        controller_->timeline()->updateSelection(5.0);
                        controller_->timeline()->endSelection();
                        (void)controller_->timeline()->applyOp(2, -3.0, 0.0, 0.0);
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->recorder()->setLoopEnabled(true);
                        controller_->recorder()->setLoopStartBeats(1.0);
                        controller_->recorder()->setLoopEndBeats(5.0);
                        controller_->session()->setPunchInEnabled(true);
                        controller_->session()->setPunchInMeasure(3);
                        controller_->session()->setPunchOutEnabled(true);
                        controller_->session()->setPunchOutMeasure(7);
                        controller_->timeline()->preparePeaksForDemo();
                        controller_->timeline()->setPage(0);
                        controller_->timeline()->setSelectedTrack(1);
                        controller_->timeline()->setCursorBeats(3.0);
                        controller_->setTab(9);
                      }});
    steps_.push_back({"34-ops", [this] {
                        controller_->hideValuePopup();
                        controller_->setTab(9);
                        controller_->openConfig(QStringLiteral("timelineops"));
                      }});
    steps_.push_back({"34-pipes", [this] {
                        controller_->configCancel();
                        controller_->hideValuePopup();
                        controller_->showShiftFace(true);
                        controller_->routing()->setShiftPage(1);
                        // A TRS->TRS route is the one the Pico evaluates: the app
                        // mirrors the chain with 0x18/0x19 and sends its bytes
                        // with 0x1A. Both waves are live on this route.
                        controller_->routing()->setPipeSource(0);       // TRS IN 1
                        controller_->routing()->setPipeDestination(0);  // TRS OUT 1
                        controller_->routing()->toggleRoute(0, 0);      // route ON
                        controller_->routing()->setAddTypeIndex(0);     // Filter
                        controller_->routing()->addPipe();
                        controller_->routing()->selectPipe(0);
                        controller_->routing()->setPipeParam(0, 2, 1);
                        controller_->routing()->setAddTypeIndex(11);    // Arp (Wave 2)
                        controller_->routing()->addPipe();
                        controller_->routing()->selectPipe(0);
                        controller_->setTab(1);
                      }});
    steps_.push_back({"34-route", [this] {
                        // One TRS IN1 note through the piped TRS route; the link
                        // thread must send it with 0x1A and the Monitor screen
                        // records the TRS OUT send.
                        controller_->injectDemoTrsMidi(0, 0x90, 60, 100);
                      }});
    steps_.push_back({"34-monitor", [this] {
                        controller_->setTab(6);
                      }});
    if (controller_->noPicoMode()) {
      // 35-*: bare-Pi proof. The project is the same as 31-*; only the
      // persistent NO PICO badge, the Pi free-running clock and the System
      // banner (live vs physically absent subsystems) differ.
      steps_.push_back({"35-bench-mixer", [this] {
                          controller_->hideValuePopup();
                          controller_->configCancel();
                          controller_->keypadCancel();
                          controller_->confirmCancel();
                          controller_->showShiftFace(false);
                          controller_->mixer()->setTrackMode(false);
                          controller_->mixer()->setBank(0);
                          controller_->mixer()->clearClipDemo();
                          controller_->selectStrip(0);
                          controller_->session()->setUsbClock(true);
                          controller_->session()->setUsbTransport(true);
                          controller_->play();
                          controller_->setTab(0);
                        }});
      steps_.push_back({"35-bench-system", [this] {
                          controller_->hideValuePopup();
                          controller_->play();
                          controller_->setTab(4);
                        }});
    }

    // 36-*: touch-era UI fixes. The value readout sits in the top band (never
    // under the finger), every mixer strip carries its own input source, the
    // System screen selects the device output pairs and the device lists show
    // name + backend + channel count, and the Routing primary face names the
    // SHIFT PIPES face.
    steps_.push_back({"36-popup-top", [this] {
                        controller_->hideValuePopup();
                        controller_->configCancel();
                        controller_->keypadCancel();
                        controller_->confirmCancel();
                        controller_->setFullscreen(false);
                        controller_->showShiftFace(false);
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->mixer()->clearClipDemo();
                        controller_->selectStrip(1);
                        // The tab first: AppController::setTab() hides a readout
                        // left over from the previous screen.
                        controller_->setTab(0);
                        // A finger on the strip area (window-Y 300): the readout
                        // must stay in the top band, not under the touch point.
                        controller_->setTouchAnchorY(300.0);
                        controller_->showValuePopup(QStringLiteral("IN 02 GAIN"),
                                                    QStringLiteral("db"), -6.0);
                      }});
    steps_.push_back({"36-popup-flip", [this] {
                        // Finger inside the top band (the fullscreen EQ grid
                        // reaches y = 0): the readout moves below the finger.
                        controller_->hideValuePopup();
                        controller_->setTab(0);
                        controller_->setTouchAnchorY(20.0);
                        controller_->showValuePopup(QStringLiteral("BAND 2 GAIN"),
                                                    QStringLiteral("db"), 3.0);
                      }});
    steps_.push_back({"36-mixer-input", [this] {
                        controller_->hideValuePopup();
                        controller_->setTouchAnchorY(-1.0);
                        controller_->showShiftFace(false);
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        // Distinct sources on the perform surface: a mono pick,
                        // NONE and a stereo pair, without the TRACK SHIFT face.
                        // The selector is a cycle, so step to the wanted label.
                        const auto driveTo = [this](int strip, const QString& want) {
                          for (int step = 0; step < 32; ++step) {
                            if (controller_->mixer()->inSourceText(strip) == want) return;
                            (void)controller_->mixer()->cycleInSource(strip, 1);
                          }
                        };
                        driveTo(1, QStringLiteral("IN 03"));
                        driveTo(2, QStringLiteral("NONE"));
                        driveTo(3, QStringLiteral("IN 07/08"));
                        controller_->selectStrip(1);
                        controller_->setTab(0);
                      }});
    steps_.push_back({"36-masterout", [this] {
                        // Device output pairs: Main 1&2 -> 3&4, OUT3&4 -> 5&6,
                        // phones -> 7&8. The selector shows the three model
                        // columns (name / backend / channel count); the shot
                        // picks the generator row because this offscreen run
                        // has no audio device to enumerate.
                        controller_->hideValuePopup();
                        controller_->session()->setDeviceOutMain(1);
                        controller_->session()->setDeviceOutOut34(2);
                        controller_->session()->setDeviceOutPhones(3);
                        controller_->system()->setBackendIndex(1);
                        controller_->system()->setInputIndex(0);
                        controller_->setTab(4);
                      }});
    steps_.push_back({"36-routing-hint", [this] {
                        controller_->hideValuePopup();
                        // Close the device list the previous step opened.
                        if (QObject* list = window_->findChild<QObject*>(
                                QStringLiteral("systemOutputList"))) {
                          QObject* popup = list->property("popup").value<QObject*>();
                          if (popup != nullptr) QMetaObject::invokeMethod(popup, "close");
                        }
                        controller_->showShiftFace(false);
                        controller_->setTab(1);
                      }});
    steps_.push_back({"36-device-list", [this] {
                        // The open device list: one row per device with the three
                        // model columns (name / backend / channel count).
                        controller_->hideValuePopup();
                        controller_->setTab(4);
                        if (QObject* list = window_->findChild<QObject*>(
                                QStringLiteral("systemOutputList"))) {
                          QObject* popup = list->property("popup").value<QObject*>();
                          if (popup != nullptr) QMetaObject::invokeMethod(popup, "open");
                        }
                      }});
    steps_.push_back({"36-device-out-list", [this] {
                        // The MAIN 1&2 pair picker open: the 16 pairs (1&2 ..
                        // 31&32) on a 480 px panel, so the popup has to scroll.
                        controller_->hideValuePopup();
                        if (QObject* list = window_->findChild<QObject*>(
                                QStringLiteral("systemOutputList"))) {
                          QObject* popup = list->property("popup").value<QObject*>();
                          if (popup != nullptr) QMetaObject::invokeMethod(popup, "close");
                        }
                        controller_->setTab(4);
                        if (QObject* list = window_->findChild<QObject*>(
                                QStringLiteral("systemDevoutList"))) {
                          QObject* popup = list->property("popup").value<QObject*>();
                          if (popup != nullptr) QMetaObject::invokeMethod(popup, "open");
                        }
                      }});
    steps_.push_back({"38-monitor4", [this] {
                        controller_->hideValuePopup();
                        controller_->configCancel();
                        controller_->keypadCancel();
                        controller_->confirmCancel();
                        controller_->setFullscreen(false);
                        controller_->showShiftFace(false);
                        controller_->mixer()->setTrackMode(true);
                        controller_->mixer()->setTrackIndex(2);
                        controller_->mixer()->setMonitor(2, 3);  // MON
                        controller_->mixer()->setMonitor(1, 2);  // BOTH
                        controller_->setTab(0);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"38-routing-devices", [this] {
                        controller_->hideValuePopup();
                        controller_->showShiftFace(false);
                        controller_->routing()->setSelectedInput(2);
                        controller_->setTab(1);
                      }});
    steps_.push_back({"38-pipechmask", [this] {
                        controller_->hideValuePopup();
                        controller_->showShiftFace(true);
                        controller_->routing()->setShiftPage(1);
                        controller_->routing()->setPipeSource(0);
                        controller_->routing()->setPipeDestination(2);
                        if (controller_->routing()->pipeCount() == 0) {
                          controller_->routing()->setAddTypeIndex(0);
                          controller_->routing()->addPipe();
                        }
                        controller_->routing()->selectPipe(0);
                        controller_->routing()->setSelectedPipeChannelMask(0x000F);
                        controller_->setTab(1);
                        controller_->openConfig(QStringLiteral("pipechannels"));
                      }});

    // ---- 39-*: wave-2 refresh set, every screen and both faces -----------
    steps_.push_back({"39-mixer", [this] {
                        resetShot();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->mixer()->clearClipDemo();
                        controller_->mixer()->setMonitor(0, 0);
                        controller_->mixer()->setMonitor(1, 1);
                        controller_->mixer()->setMonitor(2, 2);
                        controller_->selectStrip(1);
                        controller_->setTab(0);
                      }});
    steps_.push_back({"39-mixer-shift", [this] {
                        resetShot();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(0);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-mixer-clip", [this] {
                        resetShot();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->mixer()->showClipDemo();
                        controller_->setTab(0);
                      }});
    steps_.push_back({"39-track", [this] {
                        resetShot();
                        controller_->mixer()->clearClipDemo();
                        controller_->mixer()->setTrackMode(true);
                        controller_->mixer()->setTrackIndex(2);
                        controller_->setTab(0);
                      }});
    steps_.push_back({"39-track-shift", [this] {
                        resetShot();
                        controller_->mixer()->setTrackMode(true);
                        controller_->mixer()->setTrackIndex(2);
                        controller_->mixer()->setMonitor(2, 1);
                        controller_->setTab(0);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-routing", [this] {
                        resetShot();
                        controller_->routing()->setSelectedInput(-1);
                        controller_->setTab(1);
                      }});
    steps_.push_back({"39-routing-filter", [this] {
                        resetShot();
                        controller_->setTab(1);
                        controller_->showShiftFace(true);
                        controller_->routing()->setShiftPage(0);
                      }});
    steps_.push_back({"39-routing-pipes", [this] {
                        resetShot();
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
                        controller_->routing()->setSelectedPipeChannelMask(0x000F);
                      }});
    steps_.push_back({"39-fx", [this] {
                        resetShot();
                        controller_->setTab(2);
                      }});
    steps_.push_back({"39-fx-shift", [this] {
                        resetShot();
                        controller_->setTab(2);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-recorder", [this] {
                        resetShot();
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->recorder()->selectRow(0);
                        controller_->recorder()->activateSelected();
                        controller_->setTab(3);
                      }});
    steps_.push_back({"39-recorder-shift", [this] {
                        resetShot();
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->setTab(3);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-system", [this] {
                        resetShot();
                        controller_->setTab(4);
                      }});
    steps_.push_back({"39-system-monitor", [this] {
                        resetShot();
                        controller_->setTab(4);
                        controller_->setSystemShiftPage(0);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-system-learn", [this] {
                        resetShot();
                        controller_->setTab(4);
                        controller_->setSystemShiftPage(1);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-system-tools", [this] {
                        resetShot();
                        controller_->setTab(4);
                        controller_->setSystemShiftPage(2);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-projects", [this] {
                        resetShot();
                        controller_->setTab(5);
                      }});
    steps_.push_back({"39-projects-shift", [this] {
                        resetShot();
                        controller_->setTab(5);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-monitor", [this] {
                        resetShot();
                        controller_->midiMonitor()->setHideClock(false);
                        controller_->setTab(6);
                      }});
    steps_.push_back({"39-eq", [this] {
                        resetShot();
                        controller_->eq()->selectMain();
                        eqBand(0, 2, 120.0, 4.0, 1.0);
                        eqBand(1, 1, 80.0, 0.0, 1.0);
                        eqBand(2, 3, 1000.0, -3.0, 1.4);
                        eqBand(3, 4, 8000.0, 2.0, 1.0);
                        controller_->eq()->selectBand(2);
                        controller_->setTab(7);
                        controller_->hideValuePopup();
                      }});
    steps_.push_back({"39-eq-shift", [this] {
                        resetShot();
                        controller_->eq()->selectMain();
                        controller_->eq()->selectBand(2);
                        controller_->eq()->selectType(3);
                        controller_->setTab(7);
                        controller_->showShiftFace(true);
                        controller_->hideValuePopup();
                      }});
    steps_.push_back({"39-eq-full", [this] {
                        resetShot();
                        controller_->setTab(7);
                        controller_->setFullscreen(true);
                      }});
    steps_.push_back({"39-xy", [this] {
                        resetShot();
                        controller_->setTab(8);
                      }});
    steps_.push_back({"39-timeline", [this] {
                        resetShot();
                        for (int track = 0; track < 6; ++track) {
                          controller_->recorder()->setSelectedTrack(track);
                          controller_->recorder()->selectRow(0);
                          controller_->recorder()->activateSelected();
                        }
                        controller_->timeline()->refresh();
                        controller_->timeline()->preparePeaksForDemo();
                        controller_->timeline()->setSelectedTrack(1);
                        controller_->timeline()->setPage(0);
                        controller_->timeline()->setCursorBeats(3.0);
                        controller_->setTab(9);
                      }});
    steps_.push_back({"39-popup-comp", [this] {
                        resetShot();
                        controller_->setTab(4);
                        controller_->setSystemShiftPage(0);
                        controller_->showShiftFace(true);
                        controller_->openConfig(QStringLiteral("comp"));
                      }});
    steps_.push_back({"39-popup-trackmonitor", [this] {
                        resetShot();
                        controller_->mixer()->setTrackMode(true);
                        controller_->mixer()->setTrackIndex(2);
                        controller_->setTab(0);
                        controller_->showShiftFace(true);
                        controller_->openConfig(QStringLiteral("trackmonitor"));
                      }});
    steps_.push_back({"39-popup-pipechmask", [this] {
                        resetShot();
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
                        controller_->routing()->setSelectedPipeChannelMask(0x000F);
                        controller_->openConfig(QStringLiteral("pipechannels"));
                      }});
    steps_.push_back({"39-value-popup", [this] {
                        resetShot();
                        controller_->mixer()->setTrackMode(false);
                        controller_->setTab(0);
                        controller_->setTouchAnchorY(300.0);
                        controller_->showValuePopup(QStringLiteral("IN 02 GAIN"),
                                                    QStringLiteral("db"), -6.0);
                      }});
    steps_.push_back({"39-popup-sends", [this] {
                        resetShot();
                        controller_->setTab(4);
                        controller_->setSystemShiftPage(0);
                        controller_->showShiftFace(true);
                        controller_->openConfig(QStringLiteral("sends"));
                      }});
    steps_.push_back({"39-popup-eqtype", [this] {
                        resetShot();
                        controller_->eq()->selectMain();
                        controller_->eq()->selectBand(2);
                        controller_->setTab(7);
                        controller_->openConfig(QStringLiteral("eqtype"));
                      }});
    steps_.push_back({"39-popup-timelineops", [this] {
                        resetShot();
                        controller_->timeline()->setSelectedTrack(1);
                        controller_->timeline()->beginSelection(1, 1.0);
                        controller_->timeline()->updateSelection(5.0);
                        controller_->timeline()->endSelection();
                        controller_->setTab(9);
                        controller_->openConfig(QStringLiteral("timelineops"));
                      }});
    steps_.push_back({"39-transport", [this] {
                        resetShot();
                        controller_->mixer()->setTrackMode(false);
                        controller_->setTab(0);
                      }});

    // ---- 40-*: schema-driven pipe editors, chain presets, popup sizing ----
    steps_.push_back({"40-routing-pipes", [this] {
                        resetShot();
                        primePipeChain(false);
                        controller_->routing()->selectPipe(0);
                        controller_->routing()->setParamPage(0);
                      }});
    steps_.push_back({"40-routing-pipes-knob", [this] {
                        resetShot();
                        primePipeChain(true);
                        // The ARP pipe's page 2 is OCTAVES (stepper) and SWING
                        // (arc knob with a "%" popup): generated editors in one
                        // row, no per-type QML.
                        controller_->routing()->selectPipe(1);
                        controller_->routing()->setParamPage(1);
                        controller_->showTextPopup(
                            QStringLiteral("SWING"),
                            controller_->routing()->pipeParamValueText(3, 50));
                      }});
    steps_.push_back({"40-chain-export", [this] {
                        resetShot();
                        primePipeChain(true);
                        controller_->requestExportPipeChain();
                      }});
    steps_.push_back({"40-chain-import", [this] {
                        resetShot();
                        primePipeChain(true);
                        controller_->requestImportPipeChain();
                      }});
    steps_.push_back({"40-chain-imported", [this] {
                        resetShot();
                        primePipeChain(true);
                        // Real round trip: export the route's chain to a temp
                        // file, replace it with a different chain, then import
                        // the file back. The rows below are the imported ones.
                        const QString file = QDir::tempPath() +
                                             QStringLiteral("/mnkybox-40-chain.json");
                        controller_->routing()->exportPipeChain(file, QString());
                        controller_->routing()->setAddTypeIndex(15);
                        controller_->routing()->addPipe();
                        controller_->routing()->importPipeChain(file, QString());
                        controller_->routing()->selectPipe(1);
                        controller_->showTextPopup(
                            QStringLiteral("PIPE CHAIN IMPORTED"),
                            QStringLiteral("mnkybox-40-chain.json"));
                      }});
    steps_.push_back({"40-popup-comp", [this] {
                        resetShot();
                        controller_->setTab(4);
                        controller_->setSystemShiftPage(0);
                        controller_->showShiftFace(true);
                        controller_->openConfig(QStringLiteral("comp"));
                      }});

    // ---- 40-*: app UX wave - dynamic mixer, selectors, FX, popout --------
    steps_.push_back({"40-mixer-empty", [this] {
                        resetShot();
                        clearMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->setTab(0);
                      }, 1100});
    steps_.push_back({"40-mixer-add-track", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->setTab(0);
                        controller_->requestAddTrack();
                      }});
    steps_.push_back({"40-mixer-bank", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->mixer()->showClipDemo();
                        controller_->mixer()->setMonitor(0, 0);
                        controller_->mixer()->setMonitor(1, 1);
                        controller_->mixer()->setMonitor(2, 2);
                        controller_->mixer()->setMonitor(3, 3);
                        controller_->selectStrip(1);
                        controller_->setTab(0);
                      }});
    steps_.push_back({"40-fx-builtin", [this] {
                        resetShot();
                        controller_->setTab(2);
                      }});
    steps_.push_back({"40-channel-popout", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->clearClipDemo();
                        controller_->setTab(0);
                        controller_->openChannel(2);
                      }});
    steps_.push_back({"40-popup-input", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->setTab(0);
                        controller_->openInputSource(1);
                      }});
    steps_.push_back({"40-mixer-capacity", [this] {
                        resetShot();
                        primeMixerDemo();
                        // Fill every track slot the engine supports: the page
                        // readout proves the banks follow the list, not 9 or 18.
                        for (int slot = 0; slot < mnky::audio::kMaxTracks; ++slot) {
                          if (controller_->mixer()->trackSlotActive(slot)) continue;
                          controller_->mixer()->addTrack(
                              slot, QStringLiteral("t%1").arg(slot + 1, 2, 10, QLatin1Char('0')),
                              slot % mnky::audio::kMaxDeviceInputs, false, 0);
                        }
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->selectStrip(0);
                        controller_->setTab(0);
                        controller_->showTextPopup(
                            QStringLiteral("CAPACITY"),
                            QStringLiteral("%1 TRACKS / %2 PAGES")
                                .arg(controller_->mixer()->stripCount())
                                .arg(controller_->mixer()->pageCount()));
                      }});
    steps_.push_back({"40-popup-insert", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->setTab(0);
                        // Opened from the channel popout's own strip, exactly as
                        // the popout's INSERT selector does.
                        controller_->openChannel(2);
                        controller_->openInsert(2);
                      }});
    // The mixer screens above show strips; the app's own default stays empty.
    primeMixerDemo();
  }

  // Screenshot fixture: the app starts with an empty mixer by owner contract,
  // so the screen-proof run seeds a small demo track list once. The empty-mixer
  // shot clears it again and the next step re-primes.
  void primeMixerDemo() {
    controller_->mixer()->clearClipDemo();
    if (!controller_->mixer()->empty()) return;
    const char* names[] = {"drums", "bass", "keys", "gtr",
                           "vox",   "perc", "pad",  "fx"};
    for (int slot = 0; slot < 8; ++slot) {
      controller_->mixer()->addTrack(slot, QString::fromLatin1(names[slot]), slot,
                                     false, 0);
    }
    controller_->mixer()->setTrackIndex(2);
    controller_->selectStrip(1);
  }

  void clearMixerDemo() {
    while (controller_->mixer()->stripCount() > 0) {
      const QVariant slot =
          controller_->mixer()->data(controller_->mixer()->index(0, 0),
                                     mnky::app::MixerViewModel::SlotRole);
      controller_->mixer()->removeTrack(slot.toInt());
    }
    // No strip feeds the master any more; clear the decaying meter tails so the
    // empty-mixer shot reads as silence instead of the previous take's level.
    controller_->mixer()->setMeters(0.0, 0.0);
    controller_->mixer()->setStripMeters(
        std::array<double, mnky::audio::kMaxTracks>{});
  }

  // Common reset before every 39-* refresh shot: no stale overlay, no
  // fullscreen bridge, primary face.
  void resetShot() {
    controller_->hideValuePopup();
    controller_->closeChannel();
    controller_->configCancel();
    controller_->keypadCancel();
    controller_->confirmCancel();
    controller_->setFullscreen(false);
    controller_->showShiftFace(false);
  }

  // Routing SHIFT > PIPES fixture for the 40-* set: one route, a FILTER plus an
  // ARP pipe so the page shows a bool row set and a knob/stepper page.
  void primePipeChain(bool withArp) {
    controller_->setTab(1);
    controller_->showShiftFace(true);
    controller_->routing()->setShiftPage(1);
    controller_->routing()->setPipeSource(0);
    controller_->routing()->setPipeDestination(2);
    if (!controller_->routing()->routeEnabled(0, 2)) {
      controller_->routing()->toggleRoute(0, 2);
    }
    while (controller_->routing()->pipeCount() > 0) {
      controller_->routing()->removePipe(0);
    }
    controller_->routing()->setAddTypeIndex(0);  // FILTER (schema id 1)
    controller_->routing()->addPipe();
    if (withArp) {
      controller_->routing()->setAddTypeIndex(11);  // ARP (schema id 12)
      controller_->routing()->addPipe();
    }
    controller_->routing()->selectPipe(0);
    controller_->routing()->setSelectedPipeChannelMask(0x000F);
  }

  // Scripted absolute EQ band values: type 0..5, frequency, gain, Q.
  void eqBand(int band, int type, double freqHz, double gainDb, double q) {
    controller_->eq()->selectBand(band);
    controller_->eq()->selectType(type);
    controller_->eq()->adjustFreq(std::log2(freqHz / 1000.0));
    if (type == 2 || type == 3 || type == 4) {
      controller_->eq()->adjustGain(gainDb);
    }
    controller_->eq()->adjustQ(q - 1.0);
  }

  void start() {
    if (!QDir().mkpath(directory_)) {
      std::fprintf(stderr, "screenshots: cannot create %s\n", qPrintable(directory_));
      QCoreApplication::exit(1);
      return;
    }
    QTimer::singleShot(500, this, &ScreenshotRunner::next);
  }

 private:
  struct Step {
    QString name;
    std::function<void()> apply;
    // Settle time before the grab. The default matches the long-standing
    // 180 ms; a step that removes every strip asks for longer so the master
    // meter tails (a display decay, not audio) fall before the grab.
    int settleMs = 180;
  };

  void next() {
    if (index_ > 0) {
      const auto& completed = steps_[static_cast<std::size_t>(index_ - 1)];
      const QImage image = window_->grabWindow();
      if (image.isNull()) {
        std::fprintf(stderr, "screenshots: grabWindow returned no image for %s\n",
                     qPrintable(completed.name));
        failures_.push_back(completed.name);
      } else {
        const QString path = directory_ + QLatin1Char('/') + completed.name + ".png";
        if (!image.save(path, "PNG")) {
          std::fprintf(stderr, "screenshots: cannot save %s\n", qPrintable(path));
          failures_.push_back(completed.name);
        } else {
          std::printf("screenshots: %s (%dx%d)\n", qPrintable(path), image.width(),
                      image.height());
        }
      }
    }
    if (index_ >= static_cast<int>(steps_.size())) {
      std::printf("screenshots: %d step(s), %d failure(s)\n",
                  static_cast<int>(steps_.size()), static_cast<int>(failures_.size()));
      QCoreApplication::exit(failures_.empty() ? 0 : 1);
      return;
    }
    const auto& step = steps_[static_cast<std::size_t>(index_)];
    step.apply();
    controller_->system()->refresh();
    window_->update();
    ++index_;
    QTimer::singleShot(step.settleMs, this, &ScreenshotRunner::next);
  }

  mnky::app::AppController* controller_;
  QQuickWindow* window_;
  QString directory_;
  std::vector<Step> steps_;
  int index_ = 0;
  std::vector<QString> failures_;
};

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication::setOrganizationName(QStringLiteral("mnkybox"));
  QCoreApplication::setApplicationName(QStringLiteral("mnkybox"));
  QStringList arguments;
  for (int i = 1; i < argc; ++i) arguments << QString::fromLocal8Bit(argv[i]);

  if (arguments.contains(QStringLiteral("--unittests"))) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("mnkybox"));
    QCoreApplication::setApplicationName(QStringLiteral("mnkybox"));
    return mnky::app::runLogicUnitTests();
  }

  // Quick Controls' native styles cannot be customized; the dark theme needs
  // the plain Basic style on every platform.
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  // Qt's PulseAudio backend names the stream "QtPulseAudio:<pid>". The product
  // contract is that the app is visible under its own name in PipeWire graph
  // tools, so override only when the caller has not chosen a name.
  if (!qEnvironmentVariableIsSet("PULSE_PROP_OVERRIDE")) {
    qputenv("PULSE_PROP_OVERRIDE", "application.name=mnkybox");
  }

  const bool liveScreenshots = arguments.contains(QStringLiteral("--screenshots-live"));
  const bool wantsScreenshots =
      arguments.contains(QStringLiteral("--screenshots")) || liveScreenshots;
  const bool wantsTaps = arguments.contains(QStringLiteral("--ui-taps"));
  if (wantsScreenshots || wantsTaps) {
    // A headless proof must not depend on a GPU: the software renderer works
    // under QT_QPA_PLATFORM=offscreen on both the Mac and the Linux guest.
    qputenv("QT_QUICK_BACKEND", "software");
  }
  if (wantsTaps) {
    // The tap driver is headless by contract: never attach to a real desktop
    // (and never touch the owner's mouse) even if the caller exported a
    // platform plugin for another tool.
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  // A tap run presses PROJECTS SAVE, recorder START and the import keypad. Point
  // HOME at a disposable directory before the app reads the take/project paths,
  // so the runner can never overwrite the user's real projects or takes.
  std::unique_ptr<QTemporaryDir> tapHome;
  if (wantsTaps) {
    tapHome = std::make_unique<QTemporaryDir>();
    if (tapHome->isValid()) {
      qputenv("HOME", tapHome->path().toLocal8Bit());
    } else {
      std::fprintf(stderr, "mnkybox: --ui-taps needs a temporary HOME\n");
      return 2;
    }
  }
  QGuiApplication app(argc, argv);
  QCoreApplication::setApplicationVersion(QStringLiteral(MNKY_APP_VERSION));

  mnky::app::AppLogger::instance();
  const bool emulator = arguments.contains(QStringLiteral("--emulator")) ||
                        (wantsScreenshots && !liveScreenshots) || wantsTaps;
  const bool noPico = arguments.contains(QStringLiteral("--no-pico"));

  std::unique_ptr<QLockFile> instanceLock;
  if (!wantsScreenshots && !wantsTaps) {
    // One mnkybox per machine: the app owns the take directory, the audio
    // device and the single Pico SPI link. Keep the lock for the whole run.
    const QString lockPath =
        QDir::tempPath() + QLatin1Char('/') + QLatin1String(kInstanceLockName);
    instanceLock = std::make_unique<QLockFile>(lockPath);
    instanceLock->setStaleLockTime(0);
    if (!instanceLock->tryLock(0)) {
      mnky::app::log(std::string("app: another mnkybox instance owns ") +
                     lockPath.toStdString() + "; exiting");
      return 0;
    }
  }

  mnky::app::log(std::string("app: backends ") +
                 (emulator ? "emulator (--emulator)" : "hardware (Linux SPI/Qt audio)"));
  if (noPico) {
    mnky::app::log("app: --no-pico selected: no Pico, no SPI transport; Pi clock master");
  }

  // The QML Style singleton is a QML file so the theme lives with the UI; it
  // carries the contrast floors from the old theme.hpp comments as comments and
  // via logic_tests.cpp.
  qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/Style.qml")), "Mnkybox", 1, 0,
                           "Style");

  mnky::app::AppController controller(emulator, noPico);
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
  engine.rootContext()->setContextProperty(QStringLiteral("mixer"), controller.mixer());
  engine.rootContext()->setContextProperty(QStringLiteral("routing"), controller.routing());
  engine.rootContext()->setContextProperty(QStringLiteral("fx"), controller.fx());
  engine.rootContext()->setContextProperty(QStringLiteral("recorder"), controller.recorder());
  engine.rootContext()->setContextProperty(QStringLiteral("projects"), controller.projects());
  engine.rootContext()->setContextProperty(QStringLiteral("system"), controller.system());
  engine.rootContext()->setContextProperty(QStringLiteral("learn"), controller.learn());
  engine.rootContext()->setContextProperty(QStringLiteral("session"), controller.session());
  engine.rootContext()->setContextProperty(QStringLiteral("midimon"), controller.midiMonitor());
  engine.rootContext()->setContextProperty(QStringLiteral("tools"), controller.tools());
  engine.rootContext()->setContextProperty(QStringLiteral("config"), controller.config());
  engine.rootContext()->setContextProperty(QStringLiteral("eq"), controller.eq());
  engine.rootContext()->setContextProperty(QStringLiteral("xy"), controller.xy());
  engine.rootContext()->setContextProperty(QStringLiteral("timeline"), controller.timeline());

  controller.start();
  engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
  if (engine.rootObjects().isEmpty()) {
    std::fprintf(stderr, "mnkybox: QML root object failed to load\n");
    return 1;
  }

  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
  if (window == nullptr) {
    std::fprintf(stderr, "mnkybox: QML root object is not a Window\n");
    return 1;
  }
  window->setWidth(800);
  window->setHeight(480);

  std::unique_ptr<mnky::app::UiTapRunner> taps;
  if (wantsTaps) {
    if (wantsScreenshots) {
      std::fprintf(stderr, "mnkybox: --ui-taps and --screenshots are separate runs\n");
      return 2;
    }
    taps = std::make_unique<mnky::app::UiTapRunner>(&controller, window);
    taps->start();
  }

  std::unique_ptr<ScreenshotRunner> screenshots;
  if (wantsScreenshots) {
    const QString directory =
        liveScreenshots ? argumentValue(arguments, QStringLiteral("--screenshots-live"))
                        : argumentValue(arguments, QStringLiteral("--screenshots"));
    if (directory.isEmpty()) {
      std::fprintf(stderr, "mnkybox: --screenshots needs a directory\n");
      return 2;
    }
    screenshots = std::make_unique<ScreenshotRunner>(&controller, window, directory);
    screenshots->start();
  }

  QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller,
                   &mnky::app::AppController::shutdown);
  return app.exec();
}
