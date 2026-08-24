#!/usr/bin/env python3
"""Provision the Phosphor Icons glyph set.

Downloads the five Phosphor weight/style fonts and converts each one's IcoMoon
selection.json into a .codepoints map. Neither the fonts nor the generated maps
are committed (see ../../.gitignore), so run this once before using the set.

Source:  https://github.com/phosphor-icons/web
License: MIT.

This is the bundled family that ships real WEIGHTS. MDI and Fluent are
single-weight fonts, so a weight control over either has nothing to act on -
which matters because app icons generally want a heavier stroke than a UI icon
font's default. Phosphor supplies thin / light / regular / bold, plus fill as a
solid style.

Two conversions are needed:

  1. Codepoints are decimal in selection.json and must be hex in a .codepoints
     file.
  2. Every non-regular weight suffixes its names: the bold font calls its icons
     "acorn-bold", "airplane-bold". Stripping that leaves one shared vocabulary
     across all five faces, so switching weight in a picker keeps the selected
     name valid instead of emptying the list.

Upstream assigns the SAME codepoint to a given icon in every weight, so one
table serves all five faces. That is verified here, not assumed: if the weights
ever diverge this fails rather than silently rendering the wrong glyph from four
of the five fonts.

Phosphor's "duotone" family is deliberately not provisioned. Its two tones are
two separate glyphs at consecutive codepoints, and the codepoint upstream
publishes in selection.json is the 20%-opacity TINT layer, not the icon:
rendering it produces an unrecognizable blob, and rendering code+1 produces
something byte-identical to the regular face. Supporting it properly needs
two-glyph composition with per-path opacity, which glyphsvg does not do.
"""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BASE_URL = "https://raw.githubusercontent.com/phosphor-icons/web/master/src"
LICENSE_URL = "https://raw.githubusercontent.com/phosphor-icons/web/master/LICENSE"

# face -> (upstream directory, font filename). Regular is the odd one out in
# both: its directory is "regular" but its font carries no suffix at all.
FACES = [
    ("thin", "thin", "Phosphor-Thin.ttf"),
    ("light", "light", "Phosphor-Light.ttf"),
    ("regular", "regular", "Phosphor.ttf"),
    ("bold", "bold", "Phosphor-Bold.ttf"),
    ("fill", "fill", "Phosphor-Fill.ttf"),
]

CODEPOINTS_NAME = "phosphor.codepoints"
METADATA_NAME = "phosphor_metadata.json"

INSTALLED = [f for _, _, f in FACES] + [CODEPOINTS_NAME, METADATA_NAME, "LICENSE"]

FONT_MAGIC = (b"\x00\x01\x00\x00", b"true", b"ttcf", b"OTTO", b"wOFF", b"wOF2")


