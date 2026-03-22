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

### Custom Fonts

Extract glyphs from any installed font using character input or codepoint:

```bash
./build/bin/glyphsvg --font=Helvetica "Hello" 100 --output=./output/
./build/bin/glyphsvg --font=Helvetica U+0041 100 --output=A.svg
```

Arguments: `--font=<name> <characters|codepoint> <size>`

Codepoint format: `U+XXXX` or `0xXXXX`

### Output

- `--output=<path>` specifies output file or directory
- If output is a directory, each glyph is saved as a separate file named by codepoint (e.g., `U+0041.svg`)
- When extracting multiple characters, you must specify an output directory
