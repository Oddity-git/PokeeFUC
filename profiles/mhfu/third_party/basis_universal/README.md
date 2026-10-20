# Basis Universal transcoder

The files here are copies from [Basis Universal](https://github.com/BinomialLLC/basis_universal) release `v2_1_0`, commit `45d5f41015eecd9570d5a3f89ab9cc0037a25063`, under the Apache License 2.0 (see `LICENSE` and `NOTICE`). Copyright © 2016–2026 Binomial LLC. "Basis Universal" is a trademark of Binomial LLC.

Only the transcoder is kept: `transcoder/`, unmodified. The encoder, tools, examples, test files and web builds are left out.

`zstd/` holds the single-file Zstandard decompressor (`zstddeclib.c`, `zstd.h`, `zstd_errors.h`) of zstd 1.5.7 as the same Basis Universal commit ships it, unmodified, under the BSD license in `zstd/LICENSE` (Copyright (c) 2016-present, Facebook, Inc.). The compressor `zstd.c` is left out.

The port uses them in `host/gpu/ktx2_image.cpp` to read KTX2 images in texture packs made for PPSSPP: Basis Universal ETC1S and UASTC, with or without zstd supercompression, transcoded to RGBA.
