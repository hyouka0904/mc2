// SPDX-License-Identifier: GPL-3.0-only
// Downloading and installing a newer release (see gos_update.h).
//
//   Windows installer  setup.exe /VERYSILENT ... /DIR=<game folder>; the
//                      installer starts the game again when done (mc2.iss)
//   Windows zip        files replaced in place: the running mc2.exe and DLLs
//                      are renamed to *.mc2old first, removed at next start
//   Linux AppImage     new file next to the old one, renamed over it
//   Linux tarball      files replaced in place like the zip
//   macOS              the downloaded .dmg is opened; the player drags the
//                      app to Applications (no code signature: replacing the
//                      app ourselves would trip Gatekeeper again)
//
// Saves and settings live in the user directory and are never touched.
#include "gos_update.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include <zlib.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
extern char** environ;
#endif

namespace gos_update {

//==============================================================================
// SHA-256 (FIPS 180-4)

namespace {

struct Sha256 {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buf[64];
    size_t used = 0;
    uint64_t total = 0;

    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const uint8_t* p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void add(const void* data, size_t n)
    {
        const uint8_t* p = (const uint8_t*)data;
        total += n;
        while (n) {
            const size_t take = n < 64 - used ? n : 64 - used;
            memcpy(buf + used, p, take);
            used += take;
            p += take;
            n -= take;
            if (used == 64) {
                block(buf);
                used = 0;
            }
        }
    }

    std::string hex()
    {
        const uint64_t bits = total * 8;
        const uint8_t one = 0x80, zero = 0;
        add(&one, 1);
        while (used != 56)
            add(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i)
            len[i] = (uint8_t)(bits >> (56 - 8 * i));
        add(len, 8);
        static const char* digits = "0123456789abcdef";
        std::string out;
        for (uint32_t v : h)
            for (int i = 28; i >= 0; i -= 4)
                out += digits[(v >> i) & 15];
        return out;
    }
};

} // namespace

std::string sha256Hex(const void* data, size_t size)
{
    Sha256 s;
    s.add(data, size);
    return s.hex();
}

bool sha256File(const std::string& path, std::string* hex)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    Sha256 s;
    std::vector<char> buf(256 * 1024);
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
        s.add(buf.data(), n);
    const bool ok = !ferror(f);
    fclose(f);
    if (ok && hex)
        *hex = s.hex();
    return ok;
}

bool findChecksum(const std::string& sums, const std::string& name, std::string* hex)
{
    std::istringstream in(sums);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.size() < 66)
            continue;
        const std::string h = line.substr(0, 64);
        bool ok = true;
        for (char c : h)
            ok = ok && isxdigit((unsigned char)c);
        if (!ok || line[64] != ' ')
            continue;
        size_t p = 65;
        if (p < line.size() && (line[p] == ' ' || line[p] == '*'))
            ++p;
        if (line.compare(p, std::string::npos, name) == 0) {
            if (hex) {
                *hex = h;
                for (char& c : *hex)
                    c = (char)tolower((unsigned char)c);
            }
            return true;
        }
    }
    return false;
}

//==============================================================================
// Install kinds

static bool fileExists(const std::string& path)
{
#ifdef _WIN32
    const DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0;
#endif
}

static std::string withSlash(std::string dir)
{
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        dir += '/';
    return dir;
}

InstallKind detectInstallKind(const std::string& exeDir, const std::string& appImage, std::string* root)
{
    const std::string dir = withSlash(exeDir);
#if defined(_WIN32)
    (void)appImage;
    if (root)
        *root = dir;
    if (fileExists(dir + "unins000.exe"))
        return InstallKind::WindowsInstaller;
    if (fileExists(dir + "mc2.exe") && fileExists(dir + "mission.fst"))
        return InstallKind::WindowsPortable;
    return InstallKind::None;
#elif defined(__APPLE__)
    (void)appImage;
    if (root)
        *root = dir;
    return dir.find(".app/Contents/") != std::string::npos ? InstallKind::MacDmg : InstallKind::None;
#else
    if (!appImage.empty()) {
        if (root)
            *root = appImage;
        return InstallKind::LinuxAppImage;
    }
    // Tarball: <root>/mc2 (launcher), <root>/usr/bin/mc2, <root>/usr/share/mechcommander2
    const std::string tail = "usr/bin/";
    if (dir.size() > tail.size() && dir.compare(dir.size() - tail.size(), tail.size(), tail) == 0) {
        const std::string top = dir.substr(0, dir.size() - tail.size());
        if (fileExists(top + "mc2") && fileExists(top + "usr/share/mechcommander2")) {
            if (root)
                *root = top;
            return InstallKind::LinuxTarball;
        }
    }
    return InstallKind::None;
#endif
}

