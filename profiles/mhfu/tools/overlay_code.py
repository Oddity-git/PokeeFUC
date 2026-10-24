#!/usr/bin/env python3
"""Write the code each overlay library was generated from beside it.

    overlay_code.py <library_dir> <overlay_source_dir> [output_dir]

For every overlay library (ovl<base>_<name>_<hash> with the platform's
library extension) in <library_dir>, reads the code from its corpus folder in
<overlay_source_dir> (overlay.elf, as add_overlay.py wraps it) and writes it as
<library name>.code into <output_dir> (default: <library_dir>). The host
compares a loaded overlay that matches no library exactly with this code, so
an overlay FUComplete's loader or a mod has changed in a few words still runs
compiled, with only the code around those words interpreted (overlays.cpp).
The .code file holds code read from the player's own game: like the library,
it is never shared.
"""

import os
import struct
import sys

LIBRARY_EXTENSIONS = (".so", ".dylib", ".dll")
EHDR_SIZE = 52


def code_of(elf_path):
    with open(elf_path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"\x7fELF" or len(data) < EHDR_SIZE + 32:
        return None
    phoff = struct.unpack_from("<I", data, 28)[0]
    offset = struct.unpack_from("<I", data, phoff + 4)[0]
    size = struct.unpack_from("<I", data, phoff + 16)[0]
    if offset + size > len(data):
        return None
    return data[offset:offset + size]


def inside(root, *parts):
    """The real path of `parts` under `root`, or None when it leads elsewhere."""
    path = os.path.realpath(os.path.join(root, *parts))
    return path if path.startswith(root + os.sep) else None


def main(argv):
    if len(argv) not in (3, 4):
        sys.stderr.write(__doc__)
        return 2
    library_dir = os.path.realpath(argv[1])
    source_dir = os.path.realpath(argv[2])
    output_dir = os.path.realpath(argv[3]) if len(argv) == 4 else library_dir
    written = 0
    for entry in sorted(os.listdir(library_dir)):
        stem, extension = os.path.splitext(entry)
        if extension not in LIBRARY_EXTENSIONS or not stem.startswith("ovl"):
            continue
        # A corpus folder or output that a link leads out of its directory is skipped.
        elf_path = inside(source_dir, stem, "overlay.elf")
        out_path = inside(output_dir, stem + ".code")
        if elf_path is None or out_path is None or not os.path.isfile(elf_path):
            continue
        code = code_of(elf_path)
        if code is None:
            continue
        with open(out_path, "wb") as out:
            out.write(code)
        written += 1
    print(f"wrote {written} overlay code file(s)")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