def human_size(path):
    size = os.path.getsize(path)
    if size >= 1048576:
        return "%.1fM" % (size / 1048576.0)
    if size >= 1024:
        return "%dK" % (size // 1024)
    return "%dB" % size


def fail(message):
    sys.stderr.write("error: %s\n" % message)
    raise SystemExit(1)


def check_font(path):
    """Reject anything that is not a font. curl -f only rejects HTTP >= 400, and
    a captive portal answers 200 with HTML."""
    with open(path, "rb") as f:
        magic = f.read(4)
    if magic not in FONT_MAGIC:
        fail("%s is not a font (starts with %r) - a captive portal or proxy "
             "likely intercepted the download" % (os.path.basename(path), magic))


def check_name(name):
    if not name or name.split() != [name]:
        fail("upstream symbol name %r contains whitespace - the .codepoints "
             "format cannot represent it" % name)


def download(url, dest, quiet=False):
    print("Downloading %s ..." % os.path.basename(dest))
    result = subprocess.run([
        "/usr/bin/curl", "-fgL", "--retry", "3",
        "--connect-timeout", "30", "--max-time", "900", "--show-error",
        "--silent" if quiet else "--progress-bar",
        "-o", dest, url,
    ])
    if result.returncode != 0:
        fail("download failed: %s" % url)


def convert(stage, face):
    """One selection.json -> {name: codepoint} plus {name: [aliases]}."""
    path = os.path.join(stage, "selection-%s.json" % face)
    with open(path) as f:
        data = json.load(f)
    if not isinstance(data, dict):
        fail("%s is not the expected JSON object" % os.path.basename(path))
    icons = data.get("icons")
    if not isinstance(icons, list) or not icons:
        fail("%s has no icons array" % os.path.basename(path))

    # Every face but "regular" suffixes its names with its own weight.
    suffix = "" if face == "regular" else "-" + face

    table, aliases, malformed = {}, {}, []
    for icon in icons:
        props = icon.get("properties", {}) if isinstance(icon, dict) else {}
        raw, code = props.get("name"), props.get("code")
        if not raw or not code:
            malformed.append(repr(props)[:60])
            continue
        # IcoMoon stores aliases comma-separated in one name field; the first is
        # the canonical Phosphor name ("pulse, activity").
        parts = [p.strip() for p in raw.split(",") if p.strip()]
        name = parts[0]
        # Assert the suffix was there before stripping it. Without this the
        # strip is a silent no-op the day upstream changes its naming, and four
        # of the five faces would quietly get a table of names their font does
        # not contain.
        if suffix:
            if not name.endswith(suffix):
                fail("upstream name %r in the %s face does not end in %r - the "
                     "naming changed and this converter needs rechecking"
                     % (name, face, suffix))
            name = name[:-len(suffix)]
        check_name(name)
        if not isinstance(code, int) or isinstance(code, bool) or code <= 0:
            fail("upstream codepoint %r for %r is not a positive integer" % (code, raw))
        # Any duplicate at all, not just one mapping to a different code: two
        # icons collapsing onto one name is the failure worth catching, and a
        # collision that happens to share a code is still a lost icon.
        if name in table:
            fail("name collision after stripping %r: %r appears twice in the %s "
                 "face" % (suffix or "nothing", name, face))
        table[name] = code
        extra = [re.sub(re.escape(suffix) + "$", "", a) for a in parts[1:]] if suffix else parts[1:]
        if extra:
            aliases[name] = extra

    if malformed:
        fail("%d entries in the %s face had no name or no code, e.g. %s"
             % (len(malformed), face, malformed[0]))
    if not table:
        fail("no icons found for the %s face" % face)
    return table, aliases


def render_probe(stage):
    """Render the SAME symbol from every face and compare the results.

    "It rendered something" is not an oracle. The duotone family was dropped
    because its published codepoint is a 20%-opacity tint layer: it renders
    fine, exits 0, and produces an unrecognizable blob. Nothing size-based or
    status-based catches that. What does catch it is comparing the faces against
    each other - a face that is a small fraction of its siblings is either the
    wrong glyph or the wrong font, and a face byte-identical to another is a
    duplicate. Both are reported here rather than left for someone to notice in
    a picker."""
    glyphsvg = os.path.join(SCRIPT_DIR, "..", "..", "build", "bin", "glyphsvg")
    if not os.access(glyphsvg, os.X_OK):
        print("  no built glyphsvg to render-probe against - "
              "run ../../build.sh first for a full check")
        return
    shutil.copyfile(os.path.join(SCRIPT_DIR, "glyphset.conf"),
                    os.path.join(stage, "glyphset.conf"))
    with open(os.path.join(stage, CODEPOINTS_NAME)) as f:
        symbol = f.readline().split()[0]

    renders = {}
    for face, _, _ in FACES:
        probe = os.path.join(stage, "probe-%s.svg" % face)
        result = subprocess.run(
            [glyphsvg, "--set=" + stage, "--face=" + face, symbol, "256",
             "--output=" + probe], capture_output=True, text=True)
        if result.returncode != 0:
            fail("glyphsvg could not render '%s' from the %s face - the "
                 "download is corrupt or truncated" % (symbol, face))
        if not os.path.isfile(probe):
            fail("glyphsvg exited 0 but wrote no SVG for '%s' (%s)" % (symbol, face))
        body = open(probe, "rb").read()
        if not body:
            fail("glyphsvg produced an empty SVG for '%s' (%s)" % (symbol, face))
        renders[face] = body
        os.remove(probe)

    for face, body in renders.items():
        twins = [o for o, b in renders.items() if o != face and b == body]
        if twins:
            fail("the %s and %s faces render '%s' identically - two faces are "
                 "pointing at the same font, or at the same glyph"
                 % (face, twins[0], symbol))

    sizes = sorted(len(b) for b in renders.values())
    median = sizes[len(sizes) // 2]
    for face, body in renders.items():
        if len(body) * 3 < median:
            fail("the %s face renders '%s' as %d bytes against a median of %d - "
                 "that is not the same icon. Phosphor's duotone family fails "
                 "exactly this way, its published codepoint being a tint layer "
                 "rather than the glyph." % (face, symbol, len(body), median))
        print("  render probe OK (%s, %s, %d bytes)" % (face, symbol, len(body)))
    os.remove(os.path.join(stage, "glyphset.conf"))


def install(stage, names):
    """All or nothing: failing partway through leaves the set half-replaced."""
    staged = []
    for name in names:
        pending = os.path.join(SCRIPT_DIR, "." + name + ".new")
        shutil.copyfile(os.path.join(stage, name), pending)
        staged.append((pending, os.path.join(SCRIPT_DIR, name)))
    for pending, final in staged:
        os.replace(pending, final)


def main():
    stage = tempfile.mkdtemp(prefix="phosphor.")
    try:
        tables, alias_map = {}, {}
        for face, directory, font in FACES:
            download("%s/%s/%s" % (BASE_URL, directory, font),
                     os.path.join(stage, font))
            check_font(os.path.join(stage, font))
            download("%s/%s/selection.json" % (BASE_URL, directory),
                     os.path.join(stage, "selection-%s.json" % face))
            tables[face], aliases = convert(stage, face)
            print("  %s: %d icons" % (face, len(tables[face])))
            if face == "regular":
                alias_map = aliases

        # One table for all five faces, which is only sound while upstream keeps
        # the weights in lockstep. Verified, not assumed: a divergence is fatal
        # here, because the alternative is four fifths of the set rendering the
        # wrong glyph with nothing to show for it.
        base = tables["regular"]
        for face, _, _ in FACES:
            if tables[face] != base:
                missing = sorted(set(base) - set(tables[face]))[:5]
                moved = sorted(n for n in set(base) & set(tables[face])
                               if base[n] != tables[face][n])[:5]
                fail("the %s face no longer matches regular - %d names missing "
                     "(e.g. %s), %d codepoints moved (e.g. %s). The faces need "
                     "separate codepoints tables again."
                     % (face, len(set(base) - set(tables[face])), missing,
                        len(moved), moved))

        with open(os.path.join(stage, CODEPOINTS_NAME), "w") as f:
            for name in sorted(base):
                f.write("%s %x\n" % (name, base[name]))

        # Aliases as a search sidecar, in the same shape the MDI converter emits
        # so one search-index loader serves both. Without it a picker search for
        # "activity" finds nothing, because Phosphor calls that icon "pulse".
        icons = [{"name": n, "tags": alias_map.get(n, []), "categories": []}
                 for n in sorted(base)]
        with open(os.path.join(stage, METADATA_NAME), "w") as f:
            json.dump({"icons": icons}, f, separators=(",", ":"))
        print("  %d icons, %d carrying aliases" % (len(base), len(alias_map)))

        download(LICENSE_URL, os.path.join(stage, "LICENSE"), quiet=True)
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