const char* assetSuffix(InstallKind kind)
{
    switch (kind) {
    case InstallKind::WindowsInstaller: return "-windows-x64-setup.exe";
    case InstallKind::WindowsPortable: return "-windows-x64.zip";
    case InstallKind::LinuxAppImage: return "-linux-x86_64.AppImage";
    case InstallKind::LinuxTarball: return "-linux-x86_64.tar.gz";
    case InstallKind::MacDmg: return "-macos-arm64.dmg";
    default: return "";
    }
}

bool pickAsset(const Release& release, InstallKind kind, Asset* out)
{
    const std::string suffix = assetSuffix(kind);
    if (suffix.empty())
        return false;
    for (const Asset& a : release.assets) {
        if (a.name.size() > suffix.size() &&
            a.name.compare(a.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            if (out)
                *out = a;
            return true;
        }
    }
    return false;
}

//==============================================================================
// Files

static bool makeDirs(const std::string& path)
{
    std::string cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/' || path[i] == '\\') {
            if (!cur.empty() && !(cur.size() == 2 && cur[1] == ':') && !fileExists(cur)) {
#ifdef _WIN32
                if (_mkdir(cur.c_str()) != 0 && errno != EEXIST)
                    return false;
#else
                if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST)
                    return false;
#endif
            }
        }
        if (i < path.size())
            cur += path[i];
    }
    return true;
}

static bool renameReplace(const std::string& from, const std::string& to)
{
#ifdef _WIN32
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return rename(from.c_str(), to.c_str()) == 0;
#endif
}

static bool copyFile(const std::string& from, const std::string& to)
{
    FILE* in = fopen(from.c_str(), "rb");
    if (!in)
        return false;
    FILE* out = fopen(to.c_str(), "wb");
    if (!out) {
        fclose(in);
        return false;
    }
    std::vector<char> buf(256 * 1024);
    size_t n;
    bool ok = true;
    while ((n = fread(buf.data(), 1, buf.size(), in)) > 0)
        if (fwrite(buf.data(), 1, n, out) != n) {
            ok = false;
            break;
        }
    ok = ok && !ferror(in);
    fclose(in);
    if (fclose(out) != 0)
        ok = false;
    if (!ok)
        remove(to.c_str());
    return ok;
}

// A safe relative path inside the archive: no "..", no absolute paths.
static bool safeEntryName(std::string& name, bool stripTop)
{
    for (char& c : name)
        if (c == '\\')
            c = '/';
    while (name.compare(0, 2, "./") == 0)
        name.erase(0, 2);
    if (stripTop) {
        const size_t slash = name.find('/');
        if (slash == std::string::npos)
            return false; // the top folder itself
        name.erase(0, slash + 1);
    }
    if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos)
        return false;
    std::stringstream ss(name);
    std::string part;
    while (std::getline(ss, part, '/'))
        if (part == "..")
            return false;
    return true;
}

namespace {

// Collects the new files as <name>.mc2new, then swaps them in.
struct Staging {
    std::string dest;
    struct Item {
        std::string target;
        bool symlink = false;
    };
    std::vector<Item> items;
    std::set<std::string> seen;

    std::string target(const std::string& rel) const { return dest + rel; }

    // Opens <target>.mc2new for writing; creates the folders.
    FILE* open(const std::string& rel, std::string* error)
    {
        const std::string t = target(rel);
        const size_t slash = t.find_last_of('/');
        if (slash != std::string::npos && !makeDirs(t.substr(0, slash))) {
            *error = "cannot create folder for " + rel;
            return nullptr;
        }
        FILE* f = fopen((t + ".mc2new").c_str(), "wb");
        if (!f) {
            *error = "cannot write " + t;
            return nullptr;
        }
        if (seen.insert(t).second)
            items.push_back({t, false});
        return f;
    }

