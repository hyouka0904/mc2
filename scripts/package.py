#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Builds the downloadable packages of the game.

    package.py --platform linux|windows|macos --build <build_dir>
               --game <game_dir> --version <version> --out <dist_dir>
               [--shaders <shaders_out>] [--config RelWithDebInfo]
               [--linuxdeploy <linuxdeploy.AppImage>] [--iscc <ISCC.exe>]

<game_dir> is a game directory made by scripts/prepare_game_dir.sh; only its
data (data/, assets/, *.fst, *.cfg, testtxm.tga) is used, so one data build
serves every platform. The executables come from <build_dir>. The compiled
shaders (SPIR-V / MSL / HLSL) are the same on every platform: --shaders
takes them from another build (default <build_dir>/shaders_out).

Output in <dist_dir> (<v> = version):
  linux    MechCommander2-<v>-linux-x86_64.AppImage (with --linuxdeploy)
           MechCommander2-<v>-linux-x86_64.tar.gz
  windows  MechCommander2-<v>-windows-x64-setup.exe (with Inno Setup)
           MechCommander2-<v>-windows-x64.zip
  macos    MechCommander2-<v>-macos-arm64.dmg

Part updates (see GameOS/gameos/update/gos_update.h), in <dist_dir>/parts:
  linux    MechCommander2-<v>-data-<name>.zip    the game data, in a few parts
                                                 (the same on every platform)
           MechCommander2-<v>-linux-x86_64-program.zip   everything else
           MechCommander2-<v>-linux-x86_64-manifest.json (for the tarball)
  windows  MechCommander2-<v>-windows-x64-program.zip
           MechCommander2-<v>-windows-x64-manifest.json  (installer and zip)
The Linux run writes the data parts and --write-data-parts <json> (their
files and hashes); the Windows run reads that file (--data-parts) and checks
that its data is the same.

