# Sourced by the scripts here: the directories of the game a build directory
# was configured for (MHFU_TITLE, see host/title.hpp). Needs profile_dir and
# build_dir; sets title, game_dir, analysis_dir, generated_dir and
# overlay_source_dir. Without a build directory, MHFU_TITLE from the
# environment decides, and FU is the default.
title="$(sed -n 's/^MHFU_TITLE:STRING=//p' "$build_dir/CMakeCache.txt" 2> /dev/null || true)"
title="$(printf '%s' "${title:-${MHFU_TITLE:-FU}}" | tr '[:lower:]' '[:upper:]')"
case "$title" in
    FU) title_suffix="" ;;
    FUC) title_suffix="_fuc" ;;
    *)
        echo "error: MHFU_TITLE must be FU or FUC, not $title" >&2
        exit 1
        ;;
esac
game_dir="$profile_dir/game$title_suffix"
analysis_dir="$profile_dir/analysis$title_suffix"
generated_dir="$profile_dir/generated$title_suffix"
overlay_source_dir="$profile_dir/overlays$title_suffix"
