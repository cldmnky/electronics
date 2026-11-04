// mnkybox - Qt 6 QML touchscreen DAW application (Pi 5 + Pico link).
//
// Flags:
//   --emulator          use the emulator backends (scripted Pico SPI link,
//                       generated audio) instead of the Linux hardware paths.
//   --unittests         run the non-visual C++ checks and exit; no window, no
//                       audio device, no MIDI port is opened.
//   --screenshots DIR   headless proof: load every QML screen, grab the window
//                       and write 30-qt-*.png into DIR, then exit. Implies the
//                       emulator backends like --emulator. Combine with
//                       QT_QPA_PLATFORM=offscreen (the guest/macOS proof runs).
//   --screenshots-live DIR
//                       the same grab run but against the real audio backend
//                       (used for the PipeWire/Pulse evidence run).
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QLockFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app_controller.hpp"
#include "logging.hpp"

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
                        controller_->showValuePopup(QStringLiteral("IN 02 PAN"),
                                                    QStringLiteral("pan"), 0.5);
                        controller_->setTab(0);
                      }});
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
    QTimer::singleShot(180, this, &ScreenshotRunner::next);
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
  if (wantsScreenshots) {
    // A headless proof must not depend on a GPU: the software renderer works
    // under QT_QPA_PLATFORM=offscreen on both the Mac and the Linux guest.
    qputenv("QT_QUICK_BACKEND", "software");
  }
  QGuiApplication app(argc, argv);
  QCoreApplication::setApplicationVersion(QStringLiteral(MNKY_APP_VERSION));

  mnky::app::AppLogger::instance();
  const bool emulator = arguments.contains(QStringLiteral("--emulator")) ||
                        (wantsScreenshots && !liveScreenshots);

  std::unique_ptr<QLockFile> instanceLock;
  if (!wantsScreenshots) {
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

  // The QML Style singleton is a QML file so the theme lives with the UI; it
  // carries the contrast floors from the old theme.hpp comments as comments and
  // via logic_tests.cpp.
  qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/qml/Style.qml")), "Mnkybox", 1, 0,
                           "Style");

  mnky::app::AppController controller(emulator);
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
  engine.rootContext()->setContextProperty(QStringLiteral("tools"), controller.tools());
  engine.rootContext()->setContextProperty(QStringLiteral("config"), controller.config());

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
