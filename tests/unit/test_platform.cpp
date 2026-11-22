// SPDX-License-Identifier: GPL-3.0-only
// Tests for the cross-platform string helpers and Win32 shims in GameOS/src.

#include "mc2test.h"

#include "platform_str.h"
#include "platform_windows.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

TEST(strcmp_returns_sign_of_lexical_order)
{
    CHECK_EQ(S_strcmp("abc", "abc"), 0);
    CHECK_EQ(S_strcmp("abc", "abd"), -1);
    CHECK_EQ(S_strcmp("abd", "abc"), 1);
    CHECK_EQ(S_strcmp("ab", "abc"), -1);
    CHECK_EQ(S_strcmp("abc", "ab"), 1);
    CHECK_EQ(S_strcmp("", ""), 0);
    CHECK_EQ(S_strcmp("", "a"), -1);
}

TEST(strcmp_compares_high_bytes_as_unsigned)
{
    // 0xE9 ('é' in Latin-1) sorts after ASCII letters, like libc strcmp.
    CHECK_EQ(S_strcmp("\xE9", "z"), 1);
    CHECK_EQ(S_strcmp("z", "\xE9"), -1);
}

TEST(strncmp_stops_after_max_count)
{
    CHECK_EQ(S_strncmp("abcX", "abcY", 3), 0);
    CHECK_EQ(S_strncmp("abcX", "abcY", 4), -1);
    CHECK_EQ(S_strncmp("abcY", "abcX", 4), 1);
    CHECK_EQ(S_strncmp("a", "b", 0), 0);
}

TEST(stricmp_ignores_case)
{
    CHECK_EQ(S_stricmp("MiSsIoN", "mission"), 0);
    CHECK(S_stricmp("alpha", "BETA") < 0);
    CHECK(S_strnicmp("DATA/textures", "data/TEXTURES/x", 13) == 0);
}

TEST(strupr_and_strlwr_convert_in_place)
{
    char a[] = "Mech Commander 2";
    CHECK(S_strupr(a) == a);
    CHECK_EQ(std::string(a), std::string("MECH COMMANDER 2"));

    char b[] = "DATA\\Missions\\MC2_01.FIT";
    S_strlwr(b);
    CHECK_EQ(std::string(b), std::string("data\\missions\\mc2_01.fit"));
}

TEST(strlwr_path_keeps_absolute_paths)
{
    char rel[] = "Data/Missions/MC2_01.FIT";
    S_strlwr_path(rel);
    CHECK(std::string(rel) == "data/missions/mc2_01.fit");
    char unix_abs[] = "/home/Me/.local/share/MechCommander2/savegame/TestGame.ims";
    S_strlwr_path(unix_abs);
    CHECK(std::string(unix_abs) == "/home/Me/.local/share/MechCommander2/savegame/TestGame.ims");
    char win_abs[] = "C:\\Users\\Me\\AppData\\Roaming\\MechCommander2\\options.cfg";
    S_strlwr_path(win_abs);
    CHECK(std::string(win_abs) == "C:\\Users\\Me\\AppData\\Roaming\\MechCommander2\\options.cfg");
    CHECK(S_is_absolute_path("d:/x"));
    CHECK(!S_is_absolute_path("data\\x"));
    CHECK(!S_is_absolute_path(""));
}

TEST(strlwr_leaves_high_bytes_alone)
{
    char s[] = "A\xC4Z";
    S_strlwr(s);
    CHECK_EQ((unsigned char)s[0], (unsigned char)'a');
    CHECK_EQ((unsigned char)s[1], (unsigned char)0xC4);
    CHECK_EQ((unsigned char)s[2], (unsigned char)'z');
}

TEST(snprintf_truncates_and_reports_full_length)
{
    char buf[8];
    const int n = S_snprintf(buf, sizeof(buf), "%s-%d", "mission", 42);
    CHECK_EQ(n, 10); // length of the untruncated "mission-42"
    CHECK_EQ(std::string(buf), std::string("mission"));
    CHECK_EQ(buf[sizeof(buf) - 1], '\0');
}

