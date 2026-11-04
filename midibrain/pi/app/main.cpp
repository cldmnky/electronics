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
    steps_.push_back({"30-qt-mixer", [this] { controller_->setTab(0); }});
    steps_.push_back({"30-qt-mixer-bank2", [this] {
                        controller_->mixer()->setBank(1);
                        controller_->setTab(0);
                      }});
    steps_.push_back({"30-qt-routing", [this] {
                        controller_->mixer()->setBank(0);
                        controller_->routing()->setSelectedInput(-1);
                        controller_->setTab(1);
                      }});
    steps_.push_back({"30-qt-routing-active", [this] {
                        controller_->routing()->selectInput(0);
                        controller_->setTab(1);
                      }});
    steps_.push_back({"30-qt-fx", [this] {
                        controller_->routing()->setSelectedInput(-1);
                        controller_->setTab(2);
                      }});
    steps_.push_back({"30-qt-recorder", [this] {
                        controller_->setTab(3);
                      }});
    steps_.push_back({"30-qt-system", [this] {
                        controller_->setTab(4);
                      }});
    steps_.push_back({"30-qt-system-learn", [this] {
                        controller_->setTab(4);
                        if (auto* left = window_->findChild<QObject*>(QStringLiteral("systemLeft"))) {
                          const qreal content = left->property("contentHeight").toReal();
                          const qreal height = left->property("height").toReal();
                          left->setProperty("contentY", qMax<qreal>(0.0, content - height));
                        }
                      }});
    steps_.push_back({"30-qt-projects", [this] {
                        controller_->setTab(5);
                      }});
    steps_.push_back({"30-qt-keypad", [this] {
                        controller_->setTab(5);
                        controller_->openDemoKeypad();
                      }});
    steps_.push_back({"30-qt-confirm", [this] {
                        controller_->keypadCancel();
                        controller_->requestConfirm(
                            QStringLiteral("DELETE PROJECT"),
                            QStringLiteral("Delete Project-demo? The folder and its "
                                           "project.mnkybox are removed."),
                            QStringLiteral("DELETE"), QStringLiteral("project-delete"), -1);
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
