// SPDX-License-Identifier: GPL-3.0-only
#include "user_dir.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace user_dir {

const char* const kLegacyName = ".mechcommander2";
const char* const kMovedNoteName = "MOVED.txt";

static bool is_empty_dir(const fs::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec) && fs::is_empty(p, ec) && !ec;
}

bool prepare(const std::string& dir, const std::vector<std::string>& legacy_dirs,
             std::string* report)
{
    std::error_code ec;
    const fs::path target(dir);
    fs::create_directories(target, ec);
    if (!fs::is_directory(target, ec))
        return false;
    if (!is_empty_dir(target))
        return true; // already in use: never mix in old data

    for (const std::string& legacy : legacy_dirs) {
        const fs::path from(legacy);
        if (legacy.empty() || !fs::is_directory(from, ec) || is_empty_dir(from))
            continue;
        if (fs::equivalent(from, target, ec))
            return true;

        fs::copy(from, target, fs::copy_options::recursive | fs::copy_options::skip_existing, ec);
        if (ec) {
            if (report)
                *report = "could not copy " + legacy + " to " + dir + ": " + ec.message() + "\n";
            return true;
        }
        fs::remove(target / kMovedNoteName, ec); // the note belongs to the old place only

        if (FILE* f = fopen((from / kMovedNoteName).string().c_str(), "w")) {
            fprintf(f, "MechCommander 2 now keeps saved games and settings in\n%s\n"
                       "The files in this directory were copied there and are no longer used.\n",
                    dir.c_str());
            fclose(f);
        }
        if (report)
            *report = "copied saved games and settings from " + legacy + " to " + dir + "\n";
        return true;
    }
    return true;
}

std::vector<std::string> legacy_dirs_from_env()
{
    std::vector<std::string> dirs;
    auto add = [&](const char* base) {
        if (base && *base)
            dirs.push_back((fs::path(base) / kLegacyName).string());
    };
#ifdef _WIN32
    add(getenv("USERPROFILE"));
#else
    add(getenv("XDG_CONFIG_HOME"));
    add(getenv("HOME"));
#endif
    return dirs;
}

} // namespace user_dir
