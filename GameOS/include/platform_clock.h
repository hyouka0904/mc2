#ifndef PLATFORM_CLOCK_H
#define PLATFORM_CLOCK_H

// Single time source for GameOS (timeGetTime, gos_GetElapsedTime,
// gos_GetHiResTime).
//
// By default it reads the monotonic system clock. In fixed-step mode
// (command line -fixedstep <ms>) time only moves when the main loop calls
// advance_frame(), by exactly one step per frame. Together with a fixed
// random seed this makes a run reproducible frame by frame, which the
// screenshot comparison tests rely on.

#include <stdint.h>

namespace platform_clock {

// step_us == 0 restores real time.
void set_fixed_step(uint32_t step_us);
bool is_fixed();
uint32_t fixed_step_us();

// Fixed-step mode: advances virtual time by one step. No-op otherwise.
void advance_frame();

// Monotonic nanoseconds (virtual in fixed-step mode).
uint64_t now_ns();

} // namespace platform_clock

#endif
