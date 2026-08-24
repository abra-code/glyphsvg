#!/usr/bin/env python3
"""Provision the Microsoft Fluent System Icons glyph set (Resizable family).

Downloads the font and converts upstream's name->codepoint JSON into the two
.codepoints maps glyphsvg reads, one per face. Neither the font nor the
generated maps are committed (see ../../.gitignore), so run this once before
using the set.

Source:  https://github.com/microsoft/fluentui-system-icons
License: MIT (icons and fonts).

Two conversions are mandatory here, unlike MDI where the map is nearly usable as
published:

  1. Codepoints are decimal in the source and must be hex in a .codepoints file.
  2. Names must be rewritten. Upstream spells every entry
     ic_fluent_<concept>_<size>_<style>, e.g. "ic_fluent_access_time_20_regular".
     The ic_fluent_ prefix and the _<style> suffix are constant noise, and in the
     per-size families the size makes each icon appear up to seven times under
     names that differ only by a number nobody picking artwork cares about. In
     the Resizable family the size is always 20 and therefore vestigial, so
     stripping all three leaves a clean "access_time".

The style suffix becomes the face: Fluent draws filled and outlined as separate
glyphs at separate codepoints, so regular and filled are two faces over one font
file rather than a FILL axis.
"""

import collections
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FONT_NAME = "FluentSystemIcons-Resizable.ttf"
SOURCE_MAP_NAME = "FluentSystemIcons-Resizable.json"
FACES = ("regular", "filled")

BASE_URL = ("https://raw.githubusercontent.com/microsoft/fluentui-system-icons"
            "/main/fonts")
LICENSE_URL = ("https://raw.githubusercontent.com/microsoft/fluentui-system-icons"
               "/main/LICENSE")

# Anchored on _20_ deliberately rather than _\d+_: if upstream ever ships a
# Resizable family at another design size, or mixes sizes into it, the parse
# fails loudly here instead of silently collapsing two distinct icons onto one
# name.
NAME_PATTERN = re.compile(r"^ic_fluent_(.+)_20_(regular|filled)$")

INSTALLED = [FONT_NAME] + ["fluent-%s.codepoints" % f for f in FACES] + ["LICENSE"]


