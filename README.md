# glyphsvg

Extract SVG paths from font glyphs on macOS.

## Build

```bash
./build.sh
```

## Usage

### SF Symbols (default mode)

Extract SF Symbols by name from SF Pro Text:

```bash
./build/bin/glyphsvg heart bold 768 --output=heart.svg
```

Arguments: `<name> <weight> <size>`

Weight selects the SF Pro Text font variant. Valid weights: `black`, `bold`, `heavy`, `light`, `medium`, `regular`, `semibold`, `thin`, `ultralight`.

### Glyph sets

A **glyph set** is a directory holding one or more icon-font faces plus the
name-to-codepoint tables that map symbol names onto glyphs. Both variable fonts
(Google Material Symbols) and static fonts (Material Design Icons, Fluent System
Icons) are rendered by the same code path: the font is loaded from its file, asked
which variation axes it declares, and only those axes are applied.

```bash
./build/bin/glyphsvg --set=./fonts/mdi home 256 --output=home.svg
GLYPHSVG_SET_PATH=./fonts ./build/bin/glyphsvg --set=mdi home 256 --output=home.svg
```

Arguments: `--set=<name|dir> [--face=<face>] <name> [<weight>] <size>`

A set given with a `/` in it is a directory path, used as given. A bare name is
looked up as `<root>/<name>/` under each colon-separated entry of
`$GLYPHSVG_SET_PATH`, then under the current directory - and nowhere else. The tool
does not walk up from its own executable or search a user library directory:
naming the data is the caller's job, and a tool that guesses at other locations is
one that can quietly render from a set nobody asked for.

So an app embedding glyphsvg should pass `--set=<absolute dir>`, or set
`$GLYPHSVG_SET_PATH` to the directory holding its sets. This also covers the case
that motivated a wider search - a font downloaded at runtime cannot be written into
a code-signed app bundle, so it goes wherever the app keeps its support files and
the app names that path.

#### The `glyphset.conf` manifest

Each set directory carries a `glyphset.conf`, one `key = value` per line, `#`
starts a comment:

| Key | Meaning |
|---|---|
| `title` | human readable set name, reported by `--info` |
| `font` | font filename, for a set with a single face |
| `codepoints` | default codepoints filename, used by faces that omit their own |
| `metadata` | optional search-metadata filename, for callers to read |
| `default` | face to use when none is requested (default: the first face) |
| `face` | `<name> <fontfile> [codepointsfile]`, repeatable |

A single-face set needs no `face` line; the `font` key gives it a face named
`regular`. Filenames may not contain spaces. Faces whose font file is missing are
dropped, so a partial install still resolves - and because a partial install must
not shadow a complete one, the search prefers the first root whose set actually
has the requested face (by `--face`, or by a weight that names a face), falling
back to any usable set only if none does.

A static family, one file per weight:

```
title      = Material Design Icons
codepoints = mdi.codepoints
face = light    mdi-light.ttf
face = regular  mdi-regular.ttf
face = bold     mdi-bold.ttf
```

A single-face set:

```
title      = Material Design Icons
font       = materialdesignicons-webfont.ttf
codepoints = mdi.codepoints
```

#### The `.codepoints` format

Plain text, one `<name> <hexcodepoint>` per line, e.g. `home f02dc`. This is the
format Google publishes for Material Symbols, and the common format every set uses.

### Google Material Symbols

Material Symbols is a built-in set: a directory holding Google's published
`MaterialSymbols<Style>.ttf` and `.codepoints` files needs no `glyphset.conf`, and
`--material[=<style>]` is an alias for `--set=material [--face=<style>]`.

First fetch the fonts and codepoint maps (one-time, not committed to git):

```bash
./material/download.sh
```

Then extract by name:

```bash
./build/bin/glyphsvg --material home 256 --output=home.svg
./build/bin/glyphsvg --material=rounded settings bold 256 --output=settings.svg
./build/bin/glyphsvg --material=sharp star 256 --weight=600 --output=./icons/
./build/bin/glyphsvg --material favorite 256 --fill --output=favorite.svg
```

Arguments: `--material[=<style>] <name> [<weight>] <size>`

Styles (faces): `outlined` (default), `rounded`, `sharp`. The data is found the
same way as any other set - `material/` under `$GLYPHSVG_SET_PATH` or the current
directory - and `GLYPHSVG_MATERIAL_DIR` names the directory holding the files
outright, which is what a caller running from an arbitrary working directory
should use.

### Font by path

Render straight from a font file, with no set directory or manifest:

