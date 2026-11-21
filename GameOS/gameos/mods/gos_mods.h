#ifndef GOS_MODS_H
#define GOS_MODS_H

// Mods inside the running game (see mods/mod_system.h for the model).
//
// GameOS main calls gos_ModsInit() before the game starts, from the command
// line options -mods <dir> and -modpack <modpack.ini>; without options a
// ./mods directory next to the game is used when it exists (all enabled
// mods). The file layer (mclib/file.cpp) then asks gos_ModResolvePath() for
// every file it opens.

#include <string>

// Returns false if mods were requested but cannot be used (missing
// dependency, conflict, bad manifest...). *report describes the result
// either way. Mods are inactive after a failure.
bool gos_ModsInit(const char* mods_dir, const char* modpack_file, std::string* report);
void gos_ModsShutdown();
bool gos_ModsActive();

// Path of the mod file that replaces / adds `game_path`, or NULL when no
// active mod provides it. The pointer stays valid until gos_ModsShutdown().
const char* gos_ModResolvePath(const char* game_path);

#endif
