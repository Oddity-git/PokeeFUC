# Modding PokeeFUC: handoff

This is the starting point for anyone — another person or a coding agent — who wants to build mods that work with **PokeeFUC**, the FUComplete build of Pokee. Read [AGENTS.md](../AGENTS.md) first; its rules apply unchanged. The port itself is described in [FUCOMPLETE_HANDOFF.md](FUCOMPLETE_HANDOFF.md) and [FUCOMPLETE_PORT.md](FUCOMPLETE_PORT.md); the player settings in the [profile README](../profiles/mhfu/README.md).

There are two different kinds of mod, and they do not use the same machinery:

| | What it changes | How PokeeFUC loads it |
| --- | --- | --- |
| **Game-content mod** | Models, textures, audio, quest data and other files inside `DATA.BIN` | FUComplete's own **File Replacer** (`NATIVEPSP`) |
| **PokeeFUC feature** | The port itself: camera, HUD, menus, new screens, hooks into the game's code | C++ in this repository, built into PokeeFUC |

Pokee's own Mods page (the community `mod.ini` mods) is **off in the FUComplete build**. FUComplete's loader replaces files its own way, and its archive is stored differently. Use FUComplete's replacer for content, not Pokee's Mods page.

## What a PokeeFUC installation looks like

- The program is a separate build (`MHFU_TITLE=FUC`), named **PokeeFUC**, with its own data folder: `~/.local/share/Pokee/FUC` on Linux. PokeeFUC and the standard Pokee build install side by side.
- Inside that data folder, `ms0/` is the PSP's memory stick. FUComplete's files are under `ms0/PSP/SAVEDATA/FUCDAT/`:
  - `CONFIG.BIN` — its options, including **File Replacer**
  - `NATIVEPSP/` — the files the replacer serves
  - `PRELOAD.BIN`, `MODLOAD.BIN` — FUComplete's loader, copied there on first start
- Saves are under `ms0/PSP/SAVEDATA/ULUS10391/` (the Save Region format chosen for this install).
- The menu's **Game → FUComplete** section shows every `CONFIG.BIN` option and writes the file. A change applies at the next start, because FUComplete's loader reads the file then.

The Flatpak keeps the same tree inside its sandbox data directory; the paths below are the portable/tarball ones.

## Path 1: game-content mods with FUComplete's File Replacer

This is FUComplete's supported path, and it is the one to use for model, texture, audio and data mods.

### How the replacer works

FUComplete's loader redirects the game's file I/O to `NATIVEPSP`. Each file in it is named after the game file it replaces, and a `file.bin` in the same folder is a whitelist of what may be replaced. The loader can serve files whose size differs from the original (FUComplete 1.4 removed the old size limit), but the game has to start again to see a changed file, because the loader reads `NATIVEPSP` at start-up.

### Making and installing one

1. Make or download the mod's files. FUCTool's File Replacer tab expects them in a `mods` folder, named as the game's files are named (case-sensitive). Its own [File Replacer guide](https://fucomplete.github.io/docs/fuctool/file_replacer.html) has a worked example (`em83.pac` for a Lavasioth).
2. In **FUCTool → File Replacer**, select that `mods` folder, check that the files are listed, and press **Generate NATIVEPSP**. FUCTool writes the numbered files and the `file.bin` whitelist.
3. Copy the generated `NATIVEPSP` folder into PokeeFUC's data folder:
   `~/.local/share/Pokee/FUC/ms0/PSP/SAVEDATA/FUCDAT/NATIVEPSP`
