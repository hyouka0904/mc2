// SPDX-License-Identifier: GPL-3.0-only
// Tests for the update check (GameOS/gameos/update/gos_update): version
// parsing and ordering, the GitHub release JSON, update.ini, and a full
// background check against a tiny HTTP server on 127.0.0.1.

#include "mc2test.h"

#include "update/gos_update.h"

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <zlib.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define CLOSESOCK close
#endif

using namespace gos_update;

namespace {

Version ver(const char* s)
{
    Version v;
    CHECK(parseVersion(s, &v));
    return v;
}

int cmp(const char* a, const char* b) { return compareVersions(ver(a), ver(b)); }

const char* kReleaseJson =
    "{\n"
    "  \"url\": \"https://api.github.com/repos/hyouka0904/mc2/releases/1\",\n"
    "  \"html_url\": \"https://github.com/hyouka0904/mc2/releases/tag/v1.4.0\",\n"
    "  \"tag_name\": \"v1.4.0\",\n"
    "  \"name\": \"MechCommander 2 1.4.0 \\u4e2d\\u6587 \\\"q\\\"\",\n"
    "  \"draft\": false,\n"
    "  \"prerelease\": false,\n"
    "  \"author\": {\"login\": \"x\", \"id\": 1, \"site_admin\": false},\n"
    "  \"assets\": [{\"name\": \"a.zip\", \"size\": 123, \"tag_name\": \"v9.9.9\"}, null, 1.5e3],\n"
    "  \"body\": \"line1\\nline2\"\n"
    "}\n";

// Answers each connection with `status` and `body`, then closes it.
struct TinyServer {
    sock_t listener = (sock_t)-1;
    int port = 0;
    std::atomic<int> requests{0};
    std::string status = "200 OK";
    std::string body;
    std::map<std::string, std::string> routes; // path -> body (else `body`)
    std::thread thread;
    std::atomic<bool> stop{false};

    bool start()
    {
#ifdef _WIN32
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        listener = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listener, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(listener, 4) != 0)
            return false;
        socklen_t len = sizeof(addr);
        getsockname(listener, (sockaddr*)&addr, &len);
        port = ntohs(addr.sin_port);
        thread = std::thread([this] {
            while (!stop) {
                sock_t c = accept(listener, nullptr, nullptr);
                if (c == (sock_t)-1 || stop) {
                    if (c != (sock_t)-1)
                        CLOSESOCK(c);
                    break;
                }
                std::string req;
                char buf[1024];
                while (req.find("\r\n\r\n") == std::string::npos) {
                    const int n = (int)recv(c, buf, sizeof(buf), 0);
                    if (n <= 0)
                        break;
                    req.append(buf, n);
                }
                ++requests;
                std::string path;
                const size_t sp = req.find(' ');
                if (sp != std::string::npos)
                    path = req.substr(sp + 1, req.find(' ', sp + 1) - sp - 1);
                std::string st = status;
                std::string b = body;
                if (!routes.empty()) {
                    auto it = routes.find(path);
                    if (it != routes.end())
                        b = it->second;
                    else {
                        st = "404 Not Found";
                        b = "missing";
                    }
                }
                const std::string resp = "HTTP/1.1 " + st + "\r\nContent-Type: application/json\r\n" +
                                         "Content-Length: " + std::to_string(b.size()) +
                                         "\r\nConnection: close\r\n\r\n" + b;
                send(c, resp.data(), (int)resp.size(), 0);
                CLOSESOCK(c);
            }
        });
        return true;
    }

    std::string url(const char* path = "/latest") const
    {
        return "http://127.0.0.1:" + std::to_string(port) + path;
    }

    ~TinyServer()
    {
        stop = true;
        // Wake accept() with one last connection.
        sock_t s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((unsigned short)port);
        connect(s, (sockaddr*)&addr, sizeof(addr));
        CLOSESOCK(s);
        if (thread.joinable())
            thread.join();
        CLOSESOCK(listener);
    }
};

std::string statePath(const char* name)
{
    const std::string p = std::string("unit_update_") + name + ".ini";
    remove(p.c_str());
    return p;
}

} // namespace

