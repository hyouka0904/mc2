// SPDX-License-Identifier: GPL-3.0-only
// Tests for the mod system (GameOS/gameos/mods): versions and requirements,
// manifests, modpacks, load order, the file overlay, scaffolding and the
// in-game hook gos_ModResolvePath().

#include "mc2test.h"

#include "gos_mods.h"
#include "mod_system.h"

#include <filesystem>
#include <fstream>
#include <random>

namespace fs = std::filesystem;
using namespace mc2mod;

namespace {

// A temporary directory removed when the test ends.
struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("mc2mod_test_" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string str() const { return path.string(); }
};

void writeFile(const fs::path& p, const std::string& text)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p.string()) << text;
}

// Creates <dir>/<id>/mod.ini with the given extra sections and content files.
void makeMod(const fs::path& dir, const std::string& id, const std::string& version, const std::string& extra,
             const std::vector<std::string>& files = {}, const std::string& mod_section_extra = "")
{
    writeFile(dir / id / "mod.ini", "[mod]\nid = " + id + "\nversion = " + version + "\n" +
                                        mod_section_extra + extra);
    for (const std::string& f : files)
        writeFile(dir / id / "content" / f, id + ":" + f);
}

std::vector<std::string> ids(const LoadOrder& o)
{
    std::vector<std::string> v;
    for (const ModManifest* m : o.mods)
        v.push_back(m->id);
    return v;
}

std::string readAll(const std::string& path)
{
    std::ifstream f(path);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

bool contains(const std::vector<std::string>& errors, const std::string& needle)
{
    for (const std::string& e : errors)
        if (e.find(needle) != std::string::npos)
            return true;
    return false;
}

} // namespace

// ----------------------------------------------------------------- versions

TEST(version_parse_and_compare)
{
    Version v;
    REQUIRE(Version::parse("1.2.3", &v));
    CHECK(v.major == 1 && v.minor == 2 && v.patch == 3);
    REQUIRE(Version::parse(" 2 ", &v));
    CHECK(v.toString() == "2.0.0");
    REQUIRE(Version::parse("0.10", &v));
    CHECK(v.toString() == "0.10.0");
    for (const char* bad : {"", "a", "1.", ".1", "1.2.3.4", "1.-2", "1.2b"})
        CHECK(!Version::parse(bad, nullptr));
    Version a, b;
    Version::parse("1.10.0", &a);
    Version::parse("1.9.9", &b);
    CHECK(b < a); // numeric, not lexical
    CHECK(a.compare(a) == 0);
}

TEST(version_requirements)
{
    struct Case { const char* req; const char* version; bool ok; } cases[] = {
        {"*", "0.0.1", true},       {"1.2.3", "1.2.3", true},  {"1.2.3", "1.2.4", false},
        {"=1.2", "1.2.0", true},    {">=1.0", "1.0.0", true},  {">=1.0", "0.9.9", false},
        {">1.0", "1.0.0", false},   {"<2", "1.99.0", true},    {"<=2", "2.0.0", true},
        {"^1.2", "1.9.0", true},    {"^1.2", "2.0.0", false},  {"^1.2", "1.1.9", false},
        {"^0.3", "0.3.5", true},    {"^0.3", "0.4.0", false},  {"^0.0.3", "0.0.4", false},
        {"~1.2", "1.2.9", true},    {"~1.2", "1.3.0", false},
        {">=1.0, <1.5", "1.4.9", true}, {">=1.0, <1.5", "1.5.0", false},
    };
    for (const Case& c : cases) {
        VersionReq r;
        Version v;
        REQUIRE(VersionReq::parse(c.req, &r));
        REQUIRE(Version::parse(c.version, &v));
        CHECK(r.matches(v) == c.ok);
    }
    for (const char* bad : {">=", "^x", "1.2,,", ">=1.0, abc"})
        CHECK(!VersionReq::parse(bad, nullptr));
}

// --------------------------------------------------------------------- INI

