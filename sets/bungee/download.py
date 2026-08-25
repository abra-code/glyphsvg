#!/usr/bin/env python3
"""Provision the Bungee glyph set.

Drawn for signage, and it shows: the only face surveyed that keeps a
three-letter word legible at a 16px icon. Static, so it has no weight axis - its
single weight is already heavy.

Source:  https://github.com/google/fonts/tree/main/ofl/bungee
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

FONT_URL = ("https://raw.githubusercontent.com/google/fonts/main/ofl/bungee/Bungee-Regular.ttf")
LICENSE_URL = ("https://raw.githubusercontent.com/google/fonts/main/ofl/bungee/OFL.txt")

if __name__ == "__main__":
    sys.exit(textfont.provision(
        SCRIPT_DIR,
        font_url=FONT_URL,
        font_name="Bungee-Regular.ttf",
        codepoints_name="bungee.codepoints",
        metadata_name="bungee_metadata.json",
        license_url=LICENSE_URL,
    ))
