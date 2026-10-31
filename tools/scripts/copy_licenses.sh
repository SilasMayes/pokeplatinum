#!/bin/bash
# Copies the license notices every release must carry (see THIRD_PARTY_LICENSES.md) into $1.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEST=${1:?usage: copy_licenses.sh <dest dir>}
rm -rf "$DEST"
mkdir -p "$DEST"
cp "$ROOT/LICENSE" "$DEST/pokeplatinum-thor.txt"
cp "$ROOT/THIRD_PARTY_LICENSES.md" "$DEST/"
cp "$ROOT"/licenses/*.txt "$DEST/"
cp "$ROOT/res/pc_port/fonts/AtkinsonHyperlegible-OFL.txt" "$DEST/"
