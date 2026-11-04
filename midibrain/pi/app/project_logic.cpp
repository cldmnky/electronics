#include "project_logic.hpp"
#include "text_buffer.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>

namespace mnky::app {
namespace {

std::string validateName(std::string_view name, int maxLength, bool requireTakePrefix) {
  if (name.empty()) return "name is empty";
  if (static_cast<int>(name.size()) > maxLength) {
    return "name is longer than " + std::to_string(maxLength) + " characters";
  }
  if (requireTakePrefix && name.substr(0, kTakePrefix.size()) != kTakePrefix) {
    // The v2 per-track naming form is the other accepted take name.
    if (!parseTakeFileName(name).valid) {
      return "take name must be take-... or nn-name-nnn";
    }
  }
  if (name.front() == ' ' || name.back() == ' ') return "no leading or trailing space";
  for (char character : name) {
    const auto value = static_cast<unsigned char>(character);
    if (value < 0x20 || value > 0x7e) return "use printable ASCII only";
    if (character == '/' || character == '\\') return "no path separators";
  }
  if (name == "." || name == "..") return "reserved name";
  return {};
}

bool allDigits(std::string_view text) {
  if (text.empty()) return false;
  for (char character : text) {
    if (character < '0' || character > '9') return false;
  }
  return true;
}

}  // namespace

std::string validateProjectName(std::string_view name) {
  return validateName(name, kProjectNameMaxLength, false);
}

std::string validateTakeName(std::string_view name) {
  return validateName(name, kTakeNameMaxLength, true);
}

std::string defaultTrackName(int trackIndex) {
  int display = trackIndex + 1;
  if (display < 1) display = 1;
  std::string out = std::to_string(display);
  if (out.size() < 2) out.insert(out.begin(), '0');
  return "ch" + out;
}

std::string sanitizeTrackName(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  bool lastDash = false;
  for (char character : name) {
    const auto value = static_cast<unsigned char>(character);
    if (value < 0x20 || value > 0x7e) continue;
    char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    const bool keep = (lower >= 'a' && lower <= 'z') ||
                      (lower >= '0' && lower <= '9');
    if (keep) {
      out.push_back(lower);
      lastDash = false;
    } else if (!out.empty() && !lastDash) {
      out.push_back('-');
      lastDash = true;
    }
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  if (out.empty()) out = "track";
  if (out.size() > 20) out.resize(20);
  return out;
}

std::string takeFileName(int trackIndex, std::string_view trackName, int takeNumber) {
  int display = trackIndex + 1;
  if (display < 1) display = 1;
  std::string prefix = std::to_string(display);
  if (prefix.size() < 2) prefix.insert(prefix.begin(), '0');
  std::string number = std::to_string(takeNumber < 1 ? 1 : takeNumber);
  while (number.size() < 3) number.insert(number.begin(), '0');
  return prefix + "-" + sanitizeTrackName(trackName) + "-" + number + ".wav";
}

TakeNameParts parseTakeFileName(std::string_view file) {
  TakeNameParts out;
  if (file.size() >= 4 && file.substr(file.size() - 4) == ".wav") {
    file.remove_suffix(4);
  }
  // nn-name-nnn: two digits, dash, at least one name character, dash, three
  // digits. Names cannot contain a dash at the numbered boundary only.
  if (file.size() < 2 + 1 + 1 + 1 + 3) return out;
  const auto trackText = file.substr(0, 2);
  if (!allDigits(trackText) || file[2] != '-') return out;
  const auto lastDash = file.rfind('-');
  if (lastDash == std::string_view::npos || lastDash <= 2) return out;
  if (file.size() - lastDash - 1 != 3) return out;
  const auto numberText = file.substr(lastDash + 1);
  if (!allDigits(numberText)) return out;
  const auto nameText = file.substr(3, lastDash - 3);
  if (nameText.empty()) return out;
  for (char character : nameText) {
    const auto value = static_cast<unsigned char>(character);
    if (value < 0x20 || value > 0x7e) return out;
    if (character == '/' || character == '\\' || character == '.') return out;
  }
  out.valid = true;
  out.track = (trackText[0] - '0') * 10 + (trackText[1] - '0') - 1;
  out.name = std::string(nameText);
  out.number = (numberText[0] - '0') * 100 + (numberText[1] - '0') * 10 +
               (numberText[2] - '0');
  if (out.track < 0) out.valid = false;
  return out;
}

std::string defaultProjectName(std::int64_t unixSeconds) {
  const auto seconds = static_cast<std::time_t>(unixSeconds);
  std::tm parts{};
#if defined(_WIN32)
  if (gmtime_s(&parts, &seconds) != 0) return "Project-19700101-000000";
#else
  if (gmtime_r(&seconds, &parts) == nullptr) return "Project-19700101-000000";
#endif
  char buffer[40] = {};
  std::snprintf(buffer, sizeof buffer, "Project-%04d%02d%02d-%02d%02d%02d",
                parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour,
                parts.tm_min, parts.tm_sec);
  return buffer;
}

void TextBuffer::setText(std::string_view text) {
  text_.clear();
  text_.reserve(text.size());
  for (char character : text) {
    if (!printableAscii(character)) continue;
    if (static_cast<int>(text_.size()) >= maxLength_) break;
    text_.push_back(character);
  }
  cursor_ = static_cast<int>(text_.size());
}

void TextBuffer::setCursor(int index) {
  if (index < 0) index = 0;
  const int size = static_cast<int>(text_.size());
  cursor_ = index > size ? size : index;
}

void TextBuffer::moveLeft() { setCursor(cursor_ - 1); }
void TextBuffer::moveRight() { setCursor(cursor_ + 1); }
void TextBuffer::home() { setCursor(0); }
void TextBuffer::end() { setCursor(static_cast<int>(text_.size())); }

bool TextBuffer::insert(char character) {
  if (!printableAscii(character)) return false;
  if (static_cast<int>(text_.size()) >= maxLength_) return false;
  text_.insert(text_.begin() + cursor_, character);
  ++cursor_;
  return true;
}

bool TextBuffer::insert(std::string_view characters) {
  bool changed = false;
  for (char character : characters) changed = insert(character) || changed;
  return changed;
}

bool TextBuffer::backspace() {
  if (cursor_ <= 0 || text_.empty()) return false;
  text_.erase(text_.begin() + (cursor_ - 1));
  --cursor_;
  return true;
}

bool TextBuffer::deleteForward() {
  if (cursor_ < 0 || cursor_ >= static_cast<int>(text_.size())) return false;
  text_.erase(text_.begin() + cursor_);
  return true;
}

bool TextBuffer::clear() {
  if (text_.empty()) {
    cursor_ = 0;
    return false;
  }
  text_.clear();
  cursor_ = 0;
  return true;
}

bool DirtyTracker::poll(std::string_view fingerprint) {
  if (!initialized_) {
    baseline_ = fingerprint;
    initialized_ = true;
    dirty_ = false;
    return false;
  }
  if (fingerprint != baseline_) dirty_ = true;
  return dirty_;
}

void DirtyTracker::clear(std::string_view fingerprint) {
  baseline_ = fingerprint;
  initialized_ = true;
  dirty_ = false;
}

bool ChordDetector::onPress(Button button, std::uint32_t nowMs) {
  const Button other = button == Button::Play ? Button::Rec : Button::Play;
  if (has(other)) {
    const std::uint32_t elapsed = nowMs - stamp(other);
    if (elapsed <= kWindowMs) {
      clearButton(button);
      clearButton(other);
      return true;
    }
  }
  // A press outside the window replaces this button's edge; the other edge is
  // left in place so a late third press can still pair with it.
  set(button, nowMs);
  return false;
}

void ChordDetector::reset() {
  hasPlay_ = false;
  hasRec_ = false;
  playMs_ = 0;
  recMs_ = 0;
}

}  // namespace mnky::app
