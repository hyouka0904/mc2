// SPDX-License-Identifier: GPL-3.0-only
// The per-user directory that holds saved games and settings.
//
// It is the platform's application data directory (SDL_GetPrefPath):
//   Linux    $XDG_DATA_HOME/MechCommander2 (~/.local/share/MechCommander2)
//   macOS    ~/Library/Application Support/MechCommander2
//   Windows  %APPDATA%\MechCommander2
// Earlier versions used ~/.mechcommander2 (or $XDG_CONFIG_HOME/.mechcommander2,
// %USERPROFILE%\.mechcommander2). Its contents are copied over the first time
// the new directory is used; the old directory is left in place.
#pragma once

#include <string>
#include <vector>

namespace user_dir {

// Directory name used before SDL_GetPrefPath, relative to the old base.
extern const char* const kLegacyName;
// Written into a migrated legacy directory to say where its data went.
extern const char* const kMovedNoteName;

// Makes sure `dir` exists and, if it holds no data yet, fills it from the
// first existing directory in `legacy_dirs`. Returns false only if `dir`
// cannot be created. `report` (optional) receives a line about the copy.
bool prepare(const std::string& dir, const std::vector<std::string>& legacy_dirs,
             std::string* report);

// The legacy directories for this platform, from the environment.
std::vector<std::string> legacy_dirs_from_env();

} // namespace user_dir
