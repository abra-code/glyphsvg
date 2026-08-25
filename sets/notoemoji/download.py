#!/usr/bin/env python3
"""Provision the Noto Emoji glyph set.

The monochrome emoji font: line-art pictographs rather than the colour bitmaps
of Noto Color Emoji, which glyphsvg cannot read at all. It carries a real weight
axis, so the picker's weight control drives it like the other text fonts.

What it adds over the icon sets is a vocabulary they do not have - faces,
gestures, food, animals, nature, transport. What it does NOT cover is the
general symbol blocks: there is no star here, because a star is not an emoji.
MDI has those, and better drawn.

Source:  https://github.com/google/fonts/tree/main/ofl/notoemoji
License: SIL Open Font License 1.1.

Neither the font nor the generated tables are committed (see ../../.gitignore),
so run this once before using the set. Everything below is shared with the other
text-font sets in ../textfont.py - the symbols here are characters, so the
name-to-codepoint table is generated from the font's own cmap rather than
downloaded from a vendor.
"""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(SCRIPT_DIR))

import textfont

FONT_URL = ("https://raw.githubusercontent.com/google/fonts/main/ofl/notoemoji/"
            "NotoEmoji%5Bwght%5D.ttf")
LICENSE_URL = ("https://raw.githubusercontent.com/google/fonts/main/ofl/notoemoji/"
               "OFL.txt")

# CLDR's English emoji keywords, merged into the metadata sidecar so the picker
# can be searched in words people actually use. Unicode's own names are formal -
# the page emoji is PAGE FACING UP - so without this, "document" finds nothing
# and "search" finds nothing, while MAGNIFYING GLASS TILTED LEFT sits there
# unfound. Costs about 50KB in the sidecar against a 1.9MB font.
#
# Only annotations/en.xml; see the note above load_cldr for why its Derived
# sibling is 550KB of no use here.
CLDR_URL = ("https://raw.githubusercontent.com/unicode-org/cldr/main/common/"
            "annotations/en.xml")

if __name__ == "__main__":
    sys.exit(textfont.provision(
        SCRIPT_DIR,
        font_url=FONT_URL,
        font_name="NotoEmoji.ttf",
        codepoints_name="notoemoji.codepoints",
        metadata_name="notoemoji_metadata.json",
        license_url=LICENSE_URL,
        # Emoji, not letters: the default QAg8 probe would fail on a font whose
        # Latin coverage is a handful of digits and punctuation.
        samples="\U0001F600\U0001F680❤\U0001F4C4",
        cldr_url=CLDR_URL,
    ))
