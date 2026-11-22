// SPDX-License-Identifier: GPL-3.0-only
// Tests for the cross-platform string helpers and Win32 shims in GameOS/src.

#include "mc2test.h"

#include "platform_str.h"
#include "platform_windows.h"

#include <cstring>
#include <string>

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

TEST(char_upper_lower_ascii)
{
    char s[] = "Atlas";
    CharUpperA(s);
    CHECK_EQ(std::string(s), std::string("ATLAS"));
    CharLowerA(s);
    CHECK_EQ(std::string(s), std::string("atlas"));
}
#endif
