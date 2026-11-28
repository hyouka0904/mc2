#!/usr/bin/env python3
"""Build the Traditional Chinese patch from a prepared game and a local Noto font."""

import argparse
from pathlib import Path
import re
import struct

from PIL import Image, ImageFont


ROOT = Path(__file__).resolve().parents[1]
HEADER = struct.Struct("<5I")
GLYPH = struct.Struct("<I5i3I")
PRINTF_TOKEN = re.compile(r"%%|%[-+#0 ]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diouxXfFeEgGaAcspn]")


def load_translations():
    symbols = dict(re.findall(
        r"^\s*(IDS_\w+)\s*=\s*(\d+)\s*,",
        (ROOT / "res/strings/strings.res.h").read_text(),
        re.MULTILINE,
    ))
    originals = dict(re.findall(
        r'\{\s*(IDS_\w+)\s*,\s*"((?:\\.|[^"\\])*)"\s*\}',
        (ROOT / "res/strings/strings.res.cpp").read_text(encoding="cp1252"),
    ))
    entries = {}
    for line in (ROOT / "localization/zh-TW/strings.txt").read_text(encoding="utf-8").splitlines():
        symbol, translation = line.split("\t", 1)
        number = int(symbols[symbol])
        if number in entries:
            raise ValueError(f"Duplicate translation for {symbol} ({number})")
        if PRINTF_TOKEN.findall(originals[symbol]) != PRINTF_TOKEN.findall(translation):
            raise ValueError(f"Format placeholders differ from the original for {symbol}")
        entries[number] = translation
    return entries


def bake_font(source, target, font_path, face_index, characters):
    original_metrics = source.read_bytes()
    count, _, _, ascent, line_height = HEADER.unpack_from(original_metrics)
    if len(original_metrics) != HEADER.size + count * (GLYPH.size - 4) or not line_height:
        raise ValueError(f"Expected original bitmap font metrics in {source}")

    font = ImageFont.truetype(str(font_path), max(7, line_height - 2), index=face_index)
    missing = bytes(font.getmask(chr(0x10FFFF)))
    # Small CJK glyphs have several strokes between solid pixels. Strengthen
    # their partial coverage without expanding their bounds or changing layout.
    coverage = [round(255 * (alpha / 255) ** 0.72) for alpha in range(256)]
    bitmaps = []
    for char in characters:
        mask = font.getmask(char)
        if bytes(mask) == missing or not mask.getbbox():
            raise ValueError(f"Missing glyph {char!r} (U+{ord(char):04X})")
        glyph = Image.frombytes("L", mask.size, bytes(mask))
        if line_height <= 20:
            glyph = glyph.point(coverage)
        left, top, right, bottom = font.getbbox(char, anchor="ls")
        advance = round(font.getlength(char))
        bitmaps.append((char, glyph, left, top, right, bottom, advance))

    if any(right - left != glyph.width or bottom - top != glyph.height
           for _, glyph, left, top, right, bottom, _ in bitmaps):
        raise ValueError(f"Rasterized glyph does not match metrics for {source}")

    with Image.open(source.with_suffix(".bmp")) as image:
        original = image.convert("RGBA")

    pitch_x = max(right - left for _, _, left, _, right, _, _ in bitmaps) + 2
    pitch_y = max(bottom - top for _, _, _, top, _, bottom, _ in bitmaps) + 2
    margin_x = max(2, max(left for _, _, left, _, _, _, _ in bitmaps))
    for atlas_width in (512, 1024, 2048):
        if atlas_width < original.width:
            continue
        columns = (atlas_width - margin_x) // pitch_x
        rows = (len(bitmaps) + columns - 1) // columns
        used_height = original.height + rows * pitch_y
        if used_height <= 2048:
            break
    else:
        raise ValueError(f"Font exceeds its single-page atlas limit: {source}")
    atlas_height = 1 << (used_height - 1).bit_length()

    atlas = Image.new("RGBA", (atlas_width, atlas_height))
    atlas.paste(original, (0, 0))
    metrics = bytearray(original_metrics)
    metrics.extend(struct.pack("<4sI", b"MCU1", len(bitmaps)))
    for index, (char, glyph, left, top, right, bottom, advance) in enumerate(bitmaps):
        # Place each glyph against the same baseline used by the original atlas.
        x = margin_x + (index % columns) * pitch_x
        y = original.height + (index // columns) * pitch_y
        # The game's text shader uses the red channel as the glyph mask.
        atlas.paste(Image.merge("RGBA", (glyph, glyph, glyph, glyph)), (x, y))
        metrics.extend(GLYPH.pack(ord(char), left, right, -bottom, -top, advance,
                                  1, x - left, y - ascent - top))

    atlas.save(target.with_suffix(".bmp"))
    target.write_bytes(metrics)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="Prepared game directory")
    parser.add_argument("font", type=Path, help="Noto Sans TC OTF/TTF or TTC")
    parser.add_argument("--face-index", type=int, default=0, help="TC face in a font collection")
    args = parser.parse_args()

    try:
        translations = load_translations()
        characters = sorted({char for text in translations.values() for char in text
                             if ord(char) >= 255})
        sources = sorted((args.game / "assets/graphics").glob("*.glyph"))
        if not sources:
            raise ValueError("No original fonts found under the game's assets/graphics")
        output = args.game / "localization/zh-TW"
        fonts = output / "fonts"
        fonts.mkdir(parents=True, exist_ok=True)
        for source in sources:
            bake_font(source, fonts / source.name, args.font, args.face_index, characters)
        table = "".join(f"{number}\t{text}\n" for number, text in sorted(translations.items()))
        (output / "strings.tsv").write_text(table, encoding="utf-8")
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Could not build zh-TW patch: {error}\n")

    print(f"Built {len(translations)} translations and {len(characters)} glyphs in {len(sources)} fonts.")


if __name__ == "__main__":
    main()
