// SPDX-License-Identifier: GPL-3.0-only
#ifndef GAMEOS_RUN_OPTIONS_H
#define GAMEOS_RUN_OPTIONS_H

// Command line options handled by GameOS itself (the game parses its own
// options separately and ignores these):
//
//   -fixedstep <ms>          deterministic mode: every frame advances the clock
//                            by exactly <ms> milliseconds and frameRate is
//                            1000/<ms>; the main loop does not sleep
//   -seed <n>                fixed random seed (replaces time based seeds)
//   -capture-frames a,b,c    save the back buffer of these frame numbers
//                            (1-based) as PNG files
//   -capture-dir <dir>       where captures go (default: current directory)
//   -exit-after <n>          quit after frame <n> (default: last capture frame)
//   -input-script <file>     replay mouse/keyboard events (utils/input_script.h)
//   -perf-csv <file>         write per-frame statistics (frame, wall-clock ms,
//                            draw calls, pipeline changes, distinct pipelines)
//   -mods <dir>              load the enabled mods from <dir> (default: ./mods
//                            if it exists)
//   -modpack <modpack.ini>   load exactly the mods a modpack lists
//   -renderer gl|gpu         graphics backend: SDL3 GPU (Vulkan / D3D12 /
//                            Metal) or OpenGL. Without it the game uses SDL3
//                            GPU and falls back to OpenGL if that cannot start
//   -no-batching             draw every gos_Draw* call on its own instead of
//                            merging consecutive calls with the same state
//   -gpu-shapes              experimental: also draw solid 3D model parts with
//                            gos_RenderShape (GPU transform and lighting; the
//                            lighting does not match the game yet)
//   -shader-reload           development: recompile shaders whose source files
//                            changed (checked twice a second)
//   -windowed                stay in a window even if the game asks for
//                            fullscreen (fullscreen depends on the window
//                            manager, which makes captures environment specific)

#include <stdint.h>
#include <string>
#include <vector>

namespace gos_run_options {

struct Options {
    uint32_t fixed_step_us = 0;     // 0 = real time
    bool has_seed = false;
    uint32_t seed = 0;
    std::vector<uint32_t> capture_frames; // sorted, unique
    std::string capture_dir = ".";
    uint32_t exit_after_frame = 0;  // 0 = never
    std::string input_script;       // empty = none
    bool windowed = false;
    bool shader_reload = false;
    bool batching = true;
    bool gpu_shapes = false;
    std::string renderer;           // "gl", "gpu" or empty (gpu, else gl)
    std::string perf_csv;           // empty = none
    std::string mods_dir;           // empty = ./mods if present
    std::string modpack;            // empty = none

    bool should_capture(uint32_t frame) const;
};

// Returns false and sets *error for malformed values. Unknown arguments are
// left for the game.
bool parse(int argc, const char* const* argv, Options& out, std::string* error);

// "capture_000300.png"
std::string capture_file_name(uint32_t frame);

} // namespace gos_run_options

#endif
