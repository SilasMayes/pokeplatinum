# Contributing

Thanks for looking. This is a small hobby project, so replies may take a few days.

## Reporting a bug

Open an [issue](../../issues/new/choose) with the bug report form. The most useful details are the device and
Android version, the build (the release you downloaded, or the version in Android's app info for "Platinum"), which game (Platinum,
Renegade Platinum, randomized), and the steps to make it happen. A logcat helps with crashes:
`adb logcat -s pokeplatinum libntr-display`.

Please don't attach ROMs, and only attach a save if you're fine with it being public.

## Pull requests

- Branch from `platinum-thor` and open the PR against it. GitHub Actions builds an APK for every PR; you can
  download it from the run.
- Keep a PR to one change, and update the manual in `docs/thor/` if players will notice it.
- Changes to the decompiled game (outside `src/port/` and `include/port/`) must leave the DS ROM build unchanged:
  put port-only code behind `SDK_PORT`.
- Game-independent features usually belong in [port-common](https://github.com/SilasMayes/port-common), and
  renderer or platform changes in [the libntr fork](https://github.com/SilasMayes/libntr).
- AI-assisted PRs are welcome if you've tested them on hardware (or a desktop build, for things a desktop can
  show). Say what you ran it on and what you checked.

## Upstream

`pc_port` tracks [cybervisi0n's PC port](https://github.com/cybervisi0n/pokeplatinum), which this fork is based on.
cybervisi0n isn't involved in this fork, so please don't send issues about it there. Fixes to the decompilation
itself are better sent to [pret](https://github.com/pret/pokeplatinum); pret's contributing guide (naming, code
style, formatting) is kept in [docs/pret-contributing.md](docs/pret-contributing.md).

By contributing you agree that your code is released under this repository's [MIT license](LICENSE).
