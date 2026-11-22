// SPDX-License-Identifier: GPL-3.0-only
// Implementation of the mod system (see mod_system.h). Built as C++17 for
// std::filesystem; the header is plain C++11 so the game can include it.

#include "mod_system.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace mc2mod {

namespace {

std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool parseInt(const std::string& s, int* out)
{
    if (s.empty())
        return false;
    errno = 0;
    char* end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    if (errno || *end || v < -1000000 || v > 1000000)
        return false;
    *out = (int)v;
    return true;
}

bool parseBool(const std::string& s, bool* out)
{
    const std::string v = lower(s);
    if (v == "true" || v == "yes" || v == "1" || v == "on") { *out = true; return true; }
    if (v == "false" || v == "no" || v == "0" || v == "off") { *out = false; return true; }
    return false;
}

bool readFile(const std::string& path, std::string* out)
{
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

} // namespace

// ----------------------------------------------------------------- versions

bool Version::parse(const std::string& text, Version* out)
{
    const std::string t = trim(text);
    if (t.empty())
        return false;
    int parts[3] = {0, 0, 0};
    int n = 0;
    size_t pos = 0;
    while (true) {
        size_t dot = t.find('.', pos);
        std::string part = t.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        if (n >= 3 || part.empty() || part.find_first_not_of("0123456789") != std::string::npos ||
            part.size() > 6)
            return false;
        parts[n++] = atoi(part.c_str());
        if (dot == std::string::npos)
            break;
        pos = dot + 1;
    }
    if (out) {
        out->major = parts[0];
        out->minor = parts[1];
        out->patch = parts[2];
    }
    return true;
}

std::string Version::toString() const
{
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

int Version::compare(const Version& o) const
{
    if (major != o.major) return major < o.major ? -1 : 1;
    if (minor != o.minor) return minor < o.minor ? -1 : 1;
    if (patch != o.patch) return patch < o.patch ? -1 : 1;
    return 0;
}

bool VersionReq::parse(const std::string& text, VersionReq* out)
{
    VersionReq r;
    r.text_ = trim(text);
    if (r.text_.empty() || r.text_ == "*") {
        r.text_ = "*";
        if (out) *out = r;
        return true;
    }
    std::stringstream ss(r.text_);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string t = trim(item);
        Term term;
        if (t == "*") { term.op = Any; r.terms_.push_back(term); continue; }
        size_t skip = 0;
        if (t.compare(0, 2, ">=") == 0) { term.op = Ge; skip = 2; }
        else if (t.compare(0, 2, "<=") == 0) { term.op = Le; skip = 2; }
        else if (t[0] == '>') { term.op = Gt; skip = 1; }
        else if (t[0] == '<') { term.op = Lt; skip = 1; }
        else if (t[0] == '=') { term.op = Eq; skip = 1; }
        else if (t[0] == '^') { term.op = Caret; skip = 1; }
        else if (t[0] == '~') { term.op = Tilde; skip = 1; }
        else { term.op = Eq; }
        if (!Version::parse(t.substr(skip), &term.v))
            return false;
        r.terms_.push_back(term);
    }
    if (r.terms_.empty())
        return false;
    if (out) *out = r;
    return true;
}

bool VersionReq::matches(const Version& v) const
{
    for (const Term& t : terms_) {
        const int c = v.compare(t.v);
        bool ok = true;
        switch (t.op) {
        case Any: ok = true; break;
        case Eq: ok = c == 0; break;
        case Ge: ok = c >= 0; break;
        case Gt: ok = c > 0; break;
        case Le: ok = c <= 0; break;
        case Lt: ok = c < 0; break;
        case Caret: {
            // same leftmost non-zero component, at least t.v
            Version upper;
            if (t.v.major > 0) upper.major = t.v.major + 1;
            else if (t.v.minor > 0) upper.minor = t.v.minor + 1;
            else upper.patch = t.v.patch + 1;
            ok = c >= 0 && v.compare(upper) < 0;
            break;
        }
        case Tilde: {
            Version upper = t.v;
            upper.minor += 1;
            upper.patch = 0;
            ok = c >= 0 && v.compare(upper) < 0;
            break;
        }
        }
        if (!ok)
            return false;
    }
    return true;
}

// --------------------------------------------------------------------- INI

bool IniFile::parse(const std::string& text, IniFile* out, std::string* error)
{
    IniFile ini;
    std::istringstream in(text);
    std::string line, section;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (line_no == 1 && line.size() >= 3 && (unsigned char)line[0] == 0xEF)
            line.erase(0, 3); // UTF-8 BOM
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#')
            continue;
        if (t[0] == '[') {
            if (t.back() != ']') {
                if (error) *error = "line " + std::to_string(line_no) + ": missing ']'";
                return false;
            }
            section = lower(trim(t.substr(1, t.size() - 2)));
            ini.sections[section];
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos || eq == 0) {
            if (error) *error = "line " + std::to_string(line_no) + ": expected 'key = value'";
            return false;
        }
        const std::string key = lower(trim(t.substr(0, eq)));
        std::string value = trim(t.substr(eq + 1));
        // strip a trailing comment that follows whitespace
        for (const char* c : {" ;", " #", "\t;", "\t#"}) {
            size_t p = value.find(c);
            if (p != std::string::npos)
                value = trim(value.substr(0, p));
        }
        if (!ini.sections[section].count(key))
            ini.key_order[section].push_back(key);
        ini.sections[section][key] = value;
    }
    *out = ini;
    return true;
}

