#include "session.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <exception>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace mnky::session {
namespace {

constexpr std::size_t kMaxFileBytes = 4u * 1024u * 1024u;
constexpr int kMaxJsonDepth = 64;

// ---------------------------------------------------------------- JSON model
// Minimal DOM for the documented subset. Object members keep their file order;
// duplicate keys resolve to the first occurrence, which is also what a
// tolerant reader should do with a hand-edited file.

struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  // Exact float parse of the same token. Kept separately so a float field
  // round-trips through its shortest representation without double rounding,
  // while a double field may still use values outside float range.
  float floatNumber = 0.0f;
  bool hasFloat = false;
  std::string string;
  std::vector<JsonValue> array;
  std::vector<std::pair<std::string, JsonValue>> object;

  const JsonValue* find(std::string_view key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& member : object) {
      if (std::string_view(member.first) == key) return &member.second;
    }
    return nullptr;
  }
};

bool isDigit(char c) { return c >= '0' && c <= '9'; }

void appendUtf8(std::string& out, std::uint32_t codePoint) {
  if (codePoint <= 0x7F) {
    out.push_back(static_cast<char>(codePoint));
  } else if (codePoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else if (codePoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  }
}

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) : text_(text) {}

  bool parse(JsonValue& out) {
    skipWhitespace();
    if (!parseValue(out, 0)) return false;
    skipWhitespace();
    if (pos_ != text_.size()) return fail("trailing characters after JSON document");
    return true;
  }

  const std::string& error() const { return error_; }

 private:
  bool fail(std::string message) {
    if (!error_.empty()) return false;
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
      if (text_[i] == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
    }
    error_ = "parse error at line " + std::to_string(line) + " column " +
             std::to_string(column) + ": " + std::move(message);
    return false;
  }

  void skipWhitespace() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
      ++pos_;
    }
  }

  bool parseValue(JsonValue& value, int depth) {
    if (depth > kMaxJsonDepth) return fail("nesting too deep");
    skipWhitespace();
    if (pos_ >= text_.size()) return fail("unexpected end of input");
    switch (text_[pos_]) {
      case '{':
        return parseObject(value, depth);
      case '[':
        return parseArray(value, depth);
      case '"':
        return parseString(value);
      case 't':
        return parseLiteral("true", JsonValue::Type::Bool, true, value);
      case 'f':
        return parseLiteral("false", JsonValue::Type::Bool, false, value);
      case 'n':
        return parseLiteral("null", JsonValue::Type::Null, false, value);
      default:
        break;
    }
    if (text_[pos_] == '-' || isDigit(text_[pos_])) return parseNumber(value);
    return fail("unexpected character");
  }

  bool parseLiteral(std::string_view literal, JsonValue::Type type, bool boolean,
                    JsonValue& value) {
    if (text_.substr(pos_, literal.size()) != literal) return fail("invalid literal");
    pos_ += literal.size();
    value.type = type;
    value.boolean = boolean;
    return true;
  }

  bool parseObject(JsonValue& value, int depth) {
    ++pos_;  // '{'
    value.type = JsonValue::Type::Object;
    skipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return true;
    }
    while (true) {
      skipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected object key");
      JsonValue key;
      if (!parseString(key)) return false;
      skipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        return fail("expected ':' after object key");
      }
      ++pos_;
      JsonValue member;
      if (!parseValue(member, depth + 1)) return false;
      value.object.emplace_back(std::move(key.string), std::move(member));
      skipWhitespace();
      if (pos_ >= text_.size()) return fail("unterminated object");
      const char c = text_[pos_++];
      if (c == '}') return true;
      if (c != ',') return fail("expected ',' or '}'");
    }
  }

  bool parseArray(JsonValue& value, int depth) {
    ++pos_;  // '['
    value.type = JsonValue::Type::Array;
    skipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return true;
    }
    while (true) {
      JsonValue item;
      if (!parseValue(item, depth + 1)) return false;
      value.array.push_back(std::move(item));
      skipWhitespace();
      if (pos_ >= text_.size()) return fail("unterminated array");
      const char c = text_[pos_++];
      if (c == ']') return true;
      if (c != ',') return fail("expected ',' or ']'");
    }
  }

  bool parseHex4(std::uint32_t& out) {
    if (pos_ + 4 > text_.size()) return fail("short \\u escape");
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return fail("invalid hex digit in \\u escape");
      }
    }
    out = value;
    return true;
  }

  bool parseString(JsonValue& value) {
    ++pos_;  // opening quote
    std::string out;
    while (pos_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
      if (c == '"') {
        value.type = JsonValue::Type::String;
        value.string = std::move(out);
        return true;
      }
      if (c < 0x20) return fail("control character in string");
      if (c != '\\') {
        out.push_back(static_cast<char>(c));
        continue;
      }
      if (pos_ >= text_.size()) return fail("unterminated escape");
      const char escape = text_[pos_++];
      switch (escape) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          std::uint32_t codePoint = 0;
          if (!parseHex4(codePoint)) return false;
          if (codePoint >= 0xD800 && codePoint <= 0xDBFF) {
            if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' ||
                text_[pos_ + 1] != 'u') {
              return fail("unpaired high surrogate");
            }
            pos_ += 2;
            std::uint32_t low = 0;
            if (!parseHex4(low)) return false;
            if (low < 0xDC00 || low > 0xDFFF) return fail("invalid low surrogate");
            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
          } else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF) {
            return fail("unpaired low surrogate");
          }
          appendUtf8(out, codePoint);
          break;
        }
        default:
          return fail("invalid escape");
      }
    }
    return fail("unterminated string");
  }

  bool parseNumber(JsonValue& value) {
    const std::size_t start = pos_;
    if (text_[pos_] == '-') ++pos_;
    if (pos_ >= text_.size()) return fail("invalid number");
    if (text_[pos_] == '0') {
      ++pos_;
      if (pos_ < text_.size() && isDigit(text_[pos_])) {
        return fail("leading zero in number");
      }
    } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
      while (pos_ < text_.size() && isDigit(text_[pos_])) ++pos_;
    } else {
      return fail("invalid number");
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      if (pos_ >= text_.size() || !isDigit(text_[pos_])) {
        return fail("missing fraction digits");
      }
      while (pos_ < text_.size() && isDigit(text_[pos_])) ++pos_;
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
      if (pos_ >= text_.size() || !isDigit(text_[pos_])) {
        return fail("missing exponent digits");
      }
      while (pos_ < text_.size() && isDigit(text_[pos_])) ++pos_;
    }

    double number = 0.0;
    const char* first = text_.data() + start;
    const char* last = text_.data() + pos_;
    const auto result = std::from_chars(first, last, number);
    if (result.ec != std::errc{} || result.ptr != last || !std::isfinite(number)) {
      return fail("number out of range");
    }
    value.type = JsonValue::Type::Number;
    value.number = number;
    float floatNumber = 0.0f;
    const auto floatResult = std::from_chars(first, last, floatNumber);
    if (floatResult.ec == std::errc{} && floatResult.ptr == last &&
        std::isfinite(floatNumber)) {
      value.floatNumber = floatNumber;
      value.hasFloat = true;
    }
    return true;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::string error_;
};

// ------------------------------------------------------------- field readers

bool readInt(const JsonValue& value, int& out) {
  if (value.type != JsonValue::Type::Number) return false;
  if (!std::isfinite(value.number) || std::floor(value.number) != value.number) {
    return false;
  }
  if (value.number < static_cast<double>(std::numeric_limits<int>::min()) ||
      value.number > static_cast<double>(std::numeric_limits<int>::max())) {
    return false;
  }
  out = static_cast<int>(value.number);
  return true;
}

