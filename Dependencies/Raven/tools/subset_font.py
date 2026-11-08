#!/usr/bin/env python3
"""Builds the font embedded in Raven from a full TrueType file.

    python3 tools/subset_font.py Roboto-Regular.ttf third_party/roboto/Roboto-Regular-subset.ttf

Keeps Basic Latin, Latin-1 Supplement, Latin Extended-A and common punctuation,
and drops hinting and layout tables, which the Raven rasterizer does not use.
Requires fontTools (pip install fonttools).
"""
import sys
from fontTools import subset

UNICODES = (
    list(range(0x0020, 0x007F))      # Basic Latin
    + list(range(0x00A0, 0x0180))    # Latin-1 Supplement, Latin Extended-A
    + list(range(0x2010, 0x2028))    # dashes, quotes, bullet, ellipsis
    + [0x2030, 0x2039, 0x203A, 0x20AC, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x00D7, 0xFFFD]
)

def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    options = subset.Options()
    options.hinting = False
    options.desubroutinize = True
    options.layout_features = []
    options.drop_tables += ["GSUB", "GPOS", "GDEF", "kern", "DSIG"]
    options.name_IDs = [0, 1, 2, 4, 6, 13, 14]   # keep the copyright and license notices
    options.notdef_outline = True
    options.glyph_names = False
    font = subset.load_font(sys.argv[1], options)
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=UNICODES)
    subsetter.subset(font)
    subset.save_font(font, sys.argv[2], options)

if __name__ == "__main__":
    main()
