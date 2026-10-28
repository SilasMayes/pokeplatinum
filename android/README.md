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
* **Gameplay**: instant text and HP/EXP bars, field moves without HMs, *Use another
  Repel?* (on by default: when a Repel wears off and the bag has another, the game asks,
  as in Black and White; Yes uses the same kind as last time, or the weakest), *Exp. Share
  for the whole party* (off by default: as in X and Y, the Pokemon that battled each get
  all the EXP and the rest of the party half), the hard level cap, the game clock (hours
  and days ahead of the device's), the Nuzlocke helper, 60 FPS options, drawing every DS
  frame.
* **Display**: 3D sharpness and edge smoothing, second display, layout, swap screens,
  widescreen (experimental), texture packs, vsync.
* **GBA slot**: a Ruby, Sapphire, Emerald, FireRed or LeafGreen save in the DS's GBA
  slot (see below).
* **Sound**, **Controls** (fast-forward speed and button; remap buttons: tap a control,
  press the button).
* **Gear**, **Cheats** (see below).
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

**Widescreen (experimental, off by default):** *Settings > Display > Widescreen* fills
the Thor's 16:9 top panel instead of a 4:3 box, on the overworld and in battles, with
the bottom screen on the second display (not swapped):

* **Overworld**: the field camera sees more of the map on the left and right,
  unstretched. Text boxes, menus and the location banner stay 4:3 in the middle. The
  next map blocks load in one frame, so running or cycling sideways doesn't show
  unloaded ground at the edges. Rain, snow, sand, fog and dark caves stay 4:3: their
  weather and darkness are 2D overlays that would only cover the middle. The Poke Radar
  keeps its patches while they are anywhere on the wider screen.
* **Battles**: the backdrop goes on across the whole screen, and the Pokemon, trainers
  and move effects keep their places and shapes (with more room around them when they
  slide in). The text box, HP boxes and the bases stay in the middle.

Menus and other screens stay 4:3.

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

  **Damage and turn order.** Each move also shows the damage it does, as a share of
  the target's HP (`x2 52-63%`), with a **KO** tag when it knocks the target out
  whatever the roll and **KO?** when it might. The numbers come from the game's own
  damage formula, so they count stat changes, abilities (Technician, Thick Fat,
  Levitate, Volt Absorb, ...), held items (Choice Band, type-boosting items and plates, Life Orb,
  Expert Belt, resist berries, ...), weather, screens, burns, STAB and the type chart.
  Only the random roll is left (the range) and critical hits are not counted.
  Multi-hit moves show the whole move (or one hit with `x2-5`), moves with a set amount
  (Seismic Toss, Super Fang, ...) show that, and moves whose power the battle only
  knows when they're used (Magnitude, Present, Fling, Counter, ...) show the matchup
  only. A line above the moves says who moves first between same-priority moves
  (speed with its stat stage, items, abilities, paralysis, Tailwind and Trick Room,
  Lagging Tail and Stall; not Quick Claw), and moves with priority show it (`+1`).
  With *Everything* revealed, the lists further down give exact damage in HP and the
  speeds, and also what the opponent's moves do to your Pokemon.
* **Party**: each Pokemon's icon (hopping like in the party menu, slower when it's
  hurt), HP, status and level at a glance; tap a Pokemon for its portrait (its battle
  sprite, shiny colors included), moves, stats, ability, nature and item.
  Below the party, the **eggs** you carry with about how many steps each still needs
  (Flame Body and Magma Armor counted), and the **Day Care**: its two Pokemon with the
  levels they've gained, how well they get along (as the Day Care Man says it) with the
  chance of an egg every 256 steps and the steps to the next check, whether an egg is
  waiting, what the egg would be (incense babies included), the Everstone's nature, how
  IVs pass on, the Masuda method, and the moves the egg would hatch with (egg moves, TMs
  and level-up moves both parents know, Volt Tackle with a Light Ball), worked out the
  way the game's Day Care does it. With *Everything* revealed, the parents' IVs and
  natures too.
