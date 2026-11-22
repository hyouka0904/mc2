// SPDX-License-Identifier: GPL-3.0-only
// Tests for the user data directory and the migration from ~/.mechcommander2
// (GameOS/gameos/utils/user_dir.cpp).

#include "mc2test.h"

#include "utils/user_dir.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag)
    {
        path = fs::temp_directory_path() / (std::string("mc2_userdir_") + tag);
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { fs::remove_all(path); }
};

void write_file(const fs::path& p, const char* text)
{
    fs::create_directories(p.parent_path());
    FILE* f = fopen(p.string().c_str(), "wb");
    fputs(text, f);
    fclose(f);
}

std::string read_file(const fs::path& p)
{
    std::string s;
    if (FILE* f = fopen(p.string().c_str(), "rb")) {
        int c;
        while ((c = fgetc(f)) != EOF)
            s += (char)c;
        fclose(f);
    }
    return s;
}
}

TEST(creates_missing_directory)
{
    TempDir t("create");
    const fs::path dir = t.path / "a" / "MechCommander2";
    std::string report;
    CHECK(user_dir::prepare(dir.string(), {}, &report));
    CHECK(fs::is_directory(dir));
    CHECK(report.empty());
}

TEST(copies_legacy_data_into_empty_directory)
{
    TempDir t("copy");
    const fs::path old_dir = t.path / ".mechcommander2";
    write_file(old_dir / ".registry", "reg");
    write_file(old_dir / "savegame" / "slot1.fit", "save");
    const fs::path dir = t.path / "new";

    std::string report;
    CHECK(user_dir::prepare(dir.string(), {(t.path / "missing").string(), old_dir.string()}, &report));
    CHECK_EQ(read_file(dir / ".registry"), std::string("reg"));
    CHECK_EQ(read_file(dir / "savegame" / "slot1.fit"), std::string("save"));
    CHECK(!report.empty());
    // The old directory stays, with a note pointing to the new one
    CHECK(fs::exists(old_dir / "savegame" / "slot1.fit"));
    CHECK(read_file(old_dir / user_dir::kMovedNoteName).find(dir.string()) != std::string::npos);
    CHECK(!fs::exists(dir / user_dir::kMovedNoteName));
}

TEST(never_copies_into_directory_in_use)
{
    TempDir t("inuse");
    const fs::path old_dir = t.path / ".mechcommander2";
    write_file(old_dir / ".registry", "old");
    const fs::path dir = t.path / "new";
    write_file(dir / ".registry", "new");

    std::string report;
    CHECK(user_dir::prepare(dir.string(), {old_dir.string()}, &report));
    CHECK_EQ(read_file(dir / ".registry"), std::string("new"));
    CHECK(report.empty());
    CHECK(!fs::exists(old_dir / user_dir::kMovedNoteName));
}

TEST(migration_happens_once)
{
    TempDir t("once");
    const fs::path old_dir = t.path / ".mechcommander2";
    write_file(old_dir / ".registry", "v1");
    const fs::path dir = t.path / "new";
    CHECK(user_dir::prepare(dir.string(), {old_dir.string()}, NULL));
    write_file(dir / ".registry", "v2"); // the game writes new settings
    std::string report;
    CHECK(user_dir::prepare(dir.string(), {old_dir.string()}, &report));
    CHECK_EQ(read_file(dir / ".registry"), std::string("v2"));
    CHECK(report.empty());
}

TEST(empty_legacy_directory_is_skipped)
{
    TempDir t("emptylegacy");
    const fs::path empty_old = t.path / "a" / ".mechcommander2";
    const fs::path old_dir = t.path / "b" / ".mechcommander2";
    fs::create_directories(empty_old);
    write_file(old_dir / ".registry", "b");
    const fs::path dir = t.path / "new";
    CHECK(user_dir::prepare(dir.string(), {empty_old.string(), old_dir.string()}, NULL));
    CHECK_EQ(read_file(dir / ".registry"), std::string("b"));
}