def human_size(path):
    """Bytes, KB or MB - a 992-byte LICENSE reporting as "0K" is a size column
    that cannot be trusted for the files that matter either."""
    size = os.path.getsize(path)
    if size >= 1048576:
        return "%.1fM" % (size / 1048576.0)
    if size >= 1024:
        return "%dK" % (size // 1024)
    return "%dB" % size


FONT_MAGIC = (b"\x00\x01\x00\x00", b"true", b"ttcf", b"OTTO", b"wOFF", b"wOF2")


def check_font(path):
    """Reject anything that is not a font, unconditionally.

    curl -f only rejects HTTP >= 400, and a captive portal or a rewriting proxy
    answers 200 with an HTML page. The JSON downloads are caught by json.load;
    nothing else would catch this one. The render probe would, but it is skipped
    when no glyphsvg has been built - which is exactly the state of a fresh
    clone, so it cannot be the only check."""
    with open(path, "rb") as f:
        magic = f.read(4)
    if magic not in FONT_MAGIC:
        fail("%s is not a font (starts with %r) - a captive portal or proxy "
             "likely intercepted the download" % (os.path.basename(path), magic))


def check_name(name):
    """A symbol name with whitespace in it produces a .codepoints line glyphsvg
    reads as a different name and a bogus codepoint: an entry that can never
    render, and no error anywhere."""
    if not name or name.split() != [name]:
        fail("upstream symbol name %r contains whitespace - the .codepoints "
             "format cannot represent it" % name)


def install(stage, names):
    """Move the finished files into place, all or nothing.

    Moving them one by one is the partial state the staging directory exists to
    avoid: failing on the third of four leaves two installed and the staging copy
    deleted behind us."""
    staged = []
    for name in names:
        pending = os.path.join(SCRIPT_DIR, "." + name + ".new")
        shutil.copyfile(os.path.join(stage, name), pending)
        staged.append((pending, os.path.join(SCRIPT_DIR, name)))
    for pending, final in staged:
        os.replace(pending, final)


def fail(message):
    sys.stderr.write("error: %s\n" % message)
    raise SystemExit(1)


def download(url, dest, quiet=False):
    """Fetch one URL with curl. The timeouts are explicit: a black-holed
    connection would otherwise hang a build indefinitely instead of failing."""
    print("Downloading %s ..." % os.path.basename(dest))
    result = subprocess.run([
        "/usr/bin/curl", "-fgL", "--retry", "3",
        "--connect-timeout", "30", "--max-time", "900", "--show-error",
        "--silent" if quiet else "--progress-bar",
        "-o", dest, url,
    ])
    if result.returncode != 0:
        fail("download failed: %s" % url)


def convert(stage):
    """The source JSON -> one .codepoints file per face."""
    with open(os.path.join(stage, SOURCE_MAP_NAME)) as f:
        source = json.load(f)
    if not isinstance(source, dict) or not source:
        fail("%s is not the expected non-empty object" % SOURCE_MAP_NAME)

    faces = {face: {} for face in FACES}
    unparsed = []
    collisions = collections.Counter()
    for key, value in source.items():
        match = NAME_PATTERN.match(key)
        if match is None:
            unparsed.append(key)
            continue
        name, face = match.group(1), match.group(2)
        check_name(name)
        if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
            fail("upstream codepoint %r for %r is not a positive integer"
                 % (value, key))
        collisions[(face, name)] += 1
        faces[face][name] = value

    if unparsed:
        fail("%d names did not match ic_fluent_<name>_20_<regular|filled>, e.g. "
             "%s - upstream changed its naming and this converter needs "
             "rechecking" % (len(unparsed), ", ".join(sorted(unparsed)[:5])))

    # Stripping three components off a name can in principle collide two
    # distinct icons. It does not today, and this is what proves it stays so.
    duplicated = sorted("%s/%s" % (f, n) for (f, n), c in collisions.items() if c > 1)
    if duplicated:
        fail("name collisions after stripping: %s" % ", ".join(duplicated[:5]))

    for face in FACES:
        table = faces[face]
        if not table:
            fail("no %s glyphs found - the source JSON is not what was expected" % face)
        with open(os.path.join(stage, "fluent-%s.codepoints" % face), "w") as f:
            for name in sorted(table):
                f.write("%s %x\n" % (name, table[name]))
        print("  %s: %d icons" % (face, len(table)))


def render_probe(stage):
    """Render one glyph from EVERY face.

    Fluent points both faces at a single .ttf and separates them only by
    codepoint table, so probing the default alone would leave the other table
    entirely unverified."""
    glyphsvg = os.path.join(SCRIPT_DIR, "..", "..", "build", "bin", "glyphsvg")
    if not os.access(glyphsvg, os.X_OK):
        print("  no built glyphsvg to render-probe against - "
              "run ../../build.sh first for a full check")
        return

    shutil.copyfile(os.path.join(SCRIPT_DIR, "glyphset.conf"),
                    os.path.join(stage, "glyphset.conf"))
    for face in FACES:
        with open(os.path.join(stage, "fluent-%s.codepoints" % face)) as f:
            symbol = f.readline().split()[0]
        probe = os.path.join(stage, "probe.svg")
        result = subprocess.run(
            [glyphsvg, "--set=" + stage, "--face=" + face, symbol, "64",
             "--output=" + probe],
            capture_output=True, text=True)
        if result.returncode != 0:
            fail("glyphsvg could not render '%s' from the %s face - "
                 "the download is corrupt or truncated" % (symbol, face))
        if not os.path.isfile(probe):
            fail("glyphsvg exited 0 but wrote no SVG for '%s' (%s)" % (symbol, face))
        if not os.path.getsize(probe):
            fail("glyphsvg produced an empty SVG for '%s' (%s)" % (symbol, face))
        os.remove(probe)
        print("  render probe OK (%s, %s)" % (face, symbol))
    os.remove(os.path.join(stage, "glyphset.conf"))


def main():
    stage = tempfile.mkdtemp(prefix="fluent.")
    try:
        download("%s/%s" % (BASE_URL, FONT_NAME), os.path.join(stage, FONT_NAME))
        download("%s/%s" % (BASE_URL, SOURCE_MAP_NAME),
                 os.path.join(stage, SOURCE_MAP_NAME))
        download(LICENSE_URL, os.path.join(stage, "LICENSE"), quiet=True)
        check_font(os.path.join(stage, FONT_NAME))
        convert(stage)
        render_probe(stage)
        install(stage, INSTALLED)
    finally:
        shutil.rmtree(stage, ignore_errors=True)

    print("\nDownloaded into %s:" % SCRIPT_DIR)
    for name in INSTALLED:
        print("  %-40s %s" % (name, human_size(os.path.join(SCRIPT_DIR, name))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
