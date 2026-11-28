// SPDX-License-Identifier: GPL-3.0-only
// Layout fingerprint of the LAN multiplayer wire format.
//
// Builds only play together when MC2_NET_PROTOCOL_STAMP matches, so any
// change to a structure that travels over the network must bump it.
// unit_netlayout compares this fingerprint with the value recorded next to
// the stamp and fails when a structure changed size without a new stamp.
#pragma once

#include <cstdint>

// FNV-1a over the sizes of every MCMSG_* class and wire structure in use.
uint32_t NetLayoutHash();

// MC2_NET_PROTOCOL_STAMP, for the test.
const char* NetLayoutStamp();
