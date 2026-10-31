#!/bin/bash
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILDDIR=build_linux/pokeplatinum

cd $BUILDDIR
mkdir release_linux
cp firmware.bin release_linux/firmware.bin
cp header.bin release_linux/header.bin
cp main release_linux/main
cp pokeplatinum.AppImage release_linux/pokeplatinum.AppImage
"$ROOT/tools/scripts/copy_licenses.sh" release_linux/licenses