bool readFloat(const JsonValue& value, float& out) {
  if (value.type != JsonValue::Type::Number || !value.hasFloat) return false;
  out = value.floatNumber;
  return true;
}

bool readDouble(const JsonValue& value, double& out) {
  if (value.type != JsonValue::Type::Number || !std::isfinite(value.number)) {
    return false;
  }
  out = value.number;
  return true;
}

bool readBool(const JsonValue& value, bool& out) {
  if (value.type != JsonValue::Type::Bool) return false;
  out = value.boolean;
  return true;
}

bool readOptionalDouble(const JsonValue& object, const char* key, double& out,
                        const std::string& path, std::string& error) {
  const JsonValue* value = object.find(key);
  if (value == nullptr) return true;
  if (!readDouble(*value, out)) {
    error = "session: \"" + path + "." + key + "\" is not a number";
    return false;
  }
  return true;
}

bool readString(const JsonValue& value, std::string& out) {
  if (value.type != JsonValue::Type::String) return false;
  out = value.string;
  return true;
}

bool readOptionalInt(const JsonValue& object, const char* key, int& out,
                     const std::string& path, std::string& error) {
  const JsonValue* value = object.find(key);
  if (value == nullptr) return true;
  if (!readInt(*value, out)) {
    error = "session: \"" + path + "." + key + "\" is not an integer";
    return false;
  }
  return true;
}

bool readOptionalFloat(const JsonValue& object, const char* key, float& out,
                       const std::string& path, std::string& error) {
  const JsonValue* value = object.find(key);
  if (value == nullptr) return true;
  if (!readFloat(*value, out)) {
    error = "session: \"" + path + "." + key + "\" is not a number";
    return false;
  }
  return true;
}

bool readOptionalBool(const JsonValue& object, const char* key, bool& out,
                      const std::string& path, std::string& error) {
  const JsonValue* value = object.find(key);
  if (value == nullptr) return true;
  if (!readBool(*value, out)) {
    error = "session: \"" + path + "." + key + "\" is not a boolean";
    return false;
  }
  return true;
}

bool parsePortKey(const std::string& key, int& port) {
  if (key.empty()) return false;
  int parsed = 0;
  const auto result =
      std::from_chars(key.data(), key.data() + key.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != key.data() + key.size()) {
    return false;
  }
  if (parsed < 0 || parsed >= midi::kPortCount) return false;
  port = parsed;
  return true;
}

// ------------------------------------------------------------------ decoders

bool decodeRouter(const JsonValue& node, RouterState& out, std::string& error) {
  if (const JsonValue* routes = node.find("routes")) {
    if (routes->type != JsonValue::Type::Array) {
      error = "session: \"router.routes\" is not an array";
      return false;
    }
    std::size_t index = 0;
    for (const JsonValue& item : routes->array) {
      const std::string path = "router.routes[" + std::to_string(index) + "]";
      ++index;
      if (item.type != JsonValue::Type::Object) {
        error = "session: \"" + path + "\" is not an object";
        return false;
      }
      RouteState route;
      if (!readOptionalInt(item, "src", route.src, path, error)) return false;
      if (!readOptionalInt(item, "dst", route.dst, path, error)) return false;
      if (!readOptionalBool(item, "on", route.on, path, error)) return false;
      if (const JsonValue* pipes = item.find("pipes")) {
        if (pipes->type != JsonValue::Type::Array) {
          error = "session: \"" + path + ".pipes\" is not an array";
          return false;
        }
        std::size_t pipeIndex = 0;
        for (const JsonValue& pipeItem : pipes->array) {
          const std::string pipePath = path + ".pipes[" +
                                       std::to_string(pipeIndex++) + "]";
          if (pipeItem.type != JsonValue::Type::Object) {
            error = "session: \"" + pipePath + "\" is not an object";
            return false;
          }
          MidiPipe pipe;
          int type = 0;
          if (!readOptionalInt(pipeItem, "t", type, pipePath, error)) {
            return false;
          }
          if (type < 1 || type > 16) {
            error = "session: \"" + pipePath + ".t\" is not a pipe type";
            return false;
          }
          pipe.type = static_cast<MidiPipeType>(type);
          if (!readOptionalBool(pipeItem, "bypass", pipe.bypass, pipePath,
                                 error)) {
            return false;
          }
          if (const JsonValue* params = pipeItem.find("params")) {
            if (params->type != JsonValue::Type::Array) {
              error = "session: \"" + pipePath +
                      ".params\" is not an array";
              return false;
            }
            for (const JsonValue& value : params->array) {
              int param = 0;
              if (!readInt(value, param)) {
                error = "session: \"" + pipePath +
                        ".params\" holds a non-integer";
                return false;
              }
              pipe.params.push_back(param);
            }
          }
          route.pipes.push_back(std::move(pipe));
        }
      }
      out.routes.push_back(std::move(route));
    }
  }

  if (const JsonValue* channels = node.find("channels")) {
    if (channels->type != JsonValue::Type::Object) {
      error = "session: \"router.channels\" is not an object";
      return false;
    }
    for (const auto& member : channels->object) {
      int port = 0;
      if (!parsePortKey(member.first, port)) continue;  // unknown port key
      int mask = 0;
      if (!readInt(member.second, mask)) {
        error = "session: \"router.channels." + member.first +
                "\" is not an integer";
        return false;
      }
      if (mask < 0) mask = 0;
      if (mask > 0xFFFF) mask = 0xFFFF;
      out.channels[static_cast<std::size_t>(port)] =
          static_cast<std::uint16_t>(mask);
    }
  }

  if (const JsonValue* transpose = node.find("transpose")) {
    if (transpose->type != JsonValue::Type::Object) {
      error = "session: \"router.transpose\" is not an object";
      return false;
    }
    for (const auto& member : transpose->object) {
      int port = 0;
      if (!parsePortKey(member.first, port)) continue;  // unknown port key
      int semitones = 0;
      if (!readInt(member.second, semitones)) {
        error = "session: \"router.transpose." + member.first +
                "\" is not an integer";
        return false;
      }
      out.transpose[static_cast<std::size_t>(port)] = semitones;
    }
  }
  return true;
}

bool decodeMixer(const JsonValue& node, MixerState& out, std::string& error) {
  if (const JsonValue* channels = node.find("channels")) {
    if (channels->type != JsonValue::Type::Array) {
      error = "session: \"mixer.channels\" is not an array";
      return false;
    }
    std::size_t index = 0;
    for (const JsonValue& item : channels->array) {
      if (index >= static_cast<std::size_t>(audio::kMaxInputs)) break;
      const std::string path = "mixer.channels[" + std::to_string(index) + "]";
      if (item.type != JsonValue::Type::Object) {
        error = "session: \"" + path + "\" is not an object";
        return false;
      }
      MixerChannelState& channel = out.channels[index];
      if (!readOptionalFloat(item, "gain", channel.gain, path, error)) {
        return false;
      }
      if (!readOptionalFloat(item, "pan", channel.pan, path, error)) {
        return false;
      }
      if (!readOptionalBool(item, "mute", channel.mute, path, error)) {
        return false;
      }
      if (!readOptionalBool(item, "solo", channel.solo, path, error)) {
        return false;
      }
      if (!readOptionalBool(item, "arm", channel.arm, path, error)) {
        return false;
      }
      if (!readOptionalFloat(item, "cue", channel.cueSend, path, error)) {
        return false;
      }
      if (!readOptionalFloat(item, "out34", channel.out34Send, path, error)) {
        return false;
      }
      int monitor = channel.monitor;
      if (!readOptionalInt(item, "monitor", monitor, path, error)) {
        return false;
      }
      if (monitor < 0) monitor = 0;
      if (monitor > 2) monitor = 2;
      channel.monitor = monitor;
      if (const JsonValue* send = item.find("send")) {
        if (send->type != JsonValue::Type::Array) {
          error = "session: \"" + path + ".send\" is not an array";
          return false;
        }
        std::size_t sendIndex = 0;
        for (const JsonValue& value : send->array) {
          if (sendIndex >= static_cast<std::size_t>(audio::kNumSends)) break;
          if (!readFloat(value, channel.send[sendIndex])) {
            error = "session: \"" + path + ".send[" +
                    std::to_string(sendIndex) + "]\" is not a number";
            return false;
          }
          ++sendIndex;
        }
      }
      ++index;
    }
  }

  if (!readOptionalFloat(node, "master", out.master, "mixer", error)) {
    return false;
  }
  int out34Mode = out.out34Mode;
  if (!readOptionalInt(node, "out34mode", out34Mode, "mixer", error)) {
    return false;
  }
  if (out34Mode < 0) out34Mode = 0;
  if (out34Mode > 1) out34Mode = 1;
  out.out34Mode = out34Mode;
  return true;
}