Layouts (the game finds its data next to the executable, in
../share/mechcommander2, or in the .app's Resources):
  linux    usr/bin/{mc2,libmc2res.so}, usr/lib/ (bundled libraries),
           usr/share/mechcommander2/<data>; the tarball adds ./mc2
  windows  mc2.exe, mc2res_64.dll, Visual C++ runtime DLLs, <data>
  macos    MechCommander 2.app/Contents/Resources/<data>
"""

import argparse
import glob
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import tarfile
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG = os.path.join(REPO, "packaging")
NAME = "MechCommander2"
APP_NAME = "MechCommander 2"

# What a game directory holds besides the executables and shaders
DATA_DIRS = ["data", "assets"]
DATA_GLOBS = ["*.fst", "*.cfg", "testtxm.tga"]
# Written by older versions or the hardware check, never shipped
NOT_DATA = {"options.cfg", "orgprefs.cfg", "options.cfg.old", "sniff.dat"}


def log(msg):
    print("package: " + msg, flush=True)


def run(cmd, **kw):
    log("$ " + " ".join(cmd))
    subprocess.run(cmd, check=True, **kw)


def copy_payload(game_dir, shaders_out, dest):
    """Game data, shaders, licenses and the player README into dest."""
    os.makedirs(dest, exist_ok=True)
    for d in DATA_DIRS:
        src = os.path.join(game_dir, d)
        if not os.path.isdir(src):
            sys.exit("%s is missing: build the game data first" % src)
        shutil.copytree(src, os.path.join(dest, d), dirs_exist_ok=True)
    for pattern in DATA_GLOBS:
        for f in glob.glob(os.path.join(game_dir, pattern)):
            if os.path.basename(f).lower() in NOT_DATA:
                continue
            shutil.copy2(f, dest)
    if not glob.glob(os.path.join(game_dir, "*.fst")):
        sys.exit("no .fst files in %s" % game_dir)

    shaders = os.path.join(dest, "shaders")
    shutil.copytree(os.path.join(REPO, "shaders"), shaders, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("compiled"))
    if not os.path.isfile(os.path.join(shaders_out, "shaders.txt")):
        sys.exit("%s/shaders.txt is missing: build with glslangValidator and spirv-val" %
                 shaders_out)
    shutil.copytree(shaders_out, os.path.join(shaders, "compiled"), dirs_exist_ok=True)

    lic = os.path.join(dest, "licenses")
    os.makedirs(lic, exist_ok=True)
    for src, name in [("EULA.txt", "EULA.txt"), ("license.txt", "GPL-3.0.txt"),
                      ("thirdparty/pl_mpeg/LICENSE", "pl_mpeg.txt"),
                      ("thirdparty/enet/LICENSE", "enet.txt")]:
        shutil.copy2(os.path.join(REPO, src), os.path.join(lic, name))
    shutil.copy2(os.path.join(PKG, "THIRD_PARTY.txt"), lic)
    shutil.copy2(os.path.join(PKG, "README.txt"), dest)


def copy_dep_licenses(build_dir, dest):
    """Licenses of the libraries the configure step downloaded (SDL3 etc.)."""
    lic = os.path.join(dest, "licenses")
    for f in glob.glob(os.path.join(build_dir, "_deps", "*-src", "LICENSE.txt")):
        name = os.path.basename(os.path.dirname(f))[:-len("-src")]
        shutil.copy2(f, os.path.join(lic, name + ".txt"))


def make_tar(src_dir, out_path):
    log("writing " + out_path)
    with tarfile.open(out_path, "w:gz") as t:
        t.add(src_dir, arcname=os.path.basename(src_dir))


def make_zip(src_dir, out_path):
    log("writing " + out_path)
    base = os.path.dirname(src_dir)
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(src_dir):
            for f in files:
                p = os.path.join(root, f)
                z.write(p, os.path.relpath(p, base))


# ---------------------------------------------------------------- Parts
def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def data_part_of(rel):
    """The data part of a file (path relative to the data folder), or None
    for the program part. Big archives that rarely change get their own part."""
    top = rel.split("/")[0]
    if rel == "textures.fst":
        return "textures"
    if rel == "tgl.fst":
        return "models"
    if "/" not in rel and rel.endswith(".fst"):
        return "archives"
    if rel.startswith("data/sound/"):
        return "sound"
    if rel.startswith("data/movies/"):
        return "movies"
    if top in DATA_DIRS:
        return "misc"
    if "/" not in rel and (rel.endswith(".cfg") or rel == "testtxm.tga"):
        return "misc"
    return None


def tree_files(root):
    """Regular files below root (relative, '/'), sorted; symlinks are left
    out (the part zips cannot hold them; they stay from the first install)."""
    out = []
    for d, dirs, files in os.walk(root):
        dirs.sort()
        for f in files:
            p = os.path.join(d, f)
            if os.path.islink(p) or not os.path.isfile(p):
                continue
            out.append(os.path.relpath(p, root).replace(os.sep, "/"))
    return sorted(out)


def write_part_zip(out_path, entries):
    """entries: [(zip name, source path)]. Fixed dates and order, so the same
    files give the same zip."""
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        for name, src in sorted(entries):
            info = zipfile.ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o755 if os.access(src, os.X_OK) else 0o644) << 16
            with open(src, "rb") as f:
                z.writestr(info, f.read())


def file_entry(path, rel, windows):
    return [rel, os.path.getsize(path), sha256_file(path),
            0 if windows else int(os.access(path, os.X_OK))]


def write_parts(a, tree, data_rel, platform):
    """Splits the package tree into parts in <out>/parts and writes the
    manifest. data_rel: the data folder inside the tree ("" on Windows)."""
    parts_dir = os.path.join(a.out, "parts")
    os.makedirs(parts_dir, exist_ok=True)
    windows = platform.startswith("windows")
    prefix = data_rel + "/" if data_rel else ""
    data = {}     # part name -> [(rel in data folder, path)]
    program = []  # (rel in tree, path)
    for rel in tree_files(tree):
        path = os.path.join(tree, rel)
        part = data_part_of(rel[len(prefix):]) if rel.startswith(prefix) else None
        if part:
            data.setdefault(part, []).append((rel[len(prefix):], path))
        else:
            program.append((rel, path))

    if a.write_data_parts:
        meta = []
        for name in sorted(data):
            zname = "%s-%s-data-%s.zip" % (NAME, a.version, name)
            zpath = os.path.join(parts_dir, zname)
            log("writing " + zpath)
            write_part_zip(zpath, data[name])
            meta.append({"name": zname, "size": os.path.getsize(zpath), "sha256": sha256_file(zpath),
                         "files": [file_entry(p, r, True)[:3] for r, p in sorted(data[name])]})
        with open(a.write_data_parts, "w") as f:
            json.dump(meta, f, indent=0)
    elif a.data_parts:
        meta = json.load(open(a.data_parts))
    else:
        sys.exit("parts: --write-data-parts or --data-parts is needed")

    # The data must be what the data parts hold
    mine = {r: p for files in data.values() for r, p in files}
    theirs = {f[0]: f for m in meta for f in m["files"]}
    if set(mine) != set(theirs):
        sys.exit("parts: the data files differ from the data parts (%s)" %
                 sorted(set(mine) ^ set(theirs))[:5])
    for r, p in mine.items():
        if os.path.getsize(p) != theirs[r][1] or sha256_file(p) != theirs[r][2]:
            sys.exit("parts: %s differs from the data parts" % r)

    pname = "%s-%s-%s-program.zip" % (NAME, a.version, platform)
    ppath = os.path.join(parts_dir, pname)
    log("writing " + ppath)
    write_part_zip(ppath, program)
    manifest = {"format": 1, "version": a.version, "parts": [
        {"name": pname, "size": os.path.getsize(ppath), "sha256": sha256_file(ppath), "dest": "",
         "files": [file_entry(p, r, windows) for r, p in sorted(program)]}]}
    for m in meta:
        files = [f + [0] for f in m["files"]]
        manifest["parts"].append({"name": m["name"], "size": m["size"], "sha256": m["sha256"],
                                  "dest": prefix, "files": files})
    mpath = os.path.join(parts_dir, "%s-%s-%s-manifest.json" % (NAME, a.version, platform))
    with open(mpath, "w") as f:
        json.dump(manifest, f, separators=(",", ":"))
    log("wrote %s (%d parts)" % (mpath, len(manifest["parts"])))


def chmod_x(path):
    os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


# ---------------------------------------------------------------- Linux
def package_linux(a, work):
    appdir = os.path.join(work, "%s-%s-linux-x86_64" % (NAME, a.version))
    bindir = os.path.join(appdir, "usr", "bin")
    share = os.path.join(appdir, "usr", "share")
    os.makedirs(bindir)
    shutil.copy2(os.path.join(a.build, "mc2"), bindir)
    shutil.copy2(os.path.join(a.build, "out", "res", "libmc2res_64.so"),
                 os.path.join(bindir, "libmc2res.so"))
    # Without the debug information (67 MB -> 9 MB; function names stay for
    # backtraces): smaller packages and program parts
    if shutil.which("strip"):
        run(["strip", "--strip-debug", os.path.join(bindir, "mc2"), os.path.join(bindir, "libmc2res.so")])
    copy_payload(a.game, a.shaders, os.path.join(share, "mechcommander2"))
    copy_dep_licenses(a.build, os.path.join(share, "mechcommander2"))
    os.makedirs(os.path.join(share, "applications"))
    desktop = os.path.join(share, "applications", "mechcommander2.desktop")
    shutil.copy2(os.path.join(PKG, "linux", "mechcommander2.desktop"), desktop)
    icons = os.path.join(share, "icons", "hicolor", "512x512", "apps")
    os.makedirs(icons)
    icon = os.path.join(icons, "mechcommander2.png")
    shutil.copy2(os.path.join(PKG, "icon.png"), icon)

    if a.linuxdeploy:
        # Copies the libraries mc2 needs that are not on every system (GLEW,
        # ...) into usr/lib and points mc2's RPATH there; OpenGL, X11, libc
        # and other system libraries stay outside.
        out = os.path.join(a.out, "%s-%s-linux-x86_64.AppImage" % (NAME, a.version))
        env = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1", ARCH="x86_64",
                   LDAI_OUTPUT=out, OUTPUT=out, VERSION=a.version)
        run([a.linuxdeploy, "--appdir", appdir, "--executable", os.path.join(bindir, "mc2"),
             "--desktop-file", desktop, "--icon-file", icon, "--output", "appimage"],
            env=env, cwd=work)
        if not os.path.isfile(out):
            sys.exit("linuxdeploy did not write " + out)
        log("wrote " + out)
    else:
        log("no --linuxdeploy: skipping the AppImage (the tarball needs the system's GLEW)")

    # Tarball: the same tree with a launcher at the top
    launcher = os.path.join(appdir, "mc2")
    with open(launcher, "w") as f:
        f.write('#!/bin/sh\nexec "$(dirname "$(readlink -f "$0")")/usr/bin/mc2" "$@"\n')
    chmod_x(launcher)
    shutil.copy2(os.path.join(PKG, "README.txt"), appdir)
    make_tar(appdir, os.path.join(a.out, os.path.basename(appdir) + ".tar.gz"))
    if a.parts:
        write_parts(a, appdir, "usr/share/mechcommander2", "linux-x86_64")


# ---------------------------------------------------------------- Windows
def package_windows(a, work):
    stage = os.path.join(work, "%s-%s-windows-x64" % (NAME, a.version))
    os.makedirs(stage)
    shutil.copy2(os.path.join(a.build, a.config, "mc2.exe"), stage)
    shutil.copy2(os.path.join(a.build, "out", "res", a.config, "mc2res_64.dll"), stage)
    runtime = os.path.join(a.build, "runtime_libs.txt")
    if os.path.isfile(runtime):
        for line in open(runtime):
            dll = line.strip()
            if dll:
                shutil.copy2(dll, stage)
                log("runtime " + os.path.basename(dll))
    else:
        log("warning: no runtime_libs.txt, the Visual C++ runtime is not included")
    copy_payload(a.game, a.shaders, stage)
    copy_dep_licenses(a.build, stage)
    make_zip(stage, os.path.join(a.out, os.path.basename(stage) + ".zip"))
    if a.parts:
        write_parts(a, stage, "", "windows-x64")

    iscc = a.iscc or shutil.which("iscc") or shutil.which("ISCC")
    if not iscc:
        for p in [r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
                  r"C:\Program Files\Inno Setup 6\ISCC.exe"]:
            if os.path.isfile(p):
                iscc = p
    if not iscc:
        log("Inno Setup (ISCC) not found: skipping the installer")
        return
    run([iscc, "/Q", "/DAppVersion=" + a.version, "/DSourceDir=" + stage,
         "/DOutputDir=" + os.path.abspath(a.out),
         "/DOutputName=%s-%s-windows-x64-setup" % (NAME, a.version),
         "/DIconFile=" + os.path.join(PKG, "icon.ico"),
         os.path.join(PKG, "windows", "mc2.iss")])


# ---------------------------------------------------------------- macOS
def package_macos(a, work):
    stage = os.path.join(work, "dmg")
    os.makedirs(stage)
    app = os.path.join(stage, APP_NAME + ".app")
    shutil.copytree(os.path.join(a.build, "mc2.app"), app, symlinks=True)
    res = os.path.join(app, "Contents", "Resources")
    copy_payload(a.game, a.shaders, res)
    copy_dep_licenses(a.build, res)

    # Icon: AppIcon.icns from icon.png (Info.plist names it)
    iconset = os.path.join(work, "AppIcon.iconset")
    os.makedirs(iconset)
    src = os.path.join(PKG, "icon.png")
    for size in [16, 32, 128, 256, 512]:
        for scale in [1, 2]:
            px = size * scale
            if px > 512:
                continue
            name = "icon_%dx%d%s.png" % (size, size, "@2x" if scale == 2 else "")
            run(["sips", "-z", str(px), str(px), src, "--out", os.path.join(iconset, name)],
                stdout=subprocess.DEVNULL)
    run(["iconutil", "-c", "icns", iconset, "-o", os.path.join(res, "AppIcon.icns")])

    # Adding files broke the linker's ad-hoc signature; Apple Silicon only
    # starts signed programs, so sign the whole bundle again (ad hoc: no
    # Apple developer certificate, so Gatekeeper still asks once).
    run(["codesign", "--force", "--deep", "--sign", "-", app])
    run(["codesign", "--verify", "--deep", "--strict", app])

    shutil.copy2(os.path.join(PKG, "README.txt"), stage)
    os.symlink("/Applications", os.path.join(stage, "Applications"))
    out = os.path.join(a.out, "%s-%s-macos-arm64.dmg" % (NAME, a.version))
    run(["hdiutil", "create", "-volname", APP_NAME, "-srcfolder", stage, "-ov",
         "-format", "UDZO", out])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--platform", required=True, choices=["linux", "windows", "macos"])
    ap.add_argument("--build", required=True, help="CMake build directory")
    ap.add_argument("--game", required=True, help="game directory with the built data")
    ap.add_argument("--version", required=True)
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--shaders", help="compiled shaders (default <build>/shaders_out)")
    ap.add_argument("--config", default="RelWithDebInfo",
                    help="Windows: build configuration subdirectory")
    ap.add_argument("--linuxdeploy", help="Linux: linuxdeploy executable, for the AppImage")
    ap.add_argument("--iscc", help="Windows: Inno Setup compiler (default: search)")
    ap.add_argument("--write-data-parts", metavar="JSON",
                    help="write the data parts and their file list to JSON (Linux)")
    ap.add_argument("--data-parts", metavar="JSON",
                    help="the data parts another run wrote (Windows)")
    a = ap.parse_args()
    a.version = a.version[1:] if a.version.startswith("v") else a.version
    a.build = os.path.abspath(a.build)
    a.game = os.path.abspath(a.game)
    a.shaders = os.path.abspath(a.shaders or os.path.join(a.build, "shaders_out"))
    a.out = os.path.abspath(a.out)
    if a.linuxdeploy:
        a.linuxdeploy = os.path.abspath(a.linuxdeploy)
    for k in ("write_data_parts", "data_parts"):
        if getattr(a, k):
            setattr(a, k, os.path.abspath(getattr(a, k)))
    a.parts = bool(a.write_data_parts or a.data_parts) and a.platform != "macos"
    os.makedirs(a.out, exist_ok=True)
    work = os.path.join(a.out, "_work")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    {"linux": package_linux, "windows": package_windows, "macos": package_macos}[a.platform](a, work)
    shutil.rmtree(work, ignore_errors=True)
    for f in sorted(os.listdir(a.out)):
        p = os.path.join(a.out, f)
        if os.path.isfile(p):
            log("%-55s %7.1f MB" % (f, os.path.getsize(p) / 1e6))


if __name__ == "__main__":
    main()