bool IniFile::load(const std::string& path, IniFile* out, std::string* error)
{
    std::string text;
    if (!readFile(path, &text)) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    if (!parse(text, out, error)) {
        if (error) *error = path + ": " + *error;
        return false;
    }
    return true;
}

const std::string* IniFile::get(const std::string& section, const std::string& key) const
{
    auto s = sections.find(section);
    if (s == sections.end())
        return nullptr;
    auto k = s->second.find(key);
    return k == s->second.end() ? nullptr : &k->second;
}

// ---------------------------------------------------------------- manifests

bool isValidModId(const std::string& id)
{
    if (id.empty() || id.size() > 64)
        return false;
    for (char c : id)
        if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_' || c == '-' || c == '.'))
            return false;
    return id[0] != '.' && id[0] != '-';
}

static void parseDependencyList(const IniFile& ini, const std::string& section, bool optional,
                                std::vector<Dependency>* out, std::vector<std::string>* errors,
                                const std::string& where)
{
    auto it = ini.key_order.find(section);
    if (it == ini.key_order.end())
        return;
    for (const std::string& id : it->second) {
        Dependency d;
        d.id = id;
        d.optional = optional;
        if (!isValidModId(id))
            errors->push_back(where + ": [" + section + "] invalid mod id '" + id + "'");
        else if (!VersionReq::parse(*ini.get(section, id), &d.req))
            errors->push_back(where + ": [" + section + "] bad version requirement '" +
                              *ini.get(section, id) + "' for " + id);
        else
            out->push_back(d);
    }
}

bool ModManifest::parse(const std::string& text, const std::string& dir, ModManifest* out,
                        std::vector<std::string>* errors)
{
    const std::string where = dir.empty() ? "mod.ini" : dir + "/mod.ini";
    const size_t errors_before = errors->size();
    IniFile ini;
    std::string err;
    if (!IniFile::parse(text, &ini, &err)) {
        errors->push_back(where + ": " + err);
        return false;
    }
    ModManifest m;
    m.dir = dir;
    m.content_dir = dir.empty() ? "content" : dir + "/content";

    const std::string* v = ini.get("mod", "id");
    if (!v || !isValidModId(*v))
        errors->push_back(where + ": [mod] id is missing or invalid (use a-z, 0-9, _ . -)");
    else
        m.id = *v;
    v = ini.get("mod", "version");
    if (!v || !Version::parse(*v, &m.version))
        errors->push_back(where + ": [mod] version is missing or invalid (e.g. 1.0.0)");
    v = ini.get("mod", "name");
    m.name = v ? *v : m.id;
    if ((v = ini.get("mod", "author"))) m.author = *v;
    if ((v = ini.get("mod", "description"))) m.description = *v;
    if ((v = ini.get("mod", "priority")) && !parseInt(*v, &m.priority))
        errors->push_back(where + ": [mod] priority must be an integer");
    if ((v = ini.get("mod", "enabled")) && !parseBool(*v, &m.enabled))
        errors->push_back(where + ": [mod] enabled must be true or false");

    parseDependencyList(ini, "dependencies", false, &m.dependencies, errors, where);
    parseDependencyList(ini, "optional", true, &m.dependencies, errors, where);
    parseDependencyList(ini, "conflicts", false, &m.conflicts, errors, where);
    for (const Dependency& d : m.dependencies)
        if (d.id == m.id)
            errors->push_back(where + ": a mod cannot depend on itself");

    if (errors->size() != errors_before)
        return false;
    *out = m;
    return true;
}

