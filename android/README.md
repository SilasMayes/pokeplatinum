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

## Settings

The **back button / back gesture** or **R3** opens the settings on the top screen (the
game waits meanwhile). Everything is big enough to tap, scrolls with a finger, and works
with the controller too: D-pad to move, A to choose, B to go back, L/R for the previous /
next page.

* **Games**: reset to the title screen, switch games, add one (Renegade Platinum),
  choose at launch.
* **Saves**: save slots, import / export, autosave, save now.
* **Gameplay**: 60 FPS options.
* **Display**: 3D resolution, second display, layout, swap screens, vsync.
* **Sound**, **Controls** (remap buttons: tap a control, press the button).
* **Gear**, **Cheats**.
* **Advanced**: frame timings, the game's assertion checks, and *Developer windows* (the
  original floating debug windows: libntr, multiplayer, Gen Pokemon, Gen Items, Jump to
  Map...).

## More than one game (Renegade Platinum)

The app can hold several games side by side, each with its own extracted ROM and its
own saves (the settings are shared). Switch between them in *Settings > Games* (tap a
game twice); the app saves first when the game allows it and restarts into the other
game. When more than one game is installed, a picker also comes up when the app starts
(turn it off there or in *Settings > Games*).

**Adding Renegade Platinum** (Drayano's hack, including the Classic version): unzip the
download, put your Pokemon Platinum (USA) ROM in the same folder (or choose it
afterwards), then *Settings > Games > Add a game > Choose a folder* and pick the folder.
The app tries every patch in it on the ROM and on each other (xdelta's own checksums say
whether a patch was made for what it is applied to, `sim_vcdiff.c`) and shows what it
found:

* the patch that goes on the ROM: `RenegadePlatinum4997.xdelta`, or
  `RenegadePlatinum3541.xdelta` for early ROMs (the one that doesn't match is listed
  under *Not used*);
* the extras that go on top of it, as switches: `ClassicVersion`, `SpeedUpPatch`;
* extras that change the same thing as a choice: *ShinyRate: None / 1 4096 / 1 8192*.

*Patch and add* builds the game and extracts it as a new game (named "Renegade Platinum
Classic", "Renegade Platinum"...). Extras that change bytes in place are combined from
their changes, so any of them can go together. Folders with several ROMs, a ROM of another
game, archives that still need unzipping and unsupported patch formats (IPS/BPS/UPS) are
handled and explained on the same screen. On Android the folder's patches and Platinum ROMs
are copied in first and deleted again afterwards.

*Choose the files one by one* is still there: the ROM, then each patch in order. An
already patched `.nds` can be added that way (or from a folder) without patches.

The hack's new data files (Pokemon, trainers, encounters, text, scripts...) load like
the original's. The few things it changes inside the game's code are read from the
patched ROM at startup (`src/port/rom_patches.c`): map headers, shop stock, Game Corner
prizes, TM moves, icon palettes, shiny odds (1 in 512, or what a ShinyRate patch sets),
every Pokemon obeying, the Fairy type and its type chart, the Fairy move colours and
Pokedex icon, and the Pokemon in Professor Rowan's introduction. The SpeedUp patch's
code changes (instant text and HP bars) are not carried over.

## Saves

* **Save slots:** four independent games. On the main menu, **L/R** switch slots (the
  slot shows next to CONTINUE / NEW GAME). An empty slot offers NEW GAME without
  touching the others. Slot 1 is `save.bin`, slots 2-4 are `save_slot2.bin` and so on;
  each is a raw 512 KB DS save, the same format as a melonDS/DeSmuME `.sav`.
* **Import / export:** *Settings > Saves*. Import takes a
  `.sav` (or DeSmuME `.dsv`) from anywhere through the file picker; the slot's previous
  save is kept as `.bak`. Export writes a 512 KB `.sav` an emulator can load.
* **Autosave:** the game's own save, run automatically every few minutes (default 5)
  and when the app is closed or sent to the background, but only when the start
  menu's SAVE would be allowed, the player is standing still on the overworld with
  nothing else going on, and the game has been saved normally at least once (it never
  overwrites a different game). A small "Autosaved" notice appears in the corner.
  Its settings and *Save now* are on the same page.

## Screens

* **Dual-screen devices (AYN Thor):** top screen on the main panel, bottom screen on
  the lower panel. Touch the lower panel to use the touch screen. Touching it does not
  steal controller focus from the game.
* **Single screen:** the normal libntr layouts (vertical, side by side, large screen).
  Touch the bottom screen area directly.

Turn the second display off with *Settings > Display > Bottom screen on the second
display*. *Swap screens* swaps which screen goes on which panel.

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
(or change what it reveals) in *Settings > Gear*. On desktop builds it appears in the
"Bottom Screen" window (F1 toggles full screen).

## 60 FPS

The DS game runs its logic at 30 frames a second. With *60 FPS* on (the default), the
logic runs at 60 while you are walking around the overworld, which makes walking,
biking and scrolling smooth; the speed fixes keep walking, running, biking, turning,
ledge jumps, sprite animations, water/flower animations and map props at their real
speed. Battles, menus, text, events and everything else switch back to the original
30 so they run at their normal speed. Music always keeps its tempo.

*60 FPS in battles and menus too* (*Settings > Gameplay*, or `60FpsEverywhere=true`)
runs the whole game at 60, which makes everything off the
overworld twice as fast. Known leftovers at 60 on the overworld: rain/snow particles and
some small field effects move faster than on the DS, and wandering NPCs pause for
shorter.

## Controls

The built-in controller works out of the box (A, B, X, Y, L, R, Start, Select, D-pad).
Remap under *Settings > Controls*.

* **Back button / back gesture** or **R3**: open (and close) the settings.
* **L3**: the Gear on the whole lower panel (or back).
* **L + R + Start + Select**: soft reset, as on the DS (or *Settings > Games > Reset to
  the title screen*, tap twice). Nothing is saved: the app restarts into the same game and
  goes straight to the title screen, so you're back at your last save (resetting for a
  nature, a shiny...). Autosave never runs in the first minutes after loading a save, but
  *Save when leaving the app* would save if you switch apps in between.

## Notes

* Default 3D resolution is 5x; change it under *Settings > Display > 3D resolution*.
* *Settings > Advanced* shows where each frame's time goes (game logic, 3D draw calls,
  compositing, frame-limiter wait, buffer swap).
* The Android build is compiled with optimization (`-O2 -fno-strict-aliasing -fwrapv`);
  the PC builds are not.
* Double battles: picking a target overran a 4-byte stack array on 64-bit (the DS code
  counted a pointer's size); fixed in `battle_subscreen.c`.
* Debug GUI *Gen Pokemon* can also start a wild battle against the chosen species, or a
  battle against a trainer ID (optionally as a double battle), for testing.
* stdout/stderr go to logcat with the tag `pokeplatinum`; the second-screen code logs as
  `libntr-display`.
