// SPDX-License-Identifier: GPL-3.0-only
#include "gos_update.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <dlfcn.h>
#endif

namespace gos_update {

const char* const kDefaultUrl = "https://api.github.com/repos/hyouka0904/mc2/releases/latest";
const char* const kReleasesPage = "https://github.com/hyouka0904/mc2/releases/latest";

namespace {
const size_t kMaxBody = 1024 * 1024;
const char* const kUserAgent = "mc2-update-check";
} // namespace

//==============================================================================
// Versions

static bool parseNumber(const char*& p, int* out)
{
    if (!isdigit((unsigned char)*p))
        return false;
    long v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        if (v > 1000000)
            return false;
        ++p;
    }
    *out = (int)v;
    return true;
}

bool parseVersion(const std::string& text, Version* out)
{
    Version v;
    const char* p = text.c_str();
    if (*p == 'v' || *p == 'V')
        ++p;
    if (!parseNumber(p, &v.major) || *p++ != '.' || !parseNumber(p, &v.minor))
        return false;
    if (*p == '.') {
        ++p;
        if (!parseNumber(p, &v.patch))
            return false;
    }
    if (*p == '-') {
        ++p;
        const char* start = p;
        while (*p && *p != '+') {
            if (!isalnum((unsigned char)*p) && *p != '.' && *p != '-')
                return false;
            ++p;
        }
        if (p == start)
            return false;
        v.pre.assign(start, p);
    }
    if (*p == '+')
        p += strlen(p);
    if (*p)
        return false;
    if (out)
        *out = v;
    return true;
}

static bool allDigits(const std::string& s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (!isdigit((unsigned char)c))
            return false;
    return true;
}

// SemVer pre-release precedence: dot-separated fields, numbers compare
// numerically and sort before text.
static int comparePre(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty())
        return a.empty() ? (b.empty() ? 0 : 1) : -1;
    size_t i = 0, j = 0;
    while (i <= a.size() && j <= b.size()) {
        size_t ie = a.find('.', i), je = b.find('.', j);
        if (ie == std::string::npos) ie = a.size();
        if (je == std::string::npos) je = b.size();
        const std::string fa = a.substr(i, ie - i), fb = b.substr(j, je - j);
        const bool na = allDigits(fa), nb = allDigits(fb);
        int c;
        if (na && nb)
            c = fa.size() != fb.size() ? (fa.size() < fb.size() ? -1 : 1) : fa.compare(fb);
        else if (na != nb)
            c = na ? -1 : 1;
        else
            c = fa.compare(fb);
        if (c)
            return c < 0 ? -1 : 1;
        const bool enda = ie >= a.size(), endb = je >= b.size();
        if (enda || endb)
            return enda == endb ? 0 : (enda ? -1 : 1);
        i = ie + 1;
        j = je + 1;
    }
    return 0;
}

int compareVersions(const Version& a, const Version& b)
{
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return comparePre(a.pre, b.pre);
}

bool isDevelopmentBuild(const std::string& version)
{
    Version v;
    if (!parseVersion(version, &v))
        return true;
    if (v.major == 0 && v.minor == 0 && v.patch == 0)
        return true;
    // git describe: <tag>-<commits>-g<hash>[-dirty]
    std::string pre = v.pre;
    const std::string dirty = "-dirty";
    if (pre == "dirty")
        return true;
    if (pre.size() > dirty.size() && pre.compare(pre.size() - dirty.size(), dirty.size(), dirty) == 0)
        pre.resize(pre.size() - dirty.size());
    const size_t g = pre.rfind("-g");
    if (g != std::string::npos && g > 0) {
        const std::string count = pre.substr(0, g);
        const std::string hash = pre.substr(g + 2);
        bool hex = hash.size() >= 4;
        for (char c : hash)
            hex = hex && isxdigit((unsigned char)c);
        size_t lastDash = count.rfind('-');
        const std::string n = lastDash == std::string::npos ? count : count.substr(lastDash + 1);
        if (hex && allDigits(n))
            return true;
    }
    return false;
}

//==============================================================================
// Minimal JSON reader: only the top-level object's string and boolean
// members are kept; nested values are parsed and skipped.

namespace {

struct Json {
    const char* p;
    const char* end;
    int depth = 0;

    void ws()
    {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            ++p;
    }

