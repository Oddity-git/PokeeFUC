#!/bin/sh
# Starts PokeeFUC. The tarball has it as ./pokee-fuc next to Pokee; the
# Flatpak runs it as its command, from /app/lib/pokee-fuc.
here="$(dirname "$(readlink -f "$0")")"

# Recompiled code nests native calls deeply, and the main thread needs a
# 64 MiB stack. PokeeFUC raises the limit itself when it can; doing it
# here as well saves it a restart.
ulimit -s 65536 2>/dev/null || true

exec "$here/Pokee" "$@"
