# The FUComplete port (PokeeFUC)

How the FUComplete build of the mhfu profile was made, what state it is in, and how to carry on. Written as a handover for the next contributor or coding agent. Read [AGENTS.md](../AGENTS.md) first; this page assumes it. [FUCOMPLETE_HANDOFF.md](FUCOMPLETE_HANDOFF.md) covers the branches, the two working copies and the pull request's CI checks. Player-facing details are in the [profile README](../profiles/mhfu/README.md#fucomplete-work-in-progress), verified results in [COMPATIBILITY.md](COMPATIBILITY.md#fucomplete-build-mhfu_titlefuc).

## What it is

[FUComplete](https://github.com/FUComplete/Patch) patches **Monster Hunter Portable 2nd G** (`ULJM-05500`) with an English translation, event quests, a file replacer and options in `CONFIG.BIN`. It is a different executable and different overlays from Freedom Unite (`ULUS-10391`), so it is a second **title** of the same profile, chosen at configure time:

```sh
cmake -S . -B out/fuc -G Ninja -DCMAKE_BUILD_TYPE=Release -DMHFU_TITLE=FUC
```

The build calls itself **PokeeFUC** wherever a player reads its name (`MHFU_APP_NAME`, `MHFU_APP_ID` in `host/title.hpp`) and keeps its corpus in `profiles/mhfu/generated_fuc/`, its overlay corpora in `overlays_fuc/`, its game files in `game_fuc/`, its analysis in `analysis_fuc/`. A Freedom Unite build directory and a FUComplete one can live in the same checkout.

The work lives on the branch `MHFU-to-FUComplete-port` of the fork, with draft pull request into the fork's `main`. The fork's `main` mirrors upstream Pokee; keep it that way (see *Keeping up with Pokee*).

## How it was made, in order

1. **Title switch.** `host/title.hpp` holds `enum class Title`, `kTitle` from `MHFU_TITLE_FUC`, and one `constexpr bool` per extra: `kAnalogCamera`, `kLockOn`, `kGatherHold`, `kGameAspect`, `kGiveRarity`, `kEmptyItemSlot`, `kHideHud`, `kDamageNumbers`, `kUiScaling`, `kSharpnessGauge`, `kGatherIcon`, `kLayeredArmor`, `kHallMeal`, `kItemSets`, `kHallEquipment`, `kKeyQuests` (all ported), `kTrueAttack` (Freedom Unite only: FUComplete has True Raw), `kFreedomUniteExtras` (the fast item box and the camera probe, Freedom Unite only). Every call site, menu row and test follows its flag. A new upstream extra starts as `= kFreedomUniteExtras` until it is ported.
2. **Installer and identity.** `host/install/game_identity.hpp` describes the FUComplete release: `ULJM05500`, a plain `EBOOT.BIN` (no hash pinned; every FUComplete version changes it), FUComplete's `FUC/PRELOAD.BIN` on the disc. The executable is prepared from the player's own patched ISO.
3. **Corpus.** `scripts/generate.sh <build dir>` and `scripts/build_overlays.sh` read the title from the build directory. FUComplete's loader (`PRELOAD.BIN`, `MODLOAD.BIN`, copied to `ms0:/PSP/SAVEDATA/FUCDAT`) runs interpreted.
4. **The loader's code changes** (`host/fuc_code_patches.*`). The loader rewrites words of the executable at boot (file I/O hooks, save keys). The first start writes the executable as the loader left it, `EBOOT.FUC.ELF`; the corpus is generated from that file (`MHFU_EXECUTABLE=<data>/EBOOT.FUC.ELF scripts/generate.sh`), and `main.cpp` starts from it whenever the corpus's SHA-256 matches.
5. **Patched overlays.** The loader also patches `game_task` and `lobby_task` as they load. Overlays no library matches are dumped to `<data>/overlay_dumps`; `scripts/build_overlay_dumps.sh <build dir>` builds a library from each (`tools/add_overlay.py`). There are two `lobby_task` corpora: the plain one and the patched one.
6. **Save keys.** The Save Region patch changes the save's encryption key. `host/save_data/savedata_store.*` keeps every key the game passes in `savedata_keys.txt` and loads a save with an earlier key when the current one fails.
7. **Porting each extra.** Every extra reads or hooks addresses traced in Freedom Unite. Each got a FUComplete value beside the Freedom Unite one (`kFreedomUniteCode ? FU : FUC` or a `for_title` table) and a second signature array (`kSignatureFUComplete`), selected by title. How the addresses were found is below.
8. **Renaming and install.** `packaging/linux/install_user.sh <build dir> --overlays <dir>` installs into `~/.local/share/pokee-fuc` with a `.desktop` entry; data lives in `~/.local/share/Pokee/FUC`.
9. **Texture packs in KTX2** (Basis Universal UASTC/ETC1S, zstd): `host/gpu/ktx2_image.*` with the vendored transcoder in `third_party/basis_universal` (title-independent; Freedom Unite gets it too).
10. **FUComplete's options in the menu** (`host/fuc_config.*`, the FUComplete section of the Game tab) and **running changed code without a new corpus** (`fuc::run_interpreted`, `install_nearest_overlay` in `host/overlays.cpp`, `tools/overlay_code.py`). See *How CONFIG.BIN options run* below.

## How addresses were ported

FUComplete's code is the same code as Freedom Unite's, shifted: the executable a few words or 0x2FC lower (0x2948 higher in places), `game_task`/`lobby_task` at 0x09A5A580 instead of 0x09A5F300 (code a further 0x78 to 0x5168 lower), `game_sub` at 0x09C14280 instead of 0x09C19100. Data moved too: GameSys pointer 0x089C7508 (FU 0x089CC558), most data in that region by −0x5050, lobby data by −0x53C0 to −0x5488.

The method, which worked for every extra:

1. **Code addresses**: match a window of ±10 instructions around the FU address against the FUComplete image, with immediates and jump targets masked (opcode-level match), and take the best unique hit. Widen the window when two hits tie. Images: FU executable and overlays (extracted from DATA.BIN with `tools/databin.py`), FUComplete `EBOOT.FUC.ELF` and its overlay dumps.
2. **Check each mapped word**: the FUComplete word must be the same instruction; for a `jal`/`j`, its target must be the mapping of the FU target; for an immediate that differs, explain it.
3. **Data addresses**: never map data by bytes. Find FU instructions that form the address (`lui` + `lw`/`addiu`/`ori` on the same register), map those instructions, and read the address the FUComplete instructions form. Several references should agree.
4. **Strings and tables**: read the FUComplete word at the address the code forms (e.g. "%s%s" at 0x0893875C, the box label lists at 0x09AFFD98).
5. **Generate the FUComplete signature from the checked mapping**, never by hand, and confirm in a scripted run that the feature's `[trace]` line says it put its hook in place (all words matched memory).

Differences that are not just addresses, found this way:

- **Buttons**: FUComplete's item box chooses with ○ (0x2000) and backs out with × (0x4000), the Japanese way. `item_sets_menu.hpp` and `hall_equipment.hpp` swap `kConfirm`/`kCancel` by title.
- **Box menu rows** are 18 pixels high, not 20 (`kRecordRows` in `item_sets.cpp`).
- **HUD textures** are 0x4400 lower; FUComplete's name plate is 8 pixels shorter (`hud_scale.hpp`).
- **Quest packs** in DATA.BIN are entries 5310–5320, 32 below Freedom Unite's 5342–5352, with the same key quests flagged.
- **DATA.BIN is stored plain** (no cipher): `Directory::parse(..., plain)` and `ArchiveView(..., plain)`.
- **GameSys option byte +0x6ADE0**: in FUComplete it is set, which makes the follow camera snap back to a vertical preset; the camera driver reads it as 0 while it owns the pitch (and then holds the gate negative so the behind placement stays off). Upstream reads the same byte as "placement off".

The scratch tools used for this (`mapfu.py`, `portsig.py`, `gensig.py`, `dataport.py`) were not committed; they are about 200 lines of Python over numpy. Rewrite them from the method above if needed; nothing in them is specific beyond the image paths.

## How CONFIG.BIN options run

FUComplete reads `ms0:/PSP/SAVEDATA/FUCDAT/CONFIG.BIN` at boot (64 bytes; options at 0x10–0x1B; field of view is a halfword at 0x14). Most options make the loader change code words that the corpus was not generated with:

- **Executable**: running from the file the corpus was generated from (`fuc::watch(..., compiled=true)`), the file is never rewritten. At each instruction-cache flush (`sceKernelIcacheInvalidate*` → `fuc::code_flushed`) and once a second for a minute, the words that differ from the file are found and every registered address within 0x4000 bytes of each is re-registered to the interpreter (`fuc::run_interpreted`). That span covers the whole 16 KiB generated unit around the word, because basic blocks inside a unit jump to each other directly. Re-registering also poisons the unit for compile-time direct chains. Feature hooks that wrap a function later wrap the interpreter and still work.
- **Overlays**: an image no library matches exactly is matched against each library's own code (`<library>.code`, written by `tools/overlay_code.py`, run by `install_user.sh`). The library of the same overlay name and base that differs least (≤ 1/64 of its words) is installed, and the units around the differing words are interpreted the same way.
- `MHFU_FUC_INTERPRET_CHANGES=0` turns both off. With the options File Replacer, True Raw, FoV Minor, horn tweak, Lao/Shen, full chest and drink buff on, one executable word (FoV, 0x08886DA4) and six `lobby_task` words differ.
- Options that patch code a feature hooks make that feature's signature fail, and the feature stays off. Guild Hall Full Chest does that to Pokee's Change Equipment, which is intended.

## Testing

- Two build directories: one per title (for example `out/futest` for FU, `out/fuccam` for FUC, with the FUC overlays built in `out/fuc`). Both suites must pass: `cmake --build <dir> --target psprecomp_test_binaries -j2 && ctest --test-dir <dir>`. Tests are title-aware; a test that only makes sense for Freedom Unite prints `skipped` under FUComplete.
- Scripted runs: `MHFU_DATA_DIR=<a test copy of the data folder> MHFU_OVERLAY_DIR=<build>/bin/overlays MHFU_INPUT_SCRIPT=... timeout 200 <build>/bin/Pokee`. In FUComplete the title needs `pad start`, then `pad b` (○) repeatedly; the default menu entry is Continue, and the save loads to the village. Give enough `pad b` presses: the "Press the ○ button" prompt comes late after a fresh boot. Never use the player's data folder or saves for tests.
- Each extra has a trace switch that says when its hook is in place (`MHFU_TRACE_HALL_MEAL`, `MHFU_TRACE_ITEM_SETS`, `MHFU_TRACE_HALL_EQUIPMENT`, `MHFU_TRACE_KEY_QUESTS`, ...). Check those lines rather than assuming.
- Scripted runs cannot easily reach quest givers, the Guild Hall or a quest; anything there needs the player.

## State

Verified by the player in FUComplete: analog camera and tilt, lock-on, hold to gather, gather icon, UI scaling, hide HUD, empty item slot, sharpness gauge, layered armor, KTX2 HD texture pack, the three Guild Hall features (Eat in the Guild Hall, Item Sets, Change Equipment), key quests' menu row.

Open:

1. **Key quest marks do not show in FUComplete.** The packs are entries 5310–5320 and the archive is now read plain, so reads are reported (`[key quests] entry N read` with `MHFU_TRACE_KEY_QUESTS=1`). Not yet seen: whether the game loads the pack unchanged into RAM when the quest list opens, as Freedom Unite does. At boot all eleven are read and then not found in RAM (probably an integrity scan, not the list's load). Next step: a traced session where the player opens the Village Chief's 2★ list; if the pack is not found then, look for its name strings in RAM to see how the game holds it.
2. **CONFIG.BIN options' effects** are not checked in the game (FoV, horn tweak, minimap, True Raw, Lao/Shen, full chest, drink buff, Dos audio). The mechanism is checked: the changed code runs interpreted, the rest compiled.
3. **Give Any Rarity Item** and **Change Equipment's sync to other hunters** need a second player.
4. **True Attack Value** is off in FUComplete (its True Raw does the same). Port it only if wanted.
5. **Upstream release flows** (`release_*.sh`, Flatpak) know nothing of FUComplete, and do not ship `.code` files. Only `install_user.sh` installs PokeeFUC.

## Keeping up with Pokee

1. The player presses **Sync fork** on the fork's `main` on GitHub. That is always safe: `main` stays identical to upstream.
2. Merge `origin/main` into `MHFU-to-FUComplete-port` locally. Never use the pull request's *Update branch* button, and never rebase or force-push the branch.
3. For each conflict, take upstream's version and re-apply the title switches, rather than the reverse. For each **new** extra that reads or hooks game addresses, add a flag in `title.hpp` set to `kFreedomUniteExtras`, gate its frame call, menu row and title-specific tests, then port it as above.
4. Build and test both titles, boot FUComplete to the village in a scripted run, commit the merge, push, reinstall with `install_user.sh`.
5. Other sessions push to the same branch: fetch and merge before pushing.

## Rules that matter here

- No game data, generated code, overlay corpora, saves, `.code` files, personal paths or machine names in commits. The `.code` files hold code from the player's own game, like the libraries.
- Commits are authored with the GitHub identity the repository is configured with (repo-local `user.name`/`user.email`); check before committing.
- Pull requests, issues and comments only on the fork, never on upstream, and only when the owner asks.
- FUCTool is GPL-3: take only file-format facts (offsets, values) from it, never its code or text.
