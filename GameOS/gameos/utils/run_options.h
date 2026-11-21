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

    bool should_capture(uint32_t frame) const;
};

// Returns false and sets *error for malformed values. Unknown arguments are
// left for the game.
bool parse(int argc, const char* const* argv, Options& out, std::string* error);

// "capture_000300.png"
std::string capture_file_name(uint32_t frame);

} // namespace gos_run_options

#endif
