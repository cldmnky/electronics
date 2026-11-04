#include "params.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace mnky::audio::fx {
namespace {

void appendEscaped(std::string& out, const std::string& text) {
    static const char kHex[] = "0123456789abcdef";
    for (const unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 0x0f];
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
}

void appendNumber(std::string& out, double value) {
    char buffer[64] = {};
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    if (result.ec != std::errc{}) {
        // Only reachable for non-finite values on standard library versions
        // without shortest-format support; keep the output deterministic.
        out += "0";
        return;
    }
    out.append(buffer, result.ptr);
}

class JsonParser {
public:
    JsonParser(const std::string& text, std::string* error) : mText(text), mError(error) {}

    bool parse(EffectState& out) {
        skipWhitespace();
        if (!consume('{')) return fail("expected '{'");
        bool haveId = false;
        bool haveVersion = false;
        bool haveBypass = false;
        bool haveParams = false;
        skipWhitespace();
        if (consume('}')) return fail("empty state object");
        for (;;) {
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (!consume(':')) return fail("expected ':'");
            if (key == "id") {
                if (haveId) return fail("duplicate id");
                if (!parseString(out.id)) return false;
                haveId = true;
            } else if (key == "v") {
                if (haveVersion) return fail("duplicate v");
                if (!parseUint(out.version)) return false;
                haveVersion = true;
            } else if (key == "bypass") {
                if (haveBypass) return fail("duplicate bypass");
                if (!parseBool(out.bypass)) return false;
                haveBypass = true;
            } else if (key == "params") {
                if (haveParams) return fail("duplicate params");
                if (!parseParams(out.params)) return false;
                haveParams = true;
            } else {
                return fail("unknown key");
            }
            skipWhitespace();
            if (consume(',')) {
                skipWhitespace();
                continue;
            }
            if (consume('}')) break;
            return fail("expected ',' or '}'");
        }
        if (!haveId || !haveVersion || !haveBypass || !haveParams) {
            return fail("missing id, v, bypass or params");
        }
        if (out.id.empty()) return fail("empty effect id");
        skipWhitespace();
        if (mPos != mText.size()) return fail("trailing bytes");
        return true;
    }

private:
    bool fail(const char* message) {
        if (mError != nullptr) *mError = message;
        return false;
    }

    char peek() const { return mPos < mText.size() ? mText[mPos] : '\0'; }

    void skipWhitespace() {
        while (mPos < mText.size()) {
            const char c = mText[mPos];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
            ++mPos;
        }
    }

    bool consume(char expected) {
        skipWhitespace();
        if (peek() != expected) return false;
        ++mPos;
        return true;
    }

