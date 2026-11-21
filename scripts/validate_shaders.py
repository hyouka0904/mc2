#!/usr/bin/env python3
"""Compile and link every shader variant offline (no GPU needed).

    validate_shaders.py <shaders_dir>

Builds each vertex/fragment pair exactly as GameOS does at run time
(#version 420, optional "#define ALPHA_TEST", #include <...> inlined from
the shaders directory) and checks it with glslangValidator, including the
link step that matches vertex outputs with fragment inputs.

Exit status: 0 ok, 1 errors, 77 glslangValidator not installed (CTest skip).
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile

VARIANTS = {"base": "", "alpha_test": "#define ALPHA_TEST\n"}  # gameos_graphics.cpp g_shader_flags
INCLUDE = re.compile(r'^\s*#include\s*[<"]([^>"]+)[>"]', re.M)


def expand(path, root, seen=()):
    if path in seen:
        raise ValueError("recursive #include of %s" % path)
    with open(path) as f:
        text = f.read()
    def repl(m):
        # next to the including file first, then from the shaders root
        local = os.path.join(os.path.dirname(path), m.group(1))
        target = local if os.path.exists(local) else os.path.join(root, m.group(1))
        return expand(target, root, seen + (path,))
    return INCLUDE.sub(repl, text)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    root = os.path.abspath(sys.argv[1])
    tool = shutil.which("glslangValidator")
    if not tool:
        print("glslangValidator not found (apt install glslang-tools): skipped")
        return 77

    programs = sorted(os.path.splitext(n)[0] for n in os.listdir(root) if n.endswith(".vert"))
    failures = 0
    checked = 0
    with tempfile.TemporaryDirectory() as tmp:
        for prog in programs:
            frag = os.path.join(root, prog + ".frag")
            if not os.path.exists(frag):
                print("FAIL %s: no matching .frag" % prog)
                failures += 1
                continue
            for vname, defines in VARIANTS.items():
                files = []
                for ext in ("vert", "frag"):
                    src = "#version 420\n" + defines + expand(os.path.join(root, "%s.%s" % (prog, ext)), root)
                    out = os.path.join(tmp, "%s_%s.%s" % (prog, vname, ext))
                    with open(out, "w") as f:
                        f.write(src)
                    files.append(out)
                r = subprocess.run([tool, "-l"] + files, capture_output=True, text=True)
                checked += 1
                if r.returncode != 0:
                    failures += 1
                    print("FAIL %s (%s):\n%s" % (prog, vname, (r.stdout + r.stderr).strip()))
                else:
                    print("ok   %s (%s)" % (prog, vname))
    print("%d of %d shader programs compile and link" % (checked - failures, checked))
    return 1 if failures or not checked else 0


if __name__ == "__main__":
    sys.exit(main())
