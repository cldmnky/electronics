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
// Legacy sidecar from before the song timing fields moved into the session
// `song` object. Never written; load() folds it into the song state once and
// removes it, then project.mnkybox is the only project file.
inline constexpr const char* kLegacyArrangeFileName = "arrange.mnkybox";

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

// Graceful-shutdown flush: saves the open project through save(), covering the
// whole session snapshot. True when there is nothing to flush (no project and
// not dirty) or the save succeeded; false with `error` when dirty state has no
// project folder or the save fails.
bool flushCurrentProject(const AppContext& context, std::string& error);

}  // namespace mnky::app::project