```bash
./build/bin/glyphsvg --font-file=./mdi.ttf --codepoints=./mdi.codepoints home 256 --output=home.svg
./build/bin/glyphsvg --font-file=./mdi.ttf U+F02DC 256 --output=home.svg
./build/bin/glyphsvg --font-file=./Inter.ttf "Hello" 100 --output=./out/
```

Arguments:

```
--font-file=<path> --codepoints=<path> <name> [<weight>] <size>
--font-file=<path> <characters|codepoint> <size>
```

With `--codepoints=<path>` the first argument is a symbol name looked up in that
file, and a weight may precede the size; without it, the first argument is
characters or a codepoint. A single font file has a single face, so `--face=` does
not apply here - use `--set` for a family. A `--font=` argument that names
a font file (it contains a `/`, or ends in `.ttf`/`.otf`/`.ttc`/`.otc` and exists)
takes the same by-path route.

### Weight, fill and other variation axes

These apply to the modes that load a font by path:

| Option | Effect | Applies to |
|---|---|---|
| `--face=<name>` | face to render with (`--style=` is an alias) | `--set`, `--material` |
| `--weight=<N\|name>` | the `wght` axis, or the nearest face of a static family | all three |
| `--fill[=<0..1>]` | the `FILL` axis; default is outline (`0`) | all three |
| `--axis=<TAG>=<N>` | any other variation axis, e.g. `--axis=GRAD=200` | all three |
| `--info` | print the resolved paths, faces and axes, then exit | all three |

A weight may also be given positionally, before the size. Named weights are the SF
Symbols names (`ultralight` 100 through `black` 900) and map onto the `wght` axis,
clamped to that font's declared range.

An axis a font does not declare is ignored, which is what lets one code path serve
variable and static fonts:

- On a **variable font** the axes do the work. `--weight`/`--fill`/`--axis` set
  `wght`/`FILL`/anything else, each clamped to the font's own range for that axis.
  An axis you did *not* ask about is set to the font's own declared default, never
  to this tool's idea of "regular" - a font whose `wght` axis runs 0.48 to 3.2 is
  rendered at its default instance, not pinned to an endpoint.
- On a **static font** there are no axes, so the *face* does the work. A weight
  matching a face name selects that face, and any other weight selects the nearest
  face the set actually offers, measured by what each face name means as a weight
  (`--weight=800` on a `regular`/`bold`/`black` family picks `bold`, ties going to
  the lighter face). Nearest-face matching applies only when *every* face is named
  for a weight - a set mixing styles and weights (`Outlined`, `bold`) is not
  weight-indexed, so the weight goes to the axis and the default face is used. A
  weight or fill that nothing can honor is reported as a warning, not an error.
- The two are not exclusive. A set may pick a face by weight *and* be variable, in
  which case the face selects the file and the axis still moves within it.
- `--axis=` wins over `--weight`/`--fill` when both name the same axis.

`--info` is the way to find out which of the two you are dealing with. It prints
`key: value` lines meant to be parsed:

```
$ glyphsvg --material=rounded --info
set: material
title: Google Material Symbols
dir: /path/to/material
faces: Outlined Rounded Sharp
face: Rounded
font: /path/to/material/MaterialSymbolsRounded.ttf
codepoints: /path/to/material/MaterialSymbolsRounded.codepoints
metadata: /path/to/material/material_symbols_metadata.json
postscript-name: MaterialSymbolsRounded-Regular_Thin
glyphs: 6587
variable: yes
axis: FILL 0 0 1 Fill
axis: GRAD -50 0 200 Grade
axis: opsz 20 24 48 Optical Size
axis: wght 100 400 700 Weight
```

Each `axis:` line is `<tag> <min> <default> <max> <name>`. A static font reports
`variable: no` and no axis lines, so a UI can offer a face list instead of a weight
control.

### Custom Fonts

Extract glyphs from any installed font using character input or codepoint:

```bash
./build/bin/glyphsvg --font=Helvetica "Hello" 100 --output=./output/
./build/bin/glyphsvg --font=Helvetica U+0041 100 --output=A.svg
```

Arguments: `--font=<name|path> <characters|codepoint> <size>`

Codepoint format: `U+XXXX` or `0xXXXX`

### Output

- `--output=<path>` specifies output file or directory (a trailing `/` or an
  existing directory is treated as a directory)
- If output is a directory, each glyph is saved as a separate file. The name is
  the symbol name when the glyph was looked up by name (e.g. `home.svg`), the
  codepoint and index for character input (e.g. `U+48_0.svg`), and the codepoint
  alone otherwise (e.g. `U+0041.svg`)
- With no `--output`, a single glyph is written to stdout
- When extracting multiple characters, you must specify an output directory