TEST(ini_parsing)
{
    IniFile ini;
    std::string err;
    REQUIRE(IniFile::parse("; comment\n[Mod]\nID = abc  ; trailing\nname=Hello World\n# c\n[deps]\nb = 1\na = 2\n",
                           &ini, &err));
    CHECK(*ini.get("mod", "id") == "abc");
    CHECK(*ini.get("mod", "name") == "Hello World");
    CHECK(ini.key_order["deps"].size() == 2 && ini.key_order["deps"][0] == "b"); // file order kept
    CHECK(ini.get("mod", "missing") == nullptr);
    CHECK(!IniFile::parse("[mod\nid=1\n", &ini, &err));
    CHECK(err.find("line 1") == 0);
    CHECK(!IniFile::parse("[mod]\njustakey\n", &ini, &err));
    CHECK(err.find("line 2") == 0);
}

// ---------------------------------------------------------------- manifests

TEST(mod_manifest_parse)
{
    ModManifest m;
    std::vector<std::string> errors;
    REQUIRE(ModManifest::parse("[mod]\nid = laser_pack\nname = Laser Pack\nversion = 2.1\nauthor = me\n"
                               "priority = -3\nenabled = no\n[dependencies]\ncore = ^1.0\n"
                               "[optional]\nfancy_ui = >=2\n[conflicts]\nold_lasers = *\n",
                               "/mods/laser_pack", &m, &errors));
    CHECK(errors.empty());
    CHECK(m.id == "laser_pack");
    CHECK(m.name == "Laser Pack");
    CHECK(m.version.toString() == "2.1.0");
    CHECK(m.priority == -3);
    CHECK(!m.enabled);
    REQUIRE(m.dependencies.size() == 2);
    CHECK(m.dependencies[0].id == "core" && !m.dependencies[0].optional);
    CHECK(m.dependencies[1].id == "fancy_ui" && m.dependencies[1].optional);
    CHECK(m.conflicts.size() == 1);
    CHECK(m.content_dir == "/mods/laser_pack/content");
}

TEST(mod_manifest_reports_every_problem)
{
    ModManifest m;
    std::vector<std::string> errors;
    CHECK(!ModManifest::parse("[mod]\nid = Bad Id\nversion = x\npriority = high\n[dependencies]\nok = >=\n",
                              "d", &m, &errors));
    CHECK(contains(errors, "id is missing or invalid"));
    CHECK(contains(errors, "version is missing or invalid"));
    CHECK(contains(errors, "priority must be an integer"));
    CHECK(contains(errors, "bad version requirement"));

    errors.clear();
    CHECK(!ModManifest::parse("[mod]\nid = a\nversion = 1\n[dependencies]\na = *\n", "d", &m, &errors));
    CHECK(contains(errors, "cannot depend on itself"));
    CHECK(isValidModId("a.b-c_9"));
    CHECK(!isValidModId("Upper"));
    CHECK(!isValidModId(".hidden"));
    CHECK(!isValidModId(""));
}

TEST(modpack_manifest_parse)
{
    ModpackManifest p;
    std::vector<std::string> errors;
    REQUIRE(ModpackManifest::parse("[modpack]\nid = hardcore\nversion = 1.0\n[mods]\nzeta = *\nalpha = ^2\n"
                                   "[priority]\nalpha = 50\n", "/packs/hc", &p, &errors));
    REQUIRE(p.mods.size() == 2);
    CHECK(p.mods[0].id == "zeta"); // file order
    CHECK(p.mods[1].has_priority && p.mods[1].priority == 50);
    CHECK(p.local_mods_dir == "/packs/hc/mods");

    errors.clear();
    CHECK(!ModpackManifest::parse("[modpack]\nid = x\nversion = 1\n[priority]\nnope = 1\n", "", &p, &errors));
    CHECK(contains(errors, "lists no mods"));
    CHECK(contains(errors, "not listed in [mods]"));
}

// ----------------------------------------------------------------- registry