TEST(update_parse_version)
{
    Version v = ver("v1.2.3");
    CHECK_EQ(v.major, 1);
    CHECK_EQ(v.minor, 2);
    CHECK_EQ(v.patch, 3);
    CHECK(v.pre.empty());
    v = ver("1.2");
    CHECK_EQ(v.patch, 0);
    v = ver("2.0.0-beta.1+build.5");
    CHECK_EQ(v.pre, std::string("beta.1"));
    CHECK(!parseVersion("", nullptr));
    CHECK(!parseVersion("1", nullptr));
    CHECK(!parseVersion("1.x", nullptr));
    CHECK(!parseVersion("1.2.3-", nullptr));
    CHECK(!parseVersion("1.2.3 ", nullptr));
    CHECK(!parseVersion("release", nullptr));
}

TEST(update_compare_versions)
{
    CHECK(cmp("1.0.0", "1.0.0") == 0);
    CHECK(cmp("1.0", "1.0.0") == 0);
    CHECK(cmp("1.0.1", "1.0.0") > 0);
    CHECK(cmp("1.10.0", "1.9.9") > 0);
    CHECK(cmp("2.0.0", "1.99.99") > 0);
    CHECK(cmp("1.0.0-beta", "1.0.0") < 0);
    CHECK(cmp("1.0.0-alpha", "1.0.0-alpha.1") < 0);
    CHECK(cmp("1.0.0-alpha.1", "1.0.0-alpha.beta") < 0);
    CHECK(cmp("1.0.0-beta.2", "1.0.0-beta.11") < 0);
    CHECK(cmp("1.0.0-rc.1", "1.0.0-beta.11") > 0);
}

TEST(update_development_builds)
{
    CHECK(isDevelopmentBuild("0.0.0-dev"));
    CHECK(isDevelopmentBuild("0.0.0-test"));
    CHECK(isDevelopmentBuild("1.2.0-14-gabc1234"));
    CHECK(isDevelopmentBuild("1.2.0-14-gabc1234-dirty"));
    CHECK(isDevelopmentBuild("1.2.0-rc.1-3-g0123abcd"));
    CHECK(isDevelopmentBuild("1.2.0-dirty"));
    CHECK(isDevelopmentBuild("abc1234"));
    CHECK(!isDevelopmentBuild("1.2.0"));
    CHECK(!isDevelopmentBuild("0.3.1"));
    CHECK(!isDevelopmentBuild("1.2.0-beta.1"));
}

TEST(update_parse_release_json)
{
    Release r;
    CHECK(parseLatestRelease(kReleaseJson, &r));
    CHECK_EQ(r.version, std::string("1.4.0"));
    CHECK_EQ(r.url, std::string("https://github.com/hyouka0904/mc2/releases/tag/v1.4.0"));

    CHECK(!parseLatestRelease("", &r));
    CHECK(!parseLatestRelease("[]", &r));
    CHECK(!parseLatestRelease("{\"tag_name\": \"v1.0.0\"", &r));
    CHECK(!parseLatestRelease("{\"message\": \"Not Found\"}", &r));
    CHECK(!parseLatestRelease("{\"tag_name\": \"nightly\"}", &r));
    CHECK(!parseLatestRelease("{\"tag_name\": \"v2.0.0\", \"draft\": true}", &r));
    CHECK(!parseLatestRelease("{\"tag_name\": \"v2.0.0\", \"prerelease\": true}", &r));
    CHECK(!parseLatestRelease("{\"tag_name\": \"v2.0.0\" \"x\": 1}", &r));
    CHECK(!parseLatestRelease("{\"tag_name\": \"v2.0.0\", \"x\": \"\\q\"}", &r));

    // A page outside github.com is never opened.
    CHECK(parseLatestRelease("{\"tag_name\": \"2.0.0\", \"html_url\": \"https://evil.example/x\"}", &r));
    CHECK_EQ(r.version, std::string("2.0.0"));
    CHECK_EQ(r.url, std::string(kReleasesPage));
}

