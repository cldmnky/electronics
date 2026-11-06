#pragma once
// Project persistence: one directory per project with a session-format
// `project.mnkybox`. Capture reads the app's live models into a session
// snapshot; load applies through the session core and then syncs the UI-owned
// MixerModel/click/tempo/learn state. Also the single source for the project
// list and the dirty-state fingerprint.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "app_context.hpp"

namespace mnky::app::project {

inline constexpr const char* kProjectFileName = "project.mnkybox";
// Temporary carrier for the W4/W5 settled song fields that the frozen session
// object does not carry yet. TODO(fold-into-session): when SongState gains
// mode/lengthBeats/loop, delete this sidecar and read/write those fields from
// project.mnkybox instead. The sidecar keeps exactly the settled names/shapes.
inline constexpr const char* kArrangeFileName = "arrange.mnkybox";

struct Entry {
  std::string name;
  std::filesystem::path directory;
  std::int64_t modifiedMs = 0;
};

// ~/Music/mnkybox-projects, created on first use.
std::filesystem::path root();

// Every child directory that contains project.mnkybox, newest modification
// first. Missing/unreadable roots produce an empty list, never a throw.
std::vector<Entry> list();

// Writes the current app state to <directory>/project.mnkybox. Returns false and
// sets `error` on failure.
bool save(const AppContext& context, const std::filesystem::path& directory,
          std::string& error);

// Reads <directory>/project.mnkybox and applies it. Returns false and sets
// `error` when the file is missing/corrupt or the FX slot cannot be restored;
// FX restoration is tolerant (a slot whose plugin is unavailable is skipped).
bool load(const AppContext& context, const std::filesystem::path& directory,
          std::string& error);

// Canonical string of every project-persisted value (route, mixer, FX, tempo,
// click, MIDI map). The controller compares it to detect a change for the dirty
// flag.
std::string fingerprint(const AppContext& context);

}  // namespace mnky::app::project
