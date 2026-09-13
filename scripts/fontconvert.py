#!/usr/bin/env python3
"""TrueType to Adafruit_GFX font converter (Python port of fontconvert.c).

Usage: fontconvert.py fontfile size [first] [last] > FontName.h
"""

import os
import string
import sys

import freetype

DPI = 141  # Approximate res. of Adafruit 2.8" TFT


def _enbit(value, st):
    if value:
        st["sum"] |= st["bit"]
    st["bit"] >>= 1
    if st["bit"] == 0:
        if not st["firstCall"]:
            st["row"] += 1
            if st["row"] >= 12:
                st["out"].append(",\n  ")
                st["row"] = 0
            else:
                st["out"].append(", ")
        st["out"].append("0x%02X" % st["sum"])
        st["sum"] = 0
        st["bit"] = 0x80
        st["firstCall"] = False


def main():
    if len(sys.argv) < 3:
        print("Usage: %s fontfile size [first] [last]" % sys.argv[0], file=sys.stderr)
        return 1

    fontfile = sys.argv[1]
    size = int(sys.argv[2])
    first = ord(" ")
    last = ord("~")
    if len(sys.argv) == 4:
        last = int(sys.argv[3])
    elif len(sys.argv) == 5:
        first = int(sys.argv[3])
        last = int(sys.argv[4])
    if last < first:
        first, last = last, first

    base = os.path.basename(fontfile)
    name = os.path.splitext(base)[0]
    font_name = "%s%dpt%db" % (name, size, 8 if last > 127 else 7)
    font_name = "".join(
        "_" if (c.isspace() or c in string.punctuation) else c for c in font_name
    )

    library = freetype.FT_Library()
    freetype.FT_Property_Set(library, "truetype", "interpreter-version", 35)
    face = freetype.Face(fontfile)
    face.set_char_size(size << 6, 0, DPI, 0)

    bitmaps_out = []
    glyphs = []
    bitmap_offset = 0
    st = {"row": 0, "sum": 0, "bit": 0x80, "firstCall": True, "out": bitmaps_out}

    for i in range(first, last + 1):
        try:
            face.load_char(chr(i), freetype.FT_LOAD_TARGET_MONO)
            face.glyph.render(freetype.FT_RENDER_MODE_MONO)
        except Exception as e:
            print("Error rendering char 0x%02X: %s" % (i, e), file=sys.stderr)
            continue

        bitmap = face.glyph.bitmap
        width = bitmap.width
        rows = bitmap.rows
        pitch = bitmap.pitch
        buffer = bitmap.buffer
        x_advance = face.glyph.advance.x >> 6
        x_offset = face.glyph.bitmap_left
        y_offset = 1 - face.glyph.bitmap_top

        glyphs.append((bitmap_offset, width, rows, x_advance, x_offset, y_offset))

        for y in range(rows):
            for x in range(width):
                byte = x // 8
                bit = 0x80 >> (x & 7)
                _enbit(buffer[y * pitch + byte] & bit, st)

        n = (width * rows) & 7
        if n:
            for _ in range(8 - n):
                _enbit(0, st)
        bitmap_offset += (width * rows + 7) // 8

    out = []
    out.append("const uint8_t %sBitmaps[] PROGMEM = {\n  " % font_name)
    out.append("".join(bitmaps_out))
    out.append(" };\n\n")

    out.append("const GFXglyph %sGlyphs[] PROGMEM = {\n" % font_name)
    for i, g in zip(range(first, last + 1), glyphs):
        out.append("  { %5d, %3d, %3d, %3d, %4d, %4d }" % g)
        if i < last:
            out.append(",   // 0x%02X" % i)
            if ord(" ") <= i <= ord("~"):
                out.append(" '%c'" % i)
            out.append("\n")
    out.append(" }; // 0x%02X" % last)
    if ord(" ") <= last <= ord("~"):
        out.append(" '%c'" % last)
    out.append("\n\n")

    out.append("const GFXfont %s PROGMEM = {\n" % font_name)
    out.append("  (uint8_t  *)%sBitmaps,\n" % font_name)
    out.append("  (GFXglyph *)%sGlyphs,\n" % font_name)
    height = face.size.height >> 6
    if height == 0:
        out.append(
            "  0x%02X, 0x%02X, %d };\n\n" % (first, last, glyphs[0][2] if glyphs else 0)
        )
    else:
        out.append("  0x%02X, 0x%02X, %d };\n\n" % (first, last, height))
    out.append("// Approx. %d bytes\n" % (bitmap_offset + (last - first + 1) * 7 + 7))

    sys.stdout.write("".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