bool decodeFx(const JsonValue& node, FxState& out, std::string& error) {
  const JsonValue* bus = node.find("bus");
  if (bus == nullptr) return true;
  if (bus->type != JsonValue::Type::Array) {
    error = "session: \"fx.bus\" is not an array";
    return false;
  }
  std::size_t index = 0;
  for (const JsonValue& item : bus->array) {
    if (index >= static_cast<std::size_t>(kFxBuses)) break;
    const std::string path = "fx.bus[" + std::to_string(index) + "]";
    if (item.type != JsonValue::Type::Object) {
      error = "session: \"" + path + "\" is not an object";
      return false;
    }
    FxBusState& slot = out.bus[index];
    if (const JsonValue* id = item.find("id")) {
      if (!readString(*id, slot.id)) {
        error = "session: \"" + path + ".id\" is not a string";
        return false;
      }
    }
    if (const JsonValue* params = item.find("params")) {
      if (params->type != JsonValue::Type::Array) {
        error = "session: \"" + path + ".params\" is not an array";
        return false;
      }
      std::size_t paramIndex = 0;
      for (const JsonValue& value : params->array) {
        double param = 0.0;
        if (!readDouble(value, param)) {
          error = "session: \"" + path + ".params[" +
                  std::to_string(paramIndex) + "]\" is not a number";
          return false;
        }
        slot.params.push_back(param);
        ++paramIndex;
      }
    }
    if (const JsonValue* bypass = item.find("bypass")) {
      if (!readBool(*bypass, slot.bypass)) {
        error = "session: \"" + path + ".bypass\" is not a boolean";
        return false;
      }
    }
    ++index;
  }
  return true;
}

bool decodeTransport(const JsonValue& node, TransportState& out,
                     std::string& error) {
  int bpm = out.bpmX100;
  if (!readOptionalInt(node, "bpm_x100", bpm, "transport", error)) return false;
  if (bpm < midi::UsbClock::kMinBpmX100) bpm = midi::UsbClock::kMinBpmX100;
  if (bpm > midi::UsbClock::kMaxBpmX100) bpm = midi::UsbClock::kMaxBpmX100;
  out.bpmX100 = bpm;
  return true;
}

bool decodeClick(const JsonValue& node, ClickState& out, std::string& error) {
  // v1 legacy first: enabled/level map onto mode/gainDb unless v2 wins.
  bool legacyEnabled = out.enabled;
  bool haveLegacyEnabled = node.find("enabled") != nullptr;
  if (!readOptionalBool(node, "enabled", legacyEnabled, "click", error)) {
    return false;
  }
  float legacyLevel = out.level;
  bool haveLegacyLevel = node.find("level") != nullptr;
  if (!readOptionalFloat(node, "level", legacyLevel, "click", error)) {
    return false;
  }
  if (legacyLevel < 0.0f) legacyLevel = 0.0f;
  if (legacyLevel > 1.0f) legacyLevel = 1.0f;
  out.enabled = legacyEnabled;
  out.level = legacyLevel;

  bool haveV2 = node.find("mode") != nullptr ||
                node.find("output") != nullptr ||
                node.find("gainDb") != nullptr;
  int mode = out.mode;
  if (!readOptionalInt(node, "mode", mode, "click", error)) return false;
  if (mode < 0) mode = 0;
  if (mode > 2) mode = 2;
  int output = out.output;
  if (!readOptionalInt(node, "output", output, "click", error)) return false;
  if (output < 0) output = 0;
  if (output > 2) output = 2;
  float gainDb = out.gainDb;
  if (!readOptionalFloat(node, "gainDb", gainDb, "click", error)) {
    return false;
  }
  if (gainDb < -96.0f) gainDb = -96.0f;
  if (gainDb > 20.0f) gainDb = 20.0f;
  if (haveV2) {
    out.mode = mode;
    out.output = output;
    out.gainDb = gainDb;
  } else if (haveLegacyEnabled || haveLegacyLevel) {
    out.mode = legacyEnabled ? 2 : 0;
    out.output = 0;
    // Rough v1 level 0..1 to dB for continuity; silence stays silent.
    out.gainDb = legacyLevel <= 0.0f ? -96.0f : 20.0f * (legacyLevel - 1.0f);
  }
  return readOptionalInt(node, "bars", out.bars, "click", error);
}

bool decodePunchPoint(const JsonValue& node, const std::string& path,
                      PunchPoint& out, std::string& error) {
  if (!readOptionalBool(node, "enabled", out.enabled, path, error)) {
    return false;
  }
  int measure = out.measure;
  if (!readOptionalInt(node, "measure", measure, path, error)) return false;
  if (measure < 1) measure = 1;
  out.measure = measure;
  return true;
}

bool decodePunch(const JsonValue& node, PunchState& out, std::string& error) {
  if (const JsonValue* playFrom = node.find("playFrom")) {
    if (playFrom->type != JsonValue::Type::Object) {
      error = "session: \"punch.playFrom\" is not an object";
      return false;
    }
    if (!decodePunchPoint(*playFrom, "punch.playFrom", out.playFrom, error)) {
      return false;
    }
  }
  if (const JsonValue* in = node.find("in")) {
    if (in->type != JsonValue::Type::Object) {
      error = "session: \"punch.in\" is not an object";
      return false;
    }
    if (!decodePunchPoint(*in, "punch.in", out.in, error)) return false;
  }
  if (const JsonValue* punchOut = node.find("out")) {
    if (punchOut->type != JsonValue::Type::Object) {
      error = "session: \"punch.out\" is not an object";
      return false;
    }
    if (!decodePunchPoint(*punchOut, "punch.out", out.out, error)) {
      return false;
    }
  }
  return true;
}

