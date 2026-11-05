// Schema consistency test: compiles the generated golden checks
// (midifx/gen/generated/schema_tests.cpp) and adds the frozen facts the
// generator cannot re-derive by reading pipes.json again.
//
// The wire contract is the 0x19 positional param offset: index i in a pipe's
// params[] IS the offset the Pi sends. The generated checks prove the tables
// are internally consistent (unique keys, defaults in range, index == array
// position). The checks below pin the wire ABI itself: a deliberate reorder has
// to edit wire-order.txt, this file and the schema version together, and an
// accidental one fails gen_params.py before it can regenerate anything.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "midifx/gen/generated/midifx_params.hpp"
#include "midifx/gen/generated/schema_tests.cpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  if (ok) return;
  std::fprintf(stderr, "FAIL schema: %s\n", what);
  ++failures;
}

void expectParam(std::uint8_t id, std::size_t index, const char* key,
                 std::int32_t defaultValue) {
  const auto* spec = mnky::midifx::generated::pipeParamSpec(id, index);
  check(spec != nullptr, key);
  if (spec == nullptr) return;
  check(std::strcmp(spec->key, key) == 0, key);
  check(spec->index == index, key);
  check(spec->defaultValue == defaultValue, key);
}

}  // namespace

int main() {
  using namespace mnky::midifx::generated;
  failures += test::runSchemaTests();

  // Frozen 0x19 positions that the firmware's wire decode and the Pi's session
  // writer both rely on. Changing any of these is a wire break, not an edit.
  expectParam(1, 0, "noteOn", 1);
  expectParam(1, 7, "sysex", 1);
  expectParam(5, 2, "mask12", 4095);
  expectParam(10, 0, "lengthMs", 0);
  expectParam(11, 4, "outVal", 0);
  expectParam(12, 0, "div", 0);
  expectParam(12, 5, "gate", 50);
  expectParam(13, 1, "repeats", 1);
  expectParam(14, 4, "center", 64);
  expectParam(15, 4, "i4", 0);
  expectParam(16, 0, "delayMs", 0);
  expectParam(16, 2, "falloff", 0);

  // The wire accepts ids 1..16 and 10 positional params; the schema must stay
  // inside both limits or the generated table could describe a frame the
  // firmware cannot decode.
  check(kChannelScope.defaultValue == 0xFFFF,
        "channel scope defaults to all channels");
  check(kChannelScope.min == 0 && kChannelScope.max == 0xFFFF,
        "channel scope covers the 16-bit mask");
  check(std::strcmp(kChannelScope.key, "chMask") == 0,
        "channel scope key matches the session field");
  check(kPipeCount == 16, "schema describes exactly the 0x19 id range");
  check(kMaxParamsPerPipe <= 10, "schema fits the 0x19 param limit");
  check(kSchemaVersion == 1, "schema version is the agreed wire version");

  if (failures != 0) {
    std::fprintf(stderr, "%d schema failure(s)\n", failures);
    return 1;
  }
  std::printf("PASS midifx schema (v%u, %zu pipes, max %zu params)\n",
              static_cast<unsigned>(kSchemaVersion), kPipeCount, kMaxParamsPerPipe);
  return 0;
}
