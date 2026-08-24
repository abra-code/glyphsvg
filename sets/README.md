# Glyph Sets

Each subdirectory here is a glyph set: a `glyphset.conf` manifest plus the icon
font and name-to-codepoint tables it names. See the "Glyph sets" section of
`../README.md` for the manifest format and how a set is found.

The manifests and the download scripts are committed. The fonts, the generated
`.codepoints` maps, the metadata sidecars and the vendored `LICENSE` files are
not (see `../.gitignore`), so run each set's `download.py` once before use:

```bash
./sets/mdi/download.py
./sets/fluent/download.py
GLYPHSVG_SET_PATH=./sets ./build/bin/glyphsvg --set=mdi home 256 --output=home.svg
```

Run `../build.sh` first if you can: each script render-probes the font it just
fetched, and without a built binary that check is skipped.

| Set | Icons | Faces | Font | License |
|---|---|---|---|---|
| `mdi` | 7188 | `regular` | 1.3 MB | Pictogrammers Free License (Apache 2.0 terms) |
| `fluent` | 2819 + 2859 | `regular`, `filled` | 1.5 MB | MIT |

Both are static fonts: neither declares a variation axis, so `--weight`,
`--fill` and `--axis` have nothing to act on and are reported as warnings. Use
`--face` instead. `--info` reports `variable: no` for both, which is how a
caller can tell a face list is the right control to offer.

## mdi - Pictogrammers Material Design Icons

Names are hyphenated (`ab-testing`, `airplane-plus`), which is worth knowing if
you are writing a search filter: splitting a name into words needs to break on
`-`, not just `_`.

`download.py` converts upstream's `meta.json` into `mdi.codepoints` and a
`mdi_metadata.json` sidecar. The sidecar uses the same
`{"icons":[{name,tags,categories}]}` shape Google publishes for Material
Symbols, so one search-index loader serves both sets. MDI's own `tags` are
category paths ("Developer / Languages") and its `aliases` are synonyms, so they
map onto `categories` and `tags` respectively. 5388 of the icons carry
categories and 3603 carry aliases.

Codepoints are written exactly as upstream spells them - uppercase, `F02DC`.
glyphsvg parses them with `strtoul(base 16)`, which is case-blind, so a set may
use either case and converting would only make this file disagree with its
source. They sit in Supplementary PUA-A (`U+F0001`..`U+F1D17`), which is past
the BMP, so every lookup goes through the surrogate-pair path.

259 deprecated icons are dropped. Upstream marks them for removal or
replacement, and offering them in a picker is a trap for artwork meant to ship.
`--include-deprecated` keeps them, giving the full 7447.

## fluent - Microsoft Fluent System Icons

This is the **Resizable** family, not the per-size ones. Fluent publishes each
icon at up to seven optical sizes (12, 16, 20, 24, 28, 32, 48), so the Regular
family's 9708 entries are only 2915 distinct icons - the same picture repeated
under names differing by a number. Resizable is the size-independent cut, which
is the right one for icon artwork and also the smaller download.

Upstream names every entry `ic_fluent_<concept>_<size>_<style>`, e.g.
`ic_fluent_access_time_20_regular`. `download.py` strips all three affixes to
leave `access_time`. In Resizable the size is always 20 and therefore
vestigial; the converter anchors on exactly `_20_` rather than `_<digits>_` so
that if upstream ever mixes sizes into this family the parse fails loudly
instead of silently collapsing two icons onto one name. It also asserts that no
two names collide after stripping - they do not today, in either face.

Filled and outlined are separate glyphs at separate codepoints, not a `FILL`
axis, so they are modeled as two faces over the same `.ttf` - which is why both
`face` lines in the manifest name the same font file and differ only in their
codepoints table. `--fill` does nothing here; `--face=filled` is the control.

There is no metadata sidecar: Fluent publishes no tags, synonyms or categories,
so search against this set degrades to name matching only.

Coverage is not the same in both faces - 2811 icons exist in both, 8 are
outline-only and 48 are filled-only - so a UI that keeps the selected name while
switching face has to handle the name going missing.

## A note on trademarks

Both sets include brand and company logos (`slack`, `github`, `microsoft` and
many more). A permissive font license covers the artwork's copyright and grants
nothing on third-party trademarks. That distinction matters most for exactly the
use this tool serves - an app icon is trademark use.
