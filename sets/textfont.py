#!/usr/bin/env python3
"""Shared provisioning for TEXT fonts - the sets whose symbols are characters.

An icon font maps invented names ("home", "ab-testing") onto private-use
codepoints, so its set needs a name table published by its vendor. A text font
already has the mapping every reader knows: its cmap. So the .codepoints table
here is generated from the font itself, and the name of each entry is simply the
character - 'Q 51', 'A 41'. Typing "Q" in the picker finds Q.

The Unicode character name goes in the metadata sidecar as search tags rather
than into the .codepoints file. Two reasons, both load-bearing:

  * ICEdit's picker lists one row per .codepoints line, so emitting both 'Q 51'
    and 'latin-capital-letter-q 51' would show every character twice.
  * The sidecar is already how MDI and Phosphor carry their aliases, so the
    existing search-index loader serves these sets with no change.

Nothing here needs fontTools. The four text sets would be the only part of this
repo with a pip dependency, and a cmap reader that handles formats 4 and 12 is
about eighty lines - the same table layout in .ttf and .otf, since cmap is
independent of how the outlines are stored.
"""

import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unicodedata

# WOFF/WOFF2 are deliberately absent: they are compressed wrappers, not sfnt,
# and _tables would read one as a table directory and report a misleading "no
# cmap table". Nothing upstream serves them, and an explicit rejection beats a
# confusing parse.
FONT_MAGIC = (b"\x00\x01\x00\x00", b"true", b"ttcf", b"OTTO")

# Categories with no business in a glyph picker: controls, formatting,
# surrogates, private use, unassigned, every kind of separator (a .codepoints
# name may not contain whitespace), and combining marks, which on their own
# render as an accent floating over nothing.
SKIP_CATEGORIES = {"Cc", "Cf", "Cs", "Co", "Cn", "Zs", "Zl", "Zp", "Mn", "Mc", "Me"}


def fail(message):
    sys.stderr.write("error: %s\n" % message)
    raise SystemExit(1)