    bool addSymlink(const std::string& rel, const std::string& linkTarget, std::string* error)
    {
#ifdef _WIN32
        (void)rel; (void)linkTarget; (void)error;
        return true; // not in Windows packages
#else
        const std::string t = target(rel);
        const size_t slash = t.find_last_of('/');
        if (slash != std::string::npos && !makeDirs(t.substr(0, slash))) {
            *error = "cannot create folder for " + rel;
            return false;
        }
        const std::string tmp = t + ".mc2new";
        unlink(tmp.c_str());
        if (symlink(linkTarget.c_str(), tmp.c_str()) != 0) {
            *error = "cannot create link " + t;
            return false;
        }
        if (seen.insert(t).second)
            items.push_back({t, true});
        return true;
#endif
    }

    void abandon()
    {
        for (const Item& it : items)
            remove((it.target + ".mc2new").c_str());
    }

    // Renames the old files away and the new ones in. Undoes on failure.
    bool commit(std::vector<std::string>* oldFiles, std::string* error)
    {
        std::vector<std::pair<std::string, std::string>> moved; // old name -> parked name
        std::vector<std::string> placed;
        for (const Item& it : items) {
            const std::string& t = it.target;
            bool exists = fileExists(t);
#ifndef _WIN32
            struct stat st;
            exists = lstat(t.c_str(), &st) == 0;
#endif
            std::string parked;
            if (exists) {
                parked = t + ".mc2old";
                // A leftover from an update that could not clean up yet
                remove(parked.c_str());
                for (int n = 1; fileExists(parked) && n < 100; ++n)
                    parked = t + ".mc2old" + std::to_string(n);
                if (!renameReplace(t, parked)) {
                    *error = "cannot move " + t + " aside";
                    goto undo;
                }
                moved.push_back({t, parked});
            }
            if (!renameReplace(t + ".mc2new", t)) {
                *error = "cannot replace " + t;
                goto undo;
            }
            placed.push_back(t);
        }
        if (oldFiles)
            for (const auto& m : moved)
                oldFiles->push_back(m.second);
        return true;
    undo:
        for (const std::string& p : placed)
            remove(p.c_str());
        for (const auto& m : moved)
            renameReplace(m.second, m.first);
        abandon();
        return false;
    }
};

} // namespace

//==============================================================================
// Zip (stored and deflated entries; what Python's zipfile writes)

static uint32_t le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }

