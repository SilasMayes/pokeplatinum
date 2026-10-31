# Saves and save sync

Save slots, import/export, autosave, and keeping saves in step between devices.

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

## Save sync between devices

*Settings > Saves > Sync with your other devices* keeps the save slots the same on the
Thor and a computer (or two handhelds), through a folder that something else copies
between them: Syncthing, a network share, a NAS on your homelab, a cloud drive's synced
folder. Choose the folder on each device and turn on *Sync saves*.

* In the folder each game gets a folder of its own (its name, e.g. `Renegade Platinum`),
  with each slot as `slotN.sav` (a plain 512 KB `.sav` emulators load) and `slotN.txt`
  (which device saved it, when, the trainer, play time, badges and the save's hash). The
  Nuzlocke and shiny hunt records, and the save's version (Complete or Classic), go along.
* After every save (yours or autosave) the slot is copied there. A save made on the other
  device is brought in when the app starts, before the save is loaded, or on the main
  menu, which then reloads the slot. During a game it says so in the corner: reset to the
  title to load it.
* Each device remembers the last save it synced, so it knows which side changed. Saved
  on both since then is a conflict: nothing is overwritten, the slot says *Saved on two
  devices* and *Settings > Saves* offers *Use AYN Thor's save* or *Keep this device's
  save*. A replaced save is always kept as `.bak`.
* Files are written next to their target and renamed over it, and a save is only taken
  once the folder's copy matches the hash in its description (sync programs copy one
  file at a time).
* **Android:** the folder picker's choice is used as a path, which needs *All files
  access*; the page has a button that opens the system screen to turn it on (Android 10
  asks for the storage permission instead). The folder must be on the device's storage
  (internal or SD card), e.g. the one Syncthing syncs.
