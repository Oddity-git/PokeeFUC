#!/usr/bin/env bash
# Install a build for the current user, with an entry in the application menu.
#
#   install_user.sh [build_dir] [--overlays DIR] [--prefix DIR]
#   install_user.sh [build_dir] --uninstall
#
# Copies the executable, its libraries and the overlay libraries of a source
# build into a directory of its own (~/.local/share/pokee or
# ~/.local/share/pokee-fuc by default), adds a launcher that raises the stack
# limit as the release's does, and installs a .desktop entry and the icon under
# ~/.local/share, so the desktop lists the game like an installed app. Nothing
# needs root. The build's title (MHFU_TITLE, read from build_dir) decides the
# name: Pokee for Freedom Unite, PokeeFUC for FUComplete, each with its own
# app id, directory and entry, so both can be installed side by side. Run it
# again after a rebuild, or once more overlays are built, to update the copy.
#
# The game data stays where the game keeps it (~/.local/share/Pokee/MHFU or
# ~/.local/share/Pokee/FUC); this script does not touch it. The copy holds
# code recompiled from your own game, so keep it to yourself: never upload or
# share it.
#
#   --overlays DIR  take the overlay libraries from DIR instead of
#                   build_dir/bin/overlays
#   --prefix DIR    install into DIR instead of ~/.local/share/<name>
#   --uninstall     remove the copy, the launcher entry and the icon
set -euo pipefail

profile_dir="$(cd "$(dirname "$0")/../.." && pwd)"
repo_dir="$(cd "$profile_dir/../.." && pwd)"

build_dir=""
overlays=""
prefix=""
uninstall=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --overlays) overlays="${2:?--overlays needs a directory}"; shift 2 ;;
        --prefix) prefix="${2:?--prefix needs a directory}"; shift 2 ;;
        --uninstall) uninstall=1; shift ;;
        -h|--help) sed -n '2,/^set -euo/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) build_dir="$1"; shift ;;
    esac
done
build_dir="${build_dir:-$repo_dir/out/mhfu}"
build_dir="$(cd "$build_dir" && pwd)"
# shellcheck source=../../scripts/title_dirs.sh
source "$profile_dir/scripts/title_dirs.sh"

case "$title" in
    FU)
        app_id=io.github.teamgdb.Pokee
        name=Pokee
        command=pokee
        generic="Native port of Monster Hunter Freedom Unite (USA)"
        comment="Play Monster Hunter Freedom Unite (USA) natively from your own disc image"
        keywords="Monster Hunter;Freedom Unite;MHFU;PSP;recompilation;"
        ;;
    FUC)
        app_id=io.github.teamgdb.PokeeFUC
        name=PokeeFUC
        command=pokee-fuc
        generic="Native port of Monster Hunter Portable 2nd G with FUComplete"
        comment="Play FUComplete (Monster Hunter Portable 2nd G) natively from your own patched disc image"
        keywords="Monster Hunter;FUComplete;Portable 2nd G;MHP2G;PSP;recompilation;"
        ;;
esac

data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
prefix="${prefix:-$data_home/$command}"
desktop_file="$data_home/applications/$app_id.desktop"
icon_file="$data_home/icons/hicolor/scalable/apps/$app_id.svg"

refresh_menus() {
    command -v update-desktop-database > /dev/null &&
        update-desktop-database -q "$data_home/applications" 2> /dev/null || true
    command -v gtk-update-icon-cache > /dev/null &&
        gtk-update-icon-cache -q -t "$data_home/icons/hicolor" 2> /dev/null || true
}

if [[ $uninstall -eq 1 ]]; then
    # Only what this script installs: a directory with its launcher in it.
    if [[ -x "$prefix/$command" && -f "$prefix/Pokee" ]]; then
        rm -rf -- "$prefix"
    elif [[ -e "$prefix" ]]; then
        echo "error: $prefix is not a $name installed by this script; leaving it" >&2
        exit 1
    fi
    rm -f -- "$desktop_file" "$icon_file"
    refresh_menus
    echo "$name removed. Its game data and saves are where the game keeps them."
    exit 0
fi

executable="$build_dir/bin/Pokee"
[[ -x "$executable" ]] || { echo "error: no executable at $executable; build the Pokee target first" >&2; exit 1; }
overlays="${overlays:-$build_dir/bin/overlays}"

mkdir -p "$prefix"
# The libraries the build bundles (FFmpeg), found through the rpath $ORIGIN/lib.
cp -f "$executable" "$prefix/Pokee"
if [[ -d "$build_dir/bin/lib" ]]; then
    rm -rf -- "$prefix/lib"
    cp -R "$build_dir/bin/lib" "$prefix/lib"
fi
count=0
if [[ -d "$overlays" ]]; then
    mkdir -p "$prefix/overlays"
    # Replace the set, so a library from an older build is not left behind.
    find "$prefix/overlays" -mindepth 1 -maxdepth 1 -name '*.so' -delete
    while IFS= read -r -d '' library; do
        cp -f "$library" "$prefix/overlays/"
        count=$((count + 1))
    done < <(find "$overlays" -mindepth 1 -maxdepth 1 -name '*.so' -print0)
fi
cat > "$prefix/$command" << EOF
#!/bin/sh
# Starts $name, installed by install_user.sh. The recompiled code nests native
# calls deeply, and the main thread needs a 64 MiB stack.
here="\$(dirname "\$(readlink -f "\$0")")"
ulimit -s 65536 2>/dev/null || true
export MHFU_OVERLAY_DIR="\${MHFU_OVERLAY_DIR:-\$here/overlays}"
exec "\$here/Pokee" "\$@"
EOF
chmod 755 "$prefix/$command" "$prefix/Pokee"

mkdir -p "$(dirname "$desktop_file")" "$(dirname "$icon_file")"
cp -f "$repo_dir/docs/images/emblem.svg" "$icon_file"
cat > "$desktop_file" << EOF
[Desktop Entry]
Type=Application
Name=$name
GenericName=$generic
Comment=$comment
Exec="$prefix/$command"
Icon=$app_id
Terminal=false
Categories=Game;ActionGame;
Keywords=$keywords
StartupNotify=true
StartupWMClass=$app_id
EOF
command -v desktop-file-validate > /dev/null && desktop-file-validate "$desktop_file"
refresh_menus

echo "$name installed in $prefix, with $count overlay librar$([[ $count -eq 1 ]] && echo y || echo ies)."
echo "It is in the application menu as $name; from a terminal: $prefix/$command"
