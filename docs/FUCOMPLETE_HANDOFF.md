# FUComplete port: handoff

The porting method (how addresses were mapped and checked), the differences beyond addresses and how CONFIG.BIN options run without a new corpus are in [FUCOMPLETE_PORT.md](FUCOMPLETE_PORT.md).

This is the starting point for a contributor or coding agent picking up the FUComplete port ("PokeeFUC"). It says how the port was built, where it stands, how the work is run and what comes next. The player-facing details live in the [profile README](../profiles/mhfu/README.md#fucomplete-work-in-progress) and the tested results in [COMPATIBILITY.md](COMPATIBILITY.md#fucomplete-build-mhfu_titlefuc); this file links to them instead of repeating them. Read [AGENTS.md](../AGENTS.md) first: its rules apply here unchanged.

## What the port is

Pokee runs USA *Monster Hunter Freedom Unite* (`ULUS-10391`) natively, by recompiling its MIPS code to C++. FUComplete is a patch, applied with FUCTool, that turns the Japanese *Monster Hunter Portable 2nd G* (`ULJM-05500`) into an English game with its own loader, mod file replacer and options (`CONFIG.BIN`). It is a different executable, so the port is a second build of the same profile:

- `-DMHFU_TITLE=FUC` at configure time defines `MHFU_TITLE_FUC`. Everything title-specific keys off `mhfu::kTitle` in [`profiles/mhfu/host/title.hpp`](../profiles/mhfu/host/title.hpp).
- The build calls itself **PokeeFUC** (`MHFU_APP_NAME`), with its own desktop id, data folder (`~/.local/share/Pokee/FUC`), generated corpus and overlays. It installs beside the standard Pokee.
- `title.hpp` has one `constexpr bool` per extra. An extra is enabled for FUComplete only once its addresses have been traced again in FUComplete's executable and overlays. Each address has its FUComplete value next to Freedom Unite's, selected by `kTitle`. Each feature still checks the instructions it relies on before it writes, so a different FUComplete version leaves the feature off instead of corrupting memory.

## How it was built

The fork's history on the port branch (`git log --no-merges origin/main..MHFU-to-FUComplete-port`) tells the story commit by commit. The milestones:

1. **A second title** (`9405273`). The installer accepts a FUComplete image (`ULJM05500` with `FUC/PRELOAD.BIN`), and the scripts read the title from the build directory. Generated code, overlays and data are kept apart per title.
2. **FUComplete's loader** (`f444595`, `e9d9c95`).
   - The loader rewrites words of the executable at start-up. The first start writes the patched executable as `EBOOT.FUC.ELF`, and the corpus is generated from that.
   - The loader also patches overlays as they load. Overlays without a matching library are dumped to `overlay_dumps`, and `build_overlay_dumps.sh` builds libraries from them.
3. **Saves across Save Region changes** (`4ed97dd`). Every save key the game has used is kept in `savedata_keys.txt`, so a save written with an earlier key still loads.
4. **The extras, one by one.** Each was re-traced at FUComplete's addresses and then switched on in `title.hpp`, with tests:
   - the analog camera and hold to gather;
   - the aspect ratio's Fill, Give Any Rarity, Empty item slot and Hide HUD;
   - lock-on and damage numbers;
   - UI Scaling, the sharpness gauge and the gather icon, which read the HUD textures 0x4400 lower;
   - layered armor;
   - Eat and Change Equipment in the Guild Hall, and Item Sets;
   - key quests, whose quest packs are DATA.BIN entries 5310 to 5320 instead of 5342 to 5352.

   True Attack Value stays Freedom Unite-only, because FUComplete has its own True Raw option. The Mods page is off, because FUComplete has its own file replacer (`ms0:/PSP/SAVEDATA/FUCDAT/NATIVEPSP`).
5. **FUComplete's options in the menu** (`d231fa1`).
   - [`fuc_config.cpp`](../profiles/mhfu/host/fuc_config.cpp) reads and writes `CONFIG.BIN`, and the Game tab shows every option. Save Region is shown but locked.
   - Options that make the loader change code the corpus was not generated from no longer need a new corpus. The code around each changed word runs in the interpreter ([`fuc_code_patches.cpp`](../profiles/mhfu/host/fuc_code_patches.cpp)).
   - An overlay that differs from a library in a few words is installed from that library, with the changed words interpreted. This needs `<library>.code` files, which `install_user.sh` writes with `tools/overlay_code.py`.
6. **Packaging** (`6e6ae2f`). [`profiles/mhfu/packaging/linux/install_user.sh`](../profiles/mhfu/packaging/linux/install_user.sh) installs a build into `~/.local/share/pokee-fuc` with a launcher and an app-menu entry, without root.

Method that worked, and that should continue:

- Trace, don't recall. Use the `MHFU_TRACE_*` switches and compare against the Freedom Unite build at the same moment.
- Find each Freedom Unite instruction pattern in FUComplete's code. The executable's code mostly sits a few words, or 0x2FC, lower, and 0x2948 higher in places.
- Data moved too: GameSys is at 0x089C7508, the camera object at 0x089C6CB4, the entity manager at 0x09D146E0, and the item atlas at 0x091830A0.
- The game may hold several copies of a file it read. FUComplete reads a quest pack into a staging buffer and copies it to the list's own; a name offset belongs to one copy, so the key quests tag every copy against its own start (tagging only the staging copy crashed the game on the list's copy).
- `profiles/mhfu/tools/databin.py` reads FUComplete's `DATA.BIN`, which is stored without the obfuscation.

