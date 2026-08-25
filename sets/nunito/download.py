#!/usr/bin/env python3
"""Provision the Nunito glyph set.

The rounded face of the set: soft terminals with a weight axis running to
1000, which is heavier than the 900 any named weight can reach.

Source:  https://github.com/google/fonts/tree/main/ofl/nunito
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

FONT_URL = ("https://raw.githubusercontent.com/google/fonts/main/ofl/nunito/Nunito%5Bwght%5D.ttf")
LICENSE_URL = ("https://raw.githubusercontent.com/google/fonts/main/ofl/nunito/OFL.txt")

if __name__ == "__main__":
    sys.exit(textfont.provision(
        SCRIPT_DIR,
        font_url=FONT_URL,
        font_name="Nunito.ttf",
        codepoints_name="nunito.codepoints",
        metadata_name="nunito_metadata.json",
        license_url=LICENSE_URL,
    ))
