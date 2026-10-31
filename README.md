# Pokémon Platinum for the AYN Thor

A native port of Pokémon Platinum for the **AYN Thor** and other Android devices, with PC builds for Windows and
Linux. It runs the decompiled game directly (no emulator), puts the DS bottom screen on the Thor's lower panel,
and fills the rest of that panel with the **Gear**: a companion screen with the battle menu, damage and turn
order, your party, the area's wild Pokémon, a Pokédex, a map with items, the PC and the bosses' teams.

It is built on [cybervisi0n's PC port](https://github.com/cybervisi0n/pokeplatinum) and its
[libntr](https://github.com/cybervisi0n/libntr) libraries, which are built on the
[pret decompilation](https://github.com/pret/pokeplatinum). This is an independent fork: cybervisi0n and pret
aren't involved in it, so please bring questions and bug reports here, not to them.

## You need your own copy of the game

This repository and its releases contain **no ROM and no game assets**. On first launch the game asks for your own
dump of **Pokémon Platinum (USA)** (`.nds`, game code CPUE) and extracts what it needs from it. Renegade Platinum
works the same way: the app applies Drayano's patch, which you download yourself, to your own ROM. Nothing here
includes or links to ROMs or patches.

## Download

Get the newest release from the [Releases page](https://github.com/SilasMayes/pokeplatinum-thor/releases):

| File | For |
|---|---|
| `.apk` | Android 10+ on arm64 (the AYN Thor, phones, other handhelds) |
| `windows-x64.zip` | Windows 10/11, 64-bit. Run `main.exe` |
| `linux-x64.tar.gz` | 64-bit Linux (glibc 2.38+). Run `main` or the AppImage |

`thor-android-latest` is an automatic build of the newest commit; numbered releases are the ones played on a Thor.

**Install the APK only from this repository's Releases page.** All builds are signed with one key so that each
installs over the last and keeps your saves, and that key is part of the source code. Anyone can therefore build
an APK that Android accepts as an update to this app. Details in
[Getting started](docs/thor/getting-started.md#getting-the-apk).

## Features

All optional, and the defaults stay close to the DS game. The [manual](docs/thor/README.md) covers each one.

- **Two screens on the Thor**: top screen on the main panel, touch screen on the lower one; widescreen on the
  overworld and in battles (experimental). [Features](docs/thor/features.md#screens)
- **The Gear**: battle menu with damage ranges, KO marks and turn order; party, Day Care and eggs; wild Pokémon by
  area and time; Pokédex; map with item balls and hidden items; PC sorting; trainer card, level cap and the bosses'
  teams; shiny-hunt counters. [The Gear](docs/thor/gear.md)
- **Renegade Platinum** and **randomized games** side by side with the original, each with its own saves.
  [Features](docs/thor/features.md#more-than-one-game-renegade-platinum)
- **Saves**: four slots, `.sav` import and export, autosave, and sync between devices through Syncthing or any
  synced folder. [Saves and save sync](docs/thor/saves-and-sync.md)
- **Quality of life**: 60 FPS on the overworld, instant text, fast-forward, field moves without HMs, *Use another
  Repel?*, whole-party Exp. Share, game clock offset, Nuzlocke helper. [Features](docs/thor/features.md)
- **GBA slot** for Pal Park and dual-slot encounters from a GBA save, **link play** between two devices for trades
  and battles, and **texture packs**. [Features](docs/thor/features.md#gba-slot-pal-park-and-dual-slot-pokemon),
  [Link play](docs/thor/link-play.md)
- **Cheats and a save editor**, off unless you open them. [Cheats and the save editor](docs/thor/cheats-and-save-editor.md)

## Build from source

You need the same ROM to play what you build. Building doesn't need it.

**Android:** see [docs/thor/building.md](docs/thor/building.md). In short, with the Android NDK, SDK, JDK 17 and
Gradle installed:

```sh
make meson
ANDROID_NDK_HOME=/path/to/ndk ./tools/android/build_apk.sh
```

**Linux:** with Docker, `./drun.sh make linux`. Without it, install `nasm`, `enet`, `arm-none-eabi-gcc`, `ninja`,
`flex` and `bison`, then `make linux`. The game is built in `build_linux/pokeplatinum/`.

**Windows:** upstream's MSYS2 route: run `Install_MSys2.ps1` once in PowerShell, then `Launch_MSys2.ps1` and
`make win64` in the shell it opens. The released Windows builds are cross-compiled from Linux instead, and the
changes that needs aren't in this repository yet.

**The DS ROM** still builds from the same tree with `make`, as in pret's project; see [INSTALL.md](INSTALL.md).

The port's own code is in `src/port/` and `include/port/`, and the game-independent part (save slots, autosave,
save sync, cheats and more) is in [port-common](https://github.com/SilasMayes/port-common), shared with a
HeartGold/SoulSilver port. Everything else under `src/` is the decompiled game.

## Contributing

Bug reports and ideas are welcome in [Issues](https://github.com/SilasMayes/pokeplatinum-thor/issues); questions in
[Discussions](https://github.com/SilasMayes/pokeplatinum-thor/discussions). See [CONTRIBUTING.md](CONTRIBUTING.md).

**On AI:** this fork is built with AI assistance (Claude), and commits made that way say so. AI-assisted pull
requests are welcome if you've tested them on hardware: say what you ran it on and what you checked.

## Credits

- [pret](https://github.com/pret/pokeplatinum): the decompilation of Pokémon Platinum.
- [cybervisi0n](https://github.com/cybervisi0n): the PC port and libntr, which do the hard part of running a DS
  game natively.
- Drayano: Renegade Platinum, which this build can apply to your ROM.
- [SDL](https://www.libsdl.org/), [ENet](http://enet.bespin.org/), [Dear ImGui](https://github.com/ocornut/imgui),
  and the [Atkinson Hyperlegible](https://brailleinstitute.org/freefont) font by the Braille Institute.

## License

The code this fork adds (`src/port/`, `include/port/`, `android/`, `tools/android/`) is MIT; see [LICENSE](LICENSE).
The decompiled game code has no license. Bundled libraries keep their own licenses; see
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md). Releases include all of these notices.

Pokémon is © Nintendo, Game Freak and Creatures. This project isn't affiliated with or endorsed by them. Please
support the official games.
