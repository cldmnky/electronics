#pragma once
// Per-route pipe-chain preset files: one JSON file per chain, the Midihub
// pipeline-file analog. A chain travels inside a project folder, so the file is
// named like a take (`chain-1-3.json`) and the recorder-style path entry picks
// it up again.
//
// The file carries the generated schema version and the same positional fields
// a session route uses (`t`, `bypass`, `chm`, `params`), so export/import can
// never invent a shape the engine does not read. Import validates every pipe
// against the schema: an unknown type id is refused with the id and pipe index,
// never silently dropped.
#include <QByteArray>
#include <QString>

#include <filesystem>
#include <vector>

#include "../session/session.hpp"

namespace mnky::app::pipe_preset {

// Top-level "format" marker; a file without it is not a chain preset.
inline constexpr const char* kFormat = "mnkybox-pipe-chain";

struct ChainFile {
  QString name;
  std::vector<session::MidiPipe> pipes;
};

// JSON text for one chain, including generated::kSchemaVersion.
QByteArray encode(const ChainFile& chain);

// Parses and validates one file body. Returns an empty string on success, else
// a one-line ASCII reason for the keypad. Missing fields default exactly like
// the session reader (missing params use the schema default, extra ones beyond
// the schema limit are refused).
QString decode(const QByteArray& bytes, ChainFile& out);

// Resolves a keypad entry to a file path. A value with '/' is a path (relative
// ones resolve against `projectDirectory`); a bare name resolves to
// <projectDirectory>/<name>.json. The .json extension is added when missing.
QString resolvePath(const QString& entry, const QString& projectDirectory,
                    std::filesystem::path& out);

// File name rule for a bare preset name: printable ASCII, no path separators.
QString validateName(const QString& name);

// Writes/reads one chain file (atomic write; missing file, bad JSON and schema
// mismatches all return an ASCII reason instead of throwing).
QString write(const std::filesystem::path& file, const ChainFile& chain);
QString read(const std::filesystem::path& file, ChainFile& out);

}  // namespace mnky::app::pipe_preset
