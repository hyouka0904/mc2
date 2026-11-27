// SPDX-License-Identifier: GPL-3.0-only
// Checks GitHub for a newer release of the game.
//
// startCheck() runs in a background thread: it asks the GitHub API for the
// latest published release (drafts and pre-releases are never returned
// there), compares its tag with the running version and remembers the answer
// in update.ini in the user directory, so the network is asked at most once
// a day. The main menu polls status() and shows a notice.
//
// Test channel ("channel=test" in update.ini, the "receive test versions"
// option): the release list is asked instead, at every start, and the newest
// release wins, pre-releases included. The Release workflow publishes builds
// started by hand as pre-releases v<next>-test.<run>.
//
// Installing (gos_update_install.cpp): the package for the way the game was
// installed is downloaded into <user dir>/updates, checked against the
// release's SHA256SUMS.txt and put in place; the game then restarts. With a
// part manifest (see Manifest below) only the changed parts are downloaded.
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

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace gos_update {

extern const char* const kDefaultUrl;
extern const char* const kListUrl; // all releases, newest first
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

struct Asset {
    std::string name;
    std::string url; // browser_download_url
    int64_t size = 0;
};

struct Release {
    std::string version; // tag without the leading 'v'
    std::string url;     // release page
    bool prerelease = false;
    std::vector<Asset> assets;
};
// Reads the GitHub "latest release" JSON. False on malformed JSON, a draft,
// a pre-release, or a missing / unparsable tag.
bool parseLatestRelease(const std::string& json, Release* out);
// A release object or a list of them: the newest release that is not a
// draft; pre-releases only with includePrereleases. False when none.
bool parseReleases(const std::string& json, bool includePrereleases, Release* out);

struct State {
    bool checkEnabled = true;
    bool testChannel = false;    // channel=test: pre-releases too
    std::string latestChannel;   // channel of the cached answer
    int64_t lastCheck = 0;       // unix time of the last successful check
    std::string latestVersion;   // from that check
    std::string latestUrl;
    std::string snoozedVersion;  // "later" was chosen for this version...
    int64_t snoozedUntil = 0;    // ...until this time
    std::vector<Asset> latestAssets;
};
bool loadState(const std::string& path, State* out); // false: no file (defaults)
bool saveState(const std::string& path, const State& state);

// Whether the notice should be shown for `latest` at time `now`.
bool shouldNotify(const State& state, const std::string& current, int64_t now);

// One GET; follows redirects. Fills body (at most 1 MB) and returns true on
// HTTP 200. Blocks for up to timeoutSec.
bool httpGet(const std::string& url, std::string* body, std::string* error, int timeoutSec);
// Downloads to a file (any size). *received counts the bytes as they come;
// setting *cancel stops it. Removes the file on failure.
bool httpDownload(const std::string& url, const std::string& path, std::atomic<int64_t>* received,
                  const std::atomic<bool>* cancel, std::string* error);

enum class Status { Idle, Disabled, Checking, UpToDate, Available, CheckFailed };

struct Config {
    std::string currentVersion;
    std::string url;       // empty: MC2_UPDATE_URL, else kDefaultUrl / kListUrl by channel
    std::string statePath; // update.ini
    int64_t now = 0;       // 0: the current time
    bool force = false;    // ignore the once-a-day limit
};

// Starts the background check (once). Config.statePath must be writable.
void startCheck(const Config& config);
// Where update.ini lives, for the option and the updates folder even when
// no check runs (startCheck sets it too).
void setStatePath(const std::string& path);
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

// The "receive test versions" option (update.ini of the running check).
bool testChannel();
// Saves the option and checks again at once when it changed.
void setTestChannel(bool on);
// The check asks a server other than GitHub (Config.url or MC2_UPDATE_URL):
// downloads may then come from it too.
bool usingTestServer();
// <user dir>/updates, empty before startCheck.
std::string updatesDir();

//==============================================================================
// Installing (gos_update_install.cpp)

enum class InstallKind { None, WindowsInstaller, WindowsPortable, LinuxAppImage, LinuxTarball, MacDmg };
// How the running game was installed. exeDir: the folder of the executable;
// appImage: $APPIMAGE (or empty). *root receives the folder that the package
// replaces (the AppImage file itself for LinuxAppImage).
InstallKind detectInstallKind(const std::string& exeDir, const std::string& appImage, std::string* root);
// The package name ends with this for each kind ("" for None).
const char* assetSuffix(InstallKind kind);
bool pickAsset(const Release& release, InstallKind kind, Asset* out);

std::string sha256Hex(const void* data, size_t size);
bool sha256File(const std::string& path, std::string* hex);
// Finds "<hex>  <name>" (or "<hex> *<name>") in a SHA256SUMS.txt.
bool findChecksum(const std::string& sums, const std::string& name, std::string* hex);

// Puts every file of the archive into destDir (without the archive's top
// folder when stripTop). Existing files are first renamed to <name>.mc2old
// (Windows can rename a running exe, but not overwrite it); their names go
// to *oldFiles. All new files are written before anything is renamed; on a
// failure nothing in destDir is changed.
bool extractZip(const std::string& archive, const std::string& destDir, bool stripTop,
                std::vector<std::string>* oldFiles, std::string* error);
bool extractTarGz(const std::string& archive, const std::string& destDir, bool stripTop,
                  std::vector<std::string>* oldFiles, std::string* error);

// Part updates. A release carries, per platform, <package>-manifest.json:
// the package split into parts (the program, and data parts shared by every
// platform), each a zip with the list of its files. The game downloads only
// the parts holding a file that differs from what is installed, and puts in
// only those files. Installs without a manifest (AppImage, macOS) and
// releases without one get the whole package.
struct PartFile {
    std::string path;   // inside the part's zip
    int64_t size = 0;
    std::string sha256; // lower-case hex
    bool exec = false;  // Unix: executable
};
struct Part {
    std::string name;   // release asset name
    int64_t size = 0;
    std::string sha256;
    std::string dest;   // folder below the install root ("" or "usr/share/mechcommander2/")
    std::vector<PartFile> files;
};
struct Manifest {
    std::string version;
    std::vector<Part> parts;
};
// The manifest asset name for a package name ("" when kind has none).
std::string manifestSuffix(InstallKind kind);
bool parseManifest(const std::string& json, Manifest* out);
// The files of each part that are missing in root or differ from it (same
// order as manifest.parts). hashCache: optional file of known hashes (keyed
// by path, size and modification time), read and rewritten.
std::vector<std::vector<PartFile>> changedFiles(const Manifest& manifest, const std::string& root,
                                                const std::string& hashCache = std::string());
struct PartInstall {
    std::string archive;  // the downloaded part
    std::string dest;     // Part::dest
    std::vector<PartFile> files;
};
// Puts the listed files of each part into root, all or nothing (like
// extractZip): each new file is checked against its hash before anything is
// replaced.
bool applyParts(const std::string& root, const std::vector<PartInstall>& parts,
                std::vector<std::string>* oldFiles, std::string* error);
// The plan file the elevated helper (mc2 -apply-update <plan>) reads.
bool writeApplyPlan(const std::string& path, const std::string& root, const std::string& version,
                    const std::vector<PartInstall>& parts);
// mc2 -apply-update <plan>: run by the game with administrator rights when
// the game folder is not writable (Windows, all-users install). Returns the
// process exit code; the error goes to <plan>.err.
int runApplyPlan(const std::string& planPath);

enum class DownloadStatus { Idle, Downloading, Ready, Failure };
// The newer release can be installed by the game (known install kind and a
// matching package); otherwise the notice opens the release page.
bool canInstall();
// Starts downloading available() in the background.
void startDownload();
DownloadStatus downloadStatus();
float downloadProgress();          // 0..1
std::string downloadError();
// After Ready: puts the new version in place. True: the game must quit now
// (the new version starts on its own). False with *error on failure.
bool installDownloaded(std::string* error);

// main(): remembers how to start the game again; removes what the previous
// update left behind.
void setLaunchCommand(int argc, char** argv);
void cleanupAfterUpdate(const std::string& currentVersion);
// main(), after shutdown: starts the new version if installDownloaded asked for it.
// Relaunch the current game after normal shutdown.
bool queueGameRestart();
void launchPendingRestart();

} // namespace gos_update