TEST(update_state_file)
{
    const std::string path = statePath("state");
    State s;
    CHECK(!loadState(path, &s));
    CHECK(s.checkEnabled);
    s.checkEnabled = false;
    s.lastCheck = 1700000000;
    s.latestVersion = "1.4.0";
    s.latestUrl = "https://github.com/hyouka0904/mc2/releases/tag/v1.4.0";
    s.snoozedVersion = "1.3.0";
    s.snoozedUntil = 1700600000;
    CHECK(saveState(path, s));
    State t;
    CHECK(loadState(path, &t));
    CHECK(!t.checkEnabled);
    CHECK_EQ(t.lastCheck, (int64_t)1700000000);
    CHECK_EQ(t.latestVersion, s.latestVersion);
    CHECK_EQ(t.latestUrl, s.latestUrl);
    CHECK_EQ(t.snoozedVersion, s.snoozedVersion);
    CHECK_EQ(t.snoozedUntil, s.snoozedUntil);
    remove(path.c_str());
}

TEST(update_should_notify)
{
    State s;
    s.latestVersion = "1.4.0";
    CHECK(shouldNotify(s, "1.3.2", 1000));
    CHECK(!shouldNotify(s, "1.4.0", 1000));
    CHECK(!shouldNotify(s, "1.5.0", 1000));
    s.snoozedVersion = "1.4.0";
    s.snoozedUntil = 2000;
    CHECK(!shouldNotify(s, "1.3.2", 1000));
    CHECK(shouldNotify(s, "1.3.2", 2000));
    s.latestVersion = "1.4.1"; // a newer release ends the snooze
    CHECK(shouldNotify(s, "1.3.2", 1000));
    CHECK(shouldNotify(s, "9b62e18-dirty", 1000)); // development build
    s.latestVersion = "";
    CHECK(!shouldNotify(s, "1.3.2", 1000));
}

TEST(update_check_against_local_server)
{
    // CI sets MC2_NO_UPDATE_CHECK=1 for the game; this test needs the check.
#ifdef _WIN32
    _putenv("MC2_NO_UPDATE_CHECK=");
    _putenv("MC2_UPDATE_URL=");
#else
    unsetenv("MC2_NO_UPDATE_CHECK");
    unsetenv("MC2_UPDATE_URL");
#endif
    TinyServer server;
    CHECK(server.start());
    server.body = kReleaseJson;
    const std::string path = statePath("check");

    // Newer release: Available, and the answer is cached.
    Config cfg;
    cfg.currentVersion = "1.3.0";
    cfg.url = server.url();
    cfg.statePath = path;
    cfg.now = 1700000000;
    resetForTests();
    startCheck(cfg);
    waitForCheck();
    CHECK(status() == Status::Available);
    CHECK_EQ(available().version, std::string("1.4.0"));
    CHECK_EQ(server.requests.load(), 1);
    State s;
    CHECK(loadState(path, &s));
    CHECK_EQ(s.latestVersion, std::string("1.4.0"));
    CHECK_EQ(s.lastCheck, (int64_t)1700000000);

    // Within a day: no request, same answer from update.ini.
    resetForTests();
    cfg.now += 3600;
    startCheck(cfg);
    waitForCheck();
    CHECK(status() == Status::Available);
    CHECK_EQ(server.requests.load(), 1);

    // "Later" hides it.
    snooze();
    CHECK(status() == Status::UpToDate);
    CHECK(loadState(path, &s));
    CHECK_EQ(s.snoozedVersion, std::string("1.4.0"));

    // A day later the server is asked again; already up to date.
    resetForTests();
    cfg.now += kCheckInterval;
    cfg.currentVersion = "1.4.0";
    startCheck(cfg);
    waitForCheck();
    CHECK(status() == Status::UpToDate);
    CHECK_EQ(server.requests.load(), 2);

    // Server errors leave the cached answer alone.
    server.status = "500 Internal Server Error";
    resetForTests();
    cfg.force = true;
    startCheck(cfg);
    waitForCheck();
    CHECK(status() == Status::CheckFailed);
    CHECK(loadState(path, &s));
    CHECK_EQ(s.latestVersion, std::string("1.4.0"));

    // check=0 in update.ini turns it off without asking.
    s.checkEnabled = false;
    CHECK(saveState(path, s));
    const int before = server.requests.load();
    resetForTests();
    startCheck(cfg);
    waitForCheck();
    CHECK(status() == Status::Disabled);
    CHECK_EQ(server.requests.load(), before);

    // Development builds never ask.
    remove(path.c_str());
    resetForTests();
    cfg.currentVersion = "1.3.0-5-gdeadbee";
    startCheck(cfg);
    waitForCheck();
    CHECK(status() == Status::Disabled);
    CHECK_EQ(server.requests.load(), before);
    remove(path.c_str());
    resetForTests();
}

