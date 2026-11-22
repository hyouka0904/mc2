// SPDX-License-Identifier: GPL-3.0-only
// Win32 file, time and virtual memory functions for platforms other than
// Windows, built on the C++ standard library (std::filesystem, <ctime>) and
// POSIX mmap. The same code runs on Linux and macOS.

#ifndef PLATFORM_WINDOWS

#include <sys/mman.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include <strings.h> // strcasecmp

#include "platform_windows.h"

namespace fs = std::filesystem;

static int gGetLastError = 0;

static void set_last_error(const std::error_code& ec)
{
    if (ec == std::errc::file_exists)
        gGetLastError = ERROR_ALREADY_EXISTS;
    else if (ec == std::errc::no_such_file_or_directory)
        gGetLastError = ERROR_PATH_NOT_FOUND;
    else
        gGetLastError = ec.value();
}

static void fill_system_time(const struct tm& t, LPSYSTEMTIME st)
{
    st->wYear = (WORD)(1900 + t.tm_year);
    st->wMonth = (WORD)(1 + t.tm_mon);
    st->wDayOfWeek = (WORD)t.tm_wday;
    st->wDay = (WORD)t.tm_mday;
    st->wHour = (WORD)t.tm_hour;
    st->wMinute = (WORD)t.tm_min;
    st->wSecond = (WORD)t.tm_sec;
    st->wMilliseconds = 0;
}

DWORD GetTimeZoneInformation(LPTIME_ZONE_INFORMATION tzi)
{
    memset(tzi, 0, sizeof(*tzi));
    const time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);

    // Win32: UTC = local time + Bias (minutes)
    tzi->Bias = (LONG)(-local.tm_gmtoff / 60);
    if (local.tm_zone)
        mbstowcs(tzi->StandardName, local.tm_zone, 31);
    GetSystemTime(&tzi->StandardDate);
    return local.tm_isdst > 0 ? TIME_ZONE_ID_DAYLIGHT : TIME_ZONE_ID_STANDARD;
}

VOID WINAPI GetSystemTime(LPSYSTEMTIME st)
{
    const time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    fill_system_time(utc, st);
}

BOOL CreateDirectory(LPCSTR dir, LPSECURITY_ATTRIBUTES)
{
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        gGetLastError = ERROR_ALREADY_EXISTS;
        return FALSE;
    }
    if (!fs::create_directory(dir, ec)) {
        set_last_error(ec ? ec : std::make_error_code(std::errc::io_error));
        return FALSE;
    }
    return TRUE;
}

BOOL SetFileAttributes(LPCSTR, DWORD)
{
    // Only used to clear the read-only bit before overwriting a file, which
    // POSIX permissions do not need.
    return TRUE;
}

BOOL DeleteFile(LPCSTR file)
{
    std::error_code ec;
    if (!fs::is_regular_file(file, ec) || !fs::remove(file, ec)) {
        set_last_error(ec ? ec : std::make_error_code(std::errc::no_such_file_or_directory));
        return FALSE;
    }
    return TRUE;
}

BOOL CopyFile(LPCTSTR from, LPCTSTR to, BOOL bFailIfExists)
{
    std::error_code ec;
    const auto opt = bFailIfExists ? fs::copy_options::none : fs::copy_options::overwrite_existing;
    if (!fs::copy_file(from, to, opt, ec)) {
        set_last_error(ec ? ec : std::make_error_code(std::errc::file_exists));
        return FALSE;
    }
    // Like Win32, the copy keeps the source's modification time.
    const auto t = fs::last_write_time(from, ec);
    if (!ec)
        fs::last_write_time(to, t, ec);
    return TRUE;
}

int lstrlen(LPCSTR str)
{
    return (int)strlen(str);
}

int lstrlenA(LPCSTR str)
{
    return lstrlen(str);
}

int lstrcmpiA(LPCSTR str1, LPCSTR str2)
{
    return strcasecmp(str1, str2);
}

int lstrcmpiW(LPCWSTR str1, LPCWSTR str2)
{
    for (;; ++str1, ++str2) {
        const wint_t a = towlower(*str1), b = towlower(*str2);
        if (a != b)
            return a < b ? -1 : 1;
        if (!a)
            return 0;
    }
}

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

static int page_protection(DWORD flProtect)
{
    switch (flProtect) {
        case PAGE_READONLY: return PROT_READ;
        case PAGE_READWRITE: return PROT_READ | PROT_WRITE;
        default: return PROT_NONE;
    }
}

