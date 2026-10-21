# PokeeFUC

PokeeFUC runs **Monster Hunter Portable 2nd G** (Japan, `ULJM-05500`) patched with [FUComplete](https://github.com/FUComplete/Patch) natively on Linux, by recompiling the game's PSP code to C++. It is the FUComplete build of [Pokee](https://github.com/LocalBBQ/Pokee), which runs *Monster Hunter Freedom Unite* (USA); the base project and its build are described [below](#the-base-project).

> **This project does not include the game.** You need your own legally obtained copy of Monster Hunter Portable 2nd G, and you must patch it with FUComplete yourself.

## What you need

1. Your own copy of **Monster Hunter Portable 2nd G** for PSP (`ULJM-05500`), dumped as an ISO.
2. **FUComplete** applied to that ISO. Follow FUComplete's [setup guide](https://fucomplete.github.io/docs/setup.html) and use [FUCTool](https://github.com/FUComplete/FUCTool); its [configuration page](https://fucomplete.github.io/docs/fuctool/config.html) explains the options FUCTool can set. FUCTool checks that the dump is a supported release and writes a patched image. That patched image is what PokeeFUC starts from.

The FUComplete project provides the patch and its own instructions; it is separate from PokeeFUC. No game data, assets or patched executables are included in this repository or its releases.

## Install on Linux

Linux is the platform tested so far. Windows and macOS are not built yet; an Android port is in progress on its own branch.

### Release

The Linux tarball and Flatpak for x86-64 are on the [releases page](https://github.com/Oddity-git/PokeeFUC/releases).

**Flatpak** (recommended on the Steam Deck and most desktops):

```bash
flatpak install --user ./pokeefuc-<version>-linux-x86_64.flatpak
flatpak run io.github.teamgdb.PokeeFUC
```

**Portable tarball**:

```bash
tar -xzf pokeefuc-<version>-linux-x86_64.tar.gz
cd pokeefuc-<version>-linux-x86_64
./pokee-fuc --install /path/to/MHP2G_FUC.iso   # or let the first start ask for it
./pokee-fuc
```

The first start asks for the FUComplete-patched image, checks it and prepares the game. Settings and saves live in `~/.local/share/Pokee/FUC` (the Flatpak keeps them in its own data directory). PokeeFUC installs beside the standard Pokee build.

### From source

Building needs CMake 3.20 or newer, Ninja, a C++20 compiler, Python 3, SDL3, Vulkan, `glslangValidator`, and a few gigabytes of free memory.

```bash
cmake -S . -B out/fuc -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSPRECOMP_PROFILE=mhfu -DMHFU_TITLE=FUC
cmake --build out/fuc --target Pokee -j 2
out/fuc/bin/Pokee --install "/path/to/MHP2G_FUC.iso"
out/fuc/bin/Pokee                              # first start: writes EBOOT.FUC.ELF, then quit
MHFU_EXECUTABLE="$HOME/.local/share/Pokee/FUC/EBOOT.FUC.ELF" \
    profiles/mhfu/scripts/generate.sh out/fuc
cmake --build out/fuc --target Pokee -j 2
profiles/mhfu/scripts/build_overlays.sh out/fuc      # about 40 minutes, resumable
profiles/mhfu/packaging/linux/install_user.sh out/fuc --overlays out/fuc/bin/overlays
```

The corpus is generated from `EBOOT.FUC.ELF`, not from the disc's executable: FUComplete's loader rewrites words of the executable at start-up, and compiled code runs the words it was generated from. The first start writes the loader's result as `EBOOT.FUC.ELF`; from then on the build starts from it.

Full details: [the FUComplete section of the profile README](profiles/mhfu/README.md#fucomplete-work-in-progress) and [the port handoff](docs/FUCOMPLETE_HANDOFF.md).

## Status

Working on Linux: FUComplete's loader, menus, character creation, the village, the Guild Hall, quests and saves; FUComplete's own options in the Game tab; the analog camera and tilt, lock-on, hold to gather, the aspect ratio's Fill, HUD hiding, UI scaling, the gather icon, the sharpness gauge, the empty item slot, Give Any Rarity, damage numbers and layered armor; Eat in the Guild Hall, Item Sets and Change Equipment; the `[Key]` marks on quests; a live field of view scale; HD texture packs in KTX2.

Still open: the in-game effect of each `CONFIG.BIN` option; Change Equipment's sync to other hunters, and Give Any Rarity with a second player; the bow and bowgun aim extras while `game_task` runs interpreted; Windows, macOS and Android. The detailed state is in [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md#fucomplete-build-mhfu_titlefuc).

## Legal disclaimer

**PokeeFUC** is an independent, open-source project and is not affiliated with, authorized by, sponsored by, or endorsed by CAPCOM, Sony, the FUComplete authors, or any of their affiliates.

Monster Hunter, Monster Hunter Portable 2nd G, Monster Hunter Freedom Unite, CAPCOM, PlayStation, PSP, FUComplete and all related trademarks, game assets, artwork, audio, characters, and other intellectual property belong to their respective owners.

**PokeeFUC** does not include any game assets or original game files: no disc image, no copy of the game's executable or data, and no textures, models, audio or video from the game. You must provide the files from your own legally obtained copy of Monster Hunter Portable 2nd G (Japan, `ULJM-05500`), patched with FUComplete, to install or build **PokeeFUC**.

Users are solely responsible for obtaining, dumping, extracting, patching, and using their game copy in accordance with the laws applicable in their jurisdiction. **PokeeFUC** does not support, provide, link to, or encourage the use of unauthorized or pirated copies of the game.

Any references to the original game or its trademarks are made solely for identification, compatibility, and interoperability purposes. Screenshots and other depictions of the original game may be used solely to document or demonstrate **PokeeFUC's** functionality. All depicted third-party game content remains the property of its respective rights holders.

The license covering **PokeeFUC** applies only to the project's own original code and materials and does not grant any rights to third-party intellectual property. **PokeeFUC** provides the software, not the game.

## Controls

On a gamepad the buttons are where you expect them: on a PlayStation pad circle confirms and cross backs out, as the game's prompts say, and the right stick drives the camera and aiming. Control presets (Default, Modern with attacks on the triggers, Left-handed, Classic keyboard, and your own) cover every control, and any of them can be bound to a single button or a combination such as L1 + ○. Keyboard and mouse play is complete; by default WASD moves, the mouse controls the camera, and its buttons attack. The full tables are in the [profile README](profiles/mhfu/README.md#running).

Esc, or both sticks pressed together (L3+R3), opens PokeeFUC's own menu. It holds video, audio, control, network and save settings, including aspect ratio, frame rate, internal resolution, the field of view and HD texture pack import. The first start sets the game up from your patched image in the same window, and works with a gamepad alone.

## The base project

PokeeFUC is built from [Pokee](https://github.com/LocalBBQ/Pokee), the native port of *Monster Hunter Freedom Unite* (USA, `ULUS-10391`), which is itself built from [Yakumo](https://github.com/TeamGDB/Yakumo). The Freedom Unite build remains in this repository (`MHFU_TITLE=FU`, the default) and shares the host, the runtime and the profile; it is documented in the [profile README](profiles/mhfu/README.md). Upstream Pokee publishes its Freedom Unite releases and issue tracker.

The FUComplete build is this fork's work: the second title, its loader, saves across Save Region changes, the ported extras and the options in its menu. [The port handoff](docs/FUCOMPLETE_HANDOFF.md) and [the port notes](docs/FUCOMPLETE_PORT.md) describe how that was done and what is left.

## Roadmap

- The in-game effect of FUComplete's `CONFIG.BIN` options
- Overlay libraries built from the loader's dumps, so the aim extras can turn on
- Windows, macOS and Android
- A formatting-only pass over the files this branch touches

## How it works

The executable is analyzed and every instruction of its code is emitted as C++, which is compiled into the program. The game also loads 290 code overlays at run time into a handful of shared memory slots; each is recompiled into its own library, and when the game loads one, the matching library is installed between frames. An interpreter covers any code the recompiled set does not reach, so nothing stops the game — it only runs slower there. FUComplete's loader and the code it patches are handled on top of that: see [the port notes](docs/FUCOMPLETE_PORT.md).

Around that code sits a reimplementation of the PSP system: a kernel with threads, semaphores, event flags and timers; disc I/O read straight from the image; a Vulkan renderer for the PSP's graphics engine; software voice mixing for audio; and input from SDL3.

[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) describes the execution model and [`docs/DATA_BIN.md`](docs/DATA_BIN.md) the game's archive format. [`docs/TESTING.md`](docs/TESTING.md) has the smoke test and how to report results.

## Repository layout

```text
include/psprecomp/   Framework interfaces: runtime, memory, Allegrex state
src/                 Framework: ELF loading, decoder, runtime, interpreter
tools/               Framework: analyzer and C++ code generator
tests/               Framework regression tests
configs/             PSP NID data and generic examples
profiles/mhfu/       Everything specific to this game: host, kernel, renderer,
                     audio, input, configuration and build scripts
docs/                Architecture, archive format, profile guide, source rules,
                     and the FUComplete port notes
```

The recompiled code itself is generated locally from your copy of the game and is never committed.

## Built on PSPRecomp

**PokeeFUC** is built on [PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp), a static recompilation framework for PSP software. The framework is game-neutral and can be built on its own:

```bash
cmake -S . -B out/framework -DPSPRECOMP_PROFILE=""
cmake --build out/framework --config Release
ctest --test-dir out/framework -C Release --output-on-failure
```

To target another title, see [`docs/PROFILE_GUIDE.md`](docs/PROFILE_GUIDE.md). [`docs/SOURCE_PROVENANCE.md`](docs/SOURCE_PROVENANCE.md) sets out the rules for independently written code and third-party source.

## Authors

[PokeeFUC](https://github.com/Oddity-git/PokeeFUC) is a fork of [Pokee](https://github.com/LocalBBQ/Pokee), built from [Yakumo](https://github.com/TeamGDB/Yakumo), the Portable 3rd port. Those projects and their contributors keep the credit for the host this tree starts from:

- [@MHunterG](https://github.com/MHunterG)
- [@mojitosunrise](https://github.com/mojitosunrise)

The FUComplete port, its title and its extras are this fork's work.

## Credits

- [Yakumo](https://github.com/TeamGDB/Yakumo) — the original port, by its contributors
- [Pokee](https://github.com/LocalBBQ/Pokee) — the Freedom Unite port this fork starts from
- [FUComplete](https://github.com/FUComplete/Patch) — the patch, its loader and its configuration tool
- [PSPRecomp](https://github.com/jessicanataliagta/PSPRecomp) — the recompilation framework this project builds on
- [SDL3](https://www.libsdl.org/) — windowing, input and audio output
- [FFmpeg](https://ffmpeg.org/) — music and movie decoding
- [Vulkan](https://www.vulkan.org/) and [MoltenVK](https://github.com/KhronosGroup/MoltenVK) — rendering
- [Dear ImGui](https://github.com/ocornut/imgui) — PokeeFUC's menus and setup screens
- [tiny-AES-c](https://github.com/kokke/tiny-AES-c) — the installer and PSP save-data support
- [stb_truetype](https://github.com/nothings/stb) — font rasterization
- [svanheulen/mhef](https://github.com/svanheulen/mhef) — community documentation of the game's archive format
- [Cinzel](https://github.com/NDISCOVER/Cinzel) and [Shippori Mincho](https://github.com/fontdasu/ShipporiMincho) — the logo's lettering, under the SIL Open Font License

## License

The repository is distributed under the MIT License; see [`LICENSE`](LICENSE). Third-party files keep their own notices beside them; [`docs/SOURCE_PROVENANCE.md`](docs/SOURCE_PROVENANCE.md) lists their origins and licences.
