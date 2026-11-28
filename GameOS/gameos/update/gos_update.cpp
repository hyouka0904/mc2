// SPDX-License-Identifier: GPL-3.0-only
#include "gos_update.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <map>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

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
const char* const kListUrl = "https://api.github.com/repos/hyouka0904/mc2/releases?per_page=20";
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

namespace {
// Divisor and decimals for the unit that fits bytes.
const char* sizeUnit(int64_t bytes, double* div, int* decimals)
{
    const double mb = 1024.0 * 1024.0;
    if (bytes < 1024 * 1024) {
        *div = 1024.0;
        *decimals = 0;
        return "KB";
    }
    if (bytes < (int64_t)1000 * 1024 * 1024) {
        *div = mb;
        *decimals = 1;
        return "MB";
    }
    *div = mb * 1024.0;
    *decimals = 2;
    return "GB";
}
} // namespace

std::string formatSize(int64_t bytes)
{
    if (bytes < 0)
        bytes = 0;
    double div;
    int dec;
    const char* unit = sizeUnit(bytes, &div, &dec);
    double v = bytes / div;
    if (bytes > 0 && dec == 0 && v < 1.0)
        v = 1.0; // never "0 KB" for something that is there
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f %s", dec, v, unit);
    return buf;
}

std::string formatProgress(int64_t received, int64_t total)
{
    if (total < 0)
        total = 0;
    if (received < 0)
        received = 0;
    if (received > total)
        received = total;
    double div;
    int dec;
    const char* unit = sizeUnit(total, &div, &dec);
    char buf[96];
    snprintf(buf, sizeof(buf), "%.*f / %.*f %s", dec, received / div, dec, total / div, unit);
    return buf;
}

//==============================================================================
// Minimal JSON reader: builds a small tree (numbers are kept as text).

namespace {

struct JNode {
    char kind = 'n'; // 's' string, 'b' boolean, 'd' number, 'o' object, 'a' array, 'n' null
    std::string str; // string, or number text
    bool b = false;
    std::vector<std::pair<std::string, JNode>> members;
    std::vector<JNode> items;

    const JNode* get(const char* key) const
    {
        for (const auto& m : members)
            if (m.first == key)
                return &m.second;
        return nullptr;
    }
    std::string getString(const char* key) const
    {
        const JNode* n = get(key);
        return n && n->kind == 's' ? n->str : std::string();
    }
    bool getBool(const char* key) const
    {
        const JNode* n = get(key);
        return n && n->kind == 'b' && n->b;
    }
    int64_t getInt(const char* key) const
    {
        const JNode* n = get(key);
        return n && n->kind == 'd' ? strtoll(n->str.c_str(), nullptr, 10) : 0;
    }
};

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