bool extractZip(const std::string& archive, const std::string& destDir, bool stripTop,
                std::vector<std::string>* oldFiles, std::string* error)
{
    std::string err;
    FILE* f = fopen(archive.c_str(), "rb");
    if (!f) {
        if (error) *error = "cannot open " + archive;
        return false;
    }
    Staging st;
    st.dest = withSlash(destDir);
    bool ok = false;
    std::vector<unsigned char> tail;
    long size = 0;
    uint32_t cdOffset = 0, cdSize = 0;
    uint16_t count = 0;
    std::vector<unsigned char> cd;
    size_t pos = 0;
    // End of central directory record: in the last 64 KiB + 22 bytes
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 22) {
        err = "not a zip file";
        goto done;
    }
    {
        const long tailSize = size < 65557 ? size : 65557;
        tail.resize(tailSize);
        fseek(f, size - tailSize, SEEK_SET);
        if (fread(tail.data(), 1, tailSize, f) != (size_t)tailSize) {
            err = "read error";
            goto done;
        }
        long eocd = -1;
        for (long i = tailSize - 22; i >= 0; --i)
            if (le32(&tail[i]) == 0x06054b50) {
                eocd = i;
                break;
            }
        if (eocd < 0) {
            err = "not a zip file";
            goto done;
        }
        count = le16(&tail[eocd + 10]);
        cdSize = le32(&tail[eocd + 12]);
        cdOffset = le32(&tail[eocd + 16]);
        if (cdOffset == 0xffffffffu || (long)cdOffset + (long)cdSize > size) {
            err = "unsupported zip (zip64)";
            goto done;
        }
    }
    cd.resize(cdSize);
    fseek(f, cdOffset, SEEK_SET);
    if (fread(cd.data(), 1, cdSize, f) != cdSize) {
        err = "read error";
        goto done;
    }
    for (uint16_t i = 0; i < count; ++i) {
        if (pos + 46 > cd.size() || le32(&cd[pos]) != 0x02014b50) {
            err = "damaged zip directory";
            goto done;
        }
        const uint16_t method = le16(&cd[pos + 10]);
        const uint32_t csize = le32(&cd[pos + 20]);
        const uint32_t usize = le32(&cd[pos + 24]);
        const uint16_t nameLen = le16(&cd[pos + 28]);
        const uint16_t extraLen = le16(&cd[pos + 30]);
        const uint16_t commentLen = le16(&cd[pos + 32]);
        const uint32_t local = le32(&cd[pos + 42]);
        if (pos + 46 + nameLen > cd.size()) {
            err = "damaged zip directory";
            goto done;
        }
        std::string name((const char*)&cd[pos + 46], nameLen);
        pos += 46 + nameLen + extraLen + commentLen;
        if (name.empty() || name.back() == '/')
            continue; // folder
        if (!safeEntryName(name, stripTop))
            continue;
        if (csize == 0xffffffffu || usize == 0xffffffffu || local == 0xffffffffu) {
            err = "unsupported zip (zip64)";
            goto done;
        }
        if (method != 0 && method != 8) {
            err = "unsupported compression in " + name;
            goto done;
        }
        unsigned char lh[30];
        if (fseek(f, local, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30 || le32(lh) != 0x04034b50) {
            err = "damaged zip entry " + name;
            goto done;
        }
        fseek(f, local + 30 + le16(lh + 26) + le16(lh + 28), SEEK_SET);
        FILE* out = st.open(name, &err);
        if (!out)
            goto done;
        std::vector<unsigned char> in(64 * 1024), outBuf(256 * 1024);
        uint32_t left = csize, written = 0;
        bool entryOk = true;
        if (method == 0) {
            while (left && entryOk) {
                const size_t take = left < in.size() ? left : in.size();
                entryOk = fread(in.data(), 1, take, f) == take && fwrite(in.data(), 1, take, out) == take;
                left -= (uint32_t)take;
                written += (uint32_t)take;
            }
        } else {
            z_stream z;
            memset(&z, 0, sizeof(z));
            entryOk = inflateInit2(&z, -MAX_WBITS) == Z_OK;
            int rc = Z_OK;
            while (entryOk && rc != Z_STREAM_END) {
                if (z.avail_in == 0) {
                    if (!left) {
                        entryOk = false;
                        break;
                    }
                    const size_t take = left < in.size() ? left : in.size();
                    if (fread(in.data(), 1, take, f) != take) {
                        entryOk = false;
                        break;
                    }
                    left -= (uint32_t)take;
                    z.next_in = in.data();
                    z.avail_in = (uInt)take;
                }
                z.next_out = outBuf.data();
                z.avail_out = (uInt)outBuf.size();
                rc = inflate(&z, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END) {
                    entryOk = false;
                    break;
                }
                const size_t have = outBuf.size() - z.avail_out;
                if (fwrite(outBuf.data(), 1, have, out) != have)
                    entryOk = false;
                written += (uint32_t)have;
            }
            inflateEnd(&z);
        }
        if (fclose(out) != 0)
            entryOk = false;
        if (!entryOk || written != usize) {
            err = "cannot unpack " + name;
            goto done;
        }
    }
    ok = st.commit(oldFiles, &err);
done:
    fclose(f);
    if (!ok) {
        st.abandon();
        if (error) *error = err;
    }
    return ok;
}

//==============================================================================
// tar.gz (ustar, GNU long names and pax headers; what Python's tarfile writes)

namespace {

struct GzReader {
    gzFile gz;
    bool read(void* p, size_t n)
    {
        char* c = (char*)p;
        while (n) {
            const int got = gzread(gz, c, (unsigned)(n > (1u << 30) ? (1u << 30) : n));
            if (got <= 0)
                return false;
            c += got;
            n -= (size_t)got;
        }
        return true;
    }
    bool skip(uint64_t n)
    {
        char buf[16384];
        while (n) {
            const size_t take = n < sizeof(buf) ? (size_t)n : sizeof(buf);
            if (!read(buf, take))
                return false;
            n -= take;
        }
        return true;
    }
};

uint64_t octal(const char* p, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n && p[i]; ++i) {
        if (p[i] == ' ')
            continue;
        if (p[i] < '0' || p[i] > '7')
            break;
        v = v * 8 + (uint64_t)(p[i] - '0');
    }
    return v;
}

std::string field(const char* p, size_t n)
{
    size_t len = 0;
    while (len < n && p[len])
        ++len;
    return std::string(p, len);
}

