#!/usr/bin/env python3
"""Provision the Pictogrammers Material Design Icons glyph set.

Downloads the MDI webfont and converts upstream's meta.json into the two files
glyphsvg reads: a .codepoints map and an optional search-metadata sidecar.
Neither the font nor the generated files are committed (see ../../.gitignore),
so run this once before using the set.

Sources:
  font  https://github.com/Templarian/MaterialDesign-Webfont
  meta  https://github.com/Templarian/MaterialDesign

License: Pictogrammers Free License (Apache 2.0 terms for the icons and font,
MIT for the code). GitHub reports NOASSERTION only because of the file title.

Deprecated icons are dropped by default. Upstream marks them for removal or
replacement, and offering them in a picker is a trap for artwork meant to ship.
Pass --include-deprecated to keep them.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FONT_NAME = "materialdesignicons-webfont.ttf"
CODEPOINTS_NAME = "mdi.codepoints"
METADATA_NAME = "mdi_metadata.json"

FONT_URL = ("https://raw.githubusercontent.com/Templarian/MaterialDesign-Webfont"
            "/master/fonts/" + FONT_NAME)
META_URL = "https://raw.githubusercontent.com/Templarian/MaterialDesign/master/meta.json"
LICENSE_URL = "https://raw.githubusercontent.com/Templarian/MaterialDesign/master/LICENSE"

INSTALLED = [FONT_NAME, CODEPOINTS_NAME, METADATA_NAME, "LICENSE"]


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


def convert(stage, include_deprecated):
    """meta.json -> mdi.codepoints + mdi_metadata.json.

    Codepoints are written exactly as upstream spells them (uppercase,
    "F02DC"). glyphsvg reads them with strtoul(base 16), which is case-blind, so
    converting would buy nothing and would leave this file disagreeing with
    meta.json for no reason. Sets are free to use either case.

    The sidecar is emitted in the same {"icons":[{name,tags,categories}]} shape
    Google publishes for Material Symbols, so one search-index loader serves
    both sets. MDI's own "tags" are category paths ("Developer / Languages") and
    its "aliases" are synonyms, so they map onto categories and tags
    respectively."""
    with open(os.path.join(stage, "meta.json")) as f:
        meta = json.load(f)
    if not isinstance(meta, list) or not meta:
        fail("meta.json is not the expected non-empty array of icons")

    kept, dropped, icons = [], 0, []
    for entry in meta:
        name, codepoint = entry.get("name"), entry.get("codepoint")
        if not name or not codepoint:
            continue
        if entry.get("deprecated") and not include_deprecated:
            dropped += 1
            continue
        check_name(name)
        try:
            int(codepoint, 16)
        except (TypeError, ValueError):
            fail("upstream codepoint %r for %r is not hexadecimal" % (codepoint, name))
        kept.append((name, codepoint))
        icons.append({
            "name": name,
            "tags": entry.get("aliases") or [],
            "categories": entry.get("tags") or [],
        })

    if not kept:
        fail("meta.json yielded no icons")

    names = [n for n, _ in kept]
    if len(set(names)) != len(names):
        fail("meta.json contains duplicate icon names")

    kept.sort()
    icons.sort(key=lambda i: i["name"])
    with open(os.path.join(stage, CODEPOINTS_NAME), "w") as f:
        for name, codepoint in kept:
            f.write("%s %s\n" % (name, codepoint))
    with open(os.path.join(stage, METADATA_NAME), "w") as f:
        json.dump({"icons": icons}, f, separators=(",", ":"))

    print("  %d icons (%d deprecated dropped)" % (len(kept), dropped))


def render_probe(stage):
    """Hand the font to a built glyphsvg and see whether a glyph comes out.

    This is the check that matters. A truncated .ttf passes a size test and
    `file` reports "TrueType Font data" for a fragment of one, but CoreText
    refuses to load it - and the failure would otherwise surface as an icon
    missing from a picker long after this script reported success."""
    glyphsvg = os.path.join(SCRIPT_DIR, "..", "..", "build", "bin", "glyphsvg")
    if not os.access(glyphsvg, os.X_OK):
        print("  no built glyphsvg to render-probe against - "
              "run ../../build.sh first for a full check")
        return

    shutil.copyfile(os.path.join(SCRIPT_DIR, "glyphset.conf"),
                    os.path.join(stage, "glyphset.conf"))
    with open(os.path.join(stage, CODEPOINTS_NAME)) as f:
        symbol = f.readline().split()[0]
    probe = os.path.join(stage, "probe.svg")
    result = subprocess.run(
        [glyphsvg, "--set=" + stage, symbol, "64", "--output=" + probe],
        capture_output=True, text=True)
    if result.returncode != 0:
        fail("glyphsvg could not render '%s' from the downloaded font - "
             "it is corrupt or truncated" % symbol)
    if not os.path.isfile(probe):
        fail("glyphsvg exited 0 but wrote no SVG for '%s'" % symbol)
    if not os.path.getsize(probe):
        fail("glyphsvg produced an empty SVG for '%s'" % symbol)
    os.remove(probe)
    os.remove(os.path.join(stage, "glyphset.conf"))
    print("  render probe OK (%s)" % symbol)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--include-deprecated", action="store_true",
                        help="keep the icons upstream marks as deprecated")
    args = parser.parse_args()

    # Staged first, installed only after conversion and the probe succeed: a
    # half-written .codepoints beside a good .ttf renders an empty picker rather
    # than failing, and that reads as "the font is broken" when it is only the
    # map that is.
    stage = tempfile.mkdtemp(prefix="mdi.")
    try:
        download(FONT_URL, os.path.join(stage, FONT_NAME))
        download(META_URL, os.path.join(stage, "meta.json"))
        download(LICENSE_URL, os.path.join(stage, "LICENSE"), quiet=True)
        check_font(os.path.join(stage, FONT_NAME))
        convert(stage, args.include_deprecated)
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
