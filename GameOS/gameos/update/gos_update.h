// SPDX-License-Identifier: GPL-3.0-only
// Checks GitHub for a newer release of the game.
//
// startCheck() runs in a background thread: it asks the GitHub API for the
// latest published release (drafts and pre-releases are never returned
// there), compares its tag with the running version and remembers the answer
// in update.ini in the user directory, so the network is asked at most once
// a day. The main menu polls status() and shows a notice.
//
// HTTPS uses what the platform already has: WinHTTP on Windows, the system
// libcurl (loaded at run time with dlopen) on Linux and macOS. Without
// libcurl the check is skipped. The request carries no identifying data.
//
// Turned off by -noupdatecheck, MC2_NO_UPDATE_CHECK=1, "check=0" in
// update.ini, and for development builds (see isDevelopmentBuild).
// MC2_UPDATE_URL replaces the GitHub URL (tests use a local server); with it
// development builds and scripted runs check too.
#pragma once

#include <cstdint>
#include <string>

namespace gos_update {

extern const char* const kDefaultUrl;
extern const char* const kReleasesPage;
// Seconds between network checks, and how long "later" hides a version.
const int64_t kCheckInterval = 24 * 60 * 60;
const int64_t kSnoozeTime = 7 * 24 * 60 * 60;

struct Version {
    int major = 0, minor = 0, patch = 0;
    std::string pre; // "beta.1" in 1.2.0-beta.1; empty for a release
};

// Accepts "1.2", "1.2.3", "v1.2.3", "1.2.3-beta.1" (build metadata after
// '+' is ignored). Returns false for anything else.
bool parseVersion(const std::string& text, Version* out);
// <0, 0, >0 like strcmp. A pre-release sorts before its release.
int compareVersions(const Version& a, const Version& b);
// Builds that should not look for updates: unparsable versions, 0.0.0
// (local and test builds) and "git describe" versions past a tag
// (1.2.0-14-gabc1234, optionally -dirty).
bool isDevelopmentBuild(const std::string& version);

struct Release {
    std::string version; // tag without the leading 'v'
    std::string url;     // release page
};
// Reads the GitHub "latest release" JSON. False on malformed JSON, a draft,
// a pre-release, or a missing / unparsable tag.
bool parseLatestRelease(const std::string& json, Release* out);

struct State {
    bool checkEnabled = true;
    int64_t lastCheck = 0;       // unix time of the last successful check
    std::string latestVersion;   // from that check
    std::string latestUrl;
    std::string snoozedVersion;  // "later" was chosen for this version...
    int64_t snoozedUntil = 0;    // ...until this time
};
bool loadState(const std::string& path, State* out); // false: no file (defaults)
bool saveState(const std::string& path, const State& state);

// Whether the notice should be shown for `latest` at time `now`.
bool shouldNotify(const State& state, const std::string& current, int64_t now);

// One GET; follows redirects. Fills body (at most 1 MB) and returns true on
// HTTP 200. Blocks for up to timeoutSec.
bool httpGet(const std::string& url, std::string* body, std::string* error, int timeoutSec);

enum class Status { Idle, Disabled, Checking, UpToDate, Available, CheckFailed };

struct Config {
    std::string currentVersion;
    std::string url;       // empty: MC2_UPDATE_URL or kDefaultUrl
    std::string statePath; // update.ini
    int64_t now = 0;       // 0: the current time
    bool force = false;    // ignore the once-a-day limit
};

// Starts the background check (once). Config.statePath must be writable.
void startCheck(const Config& config);
Status status();
// The newer release when status() is Available.
Release available();
// "Later": hide this version for kSnoozeTime.
void snooze();
// Waits for the background thread (tests, shutdown).
void waitForCheck();
// Forget the result so startCheck can run again (tests).
void resetForTests();

// True when the user or the environment turned the check off.
bool disabledByEnvironment();

} // namespace gos_update