#ifndef PLATFORM_WINDOWS
TEST(messagebox_never_answers_retry)
{
    // The missing-file prompts loop until IDCANCEL; answering anything else
    // with no user present would hang the game forever.
    CHECK_EQ(MessageBoxA(NULL, "file missing", "CD", MB_OKCANCEL | MB_ICONWARNING), IDCANCEL);
    CHECK_EQ(MessageBoxA(NULL, "retry?", "CD", MB_RETRYCANCEL), IDCANCEL);
    CHECK_EQ(MessageBoxA(NULL, "save?", "Q", MB_YESNOCANCEL), IDCANCEL);
    CHECK_EQ(MessageBoxA(NULL, "sure?", "Q", MB_YESNO), IDNO);
    CHECK_EQ(MessageBoxA(NULL, "notice", "Info", MB_OK), IDOK);
    CHECK_EQ(MessageBoxA(NULL, NULL, NULL, MB_OK), IDOK); // null strings are tolerated
}

namespace {
// Fresh empty directory under the system temp dir, removed on destruction.
struct TempDir {
    std::string path;
    TempDir()
    {
        namespace fs = std::filesystem;
        path = (fs::temp_directory_path() / ("mc2_platform_" + std::to_string((unsigned long)(uintptr_t)this))).string();
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { std::filesystem::remove_all(path); }
    std::string file(const char* name) const { return path + "/" + name; }
};

void write_file(const std::string& path, const char* text)
{
    FILE* f = fopen(path.c_str(), "wb");
    fputs(text, f);
    fclose(f);
}

std::string read_file(const std::string& path)
{
    std::string s;
    if (FILE* f = fopen(path.c_str(), "rb")) {
        char buf[256];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            s.append(buf, n);
        fclose(f);
    }
    return s;
}

std::vector<std::string> find_all(const std::string& pattern, std::vector<bool>* dirs = NULL)
{
    std::vector<std::string> names;
    WIN32_FIND_DATA data;
    HANDLE h = FindFirstFile(pattern.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE)
        return names;
    do {
        names.push_back(data.cFileName);
        if (dirs)
            dirs->push_back((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    } while (FindNextFile(h, &data));
    FindClose(h);
    return names;
}
}

TEST(find_file_matches_wildcards_in_name_order)
{
    TempDir d;
    write_file(d.file("b.fit"), "b");
    write_file(d.file("a.fit"), "a");
    write_file(d.file("a.fitx"), "x");
    write_file(d.file("c.txt"), "c");
    std::filesystem::create_directory(d.file("sub.fit"));

    std::vector<bool> dirs;
    const std::vector<std::string> fit = find_all(d.file("*.fit"), &dirs);
    CHECK_EQ(fit.size(), (size_t)3);
    CHECK_EQ(fit[0], std::string("a.fit"));
    CHECK_EQ(fit[1], std::string("b.fit"));
    CHECK_EQ(fit[2], std::string("sub.fit"));
    CHECK(!dirs[0] && !dirs[1] && dirs[2]);

    CHECK_EQ(find_all(d.file("?.txt")).size(), (size_t)1);
    CHECK_EQ(find_all(d.file("*")).size(), (size_t)5); // no "." or ".."
    CHECK_EQ(find_all(d.file("a.fit")).size(), (size_t)1);
}

TEST(find_file_reports_no_match)
{
    TempDir d;
    WIN32_FIND_DATA data;
    CHECK(FindFirstFile(d.file("*.none").c_str(), &data) == INVALID_HANDLE_VALUE);
    CHECK(FindFirstFile(d.file("missing_dir/*").c_str(), &data) == INVALID_HANDLE_VALUE);
    CHECK(FindClose(INVALID_HANDLE_VALUE));
}

TEST(create_copy_delete_files)
{
    TempDir d;
    const std::string sub = d.file("savegame");
    CHECK(CreateDirectory(sub.c_str(), NULL));
    CHECK(!CreateDirectory(sub.c_str(), NULL));
    CHECK_EQ(GetLastError(), (DWORD)ERROR_ALREADY_EXISTS);

    const std::string a = sub + "/a.fit", b = sub + "/b.fit";
    write_file(a, "first");
    CHECK(CopyFile(a.c_str(), b.c_str(), TRUE));
    CHECK_EQ(read_file(b), std::string("first"));

    write_file(a, "second");
    CHECK(!CopyFile(a.c_str(), b.c_str(), TRUE)); // fail if exists
    CHECK_EQ(read_file(b), std::string("first"));
    CHECK(CopyFile(a.c_str(), b.c_str(), FALSE)); // overwrite
    CHECK_EQ(read_file(b), std::string("second"));

    CHECK(DeleteFile(b.c_str()));
    CHECK(!DeleteFile(b.c_str()));
    CHECK(!DeleteFile(sub.c_str())); // not a file
}

TEST(wide_conversions_follow_win32_lengths)
{
    char narrow[16];
    // -1: null-terminated source, terminator counted
    CHECK_EQ(WideCharToMultiByte(CP_ACP, 0, L"mech", -1, narrow, sizeof(narrow), NULL, NULL), 5);
    CHECK_EQ(std::string(narrow), std::string("mech"));
    // explicit length: no terminator written or counted
    CHECK_EQ(WideCharToMultiByte(CP_ACP, 0, L"mech", 2, narrow, sizeof(narrow), NULL, NULL), 2);
    // no buffer: size needed
    CHECK_EQ(WideCharToMultiByte(CP_ACP, 0, L"mech", -1, NULL, 0, NULL, NULL), 5);
    // too small: fails, but leaves a terminated string
    CHECK_EQ(WideCharToMultiByte(CP_ACP, 0, L"commander", -1, narrow, 4, NULL, NULL), 0);
    CHECK_EQ(std::string(narrow), std::string("com"));

    wchar_t wide[16];
    CHECK_EQ(MultiByteToWideChar(CP_ACP, 0, "atlas", -1, wide, 16), 6);
    CHECK(std::wstring(wide) == L"atlas");
    CHECK_EQ(MultiByteToWideChar(CP_ACP, 0, "atlas", -1, NULL, 0), 6);
    CHECK_EQ(MultiByteToWideChar(CP_ACP, 0, "atlas", 3, wide, 16), 3);
    CHECK_EQ(MultiByteToWideChar(CP_ACP, 0, "commander", -1, wide, 4), 0);
    CHECK(std::wstring(wide) == L"com");
}

TEST(string_type_classifies_every_character)
{
    WORD types[4] = {0xffff, 0xffff, 0xffff, 0xffff};
    GetStringTypeEx(LOCALE_SYSTEM_DEFAULT, CT_CTYPE1, "a1 ,", 4, types);
    CHECK((types[0] & C1_ALPHA) && (types[0] & C1_LOWER));
    CHECK((types[1] & C1_DIGIT) && !(types[1] & C1_ALPHA));
    CHECK_EQ(types[2], (WORD)C1_SPACE);
    CHECK_EQ(types[3], (WORD)C1_PUNCT);
}

static int g_seen_type = -1;
static int answer_yes(const char*, const char*, UINT type)
{
    g_seen_type = (int)type;
    return IDYES;
}

TEST(messagebox_handler_can_be_replaced)
{
    platform_set_message_box_handler(answer_yes);
    CHECK_EQ(MessageBoxA(NULL, "sure?", "Q", MB_YESNO), IDYES);
    CHECK_EQ(g_seen_type, (int)MB_YESNO);
    platform_set_message_box_handler(NULL); // back to the automatic answer
    CHECK_EQ(MessageBoxA(NULL, "sure?", "Q", MB_YESNO), IDNO);
}

TEST(char_upper_lower_ascii)
{
    char s[] = "Atlas";
    CharUpperA(s);
    CHECK_EQ(std::string(s), std::string("ATLAS"));
    CharLowerA(s);
    CHECK_EQ(std::string(s), std::string("atlas"));
}
#endif