bool decodeTakes(const JsonValue& node, TakesState& out, std::string& error) {
  const JsonValue* active = node.find("active");
  if (active == nullptr) return true;
  if (active->type != JsonValue::Type::Object) {
    error = "session: \"takes.active\" is not an object";
    return false;
  }
  for (const auto& member : active->object) {
    int track = -1;
    try {
      std::size_t pos = 0;
      track = std::stoi(member.first, &pos);
      if (pos != member.first.size()) track = -1;
    } catch (...) {
      track = -1;
    }
    if (track < 0 || track >= audio::kMaxInputs) continue;  // unknown track
    if (member.second.type != JsonValue::Type::String) {
      error = "session: \"takes.active." + member.first +
              "\" is not a string";
      return false;
    }
    out.active[static_cast<std::size_t>(track)] = member.second.string;
  }
  const JsonValue* loop = node.find("loop");
  if (loop != nullptr) {
  if (loop->type != JsonValue::Type::Object) {
    error = "session: \"takes.loop\" is not an object";
    return false;
  }
  for (const auto& member : loop->object) {
    int track = -1;
    try {
      std::size_t pos = 0;
      track = std::stoi(member.first, &pos);
      if (pos != member.first.size()) track = -1;
    } catch (...) {
      track = -1;
    }
    if (track < 0 || track >= audio::kMaxInputs) continue;
    if (member.second.type != JsonValue::Type::Object) {
      error = "session: \"takes.loop." + member.first +
              "\" is not an object";
      return false;
    }
    LoopState& state = out.loop[static_cast<std::size_t>(track)];
    const std::string path = "takes.loop." + member.first;
    if (!readOptionalBool(member.second, "enabled", state.enabled, path,
                           error)) {
      return false;
    }
    if (!readOptionalDouble(member.second, "start", state.startBeats, path,
                             error)) {
      return false;
    }
    if (!readOptionalDouble(member.second, "end", state.endBeats, path,
                             error)) {
      return false;
    }
    if (state.startBeats < 0.0) state.startBeats = 0.0;
    if (state.endBeats < 0.0) state.endBeats = 0.0;
  }
  }
  const JsonValue* edit = node.find("edit");
  if (edit == nullptr) return true;
  if (edit->type != JsonValue::Type::Object) {
    error = "session: \"takes.edit\" is not an object";
    return false;
  }
  for (const auto& member : edit->object) {
    int track = -1;
    try {
      std::size_t pos = 0;
      track = std::stoi(member.first, &pos);
      if (pos != member.first.size()) track = -1;
    } catch (...) {
      track = -1;
    }
    if (track < 0 || track >= audio::kMaxInputs) continue;
    if (member.second.type != JsonValue::Type::Array) {
      error = "session: \"takes.edit." + member.first +
              "\" is not an array";
      return false;
    }
    std::vector<TakeRegion>& regions =
        out.edit[static_cast<std::size_t>(track)];
    std::size_t regionIndex = 0;
    for (const JsonValue& item : member.second.array) {
      const std::string path = "takes.edit." + member.first + "[" +
                               std::to_string(regionIndex++) + "]";
      if (item.type != JsonValue::Type::Object) {
        error = "session: \"" + path + "\" is not an object";
        return false;
      }
      TakeRegion region;
      const JsonValue* take = item.find("take");
      if (take == nullptr || !readString(*take, region.take)) {
        error = "session: \"" + path + ".take\" is not a string";
        return false;
      }
      if (!readOptionalDouble(item, "offset", region.offsetBeats, path,
                               error)) {
        return false;
      }
      if (!readOptionalDouble(item, "length", region.lengthBeats, path,
                               error)) {
        return false;
      }
      float gainDb = region.gainDb;
      if (!readOptionalFloat(item, "gainDb", gainDb, path, error)) {
        return false;
      }
      if (gainDb < -96.0f) gainDb = -96.0f;
      if (gainDb > 12.0f) gainDb = 12.0f;
      region.gainDb = gainDb;
      if (!readOptionalDouble(item, "fadeIn", region.fadeInBeats, path,
                               error)) {
        return false;
      }
      if (!readOptionalDouble(item, "fadeOut", region.fadeOutBeats, path,
                               error)) {
        return false;
      }
      if (region.offsetBeats < 0.0) region.offsetBeats = 0.0;
      if (region.lengthBeats < 0.0) region.lengthBeats = 0.0;
      if (region.fadeInBeats < 0.0) region.fadeInBeats = 0.0;
      if (region.fadeOutBeats < 0.0) region.fadeOutBeats = 0.0;
      if (!readOptionalBool(item, "normalize", region.normalize, path,
                             error)) {
        return false;
      }
      regions.push_back(std::move(region));
    }
  }
  return true;
}

bool decodeCue(const JsonValue& node, CueState& out, std::string& error) {
  int phonesSrc = out.phonesSrc;
  if (!readOptionalInt(node, "phonesSrc", phonesSrc, "cue", error)) {
    return false;
  }
  if (phonesSrc < 0) phonesSrc = 0;
  if (phonesSrc > 1) phonesSrc = 1;
  out.phonesSrc = phonesSrc;
  int style = out.style;
  if (!readOptionalInt(node, "style", style, "cue", error)) return false;
  if (style < 0) style = 0;
  if (style > 1) style = 1;
  out.style = style;
  return true;
}

bool decodeSends(const JsonValue& node, SendsState& out, std::string& error) {
  const char* const keys[5] = {"cue", "fx1", "fx2", "out34", "rec"};
  int* const fields[5] = {&out.cue, &out.fx1, &out.fx2, &out.out34, &out.rec};
  for (int i = 0; i < 5; ++i) {
    int value = *fields[i];
    if (!readOptionalInt(node, keys[i], value, "sends", error)) return false;
    if (value < 0) value = 0;
    if (value > 1) value = 1;
    *fields[i] = value;
  }
  return true;
}

bool decodeClockRoute(const JsonValue& node, const std::string& path,
                      ClockRoute& out, std::string& error) {
  if (!readOptionalBool(node, "clock", out.clock, path, error)) return false;
  if (!readOptionalBool(node, "transport", out.transport, path, error)) {
    return false;
  }
  int tempoDiv = out.tempoDiv;
  if (!readOptionalInt(node, "tempoDiv", tempoDiv, path, error)) return false;
  if (tempoDiv != 1 && tempoDiv != 2 && tempoDiv != 4) tempoDiv = 1;
  out.tempoDiv = tempoDiv;
  int syncDelayMs = out.syncDelayMs;
  if (!readOptionalInt(node, "syncDelayMs", syncDelayMs, path, error)) {
    return false;
  }
  if (syncDelayMs < 0) syncDelayMs = 0;
  if (syncDelayMs > 500) syncDelayMs = 500;
  out.syncDelayMs = syncDelayMs;
  return true;
}

bool decodeClock(const JsonValue& node, ClockState& out, std::string& error) {
  if (const JsonValue* trs = node.find("trs")) {
    if (trs->type != JsonValue::Type::Array) {
      error = "session: \"clock.trs\" is not an array";
      return false;
    }
    std::size_t index = 0;
    for (const JsonValue& item : trs->array) {
      if (index >= out.trs.size()) break;
      if (item.type != JsonValue::Type::Object) {
        error = "session: \"clock.trs[" + std::to_string(index) +
                "]\" is not an object";
        return false;
      }
      if (!decodeClockRoute(item, "clock.trs[" + std::to_string(index) + "]",
                             out.trs[index], error)) {
        return false;
      }
      ++index;
    }
  }
  if (const JsonValue* usb = node.find("usb")) {
    if (usb->type != JsonValue::Type::Object) {
      error = "session: \"clock.usb\" is not an object";
      return false;
    }
    if (!decodeClockRoute(*usb, "clock.usb", out.usb, error)) return false;
  }
  int source = out.source;
  if (!readOptionalInt(node, "source", source, "clock", error)) return false;
  if (source < 0 || source > 2) source = 0;  // USB source planned, not read
  out.source = source;
  if (const JsonValue* follow = node.find("transportFollow")) {
    if (follow->type != JsonValue::Type::Array) {
      error = "session: \"clock.transportFollow\" is not an array";
      return false;
    }
    std::size_t index = 0;
    for (const JsonValue& value : follow->array) {
      if (index >= out.transportFollow.size()) break;
      if (value.type != JsonValue::Type::Bool) {
        error = "session: \"clock.transportFollow[" +
                std::to_string(index) + "]\" is not a boolean";
        return false;
      }
      out.transportFollow[index] = value.boolean;
      ++index;
    }
  }
  return true;
}

