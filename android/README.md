# Android build (AYN Thor and other Android devices)

An arm64 Android build of the PC port. It runs natively (no emulator) through libntr's
OpenGL ES 3.2 renderer, and on dual-screen devices such as the AYN Thor it puts the DS
bottom screen on the lower panel.

## Getting the APK

GitHub Actions builds it on every push to `thor-android`
(`.github/workflows/build_android.yml`). Download the `pokeplatinum-android` artifact
from the run and install `app-release.apk`.

Every build is signed with the same key (`android/app/stable.keystore`), so new builds
install over old ones and keep your saves. Do not uninstall the app unless you have
copied your saves out first: uninstalling deletes its files folder.

To build locally you need the Android NDK (r26+), the SDK and Gradle 8.9+:

```sh
make meson
ANDROID_NDK_HOME=/path/to/ndk ./tools/android/build_apk.sh
```

## First launch

The APK contains no game data. On first launch the game asks for your own dump of
Pokemon Platinum (USA). Pick the `.nds` file in the file picker; it is copied in,
extracted, and deleted again.

All files live in `Android/data/org.pokeplatinum.port/files/`: the extracted game
files, `save.bin`, `sim_config.ini` (libntr settings) and `sim_config_prj.ini` (game
settings).

## Screens

* **Dual-screen devices (AYN Thor):** top screen on the main panel, bottom screen on
  the lower panel. Touch the lower panel to use the touch screen. Touching it does not
  steal controller focus from the game.
* **Single screen:** the normal libntr layouts (vertical, side by side, large screen).
  Touch the bottom screen area directly.

Turn the second display off with *Config -> Bottom screen on second display* in the
settings GUI. *Swap Screens* swaps which screen goes on which panel.

## Controls

The built-in controller works out of the box (A, B, X, Y, L, R, Start, Select, D-pad).
Remap under *Input* in the settings GUI.

* **Back button / back gesture** or **R3**: open the settings GUI.

## Notes

* Default 3D resolution is 5x; change it under *Config -> Internal Resolution*.
* stdout/stderr go to logcat with the tag `pokeplatinum`; the second-screen code logs as
  `libntr-display`.
