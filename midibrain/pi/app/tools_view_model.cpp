#include "tools_view_model.hpp"

#include <QCoreApplication>
#include <QSettings>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <system_error>

#include "logic.hpp"

#ifdef __linux__
#include <fstream>
#endif

namespace mnky::app {
namespace {
QString utf8(const std::string& text) { return QString::fromStdString(text); }

#ifdef __linux__
std::filesystem::path findBacklight() {
  std::error_code error;
  const std::filesystem::path root("/sys/class/backlight");
  for (std::filesystem::directory_iterator it(root, error), end; it != end;
       it.increment(error)) {
    if (error) break;
    std::error_code typeError;
    if (it->is_directory(typeError)) return it->path();
  }
  return {};
}
#endif
}  // namespace

ToolsViewModel::ToolsViewModel(AppState& state, LinkClient& link, QObject* parent)
    : QObject(parent), state_(state), link_(link) {
  loadBrightness();
  refresh();
}

QString ToolsViewModel::appVersion() const {
  const QString version = QCoreApplication::applicationVersion();
  return version.isEmpty() ? QStringLiteral("unknown") : version;
}

QString ToolsViewModel::firmwareVersion() const {
  if (link_.noPico()) {
    return QStringLiteral("n/a - NO PICO mode (no Pico attached)");
  }
  if (link_.emulated()) {
    return QStringLiteral("emulator (scripted Pico, no firmware image)");
  }
  return QStringLiteral("not reported by the link status (no version field)");
}

std::uint64_t ToolsViewModel::readDisk(std::uint64_t* capacity) const {
  std::error_code error;
  const auto space = std::filesystem::space(state_.takeDirectory(), error);
  if (error) {
    if (capacity != nullptr) *capacity = 0;
    return 0;
  }
  if (capacity != nullptr) *capacity = static_cast<std::uint64_t>(space.capacity);
  return static_cast<std::uint64_t>(space.available);
}

void ToolsViewModel::refresh() {
  std::uint64_t capacity = 0;
  const std::uint64_t available = readDisk(&capacity);
  const bool statOk = capacity != 0;
  lowSpace_ = demoLowSpace_ || (statOk && isLowDiskSpace(available, capacity));
  const QString directory = utf8(state_.takeDirectory().string());
  QString disk;
  QString free;
  if (demoLowSpace_) {
    // The screenshot proof runs on a host with plenty of space; show a
    // consistent low-space state instead of a banner over a healthy readout.
    disk = QStringLiteral("DISK  1.1 GiB free of 19.5 GiB");
    free = QStringLiteral("1.1 GiB free");
  } else if (statOk) {
    disk = QStringLiteral("DISK  %1 free of %2")
               .arg(utf8(formatBytes(static_cast<std::int64_t>(available))))
               .arg(utf8(formatBytes(static_cast<std::int64_t>(capacity))));
    free = utf8(formatBytes(static_cast<std::int64_t>(available))) +
           QStringLiteral(" free");
  } else {
    disk = QStringLiteral("DISK  unavailable (take volume not readable)");
    free = QStringLiteral("unavailable");
  }
  const QString banner =
      lowSpace_ ? QStringLiteral("LOW SPACE - recording stops when the take volume "
                                 "fills; free space or move finished takes")
                : QString();
  if (disk == diskText_ && free == diskFreeText_ && directory == takeDirectoryText_ &&
      banner == bannerText_) {
    return;
  }
  diskText_ = disk;
  diskFreeText_ = free;
  takeDirectoryText_ = directory;
  bannerText_ = banner;
  emit statusChanged();
}

void ToolsViewModel::loadBrightness() {
  QSettings settings;
  brightness_ = std::clamp(settings.value(QStringLiteral("tools/brightness"), 50).toInt(),
                           5, 100);
#ifdef __linux__
  const auto backlight = findBacklight();
  brightnessAvailable_ = !backlight.empty();
  if (!brightnessAvailable_) {
    brightnessNote_ = QStringLiteral("No /sys/class/backlight device on this host; "
                                     "brightness is stored but not applied.");
    return;
  }
  brightnessNote_ = QStringLiteral("Applied to ") +
                    utf8(backlight.filename().string());
  setBrightness(brightness_);
#else
  brightnessAvailable_ = false;
  brightnessNote_ = QStringLiteral("Backlight control is a Linux/Pi sysfs path; "
                                   "stored only on this desktop.");
#endif
}

void ToolsViewModel::showLowSpaceDemo() {
  demoLowSpace_ = true;
  refresh();
  if (!lowSpace_) {
    lowSpace_ = true;
    bannerText_ = QStringLiteral("LOW SPACE - recording stops when the take volume "
                                 "fills; free space or move finished takes");
    emit statusChanged();
  }
}

void ToolsViewModel::setBrightness(int percent) {
  const int clamped = std::clamp(percent, 5, 100);
  if (clamped == brightness_) {
    emit brightnessChanged();
    return;
  }
  brightness_ = clamped;
  QSettings settings;
  settings.setValue(QStringLiteral("tools/brightness"), brightness_);
#ifdef __linux__
  const auto backlight = findBacklight();
  if (!backlight.empty()) {
    std::ifstream maxFile(backlight / "max_brightness");
    int maximum = 0;
    if (maxFile >> maximum && maximum > 0) {
      std::ofstream valueFile(backlight / "brightness");
      if (valueFile) {
        valueFile << std::max(1, maximum * brightness_ / 100) << "\n";
        brightnessAvailable_ = true;
      }
    }
  }
#endif
  emit brightnessChanged();
}

}  // namespace mnky::app