* **Area**: the wild Pokemon of the route you are on for the current time of day
  (walking, surfing, each rod, Poke Radar, swarms and the GBA slot's game) with their
  icons, rates and levels, and which ones you have caught. Tap one to look it up in the
  Pokedex. Species you have never seen show as a
  silhouette while their names stay hidden. On top, the **shiny hunt** counters: Pokemon met (wild
  and one-off battles) since the last shiny with the odds of having seen one by now,
  eggs hatched and soft resets since then, the Poke Radar chain with its shiny rate per
  patch, and the last shiny found. Each species in the list shows how many of it you
  have met since its last shiny (`x154`), and so does the battle tab for the wild
  Pokemon in front of you. The counters live beside the save slot (`save_slotN.hunt`),
  so soft resets don't take them back; *Reset* (tap twice) starts them over.
* **Pokedex** (the switch at the top of Area): look up any Pokemon, A to Z with a
  letter bar, and see its types, base stats, abilities, EV yield, catch rate, gender
  ratio, egg groups, hatch steps, growth rate, how it evolves (and from what), where to
  find it (every map's walking, surfing, rod, Poke Radar, swarm and GBA-slot tables,
  with levels and the rate at each time of day), and its level-up moves, TMs/HMs, egg
  moves and tutor moves. It's all read from the ROM, so Renegade Platinum shows its own
  stats, learnsets, evolutions and encounters. Species you haven't seen stay hidden
  unless the Gear reveals *Everything*.
* **Berries** (the third switch in Area): your berry patches, the soonest ripe first,
  with where they are, their stage, how long until they're ripe (and the game clock's
  time then), about how many berries, whether the soil needs water and the mulch; ripe
  ones show how long the berries keep. Below, the trees around Sinnoh that start with
  berries, until you pick them.
* **Pokétch** (once you have it): the Pokétch itself, bigger, on the whole lower panel.
  Tap its screen and its buttons as usual; the app's name is shown with it.
* **PC**: the boxes as they are, one at a time with each Pokemon's level (shiny ones
  marked), and a list of the species in your PC, A to Z, with how many and in which
  boxes: tap one to see where they are. **Sort** a box, or all of them (filling them in
  order from Box 1), by Pokedex number, name, level or type, eggs last. **Bag**: sort
  every pocket by name or by number (TMs and berries stay by number). The PC tab works
  while you're walking around, like the save editor, since the game's own PC and bag
  use the same data.
* **Trainer**: money, badges, Pokedex counts, play time and the save slot. Below, the
  **level cap** (the level of the next gym leader's strongest Pokemon, the Champion's
  after 8 badges; Pokemon over it show in yellow here and in Party) and the **bosses**:
  the rival, Team Galactic, gym leaders, Elite Four and Champion in story order, with
  the ones before your next badge open. Each shows its team as the battle builds it,
  from the game's own trainer data, so a ROM hack such as Renegade Platinum shows its
  own teams: Pokemon, levels, held items and moves, and with *Everything* revealed
  their abilities, natures and stats. The rival's team follows the starter you chose.
  *Settings > Gameplay > Hard level cap* (off by default) stops battle EXP at the cap.
* **Settings**: how much the Gear reveals. *Vanilla* shows only what the game shows,
  *Guide* (default) adds the encounter guide, type matchups, damage estimates and turn
  order but hides species you have never seen, *Everything* also shows hidden values
  (IVs/EVs, exact damage and speeds, the opponents' abilities, items, moves and whole
  team).

The battle cards, the bosses' teams and the Nuzlocke graveyard show the Pokemon's icons
too. The pictures come from the game's own graphics files, so a ROM hack's sprites show
as they are.

Tap **Full** (or press **L3**) to give the Gear the whole lower panel and **Split** to
bring the DS touch screen back. Turn the Gear off
(or change what it reveals) in *Settings > Gear*. On desktop builds it appears in the
"Bottom Screen" window (F1 toggles full screen).

## GBA slot (Pal Park and dual-slot Pokemon)

On a DS Lite, Platinum looks in the GBA slot. *Settings > GBA slot* puts a game there
without one:

* Choose **Ruby, Sapphire, Emerald, FireRed or LeafGreen**, and **Choose a save**: a
  `.sav` from a GBA emulator (mGBA, VBA, RetroArch, ...; 128 KB). The page shows whose
  save it is, the play time and how many Pokemon are in the party and the PC. Importing
  sets the slot to the save's pair of games (Ruby/Sapphire and FireRed/LeafGreen look
  the same in a save: pick the right one of the two).
* **Dual-slot Pokemon**: with the National Pokedex, some grass gets Pokemon from the
  slot's game (the 4% slots), straight away. The Gear's Area tab lists them under *With
  Emerald in the GBA slot*. This works with no save too.
* **Pal Park**: once you have the National Pokedex the main menu offers *Migrate from
  Emerald* (and so on) to send six Pokemon from the save's PC boxes to Pal Park, as on a
  DS. The GBA game has to be in the same language as this one.
* Platinum works on its own copy of the save: the file you picked never changes.
  Migrating takes the Pokemon out of the copy, as from a cartridge; **Export the save**
  to carry on with it in an emulator.

## Texture packs

*Settings > Display > Texture packs* replaces the game's 3D textures (the overworld,
people, battle Pokemon and effects) with PNGs, the way Dolphin does, at any size: HD
textures, fixes, recolours.