## Building and running

Follow the [profile README's FUComplete section](../profiles/mhfu/README.md#fucomplete-work-in-progress): configure `out/fuc` with `-DMHFU_TITLE=FUC`, install the FUCTool image, generate from `EBOOT.FUC.ELF`, build, optionally build overlays, then run `install_user.sh out/fuc`. Unit tests use the same commands as the standard build (`psprecomp_test_binaries`, then `ctest`). For a quick boot check, run `MHFU_NO_RENDER=1 MHFU_NO_AUDIO=1 timeout 40 out/fuc/bin/Pokee <game dir>`.

## How the work is run

- **Repository.** This is the fork `Oddity-git/PokeeFUC`, a fork of Pokee. Pull requests are opened only on this fork, never upstream. Commits and posts use the GitHub username only, with no personal name.
- **Branches.**
  - `MHFU-to-FUComplete-port` holds the port. Draft PR #4 targets the fork's `main` and is not to be merged for now.
  - `android-port` holds the Android work (PR #3).
  - Never push to `main`.
- **Keeping up with Pokee.**
  1. The owner presses *Sync fork* on GitHub, which brings Pokee's latest into the fork's `main`.
  2. The agent merges `main` into the port branch. Do not rebase, and do not use the PR's *Update branch* button.
  3. Any new Freedom Unite-address feature from Pokee stays gated off for FUComplete in `title.hpp`, until it is re-traced.
- **Two working copies.**
  - Cloud sessions work in a clean clone.
  - A local session on the owner's Linux PC builds, installs and plays the game there.
  - Both push to the port branch. Always fetch before committing.
- **CI on PR #4.** Two checks also fail on Pokee's `main`, and are not this branch's to fix:
  - the C++ formatting check: files formatted differently upstream;
  - cppcheck's three blocking findings in `item_sets.cpp:555`, `vertex_hold.hpp:94` and `vertex_cache_tests.cpp:73`.

  Every other check should be green. The coverage gate requires 80% of changed lines. A changed header that holds only declarations is exempt (`scripts/ci/project_coverage.py`). The Android NDK job occasionally flakes on setup; rerun it once.

## Current state

The game boots, plays through the village, Guild Hall and quests, and saves on Linux. Every extra in `title.hpp` except True Attack Value is on for FUComplete, including the `[Key]` marks on the quest lists. Player checks are recorded in [COMPATIBILITY.md](COMPATIBILITY.md#fucomplete-build-mhfu_titlefuc).

## Open work, in order

1. **Play-test FUComplete's options.** The hunting horn tweak, the Guild Hall chest and drink buff, and the rest. Field of view has a live control of Pokee's own (Video > Field of view, `video.fov_scale`); FUComplete's own option sets the player's field of view in a quest and still applies at the next start. Also check the camera tilt and the Guild Hall features, then record the results in `COMPATIBILITY.md` with the commit tested.
2. **Overlays FUComplete patches.** The bow and bowgun aim extras stay off while game_task runs interpreted. Building libraries from the dumps (`build_overlay_dumps.sh`) after a full village-and-quest run removes most of that.
3. **A formatting-only commit** for the files this branch touches, once feature work settles.
4. **Android** (`android-port`, PR #3): continue there, with the same title gating.
5. **Each Pokee sync:** merge, gate new extras, and port them one at a time as in step 4 of the build history.
