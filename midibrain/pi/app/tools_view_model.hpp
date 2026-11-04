#pragma once
// QML view model for the TOOLS status page: take-volume free space with the
// low-space banner, screen brightness and the app/firmware version readout.
// Brightness is a best-effort Linux backlight write; on a desktop with no
// backlight it is reported as unavailable instead of pretending to work.
#include <QObject>
#include <QString>

#include "app_state.hpp"
#include "link_client.hpp"

namespace mnky::app {

class ToolsViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString diskText READ diskText NOTIFY statusChanged)
  Q_PROPERTY(QString diskFreeText READ diskFreeText NOTIFY statusChanged)
  Q_PROPERTY(QString takeDirectoryText READ takeDirectoryText NOTIFY statusChanged)
  Q_PROPERTY(bool lowSpace READ lowSpace NOTIFY statusChanged)
  Q_PROPERTY(QString bannerText READ bannerText NOTIFY statusChanged)
  Q_PROPERTY(int brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged)
  Q_PROPERTY(bool brightnessAvailable READ brightnessAvailable NOTIFY brightnessChanged)
  Q_PROPERTY(QString brightnessNote READ brightnessNote NOTIFY brightnessChanged)
  Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
  Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY statusChanged)

 public:
  ToolsViewModel(AppState& state, LinkClient& link, QObject* parent = nullptr);

  QString diskText() const { return diskText_; }
  QString diskFreeText() const { return diskFreeText_; }
  QString takeDirectoryText() const { return takeDirectoryText_; }
  bool lowSpace() const { return lowSpace_; }
  QString bannerText() const { return bannerText_; }
  int brightness() const { return brightness_; }
  bool brightnessAvailable() const { return brightnessAvailable_; }
  QString brightnessNote() const { return brightnessNote_; }
  QString appVersion() const;
  QString firmwareVersion() const;

  void setBrightness(int percent);

  // Deterministic low-space banner for the offscreen screenshot run.
  Q_INVOKABLE void showLowSpaceDemo();

  // Re-read disk space and the link state; called about once a second.
  void refresh();

 signals:
  void statusChanged();
  void brightnessChanged();

 private:
  void loadBrightness();
  std::uint64_t readDisk(std::uint64_t* capacity) const;

  AppState& state_;
  LinkClient& link_;
  QString diskText_;
  QString diskFreeText_;
  QString takeDirectoryText_;
  bool lowSpace_ = false;
  bool demoLowSpace_ = false;
  QString bannerText_;
  int brightness_ = 50;
  bool brightnessAvailable_ = false;
  QString brightnessNote_;
};

}  // namespace mnky::app