bool decodeMainComp(const JsonValue& node, MainCompState& out,
                    std::string& error) {
  if (!readOptionalBool(node, "on", out.on, "mainComp", error)) return false;
  float threshDb = out.threshDb;
  if (!readOptionalFloat(node, "threshDb", threshDb, "mainComp", error)) {
    return false;
  }
  if (threshDb < -48.0f) threshDb = -48.0f;
  if (threshDb > 0.0f) threshDb = 0.0f;
  out.threshDb = threshDb;
  float ratio = out.ratio;
  if (!readOptionalFloat(node, "ratio", ratio, "mainComp", error)) {
    return false;
  }
  if (ratio < 1.0f) ratio = 1.0f;
  if (ratio > 100.0f) ratio = 100.0f;
  out.ratio = ratio;
  if (!readOptionalFloat(node, "attackMs", out.attackMs, "mainComp", error)) {
    return false;
  }
  if (out.attackMs < 0.0f) out.attackMs = 0.0f;
  if (out.attackMs > 1000.0f) out.attackMs = 1000.0f;
  if (!readOptionalFloat(node, "releaseMs", out.releaseMs, "mainComp", error)) {
    return false;
  }
  if (out.releaseMs < 0.0f) out.releaseMs = 0.0f;
  if (out.releaseMs > 1000.0f) out.releaseMs = 1000.0f;
  if (!readOptionalFloat(node, "makeupDb", out.makeupDb, "mainComp", error)) {
    return false;
  }
  if (out.makeupDb < -36.0f) out.makeupDb = -36.0f;
  if (out.makeupDb > 36.0f) out.makeupDb = 36.0f;
  int mode = out.mode;
  if (!readOptionalInt(node, "mode", mode, "mainComp", error)) return false;
  if (mode < 0) mode = 0;
  if (mode > 1) mode = 1;
  out.mode = mode;
  return true;
}

bool decodeMidiMap(const JsonValue& node, std::vector<MidiMapEntry>& out,
                   std::string& error) {
  if (node.type != JsonValue::Type::Array) {
    error = "session: \"midimap\" is not an array";
    return false;
  }
  std::size_t index = 0;
  for (const JsonValue& item : node.array) {
    const std::string path = "midimap[" + std::to_string(index) + "]";
    ++index;
    if (item.type != JsonValue::Type::Object) {
      error = "session: \"" + path + "\" is not an object";
      return false;
    }
    const JsonValue* cc = item.find("cc");
    const JsonValue* ch = item.find("ch");
    const JsonValue* target = item.find("target");
    const JsonValue* track = item.find("track");
    if (cc == nullptr || ch == nullptr || target == nullptr || track == nullptr) {
      error = "session: \"" + path + "\" needs cc, ch, target and track";
      return false;
    }
    MidiMapEntry entry;
    if (!readInt(*cc, entry.cc)) {
      error = "session: \"" + path + ".cc\" is not an integer";
      return false;
    }
    if (entry.cc < 0 || entry.cc > 127) {
      error = "session: \"" + path + ".cc\" is outside 0..127";
      return false;
    }
    if (!readInt(*ch, entry.ch)) {
      error = "session: \"" + path + ".ch\" is not an integer";
      return false;
    }
    if (entry.ch < 1 || entry.ch > 16) {
      error = "session: \"" + path + ".ch\" is outside 1..16";
      return false;
    }
    if (!readString(*target, entry.target)) {
      error = "session: \"" + path + ".target\" is not a string";
      return false;
    }
    if (!readInt(*track, entry.track)) {
      error = "session: \"" + path + ".track\" is not an integer";
      return false;
    }
    out.push_back(std::move(entry));
  }
  return true;
}

bool decode(const JsonValue& root, Snapshot& out, std::string& error) {
  if (root.type != JsonValue::Type::Object) {
    error = "session: root is not a JSON object";
    return false;
  }

  const JsonValue* app = root.find("app");
  if (app == nullptr) {
    error = "session: missing \"app\"";
    return false;
  }
  if (app->type != JsonValue::Type::String) {
    error = "session: \"app\" is not a string";
    return false;
  }
  if (app->string != kAppId) {
    error = "session: app is \"" + app->string + "\", expected \"" +
            std::string(kAppId) + "\"";
    return false;
  }

  const JsonValue* version = root.find("session");
  if (version == nullptr) {
    error = "session: missing \"session\" version";
    return false;
  }
  int versionNumber = 0;
  if (!readInt(*version, versionNumber)) {
    error = "session: \"session\" is not an integer";
    return false;
  }
  if (versionNumber != 1 && versionNumber != kVersion) {
    error = "session: unsupported session version " +
            std::to_string(versionNumber) + " (this build reads 1.." +
            std::to_string(kVersion) + ")";
    return false;
  }

  if (!readOptionalBool(root, "template", out.isTemplate, "session", error)) {
    return false;
  }

  if (const JsonValue* router = root.find("router")) {
    if (router->type != JsonValue::Type::Object) {
      error = "session: \"router\" is not an object";
      return false;
    }
    if (!decodeRouter(*router, out.router, error)) return false;
  }
  if (const JsonValue* mixer = root.find("mixer")) {
    if (mixer->type != JsonValue::Type::Object) {
      error = "session: \"mixer\" is not an object";
      return false;
    }
    if (!decodeMixer(*mixer, out.mixer, error)) return false;
  }
  if (const JsonValue* fx = root.find("fx")) {
    if (fx->type != JsonValue::Type::Object) {
      error = "session: \"fx\" is not an object";
      return false;
    }
    if (!decodeFx(*fx, out.fx, error)) return false;
  }
  if (const JsonValue* transport = root.find("transport")) {
    if (transport->type != JsonValue::Type::Object) {
      error = "session: \"transport\" is not an object";
      return false;
    }
    if (!decodeTransport(*transport, out.transport, error)) return false;
  }
  if (const JsonValue* click = root.find("click")) {
    if (click->type != JsonValue::Type::Object) {
      error = "session: \"click\" is not an object";
      return false;
    }
    if (!decodeClick(*click, out.click, error)) return false;
  }
  if (const JsonValue* midimap = root.find("midimap")) {
    if (!decodeMidiMap(*midimap, out.midimap, error)) return false;
  }
  if (const JsonValue* takes = root.find("takes")) {
    if (takes->type != JsonValue::Type::Object) {
      error = "session: \"takes\" is not an object";
      return false;
    }
    if (!decodeTakes(*takes, out.takes, error)) return false;
  }
  if (const JsonValue* punch = root.find("punch")) {
    if (punch->type != JsonValue::Type::Object) {
      error = "session: \"punch\" is not an object";
      return false;
    }
    if (!decodePunch(*punch, out.punch, error)) return false;
  }
  if (const JsonValue* cue = root.find("cue")) {
    if (cue->type != JsonValue::Type::Object) {
      error = "session: \"cue\" is not an object";
      return false;
    }
    if (!decodeCue(*cue, out.cue, error)) return false;
  }
  if (const JsonValue* sends = root.find("sends")) {
    if (sends->type != JsonValue::Type::Object) {
      error = "session: \"sends\" is not an object";
      return false;
    }
    if (!decodeSends(*sends, out.sends, error)) return false;
  }
  if (const JsonValue* mainComp = root.find("mainComp")) {
    if (mainComp->type != JsonValue::Type::Object) {
      error = "session: \"mainComp\" is not an object";
      return false;
    }
    if (!decodeMainComp(*mainComp, out.mainComp, error)) return false;
  }
  if (const JsonValue* clock = root.find("clock")) {
    if (clock->type != JsonValue::Type::Object) {
      error = "session: \"clock\" is not an object";
      return false;
    }
    if (!decodeClock(*clock, out.clock, error)) return false;
  }
  return true;
}

