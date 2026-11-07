#include "pico_stat.hpp"

#include <charconv>
#include <cstddef>
#include <sstream>
#include <vector>

namespace mnky::bench {
namespace {

bool parseU64(std::string_view text, std::uint64_t& out) {
  if (text.empty()) return false;
  std::uint64_t value = 0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return false;
  out = value;
  return true;
}

// "a+b" -> a+b, "a/b" -> a and b. Returns false when a part is not a number.
bool parsePair(std::string_view text, char separator, std::uint64_t& first,
               std::uint64_t& second) {
  const std::size_t at = text.find(separator);
  if (at == std::string_view::npos) return false;
  return parseU64(text.substr(0, at), first) && parseU64(text.substr(at + 1), second);
}

bool parseBpm(std::string_view text, std::uint64_t& out) {
  const std::size_t dot = text.find('.');
  std::uint64_t whole = 0;
  std::uint64_t fraction = 0;
  if (dot == std::string_view::npos) {
    if (!parseU64(text, whole)) return false;
    out = whole * 100;
    return true;
  }
  if (!parseU64(text.substr(0, dot), whole)) return false;
  std::string_view tail = text.substr(dot + 1);
  if (tail.size() != 2) return false;
  if (!parseU64(tail, fraction)) return false;
  out = whole * 100 + fraction;
  return true;
}

}  // namespace

bool parsePicoStatLine(std::string_view line, PicoStat& out) {
  PicoStat parsed;
  const std::size_t marker = line.find("[stat]");
  if (marker == std::string_view::npos) return false;
  line.remove_prefix(marker + 6);

  std::size_t cursor = 0;
  while (cursor < line.size()) {
    while (cursor < line.size() && line[cursor] == ' ') ++cursor;
    const std::size_t start = cursor;
    while (cursor < line.size() && line[cursor] != ' ') ++cursor;
    if (cursor == start) break;
    const std::string_view token = line.substr(start, cursor - start);
    const std::size_t equals = token.find('=');
    if (equals == std::string_view::npos || equals == 0) return false;
    const std::string key(token.substr(0, equals));
    const std::string_view value = token.substr(equals + 1);
    parsed.fields[key] = std::string(value);

    std::uint64_t number = 0;
    if (key == "bpm") {
      if (!parseBpm(value, number)) return false;
      parsed.bpm_x100 = number;
    } else if (key == "mtx") {
      // normal depths for both outputs, then realtime depths for both.
      const std::size_t slash = value.find('/');
      if (slash == std::string_view::npos) return false;
      std::uint64_t a = 0, b = 0;
      if (!parsePair(value.substr(0, slash), '+', a, b)) return false;
      parsed.mtx_normal = a + b;
      if (!parsePair(value.substr(slash + 1), '+', a, b)) return false;
      parsed.mtx_realtime = a + b;
    } else if (key == "fxq") {
      std::uint64_t a = 0, b = 0;
      if (!parsePair(value, '+', a, b)) return false;
      parsed.fxq = a + b;
    } else if (key == "rxpend") {
      if (!parsePair(value, '/', parsed.rxpend1, parsed.rxpend2)) return false;
    } else if (key == "rxov") {
      if (!parsePair(value, '/', parsed.rxov1, parsed.rxov2)) return false;
    } else {
      if (!parseU64(value, number)) return false;
      if (key == "link") parsed.link = number;
      else if (key == "run") parsed.run = number;
      else if (key == "ticks") parsed.ticks = number;
      else if (key == "txq") parsed.txq = number;
      else if (key == "bad") parsed.bad = number;
      else if (key == "dup") parsed.dup = number;
      else if (key == "txov") parsed.txov = number;
      else if (key == "miss") parsed.miss = number;
      else if (key == "spiabort") parsed.spiabort = number;
      else if (key == "spibad") parsed.spibad = number;
      else if (key == "spigarb") parsed.spigarb = number;
      else if (key == "spiok") parsed.spiok = number;
      else if (key == "spiak") parsed.spiak = number;
      else if (key == "spiov") parsed.spiov = number;
      else if (key == "fxdrop") parsed.fxdrop = number;
      else if (key == "mtxdrop") parsed.mtxdrop = number;
      else if (key == "mtxrej") parsed.mtxrej = number;
      else if (key == "mtxrt") parsed.mtxrt = number;
      // Unknown numeric keys stay in fields for the delta printer.
    }
  }
  // A healthy line always carries these two; require them so an unrelated
  // console line that happens to contain "[stat]" cannot pass as a snapshot.
  if (parsed.fields.find("spiok") == parsed.fields.end() ||
      parsed.fields.find("bpm") == parsed.fields.end()) {
    return false;
  }
  parsed.valid = true;
  out = parsed;
  return true;
}

std::string formatPicoStatDelta(const PicoStat& before, const PicoStat& after) {
  std::ostringstream out;
  const auto delta = [&](const char* name, std::uint64_t first, std::uint64_t second) {
    out << ' ' << name << '=' << (second >= first ? second - first : 0);
  };
  delta("spiok", before.spiok, after.spiok);
  delta("spiabort", before.spiabort, after.spiabort);
  delta("spibad", before.spibad, after.spibad);
  delta("spigarb", before.spigarb, after.spigarb);
  delta("spiak", before.spiak, after.spiak);
  delta("spiov", before.spiov, after.spiov);
  delta("miss", before.miss, after.miss);
  delta("mtxdrop", before.mtxdrop, after.mtxdrop);
  delta("mtxrej", before.mtxrej, after.mtxrej);
  delta("mtxrt", before.mtxrt, after.mtxrt);
  delta("fxdrop", before.fxdrop, after.fxdrop);
  delta("bad", before.bad, after.bad);
  delta("dup", before.dup, after.dup);
  delta("txov", before.txov, after.txov);
  return out.str();
}

}  // namespace mnky::bench