TEST(registry_scan_collects_mods_and_errors)
{
    TempDir t;
    makeMod(t.path, "good", "1.0.0", "");
    writeFile(t.path / "broken" / "mod.ini", "[mod]\nid = broken\n"); // no version
    fs::create_directories(t.path / "not_a_mod");                    // no mod.ini: ignored
    makeMod(t.path, "dupe_a", "1.0.0", "");
    writeFile(t.path / "dupe_b" / "mod.ini", "[mod]\nid = dupe_a\nversion = 2\n");

    ModRegistry reg;
    reg.scan(t.str());
    CHECK(reg.find("good") != nullptr);
    CHECK(reg.find("broken") == nullptr);
    CHECK(reg.find("dupe_a") != nullptr);
    CHECK(contains(reg.errors(), "version is missing"));
    CHECK(contains(reg.errors(), "duplicate mod id 'dupe_a'"));
    CHECK_EQ(reg.all().size(), (size_t)2);

    ModRegistry missing;
    missing.scan((t.path / "nope").string());
    CHECK(contains(missing.errors(), "not a directory"));
}

// --------------------------------------------------------------- load order

TEST(load_order_respects_dependencies_then_priority)
{
    TempDir t;
    makeMod(t.path, "core", "1.4.0", "", {}, "priority = 100\n");     // high priority...
    makeMod(t.path, "lasers", "2.0.0", "[dependencies]\ncore = ^1.0\n"); // ...but a dependency loads first
    makeMod(t.path, "ui", "1.0.0", "", {}, "priority = 5\n");
    makeMod(t.path, "audio", "1.0.0", "");
    makeMod(t.path, "off", "1.0.0", "", {}, "enabled = false\n");

    ModRegistry reg;
    reg.scan(t.str());
    LoadOrder o = resolveLoadOrder(reg, selectEnabled(reg));
    REQUIRE(o.ok());
    // ready set {audio(0), core(100), ui(5)} -> audio, ui, core, then lasers
    const std::vector<std::string> expect = {"audio", "ui", "core", "lasers"};
    CHECK(ids(o) == expect);
}

TEST(load_order_pulls_in_required_dependencies)
{
    TempDir t;
    makeMod(t.path, "base", "1.0.0", "", {}, "enabled = false\n");
    makeMod(t.path, "addon", "1.0.0", "[dependencies]\nbase = *\n");
    ModRegistry reg;
    reg.scan(t.str());
    LoadOrder o = resolveLoadOrder(reg, selectEnabled(reg));
    REQUIRE(o.ok());
    const std::vector<std::string> expect = {"base", "addon"};
    CHECK(ids(o) == expect);
}

TEST(load_order_reports_problems)
{
    TempDir t;
    makeMod(t.path, "needs_missing", "1.0.0", "[dependencies]\nghost = *\n");
    makeMod(t.path, "old_core", "0.9.0", "");
    makeMod(t.path, "needs_new_core", "1.0.0", "[dependencies]\nold_core = >=1.0\n");
    makeMod(t.path, "a", "1.0.0", "[dependencies]\nb = *\n");
    makeMod(t.path, "b", "1.0.0", "[dependencies]\na = *\n");
    makeMod(t.path, "x", "1.0.0", "[conflicts]\ny = <2\n");
    makeMod(t.path, "y", "1.5.0", "");
    makeMod(t.path, "opt", "1.0.0", "[optional]\nghost = *\n"); // fine without it

    ModRegistry reg;
    reg.scan(t.str());
    LoadOrder o = resolveLoadOrder(reg, selectEnabled(reg));
    CHECK(!o.ok());
    CHECK(contains(o.errors, "'needs_missing' needs 'ghost'"));
    CHECK(contains(o.errors, "needs 'old_core' >=1.0, found 0.9.0"));
    CHECK(contains(o.errors, "dependency cycle between: a, b"));
    CHECK(contains(o.errors, "'x' conflicts with 'y'"));
    CHECK(!contains(o.errors, "'opt'"));

    Selection sel;
    sel.ids.push_back("not_installed");
    CHECK(contains(resolveLoadOrder(reg, sel).errors, "'not_installed' is not installed"));
}

