// SPDX-License-Identifier: GPL-3.0-only
// LAN games only start between builds with the same MC2_NET_PROTOCOL_STAMP
// (code/multplyr_wire.h). This test fails when a structure that travels over
// the network changed size, so the stamp is bumped on purpose:
//   1. bump MC2_NET_PROTOCOL_STAMP (MC2NET-4 -> MC2NET-5),
//   2. set kStamp below to the same text,
//   3. set kCurrent to the hash this test prints, for every platform (the
//      Windows value comes from the CI log).
// Changes inside a field (the bit layout of a chunk, a new enum value) do
// not change sizes: bump the stamp for those by hand.
#include "mc2test.h"
#include "netlayout.h"

#include <cstdio>
#include <cstring>

static const char* const kStamp = "MC2NET-4";
#if defined(_WIN32)
static const uint32_t kCurrent = 0x00000000u; // Windows x64
#else
static const uint32_t kCurrent = 0x8cb28b61u; // Linux x86_64, macOS arm64 (LP64)
#endif

TEST(net_layout_matches_the_protocol_stamp)
{
    uint32_t h = NetLayoutHash();
    std::printf("net layout: %08x, stamp %s\n", (unsigned)h, NetLayoutStamp());
    CHECK_EQ(h, kCurrent);
    CHECK(std::strcmp(NetLayoutStamp(), kStamp) == 0);
}
