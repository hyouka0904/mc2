#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the GameOS shaders for the non-OpenGL backends.

    build_shaders.py <shaders_dir> <out_dir> [--require msl,hlsl,dxil]

For every program (a .vert/.frag pair) and variant (base, alpha_test) this
  1. expands #include like GameOS does and compiles each stage to SPIR-V for
     Vulkan with glslangValidator (VULKAN and GOS_<STAGE>_SHADER defined),
  2. links the two stages (checks that vertex outputs match fragment inputs),
  3. validates the SPIR-V with spirv-val,
  4. converts it to Metal (MSL) and HLSL with spirv-cross, and the HLSL to
     DXIL with dxc when those tools are installed,
  5. records the resources of each stage (number of samplers and uniform
     buffers, which SDL_CreateGPUShader needs) in <out_dir>/shaders.json and
     <out_dir>/shaders.txt (read by the SDL3 GPU backend).

It also checks that the PerDraw uniform block has the layout of the C++
struct gos_render::PerDraw (GameOS/gameos/render/shader_params.h).

Output: <out_dir>/{spirv,msl,hlsl,dxil}/<program>.<variant>.<stage>.<ext>

Exit status: 0 ok, 1 errors, 77 glslangValidator or spirv-val missing
(CTest skip). A converter listed in --require that is missing is an error;
otherwise its output is skipped with a note.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

# gameos_graphics.cpp: g_shader_flags (each combination is a variant)
VARIANTS = {"base": [], "alpha_test": ["ALPHA_TEST"]}
STAGES = {"vert": ("vertex", "GOS_VERTEX_SHADER", "vs_6_0"),
          "frag": ("fragment", "GOS_FRAGMENT_SHADER", "ps_6_0")}
INCLUDE = re.compile(r'^\s*#include\s*[<"]([^>"]+)[>"]', re.M)

# Must match struct gos_render::PerDraw in render/shader_params.h (std140)
PER_DRAW_LAYOUT = [
    ("mvp", 0), ("projection_", 64), ("world_", 128), ("view_", 192), ("wvp_", 256),
    ("vp", 320), ("light_offset_", 336), ("fog_color", 352), ("Foreground", 368),
]
PER_DRAW_SIZE = 384


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


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode == 0, (r.stdout + r.stderr).strip()


def check_per_draw(reflection, where):
    """Returns error strings if the reflected PerDraw block is not as expected."""
    errors = []
    for ubo in reflection.get("ubos", []):
        if ubo.get("name") != "PerDraw":
            continue
        if ubo.get("block_size") != PER_DRAW_SIZE:
            errors.append("%s: PerDraw is %s bytes, expected %d" % (where, ubo.get("block_size"), PER_DRAW_SIZE))
        members = reflection["types"][ubo["type"]]["members"]
        got = [(m["name"], m["offset"]) for m in members]
        if got != PER_DRAW_LAYOUT:
            errors.append("%s: PerDraw layout %s differs from shader_params.h %s" % (where, got, PER_DRAW_LAYOUT))
    return errors