    bool number(std::string* out)
    {
        const char* start = p;
        if (p < end && *p == '-')
            ++p;
        while (p < end && (isdigit((unsigned char)*p) || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-'))
            ++p;
        out->assign(start, p);
        return p > start;
    }

    bool value(JNode* n)
    {
        ws();
        if (p >= end)
            return false;
        if (*p == '"') {
            n->kind = 's';
            return string(&n->str);
        }
        if (*p == '{' || *p == '[') {
            const bool obj = *p == '{';
            n->kind = obj ? 'o' : 'a';
            if (++depth > 64)
                return false;
            const char close = obj ? '}' : ']';
            ++p;
            ws();
            if (p < end && *p == close) {
                ++p;
                --depth;
                return true;
            }
            for (;;) {
                std::string key;
                if (obj) {
                    ws();
                    if (!string(&key))
                        return false;
                    ws();
                    if (p >= end || *p++ != ':')
                        return false;
                }
                JNode child;
                if (!value(&child))
                    return false;
                if (obj)
                    n->members.emplace_back(std::move(key), std::move(child));
                else
                    n->items.push_back(std::move(child));
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
        if (literal("true")) { n->kind = 'b'; n->b = true; return true; }
        if (literal("false")) { n->kind = 'b'; n->b = false; return true; }
        if (literal("null")) { n->kind = 'n'; return true; }
        n->kind = 'd';
        return number(&n->str);
    }
};

bool parseJson(const std::string& text, JNode* out)
{
    Json j{text.data(), text.data() + text.size()};
    if (!j.value(out))
        return false;
    j.ws();
    return j.p == j.end;
}

// One release object. False for drafts, unparsable tags and (unless allowed)
// pre-releases.
bool readRelease(const JNode& n, bool includePrereleases, Release* out)
{
    if (n.kind != 'o' || n.getBool("draft"))
        return false;
    const bool pre = n.getBool("prerelease");
    if (pre && !includePrereleases)
        return false;
    std::string tag = n.getString("tag_name");
    if (!parseVersion(tag, nullptr))
        return false;
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V'))
        tag.erase(0, 1);
    Release r;
    r.version = tag;
    r.prerelease = pre;
    r.url = n.getString("html_url");
    // Only ever open a GitHub page from the notice.
    if (r.url.compare(0, 19, "https://github.com/") != 0)
        r.url = kReleasesPage;
    if (const JNode* assets = n.get("assets")) {
        for (const JNode& a : assets->items) {
            if (a.kind != 'o')
                continue;
            Asset as;
            as.name = a.getString("name");
            as.url = a.getString("browser_download_url");
            as.size = a.getInt("size");
            // Names end up in file names and update.ini lines
            const bool plain = !as.name.empty() && as.name.find_first_of("/\\|\r\n") == std::string::npos &&
                               as.name[0] != '.';
            if (plain && !as.url.empty() && as.url.find_first_of("|\r\n") == std::string::npos)
                r.assets.push_back(as);
        }
    }
    if (out)
        *out = r;
    return true;
}

} // namespace

bool parseLatestRelease(const std::string& json, Release* out)
{
    JNode root;
    if (!parseJson(json, &root) || root.kind != 'o')
        return false;
    return readRelease(root, false, out);
}

bool parseReleases(const std::string& json, bool includePrereleases, Release* out)
{
    JNode root;
    if (!parseJson(json, &root))
        return false;
    if (root.kind == 'o')
        return readRelease(root, includePrereleases, out);
    if (root.kind != 'a')
        return false;
    bool found = false;
    Release best;
    Version bestV;
    for (const JNode& n : root.items) {
        Release r;
        Version v;
        if (!readRelease(n, includePrereleases, &r) || !parseVersion(r.version, &v))
            continue;
        if (!found || compareVersions(v, bestV) > 0) {
            best = r;
            bestV = v;
            found = true;
        }
    }
    if (found && out)
        *out = best;
    return found;
}

// {"format":1,"version":"..","parts":[{"name","size","sha256","dest",
//   "files":[["path",size,"sha256",exec],...]}]}
bool parseManifest(const std::string& json, Manifest* out)
{
    JNode root;
    if (!parseJson(json, &root) || root.kind != 'o' || root.getInt("format") != 1)
        return false;
    const JNode* parts = root.get("parts");
    if (!parts || parts->kind != 'a')
        return false;
    auto isHex = [](const std::string& s) {
        if (s.size() != 64)
            return false;
        for (char c : s)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                return false;
        return true;
    };
    // Relative, no "..", no drive or absolute path, no line breaks
    auto safePath = [](const std::string& p, bool folder) {
        if (p.empty())
            return folder;
        if (p[0] == '/' || p[0] == '\\' || p.find(':') != std::string::npos ||
            p.find_first_of("\r\n|") != std::string::npos)
            return false;
        if (folder && p.back() != '/')
            return false;
        size_t start = 0;
        while (start <= p.size()) {
            size_t end = p.find_first_of("/\\", start);
            if (end == std::string::npos)
                end = p.size();
            const std::string seg = p.substr(start, end - start);
            if (seg == ".." || seg == "." || (seg.empty() && end != p.size()))
                return false;
            start = end + 1;
        }
        return true;
    };
    Manifest m;
    m.version = root.getString("version");
    for (const JNode& pn : parts->items) {
        if (pn.kind != 'o')
            return false;
        Part p;
        p.name = pn.getString("name");
        p.size = pn.getInt("size");
        p.sha256 = pn.getString("sha256");
        p.dest = pn.getString("dest");
        if (p.name.empty() || p.name.find_first_of("/\\|\r\n") != std::string::npos || p.name[0] == '.' ||
            !isHex(p.sha256) || !safePath(p.dest, true))
            return false;
        const JNode* files = pn.get("files");
        if (!files || files->kind != 'a')
            return false;
        for (const JNode& fn : files->items) {
            if (fn.kind != 'a' || fn.items.size() < 3 || fn.items[0].kind != 's' || fn.items[1].kind != 'd' ||
                fn.items[2].kind != 's')
                return false;
            PartFile f;
            f.path = fn.items[0].str;
            f.size = strtoll(fn.items[1].str.c_str(), nullptr, 10);
            f.sha256 = fn.items[2].str;
            f.exec = fn.items.size() > 3 && ((fn.items[3].kind == 'b' && fn.items[3].b) ||
                                             (fn.items[3].kind == 'd' && fn.items[3].str != "0"));
            if (!safePath(f.path, false) || f.size < 0 || !isHex(f.sha256))
                return false;
            p.files.push_back(f);
        }
        m.parts.push_back(p);
    }
    if (out)
        *out = m;
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
        else if (key == "channel") s.testChannel = val == "test";
        else if (key == "latest_channel") s.latestChannel = val;
        else if (key == "asset") {
            // name|size|url
            const size_t a = val.find('|'), b = a == std::string::npos ? a : val.find('|', a + 1);
            if (b != std::string::npos) {
                Asset as;
                as.name = val.substr(0, a);
                as.size = strtoll(val.substr(a + 1, b - a - 1).c_str(), nullptr, 10);
                as.url = val.substr(b + 1);
                s.latestAssets.push_back(as);
            }
        }
        else if (key == "last_check") s.lastCheck = strtoll(val.c_str(), nullptr, 10);
        else if (key == "latest_version") s.latestVersion = val;
        else if (key == "latest_url") s.latestUrl = val;
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
          << "; channel=test also offers test versions (pre-releases)\n"
          << "channel=" << (s.testChannel ? "test" : "stable") << "\n"
          << "latest_channel=" << s.latestChannel << "\n"
          << "last_check=" << (long long)s.lastCheck << "\n"
          << "latest_version=" << s.latestVersion << "\n"
          << "latest_url=" << s.latestUrl << "\n";
        for (const Asset& a : s.latestAssets)
            o << "asset=" << a.name << "|" << (long long)a.size << "|" << a.url << "\n";
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

bool shouldNotify(const State& s, const std::string& current)
{
    Version cur, latest;
    if (!parseVersion(s.latestVersion, &latest))
        return false;
    // Unparsable versions only get here from development builds checking
    // against a test server (MC2_UPDATE_URL): treat them as 0.0.0.
    if (!parseVersion(current, &cur))
        cur = Version();
    return compareVersions(latest, cur) > 0;
}

//==============================================================================
// HTTP

namespace {
// Receives the body piece by piece; false stops the transfer.
typedef std::function<bool(const char* data, size_t size)> DataFn;
}

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

// timeoutSec applies to each step (connect, each read), not the whole transfer.
static bool httpFetch(const std::string& url, const char* accept, int timeoutSec, const DataFn& onData,
                      std::string* error)
{
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0};
    wchar_t urlPath[4096] = {0};
    wchar_t extra[4096] = {0};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = urlPath;
    uc.dwUrlPathLength = 4095;
    uc.lpszExtraInfo = extra; // the query ("?per_page=20")
    uc.dwExtraInfoLength = 4095;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        if (error) *error = "bad URL";
        return false;
    }
    const std::wstring path = std::wstring(urlPath) + extra;
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
        request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (request) {
        const std::wstring headers = widen(std::string("Accept: ") + accept + "\r\n");
        if (WinHttpSendRequest(request, headers.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD code = 0, size = sizeof(code);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
            if (code != 200) {
                if (error) *error = "HTTP " + std::to_string(code);
            } else {
                std::vector<char> buf(64 * 1024);
                bool readOk = true;
                for (;;) {
                    DWORD got = 0;
                    if (!WinHttpReadData(request, buf.data(), (DWORD)buf.size(), &got)) {
                        readOk = false;
                        if (error) *error = "read failed (" + std::to_string(GetLastError()) + ")";
                        break;
                    }
                    if (got == 0)
                        break;
                    if (!onData(buf.data(), got)) {
                        readOk = false;
                        if (error && error->empty()) *error = "stopped";
                        break;
                    }
                }
                ok = readOk;
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
    kCurlOptLowSpeedLimit = 19,
    kCurlOptLowSpeedTime = 20,
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
    const DataFn* fn;
    long* code;
    CURL* curl;
    CurlApi* api;
    bool stopped = false;
};

size_t writeBody(char* ptr, size_t size, size_t n, void* user)
{
    Sink* s = (Sink*)user;
    const size_t bytes = size * n;
    // Only the final response's body counts (not error pages)
    long code = 0;
    s->api->easy_getinfo(s->curl, kCurlInfoResponseCode, &code);
    if (code != 200)
        return bytes;
    if (!(*s->fn)(ptr, bytes)) {
        s->stopped = true;
        return 0;
    }
    return bytes;
}

} // namespace

// timeoutSec: whole request when totalTimeout, else the connection and
// stalls (less than 1 byte/s for that long).
static bool httpFetchCurl(const std::string& url, const char* accept, int timeoutSec, bool totalTimeout,
                          const DataFn& onData, std::string* error)
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
    long code = 0;
    Sink sink{&onData, &code, c, api};
    const std::string acceptHeader = std::string("Accept: ") + accept;
    curl_slist* headers = api->slist_append(nullptr, acceptHeader.c_str());
    api->easy_setopt(c, kCurlOptUrl, url.c_str());
    api->easy_setopt(c, kCurlOptUserAgent, kUserAgent);
    api->easy_setopt(c, kCurlOptHttpHeader, headers);
    api->easy_setopt(c, kCurlOptWriteFunction, &writeBody);
    api->easy_setopt(c, kCurlOptWriteData, (void*)&sink);
    api->easy_setopt(c, kCurlOptFollowLocation, 1L);
    api->easy_setopt(c, kCurlOptMaxRedirs, 5L);
    if (totalTimeout) {
        api->easy_setopt(c, kCurlOptTimeout, (long)timeoutSec);
    } else {
        api->easy_setopt(c, kCurlOptLowSpeedLimit, 1L);
        api->easy_setopt(c, kCurlOptLowSpeedTime, (long)timeoutSec);
    }
    api->easy_setopt(c, kCurlOptConnectTimeout, (long)timeoutSec);
    api->easy_setopt(c, kCurlOptNoSignal, 1L);
    const int rc = api->easy_perform(c);
    api->easy_getinfo(c, kCurlInfoResponseCode, &code);
    api->easy_cleanup(c);
    api->slist_free_all(headers);
    if (code != 200 && code != 0) {
        if (error) *error = "HTTP " + std::to_string(code);
        return false;
    }
    if (rc != 0) {
        if (error && error->empty()) *error = sink.stopped ? "stopped" : api->easy_strerror(rc);
        return false;
    }
    if (code != 200) {
        if (error) *error = "no response";
        return false;
    }
    return true;
}

static bool httpFetch(const std::string& url, const char* accept, int timeoutSec, const DataFn& onData,
                      std::string* error)
{
    return httpFetchCurl(url, accept, timeoutSec, true, onData, error);
}

#endif

bool httpGet(const std::string& url, std::string* body, std::string* error, int timeoutSec)
{
    std::string data;
    std::string err;
    const bool ok = httpFetch(url, "application/vnd.github+json", timeoutSec,
        [&](const char* p, size_t n) {
            if (data.size() + n > kMaxBody) {
                err = "response too large";
                return false;
            }
            data.append(p, n);
            return true;
        }, &err);
    if (!ok) {
        if (error) *error = err;
        return false;
    }
    if (body)
        *body = data;
    return true;
}

bool httpDownload(const std::string& url, const std::string& path, std::atomic<int64_t>* received,
                  const std::atomic<bool>* cancel, std::string* error)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    if (received)
        *received = 0;
    std::string err;
    const DataFn onData = [&](const char* p, size_t n) {
        if (cancel && *cancel) {
            err = "cancelled";
            return false;
        }
        if (fwrite(p, 1, n, f) != n) {
            err = "disk full?";
            return false;
        }
        if (received)
            *received += (int64_t)n;
        return true;
    };
#ifdef _WIN32
    bool ok = httpFetch(url, "application/octet-stream", 30, onData, &err);
#else
    bool ok = httpFetchCurl(url, "application/octet-stream", 30, false, onData, &err);
#endif
    if (fclose(f) != 0 && ok) {
        ok = false;
        err = "disk full?";
    }
    if (!ok) {
        remove(path.c_str());
        if (error) *error = err;
    }
    return ok;
}

//==============================================================================
// Background check

namespace {

struct Checker {
    std::mutex m;
    Status status = Status::Idle;
    Release release;
    std::string statePath;
    Config config;        // of the running check (for setTestChannel)
    bool testServer = false;
    std::condition_variable done;
    bool running = false; // the detached thread has not finished yet
    bool started = false;
    bool recheck = false; // setTestChannel during a check: run again
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

void runCheckBody(const Config& cfg);

void runCheck(Config cfg)
{
    for (;;) {
        runCheckBody(cfg);
        Checker& c = checker();
        std::lock_guard<std::mutex> lock(c.m);
        if (!c.recheck) {
            c.running = false;
            c.done.notify_all();
            return;
        }
        c.recheck = false;
        cfg.force = true;
        cfg.now = (int64_t)time(nullptr);
    }
}

void runCheckBody(const Config& cfg)
{
    State state;
    loadState(cfg.statePath, &state);
    if (!state.checkEnabled) {
        setResult(Status::Disabled);
        return;
    }
    const char* channel = state.testChannel ? "test" : "stable";
    // Test versions come often: the test channel asks at every start.
    const bool fresh = !cfg.force && !state.testChannel && state.latestChannel == channel &&
                       state.lastCheck > 0 && cfg.now >= state.lastCheck &&
                       cfg.now - state.lastCheck < kCheckInterval && !state.latestVersion.empty();
    if (!fresh) {
        const std::string url = !cfg.url.empty() ? cfg.url : state.testChannel ? kListUrl : kDefaultUrl;
        std::string body, error;
        Release r;
        if (!httpGet(url, &body, &error, 5) || !parseReleases(body, state.testChannel, &r)) {
            if (error.empty())
                error = "unexpected response";
            fprintf(stderr, "update check: %s\n", error.c_str());
            setResult(Status::CheckFailed);
            return;
        }
        // The option may have changed while the request ran
        State now;
        loadState(cfg.statePath, &now);
        state.testChannel = now.testChannel;
        state.checkEnabled = now.checkEnabled;
        state.lastCheck = cfg.now;
        state.latestChannel = channel;
        state.latestVersion = r.version;
        state.latestUrl = r.url;
        state.latestAssets = r.assets;
        if (!saveState(cfg.statePath, state))
            fprintf(stderr, "update check: cannot write %s\n", cfg.statePath.c_str());
    }
    if (shouldNotify(state, cfg.currentVersion)) {
        Release r;
        r.version = state.latestVersion;
        r.url = state.latestUrl.empty() ? kReleasesPage : state.latestUrl;
        r.assets = state.latestAssets;
        Version v;
        r.prerelease = parseVersion(r.version, &v) && !v.pre.empty();
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
    // With a test server (MC2_UPDATE_URL) development builds check too.
    const char* testUrl = getenv("MC2_UPDATE_URL");
    const bool testing = config.url.empty() && testUrl && *testUrl;
    if (testing)
        cfg.url = testUrl;
    if (cfg.now == 0)
        cfg.now = (int64_t)time(nullptr);
    c.statePath = cfg.statePath;
    c.config = cfg;
    c.testServer = !cfg.url.empty();
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

namespace {
// With c.m held: run the check again now (or right after the running one).
void recheckLocked(Checker& c)
{
    if (c.status == Status::Disabled || c.status == Status::Idle)
        return;
    if (c.running) {
        c.recheck = true;
        return;
    }
    Config cfg = c.config;
    cfg.force = true;
    cfg.now = (int64_t)time(nullptr);
    c.status = Status::Checking;
    c.running = true;
    std::thread(runCheck, cfg).detach();
}
} // namespace

void checkNow()
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    recheckLocked(c);
}

void setStatePath(const std::string& path)
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    c.statePath = path;
}

bool testChannel()
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    State state;
    loadState(c.statePath, &state);
    return state.testChannel;
}

void setTestChannel(bool on)
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    if (c.statePath.empty())
        return;
    State state;
    loadState(c.statePath, &state);
    if (state.testChannel == on)
        return;
    state.testChannel = on;
    saveState(c.statePath, state);
    // Check again with the new channel (unless the check is off for good)
    recheckLocked(c);
}

bool usingTestServer()
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    return c.testServer;
}

std::string updatesDir()
{
    Checker& c = checker();
    std::lock_guard<std::mutex> lock(c.m);
    if (c.statePath.empty())
        return std::string();
    const size_t slash = c.statePath.find_last_of("/\\");
    const std::string dir = slash == std::string::npos ? std::string(".") : c.statePath.substr(0, slash);
    return dir + "/updates";
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
    c.recheck = false;
}

} // namespace gos_update