WINBASEAPI PVOID WINAPI VirtualAlloc(PVOID lpAddress, DWORD dwSize, DWORD flAllocationType, DWORD flProtect)
{
    // MEM_RESERVE: address space only (inaccessible); MEM_COMMIT: make a
    // reserved range (or, with no address, a new one) accessible.
    if (flAllocationType != MEM_RESERVE && flAllocationType != MEM_COMMIT) {
        assert(0 && "VirtualAlloc: unsupported allocation type");
        return NULL;
    }
    if (flAllocationType == MEM_RESERVE || lpAddress == NULL) {
        void* p = mmap(NULL, dwSize, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            gGetLastError = errno;
            return NULL;
        }
        if (flAllocationType == MEM_RESERVE)
            return p;
        lpAddress = p;
    }
    if (mprotect(lpAddress, dwSize, page_protection(flProtect)) == -1) {
        gGetLastError = errno;
        return NULL;
    }
    return lpAddress;
}

WINBASEAPI BOOL WINAPI VirtualFree(PVOID lpAddress, DWORD dwSize, DWORD dwFreeType)
{
    int rv;
    if (dwFreeType == MEM_RELEASE)
        rv = munmap(lpAddress, dwSize);
    else if (dwFreeType == MEM_DECOMMIT)
        rv = mprotect(lpAddress, dwSize, PROT_NONE);
    else {
        assert(0 && "VirtualFree: unsupported dwFreeType");
        return FALSE;
    }
    if (rv == -1) {
        gGetLastError = errno;
        return FALSE;
    }
    return TRUE;
}

WINBASEAPI DWORD WINAPI GetLastError()
{
    return gGetLastError;
}

// FindFirstFile / FindNextFile: the pattern's last component may contain the
// wildcards '*' and '?' (case-sensitive, like the file system); the
// directory part may not. Entries are returned in byte order of their names.

static bool wildcard_match(const char* pat, const char* s)
{
    const char* star = NULL;
    const char* resume = NULL;
    while (*s) {
        if (*pat == '?' || *pat == *s) {
            ++pat;
            ++s;
        } else if (*pat == '*') {
            star = pat++;
            resume = s;
        } else if (star) {
            pat = star + 1;
            s = ++resume;
        } else {
            return false;
        }
    }
    while (*pat == '*')
        ++pat;
    return *pat == '\0';
}

namespace {
struct FindState {
    std::vector<std::string> names;
    std::vector<bool> is_dir;
    size_t next = 0;
};
}

static void fill_find_data(const FindState& st, size_t i, LPWIN32_FIND_DATAA data)
{
    memset(data, 0, sizeof(*data));
    data->dwFileAttributes = st.is_dir[i] ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    strncpy(data->cFileName, st.names[i].c_str(), MAX_PATH - 1);
}

HANDLE WINAPI FindFirstFileA(LPCTSTR lpFileName, LPWIN32_FIND_DATAA lpFindFileData)
{
    const std::string spec(lpFileName);
    const size_t slash = spec.find_last_of('/');
    const std::string dir = slash == std::string::npos ? "." : (slash == 0 ? "/" : spec.substr(0, slash));
    const std::string pattern = slash == std::string::npos ? spec : spec.substr(slash + 1);

    std::vector<std::pair<std::string, bool>> found;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (wildcard_match(pattern.c_str(), name.c_str())) {
            std::error_code ec2;
            found.emplace_back(name, it->is_directory(ec2));
        }
    }
    if (found.empty()) {
        gGetLastError = ERROR_NO_MORE_FILES;
        return INVALID_HANDLE_VALUE;
    }
    std::sort(found.begin(), found.end());

    FindState* st = new FindState();
    for (auto& f : found) {
        st->names.push_back(f.first);
        st->is_dir.push_back(f.second);
    }
    fill_find_data(*st, st->next++, lpFindFileData);
    return st;
}

BOOL WINAPI FindNextFileA(HANDLE hFindFile, LPWIN32_FIND_DATAA lpFindFileData)
{
    assert(hFindFile && hFindFile != INVALID_HANDLE_VALUE);
    FindState* st = (FindState*)hFindFile;
    if (st->next >= st->names.size()) {
        gGetLastError = ERROR_NO_MORE_FILES;
        return FALSE;
    }
    fill_find_data(*st, st->next++, lpFindFileData);
    return TRUE;
}

BOOL WINAPI FindClose(HANDLE hFindFile)
{
    if (hFindFile && hFindFile != INVALID_HANDLE_VALUE)
        delete (FindState*)hFindFile;
    return TRUE;
}

#endif // PLATFORM_WINDOWS