bool ModManifest::load(const std::string& dir, ModManifest* out, std::vector<std::string>* errors)
{
    std::string text;
    if (!readFile(dir + "/mod.ini", &text)) {
        errors->push_back(dir + ": no mod.ini");
        return false;
    }
    return parse(text, dir, out, errors);
}

bool ModpackManifest::parse(const std::string& text, const std::string& dir, ModpackManifest* out,
                            std::vector<std::string>* errors)
{
    const std::string where = dir.empty() ? "modpack.ini" : dir + "/modpack.ini";
    const size_t errors_before = errors->size();
    IniFile ini;
    std::string err;
    if (!IniFile::parse(text, &ini, &err)) {
        errors->push_back(where + ": " + err);
        return false;
    }
    ModpackManifest p;
    p.dir = dir;
    p.local_mods_dir = dir.empty() ? "mods" : dir + "/mods";

    const std::string* v = ini.get("modpack", "id");
    if (!v || !isValidModId(*v))
        errors->push_back(where + ": [modpack] id is missing or invalid");
    else
        p.id = *v;
    v = ini.get("modpack", "version");
    if (!v || !Version::parse(*v, &p.version))
        errors->push_back(where + ": [modpack] version is missing or invalid");
    v = ini.get("modpack", "name");
    p.name = v ? *v : p.id;
    if ((v = ini.get("modpack", "author"))) p.author = *v;
    if ((v = ini.get("modpack", "description"))) p.description = *v;

    auto mods = ini.key_order.find("mods");
    if (mods == ini.key_order.end() || mods->second.empty())
        errors->push_back(where + ": [mods] lists no mods");
    else
        for (const std::string& id : mods->second) {
            ModpackEntry e;
            e.id = id;
            if (!isValidModId(id))
                errors->push_back(where + ": [mods] invalid mod id '" + id + "'");
            else if (!VersionReq::parse(*ini.get("mods", id), &e.req))
                errors->push_back(where + ": [mods] bad version requirement for " + id);
            else
                p.mods.push_back(e);
        }
    auto prio = ini.key_order.find("priority");
    if (prio != ini.key_order.end())
        for (const std::string& id : prio->second) {
            auto e = std::find_if(p.mods.begin(), p.mods.end(),
                                  [&](const ModpackEntry& x) { return x.id == id; });
            int value = 0;
            if (e == p.mods.end())
                errors->push_back(where + ": [priority] " + id + " is not listed in [mods]");
            else if (!parseInt(*ini.get("priority", id), &value))
                errors->push_back(where + ": [priority] " + id + " must be an integer");
            else {
                e->has_priority = true;
                e->priority = value;
            }
        }

    if (errors->size() != errors_before)
        return false;
    *out = p;
    return true;
}

bool ModpackManifest::load(const std::string& path, ModpackManifest* out, std::vector<std::string>* errors)
{
    std::string file = path;
    std::error_code ec;
    if (fs::is_directory(path, ec))
        file = path + "/modpack.ini";
    std::string text;
    if (!readFile(file, &text)) {
        errors->push_back("cannot read " + file);
        return false;
    }
    std::string dir = fs::path(file).parent_path().string();
    if (dir.empty())
        dir = ".";
    return parse(text, dir, out, errors);
}