def main():
    args = sys.argv[1:]
    require = set()
    if "--require" in args:
        i = args.index("--require")
        require = set(x for x in args[i + 1].split(",") if x)
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__)
        return 2
    root, out = os.path.abspath(args[0]), os.path.abspath(args[1])

    glslang, spirv_val = shutil.which("glslangValidator"), shutil.which("spirv-val")
    if not glslang or not spirv_val:
        print("glslangValidator / spirv-val not found (apt install glslang-tools spirv-tools): skipped")
        return 77
    tools = {"msl": shutil.which("spirv-cross"), "hlsl": shutil.which("spirv-cross"), "dxil": shutil.which("dxc")}
    errors = []
    for fmt in ("msl", "hlsl", "dxil"):
        if not tools[fmt]:
            msg = "%s output needs %s, which is not installed" % (fmt, "dxc" if fmt == "dxil" else "spirv-cross")
            if fmt in require:
                errors.append(msg)
            else:
                print("note: " + msg + ": skipped")
    if tools["dxil"] and not tools["hlsl"]:
        tools["dxil"] = None

    for d in ("spirv", "msl", "hlsl", "dxil"):
        os.makedirs(os.path.join(out, d), exist_ok=True)

    programs = sorted(os.path.splitext(n)[0] for n in os.listdir(root) if n.endswith(".vert"))
    manifest = {"programs": {}}
    counts = {"spirv": 0, "msl": 0, "hlsl": 0, "dxil": 0}
    with tempfile.TemporaryDirectory() as tmp:
        for prog in programs:
            if not os.path.exists(os.path.join(root, prog + ".frag")):
                errors.append("%s: no matching .frag" % prog)
                continue
            for vname, defines in VARIANTS.items():
                base = "%s.%s" % (prog, vname)
                entry = {"defines": defines, "stages": {}}
                srcs = []
                for ext, (stage, stage_define, _) in STAGES.items():
                    header = "#version 450\n#define %s\n" % stage_define
                    header += "".join("#define %s 1\n" % d for d in defines)
                    src = os.path.join(tmp, "%s.%s" % (base, ext))
                    with open(src, "w") as f:
                        f.write(header + expand(os.path.join(root, "%s.%s" % (prog, ext)), root))
                    srcs.append(src)
                    spv = os.path.join(out, "spirv", "%s.%s.spv" % (base, ext))
                    ok, log = run([glslang, "-V", "--target-env", "vulkan1.0", "-o", spv, src])
                    if not ok:
                        errors.append("%s (%s): compile failed:\n%s" % (base, stage, log))
                        continue
                    ok, log = run([spirv_val, "--target-env", "vulkan1.0", spv])
                    if not ok:
                        errors.append("%s (%s): spirv-val: %s" % (base, stage, log))
                        continue
                    counts["spirv"] += 1

                    info = {"spirv": os.path.relpath(spv, out)}
                    if tools["msl"]:
                        ok, log = run([tools["msl"], spv, "--reflect"])
                        if ok:
                            refl = json.loads(log)
                            # slots, not counts: SDL GPU binds slot i at binding i
                            info["samplers"] = 1 + max([x["binding"] for x in refl.get("textures", [])] or [-1])
                            info["uniform_buffers"] = 1 + max([x["binding"] for x in refl.get("ubos", [])] or [-1])
                            info["entry_point"] = "main"
                            errors += check_per_draw(refl, "%s (%s)" % (base, stage))
                        else:
                            errors.append("%s (%s): reflection failed: %s" % (base, stage, log))
                        msl = os.path.join(out, "msl", "%s.%s.metal" % (base, ext))
                        ok, log = run([tools["msl"], "--msl", "--msl-version", "20000", "--output", msl, spv])
                        if ok:
                            info["msl"] = os.path.relpath(msl, out)
                            counts["msl"] += 1
                        else:
                            errors.append("%s (%s): MSL conversion failed: %s" % (base, stage, log))
                    if tools["hlsl"]:
                        hlsl = os.path.join(out, "hlsl", "%s.%s.hlsl" % (base, ext))
                        ok, log = run([tools["hlsl"], "--hlsl", "--shader-model", "60", "--hlsl-enable-compat", "--output", hlsl, spv])
                        if ok:
                            info["hlsl"] = os.path.relpath(hlsl, out)
                            counts["hlsl"] += 1
                            if tools["dxil"]:
                                dxil = os.path.join(out, "dxil", "%s.%s.dxil" % (base, ext))
                                ok, log = run([tools["dxil"], "-T", STAGES[ext][2], "-E", "main", "-Fo", dxil, hlsl])
                                if ok:
                                    info["dxil"] = os.path.relpath(dxil, out)
                                    counts["dxil"] += 1
                                else:
                                    errors.append("%s (%s): dxc failed: %s" % (base, stage, log))
                        else:
                            errors.append("%s (%s): HLSL conversion failed: %s" % (base, stage, log))
                    entry["stages"][stage] = info
                # cross-stage interface check
                ok, log = run([glslang, "-V", "--target-env", "vulkan1.0", "-l", "-o",
                               os.path.join(tmp, "linked.spv")] + srcs)
                if not ok:
                    errors.append("%s: link failed:\n%s" % (base, log))
                manifest["programs"][base] = entry

    with open(os.path.join(out, "shaders.json"), "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    # The same for the C++ side (render/gpu/gpu_device.cpp), one stage a line:
    #   <program>.<variant> <vertex|fragment> <sampler slots> <uniform buffer slots>
    with open(os.path.join(out, "shaders.txt"), "w") as f:
        for name in sorted(manifest["programs"]):
            for stage, info in sorted(manifest["programs"][name]["stages"].items()):
                if "samplers" in info:
                    f.write("%s %s %d %d\n" % (name, stage, info["samplers"], info["uniform_buffers"]))

    for e in errors:
        print("FAIL " + e)
    print("built %(spirv)d SPIR-V, %(msl)d MSL, %(hlsl)d HLSL, %(dxil)d DXIL shader stages" % counts)
    return 1 if errors or not counts["spirv"] else 0


if __name__ == "__main__":
    sys.exit(main())
