#!/usr/bin/env python3
"""Provision the Monaspace Krypton glyph set.

Monospace, and the most distinctive Q of anything surveyed - a long detached
tail that stays unambiguous where most faces blur into an O.

Upstream ships this as 'Monaspace Krypton Var.ttf'; it is installed here without
the spaces, because glyphset.conf filenames may not contain any. Its licence
reserves the font name, so it is shipped unmodified and unrenamed.

Source:  https://github.com/githubnext/monaspace
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

FONT_URL = ("https://raw.githubusercontent.com/githubnext/monaspace/main/fonts/Variable%20Fonts/Monaspace%20Krypton/Monaspace%20Krypton%20Var.ttf")
LICENSE_URL = ("https://raw.githubusercontent.com/githubnext/monaspace/main/LICENSE")

if __name__ == "__main__":
    sys.exit(textfont.provision(
        SCRIPT_DIR,
        font_url=FONT_URL,
        font_name="MonaspaceKrypton-Var.ttf",
        codepoints_name="monaspace.codepoints",
        metadata_name="monaspace_metadata.json",
        license_url=LICENSE_URL,
    ))