// ----------------------------------------------------------------- registry

void ModRegistry::scan(const std::string& mods_dir)
{
    std::error_code ec;
    if (!fs::is_directory(mods_dir, ec)) {
        errors_.push_back(mods_dir + ": not a directory");
        return;
    }
    std::vector<std::string> dirs;
    for (fs::directory_iterator it(mods_dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec) && fs::exists(it->path() / "mod.ini", ec))
            dirs.push_back(it->path().string());
    std::sort(dirs.begin(), dirs.end());

    std::set<std::string> seen; // duplicate ids within this directory
    for (const std::string& d : dirs) {
        ModManifest m;
        if (!ModManifest::load(d, &m, &errors_))
            continue;
        if (seen.count(m.id)) {
            errors_.push_back(d + ": duplicate mod id '" + m.id + "' in " + mods_dir);
            continue;
        }
        seen.insert(m.id);
        mods_[m.id] = m; // replaces a mod of an earlier scan
    }
}

bool ModRegistry::add(const ModManifest& m)
{
    if (mods_.count(m.id))
        return false;
    mods_[m.id] = m;
    return true;
}

const ModManifest* ModRegistry::find(const std::string& id) const
{
    auto it = mods_.find(id);
    return it == mods_.end() ? nullptr : &it->second;
}

std::vector<const ModManifest*> ModRegistry::all() const
{
    std::vector<const ModManifest*> v;
    for (auto& kv : mods_)
        v.push_back(&kv.second);
    return v;
}

Selection selectEnabled(const ModRegistry& reg)
{
    Selection s;
    for (const ModManifest* m : reg.all())
        if (m->enabled)
            s.ids.push_back(m->id);
    return s;
}

Selection selectModpack(const ModpackManifest& pack)
{
    Selection s;
    for (const ModpackEntry& e : pack.mods) {
        s.ids.push_back(e.id);
        s.required[e.id] = e.req;
        if (e.has_priority)
            s.priority_override[e.id] = e.priority;
    }
    return s;
}

LoadOrder resolveLoadOrder(const ModRegistry& reg, const Selection& sel)
{
    LoadOrder out;

    // 1. selected mods exist and satisfy the selection's requirements
    std::set<std::string> chosen;
    std::vector<std::string> queue;
    for (const std::string& id : sel.ids) {
        const ModManifest* m = reg.find(id);
        if (!m) {
            out.errors.push_back("mod '" + id + "' is not installed");
            continue;
        }
        auto r = sel.required.find(id);
        if (r != sel.required.end() && !r->second.matches(m->version)) {
            out.errors.push_back("mod '" + id + "' is version " + m->version.toString() +
                                 ", but " + r->second.text() + " is required");
        }
        if (chosen.insert(id).second)
            queue.push_back(id);
    }

    // 2. pull in required dependencies; check versions
    for (size_t i = 0; i < queue.size(); ++i) {
        const ModManifest* m = reg.find(queue[i]);
        for (const Dependency& d : m->dependencies) {
            const ModManifest* dep = reg.find(d.id);
            if (!dep) {
                if (!d.optional)
                    out.errors.push_back("mod '" + m->id + "' needs '" + d.id + "' (" + d.req.text() +
                                         "), which is not installed");
                continue;
            }
            if (!d.req.matches(dep->version)) {
                if (!d.optional || chosen.count(d.id))
                    out.errors.push_back("mod '" + m->id + "' needs '" + d.id + "' " + d.req.text() +
                                         ", found " + dep->version.toString());
                continue;
            }
            if (!d.optional && chosen.insert(d.id).second)
                queue.push_back(d.id);
        }
    }

    // 3. conflicts between active mods
    for (const std::string& id : chosen) {
        const ModManifest* m = reg.find(id);
        for (const Dependency& c : m->conflicts) {
            const ModManifest* other = reg.find(c.id);
            if (other && chosen.count(c.id) && c.req.matches(other->version))
                out.errors.push_back("mod '" + id + "' conflicts with '" + c.id + "' " +
                                     other->version.toString());
        }
    }

    // 4. topological order; among ready mods lowest (priority, id) first
    auto priority = [&](const std::string& id) {
        auto o = sel.priority_override.find(id);
        return o != sel.priority_override.end() ? o->second : reg.find(id)->priority;
    };
    std::map<std::string, std::set<std::string> > before; // id -> mods it must follow
    for (const std::string& id : chosen)
        for (const Dependency& d : reg.find(id)->dependencies)
            if (chosen.count(d.id))
                before[id].insert(d.id);

    std::set<std::string> placed;
    while (placed.size() < chosen.size()) {
        std::string best;
        bool found = false;
        for (const std::string& id : chosen) {
            if (placed.count(id))
                continue;
            bool ready = true;
            for (const std::string& b : before[id])
                if (!placed.count(b)) { ready = false; break; }
            if (!ready)
                continue;
            if (!found || priority(id) < priority(best) || (priority(id) == priority(best) && id < best)) {
                best = id;
                found = true;
            }
        }
        if (!found) {
            std::string cycle;
            for (const std::string& id : chosen)
                if (!placed.count(id))
                    cycle += (cycle.empty() ? "" : ", ") + id;
            out.errors.push_back("dependency cycle between: " + cycle);
            break;
        }
        placed.insert(best);
        out.mods.push_back(reg.find(best));
    }
    return out;
}

