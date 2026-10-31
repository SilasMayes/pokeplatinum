# Notes for AI coding agents (and people)

This fork is developed with AI assistance. Contributions made with AI are welcome if they were tested on real
hardware or at least on a desktop build; say in the PR what you ran and what you checked.

## Layout

- `src/port/`, `include/port/`: the port layer this fork adds (Gear, settings pages, save editor, randomizer,
  ROM-hack support, ...). `src/port/game_platinum.c` is the adapter for port-common.
- `subprojects/port-common` (from `port-common.wrap`, pinned to a commit): the game-independent modules shared
  with the HeartGold/SoulSilver port. Change those in [SilasMayes/port-common](https://github.com/SilasMayes/port-common),
  then bump the pin here.
- `subprojects/libntr` (from `libntr.wrap`, pinned to a commit of the `thor-android` branch of
  [SilasMayes/libntr](https://github.com/SilasMayes/libntr)): renderer, audio, input, files, settings UI.
- `android/`, `tools/android/build_apk.sh`: the APK. `docs/thor/`: the user manual; update it with any
  user-visible change.
- Everything else in `src/`, `include/`, `asm/` and `res/` is the decompiled game from pret. Changes there must be
  behind `SDK_PORT` (or otherwise leave the DS build identical), so `make` still produces the matching ROM.

## Building and checking

- Linux: `make linux` (or `./drun.sh make linux`). Android: `make meson`, then
  `ANDROID_NDK_HOME=... ./tools/android/build_apk.sh`.
- Playing needs a Pokémon Platinum (USA) ROM, which is never committed. Don't add ROMs, saves, extracted game
  files or patched data to the repository.
- Before calling a change done: it builds for Linux and Android, and if it touches the game loop, a desktop run
  gets from boot to the overworld without an `[assert]`.

## Branches

- `platinum-thor` is the main branch; work on a branch and open a PR against it.
- `pc_port` tracks upstream (cybervisi0n). Don't commit to it.