// -------------------------------------------------------------------- writer

template <typename T>
void appendNumber(std::string& out, T value) {
  if constexpr (std::is_floating_point_v<T>) {
    if (!std::isfinite(value)) value = static_cast<T>(0);
  }
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (result.ec == std::errc{}) {
    out.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
  } else {
    out.push_back('0');
  }
}

void appendBool(std::string& out, bool value) { out += value ? "true" : "false"; }

void writeString(std::string& out, std::string_view value) {
  out.push_back('"');
  for (const char ch : value) {
    const unsigned char c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char escaped[7];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x",
                        static_cast<unsigned>(c));
          out += escaped;
        } else {
          out.push_back(ch);
        }
        break;
    }
  }
  out.push_back('"');
}

std::string encode(const Snapshot& snapshot) {
  std::string out;
  out.reserve(8192);

  out += "{\"app\":\"";
  out += kAppId;
  out += "\",\"session\":";
  appendNumber(out, kVersion);
  out += ",\"template\":";
  appendBool(out, snapshot.isTemplate);

  out += ",\"router\":{\"routes\":[";
  for (std::size_t i = 0; i < snapshot.router.routes.size(); ++i) {
    if (i != 0) out.push_back(',');
    const RouteState& route = snapshot.router.routes[i];
    out += "{\"src\":";
    appendNumber(out, route.src);
    out += ",\"dst\":";
    appendNumber(out, route.dst);
    out += ",\"on\":";
    appendBool(out, route.on);
    out += ",\"pipes\":[";
    for (std::size_t p = 0; p < route.pipes.size(); ++p) {
      if (p != 0) out.push_back(',');
      const MidiPipe& pipe = route.pipes[p];
      out += "{\"t\":";
      appendNumber(out, static_cast<int>(pipe.type));
      out += ",\"bypass\":";
      appendBool(out, pipe.bypass);
      out += ",\"params\":[";
      for (std::size_t q = 0; q < pipe.params.size(); ++q) {
        if (q != 0) out.push_back(',');
        appendNumber(out, pipe.params[q]);
      }
      out += "]}";
    }
    out += "]}";
  }
  out += "],\"channels\":{";
  for (int port = 0; port < midi::kPortCount; ++port) {
    if (port != 0) out.push_back(',');
    writeString(out, std::to_string(port));
    out.push_back(':');
    appendNumber(out, snapshot.router.channels[static_cast<std::size_t>(port)]);
  }
  out += "},\"transpose\":{";
  for (int port = 0; port < midi::kPortCount; ++port) {
    if (port != 0) out.push_back(',');
    writeString(out, std::to_string(port));
    out.push_back(':');
    appendNumber(out, snapshot.router.transpose[static_cast<std::size_t>(port)]);
  }
  out += "}}";

  out += ",\"mixer\":{\"channels\":[";
  for (int index = 0; index < audio::kMaxInputs; ++index) {
    if (index != 0) out.push_back(',');
    const MixerChannelState& channel =
        snapshot.mixer.channels[static_cast<std::size_t>(index)];
    out += "{\"gain\":";
    appendNumber(out, channel.gain);
    out += ",\"pan\":";
    appendNumber(out, channel.pan);
    out += ",\"mute\":";
    appendBool(out, channel.mute);
    out += ",\"solo\":";
    appendBool(out, channel.solo);
    out += ",\"arm\":";
    appendBool(out, channel.arm);
    out += ",\"send\":[";
    appendNumber(out, channel.send[0]);
    out.push_back(',');
    appendNumber(out, channel.send[1]);
    out += "],\"cue\":";
    appendNumber(out, channel.cueSend);
    out += ",\"out34\":";
    appendNumber(out, channel.out34Send);
    out += ",\"monitor\":";
    appendNumber(out, channel.monitor);
    out += "}";
  }
  out += "],\"master\":";
  appendNumber(out, snapshot.mixer.master);
  out += ",\"out34mode\":";
  appendNumber(out, snapshot.mixer.out34Mode);
  out.push_back('}');

  out += ",\"fx\":{\"bus\":[";
  for (int index = 0; index < kFxBuses; ++index) {
    if (index != 0) out.push_back(',');
    const FxBusState& bus = snapshot.fx.bus[static_cast<std::size_t>(index)];
    out += "{\"id\":";
    writeString(out, bus.id);
    out += ",\"params\":[";
    for (std::size_t param = 0; param < bus.params.size(); ++param) {
      if (param != 0) out.push_back(',');
      appendNumber(out, bus.params[param]);
    }
    out += "],\"bypass\":";
    appendBool(out, bus.bypass);
    out.push_back('}');
  }
  out += "]}";

  out += ",\"transport\":{\"bpm_x100\":";
  appendNumber(out, snapshot.transport.bpmX100);
  out.push_back('}');

  out += ",\"click\":{\"mode\":";
  appendNumber(out, snapshot.click.mode);
  out += ",\"output\":";
  appendNumber(out, snapshot.click.output);
  out += ",\"gainDb\":";
  appendNumber(out, snapshot.click.gainDb);
  out += ",\"bars\":";
  appendNumber(out, snapshot.click.bars);
  out.push_back('}');

  out += ",\"midimap\":[";
  for (std::size_t index = 0; index < snapshot.midimap.size(); ++index) {
    if (index != 0) out.push_back(',');
    const MidiMapEntry& entry = snapshot.midimap[index];
    out += "{\"cc\":";
    appendNumber(out, entry.cc);
    out += ",\"ch\":";
    appendNumber(out, entry.ch);
    out += ",\"target\":";
    writeString(out, entry.target);
    out += ",\"track\":";
    appendNumber(out, entry.track);
    out.push_back('}');
  }
  out += "],\"takes\":{\"active\":{";
  bool firstTake = true;
  for (int track = 0; track < audio::kMaxInputs; ++track) {
    const std::string& name =
        snapshot.takes.active[static_cast<std::size_t>(track)];
    if (name.empty()) continue;
    if (!firstTake) out.push_back(',');
    firstTake = false;
    writeString(out, std::to_string(track));
    out.push_back(':');
    writeString(out, name);
  }
  out += "},\"loop\":{";
  bool firstLoop = true;
  for (int track = 0; track < audio::kMaxInputs; ++track) {
    const LoopState& state =
        snapshot.takes.loop[static_cast<std::size_t>(track)];
    if (!state.enabled) continue;
    if (!firstLoop) out.push_back(',');
    firstLoop = false;
    writeString(out, std::to_string(track));
    out += ":{\"enabled\":true,\"start\":";
    appendNumber(out, state.startBeats);
    out += ",\"end\":";
    appendNumber(out, state.endBeats);
    out.push_back('}');
  }
  out += "},\"edit\":{";
  bool firstEdit = true;
  for (int track = 0; track < audio::kMaxInputs; ++track) {
    const std::vector<TakeRegion>& regions =
        snapshot.takes.edit[static_cast<std::size_t>(track)];
    if (regions.empty()) continue;
    if (!firstEdit) out.push_back(',');
    firstEdit = false;
    writeString(out, std::to_string(track));
    out.push_back(':');
    out.push_back('[');
    for (std::size_t i = 0; i < regions.size(); ++i) {
      if (i != 0) out.push_back(',');
      const TakeRegion& region = regions[i];
      out += "{\"take\":";
      writeString(out, region.take);
      out += ",\"offset\":";
      appendNumber(out, region.offsetBeats);
      out += ",\"length\":";
      appendNumber(out, region.lengthBeats);
      out += ",\"gainDb\":";
      appendNumber(out, region.gainDb);
      out += ",\"fadeIn\":";
      appendNumber(out, region.fadeInBeats);
      out += ",\"fadeOut\":";
      appendNumber(out, region.fadeOutBeats);
      out += ",\"normalize\":";
      appendBool(out, region.normalize);
      out.push_back('}');
    }
    out.push_back(']');
  }
  out += "}},\"punch\":{\"playFrom\":{\"enabled\":";
  appendBool(out, snapshot.punch.playFrom.enabled);
  out += ",\"measure\":";
  appendNumber(out, snapshot.punch.playFrom.measure);
  out += "},\"in\":{\"enabled\":";
  appendBool(out, snapshot.punch.in.enabled);
  out += ",\"measure\":";
  appendNumber(out, snapshot.punch.in.measure);
  out += "},\"out\":{\"enabled\":";
  appendBool(out, snapshot.punch.out.enabled);
  out += ",\"measure\":";
  appendNumber(out, snapshot.punch.out.measure);
  out += "}},\"cue\":{\"phonesSrc\":";
  appendNumber(out, snapshot.cue.phonesSrc);
  out += ",\"style\":";
  appendNumber(out, snapshot.cue.style);
  out += "},\"sends\":{\"cue\":";
  appendNumber(out, snapshot.sends.cue);
  out += ",\"fx1\":";
  appendNumber(out, snapshot.sends.fx1);
  out += ",\"fx2\":";
  appendNumber(out, snapshot.sends.fx2);
  out += ",\"out34\":";
  appendNumber(out, snapshot.sends.out34);
  out += ",\"rec\":";
  appendNumber(out, snapshot.sends.rec);
  out += "},\"clock\":{\"trs\":[";
  for (std::size_t i = 0; i < snapshot.clock.trs.size(); ++i) {
    if (i != 0) out.push_back(',');
    out += "{\"clock\":";
    appendBool(out, snapshot.clock.trs[i].clock);
    out += ",\"transport\":";
    appendBool(out, snapshot.clock.trs[i].transport);
    out += ",\"tempoDiv\":";
    appendNumber(out, snapshot.clock.trs[i].tempoDiv);
    out += ",\"syncDelayMs\":";
    appendNumber(out, snapshot.clock.trs[i].syncDelayMs);
    out.push_back('}');
  }
  out += "],\"usb\":{\"clock\":";
  appendBool(out, snapshot.clock.usb.clock);
  out += ",\"transport\":";
  appendBool(out, snapshot.clock.usb.transport);
  out += ",\"tempoDiv\":";
  appendNumber(out, snapshot.clock.usb.tempoDiv);
  out += ",\"syncDelayMs\":";
  appendNumber(out, snapshot.clock.usb.syncDelayMs);
  out += "},\"source\":";
  appendNumber(out, snapshot.clock.source);
  out += ",\"transportFollow\":[";
  for (std::size_t i = 0; i < snapshot.clock.transportFollow.size(); ++i) {
    if (i != 0) out.push_back(',');
    appendBool(out, snapshot.clock.transportFollow[i]);
  }
  out += "]},\"mainComp\":{\"on\":";
  appendBool(out, snapshot.mainComp.on);
  out += ",\"threshDb\":";
  appendNumber(out, snapshot.mainComp.threshDb);
  out += ",\"ratio\":";
  appendNumber(out, snapshot.mainComp.ratio);
  out += ",\"attackMs\":";
  appendNumber(out, snapshot.mainComp.attackMs);
  out += ",\"releaseMs\":";
  appendNumber(out, snapshot.mainComp.releaseMs);
  out += ",\"makeupDb\":";
  appendNumber(out, snapshot.mainComp.makeupDb);
  out += ",\"mode\":";
  appendNumber(out, snapshot.mainComp.mode);
  out += "}}\n";
  return out;
}

}  // namespace