TEST(optional_dependency_orders_only_when_present)
{
    TempDir t;
    makeMod(t.path, "zz_theme", "1.0.0", "", {}, "priority = 9\n");
    makeMod(t.path, "hud", "1.0.0", "[optional]\nzz_theme = *\n");
    ModRegistry reg;
    reg.scan(t.str());
    LoadOrder o = resolveLoadOrder(reg, selectEnabled(reg));
    REQUIRE(o.ok());
    const std::vector<std::string> expect = {"zz_theme", "hud"}; // despite priorities 9 vs 0
    CHECK(ids(o) == expect);
}

TEST(modpack_selects_versions_and_priorities)
{
    TempDir t;
    makeMod(t.path / "mods", "red", "1.0.0", "", {"data/art/menu.tga"});
    makeMod(t.path / "mods", "blue", "1.2.0", "", {"data/art/menu.tga"}, "priority = 5\n");
    makeMod(t.path / "mods", "unlisted", "1.0.0", "", {"data/x.txt"});
    writeFile(t.path / "pack" / "modpack.ini",
              "[modpack]\nid = p\nversion = 1\n[mods]\nblue = ^1.0\nred = *\n[priority]\nred = 10\n");
    // the pack's own copy of red replaces the global one
    makeMod(t.path / "pack" / "mods", "red", "1.1.0", "", {"data/art/menu.tga"});

    ModpackManifest pack;
    std::vector<std::string> errors;
    REQUIRE(ModpackManifest::load((t.path / "pack").string(), &pack, &errors)); // directory form
    ModRegistry reg;
    reg.scan((t.path / "mods").string());
    reg.scan(pack.local_mods_dir);
    LoadOrder o = resolveLoadOrder(reg, selectModpack(pack));
    REQUIRE(o.ok());
    const std::vector<std::string> expect = {"blue", "red"};
    CHECK(ids(o) == expect);
    CHECK(reg.find("red")->version.toString() == "1.1.0");

    Overlay ov;
    ov.build(o);
    const Overlay::Entry* e = ov.find("data/art/menu.tga");
    REQUIRE(e != nullptr);
    CHECK(e->mod_id == "red");
    CHECK(e->real_path.find("pack") != std::string::npos);
    CHECK(ov.find("data/x.txt") == nullptr); // unlisted mod is not active

    // a requirement the installed version does not meet
    writeFile(t.path / "pack2" / "modpack.ini", "[modpack]\nid = p2\nversion = 1\n[mods]\nblue = >=2\n");
    REQUIRE(ModpackManifest::load((t.path / "pack2" / "modpack.ini").string(), &pack, &errors));
    CHECK(contains(resolveLoadOrder(reg, selectModpack(pack)).errors, "1.2.0, but >=2 is required"));
}

// ------------------------------------------------------------------ overlay

TEST(normalize_path)
{
    CHECK(normalizePath("Data\\Art\\MCL_MM.fit") == "data/art/mcl_mm.fit");
    CHECK(normalizePath("./data//objects/./pilots.csv") == "data/objects/pilots.csv");
    CHECK(normalizePath("/data/x") == "data/x");
}

TEST(overlay_last_mod_wins_and_records_overrides)
{
    TempDir t;
    makeMod(t.path, "a", "1.0.0", "", {"data/objects/Pilots.csv", "data/a_only.txt"});
    makeMod(t.path, "b", "1.0.0", "", {"data/objects/pilots.csv"}, "priority = 1\n");
    ModRegistry reg;
    reg.scan(t.str());
    LoadOrder o = resolveLoadOrder(reg, selectEnabled(reg));
    Overlay ov;
    ov.build(o);
    CHECK_EQ(ov.size(), (size_t)2);
    const Overlay::Entry* e = ov.find("DATA\\OBJECTS\\PILOTS.CSV");
    REQUIRE(e != nullptr);
    CHECK(e->mod_id == "b");
    REQUIRE(e->overridden.size() == 1);
    CHECK(e->overridden[0] == "a");
    CHECK(readAll(e->real_path) == "b:data/objects/pilots.csv");
    CHECK(ov.find("data/a_only.txt")->mod_id == "a");
    CHECK(ov.find("data/missing.txt") == nullptr);

    const std::string report = describe(o, ov);
    CHECK(report.find("1. a 1.0.0") != std::string::npos);
    CHECK(report.find("data/objects/pilots.csv: b (over a)") != std::string::npos);
}

