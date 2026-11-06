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
//   --clap-bench        measure N simultaneous mnkygain instances at a 64-frame
//                       block and print the 16-instance cap refusal; exits.
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
//                       MNKYBOX_TAP_TAKES=<dir> symlinks that folder's real
//                       takes (and .peaks sidecars) into the temporary HOME, so
//                       the clip-click passes run on real multi-MB material
//                       while every write still lands on the temporary paths.
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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
                        controller_->mixer()->setMode(0, 0);
                        controller_->mixer()->setMode(1, 1);
                        controller_->mixer()->setMode(2, 2);
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
                        controller_->setTab(2);
                      }});
    steps_.push_back({"31-punch", [this] {
                        controller_->session()->setPunchPlayFromEnabled(true);
                        controller_->session()->setPunchPlayFromMeasure(1);
                        controller_->session()->setPunchInEnabled(true);
                        controller_->session()->setPunchInMeasure(5);
                        controller_->session()->setPunchOutEnabled(true);
                        controller_->session()->setPunchOutMeasure(9);
                        controller_->setToolsPage(0);
                        controller_->setTab(5);
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
                        controller_->setTab(5);
                      }});
    steps_.push_back({"31-tools", [this] {
                        controller_->setToolsPage(2);
                        controller_->tools()->showLowSpaceDemo();
                        controller_->setTab(5);
                      }});
    steps_.push_back({"31-keypad", [this] {
                        controller_->setTab(2);
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
                        controller_->setTab(2);
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
                        controller_->setTab(5);
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
                        controller_->setTab(6);
                      }});
    steps_.push_back({"33-eq-popup", [this] {
                        // Transient readout on a Q change (Hz/dB/Q popups).
                        controller_->eq()->selectBand(1);
                        controller_->eq()->adjustQ(0.5);
                        controller_->setTab(6);
                      }});
    steps_.push_back({"33-eq-full", [this] {
                        controller_->hideValuePopup();
                        controller_->eq()->selectBand(2);
                        controller_->setFullscreen(true);
                        controller_->setTab(6);
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
                        controller_->setTab(7);
                      }});
    steps_.push_back({"33-xy-params", [this] {
                        // Secondary parameters of the FX2 grid live in the
                        // shared ConfigPopup (draft + OK), not on the grid.
                        controller_->hideValuePopup();
                        primeMixerDemo();
                        controller_->mixer()->setFxChoiceAt(
                            controller_->mixer()->selectedStrip(), 1, 2);
                        controller_->mixer()->setFxChoiceAt(
                            controller_->mixer()->selectedStrip(), 0, 1);
                        controller_->setTab(7);
                        controller_->openConfig(QStringLiteral("reverb"));
                      }});
    steps_.push_back({"33-xy-freeze", [this] {
                        controller_->configCancel();
                        controller_->xy()->toggleFreeze();
                        controller_->setTab(7);
                      }});
    steps_.push_back({"33-eq-type", [this] {
                        controller_->setFullscreen(false);
                        controller_->eq()->selectBand(2);
                        controller_->setTab(6);
                        controller_->openConfig(QStringLiteral("eqtype"));
                      }});
    steps_.push_back({"33-eq-shift", [this] {
                        // SHIFT face: the band sliders and the six type buttons
                        // (the panel reaches it with a 500 ms EXIT long-hold).
                        controller_->configCancel();
                        controller_->hideValuePopup();
                        controller_->showShiftFace(true);
                        controller_->setTab(6);
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
                        controller_->setTab(8);
                      }});
    steps_.push_back({"34-ops", [this] {
                        controller_->hideValuePopup();
                        controller_->setTab(8);
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
                        controller_->setTab(5);
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
                          controller_->setTab(3);
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
                        controller_->setTab(3);
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
                        controller_->setTab(3);
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
                        controller_->setTab(3);
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
                        controller_->mixer()->setMode(2, 3);  // MON
                        controller_->mixer()->setMode(1, 2);  // BOTH
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
                        controller_->mixer()->setMode(0, 0);
                        controller_->mixer()->setMode(1, 1);
                        controller_->mixer()->setMode(2, 2);
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
                        controller_->mixer()->setMode(2, 1);
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
    steps_.push_back({"39-recorder", [this] {
                        resetShot();
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->recorder()->selectRow(0);
                        controller_->recorder()->activateSelected();
                        controller_->setTab(2);
                      }});
    steps_.push_back({"39-recorder-shift", [this] {
                        resetShot();
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->setTab(2);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-system", [this] {
                        resetShot();
                        controller_->setTab(3);
                      }});
    steps_.push_back({"39-system-monitor", [this] {
                        resetShot();
                        controller_->setTab(3);
                        controller_->setSystemShiftPage(0);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-system-learn", [this] {
                        resetShot();
                        controller_->setTab(3);
                        controller_->setSystemShiftPage(1);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-system-tools", [this] {
                        resetShot();
                        controller_->setTab(3);
                        controller_->setSystemShiftPage(2);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-projects", [this] {
                        resetShot();
                        controller_->setTab(4);
                      }});
    steps_.push_back({"39-projects-shift", [this] {
                        resetShot();
                        controller_->setTab(4);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"39-monitor", [this] {
                        resetShot();
                        controller_->midiMonitor()->setHideClock(false);
                        controller_->setTab(5);
                      }});
    steps_.push_back({"39-eq", [this] {
                        resetShot();
                        controller_->eq()->selectMain();
                        eqBand(0, 2, 120.0, 4.0, 1.0);
                        eqBand(1, 1, 80.0, 0.0, 1.0);
                        eqBand(2, 3, 1000.0, -3.0, 1.4);
                        eqBand(3, 4, 8000.0, 2.0, 1.0);
                        controller_->eq()->selectBand(2);
                        controller_->setTab(6);
                        controller_->hideValuePopup();
                      }});
    steps_.push_back({"39-eq-shift", [this] {
                        resetShot();
                        controller_->eq()->selectMain();
                        controller_->eq()->selectBand(2);
                        controller_->eq()->selectType(3);
                        controller_->setTab(6);
                        controller_->showShiftFace(true);
                        controller_->hideValuePopup();
                      }});
    steps_.push_back({"39-eq-full", [this] {
                        resetShot();
                        controller_->setTab(6);
                        controller_->setFullscreen(true);
                      }});
    steps_.push_back({"39-xy", [this] {
                        resetShot();
                        controller_->setTab(7);
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
                        controller_->setTab(8);
                      }});
    steps_.push_back({"39-popup-comp", [this] {
                        resetShot();
                        controller_->setTab(3);
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
                        // W1: the monitor popup is the TRACK MODE popup now (the
                        // step keeps its name so the shot set stays stable).
                        controller_->openConfig(QStringLiteral("trackmode"));
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
                        controller_->setTab(3);
                        controller_->setSystemShiftPage(0);
                        controller_->showShiftFace(true);
                        controller_->openConfig(QStringLiteral("sends"));
                      }});
    steps_.push_back({"39-popup-eqtype", [this] {
                        resetShot();
                        controller_->eq()->selectMain();
                        controller_->eq()->selectBand(2);
                        controller_->setTab(6);
                        controller_->openConfig(QStringLiteral("eqtype"));
                      }});
    steps_.push_back({"39-popup-timelineops", [this] {
                        resetShot();
                        controller_->timeline()->setSelectedTrack(1);
                        controller_->timeline()->beginSelection(1, 1.0);
                        controller_->timeline()->updateSelection(5.0);
                        controller_->timeline()->endSelection();
                        controller_->setTab(8);
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
                      }});
    steps_.push_back({"40-routing-pipes-knob", [this] {
                        resetShot();
                        primePipeChain(true);
                        // The ARP pipe's cards are DIVISION/TYPE (choice chips)
                        // and OCTAVES/SWING (arc dials): generated cards, no
                        // per-type QML.
                        controller_->routing()->selectPipe(1);
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
                        controller_->setTab(3);
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
                        controller_->mixer()->setMode(0, 0);
                        controller_->mixer()->setMode(1, 1);
                        controller_->mixer()->setMode(2, 2);
                        controller_->mixer()->setMode(3, 3);
                        controller_->selectStrip(1);
                        controller_->setTab(0);
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
    steps_.push_back({"40-popup-trackfx", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->setTab(0);
                        // Opened from the channel popout's own strip, exactly as
                        // the popout's TRACK FX row does.
                        controller_->openChannel(2);
                        controller_->openTrackFx(0);
                      }});
    // ---- 41-*: pipes page declutter, popup selectors, knob-card params ----
    steps_.push_back({"41-routing-pipes", [this] {
                        resetShot();
                        primePipeChain(true);
                        // FILTER selected: one-line chain rows with the row
                        // actions on the selected row only, and eight bool
                        // parameters as popup-selector chips.
                        controller_->routing()->selectPipe(0);
                      }});
    steps_.push_back({"41-routing-pipes-knob", [this] {
                        resetShot();
                        primePipeChain(true);
                        // ARP selected: DIVISION/TYPE chips beside the OCTAVES
                        // and SWING arc-dial cards, with the value readout in
                        // the band below the bridge (never over the tab row).
                        controller_->routing()->selectPipe(1);
                        controller_->setTouchAnchorY(332.0);
                        controller_->showTextPopup(
                            QStringLiteral("SWING"),
                            controller_->routing()->pipeParamValueText(3, 50));
                      }});
    steps_.push_back({"41-popup-pipetype", [this] {
                        resetShot();
                        primePipeChain(true);
                        controller_->routing()->selectPipe(0);
                        // The ADD type selector: every schema type with its role.
                        controller_->openConfigFrom(QStringLiteral("pipetype"),
                                                    nullptr);
                      }});
    steps_.push_back({"41-popup-pipeparam", [this] {
                        resetShot();
                        primePipeChain(true);
                        controller_->routing()->selectPipe(1);
                        // ARP DIVISION (a schema enum) as the choice list its
                        // card opens.
                        controller_->openPipeParam(1, 0, nullptr);
                      }});
    steps_.push_back({"41-popup-metronome", [this] {
                        resetShot();
                        primePipeChain(true);
                        controller_->routing()->selectPipe(0);
                        // The sweep's numeric steppers are knob cards now: the
                        // click BARS value is the metronome's.
                        controller_->setTab(3);
                        controller_->setSystemShiftPage(0);
                        controller_->showShiftFace(true);
                        controller_->openConfig(QStringLiteral("metronome"));
                      }});
    steps_.push_back({"41-popup-delay", [this] {
                        resetShot();
                        // The delay DIVISION is a full choice row instead of a
                        // prev/name/next stepper.
                        controller_->setTab(7);
                        controller_->openConfig(QStringLiteral("delay"));
                      }});
    steps_.push_back({"41-chain-menu", [this] {
                        resetShot();
                        primePipeChain(true);
                        controller_->routing()->selectPipe(0);
                        controller_->openConfigFrom(QStringLiteral("chainmenu"),
                                                    nullptr);
                      }});

    // ---- 41-* sweep: tight-space values are dial chips, never sliders ------
    steps_.push_back({"41-mixer-shift", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->clearClipDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(0);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"41-mixer-track", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->clearClipDemo();
                        controller_->mixer()->setTrackMode(true);
                        controller_->mixer()->setTrackIndex(2);
                        controller_->setTab(0);
                      }});
    steps_.push_back({"41-channel-popout", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->clearClipDemo();
                        controller_->setTab(0);
                        controller_->openChannel(2);
                      }});
    steps_.push_back({"41-recorder-shift", [this] {
                        resetShot();
                        controller_->setTab(2);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"41-system-tools", [this] {
                        resetShot();
                        controller_->setTab(3);
                        controller_->setSystemShiftPage(1);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"41-popup-reverb", [this] {
                        resetShot();
                        // The X/Y reverb secondary parameters: one row of cards.
                        controller_->setTab(7);
                        controller_->openConfig(QStringLiteral("reverb"));
                      }});

    // ---- 42-*: UI fix wave 1 (review A1/A3/A4/A5/A7/A8/A9/A10/A14/B1/C1/C4) --
    steps_.push_back({"42-transport-tempo", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->setTab(0);
                        // A7: the BPM button opens the touch tempo popup.
                        controller_->openTempoPopup();
                      }});
    steps_.push_back({"42-transport-shift", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->setTab(0);
                        // B1: the chip reads < PRIMARY and is checked while the
                        // current tab's second face is up.
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"42-transport-no-shift", [this] {
                        resetShot();
                        primeMixerDemo();
                        // D3/B1: tab 6 (MIDI) has no second face; the chip is
                        // dimmed and the tab long-press cannot enter it.
                        controller_->setTab(5);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"42-mixer-arm-mutes", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->recorder()->selectRow(0);
                        controller_->recorder()->activateSelected();
                        setStripArm(1, true);
                        // W1: ARM is an explicit mode word now; the strip shows
                        // ARM in the record colour instead of a hazard badge.
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(0);
                      }});
    steps_.push_back({"42-mixer-shift-legend", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(0);
                        // C1: solo green / mute gray / arm red plus the monitor
                        // legend under the SHIFT strips.
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"42-recorder-arm-mutes", [this] {
                        resetShot();
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->recorder()->selectRow(0);
                        controller_->recorder()->activateSelected();
                        setStripArm(1, true);
                        controller_->setTab(2);
                      }});
    steps_.push_back({"42-recorder-shift-selected", [this] {
                        resetShot();
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->recorder()->selectRow(0);
                        controller_->recorder()->activateSelected();
                        controller_->setTab(2);
                        // A3: the SHIFT face names the take its buttons act on.
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"42-projects-select", [this] {
                        resetShot();
                        controller_->setTab(4);
                        // A deterministic row in the isolated HOME: the
                        // capacity step above leaves 64 tracks, which the
                        // session store refuses to save.
                        clearMixerDemo();
                        primeMixerDemo();
                        controller_->projects()->saveProject();
                        controller_->projects()->scanProjects();
                        // A3: "..." selects without loading; the row body loads.
                        controller_->selectProjectRow(0);
                        controller_->hideValuePopup();
                      }});
    steps_.push_back({"42-projects-shift-mirror", [this] {
                        resetShot();
                        controller_->setTab(4);
                        clearMixerDemo();
                        primeMixerDemo();
                        controller_->projects()->saveProject();
                        controller_->projects()->scanProjects();
                        controller_->selectProjectRow(0);
                        controller_->hideValuePopup();
                        // A3: the mirrored list on the destructive face.
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"42-timeline-take-sheet", [this] {
                        resetShot();
                        controller_->setTab(8);
                        // A10: the lane label opens TAKE FOR TRACK nn.
                        controller_->openTakeSheet(1);
                      }});
    steps_.push_back({"42-popup-timelineops", [this] {
                        resetShot();
                        controller_->setTab(8);
                        // A14: seven ops (no RENDER), footer names the peaks
                        // sidecar, missing peaks is a RENDER PEAKS action.
                        controller_->openConfig(QStringLiteral("timelineops"));
                      }});
    steps_.push_back({"42-popup-metronome", [this] {
                        resetShot();
                        controller_->setTab(3);
                        // A8: OFF / REC ONLY / ON PLAY, MAIN / PHONES / OUT3&4,
                        // LEVEL and COUNT-IN.
                        controller_->openConfig(QStringLiteral("metronome"));
                      }});
    steps_.push_back({"42-mixer-learn-armed", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(0);
                        // A9: the learnable gain/pan/FX-wet controls outline in
                        // the accent colour while the target capture waits.
                        controller_->armLearnTargetCapture();
                      }});
    steps_.push_back({"42-system-learn", [this] {
                        resetShot();
                        controller_->setTab(3);
                        controller_->setSystemShiftPage(1);
                        controller_->showShiftFace(true);
                        // A9: ARM CAPTURE with no target outlines every
                        // learnable control and waits for a touch.
                        controller_->armLearnTargetCapture();
                      }});

    // ---- 43-*: per-track FX rack ----------------------------------------
    // The two rack slots reach every surface: the tab row lost its FX entry,
    // the strip knobs edit the slot wet, the channel popout opens TRACK FX and
    // the X/Y tab bends the selected track's slots.
    steps_.push_back({"43-tabs", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->selectStrip(1);
                        // No FX entry in the row (9 tabs), and the selected
                        // strip keeps its own wet values in the mixer.
                        controller_->setTab(0);
                      }});
    steps_.push_back({"43-track-fx", [this] {
                        resetShot();
                        primeMixerDemo();
                        primeTrackFx(2);
                        controller_->mixer()->setFxWetAt(2, 0, 0.65);
                        controller_->mixer()->setFxWetAt(2, 1, 0.35);
                        controller_->mixer()->setTrackMode(false);
                        controller_->setTab(0);
                        controller_->openChannel(2);
                        controller_->openTrackFx(0);
                      }});
    steps_.push_back({"43-track-fx-wet", [this] {
                        resetShot();
                        primeMixerDemo();
                        primeTrackFx(1);
                        controller_->mixer()->setFxWetAt(1, 1, 0.35);
                        controller_->setTab(0);
                        controller_->openChannel(1);
                        controller_->openTrackFx(1);
                        // The wet value popup reads the slot mix, not a send.
                        controller_->showValuePopup(QStringLiteral("FX2 WET"),
                                                    QStringLiteral("percent"), 0.35);
                      }});
    steps_.push_back({"43-popup-fxslot", [this] {
                        resetShot();
                        primeMixerDemo();
                        primeTrackFx(2);
                        controller_->setTab(0);
                        controller_->openChannel(2);
                        controller_->openTrackFx(0);
                        // NONE / DELAY / REVERB plus the discovered plugin
                        // list; selecting a plugin loads it live.
                        controller_->openFxSlot(1);
                      }});
    steps_.push_back({"43-mixer-shift-wet", [this] {
                        resetShot();
                        primeMixerDemo();
                        primeTrackFx(0);
                        primeTrackFx(1);
                        controller_->mixer()->setFxWetAt(0, 0, 0.5);
                        controller_->mixer()->setFxWetAt(1, 1, 0.8);
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->selectStrip(1);
                        controller_->setTab(0);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"43-track-shift", [this] {
                        resetShot();
                        primeMixerDemo();
                        primeTrackFx(2);
                        controller_->mixer()->setFxWetAt(2, 0, 0.65);
                        controller_->mixer()->setFxWetAt(2, 1, 0.35);
                        controller_->mixer()->setTrackMode(true);
                        controller_->mixer()->setTrackIndex(2);
                        controller_->setTab(0);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"43-channel-popout", [this] {
                        resetShot();
                        primeMixerDemo();
                        primeTrackFx(2);
                        controller_->mixer()->setFxWetAt(2, 0, 0.65);
                        controller_->mixer()->setFxWetAt(2, 1, 0.35);
                        controller_->setTab(0);
                        controller_->openChannel(2);
                      }});
    steps_.push_back({"43-xy-track", [this] {
                        resetShot();
                        primeMixerDemo();
                        primeTrackFx(1);
                        controller_->selectStrip(1);
                        controller_->xy()->setPoint(0, 0.7, 0.4);
                        controller_->xy()->setPoint(1, 0.35, 0.8);
                        controller_->hideValuePopup();
                        controller_->setTab(7);
                      }});
    steps_.push_back({"43-xy-picker", [this] {
                        resetShot();
                        primeMixerDemo();
                        primeTrackFx(1);
                        controller_->mixer()->setFxChoiceAt(1, 1, 0);
                        controller_->selectStrip(1);
                        controller_->xy()->setPoint(0, 0.6, 0.5);
                        controller_->hideValuePopup();
                        controller_->setTab(7);
                      }});

    // ---- 44-*: review wave 2 (remaining findings) -----------------------
    // Each shot is the proof for one changed surface: renamed tabs, the
    // pager grid, clip clear, PLAY wording, hold-to-reset, the pipe footer,
    // one dirty indicator, the OUTPUT page + clock source, the EQ subject row,
    // the X/Y mode label, the EDIT lane hint, the import preview and the verbs.
    steps_.push_back({"44-tabs", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(0);
                      }});
    steps_.push_back({"44-mixer", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        setStripArm(1, true);
                        controller_->mixer()->showClipDemo();
                        controller_->setTab(0);
                      }});
    steps_.push_back({"44-mixer-bankgrid", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(0);
                        controller_->openBankGrid();
                      }});
    steps_.push_back({"44-mixer-clip-cleared", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->mixer()->showClipDemo();
                        const int firstSlot =
                            controller_->mixer()
                                ->data(controller_->mixer()->index(0, 0),
                                       mnky::app::MixerViewModel::SlotRole)
                                .toInt();
                        controller_->mixer()->clearClip(firstSlot);
                        controller_->showTextPopup(QStringLiteral("CLIP"),
                                                   QStringLiteral("CLIP CLEARED"));
                        controller_->setTab(0);
                      }});
    steps_.push_back({"44-recorder-play", [this] {
                        resetShot();
                        primeMixerDemo();
                        primeTakeFixture();
                        controller_->play();
                        controller_->setTab(2);
                      }});
    steps_.push_back({"44-recorder-import", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->setTab(2);
                        const auto path = writeWavFixture(48000);
                        controller_->requestConfirm(
                            QStringLiteral("IMPORT TAKE"),
                            controller_->recorder()->takeImportPreview(
                                QString::fromStdString(path.string())),
                            QStringLiteral("IMPORT"),
                            QStringLiteral("take-import"), -1);
                      }});
    steps_.push_back({"44-routing", [this] {
                        resetShot();
                        controller_->setTab(1);
                      }});
    // A13: GENERATOR is a source-less FROM. The primary patchbay shows its wire
    // without any input being selected.
    steps_.push_back({"44-routing-generator", [this] {
                        resetShot();
                        controller_->setTab(1);
                        const int generator = generatorNode();
                        const int usbOut = outputNodeFor(mnky::midi::Port::UsbOut);
                        if (generator >= 0 && usbOut >= 0) {
                          controller_->routing()->selectInput(generator);
                          controller_->routing()->toggleRoute(generator, usbOut);
                        }
                      }});
    steps_.push_back({"44-routing-pipes", [this] {
                        resetShot();
                        primePipeChain(true);
                        controller_->routing()->selectPipe(0);
                      }});
    // A13: the pipe page with FROM = GENERATOR and no input source at all.
    steps_.push_back({"44-pipes-generator", [this] {
                        resetShot();
                        primeGeneratorChain();
                        controller_->routing()->setShiftPage(1);
                        controller_->showShiftFace(true);
                        controller_->routing()->selectPipe(0);
                      }});
    // The pending head's type list: only the generator role, with the header
    // saying why (no modifier can start a chain that has no input).
    steps_.push_back({"44-popup-pipetype-generator", [this] {
                        resetShot();
                        primeGeneratorChain(false);
                        controller_->routing()->setShiftPage(1);
                        controller_->showShiftFace(true);
                        controller_->openConfig(QStringLiteral("pipetype"));
                      }});
    steps_.push_back({"44-projects-unsaved", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(4);
                        controller_->mixer()->setGainDb(0, -3.0);
                      }});
    steps_.push_back({"44-system", [this] {
                        resetShot();
                        controller_->setTab(3);
                      }});
    steps_.push_back({"44-system-output", [this] {
                        resetShot();
                        controller_->setTab(3);
                        controller_->setSystemShiftPage(0);
                        controller_->showShiftFace(true);
                      }});
    steps_.push_back({"44-eq-subject", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->eq()->selectSubject(1);
                        eqBand(2, 3, 1000.0, -3.0, 1.4);
                        controller_->eq()->selectBand(2);
                        controller_->hideValuePopup();
                        controller_->setTab(6);
                      }});
    steps_.push_back({"44-xy-mode", [this] {
                        resetShot();
                        primeMixerDemo();
                        primeTrackFx(1);
                        controller_->selectStrip(1);
                        controller_->setTab(7);
                      }});
    steps_.push_back({"44-timeline-empty", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        // One real take on track 2, so the other lanes show the
                        // empty hint and the playing lane shows its TAKE chip.
                        for (int track = 0; track < 6; ++track) {
                          if (track != 1) {
                            controller_->recorder()->setSelectedTrack(track);
                            controller_->recorder()->clearActive();
                          }
                        }
                        primeTakeFixture();
                        controller_->timeline()->preparePeaksForDemo();
                        controller_->timeline()->setSelectedTrack(1);
                        controller_->timeline()->setPage(0);
                        controller_->setTab(8);
                      }});
    steps_.push_back({"44-timeline-ops", [this] {
                        resetShot();
                        controller_->setTab(8);
                        controller_->timeline()->clearSelection();
                        controller_->openConfig(QStringLiteral("timelineops"));
                      }});
    steps_.push_back({"44-nopico-popup", [this] {
                        resetShot();
                        controller_->showNoPicoInfo();
                      }});
    steps_.push_back({"44-nopico-chip", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(0);
                      }});
    // B6: an ordinary value readout lives in the transport notice slot.
    steps_.push_back({"44-value-slot", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(0);
                        controller_->showValuePopup(QStringLiteral("IN 02 GAIN"),
                                                    QStringLiteral("db"), -6.0);
                      }});
    // B6: the fullscreen EQ face hides the strip, so there the readout floats.
    steps_.push_back({"44-eq-full-value", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->eq()->selectBand(2);
                        controller_->setTab(6);
                        controller_->setFullscreen(true);
                        controller_->showValuePopup(QStringLiteral("BAND 3 GAIN"),
                                                    QStringLiteral("db"), -3.0);
                      }});
    // B13: the pipe scope popup names MIDI channels and states the bypass rule.
    steps_.push_back({"44-popup-midi-filter", [this] {
                        resetShot();
                        primePipeChain(true);
                        controller_->openConfig(QStringLiteral("pipechannels"));
                      }});
    steps_.push_back({"44-popup-add-track", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->setTab(0);
                        controller_->requestAddTrack();
                      }});

    // ---- 45-*: per-track CLAP hosting + output inserts -------------------
    // A live CLAP plugin slot: the card name, note and parameter chips come
    // from the plugin's own clap.params table (mnkygain in this build).
    steps_.push_back({"45-track-fx-plugin", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->selectTrackAt(2);
                        controller_->mixer()->setFxChoiceAt(2, 0, 0);
                        controller_->mixer()->setFxChoiceAt(
                            2, 0, mnky::app::MixerViewModel::kFxChoicePlugin);
                        controller_->mixer()->setFxWetAt(2, 0, 0.65);
                        controller_->mixer()->setFxChoiceAt(2, 1, 2);
                        controller_->setTab(0);
                        controller_->openChannel(2);
                        controller_->openTrackFx(0);
                      }});
    // The same slot's selector: NONE / DELAY / REVERB / MNKYGAIN (the first
    // discovered plugin is selected), with the live parameter count noted.
    steps_.push_back({"45-popup-fxslot-plugin", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->selectTrackAt(2);
                        controller_->mixer()->setFxChoiceAt(2, 0, 0);
                        controller_->mixer()->setFxChoiceAt(
                            2, 0, mnky::app::MixerViewModel::kFxChoicePlugin);
                        controller_->mixer()->setFxWetAt(2, 0, 0.65);
                        controller_->setTab(0);
                        controller_->openChannel(2);
                        controller_->openTrackFx(0);
                        controller_->openFxSlot(0);
                      }});
    // An output insert chain on USB OUT, edited in the same schema-driven pipe
    // editor as a normal route; the INSERTS strip owns add/remove/reorder.
    steps_.push_back({"45-output-insert", [this] {
                        resetShot();
                        primeOutputInsert();
                      }});
    // The generator route is unchanged by the insert work: FROM = GENERATOR,
    // TO = USB OUT, head = CC LFO, no input selected.
    steps_.push_back({"45-generator-route", [this] {
                        resetShot();
                        primeGeneratorChain();
                      }});

    // ---- 46-*: track modes, RECMODE, REC SOURCE, take manager, take->track --
    // W1: the mode selector as a word + colour. TRACK 02 is ARM (record red),
    // TRACK 03 is OVERDUB (accent), TRACK 04 is LIVE (accent), the rest PLAY
    // (dim) - never a bare letter.
    steps_.push_back({"46-track-modes", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setMode(1, 1);   // bass -> ARM
                        controller_->mixer()->setMode(2, 2);   // keys -> OVERDUB
                        controller_->mixer()->setMode(3, 3);   // gtr  -> LIVE
                        controller_->mixer()->setTrackMode(false);
                        controller_->mixer()->setBank(0);
                        controller_->setTab(0);
                      }});
    // W1/W2b: the ARM-area popup - mode words plus the REC SOURCE selector.
    steps_.push_back({"46-popup-track-mode", [this] {
                        resetShot();
                        primeMixerDemo();
                        controller_->mixer()->setMode(1, 1);
                        controller_->setTab(0);
                        controller_->openChannel(1);
                        controller_->openTrackMode(1);
                      }});
    // W2b: REC SOURCE picked on the Recorder's own mode popup (POST+FX).
    steps_.push_back({"46-rec-source", [this] {
                        resetShot();
                        controller_->stop();
                        primeMixerDemo();
                        controller_->mixer()->setMode(1, 1);
                        controller_->mixer()->setRecSource(1, 2);
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->setTab(2);
                        controller_->openTrackMode(1);
                      }});
    // W2: COUNT-IN 2 bars armed and REC pressed - the roll runs, the recorder
    // is silent, and the status line states exactly what will happen and when.
    steps_.push_back({"46-recmode-countin", [this] {
                        resetShot();
                        controller_->stop();
                        primeMixerDemo();
                        controller_->mixer()->setMode(1, 1);
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->setTab(2);
                        controller_->openRecMode();
                        controller_->config()->setRecMode(1);
                        controller_->config()->setCountInBars(2);
                        controller_->configAccept();
                        controller_->recToggle();
                      },
                      // The touch REC waits out the 300 ms chord window before
                      // the schedule starts, so the grab happens mid-count-in.
                      800});
    // W2: the RECMODE picker itself, COUNT-IN selected with the bar stepper.
    steps_.push_back({"46-popup-recmode", [this] {
                        resetShot();
                        controller_->stop();
                        primeMixerDemo();
                        controller_->setTab(2);
                        controller_->openRecMode();
                        controller_->config()->setRecMode(1);
                        controller_->config()->setCountInBars(2);
                      }});
    // W2b: a running take names its REC SOURCE in the status line:
    // `REC 00:00 - 02-bass-001 (POST+FX)`.
    steps_.push_back({"46-rec-status", [this] {
                        resetShot();
                        controller_->stop();
                        primeMixerDemo();
                        // One armed track, so the status names one take and no
                        // "+N"; earlier fixture steps leave their own arms.
                        for (int slot = 0; slot < 8; ++slot) {
                          controller_->mixer()->setMode(slot, 0);
                        }
                        controller_->mixer()->setMode(1, 1);
                        controller_->mixer()->setRecSource(1, 2);
                        controller_->recorder()->setSelectedTrack(1);
                        controller_->setTab(2);
                        // A previous step may have armed COUNT-IN; record now.
                        controller_->openRecMode();
                        controller_->config()->setRecMode(0);
                        controller_->configAccept();
                        controller_->recToggle();
                      },
                      700});
    // W4/W3: the take row "..." menu with ACTIVE/EDIT/RENAME/DELETE/REMOVE/
    // IMPORT/NEW TRACK FROM THIS TAKE, for a real row.
    steps_.push_back({"46-take-manager", [this] {
                        resetShot();
                        controller_->stop();
                        primeMixerDemo();
                        primeTakeFixture();
                        controller_->setTab(2);
                        controller_->openTakeActions(0);
                      }});
    // W4: the DELETE confirmation names the file it removes.
    steps_.push_back({"46-take-delete-confirm", [this] {
                        resetShot();
                        controller_->stop();
                        primeMixerDemo();
                        primeTakeFixture();
                        controller_->setTab(2);
                        controller_->requestDeleteTake();
                      }});
    // W3: NEW TRACK FROM THIS TAKE - MOVE (default) or KEEP ON SOURCE after the
    // name keypad.
    steps_.push_back({"46-take-new-track", [this] {
                        resetShot();
                        controller_->stop();
                        primeMixerDemo();
                        primeTakeFixture();
                        controller_->setTab(2);
                        controller_->requestNewTrackFromTake(0);
                        controller_->keypadAccept(QStringLiteral("beat"));
                      }});
    // W3 result: MOVE created TRACK 09 `beat` - input NONE, output MAIN, mode
    // PLAY - and the take is ACTIVE on it while the source track let it go.
    steps_.push_back({"46-take-new-track-result", [this] {
                        controller_->config()->chooseTrackFromTake(0);
                        controller_->recorder()->setSelectedTrack(8);
                        controller_->setTab(2);
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

  // One deterministic take in the real take directory for the PLAY wording and
  // the EDIT lane chip. It carries a name no real session would use, so the
  // fixture can never shadow a user's take.
  void primeTakeFixture() {
    const auto dir = std::filesystem::path(
        controller_->recorder()->takeDirectory().toStdString());
    writeWavFixture(48000, dir / "02-fixturetake-001.wav");
    // takeStateChanged() rescans; refresh() only scans every 20th call.
    controller_->recorder()->takeStateChanged();
    controller_->recorder()->setSelectedTrack(1);
    for (int row = 0; row < controller_->recorder()->takes()->count(); ++row) {
      const auto* entry = controller_->recorder()->takes()->entry(row);
      if (entry != nullptr && entry->name.contains(QStringLiteral("fixturetake"))) {
        controller_->recorder()->selectRow(row);
        break;
      }
    }
    controller_->recorder()->activateSelected();
    controller_->timeline()->refresh();
  }

  // D7 proof: a minimal 48 kHz / 2 ch float WAV written to the temp dir, so
  // the import confirmation shows the duration/channel preview the recorder
  // reads from the header.
  std::filesystem::path writeWavFixture(int sampleRate,
                                        const std::filesystem::path& target = {}) {
    constexpr std::uint32_t frames = 576000;  // 12 s at 48 kHz
    constexpr std::uint16_t channels = 2;
    const auto path = target.empty() ? std::filesystem::temp_directory_path() /
                                           "mnkybox-44-import-preview.wav"
                                     : target;
    std::vector<unsigned char> bytes;
    const auto tag = [&bytes](const char* text) {
      bytes.insert(bytes.end(), text, text + 4);
    };
    const auto le32 = [&bytes](std::uint32_t value) {
      for (int shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<unsigned char>((value >> shift) & 0xFF));
      }
    };
    const auto le16 = [&bytes](std::uint16_t value) {
      bytes.push_back(static_cast<unsigned char>(value & 0xFF));
      bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    };
    const std::uint32_t dataBytes = frames * channels * 4;
    tag("RIFF");
    le32(36 + dataBytes);
    tag("WAVE");
    tag("fmt ");
    le32(16);
    le16(3);  // IEEE float
    le16(channels);
    le32(static_cast<std::uint32_t>(sampleRate));
    le32(static_cast<std::uint32_t>(sampleRate) * channels * 4);
    le16(static_cast<std::uint16_t>(channels * 4));
    le16(32);
    tag("data");
    le32(dataBytes);
    bytes.resize(bytes.size() + dataBytes, 0);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return path;
  }

  // Deterministic arm state for the A1 shots: a previous step may have left
  // the strip armed, and toggleArm() would then disarm it.
  void setStripArm(int slot, bool armed) {
    const int position = controller_->mixer()->positionOf(slot);
    if (position < 0) return;
    const bool current =
        controller_->mixer()->data(controller_->mixer()->index(position, 0),
                                   mnky::app::MixerViewModel::ArmRole)
            .toBool();
    if (current != armed) controller_->mixer()->toggleArm(slot);
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
    controller_->closeTrackFx();
    // A previous X/Y shot may have left finger points and the freeze macro
    // latched; no shot inherits either.
    controller_->xy()->touchEnd(0, 41);
    controller_->xy()->touchEnd(1, 42);
    if (controller_->xy()->frozen()) controller_->xy()->toggleFreeze();
    controller_->hideValuePopup();
    controller_->closeChannel();
    controller_->configCancel();
    controller_->keypadCancel();
    controller_->confirmCancel();
    controller_->closeTempoPopup();
    controller_->closeTabGrid();
    controller_->closeBankGrid();
    controller_->closeTakeSheet();
    controller_->clearNotice();
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

  int generatorNode() const {
    for (const auto& row : controller_->routing()->inputNodes()) {
      if (row.toMap().value(QStringLiteral("port")).toInt() == mnky::midi::kSrcGenerator) {
        return row.toMap().value(QStringLiteral("nodeIndex")).toInt();
      }
    }
    return -1;
  }

  int outputNodeFor(mnky::midi::Port port) const {
    for (const auto& row : controller_->routing()->outputNodes()) {
      if (row.toMap().value(QStringLiteral("port")).toInt() == mnky::midi::portId(port)) {
        return row.toMap().value(QStringLiteral("nodeIndex")).toInt();
      }
    }
    return -1;
  }

  // Output-insert fixture: USB OUT with one FILTER insert chain selected, so
  // the INSERTS strip, the chain row and the modifier cards are all populated.
  void primeOutputInsert() {
    auto* routing = controller_->routing();
    controller_->setTab(1);
    controller_->showShiftFace(true);
    routing->setShiftPage(1);
    const int usbOut = outputNodeFor(mnky::midi::Port::UsbOut);
    if (usbOut < 0) return;
    routing->setPipeDestination(usbOut);
    while (routing->insertRows().size() > 0) routing->removeInsertChain(0);
    routing->addInsertChain();
    routing->setAddTypeIndex(0);  // FILTER
    routing->addPipe();
    routing->selectPipe(0);
  }

  // A13 fixture: FROM = GENERATOR (no input), TO = USB OUT, head = CC LFO.
  void primeGeneratorChain(bool withHead = true) {
    auto* routing = controller_->routing();
    controller_->setTab(1);
    controller_->showShiftFace(true);
    routing->setShiftPage(1);
    const int generator = generatorNode();
    const int usbOut = outputNodeFor(mnky::midi::Port::UsbOut);
    if (generator < 0 || usbOut < 0) return;
    routing->setPipeSource(generator);
    routing->setPipeDestination(usbOut);
    while (routing->insertRows().size() > 0) routing->removeInsertChain(0);
    while (routing->pipeCount() > 0) routing->removePipe(0);
    if (!withHead) {
      routing->setAddTypeIndex(0);
      return;
    }
    routing->setAddTypeIndex(0);  // the filtered list's only entry: CC LFO
    routing->addPipe();
    routing->selectPipe(0);
    routing->setSelectedPipeChannelMask(0x000F);
  }

  // Deterministic rack for the 43-* shots: both slots are cleared first, so a
  // shot shows the effects' published defaults rather than a previous step's
  // parameter edits (or a freeze macro that never released).
  void primeTrackFx(int slot) {
    controller_->mixer()->setFxChoiceAt(slot, 0, 0);
    controller_->mixer()->setFxChoiceAt(slot, 1, 0);
    controller_->mixer()->setFxChoiceAt(slot, 0, 1);
    controller_->mixer()->setFxChoiceAt(slot, 1, 2);
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

namespace {

// --clap-bench: the Studio Pi CPU table for the per-track CLAP host and the
// admission-cap proof. It loads the discovered plugin into 1..16 track slots
// directly (no audio device, no QML), times N adapters on the same 64-frame
// stereo block, then asks MixerViewModel for a 17th selection and prints the
// exact refusal. Measurement only; the numbers are target-machine evidence, not
// a real-time guarantee.
int runClapBench() {
  using namespace mnky::app;
  AppState state;
  SessionSettings settings;
  MixerModel mixer;
  EqModel eq;
  FxManager fx;
  const auto& plugins = fx.plugins();
  if (plugins.empty()) {
    std::fprintf(stderr, "clap-bench: no CLAP plugin discovered\n");
    return 1;
  }
  const auto& plugin = plugins.front();
  std::printf("clap-bench: plugin %s (%s)\n", plugin.name.c_str(),
              plugin.id.c_str());
  std::printf("clap-bench: sample rate 48000 Hz, block 64 frames, stereo\n");

  int loaded = 0;
  for (int track = 0; track < FxManager::kMaxTrackInstances; ++track) {
    if (!fx.loadTrackEffect(track, 0, plugin.id, 48000.0, 128)) {
      std::fprintf(stderr, "clap-bench: load failed at instance %d\n", loaded + 1);
      return 1;
    }
    ++loaded;
  }
  std::printf("clap-bench: %d instances loaded (host cap %d)\n", loaded,
              FxManager::kMaxTrackInstances);

  MixerViewModel view(mixer, settings, eq);
  view.setClapHost(&fx);
  const bool accepted = view.setFxChoiceAt(FxManager::kMaxTrackInstances, 0,
                                           MixerViewModel::kFxChoicePlugin);
  std::printf("clap-bench: 17th selection accepted=%d message=\"%s\"\n",
              accepted ? 1 : 0,
              view.fxChoiceRefusal(FxManager::kMaxTrackInstances, 0)
                  .toStdString()
                  .c_str());

  constexpr int frames = 64;
  constexpr int blocks = 20000;
  constexpr int warmup = 2000;
  std::array<float, frames> left{};
  std::array<float, frames> right{};
  for (int frame = 0; frame < frames; ++frame) {
    left[static_cast<std::size_t>(frame)] =
        0.1f * std::sin(2.0f * 3.14159265f *
                        static_cast<float>(frame) / static_cast<float>(frames));
    right[static_cast<std::size_t>(frame)] =
        0.1f * std::cos(2.0f * 3.14159265f *
                        static_cast<float>(frame) / static_cast<float>(frames));
  }
  std::array<mnky::audio::IEffect*, FxManager::kMaxTrackInstances> effects{};
  for (int index = 0; index < FxManager::kMaxTrackInstances; ++index) {
    effects[static_cast<std::size_t>(index)] = fx.trackEffect(index, 0);
  }
  std::printf("clap-bench: instances  us/block  us/instance  %% of 1333.3 us\n");
  for (const int count : {1, 2, 4, 8, 16}) {
    for (int block = 0; block < warmup; ++block) {
      for (int index = 0; index < count; ++index) {
        float* channels[2] = {left.data(), right.data()};
        effects[static_cast<std::size_t>(index)]->process(channels, 2, frames);
      }
    }
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < blocks; ++block) {
      for (int index = 0; index < count; ++index) {
        float* channels[2] = {left.data(), right.data()};
        effects[static_cast<std::size_t>(index)]->process(channels, 2, frames);
      }
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const double totalUs =
        std::chrono::duration<double, std::micro>(elapsed).count();
    const double perBlockUs = totalUs / blocks;
    const double perInstanceUs = perBlockUs / count;
    const double budgetUs = 1000000.0 * frames / 48000.0;
    std::printf("clap-bench: %9d  %8.2f  %11.2f  %13.2f%%\n", count,
                perBlockUs, perInstanceUs, 100.0 * perBlockUs / budgetUs);
  }

  for (int track = 0; track < FxManager::kMaxTrackInstances; ++track) {
    fx.unloadTrackEffect(track, 0);
  }
  std::printf("clap-bench: teardown leaves %d instance(s)\n",
              fx.trackInstanceCount());
  return 0;
}

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
  if (arguments.contains(QStringLiteral("--clap-bench"))) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("mnkybox"));
    QCoreApplication::setApplicationName(QStringLiteral("mnkybox"));
    return runClapBench();
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
    // Read-only take seeding: MNKYBOX_TAP_TAKES points at a real take folder,
    // whose files are symlinked into the disposable HOME. The app then clicks
    // real multi-MB takes (and their .peaks sidecars) while every write, rename
    // or delete still lands on the temporary paths - a rename retargets the
    // link, it never touches the original file.
    const QByteArray seedSource = qgetenv("MNKYBOX_TAP_TAKES");
    if (!seedSource.isEmpty()) {
      const QString source = QString::fromLocal8Bit(seedSource);
      const QString target = tapHome->path() + QStringLiteral("/Music/mnkybox-takes");
      QDir().mkpath(target);
      const auto linkFiles = [](const QString& from, const QString& to) {
        int linked = 0;
        const auto entries = QDir(from).entryInfoList(
            {QStringLiteral("*.wav"), QStringLiteral("*.peaks")}, QDir::Files);
        for (const auto& entry : entries) {
          const QString link = to + QLatin1Char('/') + entry.fileName();
          QFile::remove(link);
          if (QFile::link(entry.absoluteFilePath(), link)) ++linked;
        }
        return linked;
      };
      int linked = linkFiles(source, target);
      // Takes that live inside a take-<utc> folder are the recorder's own form;
      // mirror those folders too so the real list is reproduced.
      const auto folders = QDir(source).entryInfoList(
          {QStringLiteral("take-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
      for (const auto& folder : folders) {
        const QString sub = target + QLatin1Char('/') + folder.fileName();
        QDir().mkpath(sub);
        linked += linkFiles(folder.absoluteFilePath(), sub);
      }
      std::printf("ui-taps: seeded %d real take file(s) from %s\n", linked,
                  qPrintable(source));
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
