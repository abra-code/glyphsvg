# Google Material Symbols Data

glyphsvg's material mode extracts SVGs from Google's Material Symbols variable
fonts, mapping symbol names to glyphs via Google's published codepoint files.

Source: https://github.com/google/material-design-icons/tree/master/variablefont

## Download

The fonts and codepoint maps are **not committed to git**. Run the download
script once before using material mode:

```bash
./material/download.sh
```

This populates this directory with, for each style (Outlined, Rounded, Sharp):

- `MaterialSymbols<Style>.codepoints` — plain-text `name codepoint` map
- `MaterialSymbols<Style>.ttf` — the variable font used to render glyphs

It also fetches, once (style-independent):

- `material_symbols_metadata.json` — Google's per-symbol search metadata
  (`tags`, `categories`, `popularity`, `codepoint`) for all ~6,090 symbols, from
  `https://fonts.google.com/metadata/icons`. Used by the SF -> Material name-mapping
  pipeline (`../mapping/`) to find candidates by concept/tag, not just by name.

## File Formats

### `MaterialSymbols<Style>.codepoints`
Plain text, one entry per line: `icon_name hex_codepoint` (space-separated),
e.g. `home e88a`. Codepoints are hexadecimal (Private Use Area). All three
styles share the same names and codepoints; the style only selects the glyph
design (font file).

### `MaterialSymbols<Style>.ttf`
Material Symbols variable font with axes `FILL`, `GRAD`, `opsz`, `wght`.

### `material_symbols_metadata.json`
JSON `{"icons":[{"name","tags":[...],"categories":[...],"popularity","codepoint"}, ...]}`.
The endpoint guards its response with an XSSI `)]}'` prefix; `download.sh` strips
it. Not every listed icon exists in the variable font, so intersect names with
the `.codepoints` set before rendering.
