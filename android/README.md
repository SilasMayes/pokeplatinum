# Android build

The Gradle project for the Android/AYN Thor APK. `tools/android/build_apk.sh` builds the
native code with meson and fills in `app/src/main/jniLibs`, `assets` and the SDL Java
sources before running Gradle.

The manual (installing, settings, the Gear, every feature) is in
[docs/thor/](../docs/thor/README.md); building is in
[docs/thor/building.md](../docs/thor/building.md).