// ------------------------------------------------------------------ overlay

std::string normalizePath(const std::string& path)
{
    std::string p = lower(path);
    std::replace(p.begin(), p.end(), '\\', '/');
    std::string out;
    out.reserve(p.size());
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '/' && (out.empty() || out.back() == '/'))
            continue; // leading or repeated separators
        out.push_back(p[i]);
    }
    while (out.compare(0, 2, "./") == 0)
        out.erase(0, 2);
    size_t pos;
    while ((pos = out.find("/./")) != std::string::npos)
        out.erase(pos, 2);
    return out;
}

void Overlay::build(const LoadOrder& order)
{
    files_.clear();
    mod_ids_.clear();
    for (const ModManifest* m : order.mods) {
        mod_ids_.push_back(m->id);
        std::error_code ec;
        if (!fs::is_directory(m->content_dir, ec))
            continue;
        for (fs::recursive_directory_iterator it(m->content_dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec))
                continue;
            const std::string rel = fs::relative(it->path(), m->content_dir, ec).generic_string();
            Entry& e = files_[normalizePath(rel)];
            if (!e.mod_id.empty())
                e.overridden.push_back(e.mod_id);
            e.mod_id = m->id;
            e.real_path = it->path().string();
        }
    }
}

const Overlay::Entry* Overlay::find(const std::string& game_path) const
{
    if (files_.empty())
        return nullptr;
    auto it = files_.find(normalizePath(game_path));
    return it == files_.end() ? nullptr : &it->second;
}

std::string describe(const LoadOrder& order, const Overlay& overlay)
{
    std::ostringstream o;
    o << "Load order (" << order.mods.size() << " mods, last wins):\n";
    int i = 1;
    for (const ModManifest* m : order.mods) {
        size_t n = 0;
        for (auto& kv : overlay.files())
            if (kv.second.mod_id == m->id) ++n;
        o << "  " << i++ << ". " << m->id << " " << m->version.toString() << "  (" << m->name
          << ", priority " << m->priority << ", " << n << " files)\n";
    }
    size_t overrides = 0;
    for (auto& kv : overlay.files())
        if (!kv.second.overridden.empty()) {
            if (!overrides++)
                o << "Files provided by more than one mod:\n";
            o << "  " << kv.first << ": " << kv.second.mod_id << " (over";
            for (const std::string& id : kv.second.overridden)
                o << " " << id;
            o << ")\n";
        }
    if (!order.errors.empty()) {
        o << "Errors:\n";
        for (const std::string& e : order.errors)
            o << "  " << e << "\n";
    }
    return o.str();
}

// --------------------------------------------------------------- scaffolding

