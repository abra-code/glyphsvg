#!/bin/bash
set -euo pipefail

# Downloads Google Material Symbols variable fonts and their name->codepoint
# maps into this directory. These files are NOT committed to git (see
# ../.gitignore), so run this script once before using glyphsvg's material mode.
#
# Source: https://github.com/google/material-design-icons/tree/master/variablefont

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BASE_URL="https://raw.githubusercontent.com/google/material-design-icons/master/variablefont"
AXES="FILL,GRAD,opsz,wght"

STYLES=(Outlined Rounded Sharp)

# URL-encode the bracketed axis suffix once: [FILL,GRAD,opsz,wght]
ENCODED_AXES="%5B${AXES//,/%2C}%5D"

for style in "${STYLES[@]}"; do
    for ext in codepoints ttf; do
        url="${BASE_URL}/MaterialSymbols${style}${ENCODED_AXES}.${ext}"
        out="${SCRIPT_DIR}/MaterialSymbols${style}.${ext}"
        echo "Downloading MaterialSymbols${style}.${ext} ..."
        curl -fgL --retry 3 -o "$out" "$url"
    done
done

# Per-symbol search metadata (tags / synonyms / categories / popularity). Used by
# the name-mapping pipeline to find candidates by concept, not just by name. The
# response has an XSSI guard ")]}'" prefix which we strip to leave valid JSON.
echo "Downloading material_symbols_metadata.json ..."
META_URL="https://fonts.google.com/metadata/icons?key=material_symbols&incomplete=true"
curl -fgL --retry 3 -o "${SCRIPT_DIR}/material_symbols_metadata.raw" "$META_URL"
python3 -c "import sys; d=open('${SCRIPT_DIR}/material_symbols_metadata.raw').read(); open('${SCRIPT_DIR}/material_symbols_metadata.json','w').write(d[d.find('{'):])"
rm -f "${SCRIPT_DIR}/material_symbols_metadata.raw"

echo ""
echo "Downloaded into ${SCRIPT_DIR}:"
for style in "${STYLES[@]}"; do
    for ext in codepoints ttf; do
        f="${SCRIPT_DIR}/MaterialSymbols${style}.${ext}"
        printf "  %-40s %s\n" "$(basename "$f")" "$(du -h "$f" | cut -f1)"
    done
done
