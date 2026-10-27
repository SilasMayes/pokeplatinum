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
* **Save editor**: see below.
* **Gameplay**: instant text and HP/EXP bars, 60 FPS options, drawing every DS frame.
* **Display**: 3D resolution, second display, layout, swap screens, vsync.
* **Sound**, **Controls** (fast-forward speed and button; remap buttons: tap a control,
  press the button).
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

* **Battle** (only during battles, opens by itself and takes the whole lower panel):
  the battle menu itself, so there is no switching back and forth. Fight, Bag, Pokemon
  and Run (Ball / Bait / Mud in the Safari Zone), your moves with their PP and how well
  each hits the opponents (`x2 Super effective`, `L x1/2 R x2` in double battles), the
  targets in double battles, and the yes/no questions (forget a move, switch Pokemon,
  ...). The D-pad cursor shows on the Gear too. The DS screen comes back by itself for
  the bag and the party, and goes away again after. Above the menu: the opponents'
  types, HP and status, and your Pokemon; further down: stat changes, abilities and
  items. Turn this off with *Settings > Gear > Battle menu on the Gear* (the Gear then
  shows the information only, beside the DS screen).
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
bring the DS touch screen back. Turn the Gear off
(or change what it reveals) in *Settings > Gear*. On desktop builds it appears in the
"Bottom Screen" window (F1 toggles full screen).

## Save editor

*Settings > Save editor* changes the game you are playing, in memory: save in the game
to keep the changes (or reset without saving to undo them). It works while you are
walking around (not in a battle or one of the game's menus).

* **Party** and **PC boxes**: level, nature, shiny, gender, ability, held item,
  friendship, IVs (or all 31), EVs (510 in all), moves (the ones it can learn by level
  and TM/HM, or any move), heal. Nature, shininess, gender and ability come from the
  Pokemon's personality value, so changing one picks a new value that keeps the others.
* **Give a Pokemon**: any species (by Pokedex number or by name) at any level, with a
  nature, shiny or not, perfect IVs, a held item; to the party or the PC. It is caught
  in a Poke Ball with you as its trainer and counts as caught in the Pokedex.
* **Bag**: every pocket, how many of each item (0 takes it out), add any item.
* **Trainer**: money, Battle Points, badges, play time, the National Pokedex.
* **Legendary events**: the Mystery Gift events Nintendo gave out. *Start* gives the key
  item as the Poke Mart delivery man would and turns the event on: Darkrai (Member Card:
  sleep at the Harbor Inn in Canalave City), Shaymin (Oak's Letter: the tablet on Route
  224), Arceus (Azure Flute: the top of Spear Pillar) and Rotom's forms (Secret Key:
  the locked room of the Team Galactic building in Eterna City). Each shows what the
  game still needs first (the Hall of Fame and the National Pokedex for the first
  three). The game checks when you arrive at the place, so leave and come back if you
  are already there.

Numbers open a keypad when tapped (the controller works too: A on the number, D-pad
and A on the keys, B to cancel), and long lists go a letter at a time.

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

## Speed

* **Instant text** and **instant HP/EXP bars** (*Settings > Gameplay*, both on by
  default): what Renegade Platinum's SpeedUp patch does, for any game. Messages appear
  at once up to where they wait for a button; battle bars jump to their new value.
* **Fast-forward**: hold **R2** (or tap **>>** in the Gear's header to switch it on and
  off). *Settings > Controls* sets the speed (2x to 8x, or Max: as fast as the device
  goes; 4x by default), whether the button works while held or switches it on and off,
  and which button it is. `>> 4x` shows in the top-right corner while it's on. Music
  keeps its normal tempo.
* Battles, menus and other scenes that run at 30 frames a second only draw the frames
  they show (half the drawing). *Settings > Gameplay > Draw every DS frame* draws the
  in-between ones too, should anything flicker.

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

## Notes

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
* stdout/stderr go to logcat with the tag `pokeplatinum`; the second-screen code logs as
  `libntr-display`.