// "<len> key=value\n" records
void parsePax(const std::string& data, std::string* path, std::string* link)
{
    size_t pos = 0;
    while (pos < data.size()) {
        const size_t sp = data.find(' ', pos);
        if (sp == std::string::npos)
            break;
        const size_t len = (size_t)strtoul(data.substr(pos, sp - pos).c_str(), nullptr, 10);
        if (len == 0 || pos + len > data.size())
            break;
        const std::string rec = data.substr(sp + 1, pos + len - sp - 2);
        const size_t eq = rec.find('=');
        if (eq != std::string::npos) {
            const std::string k = rec.substr(0, eq), v = rec.substr(eq + 1);
            if (k == "path") *path = v;
            else if (k == "linkpath") *link = v;
        }
        pos += len;
    }
}

} // namespace

bool extractTarGz(const std::string& archive, const std::string& destDir, bool stripTop,
                  std::vector<std::string>* oldFiles, std::string* error)
{
    gzFile gz = gzopen(archive.c_str(), "rb");
    if (!gz) {
        if (error) *error = "cannot open " + archive;
        return false;
    }
    gzbuffer(gz, 256 * 1024);
    GzReader r{gz};
    Staging st;
    st.dest = withSlash(destDir);
    std::string err;
    bool ok = false;
    std::string longName, longLink;
    std::vector<char> buf(256 * 1024);
    for (;;) {
        char h[512];
        if (!r.read(h, 512)) {
            err = "truncated archive";
            goto done;
        }
        bool zero = true;
        for (char c : h)
            zero = zero && c == 0;
        if (zero)
            break; // end of archive
        const uint64_t size = octal(h + 124, 12);
        const char type = h[156];
        const uint64_t padded = (size + 511) & ~(uint64_t)511;
        if (type == 'L' || type == 'K' || type == 'x') {
            if (size > (1 << 20)) {
                err = "bad archive header";
                goto done;
            }
            std::string data((size_t)size, '\0');
            if (!r.read(&data[0], (size_t)size) || !r.skip(padded - size)) {
                err = "truncated archive";
                goto done;
            }
            if (type == 'L') longName = field(data.data(), data.size());
            else if (type == 'K') longLink = field(data.data(), data.size());
            else parsePax(data, &longName, &longLink);
            continue;
        }
        std::string name = field(h, 100);
        const std::string prefix = memcmp(h + 257, "ustar", 5) == 0 ? field(h + 345, 155) : std::string();
        if (!prefix.empty())
            name = prefix + "/" + name;
        if (!longName.empty())
            name = longName;
        std::string link = longLink.empty() ? field(h + 157, 100) : longLink;
        longName.clear();
        longLink.clear();
        const bool regular = type == '0' || type == 0 || type == '7';
        const bool keep = (regular || type == '2') && safeEntryName(name, stripTop);
        if (!keep) {
            if (!r.skip(padded)) {
                err = "truncated archive";
                goto done;
            }
            continue;
        }
        if (type == '2') {
            if (!st.addSymlink(name, link, &err) || !r.skip(padded))
                goto done;
            continue;
        }
        FILE* out = st.open(name, &err);
        if (!out)
            goto done;
        uint64_t left = size;
        bool entryOk = true;
        while (left && entryOk) {
            const size_t take = left < buf.size() ? (size_t)left : buf.size();
            entryOk = r.read(buf.data(), take) && fwrite(buf.data(), 1, take, out) == take;
            left -= take;
        }
        if (fclose(out) != 0)
            entryOk = false;
        if (!entryOk || !r.skip(padded - size)) {
            err = "cannot unpack " + name;
            goto done;
        }
#ifndef _WIN32
        chmod((st.target(name) + ".mc2new").c_str(), (mode_t)(octal(h + 100, 8) & 0777));
#endif
    }
    ok = st.commit(oldFiles, &err);
done:
    gzclose(gz);
    if (!ok) {
        st.abandon();
        if (error) *error = err;
    }
    return ok;
}

//==============================================================================
// Download and install

