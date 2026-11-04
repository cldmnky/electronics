#include "project_logic.hpp"
#include "text_buffer.hpp"

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
    return "take name must start with take-";
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

}  // namespace

std::string validateProjectName(std::string_view name) {
  return validateName(name, kProjectNameMaxLength, false);
}

std::string validateTakeName(std::string_view name) {
  return validateName(name, kTakeNameMaxLength, true);
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
