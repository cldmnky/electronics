#!/usr/bin/env python3
"""Generate the shared MIDI FX pipe tables and their consistency tests.

Input, both human-owned:
  midifx/pipes.json       pipe/param definitions (single source for metadata)
  midifx/wire-order.txt   frozen 0x19 positional contract (wire ABI)

Output (committed, never hand-edited):
  midifx/gen/generated/midifx_params.hpp   constexpr tables for both builds
  midifx/gen/generated/schema_tests.cpp    generated consistency tests
  midifx/gen/generated/pipes-docs.md       docs table fragments

Usage:
  python3 midifx/gen/gen_params.py           # regenerate
  python3 midifx/gen/gen_params.py --check   # fail if the committed files are stale

Exit codes: 0 ok, 1 stale output (--check), 2 invalid schema/wire order.
Python 3 standard library only; the firmware never parses JSON at runtime.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

MIDIFX = Path(__file__).resolve().parent.parent
PIPES_JSON = MIDIFX / "pipes.json"
WIRE_ORDER = MIDIFX / "wire-order.txt"
OUT_DIR = MIDIFX / "gen" / "generated"
HEADER = OUT_DIR / "midifx_params.hpp"
SCHEMA_TESTS = OUT_DIR / "schema_tests.cpp"
DOCS = OUT_DIR / "pipes-docs.md"

KINDS = {"int", "bool", "enum"}
CATEGORIES = {"stateless", "clocked"}
MAX_PARAMS = 10
FIRST_CLOCKED_ID = 12


def c_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_ident(value: str) -> str:
    return value[:1].upper() + value[1:]


class SchemaError(Exception):
    pass


def load_wire_order() -> list[tuple[int, str, list[str]]]:
    rows: list[tuple[int, str, list[str]]] = []
    for number, raw in enumerate(WIRE_ORDER.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        if len(fields) < 2:
            raise SchemaError(f"wire-order.txt:{number}: expected '<id> <pipe key> <param>...'")
        try:
            pipe_id = int(fields[0])
        except ValueError as exc:
            raise SchemaError(f"wire-order.txt:{number}: '{fields[0]}' is not a type id") from exc
        rows.append((pipe_id, fields[1], fields[2:]))
    return rows


def load_schema() -> dict:
    try:
        return json.loads(PIPES_JSON.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SchemaError(f"pipes.json: {exc}") from exc


def validate(schema: dict, wire: list[tuple[int, str, list[str]]]) -> None:
    errors: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    version = schema.get("schemaVersion")
    require(isinstance(version, int) and version >= 1,
            "pipes.json: schemaVersion must be an integer >= 1")
    scope = schema.get("channelScope")
    require(isinstance(scope, dict), "pipes.json: channelScope object missing")
    if isinstance(scope, dict):
        require(isinstance(scope.get("key"), str) and bool(scope["key"]),
                "pipes.json: channelScope.key must be a non-empty string")
        require(scope.get("min") == 0 and scope.get("max") == 65535,
                "pipes.json: channelScope min/max must be the 16-bit mask range 0..65535")
        default = scope.get("default")
        require(isinstance(default, int) and 0 <= default <= 65535,
                "pipes.json: channelScope.default must be a 16-bit mask")
        require(isinstance(scope.get("docs"), str) and bool(scope["docs"]),
                "pipes.json: channelScope.docs must be a non-empty string")
    pipes = schema.get("pipes")
    require(isinstance(pipes, list) and bool(pipes), "pipes.json: pipes[] missing or empty")
    if errors:
        raise SchemaError("\n".join(errors))

    require(len(pipes) == len(wire),
            f"pipes.json: {len(pipes)} pipes but wire-order.txt has {len(wire)} rows")
    seen_pipe_ids: set[int] = set()
    seen_pipe_keys: set[str] = set()
    for index, pipe in enumerate(pipes):
        where = f"pipes.json:pipes[{index}]"
        pipe_id = pipe.get("id")
        require(isinstance(pipe_id, int), f"{where}: id must be an integer")
        if not isinstance(pipe_id, int):
            continue
        require(pipe_id not in seen_pipe_ids, f"{where}: duplicate id {pipe_id}")
        seen_pipe_ids.add(pipe_id)
        require(pipe_id == index + 1,
                f"{where}: id {pipe_id} is not contiguous/ascending (expected {index + 1}); "
                "the wire accepts ids 1..16")
        for field in ("key", "name", "short", "category", "docs"):
            require(isinstance(pipe.get(field), str) and bool(pipe[field]),
                    f"{where}: {field} must be a non-empty string")
        key = pipe.get("key")
        require(key not in seen_pipe_keys, f"{where}: duplicate pipe key '{key}'")
        seen_pipe_keys.add(key)
        category = pipe.get("category")
        require(category in CATEGORIES,
                f"{where}: category must be one of {sorted(CATEGORIES)}")
        expected_category = "clocked" if pipe_id >= FIRST_CLOCKED_ID else "stateless"
        require(category == expected_category,
                f"{where}: category '{category}' contradicts the id {pipe_id} "
                f"(ids >= {FIRST_CLOCKED_ID} are clocked)")

        params = pipe.get("params")
        require(isinstance(params, list) and bool(params), f"{where}: params[] missing or empty")
        if not isinstance(params, list):
            continue
        require(len(params) <= MAX_PARAMS,
                f"{where}: {len(params)} params exceed the 0x19 limit of {MAX_PARAMS}")
        seen_param_keys: set[str] = set()
        for position, param in enumerate(params):
            pwhere = f"{where}.params[{position}]"
            param_key = param.get("key")
            require(isinstance(param_key, str) and bool(param_key),
                    f"{pwhere}: key must be a non-empty string")
            require(param_key not in seen_param_keys,
                    f"{pwhere}: duplicate param key '{param_key}'")
            seen_param_keys.add(param_key)
            require(isinstance(param.get("label"), str) and bool(param["label"]),
                    f"{pwhere}: label must be a non-empty string")
            kind = param.get("kind")
            require(kind in KINDS, f"{pwhere}: kind must be one of {sorted(KINDS)}")
            if kind == "bool":
                require(param.get("min", 0) == 0 and param.get("max", 1) == 1,
                        f"{pwhere}: bool min/max are fixed at 0..1")
                require(param.get("default") in (0, 1),
                        f"{pwhere}: bool default must be 0 or 1")
            elif kind == "int":
                low, high = param.get("min"), param.get("max")
                require(isinstance(low, int) and isinstance(high, int) and low <= high,
                        f"{pwhere}: int params need integer min <= max")
                if isinstance(low, int) and isinstance(high, int):
                    default = param.get("default")
                    require(isinstance(default, int) and low <= default <= high,
                            f"{pwhere}: default {default!r} is outside {low}..{high}")
            elif kind == "enum":
                choices = param.get("choices")
                require(isinstance(choices, list) and len(choices) >= 2,
                        f"{pwhere}: enum needs >= 2 choices")
                if isinstance(choices, list):
                    require(all(isinstance(choice, str) and choice for choice in choices),
                            f"{pwhere}: enum choices must be non-empty strings")
                    require(len(set(choices)) == len(choices),
                            f"{pwhere}: enum choices repeat")
                    default = param.get("default")
                    require(isinstance(default, int) and 0 <= default < len(choices),
                            f"{pwhere}: enum default {default!r} is not a choice index")
            unit = param.get("unit", "")
            require(isinstance(unit, str), f"{pwhere}: unit must be a string")

        if index < len(wire):
            wire_id, wire_key, wire_params = wire[index]
            require(wire_id == pipe_id,
                    f"{where}: id {pipe_id} does not match wire-order.txt id {wire_id}")
            require(wire_key == key,
                    f"{where}: key '{key}' does not match wire-order.txt key '{wire_key}'")
            actual = [param.get("key") for param in params]
            require(actual == wire_params,
                    f"{where}: positional order {actual} does not match the frozen wire order "
                    f"{wire_params}; 0x19 params would move. Edit wire-order.txt and bump "
                    "schemaVersion deliberately, or restore the old order.")

    if errors:
        raise SchemaError("\n".join(errors))


def render_header(schema: dict) -> str:
    version = schema["schemaVersion"]
    pipes = schema["pipes"]
    max_params = max(len(pipe["params"]) for pipe in pipes)
    out: list[str] = []
    out.append("// GENERATED by midifx/gen/gen_params.py from midifx/pipes.json and")
    out.append("// midifx/wire-order.txt - do not edit. Regenerate with:")
    out.append("//   python3 midibrain/midifx/gen/gen_params.py")
    out.append("#pragma once")
    out.append("")
    out.append("#include <cstddef>")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace mnky::midifx::generated {")
    out.append("")
    out.append("// Bumped deliberately in pipes.json whenever a param, default or the")
    out.append("// positional order changes; the firmware reports it at boot and both builds")
    out.append("// read the same tables, so a session can never meet a stale schema silently.")
    out.append(f"inline constexpr std::uint32_t kSchemaVersion = {version};")
    out.append("")
    out.append("// Positional parameter order IS the 0x19 wire order: index i is the")
    out.append("// int16 at param offset i. Missing offsets use defaultValue, extra ones")
    out.append("// are ignored.")
    out.append("enum class ParamKind : std::uint8_t { Int = 0, Bool = 1, Enum = 2 };")
    out.append("")
    out.append("struct ParamSpec {")
    out.append("  const char* key;")
    out.append("  const char* label;")
    out.append("  ParamKind kind;")
    out.append("  std::int32_t min;")
    out.append("  std::int32_t max;")
    out.append("  std::int32_t defaultValue;")
    out.append("  const char* unit;             // \"\" when unitless")
    out.append("  const char* const* choices;   // nullptr for int/bool")
    out.append("  std::uint8_t choiceCount;")
    out.append("  std::uint8_t index;           // == 0x19 positional offset")
    out.append("};")
    out.append("")
    out.append("struct PipeSpec {")
    out.append("  std::uint8_t id;")
    out.append("  const char* key;")
    out.append("  const char* name;")
    out.append("  const char* shortName;")
    out.append("  const char* category;")
    out.append("  bool clocked;")
    out.append("  const char* docs;")
    out.append("  const ParamSpec* params;")
    out.append("  std::uint8_t paramCount;")
    out.append("};")
    out.append("")
    scope = schema["channelScope"]
    out.append("// Per-pipe channel scope, carried alongside the positional params on the")
    out.append("// 0x19 offset-0 chunk: bit n admits channel n, every other message passes")
    out.append("// the pipe unchanged. 0xFFFF is all channels, 0 makes the pipe inert.")
    out.append("struct ChannelScopeSpec {")
    out.append("  const char* key;")
    out.append("  std::uint16_t min;")
    out.append("  std::uint16_t max;")
    out.append("  std::uint16_t defaultValue;")
    out.append("  const char* docs;")
    out.append("};")
    out.append("")
    out.append(
        f"inline constexpr ChannelScopeSpec kChannelScope = {{{c_string(scope['key'])}, "
        f"{scope['min']}, {scope['max']}, {scope['default']}, "
        f"{c_string(scope['docs'])}}};")
    out.append("")
    for pipe in pipes:
        for param in pipe["params"]:
            if param["kind"] != "enum":
                continue
            name = f"kChoices{c_ident(pipe['key'])}{c_ident(param['key'])}"
            choices = ", ".join(c_string(choice) for choice in param["choices"])
            out.append(f"inline constexpr const char* {name}[] = {{{choices}}};")
    if any(param["kind"] == "enum" for pipe in pipes for param in pipe["params"]):
        out.append("")
    for pipe in pipes:
        name = f"kParams{c_ident(pipe['key'])}"
        out.append(f"inline constexpr ParamSpec {name}[] = {{")
        for position, param in enumerate(pipe["params"]):
            kind = {"int": "ParamKind::Int", "bool": "ParamKind::Bool",
                    "enum": "ParamKind::Enum"}[param["kind"]]
            if param["kind"] == "int":
                low, high = param["min"], param["max"]
            elif param["kind"] == "bool":
                low, high = 0, 1
            else:
                low, high = 0, len(param["choices"]) - 1
            unit = c_string(param.get("unit", ""))
            if param["kind"] == "enum":
                choices = (f"kChoices{c_ident(pipe['key'])}{c_ident(param['key'])}")
                choice_count = len(param["choices"])
            else:
                choices = "nullptr"
                choice_count = 0
            out.append(
                f"    {{{c_string(param['key'])}, {c_string(param['label'])}, {kind}, "
                f"{low}, {high}, {param['default']}, {unit}, {choices}, "
                f"{choice_count}, {position}}},")
        out.append("};")
        out.append("")
    out.append("inline constexpr PipeSpec kPipes[] = {")
    for pipe in pipes:
        out.append(
            f"    {{{pipe['id']}, {c_string(pipe['key'])}, {c_string(pipe['name'])}, "
            f"{c_string(pipe['short'])}, {c_string(pipe['category'])}, "
            f"{'true' if pipe['category'] == 'clocked' else 'false'}, "
            f"{c_string(pipe['docs'])}, kParams{c_ident(pipe['key'])}, "
            f"{len(pipe['params'])}}},")
    out.append("};")
    out.append("")
    out.append(f"inline constexpr std::size_t kPipeCount = {len(pipes)};")
    out.append(f"inline constexpr std::size_t kMaxParamsPerPipe = {max_params};")
    out.append("")
    out.append("constexpr const PipeSpec* pipeSpecById(std::uint8_t id) {")
    out.append("  for (const PipeSpec& pipe : kPipes)")
    out.append("    if (pipe.id == id) return &pipe;")
    out.append("  return nullptr;")
    out.append("}")
    out.append("")
    out.append("constexpr const ParamSpec* pipeParamSpec(std::uint8_t id, std::size_t index) {")
    out.append("  const PipeSpec* pipe = pipeSpecById(id);")
    out.append("  if (pipe == nullptr || index >= pipe->paramCount) return nullptr;")
    out.append("  return &pipe->params[index];")
    out.append("}")
    out.append("")
    out.append("// Schema default for a missing 0x19 positional param; 0 when the id or")
    out.append("// offset is outside the schema (an unconfigured pipe must stay harmless).")
    out.append("constexpr std::int32_t pipeDefaultParam(std::uint8_t id, std::size_t index) {")
    out.append("  const ParamSpec* param = pipeParamSpec(id, index);")
    out.append("  return param == nullptr ? 0 : param->defaultValue;")
    out.append("}")
    out.append("")
    out.append("}  // namespace mnky::midifx::generated")
    out.append("")
    return "\n".join(out)


def render_schema_tests(schema: dict) -> str:
    version = schema["schemaVersion"]
    pipes = schema["pipes"]
    out: list[str] = []
    out.append("// GENERATED by midifx/gen/gen_params.py - do not edit.")
    out.append("// Golden-consistency checks for midifx/gen/generated/midifx_params.hpp.")
    out.append("// schema_tests.cpp is compiled by midifx/tests/test_schema.cpp; a schema edit")
    out.append("// that is not regenerated fails the codegen diff test, and an edit that")
    out.append("// breaks these invariants fails here.")
    out.append("#include \"midifx_params.hpp\"")
    out.append("")
    out.append("#include <cstddef>")
    out.append("#include <cstdint>")
    out.append("#include <cstdio>")
    out.append("#include <cstring>")
    out.append("")
    out.append("namespace mnky::midifx::generated::test {")
    out.append("")
    out.append("inline int failures = 0;")
    out.append("")
    out.append("inline void check(bool ok, const char* what) {")
    out.append("  if (ok) return;")
    out.append("  std::fprintf(stderr, \"FAIL schema: %s\\n\", what);")
    out.append("  ++failures;")
    out.append("}")
    out.append("")
    out.append("inline bool uniqueKeys(const ParamSpec* params, std::size_t count) {")
    out.append("  for (std::size_t i = 0; i < count; ++i)")
    out.append("    for (std::size_t j = i + 1; j < count; ++j)")
    out.append("      if (std::strcmp(params[i].key, params[j].key) == 0) return false;")
    out.append("  return true;")
    out.append("}")
    out.append("")
    out.append("inline int runSchemaTests() {")
    out.append("  failures = 0;")
    out.append(f"  check(kSchemaVersion == {version}u, \"schemaVersion matches pipes.json\");")
    out.append(f"  check(kPipeCount == {len(pipes)}u, \"pipe count is 16 (the 0x19 id range)\");")
    out.append("  check(kMaxParamsPerPipe <= 10u, \"no pipe exceeds the 0x19 param limit of 10\");")
    out.append("  for (std::size_t i = 0; i < kPipeCount; ++i) {")
    out.append("    const PipeSpec& pipe = kPipes[i];")
    out.append("    check(pipe.id == i + 1, \"pipe ids ascend 1..16 in table order\");")
    out.append("    check(pipe.key != nullptr && pipe.key[0] != 0, \"pipe key is set\");")
    out.append("    check(pipe.name != nullptr && pipe.name[0] != 0, \"pipe name is set\");")
    out.append("    check(pipe.shortName != nullptr && pipe.shortName[0] != 0,")
    out.append("          \"pipe short name is set\");")
    out.append("    check(pipe.clocked == (pipe.id >= 12), \"clocked flag matches the id range\");")
    out.append("    check(pipe.paramCount == 0 ? pipe.params == nullptr : pipe.params != nullptr,")
    out.append("          \"param table matches paramCount\");")
    out.append("    check(uniqueKeys(pipe.params, pipe.paramCount), \"param keys are unique\");")
    out.append("    for (std::size_t p = 0; p < pipe.paramCount; ++p) {")
    out.append("      const ParamSpec& param = pipe.params[p];")
    out.append("      check(param.index == p, \"param index equals its 0x19 position\");")
    out.append("      check(param.key != nullptr && param.key[0] != 0, \"param key is set\");")
    out.append("      check(param.label != nullptr && param.label[0] != 0, \"param label is set\");")
    out.append("      check(param.min <= param.defaultValue && param.defaultValue <= param.max,")
    out.append("            \"default is inside the declared range\");")
    out.append("      if (param.kind == ParamKind::Bool)")
    out.append("        check(param.min == 0 && param.max == 1, \"bool range is 0..1\");")
    out.append("      if (param.kind == ParamKind::Enum) {")
    out.append("        check(param.choices != nullptr && param.choiceCount >= 2,")
    out.append("              \"enum has choices\");")
    out.append("        check(param.min == 0 && param.max == param.choiceCount - 1,")
    out.append("              \"enum range covers its choices\");")
    out.append("        for (std::size_t c = 0; c < param.choiceCount; ++c) {")
    out.append("          check(param.choices[c] != nullptr && param.choices[c][0] != 0,")
    out.append("                \"enum choice is non-empty\");")
    out.append("          for (std::size_t d = c + 1; d < param.choiceCount; ++d)")
    out.append("            check(std::strcmp(param.choices[c], param.choices[d]) != 0,")
    out.append("                  \"enum choices are unique\");")
    out.append("        }")
    out.append("      }")
    out.append("    }")
    out.append("  }")
    out.append("  for (std::size_t i = 0; i < kPipeCount; ++i) {")
    out.append("    for (std::size_t j = i + 1; j < kPipeCount; ++j)")
    out.append("      check(std::strcmp(kPipes[i].key, kPipes[j].key) != 0,")
    out.append("            \"pipe keys are unique\");")
    out.append("    check(pipeSpecById(kPipes[i].id) == &kPipes[i], \"pipeSpecById finds the row\");")
    out.append("    const ParamSpec* param = pipeParamSpec(kPipes[i].id, kPipes[i].paramCount);")
    out.append("    check(param == nullptr, \"out-of-range param lookup returns nullptr\");")
    out.append("    check(pipeDefaultParam(kPipes[i].id, kPipes[i].paramCount) == 0,")
    out.append("          \"missing out-of-range default is 0\");")
    out.append("  }")
    out.append("  check(kChannelScope.key != nullptr && kChannelScope.key[0] != 0,")
    out.append("        \"channel-scope key is set\");")
    out.append("  check(kChannelScope.min == 0 && kChannelScope.max == 65535,")
    out.append("        \"channel-scope range is the 16-bit mask\");")
    out.append("  check(kChannelScope.min <= kChannelScope.defaultValue &&")
    out.append("            kChannelScope.defaultValue <= kChannelScope.max,")
    out.append("        \"channel-scope default is inside its range\");")
    out.append("  check(pipeSpecById(0) == nullptr && pipeSpecById(17) == nullptr,")
    out.append("        \"unknown pipe ids have no spec\");")
    out.append("  return failures;")
    out.append("}")
    out.append("")
    out.append("}  // namespace mnky::midifx::generated::test")
    out.append("")
    return "\n".join(out)


def render_docs(schema: dict) -> str:
    out: list[str] = []
    out.append("<!-- GENERATED by midifx/gen/gen_params.py from midifx/pipes.json - do not edit. -->")
    out.append("")
    out.append(f"Schema version: {schema['schemaVersion']}. Param index IS the `0x19`")
    out.append("positional wire offset; a missing offset uses the default, an extra one is")
    out.append("ignored. Every pipe also carries the channel scope below on the `0x19`")
    out.append("offset-0 chunk.")
    scope = schema["channelScope"]
    out.append("")
    out.append("## Per-pipe channel scope (`{}`)".format(scope["key"]))
    out.append("")
    out.append(scope["docs"])
    out.append("")
    out.append("| Key | Min | Max | Default |")
    out.append("|---|---|---|---|")
    out.append("| `{}` | {} | {} | {} |".format(scope["key"], scope["min"],
                                                 scope["max"], scope["default"]))
    out.append("")
    out.append("| Id | Key | Name | Short | Category | Params |")
    out.append("|---|---|---|---|---|---|")
    for pipe in schema["pipes"]:
        out.append(
            f"| {pipe['id']} | `{pipe['key']}` | {pipe['name']} | {pipe['short']} | "
            f"{pipe['category']} | {len(pipe['params'])} |")
    out.append("")
    for pipe in schema["pipes"]:
        out.append(f"## {pipe['id']} - {pipe['name']} (`{pipe['key']}`, {pipe['short']})")
        out.append("")
        out.append(pipe["docs"])
        out.append("")
        out.append("| # | Key | Label | Kind | Range / choices | Default | Unit |")
        out.append("|---|---|---|---|---|---|---|")
        for position, param in enumerate(pipe["params"]):
            kind = param["kind"]
            if kind == "enum":
                extent = ", ".join(f"{index}={choice}"
                                   for index, choice in enumerate(param["choices"]))
                extent = f"{0}..{len(param['choices']) - 1}: {extent}"
            elif kind == "bool":
                extent = "0..1"
            else:
                extent = f"{param['min']}..{param['max']}"
            out.append(
                f"| {position} | `{param['key']}` | {param['label']} | {kind} | {extent} | "
                f"{param['default']} | {param.get('unit', '') or '-'} |")
        out.append("")
    return "\n".join(out)


def outputs() -> dict[Path, str]:
    schema = load_schema()
    wire = load_wire_order()
    validate(schema, wire)
    return {
        HEADER: render_header(schema),
        SCHEMA_TESTS: render_schema_tests(schema),
        DOCS: render_docs(schema),
    }


def main(argv: list[str]) -> int:
    check = "--check" in argv[1:]
    unknown = [arg for arg in argv[1:] if arg != "--check"]
    if unknown:
        print(f"gen_params.py: unknown arguments: {' '.join(unknown)}", file=sys.stderr)
        return 2
    try:
        rendered = outputs()
    except SchemaError as exc:
        print("gen_params.py: schema is invalid:\n" + str(exc), file=sys.stderr)
        return 2

    if check:
        stale = False
        for path, content in rendered.items():
            try:
                current = path.read_text(encoding="utf-8")
            except FileNotFoundError:
                print(f"STALE: {path.relative_to(MIDIFX)} is missing", file=sys.stderr)
                stale = True
                continue
            if current != content:
                print(f"STALE: {path.relative_to(MIDIFX)} does not match pipes.json/wire-order.txt; "
                      "regenerate with `python3 midifx/gen/gen_params.py` and commit the diff",
                      file=sys.stderr)
                stale = True
        if stale:
            return 1
        print(f"gen_params.py --check: generated tables match pipes.json "
              f"(schema v{load_schema()['schemaVersion']})")
        return 0

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for path, content in rendered.items():
        path.write_text(content, encoding="utf-8")
        print(f"wrote {path.relative_to(MIDIFX)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
