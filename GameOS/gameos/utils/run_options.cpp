// SPDX-License-Identifier: GPL-3.0-only
#include "run_options.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace gos_run_options {

namespace {

bool parse_uint(const char* s, uint32_t max_value, uint32_t& out)
{
    if (!s || !*s || *s == '-' || *s == '+')
        return false;
    errno = 0;
    char* end = nullptr;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || *end != '\0' || v > max_value)
        return false;
    out = (uint32_t)v;
    return true;
}

bool parse_frame_list(const char* s, std::vector<uint32_t>& out)
{
    std::string text(s ? s : "");
    if (text.empty())
        return false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t comma = text.find(',', pos);
        if (comma == std::string::npos)
            comma = text.size();
        uint32_t frame = 0;
        if (!parse_uint(text.substr(pos, comma - pos).c_str(), 0xFFFFFFFFu, frame) || frame == 0)
            return false;
        out.push_back(frame);
        pos = comma + 1;
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return true;
}

} // namespace

bool Options::should_capture(uint32_t frame) const
{
    return std::binary_search(capture_frames.begin(), capture_frames.end(), frame);
}

bool parse(int argc, const char* const* argv, Options& out, std::string* error)
{
    auto fail = [&](const char* opt, const char* why) {
        if (error)
            *error = std::string(opt) + ": " + why;
        return false;
    };

    bool explicit_exit = false;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* value = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (!strcmp(a, "-fixedstep")) {
            uint32_t ms = 0;
            if (!parse_uint(value, 1000, ms) || ms == 0)
                return fail(a, "expected milliseconds in 1..1000");
            out.fixed_step_us = ms * 1000;
            ++i;
        } else if (!strcmp(a, "-seed")) {
            if (!parse_uint(value, 0xFFFFFFFFu, out.seed))
                return fail(a, "expected an unsigned integer");
            out.has_seed = true;
            ++i;
        } else if (!strcmp(a, "-capture-frames")) {
            if (!parse_frame_list(value, out.capture_frames))
                return fail(a, "expected a comma separated list of frame numbers >= 1");
            ++i;
        } else if (!strcmp(a, "-capture-dir")) {
            if (!value || !*value)
                return fail(a, "expected a directory");
            out.capture_dir = value;
            ++i;
        } else if (!strcmp(a, "-mods") || !strcmp(a, "-modpack")) {
            if (!value || !*value)
                return fail(a, "expected a path");
            (a[4] == 'p' ? out.modpack : out.mods_dir) = value;
            ++i;
        } else if (!strcmp(a, "-datapath")) {
            if (!value || !*value)
                return fail(a, "expected a directory");
            out.data_path = value;
            ++i;
        } else if (!strcmp(a, "-perf-csv")) {
            if (!value || !*value)
                return fail(a, "expected a file name");
            out.perf_csv = value;
            ++i;
        } else if (!strcmp(a, "-version") || !strcmp(a, "--version")) {
            out.print_version = true;
        } else if (!strcmp(a, "-noupdatecheck")) {
            out.no_update_check = true;
        } else if (!strcmp(a, "-windowed")) {
            out.windowed = true;
        } else if (!strcmp(a, "-renderer")) {
            if (!value || (strcmp(value, "gl") && strcmp(value, "gpu")))
                return fail(a, "expected gl or gpu");
            out.renderer = value;
            ++i;
        } else if (!strcmp(a, "-no-batching")) {
            out.batching = false;
        } else if (!strcmp(a, "-gpu-shapes")) {
            out.gpu_shapes = true;
        } else if (!strcmp(a, "-shader-reload")) {
            out.shader_reload = true;
        } else if (!strcmp(a, "-input-script")) {
            if (!value || !*value)
                return fail(a, "expected a file name");
            out.input_script = value;
            ++i;
        } else if (!strcmp(a, "-exit-after")) {
            if (!parse_uint(value, 0xFFFFFFFFu, out.exit_after_frame) || out.exit_after_frame == 0)
                return fail(a, "expected a frame number >= 1");
            explicit_exit = true;
            ++i;
        }
    }
    if (!explicit_exit && !out.capture_frames.empty())
        out.exit_after_frame = out.capture_frames.back();
    return true;
}

std::string capture_file_name(uint32_t frame)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "capture_%06u.png", frame);
    return buf;
}

} // namespace gos_run_options
