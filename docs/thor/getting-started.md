# Getting started

Installing the APK, the first launch, and the controls.

## What you need

* An arm64 Android device running Android 10 or newer. The build is made for the AYN Thor
  (two screens); single-screen devices work too.
* Your own dump of **Pokémon Platinum (USA)**, the `.nds` file (game code CPUE). The APK
  contains no game data, and this project doesn't provide or link to ROMs.

## Getting the APK

Download `pokeplatinum-thor.apk` (or the numbered APK) from the newest release on the
[Releases page](https://github.com/SilasMayes/pokeplatinum-thor/releases) and open it on the
device. Android asks once to allow installs from your browser or file manager.

* **Numbered releases** are the builds that have been played on the Thor.
* **thor-android-latest** is rebuilt by GitHub Actions on every push to `platinum-thor`.
  It has the newest changes and hasn't been tested on a device.

New builds install over old ones and keep your saves. **Don't uninstall the app unless
you've copied your saves out first** (*Settings > Saves > Export*, or turn on save sync):
uninstalling deletes its files folder.

> **Signing key: only install APKs from this repository's Releases page.** Every build is
> signed with the same key so that it installs over the last one, and that key
> (`android/app/stable.keystore`, password `android`) is part of the source code. That
> means anyone can build an APK that Android will accept as an update to this app, with
> access to its saves. An APK from somewhere else may not be what it says it is.

## First launch

The APK contains no game data. On first launch the game asks for your own dump of
Pokemon Platinum (USA). Pick the `.nds` file in the file picker; it is copied in,
extracted, and deleted again.

All files live in `Android/data/org.pokeplatinum.port/files/`: the extracted game
files, the saves, `sim_config.ini` (libntr settings) and `sim_config_prj.ini` (game
settings).

## Controls

The built-in controller works out of the box (A, B, X, Y, L, R, Start, Select, D-pad).
Remap under *Settings > Controls*.

* **Back button / back gesture** or **R3**: open (and close) the settings.
* **L3**: the Gear on the whole lower panel (or back).
* **R2**: fast-forward (while held, by default).
* **L + R + Start + Select**: soft reset, as on the DS (or *Settings > Games > Reset to
  the title screen*, tap twice). Nothing is saved: the app restarts into the same game and
  goes straight to the title screen, so you're back at your last save (resetting for a
  nature, a shiny...). Autosave never runs in the first minutes after loading a save, but
  *Save when leaving the app* would save if you switch apps in between.
