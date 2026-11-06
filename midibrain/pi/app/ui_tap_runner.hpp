#pragma once
// Headless QML tap driver (`--ui-taps`).
//
// Visits every screen (primary and SHIFT faces), every ConfigPopup kind and the
// shared TransportStrip, finds every visible QML item that carries a non-empty
// `focusId`, and sends a real QMouseEvent press/release pair at its centre
// through QQuickWindow. This is the in-process path a finger takes: a QML
// handler that calls a missing Q_INVOKABLE (the setTab/setShiftPage class of
// bug) fails at that tap. Any QML TypeError/ReferenceError/warning raised while
// tapping is collected and makes the process exit non-zero.
//
// The run is offscreen and isolated: main() points HOME at a temporary
// directory first, so a tap on a PROJECTS/recorder control cannot touch the
// user's projects or takes.
#include <QObject>
#include <QStringList>
#include <QtGlobal>

#include <functional>
#include <vector>

class QQuickItem;
class QQuickWindow;

namespace mnky::app {

class AppController;

class UiTapRunner final : public QObject {
  Q_OBJECT

 public:
  UiTapRunner(AppController* controller, QQuickWindow* window,
              QObject* parent = nullptr);
  ~UiTapRunner() override;

  void start();

  // Installed for the whole run so QML errors are attributed to the tap that
  // raised them; restoring happens in the destructor.
  static void messageHandler(QtMsgType type, const QMessageLogContext& context,
                             const QString& message);

 private:
  struct Target {
    QQuickItem* item = nullptr;
    QString focusId;
  };
  struct Scenario {
    QString name;
    std::function<void()> setup;
    QString allowedPrefix;
    QStringList excludedPrefixes;
    QStringList terminalIds;  // tapped only after every other target
    bool onlyTerminal = false;
    bool expectTargets = false;
  };

  void buildScenarios();
  void closeOverlays();
  void beginScenario();
  void tapNext();
  void finishScenario();
  void finish();
  void scheduleNext(int delayMs);
  std::vector<Target> visibleTargets() const;
  static void collectTargets(QQuickItem* item, std::vector<Target>& targets);
  static bool tap(QQuickWindow* window, QQuickItem* item);
  static bool isQmlWarning(const QMessageLogContext& context,
                           const QString& message);
  void recordQmlWarning(const QMessageLogContext& context, const QString& message);

  AppController* controller_;
  QQuickWindow* window_;
  std::vector<Scenario> scenarios_;
  std::size_t scenarioIndex_ = 0;
  bool scenarioStarted_ = false;
  bool scenarioSawTarget_ = false;
  QStringList tapped_;
  QStringList warnings_;
  int taps_ = 0;
  int failures_ = 0;
  QtMessageHandler previousHandler_ = nullptr;
  static UiTapRunner* active_;
};

}  // namespace mnky::app
