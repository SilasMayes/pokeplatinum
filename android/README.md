# Android build (AYN Thor and other Android devices)

An arm64 Android build of the PC port. It runs natively (no emulator) through libntr's
OpenGL ES 3.2 renderer, and on dual-screen devices such as the AYN Thor it puts the DS
bottom screen on the lower panel.

## Getting the APK

GitHub Actions builds it on every push to `thor-android`
(`.github/workflows/build_android.yml`) and publishes it as the **thor-android-latest**
pre-release (`pokeplatinum-thor.apk`), so it can be downloaded straight from the
device's browser. The `pokeplatinum-android` artifact on the run has the same APK.

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
files, the saves, `sim_config.ini` (libntr settings) and `sim_config_prj.ini` (game
settings).

## Saves

* **Save slots:** four independent games. On the main menu, **L/R** switch slots (the
  slot shows next to CONTINUE / NEW GAME). An empty slot offers NEW GAME without
  touching the others. Slot 1 is `save.bin`, slots 2-4 are `save_slot2.bin` and so on;
  each is a raw 512 KB DS save, the same format as a melonDS/DeSmuME `.sav`.
* **Import / export:** *Application -> Saves* in the settings GUI. Import takes a
  `.sav` (or DeSmuME `.dsv`) from anywhere through the file picker; the slot's previous
  save is kept as `.bak`. Export writes a 512 KB `.sav` an emulator can load.
* **Autosave:** the game's own save, run automatically every few minutes (default 5)
  and when the app is closed or sent to the background, but only when the start
  menu's SAVE would be allowed, the player is standing still on the overworld with
  nothing else going on, and the game has been saved normally at least once (it never
  overwrites a different game). A small "Autosaved" notice appears in the corner.
  Settings are in the same Saves window.

## Screens

* **Dual-screen devices (AYN Thor):** top screen on the main panel, bottom screen on
  the lower panel. Touch the lower panel to use the touch screen. Touching it does not
  steal controller focus from the game.
* **Single screen:** the normal libntr layouts (vertical, side by side, large screen).
  Touch the bottom screen area directly.

Turn the second display off with *Config -> Bottom screen on second display* in the
settings GUI. *Swap Screens* swaps which screen goes on which panel.

## Gear (lower-panel companion)

The Thor's lower panel is taller than a DS screen, so the DS touch screen sits at its
top and the **Gear** fills the space below it, in the spirit of HeartGold's
Pokegear and Kanto Gear:

* **Battle** (only during battles, opens by itself): the opponents' types, HP and
  status, and every move of the Pokemon you are commanding with how well it hits each
  opponent (`x2 Super effective`, `L x1/2 R x2` in double battles). Further down: stat
  changes, your Pokemon's ability and item.
* **Party**: HP, status and level at a glance; tap a Pokemon for its moves, stats,
  ability, nature and item.
* **Area**: the wild Pokemon of the route you are on for the current time of day
  (walking, surfing, each rod, Poke Radar and swarms) with their rates and levels, and
  which ones you have caught.
* **Trainer**: money, badges, Pokedex counts, play time and the save slot.
* **Settings**: how much the Gear reveals. *Vanilla* shows only what the game shows,
  *Guide* (default) adds the encounter guide and type matchups but hides species you
  have never seen, *Everything* also shows hidden values (IVs/EVs, the opponents'
  abilities, items, moves and whole team).

Tap **Full** (or press **L3**) to give the Gear the whole lower panel and **Split** to
bring the DS touch screen back; battles bring it back by themselves. Turn the Gear off
with *Config -> Gear companion on the lower screen*. On desktop builds it appears in
the "Bottom Screen" window (F1 toggles full screen).

## 60 FPS

The DS game runs its logic at 30 frames a second. With *60 FPS* on (the default), the
logic runs at 60 while you are walking around the overworld, which makes walking,
biking and scrolling smooth; the speed fixes keep walking, running, biking, turning,
ledge jumps, sprite animations, water/flower animations and map props at their real
speed. Battles, menus, text, events and everything else switch back to the original
30 so they run at their normal speed. Music always keeps its tempo.

*60 FPS in battles and menus too* (settings GUI, *Application -> Config*, or
`60FpsEverywhere=true`) runs the whole game at 60, which makes everything off the
overworld twice as fast. Known leftovers at 60 on the overworld: rain/snow particles and
some small field effects move faster than on the DS, and wandering NPCs pause for
shorter.

## Controls

The built-in controller works out of the box (A, B, X, Y, L, R, Start, Select, D-pad).
Remap under *Input* in the settings GUI.

* **Back button / back gesture** or **R3**: open the settings GUI.

## Notes

* Default 3D resolution is 5x; change it under *Config -> Internal Resolution*.
* The libntr window shows where each frame's time goes (game logic, 3D draw calls,
  compositing, frame-limiter wait, buffer swap).
* The Android build is compiled with optimization (`-O2 -fno-strict-aliasing -fwrapv`);
  the PC builds are not.
* Double battles: picking a target overran a 4-byte stack array on 64-bit (the DS code
  counted a pointer's size); fixed in `battle_subscreen.c`.
* Debug GUI *Gen Pokemon* can also start a wild battle against the chosen species, or a
  battle against a trainer ID (optionally as a double battle), for testing.
* stdout/stderr go to logcat with the tag `pokeplatinum`; the second-screen code logs as
  `libntr-display`.