4. Turn the replacer on. Either **Game → FUComplete → File Replacer → On** in PokeeFUC, or FUComplete's own **Options → File Replacer** in-game. If the mod must load from the memory stick, set **File Replacer Path** to `MS0` in FUCTool's configuration (the default `DISC0` only carries FUComplete's own Dos audio files).
5. Restart PokeeFUC. The mod is live.

Do not edit `CONFIG.BIN` by hand for this; both menus write it correctly. Keep a copy of the file before experimenting.

### Testing a mod without touching a player's data

Never test in the owner's real data folder. Copy it, or start from a test copy (the repository has `testdata` and `testcfg` for exactly this), and point the build at it:

```sh
MHFU_DATA_DIR=/path/to/a/test/copy \
MHFU_OVERLAY_DIR=$PWD/out/fuc/bin/overlays \
MHFU_NO_RENDER=1 MHFU_NO_AUDIO=1 timeout 60 out/fuccam/bin/Pokee
```

For a menu-driven check, run with a window and a save in that copy. The first start asks for the patched image; the mod files go under the copy's `ms0/PSP/SAVEDATA/FUCDAT/`.

Useful switches:

- `MHFU_TRACE_MODS=1` — Pokee's own mod machinery; **not used by the FUC replacer**, but useful when comparing with the standard build.
- `MHFU_TRACE_DATA_BIN=1` — which archive entry the game reads; useful to see the file a model or menu comes from.
- `MHFU_TRACE_IO=1` — file opens and reads, including the memory stick, which shows whether the game asks for a `NATIVEPSP` file.

### Limits and open questions

- FUCTool decides which names it recognises and what goes into `file.bin`. Its table is the source of truth; a file it does not list is not generated.
- Whether a **quest pack** can be replaced this way is not checked. In FUComplete the quest packs are `DATA.BIN` entries 5310–5320; the replacer works by file, and the whitelist may or may not accept them. Try with a copy, and check the quest list and the key-quest marks afterwards.
- A mod cannot add new PSP code. FUComplete's replacer serves data files; Pokee's Mods page lists `Code` mods but cannot run them (recompiled code does not execute guest-supplied code). A code-level change belongs in Path 2 or in a loader patch.
- Freedom Unite mods are not automatically FUComplete mods: the archive layout differs (FUComplete stores it plain) and the file tables are not identical. Use FUComplete's mod tooling and verify in game.

## Path 2: PokeeFUC features in C++

This is how the port's own extras are made. It is the path for anything the game's data cannot express: a new menu, a camera or HUD change, a different quest list, a hook around a game function.

### The shape of a feature

1. **Trace it, do not recall it.** Add or use an `MHFU_TRACE_*` switch and find the game's code or data in FUComplete's executable. The method and the address shifts are in [FUCOMPLETE_PORT.md](FUCOMPLETE_PORT.md#how-addresses-were-ported). Compare against the Freedom Unite build at the same moment when both titles have the feature.
2. **Give it a title flag** in [`profiles/mhfu/host/title.hpp`](../profiles/mhfu/host/title.hpp). A feature traced only in Freedom Unite must start as `kFreedomUniteExtras`; porting it means adding FUComplete addresses and switching the flag on.
3. **Check the instructions before writing.** Every feature has a signature (`CodeWord` arrays or an equivalent) checked against guest memory; a different game version then leaves the feature off instead of writing to the wrong place.
4. **Gate every call site, menu row and test** with the flag, and keep Freedom Unite working. Tests are title-aware; a test that only makes sense for one title prints `skipped` for the other.
5. **Add a trace switch** (`MHFU_TRACE_<THING>`) that says when the hook is in place, and use it in scripted runs. Look at the captures rather than assuming.
6. **Document it** in the profile README (setting, key, variable, behavior), [COMPATIBILITY.md](COMPATIBILITY.md) when it has been checked in game, and the port notes if it changes the porting picture.

Good examples to copy:

- `host/camera/game_camera.cpp` — a host wrapper around the game's camera update, with the original saved and called, signature checks, and a test in `tests/analog_camera_tests.cpp`.
- `host/camera/game_aspect.cpp` — writing a live camera field each frame instead of changing baked code (the lesson: compiled code runs the words it was generated from).
- `host/game/key_quests.cpp` — finding a file the game loaded, tagging every copy of it, and putting the game's words back when the switch goes off.
- `host/game/layered_armor_hook.cpp` — wrapping several game functions with signature checks and title-specific addresses.
- `host/game/hall_meal.cpp`, `item_sets.cpp`, `hall_equipment.cpp` — whole screens built from the game's own windows and input.

### Building and testing

```sh
cmake -S . -B out/fuc -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSPRECOMP_PROFILE=mhfu -DMHFU_TITLE=FUC
cmake --build out/fuc --target Pokee -j 2
cmake --build out/fuc --target psprecomp_test_binaries -j 2
ctest --test-dir out/fuc
```

The FUC build needs its corpus from `EBOOT.FUC.ELF`; the full sequence is in the [profile README](../profiles/mhfu/README.md#fucomplete-work-in-progress). Host-only changes rebuild in seconds; changes under `include/psprecomp/` rebuild everything, including the overlay libraries. Run one build per directory, keep parallelism low (`-j2`), and never drive the game with an open-ended input loop.

### Code that differs from the corpus

FUComplete's loader changes code words at start-up, and its options change more. PokeeFUC detects the differences and runs the affected units in the interpreter (`fuc_code_patches.*`). Two consequences for a mod:

- A feature that wraps the game's function still works when the unit is interpreted, because the wrapper wraps the interpreter.
- A feature that depends on the exact compiled words of a changed unit must check the signature and stay off if the words differ.

The overlay libraries are matched by name and base; a loader-patched overlay is installed from the nearest library, with the differing words interpreted, using the `.code` files `install_user.sh` writes (`tools/overlay_code.py`). If a mod ships an overlay, it needs the same treatment.

### Rules that save time

- **No game data in the repository, commits or releases.** The generated corpus, overlays, `.code` files, disc images, saves and `EBOOT` files stay local.
- **Write it yourself.** FUComplete and FUCTool are GPL-3: take file-format facts (offsets, values, names), never code or text. Record provenance in [SOURCE_PROVENANCE.md](SOURCE_PROVENANCE.md) when third-party code is genuinely included.
- **The owner tests in game.** Do the quick checks, push, and leave the in-game verification to the owner.
- **Say what you did not verify.** Platform, commit, and what was looked at go in the PR and in COMPATIBILITY.
- **Keep it title-aware.** A feature that works in the Freedom Unite build must not read FUComplete memory, or the reverse.

## HD textures

PokeeFUC reads Pokee's KTX2 texture packs, which replace the game's textures at draw time and are independent of FUComplete's replacer. Set one in **Video → Texture pack**, or point **Texture pack folder** at an unpacked pack. Because the FUC disc id differs from Freedom Unite's, a pack that expects `textures/ULUS10391` may need the folder setting instead. The format and the pack layout are in the [profile README](../profiles/mhfu/README.md#hd-texture-packs).

## Where to go next

1. Decide which path the mod needs: **data** → FUComplete's replacer; **behavior** → PokeeFUC C++.
2. For data mods, make a test copy of the data folder and try one file end to end before building a pack.
3. For C++ features, port one small feature by the checklist above, with a trace switch, a test and a README row.
4. Keep the open questions of this document up to date as they are answered.

### Open questions for the next session

- Which names FUCTool accepts in `mods/`, and the exact `file.bin` whitelist format.
- Whether quest packs (5310–5320) and event quest files can be replaced through `NATIVEPSP`.
- Whether a replaced file with a different size is safe in every subsystem on FUComplete 1.4, and where it still needs a restart.
- How a data mod interacts with Pokee's overlay library matching when the same file is also a code overlay (it should not be one, but check).
- The equivalent data-folder paths for a Windows build, when one exists, and for the Android port.

## References

- [FUComplete File Replacer](https://fucomplete.github.io/docs/fuctool/file_replacer.html) — mods folder, `NATIVEPSP`, File Replacer option.
- [FUComplete configuration](https://fucomplete.github.io/docs/fuctool/config.html) — File Replacer, File Replacer Path, every option.
- [FUComplete Patch](https://github.com/FUComplete/Patch) and [FUCTool](https://github.com/FUComplete/FUCTool) — the patch and its tool. Take facts, not code.
- [FUCOMPLETE_PORT.md](FUCOMPLETE_PORT.md) — address mapping, overlays, `CONFIG.BIN` and the interpreter.
- [DATA_BIN.md](DATA_BIN.md) — archive layout, file ids and mod I/O.
- [EQUIPMENT_MODS.md](EQUIPMENT_MODS.md) — which file an equipment mod replaces.
- [LAYERED_ARMOR.md](LAYERED_ARMOR.md) — the armor model records the layered-armor extra reads.
- Tools: `profiles/mhfu/tools/databin.py` (list, extract, cat archive entries), `overlay_code.py`, `add_overlay.py`, `profiles/mhfu/scripts/build_overlay_dumps.sh`.