// ------------------------------------------------------------------- capture

Snapshot capture(const midi::Router& router, const audio::Engine& engine,
                 std::span<FxSlotTarget*> fxSlots) {
  Snapshot snapshot;

  for (int src = 0; src < midi::kPortCount; ++src) {
    const auto source = static_cast<midi::Port>(src);
    if (!midi::isInput(source)) continue;
    for (int dst = 0; dst < midi::kPortCount; ++dst) {
      const auto destination = static_cast<midi::Port>(dst);
      if (!midi::isOutput(destination)) continue;
      if (router.routeEnabled(source, destination)) {
        snapshot.router.routes.push_back(RouteState{src, dst, true, {}});
      }
    }
  }
  for (int port = 0; port < midi::kPortCount; ++port) {
    const auto value = static_cast<midi::Port>(port);
    snapshot.router.channels[static_cast<std::size_t>(port)] =
        router.channelMask(value);
    snapshot.router.transpose[static_cast<std::size_t>(port)] =
        router.transpose(value);
  }

  for (int index = 0; index < audio::kMaxInputs; ++index) {
    const audio::Channel live = engine.channel(index);
    MixerChannelState& channel =
        snapshot.mixer.channels[static_cast<std::size_t>(index)];
    channel.gain = live.gain;
    channel.pan = live.pan;
    channel.mute = live.mute;
    channel.solo = live.solo;
    channel.arm = live.arm;
    for (int send = 0; send < audio::kNumSends; ++send) {
      channel.send[static_cast<std::size_t>(send)] = live.send[send];
    }
    channel.cueSend = live.cueSend;
    channel.out34Send = live.out34Send;
    channel.monitor = static_cast<int>(live.monitor);
  }
  snapshot.mixer.master = engine.masterGain();
  snapshot.mixer.out34Mode = static_cast<int>(engine.out34Mode());

  snapshot.click.mode = static_cast<int>(engine.clickMode());
  snapshot.click.output = static_cast<int>(engine.clickOutput());
  snapshot.click.gainDb = engine.clickGainDb();

  const audio::MainCompSettings comp = engine.mainComp();
  snapshot.mainComp.on = comp.on;
  snapshot.mainComp.threshDb = comp.threshDb;
  snapshot.mainComp.ratio = comp.ratio;
  snapshot.mainComp.attackMs = comp.attackMs;
  snapshot.mainComp.releaseMs = comp.releaseMs;
  snapshot.mainComp.makeupDb = comp.makeupDb;
  snapshot.mainComp.mode = static_cast<int>(comp.mode);

  snapshot.cue.phonesSrc = static_cast<int>(engine.phonesSource());
  snapshot.cue.style = static_cast<int>(engine.cueStyle());

  snapshot.sends.cue = static_cast<int>(
      engine.sendSource(audio::SendDestination::Cue));
  snapshot.sends.fx1 = static_cast<int>(
      engine.sendSource(audio::SendDestination::Fx1));
  snapshot.sends.fx2 = static_cast<int>(
      engine.sendSource(audio::SendDestination::Fx2));
  snapshot.sends.out34 = static_cast<int>(
      engine.sendSource(audio::SendDestination::Out34));
  snapshot.sends.rec = static_cast<int>(
      engine.sendSource(audio::SendDestination::Rec));

  // TakesState.active stays app-owned (the engine only exposes the newest take
  // path), but per-track take looping is engine state, so it captures here.
  for (int index = 0; index < audio::kMaxInputs; ++index) {
    const audio::TakeLoop live = engine.takeLoop(index);
    LoopState& loop = snapshot.takes.loop[static_cast<std::size_t>(index)];
    loop.enabled = live.enabled;
    loop.startBeats = live.startBeats;
    loop.endBeats = live.endBeats;
  }

  for (int index = 0;
       index < kFxBuses && index < static_cast<int>(fxSlots.size()); ++index) {
    FxSlotTarget* slot = fxSlots[static_cast<std::size_t>(index)];
    if (slot == nullptr) continue;
    FxBusState& bus = snapshot.fx.bus[static_cast<std::size_t>(index)];
    bus.id = slot->effectId();
    const std::size_t count = slot->paramCount();
    bus.params.reserve(count);
    for (std::size_t param = 0; param < count; ++param) {
      bus.params.push_back(slot->paramValue(param));
    }
    bus.bypass = slot->bypassed();
  }
  return snapshot;
}

