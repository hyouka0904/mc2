// SPDX-License-Identifier: GPL-3.0-only
// The in-mission save writes game structures byte for byte (see
// code/savelayout.h). This test fails when one of them changes size, so the
// change is made on purpose: quick saves from earlier builds then stop
// loading (with a message), and kCurrent below gets the new value.
#include "mc2test.h"
#include "savelayout.h"

#include <cstdio>

#if defined(_WIN32)
static const uint32_t kCurrent = 0x00000000u; // Windows x64
#elif defined(__APPLE__)
static const uint32_t kCurrent = 0x98d5de0eu; // macOS arm64 (LP64 like Linux)
#else
static const uint32_t kCurrent = 0x98d5de0eu; // Linux x86_64
#endif

TEST(save_layout_is_the_expected_one)
{
    uint32_t h = SaveLayoutHash();
    std::printf("save layout: %08x\n", (unsigned)h);
    CHECK_EQ(h, kCurrent);
}

TEST(builds_before_the_fingerprint_are_read)
{
    // Until the first deliberate change the layout is still the 0.1.0 one.
    if (SaveLayoutHash() == kCurrent)
        CHECK(SaveLayoutIsLegacy(SaveLayoutHash()));
    CHECK(!SaveLayoutIsLegacy(0x12345678u));
}
