// SPDX-License-Identifier: GPL-3.0-only
// Tests for the game's random helpers (mclib/routines.cpp). RollDice(p) must
// succeed about p percent of the time on every platform: it scales a 15-bit
// value, so it has to use gos_rand() rather than the C library rand(), whose
// range is 0..2^31-1 on Linux and made every roll fail.

#include "mc2test.h"

#include "gameos.hpp"

long RandomNumber(long range);
bool RollDice(long percent);

TEST(gos_rand_is_15_bit)
{
    gos_srand(1234);
    int max_seen = 0;
    for (int i = 0; i < 100000; ++i) {
        const int r = gos_rand();
        CHECK(r >= 0 && r < (1 << 15));
        if (r > max_seen)
            max_seen = r;
    }
    CHECK(max_seen > (1 << 14)); // uses the whole range
}

TEST(roll_dice_matches_percentage)
{
    gos_srand(42);
    const int n = 100000;
    for (long percent : {0L, 10L, 50L, 90L, 100L}) {
        int hits = 0;
        for (int i = 0; i < n; ++i)
            hits += RollDice(percent) ? 1 : 0;
        const double rate = 100.0 * hits / n;
        CHECK(rate > percent - 1.0 && rate < percent + 1.0);
    }
}

TEST(random_number_in_range)
{
    gos_srand(7);
    for (int i = 0; i < 10000; ++i) {
        const long r = RandomNumber(6);
        CHECK(r >= 0 && r < 6);
    }
}