namespace {

struct Installer {
    std::mutex m;
    DownloadStatus status = DownloadStatus::Idle;
    std::string error;
    std::string version;
    std::string file;     // the verified package
    InstallKind kind = InstallKind::None;
    std::string root;
    std::atomic<int64_t> received{0};
    int64_t total = 0;
    std::atomic<bool> cancel{false};
    // launchPendingRestart
    std::vector<std::string> args;
    std::string relaunch; // executable to start after shutdown
    bool restart = false;
};

Installer& installer()
{
    static Installer* i = new Installer;
    return *i;
}

std::string exeDir()
{
#ifdef _WIN32
    char path[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameA(nullptr, path, sizeof(path));
    std::string p(path, n);
#elif defined(__APPLE__)
    char path[4096];
    uint32_t size = sizeof(path);
    std::string p = _NSGetExecutablePath(path, &size) == 0 ? std::string(path) : std::string();
#else
    char path[4096];
    const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    std::string p = n > 0 ? std::string(path, (size_t)n) : std::string();
#endif
    const size_t slash = p.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : p.substr(0, slash + 1);
}

std::string appImagePath()
{
    const char* a = getenv("APPIMAGE");
    return a && *a ? std::string(a) : std::string();
}

// Downloads may come from GitHub release assets only (or the test server).
bool trustedUrl(const std::string& url)
{
    if (usingTestServer())
        return true;
    const char* prefix = "https://github.com/hyouka0904/mc2/releases/download/";
    return url.compare(0, strlen(prefix), prefix) == 0;
}

bool folderWritable(const std::string& dir)
{
    const std::string probe = withSlash(dir) + ".mc2-write-test";
    FILE* f = fopen(probe.c_str(), "wb");
    if (!f)
        return false;
    fclose(f);
    remove(probe.c_str());
    return true;
}

void fail(const std::string& why)
{
    Installer& in = installer();
    fprintf(stderr, "update: %s\n", why.c_str());
    std::lock_guard<std::mutex> lock(in.m);
    in.status = DownloadStatus::Failure;
    in.error = why;
}

void downloadThread(Release rel, Asset asset, Asset sums, std::string dir)
{
    Installer& in = installer();
    makeDirs(dir);
    const std::string target = withSlash(dir) + asset.name;
    const std::string part = target + ".part";
    std::string sumsText, err;
    if (!httpGet(sums.url, &sumsText, &err, 30)) {
        fail("SHA256SUMS.txt: " + err);
        return;
    }
    std::string expected;
    if (!findChecksum(sumsText, asset.name, &expected)) {
        fail(asset.name + " is not in SHA256SUMS.txt");
        return;
    }
    std::string actual;
    // Downloaded before but not installed: use it again
    if (sha256File(target, &actual) && actual == expected) {
        std::lock_guard<std::mutex> lock(in.m);
        in.status = DownloadStatus::Ready;
        in.file = target;
        in.version = rel.version;
        return;
    }
    fprintf(stderr, "update: downloading %s\n", asset.url.c_str());
    if (!httpDownload(asset.url, part, &in.received, &in.cancel, &err)) {
        fail("download: " + err);
        return;
    }
    if (!sha256File(part, &actual) || actual != expected) {
        remove(part.c_str());
        fail(asset.name + ": checksum does not match");
        return;
    }
    if (!renameReplace(part, target)) {
        fail("cannot write " + target);
        return;
    }
    fprintf(stderr, "update: %s downloaded and verified\n", asset.name.c_str());
    std::lock_guard<std::mutex> lock(in.m);
    in.status = DownloadStatus::Ready;
    in.file = target;
    in.version = rel.version;
}

std::vector<std::string> listDir(const std::string& dir)
{
    std::vector<std::string> names;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((withSlash(dir) + "*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                names.push_back(fd.cFileName);
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d))
            if (e->d_name[0] != '.')
                names.push_back(e->d_name);
        closedir(d);
    }
#endif
    return names;
}

std::string cleanupListPath()
{
    const std::string dir = updatesDir();
    return dir.empty() ? std::string() : withSlash(dir) + "cleanup.txt";
}

void rememberForCleanup(const std::vector<std::string>& files)
{
    const std::string list = cleanupListPath();
    if (list.empty())
        return;
    std::ofstream o(list.c_str(), std::ios::app);
    for (const std::string& f : files)
        o << f << "\n";
}

#ifdef _WIN32
std::wstring widenPath(const std::string& s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Windows command-line quoting (CommandLineToArgvW rules)
std::string quoteArg(const std::string& a)
{
    if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos)
        return a;
    std::string out = "\"";
    size_t bs = 0;
    for (char c : a) {
        if (c == '\\') {
            ++bs;
            continue;
        }
        if (c == '"')
            out.append(bs * 2 + 1, '\\');
        else
            out.append(bs, '\\');
        bs = 0;
        out += c;
    }
    out.append(bs * 2, '\\');
    return out + "\"";
}

bool startProcess(const std::string& exe, const std::vector<std::string>& args)
{
    std::string cmd = quoteArg(exe);
    for (const std::string& a : args)
        cmd += " " + (a.compare(0, 5, "/DIR=") == 0 ? a : quoteArg(a));
    std::wstring wcmd = widenPath(cmd);
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(widenPath(exe).c_str(), &wcmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
#else
bool startProcess(const std::string& exe, const std::vector<std::string>& args)
{
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(exe.c_str()));
    for (const std::string& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid;
    return posix_spawn(&pid, exe.c_str(), nullptr, nullptr, argv.data(), environ) == 0;
}
#endif

} // namespace

bool canInstall()
{
    if (status() != Status::Available)
        return false;
    const Release rel = available();
    std::string root;
    const InstallKind kind = detectInstallKind(exeDir(), appImagePath(), &root);
    Asset a;
    if (!pickAsset(rel, kind, &a) || !trustedUrl(a.url))
        return false;
    bool sums = false;
    for (const Asset& s : rel.assets)
        sums = sums || (s.name == "SHA256SUMS.txt" && trustedUrl(s.url));
    return sums;
}

void startDownload()
{
    Installer& in = installer();
    {
        std::lock_guard<std::mutex> lock(in.m);
        if (in.status == DownloadStatus::Downloading || in.status == DownloadStatus::Ready)
            return;
    }
    const Release rel = available();
    std::string root;
    const InstallKind kind = detectInstallKind(exeDir(), appImagePath(), &root);
    Asset asset, sums;
    bool haveSums = false;
    for (const Asset& s : rel.assets)
        if (s.name == "SHA256SUMS.txt") {
            sums = s;
            haveSums = true;
        }
    const std::string dir = updatesDir();
    if (rel.version.empty() || dir.empty() || !pickAsset(rel, kind, &asset) || !haveSums ||
        !trustedUrl(asset.url) || !trustedUrl(sums.url)) {
        fail("no package for this installation");
        return;
    }
    std::lock_guard<std::mutex> lock(in.m);
    in.status = DownloadStatus::Downloading;
    in.error.clear();
    in.kind = kind;
    in.root = root;
    in.total = asset.size;
    in.received = 0;
    in.cancel = false;
    std::thread(downloadThread, rel, asset, sums, dir).detach();
}

DownloadStatus downloadStatus()
{
    Installer& in = installer();
    std::lock_guard<std::mutex> lock(in.m);
    return in.status;
}

float downloadProgress()
{
    Installer& in = installer();
    std::lock_guard<std::mutex> lock(in.m);
    if (in.status == DownloadStatus::Ready)
        return 1.f;
    if (in.total <= 0)
        return 0.f;
    const float p = (float)in.received.load() / (float)in.total;
    return p < 0.f ? 0.f : p > 1.f ? 1.f : p;
}

std::string downloadError()
{
    Installer& in = installer();
    std::lock_guard<std::mutex> lock(in.m);
    return in.error;
}

bool installDownloaded(std::string* error)
{
    Installer& in = installer();
    std::unique_lock<std::mutex> lock(in.m);
    if (in.status != DownloadStatus::Ready) {
        if (error) *error = "nothing downloaded";
        return false;
    }
    const std::string file = in.file, root = in.root;
    const InstallKind kind = in.kind;
    lock.unlock();
    std::string err;
    std::vector<std::string> old;
    fprintf(stderr, "update: installing %s\n", file.c_str());
    switch (kind) {
    case InstallKind::WindowsInstaller: {
        // The installer waits for the game to close (Restart Manager) and
        // starts it again (/MC2RELAUNCH=1, see mc2.iss). Program Files needs
        // an administrator: /ALLUSERS lets the installer ask (UAC).
        std::string dir = root;
        while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/'))
            dir.pop_back();
        std::vector<std::string> args = {"/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
                                         "/CLOSEAPPLICATIONS", "/MC2RELAUNCH=1", "/DIR=\"" + dir + "\""};
        args.push_back(folderWritable(root) ? "/CURRENTUSER" : "/ALLUSERS");
        if (!startProcess(file, args)) {
            if (error) *error = "cannot start the installer";
            return false;
        }
        return true; // quit now; the installer starts the game again
    }
    case InstallKind::WindowsPortable:
    case InstallKind::LinuxTarball: {
        if (!folderWritable(root)) {
            if (error) *error = "the game folder is read-only";
            return false;
        }
        const bool ok = kind == InstallKind::WindowsPortable ? extractZip(file, root, true, &old, &err)
                                                             : extractTarGz(file, root, true, &old, &err);
        if (!ok) {
            if (error) *error = err;
            return false;
        }
        rememberForCleanup(old);
        lock.lock();
#ifdef _WIN32
        in.relaunch = withSlash(root) + "mc2.exe";
#else
        in.relaunch = withSlash(root) + "usr/bin/mc2";
#endif
        in.restart = true;
        return true;
    }
    case InstallKind::LinuxAppImage: {
        const size_t slash = root.find_last_of('/');
        const std::string dir = slash == std::string::npos ? std::string(".") : root.substr(0, slash);
        if (!folderWritable(dir)) {
            if (error) *error = "the AppImage folder is read-only";
            return false;
        }
        const std::string tmp = root + ".mc2new";
        if (!copyFile(file, tmp)) {
            if (error) *error = "cannot write " + tmp;
            return false;
        }
#ifndef _WIN32
        chmod(tmp.c_str(), 0755);
#endif
        if (!renameReplace(tmp, root)) {
            remove(tmp.c_str());
            if (error) *error = "cannot replace " + root;
            return false;
        }
        remove(file.c_str());
        lock.lock();
        in.relaunch = root;
        in.restart = true;
        return true;
    }
    case InstallKind::MacDmg: {
        std::vector<std::string> args = {file};
        if (!startProcess("/usr/bin/open", args)) {
            if (error) *error = "cannot open " + file;
            return false;
        }
        return false; // keep running; the player copies the app
    }
    default:
        if (error) *error = "unknown installation";
        return false;
    }
}

void setLaunchCommand(int argc, char** argv)
{
    Installer& in = installer();
    std::lock_guard<std::mutex> lock(in.m);
    in.args.clear();
    for (int i = 1; i < argc; ++i)
        in.args.push_back(argv[i]);
}

void cleanupAfterUpdate(const std::string& currentVersion)
{
    const std::string dir = updatesDir();
    if (dir.empty())
        return;
    const std::string list = cleanupListPath();
    std::vector<std::string> keep;
    {
        std::ifstream inList(list.c_str());
        std::string line;
        while (std::getline(inList, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            // Only files this code renamed
            if (line.find(".mc2old") == std::string::npos)
                continue;
            if (remove(line.c_str()) != 0 && fileExists(line))
                keep.push_back(line); // still in use (Windows): next time
        }
    }
    if (keep.empty()) {
        remove(list.c_str());
    } else {
        std::ofstream o(list.c_str(), std::ios::trunc);
        for (const std::string& k : keep)
            o << k << "\n";
    }
    // Downloaded packages are not needed once the game starts again (the
    // Windows installer may still be finishing: removing it fails then, and
    // it goes next time). Only the package names the release has.
    // Unfinished downloads and packages of this version or older go; a newer
    // one that was downloaded but not installed is used again.
    Version cur;
    const bool curOk = parseVersion(currentVersion, &cur);
    for (const std::string& name : listDir(dir)) {
        if (name.compare(0, 15, "MechCommander2-") != 0)
            continue;
        bool drop = name.size() > 5 && name.compare(name.size() - 5, 5, ".part") == 0;
        std::string ver;
        for (const char* platform : {"-windows-", "-linux-", "-macos-"}) {
            const size_t at = name.find(platform, 15);
            if (at != std::string::npos)
                ver = name.substr(15, at - 15);
        }
        Version v;
        if (!drop && (!curOk || !parseVersion(ver, &v) || compareVersions(v, cur) <= 0))
            drop = true;
        if (drop)
            remove((withSlash(dir) + name).c_str());
    }
}

void launchPendingRestart()
{
    Installer& in = installer();
    std::lock_guard<std::mutex> lock(in.m);
    if (!in.restart || in.relaunch.empty())
        return;
    in.restart = false;
    fprintf(stderr, "update: starting %s\n", in.relaunch.c_str());
    if (!startProcess(in.relaunch, in.args))
        fprintf(stderr, "update: cannot start %s\n", in.relaunch.c_str());
}

} // namespace gos_update
