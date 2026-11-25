#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes a plan for `mc2 -apply-update <plan>` (CI tests of part updates).

    part_plan.py <parts dir> <install root> <platform> <plan file> <file>...
                 [--version V]

<parts dir> is what scripts/package.py wrote (dist/parts); <platform> is
linux-x86_64 or windows-x64. Each <file> is a path below the install root
(e.g. usr/bin/mc2); the plan puts it back from the part that holds it, the
way the game does after downloading the parts it needs. The format is read
by readApplyPlan in GameOS/gameos/update/gos_update_install.cpp.
"""

import argparse
import glob
import json
import os
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("parts")
    ap.add_argument("root")
    ap.add_argument("platform")
    ap.add_argument("plan")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--version", default="")
    a = ap.parse_args()
    manifests = glob.glob(os.path.join(a.parts, "*-%s-manifest.json" % a.platform))
    if len(manifests) != 1:
        sys.exit("expected one %s manifest in %s" % (a.platform, a.parts))
    m = json.load(open(manifests[0]))
    wanted = set(a.files)
    lines = ["mc2-apply-plan 1", "root=" + a.root, "version=" + a.version]
    for part in m["parts"]:
        files = [f for f in part["files"] if part["dest"] + f[0] in wanted]
        if not files:
            continue
        lines.append("part=%s|%s" % (os.path.abspath(os.path.join(a.parts, part["name"])), part["dest"]))
        for f in files:
            lines.append("file=%s|%d|%s|%d" % (f[0], f[1], f[2], f[3] if len(f) > 3 else 0))
            wanted.discard(part["dest"] + f[0])
    if wanted:
        sys.exit("not in the manifest: %s" % ", ".join(sorted(wanted)))
    with open(a.plan, "w", newline="\n") as o:
        o.write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