// --------------------------------------------------------------- scaffolding

TEST(create_and_validate_mod)
{
    TempDir t;
    std::string err;
    REQUIRE(createModSkeleton(t.str(), "my_mod", "My Mod", &err));
    CHECK(!createModSkeleton(t.str(), "my_mod", "", &err)); // exists
    CHECK(!createModSkeleton(t.str(), "Bad", "", &err));

    // fresh skeleton: valid manifest but nothing to load yet
    std::vector<std::string> problems = validateMod((t.path / "my_mod").string());
    CHECK(contains(problems, "content/ is empty"));

    writeFile(t.path / "my_mod" / "content" / "data" / "objects" / "pilots.csv", "x");
    CHECK(validateMod((t.path / "my_mod").string()).empty());

    // Only on case-sensitive file systems: on Windows and (by default) macOS
    // the second name opens the first file.
    writeFile(t.path / "my_mod" / "content" / "data" / "objects" / "Pilots.CSV", "y");
    {
        int n = 0;
        for (const auto& e : fs::directory_iterator(t.path / "my_mod" / "content" / "data" / "objects"))
            n += e.path().filename() == "pilots.csv" || e.path().filename() == "Pilots.CSV";
        if (n == 2)
            CHECK(contains(validateMod((t.path / "my_mod").string()), "differ only in case"));
    }

    ModManifest m;
    std::vector<std::string> errors;
    REQUIRE(ModManifest::load((t.path / "my_mod").string(), &m, &errors));
    CHECK(m.name == "My Mod");
    CHECK(m.version.toString() == "0.1.0");
}

TEST(create_modpack_file)
{
    TempDir t;
    const std::string path = (t.path / "modpack.ini").string();
    std::string err;
    REQUIRE(createModpack(path, "pack", "Pack", {"a", "b=^1.2"}, &err));
    ModpackManifest p;
    std::vector<std::string> errors;
    REQUIRE(ModpackManifest::load(path, &p, &errors));
    REQUIRE(p.mods.size() == 2);
    CHECK(p.mods[1].req.text() == "^1.2");
    CHECK(!createModpack(path, "pack", "", {"bad id"}, &err));
}

// ------------------------------------------------------------ game hook

TEST(game_hook_resolves_paths_while_active)
{
    TempDir t;
    makeMod(t.path, "m", "1.0.0", "", {"data/art/mcl_mm_1.tga"});
    std::string report;
    REQUIRE(gos_ModsInit(t.str().c_str(), nullptr, &report));
    CHECK(gos_ModsActive());
    const char* p = gos_ModResolvePath("data/art/MCL_MM_1.TGA");
    REQUIRE(p != nullptr);
    CHECK(readAll(p) == "m:data/art/mcl_mm_1.tga");
    CHECK(gos_ModResolvePath("data/art/other.tga") == nullptr);
    CHECK(report.find("1. m 1.0.0") != std::string::npos);
    gos_ModsShutdown();
    CHECK(!gos_ModsActive());
    CHECK(gos_ModResolvePath("data/art/mcl_mm_1.tga") == nullptr);
}

TEST(game_hook_refuses_broken_setups)
{
    TempDir t;
    makeMod(t.path, "m", "1.0.0", "[dependencies]\nghost = *\n", {"data/x"});
    std::string report;
    CHECK(!gos_ModsInit(t.str().c_str(), nullptr, &report));
    CHECK(!gos_ModsActive());
    CHECK(report.find("needs 'ghost'") != std::string::npos);
    CHECK(!gos_ModsInit(nullptr, (t.path / "no_such_pack.ini").string().c_str(), &report));
    CHECK(report.find("cannot read") != std::string::npos);
}
