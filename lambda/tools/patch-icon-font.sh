#!/usr/bin/env bash
# Patch vertical metrics on Material Symbols Rounded (upstream issue google/material-design-icons#1500).
# Requires `ttx` from fonttools (e.g. brew install fonttools).
#
# Usage:
#   ./lambda/tools/patch-icon-font.sh path/to/MaterialSymbolsRounded[FILL,...].ttf [output.ttf]
#
# Default output: MaterialSymbolsRounded-patched.ttf next to the input file.

set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <upstream.ttf> [output.ttf]" >&2
  exit 1
fi

INPUT=$1
OUT=${2:-$(dirname "$INPUT")/MaterialSymbolsRounded-patched.ttf}
TMP=$(mktemp)
trap 'rm -f "$TMP"' EXIT

ttx -t hhea -t OS/2 -o "$TMP" "$INPUT"

sed -n '1,99999p' "$TMP" | \
  sed 's/<ascent value="1056"\/>/<ascent value="960"\/>/' | \
  sed 's/<descent value="-96"\/>/<descent value="0"\/>/' | \
  sed 's/<sTypoAscender value="1056"\/>/<sTypoAscender value="960"\/>/' | \
  sed 's/<sTypoDescender value="-96"\/>/<sTypoDescender value="0"\/>/' | \
  sed 's/<usWinAscent value="1062"\/>/<usWinAscent value="960"\/>/' | \
  sed 's/<usWinDescent value="91"\/>/<usWinDescent value="0"\/>/' > "${TMP}.edited"

mv "${TMP}.edited" "$TMP"

ttx -o "$OUT" -m "$INPUT" "$TMP"
echo "Wrote $OUT"
