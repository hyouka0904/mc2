// SPDX-License-Identifier: GPL-3.0-only
// Layout fingerprint of the in-mission save (.ims).
//
// The quick save writes game structures (MechData, MoverData, ...) to the
// file byte for byte, and the loader even tells objects apart by packet size.
// A save written by a build whose structures had other sizes cannot be read:
// it would be mistaken for other objects or overflow the buffers. The file
// therefore starts with versionStamp plus this fingerprint, and the loader
// refuses a file whose fingerprint differs, with a message instead of a crash.
#pragma once

#include <cstdint>

// FNV-1a over the sizes of every structure the quick save stores.
uint32_t SaveLayoutHash();

// Quick saves written before the fingerprint existed carry versionStamp
// alone. They are read when this build's layout is the one those builds had
// on this platform (the values are frozen; see code/savelayout.cpp).
bool SaveLayoutIsLegacy(uint32_t hash);