* **Save the game's textures** writes each 3D texture as a PNG the first time it's
  drawn, into `textures/dump/CPUE/` in the app's folder (on Android that's
  `Android/data/org.pokeplatinum.port/files/`, reachable from a computer over USB). The
  name is the texture's size, hashes of its data and of the colours it uses, and its
  format: `tex_64x32_1a2b3c4d_5e6f7a8b_3.png`.
* Edit one (keep its name; any size works) and put it in `textures/load/CPUE/`, in any
  folders, or use **Add a pack** to copy a pack's folder in (its PNGs, with their
  folders). **Look again** picks up files added by hand.
* Menus, text and other 2D graphics aren't textures, so they stay as they are.

## Cheats

*Settings > Cheats*, all off by default:

* **Exploring**: walk through walls, no wild encounters, eggs hatch in a few steps.
* **Battles**: EXP from battles (1x, 1.5x, 2x, 3x or 5x, on top of the game's own
  bonuses, never past level 100), every Poke Ball catches, moves don't use PP (your
  side), always get away from wild Pokemon (even when trapped), run from trainer
  battles.
* **Pokemon**: shiny odds (the game's own 1 in 8192, or 1 in 1024, 256, 64, or every
  one) for wild Pokemon, legendaries included, and hatched eggs. A Pokemon made shiny
  keeps its nature, gender and ability. The Gear's shiny-hunt odds follow the setting.
  *Any Pokemon learns any TM or HM* lets every Pokemon be taught every TM and HM (and,
  with field moves without HMs, use them).

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
* **.pk4 files**: *Export as .pk4* on any Pokemon writes the 136-byte file PKHeX and
  other tools read (named like `0448 - Lucario.pk4`); *Import a .pk4* on the editor's
  first page adds one (136 bytes, or 236 with party stats) to the party, or the PC when
  the party is full. The file picker lets you choose where files go and come from.
* **Trade box** (trading between your save slots): *Send to the trade box* on a
  Pokemon's page takes it out of this save into a box every slot shares
  (`trade_box.bin`, up to 30). In another slot, *Save editor > Trade box* lists them
  with who sent them; tap one to take it (to the party, or the PC when it's full).
  Taken by another slot than the one that sent it, it is traded: Kadabra, Machoke,
  Graveler and Haunter evolve, and so do Onix, Scyther, Seadra, Poliwhirl, Slowpoke,
  Porygon, Porygon2, Rhydon, Electabuzz, Magmar, Dusclops and Clamperl holding their
  item (which is used up), as after a trade in the game (without the evolution scene),
  and the Pokedex counts them. Save in the game afterwards in both slots.
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
overworld twice as fast. On the overworld everything keeps the DS speed at 60: walking
and biking, animations, map props, weather (rain, snow, sand, fog), footprints and bike
tracks, the surf blob's bob, reflections, the "!" emotes, door-mat arrows, shadows
changing with the time of day, wandering NPCs' pauses, and the Distortion World's
clouds.

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

## Nuzlocke helper

Off by default (*Settings > Gameplay > Nuzlocke*). It keeps the run's books and shows
them on the Gear; it doesn't stop you doing anything:

* Each area's first wild encounter is the one that counts. The battle tab says so
  ("This one counts!"), or what the area's encounter was if it's used up, and the
  Area tab shows the current area's. With the *dupes clause* (on), species you have
  already caught don't count; with the *shiny clause* (on), shinies never count and
  can always be caught.
* Pokemon that faint are dead: dimmed with a cross in Party, and listed with where they
  fell in the Trainer tab, with the run's count of areas caught and missed. *Send
  fainted Pokemon to the PC* (off) takes them out of the party after each battle, as
  long as someone is left.
* The books are kept beside the save slot (`save_slotN.nuzlocke`); *Start a new run*
  clears them.

## Game clock

The game reads the device's clock, as a DS reads its own. *Settings > Gameplay > Clock*
runs it up to 23 hours ahead or behind and up to 6 days ahead, e.g. for night-time
encounters and evolutions, or a weekday's event, without touching the device's clock.
The page shows the day and time the game sees. It is a plain offset: the clock keeps
running and the game does not see it as changed.

## Field moves without HMs

On by default (*Settings > Gameplay > Field moves without HMs*). Once an HM is in your
bag, and you have the badge the game asks for, its move works without teaching it to
anyone: walk up to a tree, rock, boulder, rocky wall, water or waterfall and press A as
usual. A Pokemon in your party that could learn the move does it (the first one that
isn't an Egg if none could), with the usual message and cut-in. Fly (and the others,
where they can be used) shows in that Pokemon's menu in the party screen. Owning the HM
is what keeps the story in order: places the game blocks until you have a move are
opened by the same HM as before.

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
