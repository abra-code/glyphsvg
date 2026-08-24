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
./sets/phosphor/download.py
GLYPHSVG_SET_PATH=./sets ./build/bin/glyphsvg --set=mdi home 256 --output=home.svg
```

Run `../build.sh` first if you can: each script render-probes the font it just
fetched, and without a built binary that check is skipped.

| Set | Icons | Faces | Font | License |
|---|---|---|---|---|
| `mdi` | 7188 | `regular` | 1.3 MB | Pictogrammers Free License (Apache 2.0 terms) |
| `fluent` | 2819 + 2859 | `regular`, `filled` | 1.5 MB | MIT |
| `phosphor` | 1512 x 5 | `thin`, `light`, `regular`, `bold`, `fill` | 2.4 MB | MIT |

All three are static fonts: none declares a variation axis, so `--weight`,
`--fill` and `--axis` have nothing to act on and are reported as warnings. Use
`--face` instead. `--info` reports `variable: no` for all of them, which is how
a caller can tell a face list is the right control to offer.

**Phosphor is the one that has weights.** MDI ships a single weight and Fluent
two styles at one weight, so a weight control over either can only offer what is
there. Icon artwork usually wants a heavier stroke than a UI icon font's default,
and Phosphor supplies thin through bold plus a solid `fill` - as five separate
fonts, so the weight is a face rather than an axis.

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

## phosphor - Phosphor Icons

Five static fonts, one per weight, listed in the manifest light to heavy so a
picker built from the face list reads as a ramp. 1512 icons in every face.

Every face except `regular` suffixes its names upstream - the bold font calls its
icons `acorn-bold`, `airplane-bold` - so the converter strips the suffix per
face, and asserts it was there before stripping rather than letting the strip
become a silent no-op if upstream's naming ever changes.

All five faces share **one** codepoints table. Upstream assigns a given icon the
same codepoint in every weight, so five separate tables were five identical
files. `download.py` verifies the tables match and fails if they ever diverge,
which is the point at which per-face tables would be needed again.

`phosphor_metadata.json` carries the 18 icons that have aliases, in the same
shape the MDI sidecar uses, so a search for "activity" finds `pulse` and one
loader serves both sets.

### Weight is a face here, and `--weight` is half-usable

The face list mixes weight names with a style name (`fill`), so glyphsvg does not
treat this set as weight-indexed. The consequence is asymmetric and worth stating
plainly:

- `--weight=bold` **does** select the bold face, and is equivalent to
  `--face=bold`. Named weights are matched against face names.
- `--weight=700` does **not**. A static font has no `wght` axis, so a numeric
  weight is reported as an ignored-request warning and the default face renders.

`--face` is the unambiguous control.

### Why duotone is not here

Phosphor publishes a sixth family, `duotone`, and it is deliberately not
provisioned. Its two tones are not two paths inside one glyph - they are **two
separate glyphs at consecutive codepoints**, and the codepoint `selection.json`
publishes is the 20%-opacity tint layer, not the icon. Rendering it gives an
unrecognizable blob; rendering `code + 1` gives something byte-identical to the
regular face. So the choice would be shipping wrong artwork or shipping a
duplicate. Carrying it properly needs two-glyph composition with per-path
opacity, which glyphsvg does not do.

The tell is visible in the data: 1504 of the 1512 duotone codepoints are even,
because the space is allocated two per icon to leave the odd slot for the second
layer.

`download.py`'s render probe now compares the faces against each other rather
than only checking that each produced a non-empty file, and refuses a face whose
render is a small fraction of its siblings. That is what catches this class of
defect - "it rendered something" never would.

## A note on trademarks

All three sets include brand and company logos (`slack`, `github`,
`apple-logo`, `windows-logo` and many more). A permissive font license covers the artwork's copyright and grants
nothing on third-party trademarks. That distinction matters most for exactly the
use this tool serves - an app icon is trademark use.
