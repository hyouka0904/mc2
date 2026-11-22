// SPDX-License-Identifier: GPL-3.0-only
#include "png_writer.h"

#include <cstdio>
#include <zlib.h>

namespace png_writer {

namespace {

void put_u32(std::vector<uint8_t>& out, uint32_t v)
{
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

void put_chunk(std::vector<uint8_t>& out, const char type[4], const uint8_t* data, size_t len)
{
    put_u32(out, (uint32_t)len);
    const size_t type_pos = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data, data + len);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, out.data() + type_pos, (uInt)(4 + len));
    put_u32(out, (uint32_t)crc);
}

} // namespace

bool encode_rgb(const uint8_t* rgba, int width, int height, int stride,
                bool bottom_up, std::vector<uint8_t>& out)
{
    if (!rgba || width <= 0 || height <= 0 || stride < width * 4)
        return false;

    // Filter type 0 per row followed by RGB bytes.
    const size_t row_bytes = (size_t)width * 3 + 1;
    std::vector<uint8_t> raw(row_bytes * height);
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = rgba + (size_t)(bottom_up ? height - 1 - y : y) * stride;
        uint8_t* dst = &raw[row_bytes * y];
        *dst++ = 0;
        for (int x = 0; x < width; ++x) {
            *dst++ = src[x * 4 + 0];
            *dst++ = src[x * 4 + 1];
            *dst++ = src[x * 4 + 2];
        }
    }

    uLongf zlen = compressBound((uLong)raw.size());
    std::vector<uint8_t> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), (uLong)raw.size(), 6) != Z_OK)
        return false;
    z.resize(zlen);

    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    out.assign(signature, signature + 8);

    std::vector<uint8_t> ihdr;
    put_u32(ihdr, (uint32_t)width);
    put_u32(ihdr, (uint32_t)height);
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(2);  // colour type: truecolour
    ihdr.push_back(0);  // compression
    ihdr.push_back(0);  // filter
    ihdr.push_back(0);  // interlace
    put_chunk(out, "IHDR", ihdr.data(), ihdr.size());
    put_chunk(out, "IDAT", z.data(), z.size());
    put_chunk(out, "IEND", nullptr, 0);
    return true;
}

bool write_file(const std::string& path, const uint8_t* rgba, int width,
                int height, int stride, bool bottom_up)
{
    std::vector<uint8_t> png;
    if (!encode_rgb(rgba, width, height, stride, bottom_up, png))
        return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    const bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
    return (fclose(f) == 0) && ok;
}

} // namespace png_writer
