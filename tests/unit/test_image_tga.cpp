// Tests for TGA decoding in GameOS/gameos/utils/Image.cpp, which loads every
// texture shipped inside the game's fastfiles.

#include "mc2test.h"

#include "utils/Image.h"

#include <cstring>
#include <vector>

typedef std::vector<unsigned char> Bytes;

// Builds an 18-byte TGA header.
static Bytes tgaHeader(unsigned char type, int w, int h, int bpp, bool topLeft)
{
    Bytes b(18, 0);
    b[2] = type;
    b[12] = (unsigned char)(w & 0xff);
    b[13] = (unsigned char)(w >> 8);
    b[14] = (unsigned char)(h & 0xff);
    b[15] = (unsigned char)(h >> 8);
    b[16] = (unsigned char)bpp;
    b[17] = topLeft ? 0x20 : 0x00;
    return b;
}

static void append(Bytes& dst, const Bytes& src) { dst.insert(dst.end(), src.begin(), src.end()); }

// A 3x2 image, stored bottom-up in BGR order as TGA requires:
//   top row:    red   green  blue
//   bottom row: white white  black
static const unsigned char kBottomRowBGR[] = {255,255,255, 255,255,255, 0,0,0};
static const unsigned char kTopRowBGR[]    = {0,0,255,     0,255,0,     255,0,0};
// Expected decoder output: top row first, RGB order.
static const unsigned char kExpectedRGB[] = {
    255,0,0,  0,255,0,  0,0,255,
    255,255,255, 255,255,255, 0,0,0,
};

static Bytes uncompressed24()
{
    Bytes f = tgaHeader(2, 3, 2, 24, false);
    f.insert(f.end(), kBottomRowBGR, kBottomRowBGR + 9);
    f.insert(f.end(), kTopRowBGR, kTopRowBGR + 9);
    return f;
}

static Bytes rle24()
{
    Bytes f = tgaHeader(10, 3, 2, 24, false);
    // bottom row: run of 2 white pixels, then 1 raw black pixel
    const unsigned char run[] = {0x80 | 1, 255,255,255};
    const unsigned char raw1[] = {0x00, 0,0,0};
    // top row: 3 raw pixels
    const unsigned char raw3[] = {0x02, 0,0,255, 0,255,0, 255,0,0};
    append(f, Bytes(run, run + sizeof(run)));
    append(f, Bytes(raw1, raw1 + sizeof(raw1)));
    append(f, Bytes(raw3, raw3 + sizeof(raw3)));
    return f;
}

static bool pixelsEqual(const Image& img, const unsigned char* expected, size_t n)
{
    return img.getPixels() && memcmp(img.getPixels(), expected, n) == 0;
}

TEST(tga_uncompressed_24bit_is_top_row_first_rgb)
{
    const Bytes f = uncompressed24();
    Image img;
    REQUIRE(img.loadTGA(f.data(), f.size()));
    CHECK_EQ(img.getWidth(), 3);
    CHECK_EQ(img.getHeight(), 2);
    CHECK_EQ((int)img.getFormat(), (int)FORMAT_RGB8);
    CHECK(pixelsEqual(img, kExpectedRGB, sizeof(kExpectedRGB)));
}

TEST(tga_rle_24bit_matches_uncompressed)
{
    const Bytes f = rle24();
    Image img;
    REQUIRE(img.loadTGA(f.data(), f.size()));
    CHECK_EQ(img.getWidth(), 3);
    CHECK_EQ(img.getHeight(), 2);
    CHECK_EQ((int)img.getFormat(), (int)FORMAT_RGB8);
    CHECK(pixelsEqual(img, kExpectedRGB, sizeof(kExpectedRGB)));
}

TEST(tga_rle_32bit_top_left_origin_keeps_alpha)
{
    // 2x1, top-left origin: one run of two semi-transparent BGRA pixels
    Bytes f = tgaHeader(10, 2, 1, 32, true);
    const unsigned char run[] = {0x81, 10, 20, 30, 128};
    append(f, Bytes(run, run + sizeof(run)));

    Image img;
    REQUIRE(img.loadTGA(f.data(), f.size()));
    CHECK_EQ((int)img.getFormat(), (int)FORMAT_RGBA8);
    const unsigned char expected[] = {30, 20, 10, 128, 30, 20, 10, 128};
    CHECK(pixelsEqual(img, expected, sizeof(expected)));
}

TEST(tga_rle_16bit_expands_to_rgba8)
{
    // 1x1 ARGB1555: alpha=1, r=31, g=0, b=0 -> 0xFC00
    Bytes f = tgaHeader(10, 1, 1, 16, true);
    const unsigned char raw[] = {0x00, 0x00, 0xFC};
    append(f, Bytes(raw, raw + sizeof(raw)));

    Image img;
    REQUIRE(img.loadTGA(f.data(), f.size()));
    CHECK_EQ((int)img.getFormat(), (int)FORMAT_RGBA8);
    const unsigned char expected[] = {248, 0, 0, 255};
    CHECK(pixelsEqual(img, expected, sizeof(expected)));
}

TEST(tga_rle_truncated_data_is_rejected)
{
    Bytes f = rle24();
    f.resize(f.size() - 4); // cut into the last raw packet
    Image img;
    CHECK(!img.loadTGA(f.data(), f.size()));
}

TEST(tga_rle_run_past_end_of_image_is_rejected)
{
    // 1x1 image whose only packet claims 128 pixels: must not overflow
    Bytes f = tgaHeader(10, 1, 1, 24, false);
    const unsigned char run[] = {0xFF, 1, 2, 3};
    append(f, Bytes(run, run + sizeof(run)));
    Image img;
    CHECK(!img.loadTGA(f.data(), f.size()));
}

TEST(tga_image_can_be_reloaded)
{
    Image img;
    const Bytes a = uncompressed24();
    const Bytes b = rle24();
    REQUIRE(img.loadTGA(a.data(), a.size()));
    REQUIRE(img.loadTGA(b.data(), b.size()));
    CHECK(pixelsEqual(img, kExpectedRGB, sizeof(kExpectedRGB)));
}