    static void putUtf8(std::string& s, unsigned cp)
    {
        if (cp < 0x80) {
            s += (char)cp;
        } else if (cp < 0x800) {
            s += (char)(0xC0 | (cp >> 6));
            s += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += (char)(0xE0 | (cp >> 12));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        } else {
            s += (char)(0xF0 | (cp >> 18));
            s += (char)(0x80 | ((cp >> 12) & 0x3F));
            s += (char)(0x80 | ((cp >> 6) & 0x3F));
            s += (char)(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(unsigned* out)
    {
        if (end - p < 4)
            return false;
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = *p++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return false;
        }
        *out = v;
        return true;
    }

    bool string(std::string* out)
    {
        if (p >= end || *p != '"')
            return false;
        ++p;
        std::string s;
        while (p < end && *p != '"') {
            char c = *p++;
            if ((unsigned char)c < 0x20)
                return false;
            if (c != '\\') {
                s += c;
                continue;
            }
            if (p >= end)
                return false;
            c = *p++;
            switch (c) {
            case '"': s += '"'; break;
            case '\\': s += '\\'; break;
            case '/': s += '/'; break;
            case 'b': s += '\b'; break;
            case 'f': s += '\f'; break;
            case 'n': s += '\n'; break;
            case 'r': s += '\r'; break;
            case 't': s += '\t'; break;
            case 'u': {
                unsigned cp;
                if (!hex4(&cp))
                    return false;
                if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    p += 2;
                    unsigned lo;
                    if (!hex4(&lo) || lo < 0xDC00 || lo >= 0xE000)
                        return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                putUtf8(s, cp);
                break;
            }
            default:
                return false;
            }
        }
        if (p >= end)
            return false;
        ++p;
        if (out)
            *out = s;
        return true;
    }

    bool literal(const char* word)
    {
        const size_t n = strlen(word);
        if ((size_t)(end - p) < n || strncmp(p, word, n) != 0)
            return false;
        p += n;
        return true;
    }

    bool number()
    {
        const char* start = p;
        if (p < end && *p == '-')
            ++p;
        while (p < end && (isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-'))
            ++p;
        return p > start;
    }

    // kind: 's' string, 't'/'f' boolean, 'o' anything else
    bool value(char* kind, std::string* str)
    {
        ws();
        if (p >= end)
            return false;
        if (*p == '"') {
            *kind = 's';
            return string(str);
        }
        if (*p == '{' || *p == '[') {
            *kind = 'o';
            if (++depth > 64)
                return false;
            const char close = *p == '{' ? '}' : ']';
            const bool obj = *p == '{';
            ++p;
            ws();
            if (p < end && *p == close) {
                ++p;
                --depth;
                return true;
            }
            for (;;) {
                if (obj) {
                    ws();
                    if (!string(nullptr))
                        return false;
                    ws();
                    if (p >= end || *p++ != ':')
                        return false;
                }
                char k;
                if (!value(&k, nullptr))
                    return false;
                ws();
                if (p >= end)
                    return false;
                if (*p == ',') {
                    ++p;
                    continue;
                }
                if (*p++ != close)
                    return false;
                --depth;
                return true;
            }
        }
        if (literal("true")) { *kind = 't'; return true; }
        if (literal("false")) { *kind = 'f'; return true; }
        if (literal("null")) { *kind = 'o'; return true; }
        *kind = 'o';
        return number();
    }
};

} // namespace

bool parseLatestRelease(const std::string& json, Release* out)
{
    Json j{json.data(), json.data() + json.size()};
    std::map<std::string, std::string> strings;
    std::map<std::string, bool> bools;
    j.ws();
    if (j.p >= j.end || *j.p != '{')
        return false;
    ++j.p;
    j.ws();
    if (j.p < j.end && *j.p == '}') {
        ++j.p;
    } else {
        for (;;) {
            j.ws();
            std::string key;
            if (!j.string(&key))
                return false;
            j.ws();
            if (j.p >= j.end || *j.p++ != ':')
                return false;
            char kind;
            std::string str;
            if (!j.value(&kind, &str))
                return false;
            if (kind == 's')
                strings[key] = str;
            else if (kind == 't' || kind == 'f')
                bools[key] = kind == 't';
            j.ws();
            if (j.p >= j.end)
                return false;
            if (*j.p == ',') {
                ++j.p;
                continue;
            }
            if (*j.p++ != '}')
                return false;
            break;
        }
    }
    if (bools["draft"] || bools["prerelease"])
        return false;
    std::string tag = strings["tag_name"];
    Version v;
    if (!parseVersion(tag, &v))
        return false;
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
        tag.erase(0, 1);
    if (out) {
        out->version = tag;
        out->url = strings["html_url"];
        // Only ever open a GitHub page from the notice.
        if (out->url.compare(0, 19, "https://github.com/") != 0)
            out->url = kReleasesPage;
    }
    return true;
}

//==============================================================================
// update.ini: key=value lines

bool loadState(const std::string& path, State* out)
{
    State s;
    std::ifstream in(path.c_str());
    if (!in) {
        if (out)
            *out = s;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[' || eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        if (key == "check") s.checkEnabled = val != "0";
        else if (key == "last_check") s.lastCheck = strtoll(val.c_str(), nullptr, 10);
        else if (key == "latest_version") s.latestVersion = val;
        else if (key == "latest_url") s.latestUrl = val;
        else if (key == "snoozed_version") s.snoozedVersion = val;
        else if (key == "snoozed_until") s.snoozedUntil = strtoll(val.c_str(), nullptr, 10);
    }
    if (out)
        *out = s;
    return true;
}

bool saveState(const std::string& path, const State& s)
{
    const std::string tmp = path + ".tmp";
    {
        std::ofstream o(tmp.c_str(), std::ios::trunc);
        if (!o)
            return false;
        o << "[update]\n"
          << "; check=0 turns off the check for new versions\n"
          << "check=" << (s.checkEnabled ? 1 : 0) << "\n"
          << "last_check=" << (long long)s.lastCheck << "\n"
          << "latest_version=" << s.latestVersion << "\n"
          << "latest_url=" << s.latestUrl << "\n"
          << "snoozed_version=" << s.snoozedVersion << "\n"
          << "snoozed_until=" << (long long)s.snoozedUntil << "\n";
        if (!o)
            return false;
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        return false;
#else
    if (rename(tmp.c_str(), path.c_str()) != 0)
        return false;
#endif
    return true;
}

bool shouldNotify(const State& s, const std::string& current, int64_t now)
{
    Version cur, latest;
    if (!parseVersion(s.latestVersion, &latest))
        return false;
    // Unparsable versions only get here from development builds checking
    // against a test server (MC2_UPDATE_URL): treat them as 0.0.0.
    if (!parseVersion(current, &cur))
        cur = Version();
    if (compareVersions(latest, cur) <= 0)
        return false;
    if (s.snoozedVersion == s.latestVersion && now < s.snoozedUntil)
        return false;
    return true;
}

//==============================================================================
// HTTP

#ifdef _WIN32

static std::wstring widen(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

bool httpGet(const std::string& url, std::string* body, std::string* error, int timeoutSec)
{
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0};
    wchar_t path[2048] = {0};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        if (error) *error = "bad URL";
        return false;
    }
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    bool ok = false;
    HINTERNET session = WinHttpOpen(widen(kUserAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connect = nullptr, request = nullptr;
    if (session) {
        const int ms = timeoutSec * 1000;
        WinHttpSetTimeouts(session, ms, ms, ms, ms);
        connect = WinHttpConnect(session, host, uc.nPort, 0);
    }
    if (connect)
        request = WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (request) {
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
        if (WinHttpSendRequest(request, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            std::string data;
            DWORD avail = 0;
            bool readOk = true;
            while (WinHttpQueryDataAvailable(request, &avail) && avail > 0) {
                if (data.size() + avail > kMaxBody) {
                    readOk = false;
                    break;
                }
                const size_t old = data.size();
                data.resize(old + avail);
                DWORD got = 0;
                if (!WinHttpReadData(request, &data[old], avail, &got)) {
                    readOk = false;
                    break;
                }
                data.resize(old + got);
            }
            if (code == 200 && readOk) {
                if (body) *body = data;
                ok = true;
            } else if (error) {
                *error = readOk ? "HTTP " + std::to_string(code) : "read failed";
            }
        } else if (error) {
            *error = "request failed (" + std::to_string(GetLastError()) + ")";
        }
    } else if (error) {
        *error = "cannot open connection (" + std::to_string(GetLastError()) + ")";
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

#else

namespace {

// The few libcurl symbols and constants needed, so no headers are required.
typedef void CURL;
struct curl_slist;
enum {
    kCurlOptWriteData = 10001,
    kCurlOptUrl = 10002,
    kCurlOptUserAgent = 10018,
    kCurlOptHttpHeader = 10023,
    kCurlOptWriteFunction = 20011,
    kCurlOptTimeout = 13,
    kCurlOptFollowLocation = 52,
    kCurlOptConnectTimeout = 78,
    kCurlOptNoSignal = 99,
    kCurlOptMaxRedirs = 68,
    kCurlInfoResponseCode = 0x200002,
};

struct CurlApi {
    bool loaded = false;
    CURL* (*easy_init)();
    int (*easy_setopt)(CURL*, int, ...);
    int (*easy_perform)(CURL*);
    int (*easy_getinfo)(CURL*, int, ...);
    void (*easy_cleanup)(CURL*);
    const char* (*easy_strerror)(int);
    curl_slist* (*slist_append)(curl_slist*, const char*);
    void (*slist_free_all)(curl_slist*);
    int (*global_init)(long);
};

CurlApi* curlApi()
{
    static CurlApi api;
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef __APPLE__
        const char* names[] = {"libcurl.4.dylib", "/usr/lib/libcurl.4.dylib", "libcurl.dylib"};
#else
        const char* names[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"};
#endif
        void* lib = nullptr;
        for (const char* n : names)
            if ((lib = dlopen(n, RTLD_NOW | RTLD_LOCAL)))
                break;
        if (!lib)
            return;
#define MC2_CURL_SYM(field, name) \
        *(void**)&api.field = dlsym(lib, name); \
        if (!api.field) return;
        MC2_CURL_SYM(easy_init, "curl_easy_init")
        MC2_CURL_SYM(easy_setopt, "curl_easy_setopt")
        MC2_CURL_SYM(easy_perform, "curl_easy_perform")
        MC2_CURL_SYM(easy_getinfo, "curl_easy_getinfo")
        MC2_CURL_SYM(easy_cleanup, "curl_easy_cleanup")
        MC2_CURL_SYM(easy_strerror, "curl_easy_strerror")
        MC2_CURL_SYM(slist_append, "curl_slist_append")
        MC2_CURL_SYM(slist_free_all, "curl_slist_free_all")
        MC2_CURL_SYM(global_init, "curl_global_init")
#undef MC2_CURL_SYM
        api.global_init(3 /* CURL_GLOBAL_ALL */);
        api.loaded = true;
    });
    return &api;
}

struct Sink {
    std::string data;
    bool overflow = false;
};

size_t writeBody(char* ptr, size_t size, size_t n, void* user)
{
    Sink* s = (Sink*)user;
    const size_t bytes = size * n;
    if (s->data.size() + bytes > kMaxBody) {
        s->overflow = true;
        return 0;
    }
    s->data.append(ptr, bytes);
    return bytes;
}

} // namespace

bool httpGet(const std::string& url, std::string* body, std::string* error, int timeoutSec)
{
    CurlApi* api = curlApi();
    if (!api->loaded) {
        if (error) *error = "libcurl not found";
        return false;
    }
    CURL* c = api->easy_init();
    if (!c) {
        if (error) *error = "curl_easy_init failed";
        return false;
    }
    Sink sink;
    curl_slist* headers = api->slist_append(nullptr, "Accept: application/vnd.github+json");
    api->easy_setopt(c, kCurlOptUrl, url.c_str());
    api->easy_setopt(c, kCurlOptUserAgent, kUserAgent);
    api->easy_setopt(c, kCurlOptHttpHeader, headers);
    api->easy_setopt(c, kCurlOptWriteFunction, &writeBody);
    api->easy_setopt(c, kCurlOptWriteData, (void*)&sink);
    api->easy_setopt(c, kCurlOptFollowLocation, 1L);
    api->easy_setopt(c, kCurlOptMaxRedirs, 5L);
    api->easy_setopt(c, kCurlOptTimeout, (long)timeoutSec);
    api->easy_setopt(c, kCurlOptConnectTimeout, (long)timeoutSec);
    api->easy_setopt(c, kCurlOptNoSignal, 1L);
    const int rc = api->easy_perform(c);
    long code = 0;
    api->easy_getinfo(c, kCurlInfoResponseCode, &code);
    api->easy_cleanup(c);
    api->slist_free_all(headers);
    if (rc != 0) {
        if (error) *error = sink.overflow ? "response too large" : api->easy_strerror(rc);
        return false;
    }
    if (code != 200) {
        if (error) *error = "HTTP " + std::to_string(code);
        return false;
    }
    if (body)
        *body = sink.data;
    return true;
}

#endif

//==============================================================================
// Background check

namespace {

struct Checker {
    std::mutex m;
    Status status = Status::Idle;
    Release release;
    std::string statePath;
    std::condition_variable done;
    bool running = false; // the detached thread has not finished yet
    bool started = false;
};

// Never freed: the detached thread may still be inside it while the game
// exits.
Checker& checker()
{
    static Checker* c = new Checker;
    return *c;
}

void setResult(Status s, const Release& r = Release())
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    c.status = s;
    c.release = r;
}

void finished()
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    c.running = false;
    c.done.notify_all();
}

void runCheckBody(const Config& cfg);

void runCheck(Config cfg)
{
    runCheckBody(cfg);
    finished();
}

void runCheckBody(const Config& cfg)
{
    State state;
    loadState(cfg.statePath, &state);
    if (!state.checkEnabled) {
        setResult(Status::Disabled);
        return;
    }
    const bool fresh = !cfg.force && state.lastCheck > 0 && cfg.now >= state.lastCheck &&
                       cfg.now - state.lastCheck < kCheckInterval && !state.latestVersion.empty();
    if (!fresh) {
        std::string body, error;
        Release r;
        if (!httpGet(cfg.url, &body, &error, 5) || !parseLatestRelease(body, &r)) {
            if (error.empty())
                error = "unexpected response";
            fprintf(stderr, "update check: %s\n", error.c_str());
            setResult(Status::CheckFailed);
            return;
        }
        state.lastCheck = cfg.now;
        state.latestVersion = r.version;
        state.latestUrl = r.url;
        if (!saveState(cfg.statePath, state))
            fprintf(stderr, "update check: cannot write %s\n", cfg.statePath.c_str());
    }
    if (shouldNotify(state, cfg.currentVersion, cfg.now)) {
        Release r;
        r.version = state.latestVersion;
        r.url = state.latestUrl.empty() ? kReleasesPage : state.latestUrl;
        fprintf(stderr, "update check: version %s is available (running %s)\n",
                r.version.c_str(), cfg.currentVersion.c_str());
        setResult(Status::Available, r);
    } else {
        setResult(Status::UpToDate);
    }
}

} // namespace

bool disabledByEnvironment()
{
    const char* e = getenv("MC2_NO_UPDATE_CHECK");
    return e && *e && strcmp(e, "0") != 0;
}

void startCheck(const Config& config)
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    if (c.started)
        return;
    c.started = true;
    Config cfg = config;
    if (cfg.url.empty()) {
        const char* env = getenv("MC2_UPDATE_URL");
        cfg.url = env && *env ? env : kDefaultUrl;
    }
    if (cfg.now == 0)
        cfg.now = (int64_t)time(nullptr);
    c.statePath = cfg.statePath;
    // With a test server (MC2_UPDATE_URL) development builds check too.
    const char* testUrl = getenv("MC2_UPDATE_URL");
    const bool testing = config.url.empty() && testUrl && *testUrl;
    if (disabledByEnvironment() || (!testing && isDevelopmentBuild(cfg.currentVersion))) {
        c.status = Status::Disabled;
        return;
    }
    c.status = Status::Checking;
    c.running = true;
    std::thread(runCheck, cfg).detach();
}

Status status()
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    return c.status;
}

Release available()
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    return c.release;
}

void snooze()
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    if (c.status != Status::Available)
        return;
    State state;
    loadState(c.statePath, &state);
    state.snoozedVersion = c.release.version;
    state.snoozedUntil = (int64_t)time(nullptr) + kSnoozeTime;
    saveState(c.statePath, state);
    c.status = Status::UpToDate;
}

void waitForCheck()
{
    Checker& c = checker();
    std::unique_lock<std::mutex> lock(c.m);
    c.done.wait(lock, [&] { return !c.running; });
}

void resetForTests()
{
    waitForCheck();
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    c.status = Status::Idle;
    c.release = Release();
    c.started = false;
}

} // namespace gos_update
