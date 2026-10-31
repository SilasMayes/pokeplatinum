# Licenses

This repository mixes code from several sources under different terms.

| Part | Source | License | Text |
|---|---|---|---|
| The fork's own code: `src/port/`, `include/port/`, `android/`, `tools/android/`, `tools/scripts/bin2c.py`, `tools/scripts/copy_licenses.sh` | Silas Mayes | MIT | [LICENSE](LICENSE) |
| The decompiled game code and data (`src/`, `include/`, `asm/`, `res/`, ...) | [pret/pokeplatinum](https://github.com/pret/pokeplatinum) | None. The decompilation carries no license. | — |
| PC port changes to the decompilation | [cybervisi0n/pokeplatinum](https://github.com/cybervisi0n/pokeplatinum) | None stated | — |
| [port-common](https://github.com/SilasMayes/port-common) (meson subproject) | Silas Mayes | MIT | in that repo |

*Pokémon* and *Pokémon Platinum* are trademarks and copyrights of Nintendo, Game Freak and Creatures. This
project is not affiliated with or endorsed by them, or by pret or cybervisi0n.

## Linked into the builds

These are compiled into the Android APK and the PC builds. Every release must carry their notices; the release
archives do this in a `licenses/` folder, built from the files listed here.

| Library | Used for | License | Text |
|---|---|---|---|
| [libntr](https://github.com/cybervisi0n/libntr) (via the [SilasMayes/libntr](https://github.com/SilasMayes/libntr) fork) | NitroSDK replacement: renderer, audio, input, files | MIT, © cybervisi0n | [licenses/libntr.txt](licenses/libntr.txt) |
| libntrsystem, libntrwifi, libntrdwc, libvct (cybervisi0n) | NitroSystem / WiFi / DWC / VCT replacements | see each repo | linked in `subprojects/*.wrap` |
| [SDL2](https://github.com/libsdl-org/SDL) 2.30 | window, input, audio, Android glue | zlib | [licenses/SDL2.txt](licenses/SDL2.txt) |
| [ENet](https://github.com/lsalzman/enet) 1.3.18 | link play over the network | MIT | [licenses/enet.txt](licenses/enet.txt) |
| [yyjson](https://github.com/ibireme/yyjson) 0.12.0 | JSON | MIT | [licenses/yyjson.txt](licenses/yyjson.txt) |
| [RapidJSON](https://github.com/Tencent/rapidjson) 1.1.0 | JSON | MIT (plus the notices in its file) | [licenses/rapidjson.txt](licenses/rapidjson.txt) |
| [Dear ImGui](https://github.com/ocornut/imgui) (bundled in libntr) | settings, Gear and debug UI | MIT | [licenses/dear-imgui.txt](licenses/dear-imgui.txt) |
| Other code bundled in libntr (`libraries/sim/src/`): blip_buf (LGPL-2.1), fireflower (BSD-3-Clause), ImGuiFileDialog, stb, glad | audio resampling, sound, file dialogs, images, GL loading | see each folder in libntr | in libntr |
| [Atkinson Hyperlegible](https://brailleinstitute.org/freefont) | the Gear and settings font | SIL Open Font License 1.1 | [res/pc_port/fonts/AtkinsonHyperlegible-OFL.txt](res/pc_port/fonts/AtkinsonHyperlegible-OFL.txt) |

blip_buf is LGPL-2.1 and is linked statically. Its source, and the source of everything it is linked with, is in
this repository and its subprojects, so the build can be redone with a modified copy.

## Build tools only (not in releases)

- `tools/cw/` (CodeWarrior compiler binaries), metroskrew and `res/prebuilt/` come from pret/upstream and are
  used only to build the matching DS ROM. They are not part of any release.
- `tools/msgenc/` includes pugixml (MIT, `tools/msgenc/LICENSE.pugixml.md`).
- Tracy (BSD-3-Clause) is an optional profiler, off by default.
