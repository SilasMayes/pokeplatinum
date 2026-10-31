# Cheats and the save editor

All off by default; nothing here changes a save until you save in the game.

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
