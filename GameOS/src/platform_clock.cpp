// SPDX-License-Identifier: GPL-3.0-only
#include "platform_clock.h"

#include <chrono>

namespace platform_clock {

namespace {
uint32_t g_step_us = 0;
// Virtual time starts away from zero: game code uses 0 as "never happened".
const uint64_t kVirtualStartNs = 1000ull * 1000000000ull;
uint64_t g_virtual_ns = kVirtualStartNs;
}

void set_fixed_step(uint32_t step_us)
{
    g_step_us = step_us;
    g_virtual_ns = kVirtualStartNs;
}

bool is_fixed() { return g_step_us != 0; }

uint32_t fixed_step_us() { return g_step_us; }

void advance_frame()
{
    if (g_step_us)
        g_virtual_ns += (uint64_t)g_step_us * 1000u;
}

uint64_t now_ns()
{
    if (g_step_us)
        return g_virtual_ns;
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace platform_clock
