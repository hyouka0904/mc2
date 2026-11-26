#include <algorithm>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
// errno + strerror
#include <errno.h>

#include "gameos.hpp"
#include "gos_font.h"

bool gos_load_glyphs(const char* glyphFile, gosGlyphInfo& gi)
{
    FILE* glyph_info = fopen(glyphFile, "rb");
    if(!glyph_info) {
        int last_err = errno; (void)last_err; // used only by debug / LAB_ONLY code
        SPEW(("fopen: %s\n", strerror(last_err)));
        return false;
    }

    if (fread(&gi.num_glyphs_, sizeof(gi.num_glyphs_), 1, glyph_info) != 1 ||
        fread(&gi.start_glyph_, sizeof(gi.start_glyph_), 1, glyph_info) != 1 ||
        fread(&gi.max_advance_, sizeof(gi.max_advance_), 1, glyph_info) != 1 ||
        fread(&gi.font_ascent_, sizeof(gi.font_ascent_), 1, glyph_info) != 1 ||
        fread(&gi.font_line_skip_, sizeof(gi.font_line_skip_), 1, glyph_info) != 1) {
        SPEW(("%s: truncated glyph file\n", glyphFile));
        fclose(glyph_info);
        return false;
    }

    size_t num_structs_read = 0;

    gi.glyphs_ = new gosGlyphMetrics[gi.num_glyphs_];

    while(num_structs_read!= gi.num_glyphs_) {
        const size_t n = fread(&gi.glyphs_[num_structs_read],
                sizeof(gosGlyphMetrics),
                gi.num_glyphs_ - num_structs_read,
                glyph_info);
        if (n == 0) { // truncated file: used to loop forever
            SPEW(("%s: truncated glyph file\n", glyphFile));
            memset(&gi.glyphs_[num_structs_read], 0, sizeof(gosGlyphMetrics) * (gi.num_glyphs_ - num_structs_read));
            break;
        }
        num_structs_read += n;
    }

    // An optional, sorted Unicode table follows the original glyph records.
    char magic[4];
    uint32_t count = 0;
    if (fread(magic, 1, sizeof(magic), glyph_info) == sizeof(magic) &&
        memcmp(magic, "MCU1", sizeof(magic)) == 0 &&
        fread(&count, sizeof(count), 1, glyph_info) == 1 && count <= 0x110000) {
        gi.unicode_glyphs_.resize(count);
        uint32_t previous = 0;
        for (auto& glyph : gi.unicode_glyphs_) {
            if (fread(&glyph.codepoint, sizeof(glyph.codepoint), 1, glyph_info) != 1 ||
                fread(&glyph.metrics, sizeof(glyph.metrics), 1, glyph_info) != 1 ||
                glyph.codepoint <= previous || glyph.codepoint > 0x10ffff) {
                gi.unicode_glyphs_.clear();
                break;
            }
            previous = glyph.codepoint;
        }
    }

    fclose(glyph_info);
    return true;
}

const gosGlyphMetrics* gosGlyphInfo::find(int codepoint) const
{
    const int position = codepoint - static_cast<int>(start_glyph_);
    if (position >= 0 && static_cast<uint32_t>(position) < num_glyphs_)
        return &glyphs_[position];

    const auto it = std::lower_bound(unicode_glyphs_.begin(), unicode_glyphs_.end(),
        static_cast<uint32_t>(codepoint), [](const gosUnicodeGlyph& glyph, uint32_t value) {
            return glyph.codepoint < value;
        });
    if (it != unicode_glyphs_.end() && it->codepoint == static_cast<uint32_t>(codepoint))
        return &it->metrics;
    return nullptr;
}