bool createModSkeleton(const std::string& mods_dir, const std::string& id, const std::string& name,
                       std::string* error)
{
    if (!isValidModId(id)) {
        if (error) *error = "invalid mod id '" + id + "' (use a-z, 0-9, _ . -)";
        return false;
    }
    const fs::path dir = fs::path(mods_dir) / id;
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        if (error) *error = dir.string() + " already exists";
        return false;
    }
    for (const char* sub : {"content/data/objects", "content/data/missions", "content/data/art",
                            "content/data/sound"})
        fs::create_directories(dir / sub, ec);
    if (ec) {
        if (error) *error = "cannot create " + dir.string() + ": " + ec.message();
        return false;
    }
    std::ofstream ini((dir / "mod.ini").string());
    ini << "; MechCommander 2 mod manifest\n"
           "[mod]\n"
           "id = " << id << "\n"
           "name = " << (name.empty() ? id : name) << "\n"
           "version = 0.1.0\n"
           "author =\n"
           "description =\n"
           "; higher priority loads later and wins file conflicts\n"
           "priority = 0\n"
           "enabled = true\n"
           "\n"
           "; mods that must be installed, with version requirements (*, 1.2, >=1.0, ^1.2, ~1.2)\n"
           "[dependencies]\n"
           "\n"
           "; mods that only affect the load order when installed\n"
           "[optional]\n"
           "\n"
           "; mods (and versions) this mod cannot be used with\n"
           "[conflicts]\n";
    std::ofstream readme((dir / "README.txt").string());
    readme << (name.empty() ? id : name) << "\n\n"
           "Put files under content/ with the same relative path as in the game directory,\n"
           "e.g. content/data/objects/<mech>.csv or content/data/missions/<mission>.fit.\n"
           "They replace the game's files (also those inside .fst archives) or add new ones.\n"
           "Check the mod with:  mc2mod validate <this directory>\n";
    if (!ini || !readme) {
        if (error) *error = "cannot write files in " + dir.string();
        return false;
    }
    return true;
}

bool createModpack(const std::string& path, const std::string& id, const std::string& name,
                   const std::vector<std::string>& mods, std::string* error)
{
    if (!isValidModId(id)) {
        if (error) *error = "invalid modpack id '" + id + "'";
        return false;
    }
    std::ostringstream o;
    o << "; MechCommander 2 modpack\n[modpack]\nid = " << id << "\nname = " << (name.empty() ? id : name)
      << "\nversion = 0.1.0\nauthor =\ndescription =\n\n; mod id = version requirement\n[mods]\n";
    for (const std::string& m : mods) {
        const size_t eq = m.find('=');
        const std::string mid = trim(m.substr(0, eq));
        const std::string req = eq == std::string::npos ? "*" : trim(m.substr(eq + 1));
        VersionReq r;
        if (!isValidModId(mid) || !VersionReq::parse(req, &r)) {
            if (error) *error = "bad mod entry '" + m + "'";
            return false;
        }
        o << mid << " = " << req << "\n";
    }
    o << "\n; optional priority overrides: mod id = number\n[priority]\n";
    std::ofstream f(path.c_str());
    f << o.str();
    if (!f) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    return true;
}

std::vector<std::string> validateMod(const std::string& mod_dir)
{
    std::vector<std::string> errors;
    ModManifest m;
    if (!ModManifest::load(mod_dir, &m, &errors))
        return errors;
    std::error_code ec;
    if (!fs::is_directory(m.content_dir, ec)) {
        errors.push_back(mod_dir + ": no content/ directory");
        return errors;
    }
    size_t files = 0;
    std::map<std::string, std::string> seen; // case-insensitive duplicates
    for (fs::recursive_directory_iterator it(m.content_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        ++files;
        const std::string rel = fs::relative(it->path(), m.content_dir, ec).generic_string();
        const std::string key = normalizePath(rel);
        auto prev = seen.find(key);
        if (prev != seen.end())
            errors.push_back(mod_dir + ": '" + rel + "' and '" + prev->second +
                             "' differ only in case; the game cannot tell them apart");
        else
            seen[key] = rel;
    }
    if (files == 0)
        errors.push_back(mod_dir + ": content/ is empty");
    return errors;
}

} // namespace mc2mod