//==============================================================================
// Test channel, packages, installing

namespace {

const char* kReleaseList =
    "[\n"
    "  {\"tag_name\": \"v1.5.0-test.7\", \"prerelease\": true, \"draft\": false,\n"
    "   \"html_url\": \"https://github.com/hyouka0904/mc2/releases/tag/v1.5.0-test.7\",\n"
    "   \"assets\": [\n"
    "     {\"name\": \"MechCommander2-1.5.0-test.7-windows-x64.zip\", \"size\": 10,\n"
    "      \"browser_download_url\": \"https://github.com/hyouka0904/mc2/releases/download/v1.5.0-test.7/MechCommander2-1.5.0-test.7-windows-x64.zip\"},\n"
    "     {\"name\": \"MechCommander2-1.5.0-test.7-linux-x86_64.tar.gz\", \"size\": 20,\n"
    "      \"browser_download_url\": \"https://github.com/hyouka0904/mc2/releases/download/v1.5.0-test.7/MechCommander2-1.5.0-test.7-linux-x86_64.tar.gz\"},\n"
    "     {\"name\": \"../evil\", \"size\": 1, \"browser_download_url\": \"https://x/y\"},\n"
    "     {\"name\": \"SHA256SUMS.txt\", \"size\": 300,\n"
    "      \"browser_download_url\": \"https://github.com/hyouka0904/mc2/releases/download/v1.5.0-test.7/SHA256SUMS.txt\"}]},\n"
    "  {\"tag_name\": \"v9.0.0\", \"draft\": true},\n"
    "  {\"tag_name\": \"v1.4.0\", \"prerelease\": false,\n"
    "   \"html_url\": \"https://github.com/hyouka0904/mc2/releases/tag/v1.4.0\", \"assets\": []},\n"
    "  {\"tag_name\": \"v1.5.0-test.6\", \"prerelease\": true},\n"
    "  {\"tag_name\": \"nightly\", \"prerelease\": true}\n"
    "]\n";

std::string readFile(const std::string& path)
{
    std::ifstream in(path.c_str(), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void writeFile(const std::string& path, const std::string& data)
{
    std::ofstream o(path.c_str(), std::ios::binary | std::ios::trunc);
    o << data;
}

bool exists(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (f)
        fclose(f);
    return f != nullptr;
}

void makeDir(const std::string& p)
{
#ifdef _WIN32
    CreateDirectoryA(p.c_str(), nullptr);
#else
    mkdir(p.c_str(), 0755);
#endif
}

// --- a tiny zip writer: entry 0 stored, others deflated
void putLe16(std::string& s, unsigned v) { s += (char)(v & 255); s += (char)(v >> 8); }
void putLe32(std::string& s, uint32_t v) { putLe16(s, v & 0xffff); putLe16(s, v >> 16); }

std::string makeZip(const std::vector<std::pair<std::string, std::string>>& files)
{
    std::string out, cd;
    int index = 0;
    for (const auto& f : files) {
        const bool deflated = index++ > 0;
        std::string data = f.second;
        if (deflated) {
            z_stream z;
            memset(&z, 0, sizeof(z));
            deflateInit2(&z, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            std::string packed(compressBound((uLong)data.size()) + 64, '\0');
            z.next_in = (Bytef*)data.data();
            z.avail_in = (uInt)data.size();
            z.next_out = (Bytef*)&packed[0];
            z.avail_out = (uInt)packed.size();
            deflate(&z, Z_FINISH);
            packed.resize(z.total_out);
            deflateEnd(&z);
            data = packed;
        }
        const uint32_t crc = (uint32_t)crc32(0, (const Bytef*)f.second.data(), (uInt)f.second.size());
        const uint32_t offset = (uint32_t)out.size();
        putLe32(out, 0x04034b50); putLe16(out, 20); putLe16(out, 0); putLe16(out, deflated ? 8 : 0);
        putLe32(out, 0); putLe32(out, crc); putLe32(out, (uint32_t)data.size());
        putLe32(out, (uint32_t)f.second.size()); putLe16(out, (unsigned)f.first.size()); putLe16(out, 0);
        out += f.first;
        out += data;
        putLe32(cd, 0x02014b50); putLe16(cd, 20); putLe16(cd, 20); putLe16(cd, 0); putLe16(cd, deflated ? 8 : 0);
        putLe32(cd, 0); putLe32(cd, crc); putLe32(cd, (uint32_t)data.size()); putLe32(cd, (uint32_t)f.second.size());
        putLe16(cd, (unsigned)f.first.size()); putLe16(cd, 0); putLe16(cd, 0); putLe16(cd, 0); putLe16(cd, 0);
        putLe32(cd, 0); putLe32(cd, offset);
        cd += f.first;
    }
    const uint32_t cdOffset = (uint32_t)out.size();
    out += cd;
    putLe32(out, 0x06054b50); putLe16(out, 0); putLe16(out, 0); putLe16(out, (unsigned)files.size());
    putLe16(out, (unsigned)files.size()); putLe32(out, (uint32_t)cd.size()); putLe32(out, cdOffset); putLe16(out, 0);
    return out;
}

// --- a tiny tar writer
struct TarEntry {
    std::string name;
    std::string data;
    char type = '0';
    std::string link;
    unsigned mode = 0644;
};

void tarHeader(std::string& out, const std::string& name, size_t size, char type, const std::string& link,
               unsigned mode)
{
    char h[512];
    memset(h, 0, sizeof(h));
    strncpy(h, name.c_str(), 99);
    snprintf(h + 100, 8, "%07o", mode);
    snprintf(h + 108, 8, "%07o", 0);
    snprintf(h + 116, 8, "%07o", 0);
    snprintf(h + 124, 12, "%011o", (unsigned)size);
    snprintf(h + 136, 12, "%011o", 0);
    h[156] = type;
    strncpy(h + 157, link.c_str(), 99);
    memcpy(h + 257, "ustar\0" "00", 8);
    memset(h + 148, ' ', 8);
    unsigned sum = 0;
    for (unsigned char c : h)
        sum += c;
    snprintf(h + 148, 8, "%06o", sum);
    out.append(h, 512);
}

void tarData(std::string& out, const std::string& data)
{
    out += data;
    out.append((512 - data.size() % 512) % 512, '\0');
}

std::string makeTar(const std::vector<TarEntry>& entries)
{
    std::string out;
    for (const TarEntry& e : entries) {
        if (e.name.size() > 99) {
            // pax header with the long path
            std::string rec = " path=" + e.name + "\n";
            std::string len = std::to_string(rec.size() + 2);
            std::string full = len + rec;
            if (full.size() != std::stoul(len))
                full = std::to_string(full.size()) + rec;
            tarHeader(out, "PaxHeader", full.size(), 'x', "", 0644);
            tarData(out, full);
        }
        tarHeader(out, e.name.substr(0, 99), e.data.size(), e.type, e.link, e.mode);
        tarData(out, e.data);
    }
    out.append(1024, '\0');
    return out;
}

void writeGz(const std::string& path, const std::string& data)
{
    gzFile g = gzopen(path.c_str(), "wb");
    gzwrite(g, data.data(), (unsigned)data.size());
    gzclose(g);
}

} // namespace

TEST(update_parse_release_list)
{
    Release r;
    // Stable channel: pre-releases and drafts are skipped
    CHECK(parseReleases(kReleaseList, false, &r));
    CHECK_EQ(r.version, std::string("1.4.0"));
    CHECK(!r.prerelease);
    // Test channel: the newest, pre-release or not
    CHECK(parseReleases(kReleaseList, true, &r));
    CHECK_EQ(r.version, std::string("1.5.0-test.7"));
    CHECK(r.prerelease);
    CHECK_EQ(r.assets.size(), (size_t)3); // "../evil" dropped
    CHECK_EQ(r.assets[1].name, std::string("MechCommander2-1.5.0-test.7-linux-x86_64.tar.gz"));
    CHECK_EQ(r.assets[1].size, (int64_t)20);
    // A single object works too (releases/latest)
    CHECK(parseReleases(kReleaseJson, true, &r));
    CHECK_EQ(r.version, std::string("1.4.0"));
    CHECK(!parseReleases("[]", true, &r));
    CHECK(!parseReleases("[{\"tag_name\": \"v2.0.0\", \"draft\": true}]", true, &r));
    // A release sorts after its own test versions
    CHECK(parseReleases("[{\"tag_name\":\"v1.5.0-test.9\",\"prerelease\":true},{\"tag_name\":\"v1.5.0\"}]", true, &r));
    CHECK_EQ(r.version, std::string("1.5.0"));
}

TEST(update_state_channel_and_assets)
{
    const std::string path = statePath("channel");
    State s;
    s.testChannel = true;
    s.latestChannel = "test";
    s.latestVersion = "1.5.0-test.7";
    Asset a;
    a.name = "MechCommander2-1.5.0-test.7-windows-x64.zip";
    a.size = 600000000;
    a.url = "https://github.com/hyouka0904/mc2/releases/download/v1.5.0-test.7/x.zip";
    s.latestAssets.push_back(a);
    CHECK(saveState(path, s));
    State t;
    CHECK(loadState(path, &t));
    CHECK(t.testChannel);
    CHECK_EQ(t.latestChannel, std::string("test"));
    CHECK_EQ(t.latestAssets.size(), (size_t)1);
    CHECK_EQ(t.latestAssets[0].name, a.name);
    CHECK_EQ(t.latestAssets[0].size, a.size);
    CHECK_EQ(t.latestAssets[0].url, a.url);
    remove(path.c_str());
}

TEST(update_sha256)
{
    CHECK_EQ(sha256Hex("", 0), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256Hex("abc", 3), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const std::string m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK_EQ(sha256Hex(m.data(), m.size()),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    const std::string million(1000000, 'a');
    writeFile("unit_update_sha.bin", million);
    std::string hex;
    CHECK(sha256File("unit_update_sha.bin", &hex));
    CHECK_EQ(hex, std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    remove("unit_update_sha.bin");

    const std::string sums =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  a.zip\n"
        "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855 *b.tar.gz\r\n";
    CHECK(findChecksum(sums, "a.zip", &hex));
    CHECK_EQ(hex.substr(0, 6), std::string("ba7816"));
    CHECK(findChecksum(sums, "b.tar.gz", &hex));
    CHECK_EQ(hex.substr(0, 6), std::string("e3b0c4"));
    CHECK(!findChecksum(sums, "a.zi", &hex));
    CHECK(!findChecksum(sums, "c.zip", &hex));
}

TEST(update_pick_asset)
{
    Release r;
    CHECK(parseReleases(kReleaseList, true, &r));
    Asset a;
    CHECK(pickAsset(r, InstallKind::WindowsPortable, &a));
    CHECK_EQ(a.name, std::string("MechCommander2-1.5.0-test.7-windows-x64.zip"));
    CHECK(pickAsset(r, InstallKind::LinuxTarball, &a));
    CHECK_EQ(a.name, std::string("MechCommander2-1.5.0-test.7-linux-x86_64.tar.gz"));
    CHECK(!pickAsset(r, InstallKind::WindowsInstaller, &a));
    CHECK(!pickAsset(r, InstallKind::None, &a));

#ifndef _WIN32
#ifndef __APPLE__
    // Linux: the AppImage wins; a tarball is recognised by its layout
    std::string root;
    CHECK(detectInstallKind("/x/usr/bin/", "/home/a/MC2.AppImage", &root) == InstallKind::LinuxAppImage);
    CHECK_EQ(root, std::string("/home/a/MC2.AppImage"));
    makeDir("unit_update_tar");
    makeDir("unit_update_tar/usr");
    makeDir("unit_update_tar/usr/bin");
    makeDir("unit_update_tar/usr/share");
    makeDir("unit_update_tar/usr/share/mechcommander2");
    writeFile("unit_update_tar/mc2", "#!/bin/sh\n");
    CHECK(detectInstallKind("unit_update_tar/usr/bin", "", &root) == InstallKind::LinuxTarball);
    CHECK_EQ(root, std::string("unit_update_tar/"));
    CHECK(detectInstallKind("/usr/bin/", "", &root) == InstallKind::None);
#endif
#endif
}

TEST(update_extract_zip)
{
    const std::string dest = "unit_update_zipdest";
    makeDir(dest);
    makeDir(dest + "/data");
    writeFile(dest + "/mc2.exe", "old exe");
    writeFile(dest + "/keep.txt", "untouched");
    const std::string big(200000, 'x');
    writeFile("unit_update.zip", makeZip({{"Top/mc2.exe", "new exe"},
                                          {"Top/data/missions.fst", big},
                                          {"Top/../evil.txt", "no"},
                                          {"Top/sub/dir/file.txt", "deep"}}));
    std::vector<std::string> old;
    std::string err;
    CHECK(extractZip("unit_update.zip", dest, true, &old, &err));
    CHECK_EQ(err, std::string());
    CHECK_EQ(readFile(dest + "/mc2.exe"), std::string("new exe"));
    CHECK_EQ(readFile(dest + "/data/missions.fst"), big);
    CHECK_EQ(readFile(dest + "/sub/dir/file.txt"), std::string("deep"));
    CHECK_EQ(readFile(dest + "/keep.txt"), std::string("untouched"));
    CHECK(!exists("evil.txt"));
    CHECK(!exists(dest + "/mc2.exe.mc2new"));
    CHECK_EQ(old.size(), (size_t)1);
    CHECK_EQ(readFile(old[0]), std::string("old exe"));

    // A damaged archive changes nothing
    std::string zip = readFile("unit_update.zip");
    zip[zip.size() / 2] ^= 0x55;
    zip[zip.size() / 2 + 1] ^= 0x55;
    writeFile("unit_update_bad.zip", zip.substr(0, zip.size() - 30));
    CHECK(!extractZip("unit_update_bad.zip", dest, true, &old, &err));
    CHECK(!err.empty());
    CHECK_EQ(readFile(dest + "/mc2.exe"), std::string("new exe"));
    remove("unit_update.zip");
    remove("unit_update_bad.zip");
}

TEST(update_extract_tar_gz)
{
    const std::string dest = "unit_update_tgzdest";
    makeDir(dest);
    writeFile(dest + "/mc2", "old launcher");
    const std::string longName = "Top/usr/share/mechcommander2/" + std::string(90, 'n') + ".txt";
    std::vector<TarEntry> entries;
    entries.push_back({"Top/", "", '5', "", 0755});
    entries.push_back({"Top/mc2", "#!/bin/sh\nnew\n", '0', "", 0755});
    entries.push_back({longName, "long", '0', "", 0644});
    entries.push_back({"Top/../escape.txt", "no", '0', "", 0644});
#ifndef _WIN32
    entries.push_back({"Top/AppRun", "", '2', "usr/bin/mc2", 0777});
#endif
    writeGz("unit_update.tar.gz", makeTar(entries));
    std::vector<std::string> old;
    std::string err;
    CHECK(extractTarGz("unit_update.tar.gz", dest, true, &old, &err));
    CHECK_EQ(err, std::string());
    CHECK_EQ(readFile(dest + "/mc2"), std::string("#!/bin/sh\nnew\n"));
    CHECK_EQ(readFile(dest + "/" + longName.substr(4)), std::string("long"));
    CHECK(!exists("escape.txt"));
    CHECK_EQ(old.size(), (size_t)1);
#ifndef _WIN32
    struct stat st;
    CHECK(stat((dest + "/mc2").c_str(), &st) == 0 && (st.st_mode & 0100));
    char link[256] = {0};
    CHECK(readlink((dest + "/AppRun").c_str(), link, sizeof(link) - 1) > 0);
    CHECK_EQ(std::string(link), std::string("usr/bin/mc2"));
#endif
    // Truncated: nothing changes
    const std::string gz = readFile("unit_update.tar.gz");
    writeFile("unit_update_bad.tar.gz", gz.substr(0, gz.size() / 2));
    CHECK(!extractTarGz("unit_update_bad.tar.gz", dest, true, &old, &err));
    CHECK_EQ(readFile(dest + "/mc2"), std::string("#!/bin/sh\nnew\n"));
    remove("unit_update.tar.gz");
    remove("unit_update_bad.tar.gz");
}

TEST(update_download)
{
    TinyServer server;
    CHECK(server.start());
    std::string payload(3 * 1024 * 1024 + 17, '\0');
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = (char)(i * 31 + (i >> 9));
    server.routes["/pkg.zip"] = payload;
    std::atomic<int64_t> got{0};
    std::string err;
    CHECK(httpDownload(server.url("/pkg.zip"), "unit_update_dl.bin", &got, nullptr, &err));
    CHECK_EQ(got.load(), (int64_t)payload.size());
    std::string hex;
    CHECK(sha256File("unit_update_dl.bin", &hex));
    CHECK_EQ(hex, sha256Hex(payload.data(), payload.size()));
    // Errors remove the file
    CHECK(!httpDownload(server.url("/missing"), "unit_update_dl2.bin", &got, nullptr, &err));
    CHECK(err.find("404") != std::string::npos);
    CHECK(!exists("unit_update_dl2.bin"));
    std::atomic<bool> cancel{true};
    CHECK(!httpDownload(server.url("/pkg.zip"), "unit_update_dl3.bin", &got, &cancel, &err));
    CHECK(!exists("unit_update_dl3.bin"));
    remove("unit_update_dl.bin");
}

TEST(update_test_channel_check)
{
#ifdef _WIN32
    _putenv("MC2_NO_UPDATE_CHECK=");
    _putenv("MC2_UPDATE_URL=");
#else
    unsetenv("MC2_NO_UPDATE_CHECK");
    unsetenv("MC2_UPDATE_URL");
#endif
    TinyServer server;
    CHECK(server.start());
    server.body = kReleaseList;
    const std::string path = statePath("testchannel");
    Config cfg;
    cfg.currentVersion = "1.4.0";
    cfg.url = server.url("/releases");
    cfg.statePath = path;
    cfg.now = 1700000000;

    // Stable: 1.4.0 is the newest release, nothing to do
    resetForTests();
    startCheck(cfg);
    waitForCheck();
    CHECK(status() == Status::UpToDate);
    CHECK(!testChannel());

    // Turning the option on checks again at once and finds the test version
    setTestChannel(true);
    waitForCheck();
    CHECK(testChannel());
    CHECK(status() == Status::Available);
    CHECK_EQ(available().version, std::string("1.5.0-test.7"));
    CHECK(available().prerelease);
    CHECK_EQ(available().assets.size(), (size_t)3);
    State s;
    CHECK(loadState(path, &s));
    CHECK(s.testChannel);
    CHECK_EQ(s.latestChannel, std::string("test"));
    CHECK_EQ(s.latestAssets.size(), (size_t)3);

    // The test channel asks at every start (no once-a-day cache)
    const int before = server.requests.load();
    resetForTests();
    cfg.now += 60;
    startCheck(cfg);
    waitForCheck();
    CHECK_EQ(server.requests.load(), before + 1);
    CHECK(status() == Status::Available);

    // Back to stable: the cached test answer is not used
    setTestChannel(false);
    waitForCheck();
    CHECK(status() == Status::UpToDate);
    CHECK_EQ(updatesDir(), std::string("./updates"));
    remove(path.c_str());
    resetForTests();
}
