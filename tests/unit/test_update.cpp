// SPDX-License-Identifier: GPL-3.0-only
// Tests for the update check (GameOS/gameos/update/gos_update): version
// parsing and ordering, the GitHub release JSON, update.ini, and a full
// background check against a tiny HTTP server on 127.0.0.1.

#include "mc2test.h"

#include "update/gos_update.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
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
                const std::string resp = "HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\n" +
                                         "Content-Length: " + std::to_string(body.size()) +
                                         "\r\nConnection: close\r\n\r\n" + body;
                send(c, resp.data(), (int)resp.size(), 0);
                CLOSESOCK(c);
            }
        });
        return true;
    }

    std::string url() const { return "http://127.0.0.1:" + std::to_string(port) + "/latest"; }

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
