#!/usr/bin/env bash
# Builds the Android APK (arm64-v8a).
#
# Requirements: Android NDK r26+ (ANDROID_NDK_HOME), Android SDK (ANDROID_HOME) and
# Gradle 8.9+ on PATH, plus the usual PC-port host tools (python3, gcc, ninja, nasm,
# flex, bison, cmake, libpng). Run `make meson` once first.
#
# The APK contains code only. Game files are extracted from the player's own ROM on
# first launch, exactly like the PC releases.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD=${BUILD_ANDROID:-$ROOT/build_android}
API=29
ABI=arm64-v8a
SDL_VERSION=${SDL_VERSION:-2.30.11}
ENET_VERSION=${ENET_VERSION:-v1.3.18}
NDK=${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to your Android NDK}
TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64
DEPS=$BUILD/deps
PREFIX=$DEPS/prefix
MESON="python3 $ROOT/subprojects/meson-1.12.0/meson.py"

mkdir -p "$DEPS" "$PREFIX/lib" "$PREFIX/include"

cmake_android() {
    cmake -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=$ABI \
        -DANDROID_PLATFORM=android-$API \
        -DCMAKE_BUILD_TYPE=Release \
        "$@"
}

# 1. SDL2 (shared library + matching Java sources)
if [ ! -f "$PREFIX/lib/libSDL2.so" ]; then
    rm -rf "$DEPS/SDL"
    git clone --depth 1 -b "release-$SDL_VERSION" https://github.com/libsdl-org/SDL "$DEPS/SDL"
    cmake_android -S "$DEPS/SDL" -B "$DEPS/SDL/build" \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
        -DCMAKE_INSTALL_PREFIX="$PREFIX"
    cmake --build "$DEPS/SDL/build"
    cmake --install "$DEPS/SDL/build"
fi

# 2. enet (static, used by libntr's local multiplayer)
if [ ! -f "$PREFIX/lib/libenet.a" ]; then
    rm -rf "$DEPS/enet"
    git clone --depth 1 -b "$ENET_VERSION" https://github.com/lsalzman/enet "$DEPS/enet"
    cmake_android -S "$DEPS/enet" -B "$DEPS/enet/build" -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    cmake --build "$DEPS/enet/build"
    cp "$DEPS/enet/build/libenet.a" "$PREFIX/lib/"
    cp -r "$DEPS/enet/include/enet" "$PREFIX/include/"
fi

# 3. Meson cross file for the NDK
cat > "$BUILD/android.ini" <<INI
[constants]
toolchain = '$TOOLCHAIN/bin'
prefix = '$PREFIX'

[binaries]
c = toolchain / 'aarch64-linux-android$API-clang'
cpp = toolchain / 'aarch64-linux-android$API-clang++'
ar = toolchain / 'llvm-ar'
strip = toolchain / 'llvm-strip'

[built-in options]
c_args = ['-I' + prefix / 'include']
cpp_args = ['-I' + prefix / 'include']
c_link_args = ['-L' + prefix / 'lib']
cpp_link_args = ['-L' + prefix / 'lib']

[host_machine]
# Really android/aarch64; the NDK compiler targets that no matter what meson is told.
# - system: meson forces -fPIE on every target for 'android', which breaks linking the
#   static libraries into libmain.so. Everything is built with -fPIC instead.
# - cpu: meson refuses NASM for non-x86 hosts, and the project declares NASM (only DS
#   builds use it). Same workaround as the NX target.
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
INI

# 4. libmain.so
if [ ! -f "$BUILD/meson/build.ninja" ]; then
    $MESON setup -Dbuild_target=android \
        --native-file="$ROOT/meson/native.ini" \
        --cross-file="$BUILD/android.ini" \
        -- "$BUILD/meson"
fi
$MESON compile -C "$BUILD/meson" main

# 5. Assemble the Gradle project
APP=$ROOT/android/app/src/main
JNI=$APP/jniLibs/$ABI
mkdir -p "$JNI" "$APP/assets" "$APP/java/org/libsdl"
"$TOOLCHAIN/bin/llvm-strip" --strip-unneeded -o "$JNI/libmain.so" "$BUILD/meson/main.so"
cp "$PREFIX/lib/libSDL2.so" "$JNI/"
rm -rf "$APP/java/org/libsdl/app"
cp -r "$DEPS/SDL/android-project/app/src/main/java/org/libsdl/app" "$APP/java/org/libsdl/"
# Wi-Fi settings placeholder written by the port itself (not DS firmware)
cp "$ROOT/res/pc_port/dwc_nv.bin" "$APP/assets/firmware.bin"

# 6. APK
(cd "$ROOT/android" && gradle --no-daemon assembleRelease)
echo "APK: $ROOT/android/app/build/outputs/apk/release/app-release.apk"