    bool parseString(std::string& out) {
        skipWhitespace();
        if (!consume('"')) return fail("expected string");
        std::string result;
        while (mPos < mText.size()) {
            const char c = mText[mPos++];
            if (c == '"') {
                out = std::move(result);
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
            if (c != '\\') {
                result += c;
                continue;
            }
            if (mPos >= mText.size()) return fail("unterminated escape");
            const char escaped = mText[mPos++];
            switch (escaped) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    if (mPos + 4 > mText.size()) return fail("short unicode escape");
                    unsigned value = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char digit = mText[mPos++];
                        unsigned nibble = 0;
                        if (digit >= '0' && digit <= '9') {
                            nibble = static_cast<unsigned>(digit - '0');
                        } else if (digit >= 'a' && digit <= 'f') {
                            nibble = static_cast<unsigned>(digit - 'a') + 10u;
                        } else if (digit >= 'A' && digit <= 'F') {
                            nibble = static_cast<unsigned>(digit - 'A') + 10u;
                        } else {
                            return fail("bad unicode escape");
                        }
                        value = (value << 4) | nibble;
                    }
                    if (value > 0x7f) return fail("unsupported unicode escape");
                    result += static_cast<char>(value);
                    break;
                }
                default:
                    return fail("unsupported escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseUint(uint32_t& out) {
        skipWhitespace();
        if (peek() < '0' || peek() > '9') return fail("expected unsigned integer");
        constexpr uint64_t kMaximum = std::numeric_limits<uint32_t>::max();
        uint64_t value = 0;
        while (peek() >= '0' && peek() <= '9') {
            const uint64_t digit = static_cast<uint64_t>(mText[mPos] - '0');
            if (value > (kMaximum - digit) / 10u) return fail("integer overflow");
            value = value * 10u + digit;
            ++mPos;
        }
        out = static_cast<uint32_t>(value);
        return true;
    }

    bool parseBool(bool& out) {
        skipWhitespace();
        if (mText.compare(mPos, 4, "true") == 0) {
            mPos += 4;
            out = true;
            return true;
        }
        if (mText.compare(mPos, 5, "false") == 0) {
            mPos += 5;
            out = false;
            return true;
        }
        return fail("expected true or false");
    }

    bool parseNumber(double& out) {
        skipWhitespace();
        const size_t start = mPos;
        if (peek() == '-') ++mPos;
        if (peek() < '0' || peek() > '9') return fail("expected number");
        while (peek() >= '0' && peek() <= '9') ++mPos;
        if (peek() == '.') {
            ++mPos;
            if (peek() < '0' || peek() > '9') return fail("expected fraction digits");
            while (peek() >= '0' && peek() <= '9') ++mPos;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++mPos;
            if (peek() == '+' || peek() == '-') ++mPos;
            if (peek() < '0' || peek() > '9') return fail("expected exponent digits");
            while (peek() >= '0' && peek() <= '9') ++mPos;
        }
        const std::string token = mText.substr(start, mPos - start);
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(value)) return fail("invalid number");
        out = value;
        return true;
    }

    bool parseParams(std::vector<double>& out) {
        std::vector<double> values;
        skipWhitespace();
        if (!consume('[')) return fail("expected '['");
        skipWhitespace();
        if (consume(']')) {
            out = std::move(values);
            return true;
        }
        for (;;) {
            double value = 0.0;
            if (!parseNumber(value)) return false;
            values.push_back(value);
            skipWhitespace();
            if (consume(',')) continue;
            if (consume(']')) break;
            return fail("expected ',' or ']'");
        }
        out = std::move(values);
        return true;
    }

    const std::string& mText;
    std::string* mError = nullptr;
    size_t mPos = 0;
};

}  // namespace

void Ramp::snap(double value) {
    current = value;
    target = value;
    step = 0.0;
    remaining = 0;
}

void Ramp::setTarget(double value, int rampSamples) {
    if (!std::isfinite(value)) return;
    target = value;
    if (rampSamples <= 0 || value == current) {
        snap(value);
        return;
    }
    remaining = rampSamples;
    step = (value - current) / static_cast<double>(rampSamples);
}

double Ramp::next(int n) {
    if (n <= 0) return current;
    if (remaining <= 0) {
        current = target;
        return current;
    }
    const int frames = n < remaining ? n : remaining;
    const double value = current + step * (0.5 * static_cast<double>(frames));
    current += step * static_cast<double>(frames);
    remaining -= frames;
    if (remaining == 0) {
        current = target;
        step = 0.0;
    }
    return value;
}

std::string EffectState::toJson() const {
    std::string out;
    out.reserve(64 + params.size() * 20);
    out += "{\"id\":\"";
    appendEscaped(out, id);
    out += "\",\"v\":";
    out += std::to_string(version);
    out += ",\"bypass\":";
    out += bypass ? "true" : "false";
    out += ",\"params\":[";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i != 0) out += ',';
        appendNumber(out, params[i]);
    }
    out += "]}";
    return out;
}

bool EffectState::fromJson(const std::string& json, EffectState& out, std::string* error) {
    JsonParser parser(json, error);
    EffectState parsed;
    if (!parser.parse(parsed)) return false;
    out = std::move(parsed);
    return true;
}

}  // namespace mnky::audio::fx
