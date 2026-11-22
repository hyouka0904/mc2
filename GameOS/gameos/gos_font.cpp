#include<inttypes.h>
#include <stdio.h>
#include <string.h>
// errno + strerror
#include <errno.h>
#include <string.h>

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

    fclose(glyph_info);

    return true;
}


