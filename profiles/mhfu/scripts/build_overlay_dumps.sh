#!/usr/bin/env bash
# Build a library for every overlay dumped from memory.
#
#   build_overlay_dumps.sh [build_dir] [dump_dir] [jobs]
#
# The game dumps an overlay it has no library for into MHFU_DUMP_OVERLAYS,
# and the FUComplete build into overlay_dumps in its data folder by default:
# FUComplete's loader patches overlays such as game_task and lobby_task as they
# load, so they match no library built from DATA.BIN. Each dump is named
# overlay_<slot>_<name>.bin; this recompiles every one with add_overlay.py and
# builds its library beside the others. A library is named after the image's
# hash, so a patched overlay gets its own and the unpatched one stays. Play
# until the game prints no new "[overlay] dumped" line, then run this again.
set -euo pipefail

profile_dir="$(cd "$(dirname "$0")/.." && pwd)"
repo_dir="$(cd "$profile_dir/../.." && pwd)"
build_dir="${1:-$repo_dir/out/mhfu}"
# shellcheck source=title_dirs.sh
source "$profile_dir/scripts/title_dirs.sh"
case "$title" in
    FUC) default_dumps="${XDG_DATA_HOME:-$HOME/.local/share}/Pokee/FUC/overlay_dumps" ;;
    *) default_dumps="" ;;
esac
dump_dir="${2:-${MHFU_DUMP_OVERLAYS:-$default_dumps}}"
jobs="${3:-2}"
[[ -n "$dump_dir" && -d "$dump_dir" ]] || { echo "error: no dump directory (${dump_dir:-unset})" >&2; exit 1; }

built=0
failed=0
for dump in "$dump_dir"/overlay_*.bin; do
    [[ -e "$dump" ]] || continue
    # overlay_09A5A580.bin or overlay_09A5A580_game_task.bin: the slot is the base.
    stem="$(basename "$dump" .bin)"
    base="${stem#overlay_}"
    base="0x${base%%_*}"
    echo "=== $(basename "$dump") at $base"
    if python3 "$profile_dir/tools/add_overlay.py" -j "$jobs" "$build_dir" "$dump" "$base"; then
        built=$((built + 1))
    else
        failed=$((failed + 1))
    fi
done
echo "built $built, failed $failed, from $dump_dir"
[[ $failed -eq 0 ]]
