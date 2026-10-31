# Building the APK

For players: you don't need this. Download the APK from the
[Releases page](https://github.com/SilasMayes/pokeplatinum-thor/releases) instead (see
[Getting started](getting-started.md)).

## On your machine

You need the Android NDK (r26 or newer; CI uses r27), the Android SDK (platform 35),
JDK 17, Gradle 8.9+, and the PC port's host tools: python3, gcc, ninja, nasm, flex,
bison, cmake, libpng and `arm-none-eabi-gcc` (for the asset scripts).

```sh
make meson
ANDROID_NDK_HOME=/path/to/ndk ./tools/android/build_apk.sh
# -> android/app/build/outputs/apk/release/app-release.apk
```

The script builds SDL2 and ENet for Android, compiles the game as `libmain.so` with
meson, and packs the APK with Gradle. `VERSION_CODE` and `VERSION_NAME` in the
environment set the APK's version (CI uses the run number, so newer builds always
install over older ones).

The APK is signed with `android/app/stable.keystore` (see the warning in
[Getting started](getting-started.md#getting-the-apk)). Builds you make yourself install
over the released app; to keep your test builds separate, change `applicationId` in
`android/app/build.gradle`.

## In CI

`.github/workflows/build_android.yml` builds the APK on every push to `platinum-thor` and
on pull requests (the APK is attached to the run as `pokeplatinum-android`). Pushes to
`platinum-thor` also replace the `thor-android-latest` pre-release.

## Logs

stdout and stderr go to logcat with the tag `pokeplatinum`; the second-screen code logs
as `libntr-display`:

```sh
adb logcat -s pokeplatinum libntr-display
```

## Technical notes

* Default 3D resolution is 5x; change it under *Settings > Display > 3D resolution*.
* *Settings > Advanced* shows where each frame's time goes: frames shown and DS frames
  run per second, CPU time per part (game, 3D draw calls, 2D layers, layer uploads, the
  second screen and the Gear, menus, frame-limiter wait, buffer swap), GPU time per part
  (from timer queries) and the cores the game runs on. *Frame timings on screen* puts
  the same over the game.
* Speed on Android: the game thread runs on the fastest cores and tells Android each
  frame's time budget (performance hints), the 2D layer textures are a ring per screen so
  an upload never waits for the GPU, and unchanged layers aren't uploaded.
* The Android build is compiled with optimization (`-O2 -fno-strict-aliasing -fwrapv`);
  the PC builds are not.
* Double battles: picking a target overran a 4-byte stack array on 64-bit (the DS code
  counted a pointer's size); fixed in `battle_subscreen.c`.
* Debug GUI *Gen Pokemon* can also start a wild battle against the chosen species, or a
  battle against a trainer ID (optionally as a double battle), for testing.
