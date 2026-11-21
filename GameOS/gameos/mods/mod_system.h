#ifndef MC2_MOD_SYSTEM_H
#define MC2_MOD_SYSTEM_H

// MechCommander 2 mod system.
//
//   Mod      a directory with a mod.ini manifest and a content/ tree that
//            mirrors the game directory. Files in content/ add to or replace
//            game files: data tables (mechs, weapons, pilots: data/objects),
//            missions and maps (data/missions, data/terrain), ABL scripts
//            (game logic and AI), art, sounds, fonts...
//   Modpack  a modpack.ini that selects a set of mods with version
//            requirements and optional priority overrides; it may bring its
//            own mods in a mods/ directory next to the manifest.
//   Modding  creating and checking mods: the mc2mod command line tool
//            (tools/mc2mod) is built on this API.
//
// Load order: a mod always loads after the mods it depends on; otherwise by
// priority (ascending), then id. A later mod wins when several provide the
// same file. Paths are matched case-insensitively with '/' separators, as the
// game lowercases every file name it opens.
//
// This library has no dependency on the rest of the game, so it is shared by
// the game, the mc2mod tool and the tests.

#include <map>
#include <stdint.h>
#include <string>
#include <vector>

namespace mc2mod {

// ----------------------------------------------------------------- versions

struct Version {
    int major = 0, minor = 0, patch = 0;

    // "1", "1.2", "1.2.3" (missing parts are 0). Returns false on garbage.
    static bool parse(const std::string& text, Version* out);
    std::string toString() const;
    int compare(const Version& o) const; // <0, 0, >0
    bool operator==(const Version& o) const { return compare(o) == 0; }
    bool operator<(const Version& o) const { return compare(o) < 0; }
};

// A requirement such as "*", "1.2.3" (= exact), "=1.2", ">=1.0", ">1.0",
// "<=2", "<2", "^1.2" (>=1.2.0 <2.0.0), "~1.2" (>=1.2.0 <1.3.0) or several
// joined with commas, which must all hold: ">=1.0, <1.5".
class VersionReq {
public:
    static bool parse(const std::string& text, VersionReq* out);
    bool matches(const Version& v) const;
    const std::string& text() const { return text_; }

private:
    enum Op { Any, Eq, Ge, Gt, Le, Lt, Caret, Tilde };
    struct Term { Op op; Version v; };
    std::vector<Term> terms_;
    std::string text_ = "*";
};

// --------------------------------------------------------------------- INI

// Minimal INI reader: [section], key = value, ';' or '#' comments.
// Keys and section names are case-insensitive (stored lowercase); values
// keep their case with surrounding blanks removed.
struct IniFile {
    std::map<std::string, std::map<std::string, std::string> > sections;
    // Keys in file order per section (order matters for [mods] lists)
    std::map<std::string, std::vector<std::string> > key_order;

    static bool parse(const std::string& text, IniFile* out, std::string* error);
    static bool load(const std::string& path, IniFile* out, std::string* error);
    const std::string* get(const std::string& section, const std::string& key) const;
};

// ---------------------------------------------------------------- manifests

struct Dependency {
    std::string id;
    VersionReq req;
    bool optional = false; // only affects load order when present
};

struct ModManifest {
    std::string id;            // [a-z0-9_.-], unique
    std::string name;
    Version version;
    std::string author;
    std::string description;
    int priority = 0;
    bool enabled = true;       // default selection when no modpack is used
    std::vector<Dependency> dependencies;
    std::vector<Dependency> conflicts; // req says which versions conflict
    std::string dir;           // mod directory
    std::string content_dir;   // dir + "/content"

