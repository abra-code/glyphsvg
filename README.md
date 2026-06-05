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

### Google Material Symbols

Extract Material Symbols by name from Google's variable fonts. First fetch the
fonts and codepoint maps (one-time, not committed to git):

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

- **Style** (part of the flag): `outlined` (default), `rounded`, `sharp`.
- **Weight** is optional. Give an SF Symbols-style name positionally (`ultralight`,
  `thin`, `light`, `regular`, `medium`, `semibold`, `bold`, `heavy`, `black`) or an
  explicit numeric value with `--weight=<N>`. Both map onto the font's `wght`
  variation axis (clamped to Material's 100–700 range; `heavy`/`black` clamp to 700).
  Defaults to `regular` (400).
- **Fill** via the `FILL` variation axis: `--fill` renders the solid variant, or
  `--fill=<0..1>` a partial fill. Defaults to outline (`0`).
- When the output is a directory, the file is named after the symbol (e.g. `home.svg`).

The tool locates the downloaded data in `material/` relative to the executable.
Override the location with the `GLYPHSVG_MATERIAL_DIR` environment variable.

### Custom Fonts

Extract glyphs from any installed font using character input or codepoint:

```bash
./build/bin/glyphsvg --font=Helvetica "Hello" 100 --output=./output/
./build/bin/glyphsvg --font=Helvetica U+0041 100 --output=A.svg
```

Arguments: `--font=<name> <characters|codepoint> <size>`

Codepoint format: `U+XXXX` or `0xXXXX`

### Output

- `--output=<path>` specifies output file or directory (a trailing `/` or an
  existing directory is treated as a directory)
- If output is a directory, each glyph is saved as a separate file. The name is
  the symbol name in material mode (e.g. `home.svg`), or the codepoint otherwise
  (e.g. `U+0041.svg`)
- With no `--output`, a single glyph is written to stdout
- When extracting multiple characters, you must specify an output directory