// --------------------------------------------------------------------- apply

void apply(const Snapshot& snapshot, midi::Router& router, audio::Engine& engine,
           std::span<FxSlotTarget*> fxSlots) {
  // The router has no reset, so clear every input -> output pair first: a
  // session load is authoritative, not a merge into whatever was live.
  for (int src = 0; src < midi::kPortCount; ++src) {
    for (int dst = 0; dst < midi::kPortCount; ++dst) {
      router.setRoute(static_cast<midi::Port>(src),
                      static_cast<midi::Port>(dst), false);
    }
  }
  for (const RouteState& route : snapshot.router.routes) {
    if (route.src < 0 || route.src >= midi::kPortCount || route.dst < 0 ||
        route.dst >= midi::kPortCount) {
      continue;
    }
    router.setRoute(static_cast<midi::Port>(route.src),
                    static_cast<midi::Port>(route.dst), route.on);
  }
  for (int port = 0; port < midi::kPortCount; ++port) {
    const auto value = static_cast<midi::Port>(port);
    router.setChannelMask(value,
                          snapshot.router.channels[static_cast<std::size_t>(port)]);
    router.setTranspose(value,
                        snapshot.router.transpose[static_cast<std::size_t>(port)]);
  }

  for (int index = 0; index < audio::kMaxInputs; ++index) {
    const MixerChannelState& state =
        snapshot.mixer.channels[static_cast<std::size_t>(index)];
    audio::Channel channel;
    channel.gain = state.gain;
    channel.pan = state.pan;
    channel.mute = state.mute;
    channel.solo = state.solo;
    channel.arm = state.arm;
    for (int send = 0; send < audio::kNumSends; ++send) {
      channel.send[static_cast<std::size_t>(send)] =
          state.send[static_cast<std::size_t>(send)];
    }
    channel.cueSend = state.cueSend;
    channel.out34Send = state.out34Send;
    channel.monitor = static_cast<audio::MonitorMode>(state.monitor);
    engine.setChannel(index, channel);
  }
  engine.setMaster(snapshot.mixer.master);
  engine.setOut34Mode(
      static_cast<audio::Out34Mode>(snapshot.mixer.out34Mode));

  audio::MainCompSettings comp;
  comp.on = snapshot.mainComp.on;
  comp.threshDb = snapshot.mainComp.threshDb;
  comp.ratio = snapshot.mainComp.ratio;
  comp.attackMs = snapshot.mainComp.attackMs;
  comp.releaseMs = snapshot.mainComp.releaseMs;
  comp.makeupDb = snapshot.mainComp.makeupDb;
  comp.mode = static_cast<audio::CompressorMode>(snapshot.mainComp.mode);
  engine.setMainComp(comp);

  engine.setClickMode(static_cast<audio::ClickMode>(snapshot.click.mode));
  engine.setClickOutput(
      static_cast<audio::ClickOutput>(snapshot.click.output));
  engine.setClickGainDb(snapshot.click.gainDb);
  engine.setPhonesSource(
      static_cast<audio::PhonesSource>(snapshot.cue.phonesSrc));
  engine.setCueStyle(static_cast<audio::CueStyle>(snapshot.cue.style));

  const int sendValues[audio::kNumSendDestinations] = {
      snapshot.sends.cue, snapshot.sends.fx1, snapshot.sends.fx2,
      snapshot.sends.out34, snapshot.sends.rec};
  const audio::SendDestination destinations[audio::kNumSendDestinations] = {
      audio::SendDestination::Cue, audio::SendDestination::Fx1,
      audio::SendDestination::Fx2, audio::SendDestination::Out34,
      audio::SendDestination::Rec};
  for (int index = 0; index < audio::kNumSendDestinations; ++index) {
    engine.setSendSource(
        destinations[index],
        sendValues[index] == 0 ? audio::SendSource::PreFader
                               : audio::SendSource::PostFader);
  }

  // Per-track take looping is engine state; like the other forwards it is
  // playback-only and does not select takes (that stays app-owned).
  for (int index = 0; index < audio::kMaxInputs; ++index) {
    const LoopState& state =
        snapshot.takes.loop[static_cast<std::size_t>(index)];
    audio::TakeLoop loop;
    loop.enabled = state.enabled;
    loop.startBeats = state.startBeats;
    loop.endBeats = state.endBeats;
    engine.setTakeLoop(index, loop);
  }

  for (int index = 0;
       index < kFxBuses && index < static_cast<int>(fxSlots.size()); ++index) {
    FxSlotTarget* slot = fxSlots[static_cast<std::size_t>(index)];
    if (slot == nullptr) continue;
    const FxBusState& bus = snapshot.fx.bus[static_cast<std::size_t>(index)];
    if (bus.id.empty()) {
      slot->clearEffect();
      continue;
    }
    if (slot->effectId() != bus.id && !slot->loadEffect(bus.id)) {
      continue;  // unavailable effect: leave the slot alone (tolerant load)
    }
    const std::size_t count = slot->paramCount();
    for (std::size_t param = 0; param < bus.params.size() && param < count;
         ++param) {
      slot->setParamValue(param, bus.params[param]);
    }
    slot->setBypassed(bus.bypass);
  }
}

// ---------------------------------------------------------------- file access

bool save(const std::string& path, const Snapshot& snapshot) noexcept {
  try {
    const std::string text = encode(snapshot);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    return out.good();
  } catch (...) {
    return false;
  }
}

LoadResult load(const std::string& path) noexcept {
  LoadResult result;
  try {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      result.error = "session: cannot open \"" + path + "\"";
      return result;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff length = in.tellg();
    if (length < 0) {
      result.error = "session: cannot determine size of \"" + path + "\"";
      return result;
    }
    if (length > static_cast<std::streamoff>(kMaxFileBytes)) {
      result.error = "session: file is larger than 4 MiB";
      return result;
    }
    in.seekg(0, std::ios::beg);
    if (!in) {
      result.error = "session: cannot seek \"" + path + "\"";
      return result;
    }
    std::string text(static_cast<std::size_t>(length), '\0');
    if (length > 0) {
      in.read(text.data(), static_cast<std::streamsize>(length));
    }
    if (!in) {
      result.error = "session: cannot read \"" + path + "\"";
      return result;
    }

    JsonValue root;
    JsonParser parser(text);
    if (!parser.parse(root)) {
      result.error = "session: " + parser.error();
      return result;
    }
    if (!decode(root, result.snapshot, result.error)) return result;
    result.ok = true;
    return result;
  } catch (const std::exception& exception) {
    result.ok = false;
    result.error = std::string("session: internal error: ") + exception.what();
    return result;
  } catch (...) {
    result.ok = false;
    result.error = "session: internal error";
    return result;
  }
}

}  // namespace mnky::session