    // Parses mod.ini text; `dir` becomes the mod directory.
    static bool parse(const std::string& ini_text, const std::string& dir, ModManifest* out,
                      std::vector<std::string>* errors);
    static bool load(const std::string& dir, ModManifest* out, std::vector<std::string>* errors);
};

bool isValidModId(const std::string& id);

struct ModpackEntry {
    std::string id;
    VersionReq req;
    bool has_priority = false;
    int priority = 0;
};

struct ModpackManifest {
    std::string id;
    std::string name;
    Version version;
    std::string author;
    std::string description;
    std::vector<ModpackEntry> mods; // in file order
    std::string dir;                // directory of modpack.ini
    std::string local_mods_dir;     // dir + "/mods" (scanned if present)

    static bool parse(const std::string& ini_text, const std::string& dir, ModpackManifest* out,
                      std::vector<std::string>* errors);
    // `path` is the modpack.ini file (or a directory containing one)
    static bool load(const std::string& path, ModpackManifest* out, std::vector<std::string>* errors);
};

// ----------------------------------------------------------------- registry

class ModRegistry {
public:
    // Adds every subdirectory of `mods_dir` that has a mod.ini. A later scan
    // replaces mods with the same id (a modpack's own mods override the
    // global ones). Problems are appended to errors(); valid mods still load.
    void scan(const std::string& mods_dir);
    bool add(const ModManifest& m); // false on duplicate id within one scan

    const ModManifest* find(const std::string& id) const;
    std::vector<const ModManifest*> all() const; // sorted by id
    const std::vector<std::string>& errors() const { return errors_; }

private:
    std::map<std::string, ModManifest> mods_;
    std::vector<std::string> errors_;
};

struct Selection {
    std::vector<std::string> ids;
    std::map<std::string, VersionReq> required;   // from a modpack
    std::map<std::string, int> priority_override; // from a modpack
};

// Every enabled mod of the registry.
Selection selectEnabled(const ModRegistry& reg);
// The mods a modpack lists (the registry must already contain them).
Selection selectModpack(const ModpackManifest& pack);

struct LoadOrder {
    std::vector<const ModManifest*> mods; // lowest precedence first
    std::vector<std::string> errors;      // empty = usable
    bool ok() const { return errors.empty(); }
};

// Checks requirements, dependencies (missing / wrong version / cycles) and
// conflicts, then orders the mods. Required dependencies that are present
// but not selected are pulled in automatically.
LoadOrder resolveLoadOrder(const ModRegistry& reg, const Selection& sel);

// ------------------------------------------------------------------ overlay

// Normalises a game path: lowercase, '/' separators, no "./" or leading '/'.
std::string normalizePath(const std::string& path);

// Virtual file system over the game directory: maps game paths to the file
// of the mod that wins for that path.
class Overlay {
public:
    struct Entry {
        std::string real_path;               // file to open
        std::string mod_id;                  // mod providing it
        std::vector<std::string> overridden; // earlier mods with the same file
    };

    void build(const LoadOrder& order);
    // Real path of the mod file for a game path, or NULL if no mod has it.
    const Entry* find(const std::string& game_path) const;
    size_t size() const { return files_.size(); }
    const std::map<std::string, Entry>& files() const { return files_; }
    const std::vector<std::string>& modIds() const { return mod_ids_; }

private:
    std::map<std::string, Entry> files_; // normalised game path -> entry
    std::vector<std::string> mod_ids_;
};

// Human readable summary: load order, files per mod, overridden files, errors.
std::string describe(const LoadOrder& order, const Overlay& overlay);

// --------------------------------------------------------------- scaffolding

// Creates <mods_dir>/<id>/mod.ini, content/ (with data/ subfolders) and a
// README. Fails if the directory exists or the id is invalid.
bool createModSkeleton(const std::string& mods_dir, const std::string& id, const std::string& name,
                       std::string* error);
// Writes a modpack.ini listing `mods` (each "id" or "id=requirement").
bool createModpack(const std::string& path, const std::string& id, const std::string& name,
                   const std::vector<std::string>& mods, std::string* error);

// Checks a single mod: manifest, a non-empty content folder, and file names
// that differ only in case (the game matches names case-insensitively).
std::vector<std::string> validateMod(const std::string& mod_dir);

} // namespace mc2mod

#endif