def human_size(path):
    size = os.path.getsize(path)
    if size >= 1048576:
        return "%.1fM" % (size / 1048576.0)
    if size >= 1024:
        return "%dK" % (size // 1024)
    return "%dB" % size


# ---------------------------------------------------------------------------
# Minimal sfnt cmap reader
# ---------------------------------------------------------------------------

def _tables(data):
    if data[:4] == b"ttcf":
        fail("TrueType collections are not supported here - name a single font")
    if len(data) < 12:
        fail("font file is too short to hold an sfnt header")
    num_tables = struct.unpack(">H", data[4:6])[0]
    tables = {}
    for i in range(num_tables):
        base = 12 + i * 16
        if base + 16 > len(data):
            fail("truncated sfnt table directory")
        tag, _, offset, length = struct.unpack(">4sIII", data[base:base + 16])
        tables[tag] = (offset, length)
    return tables


def _cmap_format4(data, off):
    seg_x2 = struct.unpack(">H", data[off + 6:off + 8])[0]
    n = seg_x2 // 2
    ends = struct.unpack(">%dH" % n, data[off + 14:off + 14 + seg_x2])
    s = off + 16 + seg_x2                       # skip reservedPad
    starts = struct.unpack(">%dH" % n, data[s:s + seg_x2])
    d = s + seg_x2
    deltas = struct.unpack(">%dh" % n, data[d:d + seg_x2])
    r = d + seg_x2
    ranges = struct.unpack(">%dH" % n, data[r:r + seg_x2])
    out = {}
    for i in range(n):
        if starts[i] > ends[i]:
            continue
        for cp in range(starts[i], min(ends[i], 0xFFFF) + 1):
            if ranges[i] == 0:
                gid = (cp + deltas[i]) & 0xFFFF
            else:
                gi = r + i * 2 + ranges[i] + (cp - starts[i]) * 2
                if gi + 2 > len(data):
                    continue
                gid = struct.unpack(">H", data[gi:gi + 2])[0]
                if gid:
                    gid = (gid + deltas[i]) & 0xFFFF
            if gid:
                out[cp] = gid
    return out


# Unicode has 0x110000 codepoints, so no honest cmap enumerates more than that.
# The cap is on TOTAL work rather than per-group size: a few thousand groups
# each spanning the whole range are individually plausible and collectively run
# for hours, in a file small enough to look innocuous.
_MAX_CMAP_ENTRIES = 0x110000 * 2


def _cmap_format12(data, off):
    n_groups = struct.unpack(">I", data[off + 12:off + 16])[0]
    out, budget = {}, _MAX_CMAP_ENTRIES
    for i in range(n_groups):
        b = off + 16 + i * 12
        start, end, gid = struct.unpack(">III", data[b:b + 12])
        if end < start:
            fail("cmap format 12 group %d runs backwards - file is corrupt" % i)
        budget -= (end - start + 1)
        if budget < 0:
            fail("cmap format 12 enumerates more than %d codepoints - file is "
                 "corrupt" % _MAX_CMAP_ENTRIES)
        for k, cp in enumerate(range(start, end + 1)):
            out[cp] = gid + k
    return out


def read_cmap(path):
    """Return {codepoint: glyph id} from the best available cmap subtable."""
    try:
        return _read_cmap(path)
    except struct.error as exc:
        # Every read below is bounds-checked against a well-formed font, but a
        # truncated download can keep a valid magic and still run off the end
        # mid-table. That is a corrupt file, which is a message, not a traceback.
        fail("%s is truncated or corrupt (%s)" % (os.path.basename(path), exc))


def _read_cmap(path):
    with open(path, "rb") as f:
        data = f.read()
    tables = _tables(data)
    if b"cmap" not in tables:
        fail("%s has no cmap table" % os.path.basename(path))
    base = tables[b"cmap"][0]
    count = struct.unpack(">H", data[base + 2:base + 4])[0]
    subtables = []
    for i in range(count):
        rec = base + 4 + i * 8
        plat, enc, off = struct.unpack(">HHI", data[rec:rec + 8])
        subtables.append((plat, enc, base + off))
    # Preference order: full-repertoire Unicode first, then BMP. A font offering
    # both must be read from format 12, or every astral character is lost.
    def rank(t):
        plat, enc, _ = t
        if (plat, enc) in ((3, 10), (0, 4), (0, 6)):
            return 0
        if (plat, enc) in ((3, 1), (0, 3)):
            return 1
        if plat == 0:
            return 2
        return 3
    for plat, enc, off in sorted(subtables, key=rank):
        fmt = struct.unpack(">H", data[off:off + 2])[0]
        if fmt == 12:
            return _cmap_format12(data, off)
        if fmt == 4:
            return _cmap_format4(data, off)
    fail("%s has no cmap subtable in format 4 or 12" % os.path.basename(path))


# ---------------------------------------------------------------------------
# Table generation
# ---------------------------------------------------------------------------

def build_tables(font_path):
    """Return (rows, skipped) where rows is [(char, codepoint, unicode_name)].

    Sorted by codepoint so the file is stable across runs - a regenerated table
    that reshuffles produces a diff nobody can read."""
    cmap = read_cmap(font_path)
    rows, skipped = [], 0
    for cp in sorted(cmap):
        # Surrogates are not characters; chr() accepts them but unicodedata does
        # not, and nothing downstream can render one.
        if 0xD800 <= cp <= 0xDFFF:
            skipped += 1
            continue
        ch = chr(cp)
        if unicodedata.category(ch) in SKIP_CATEGORIES:
            skipped += 1
            continue
        # Belt and braces. The category filter above already excludes every
        # separator, but the .codepoints format splits on whitespace and a name
        # with a space in it would silently map to the wrong codepoint rather
        # than fail, so this is checked directly rather than inferred.
        if not ch.strip() or ch.split() != [ch]:
            skipped += 1
            continue
        try:
            uname = unicodedata.name(ch)
        except ValueError:
            uname = ""
        rows.append((ch, cp, uname))
    if not rows:
        fail("%s yielded no usable characters" % os.path.basename(font_path))
    return rows, skipped


def write_tables(rows, codepoints_path, metadata_path):
    with open(codepoints_path, "w") as f:
        for ch, cp, _ in rows:
            f.write("%s %x\n" % (ch, cp))
    icons = []
    for ch, cp, uname in rows:
        # "LATIN CAPITAL LETTER Q" -> ["latin","capital","letter","q"], so
        # searching "capital q" or just "latin" finds it. The U+ form is there
        # for anyone who thinks in codepoints.
        tags = [t.lower() for t in uname.replace("-", " ").split()] if uname else []
        tags.append("u+%04x" % cp)
        icons.append({"name": ch, "tags": tags, "categories": []})
    with open(metadata_path, "w") as f:
        json.dump({"icons": icons}, f, separators=(",", ":"))


# ---------------------------------------------------------------------------
# Download / verify / install
# ---------------------------------------------------------------------------

def check_font(path):
    """Reject anything that is not a font. curl -f only rejects HTTP >= 400, and
    a captive portal answers 200 with HTML."""
    with open(path, "rb") as f:
        magic = f.read(4)
    if magic in (b"wOFF", b"wOF2"):
        fail("%s is WOFF, which is a compressed wrapper rather than sfnt - "
             "fetch the .ttf or .otf" % os.path.basename(path))
    if magic not in FONT_MAGIC:
        fail("%s is not a font (starts with %r) - a captive portal or proxy "
             "likely intercepted the download" % (os.path.basename(path), magic))


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


def render_probe(script_dir, stage, font_name, samples="QAg8"):
    """Render a few characters and, for a variable font, check that the weight
    axis actually changes the outline.

    "It rendered something" is not an oracle - that lesson cost the Phosphor set
    a whole face. Here the specific risk is a set that resolves and renders but
    ignores --weight, which would leave ICEdit's weight control inert with
    nothing to show for it."""
    glyphsvg = os.path.join(script_dir, "..", "..", "build", "bin", "glyphsvg")
    if not os.access(glyphsvg, os.X_OK):
        print("  no built glyphsvg to render-probe against - "
              "run ../../build.sh first for a full check")
        return
    shutil.copyfile(os.path.join(script_dir, "glyphset.conf"),
                    os.path.join(stage, "glyphset.conf"))

    def render(ch, weight=None):
        out = os.path.join(stage, "probe.svg")
        args = [glyphsvg, "--set=" + stage, ch, "256", "--output=" + out]
        if weight is not None:
            args.append("--weight=%s" % weight)
        r = subprocess.run(args, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.isfile(out):
            fail("glyphsvg could not render %r from %s - the download is "
                 "corrupt, or the generated table disagrees with the font"
                 % (ch, font_name))
        body = open(out, "rb").read()
        os.remove(out)
        if not body:
            fail("glyphsvg produced an empty SVG for %r" % ch)
        return body

    for ch in samples:
        print("  render probe OK (%s, %d bytes)" % (ch, len(render(ch))))

    probe = subprocess.run([glyphsvg, "--set=" + stage, "--info"],
                           capture_output=True, text=True)
    if probe.returncode != 0:
        # Without this an --info that failed reports no axis lines, which reads
        # as "static font" and skips the only check this function exists for.
        fail("glyphsvg --info failed for %s, so the weight axis could not be "
             "checked: %s" % (font_name, probe.stderr.strip()[:200]))
    axis = [l for l in probe.stdout.splitlines() if l.startswith("axis: wght ")]
    if axis:
        _, lo, _, hi, _ = (axis[0][len("axis: "):].split(None, 4) + [""])[:5]
        light, heavy = render(samples[0], lo), render(samples[0], hi)
        if light == heavy:
            fail("%s declares a wght axis %s..%s but renders %r identically at "
                 "both ends - the axis is not being applied, and a weight "
                 "control over this set would do nothing"
                 % (font_name, lo, hi, samples[0]))
        print("  weight axis OK (wght %s vs %s differ)" % (lo, hi))
    else:
        print("  static font - no weight axis to probe")
    os.remove(os.path.join(stage, "glyphset.conf"))


def install(script_dir, stage, names):
    """All or nothing: failing partway through leaves the set half-replaced."""
    staged = []
    for name in names:
        pending = os.path.join(script_dir, "." + name + ".new")
        shutil.copyfile(os.path.join(stage, name), pending)
        staged.append((pending, os.path.join(script_dir, name)))
    for pending, final in staged:
        os.replace(pending, final)


def provision(script_dir, font_url, font_name, codepoints_name, metadata_name,
              license_url=None, license_name="LICENSE", samples="QAg8"):
    """Fetch one text font, generate its tables, probe it, install it."""
    installed = [font_name, codepoints_name, metadata_name]
    stage = tempfile.mkdtemp(prefix="textfont.")
    try:
        font_path = os.path.join(stage, font_name)
        download(font_url, font_path)
        check_font(font_path)

        rows, skipped = build_tables(font_path)
        write_tables(rows, os.path.join(stage, codepoints_name),
                     os.path.join(stage, metadata_name))
        named = sum(1 for _, _, u in rows if u)
        print("  %d characters (%d non-printing skipped, %d with Unicode names)"
              % (len(rows), skipped, named))

        if license_url:
            download(license_url, os.path.join(stage, license_name), quiet=True)
            installed.append(license_name)
        render_probe(script_dir, stage, font_name, samples)
        install(script_dir, stage, installed)
    finally:
        shutil.rmtree(stage, ignore_errors=True)

    print("\nDownloaded into %s:" % script_dir)
    for name in installed:
        print("  %-40s %s" % (name, human_size(os.path.join(script_dir, name))))
    return 0


# ---------------------------------------------------------------------------
# Standalone entry point
#
# The cmap reader is the reusable part of this file, so it is runnable on its
# own rather than only from a set's download.py: pointed at any .ttf/.otf it
# regenerates a .codepoints table and metadata sidecar without touching the
# network. Use it to rebuild a set's tables from the font already on disk, to
# check what a font would contribute before adding a set for it, or to diff a
# regenerated table against the installed one after changing the filters.
#
#   python3 sets/textfont.py <font.ttf> [<out.codepoints> <out_metadata.json>]
#
# With no output paths it reports what it found and writes nothing.
# ---------------------------------------------------------------------------

def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        sys.stderr.write(
            "usage: textfont.py <font.ttf|.otf> [<out.codepoints> "
            "<out_metadata.json>]\n"
            "  With no output paths, reports coverage and writes nothing.\n")
        return 2
    font = argv[0]
    if not os.path.isfile(font):
        fail("no such font file: %s" % font)
    check_font(font)
    rows, skipped = build_tables(font)
    named = sum(1 for _, _, u in rows if u)
    print("%s: %d characters, %d non-printing skipped, %d with Unicode names"
          % (os.path.basename(font), len(rows), skipped, named))
    print("  first: %s  last: %s"
          % (" ".join(r[0] for r in rows[:12]), " ".join(r[0] for r in rows[-6:])))
    if len(argv) == 1:
        return 0
    if len(argv) != 3:
        fail("give BOTH output paths, or neither")
    write_tables(rows, argv[1], argv[2])
    print("  wrote %s and %s" % (argv[1], argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
