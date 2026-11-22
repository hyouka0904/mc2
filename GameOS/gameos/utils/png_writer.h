// SPDX-License-Identifier: GPL-3.0-only
#ifndef GAMEOS_PNG_WRITER_H
#define GAMEOS_PNG_WRITER_H

// Minimal PNG encoder (8-bit RGB, zlib from the existing dependency). Used for
// screenshot captures; no libpng needed.

#include <stdint.h>
#include <string>
#include <vector>

namespace png_writer {

// rgba: width*height pixels, 4 bytes each, rows `stride` bytes apart.
// bottom_up: rows are stored last row first (as glReadPixels returns them).
// Alpha is dropped: the back buffer alpha is not meaningful.
bool encode_rgb(const uint8_t* rgba, int width, int height, int stride,
                bool bottom_up, std::vector<uint8_t>& out);

bool write_file(const std::string& path, const uint8_t* rgba, int width,
                int height, int stride, bool bottom_up);

} // namespace png_writer

#endif
