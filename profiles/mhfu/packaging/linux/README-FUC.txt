PokeeFUC for Linux (x86-64)
===========================

A native port of Monster Hunter Portable 2nd G patched with FUComplete.
PokeeFUC does not include any game assets or original game files: no disc
image, no copy of the game's executable or data, and no textures, models,
audio or video from the game. You must provide the files from your own
legally obtained copy of the game, patched with FUComplete (the image made by
FUCTool): its disc image. The first start checks that copy and prepares the
game from it.

Start it with

    ./pokee-fuc

The first start asks for your FUComplete-patched disc image, copies it into
PokeeFUC's data directory (~/.local/share/Pokee/FUC) and prepares the game
from it. After that the game starts straight away. Saves are kept in the same
directory, under ms0/PSP/SAVEDATA.

    ./pokee-fuc --install /path/to/image.iso   set up from a terminal instead
    ./pokee-fuc --help                         all options

Needs: an x86-64 Linux with glibc 2.29 or newer, a Vulkan driver (Mesa on
AMD and Intel, or NVIDIA's), and Wayland or X11. On a Steam Deck the Flatpak
is easier.

Project, release notes and the build guide:
    https://github.com/Oddity-git/PokeeFUC
    https://github.com/Oddity-git/PokeeFUC/blob/MHFU-to-FUComplete-port/docs/FUCOMPLETE_HANDOFF.md

Third-party software and its licenses: licenses/THIRD_PARTY_NOTICES.md
